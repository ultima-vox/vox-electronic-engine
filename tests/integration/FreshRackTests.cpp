// Owner Cubase acceptance: a new Rack instance has 16 empty, silent slots;
// instruments are loaded deliberately (first free MIDI channel) and saved
// projects - including ones written with the former Bass + Acid starter
// layout - restore exactly.
#include "PluginProcessor.h"
#include "modules/BuiltInProvider.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
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

void legacyStarterProjectRestores(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
  check(bytes.size() > 1000, "legacy project fixture read: " + path);

  VstEngineAudioProcessor p;
  p.setStateInformation(bytes.data(), static_cast<int>(bytes.size()));
  p.prepareToPlay(48000.0, 256);
  const auto &rack = p.instrumentRack().state();
  check(rack[0].instrumentId == modules::bassInstrumentId &&
            rack[1].instrumentId == modules::acidInstrumentId,
        "old project restores Bass in slot 1 and Acid in slot 2");
  check(channelOf(p, 0) == 1 && channelOf(p, 1) == 2, "old project restores CH1 / CH2");
  // The fixture saved macro1..8 = 0.5 and the Bass map cutoff/resonance/...
  for (std::size_t slot = 0; slot < 2; ++slot)
    for (float value : rack[slot].macros)
      check(value == 0.5f, "old project restores its saved macro values");
  check(rack[0].macroAssignments[0] == "cutoff" && rack[0].macroAssignments[1] == "resonance",
        "old project restores its macro assignments");
  for (std::size_t slot = 2; slot < instrument::maxSlots; ++slot)
    check(rack[slot].instrumentId.empty() && channelOf(p, slot) == 0,
          "old project keeps its other slots empty");
  p.selectSlot(0);
  bool anyGate = false;
  for (int step = 0; step < p.sequence().size(); ++step)
    anyGate = anyGate || p.sequence()[step].gate;
  check(anyGate, "old project restores its saved Bass pattern");
  check(play(p, {1}) > 1e-6, "restored Bass plays on CH1");

  // Save again and reopen: same layout and identical sound.
  juce::MemoryBlock again;
  p.getStateInformation(again);
  VstEngineAudioProcessor q;
  q.setStateInformation(again.getData(), static_cast<int>(again.getSize()));
  q.prepareToPlay(48000.0, 256);
  p.prepareToPlay(48000.0, 256);
  check(q.instrumentRack().state()[0].instrumentId == modules::bassInstrumentId &&
            channelOf(q, 1) == 2,
        "resaved project keeps the layout");
  check(play(p, {1, 2}, 12) == play(q, {1, 2}, 12), "resaved project sounds identical");
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: fresh_rack_tests <legacy starter project state>\n";
    return 2;
  }
  juce::ScopedJuceInitialiser_GUI gui;
  freshRackIsEmptyAndSilent();
  loadingTakesTheFirstFreeChannel();
  legacyStarterProjectRestores(argv[1]);
  std::cout << "fresh_rack_tests: " << checks << " checks passed\n";
  return 0;
}
