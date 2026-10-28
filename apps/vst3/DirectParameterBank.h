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
// Fixed host pool: 16 slots x 64 generic endpoints ("slotNN.pKKK"), created
// once at processor construction. Installing or loading an instrument never
// changes the host parameter list; it only rebinds a slot's pool endpoints to
// the instrument's named parameters in descriptor order (Kontakt model).
//
// A bound endpoint takes the instrument parameter's range, name, unit and value
// text. APVTS raw values therefore stay in the instrument's plain units.
class PoolParameter final : public juce::RangedAudioParameter {
public:
  // Immutable per (slot, instrument, parameter); owned by DirectParameterBank
  // for the processor lifetime, so readers may hold the pointer lock-free.
  struct Binding {
    juce::NormalisableRange<float> range;
    const instrument::ParameterDescriptor *descriptor{};
    juce::String name;
  };

  PoolParameter(std::size_t slot, std::size_t index)
      : juce::RangedAudioParameter(
            juce::ParameterID{juce::String(instrument::hostparams::poolId(slot, index)), 1},
            "Slot " + juce::String(static_cast<int>(slot + 1)) + " Param " +
                juce::String(static_cast<int>(index + 1)).paddedLeft('0', 2)) {}

  // Controller thread only; the caller then rewrites the value and notifies
  // the host that parameter info changed.
  void bind(const Binding *binding) noexcept {
    binding_.store(binding, std::memory_order_release);
  }
  [[nodiscard]] const Binding *binding() const noexcept {
    return binding_.load(std::memory_order_acquire);
  }

  float getValue() const override {
    return value_.load(std::memory_order_relaxed);
  }
  void setValue(float newValue) override {
    value_.store(juce::jlimit(0.0f, 1.0f, newValue), std::memory_order_relaxed);
  }
  float getDefaultValue() const override {
    const auto *b = binding();
    return b != nullptr ? b->range.convertTo0to1(b->descriptor->defaultValue)
                        : 0.0f;
  }
  juce::String getName(int maximumLength) const override {
    const auto *b = binding();
    return (b != nullptr ? b->name : name).substring(0, maximumLength);
  }
  juce::String getLabel() const override {
    const auto *b = binding();
    return b != nullptr ? juce::String(b->descriptor->unit) : juce::String();
  }
  juce::String getText(float normalised, int maximumLength) const override {
    const auto *b = binding();
    if (b == nullptr)
      return juce::String(normalised, 3).substring(0, maximumLength);
    const auto &d = *b->descriptor;
    const auto plain = instrument::hostparams::canonicalPlain(
        b->range.convertFrom0to1(normalised), d);
    juce::String text;
    switch (d.type) {
    case instrument::ParameterType::boolean:
      text = plain >= 0.5f ? "On" : "Off";
      break;
    case instrument::ParameterType::choice:
      text = d.choices[static_cast<std::size_t>(plain)];
      break;
    case instrument::ParameterType::integer:
      text = juce::String(static_cast<int>(plain));
      break;
    case instrument::ParameterType::floating:
      text = d.step >= 1.0f ? juce::String(juce::roundToInt(plain))
                            : juce::String(plain, 2);
      break;
    }
    return text.substring(0, maximumLength);
  }
  float getValueForText(const juce::String &text) const override {
    const auto *b = binding();
    if (b == nullptr)
      return juce::jlimit(0.0f, 1.0f, text.getFloatValue());
    const auto &d = *b->descriptor;
    float plain = text.getFloatValue();
    if (d.type == instrument::ParameterType::boolean)
      plain = text.trim().equalsIgnoreCase("on") || plain >= 0.5f ? 1.0f : 0.0f;
    if (d.type == instrument::ParameterType::choice)
      for (std::size_t i = 0; i < d.choices.size(); ++i)
        if (text.trim().equalsIgnoreCase(juce::String(d.choices[i])))
          plain = static_cast<float>(i);
    return b->range.convertTo0to1(instrument::hostparams::canonicalPlain(plain, d));
  }
  const juce::NormalisableRange<float> &getNormalisableRange() const override {
    static const juce::NormalisableRange<float> unbound{0.0f, 1.0f};
    const auto *b = binding();
    return b != nullptr ? b->range : unbound;
  }

private:
  std::atomic<float> value_{0.0f};
  std::atomic<const Binding *> binding_{nullptr};
};

// Appends the fixed pool. The catalog is validated against the pool bound; the
// layout itself never depends on it.
inline void appendPoolParameters(
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> &layout,
    std::span<const instrument::InstrumentDescriptor *const> catalog) {
  std::string diagnostic;
  if (!instrument::hostparams::validateDirectCatalog(catalog, diagnostic))
    throw std::invalid_argument(diagnostic);
  for (std::size_t slot = 0; slot < instrument::maxSlots; ++slot)
    for (std::size_t index = 0; index < instrument::poolParametersPerSlot; ++index)
      layout.push_back(std::make_unique<PoolParameter>(slot, index));
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
    PoolParameter *parameter{};
    std::atomic<float> *raw{};
    const instrument::ParameterDescriptor *descriptor{};
    PoolParameter::Binding binding;
  };
  struct Bank {
    std::string instrumentId;
    std::array<Entry, instrument::maxDirectParameters> entries{};
    std::size_t count{};
  };
  struct Pool {
    std::array<PoolParameter *, instrument::poolParametersPerSlot> parameters{};
  };
  std::array<std::vector<Bank>, instrument::maxSlots> banks_;
  std::array<Pool, instrument::maxSlots> pools_{};
  std::array<std::atomic<std::size_t>, instrument::maxSlots> active_{};
  juce::AudioProcessor *owner_{};
  bool cached_{};
  static constexpr auto none = std::numeric_limits<std::size_t>::max();

  // Rebinds a slot's pool to `bank` (nullptr = unbound) and tells the host
  // that names/ranges changed. Unbound endpoints rest at a neutral 0, so a
  // dormant slot never exposes another module's value. Controller thread only.
  void bindPool(std::size_t slot, const Bank *bank) {
    auto &pool = pools_[slot];
    bool changed = false;
    for (std::size_t i = 0; i < pool.parameters.size(); ++i) {
      auto *parameter = pool.parameters[i];
      const auto *binding = bank != nullptr && i < bank->count
                                ? &bank->entries[i].binding
                                : nullptr;
      changed |= parameter->binding() != binding;
      parameter->bind(binding);
      if (binding == nullptr && parameter->getValue() != 0.0f)
        parameter->setValueNotifyingHost(0.0f);
    }
    if (changed && owner_ != nullptr)
      owner_->updateHostDisplay(juce::AudioProcessorListener::ChangeDetails{}
                                    .withParameterInfoChanged(true));
  }

public:
  DirectParameterBank() {
    for (auto &active : active_)
      active.store(none);
  }
  void cache(juce::AudioProcessorValueTreeState &state,
             std::span<const instrument::InstrumentDescriptor *const> catalog) {
    if (cached_)
      throw std::logic_error("direct bank cache is immutable");
    owner_ = &state.processor;
    for (std::size_t slot = 0; slot < instrument::maxSlots; ++slot) {
      for (std::size_t i = 0; i < instrument::poolParametersPerSlot; ++i) {
        auto *parameter = dynamic_cast<PoolParameter *>(
            state.getParameter(instrument::hostparams::poolId(slot, i)));
        if (parameter == nullptr)
          throw std::invalid_argument("pool host binding is missing");
        pools_[slot].parameters[i] = parameter;
      }
      for (const auto *d : catalog)
        if (d && d->directParameters) {
          Bank bank;
          bank.instrumentId = d->id;
          for (const auto &p : d->parameters) {
            if (bank.count >= bank.entries.size())
              throw std::invalid_argument("direct bank overflow");
            const auto id = instrument::hostparams::poolId(slot, bank.count);
            auto &entry = bank.entries[bank.count];
            entry.key = instrument::hostparams::directKey(d->id, p.id);
            entry.parameter = pools_[slot].parameters[bank.count];
            entry.raw = state.getRawParameterValue(id);
            entry.descriptor = &p;
            entry.binding = {{p.minimum, p.maximum,
                              p.type == instrument::ParameterType::floating
                                  ? p.step
                                  : 1.0f},
                             &p,
                             "Slot " + juce::String(static_cast<int>(slot + 1)) +
                                 " " + juce::String(p.name)};
            if (entry.raw == nullptr)
              throw std::invalid_argument("pool host binding is missing");
            ++bank.count;
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
      // Bind first: values are written in the bound parameter's plain units.
      bindPool(slot, &bank);
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
    bindPool(slot, nullptr);
    return true;
  }
  void deactivate(std::size_t slot) {
    if (slot >= instrument::maxSlots)
      return;
    active_[slot].store(none, std::memory_order_release);
    bindPool(slot, nullptr);
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
