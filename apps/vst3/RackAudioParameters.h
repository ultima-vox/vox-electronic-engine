#pragma once
#include "rack/AudioRouting.h"
#include <JuceHeader.h>
#include <cmath>
#include <cstdio>
namespace vstengine::host {
inline std::string sendParameterId(std::size_t slot, std::size_t bus,
                                   const char *field) {
  char id[80]{};
  std::snprintf(id, sizeof(id), "slot%02zu.send.%zu.%s", slot + 1, bus + 1,
                field);
  return id;
}
inline std::string returnParameterId(std::size_t bus, const char *field) {
  return "rack.return." + std::to_string(bus + 1) + "." + field;
}
inline void appendRackAudioParameters(
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> &parameters) {
  auto number = [&](const std::string &id, const juce::String &name, float lo,
                    float hi, float step, float initial,
                    const juce::String &unit = {}) {
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{id, 1}, name,
        juce::NormalisableRange<float>{lo, hi, step}, initial, unit));
  };
  for (std::size_t s = 0; s < instrument::maxSlots; ++s) {
    for (std::size_t b = 0; b < rack::sendBusCount; ++b) {
      const auto name = "Slot " + juce::String(static_cast<int>(s + 1)) +
                        " Send " + juce::String(static_cast<int>(b + 1));
      number(sendParameterId(s, b, "level"), name + " Level", 0, 1, 0.0001f, 0);
      parameters.push_back(std::make_unique<juce::AudioParameterBool>(
          juce::ParameterID{sendParameterId(s, b, "pre"), 1},
          name + " Pre-fader", false));
    }
    number("slot" +
               juce::String(static_cast<int>(s + 1))
                   .paddedLeft('0', 2)
                   .toStdString() +
               ".duck.depth",
           "Slot " + juce::String(static_cast<int>(s + 1)) + " Duck Depth", 0,
           1, 0.0001f, 0);
  }
  const juce::StringArray types{"Distortion", "Wavefolder", "Phaser", "Flanger",
                                "Chorus",     "Bitcrusher", "Delay",  "Reverb"};
  constexpr int initialTypes[]{7, 6, 4, 2};
  for (std::size_t b = 0; b < rack::sendBusCount; ++b) {
    const auto name = "Return " + juce::String(static_cast<int>(b + 1));
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{returnParameterId(b, "type"), 1}, name + " Effect",
        types, initialTypes[b]));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{returnParameterId(b, "enabled"), 1},
        name + " Enabled", true));
    number(returnParameterId(b, "level"), name + " Level", 0, 2, 0.0001f, 0.5f);
    number(returnParameterId(b, "pan"), name + " Pan", -1, 1, 0.0001f, 0);
    number(returnParameterId(b, "amount"), name + " Amount", 0, 1, 0.0001f,
           0.5f);
    number(returnParameterId(b, "rate"), name + " Rate", 0, 1, 0.0001f, 0.5f);
    number(returnParameterId(b, "character"), name + " Character", 0, 1,
           0.0001f, 0.5f);
  }
  juce::StringArray sources{"Off"};
  for (int i = 1; i <= 16; ++i)
    sources.add("Slot " + juce::String(i));
  parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"rack.duck.source", 1}, "Ducking Kick Source", sources,
      0));
  number("rack.duck.note", "Ducking Kick Note", 0, 127, 1, 36, "MIDI note");
  number("rack.duck.attack", "Ducking Attack", 0, 100, 0.1f, 1, "ms");
  number("rack.duck.hold", "Ducking Hold", 0, 500, 0.1f, 40, "ms");
  number("rack.duck.release", "Ducking Release", 1, 2000, 0.1f, 120, "ms");
}
class RackAudioParameterRefs final {
  using Ref = std::atomic<float> *;
  struct Send {
    Ref gain{}, pre{};
  };
  struct Return {
    Ref type{}, enabled{}, level{}, pan{}, amount{}, rate{}, character{};
  };
  std::array<std::array<Send, rack::sendBusCount>, instrument::maxSlots>
      sends_{};
  std::array<Ref, instrument::maxSlots> depth_{};
  std::array<Return, rack::sendBusCount> returns_{};
  Ref source_{}, note_{}, attack_{}, hold_{}, release_{};

public:
  void cache(juce::AudioProcessorValueTreeState &state) {
    auto get = [&](const std::string &id) {
      return state.getRawParameterValue(id);
    };
    for (std::size_t s = 0; s < sends_.size(); ++s) {
      for (std::size_t b = 0; b < rack::sendBusCount; ++b)
        sends_[s][b] = {get(sendParameterId(s, b, "level")),
                        get(sendParameterId(s, b, "pre"))};
      depth_[s] = get("slot" +
                      juce::String(static_cast<int>(s + 1))
                          .paddedLeft('0', 2)
                          .toStdString() +
                      ".duck.depth");
    }
    for (std::size_t b = 0; b < rack::sendBusCount; ++b)
      returns_[b] = {get(returnParameterId(b, "type")),
                     get(returnParameterId(b, "enabled")),
                     get(returnParameterId(b, "level")),
                     get(returnParameterId(b, "pan")),
                     get(returnParameterId(b, "amount")),
                     get(returnParameterId(b, "rate")),
                     get(returnParameterId(b, "character"))};
    source_ = get("rack.duck.source");
    note_ = get("rack.duck.note");
    attack_ = get("rack.duck.attack");
    hold_ = get("rack.duck.hold");
    release_ = get("rack.duck.release");
  }
  [[nodiscard]] rack::AudioRoutingControls
  read(std::span<const rack::ProcessSlotControls> slots) const noexcept {
    const auto value = [](Ref p) {
      const auto v = p->load(std::memory_order_relaxed);
      return std::isfinite(v) ? v : 0.0f;
    };
    rack::AudioRoutingControls result;
    for (std::size_t s = 0; s < sends_.size(); ++s) {
      for (std::size_t b = 0; b < rack::sendBusCount; ++b)
        result.sends[s][b] = {value(sends_[s][b].gain),
                              value(sends_[s][b].pre) >= 0.5f};
      result.duckDepth[s] = value(depth_[s]);
    }
    for (std::size_t b = 0; b < rack::sendBusCount; ++b) {
      const auto &r = returns_[b];
      result.returns[b] = {static_cast<effects::EffectType>(static_cast<int>(
                               std::clamp(value(r.type), 0.0f, 7.0f))),
                           value(r.enabled) >= 0.5f,
                           value(r.level),
                           value(r.pan),
                           value(r.amount),
                           value(r.rate),
                           value(r.character)};
    }
    const auto source =
        static_cast<std::size_t>(std::clamp(value(source_), 0.0f, 16.0f));
    if (source > 0 && source <= slots.size())
      result.kickSource = slots[source - 1].slotId;
    result.kickNote =
        static_cast<std::uint8_t>(std::clamp(value(note_), 0.0f, 127.0f));
    result.attackMs = value(attack_);
    result.holdMs = value(hold_);
    result.releaseMs = value(release_);
    return result;
  }
};
} // namespace vstengine::host
