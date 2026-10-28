// Vox Demo Synth through the real Rack processor: the versioned package is
// resolved from a development package root exactly as in a DAW, the module is
// played via slot MIDI routing, its eight host-pool endpoints change the DSP,
// and projects restore bit for bit. Not DAW acceptance (see the Cubase guide).
#include "PluginProcessor.h"
#include "modules/BuiltInProvider.h"
#include "instrument/HostParameterSchema.h"
#include "instrument/native/ModulePackages.h"
#include "instrument/native/NativeModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(VOX_TEST_INTERPOSE_MALLOC)
extern "C" void voxProbeBegin(void);
extern "C" unsigned long voxProbeEnd(void);
#endif

namespace {
using namespace vstengine;
namespace fs = std::filesystem;
namespace native = instrument::native;
constexpr const char *demoId = "com.ultimavox.demo-synth";
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
enum Pool { wave, gain, attack, decay, sustain, release, cutoff, resonance };
fs::path packageRoot;
int checks = 0;

void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void setEnv(const char *name, const std::string &value) {
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  if (value.empty())
    ::unsetenv(name);
  else
    ::setenv(name, value.c_str(), 1);
#endif
}

void quietBuiltIns(VstEngineAudioProcessor &p) {
  for (const char *id : {"slot01Enabled", "slot02Enabled"})
    if (auto *enabled = p.parameters().getParameter(id))
      enabled->setValueNotifyingHost(0.0f);
  p.prepareToPlay(sampleRate, blockSize);
}

std::unique_ptr<VstEngineAudioProcessor> makeProcessor(const fs::path &root) {
  auto p = std::make_unique<VstEngineAudioProcessor>(
      modules::createBuiltInProvider(),
      root.empty() ? native::LoadReport{} : native::loadPackages(root));
  quietBuiltIns(*p);
  return p;
}

int channelOf(VstEngineAudioProcessor &p, std::size_t slot) {
  char id[32];
  std::snprintf(id, sizeof(id), "slot%02zuMidiIn", slot + 1);
  const auto *value = p.parameters().getRawParameterValue(id);
  check(value != nullptr, "slot MidiIn parameter exists");
  return static_cast<int>(std::lround(value->load()));
}

std::size_t loadDemo(VstEngineAudioProcessor &p, std::size_t slot) {
  juce::String diagnostic;
  check(p.loadSlotInstrument(slot, demoId, diagnostic),
        "demo synth loads into slot: " + diagnostic.toStdString());
  check(channelOf(p, slot) >= 1, "slot got a MIDI channel");
  return slot;
}

void setPlain(VstEngineAudioProcessor &p, std::size_t slot, int poolIndex, float plain) {
  auto *parameter = p.parameters().getParameter(instrument::hostparams::poolId(slot, poolIndex));
  check(parameter != nullptr, "pool endpoint exists");
  parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
}

float plainOf(VstEngineAudioProcessor &p, std::size_t slot, int poolIndex) {
  const auto *value =
      p.parameters().getRawParameterValue(instrument::hostparams::poolId(slot, poolIndex));
  check(value != nullptr, "pool value exists");
  return value->load();
}

// A tiny MIDI score: events at (block, message) on one channel.
struct Event {
  int block;
  juce::MidiMessage message;
};

std::vector<float> render(VstEngineAudioProcessor &p, int blocks,
                          const std::vector<Event> &score = {}) {
  std::vector<float> out;
  juce::AudioBuffer<float> audio(2, blockSize);
  juce::MidiBuffer midi;
  for (int b = 0; b < blocks; ++b) {
    midi.clear();
    for (const auto &e : score)
      if (e.block == b)
        midi.addEvent(e.message, 0);
    audio.clear();
    p.processBlock(audio, midi);
    out.insert(out.end(), audio.getReadPointer(0), audio.getReadPointer(0) + blockSize);
  }
  return out;
}

double energy(const std::vector<float> &x, int fromBlock, int toBlock) {
  double sum = 0;
  for (int i = fromBlock * blockSize; i < toBlock * blockSize && i < static_cast<int>(x.size());
       ++i)
    sum += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
  return sum;
}

// Rising zero crossings per second over the given block range.
double frequency(const std::vector<float> &x, int fromBlock, int toBlock) {
  int crossings = 0;
  const int begin = fromBlock * blockSize + 1, end = toBlock * blockSize;
  int first = -1, last = -1;
  for (int i = begin; i < end; ++i)
    if (x[static_cast<std::size_t>(i - 1)] <= 0.0f && x[static_cast<std::size_t>(i)] > 0.0f) {
      if (first < 0)
        first = i;
      last = i;
      ++crossings;
    }
  return crossings > 1 ? (crossings - 1) * sampleRate / (last - first) : 0.0;
}

int blocksFor(double seconds) {
  return static_cast<int>(std::ceil(seconds * sampleRate / blockSize));
}

using juce::MidiMessage;

// ------------------------------------------------------------------ tests
void loadsFromDevelopmentRootLikeADaw() {
  setEnv("VOX_DEV_NATIVE_MODULES", "");
  setEnv("VOX_DEV_INSTRUMENT_ROOT", packageRoot.string());
  auto p = std::make_unique<VstEngineAudioProcessor>(); // the DAW's constructor
  setEnv("VOX_DEV_INSTRUMENT_ROOT", "");
  check(p->nativeModuleDiagnostics().empty(), "demo package loads without diagnostics");
  const instrument::InstrumentDescriptor *demo = nullptr;
  for (const auto *d : p->availableInstruments())
    if (d->id == demoId)
      demo = d;
  check(demo != nullptr, "Vox Demo Synth offered in the slot catalog");
  check(demo->providerId == native::providerId && demo->directParameters &&
            demo->parameters.size() == 8,
        "eight typed host-exposed parameters");
  const char *ids[] = {"wave", "gain", "attack", "decay", "sustain", "release", "cutoff",
                       "resonance"};
  for (int i = 0; i < 8; ++i)
    check(demo->parameters[static_cast<std::size_t>(i)].id == ids[i] &&
              demo->parameters[static_cast<std::size_t>(i)].poolIndex == i,
          std::string("stable pool index for ") + ids[i]);
  // As the guide does in the editor: replace slot 1's built-in instrument;
  // the slot keeps CH1 and the editor enables it.
  quietBuiltIns(*p);
  const auto slot = loadDemo(*p, 0);
  if (auto *enabled = p->parameters().getParameter("slot01Enabled"))
    enabled->setValueNotifyingHost(1.0f);
  check(channelOf(*p, slot) == 1, "replacing slot 1 keeps MIDI channel 1");
  const auto out = render(*p, 8, {{1, MidiMessage::noteOn(1, 60, 1.0f)}});
  check(energy(out, 0, 1) == 0.0 && energy(out, 2, 8) > 1.0, "MIDI NoteOn on CH1 is audible");
}

// Owner acceptance path on a fresh (empty) Rack: Demo Synth in empty slot 5
// takes CH1 and plays; the package changes no host parameter identity.
void freshRackSlot5AndHostIdentity() {
  auto with = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                        native::loadPackages(packageRoot));
  auto without = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                           native::LoadReport{});
  with->prepareToPlay(sampleRate, blockSize);
  const auto slot = loadDemo(*with, 4);
  check(channelOf(*with, slot) == 1, "Demo Synth in empty slot 5 takes CH1");
  const auto out = render(*with, 8, {{1, MidiMessage::noteOn(1, 60, 1.0f)}});
  check(energy(out, 2, 8) > 1.0, "slot 5 Demo Synth plays MIDI on CH1");
  const auto &a = with->getParameters();
  const auto &b = without->getParameters();
  bool same = a.size() == b.size();
  for (int i = 0; same && i < a.size(); ++i) {
    const auto *pa = dynamic_cast<const juce::AudioProcessorParameterWithID *>(a[i]);
    const auto *pb = dynamic_cast<const juce::AudioProcessorParameterWithID *>(b[i]);
    same = pa != nullptr && pb != nullptr && pa->paramID == pb->paramID;
  }
  check(same, "an installed package adds or renames no host parameter");
}

void pitchAndVelocity() {
  auto p = makeProcessor(packageRoot);
  const auto slot = loadDemo(*p, 2);
  const int ch = channelOf(*p, slot);
  setPlain(*p, slot, wave, 0.0f); // sine
  setPlain(*p, slot, cutoff, 20000.0f);
  setPlain(*p, slot, resonance, 0.0f);
  const auto a4 = render(*p, 40, {{0, MidiMessage::noteOn(ch, 69, 1.0f)}});
  check(std::abs(frequency(a4, 4, 40) - 440.0) < 1.0, "note 69 sounds at 440 Hz");
  render(*p, 40, {{0, MidiMessage::noteOff(ch, 69)}});
  const auto a5 = render(*p, 40, {{0, MidiMessage::noteOn(ch, 81, 1.0f)}});
  check(std::abs(frequency(a5, 4, 40) - 880.0) < 2.0, "note 81 sounds at 880 Hz");
  render(*p, 40, {{0, MidiMessage::noteOff(ch, 81)}});

  const auto loud = render(*p, 20, {{0, MidiMessage::noteOn(ch, 60, (juce::uint8)127)}});
  render(*p, 40, {{0, MidiMessage::noteOff(ch, 60)}});
  const auto soft = render(*p, 20, {{0, MidiMessage::noteOn(ch, 60, (juce::uint8)32)}});
  render(*p, 40, {{0, MidiMessage::noteOff(ch, 60)}});
  const double ratio = energy(loud, 10, 20) / energy(soft, 10, 20);
  check(std::abs(ratio - (127.0 / 32.0) * (127.0 / 32.0)) < 0.5, "velocity scales amplitude");

  // Last-note priority: releasing the top key returns to the held one.
  const auto legato = render(*p, 60, {{0, MidiMessage::noteOn(ch, 69, 1.0f)},
                                      {20, MidiMessage::noteOn(ch, 81, 1.0f)},
                                      {40, MidiMessage::noteOff(ch, 81)}});
  check(std::abs(frequency(legato, 24, 40) - 880.0) < 2.0 &&
            std::abs(frequency(legato, 44, 60) - 440.0) < 1.0,
        "mono voice returns to the held key");
  render(*p, 40, {{0, MidiMessage::noteOff(ch, 69)}});
}

void releaseSustainAndAllOff() {
  auto p = makeProcessor(packageRoot);
  const auto slot = loadDemo(*p, 2);
  const int ch = channelOf(*p, slot);
  const int r = blocksFor(0.3); // default release
  const auto off = render(*p, 20 + r + 4, {{0, MidiMessage::noteOn(ch, 60, 1.0f)},
                                           {20, MidiMessage::noteOff(ch, 60)}});
  check(energy(off, 20, 21) > 0.1, "NoteOff starts an audible release tail");
  check(energy(off, 20 + r + 1, 20 + r + 4) == 0.0, "release reaches silence");

  setPlain(*p, slot, release, 1.0f);
  const auto longer = render(*p, 20 + blocksFor(1.0) + 4,
                             {{0, MidiMessage::noteOn(ch, 60, 1.0f)},
                              {20, MidiMessage::noteOff(ch, 60)}});
  check(energy(longer, 20 + r + 1, 20 + r + 4) > 0.01 &&
            energy(longer, 20 + blocksFor(1.0) + 1, 20 + blocksFor(1.0) + 4) == 0.0,
        "Release endpoint sets the tail length");
  setPlain(*p, slot, release, 0.3f);

  const auto pedal = render(*p, 80, {{0, MidiMessage::controllerEvent(ch, 64, 127)},
                                     {1, MidiMessage::noteOn(ch, 60, 1.0f)},
                                     {10, MidiMessage::noteOff(ch, 60)},
                                     {50, MidiMessage::controllerEvent(ch, 64, 0)}});
  check(energy(pedal, 40, 50) > 1.0, "sustain pedal holds a released key");
  check(energy(pedal, 50 + r + 1, 80) == 0.0, "pedal up releases the held key");

  const auto cc123 = render(*p, 20 + r + 4, {{0, MidiMessage::noteOn(ch, 60, 1.0f)},
                                             {20, MidiMessage::allNotesOff(ch)}});
  check(energy(cc123, 20, 21) > 0.1 && energy(cc123, 20 + r + 1, 20 + r + 4) == 0.0,
        "CC123 releases normally");
  const auto cc120 = render(*p, 24, {{0, MidiMessage::noteOn(ch, 60, 1.0f)},
                                     {20, MidiMessage::allSoundOff(ch)}});
  check(energy(cc120, 19, 20) > 1.0 && energy(cc120, 20, 24) == 0.0,
        "CC120 silences immediately");

  render(*p, 20, {{0, MidiMessage::noteOn(ch, 60, 1.0f)}});
  p->requestPanic();
  check(energy(render(*p, 4), 0, 4) == 0.0, "Rack Panic silences the slot");
  check(energy(render(*p, 10, {{0, MidiMessage::noteOn(ch, 62, 1.0f)}}), 1, 10) > 1.0,
        "plays again after Panic");
}

// Every endpoint, moved through the host pool, audibly changes the DSP.
void parametersReachTheDsp() {
  auto p = makeProcessor(packageRoot);
  const auto slot = loadDemo(*p, 2);
  const int ch = channelOf(*p, slot);
  auto note = [&] {
    auto out = render(*p, 40, {{0, MidiMessage::noteOn(ch, 57, 1.0f)}});
    render(*p, 40, {{0, MidiMessage::allSoundOff(ch)}});
    return out;
  };
  const auto base = note();
  check(energy(base, 0, 40) > 1.0, "default patch is audible");
  struct Move {
    int pool;
    float value;
    const char *name;
  } moves[] = {{wave, 0.0f, "Wave"},     {gain, 0.25f, "Gain"},       {attack, 0.5f, "Attack"},
               {decay, 0.02f, "Decay"},  {sustain, 0.2f, "Sustain"}, {cutoff, 300.0f, "Cutoff"},
               {resonance, 0.95f, "Resonance"}};
  for (const auto &m : moves) {
    const float before = plainOf(*p, slot, m.pool);
    setPlain(*p, slot, m.pool, m.value);
    check(std::abs(plainOf(*p, slot, m.pool) - m.value) < 1e-3f * std::max(1.0f, m.value),
          std::string(m.name) + " endpoint holds the plain value");
    const auto moved = note();
    check(moved != base, std::string(m.name) + " changes the sound");
    setPlain(*p, slot, m.pool, before);
  }
  check(note() == base, "restoring every endpoint restores the sound");
  setPlain(*p, slot, gain, 0.0f);
  check(energy(note(), 0, 40) == 0.0, "Gain 0 is silent");
  setPlain(*p, slot, gain, 0.7f);
  setPlain(*p, slot, cutoff, 200.0f);
  check(energy(note(), 20, 40) < 0.5 * energy(base, 20, 40), "low Cutoff darkens the saw");
}

void slotsAreIndependent() {
  auto single = makeProcessor(packageRoot);
  const auto a = loadDemo(*single, 3);
  auto pair = makeProcessor(packageRoot);
  loadDemo(*pair, 3);
  const auto b = loadDemo(*pair, 4);
  setPlain(*pair, b, wave, 2.0f);
  setPlain(*pair, b, cutoff, 300.0f);
  const auto x = render(*single, 30, {{1, MidiMessage::noteOn(channelOf(*single, a), 60, 1.0f)}});
  const auto y = render(*pair, 30, {{1, MidiMessage::noteOn(channelOf(*pair, a), 60, 1.0f)}});
  check(energy(x, 0, 30) > 1.0 && x == y,
        "a second demo slot with other settings does not change the first");
  const auto z = render(*pair, 30, {{1, MidiMessage::noteOn(channelOf(*pair, b), 60, 1.0f)}});
  check(energy(z, 2, 30) > 1.0, "the second slot plays on its own channel");
}

void projectsRestore() {
  auto first = makeProcessor(packageRoot);
  const auto slot = loadDemo(*first, 5);
  const float values[] = {2.0f, 0.55f, 0.02f, 0.4f, 0.35f, 0.8f, 1800.0f, 0.6f};
  for (int i = 0; i < 8; ++i)
    setPlain(*first, slot, i, values[i]);
  render(*first, 2);
  juce::MemoryBlock state;
  first->getStateInformation(state);

  auto second = makeProcessor(packageRoot);
  second->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  check(second->instrumentRack().state()[slot].instrumentId == demoId, "slot identity restored");
  check(channelOf(*second, slot) == channelOf(*first, slot), "MIDI channel restored");
  for (int i = 0; i < 8; ++i)
    check(std::abs(plainOf(*second, slot, i) - plainOf(*first, slot, i)) < 1e-6f,
          "endpoint " + std::to_string(i) + " restored");
  first->prepareToPlay(sampleRate, blockSize);
  second->prepareToPlay(sampleRate, blockSize);
  const std::vector<Event> phrase = {{1, MidiMessage::noteOn(channelOf(*first, slot), 48, 0.8f)},
                                     {12, MidiMessage::noteOff(channelOf(*first, slot), 48)}};
  const auto a = render(*first, 40, phrase);
  const auto b = render(*second, 40, phrase);
  check(energy(a, 0, 40) > 1.0 && a == b, "reopened project renders bit for bit");

  // Package removed: the slot keeps its identity and values, silently.
  auto without = makeProcessor({});
  without->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  check(without->instrumentRack().state()[slot].instrumentId == demoId,
        "missing package keeps the slot identity");
  check(energy(render(*without, 20, phrase), 0, 20) == 0.0, "missing package slot is silent");
  juce::MemoryBlock resaved;
  without->getStateInformation(resaved);
  auto back = makeProcessor(packageRoot);
  back->setStateInformation(resaved.getData(), static_cast<int>(resaved.getSize()));
  back->prepareToPlay(sampleRate, blockSize);
  check(render(*back, 40, phrase) == a, "restored package recovers the same sound");
}

void malformedPackagesAreRejected(const fs::path &scratch) {
  const auto source = native::resolvePackages(packageRoot);
  check(source.modules.size() == 1, "one demo package in the root");
  const auto versionDir = source.modules.front().parent_path().parent_path().parent_path();
  auto clone = [&](const char *name) {
    const auto root = scratch / name;
    fs::remove_all(root);
    const auto target = root / versionDir.parent_path().filename() / versionDir.filename();
    fs::create_directories(target);
    fs::copy(versionDir, target, fs::copy_options::recursive);
    return std::make_pair(root, target);
  };
  {
    auto [root, dir] = clone("bad-sha");
    std::ifstream in(dir / "manifest.json");
    std::string text((std::istreambuf_iterator<char>(in)), {});
    in.close();
    const auto at = text.find("\"sha256\": \"") + 11;
    text[at] = text[at] == '0' ? '1' : '0';
    std::ofstream(dir / "manifest.json", std::ios::trunc) << text;
    const auto report = native::loadPackages(root);
    check(report.providers.empty() && !report.diagnostics.empty(),
          "wrong SHA-256 is rejected with a diagnostic");
  }
  {
    auto [root, dir] = clone("truncated");
    const auto module = fs::path(dir) / fs::relative(source.modules.front(), versionDir);
    fs::resize_file(module, 512);
    const auto report = native::loadPackages(root);
    check(report.providers.empty() && !report.diagnostics.empty(),
          "corrupt module is rejected with a diagnostic");
    auto p = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                       native::loadPackages(root));
    check(!p->nativeModuleDiagnostics().empty(), "Rack still starts and reports the package");
  }
}

// The module itself rejects malformed host input without touching state.
void moduleRejectsInvalidInput() {
  const auto selection = native::resolvePackages(packageRoot);
  std::string diagnostic;
  auto module = native::NativeModule::acquire(selection.modules.front(), diagnostic);
  check(module != nullptr, "module acquired: " + diagnostic);
  const auto &t = module->table();
  const VoxCreateParamsV2 params{sizeof(VoxCreateParamsV2), VOX_MODULE_ABI_V2,
                                 VOX_SLOT_INDEX_NONE, 0};
  VoxInstance s = nullptr;
  check(t.create("com.ultimavox.other", &params, &s) == VOX_E_INVALID_ARG && s == nullptr,
        "unknown instrument id rejected");
  check(t.create(demoId, &params, &s) == VOX_OK && s != nullptr, "create");
  VoxPrepareSpecV2 spec{sizeof(VoxPrepareSpecV2), VOX_MODULE_ABI_V2, sampleRate, 64, 2, 8, 64};
  auto bad = spec;
  bad.sampleRate = std::nan("");
  check(t.prepare(s, &bad) == VOX_E_INVALID_ARG, "NaN sample rate rejected");
  bad = spec;
  bad.structSize = 8;
  check(t.prepare(s, &bad) == VOX_E_INVALID_ARG, "short prepare spec rejected");
  check(t.prepare(s, &spec) == VOX_OK, "prepare");

  float left[64], right[64];
  float *outputs[] = {left, right};
  const VoxTransportV2 transport{sizeof(VoxTransportV2), VOX_MODULE_ABI_V2, 0, 0, 0, 0,
                                 0, 0, 0, 0, 0, 0};
  VoxMidiEventV2 on{};
  on.size = 3;
  on.bytes[0] = 0x90;
  on.bytes[1] = 60;
  on.bytes[2] = 100;
  VoxProcessContextV2 c{sizeof(VoxProcessContextV2), VOX_MODULE_ABI_V2, 64, 2, outputs,
                        nullptr, 0, sizeof(VoxParamChangeV2), &on, 1, sizeof(VoxMidiEventV2),
                        0, 0, 0, &transport};
  auto x = c;
  x.numSamples = 65;
  check(t.process(s, &x) == VOX_E_INVALID_ARG, "block above prepared maximum rejected");
  x = c;
  x.midiEventStride = 3;
  check(t.process(s, &x) == VOX_E_INVALID_ARG, "misaligned MIDI stride rejected");
  x = c;
  x.midiEventCount = 65;
  check(t.process(s, &x) == VOX_E_INVALID_ARG, "MIDI above prepared budget rejected");
  x = c;
  x.transport = nullptr;
  check(t.process(s, &x) == VOX_E_INVALID_ARG, "missing transport rejected");
  x = c;
  x.releaseFlags = 0x80000000u;
  check(t.process(s, &x) == VOX_E_INVALID_ARG, "unknown release flag rejected");
  check(t.process(s, &c) == VOX_OK, "valid block processes");

  float values[8] = {1, 0.7f, 0.005f, 0.25f, 0.7f, 0.3f, 4000, 0.2f};
  values[0] = 1.5f;
  check(t.setAllParameters(s, values, 8) == VOX_E_INVALID_ARG, "fractional choice rejected");
  values[0] = 1;
  values[6] = 30000;
  check(t.setAllParameters(s, values, 8) == VOX_E_INVALID_ARG, "out-of-range cutoff rejected");
  std::uint8_t state[64];
  std::uint32_t written = 0, version = 0;
  check(t.saveState(s, state, 8, &written, &version) == VOX_E_BUFFER_TOO_SMALL && written == 40,
        "small state buffer reports the size");
  check(t.saveState(s, state, sizeof(state), &written, &version) == VOX_OK && version == 1,
        "save state v1");
  check(t.loadState(s, 2, state, written) == VOX_E_STATE_VERSION, "future state version rejected");
  state[0] ^= 0xFF;
  check(t.loadState(s, 1, state, written) == VOX_E_STATE_CORRUPT, "corrupt state rejected");
  check(t.destroy(s) == VOX_OK, "destroy");
}

void noAllocationWhilePlaying() {
  auto p = makeProcessor(packageRoot);
  const auto slot = loadDemo(*p, 7);
  const int ch = channelOf(*p, slot);
  juce::AudioBuffer<float> audio(2, blockSize);
  juce::MidiBuffer midi;
  midi.ensureSize(512);
  render(*p, 4, {{0, MidiMessage::noteOn(ch, 60, 1.0f)}}); // warm up
#if defined(VOX_TEST_INTERPOSE_MALLOC)
  voxProbeBegin();
#endif
  for (int b = 0; b < 300; ++b) {
    midi.clear();
    if (b % 20 == 0)
      midi.addEvent(MidiMessage::noteOn(ch, 48 + b % 24, 0.8f), 0);
    if (b % 20 == 10)
      midi.addEvent(MidiMessage::noteOff(ch, 48 + (b - 10) % 24), 7);
    if (b % 50 == 25)
      midi.addEvent(MidiMessage::controllerEvent(ch, 64, b % 100 < 50 ? 127 : 0), 3);
    if (auto *c = p->parameters().getParameter(instrument::hostparams::poolId(slot, cutoff)))
      c->setValue(static_cast<float>(b % 10) / 10.0f);
    p->processBlock(audio, midi);
  }
#if defined(VOX_TEST_INTERPOSE_MALLOC)
  const auto allocations = voxProbeEnd();
  check(allocations == 0, "demo slot processing allocates nothing (" +
                              std::to_string(allocations) + ")");
  std::cout << "allocation probe: 0 allocations in 300 Rack blocks with Vox Demo Synth\n";
#else
  std::cout << "allocation probe: not available on this platform\n";
#endif
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: demo_synth_tests <demo package root> <scratch dir>\n";
    return 2;
  }
  packageRoot = fs::absolute(argv[1]);
  const fs::path scratch = fs::absolute(argv[2]);
  juce::ScopedJuceInitialiser_GUI gui;
  loadsFromDevelopmentRootLikeADaw();
  freshRackSlot5AndHostIdentity();
  pitchAndVelocity();
  releaseSustainAndAllOff();
  parametersReachTheDsp();
  slotsAreIndependent();
  projectsRestore();
  malformedPackagesAreRejected(scratch);
  moduleRejectsInvalidInput();
  noAllocationWhilePlaying();
  std::cout << "demo_synth_tests: " << checks << " checks passed\n";
  return 0;
}
