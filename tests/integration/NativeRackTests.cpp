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

// Test-only exports of the already loaded test module (same absolute path as
// the host loader, so this never loads a second copy).
void *testHook(const char *name) {
#if defined(_WIN32)
  auto *library = ::LoadLibraryExW(
      std::filesystem::weakly_canonical(std::filesystem::path(modulePath)).wstring().c_str(),
      nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  void *fn = reinterpret_cast<void *>(::GetProcAddress(library, name));
  ::FreeLibrary(library); // the host's own reference keeps it loaded
#else
  void *library = ::dlopen(modulePath.c_str(), RTLD_NOW | RTLD_NOLOAD);
  void *fn = ::dlsym(library, name);
  ::dlclose(library);
#endif
  check(fn != nullptr, std::string("test module exports ") + name);
  return fn;
}

std::uint32_t liveSlotMask() {
  return reinterpret_cast<std::uint32_t(VOX_CALL *)(void)>(testHook("voxTestLiveSlotMask"))();
}

void failNext(std::uint32_t slotIndex, std::uint32_t which) {
  reinterpret_cast<void(VOX_CALL *)(std::uint32_t, std::uint32_t)>(
      testHook("voxTestFailNext"))(slotIndex, which);
}
constexpr std::uint32_t failProcess = 1, failReset = 2;

std::uint32_t moduleCalls(std::uint32_t slotIndex, std::uint32_t which) {
  return reinterpret_cast<std::uint32_t(VOX_CALL *)(std::uint32_t, std::uint32_t)>(
      testHook("voxTestCalls"))(slotIndex, which);
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

// Slot 4 and 5 hold notes; slot 4's module fails one process call in block 1
// and would play again from block 2. Returns blocks 1..7 of the mix.
std::vector<float> faultRun(bool playFaultySlot, bool injectFault, std::uint64_t &faults) {
  auto p = makeProcessor(true);
  const auto x = loadTone(*p, 4), y = loadTone(*p, 5);
  juce::AudioBuffer<float> audio(2, blockSize);
  juce::MidiBuffer midi;
  if (playFaultySlot)
    midi.addEvent(juce::MidiMessage::noteOn(channelOf(*p, x), 60, 0.9f), 0);
  midi.addEvent(juce::MidiMessage::noteOn(channelOf(*p, y), 64, 0.9f), 0);
  p->processBlock(audio, midi);
  midi.clear();
  if (injectFault)
    failNext(4, failProcess);
  std::vector<float> out;
  for (int b = 1; b < 8; ++b) {
    audio.clear();
    p->processBlock(audio, midi);
    out.insert(out.end(), audio.getReadPointer(0), audio.getReadPointer(0) + blockSize);
  }
  faults = instrument::native::totalModuleFaults();
  return out;
}

void processFaultLatches() {
  std::uint64_t before = 0, after = 0;
  const auto reference = faultRun(false, false, before);
  const auto faulted = faultRun(true, true, after);
  double e = 0;
  for (const float v : reference)
    e += static_cast<double>(v) * v;
  check(e > 1.0, "independent slot keeps sounding");
  check(faulted == reference,
        "a faulted module stays silent after the fault block; other slots unaffected");
  check(after > before, "module fault is counted and visible");

  // Controller-side recovery: reprepare clears the latch.
  auto p = makeProcessor(true);
  const auto slot = loadTone(*p, 4);
  const int channel = channelOf(*p, slot);
  failNext(4, failProcess);
  check(play(*p, channel, 4, 1, -1).energy() == 0.0, "faulted slot is silent until reprepare");
  p->releaseResources();
  p->prepareToPlay(48000.0, blockSize);
  check(energy(play(*p, channel, 4, 1, -1), 1, 4) > 1.0, "reprepare recovers the slot");
}

void resetFaultLatches() {
  std::string diagnostic;
  auto module = instrument::native::NativeModule::acquire(
      std::filesystem::weakly_canonical(std::filesystem::path(modulePath)), diagnostic);
  check(module != nullptr, "module acquired: " + diagnostic);
  auto provider = instrument::native::makeProvider(module);
  instrument::CreateContext context;
  context.slotIndex = 11;
  auto instance = provider->create(toneId, context);
  check(instance != nullptr, "adapter instance created");
  check(instance->prepare({48000.0, blockSize, 2}), "adapter prepared");
  std::vector<float> left(blockSize), right(blockSize);
  float *channels[] = {left.data(), right.data()};
  auto render = [&] {
    const VoxMidiEventV1 on{0, 3, {0x90, 60, 100}};
    instrument::ProcessBlock block;
    block.outputs = channels;
    block.sampleCount = blockSize;
    block.midi = {&on, 1};
    block.sampleRate = 48000.0;
    instance->process(block);
    double sum = 0;
    for (const float v : left)
      sum += static_cast<double>(v) * v;
    return sum;
  };
  check(render() > 1.0, "adapter instance plays");
  const auto before = instrument::native::totalModuleFaults();
  failNext(11, failReset);
  instance->reset();
  check(instrument::native::totalModuleFaults() > before, "reset fault is counted");
  check(render() == 0.0, "failed reset latches the instance silent");
  check(instance->prepare({48000.0, blockSize, 2}), "reprepare succeeds");
  check(render() > 1.0, "reprepare recovers after a reset fault");
}

// After a fault the module is quarantined: neither reset nor process enters it
// again, and a failed reprepare keeps the latch; only a successful one clears it.
void faultedModuleIsNotReentered() {
  std::string diagnostic;
  auto module = instrument::native::NativeModule::acquire(
      std::filesystem::weakly_canonical(std::filesystem::path(modulePath)), diagnostic);
  auto provider = instrument::native::makeProvider(module);
  instrument::CreateContext context;
  context.slotIndex = 12;
  auto instance = provider->create(toneId, context);
  check(instance != nullptr && instance->prepare({48000.0, blockSize, 2}), "adapter prepared");
  std::vector<float> left(blockSize), right(blockSize);
  float *channels[] = {left.data(), right.data()};
  auto render = [&] {
    const VoxMidiEventV1 on{0, 3, {0x90, 60, 100}};
    instrument::ProcessBlock block;
    block.outputs = channels;
    block.sampleCount = blockSize;
    block.midi = {&on, 1};
    block.sampleRate = 48000.0;
    instance->process(block);
    double sum = 0;
    for (const float v : left)
      sum += static_cast<double>(v) * v;
    return sum;
  };
  failNext(12, failProcess);
  check(render() == 0.0, "injected process fault silences the block");
  const auto calls = moduleCalls(12, failProcess | failReset);
  for (int i = 0; i < 3; ++i) {
    instance->reset();
    check(render() == 0.0, "faulted instance stays silent across reset");
  }
  check(moduleCalls(12, failProcess | failReset) == calls,
        "no module reset/process call while faulted");
  check(!instance->prepare({48000.0, blockSize, 0}), "invalid reprepare fails");
  instance->reset();
  check(render() == 0.0 && moduleCalls(12, failProcess | failReset) == calls,
        "a failed reprepare keeps the module quarantined");
  check(instance->prepare({48000.0, blockSize, 2}), "valid reprepare succeeds");
  check(render() > 1.0, "successful reprepare recovers");
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

std::uint64_t patternSeen(std::uint32_t slotIndex, std::uint32_t &checksum) {
  return reinterpret_cast<std::uint64_t(VOX_CALL *)(std::uint32_t, std::uint32_t *)>(
      testHook("voxTestPatternSeen"))(slotIndex, &checksum);
}

// The module's pattern caps and factory pattern are imported at load, and the
// Rack's snapshot reaches the module's process through the appended context
// fields.
void patternContentImportedAndDelivered() {
  std::string diagnostic;
  auto module = instrument::native::NativeModule::acquire(
      std::filesystem::weakly_canonical(std::filesystem::path(modulePath)), diagnostic);
  check(module != nullptr, "module acquired: " + diagnostic);
  const auto descriptors = module->descriptors();
  check(descriptors.size() == 1 && descriptors[0].patternContent != nullptr,
        "pattern content imported");
  const auto &content = *descriptors[0].patternContent;
  check(content.caps.playback == VOX_PATTERN_PLAYBACK_INTERNAL && content.caps.laneCount == 2 &&
            content.caps.laneNotes == std::vector<std::uint8_t>{60, 64} &&
            content.caps.laneNames[1] == "High",
        "caps copied from the module");
  check(content.factory.size() == 1 && content.factory[0].name == "Test Pulse",
        "factory pattern imported by name");
  const auto &factory = *content.factory[0].pattern;
  check(factory.laneCount == 2 && factory.stepsPerBar == 4 &&
            factory.at(0, 0, 0).flags == (VOX_STEP_ON | VOX_STEP_ACCENT) &&
            factory.at(1, 0, 2).velocity == 70,
        "factory cells copied exactly");

  auto provider = instrument::native::makeProvider(module);
  instrument::CreateContext context;
  context.slotIndex = 13;
  auto instance = provider->create(toneId, context);
  check(instance != nullptr && instance->prepare({48000.0, blockSize, 2}), "adapter prepared");
  std::vector<float> left(blockSize), right(blockSize);
  float *channels[] = {left.data(), right.data()};
  const auto view = factory.view();
  instrument::ProcessBlock block;
  block.outputs = channels;
  block.sampleCount = blockSize;
  block.sampleRate = 48000.0;
  block.pattern = &view;
  block.patternGeneration = 7;
  instance->process(block);
  std::uint32_t checksum = 0;
  check(patternSeen(13, checksum) == 7, "snapshot generation reaches the module");
  check(checksum == 1 * 120 + 7 * 70, "snapshot cells reach the module");
}

void setNoteMode(std::uint32_t slotIndex, std::uint32_t mode) {
  reinterpret_cast<void(VOX_CALL *)(std::uint32_t, std::uint32_t)>(
      testHook("voxTestSetNoteMode"))(slotIndex, mode);
}

// Module-written note output is validated record by record before the Rack
// may use it; anything malformed voids the block, a silenced block reports
// nothing.
void noteOutputValidated() {
  std::string diagnostic;
  auto module = instrument::native::NativeModule::acquire(
      std::filesystem::weakly_canonical(std::filesystem::path(modulePath)), diagnostic);
  auto provider = instrument::native::makeProvider(module);
  instrument::CreateContext context;
  context.slotIndex = 14;
  setNoteMode(14, 1);
  auto instance = provider->create(toneId, context);
  check(instance != nullptr && instance->prepare({48000.0, blockSize, 2}), "adapter prepared");
  std::vector<float> left(blockSize), right(blockSize);
  float *channels[] = {left.data(), right.data()};
  std::array<VoxNoteEventV2, 8> events{};
  VoxNoteOutputV2 out{sizeof(VoxNoteOutputV2), VOX_MODULE_ABI_V2, 8, sizeof(VoxNoteEventV2), 0, 0,
                      events.data()};
  auto render = [&] {
    instrument::ProcessBlock block;
    block.outputs = channels;
    block.sampleCount = blockSize;
    block.sampleRate = 48000.0;
    block.noteOutput = &out;
    out.count = 99; // stale value from a previous block
    instance->process(block);
  };
  render();
  check(out.count == 2 && out.dropped == 0 && events[0].note == 60 && events[1].lane == 1 &&
            events[1].flags == VOX_NOTE_EVENT_ACCENT,
        "valid notes pass unchanged");
  const char *bad[] = {"", "", "note above 127", "velocity 0", "unknown flag", "undeclared lane",
                       "out of order", "offset at the block end", "count above the capacity"};
  for (std::uint32_t mode = 2; mode <= 8; ++mode) {
    setNoteMode(14, mode);
    const auto before = instrument::native::totalInvalidNoteReports();
    render();
    check(out.count == 0 && out.dropped == instrument::native::voidedNoteOutput &&
              instrument::native::totalInvalidNoteReports() == before + 1,
          std::string("malformed note output voids the block: ") + bad[mode]);
  }
  // V2.2 routed-notes report: only the defined values pass; anything else
  // voids the block and leaves host triggers as they always were.
  setNoteMode(14, 10);
  render();
  check(out.count == 2 && out.routedNotes == VOX_ROUTED_NOTES_ALL_IGNORED,
        "ALL_IGNORED passes with the block's notes");
  for (std::uint32_t mode : {11u, 12u}) {
    setNoteMode(14, mode);
    const auto before = instrument::native::totalInvalidNoteReports();
    render();
    check(out.count == 0 && out.dropped == instrument::native::voidedNoteOutput &&
              out.routedNotes == VOX_ROUTED_NOTES_MAY_PLAY && out.reserved == 0 &&
              instrument::native::totalInvalidNoteReports() == before + 1,
          "an undefined routed-notes report voids the block (mode " + std::to_string(mode) + ")");
  }
  // A host with the V2.1 32-byte struct: notes still flow, nothing is written
  // past its end.
  setNoteMode(14, 10);
  out.structSize = VOX_NOTE_OUTPUT_V2_BASE_SIZE;
  out.routedNotes = 0xA5A5A5A5u;
  out.reserved = 0x5A5A5A5Au;
  render();
  check(out.count == 2 && out.routedNotes == 0xA5A5A5A5u && out.reserved == 0x5A5A5A5Au,
        "a base-size note output gets notes and nothing beyond its 32 bytes");
  out.structSize = sizeof(VoxNoteOutputV2);
  out.routedNotes = out.reserved = 0;
  instance->setBypassed(true);
  render();
  check(out.count == 0 && out.dropped == 0 && out.routedNotes == VOX_ROUTED_NOTES_MAY_PLAY,
        "a bypassed instance reports no notes and no routed-notes claim");
  setNoteMode(14, 1);
  instance->setBypassed(true);
  render();
  check(out.count == 0 && out.dropped == 0, "a bypassed instance reports no notes");
  instance->setBypassed(false);
  render();
  check(out.count == 2, "unbypassed, its notes are reported again");
  setNoteMode(14, 0);
}

// Export fails closed when the module reports malformed notes.
void exportRejectsMalformedNotes() {
  auto p = makeProcessor(true);
  loadTone(*p, 4);
  setNoteMode(4, 1);
  juce::MemoryBlock bytes;
  juce::String diagnostic;
  check(p->serializeSlotPatternMidi(4, bytes, diagnostic),
        "valid module notes export: " + diagnostic.toStdString());
  setNoteMode(4, 2);
  check(!p->serializeSlotPatternMidi(4, bytes, diagnostic) && diagnostic.contains("malformed"),
        "malformed module notes export nothing: " + diagnostic.toStdString());
  setNoteMode(4, 0);
}

// Hostile content tables reject the instrument instead of being trusted.
namespace hostile {
const char *const names[] = {"A", "B"};
const std::uint8_t notes[] = {36, 38};
VoxPatternCapsV2 caps{sizeof(VoxPatternCapsV2), VOX_MODULE_ABI_V2, VOX_PATTERN_PLAYBACK_INTERNAL,
                      2, 16, 1, VOX_FIELD_VELOCITY, 1, names, notes};
int mode = 0;
VoxResult VOX_CALL getCaps(const char *, const VoxPatternCapsV2 **out) {
  if (mode == 1)
    return VOX_E_INTERNAL;
  *out = &caps;
  return VOX_OK;
}
VoxResult VOX_CALL getFactory(const char *, std::uint32_t, const char **name, VoxPatternV2 *p) {
  *name = mode == 4 ? "" : "Factory";
  p->laneCount = 2;
  p->stepsPerBar = 4;
  p->barCount = 1;
  if (mode == 2)
    return VOX_E_INTERNAL;
  if (mode == 3)
    p->cells[0].note = 5; // a field this instrument does not read
  if (mode == 5)
    p->cellCapacity = 1u << 20; // module tampers with the host buffer bounds
  return VOX_OK;
}
const VoxContentApiV2 table{sizeof(VoxContentApiV2), VOX_MODULE_ABI_V2, getCaps, getFactory};
} // namespace hostile

void hostileContentRejected() {
  instrument::InstrumentDescriptor descriptor;
  descriptor.id = "hostile";
  std::string diagnostic;
  hostile::mode = 0;
  check(instrument::native::importContent(&hostile::table, descriptor, diagnostic) &&
            descriptor.patternContent != nullptr,
        "well-formed content imports: " + diagnostic);
  check(instrument::native::importContent(nullptr, descriptor, diagnostic) &&
            descriptor.patternContent == nullptr,
        "no content table: no pattern");
  for (int mode : {1, 2, 3, 4, 5}) {
    hostile::mode = mode;
    check(!instrument::native::importContent(&hostile::table, descriptor, diagnostic) &&
              descriptor.patternContent == nullptr,
          "hostile content mode " + std::to_string(mode) + " rejected");
  }
  hostile::mode = 0;
  hostile::caps.laneNotes = nullptr;
  check(!instrument::native::importContent(&hostile::table, descriptor, diagnostic),
        "INTERNAL caps without lane notes rejected");
  hostile::caps.laneNotes = hostile::notes;
  hostile::caps.factoryPatternCount = 2; // same name twice
  check(!instrument::native::importContent(&hostile::table, descriptor, diagnostic) &&
            diagnostic.find("duplicate") != std::string::npos,
        "duplicate factory pattern names rejected");
  hostile::caps.factoryPatternCount = 1;
  VoxModuleV2 m{};
  check(!instrument::native::validModuleTable(&m), "empty module table rejected");

  // The declared upgrade window must lie below the instrument version.
  auto module = instrument::native::NativeModule::acquire(
      std::filesystem::weakly_canonical(std::filesystem::path(modulePath)), diagnostic);
  const VoxInstrumentDescriptorV2 *raw = nullptr;
  check(module != nullptr && module->table().getInstrument(0, &raw) == VOX_OK && raw != nullptr,
        "raw descriptor available");
  auto upgrade = *raw;
  instrument::InstrumentDescriptor imported;
  check(instrument::native::importDescriptor(&upgrade, imported, diagnostic) &&
            imported.upgradeFromVersion == 0,
        "no declared upgrade imports as 0");
  upgrade.instrumentVersion = 3;
  upgrade.upgradeFromVersion = 2;
  check(instrument::native::importDescriptor(&upgrade, imported, diagnostic) &&
            imported.upgradeFromVersion == 2,
        "a declared upgrade window imports");
  upgrade.upgradeFromVersion = 3;
  check(!instrument::native::importDescriptor(&upgrade, imported, diagnostic),
        "an upgrade window at or above the instrument version is rejected");
  check(instrument::native::makeProvider(module, "0123456789abcdef")->descriptors()[0].publisherId ==
            "0123456789abcdef",
        "the provider carries the package publisher identity");
}

// Kick ducking consumes the same validated note output as export: a block of
// malformed records (voided by the adapter) ducks nothing, valid generated
// notes duck at once. Source: test module in slot 6 reporting notes 60/64;
// target: a held note in slot 7.
std::vector<float> duckRun(std::uint32_t noteMode) {
  setNoteMode(6, noteMode);
  auto p = makeProcessor(true);
  const auto source = loadTone(*p, 6), target = loadTone(*p, 7);
  auto param = [&](const juce::String &id, float plain) {
    auto *parameter = p->parameters().getParameter(id);
    check(parameter != nullptr, "routing parameter " + id.toStdString());
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
  };
  param("rack.duck.source", static_cast<float>(source + 1));
  param("rack.duck.note", 60.0f);
  param("rack.duck.attack", 0.0f);
  param("rack.duck.hold", 0.5f);
  param("rack.duck.release", 1.0f);
  param("slot08.duck.depth", 1.0f);
  const auto out = play(*p, channelOf(*p, target), 6, 0, -1, 64).left;
  setNoteMode(6, 0);
  return {out.begin() + blockSize, out.end()}; // from the second block on
}

// Routed kick NoteOn (note 62, not one the module generates) on the source's
// channel at sample 20 of every block: it ducks unless the source validly
// reports that its routed notes did not sound.
std::vector<float> routedKickRun(std::uint32_t noteMode, bool muteSource = false,
                                 bool faultSource = false) {
  setNoteMode(6, noteMode);
  auto p = makeProcessor(true);
  const auto source = loadTone(*p, 6), target = loadTone(*p, 7);
  auto param = [&](const juce::String &id, float plain) {
    auto *parameter = p->parameters().getParameter(id);
    check(parameter != nullptr, "routing parameter " + id.toStdString());
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
  };
  param("rack.duck.source", static_cast<float>(source + 1));
  param("rack.duck.note", 62.0f);
  param("rack.duck.attack", 0.0f);
  param("rack.duck.hold", 0.5f);
  param("rack.duck.release", 1.0f);
  param("slot08.duck.depth", 1.0f);
  if (muteSource)
    param("slot07Mute", 1.0f);
  setPlain(*p, source, 0, 0.0f); // the source plays, but adds no audio
  const int sourceChannel = channelOf(*p, source), targetChannel = channelOf(*p, target);
  std::vector<float> out;
  juce::AudioBuffer<float> audio(2, blockSize);
  juce::MidiBuffer midi;
  for (int b = 0; b < 6; ++b) {
    midi.clear();
    if (b == 0)
      midi.addEvent(juce::MidiMessage::noteOn(targetChannel, 64, 0.9f), 0);
    if (b > 0)
      midi.addEvent(juce::MidiMessage::noteOn(sourceChannel, 62, 0.9f), 20);
    if (b == 1 && faultSource)
      failNext(6, failProcess); // latched: the source renders nothing from here on
    audio.clear();
    p->processBlock(audio, midi);
    if (b > 0)
      out.insert(out.end(), audio.getReadPointer(0), audio.getReadPointer(0) + blockSize);
  }
  setNoteMode(6, 0);
  return out;
}

void routedNotesPolicy() {
  const auto legacy = routedKickRun(0); // the module writes no report
  check(legacy[20] == 0.0f && legacy[19] != 0.0f,
        "a routed kick ducks a legacy source at its sample");
  const auto ignored = routedKickRun(10);
  check(ignored[20] != 0.0f, "a source reporting ALL_IGNORED: its routed kick does not duck");
  check(routedKickRun(11)[20] == 0.0f && routedKickRun(12)[20] == 0.0f,
        "an undefined report is voided: routed kicks duck as before");
  check(routedKickRun(0, true)[20] != 0.0f, "a muted source ducks nothing");
  // A latched process fault silences the source, routed MIDI included: its
  // routed kick ducks nothing, in the fault block and every block after it.
  const auto faulted = routedKickRun(0, false, true);
  bool ducked = false;
  for (std::size_t b = 0; b < faulted.size() / blockSize; ++b)
    ducked = ducked || faulted[b * blockSize + 20] == 0.0f;
  check(!ducked, "a faulted source's routed kick ducks nothing");
}

void malformedNotesDuckNothing() {
  const auto undisturbed = duckRun(0);
  double e = 0;
  for (const float v : undisturbed)
    e += static_cast<double>(v) * v;
  check(e > 1.0, "the duck target sounds");
  const auto valid = duckRun(1);
  check(valid[1] == 0.0f && valid != undisturbed, "valid generated kick notes duck the target");
  for (std::uint32_t mode = 2; mode <= 8; ++mode)
    check(duckRun(mode) == undisturbed,
          "malformed note output (mode " + std::to_string(mode) + ") ducks nothing");
}

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
  processFaultLatches();
  resetFaultLatches();
  faultedModuleIsNotReentered();
  noAllocationInProcess();
  patternContentImportedAndDelivered();
  noteOutputValidated();
  exportRejectsMalformedNotes();
  hostileContentRejected();
  malformedNotesDuckNothing();
  routedNotesPolicy();
  std::cout << "native_rack_tests: " << checks << " checks passed\n";
  return 0;
}
