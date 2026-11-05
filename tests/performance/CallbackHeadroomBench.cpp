// Callback headroom of the real Rack VST3 processor (gate #77, release
// candidate measurements). Not a pass/fail timing gate: it reports, per
// sample rate (44.1 / 48 / 96 kHz) and host block (64 / 128 / 512), the time
// of each processBlock against its realtime budget, for two workloads:
//
//  representative  Full-on Drums (internal sequencer, a sampled Layer kick,
//                  filters, pad sends), Vox Demo Synth chords, the graph Demo
//                  Synth bass line; kick ducking, two returns, light
//                  automation (16 parameters per block)
//  declared-limit  16 loaded slots (Drums x4, Demo x6, Graph x6), the
//                  declared MIDI limit of 2048 host events per block, 128 x
//                  16 host automation (every pool endpoint of every slot
//                  changes each block), both returns, ducking and the B2 host
//                  outputs Out 2 .. Out 8 active with Drums pads routed there
//
// Results: p50 / p95 / p99 / p99.9 / max in microseconds, load = p99 /
// budget, headroom = 1 - load, and the audio-thread allocation count where
// the allocation probe is built. The process fails only on broken output
// (NaN/Inf, silence where sound is expected), a setup failure or an
// audio-thread allocation; timing is reported, not judged, because it
// depends on the machine.
//
// Usage: callback_headroom_bench <instruments> <scratch> <report.md> [--quick]
#include "PluginProcessor.h"
#include "RackAudioParameters.h"
#include "instrument/HostParameterSchema.h"
#include "instrument/native/NativeModule.h"
#include "instrument/vins/VinsPackage.h"
#include "modules/BuiltInProvider.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(VOX_TEST_INTERPOSE_MALLOC)
#include "AllocationProbe.h"
#endif
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

namespace {
// CPU time of the calling thread, in ticks: cycles on Windows
// (QueryThreadCycleTime), nanoseconds elsewhere (CLOCK_THREAD_CPUTIME_ID).
std::uint64_t threadTicks() noexcept {
#if defined(_WIN32)
  ULONG64 cycles = 0;
  QueryThreadCycleTime(GetCurrentThread(), &cycles);
  return cycles;
#else
  timespec t{};
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
  return static_cast<std::uint64_t>(t.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(t.tv_nsec);
#endif
}
using namespace vstengine;
namespace fs = std::filesystem;
using juce::MidiMessage;

constexpr const char *drumsId = "com.ultimavox.fullon-drums";
constexpr const char *demoId = "com.ultimavox.demo-synth";
constexpr const char *graphId = "com.ultimavox.demo-synth-graph";
constexpr std::size_t poolPerSlot = 128;
fs::path instrumentsDir, cacheRoot;

[[noreturn]] void fail(const std::string &message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

instrument::native::LoadReport installed() {
  instrument::vins::LoadOptions options;
  options.cacheRoot = cacheRoot;
  options.trusted = instrument::vins::pinnedPublisherKeys();
  return instrument::vins::loadFolder(instrumentsDir, options);
}

void setRaw(VstEngineAudioProcessor &p, const std::string &id, float plain) {
  auto *parameter = p.parameters().getParameter(id);
  if (parameter == nullptr)
    fail("missing parameter " + id);
  parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
}

void setUi(VstEngineAudioProcessor &p, std::size_t slot, int pad, const char *key, float v) {
  static const std::array<const char *, 8> pads{"kick", "snare", "clap", "chat",
                                                "ohat", "tom",   "rim",  "cymbal"};
  juce::String why;
  if (!p.setSlotParameter(slot, std::string(pads[static_cast<std::size_t>(pad)]) + "." + key, v,
                          why))
    fail(std::string("UI-only ") + pads[static_cast<std::size_t>(pad)] + "." + key + ": " +
         why.toStdString());
}

int channelOf(VstEngineAudioProcessor &p, std::size_t slot) {
  char id[32];
  std::snprintf(id, sizeof(id), "slot%02zuMidiIn", slot + 1);
  const auto *value = p.parameters().getRawParameterValue(id);
  if (value == nullptr)
    fail("slot MidiIn parameter");
  return static_cast<int>(std::lround(value->load()));
}

class PlayHead final : public juce::AudioPlayHead {
public:
  juce::Optional<PositionInfo> getPosition() const override {
    PositionInfo info;
    info.setBpm(145.0);
    info.setPpqPosition(ppq);
    info.setIsPlaying(true);
    info.setTimeInSamples(static_cast<juce::int64>(samples));
    return info;
  }
  double ppq = 0.0, samples = 0.0;
};

struct Workload {
  std::string name;
  std::unique_ptr<VstEngineAudioProcessor> p;
  std::vector<std::size_t> slots;      // loaded slots
  std::vector<int> channels;           // their MIDI channels
  std::vector<std::string> kinds;      // instrument per slot
  bool limit{};
  bool hostOutputs{};                  // B2 Out 2 .. Out 8 active
};

void load(Workload &w, std::size_t slot, const char *id) {
  juce::String diagnostic;
  if (!w.p->loadSlotInstrument(slot, id, diagnostic))
    fail(std::string("load ") + id + " into slot " + std::to_string(slot + 1) + ": " +
         diagnostic.toStdString());
  w.slots.push_back(slot);
  w.kinds.emplace_back(id);
}

std::unique_ptr<instrument::SampleBuffer> constantSample(std::size_t frames, float level) {
  auto buffer = std::make_unique<instrument::SampleBuffer>();
  buffer->channels.push_back(std::vector<float>(frames));
  for (std::size_t i = 0; i < frames; ++i)
    buffer->channels.back()[i] = level * std::sin(0.02f * static_cast<float>(i)) *
                                 std::exp(-static_cast<float>(i) / 4000.0f);
  buffer->sampleRate = 48000.0;
  buffer->peak = level;
  return buffer;
}

void configureDrums(Workload &w, std::size_t slot, bool hostOutputs) {
  setRaw(*w.p, instrument::hostparams::poolId(slot, 48), 1.0f); // Seq Mode Internal
  setUi(*w.p, slot, 0, "source", 2.0f);                        // Layer kick
  std::string why;
  if (!w.p->instrumentRack().setSlotSample(slot, 0, constantSample(9000, 0.6f), why))
    fail("kick sample: " + why);
  setUi(*w.p, slot, 3, "filter", 2.0f); // closed hat LP 24
  setUi(*w.p, slot, 4, "filter", 5.0f); // open hat BP
  setUi(*w.p, slot, 2, "transient_attack", 0.5f);
  setRaw(*w.p, instrument::hostparams::poolId(slot, 59 + 1 * 5 + 0), 0.4f); // snare send1
  setRaw(*w.p, instrument::hostparams::poolId(slot, 59 + 2 * 5 + 1), 0.3f); // clap send2
  if (hostOutputs)
    for (int pad = 1; pad < 8; ++pad)
      setUi(*w.p, slot, pad, "output", static_cast<float>(pad));
}

// drumSlots == 0: the mixed workloads (Drums, Demo, Graph). drumSlots > 0:
// the production-scope Rack + Drums 1.0 load, N Full-on Drums kits.
Workload makeWorkload(bool limit, int drumSlots, double rate, int block) {
  Workload w;
  w.limit = limit;
  w.hostOutputs = limit || drumSlots > 0;
  w.name = drumSlots > 0 ? "drums x" + std::to_string(drumSlots) + (limit ? " limit" : "")
                         : (limit ? "declared-limit" : "representative");
  w.p = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(), installed());
  if (w.hostOutputs) {
    // B2: every host output on before prepare.
    auto layout = w.p->getBusesLayout();
    for (int b = 1; b < layout.outputBuses.size(); ++b)
      layout.outputBuses.getReference(b) = juce::AudioChannelSet::stereo();
    if (!w.p->setBusesLayout(layout))
      fail("B2 layout");
  }
  w.p->prepareToPlay(rate, block);
  if (drumSlots > 0) {
    for (std::size_t s = 0; s < static_cast<std::size_t>(drumSlots); ++s)
      load(w, s, drumsId);
  } else if (limit) {
    for (std::size_t s = 0; s < 16; ++s)
      load(w, s, s % 4 == 0 ? drumsId : (s % 2 ? demoId : graphId));
  } else {
    for (const char *id : {"slot01Enabled", "slot02Enabled"})
      if (auto *enabled = w.p->parameters().getParameter(id))
        enabled->setValueNotifyingHost(0.0f);
    load(w, 2, drumsId);
    load(w, 3, demoId);
    load(w, 4, graphId);
  }
  for (const auto slot : w.slots)
    w.channels.push_back(channelOf(*w.p, slot));
  for (std::size_t i = 0; i < w.slots.size(); ++i)
    if (w.kinds[i] == drumsId) {
      configureDrums(w, w.slots[i], w.hostOutputs);
      // Drums-only limit: host notes must sound too (Both), not be ignored.
      if (drumSlots > 0 && limit)
        setRaw(*w.p, instrument::hostparams::poolId(w.slots[i], 48), 2.0f);
    }
  // Returns, sends and kick ducking from the first kit.
  setRaw(*w.p, host::returnParameterId(0, "type"), 1.0f);
  setRaw(*w.p, host::returnParameterId(1, "type"), 2.0f);
  setRaw(*w.p, "rack.duck.source", static_cast<float>(w.slots.front() + 1));
  setRaw(*w.p, "rack.duck.note", 36.0f);
  for (std::size_t i = 0; i < w.slots.size(); ++i)
    if (w.kinds[i] != drumsId || (drumSlots > 0 && i > 0)) {
      char depth[32];
      std::snprintf(depth, sizeof(depth), "slot%02zu.duck.depth", w.slots[i] + 1);
      setRaw(*w.p, depth, 0.7f);
      setRaw(*w.p, host::sendParameterId(w.slots[i], 0, "level"), 0.3f);
    }
  w.p->prepareToPlay(rate, block); // apply everything to a prepared Rack
  return w;
}

// Host MIDI for one block: chords and a bass line on the synth slots; at the
// declared limit, exactly 2048 events (NoteOn/NoteOff pairs and CCs over all
// channels).
void fillMidi(const Workload &w, juce::MidiBuffer &midi, long long blockIndex, int n) {
  midi.clear();
  if (w.limit) {
    // Matched triples on one channel: NoteOn (ch, note) at offset o, its
    // NoteOff (same ch, same note) at o + 1, and a CC at o. Every note
    // starts and ends inside this block, so nothing is held across blocks
    // (review 5777473229, P2). 682 triples + one final NoteOn/NoteOff pair =
    // exactly 2048 events.
    int count = 0;
    for (int k = 0; count + 2 <= 2048; ++k) {
      const int ch = w.channels[static_cast<std::size_t>(k) % w.channels.size()];
      const int note = 36 + (k * 5 + static_cast<int>(blockIndex)) % 48;
      const int on = (k * 7) % (n - 1);
      midi.addEvent(MidiMessage::noteOn(ch, note, (juce::uint8)(40 + k % 80)), on);
      midi.addEvent(MidiMessage::noteOff(ch, note), on + 1);
      count += 2;
      if (count < 2048) {
        midi.addEvent(MidiMessage::controllerEvent(ch, 1, (k + static_cast<int>(blockIndex)) % 128), on);
        ++count;
      }
    }
    return;
  }
  // Representative: every ~quarter second a new chord on Demo, 16ths on Graph.
  const long long period = 6000;
  for (std::size_t i = 0; i < w.slots.size(); ++i) {
    if (w.kinds[i] == drumsId)
      continue;
    const int ch = w.channels[i];
    for (int s = 0; s < n; ++s) {
      const long long t = blockIndex * n + s;
      if (w.kinds[i] == demoId && t % period == 0) {
        const int root = 48 + static_cast<int>((t / period) % 5) * 2;
        for (const int interval : {0, 4, 7, 11}) {
          midi.addEvent(MidiMessage::noteOff(ch, root - 2 + interval), s);
          midi.addEvent(MidiMessage::noteOn(ch, root + interval, (juce::uint8)90), s);
        }
      }
      if (w.kinds[i] == graphId && t % (period / 4) == 0) {
        const int note = 36 + static_cast<int>((t / (period / 4)) % 8);
        midi.addEvent(MidiMessage::noteOff(ch, note - 1), s);
        midi.addEvent(MidiMessage::noteOn(ch, note, (juce::uint8)100), s);
      }
    }
  }
}

struct Measurement {
  std::string workload;
  double rate{};
  int block{};
  std::size_t blocks{};
  double budgetUs{}, p50{}, p95{}, p99{}, p999{}, max{};
  std::size_t overBudget{}; // measured blocks slower than realtime
  std::size_t overWork{}, overPreempted{}; // of those: own CPU time over budget / not
  unsigned long allocations{};
  bool allocationProbe{};
};

double percentile(std::vector<double> v, double q) {
  std::sort(v.begin(), v.end());
  const auto index = static_cast<std::size_t>(std::ceil(q * static_cast<double>(v.size()))) - 1;
  return v[std::min(index, v.size() - 1)];
}

Measurement run(bool limit, int drumSlots, double rate, int block, double seconds,
                const std::string &label = {}) {
  auto w = makeWorkload(limit, drumSlots, rate, block);
  PlayHead head;
  w.p->setPlayHead(&head);
  const int total = w.p->getTotalNumOutputChannels();
  juce::AudioBuffer<float> audio(total, block);
  juce::MidiBuffer midi;
  midi.ensureSize(2048 * 8);
  // The parameters moved every block: 16 (representative) or every pool
  // endpoint of every slot (128 x 16 at the limit).
  std::vector<juce::AudioProcessorParameter *> automated;
  for (std::size_t i = 0; i < w.slots.size(); ++i)
    for (std::size_t k = 0; k < (limit ? poolPerSlot : 6); ++k)
      if (auto *parameter =
              w.p->parameters().getParameter(instrument::hostparams::poolId(w.slots[i], k)))
        automated.push_back(parameter);
  if (limit && automated.size() != poolPerSlot * w.slots.size())
    fail("expected 128 host automation endpoints per slot, found " +
         std::to_string(automated.size()));
  if (limit) {
    // The limit stimulus must be what the report says it is: 16 distinct
    // channels, exactly 2048 events, and every NoteOn released on its own
    // channel and note, later in the same block.
    std::vector<int> distinct = w.channels;
    std::sort(distinct.begin(), distinct.end());
    if (std::unique(distinct.begin(), distinct.end()) != distinct.end() ||
        distinct.size() != w.slots.size())
      fail("the limit slots must listen on distinct MIDI channels");
    for (long long probe : {0LL, 1LL, 977LL}) {
      fillMidi(w, midi, probe, block);
      std::array<std::array<int, 128>, 17> held{};
      std::array<std::array<int, 128>, 17> onAt{};
      int events = 0;
      bool ordered = true;
      for (const auto metadata : midi) {
        const auto m = metadata.getMessage();
        ++events;
        if (m.isNoteOn()) {
          ++held[static_cast<std::size_t>(m.getChannel())][static_cast<std::size_t>(m.getNoteNumber())];
          onAt[static_cast<std::size_t>(m.getChannel())][static_cast<std::size_t>(m.getNoteNumber())] =
              metadata.samplePosition;
        } else if (m.isNoteOff()) {
          auto &h = held[static_cast<std::size_t>(m.getChannel())][static_cast<std::size_t>(m.getNoteNumber())];
          ordered = ordered && h > 0 &&
                    metadata.samplePosition >
                        onAt[static_cast<std::size_t>(m.getChannel())][static_cast<std::size_t>(m.getNoteNumber())];
          --h;
        }
      }
      bool balanced = true;
      for (const auto &channel : held)
        for (const int h : channel)
          balanced = balanced && h == 0;
      if (events != 2048 || !balanced || !ordered)
        fail("limit stimulus: " + std::to_string(events) +
             " events, every NoteOff must follow its NoteOn on the same channel and note");
    }
  }
  const auto blocks = static_cast<std::size_t>(seconds * rate / block);
  const std::size_t warmup = std::max<std::size_t>(64, blocks / 20);
  std::vector<double> times, ticks;
  times.reserve(blocks);
  ticks.reserve(blocks);
  double energy = 0.0, hostOutputEnergy = 0.0;
  bool finite = true;
  Measurement r;
  r.workload = label.empty() ? w.name : w.name + " " + label;
  r.rate = rate;
  r.block = block;
  r.blocks = blocks;
  r.budgetUs = 1e6 * block / rate;
  for (std::size_t b = 0; b < warmup + blocks; ++b) {
    fillMidi(w, midi, static_cast<long long>(b), block);
    for (std::size_t k = 0; k < automated.size(); ++k)
      automated[k]->setValue(static_cast<float>((b + k) % 100) / 100.0f);
    const bool measured = b >= warmup;
#if defined(VOX_TEST_INTERPOSE_MALLOC)
    if (measured)
      voxProbeBegin();
#endif
    const auto tick0 = threadTicks();
    const auto start = std::chrono::steady_clock::now();
    w.p->processBlock(audio, midi);
    const auto end = std::chrono::steady_clock::now();
    const auto tick1 = threadTicks();
#if defined(VOX_TEST_INTERPOSE_MALLOC)
    if (measured)
      r.allocations += voxProbeEnd();
    r.allocationProbe = true;
#endif
    if (measured) {
      times.push_back(std::chrono::duration<double, std::micro>(end - start).count());
      ticks.push_back(static_cast<double>(tick1 - tick0));
      for (int c = 0; c < total; ++c)
        for (int s = 0; s < block; ++s) {
          const float x = audio.getSample(c, s);
          finite = finite && std::isfinite(x);
          energy += static_cast<double>(x) * x;
          if (c >= 2)
            hostOutputEnergy += static_cast<double>(x) * x;
        }
    }
    head.samples += block;
    head.ppq += block * 145.0 / (60.0 * rate);
  }
  w.p->setPlayHead(nullptr);
  if (!finite)
    fail(r.workload + ": non-finite output");
  if (!(energy > 0.0))
    fail(r.workload + ": silent output");
  if (w.hostOutputs && !(hostOutputEnergy > 0.0))
    fail(r.workload + ": the B2 host outputs are silent");
  // The declared limit is a limit, not an overload: nothing may be dropped.
  const auto &diagnostics = w.p->instrumentRack().diagnostics();
  for (const auto slot : w.slots)
    if (diagnostics[slot].droppedMidiEventsTotal != 0)
      fail(r.workload + ": slot " + std::to_string(slot + 1) + " dropped " +
           std::to_string(diagnostics[slot].droppedMidiEventsTotal) + " MIDI events");
  r.p50 = percentile(times, 0.50);
  r.p95 = percentile(times, 0.95);
  r.p99 = percentile(times, 0.99);
  r.p999 = percentile(times, 0.999);
  r.max = *std::max_element(times.begin(), times.end());
  r.overBudget = static_cast<std::size_t>(
      std::count_if(times.begin(), times.end(), [&](double t) { return t > r.budgetUs; }));
  // Jitter versus work: thread CPU ticks are converted to microseconds with
  // the median ticks-per-microsecond of this run (most blocks are not
  // preempted). A late block whose own CPU time fits the budget was delayed
  // by the scheduler; one whose CPU time exceeds it did too much work.
  std::vector<double> rate_;
  for (std::size_t i = 0; i < times.size(); ++i)
    if (times[i] > 0.0)
      rate_.push_back(ticks[i] / times[i]);
  const double ticksPerUs = rate_.empty() ? 0.0 : percentile(rate_, 0.5);
  for (std::size_t i = 0; i < times.size(); ++i)
    if (times[i] > r.budgetUs) {
      const double cpuUs = ticksPerUs > 0.0 ? ticks[i] / ticksPerUs : times[i];
      if (cpuUs > r.budgetUs)
        ++r.overWork;
      else
        ++r.overPreempted;
    }
  return r;
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 4)
    fail("usage: callback_headroom_bench <instruments> <scratch> <report.md> [--quick]");
  instrumentsDir = fs::absolute(argv[1]);
  cacheRoot = fs::absolute(argv[2]) / "cache";
  fs::create_directories(cacheRoot);
  const bool quick = argc > 4 && std::string(argv[4]) == "--quick";
  juce::ScopedJuceInitialiser_GUI juce;
  std::vector<Measurement> results;
  const std::vector<double> rates = quick ? std::vector<double>{48000.0}
                                          : std::vector<double>{44100.0, 48000.0, 96000.0};
  const std::vector<int> blockSizes = quick ? std::vector<int>{128} : std::vector<int>{64, 128, 512};
  const double seconds = quick ? 0.5 : 4.0;
  for (const bool limit : {false, true})
    for (const double rate : rates)
      for (const int block : blockSizes)
        results.push_back(run(limit, 0, rate, block, seconds));
  // Rack + Drums 1.0 production scope: 1 / 2 / 8 / 16 Full-on Drums kits.
  for (const int kits : quick ? std::vector<int>{2} : std::vector<int>{1, 2, 8, 16})
    for (const bool limit : {false, true})
      for (const double rate : rates)
        for (const int block : blockSizes)
          results.push_back(run(limit, kits, rate, block, quick ? 0.3 : 3.0));
  // Repeats of the tight declared-limit cases on the same machine, in the
  // same process (#106 P1: tail stability, jitter versus work).
  if (!quick)
    for (int k = 1; k <= 3; ++k)
      for (const int block : {64, 128})
        results.push_back(run(true, 0, 96000.0, block, seconds, "(repeat " + std::to_string(k) + ")"));

  std::ostringstream md;
  md << "| Workload | Rate | Block | Budget µs | p50 µs | p95 µs | p99 µs | p99.9 µs | max µs "
        "| Load p99 | Headroom p99 | Blocks over budget | of which work / preempted | Audio-thread allocations |\n"
     << "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
  bool allocated = false;
  for (const auto &r : results) {
    char line[512];
    const double load = r.p99 / r.budgetUs;
    std::snprintf(line, sizeof(line),
                  "| %s | %.1f kHz | %d | %.0f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f%% | "
                  "%.1f%% | %zu / %zu | %zu / %zu | %s |\n",
                  r.workload.c_str(), r.rate / 1000.0, r.block, r.budgetUs, r.p50, r.p95, r.p99,
                  r.p999, r.max, 100.0 * load, 100.0 * (1.0 - load), r.overBudget, r.blocks,
                  r.overWork, r.overPreempted,
                  r.allocationProbe ? std::to_string(r.allocations).c_str() : "not probed");
    md << line;
    allocated = allocated || r.allocations != 0;
  }
  std::cout << md.str();
  std::ofstream(argv[3]) << md.str();
  if (allocated)
    fail("the audio thread allocated during a measured block");
  std::cout << "callback_headroom_bench: " << results.size() << " configurations\n";
  return 0;
}
