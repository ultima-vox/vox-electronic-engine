#include "DirectParameterBank.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
namespace {
using namespace vstengine;
int checks{};
void require(bool value, const char *text) {
  ++checks;
  if (!value) {
    std::cerr << "FAIL: " << text << '\n';
    std::exit(1);
  }
}
instrument::InstrumentDescriptor descriptor(std::string id) {
  instrument::InstrumentDescriptor d;
  d.id = std::move(id);
  d.name = "Direct fixture";
  d.providerId = "com.ultimavox.test";
  d.vendor = "Ultima Vox";
  d.directParameters = true;
  d.parameters = {{"gain",
                   "Gain",
                   "",
                   0,
                   2,
                   1,
                   0.01f,
                   instrument::ParameterType::floating,
                   "Output",
                   {},
                   true,
                   true,
                   -1,
                   0},
                  {"cutoff",
                   "Cutoff",
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
                   1},
                  {"octave",
                   "Octave",
                   "st",
                   -3,
                   3,
                   0,
                   1,
                   instrument::ParameterType::integer,
                   "Oscillator",
                   {},
                   true,
                   false,
                   -1,
                   2},
                  {"enabled",
                   "Enabled",
                   "",
                   0,
                   1,
                   1,
                   1,
                   instrument::ParameterType::boolean,
                   "Output",
                   {},
                   true,
                   false,
                   -1,
                   3},
                  {"wave",
                   "Wave",
                   "",
                   0,
                   2,
                   0,
                   1,
                   instrument::ParameterType::choice,
                   "Oscillator",
                   {"Saw", "Square", "Sine"},
                   true,
                   false,
                   -1,
                   4}};
  return d;
}
// Same pool indices as the alpha/beta fixture, different host types:
// choice, boolean, floating, integer, floating.
instrument::InstrumentDescriptor reshaped() {
  instrument::InstrumentDescriptor d;
  d.id = "com.ultimavox.test.gamma";
  d.name = "Reshaped fixture";
  d.providerId = "com.ultimavox.test";
  d.vendor = "Ultima Vox";
  d.directParameters = true;
  using T = instrument::ParameterType;
  d.parameters = {
      {"mode", "Mode", "", 0, 3, 0, 1, T::choice, "Main", {"A", "B", "C", "D"}, true, false, -1,
                   0},
      {"sync", "Sync", "", 0, 1, 0, 1, T::boolean, "Main", {}, true, false, -1,
                   1},
      {"depth", "Depth", "%", 0, 100, 50, 0, T::floating, "Main", {}, true, true, -1,
                   2},
      {"steps", "Steps", "", 0, 9, 4, 1, T::integer, "Main", {}, true, false, -1,
                   3},
      {"level", "Level", "", 0, 1, 1, 0.01f, T::floating, "Main", {}, true, true, -1,
                   4}};
  return d;
}
// A later revision of the alpha fixture: parameters listed in reverse order
// and a new one inserted first. Explicit pool indices keep every existing
// parameter on its endpoint, so recorded automation keeps its control.
instrument::InstrumentDescriptor revised() {
  auto d = descriptor("com.ultimavox.test.alpha.revised");
  std::reverse(d.parameters.begin(), d.parameters.end());
  auto drive = d.parameters.back(); // "gain" after the reverse
  drive.id = "drive";
  drive.name = "Drive";
  drive.poolIndex = 5;
  d.parameters.insert(d.parameters.begin(), drive);
  return d;
}
class Processor final : public juce::AudioProcessor {
public:
  instrument::InstrumentDescriptor first =
                                       descriptor("com.ultimavox.test.alpha"),
                                   second =
                                       descriptor("com.ultimavox.test.beta"),
                                   third = reshaped(), fourth = revised();
  std::array<const instrument::InstrumentDescriptor *, 4> catalog{
      &first, &second, &third, &fourth};
  juce::AudioProcessorValueTreeState state;
  host::DirectParameterBank bank;
  juce::AudioProcessorValueTreeState::ParameterLayout layout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;
    host::appendPoolParameters(result, catalog);
    return {result.begin(), result.end()};
  }
  Processor() : state(*this, nullptr, "DIRECT_FIXTURE", layout()) {
    bank.cache(state, catalog);
  }
  const juce::String getName() const override { return "Direct fixture"; }
  void prepareToPlay(double, int) override {}
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override {}
  juce::AudioProcessorEditor *createEditor() override { return nullptr; }
  bool hasEditor() const override { return false; }
  double getTailLengthSeconds() const override { return 0; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &) override {}
  void setStateInformation(const void *, int) override {}
};
rack::PersistentSlotState base(const instrument::InstrumentDescriptor &d) {
  rack::PersistentSlotState result;
  result.slotId = 1;
  result.instrumentId = d.id;
  for (const auto &p : d.parameters)
    result.directParameters.push_back({p.id, p.defaultValue});
  return result;
}
// Pool endpoint bound to a fixture parameter: descriptor order (both fixtures
// share one parameter list, so the module only documents the caller's intent).
std::string poolFor(std::size_t slot, std::string_view, std::string_view name) {
  const auto d = descriptor("index");
  for (std::size_t i = 0; i < d.parameters.size(); ++i)
    if (d.parameters[i].id == name)
      return instrument::hostparams::poolId(slot, i);
  require(false, "fixture parameter exists");
  return {};
}
float raw(Processor &p, std::size_t slot, std::string_view module,
          std::string_view name) {
  return p.state.getRawParameterValue(poolFor(slot, module, name))->load();
}
void set(Processor &p, std::size_t slot, std::string_view module,
         std::string_view name, float value) {
  auto *parameter = p.state.getParameter(poolFor(slot, module, name));
  require(parameter != nullptr, "named parameter exists");
  parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
float value(const rack::ProcessSlotControls &c, std::string_view module,
            std::string_view name) {
  const auto key = instrument::hostparams::directKey(module, name);
  for (const auto &v : c.directValues())
    if (v.key == key)
      return v.value;
  require(false, "snapshot contains named key");
  return 0;
}
// Host-facing metadata of one pool endpoint: discrete/boolean/step count.
struct Metadata {
  bool discrete, boolean;
  int steps;
  bool operator==(const Metadata &) const = default;
};
Metadata metadata(const juce::AudioProcessorParameter *p) {
  return {p->isDiscrete(), p->isBoolean(), p->getNumSteps()};
}
// Counts the host notifications the VST3 wrapper turns into
// kParamTitlesChanged (title, unit, default and stepCount refresh).
struct InfoListener final : juce::AudioProcessorListener {
  int infoChanges{};
  void audioProcessorParameterChanged(juce::AudioProcessor *, int, float) override {}
  void audioProcessorChanged(juce::AudioProcessor *,
                             const ChangeDetails &details) override {
    infoChanges += details.parameterInfoChanged ? 1 : 0;
  }
};
void metadataFollowsBinding(Processor &p) {
  constexpr std::size_t slot = 2; // untouched by the other checks
  const auto at = [&](std::size_t index) {
    return p.state.getParameter(instrument::hostparams::poolId(slot, index));
  };
  const Metadata continuous{false, false,
                            juce::AudioProcessorParameter::getDefaultNumParameterSteps()};
  InfoListener listener;
  p.addListener(&listener);
  for (std::size_t i = 0; i < 5; ++i)
    require(metadata(at(i)) == continuous, "unbound endpoint is continuous");
  require(p.bank.selectAndRestore(slot, base(p.first)), "bind alpha metadata");
  require(metadata(at(0)) == continuous && metadata(at(1)) == continuous,
          "floating parameters keep continuous host metadata");
  require(metadata(at(2)) == Metadata{true, false, 7},
          "integer -3..3 is discrete with its inclusive span");
  require(metadata(at(3)) == Metadata{true, true, 2},
          "boolean is a two-step switch");
  require(metadata(at(4)) == Metadata{true, false, 3},
          "choice steps equal its label count");
  require(listener.infoChanges == 1, "binding notifies the host once");
  require(p.bank.selectAndRestore(slot, base(p.third)), "switch to gamma");
  require(metadata(at(0)) == Metadata{true, false, 4} &&
              metadata(at(1)) == Metadata{true, true, 2} &&
              metadata(at(2)) == continuous &&
              metadata(at(3)) == Metadata{true, false, 10} &&
              metadata(at(4)) == continuous,
          "instrument switch retypes the same pool indices");
  require(at(0)->getDefaultValue() == 0.0f &&
              std::abs(at(2)->getDefaultValue() - 0.5f) < 1e-6f,
          "default value follows the binding");
  require(listener.infoChanges == 2, "instrument switch notifies the host");
  require(p.bank.selectAndRestore(slot, base(p.third)) &&
              listener.infoChanges == 2,
          "rebinding the same instrument does not renotify");
  p.bank.deactivate(slot);
  for (std::size_t i = 0; i < 5; ++i)
    require(metadata(at(i)) == continuous,
            "deactivated endpoint returns to continuous metadata");
  require(listener.infoChanges == 3, "unbinding notifies the host");
  p.removeListener(&listener);
}
void poolIndexIsStableAcrossReordering(Processor &p) {
  constexpr std::size_t slot = 3;
  auto state = base(p.fourth);
  for (auto &value : state.directParameters)
    if (value.id == "cutoff")
      value.value = 777;
  require(p.bank.selectAndRestore(slot, state), "bind revised fixture");
  const auto at = [&](std::size_t index) {
    return p.state.getParameter(instrument::hostparams::poolId(slot, index));
  };
  require(at(0)->getName(64) == "Slot 4 Gain" &&
              at(1)->getName(64) == "Slot 4 Cutoff" &&
              at(2)->getName(64) == "Slot 4 Octave" &&
              at(3)->getName(64) == "Slot 4 Enabled" &&
              at(4)->getName(64) == "Slot 4 Wave" &&
              at(5)->getName(64) == "Slot 4 Drive",
          "reordered/inserted parameters keep their declared pool endpoints");
  require(p.state.getRawParameterValue(instrument::hostparams::poolId(slot, 1))
                  ->load() == 777 &&
              at(4)->getNumSteps() == 3,
          "value and host type follow the pool index, not descriptor order");
  rack::ProcessSlotControls controls;
  p.bank.read(slot, controls);
  require(value(controls, p.fourth.id, "cutoff") == 777,
          "audio snapshot reads each parameter from its declared endpoint");
}
} // namespace
int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  Processor p;
  const auto param = [&](std::string_view name) {
    return p.state.getParameter(poolFor(0, p.first.id, name));
  };
  require(p.getParameters().size() ==
              static_cast<int>(instrument::maxSlots *
                               instrument::poolParametersPerSlot),
          "fixed pool layout: 16 x 128 endpoints, independent of the catalog");
  require(instrument::poolParametersPerSlot == 128 &&
              p.state.getParameter("slot16.p128") != nullptr &&
              p.state.getParameter("slot01.p129") == nullptr,
          "pool IDs cover exactly slotNN.p001..p128");
  require(param("cutoff")->getName(64) == "Slot 1 Param 02" &&
              param("cutoff")->getLabel().isEmpty(),
          "unbound endpoint has a neutral generic name");
  require(param("cutoff")->getText(0.5f, 0) == "0.500",
          "unbound endpoint shows its value with length 0 (no limit)");
  auto a = base(p.first);
  auto b = base(p.second);
  require(p.bank.selectAndRestore(0, a), "select alpha bank");
  require(param("cutoff")->getName(64) == "Slot 1 Cutoff" &&
              param("cutoff")->getLabel() == "Hz",
          "bound endpoint takes the instrument name and unit");
  require(param("wave")->getText(param("wave")->convertTo0to1(1), 32) ==
                  "Square" &&
              param("enabled")->getText(1.0f, 32) == "On" &&
              param("octave")->getText(param("octave")->convertTo0to1(-2), 32) ==
                  "-2",
          "bound endpoint formats choice, boolean and integer values");
  // JUCE editor attachments ask for the text with length 0 ("no limit").
  require(param("wave")->getText(param("wave")->convertTo0to1(1), 0) == "Square" &&
              param("cutoff")->getText(0.5f, 0) == param("cutoff")->getText(0.5f, 64) &&
              param("cutoff")->getText(0.5f, 0).isNotEmpty() &&
              param("wave")->getText(param("wave")->convertTo0to1(1), 3) == "Squ",
          "length 0 means no limit; a positive length still truncates");
  require(param("wave")->convertFrom0to1(
              param("wave")->getValueForText("Sine")) == 2 &&
              param("cutoff")->convertFrom0to1(
                  param("cutoff")->getValueForText("440")) == 440,
          "bound endpoint parses host-entered text");
  require(p.bank.selectAndRestore(1, b), "select beta bank");
  set(p, 0, p.first.id, "cutoff", 3456);
  set(p, 1, p.second.id, "gain", 0.72f);
  rack::ProcessSlotControls controls;
  p.bank.read(0, controls);
  require(value(controls, p.first.id, "cutoff") == 3456,
          "host replay reaches named snapshot");
  require(value(controls, p.first.id, "gain") == 1,
          "one control preserves unrelated gain");
  const auto otherGain = raw(p, 1, p.second.id, "gain");
  a.directParameters[0].value = 0.25f;
  require(p.bank.selectAndRestore(0, a), "preset base restore");
  require(raw(p, 1, p.second.id, "gain") == otherGain,
          "single-slot restore does not overwrite another slot");
  const auto before = raw(p, 0, p.first.id, "gain");
  auto invalid = a;
  invalid.directParameters[0].value = 1.25f;
  invalid.directParameters.back().id = "missing";
  require(!p.bank.selectAndRestore(0, invalid), "invalid restore rejected");
  require(raw(p, 0, p.first.id, "gain") == before,
          "invalid restore has no partial host writes");
  invalid = a;
  invalid.directParameters.back() = invalid.directParameters.front();
  require(!p.bank.selectAndRestore(0, invalid),
          "duplicate named restore rejected");
  std::reverse(a.directParameters.begin(), a.directParameters.end());
  require(p.bank.selectAndRestore(0, a), "named order independent");
  set(p, 0, p.first.id, "octave", 1.8f);
  set(p, 0, p.first.id, "wave", 1.7f);
  p.bank.read(0, controls);
  require(value(controls, p.first.id, "octave") == 2 &&
              value(controls, p.first.id, "wave") == 2,
          "host discrete replay canonicalized");
  p.bank.deactivate(0);
  p.bank.read(0, controls);
  require(controls.directParameterCount == 0,
          "missing module disables bank without substitution");
  require(param("cutoff")->getName(64) == "Slot 1 Param 02" &&
              param("cutoff")->getValue() == 0.0f,
          "deactivated slot unbinds its pool to a neutral value");
  require(p.bank.selectAndRestore(0, b) && raw(p, 0, p.second.id, "gain") == 1,
          "another instrument rebinds the same slot pool with its own values");
  require(p.bank.selectAndRestore(0, a),
          "known bank restored after missing module");
  const auto xml = p.state.copyState().toXmlString();
  const auto restored = juce::ValueTree::fromXml(xml);
  require(restored.isValid(), "actual XML parameter state parses");
  p.state.replaceState(restored);
  p.bank.read(0, controls);
  require(value(controls, p.first.id, "gain") == before,
          "APVTS XML preserves base gain");
  std::vector<std::unique_ptr<juce::RangedAudioParameter>> collision;
  collision.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"Aa", 1}, "A", 0, 1, 0));
  collision.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"BB", 1}, "B", 0, 1, 0));
  bool rejected = false;
  try {
    host::appendPoolParameters(collision, {});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected,
          "VST3 hash collision rejected before host layout publication");
  metadataFollowsBinding(p);
  poolIndexIsStableAcrossReordering(p);
  std::cout << "Direct APVTS bank tests passed (" << checks << ")\n";
}
