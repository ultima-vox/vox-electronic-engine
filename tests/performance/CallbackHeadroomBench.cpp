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
// E2 (--music): musical-density workloads, VOX Drums only or with test
// fixtures (Demo Synth native / graph), N + M <= 16 slots, typical and peak,
// with pre-flight checks of the stimulus (host grid, Both mode, MIDI export)
// and of audible same-block ducking in both slot orders. See runMusicSuite.
//
// Usage: callback_headroom_bench <instruments> <scratch> <report.md> [--quick] [--music]
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
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
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

// Thread CPU time of the calling thread in microseconds, from an
// independent clock: GetThreadTimes (kernel + user, 100 ns units, coarse
// granularity) on Windows, CLOCK_THREAD_CPUTIME_ID elsewhere.
double threadCpuUs() noexcept {
#if defined(_WIN32)
  FILETIME created, exited, kernel, user;
  if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
    return 0.0;
  auto value = [](const FILETIME &f) {
    return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
  };
  return static_cast<double>(value(kernel) + value(user)) / 10.0;
#else
  return static_cast<double>(threadTicks()) / 1000.0;
#endif
}

// How per-block thread CPU ticks convert to microseconds, and how far that
// conversion can be trusted. Linux: ticks are thread-CPU nanoseconds
// (exact, 1000 per microsecond). Windows: cycles, calibrated against
// GetThreadTimes over each half of the run; valid only when each half is
// long enough for the coarse clock (>= 0.5 s of thread CPU) and both halves
// agree within 5% (no frequency or core change between them).
struct CpuCalibration {
  bool valid{};
  double ticksPerUs{};
  double uncertainty{1.0}; // relative
  std::string describe() const {
    char text[96];
#if defined(_WIN32)
    if (!valid)
      return "invalid (UNKNOWN)";
    std::snprintf(text, sizeof(text), "%.0f cyc/us ±%.1f%%", ticksPerUs, 100.0 * uncertainty);
#else
    std::snprintf(text, sizeof(text), "thread CPU ns");
#endif
    return text;
  }
};

CpuCalibration calibrate(double ticks1, double us1, double ticks2, double us2) {
  CpuCalibration c;
#if defined(_WIN32)
  if (us1 < 500000.0 || us2 < 500000.0 || ticks1 <= 0.0 || ticks2 <= 0.0)
    return c;
  const double a = ticks1 / us1, b = ticks2 / us2, mean = 0.5 * (a + b);
  const double drift = std::abs(a - b) / mean;
  const double granularity = 15625.0 / std::min(us1, us2); // one 15.6 ms tick
  c.uncertainty = std::max(drift, granularity);
  c.valid = drift <= 0.05;
  c.ticksPerUs = (ticks1 + ticks2) / (us1 + us2);
#else
  (void)ticks1, (void)us1, (void)ticks2, (void)us2;
  c.valid = true;
  c.ticksPerUs = 1000.0;
  c.uncertainty = 0.01; // clock read placement around the call
#endif
  return c;
}

enum class LateCause { work, preempted, unknown };
// A late block is `work` when its own CPU time exceeds the budget beyond the
// calibration's uncertainty, `preempted` when it is clearly below, and
// `unknown` otherwise or when the calibration is invalid.
LateCause classify(double ticks, double budgetUs, const CpuCalibration &c) {
  if (!c.valid || c.ticksPerUs <= 0.0)
    return LateCause::unknown;
  const double cpuUs = ticks / c.ticksPerUs;
  if (cpuUs > budgetUs * (1.0 + c.uncertainty))
    return LateCause::work;
  if (cpuUs < budgetUs * (1.0 - c.uncertainty))
    return LateCause::preempted;
  return LateCause::unknown;
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
  std::size_t overWork{}, overPreempted{}, overUnknown{}; // of those, by cause
  std::string calibration;
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
  double calTicks[3]{}, calUs[3]{};
  for (std::size_t b = 0; b < warmup + blocks; ++b) {
    if (b == warmup || b == warmup + blocks / 2) {
      const int k = b == warmup ? 0 : 1;
      calTicks[k] = static_cast<double>(threadTicks());
      calUs[k] = threadCpuUs();
    }
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
  calTicks[2] = static_cast<double>(threadTicks());
  calUs[2] = threadCpuUs();
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
  // Cause of each late block from its own thread CPU time (see classify);
  // the calibration comes from an independent clock, not from wall time.
  const auto calibration = calibrate(calTicks[1] - calTicks[0], calUs[1] - calUs[0],
                                     calTicks[2] - calTicks[1], calUs[2] - calUs[1]);
  r.calibration = calibration.describe();
  for (std::size_t i = 0; i < times.size(); ++i)
    if (times[i] > r.budgetUs)
      switch (classify(ticks[i], r.budgetUs, calibration)) {
      case LateCause::work: ++r.overWork; break;
      case LateCause::preempted: ++r.overPreempted; break;
      case LateCause::unknown: ++r.overUnknown; break;
      }
  return r;
}

// ---------------------------------------------------------------------------
// E2 musical-density workloads (review 6703361017, matrix revision 2 in
// #109 comment 6691963182). Run only with --music; the representative and
// declared-limit suites above are unchanged and reported separately.
//
// These workloads use the factory "Full-on Driving" pattern (typical) and a
// synthetic 32nd-note grid (peak). They do NOT establish a realtime
// guarantee for every genre-neutral VOX Drums kit; kit-specific workloads
// follow when approved kits exist. Demo Synth (native) and the graph Demo
// are regression FIXTURES, not shipped v1.0 instruments.
constexpr int padNotes[8] = {36, 38, 39, 42, 46, 45, 37, 49};
constexpr float seqAccentDefault = 0.5f; // seq.accent default, left unchanged

struct MusicSpec {
  bool peak{};
  int drums{};
  int fixtures{};
  bool kickLast{}; // fixtures in the low slots, the kick source above them
  std::string name() const {
    std::string n = std::string("music ") + (peak ? "peak" : "typical");
    if (fixtures == 0)
      return n + " drums x" + std::to_string(drums);
    return n + " mixed " + std::to_string(drums) + "+" + std::to_string(fixtures) +
           (kickLast ? " K-last" : " K-first");
  }
};

// Slot layout: Drums slots and fixture slots, by the spec's order.
struct MusicLayout {
  std::vector<std::size_t> drumSlots, fixtureSlots;
};
MusicLayout layoutOf(const MusicSpec &m) {
  MusicLayout l;
  const auto d = static_cast<std::size_t>(m.drums), f = static_cast<std::size_t>(m.fixtures);
  for (std::size_t i = 0; i < d; ++i)
    l.drumSlots.push_back(m.kickLast ? f + i : i);
  for (std::size_t i = 0; i < f; ++i)
    l.fixtureSlots.push_back(m.kickLast ? i : d + i);
  if (d + f > 16)
    fail(m.name() + ": more than 16 slots");
  return l;
}

// Sequencer mode per Drums kit index j: 1/2 Internal, 1/4 Host, 1/4 Both.
int modeOf(std::size_t j) { return j % 4 == 2 ? 0 : (j % 4 == 3 ? 2 : 1); }
const char *modeName(int mode) { return mode == 0 ? "Host" : (mode == 1 ? "Internal" : "Both"); }

// typical: factory "Full-on Driving" (16ths, 4 bars: 116 hits). peak:
// 32nd-note grid, 4 pattern bars of 16 steps = 2 bars of 4/4, 188 hits
// (94 per 4/4 bar), seeded velocities 40..127, accents on the beats.
pattern::Pattern musicPattern(VstEngineAudioProcessor &p, std::size_t slot, bool peak) {
  const auto *content = p.instrumentRack().descriptor(slot)->patternContent.get();
  if (content == nullptr || content->factory.size() < 2 ||
      content->factory[1].name != "Full-on Driving")
    fail("the kit must ship the factory pattern Full-on Driving");
  if (!peak)
    return *content->factory[1].pattern;
  auto q = pattern::emptyPattern(content->caps);
  q.stepsPerBar = 16;
  q.barCount = 4;
  q.timing = VOX_TIMING_THIRTY_SECOND;
  std::uint32_t seed = 0x2545F491u;
  auto velocity = [&] {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return static_cast<std::uint8_t>(40 + seed % 88);
  };
  auto on = [&](std::uint32_t lane, std::uint32_t bar, std::uint32_t step) {
    auto &cell = q.at(lane, bar, step);
    cell.flags = VOX_STEP_ON | (step % 8 == 0 ? VOX_STEP_ACCENT : 0u);
    cell.velocity = velocity();
  };
  for (std::uint32_t bar = 0; bar < 4; ++bar)
    for (std::uint32_t st = 0; st < 16; ++st) {
      on(3, bar, st); // closed hat: every 32nd (16)
      if (st % 4 == 0 || st == 10 || st == 14)
        on(0, bar, st); // kick: 8ths of the half bar + double hits (6)
      if (st == 8 || st % 4 == 3)
        on(1, bar, st); // snare + ghost notes (5)
      if (st == 4 || st == 8 || st == 12)
        on(2, bar, st); // clap (3)
      if (st % 4 == 2)
        on(4, bar, st); // open hat offbeats (4)
      if (st % 2 == 1)
        on(6, bar, st); // rim on every odd 32nd (8)
      if (st >= 13 || (bar == 3 && st >= 9))
        on(5, bar, st); // tom fills (3; 7 in the fill bar)
      if (st == 0)
        on(7, bar, st); // cymbal (1)
    }
  return q;
}

// Hits in one pass of the pattern (all lanes on).
std::size_t gridHits(const pattern::Pattern &q) {
  std::size_t n = 0;
  for (std::uint32_t lane = 0; lane < q.laneCount; ++lane)
    for (std::uint32_t bar = 0; bar < q.barCount; ++bar)
      for (std::uint32_t st = 0; st < q.stepsPerBar; ++st)
        n += (q.at(lane, bar, st).flags & VOX_STEP_ON) != 0;
  return n;
}

// The host grid of one Drums slot: the same steps the kit's sequencer plays,
// at the same samples and velocities, computed with the module's own
// expressions (libs/drums scheduleSequencer, swing 0) from the block's ppq.
struct HostGrid {
  pattern::Pattern pattern;
  int channel{};
  std::int64_t lastStep{std::numeric_limits<std::int64_t>::min()};
  void fill(juce::MidiBuffer &midi, double ppq0, double rate, double bpm, int n) {
    static const double stepPpq[4] = {0.25, 0.5, 0.125, 1.0 / 3.0};
    const double step = stepPpq[pattern.timing];
    const double samplesPerPpq = 60.0 * rate / bpm;
    const double accent = seqAccentDefault;
    const auto steps = static_cast<std::int64_t>(pattern.stepsPerBar);
    const auto bars = static_cast<std::int64_t>(pattern.barCount);
    for (auto g = static_cast<std::int64_t>(std::floor(ppq0 / step)) - 1;; ++g) {
      if (g < 0)
        continue;
      const double t = static_cast<double>(g) * step;
      const double at = std::ceil((t - ppq0) * samplesPerPpq - 1e-6);
      if (at >= static_cast<double>(n))
        break;
      if (at < 0.0 || g <= lastStep)
        continue;
      lastStep = g;
      const auto st = static_cast<std::uint32_t>(g % steps);
      const auto bar = static_cast<std::uint32_t>((g / steps) % bars);
      for (std::uint32_t lane = 0; lane < 8; ++lane) {
        const auto &cell = pattern.at(lane, bar, st);
        if (!(cell.flags & VOX_STEP_ON))
          continue;
        double v = cell.velocity;
        if (cell.flags & VOX_STEP_ACCENT)
          v += accent * (127.0 - v);
        const auto vel = static_cast<juce::uint8>(std::fmin(127.0, std::floor(v + 0.5)));
        midi.addEvent(MidiMessage::noteOn(channel, padNotes[lane], vel), static_cast<int>(at));
        midi.addEvent(MidiMessage::noteOff(channel, padNotes[lane]), static_cast<int>(at));
      }
    }
  }
};

double tempoOf(const MusicSpec &m) { return m.peak ? 175.0 : 145.0; }

class MusicPlayHead final : public juce::AudioPlayHead {
public:
  juce::Optional<PositionInfo> getPosition() const override {
    PositionInfo info;
    info.setBpm(bpm);
    info.setPpqPosition(ppq);
    info.setIsPlaying(true);
    info.setTimeInSamples(static_cast<juce::int64>(samples));
    return info;
  }
  double bpm = 145.0, ppq = 0.0, samples = 0.0;
};

// One VOX Drums kit as the spec configures it (sources, samples, shaping,
// sends, mode, pattern); `b2` routes pads 2..8 to Out 2..Out 8.
void configureMusicKit(VstEngineAudioProcessor &p, std::size_t slot, int mode, bool peak, bool b2,
                       const pattern::Pattern &q) {
  setRaw(p, instrument::hostparams::poolId(slot, 48), static_cast<float>(mode));
  std::string why;
  if (!p.instrumentRack().setPattern(slot, q, why))
    fail("music pattern: " + why);
  // 4 Synth, 2 Sample (clap, tom), 2 Layer (kick, rim), real sample buffers.
  for (const auto [pad, source] : {std::pair{0, 2.0f}, std::pair{2, 1.0f}, std::pair{5, 1.0f},
                                   std::pair{6, 2.0f}}) {
    setUi(p, slot, pad, "source", source);
    if (!p.instrumentRack().setSlotSample(
            slot, static_cast<std::uint32_t>(pad),
            constantSample(9000 + 500 * static_cast<std::size_t>(pad), 0.6f), why))
      fail("music sample: " + why);
  }
  static const float decays[8] = {0.38f, 0.25f, 0.35f, 0.07f, 0.45f, 0.5f, 0.08f, 1.8f};
  static const float decayMax[8] = {1.5f, 1.0f, 1.5f, 0.5f, 3.0f, 2.0f, 0.5f, 6.0f};
  if (peak) {
    setUi(p, slot, 5, "sample_loop", 1.0f); // one looped Sample pad
    for (int pad = 0; pad < 8; ++pad) {
      setUi(p, slot, pad, "filter", static_cast<float>(1 + pad % 5));
      setRaw(p, instrument::hostparams::poolId(slot, static_cast<std::size_t>(pad * 5 + 2)),
             std::min(decays[pad] * 1.5f, decayMax[pad]));
      setRaw(p, instrument::hostparams::poolId(slot, static_cast<std::size_t>(59 + pad * 5 + 0)), 0.2f);
      setRaw(p, instrument::hostparams::poolId(slot, static_cast<std::size_t>(59 + pad * 5 + 1)), 0.15f);
    }
    for (int pad = 0; pad < 4; ++pad)
      setUi(p, slot, pad, "transient_attack", 0.4f);
  } else {
    setUi(p, slot, 3, "filter", 2.0f); // closed hat LP 24
    setUi(p, slot, 4, "filter", 3.0f); // open hat HP 12
    setUi(p, slot, 5, "filter", 5.0f); // tom BP
    setUi(p, slot, 1, "transient_attack", 0.5f);
    setUi(p, slot, 2, "transient_attack", 0.5f);
    setRaw(p, instrument::hostparams::poolId(slot, 59 + 1 * 5 + 0), 0.4f); // snare send 1
    setRaw(p, instrument::hostparams::poolId(slot, 59 + 2 * 5 + 1), 0.3f); // clap send 2
  }
  if (b2)
    for (int pad = 1; pad < 8; ++pad)
      setUi(p, slot, pad, "output", static_cast<float>(pad));
}

// Fixture MIDI in musical time (review 6703842178, P1): scheduled from the
// transport's ppq and tempo with the same step expression as the Drums host
// grid, so the density per bar is the same at every rate and tempo.
//  - Demo Synth native: a 4-note chord on every bar (4 quarters), held
//    legato until the next chord (NoteOffs of the previous chord at the
//    new onset). Roots cycle 48, 50, 52, 54, 56.
//  - Demo Synth graph: a bass note on every 16th (0.25 ppq), legato,
//    notes cycle 36..43.
struct FixtureSchedule {
  int channel{};
  bool chords{};
  std::int64_t lastStep{std::numeric_limits<std::int64_t>::min()};
  std::vector<int> held;
  std::size_t onsets{}; // steps started (chords or bass notes)
  double step() const { return chords ? 4.0 : 0.25; }
  void fill(juce::MidiBuffer &midi, double ppq0, double rate, double bpm, int n) {
    const double samplesPerPpq = 60.0 * rate / bpm;
    for (auto g = static_cast<std::int64_t>(std::floor(ppq0 / step())) - 1;; ++g) {
      if (g < 0)
        continue;
      const double at = std::ceil((static_cast<double>(g) * step() - ppq0) * samplesPerPpq - 1e-6);
      if (at >= static_cast<double>(n))
        break;
      if (at < 0.0 || g <= lastStep)
        continue;
      lastStep = g;
      const int s = static_cast<int>(at);
      for (const int note : held)
        midi.addEvent(MidiMessage::noteOff(channel, note), s);
      held.clear();
      if (chords) {
        const int root = 48 + static_cast<int>(g % 5) * 2;
        for (const int interval : {0, 4, 7, 11})
          held.push_back(root + interval);
      } else {
        held.push_back(36 + static_cast<int>(g % 8));
      }
      for (const int note : held)
        midi.addEvent(MidiMessage::noteOn(channel, note, (juce::uint8)(chords ? 90 : 100)), s);
      ++onsets;
    }
  }
};

struct MusicRig {
  std::unique_ptr<VstEngineAudioProcessor> p;
  MusicLayout layout;
  std::vector<int> drumModes;
  std::vector<HostGrid> grids;          // per Drums slot (used by Host / Both)
  std::vector<int> fixtureChannels;
  std::vector<FixtureSchedule> fixtureSchedules;
  std::vector<const char *> fixtureIds;
  std::size_t hitsPerPass{};
  std::string slotMap;
};

// Builds a Rack for spec m. `b2`: Out 2..8 enabled and kit 1 routed there.
MusicRig makeMusicRig(const MusicSpec &m, double rate, int block, bool b2) {
  MusicRig r;
  r.layout = layoutOf(m);
  r.p = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(), installed());
  if (b2) {
    auto layout = r.p->getBusesLayout();
    for (int b = 1; b < layout.outputBuses.size(); ++b)
      layout.outputBuses.getReference(b) = juce::AudioChannelSet::stereo();
    if (!r.p->setBusesLayout(layout))
      fail("B2 layout");
  }
  r.p->prepareToPlay(rate, block);
  Workload loader;
  loader.p = std::move(r.p);
  for (const auto slot : r.layout.drumSlots)
    load(loader, slot, drumsId);
  for (std::size_t i = 0; i < r.layout.fixtureSlots.size(); ++i) {
    r.fixtureIds.push_back(i % 2 == 0 ? demoId : graphId);
    load(loader, r.layout.fixtureSlots[i], r.fixtureIds.back());
  }
  r.p = std::move(loader.p);
  std::ostringstream map;
  for (std::size_t j = 0; j < r.layout.drumSlots.size(); ++j) {
    const auto slot = r.layout.drumSlots[j];
    const auto q = musicPattern(*r.p, slot, m.peak);
    r.drumModes.push_back(modeOf(j));
    configureMusicKit(*r.p, slot, r.drumModes.back(), m.peak, b2 && j == 0, q);
    HostGrid g;
    g.pattern = q;
    g.channel = channelOf(*r.p, slot);
    r.grids.push_back(g);
    r.hitsPerPass = gridHits(q);
    map << (map.tellp() > 0 ? ", " : "") << "slot " << slot + 1 << " VOX Drums (shipped, "
        << modeName(r.drumModes.back()) << (b2 && j == 0 ? ", pads 2-8 on Out 2-8" : "") << ")";
  }
  for (std::size_t i = 0; i < r.layout.fixtureSlots.size(); ++i) {
    r.fixtureChannels.push_back(channelOf(*r.p, r.layout.fixtureSlots[i]));
    FixtureSchedule f;
    f.channel = r.fixtureChannels.back();
    f.chords = r.fixtureIds[i] == demoId;
    r.fixtureSchedules.push_back(f);
    map << ", slot " << r.layout.fixtureSlots[i] + 1
        << (r.fixtureIds[i] == demoId ? " Demo Synth native (FIXTURE)" : " Demo Synth graph (FIXTURE)");
  }
  r.slotMap = map.str();
  // Returns, sends and same-block kick ducking from the first kit's kick.
  setRaw(*r.p, host::returnParameterId(0, "type"), 1.0f);
  setRaw(*r.p, host::returnParameterId(1, "type"), 2.0f);
  setRaw(*r.p, "rack.duck.source", static_cast<float>(r.layout.drumSlots.front() + 1));
  setRaw(*r.p, "rack.duck.note", 36.0f);
  std::vector<std::size_t> targets = m.fixtures > 0
                                         ? r.layout.fixtureSlots
                                         : std::vector<std::size_t>(r.layout.drumSlots.begin() + 1,
                                                                    r.layout.drumSlots.end());
  for (const auto slot : targets) {
    char depth[32];
    std::snprintf(depth, sizeof(depth), "slot%02zu.duck.depth", slot + 1);
    setRaw(*r.p, depth, 0.7f);
  }
  for (const auto slot : r.layout.fixtureSlots)
    setRaw(*r.p, host::sendParameterId(slot, 0, "level"), 0.3f);
  r.p->prepareToPlay(rate, block);
  return r;
}

struct Render {
  std::vector<float> l, r;
  double energy() const {
    double e = 0;
    for (std::size_t i = 0; i < l.size(); ++i)
      e += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
    return e;
  }
};

// Renders `samples` frames of main output; host grids feed Host/Both kits,
// `extra` adds fixed events (absolute sample, message).
Render renderMain(MusicRig &rig, const MusicSpec &m, double rate, int block, int samples,
                  const std::vector<std::pair<int, MidiMessage>> &extra, bool hostGrids,
                  double ppqStart = 0.0) {
  MusicPlayHead head;
  head.bpm = tempoOf(m);
  head.ppq = ppqStart;
  rig.p->setPlayHead(&head);
  juce::AudioBuffer<float> audio(rig.p->getTotalNumOutputChannels(), block);
  juce::MidiBuffer midi;
  Render out;
  for (int start = 0; start < samples; start += block) {
    const int n = std::min(block, samples - start);
    midi.clear();
    for (std::size_t j = 0; hostGrids && j < rig.grids.size(); ++j)
      if (rig.drumModes[j] != 1)
        rig.grids[j].fill(midi, head.ppq, rate, head.bpm, n);
    for (const auto &[at, message] : extra)
      if (at >= start && at < start + n)
        midi.addEvent(message, at - start);
    audio.setSize(audio.getNumChannels(), n, false, false, true);
    audio.clear();
    rig.p->processBlock(audio, midi);
    out.l.insert(out.l.end(), audio.getReadPointer(0), audio.getReadPointer(0) + n);
    out.r.insert(out.r.end(), audio.getReadPointer(1), audio.getReadPointer(1) + n);
    head.ppq += n * head.bpm / (60.0 * rate);
    head.samples += n;
  }
  rig.p->setPlayHead(nullptr);
  return out;
}

double maxDiff(const Render &a, const Render &b) {
  double d = 0;
  for (std::size_t i = 0; i < a.l.size() && i < b.l.size(); ++i)
    d = std::max({d, std::abs(static_cast<double>(a.l[i]) - b.l[i]),
                  std::abs(static_cast<double>(a.r[i]) - b.r[i])});
  return a.l.size() == b.l.size() ? d : 1e30;
}

// Pre-flight 1 (per pattern, rate, block): the stimulus is what the spec
// says. One kit, exactly one pass of the four-bar pattern:
//  - Internal renders the pattern (A);
//  - Host mode fed by the bench's host grid renders bit-identically (the
//    generator delivers the same hits at the same samples and velocities);
//  - Both mode with the same host grid renders bit-identically to Internal
//    (one hit per step and pad: no double trigger);
//  - the Rack's MIDI export has exactly the pattern's hits and replays in
//    Host mode like the internal sequencer (the exportMatchesPlayback oracle,
//    tolerance 1e-6).
std::string preflightStimulus(bool peak, double rate, int block) {
  MusicSpec one{peak, 1, 0, false};
  const double bpm = tempoOf(one);
  const double quarters = peak ? 8.0 : 16.0; // four pattern bars
  // Exactly one pass of the pattern: the export covers one pass, and a tail
  // would let the sequencer start the next one.
  const int samples = static_cast<int>(quarters * 60.0 * rate / bpm);
  auto modeRender = [&](int mode, bool hostGrids, const std::vector<std::pair<int, MidiMessage>> &ev) {
    auto rig = makeMusicRig(one, rate, block, false);
    rig.drumModes[0] = mode;
    setRaw(*rig.p, instrument::hostparams::poolId(rig.layout.drumSlots[0], 48), static_cast<float>(mode));
    rig.p->prepareToPlay(rate, block);
    return std::pair{renderMain(rig, one, rate, block, samples, ev, hostGrids), std::move(rig)};
  };
  auto [internal, internalRig] = modeRender(1, false, {});
  if (!(internal.energy() > 0.0))
    fail(one.name() + " pre-flight: the internal pattern is silent");
  const auto host = modeRender(0, true, {}).first;
  if (maxDiff(internal, host) != 0.0)
    fail(one.name() + " pre-flight: the host grid does not reproduce the internal sequencer "
         "(max difference " + std::to_string(maxDiff(internal, host)) + ")");
  const auto both = modeRender(2, true, {}).first;
  if (maxDiff(internal, both) != 0.0)
    fail(one.name() + " pre-flight: Both mode double-triggers (differs from Internal by " +
         std::to_string(maxDiff(internal, both)) + ")");
  juce::MemoryBlock bytes;
  juce::String diagnostic;
  const auto slot = internalRig.layout.drumSlots[0];
  if (!internalRig.p->serializeSlotPatternMidi(slot, bytes, diagnostic))
    fail("pre-flight export: " + diagnostic.toStdString());
  juce::MemoryInputStream stream(bytes, false);
  juce::MidiFile file;
  if (!file.readFrom(stream))
    fail("pre-flight export: unreadable MIDI file");
  const double spp = 60.0 * rate / bpm;
  std::vector<std::pair<int, MidiMessage>> exported;
  const int channel = internalRig.grids[0].channel;
  for (int t = 0; t < file.getNumTracks(); ++t)
    for (int e = 0; e < file.getTrack(t)->getNumEvents(); ++e) {
      const auto &msg = file.getTrack(t)->getEventPointer(e)->message;
      if (!msg.isNoteOn())
        continue;
      const double ppq = msg.getTimeStamp() / file.getTimeFormat();
      exported.push_back({static_cast<int>(std::ceil(ppq * spp - 1e-6)),
                          MidiMessage::noteOn(channel, msg.getNoteNumber(),
                                              static_cast<juce::uint8>(msg.getVelocity()))});
    }
  const auto expected = internalRig.hitsPerPass;
  if (exported.size() != expected)
    fail(one.name() + " pre-flight: export has " + std::to_string(exported.size()) +
         " notes, the pattern " + std::to_string(expected));
  const auto replay = modeRender(0, false, exported).first;
  if (maxDiff(internal, replay) >= 1e-6)
    fail(one.name() + " pre-flight: the exported MIDI does not replay like the sequencer (" +
         std::to_string(maxDiff(internal, replay)) + ")");
  char text[192];
  std::snprintf(text, sizeof(text),
                "%s pattern @ %.1f kHz/%d: %zu hits per 4 bars; host grid = Internal (bit-exact); "
                "Both = Internal (bit-exact); export %zu notes, replay max diff %.2g",
                peak ? "peak" : "typical", rate / 1000.0, block, expected, exported.size(),
                maxDiff(internal, replay));
  return text;
}

// Pre-flight 2 (per mixed layout, both slot orders): audible same-block
// ducking. Kits silent (kit volume 0, no sends), the first kit's kick in
// Internal mode, every fixture holding a note; attack 0, hold 0, release
// 1 ms. With depth 0.7 every fixture sample at a kick equals 0.3 x the
// depth-0 reference (gain 1 - depth x envelope, envelope 1 at the hit), and
// the sample before the first kick is untouched.
std::string preflightDucking(const MusicSpec &m, double rate, int block) {
  const double bpm = tempoOf(m);
  const int samples = static_cast<int>(4.0 * 60.0 * rate / bpm); // one 4/4 bar
  auto render = [&](float depthValue) {
    auto rig = makeMusicRig(m, rate, block, false);
    for (std::size_t j = 0; j < rig.layout.drumSlots.size(); ++j) {
      const auto slot = rig.layout.drumSlots[j];
      setRaw(*rig.p, instrument::hostparams::poolId(slot, 40), 0.0f); // kit volume
      for (int pad = 0; pad < 8; ++pad)
        for (int k = 0; k < 2; ++k)
          setRaw(*rig.p, instrument::hostparams::poolId(slot, static_cast<std::size_t>(59 + pad * 5 + k)), 0.0f);
    }
    setRaw(*rig.p, instrument::hostparams::poolId(rig.layout.drumSlots[0], 48), 1.0f);
    rig.drumModes[0] = 1;
    for (std::size_t j = 1; j < rig.drumModes.size(); ++j) { // other kits idle
      setRaw(*rig.p, instrument::hostparams::poolId(rig.layout.drumSlots[j], 48), 0.0f);
      rig.drumModes[j] = 1; // no host grid either
    }
    setRaw(*rig.p, "rack.duck.attack", 0.0f);
    setRaw(*rig.p, "rack.duck.hold", 0.0f);
    setRaw(*rig.p, "rack.duck.release", 1.0f);
    std::vector<std::pair<int, MidiMessage>> hold;
    for (std::size_t i = 0; i < rig.layout.fixtureSlots.size(); ++i) {
      const auto slot = rig.layout.fixtureSlots[i];
      char depth[32];
      std::snprintf(depth, sizeof(depth), "slot%02zu.duck.depth", slot + 1);
      setRaw(*rig.p, depth, depthValue);
      setRaw(*rig.p, host::sendParameterId(slot, 0, "level"), 0.0f);
      hold.push_back({0, MidiMessage::noteOn(rig.fixtureChannels[i], 60, (juce::uint8)110)});
    }
    rig.p->prepareToPlay(rate, block);
    const auto kickGrid = rig.grids[0];
    // Start a quarter of a beat in, so the fixtures sound before the first kick.
    return std::pair{renderMain(rig, m, rate, block, samples, hold, false, 0.25), kickGrid};
  };
  const auto [reference, grid] = render(0.0f);
  const auto ducked = render(0.7f).first;
  // Kick samples from the pattern, with the module's step timing.
  HostGrid kicks = grid;
  for (std::uint32_t lane = 1; lane < 8; ++lane)
    for (std::uint32_t bar = 0; bar < kicks.pattern.barCount; ++bar)
      for (std::uint32_t st = 0; st < kicks.pattern.stepsPerBar; ++st)
        kicks.pattern.at(lane, bar, st).flags = 0;
  std::vector<int> at;
  {
    juce::MidiBuffer midi;
    MusicPlayHead head;
    head.bpm = bpm;
    head.ppq = 0.25;
    for (int start = 0; start < samples; start += block) {
      const int n = std::min(block, samples - start);
      midi.clear();
      kicks.fill(midi, head.ppq, rate, bpm, n);
      for (const auto meta : midi)
        if (meta.getMessage().isNoteOn())
          at.push_back(start + meta.samplePosition);
      head.ppq += n * bpm / (60.0 * rate);
    }
  }
  if (at.size() < 2 || at.front() < 1)
    fail(m.name() + " ducking pre-flight: expected kicks after the first sample");
  if (reference.l[static_cast<std::size_t>(at.front() - 1)] !=
      ducked.l[static_cast<std::size_t>(at.front() - 1)])
    fail(m.name() + " ducking pre-flight: the fixture is attenuated before the first kick");
  for (const int s : at) {
    const auto i = static_cast<std::size_t>(s);
    bool audible = false;
    for (const bool left : {true, false}) {
      const double ref = left ? reference.l[i] : reference.r[i];
      const double got = left ? ducked.l[i] : ducked.r[i];
      // A held synth crosses zero now and then: judge only audible samples,
      // and require at least one audible channel at every kick.
      if (std::abs(ref) < 1e-4)
        continue;
      audible = true;
      if (std::abs(got - 0.3 * ref) > 1e-4 * std::abs(ref))
        fail(m.name() + " ducking pre-flight: sample " + std::to_string(s) + " is " +
             std::to_string(got) + ", expected 0.3 x " + std::to_string(ref));
    }
    if (!audible)
      fail(m.name() + " ducking pre-flight: the fixture reference is silent at kick sample " +
           std::to_string(s));
  }
  char text[160];
  std::snprintf(text, sizeof(text),
                "%s @ %.1f kHz/%d: %zu kicks, fixture gain at each kick = 0.30 of reference, "
                "untouched before the first",
                m.name().c_str(), rate / 1000.0, block, at.size());
  return text;
}

struct MusicMeasurement {
  Measurement m;
  double eventsMean{}, eventsMax{}; // host MIDI events per slot channel per block
  // Fixture steps started in the measured blocks vs. musical steps the
  // transport crossed, summed over the fixtures of each kind.
  double chordSteps{}, chordExpected{}, bassSteps{}, bassExpected{};
  std::string slotMap;
  std::size_t hitsPerPass{};
};

MusicMeasurement runMusic(const MusicSpec &spec, double rate, int block, double seconds) {
  auto rig = makeMusicRig(spec, rate, block, true);
  MusicPlayHead head;
  head.bpm = tempoOf(spec);
  rig.p->setPlayHead(&head);
  const int total = rig.p->getTotalNumOutputChannels();
  juce::AudioBuffer<float> audio(total, block);
  juce::MidiBuffer midi;
  midi.ensureSize(2048 * 8);
  // Automation: typical 4 endpoints per slot, peak 16, slow ramps.
  std::vector<juce::AudioProcessorParameter *> automated;
  const std::vector<std::size_t> typicalDrums = {59 + 1 * 5 + 2, 2, 5, 59 + 1 * 5 + 0};
  auto add = [&](std::size_t slot, std::size_t k) {
    auto *parameter = rig.p->parameters().getParameter(instrument::hostparams::poolId(slot, k));
    if (parameter == nullptr)
      fail("missing automation endpoint");
    automated.push_back(parameter);
  };
  for (const auto slot : rig.layout.drumSlots)
    for (std::size_t k = 0; k < (spec.peak ? 16u : 4u); ++k)
      add(slot, spec.peak ? k : typicalDrums[k]);
  for (const auto slot : rig.layout.fixtureSlots)
    for (std::size_t k = 0; k < (spec.peak ? 16u : 4u); ++k)
      add(slot, k);
  std::vector<int> channels;
  for (const auto &g : rig.grids)
    channels.push_back(g.channel);
  for (const int c : rig.fixtureChannels)
    channels.push_back(c);
  const auto blocks = static_cast<std::size_t>(seconds * rate / block);
  const std::size_t warmup = std::max<std::size_t>(64, blocks / 20);
  std::vector<double> times, ticks;
  times.reserve(blocks);
  ticks.reserve(blocks);
  double energy = 0.0, hostOutputEnergy = 0.0, eventSum = 0.0, eventMax = 0.0;
  bool finite = true;
  MusicMeasurement out;
  Measurement &r = out.m;
  r.workload = spec.name();
  r.rate = rate;
  r.block = block;
  r.blocks = blocks;
  r.budgetUs = 1e6 * block / rate;
  double calTicks[3]{}, calUs[3]{};
  std::array<int, 17> perChannel{};
  double measuredPpqStart = 0.0;
  std::vector<std::size_t> onsetsAtWarmup;
  for (std::size_t b = 0; b < warmup + blocks; ++b) {
    if (b == warmup || b == warmup + blocks / 2) {
      const int k = b == warmup ? 0 : 1;
      calTicks[k] = static_cast<double>(threadTicks());
      calUs[k] = threadCpuUs();
    }
    midi.clear();
    for (std::size_t j = 0; j < rig.grids.size(); ++j)
      if (rig.drumModes[j] != 1)
        rig.grids[j].fill(midi, head.ppq, rate, head.bpm, block);
    if (b == warmup) {
      measuredPpqStart = head.ppq;
      for (auto &f : rig.fixtureSchedules)
        onsetsAtWarmup.push_back(f.onsets);
    }
    for (auto &f : rig.fixtureSchedules)
      f.fill(midi, head.ppq, rate, head.bpm, block);
    for (std::size_t k = 0; k < automated.size(); ++k)
      automated[k]->setValue(static_cast<float>((b / 8 + k * 7) % 100) / 100.0f);
    const bool measured = b >= warmup;
    if (measured) {
      perChannel.fill(0);
      for (const auto meta : midi)
        ++perChannel[static_cast<std::size_t>(meta.getMessage().getChannel())];
      for (const int c : channels) {
        eventSum += perChannel[static_cast<std::size_t>(c)];
        eventMax = std::max(eventMax, static_cast<double>(perChannel[static_cast<std::size_t>(c)]));
      }
    }
#if defined(VOX_TEST_INTERPOSE_MALLOC)
    if (measured)
      voxProbeBegin();
#endif
    const auto tick0 = threadTicks();
    const auto start = std::chrono::steady_clock::now();
    rig.p->processBlock(audio, midi);
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
    head.ppq += block * head.bpm / (60.0 * rate);
  }
  calTicks[2] = static_cast<double>(threadTicks());
  calUs[2] = threadCpuUs();
  rig.p->setPlayHead(nullptr);
  // Fixture onsets in the measured blocks must be the musical steps the
  // transport crossed (at most one step off at either edge).
  const double measuredQuarters = head.ppq - measuredPpqStart;
  for (std::size_t i = 0; i < rig.fixtureSchedules.size(); ++i) {
    const auto &f = rig.fixtureSchedules[i];
    const double onsets = static_cast<double>(f.onsets - onsetsAtWarmup[i]);
    const double expected = std::floor(head.ppq / f.step()) - std::floor(measuredPpqStart / f.step());
    if (std::abs(onsets - expected) > 1.0)
      fail(r.workload + ": fixture " + std::to_string(i + 1) + " started " +
           std::to_string(onsets) + " steps, the transport crossed " + std::to_string(expected));
    (f.chords ? out.chordSteps : out.bassSteps) += onsets;
    (f.chords ? out.chordExpected : out.bassExpected) += expected;
  }
  (void)measuredQuarters;
  if (!finite)
    fail(r.workload + ": non-finite output");
  if (!(energy > 0.0))
    fail(r.workload + ": silent output");
  if (!(hostOutputEnergy > 0.0))
    fail(r.workload + ": the B2 host outputs are silent");
  const auto &diagnostics = rig.p->instrumentRack().diagnostics();
  for (const auto slot : rig.layout.drumSlots)
    if (diagnostics[slot].droppedMidiEventsTotal != 0)
      fail(r.workload + ": slot " + std::to_string(slot + 1) + " dropped MIDI events");
  for (const auto slot : rig.layout.fixtureSlots)
    if (diagnostics[slot].droppedMidiEventsTotal != 0)
      fail(r.workload + ": slot " + std::to_string(slot + 1) + " dropped MIDI events");
  r.p50 = percentile(times, 0.50);
  r.p95 = percentile(times, 0.95);
  r.p99 = percentile(times, 0.99);
  r.p999 = percentile(times, 0.999);
  r.max = *std::max_element(times.begin(), times.end());
  r.overBudget = static_cast<std::size_t>(
      std::count_if(times.begin(), times.end(), [&](double t) { return t > r.budgetUs; }));
  const auto calibration = calibrate(calTicks[1] - calTicks[0], calUs[1] - calUs[0],
                                     calTicks[2] - calTicks[1], calUs[2] - calUs[1]);
  r.calibration = calibration.describe();
  for (std::size_t i = 0; i < times.size(); ++i)
    if (times[i] > r.budgetUs)
      switch (classify(ticks[i], r.budgetUs, calibration)) {
      case LateCause::work: ++r.overWork; break;
      case LateCause::preempted: ++r.overPreempted; break;
      case LateCause::unknown: ++r.overUnknown; break;
      }
  out.eventsMean = eventSum / static_cast<double>(blocks * channels.size());
  out.eventsMax = eventMax;
  out.slotMap = rig.slotMap;
  out.hitsPerPass = rig.hitsPerPass;
  return out;
}

std::string onsetText(const MusicMeasurement &x) {
  char t[96];
  std::snprintf(t, sizeof(t), "%.0f/%.0f, %.0f/%.0f", x.chordSteps, x.chordExpected, x.bassSteps,
                x.bassExpected);
  return t;
}

// Pre-flight 3: the fixture generators alone over exactly four 4/4 bars
// produce 4 chords and 64 bass notes at every rate, block and tempo, at the
// samples ceil(step x samples-per-quarter) (density in musical time, not
// in samples).
std::string preflightFixtures(double rate, int block) {
  std::string text;
  for (const double bpm : {145.0, 175.0}) {
    FixtureSchedule chords, bass;
    chords.chords = true;
    const double spp = 60.0 * rate / bpm;
    const int samples = static_cast<int>(16.0 * spp);
    std::vector<int> chordAt, bassAt;
    juce::MidiBuffer midi;
    double ppq = 0.0;
    for (int start = 0; start < samples; start += block) {
      const int n = std::min(block, samples - start);
      for (auto *f : {&chords, &bass}) {
        midi.clear();
        f->fill(midi, ppq, rate, bpm, n);
        for (const auto meta : midi)
          if (meta.getMessage().isNoteOn())
            (f == &chords ? chordAt : bassAt).push_back(start + meta.samplePosition);
      }
      ppq += n * bpm / (60.0 * rate);
    }
    if (chords.onsets != 4 || bass.onsets != 64 || chordAt.size() != 16 || bassAt.size() != 64)
      fail("fixture pre-flight: " + std::to_string(chords.onsets) + " chords and " +
           std::to_string(bass.onsets) + " bass notes in 4 bars at " + std::to_string(bpm) + " BPM");
    for (std::size_t k = 0; k < bassAt.size(); ++k)
      if (std::abs(bassAt[k] - static_cast<int>(std::ceil(0.25 * static_cast<double>(k) * spp - 1e-6))) > 1)
        fail("fixture pre-flight: bass note " + std::to_string(k) + " off its 16th");
    char line[96];
    std::snprintf(line, sizeof(line), "%s%.0f BPM: 4 chords + 64 bass notes in 4 bars", text.empty() ? "" : "; ", bpm);
    text += line;
  }
  char head[48];
  std::snprintf(head, sizeof(head), "fixtures @ %.1f kHz/%d: ", rate / 1000.0, block);
  return head + text;
}

int runMusicSuite(bool quick, const char *reportPath, std::ostringstream &md) {
  std::vector<MusicSpec> specs;
  for (const bool peak : {false, true}) {
    if (quick) {
      specs.push_back({peak, 2, 0, false});
      specs.push_back({peak, 1, 2, false});
      specs.push_back({peak, 1, 2, true});
      continue;
    }
    for (const int n : {1, 2, 4, 8, 16})
      specs.push_back({peak, n, 0, false});
    for (const auto [n, f] : {std::pair{1, 2}, std::pair{4, 4}, std::pair{8, 8}, std::pair{12, 4}})
      for (const bool last : {false, true})
        specs.push_back({peak, n, f, last});
  }
  const std::vector<double> rates = quick ? std::vector<double>{48000.0}
                                          : std::vector<double>{44100.0, 48000.0, 96000.0};
  const std::vector<int> blockSizes = quick ? std::vector<int>{128} : std::vector<int>{64, 128, 512};
  // Pre-flight in every measured rate/block cell (review 6703842178, P2).
  std::vector<std::pair<double, int>> preflightConfigs;
  for (const double rate : rates)
    for (const int block : blockSizes)
      preflightConfigs.push_back({rate, block});
  md << "## E2 musical-density workloads\n\n"
        "The factory pattern Full-on Driving (typical, 145 BPM) and a synthetic 32nd grid (peak, "
        "175 BPM) do **not** establish a realtime guarantee for every VOX Drums kit. Demo Synth "
        "native and graph slots are **test fixtures**, not shipped instruments. The declared-limit "
        "suite (2048 events + 128x16) is a separate run. Fixture MIDI is scheduled in musical time "
        "(a chord per bar, bass in 16ths) and checked per cell. Pre-flight runs in every measured "
        "rate/block cell. Calibration-invalid cells: late-block causes and tails are "
        "**inconclusive**.\n\nPre-flight (fails the run on mismatch):\n\n";
  for (const auto &[rate, block] : preflightConfigs) {
    for (const bool peak : {false, true})
      md << "- " << preflightStimulus(peak, rate, block) << "\n";
    md << "- " << preflightFixtures(rate, block) << "\n";
    for (const auto &s : specs)
      if (s.fixtures > 0)
        md << "- ducking " << preflightDucking(s, rate, block) << "\n";
  }
  std::vector<MusicMeasurement> results;
  for (const auto &s : specs)
    for (const double rate : rates)
      for (const int block : blockSizes)
        results.push_back(runMusic(s, rate, block, quick ? 0.3 : 3.0));
  md << "\nSlot maps:\n\n";
  std::string last;
  for (const auto &r : results)
    if (r.m.workload != last) {
      last = r.m.workload;
      md << "- " << r.m.workload << " (" << r.hitsPerPass << " hits per 4-bar pattern pass): "
         << r.slotMap << "\n";
    }
  md << "\n| Workload | Rate | Block | Budget µs | p50 µs | p95 µs | p99 µs | p99.9 µs | max µs "
        "| Load p99 | Blocks over budget | of which work / preempted / unknown | CPU clock | "
        "Host events/slot/block mean / max | Fixture steps started/crossed: chords, bass | Audio-thread allocations |\n"
     << "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
  bool allocated = false;
  for (const auto &x : results) {
    const auto &r = x.m;
    char line[512];
    std::snprintf(line, sizeof(line),
                  "| %s | %.1f kHz | %d | %.0f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f%% | "
                  "%zu / %zu | %zu / %zu / %zu | %s | %.3f / %.0f | %s | %s |\n",
                  r.workload.c_str(), r.rate / 1000.0, r.block, r.budgetUs, r.p50, r.p95, r.p99,
                  r.p999, r.max, 100.0 * r.p99 / r.budgetUs, r.overBudget, r.blocks, r.overWork,
                  r.overPreempted, r.overUnknown,
                  (r.calibration + (r.calibration.find("invalid") != std::string::npos
                                        ? " - tails inconclusive" : "")).c_str(),
                  x.eventsMean, x.eventsMax, onsetText(x).c_str(),
                  r.allocationProbe ? std::to_string(r.allocations).c_str() : "not probed");
    md << line;
    allocated = allocated || r.allocations != 0;
  }
  std::cout << md.str();
  std::ofstream(reportPath) << md.str();
  if (allocated)
    fail("the audio thread allocated during a measured block");
  std::cout << "callback_headroom_bench --music: " << results.size() << " configurations\n";
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 4)
    fail("usage: callback_headroom_bench <instruments> <scratch> <report.md> [--quick] [--music]");
  instrumentsDir = fs::absolute(argv[1]);
  cacheRoot = fs::absolute(argv[2]) / "cache";
  fs::create_directories(cacheRoot);
  bool quick = false, music = false;
  for (int a = 4; a < argc; ++a) {
    quick = quick || std::string(argv[a]) == "--quick";
    music = music || std::string(argv[a]) == "--music";
  }
  juce::ScopedJuceInitialiser_GUI juce;
  std::ostringstream md;
  {
    // Classifier self-test against a 1 ms budget, with its own calibration
    // over 1.2 s of busy work (two halves): a 3 ms busy loop must read as
    // work, a 3 ms sleep as preempted. Plus the cost of the timing calls.
    auto spin = [](double us) {
      const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double, std::micro>(us);
      volatile double x = 1.0;
      while (std::chrono::steady_clock::now() < until)
        x = x * 1.0000001 + 1e-9;
    };
    const double t0 = static_cast<double>(threadTicks()), u0 = threadCpuUs();
    spin(600000.0);
    const double t1 = static_cast<double>(threadTicks()), u1 = threadCpuUs();
    spin(600000.0);
    const double t2 = static_cast<double>(threadTicks()), u2 = threadCpuUs();
    const auto cal = calibrate(t1 - t0, u1 - u0, t2 - t1, u2 - u1);
    const double a0 = static_cast<double>(threadTicks());
    spin(3000.0);
    const auto busy = classify(static_cast<double>(threadTicks()) - a0, 1000.0, cal);
    const double b0 = static_cast<double>(threadTicks());
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    const auto slept = classify(static_cast<double>(threadTicks()) - b0, 1000.0, cal);
    std::vector<double> cost;
    for (int k = 0; k < 2000; ++k) {
      const auto s0 = std::chrono::steady_clock::now();
      (void)threadTicks();
      (void)threadTicks();
      cost.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - s0).count());
    }
    auto name = [](LateCause c) {
      return c == LateCause::work ? "work" : c == LateCause::preempted ? "preempted" : "UNKNOWN";
    };
    char text[256];
    std::snprintf(text, sizeof(text),
                  "Classifier self-test (1 ms budget, calibration %s): 3 ms busy loop -> %s, "
                  "3 ms sleep -> %s; timing overhead per block (2 thread-clock reads) median "
                  "%.2f us\n\n",
                  cal.describe().c_str(), name(busy), name(slept), percentile(cost, 0.5));
    md << text;
    if (cal.valid && (busy != LateCause::work || slept != LateCause::preempted))
      md << "**Classifier self-test FAILED: treat every work/preempted count below as UNKNOWN.**\n\n";
  }
  if (music)
    return runMusicSuite(quick, argv[3], md);
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

  md << "| Workload | Rate | Block | Budget µs | p50 µs | p95 µs | p99 µs | p99.9 µs | max µs "
        "| Load p99 | Headroom p99 | Blocks over budget | of which work / preempted / unknown | CPU clock | Audio-thread allocations |\n"
     << "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
  bool allocated = false;
  for (const auto &r : results) {
    char line[512];
    const double load = r.p99 / r.budgetUs;
    std::snprintf(line, sizeof(line),
                  "| %s | %.1f kHz | %d | %.0f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f%% | "
                  "%.1f%% | %zu / %zu | %zu / %zu / %zu | %s | %s |\n",
                  r.workload.c_str(), r.rate / 1000.0, r.block, r.budgetUs, r.p50, r.p95, r.p99,
                  r.p999, r.max, 100.0 * load, 100.0 * (1.0 - load), r.overBudget, r.blocks,
                  r.overWork, r.overPreempted, r.overUnknown, r.calibration.c_str(),
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
