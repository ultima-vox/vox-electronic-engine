#include "rack/AudioRouting.h"
#include <algorithm>
#include <cmath>
namespace vstengine::rack {
namespace {
bool range(float v, float lo, float hi) noexcept {
  return std::isfinite(v) && v >= lo && v <= hi;
}
} // namespace
bool AudioRouting::valid(const AudioRoutingControls &c) noexcept {
  if (c.kickNote > 127 || !range(c.attackMs, 0, 100) ||
      !range(c.holdMs, 0, 500) || !range(c.releaseMs, 1, 2000))
    return false;
  for (const auto &slot : c.sends)
    for (const auto &send : slot)
      if (!range(send.gain, 0, 1))
        return false;
  for (auto depth : c.duckDepth)
    if (!range(depth, 0, 1))
      return false;
  for (const auto &r : c.returns)
    if (static_cast<unsigned>(r.type) > 7 || !range(r.level, 0, 2) ||
        !range(r.pan, -1, 1) || !range(r.amount, 0, 1) ||
        !range(r.rate, 0, 1) || !range(r.character, 0, 1))
      return false;
  return true;
}
bool AudioRouting::prepare(const instrument::PrepareSpec &spec) {
  prepared_ = false;
  blockReady_ = false;
  if (!std::isfinite(spec.sampleRate) || spec.sampleRate < 8000 ||
      spec.sampleRate > 384000 || spec.maximumBlockSize == 0 ||
      spec.maximumBlockSize > 65536 || spec.outputChannels < 1 ||
      spec.outputChannels > 2)
    return false;
  try {
    for (auto &r : returns_) {
      if (!r.chain.prepare(spec.sampleRate, spec.maximumBlockSize,
                           spec.outputChannels))
        return false;
      for (std::size_t c = 0; c < spec.outputChannels; ++c) {
        r.audio[c].assign(spec.maximumBlockSize, 0);
        r.pointers[c] = r.audio[c].data();
      }
    }
    envelopeBuffer_.assign(spec.maximumBlockSize, 0);
  } catch (...) {
    spec_ = {};
    return false;
  }
  spec_ = spec;
  reset();
  prepared_ = true;
  return true;
}
void AudioRouting::reset() noexcept {
  blockReady_ = false;
  for (auto &r : returns_) {
    r.chain.reset();
    r.wasEnabled = false;
  }
  phase_ = Phase::idle;
  envelope_ = 0;
  phasePosition_ = phaseLength_ = 0;
  previousSource_ = 0;
}
bool AudioRouting::begin(const AudioRoutingControls &controls,
                         std::span<const ProcessSlotControls> slots,
                         std::span<const RackRouter::DestinationBuffer> events,
                         std::uint32_t count) noexcept {
  blockReady_ = false;
  if (!prepared_ || count == 0 || count > spec_.maximumBlockSize ||
      !valid(controls) || slots.size() > instrument::maxSlots ||
      events.size() != slots.size())
    return false;
  for (const auto &buffer : events)
    if (buffer.size > RackRouter::eventCapacity)
      return false;
  blockReady_ = true;
  blockSamples_ = count;
  controls_ = controls;
  for (auto &r : returns_)
    for (std::size_t c = 0; c < spec_.outputChannels; ++c)
      std::fill_n(r.audio[c].data(), count, 0.0f);
  std::span<const VoxMidiEventV1> triggers;
  const bool solo = std::any_of(slots.begin(), slots.end(),
                                [](const auto &s) { return s.solo; });
  for (std::size_t i = 0; i < slots.size(); ++i)
    if (slots[i].slotId == controls.kickSource && slots[i].enabled &&
        !slots[i].mute && (!solo || slots[i].solo))
      triggers = events[i].view();
  if (previousSource_ != controls.kickSource ||
      previousNote_ != controls.kickNote || triggers.data() == nullptr) {
    phase_ = Phase::idle;
    envelope_ = 0;
  }
  previousSource_ = controls.kickSource;
  previousNote_ = controls.kickNote;
  const auto duration = [this](float ms) {
    return static_cast<std::uint32_t>(
        std::llround(ms * 0.001 * spec_.sampleRate));
  };
  std::size_t event = 0;
  for (std::uint32_t s = 0; s < count; ++s) {
    while (event < triggers.size() && triggers[event].sampleOffset <= s) {
      const auto &e = triggers[event++];
      if (e.sampleOffset == s && e.size >= 3 && (e.data[0] & 0xf0) == 0x90 &&
          e.data[1] == controls.kickNote && e.data[2] > 0) {
        attackStart_ = envelope_;
        phasePosition_ = 0;
        phaseLength_ = duration(controls.attackMs);
        phase_ = Phase::attack;
      }
    }
    if (phase_ == Phase::attack) {
      envelope_ = phaseLength_ ? attackStart_ +
                                     (1 - attackStart_) *
                                         static_cast<float>(++phasePosition_) /
                                         phaseLength_
                               : 1.0f;
      if (phasePosition_ >= phaseLength_) {
        envelope_ = 1;
        phasePosition_ = 0;
        phaseLength_ = duration(controls.holdMs);
        if (phaseLength_ == 0) {
          phase_ = Phase::release;
          phaseLength_ = std::max(1u, duration(controls.releaseMs));
        } else
          phase_ = Phase::hold;
      }
    } else if (phase_ == Phase::hold) {
      envelope_ = 1;
      if (++phasePosition_ >= phaseLength_) {
        phase_ = Phase::release;
        phasePosition_ = 0;
        phaseLength_ = std::max(1u, duration(controls.releaseMs));
      }
    } else if (phase_ == Phase::release) {
      envelope_ = 1 - static_cast<float>(++phasePosition_) / phaseLength_;
      if (phasePosition_ >= phaseLength_) {
        envelope_ = 0;
        phase_ = Phase::idle;
      }
    }
    envelopeBuffer_[s] = envelope_;
  }
  return true;
}
void AudioRouting::mixSlot(std::size_t index, std::span<float *> input,
                           std::span<float *> output,
                           const ProcessSlotControls &slot, bool audible,
                           std::uint32_t count) noexcept {
  if (!prepared_ || !blockReady_ || count != blockSamples_ ||
      index >= instrument::maxSlots || !audible ||
      input.size() != spec_.outputChannels ||
      output.size() != spec_.outputChannels || !range(slot.level, 0, 2) ||
      !range(slot.pan, -1, 1))
    return;
  for (std::size_t c = 0; c < input.size(); ++c)
    if (!input[c] || !output[c])
      return;
  const auto left = slot.pan <= 0 ? 1.0f : 1.0f - slot.pan,
             right = slot.pan >= 0 ? 1.0f : 1.0f + slot.pan;
  const bool source = slot.slotId == controls_.kickSource;
  for (std::uint32_t s = 0; s < count; ++s) {
    const auto duck =
        source ? 1.0f : 1 - controls_.duckDepth[index] * envelopeBuffer_[s];
    for (std::size_t c = 0; c < output.size(); ++c) {
      const auto pre = input[c][s] * duck;
      const auto post = pre * slot.level * (c == 0 ? left : right);
      output[c][s] += post;
      for (std::size_t bus = 0; bus < sendBusCount; ++bus) {
        const auto &send = controls_.sends[index][bus];
        returns_[bus].audio[c][s] += (send.preFader ? pre : post) * send.gain;
      }
    }
  }
}
void AudioRouting::finish(std::span<float *> outputs,
                          std::uint32_t count) noexcept {
  if (!prepared_ || !blockReady_ || count != blockSamples_ ||
      outputs.size() != spec_.outputChannels)
    return;
  for (auto *output : outputs)
    if (!output)
      return;
  blockReady_ = false;
  for (std::size_t i = 0; i < sendBusCount; ++i) {
    auto &r = returns_[i];
    const auto &c = controls_.returns[i];
    if (c.type != r.previousType) {
      r.chain.reset();
      r.previousType = c.type;
    }
    for (std::size_t e = 0; e < effects::EffectsChain::effectCount; ++e)
      (void)r.chain.setEffect(
          e, {static_cast<effects::EffectType>(e),
              c.enabled && static_cast<std::size_t>(c.type) == e, 1.0f,
              c.amount, c.rate, c.character});
    // Disabled returns are silent and cleared: bypass cannot create a doubled
    // dry path.
    if (!c.enabled) {
      if (r.wasEnabled)
        r.chain.reset();
      r.wasEnabled = false;
      continue;
    }
    r.wasEnabled = true;
    r.chain.process({r.pointers.data(), outputs.size()}, count);
    for (std::size_t channel = 0; channel < outputs.size(); ++channel) {
      const auto gain = c.level * (channel == 0 ? (c.pan <= 0 ? 1 : 1 - c.pan)
                                                : (c.pan >= 0 ? 1 : 1 + c.pan));
      for (std::uint32_t s = 0; s < count; ++s)
        outputs[channel][s] += r.audio[channel][s] * gain;
    }
  }
}
std::uint32_t AudioRouting::tailSamples() const noexcept {
  std::uint32_t result = 0;
  for (const auto &r : returns_)
    result = std::max(result, r.chain.tailSamples());
  return result;
}
} // namespace vstengine::rack
