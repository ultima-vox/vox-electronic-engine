// M0: the real Rack processor loads the V2 test module from its absolute path,
// plays it through an existing slot's MIDI/audio routing, automates it through
// the host pool and restores the same sound after save/reload.
#include "PluginProcessor.h"
#include "modules/BuiltInProvider.h"
#include "instrument/HostParameterSchema.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if defined(VOX_TEST_INTERPOSE_MALLOC)
extern "C" void voxProbeBegin(void);
extern "C" unsigned long voxProbeEnd(void);
#endif

namespace {
using namespace vstengine;
constexpr const char *toneId = "test.abi.v2.tone";
constexpr int blockSize = 128;
std::string modulePath;
int checks = 0;

void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::unique_ptr<VstEngineAudioProcessor> makeProcessor(bool withModule) {
  auto report = withModule ? instrument::native::loadConfiguredModules(modulePath)
                           : instrument::native::LoadReport{};
  auto p = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                     std::move(report));
  // Silence the default built-in slots so the native slot is measured alone.
  for (const char *id : {"slot01Enabled", "slot02Enabled"})
    if (auto *enabled = p->parameters().getParameter(id))
      enabled->setValueNotifyingHost(0.0f);
  p->prepareToPlay(48000.0, blockSize);
  return p;
}

// The slot's MIDI channel lives in its (non-automatable) MidiIn parameter.
int channelOf(VstEngineAudioProcessor &p, std::size_t slot) {
  char id[32];
  std::snprintf(id, sizeof(id), "slot%02zuMidiIn", slot + 1);
  const auto *value = p.parameters().getRawParameterValue(id);
  check(value != nullptr, "slot MidiIn parameter exists");
  return static_cast<int>(std::lround(value->load()));
}

void setPlain(VstEngineAudioProcessor &p, std::size_t slot, int poolIndex, float plain) {
  auto *parameter = p.parameters().getParameter(instrument::hostparams::poolId(slot, poolIndex));
  check(parameter != nullptr, "pool endpoint exists");
  parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
}

struct Render {
  std::vector<float> left;
  double energy() const {
    double sum = 0;
    for (const float v : left)
      sum += static_cast<double>(v) * v;
    return sum;
  }
};

// Renders `blocks` blocks; NoteOn at the start of block `on`, NoteOff at the
// start of block `off` (negative = none), on the given channel.
Render play(VstEngineAudioProcessor &p, int channel, int blocks, int on, int off,
            int note = 60) {
  Render out;
  juce::AudioBuffer<float> audio(2, blockSize);
  juce::MidiBuffer midi;
  for (int b = 0; b < blocks; ++b) {
    midi.clear();
    if (b == on)
      midi.addEvent(juce::MidiMessage::noteOn(channel, note, 0.9f), 0);
    if (b == off)
      midi.addEvent(juce::MidiMessage::noteOff(channel, note), 0);
    audio.clear();
    p.processBlock(audio, midi);
    out.left.insert(out.left.end(), audio.getReadPointer(0),
                    audio.getReadPointer(0) + blockSize);
  }
  return out;
}

double energy(const Render &r, int fromBlock, int toBlock) {
  double sum = 0;
  for (int i = fromBlock * blockSize; i < toBlock * blockSize; ++i)
    sum += static_cast<double>(r.left[static_cast<std::size_t>(i)]) * r.left[static_cast<std::size_t>(i)];
  return sum;
}

std::size_t loadTone(VstEngineAudioProcessor &p, std::size_t slot) {
  juce::String diagnostic;
  check(p.loadSlotInstrument(slot, toneId, diagnostic),
        "native instrument loads into slot: " + diagnostic.toStdString());
  check(p.instrumentRack().state()[slot].instrumentId == toneId, "slot holds native id");
  check(channelOf(p, slot) >= 1, "slot got a MIDI channel");
  return slot;
}

void registration() {
  auto p = makeProcessor(true);
  check(p->nativeModuleDiagnostics().empty(), "configured module loads without diagnostics");
  bool listed = false;
  for (const auto *d : p->availableInstruments())
    listed |= d->id == toneId && d->providerId == instrument::native::providerId &&
              d->directParameters && d->parameters.size() == 4;
  check(listed, "native instrument offered next to built-ins");

  auto bad = instrument::native::loadConfiguredModules("relative/module.so;/no/such/module.so");
  check(bad.providers.empty() && bad.diagnostics.size() == 2,
        "relative and missing paths are reported, not loaded");
}

void playsAndAutomates() {
  auto p = makeProcessor(true);
  const auto slot = loadTone(*p, 2);
  const int channel = channelOf(*p, slot);
  check(play(*p, channel, 4, -1, -1).energy() == 0.0, "silent without MIDI");
  const auto r = play(*p, channel, 12, 1, 8);
  check(energy(r, 0, 1) == 0.0, "silent before NoteOn");
  check(energy(r, 2, 7) > 1.0, "NoteOn through the Rack is audible");
  check(energy(r, 10, 12) < 1e-6, "NoteOff ends the note");
  const auto other = play(*p, channel == 16 ? 15 : channel + 1, 6, 1, -1);
  check(other.energy() == 0.0, "other MIDI channels do not reach the slot");

  // Host automation of gain (pool p001) reaches the module.
  setPlain(*p, slot, 0, 0.0f);
  check(energy(play(*p, channel, 6, 1, 5), 2, 5) < 1e-9, "gain 0 through the host pool silences");
  setPlain(*p, slot, 0, 1.0f);
  const auto loud = play(*p, channel, 6, 1, 5);
  setPlain(*p, slot, 0, 0.25f);
  const auto quiet = play(*p, channel, 6, 1, 5);
  check(energy(loud, 2, 5) > 4.0 * energy(quiet, 2, 5), "gain endpoint scales the module output");
  setPlain(*p, slot, 2, 1.0f); // mute boolean p003
  check(energy(play(*p, channel, 6, 1, 5), 2, 5) < 1e-9, "boolean endpoint reaches the module");
  setPlain(*p, slot, 2, 0.0f);
}

void savesAndRestores() {
  auto first = makeProcessor(true);
  const auto slot = loadTone(*first, 3);
  setPlain(*first, slot, 0, 0.8f); // gain
  setPlain(*first, slot, 1, 1.0f); // octave
  setPlain(*first, slot, 3, 2.0f); // wave = saw
  play(*first, channelOf(*first, slot), 2, -1, -1); // let values settle
  juce::MemoryBlock state;
  first->getStateInformation(state);

  auto second = makeProcessor(true);
  second->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  check(second->instrumentRack().state()[slot].instrumentId == toneId, "slot identity restored");
  check(channelOf(*second, slot) == channelOf(*first, slot), "MIDI channel restored");
  const auto value = second->parameters().getRawParameterValue(instrument::hostparams::poolId(slot, 3));
  check(value != nullptr && value->load() == 2.0f, "choice endpoint restored in plain units");

  first->prepareToPlay(48000.0, blockSize);
  second->prepareToPlay(48000.0, blockSize);
  const auto a = play(*first, channelOf(*first, slot), 10, 1, 7);
  const auto b = play(*second, channelOf(*second, slot), 10, 1, 7);
  check(a.energy() > 1.0, "restored comparison is audible");
  check(a.left == b.left, "reloaded project renders the same sound bit for bit");
}

void missingModuleIsPreserved() {
  auto first = makeProcessor(true);
  const auto slot = loadTone(*first, 4);
  juce::MemoryBlock state;
  first->getStateInformation(state);

  auto without = makeProcessor(false);
  without->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  check(without->instrumentRack().state()[slot].instrumentId == toneId,
        "missing module keeps the slot identity");
  check(play(*without, channelOf(*without, slot), 4, 1, -1).energy() == 0.0,
        "missing module slot is silent");
  juce::MemoryBlock again;
  without->getStateInformation(again);
  auto restored = makeProcessor(true);
  restored->setStateInformation(again.getData(), static_cast<int>(again.getSize()));
  check(restored->instrumentRack().state()[slot].instrumentId == toneId &&
            play(*restored, channelOf(*restored, slot), 6, 1, -1).energy() > 1.0,
        "re-installed module resolves the preserved slot again");
}

void twoInstancesAndSlots() {
  auto a = makeProcessor(true);
  auto b = makeProcessor(true);
  const auto slotA = loadTone(*a, 5);
  const auto slotA2 = loadTone(*a, 6);
  const auto slotB = loadTone(*b, 5);
  setPlain(*a, slotA2, 3, 1.0f); // second slot: square
  setPlain(*a, slotA2, 0, 0.0f); // and silent
  const auto ra = play(*a, channelOf(*a, slotA), 6, 1, 5);
  const auto rb = play(*b, channelOf(*b, slotB), 6, 1, 5);
  check(ra.energy() > 1.0 && ra.left == rb.left,
        "two Rack instances share one module and render independently");
  check(play(*a, channelOf(*a, slotA2), 6, 1, -1).energy() < 1e-9,
        "slot parameters are independent");
}

// More routed MIDI than the module's prepared budget (64 for the test module)
// with the release last: the adapter must fail closed (end every voice at the
// block end) instead of silently dropping the NoteOff. Another slot is not
// affected.
double holdAndOverflow(bool overflow, std::uint64_t &overflowsAfter) {
  auto p = makeProcessor(true);
  const auto x = loadTone(*p, 8), y = loadTone(*p, 9);
  const int cx = channelOf(*p, x), cy = channelOf(*p, y);
  juce::AudioBuffer<float> audio(2, blockSize);
  juce::MidiBuffer midi;
  if (overflow)
    midi.addEvent(juce::MidiMessage::noteOn(cx, 60, 0.9f), 0);
  midi.addEvent(juce::MidiMessage::noteOn(cy, 64, 0.9f), 0);
  p->processBlock(audio, midi);
  midi.clear();
  if (overflow) {
    for (int i = 0; i < 100; ++i)
      midi.addEvent(juce::MidiMessage::controllerEvent(cx, 1, i), i);
    midi.addEvent(juce::MidiMessage::noteOff(cx, 60), 120);
  }
  p->processBlock(audio, midi);
  midi.clear();
  audio.clear();
  p->processBlock(audio, midi);
  double e = 0;
  for (int i = 0; i < blockSize; ++i)
    e += static_cast<double>(audio.getSample(0, i)) * audio.getSample(0, i);
  overflowsAfter = instrument::native::totalMidiOverflows();
  return e;
}

void midiOverflowFailsClosed() {
  std::uint64_t before = 0, after = 0;
  const double reference = holdAndOverflow(false, before);
  const double withOverflow = holdAndOverflow(true, after);
  check(reference > 1.0, "other slot keeps sounding");
  check(withOverflow == reference,
        "release lost to the module budget does not leave a stuck note");
  check(after > before, "MIDI overflow is counted and visible");
}

std::uint32_t liveSlotMask() {
#if defined(_WIN32)
  // Same absolute path as the host loader: returns the already loaded module.
  auto *library = ::LoadLibraryExW(
      std::filesystem::weakly_canonical(std::filesystem::path(modulePath)).wstring().c_str(),
      nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  auto fn = reinterpret_cast<std::uint32_t(VOX_CALL *)(void)>(
      ::GetProcAddress(library, "voxTestLiveSlotMask"));
#else
  void *library = ::dlopen(modulePath.c_str(), RTLD_NOW | RTLD_NOLOAD);
  auto fn = reinterpret_cast<std::uint32_t (*)(void)>(::dlsym(library, "voxTestLiveSlotMask"));
#endif
  check(fn != nullptr, "test module exports its slot hook");
  const auto mask = fn();
#if defined(_WIN32)
  ::FreeLibrary(library);
#else
  ::dlclose(library);
#endif
  return mask;
}

void slotIndexForwarded() {
  juce::MemoryBlock state;
  {
    auto p = makeProcessor(true);
    loadTone(*p, 3);
    loadTone(*p, 7);
    check(liveSlotMask() == ((1u << 3) | (1u << 7)),
          "module instances receive their 0-based Rack slot ordinals");
    p->getStateInformation(state);
  }
  check(liveSlotMask() == 0, "destroyed processor leaves no instances");
  auto restored = makeProcessor(true);
  restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  check(liveSlotMask() == ((1u << 3) | (1u << 7)), "slot ordinals survive save/reload");
}

void noAllocationInProcess() {
  auto p = makeProcessor(true);
  const auto slot = loadTone(*p, 7);
  const int channel = channelOf(*p, slot);
  juce::AudioBuffer<float> audio(2, blockSize);
  juce::MidiBuffer midi;
  midi.ensureSize(256);
  play(*p, channel, 4, 0, -1); // warm up
#if defined(VOX_TEST_INTERPOSE_MALLOC)
  voxProbeBegin();
#endif
  for (int b = 0; b < 200; ++b) {
    midi.clear();
    if (b % 20 == 0)
      midi.addEvent(juce::MidiMessage::noteOn(channel, 60 + b % 12, 0.8f), 0);
    if (b % 20 == 10)
      midi.addEvent(juce::MidiMessage::noteOff(channel, 60 + (b - 10) % 12), 0);
    if (auto *gain = p->parameters().getParameter(instrument::hostparams::poolId(slot, 0)))
      gain->setValue(static_cast<float>(b % 10) / 10.0f);
    p->processBlock(audio, midi);
  }
#if defined(VOX_TEST_INTERPOSE_MALLOC)
  const auto allocations = voxProbeEnd();
  check(allocations == 0, "native slot processing allocates nothing (" +
                              std::to_string(allocations) + ")");
  std::cout << "allocation probe: 0 allocations in 200 Rack blocks with a native slot\n";
#else
  std::cout << "allocation probe: not available on this platform\n";
#endif
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: native_rack_tests <absolute test module path>\n";
    return 2;
  }
  modulePath = argv[1];
  juce::ScopedJuceInitialiser_GUI gui;
  registration();
  playsAndAutomates();
  savesAndRestores();
  missingModuleIsPreserved();
  twoInstancesAndSlots();
  midiOverflowFailsClosed();
  slotIndexForwarded();
  noAllocationInProcess();
  std::cout << "native_rack_tests: " << checks << " checks passed\n";
  return 0;
}
