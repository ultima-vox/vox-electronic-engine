#pragma once
#include "instrument/HostParameterSchema.h"
#include "rack/RackTypes.h"
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <unordered_set>
namespace vstengine::host {
// A compiled catalog fixes all host endpoints at processor construction. A data
// pack may add content, never change the parameter layout or execute DSP code.
inline void appendDirectParameters(
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> &layout,
    std::span<const instrument::InstrumentDescriptor *const> catalog) {
  std::string diagnostic;
  if (!instrument::hostparams::validateDirectCatalog(catalog, diagnostic))
    throw std::invalid_argument(diagnostic);
  for (const auto *d : catalog)
    if (d && d->directParameters)
      for (const auto &p : d->parameters)
        for (std::size_t slot = 0; slot < instrument::maxSlots; ++slot) {
          const juce::ParameterID id{
              instrument::hostparams::directId(slot, d->id, p.id), 1};
          const auto name = "Slot " + juce::String(static_cast<int>(slot + 1)) +
                            " " + juce::String(d->name) + " " +
                            juce::String(p.name);
          const auto initial =
              instrument::hostparams::canonicalPlain(p.defaultValue, p);
          switch (p.type) {
          case instrument::ParameterType::boolean:
            layout.push_back(std::make_unique<juce::AudioParameterBool>(
                id, name, initial != 0));
            break;
          case instrument::ParameterType::choice: {
            juce::StringArray choices;
            for (const auto &v : p.choices)
              choices.add(v);
            layout.push_back(std::make_unique<juce::AudioParameterChoice>(
                id, name, choices, static_cast<int>(initial)));
            break;
          }
          case instrument::ParameterType::integer:
            layout.push_back(std::make_unique<juce::AudioParameterInt>(
                id, name, static_cast<int>(p.minimum),
                static_cast<int>(p.maximum), static_cast<int>(initial),
                p.unit));
            break;
          case instrument::ParameterType::floating:
            layout.push_back(std::make_unique<juce::AudioParameterFloat>(
                id, name,
                juce::NormalisableRange<float>{p.minimum, p.maximum, p.step},
                initial, p.unit));
            break;
          }
        }
  // JUCE's modern VST3 wrapper hashes stable string IDs. Reject both literal
  // and 32-bit hash collisions across the ENTIRE layout, including Rack IDs.
  std::unordered_set<std::string> ids;
  std::unordered_set<std::uint32_t> vstIds;
  for (const auto &p : layout) {
    const auto id = p->getParameterID();
    if (!ids.insert(id.toStdString()).second ||
        !vstIds
             .insert(
                 juce::VST3ClientExtensions::convertJuceParameterId(id, true))
             .second)
      throw std::invalid_argument("host parameter identity/hash collision");
  }
}
class DirectParameterBank final {
  struct Entry {
    std::uint64_t key{};
    juce::RangedAudioParameter *parameter{};
    std::atomic<float> *raw{};
    const instrument::ParameterDescriptor *descriptor{};
  };
  struct Bank {
    std::string instrumentId;
    std::array<Entry, instrument::maxDirectParameters> entries{};
    std::size_t count{};
  };
  std::array<std::vector<Bank>, instrument::maxSlots> banks_;
  std::array<std::atomic<std::size_t>, instrument::maxSlots> active_{};
  bool cached_{};
  static constexpr auto none = std::numeric_limits<std::size_t>::max();

public:
  DirectParameterBank() {
    for (auto &active : active_)
      active.store(none);
  }
  void cache(juce::AudioProcessorValueTreeState &state,
             std::span<const instrument::InstrumentDescriptor *const> catalog) {
    if (cached_)
      throw std::logic_error("direct bank cache is immutable");
    for (std::size_t slot = 0; slot < instrument::maxSlots; ++slot) {
      for (const auto *d : catalog)
        if (d && d->directParameters) {
          Bank bank;
          bank.instrumentId = d->id;
          for (const auto &p : d->parameters) {
            if (bank.count >= bank.entries.size())
              throw std::invalid_argument("direct bank overflow");
            const auto id = instrument::hostparams::directId(slot, d->id, p.id);
            auto *raw = state.getRawParameterValue(id);
            auto *parameter = state.getParameter(id);
            if (!raw || !parameter)
              throw std::invalid_argument("direct host binding is missing");
            bank.entries[bank.count++] = {
                instrument::hostparams::directKey(d->id, p.id), parameter, raw,
                &p};
          }
          banks_[slot].push_back(std::move(bank));
        }
    }
    cached_ = true;
  }
  bool
  canSelectAndRestore(std::size_t slot,
                      const rack::PersistentSlotState &state) const noexcept {
    if (slot >= instrument::maxSlots)
      return false;
    for (const auto &bank : banks_[slot])
      if (bank.instrumentId == state.instrumentId) {
        if (bank.count != state.directParameters.size())
          return false;
        std::array<std::uint64_t, instrument::maxDirectParameters> keys{};
        std::size_t count = 0;
        for (const auto &value : state.directParameters) {
          const auto key =
              instrument::hostparams::directKey(state.instrumentId, value.id);
          const auto end =
              bank.entries.begin() + static_cast<std::ptrdiff_t>(bank.count);
          const auto it =
              std::find_if(bank.entries.begin(), end,
                           [key](const auto &e) { return e.key == key; });
          if (it == end || !std::isfinite(value.value) ||
              value.value < it->descriptor->minimum ||
              value.value > it->descriptor->maximum)
            return false;
          for (std::size_t i = 0; i < count; ++i)
            if (keys[i] == key)
              return false;
          keys[count++] = key;
        }
        return true;
      }
    return state.directParameters.empty();
  }
  // Controller only; caller has suspended processing for a transactional
  // module/preset/project change. Other slots' live values are never replayed.
  bool selectAndRestore(std::size_t slot,
                        const rack::PersistentSlotState &state) {
    if (slot >= instrument::maxSlots)
      return false;
    for (std::size_t b = 0; b < banks_[slot].size(); ++b) {
      auto &bank = banks_[slot][b];
      if (bank.instrumentId != state.instrumentId)
        continue;
      if (bank.count != state.directParameters.size())
        return false;
      std::array<Entry *, instrument::maxDirectParameters> prepared{};
      std::size_t index = 0;
      for (const auto &value : state.directParameters) {
        const auto key =
            instrument::hostparams::directKey(state.instrumentId, value.id);
        auto it = std::find_if(bank.entries.begin(),
                               bank.entries.begin() +
                                   static_cast<std::ptrdiff_t>(bank.count),
                               [key](const auto &e) { return e.key == key; });
        if (it == bank.entries.begin() +
                      static_cast<std::ptrdiff_t>(bank.count) ||
            !std::isfinite(value.value) ||
            value.value < it->descriptor->minimum ||
            value.value > it->descriptor->maximum)
          return false;
        for (std::size_t prior = 0; prior < index; ++prior)
          if (prepared[prior] == &*it)
            return false;
        prepared[index++] = &*it;
      }
      for (std::size_t i = 0; i < index; ++i) {
        auto *p = prepared[i]->parameter;
        p->setValueNotifyingHost(
            p->convertTo0to1(state.directParameters[i].value));
      }
      active_[slot].store(b, std::memory_order_release);
      return true;
    }
    if (!state.directParameters.empty())
      return false;
    active_[slot].store(none, std::memory_order_release);
    return true;
  }
  void deactivate(std::size_t slot) noexcept {
    if (slot < instrument::maxSlots)
      active_[slot].store(none, std::memory_order_release);
  }
  void read(std::size_t slot,
            rack::ProcessSlotControls &controls) const noexcept {
    controls.directParameterCount = 0;
    if (slot >= instrument::maxSlots)
      return;
    const auto active = active_[slot].load(std::memory_order_acquire);
    if (active >= banks_[slot].size())
      return;
    const auto &bank = banks_[slot][active];
    controls.directParameterCount = bank.count;
    for (std::size_t p = 0; p < bank.count; ++p) {
      const auto &e = bank.entries[p];
      const auto value = e.raw->load(std::memory_order_relaxed);
      controls.directParameters[p] = {
          e.key, std::isfinite(value) ? instrument::hostparams::canonicalPlain(
                                            value, *e.descriptor)
                                      : value};
    }
  }
};
} // namespace vstengine::host
