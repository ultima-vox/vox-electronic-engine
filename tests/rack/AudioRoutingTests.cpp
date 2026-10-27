#include "rack/AudioRouting.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace vstengine;
using namespace vstengine::rack;
constexpr std::uint32_t frames = 64;
constexpr double sampleRate = 8000.0;
int checks{};
#define CHECK(condition, message)                                              \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition))                                                          \
      throw std::runtime_error(std::string(message) + " (line " +              \
                               std::to_string(__LINE__) + ")");                \
  } while (false)

struct Audio {
  std::array<std::vector<float>, 2> data;
  std::array<float *, 2> pointers;
  explicit Audio(float fill = 0.0f) {
    for (std::size_t c = 0; c < 2; ++c) {
      data[c].assign(frames + 2, fill);
      data[c].front() = data[c].back() = 123.0f;
      pointers[c] = data[c].data() + 1;
    }
  }
  float &at(std::size_t c, std::size_t s) { return data[c][s + 1]; }
  float at(std::size_t c, std::size_t s) const { return data[c][s + 1]; }
  double energy(std::size_t c) const {
    double result = 0;
    for (std::size_t s = 0; s < frames; ++s)
      result += std::abs(at(c, s));
    return result;
  }
  void guards() const {
    for (const auto &c : data)
      CHECK(c.front() == 123.0f && c.back() == 123.0f,
            "routing respects sample-buffer bounds");
  }
};

bool close(float a, float b) { return std::abs(a - b) < 0.00001f; }
void constant(const Audio &audio, float left, float right) {
  for (std::size_t s = 0; s < frames; ++s)
    CHECK(close(audio.at(0, s), left) && close(audio.at(1, s), right),
          "expected unmodified constant/dry output");
  audio.guards();
}

struct Fixture {
  AudioRouting routing;
  AudioRoutingControls controls;
  std::array<ProcessSlotControls, 2> slots{};
  std::array<RackRouter::DestinationBuffer, 2> events{};
  Fixture() {
    for (auto &r : controls.returns)
      r.enabled = false;
    slots[0].slotId = 100;
    slots[1].slotId = 200;
    controls.routeSlotIds[0] = 100;
    controls.routeSlotIds[1] = 200;
    CHECK(routing.prepare({sampleRate, frames, 2}),
          "routing prepares for stereo");
  }
  void begin(std::uint32_t count = frames) {
    routing.begin(controls, slots, events, count);
  }
  void trigger(std::uint32_t offset, std::uint8_t note = 36,
               std::uint8_t velocity = 100, std::uint8_t status = 0x90) {
    auto &destination = events[0];
    CHECK(destination.size < destination.events.size(),
          "fixture event capacity");
    destination.events[destination.size++] = {
        offset, 3, {status, note, velocity}};
  }
  void duck(float attack = 0, float hold = 1, float release = 2) {
    controls.kickSource = slots[0].slotId;
    controls.attackMs = attack;
    controls.holdMs = hold;
    controls.releaseMs = release;
    controls.duckDepth[1] = 0.75f;
  }
  void instantaneousReturn(std::size_t bus) {
    auto &r = controls.returns[bus];
    r.enabled = true;
    r.type = effects::EffectType::distortion;
    r.level = 1;
    r.amount = 0.5f;
    r.character = 0.5f;
  }
  Audio process(std::size_t index, Audio &input, bool audible = true) {
    Audio output;
    routing.mixSlot(index, input.pointers, output.pointers, slots[index],
                    audible, frames);
    routing.finish(output.pointers, frames);
    input.guards();
    output.guards();
    return output;
  }
};

void routesFollowStableIdsAcrossRenderOrder() {
  Fixture a, b;
  for (auto *f : {&a, &b}) {
    f->duck();
    f->instantaneousReturn(0);
    f->controls.sends[1][0] = {0.4f, true};
    f->trigger(0);
  }
  const auto routeSnapshot = b.controls.routeSlotIds;
  std::swap(b.slots[0], b.slots[1]);
  std::swap(b.events[0], b.events[1]);
  a.begin();
  b.begin();
  Audio input(0.25f);
  const auto normal = a.process(1, input), reordered = b.process(0, input);
  for (std::size_t c = 0; c < 2; ++c)
    for (std::size_t s = 0; s < frames; ++s)
      CHECK(close(normal.at(c, s), reordered.at(c, s)),
            "send/duck routing follows ID through render reorder");
  CHECK(b.controls.routeSlotIds == routeSnapshot,
        "render reorder does not retag route ownership");
  Fixture removed;
  removed.duck();
  removed.controls.routeSlotIds[1] = 999;
  removed.trigger(0);
  removed.begin();
  constant(removed.process(1, input), 0.25f, 0.25f);
  auto invalid = a.controls;
  invalid.routeSlotIds[1] = invalid.routeSlotIds[0];
  CHECK(!AudioRouting::valid(invalid), "duplicate recipient identity rejected");
  invalid = a.controls;
  invalid.routeSlotIds[1] = 0;
  CHECK(!AudioRouting::valid(invalid), "active zero identity route rejected");
}

void supportedRateBlockAndChannelMatrix() {
  for (const double rate : {44100.0, 48000.0, 96000.0})
    for (const std::uint32_t block : {64u, 128u, 256u, 512u, 1024u})
      for (const std::uint32_t channels : {1u, 2u}) {
        AudioRouting graph;
        CHECK(graph.prepare({rate, block, channels}),
              "matrix prepare succeeds");
        AudioRoutingControls controls;
        controls.routeSlotIds[0] = 100;
        controls.routeSlotIds[1] = 200;
        controls.kickSource = 100;
        controls.duckDepth[1] = 0.8f;
        controls.attackMs = 0;
        controls.holdMs = 2;
        controls.releaseMs = 3;
        for (auto &ret : controls.returns)
          ret.enabled = false;
        std::array<ProcessSlotControls, 2> slots{};
        slots[0].slotId = 100;
        slots[1].slotId = 200;
        std::array<RackRouter::DestinationBuffer, 2> events{};
        events[0].events[0] = {block - 1, 3, {0x90, 36, 100}};
        events[0].size = 1;
        std::array<std::vector<float>, 2> input, output;
        std::array<float *, 2> in{}, out{};
        for (std::size_t c = 0; c < channels; ++c) {
          input[c].assign(block, 0.25f);
          output[c].assign(block, 0);
          in[c] = input[c].data();
          out[c] = output[c].data();
        }
        CHECK(graph.begin(controls, slots, events, block),
              "matrix trigger block valid");
        graph.mixSlot(1, {in.data(), channels}, {out.data(), channels},
                      slots[1], true, block);
        graph.finish({out.data(), channels}, block);
        for (std::size_t c = 0; c < channels; ++c) {
          CHECK(close(output[c][block - 1], 0.05f),
                "last-sample trigger applies exact attenuation");
          if (block > 1)
            CHECK(close(output[c][block - 2], 0.25f),
                  "trigger does not move before its offset");
        }
        events[0].size = 0;
        const auto blocks =
            static_cast<unsigned>(std::ceil(rate * 0.02 / block)) + 1;
        for (unsigned b = 0; b < blocks; ++b) {
          for (std::size_t c = 0; c < channels; ++c)
            std::fill(output[c].begin(), output[c].end(), 0);
          CHECK(graph.begin(controls, slots, events, block),
                "matrix continuing block valid");
          graph.mixSlot(1, {in.data(), channels}, {out.data(), channels},
                        slots[1], true, block);
          graph.finish({out.data(), channels}, block);
        }
        for (std::size_t c = 0; c < channels; ++c)
          for (auto sample : output[c])
            CHECK(sample == 0.25f, "matrix release reaches exact unity");
        graph.reset();
        CHECK(graph.gainReduction() == 0,
              "matrix lifecycle resets attenuation");
      }
}

void dryGainAndStateInvariance() {
  Fixture f;
  f.slots[1].level = 0.4f;
  f.slots[1].pan = -0.25f;
  const auto originalLevel = f.slots[1].level;
  const auto originalPan = f.slots[1].pan;
  Audio input(0.5f);
  f.begin();
  auto output = f.process(1, input);
  constant(output, 0.2f, 0.15f);
  constant(input, 0.5f, 0.5f);
  CHECK(f.slots[1].level == originalLevel && f.slots[1].pan == originalPan,
        "runtime mixing never overwrites stored base gain/pan");
}

void preAndPostSendsUseRealAudio() {
  for (const bool pre : {false, true}) {
    Fixture f;
    f.instantaneousReturn(0);
    f.controls.sends[1][0] = {0.5f, pre};
    f.slots[1].level = 0;
    Audio input(0.25f);
    f.begin();
    const auto output = f.process(1, input);
    if (pre)
      CHECK(output.energy(0) > 1 && output.energy(1) > 1,
            "pre-fader send remains audible when slot fader is zero");
    else
      constant(output, 0, 0);
    constant(input, 0.25f, 0.25f);
  }
  Fixture pre, post;
  pre.instantaneousReturn(0);
  post.instantaneousReturn(0);
  pre.controls.sends[1][0] = {0.4f, true};
  post.controls.sends[1][0] = {0.4f, false};
  pre.slots[1].pan = post.slots[1].pan = -1;
  Audio input(0.2f);
  pre.begin();
  post.begin();
  const auto a = pre.process(1, input), b = post.process(1, input);
  CHECK(a.energy(1) > 0.1, "pre-fader return is independent of slot pan");
  CHECK(b.energy(1) == 0, "post-fader return follows slot pan");
}

void audibleFlagSuppressesDryAndEverySend() {
  for (const bool pre : {false, true}) {
    Fixture f;
    for (std::size_t bus = 0; bus < sendBusCount; ++bus) {
      f.instantaneousReturn(bus);
      f.controls.sends[1][bus] = {1, pre};
    }
    // Caller-computed audible=false represents mute, disabled, or another slot
    // solo.
    f.slots[1].mute = true;
    Audio input(0.4f);
    f.begin();
    constant(f.process(1, input, false), 0, 0);
    f.slots[1].mute = false;
    f.slots[0].solo = true;
    f.begin();
    constant(f.process(1, input, false), 0, 0);
  }
}

void returnsAreIsolatedAndDisabledNeverDoublesDry() {
  for (std::size_t bus = 0; bus < sendBusCount; ++bus) {
    Fixture f;
    f.slots[1].level = 0;
    f.controls.sends[1][bus] = {1, true};
    // A different bus is live: no cross-return leakage is allowed.
    f.instantaneousReturn((bus + 1) % sendBusCount);
    Audio input(0.2f);
    f.begin();
    constant(f.process(1, input), 0, 0);
    f.instantaneousReturn(bus);
    f.controls.returns[bus].pan = -1;
    f.begin();
    const auto wet = f.process(1, input);
    CHECK(wet.energy(0) > 1 && wet.energy(1) == 0,
          "send reaches only its enabled/panned return");
  }
  Fixture f;
  f.controls.sends[1][0] = {1, true};
  Audio input(0.2f);
  f.begin();
  constant(f.process(1, input), 0.2f, 0.2f);
}

void wetDelayTailAndReset() {
  Fixture f;
  f.slots[1].level = 0;
  f.controls.sends[1][0] = {1, true};
  auto &r = f.controls.returns[0];
  r.enabled = true;
  r.type = effects::EffectType::delay;
  r.level = 1;
  r.rate = 0;
  Audio impulse;
  impulse.at(0, 0) = impulse.at(1, 0) = 0.5f;
  f.begin();
  constant(f.process(1, impulse), 0,
           0); // Pure wet delay cannot duplicate the immediate dry impulse.
  CHECK(f.routing.tailSamples() > 0, "enabled time return declares a tail");
  Audio silence;
  double tailEnergy = 0;
  for (int block = 0; block < 12; ++block) {
    f.begin();
    const auto out = f.process(1, silence);
    tailEnergy += out.energy(0) + out.energy(1);
  }
  CHECK(tailEnergy > 0.01,
        "return retains wet tail across empty-source blocks");
  f.routing.reset();
  for (int block = 0; block < 8; ++block) {
    f.begin();
    constant(f.process(1, silence), 0, 0);
  }
  f.begin();
  (void)f.process(1, impulse);
  r.enabled = false;
  f.begin();
  constant(f.process(1, silence), 0, 0);
  r.enabled = true;
  for (int block = 0; block < 8; ++block) {
    f.begin();
    constant(f.process(1, silence), 0, 0);
  }
}

void kickOffsetsFilteringAndSourceExemption() {
  Fixture f;
  f.duck();
  f.trigger(0, 37);    // Different lane.
  f.trigger(1, 36, 0); // MIDI zero-velocity note-on means release.
  f.trigger(2, 36, 100, 0x80);
  f.trigger(10);
  Audio input(1);
  f.begin();
  const auto out = f.process(1, input);
  for (std::size_t s = 0; s < 10; ++s)
    CHECK(out.at(0, s) == 1,
          "wrong lane, note-off and zero-velocity never trigger ducking");
  CHECK(close(out.at(0, 10), 0.25f),
        "zero-attack duck begins exactly at trigger sample offset");
  CHECK(out.at(0, 40) == 1,
        "release completes without a hanging gain reduction");
  CHECK(f.slots[1].level == 1 && f.controls.duckDepth[1] == 0.75f,
        "ducking modulates runtime gain without rewriting base controls");
  f.controls.duckDepth[0] = 1;
  f.routing.reset();
  f.begin();
  constant(f.process(0, input), 1, 1);
}

void duckingContinuesAcrossBlocksAndRetriggers() {
  Fixture f;
  f.duck(0, 20, 10);
  f.trigger(frames - 2);
  Audio input(1);
  f.begin();
  const auto first = f.process(1, input);
  CHECK(first.at(0, frames - 3) == 1 && close(first.at(0, frames - 2), 0.25f),
        "late-block trigger does not duck earlier samples");
  f.events[0].clear();
  f.begin();
  const auto second = f.process(1, input);
  constant(second, 0.25f, 0.25f);
  // Trigger again after hold expires and release has begun.
  f.begin();
  (void)f.process(1, input);
  f.begin();
  const auto releasing = f.process(1, input);
  CHECK(releasing.at(0, 63) > 0.25f,
        "release progresses while valid source sends no further notes");
  f.trigger(4);
  f.begin();
  const auto retriggered = f.process(1, input);
  CHECK(close(retriggered.at(0, 4), 0.25f),
        "new kick restarts duck without waiting for old release");
  f.events[0].clear();
  for (int block = 0; block < 6; ++block) {
    f.begin();
    (void)f.process(1, input);
  }
  CHECK(f.routing.gainReduction() == 0, "envelope eventually returns to idle");
}

void missingMutedAndChangedSourceClearDucking() {
  Audio input(1);
  for (int mode = 0; mode < 5; ++mode) {
    Fixture f;
    f.duck(0, 100, 100);
    f.trigger(0);
    f.begin();
    CHECK(f.process(1, input).at(0, 0) < 1,
          "trigger established active envelope");
    f.events[0].clear();
    if (mode == 0)
      f.controls.kickSource = 999;
    if (mode == 1)
      f.slots[0].mute = true;
    if (mode == 2)
      f.slots[0].enabled = false;
    if (mode == 3)
      f.controls.kickNote = 38;
    if (mode == 4)
      f.slots[1].solo = true;
    f.begin();
    constant(f.process(1, input), 1, 1);
    CHECK(f.routing.gainReduction() == 0,
          "unavailable/changed source clears stale ducking");
  }
}

void zeroHoldDoesNotAddAnExtraSample() {
  Fixture f;
  f.duck(0, 0, 1);
  f.controls.duckDepth[1] = 1;
  f.trigger(4);
  Audio input(1);
  f.begin();
  const auto out = f.process(1, input);
  CHECK(out.at(0, 3) == 1 && out.at(0, 4) == 0,
        "zero attack reaches full duck on event sample");
  CHECK(out.at(0, 5) > 0 && out.at(0, 5) < 1,
        "zero hold begins release on following sample");
  CHECK(out.at(0, 12) == 1,
        "one millisecond release has completed after eight samples");
}

void nonzeroEnvelopeTimingAndBlockPartitionInvariance() {
  Fixture whole, split;
  whole.duck(1, 1, 1);
  split.duck(1, 1, 1);
  whole.trigger(5);
  whole.trigger(50);
  Audio input(1), wholeOutput, splitOutput;
  whole.begin();
  whole.routing.mixSlot(1, input.pointers, wholeOutput.pointers, whole.slots[1],
                        true, frames);
  whole.routing.finish(wholeOutput.pointers, frames);
  for (std::uint32_t offset : {0u, 32u}) {
    split.events[0].clear();
    split.trigger(offset == 0 ? 5 : 18);
    CHECK(split.routing.begin(split.controls, split.slots, split.events, 32),
          "half-size block accepted");
    std::array<float *, 2> in{input.pointers[0] + offset,
                              input.pointers[1] + offset};
    std::array<float *, 2> out{splitOutput.pointers[0] + offset,
                               splitOutput.pointers[1] + offset};
    split.routing.mixSlot(1, in, out, split.slots[1], true, 32);
    split.routing.finish(out, 32);
  }
  for (std::size_t c = 0; c < 2; ++c)
    for (std::size_t i = 0; i < frames; ++i)
      CHECK(close(wholeOutput.at(c, i), splitOutput.at(c, i)),
            "duck musical timing is independent of block partition");
  CHECK(wholeOutput.at(0, 4) == 1, "positive attack never anticipates event");
  CHECK(close(wholeOutput.at(0, 5), 1 - 0.75f / 8),
        "one-ms attack starts on event sample");
  CHECK(close(wholeOutput.at(0, 12), 0.25f),
        "one-ms attack reaches full depth in eight samples");
  CHECK(close(wholeOutput.at(0, 20), 0.25f),
        "hold lasts its eight declared samples");
  CHECK(wholeOutput.at(0, 21) > 0.25f && wholeOutput.at(0, 28) == 1,
        "release starts after hold and reaches unity in eight samples");
  wholeOutput.guards();
  splitOutput.guards();
}

void returnSumsMultipleSlotsBeforeProcessing() {
  Fixture multi, combined;
  multi.instantaneousReturn(2);
  combined.instantaneousReturn(2);
  multi.slots[0].level = multi.slots[1].level = combined.slots[1].level = 0;
  multi.controls.sends[0][2] = multi.controls.sends[1][2] =
      combined.controls.sends[1][2] = {0.5f, true};
  Audio a(0.1f), b(0.2f), sum(0.3f), expected, actual;
  multi.begin();
  combined.begin();
  multi.routing.mixSlot(0, a.pointers, actual.pointers, multi.slots[0], true,
                        frames);
  multi.routing.mixSlot(1, b.pointers, actual.pointers, multi.slots[1], true,
                        frames);
  multi.routing.finish(actual.pointers, frames);
  combined.routing.mixSlot(1, sum.pointers, expected.pointers,
                           combined.slots[1], true, frames);
  combined.routing.finish(expected.pointers, frames);
  for (std::size_t c = 0; c < 2; ++c)
    for (std::size_t i = 0; i < frames; ++i)
      CHECK(close(actual.at(c, i), expected.at(c, i)),
            "return processes the sum of slot sends once");
}

void resetAfterRingWrapAndChannelChurn() {
  Fixture f;
  auto &r = f.controls.returns[0];
  r.enabled = true;
  r.type = effects::EffectType::delay;
  r.level = 1;
  r.rate = 0;
  f.controls.sends[1][0] = {1, true};
  f.slots[1].level = 0;
  Audio signal(0.2f), silence;
  for (int block = 0; block < 300; ++block) {
    f.begin();
    (void)f.process(1, signal);
  }
  f.routing.reset();
  for (int block = 0; block < 10; ++block) {
    f.begin();
    constant(f.process(1, silence), 0, 0);
  }
  for (const std::uint32_t channels : {1u, 2u, 1u, 2u}) {
    CHECK(f.routing.prepare({sampleRate, frames, channels}),
          "mono/stereo reprepare succeeds");
    Audio output;
    auto in = std::span<float *>(silence.pointers.data(), channels);
    auto out = std::span<float *>(output.pointers.data(), channels);
    for (int block = 0; block < 10; ++block) {
      CHECK(f.routing.begin(f.controls, f.slots, f.events, frames),
            "reprepared block accepted");
      f.routing.mixSlot(1, in, out, f.slots[1], true, frames);
      f.routing.finish(out, frames);
    }
    constant(output, 0, 0);
  }
}

void skippedStereoChannelCannotReviveDirtyRingAudio() {
  for (const auto type :
       {effects::EffectType::flanger, effects::EffectType::chorus,
        effects::EffectType::delay, effects::EffectType::reverb}) {
    effects::EffectsChain dirty, fresh;
    CHECK(dirty.prepare(sampleRate, frames, 2) &&
              fresh.prepare(sampleRate, frames, 2),
          "two stereo reference chains prepare");
    const auto index = static_cast<std::size_t>(type);
    const effects::EffectSettings setting{type, true, 1, 0.5f, 0, 0.5f};
    CHECK(dirty.setEffect(index, setting) && fresh.setEffect(index, setting),
          "time effect configured identically");
    for (int block = 0; block < 300; ++block) {
      Audio nonzero(0.3f);
      dirty.process(nonzero.pointers, frames);
    }
    dirty.reset();
    // Channel 1 is skipped while the shared write cursor advances after lazy
    // reset.
    for (int block = 0; block < 5; ++block) {
      Audio dirtyMono(0.1f), freshMono(0.1f);
      dirty.process({dirtyMono.pointers.data(), 1}, frames);
      fresh.process({freshMono.pointers.data(), 1}, frames);
    }
    for (int block = 0; block < 16; ++block) {
      Audio actual, expected;
      dirty.process(actual.pointers, frames);
      fresh.process(expected.pointers, frames);
      for (std::size_t c = 0; c < 2; ++c)
        for (std::size_t i = 0; i < frames; ++i)
          CHECK(close(actual.at(c, i), expected.at(c, i)),
                "resuming stereo after reset and mono never revives stale "
                "channel-1 audio");
      actual.guards();
      expected.guards();
    }
  }
}

void validationRejectsMalformedControls() {
  AudioRoutingControls controls;
  CHECK(AudioRouting::valid(controls), "default controls valid");
  controls.sends[0][3].gain = std::numeric_limits<float>::quiet_NaN();
  CHECK(!AudioRouting::valid(controls), "nonfinite send rejected");
  controls = {};
  controls.returns[2].type = static_cast<effects::EffectType>(8);
  CHECK(!AudioRouting::valid(controls), "unknown return effect rejected");
  controls = {};
  controls.duckDepth[15] = 1.01f;
  CHECK(!AudioRouting::valid(controls), "depth beyond unity rejected");
  controls = {};
  controls.kickNote = 128;
  CHECK(!AudioRouting::valid(controls), "invalid MIDI note rejected");
  controls = {};
  controls.releaseMs = 0;
  CHECK(!AudioRouting::valid(controls),
        "zero release outside declared contract rejected");
}

void lifecycleAndInvalidBlockFailClosed() {
  Audio input(0.5f), output(0.125f);
  ProcessSlotControls slot;
  slot.slotId = 1;
  AudioRouting unprepared;
  unprepared.mixSlot(0, input.pointers, output.pointers, slot, true, frames);
  unprepared.finish(output.pointers, frames);
  constant(output, 0.125f, 0.125f);
  CHECK(!unprepared.prepare({8000, frames, 3}),
        "unsupported channel count rejected");
  CHECK(!unprepared.prepare({8000, 0, 2}), "zero maximum block rejected");
  CHECK(
      !unprepared.prepare({std::numeric_limits<double>::infinity(), frames, 2}),
      "nonfinite sample rate rejected");

  Fixture f;
  f.begin();
  (void)f.process(1, input);
  f.controls.sends[1][0].gain = -1;
  f.begin();
  f.routing.mixSlot(1, input.pointers, output.pointers, f.slots[1], true,
                    frames);
  f.routing.finish(output.pointers, frames);
  constant(output, 0.125f, 0.125f);
  f.controls.sends[1][0].gain = 0;
  f.begin(frames + 1);
  f.routing.mixSlot(1, input.pointers, output.pointers, f.slots[1], true,
                    frames);
  f.routing.finish(output.pointers, frames);
  constant(output, 0.125f, 0.125f);
  f.events[0].size = RackRouter::eventCapacity + 1;
  CHECK(!f.routing.begin(f.controls, f.slots, f.events, frames),
        "oversized destination event count rejected before view");
  f.events[0].size = 0;
  f.routing.mixSlot(1, input.pointers, output.pointers, f.slots[1], true,
                    frames);
  f.routing.finish(output.pointers, frames);
  constant(output, 0.125f, 0.125f);
  CHECK(f.routing.prepare({16000, frames, 1}),
        "reprepare changes channel/sample-rate shape");
  CHECK(f.routing.prepare({8000, frames, 2}),
        "reprepare stereo after mono succeeds");
  f.begin();
  CHECK(!f.routing.prepare({0, frames, 2}), "invalid reprepare rejected");
  f.routing.mixSlot(1, input.pointers, output.pointers, f.slots[1], true,
                    frames);
  f.routing.finish(output.pointers, frames);
  constant(output, 0.125f, 0.125f);
}

void invalidShapesAndBlockOrderPreserveDestination() {
  Fixture f;
  Audio input(0.2f), out(0.3f);
  f.begin();
  std::array<float *, 1> mono{input.pointers[0]};
  f.routing.mixSlot(1, mono, out.pointers, f.slots[1], true, frames);
  constant(out, 0.3f, 0.3f);
  f.routing.mixSlot(instrument::maxSlots, input.pointers, out.pointers,
                    f.slots[1], true, frames);
  constant(out, 0.3f, 0.3f);
  std::array<float *, 2> badInput{input.pointers[0], nullptr};
  f.routing.mixSlot(1, badInput, out.pointers, f.slots[1], true, frames);
  constant(out, 0.3f, 0.3f);
  std::array<float *, 2> badOutput{out.pointers[0], nullptr};
  f.routing.finish(badOutput, frames);
  constant(out, 0.3f, 0.3f);
  f.routing.finish(out.pointers, frames);
  // A block must not be mixed or finished twice after completion.
  f.routing.mixSlot(1, input.pointers, out.pointers, f.slots[1], true, frames);
  f.routing.finish(out.pointers, frames);
  constant(out, 0.3f, 0.3f);
  f.begin(0);
  f.routing.mixSlot(1, input.pointers, out.pointers, f.slots[1], true, frames);
  f.routing.finish(out.pointers, frames);
  constant(out, 0.3f, 0.3f);
}
} // namespace

int main() {
  struct Test {
    const char *name;
    void (*run)();
  };
  const std::array<Test, 18> tests{
      {Test{"rate/block/channel matrix", supportedRateBlockAndChannelMatrix},
       Test{"stable recipient identity",
            routesFollowStableIdsAcrossRenderOrder},
       Test{"dry gain/base-state invariance", dryGainAndStateInvariance},
       Test{"pre/post sends", preAndPostSendsUseRealAudio},
       Test{"mute/solo audible gating", audibleFlagSuppressesDryAndEverySend},
       Test{"return bus isolation",
            returnsAreIsolatedAndDisabledNeverDoublesDry},
       Test{"wet tails/reset", wetDelayTailAndReset},
       Test{"kick timing/filter/source exemption",
            kickOffsetsFilteringAndSourceExemption},
       Test{"cross-block duck/retrigger",
            duckingContinuesAcrossBlocksAndRetriggers},
       Test{"missing/changed source", missingMutedAndChangedSourceClearDucking},
       Test{"zero hold timing", zeroHoldDoesNotAddAnExtraSample},
       Test{"nonzero timing/block partitions",
            nonzeroEnvelopeTimingAndBlockPartitionInvariance},
       Test{"multi-slot return sum", returnSumsMultipleSlotsBeforeProcessing},
       Test{"ring-wrap reset/channel churn", resetAfterRingWrapAndChannelChurn},
       Test{"lazy-clear skipped stereo channel",
            skippedStereoChannelCannotReviveDirtyRingAudio},
       Test{"control validation", validationRejectsMalformedControls},
       Test{"lifecycle/invalid blocks", lifecycleAndInvalidBlockFailClosed},
       Test{"shape/block-order safety",
            invalidShapesAndBlockOrderPreserveDestination}}};
  int failed = 0;
  for (const auto &[name, run] : tests) {
    try {
      run();
      std::cout << "PASS: " << name << '\n';
    } catch (const std::exception &e) {
      ++failed;
      std::cerr << "FAIL: " << name << ": " << e.what() << '\n';
    }
  }
  std::cout << checks << " checks; " << failed << " failing scenarios\n";
  return failed ? 1 : 0;
}
