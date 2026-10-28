// Owner Cubase acceptance: a new Rack instance has 16 empty, silent slots;
// instruments are loaded deliberately (first free MIDI channel) and a new
// project restores exactly.
#include "PluginProcessor.h"
#include "modules/BuiltInProvider.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace vstengine;
int checks = 0;

void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

float raw(VstEngineAudioProcessor &p, const std::string &id) {
  const auto *value = p.parameters().getRawParameterValue(id);
  check(value != nullptr, "parameter " + id + " exists");
  return value->load();
}

std::string slotParam(std::size_t slot, const char *suffix) {
  char id[32];
  std::snprintf(id, sizeof(id), "slot%02zu%s", slot + 1, suffix);
  return id;
}

int channelOf(VstEngineAudioProcessor &p, std::size_t slot) {
  return static_cast<int>(std::lround(raw(p, slotParam(slot, "MidiIn"))));
}

// Energy of `blocks` blocks with a NoteOn on every listed channel, from a
// cleared runtime (no tail of an earlier call).
double play(VstEngineAudioProcessor &p, std::vector<int> channels, int blocks = 8,
            int note = 48) {
  p.releaseResources();
  p.prepareToPlay(48000.0, 256);
  juce::AudioBuffer<float> audio(2, 256);
  juce::MidiBuffer midi;
  double energy = 0;
  for (int b = 0; b < blocks; ++b) {
    midi.clear();
    if (b == 0)
      for (int channel : channels)
        midi.addEvent(juce::MidiMessage::noteOn(channel, note, 0.9f), 0);
    audio.clear();
    p.processBlock(audio, midi);
    for (int c = 0; c < 2; ++c)
      for (int i = 0; i < 256; ++i)
        energy += static_cast<double>(audio.getSample(c, i)) * audio.getSample(c, i);
  }
  return energy;
}

std::vector<int> allChannels() {
  std::vector<int> channels;
  for (int c = 1; c <= 16; ++c)
    channels.push_back(c);
  return channels;
}

void freshRackIsEmptyAndSilent() {
  VstEngineAudioProcessor p; // the constructor a DAW uses
  p.prepareToPlay(48000.0, 256);
  std::set<instrument::SlotId> ids;
  for (std::size_t slot = 0; slot < instrument::maxSlots; ++slot) {
    const auto &state = p.instrumentRack().state()[slot];
    check(state.instrumentId.empty(), "slot " + std::to_string(slot + 1) + " is empty");
    check(p.instrumentRack().descriptor(slot) == nullptr, "no instance in an empty slot");
    check(channelOf(p, slot) == 0, "empty slot owns no MIDI channel");
    check(state.slotId != instrument::invalidSlotId && ids.insert(state.slotId).second,
          "stable unique SlotId");
    check(state.patternPayload.empty(), "no starter pattern");
  }
  for (std::size_t slot = 0; slot < instrument::maxSlots; ++slot) {
    p.selectSlot(slot);
    bool anyGate = false;
    for (int step = 0; step < p.sequence().size(); ++step)
      anyGate = anyGate || p.sequence()[step].gate;
    check(!anyGate, "no starter sequence in slot " + std::to_string(slot + 1));
  }
  p.selectSlot(0);
  check(!p.availableInstruments().empty(), "built-in instruments stay in the catalog");
  check(play(p, allChannels(), 16) == 0.0, "fresh Rack is silent on all 16 channels");
}

void loadingTakesTheFirstFreeChannel() {
  VstEngineAudioProcessor p;
  p.prepareToPlay(48000.0, 256);
  juce::String diagnostic;
  check(p.loadSlotInstrument(4, modules::leadInstrumentId, diagnostic),
        "Lead loads into empty slot 5");
  check(channelOf(p, 4) == 1, "first instrument takes CH1");
  check(play(p, {1}) > 1e-6, "slot 5 plays on CH1");
  check(play(p, {2, 3, 4}) == 0.0, "other channels stay silent");
  check(p.loadSlotInstrument(0, modules::bassInstrumentId, diagnostic) && channelOf(p, 0) == 2,
        "second instrument takes CH2");
  check(p.loadSlotInstrument(4, {}, diagnostic), "unload slot 5");
  check(channelOf(p, 4) == 0 && p.instrumentRack().state()[4].instrumentId.empty(),
        "unloading releases the channel");
  check(p.channelAvailable(0, 1), "CH1 is free again");
  check(p.loadSlotInstrument(4, modules::leadInstrumentId, diagnostic) && channelOf(p, 4) == 1,
        "reload takes the freed CH1");
}

// A project created in this build (Bass + Acid loaded by hand, edited)
// reopens with the same layout, channels, macros and sound.
void newProjectSavesAndReopens() {
  VstEngineAudioProcessor p;
  p.prepareToPlay(48000.0, 256);
  juce::String diagnostic;
  check(p.loadSlotInstrument(0, modules::bassInstrumentId, diagnostic) &&
            p.loadSlotInstrument(1, modules::acidInstrumentId, diagnostic),
        "Bass and Acid loaded by hand");
  if (auto *macro = p.parameters().getParameter("slot01Macro01"))
    macro->setValueNotifyingHost(0.83f);
  juce::MemoryBlock state;
  p.getStateInformation(state);

  VstEngineAudioProcessor q;
  q.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  q.prepareToPlay(48000.0, 256);
  const auto &rack = q.instrumentRack().state();
  check(rack[0].instrumentId == modules::bassInstrumentId &&
            rack[1].instrumentId == modules::acidInstrumentId,
        "reopened project has Bass in slot 1 and Acid in slot 2");
  check(channelOf(q, 0) == 1 && channelOf(q, 1) == 2, "reopened project keeps CH1 / CH2");
  check(std::abs(raw(q, "slot01Macro01") - 0.83f) < 1e-3f, "reopened project keeps its macro");
  for (std::size_t slot = 2; slot < instrument::maxSlots; ++slot)
    check(rack[slot].instrumentId.empty() && channelOf(q, slot) == 0,
          "reopened project keeps its other slots empty");
  p.prepareToPlay(48000.0, 256);
  const auto a = play(p, {1, 2}, 12), b = play(q, {1, 2}, 12);
  check(a > 1e-6 && a == b, "reopened project sounds identical");
}
} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  freshRackIsEmptyAndSilent();
  loadingTakesTheFirstFreeChannel();
  newProjectSavesAndReopens();
  std::cout << "fresh_rack_tests: " << checks << " checks passed\n";
  return 0;
}
