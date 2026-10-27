#include "PluginProcessor.h"
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

public:
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
    if (id == "gain" && std::isfinite(v) && v >= 0 && v <= 1) {
      gain_ = v;
      return true;
    }
    if (id == "tone" && std::isfinite(v) && v >= 20 && v <= 20000) {
      tone_ = v;
      return true;
    }
    return false;
  }
  bool loadState(std::uint32_t v,
                 std::span<const std::byte> bytes) noexcept override {
    if (v != 1)
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
class Provider final : public instrument::InstrumentProvider {
  instrument::InstrumentDescriptor descriptor_;
  std::array<instrument::ContentDescriptor, 1> contents_;

public:
  Provider() {
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
                     -1},
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
                     -1}};
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
    return std::make_unique<Signal>();
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
std::string id(std::size_t slot, std::string_view name) {
  return instrument::hostparams::directId(slot, fixtureId, name);
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
  const std::array<ScalarCase, 8> invalid{
      {{"outputLevel", juce::String("0.4junk")},
       {"outputLevel", juce::String("NaN")},
       {"outputLevel", juce::String("inf")},
       {"outputLevel", juce::var(2.5)},
       {"outputLevel", juce::var(-0.1)},
       {id(0, "gain"), juce::String("NaN")},
       {id(0, "gain"), juce::var(1.5)},
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

} // namespace
int main() {
  juce::ScopedJuceInitialiser_GUI gui;
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
