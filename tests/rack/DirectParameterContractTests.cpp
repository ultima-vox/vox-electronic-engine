#include "instrument/HostParameterSchema.h"
#include "instrument/InstrumentRegistry.h"
#include "rack/Rack.h"
#include "rack/RackState.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

// The probe covers ordinary/aligned C++ allocations on the callback thread.
// It deliberately does not claim to intercept arbitrary C malloc in providers.
namespace direct_allocation_probe {
std::atomic<bool> active{false};
std::atomic<std::uint64_t> count{0};
void record() noexcept {
  if (active.load(std::memory_order_relaxed))
    count.fetch_add(1, std::memory_order_relaxed);
}
} // namespace direct_allocation_probe
void *operator new(std::size_t n) {
  direct_allocation_probe::record();
  if (auto *p = std::malloc(std::max(n, std::size_t(1))))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void *operator new(std::size_t n, const std::nothrow_t &) noexcept {
  try {
    return ::operator new(n);
  } catch (...) {
    return nullptr;
  }
}
void *operator new[](std::size_t n, const std::nothrow_t &t) noexcept {
  return ::operator new(n, t);
}
void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept {
  std::free(p);
}
namespace {
void *alignedMemory(std::size_t n, std::size_t a) noexcept {
#if defined(_MSC_VER)
  return _aligned_malloc(std::max(n, std::size_t(1)), a);
#else
  void *p{};
  return posix_memalign(&p, a, std::max(n, std::size_t(1))) == 0 ? p : nullptr;
#endif
}
void alignedFree(void *p) noexcept {
#if defined(_MSC_VER)
  _aligned_free(p);
#else
  std::free(p);
#endif
}
} // namespace
void *operator new(std::size_t n, std::align_val_t a) {
  direct_allocation_probe::record();
  if (auto *p = alignedMemory(n, static_cast<std::size_t>(a)))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n, std::align_val_t a) {
  return ::operator new(n, a);
}
void operator delete(void *p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void *p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept {
  alignedFree(p);
}
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept {
  alignedFree(p);
}

namespace {
using namespace vstengine;
using namespace vstengine::instrument;
using State = std::array<rack::PersistentSlotState, maxSlots>;
constexpr std::uint32_t frames = 32;
int checks{};
#define CHECK(c, m)                                                            \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(c))                                                                  \
      throw std::runtime_error(std::string(m) + " (line " +                    \
                               std::to_string(__LINE__) + ")");                \
  } while (false)
std::string parameterId(std::size_t index) {
  auto digits = std::to_string(index);
  return "p" + std::string(3 - digits.size(), '0') + digits;
}
std::size_t stableIndex(std::string_view id) noexcept {
  std::size_t value = maxDirectParameters;
  if (id.size() == 4 && id.front() == 'p') {
    const auto result =
        std::from_chars(id.data() + 1, id.data() + id.size(), value);
    if (result.ec != std::errc{} || result.ptr != id.data() + id.size())
      return maxDirectParameters;
  }
  return value;
}
ParameterDescriptor parameter(std::size_t index) {
  ParameterDescriptor p;
  p.id = parameterId(index);
  p.name = "Direct " + std::to_string(index);
  p.defaultValue = 0.25f;
  if (index == 0) {
    p.minimum = -24;
    p.maximum = 12;
    p.defaultValue = -6;
    p.unit = "dB";
  }
  if (index == 1) {
    p.minimum = 100;
    p.maximum = 20000;
    p.defaultValue = 1200;
    p.unit = "Hz";
  }
  if (index == 2) {
    p.minimum = 0;
    p.maximum = 7;
    p.defaultValue = 2;
    p.step = 1;
    p.type = ParameterType::integer;
  }
  if (index == 3) {
    p.minimum = 0;
    p.maximum = 1;
    p.defaultValue = 1;
    p.step = 1;
    p.type = ParameterType::boolean;
  }
  if (index == 4) {
    p.minimum = 0;
    p.maximum = 3;
    p.defaultValue = 1;
    p.step = 1;
    p.type = ParameterType::choice;
    p.choices = {"A", "B", "C", "D"};
  }
  if (index == 5) {
    p.minimum = -2;
    p.maximum = 2;
    p.defaultValue = 0;
    p.step = 0.25f;
  }
  return p;
}
InstrumentDescriptor descriptor(std::string id = "test.direct.main",
                                std::size_t count = 12) {
  InstrumentDescriptor d;
  d.id = std::move(id);
  d.providerId = "test.direct.provider";
  d.name = "Direct Contract Probe";
  d.vendor = "Tests";
  d.directParameters = true;
  d.budget.maxVoices = 1;
  d.budget.maxMidiEventsPerBlock = 32;
  d.budget.maxStateBytes = sizeof(float) * maxDirectParameters;
  d.budget.maxPatternEvents = 1;
  d.budget.maxModulationRoutes = 1;
  for (std::size_t i = 0; i < count; ++i)
    d.parameters.push_back(parameter(i));
  return d;
}
struct Observation {
  std::array<float, maxDirectParameters> values{};
  std::array<std::uint32_t, maxDirectParameters> sets{};
  std::uint32_t rejected{}, saved{}, processed{};
};
struct Faults {
  bool save{}, prepare{}, rejectLowGain{};
};
class Instance final : public InstrumentInstance {
public:
  Instance(const InstrumentDescriptor &d, std::shared_ptr<Observation> o,
           Faults &faults)
      : d_(d), o_(std::move(o)), faults_(faults) {
    for (const auto &p : d_.parameters)
      if (stableIndex(p.id) < maxDirectParameters)
        o_->values[stableIndex(p.id)] = p.defaultValue;
  }
  bool prepare(const PrepareSpec &) override { return !faults_.prepare; }
  void reset() noexcept override {}
  void suspend() noexcept override {}
  void resume() noexcept override {}
  void setBypassed(bool bypass) noexcept override { bypass_ = bypass; }
  void process(const ProcessBlock &block) noexcept override {
    ++o_->processed;
    if (bypass_)
      return;
    const float signal = o_->values[0] * 0.01f + o_->values[1] * 0.00001f +
                         o_->values[2] * 0.001f + o_->values[11] * 0.1f;
    for (auto *out : block.outputs)
      for (std::uint32_t s = 0; s < block.sampleCount; ++s)
        out[s] += signal;
  }
  bool setParameter(std::string_view id, float value) noexcept override {
    const auto it = std::find_if(d_.parameters.begin(), d_.parameters.end(),
                                 [id](const auto &p) { return p.id == id; });
    const auto index = stableIndex(id);
    if (it == d_.parameters.end() || index >= maxDirectParameters ||
        !std::isfinite(value) || value < it->minimum || value > it->maximum ||
        (it->type != ParameterType::floating && value != std::round(value)) ||
        (it->step > 0 &&
         std::abs((value - it->minimum) / it->step -
                  std::round((value - it->minimum) / it->step)) > 0.0001f) ||
        (faults_.rejectLowGain && index == 0 && value < -20)) {
      ++o_->rejected;
      return false;
    }
    o_->values[index] = value;
    ++o_->sets[index];
    return true;
  }
  bool loadState(std::uint32_t version,
                 std::span<const std::byte> bytes) noexcept override {
    if (version != 1)
      return false;
    if (bytes.empty())
      return true;
    if (bytes.size() != sizeof(o_->values))
      return false;
    std::array<float, maxDirectParameters> candidate{};
    std::memcpy(candidate.data(), bytes.data(), bytes.size());
    if (!std::isfinite(candidate[127]) || candidate[127] < 0 ||
        candidate[127] > 1000)
      return false;
    o_->values = candidate;
    return true;
  }
  bool saveState(std::span<std::byte> bytes,
                 std::uint32_t &written) const noexcept override {
    ++o_->saved;
    if (faults_.save || bytes.size() < sizeof(o_->values))
      return false;
    std::memcpy(bytes.data(), o_->values.data(), sizeof(o_->values));
    written = sizeof(o_->values);
    return true;
  }
  std::uint32_t latencySamples() const noexcept override { return 0; }
  std::uint32_t tailSamples() const noexcept override { return 0; }

private:
  const InstrumentDescriptor &d_;
  std::shared_ptr<Observation> o_;
  Faults &faults_;
  bool bypass_{};
};
class Provider final : public InstrumentProvider {
public:
  explicit Provider(InstrumentDescriptor d) : descriptor_(std::move(d)) {
    ContentDescriptor preset;
    preset.instrumentId = descriptor_.id;
    preset.id = "test.direct.preset";
    preset.name = "Complete named preset";
    preset.supportedPresetClasses = VOX_PRESET_CLASS_SOUND;
    for (const auto &p : descriptor_.parameters)
      preset.parameterValues.push_back({p.id, p.defaultValue});
    contents.push_back(std::move(preset));
  }
  std::span<const InstrumentDescriptor> descriptors() const noexcept override {
    return {&descriptor_, 1};
  }
  std::unique_ptr<InstrumentInstance> create(std::string_view id,
                                             const CreateContext &) override {
    if (id != descriptor_.id)
      return {};
    latest = std::make_shared<Observation>();
    return std::make_unique<Instance>(descriptor_, latest, faults);
  }
  std::span<const ContentDescriptor>
  contentDescriptors(std::string_view id) const noexcept override {
    return id == descriptor_.id ? std::span<const ContentDescriptor>(contents)
                                : std::span<const ContentDescriptor>{};
  }
  ContentStatus applySoundPreset(std::string_view, std::string_view,
                                 InstrumentInstance &) const noexcept override {
    ++legacyPresetCalls;
    return ContentStatus::unsupported;
  }
  InstrumentDescriptor descriptor_;
  std::vector<ContentDescriptor> contents;
  std::shared_ptr<Observation> latest;
  Faults faults;
  mutable std::uint32_t legacyPresetCalls{};
};
struct Fixture {
  InstrumentRegistry registry;
  Provider *provider{};
  std::unique_ptr<rack::Rack> engine;
  std::string error;
  explicit Fixture(bool reversed = false, std::size_t count = 12,
                   bool modulationCapable = false) {
    auto d = descriptor("test.direct.main", count);
    if (modulationCapable) {
      d.capabilities |= Capability::modulation;
      d.parameters[1].modulatable = true;
    }
    if (reversed)
      std::reverse(d.parameters.begin(), d.parameters.end());
    auto candidate = std::make_unique<Provider>(std::move(d));
    provider = candidate.get();
    CHECK(registry.registerProvider(std::move(candidate), error),
          "valid direct provider registers");
    engine = std::make_unique<rack::Rack>(registry);
    CHECK(engine->prepare({48000, frames, 2}), "rack prepares");
    CHECK(engine->loadModule(0, provider->descriptor_.id, nullptr, error),
          "direct module loads defaults");
    auto routing = engine->state()[0].routing;
    routing.mode = rack::RouteMode::channel;
    engine->updateControls(0, routing, true, false, false, false, 1, 0);
  }
  auto controls() const {
    std::array<rack::ProcessSlotControls, maxSlots> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
      result[i] = rack::processControls(engine->state()[i]);
    return result;
  }
  void render(std::span<const rack::ProcessSlotControls> control = {}) {
    std::array<float, frames> l{}, r{};
    std::array<float *, 2> pointers{l.data(), r.data()};
    engine->process(pointers, frames, {}, 145, 0, true, {}, 0, control);
    for (float x : l)
      CHECK(std::isfinite(x), "direct callback produces finite audio");
  }
};
void change(rack::ProcessSlotControls &controls, std::string_view instrument,
            std::string_view parameter, float value) {
  const auto key = hostparams::directKey(instrument, parameter);
  for (std::size_t i = 0; i < std::min(controls.directParameterCount,
                                       controls.directParameters.size());
       ++i)
    if (controls.directParameters[i].key == key) {
      controls.directParameters[i].value = value;
      return;
    }
  CHECK(controls.directParameterCount < controls.directParameters.size(),
        "fixture control capacity");
  controls.directParameters[controls.directParameterCount++] = {key, value};
}
float named(const State &state, std::string_view id) {
  for (const auto &v : state[0].directParameters)
    if (v.id == id)
      return v.value;
  throw std::runtime_error("missing named parameter " + std::string(id));
}
void namedSet(State &state, std::string_view id, float value) {
  for (auto &v : state[0].directParameters)
    if (v.id == id) {
      v.value = value;
      return;
    }
  throw std::runtime_error("missing named parameter");
}
juce::String encoded(const State &state) {
  return rack::state::serialize(state).toXmlString();
}

void independentPlainControlsIgnoreMacros() {
  Fixture f;
  auto controls = f.controls();
  auto initial = f.provider->latest->values;
  CHECK(controls[0].directParameterCount == 12,
        "more than eight real parameters exposed independently");
  for (const auto &id : f.engine->state()[0].macroAssignments)
    CHECK(id.empty(), "direct instrument never receives macro ownership");
  change(controls[0], f.provider->descriptor_.id, "p000", -3.5f);
  change(controls[0], f.provider->descriptor_.id, "p001", 4321.125f);
  change(controls[0], f.provider->descriptor_.id, "p011", 0.875f);
  controls[0].macros.fill(1);
  f.render(controls);
  CHECK(f.provider->latest->values[0] == -3.5f &&
            f.provider->latest->values[1] == 4321.125f &&
            f.provider->latest->values[11] == 0.875f,
        "separate plain dB/Hz/later-than-eight controls reach their actual "
        "parameter");
  for (std::size_t i = 2; i < 11; ++i)
    CHECK(f.provider->latest->values[i] == initial[i],
          "unrelated direct values remain unchanged");
  CHECK(f.provider->latest->rejected == 0,
        "callback delivers legal parameter values");
  const auto before = f.provider->latest->saved;
  const auto snapshot = f.engine->snapshotState(controls);
  CHECK(named(snapshot, "p000") == -3.5f &&
            named(snapshot, "p001") == 4321.125f &&
            named(snapshot, "p011") == 0.875f,
        "snapshot preserves actual named plain units");
  CHECK(f.provider->latest->saved == before,
        "snapshot does not ask mutable live DSP to save");
  f.render();
  CHECK(f.provider->latest->values[0] == -6 &&
            f.provider->latest->values[1] == 1200,
        "transient callback overrides do not overwrite persistent base");
}

void identicalInstrumentSlotsStayIndependentAndMaximumCountWorks() {
  Fixture f;
  const auto first = f.provider->latest;
  CHECK(f.engine->loadModule(1, f.provider->descriptor_.id, nullptr, f.error),
        "same direct instrument loads in second slot");
  const auto second = f.provider->latest;
  auto c = f.controls();
  change(c[0], f.provider->descriptor_.id, "p000", -2);
  change(c[1], f.provider->descriptor_.id, "p000", -12);
  f.render(c);
  CHECK(first->values[0] == -2 && second->values[0] == -12,
        "same named key is isolated by slot, never shared runtime storage");
  const auto snapshot = f.engine->snapshotState(c);
  CHECK(named(snapshot, "p000") == -2, "first slot stores its own plain value");
  const auto found = std::find_if(snapshot[1].directParameters.begin(),
                                  snapshot[1].directParameters.end(),
                                  [](const auto &v) { return v.id == "p000"; });
  CHECK(found != snapshot[1].directParameters.end() && found->value == -12,
        "second slot stores its different plain value");
  Fixture maximum(false, maxDirectParameters);
  auto largest = maximum.controls();
  change(largest[0], maximum.provider->descriptor_.id, "p127", 0.625f);
  maximum.render(largest);
  CHECK(
      maximum.provider->latest->values[127] == 0.625f &&
          maximum.engine->snapshotState(largest)[0].directParameters.size() ==
              128,
      "128th independent parameter processes and persists at exact slot bound");
}

void directModulationDoesNotOverwriteStoredBase() {
  Fixture f(false, 12, true);
  auto controls = f.controls();
  change(controls[0], f.provider->descriptor_.id, "p001", 5000);
  const modulation::Route route{
      {modulation::SourceKind::envelope, 0},
      modulation::SourceTransform::direct,
      modulation::destinationKey(f.provider->descriptor_.id, "p001"),
      0.1f};
  CHECK(f.engine->configureModulation(0, {&route, 1}, f.error),
        "direct parameter is a real modulation destination");
  const std::array<VoxMidiEventV1, 1> midi{{{0, 3, {0x90, 60, 100}}}};
  std::array<float, frames> left{}, right{};
  std::array<float *, 2> output{left.data(), right.data()};
  f.engine->process(output, frames, midi, 145, 0, true, {}, 0, controls);
  CHECK(f.provider->latest->values[1] > 5000,
        "envelope modulation materially changes direct DSP value");
  CHECK(f.provider->latest->values[0] == -6,
        "direct modulation cannot overwrite an unrelated plain value");
  const auto saved = f.engine->snapshotState(controls);
  CHECK(
      named(saved, "p001") == 5000,
      "snapshot stores unmodulated direct base instead of transient DSP value");
  CHECK(!saved[0].modulationPayload.empty(),
        "modulation graph persists independently from direct bases");
  Fixture restored(false, 12, true);
  CHECK(restored.engine->replaceState(saved, nullptr, restored.error),
        "modulated direct state restores");
  restored.render();
  CHECK(restored.provider->latest->values[1] == 5000,
        "restored zero-envelope state starts at saved unmodulated base");
}

void runtimeCanonicalizationAndNonfiniteFallback() {
  Fixture f;
  auto c = f.controls();
  const auto &id = f.provider->descriptor_.id;
  change(c[0], id, "p000", 1000);
  change(c[0], id, "p001", -1);
  change(c[0], id, "p002", 2.7f);
  change(c[0], id, "p003", 0.7f);
  change(c[0], id, "p004", 1.7f);
  change(c[0], id, "p005", 0.37f);
  f.render(c);
  const std::array<std::pair<const char *, float>, 6> expected{
      {{"p000", 12},
       {"p001", 100},
       {"p002", 3},
       {"p003", 1},
       {"p004", 2},
       {"p005", 0.25f}}};
  const auto snapshot = f.engine->snapshotState(c);
  for (std::size_t i = 0; i < expected.size(); ++i) {
    CHECK(f.provider->latest->values[i] == expected[i].second,
          "runtime applies canonical range/step/type value");
    CHECK(named(snapshot, expected[i].first) == expected[i].second,
          "snapshot canonicalization matches actual DSP");
  }
  CHECK(f.provider->latest->rejected == 0,
        "off-grid automation never reaches strict provider as invalid plain "
        "values");
  change(c[0], id, "p000", std::numeric_limits<float>::quiet_NaN());
  change(c[0], id, "p001", std::numeric_limits<float>::infinity());
  f.render(c);
  CHECK(f.provider->latest->values[0] == -6 &&
            f.provider->latest->values[1] == 1200,
        "NaN/Inf overrides fall back to persistent base");
  const auto safe = f.engine->snapshotState(c);
  CHECK(named(safe, "p000") == -6 && named(safe, "p001") == 1200,
        "snapshot also falls back from nonfinite overrides");
}

void exactNamedStateRoundtripIgnoresOrder() {
  Fixture source;
  auto c = source.controls();
  change(c[0], source.provider->descriptor_.id, "p001", 7654.125f);
  change(c[0], source.provider->descriptor_.id, "p010", 0.375f);
  auto saved = source.engine->snapshotState(c);
  std::reverse(saved[0].directParameters.begin(),
               saved[0].directParameters.end());
  const auto tree = rack::state::serialize(saved);
  const auto xml = juce::XmlDocument::parse(tree.toXmlString());
  CHECK(xml != nullptr, "serialized direct state is real XML");
  State decoded;
  std::string error;
  CHECK(
      rack::state::deserialize(juce::ValueTree::fromXml(*xml), decoded, error),
      "direct state survives XML save/load");
  CHECK(encoded(decoded) == encoded(saved),
        "save/load/save is exact including named values and order");
  Fixture destination(true);
  CHECK(destination.engine->replaceState(decoded, nullptr, error),
        "reversed descriptor order restores named state");
  destination.render();
  CHECK(destination.provider->latest->values[1] == 7654.125f &&
            destination.provider->latest->values[10] == 0.375f,
        "restored values follow names, never ordinal positions");
  CHECK(encoded(destination.engine->snapshotState()) == encoded(decoded),
        "reload does not silently rewrite named base state");
}

void malformedPersistentStateIsTransactional() {
  Fixture f;
  const auto before = encoded(f.engine->snapshotState());
  const auto active = f.provider->latest;
  for (int kind = 0; kind < 6; ++kind) {
    auto candidate = f.engine->snapshotState();
    if (kind == 0)
      candidate[0].directParameters[1].id = candidate[0].directParameters[0].id;
    if (kind == 1)
      candidate[0].directParameters[0].id = "unknown.parameter";
    if (kind == 2)
      candidate[0].directParameters.pop_back();
    if (kind == 3)
      candidate[0].directParameters[0].value = -1000;
    if (kind == 4)
      candidate[0].directParameters[1].value =
          std::numeric_limits<float>::quiet_NaN();
    if (kind == 5)
      candidate[0].macroAssignments[0] = "p000";
    CHECK(!f.engine->replaceState(candidate, nullptr, f.error),
          "duplicate/unknown/missing/range/nonfinite/macro-owned direct state "
          "rejected");
    CHECK(encoded(f.engine->snapshotState()) == before,
          "rejected state leaves complete persistent state unchanged");
    const auto calls = active->processed;
    f.render();
    CHECK(active->values[0] == -6 && active->processed == calls + 1,
          "rejected transaction leaves the original live instance/base "
          "unchanged");
  }
  auto canonical = f.engine->snapshotState();
  namedSet(canonical, "p002", 2.7f);
  CHECK(f.engine->replaceState(canonical, nullptr, f.error),
        "in-range off-grid persistent value canonicalizes");
  CHECK(named(f.engine->snapshotState(), "p002") == 3,
        "canonical persistent integer committed as actual integer");
  f.render();
  CHECK(f.provider->latest->values[2] == 3,
        "canonical persistent value reaches DSP");
}

void malformedSerializedNamedValuesRejectWithoutMutation() {
  Fixture f;
  for (int kind = 0; kind < 6; ++kind) {
    auto tree = rack::state::serialize(f.engine->snapshotState());
    auto direct = tree.getChild(0).getChild(0);
    CHECK(direct.hasType("SOUND_PARAMETER"),
          "encoded direct parameter fixture exists");
    if (kind == 0)
      direct.setProperty("value", juce::String("-3junk"), nullptr);
    if (kind == 1)
      direct.setProperty("value", juce::String("NaN"), nullptr);
    if (kind == 2)
      direct.setProperty("value", juce::String("inf"), nullptr);
    if (kind == 3)
      direct.removeProperty("value", nullptr);
    if (kind == 4)
      direct.setProperty("value", true, nullptr);
    if (kind == 5)
      tree.getChild(0).getChild(1).setProperty("id", direct.getProperty("id"),
                                               nullptr);
    auto destination = f.engine->snapshotState();
    const auto before = encoded(destination);
    CHECK(!rack::state::deserialize(tree, destination, f.error),
          "malformed serialized direct scalar or duplicate name rejected");
    CHECK(encoded(destination) == before,
          "decoder failure is transactional for caller destination");
  }
}

void completePresetsCommitAndMalformedOnesRollBack() {
  Fixture f;
  auto &values = f.provider->contents[0].parameterValues;
  values[0].value = -3.5f;
  values[1].value = 6543.125f;
  values[2].value = 4.8f;
  values[11].value = 0.875f;
  std::reverse(values.begin(), values.end());
  CHECK(f.engine->applySoundPreset(0, "test.direct.preset", nullptr, f.error),
        "complete named direct preset applies without legacy macro preset "
        "handler");
  CHECK(f.provider->legacyPresetCalls == 0,
        "direct preset never falls through to legacy provider callback");
  f.render();
  CHECK(
      f.provider->latest->values[0] == -3.5f &&
          f.provider->latest->values[1] == 6543.125f &&
          f.provider->latest->values[2] == 5 &&
          f.provider->latest->values[11] == 0.875f,
      "all named preset values, including discrete and beyond-eight, applied");
  const auto before = encoded(f.engine->snapshotState());
  const auto validValues = values;
  const auto active = f.provider->latest;
  const auto liveValues = active->values;
  for (int kind = 0; kind < 8; ++kind) {
    // Simulate an invalid late-loaded content packet after provider
    // registration.
    values = validValues;
    f.provider->contents[0].macroValueMask = 0;
    f.provider->faults = {};
    if (kind == 0)
      values.pop_back();
    if (kind == 1)
      values[1].id = values[0].id;
    if (kind == 2)
      values[0].id = "not.registered";
    if (kind == 3)
      values[0].value = std::numeric_limits<float>::infinity();
    if (kind == 4)
      values[0].value = 1000000;
    if (kind == 5)
      f.provider->contents[0].macroValueMask = 1;
    if (kind == 6)
      f.provider->faults.save = true;
    if (kind == 7) {
      f.provider->faults.rejectLowGain = true;
      for (auto &v : values)
        if (v.id == "p000")
          v.value = -22;
    }
    CHECK(
        !f.engine->applySoundPreset(0, "test.direct.preset", nullptr, f.error),
        "invalid/incomplete/failing direct preset rejected");
    CHECK(encoded(f.engine->snapshotState()) == before,
          "preset failure cannot partially change state or preset identity");
    const auto calls = active->processed;
    f.render();
    CHECK(active->processed == calls + 1 && active->values == liveValues,
          "failed preset keeps the original live instance and all parameters "
          "intact");
  }
  f.provider->faults = {};
  values = validValues;
  f.provider->contents[0].macroValueMask = 0;
  CHECK(!f.engine->applySoundPreset(0, "missing.preset", nullptr, f.error),
        "unknown preset rejected");
  CHECK(encoded(f.engine->snapshotState()) == before,
        "unknown preset leaves state intact");
  auto unrelated = f.engine->snapshotState();
  unrelated[0].patternPreset = "pattern.keep";
  unrelated[0].patternPayload = {std::byte{0x5a}};
  CHECK(f.engine->replaceState(unrelated, nullptr, f.error),
        "pattern fixture installs");
  CHECK(f.engine->applySoundPreset(0, "test.direct.preset", nullptr, f.error),
        "valid sound preset still applies after invalid attempts");
  CHECK(f.engine->state()[0].patternPreset == "pattern.keep" &&
            f.engine->state()[0].patternPayload == unrelated[0].patternPayload,
        "Sound preset never overwrites Pattern state");
}

void nativePresetResourcesAreTransactionalAndNamedBasesAuthoritative() {
  Fixture f;
  auto &content = f.provider->contents[0];
  const auto native = [](float resource) {
    std::array<float, maxDirectParameters> raw{};
    raw[0] = 9999;
    raw[1] = -999;
    raw[127] = resource;
    std::vector<std::byte> bytes(sizeof(raw));
    std::memcpy(bytes.data(), raw.data(), bytes.size());
    return bytes;
  };
  // Parameter 127 is opaque native resource metadata in this 12-parameter fake
  // instrument.
  auto initial = f.engine->snapshotState();
  initial[0].modulePayload = native(55);
  CHECK(f.engine->replaceState(initial, nullptr, f.error),
        "initial native-resource state loads");
  f.render();
  CHECK(f.provider->latest->values[127] == 55 &&
            f.provider->latest->values[0] == -6,
        "named bases override native parameter bytes without losing resource "
        "identity");
  for (auto &value : content.parameterValues)
    if (value.id == "p000")
      value.value = -3.5f;
  content.nativeState.reset();
  content.nativeStateVersion = 99;
  CHECK(f.engine->applySoundPreset(0, content.id, nullptr, f.error),
        "absent native payload preserves existing native resources");
  f.render();
  CHECK(f.provider->latest->values[127] == 55 &&
            f.provider->latest->values[0] == -3.5f,
        "named-only preset preserves resources while replacing named bases");
  content.nativeState = native(99);
  content.nativeStateVersion = 1;
  CHECK(f.engine->applySoundPreset(0, content.id, nullptr, f.error),
        "versioned native-resource preset applies");
  f.render();
  CHECK(f.provider->latest->values[127] == 99 &&
            f.provider->latest->values[0] == -3.5f &&
            f.provider->latest->values[1] == 1200,
        "native resources change but opaque parameters cannot override "
        "complete named bases");
  auto controls = f.controls();
  change(controls[0], f.provider->descriptor_.id, "p000", -0.5f);
  auto automated = f.engine->snapshotState(controls);
  Fixture restored;
  CHECK(restored.engine->replaceState(automated, nullptr, restored.error),
        "automated named base plus native resources restores");
  restored.render();
  CHECK(restored.provider->latest->values[0] == -0.5f &&
            restored.provider->latest->values[127] == 99,
        "restored host automation wins over stale opaque parameter bytes");
  const auto before = encoded(f.engine->snapshotState());
  const auto active = f.provider->latest;
  for (int invalid = 0; invalid < 5; ++invalid) {
    content.nativeState = native(88);
    content.nativeStateVersion = 1;
    if (invalid == 0)
      content.nativeStateVersion = 0;
    if (invalid == 1)
      content.nativeStateVersion = 2;
    if (invalid == 2)
      content.nativeState->resize(sizeof(float) * maxDirectParameters + 1);
    if (invalid == 3)
      content.nativeState->resize(1);
    if (invalid == 4)
      content.nativeState = native(-1);
    CHECK(!f.engine->applySoundPreset(0, content.id, nullptr, f.error),
          "wrong-version/oversized/truncated/semantically corrupt native state "
          "rejects");
    CHECK(encoded(f.engine->snapshotState()) == before,
          "native preset rejection leaves previous complete state intact");
    const auto calls = active->processed;
    f.render();
    CHECK(active->processed == calls + 1 && active->values[127] == 99 &&
              active->values[0] == -3.5f,
          "native preset failure leaves original instance/resources sounding");
  }
  content.nativeState = std::vector<std::byte>{};
  content.nativeStateVersion = 1;
  CHECK(f.engine->applySoundPreset(0, content.id, nullptr, f.error),
        "present-empty native payload explicitly resets native Init");
  f.render();
  CHECK(f.provider->latest->values[127] == 0 &&
            f.provider->latest->values[0] == -3.5f,
        "native Init reset still preserves explicit named preset bases");
}

void unresolvedModulesPreserveNamedValuesWithBounds() {
  Fixture f;
  auto candidate = f.engine->snapshotState();
  candidate[0].instrumentId = "test.missing.instrument";
  namedSet(candidate, "p000", -3.5f);
  const auto identity = candidate[0].slotId;
  CHECK(f.engine->replaceState(candidate, nullptr, f.error),
        "missing direct module remains explicit unresolved state");
  CHECK(f.engine->runtimeState()[0].resolution ==
            ResolutionStatus::missingModule,
        "missing module is not substituted");
  CHECK(f.engine->state()[0].slotId == identity &&
            named(f.engine->snapshotState(), "p000") == -3.5f,
        "unresolved slot preserves named values and SlotId");
  State decoded;
  CHECK(
      rack::state::deserialize(
          rack::state::serialize(f.engine->snapshotState()), decoded, f.error),
      "unresolved named state roundtrips");
  CHECK(named(decoded, "p000") == -3.5f, "unknown module values remain exact");
  for (std::size_t i = decoded[0].directParameters.size();
       i <= maxDirectParameters; ++i)
    decoded[0].directParameters.push_back({parameterId(i), 0});
  CHECK(!f.engine->replaceState(decoded, nullptr, f.error),
        "missing module cannot bypass direct parameter count bound");
}

void metadataAndCatalogBounds() {
  for (int kind = 0; kind < 7; ++kind) {
    InstrumentRegistry registry;
    auto d = descriptor("test.invalid." + std::to_string(kind));
    if (kind == 0)
      d.parameters[0].preferredMacro = 0;
    if (kind == 1)
      d.parameters[0].automatable = false;
    if (kind == 2)
      d.parameters[1].id = d.parameters[0].id;
    if (kind == 3)
      d.parameters[0].step = std::numeric_limits<float>::quiet_NaN();
    if (kind == 4)
      d.parameters.clear();
    if (kind == 5)
      d = descriptor("test.too.many", maxDirectParameters + 1);
    if (kind == 6)
      d.parameters[0].type = static_cast<ParameterType>(255);
    std::string error;
    CHECK(!registry.registerProvider(std::make_unique<Provider>(std::move(d)),
                                     error),
          "invalid direct metadata is rejected before exposing host controls");
    CHECK(registry.descriptors().empty(),
          "bad provider publishes no descriptors");
  }
  InstrumentRegistry registry;
  std::string error;
  for (int i = 0; i < 4; ++i)
    CHECK(registry.registerProvider(
              std::make_unique<Provider>(descriptor(
                  "test.capacity." + std::to_string(i), maxDirectParameters)),
              error),
          "128 direct parameters per instrument and 512 catalog parameters "
          "accepted");
  CHECK(!registry.registerProvider(
            std::make_unique<Provider>(descriptor("test.capacity.overflow", 1)),
            error),
        "513th direct catalog parameter rejected");
  CHECK(registry.descriptors().size() == 4,
        "failed catalog expansion leaves prior instruments available");
  InstrumentRegistry collision;
  auto a = descriptor("test.names", 1);
  a.parameters[0].id = "nested.value";
  auto b = descriptor("test.names.nested", 1);
  b.parameters[0].id = "value";
  CHECK(collision.registerProvider(std::make_unique<Provider>(std::move(a)),
                                   error),
        "first unambiguous direct host name accepts");
  CHECK(!collision.registerProvider(std::make_unique<Provider>(std::move(b)),
                                    error),
        "dotted instrument/parameter host-name ambiguity rejected");
  CHECK(hostparams::directKey("test.a", "p000") !=
            hostparams::directKey("test.b", "p000"),
        "same parameter ID in different instruments has independent key");
  CHECK(hostparams::directId(0, "test.a", "p000") !=
            hostparams::directId(1, "test.a", "p000"),
        "each slot receives independent stable host endpoint");
  CHECK(hostparams::macroId(0, 0) == "slot01Macro01",
        "existing macro endpoint identity unchanged");
}

void unknownKeysAndProcessAreBoundedWithoutAllocation() {
  Fixture f;
  auto c = f.controls();
  const auto original = f.provider->latest->values;
  c[0].directParameters[c[0].directParameterCount++] = {
      hostparams::directKey("unknown.module", "p000"), 12};
  f.render(c);
  CHECK(f.provider->latest->values == original,
        "unknown instrument key cannot control this instrument");
  c[0].directParameterCount = std::numeric_limits<std::size_t>::max();
  CHECK(c[0].directValues().size() == maxDirectParameters,
        "corrupt callback count cannot expose memory past fixed array");
  f.render(c);
  CHECK(f.provider->latest->values == original,
        "bounded unknown control slots preserve legitimate bases");
  auto normal = f.controls();
  std::array<float, frames> left{}, right{};
  std::array<float *, 2> output{left.data(), right.data()};
  for (int i = 0; i < 4; ++i)
    f.engine->process(output, frames, {}, 145, 0, true, {}, 0, normal);
  direct_allocation_probe::count.store(0);
  direct_allocation_probe::active.store(true);
  for (int i = 0; i < 128; ++i) {
    normal[0].directParameters[1].value = 1200.0f + i;
    left.fill(0);
    right.fill(0);
    f.engine->process(output, frames, {}, 145, 0, true, {}, 0, normal);
  }
  direct_allocation_probe::active.store(false);
  CHECK(direct_allocation_probe::count.load() == 0,
        "warmed direct control callback uses no C++ heap allocations");
  CHECK(f.provider->latest->values[1] == 1327,
        "nominal changing direct automation actually processes while "
        "allocation probe active");
}
} // namespace

int main() {
  struct Test {
    const char *name;
    void (*run)();
  };
  const std::array<Test, 12> tests{
      {{"independent plain controls", independentPlainControlsIgnoreMacros},
       {"slot isolation/exact bound",
        identicalInstrumentSlotsStayIndependentAndMaximumCountWorks},
       {"direct modulation/base isolation",
        directModulationDoesNotOverwriteStoredBase},
       {"runtime canonicalization",
        runtimeCanonicalizationAndNonfiniteFallback},
       {"named state/order roundtrip", exactNamedStateRoundtripIgnoresOrder},
       {"persistent rollback", malformedPersistentStateIsTransactional},
       {"serialized malformed values",
        malformedSerializedNamedValuesRejectWithoutMutation},
       {"named preset transaction",
        completePresetsCommitAndMalformedOnesRollBack},
       {"native-resource preset authority",
        nativePresetResourcesAreTransactionalAndNamedBasesAuthoritative},
       {"missing module preservation",
        unresolvedModulesPreserveNamedValuesWithBounds},
       {"metadata/catalog bounds", metadataAndCatalogBounds},
       {"bounded allocation-free callback",
        unknownKeysAndProcessAreBoundedWithoutAllocation}}};
  int failed{};
  for (const auto &test : tests)
    try {
      test.run();
      std::cout << "PASS: " << test.name << '\n';
    } catch (const std::exception &e) {
      direct_allocation_probe::active.store(false);
      ++failed;
      std::cerr << "FAIL: " << test.name << ": " << e.what() << '\n';
    }
  std::cout << checks << " checks; " << failed << " failing scenarios\n";
  return failed ? 1 : 0;
}
