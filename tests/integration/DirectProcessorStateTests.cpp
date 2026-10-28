#include "PluginProcessor.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
using namespace vstengine;
constexpr const char *fixtureId = "test.direct.processor";
std::atomic<bool> slowCreate{}, creating{};
// Fault injection affects candidate construction only; current live instances
// stay observable.
std::atomic<int> factoryFault{};
std::atomic<unsigned> faultAttempts{};
class Signal final : public instrument::InstrumentInstance {
  float gain_{0.25f}, tone_{1000};
  bool bypass_{};
  bool future_{};

public:
  explicit Signal(bool future = false) : future_(future) {}
  bool prepare(const instrument::PrepareSpec &) override {
    if (factoryFault.load() == 3) {
      ++faultAttempts;
      return false;
    }
    return true;
  }
  void reset() noexcept override {}
  void suspend() noexcept override {}
  void resume() noexcept override {}
  void setBypassed(bool b) noexcept override { bypass_ = b; }
  void process(const instrument::ProcessBlock &b) noexcept override {
    if (bypass_)
      return;
    for (std::uint32_t s = 0; s < b.sampleCount; ++s) {
      b.outputs[0][s] = gain_ * 0.1f;
      if (b.outputs.size() > 1)
        b.outputs[1][s] = tone_ / 20000.0f;
    }
  }
  bool setParameter(std::string_view id, float v) noexcept override {
    if (id == "gain" && std::isfinite(v) && v >= 0 &&
        v <= (future_ ? 2.0f : 1.0f)) {
      gain_ = v;
      return true;
    }
    if (id == "tone" && std::isfinite(v) && v >= 20 && v <= 20000) {
      tone_ = v;
      return true;
    }
    if (id == "drive" && std::isfinite(v) && v >= 0 && v <= 1)
      return true; // only declared by the extended revision

    return false;
  }
  bool loadState(std::uint32_t v,
                 std::span<const std::byte> bytes) noexcept override {
    if (v != (future_ ? 2u : 1u))
      return false;
    if (bytes.empty())
      return true;
    if (bytes.size() != 8)
      return false;
    std::memcpy(&gain_, bytes.data(), 4);
    std::memcpy(&tone_, bytes.data() + 4, 4);
    return true;
  }
  bool saveState(std::span<std::byte> bytes,
                 std::uint32_t &n) const noexcept override {
    if (bytes.size() < 8)
      return false;
    std::memcpy(bytes.data(), &gain_, 4);
    std::memcpy(bytes.data() + 4, &tone_, 4);
    n = 8;
    return true;
  }
  std::uint32_t latencySamples() const noexcept override { return 0; }
  std::uint32_t tailSamples() const noexcept override { return 0; }
};
// Later builds of the same InstrumentId: parameters listed in reverse order,
// optionally with a new parameter inserted first (with or without the
// instrumentVersion bump that a changed parameter set requires).
enum class Revision { v1, reordered, extended, extendedUnversioned };
class Provider final : public instrument::InstrumentProvider {
  instrument::InstrumentDescriptor descriptor_;
  std::array<instrument::ContentDescriptor, 1> contents_;
  bool future_{};

public:
  explicit Provider(bool future = false, Revision revision = Revision::v1)
      : future_(future) {
    auto &d = descriptor_;
    d.id = fixtureId;
    d.providerId = "test.static.provider";
    d.name = "Static signal probe";
    d.vendor = "Tests";
    d.directParameters = true;
    d.budget.maxVoices = 1;
    d.budget.maxMidiEventsPerBlock = 2048;
    d.budget.maxStateBytes = 8;
    d.parameters = {{"gain",
                     "Gain",
                     "",
                     0,
                     1,
                     0.25f,
                     0.0001f,
                     instrument::ParameterType::floating,
                     "Output",
                     {},
                     true,
                     true,
                     -1,
                     0},
                    {"tone",
                     "Tone",
                     "Hz",
                     20,
                     20000,
                     1000,
                     1,
                     instrument::ParameterType::floating,
                     "Filter",
                     {},
                     true,
                     true,
                     -1,
                     1}};
    if (revision != Revision::v1)
      std::reverse(d.parameters.begin(), d.parameters.end());
    if (revision == Revision::extended ||
        revision == Revision::extendedUnversioned) {
      auto drive = d.parameters.front();
      drive.id = "drive";
      drive.name = "Drive";
      drive.unit = "";
      drive.minimum = 0;
      drive.maximum = 1;
      drive.defaultValue = 0;
      drive.step = 0;
      drive.poolIndex = 2;
      d.parameters.insert(d.parameters.begin(), drive);
      if (revision == Revision::extended)
        d.instrumentVersion = 2;
    }
    if (future_) {
      d.stateVersion = 2;
      d.parameters[0].maximum = 2.0f;
    }
    auto &preset = contents_[0];
    preset.id = "named";
    preset.instrumentId = fixtureId;
    preset.name = "Named";
    preset.parameterValues = {{"gain", 0.55f}, {"tone", 4321}};
  }
  std::span<const instrument::InstrumentDescriptor>
  descriptors() const noexcept override {
    return {&descriptor_, 1};
  }
  std::unique_ptr<instrument::InstrumentInstance>
  create(std::string_view id, const instrument::CreateContext &) override {
    if (id != fixtureId)
      return {};
    if (factoryFault.load() == 1) {
      ++faultAttempts;
      return {};
    }
    if (factoryFault.load() == 2) {
      ++faultAttempts;
      throw std::runtime_error("injected provider factory failure");
    }
    if (slowCreate.load()) {
      creating.store(true);
      while (slowCreate.load())
        std::this_thread::yield();
      creating.store(false);
    }
    return std::make_unique<Signal>(future_);
  }
  std::span<const instrument::ContentDescriptor>
  contentDescriptors(std::string_view id) const noexcept override {
    return id == fixtureId
               ? std::span<const instrument::ContentDescriptor>(contents_)
               : std::span<const instrument::ContentDescriptor>{};
  }
};
int checks{};
void check(bool b, const char *message) {
  ++checks;
  if (!b) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
// The fixture binds to the slot's pool in descriptor order: gain, tone.
std::string id(std::size_t slot, std::string_view name) {
  return instrument::hostparams::poolId(slot, name == "gain" ? 0 : 1);
}
void set(VstEngineAudioProcessor &p, const std::string &key, float v) {
  auto *a = p.parameters().getParameter(key);
  check(a != nullptr, "real host endpoint exists");
  a->setValueNotifyingHost(a->convertTo0to1(v));
}
float raw(VstEngineAudioProcessor &p, const std::string &key) {
  return p.parameters().getRawParameterValue(key)->load();
}
float saved(const VstEngineAudioProcessor &p, std::size_t slot,
            std::string_view name) {
  for (const auto &v : p.instrumentRack().state()[slot].directParameters)
    if (v.id == name)
      return v.value;
  check(false, "named state present");
  return 0;
}
std::array<float, 2> render(VstEngineAudioProcessor &p) {
  juce::AudioBuffer<float> audio(2, 64);
  juce::MidiBuffer midi;
  p.processBlock(audio, midi);
  return {audio.getSample(0, 63), audio.getSample(1, 63)};
}

juce::MemoryBlock capture(VstEngineAudioProcessor &processor) {
  juce::MemoryBlock result;
  processor.getStateInformation(result);
  return result;
}

bool sameBytes(const juce::MemoryBlock &a, const juce::MemoryBlock &b) {
  return a.getSize() == b.getSize() &&
         (a.getSize() == 0 ||
          std::memcmp(a.getData(), b.getData(), a.getSize()) == 0);
}

juce::ValueTree decodeProject(const juce::MemoryBlock &bytes) {
  const auto xml = juce::AudioProcessor::getXmlFromBinary(
      bytes.getData(), static_cast<int>(bytes.getSize()));
  check(xml != nullptr,
        "rollback fixture is a real processor binary/XML project");
  auto result = juce::ValueTree::fromXml(*xml);
  check(result.hasType("PARAMETERS") &&
            result.getChildWithName("RACK").isValid(),
        "rollback fixture includes valid APVTS and Rack state");
  return result;
}

juce::MemoryBlock encodeProject(const juce::ValueTree &project) {
  const auto xml = project.createXml();
  check(xml != nullptr, "mutated test project encodes as XML");
  juce::MemoryBlock result;
  juce::AudioProcessor::copyXmlToBinary(*xml, result);
  return result;
}

juce::ValueTree parameterChild(const juce::ValueTree &project,
                               const std::string &parameterId) {
  for (const auto child : project)
    if (child.hasType("PARAM") &&
        child.getProperty("id").toString() == juce::String(parameterId))
      return child;
  check(false, "mutated APVTS parameter exists in actual processor project");
  return {};
}

// Named Rack value of a saved slot: the authoritative source for pool endpoints.
juce::ValueTree namedChild(const juce::ValueTree &project, int slot,
                           std::string_view name) {
  for (const auto child : project.getChildWithName("RACK").getChild(slot))
    if (child.hasType("SOUND_PARAMETER") &&
        child.getProperty("id").toString() == juce::String(std::string(name)))
      return child;
  check(false, "named Rack value exists in actual processor project");
  return {};
}

void rejectedRestoreKeepsLiveState(VstEngineAudioProcessor &processor,
                                   const juce::MemoryBlock &incoming,
                                   const juce::MemoryBlock &expectedState,
                                   const std::array<float, 2> &expectedSound) {
  const std::array<std::string, 7> observedIds{
      id(0, "gain"), id(0, "tone"), id(1, "gain"), id(1, "tone"),
      "outputLevel", "rngSeed",     "midiMode"};
  std::array<float, 7> beforeParameters{};
  for (std::size_t i = 0; i < observedIds.size(); ++i)
    beforeParameters[i] = raw(processor, observedIds[i]);
  bool escaped = false;
  try {
    processor.setStateInformation(incoming.getData(),
                                  static_cast<int>(incoming.getSize()));
  } catch (...) {
    escaped = true;
  }
  // Further inspection cannot be influenced by the injected provider fault.
  factoryFault.store(0);
  check(!escaped, "failed project factory/prepare/load cannot throw out of "
                  "host state restore");
  for (std::size_t i = 0; i < observedIds.size(); ++i)
    check(raw(processor, observedIds[i]) == beforeParameters[i],
          "rejected project retains every observed live host parameter");
  check(sameBytes(capture(processor), expectedState),
        "rejected project preserves complete serialized Rack/APVTS/FX/pattern "
        "state");
  check(render(processor) == expectedSound,
        "rejected project preserves exact audible signal, not only parameter "
        "metadata");
}

void factoryFailureRollback(VstEngineAudioProcessor &target,
                            const juce::MemoryBlock &validIncoming) {
  const auto before = capture(target);
  const auto sound = render(target);
  for (int mode : {1, 2, 3}) {
    faultAttempts.store(0);
    factoryFault.store(mode);
    rejectedRestoreKeepsLiveState(target, validIncoming, before, sound);
    check(faultAttempts.load() > 0,
          "valid project reached the fault-injected known provider path");
  }
  // The same previously rejected bytes must load when the provider recovers.
  // Use a separate processor so the caller's concurrency/scene tests keep their
  // original live state.
  auto recovered =
      std::make_unique<VstEngineAudioProcessor>(std::make_unique<Provider>());
  recovered->prepareToPlay(48000, 64);
  recovered->setStateInformation(validIncoming.getData(),
                                 static_cast<int>(validIncoming.getSize()));
  check(raw(*recovered, id(0, "tone")) == 7654 &&
            saved(*recovered, 0, "tone") == 7654,
        "recovered provider can restore the exact valid project rejected by "
        "injected failures");
}

void malformedApvtsRollback(VstEngineAudioProcessor &target,
                            const juce::MemoryBlock &validIncoming) {
  const auto original = decodeProject(validIncoming);
  const auto goodRack = original.getChildWithName("RACK").toXmlString();
  const auto before = capture(target);
  const auto sound = render(target);
  struct ScalarCase {
    std::string parameter;
    juce::var value;
  };
  const std::array<ScalarCase, 7> invalid{
      {{"outputLevel", juce::String("0.4junk")},
       {"outputLevel", juce::String("NaN")},
       {"outputLevel", juce::String("inf")},
       {"outputLevel", juce::var(2.5)},
       {"outputLevel", juce::var(-0.1)},
       {id(0, "gain"), juce::String("NaN")},
       {"rngSeed", juce::var(16777216)}}};
  for (const auto &test : invalid) {
    auto project = original.createCopy();
    parameterChild(project, test.parameter)
        .setProperty("value", test.value, nullptr);
    check(project.getChildWithName("RACK").toXmlString() == goodRack,
          "scalar corruption leaves the complete good Rack payload unchanged");
    rejectedRestoreKeepsLiveState(target, encodeProject(project), before,
                                  sound);
  }
  {
    // Out-of-range named value of an active module: the authoritative source
    // is validated against its descriptor (the pool PARAM only mirrors it).
    auto project = original.createCopy();
    namedChild(project, 0, "gain").setProperty("value", 1.5, nullptr);
    rejectedRestoreKeepsLiveState(target, encodeProject(project), before,
                                  sound);
  }
  for (const auto &parameterId :
       {std::string("rootNote"), std::string("slot01MidiIn")}) {
    auto project = original.createCopy();
    project.removeChild(parameterChild(project, parameterId), nullptr);
    check(project.getChildWithName("RACK").toXmlString() == goodRack,
          "missing core PARAM fixture leaves the complete good Rack payload "
          "unchanged");
    rejectedRestoreKeepsLiveState(target, encodeProject(project), before,
                                  sound);
  }
  for (const auto &parameterId : {std::string("outputLevel"), id(0, "gain")}) {
    auto project = original.createCopy();
    auto duplicate = parameterChild(project, parameterId).createCopy();
    duplicate.setProperty("value", parameterId == "outputLevel" ? 0.9f : 0.1f,
                          nullptr);
    project.appendChild(duplicate, nullptr);
    check(project.getChildWithName("RACK").toXmlString() == goodRack,
          "duplicate APVTS ID fixture leaves the complete good Rack payload "
          "unchanged");
    rejectedRestoreKeepsLiveState(target, encodeProject(project), before,
                                  sound);
  }
}

void incompatibleModuleStateRemainsRecoverable(
    const juce::MemoryBlock &validIncoming) {
  auto project = decodeProject(validIncoming);
  auto slot = project.getChildWithName("RACK").getChild(0);
  slot.setProperty("instrumentStateVersion", 2, nullptr);
  for (auto child : slot)
    if (child.hasType("SOUND_PARAMETER") &&
        child.getProperty("id").toString() == "gain")
      child.setProperty("value", 0.12345f, nullptr);
  const std::array<std::byte, 3> opaque{std::byte{0x5a}, std::byte{0x7f},
                                        std::byte{0x01}};
  const juce::MemoryBlock payload(opaque.data(), opaque.size());
  slot.setProperty("modulePayload", payload.toBase64Encoding(), nullptr);
  const auto incoming = encodeProject(project);
  auto processor =
      std::make_unique<VstEngineAudioProcessor>(std::make_unique<Provider>());
  processor->prepareToPlay(48000, 64);
  processor->setStateInformation(incoming.getData(),
                                 static_cast<int>(incoming.getSize()));
  check(processor->instrumentRack().runtimeState()[0].resolution ==
            instrument::ResolutionStatus::incompatibleSchema,
        "newer module schema is preserved as explicitly unresolved rather than "
        "confused with factory failure");
  const auto &retained = processor->instrumentRack().state()[0];
  check(retained.instrumentId == fixtureId &&
            retained.instrumentStateVersion == 2 &&
            retained.modulePayload ==
                std::vector<std::byte>(opaque.begin(), opaque.end()),
        "incompatible module keeps exact identity/version/opaque payload for "
        "future recovery");
  check(saved(*processor, 0, "gain") == 0.12345f,
        "incompatible named value is preserved exactly without "
        "current-descriptor step rounding");
  check(render(*processor) == std::array<float, 2>{0, 0},
        "unresolved incompatible module never silently substitutes a live "
        "instrument");
}

void futureParameterDomainRemainsOpaque(
    const juce::MemoryBlock &validIncoming) {
  auto future = decodeProject(validIncoming);
  auto slot = future.getChildWithName("RACK").getChild(0);
  slot.setProperty("instrumentStateVersion", 2, nullptr);
  constexpr float futureGain = 1.5f;
  for (auto child : slot)
    if (child.hasType("SOUND_PARAMETER") &&
        child.getProperty("id").toString() == "gain")
      child.setProperty("value", futureGain, nullptr);
  auto hostGain = parameterChild(future, id(0, "gain"));
  check(hostGain.isValid(),
        "future parameter fixture contains the actual installed host endpoint");
  hostGain.setProperty("value", futureGain, nullptr);
  const std::array<float, 2> native{futureGain, 7654.0f};
  const juce::MemoryBlock nativeBytes(native.data(), sizeof(native));
  slot.setProperty("modulePayload", nativeBytes.toBase64Encoding(), nullptr);
  const auto incoming = encodeProject(future);

  auto older =
      std::make_unique<VstEngineAudioProcessor>(std::make_unique<Provider>());
  older->prepareToPlay(48000, 64);
  older->setStateInformation(incoming.getData(),
                             static_cast<int>(incoming.getSize()));
  const auto retained = older->instrumentRack().state()[0];
  check(retained.instrumentId == fixtureId &&
            retained.instrumentStateVersion == 2,
        "newer module domain is retained rather than rejecting the entire "
        "project against old parameter ranges");
  check(older->instrumentRack().runtimeState()[0].resolution ==
            instrument::ResolutionStatus::incompatibleSchema,
        "newer parameter domain remains explicitly unresolved under old "
        "provider");
  check(saved(*older, 0, "gain") == futureGain,
        "future named value outside old range is preserved exactly without "
        "clamping");
  check(retained.modulePayload ==
            std::vector<std::byte>(
                static_cast<const std::byte *>(nativeBytes.getData()),
                static_cast<const std::byte *>(nativeBytes.getData()) +
                    nativeBytes.getSize()),
        "future native module payload remains byte-identical");
  check(raw(*older, id(0, "gain")) == 0.0f &&
            older->parameters().getParameter(id(0, "gain"))->getName(64) ==
                "Slot 1 Param 01",
        "dormant pool endpoint is unbound and neutral instead of the future "
        "value");
  check(render(*older) == std::array<float, 2>{0, 0},
        "unresolved future-domain slot remains silent");

  const auto resavedBytes = capture(*older);
  const auto resaved = decodeProject(resavedBytes);
  const auto resavedSlot = resaved.getChildWithName("RACK").getChild(0);
  bool resavedFuture = false;
  for (const auto child : resavedSlot)
    resavedFuture |= child.hasType("SOUND_PARAMETER") &&
                     child.getProperty("id").toString() == "gain" &&
                     static_cast<float>(static_cast<double>(
                         child.getProperty("value"))) == futureGain;
  check(resavedFuture,
        "re-save retains the named future value outside the installed range");
  check(resavedSlot.getProperty("modulePayload").toString() ==
            nativeBytes.toBase64Encoding(),
        "re-save retains native payload for an upgraded provider");

  // A future domain in one slot must not relax validation for another active
  // slot that still belongs to the current installed descriptor.
  auto mixed = future.createCopy();
  auto mixedRack = mixed.getChildWithName("RACK");
  auto active = decodeProject(validIncoming)
                    .getChildWithName("RACK")
                    .getChild(0)
                    .createCopy();
  active.setProperty("slotId", mixedRack.getChild(1).getProperty("slotId"),
                     nullptr);
  active.setProperty("channel", 2, nullptr);
  mixedRack.removeChild(1, nullptr);
  mixedRack.addChild(active, 1, nullptr);
  parameterChild(mixed, id(1, "gain")).setProperty("value", 0.73f, nullptr);
  parameterChild(mixed, id(1, "tone")).setProperty("value", 7654.0f, nullptr);
  const auto mixedBytes = encodeProject(mixed);
  older->setStateInformation(mixedBytes.getData(),
                             static_cast<int>(mixedBytes.getSize()));
  check(older->instrumentRack().runtimeState()[0].resolution ==
                instrument::ResolutionStatus::incompatibleSchema &&
            older->instrumentRack().runtimeState()[1].resolution ==
                instrument::ResolutionStatus::resolved,
        "future unresolved and current active module domains coexist in one "
        "valid project");
  const auto mixedCommitted = capture(*older);
  const auto mixedSound = render(*older);
  auto badActive = mixed.createCopy();
  namedChild(badActive, 1, "gain").setProperty("value", 1.5f, nullptr);
  rejectedRestoreKeepsLiveState(*older, encodeProject(badActive),
                                mixedCommitted, mixedSound);
  auto malformedOpaque = mixed.createCopy();
  parameterChild(malformedOpaque, id(0, "gain"))
      .setProperty("value", "NaN", nullptr);
  rejectedRestoreKeepsLiveState(*older, encodeProject(malformedOpaque),
                                mixedCommitted, mixedSound);
  // A pool PARAM only mirrors the named value: an out-of-range mirror loads,
  // but the live endpoint takes the validated named value.
  auto mirrorOnly = mixed.createCopy();
  parameterChild(mirrorOnly, id(1, "gain")).setProperty("value", 1.5f, nullptr);
  const auto mirrorBytes = encodeProject(mirrorOnly);
  older->setStateInformation(mirrorBytes.getData(),
                             static_cast<int>(mirrorBytes.getSize()));
  check(std::abs(raw(*older, id(1, "gain")) - saved(*older, 1, "gain")) < 1e-6f &&
            raw(*older, id(1, "gain")) <= 1.0f,
        "pool PARAM mirror cannot inject a value past the named authority");
  check(decodeProject(capture(*older)).getChildWithName("RACK").toXmlString() ==
                decodeProject(mixedCommitted).getChildWithName("RACK").toXmlString() &&
            std::abs(raw(*older, id(1, "gain")) - 0.73f) < 1e-6f,
        "mirror-only difference restores the identical committed Rack state");

  juce::String resetDiagnostic;
  check(older->loadSlotInstrument(0, fixtureId, resetDiagnostic),
        "explicit current-module replacement can reset the unresolved future "
        "slot");
  set(*older, id(0, "gain"), 0.6f);
  const auto currentAgain = decodeProject(capture(*older));
  check(
      static_cast<float>(static_cast<double>(
          parameterChild(currentAgain, id(0, "gain")).getProperty("value"))) ==
          raw(*older, id(0, "gain")),
      "old opaque host value never overwrites live bases after explicit "
      "compatible module replacement");

  auto newer = std::make_unique<VstEngineAudioProcessor>(
      std::make_unique<Provider>(true));
  newer->prepareToPlay(48000, 64);
  newer->setStateInformation(resavedBytes.getData(),
                             static_cast<int>(resavedBytes.getSize()));
  check(newer->instrumentRack().runtimeState()[0].resolution ==
            instrument::ResolutionStatus::resolved,
        "compatible upgraded provider reactivates the retained future module");
  check(raw(*newer, id(0, "gain")) == futureGain &&
            saved(*newer, 0, "gain") == futureGain,
        "upgraded endpoint and named authority recover the exact future base "
        "value");
  const auto sound = render(*newer);
  check(std::abs(sound[0] - 0.06f) < 1e-6f &&
            std::abs(sound[1] - 0.15308f) < 1e-6f,
        "reactivated provider renders its retained native/named state at saved "
        "master gain");
}

// v1 -> v2 of the SAME InstrumentId across separately constructed processors.
void sameInstrumentUpgrade() {
  auto v1 = std::make_unique<VstEngineAudioProcessor>(std::make_unique<Provider>());
  v1->prepareToPlay(48000, 64);
  juce::String loaded;
  check(v1->loadSlotInstrument(0, fixtureId, loaded), "v1 module loads");
  set(*v1, id(0, "gain"), 0.6f);
  set(*v1, id(0, "tone"), 5000);
  const auto v1Sound = render(*v1);
  const auto v1Project = capture(*v1);

  // Reordered descriptor, same version: exact restore on the same endpoints.
  auto reordered = std::make_unique<VstEngineAudioProcessor>(
      std::make_unique<Provider>(false, Revision::reordered));
  reordered->prepareToPlay(48000, 64);
  reordered->setStateInformation(v1Project.getData(),
                                 static_cast<int>(v1Project.getSize()));
  const auto *gain = reordered->parameters().getParameter(id(0, "gain"));
  const auto *tone = reordered->parameters().getParameter(id(0, "tone"));
  check(reordered->instrumentRack().runtimeState()[0].resolution ==
                instrument::ResolutionStatus::resolved &&
            id(0, "gain") == "slot01.p001" && id(0, "tone") == "slot01.p002",
        "v1 project resolves under the reordered v2 on the original endpoint IDs");
  check(gain->getName(64) == "Slot 1 Gain" && tone->getName(64) == "Slot 1 Tone" &&
            tone->getLabel() == "Hz" && !gain->isDiscrete() && !tone->isDiscrete(),
        "reordered v2 keeps endpoint names, units and host types");
  check(std::abs(raw(*reordered, id(0, "gain")) - 0.6f) < 1e-6f &&
            raw(*reordered, id(0, "tone")) == 5000 &&
            std::abs(saved(*reordered, 0, "gain") - 0.6f) < 1e-6f &&
            saved(*reordered, 0, "tone") == 5000,
        "reordered v2 restores the v1 values on the same endpoints");
  check(render(*reordered) == v1Sound,
        "reordered v2 restores identical audio controls");

  // A changed parameter set bumps instrumentVersion. There is no automatic
  // migration: the v1 slot stays unresolved with its values preserved.
  auto extended = std::make_unique<VstEngineAudioProcessor>(
      std::make_unique<Provider>(false, Revision::extended));
  extended->prepareToPlay(48000, 64);
  extended->setStateInformation(v1Project.getData(),
                                static_cast<int>(v1Project.getSize()));
  check(extended->instrumentRack().runtimeState()[0].resolution !=
                instrument::ResolutionStatus::resolved &&
            extended->instrumentRack().state()[0].instrumentVersion == 1,
        "extended v2 does not pass off the v1 snapshot as a silent upgrade");
  check(std::abs(saved(*extended, 0, "gain") - 0.6f) < 1e-6f &&
            saved(*extended, 0, "tone") == 5000 &&
            extended->parameters().getParameter(id(0, "gain"))->getName(64) ==
                "Slot 1 Param 01" &&
            render(*extended) == std::array<float, 2>{0, 0},
        "unmigrated v1 values are preserved while the slot stays silent");
  const auto kept = capture(*extended);
  auto back = std::make_unique<VstEngineAudioProcessor>(std::make_unique<Provider>());
  back->prepareToPlay(48000, 64);
  back->setStateInformation(kept.getData(), static_cast<int>(kept.getSize()));
  check(std::abs(raw(*back, id(0, "gain")) - 0.6f) < 1e-6f &&
            raw(*back, id(0, "tone")) == 5000,
        "re-saving under the extended v2 keeps the v1 slot recoverable");

  // Changing the parameter set without a version bump is a provider error:
  // the whole project is rejected and the live state stays intact.
  auto unversioned = std::make_unique<VstEngineAudioProcessor>(
      std::make_unique<Provider>(false, Revision::extendedUnversioned));
  unversioned->prepareToPlay(48000, 64);
  const auto before = capture(*unversioned);
  unversioned->setStateInformation(v1Project.getData(),
                                   static_cast<int>(v1Project.getSize()));
  check(sameBytes(capture(*unversioned), before),
        "unversioned parameter-set change rejects the v1 project transactionally");
}
} // namespace
int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  sameInstrumentUpgrade();
  auto a =
      std::make_unique<VstEngineAudioProcessor>(std::make_unique<Provider>());
  auto b =
      std::make_unique<VstEngineAudioProcessor>(std::make_unique<Provider>());
  for (auto *p : {a.get(), b.get()})
    p->prepareToPlay(48000, 64);
  juce::String diagnostic;
  check(a->loadSlotInstrument(0, fixtureId, diagnostic),
        "static direct module loads in actual processor");
  set(*a, id(0, "gain"), 0.73f);
  set(*a, id(0, "tone"), 7654);
  set(*a, "midiMode", 1);
  set(*a, "rngSeed", static_cast<float>(instrument::maxGenerationSeed));
  check(raw(*a, "rngSeed") == 16777215.0f,
        "maximum seed is exactly representable and safely round-trips");
  check(a->getLatencySamples() == 0,
        "production processor declares zero latency");
  const auto unity = render(*a);
  check(std::abs(unity[0] - 0.073f) < 1e-5f &&
            std::abs(unity[1] - 7654.0f / 20000.0f) < 1e-5f,
        "named edits reach effective DSP independently");
  set(*a, "outputLevel", 0.4f);
  for (int i = 0; i < 5; ++i)
    (void)render(*a);
  const auto before = render(*a);
  juce::MemoryBlock bytes;
  a->getStateInformation(bytes);
  b->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
  check(raw(*b, id(0, "gain")) == raw(*a, id(0, "gain")) &&
            raw(*b, id(0, "tone")) == raw(*a, id(0, "tone")),
        "APVTS -> Rack snapshot -> XML -> restore retains live edits");
  check(saved(*b, 0, "gain") == raw(*b, id(0, "gain")) &&
            saved(*b, 0, "tone") == 7654,
        "restored named authority agrees with host");
  const auto after = render(*b);
  check(before == after, "actual processor rerender is sample-identical");
  check(b->loadSlotInstrument(1, fixtureId, diagnostic),
        "second instance loads");
  set(*b, id(1, "gain"), 0.81f);
  b->selectSlot(0);
  check(b->applySelectedSoundPreset("named", diagnostic),
        "complete named sound preset applies");
  check(std::abs(raw(*b, id(0, "gain")) - 0.55f) < 1e-5f &&
            raw(*b, id(0, "tone")) == 4321,
        "preset updates only its named bases");
  check(raw(*b, id(1, "gain")) > 0.80f,
        "preset preserves another slot's live edit");
  const auto sceneName = "RackContract-" +
                         juce::String(juce::Time::currentTimeMillis()) + "-" +
                         juce::Uuid().toString().substring(0, 8);
  check(static_cast<bool>(b->presetManager()->saveFullPreset(sceneName)),
        "actual complete Rack scene saves");
  const auto sceneGain = raw(*b, id(0, "gain"));
  const auto sceneOther = raw(*b, id(1, "gain"));
  set(*b, id(0, "gain"), 0.12f);
  set(*b, id(1, "gain"), 0.22f);
  check(static_cast<bool>(b->presetManager()->loadFullPreset(sceneName)),
        "actual complete scene restores transactionally");
  check(raw(*b, id(0, "gain")) == sceneGain &&
            raw(*b, id(1, "gain")) == sceneOther,
        "scene authority restores both live named slots");
  for (const auto &entry : b->presetManager()->getPresets())
    if (entry.name == sceneName) {
      const auto original = entry.file.loadFileAsString();
      check(original.contains("rack-project-v5"),
            "complete scene uses explicit modern format");
      check(entry.file.replaceWithText(original.replace(
                "stateSchemaVersion=\"5\"", "stateSchemaVersion=\"4\"")),
            "test scene corruption writes only its own new file");
      check(!b->presetManager()->loadFullPreset(sceneName),
            "obsolete POC scene rejected");
      check(raw(*b, id(0, "gain")) == sceneGain,
            "failed scene preserves current live bases");
      check(entry.file.deleteFile(), "temporary scene cleaned up");
    }
  const auto fingerprint = saved(*b, 0, "tone");
  const std::array<std::byte, 8> invalid{};
  b->setStateInformation(invalid.data(), static_cast<int>(invalid.size()));
  check(saved(*b, 0, "tone") == fingerprint,
        "invalid project rolls back without changing named state");
  factoryFailureRollback(*b, bytes);
  malformedApvtsRollback(*b, bytes);
  incompatibleModuleStateRemainsRecoverable(bytes);
  futureParameterDomainRemainsOpaque(bytes);
  // A deliberately slow controller construction must not race a direct
  // processBlock caller or leave the gate permanently closed.
  slowCreate.store(true);
  std::thread control([&] {
    juce::String d;
    check(b->loadSlotInstrument(2, fixtureId, d),
          "concurrent module transaction succeeds");
  });
  while (!creating.load())
    std::this_thread::yield();
  check(render(*b) == std::array<float, 2>{0, 0},
        "overlapping callback fails closed without a lock");
  slowCreate.store(false);
  control.join();
  const auto resumed = render(*b);
  check(std::isfinite(resumed[0]) && resumed[0] > 0,
        "RAII gate reopens after transaction");
  b->releaseResources();
  check(render(*b) == std::array<float, 2>{0, 0}, "release remains silent");
  b->prepareToPlay(48000, 64);
  check(render(*b)[0] > 0, "reprepare recovers named modules");
  std::cout << "Direct processor state tests passed (" << checks << ")\n";
}
