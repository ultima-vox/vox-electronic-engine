// #75 first slice: the Vox Demo Synth rebuilt as a graph-only patch
// (instruments/demo-synth-graph/graph.json, no DLL) renders bit for bit like
// the native VoxDemoSynth module through the real Rack processor: same pool
// endpoints, MIDI routing, automation, sustain/CC120/CC123, two slots and
// project reopen. The compiler rejects malformed patches with a reason.
// Not DAW acceptance (see docs/testing/demo-synth-graph/CUBASE_SMOKE.md).
#include "PluginProcessor.h"
#include "dspcore/GraphInstrument.h"
#include "instrument/HostParameterSchema.h"
#include "instrument/native/ModulePackages.h"
#include "modules/BuiltInProvider.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace vstengine;
namespace fs = std::filesystem;
namespace native = instrument::native;
using juce::MidiMessage;
constexpr const char *nativeId = "com.ultimavox.demo-synth";
constexpr const char *graphId = "com.ultimavox.demo-synth-graph";
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
fs::path packageRoot;
std::string graphJson;
int checks = 0;

void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

native::LoadReport graphReport() {
  std::string why;
  auto patch = dspcore::loadPatch(graphJson, why);
  check(patch != nullptr, "graph.json compiles: " + why);
  native::LoadReport report;
  report.providers.push_back(dspcore::makeGraphProvider(std::move(patch)));
  return report;
}

std::unique_ptr<VstEngineAudioProcessor> makeProcessor(bool graph) {
  auto p = std::make_unique<VstEngineAudioProcessor>(
      modules::createBuiltInProvider(), graph ? graphReport() : native::loadPackages(packageRoot));
  check(p->nativeModuleDiagnostics().empty(), "providers register without diagnostics");
  p->prepareToPlay(sampleRate, blockSize);
  return p;
}

int channelOf(VstEngineAudioProcessor &p, std::size_t slot) {
  char id[32];
  std::snprintf(id, sizeof(id), "slot%02zuMidiIn", slot + 1);
  return static_cast<int>(std::lround(p.parameters().getRawParameterValue(id)->load()));
}

void load(VstEngineAudioProcessor &p, std::size_t slot, const char *id) {
  juce::String diagnostic;
  check(p.loadSlotInstrument(slot, id, diagnostic),
        std::string(id) + " loads: " + diagnostic.toStdString());
}

void setPlain(VstEngineAudioProcessor &p, std::size_t slot, int pool, float plain) {
  auto *parameter = p.parameters().getParameter(instrument::hostparams::poolId(slot, pool));
  check(parameter != nullptr, "pool endpoint exists");
  parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
}

// One step of a deterministic performance: MIDI and endpoint moves per block.
struct Step {
  std::vector<MidiMessage> midi;
  std::vector<std::pair<int, float>> moves; // pool, plain
};

std::vector<Step> performance(int channel, int blocks, unsigned seed) {
  std::mt19937 rng(seed);
  const float lo[8] = {0, 0, 0.001f, 0.001f, 0, 0.01f, 20, 0};
  const float hi[8] = {2, 1, 0.3f, 0.3f, 1, 0.5f, 20000, 1};
  std::vector<Step> steps(static_cast<std::size_t>(blocks));
  for (int b = 0; b < blocks; ++b) {
    auto &s = steps[static_cast<std::size_t>(b)];
    if (b % 9 == 0)
      s.midi.push_back(MidiMessage::noteOn(channel, 40 + static_cast<int>(rng() % 40),
                                           static_cast<juce::uint8>(1 + rng() % 127)));
    if (b % 13 == 4)
      s.midi.push_back(MidiMessage::noteOff(channel, 40 + static_cast<int>(rng() % 40)));
    if (b % 101 == 50)
      s.midi.push_back(MidiMessage::controllerEvent(channel, 64, (rng() % 2) ? 127 : 0));
    if (b % 211 == 100)
      s.midi.push_back((rng() % 2) ? MidiMessage::allSoundOff(channel)
                                   : MidiMessage::allNotesOff(channel));
    if (b % 37 == 5) {
      const int k = static_cast<int>(rng() % 8);
      float v = lo[k] + (hi[k] - lo[k]) * static_cast<float>(rng() % 1000) / 1000.0f;
      if (k == 0)
        v = static_cast<float>(rng() % 3);
      s.moves.push_back({k, v});
    }
  }
  return steps;
}

std::vector<float> play(VstEngineAudioProcessor &p, const std::vector<std::size_t> &slots,
                        const std::vector<std::vector<Step>> &scores) {
  std::vector<float> out;
  juce::AudioBuffer<float> audio(2, blockSize);
  juce::MidiBuffer midi;
  const auto blocks = scores.front().size();
  for (std::size_t b = 0; b < blocks; ++b) {
    midi.clear();
    for (std::size_t s = 0; s < slots.size(); ++s) {
      for (const auto &[pool, v] : scores[s][b].moves)
        setPlain(p, slots[s], pool, v);
      int offset = 0;
      for (const auto &m : scores[s][b].midi)
        midi.addEvent(m, (offset += 17) % blockSize);
    }
    audio.clear();
    p.processBlock(audio, midi);
    for (int ch = 0; ch < 2; ++ch)
      out.insert(out.end(), audio.getReadPointer(ch), audio.getReadPointer(ch) + blockSize);
  }
  return out;
}

double energy(const std::vector<float> &x) {
  double sum = 0;
  for (float v : x)
    sum += static_cast<double>(v) * v;
  return sum;
}

// ------------------------------------------------------------------ tests
void descriptorMatchesNative() {
  auto n = makeProcessor(false);
  auto g = makeProcessor(true);
  const instrument::InstrumentDescriptor *a = nullptr, *b = nullptr;
  for (const auto *d : n->availableInstruments())
    if (d->id == nativeId)
      a = d;
  for (const auto *d : g->availableInstruments())
    if (d->id == graphId)
      b = d;
  check(a != nullptr && b != nullptr, "both instruments in the slot catalog");
  check(b->providerId == dspcore::graphProviderId && b->directParameters, "graph provider id");
  check(a->parameters.size() == b->parameters.size(), "same endpoint count");
  for (std::size_t i = 0; i < a->parameters.size(); ++i) {
    const auto &x = a->parameters[i];
    const auto &y = b->parameters[i];
    check(x.id == y.id && x.poolIndex == y.poolIndex && x.type == y.type &&
              x.minimum == y.minimum && x.maximum == y.maximum &&
              x.defaultValue == y.defaultValue && x.choices == y.choices,
          "endpoint '" + x.id + "' identical to the native Demo");
  }
}

void bitExactPerformance() {
  auto n = makeProcessor(false);
  auto g = makeProcessor(true);
  load(*n, 2, nativeId);
  load(*n, 6, nativeId);
  load(*g, 2, graphId);
  load(*g, 6, graphId);
  check(channelOf(*n, 2) == channelOf(*g, 2) && channelOf(*n, 6) == channelOf(*g, 6),
        "same MIDI channels");
  const std::vector<std::vector<Step>> scores = {performance(channelOf(*n, 2), 3000, 7),
                                                 performance(channelOf(*n, 6), 3000, 11)};
  const auto a = play(*n, {2, 6}, scores);
  const auto b = play(*g, {2, 6}, scores);
  check(energy(a) > 100.0, "the performance is audible");
  std::size_t firstDiff = a.size();
  for (std::size_t i = 0; i < a.size() && firstDiff == a.size(); ++i)
    if (a[i] != b[i])
      firstDiff = i;
  check(a.size() == b.size() && firstDiff == a.size(),
        "graph Demo == native Demo bit for bit (first difference at sample " +
            std::to_string(firstDiff) + ")");
}

void projectReopens() {
  auto first = makeProcessor(true);
  load(*first, 4, graphId);
  const float values[] = {2.0f, 0.55f, 0.02f, 0.4f, 0.35f, 0.8f, 1800.0f, 0.6f};
  for (int i = 0; i < 8; ++i)
    setPlain(*first, 4, i, values[i]);
  juce::MemoryBlock state;
  first->getStateInformation(state);
  auto second = makeProcessor(true);
  second->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  check(second->instrumentRack().state()[4].instrumentId == graphId, "slot identity restored");
  first->prepareToPlay(sampleRate, blockSize);
  second->prepareToPlay(sampleRate, blockSize);
  const auto score = performance(channelOf(*first, 4), 400, 3);
  const auto a = play(*first, {4}, {score});
  const auto b = play(*second, {4}, {score});
  check(energy(a) > 1.0 && a == b, "reopened graph project renders bit for bit");
}

// What Cubase gets: the DAW's constructor offers the factory graph Demo with
// no DLL and no install step; on a fresh Rack, empty slot 5 takes CH1 and plays.
void factoryGraphInDefaultRack() {
  auto p = std::make_unique<VstEngineAudioProcessor>();
  check(p->nativeModuleDiagnostics().empty(), "factory graphs compile without diagnostics");
  const instrument::InstrumentDescriptor *demo = nullptr;
  for (const auto *d : p->availableInstruments())
    if (d->id == graphId)
      demo = d;
  check(demo != nullptr && demo->name == "Vox Demo Synth (Graph)",
        "Vox Demo Synth (Graph) offered in the slot catalog");
  p->prepareToPlay(sampleRate, blockSize);
  load(*p, 4, graphId);
  check(channelOf(*p, 4) == 1, "graph Demo in empty slot 5 takes CH1");
  Step note;
  note.midi.push_back(MidiMessage::noteOn(1, 60, 1.0f));
  std::vector<Step> score(8);
  score[1] = note;
  check(energy(play(*p, {4}, {score})) > 1.0, "graph Demo plays MIDI on CH1");
}

void compilerRejectsMalformedPatches() {
  auto rejectsBecause = [](std::string text, const std::string &from, const std::string &to,
                           const std::string &what, const std::string &reason) {
    const auto at = text.find(from);
    check(at != std::string::npos, "fixture anchor for " + what);
    text.replace(at, from.size(), to);
    std::string why;
    check(dspcore::loadPatch(text, why) == nullptr && !why.empty() &&
              why.find(reason) != std::string::npos,
          what + " is rejected for '" + reason + "' (" + why + ")");
  };
  auto rejects = [&](const std::string &text, const std::string &from, const std::string &to,
                     const std::string &what) { rejectsBecause(text, from, to, what, ""); };
  rejects(graphJson, "\"env.adsr@1\"", "\"env.adsr@9\"", "unknown block version");
  rejects(graphJson, "\"osc.blep@1\"", "\"osc.fm@1\"", "unknown block");
  rejects(graphJson, "\"in\": \"osc.out\"", "\"in\": \"vca.out\"", "a cycle");
  rejects(graphJson, "\"in\": \"osc.out\"", "\"in\": \"env.out\"", "control into audio");
  rejects(graphJson, "\"in\": \"osc.out\"", "\"in\": \"nope.out\"", "a dangling input");
  rejects(graphJson, "\"pool\": 7", "\"pool\": 6", "a duplicate pool index");
  rejects(graphJson, "\"cutoff\": \"cutoff\"", "\"cutoff\": \"missing\"", "an unknown parameter");
  rejects(graphJson, "\"output\": \"out.out\"", "\"output\": \"env.out\"", "a control output");
  rejects(graphJson, "\"graphVersion\": 1", "\"graphVersion\": 2", "a future graph version");
  rejects(graphJson, "\"default\": 0.2, \"pool\": 7", "\"default\": 5, \"pool\": 7",
          "a default outside the range");
  rejects(graphJson, "\"mono-last\"", "\"poly-8\"", "an unsupported voice policy");
  rejects(graphJson, "\"graphVersion\"", "graphVersion", "malformed JSON");

  struct Case {
    const char *from, *to, *what, *reason;
  };
  // Block parameter domains cover the whole host range, not just the default.
  constexpr const char *outside = "is outside";
  constexpr const char *cutoff = "\"min\": 20, \"max\": 20000, \"default\": 4000";
  constexpr const char *attack = "\"min\": 0.001, \"max\": 5, \"default\": 0.005";
  constexpr const char *decay = "\"min\": 0.001, \"max\": 5, \"default\": 0.25";
  constexpr const char *release = "\"min\": 0.001, \"max\": 10, \"default\": 0.3";
  constexpr const char *sustain = "\"min\": 0, \"max\": 1, \"default\": 0.7, \"pool\": 4";
  constexpr const char *resonance = "\"min\": 0, \"max\": 1, \"default\": 0.2";
  constexpr const char *gain = "\"min\": 0, \"max\": 1, \"default\": 0.7, \"pool\": 1";
  constexpr const char *wave = "\"Square\"], \"min\": 0, \"max\": 2";
  // Numbers that would overflow the C++ destination type are rejected too.
  constexpr const char *graphVersion = "\"graphVersion\": 1";
  constexpr const char *version = "\"version\": 1";
  constexpr const char *entries = "parameter entries need";
  const Case cases[] = {
      {cutoff, "\"min\": -20000, \"max\": 20000, \"default\": -12000", "a negative cutoff",
       outside},
      {cutoff, "\"min\": 20, \"max\": 30000, \"default\": 4000", "a cutoff over 20 kHz", outside},
      {attack, "\"min\": 0, \"max\": 5, \"default\": 0.005", "a zero attack", outside},
      {decay, "\"min\": -1, \"max\": 5, \"default\": 0.25", "a negative decay", outside},
      {release, "\"min\": 0.001, \"max\": 100, \"default\": 0.3", "a release over 60 s", outside},
      {sustain, "\"min\": 0, \"max\": 1.5, \"default\": 0.7, \"pool\": 4", "a sustain over 1",
       outside},
      {resonance, "\"min\": 0, \"max\": 2, \"default\": 0.2", "a resonance over 1", outside},
      {wave, "\"Square\", \"X\"], \"min\": 0, \"max\": 3", "an unsupported wave", outside},
      {"\"type\": \"choice\"", "\"type\": \"float\"", "a continuous wave", "needs an int"},
      {gain, "\"min\": -1, \"max\": 1, \"default\": 0.7, \"pool\": 1", "a negative gain", outside},
      {graphVersion, "\"graphVersion\": 1e100", "graphVersion 1e100", "graphVersion must be"},
      {graphVersion, "\"graphVersion\": 2147483648", "graphVersion 2^31", "graphVersion must be"},
      {graphVersion, "\"graphVersion\": -1", "graphVersion -1", "graphVersion must be"},
      {version, "\"version\": 1e100", "instrument version 1e100", "instrument {id"},
      {version, "\"version\": 4294967296", "instrument version 2^32", "instrument {id"},
      {version, "\"version\": 0", "instrument version 0", "instrument {id"},
      {"\"pool\": 7", "\"pool\": 1e100", "pool 1e100", entries},
      {"\"default\": 0.2, \"pool\": 7", "\"default\": 1e100, \"pool\": 7",
       "a default beyond float", entries},
      {"\"max\": 20000", "\"max\": 1e300", "a max beyond float", entries},
  };
  for (const auto &c : cases)
    rejectsBecause(graphJson, c.from, c.to, c.what, c.reason);
  {
    auto text = graphJson;
    text.replace(text.find("\"version\": 1"), 12, "\"version\": 4294967295");
    std::string why;
    check(dspcore::loadPatch(text, why) != nullptr, "instrument version 2^32-1 accepted: " + why);
  }
}

// Every host-reachable value stays finite: widen each parameter to its block's
// whole domain, then hammer corners and random values with notes and CCs.
void wholeDomainStaysFinite() {
  dspcore::PatchSpec spec;
  std::string why;
  check(dspcore::parsePatchJson(graphJson, spec, why), "graph.json parses: " + why);
  struct Range {
    const char *id;
    float lo, hi;
  } domain[] = {{"wave", 0, 2},          {"gain", 0, 4},        {"attack", 0.001f, 60},
                {"decay", 0.001f, 60},   {"sustain", 0, 1},     {"release", 0.001f, 60},
                {"cutoff", 20, 20000},   {"resonance", 0, 1}};
  for (auto &p : spec.parameters)
    for (const auto &r : domain)
      if (p.descriptor.id == r.id) {
        p.descriptor.minimum = r.lo;
        p.descriptor.maximum = r.hi;
      }
  dspcore::CompiledPatch patch;
  check(dspcore::compilePatch(spec, patch, why), "whole-domain patch compiles: " + why);
  for (const double rate : {8000.0, 44100.0, 192000.0}) {
    dspcore::PatchVoice voice(patch);
    voice.prepare(rate);
    std::mt19937 rng(static_cast<unsigned>(rate));
    double peak = 0;
    bool finite = true;
    for (int n = 0; n < 400000; ++n) {
      if (n % 2000 == 0)
        for (std::size_t k = 0; k < 8; ++k) {
          const auto &r = domain[k];
          const auto pick = rng() % 4;
          const float t = static_cast<float>(rng() % 1000) / 1000.0f;
          float v = pick == 0 ? r.lo : pick == 1 ? r.hi : r.lo + (r.hi - r.lo) * t;
          if (k == 0)
            v = std::round(v);
          if (k == 2 || k == 3)
            v = std::min(v, 0.05f); // keep envelopes moving inside the run
          voice.setParameter(k, v);
        }
      if (n % 3000 == 0) {
        const std::uint8_t on[3] = {0x90, static_cast<std::uint8_t>(rng() % 128),
                                    static_cast<std::uint8_t>(1 + rng() % 127)};
        voice.midi(on, 3);
      }
      if (n % 7000 == 0) {
        const std::uint8_t off[3] = {0x80, static_cast<std::uint8_t>(rng() % 128), 0};
        voice.midi(off, 3);
      }
      const double x = voice.next();
      finite = finite && std::isfinite(x);
      peak = std::max(peak, std::abs(x));
    }
    check(finite && peak < 100.0, "whole domain finite and bounded at " + std::to_string(rate) +
                                      " Hz (peak " + std::to_string(peak) + ")");
  }
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "usage: graph_demo_parity_tests <demo-package-root> <graph.json>\n";
    return 2;
  }
  packageRoot = argv[1];
  std::ifstream in(argv[2], std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  graphJson = text.str();
  check(!graphJson.empty(), "graph.json readable");
  juce::ScopedJuceInitialiser_GUI juce;
  descriptorMatchesNative();
  bitExactPerformance();
  projectReopens();
  factoryGraphInDefaultRack();
  compilerRejectsMalformedPatches();
  wholeDomainStaysFinite();
  std::cout << "graph_demo_parity_tests: " << checks << " checks passed\n";
  return 0;
}
