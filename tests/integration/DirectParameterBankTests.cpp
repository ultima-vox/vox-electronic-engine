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
                   -1},
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
                   -1},
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
                   -1},
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
                   -1},
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
                   -1}};
  return d;
}
class Processor final : public juce::AudioProcessor {
public:
  instrument::InstrumentDescriptor first =
                                       descriptor("com.ultimavox.test.alpha"),
                                   second =
                                       descriptor("com.ultimavox.test.beta");
  std::array<const instrument::InstrumentDescriptor *, 2> catalog{&first,
                                                                  &second};
  juce::AudioProcessorValueTreeState state;
  host::DirectParameterBank bank;
  juce::AudioProcessorValueTreeState::ParameterLayout layout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;
    host::appendDirectParameters(result, catalog);
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
float raw(Processor &p, std::size_t slot, std::string_view module,
          std::string_view name) {
  return p.state
      .getRawParameterValue(
          instrument::hostparams::directId(slot, module, name))
      ->load();
}
void set(Processor &p, std::size_t slot, std::string_view module,
         std::string_view name, float value) {
  auto *parameter = p.state.getParameter(
      instrument::hostparams::directId(slot, module, name));
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
} // namespace
int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  Processor p;
  const auto id = [&](std::string_view name) {
    return instrument::hostparams::directId(0, p.first.id, name);
  };
  require(dynamic_cast<juce::AudioParameterFloat *>(
              p.state.getParameter(id("cutoff"))) != nullptr,
          "float host parameter type");
  require(dynamic_cast<juce::AudioParameterInt *>(
              p.state.getParameter(id("octave"))) != nullptr,
          "integer host parameter type");
  require(dynamic_cast<juce::AudioParameterBool *>(
              p.state.getParameter(id("enabled"))) != nullptr,
          "boolean host parameter type");
  require(dynamic_cast<juce::AudioParameterChoice *>(
              p.state.getParameter(id("wave"))) != nullptr,
          "choice host parameter type");
  require(p.state.getParameter(id("cutoff"))->getLabel() == "Hz",
          "unit preserved");
  auto a = base(p.first);
  auto b = base(p.second);
  require(p.bank.selectAndRestore(0, a), "select alpha bank");
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
    host::appendDirectParameters(collision, {});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected,
          "VST3 hash collision rejected before host layout publication");
  std::cout << "Direct APVTS bank tests passed (" << checks << ")\n";
}
