// Same-block kick ducking: a kick an instrument generates itself (internal
// sequencer, reported through the note output) ducks its targets at the very
// sample it plays, whatever the slot order, block size or sample rate.
#include "rack/AudioRouting.h"
#include "rack/Rack.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace vstengine;
constexpr std::string_view kitId = "com.ultimavox.test.internal-kit";
constexpr std::string_view dcId = "com.ultimavox.test.dc";
int checks{};
void require(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

// What the kit reports. Absolute sample positions of its own kicks, and
// optional hostile output for the bounds tests.
struct Script {
  std::vector<std::uint64_t> hits;
  std::uint8_t note{36};
  enum class Hostile { none, overCount, unsorted, outOfBlock } hostile{Hostile::none};
};

class Kit final : public instrument::InstrumentInstance {
public:
  explicit Kit(const Script &script) : script_(script) {}
  bool prepare(const instrument::PrepareSpec &) override { return true; }
  void reset() noexcept override { position_ = 0; }
  void suspend() noexcept override {}
  void resume() noexcept override {}
  void setBypassed(bool) noexcept override {}
  void process(const instrument::ProcessBlock &block) noexcept override {
    for (auto *channel : block.outputs)
      std::fill_n(channel, block.sampleCount, 0.0f);
    auto *out = block.noteOutput;
    if (out != nullptr) {
      out->count = 0;
      for (const auto hit : script_.hits)
        if (hit >= position_ && hit < position_ + block.sampleCount && out->count < out->capacity)
          out->events[out->count++] = {static_cast<std::uint32_t>(hit - position_), script_.note,
                                       110, 0, 0};
      switch (script_.hostile) {
      case Script::Hostile::overCount:
        out->count = out->capacity + 7; // more than the buffer holds
        break;
      case Script::Hostile::unsorted:
        if (out->count == 1 && out->capacity >= 2) {
          out->events[1] = out->events[0];
          out->events[0].sampleOffset += 5; // a later event first
          out->count = 2;
        }
        break;
      case Script::Hostile::outOfBlock:
        if (out->count < out->capacity)
          out->events[out->count++] = {block.sampleCount + 3, script_.note, 110, 0, 0};
        break;
      case Script::Hostile::none:
        break;
      }
    }
    position_ += block.sampleCount;
  }
  bool setParameter(std::string_view, float) noexcept override { return false; }
  bool loadState(std::uint32_t schema, std::span<const std::byte> payload) noexcept override {
    return schema == instrument::stateSchemaVersion && payload.empty();
  }
  bool saveState(std::span<std::byte>, std::uint32_t &bytes) const noexcept override {
    bytes = 0;
    return true;
  }
  std::uint32_t latencySamples() const noexcept override { return 0; }
  std::uint32_t tailSamples() const noexcept override { return 0; }

private:
  const Script &script_;
  std::uint64_t position_{};
};

// A steady 1.0 on both channels, so the duck gain is visible sample by sample.
class Dc final : public instrument::InstrumentInstance {
public:
  bool prepare(const instrument::PrepareSpec &) override { return true; }
  void reset() noexcept override {}
  void suspend() noexcept override {}
  void resume() noexcept override {}
  void setBypassed(bool) noexcept override {}
  void process(const instrument::ProcessBlock &block) noexcept override {
    for (auto *channel : block.outputs)
      std::fill_n(channel, block.sampleCount, 1.0f);
  }
  bool setParameter(std::string_view, float) noexcept override { return false; }
  bool loadState(std::uint32_t schema, std::span<const std::byte> payload) noexcept override {
    return schema == instrument::stateSchemaVersion && payload.empty();
  }
  bool saveState(std::span<std::byte>, std::uint32_t &bytes) const noexcept override {
    bytes = 0;
    return true;
  }
  std::uint32_t latencySamples() const noexcept override { return 0; }
  std::uint32_t tailSamples() const noexcept override { return 0; }
};

class Provider final : public instrument::InstrumentProvider {
public:
  explicit Provider(const Script &script) : script_(script) {
    auto content = std::make_shared<pattern::Content>();
    content->caps.playback = VOX_PATTERN_PLAYBACK_INTERNAL;
    content->caps.laneCount = 1;
    content->caps.maxStepsPerBar = 16;
    content->caps.maxBars = 1;
    content->caps.stepFields = VOX_FIELD_VELOCITY;
    content->caps.laneNames = {"Kick"};
    content->caps.laneNotes = {36};
    for (auto *d : {&descriptors_[0], &descriptors_[1]}) {
      d->providerId = "com.ultimavox.test-ducking";
      d->vendor = "Ultima Vox";
      d->tailPolicy = instrument::TailPolicy::none;
      d->budget = {1, 2048, 64, 0, 0, 0, 0, 0, 4096};
    }
    descriptors_[0].id = kitId;
    descriptors_[0].name = "Internal kit";
    descriptors_[0].patternContent = content;
    descriptors_[1].id = dcId;
    descriptors_[1].name = "DC";
  }
  std::span<const instrument::InstrumentDescriptor> descriptors() const noexcept override {
    return descriptors_;
  }
  std::unique_ptr<instrument::InstrumentInstance>
  create(std::string_view id, const instrument::CreateContext &) override {
    if (id == kitId)
      return std::make_unique<Kit>(script_);
    if (id == dcId)
      return std::make_unique<Dc>();
    return nullptr;
  }

private:
  const Script &script_;
  std::array<instrument::InstrumentDescriptor, 2> descriptors_;
};

struct Setup {
  std::size_t kitSlot{0}, dcSlot{15};
  double rate{48000.0};
  std::uint32_t block{128};
  bool mute{}, soloOther{};
  bool midiKick{}; // the same kick also arrives as routed host MIDI (Both)
  std::uint8_t kickNote{36};
};

// Renders `total` samples; returns the DC slot's output (left channel).
std::vector<float> render(const Script &script, const Setup &s, std::uint32_t total) {
  instrument::InstrumentRegistry registry;
  std::string diagnostic;
  require(registry.registerProvider(std::make_unique<Provider>(script), diagnostic),
          "test provider registers: " + diagnostic);
  rack::Rack rack(registry);
  require(rack.prepare({s.rate, s.block, 2}), "rack prepares");
  require(rack.loadModule(s.kitSlot, kitId, nullptr, diagnostic), "kit loads: " + diagnostic);
  require(rack.loadModule(s.dcSlot, dcId, nullptr, diagnostic), "DC loads: " + diagnostic);
  rack::AudioRouting routing;
  require(routing.prepare({s.rate, s.block, 2}), "routing prepares");
  auto states = rack.snapshotState();
  std::array<rack::ProcessSlotControls, instrument::maxSlots> controls;
  for (std::size_t i = 0; i < states.size(); ++i)
    controls[i] = rack::processControls(states[i]);
  controls[s.kitSlot].routing = {rack::RouteMode::channel, 1, 0, 127, 1, 127, 0};
  controls[s.kitSlot].mute = s.mute;
  controls[s.dcSlot].solo = s.soloOther;
  rack::AudioRoutingControls audio;
  for (auto &r : audio.returns)
    r.enabled = false;
  for (std::size_t i = 0; i < states.size(); ++i)
    audio.routeSlotIds[i] = states[i].slotId;
  audio.kickSource = states[s.kitSlot].slotId;
  audio.kickNote = s.kickNote;
  audio.attackMs = 0;
  audio.holdMs = 1;
  audio.releaseMs = 2;
  audio.duckDepth[s.dcSlot] = 1.0f;
  std::vector<float> left(s.block), right(s.block), result;
  std::array<float *, 2> outputs{left.data(), right.data()};
  for (std::uint32_t start = 0; start < total; start += s.block) {
    const auto n = std::min(s.block, total - start);
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    std::vector<VoxMidiEventV1> midi;
    if (s.midiKick)
      for (const auto hit : script.hits)
        if (hit >= start && hit < start + n)
          midi.push_back({static_cast<std::uint32_t>(hit - start), 3, {0x90, 36, 110}});
    rack.process({outputs.data(), 2}, n, midi, 145.0, 0.0, true, {}, instrument::invalidSlotId,
                 controls, {}, &routing, &audio);
    result.insert(result.end(), left.begin(), left.begin() + n);
  }
  return result;
}

int firstDuck(const std::vector<float> &out) {
  for (std::size_t i = 0; i < out.size(); ++i)
    if (out[i] < 0.999f)
      return static_cast<int>(i);
  return -1;
}
} // namespace

int main() {
  // The kick at sample k ducks the target at k: same block, any slot order,
  // any block size or rate, no latency.
  for (const double rate : {44100.0, 48000.0, 96000.0})
    for (const std::uint32_t block : {64u, 128u, 512u})
      for (const auto [kit, dc] : {std::pair{0u, 15u}, std::pair{15u, 0u}, std::pair{7u, 8u}})
        for (const std::uint64_t k : {0u, 1u, 63u, 300u, 777u}) {
          Script script;
          script.hits = {k};
          Setup s;
          s.kitSlot = kit;
          s.dcSlot = dc;
          s.rate = rate;
          s.block = block;
          const auto out = render(script, s, 1536);
          const std::string where = std::to_string(static_cast<int>(rate)) + " Hz, block " +
                                    std::to_string(block) + ", kit slot " + std::to_string(kit) +
                                    ", target slot " + std::to_string(dc) + ", kick at " +
                                    std::to_string(k);
          require(firstDuck(out) == static_cast<int>(k) && out[k] == 0.0f,
                  "internal kick ducks at its own sample: " + where);
          require(out.back() == 1.0f, "the duck releases: " + where);
        }

  Script two;
  two.hits = {200, 900};
  Setup base;
  const auto internal = render(two, base, 1536);
  require(firstDuck(internal) == 200 && internal[900] == 0.0f, "every internal kick ducks");

  // Both: the same kick as host MIDI and as an internal hit triggers once.
  auto both = base;
  both.midiKick = true;
  require(render(two, both, 1536) == internal, "a coincident MIDI + internal kick ducks once");
  Script none;
  auto hostOnly = base;
  hostOnly.midiKick = true;
  // Host MIDI ducking is unchanged (script hits drive the MIDI in render()).
  Script midiDriver;
  midiDriver.hits = {200, 900};
  midiDriver.note = 99; // the kit's own reports use another note: ignored
  require(render(midiDriver, hostOnly, 1536) == internal,
          "host MIDI kicks still duck on their own (Host mode)");

  // Note filtering, muted and soloed-away sources.
  auto otherNote = base;
  otherNote.kickNote = 38;
  require(firstDuck(render(two, otherNote, 1536)) == -1, "a non-kick note does not duck");
  auto muted = base;
  muted.mute = true;
  require(firstDuck(render(two, muted, 1536)) == -1, "a muted source ducks nothing");
  auto soloed = base;
  soloed.soloOther = true;
  require(firstDuck(render(two, soloed, 1536)) == -1, "a source silenced by solo ducks nothing");
  require(firstDuck(render(none, base, 1536)) == -1, "no kick, no duck");

  // Hostile note output: bounded, ordered, inside the block.
  auto hostile = two;
  hostile.hostile = Script::Hostile::overCount;
  require(firstDuck(render(hostile, base, 1536)) == -1,
          "a count above the capacity discards the block's events (no stale reads)");
  hostile.hostile = Script::Hostile::unsorted;
  const auto unsorted = render(hostile, base, 1536);
  require(firstDuck(unsorted) == 205, "an out-of-order event is ignored, the ordered one ducks");
  hostile.hostile = Script::Hostile::outOfBlock;
  require(render(hostile, base, 1536) == internal, "an event beyond the block is ignored");

  std::cout << "Rack ducking tests passed (" << checks << ")\n";
  return 0;
}
