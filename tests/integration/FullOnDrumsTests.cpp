// Full-on Drums (#71) through the real Rack processor: VoxFullOnDrums.vins is
// loaded from an instruments folder exactly as the DAW constructor loads the
// one next to the VST3, and the kit is played from slot MIDI. Not DAW
// acceptance (see the Cubase guide).
#include "PluginProcessor.h"
#include "modules/BuiltInProvider.h"
#include "instrument/HostParameterSchema.h"
#include "instrument/vins/VinsPackage.h"
#include "instrument/native/NativeModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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
using juce::MidiMessage;

constexpr const char *drumsId = "com.ultimavox.fullon-drums";
enum Pad { kick, snare, clap, closedHat, openHat, tom, rim, cymbal, padCount };
enum Key { level, tune, decay, pan, character };
constexpr int notes[padCount] = {36, 38, 39, 42, 46, 45, 37, 49};
const char *padNames[padCount] = {"Kick", "Snare", "Clap", "Closed Hat",
                                  "Open Hat", "Tom", "Rim", "Cymbal"};
constexpr int kitVolume = 40, kickPitch = 41, kickPitchDecay = 42, kickDrive = 43,
              kickSub = 44, kickTail = 45, hatChoke = 46, velocitySens = 47;
constexpr int padParam(int pad, int key) { return pad * 5 + key; }

fs::path instrumentsDir, cacheRoot;
int checks = 0;

void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

// What the DAW constructor does for the instruments folder next to the VST3.
native::LoadReport installed(const fs::path &folder) {
  instrument::vins::LoadOptions options;
  options.cacheRoot = cacheRoot;
  options.trusted = instrument::vins::pinnedPublisherKeys();
  return instrument::vins::loadFolder(folder, options);
}

fs::path cachedModule() {
  for (const auto &entry : fs::recursive_directory_iterator(cacheRoot))
    if (entry.path().stem() == "VoxFullOnDrums" && entry.is_regular_file())
      return entry.path();
  return {};
}

struct Rig {
  std::unique_ptr<VstEngineAudioProcessor> p;
  std::size_t slot{};
  int channel{};
  double rate{48000.0};
  int block{256};
};

void quietBuiltIns(VstEngineAudioProcessor &p) {
  for (const char *id : {"slot01Enabled", "slot02Enabled"})
    if (auto *enabled = p.parameters().getParameter(id))
      enabled->setValueNotifyingHost(0.0f);
}

int channelOf(VstEngineAudioProcessor &p, std::size_t slot) {
  char id[32];
  std::snprintf(id, sizeof(id), "slot%02zuMidiIn", slot + 1);
  const auto *value = p.parameters().getRawParameterValue(id);
  check(value != nullptr, "slot MidiIn parameter exists");
  return static_cast<int>(std::lround(value->load()));
}

Rig makeRig(const fs::path &root, std::size_t slot = 2, double rate = 48000.0, int block = 256) {
  Rig r;
  r.p = std::make_unique<VstEngineAudioProcessor>(
      modules::createBuiltInProvider(),
      root.empty() ? native::LoadReport{} : installed(root));
  quietBuiltIns(*r.p);
  r.p->prepareToPlay(rate, block);
  r.rate = rate;
  r.block = block;
  r.slot = slot;
  if (!root.empty()) {
    juce::String diagnostic;
    check(r.p->loadSlotInstrument(slot, drumsId, diagnostic),
          "Full-on Drums loads into a slot: " + diagnostic.toStdString());
  }
  r.channel = channelOf(*r.p, slot);
  return r;
}

void setPlain(Rig &r, int poolIndex, float plain) {
  auto *parameter =
      r.p->parameters().getParameter(instrument::hostparams::poolId(r.slot, poolIndex));
  check(parameter != nullptr, "pool endpoint exists");
  parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
}

float plainOf(Rig &r, int poolIndex) {
  const auto *value =
      r.p->parameters().getRawParameterValue(instrument::hostparams::poolId(r.slot, poolIndex));
  check(value != nullptr, "pool value exists");
  return value->load();
}

struct Hit {
  int sample; // absolute sample position
  int pad;
  int velocity = 110;
};
struct Midi {
  int sample;
  MidiMessage message;
};
struct Stereo {
  std::vector<float> l, r;
  double energy(int from = 0, int to = -1) const {
    double e = 0;
    const int end = to < 0 ? static_cast<int>(l.size()) : std::min<int>(to, l.size());
    for (int i = std::max(0, from); i < end; ++i)
      e += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
    return e;
  }
  float peak() const {
    float m = 0;
    for (std::size_t i = 0; i < l.size(); ++i)
      m = std::max({m, std::abs(l[i]), std::abs(r[i])});
    return m;
  }
  bool finite() const {
    for (std::size_t i = 0; i < l.size(); ++i)
      if (!std::isfinite(l[i]) || !std::isfinite(r[i]))
        return false;
    return true;
  }
  bool operator==(const Stereo &o) const { return l == o.l && r == o.r; }
};

std::vector<Midi> hitsToMidi(const Rig &r, const std::vector<Hit> &hits) {
  std::vector<Midi> out;
  for (const auto &h : hits) {
    out.push_back({h.sample, MidiMessage::noteOn(r.channel, notes[h.pad],
                                                 static_cast<juce::uint8>(h.velocity))});
    out.push_back({h.sample + 1000, MidiMessage::noteOff(r.channel, notes[h.pad])});
  }
  return out;
}

// Clears every runtime tail in the Rack (slot FX, buses, voices) so two
// renders on one processor start from the same state.
void fresh(Rig &r) {
  r.p->releaseResources();
  r.p->prepareToPlay(r.rate, r.block);
}

// Renders from a cleared Rack unless `continuing` (then it follows on from
// the previous render, e.g. to test Panic or recovery).
Stereo render(Rig &r, int samples, const std::vector<Midi> &events, bool continuing = false) {
  if (!continuing)
    fresh(r);
  Stereo out;
  juce::AudioBuffer<float> audio(2, r.block);
  juce::MidiBuffer midi;
  for (int start = 0; start < samples; start += r.block) {
    const int n = std::min(r.block, samples - start);
    midi.clear();
    for (const auto &e : events)
      if (e.sample >= start && e.sample < start + n)
        midi.addEvent(e.message, e.sample - start);
    audio.setSize(2, n, false, false, true);
    audio.clear();
    r.p->processBlock(audio, midi);
    out.l.insert(out.l.end(), audio.getReadPointer(0), audio.getReadPointer(0) + n);
    out.r.insert(out.r.end(), audio.getReadPointer(1), audio.getReadPointer(1) + n);
  }
  return out;
}

Stereo renderHits(Rig &r, int samples, const std::vector<Hit> &hits, bool continuing = false) {
  return render(r, samples, hitsToMidi(r, hits), continuing);
}



// A 145 BPM, 1-bar full-on figure: kick on every beat, closed hats on 16ths,
// open hat on the off-beats, snare + clap on 2 and 4.
std::vector<Hit> groove(double rate) {
  const double sixteenth = 60.0 / 145.0 / 4.0 * rate;
  std::vector<Hit> hits;
  for (int step = 0; step < 16; ++step) {
    const int at = static_cast<int>(std::lround(step * sixteenth));
    if (step % 4 == 0)
      hits.push_back({at, kick, 120});
    if (step % 4 == 2)
      hits.push_back({at, openHat, 90});
    else
      hits.push_back({at, closedHat, step % 2 ? 70 : 95});
    if (step == 4 || step == 12) {
      hits.push_back({at, snare, 110});
      hits.push_back({at, clap, 100});
    }
  }
  return hits;
}

// Rising zero crossings per second in [from, to).
double crossingRate(const std::vector<float> &x, int from, int to, double rate) {
  int crossings = 0;
  for (int i = std::max(1, from); i < to; ++i)
    if (x[i - 1] <= 0.0f && x[i] > 0.0f)
      ++crossings;
  return crossings * rate / (to - from);
}

double maxDifference(const Stereo &a, const Stereo &b) {
  double d = 0;
  for (std::size_t i = 0; i < a.l.size(); ++i)
    d = std::max({d, std::abs(static_cast<double>(a.l[i]) - b.l[i]),
                  std::abs(static_cast<double>(a.r[i]) - b.r[i])});
  return d;
}

// ------------------------------------------------------------------ tests
void loadsFromTheInstrumentsFolder() {
  auto p = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                     installed(instrumentsDir));
  for (const auto &d : p->nativeModuleDiagnostics())
    std::cerr << "diagnostic: " << d << '\n';
  check(p->nativeModuleDiagnostics().empty(), "drum package loads without diagnostics");
  const instrument::InstrumentDescriptor *kit = nullptr;
  for (const auto *d : p->availableInstruments())
    if (d->id == drumsId)
      kit = d;
  check(kit != nullptr, "Full-on Drums offered in the slot catalog");
  check(kit->providerId == native::providerId && kit->directParameters &&
            kit->parameters.size() == 59,
        "59 typed host-exposed parameters; the pattern is content, not parameters");
  for (std::size_t i = 0; i < kit->parameters.size(); ++i)
    check(kit->parameters[i].poolIndex == static_cast<int>(i), "explicit stable pool index");
  check(kit->parameters[padParam(kick, level)].id == "kick.level" &&
            kit->parameters[padParam(cymbal, character)].id == "cymbal.character" &&
            kit->parameters[hatChoke].id == "kit.hat_choke" &&
            kit->parameters[hatChoke].type == instrument::ParameterType::boolean,
        "pad parameter identities");
}

void grooveAndStems() {
  auto r = makeRig(instrumentsDir);
  const auto hits = groove(r.rate);
  const int length = static_cast<int>(r.rate * 60.0 / 145.0 * 4) + 24000;
  const auto mix = renderHits(r, length, hits);
  check(mix.finite() && mix.energy() > 10.0, "groove is audible and finite");
  check(mix.peak() < 1.0f, "default groove does not clip (peak " + std::to_string(mix.peak()) + ")");

  // Stems before the stereo mix: each pad alone (other pads at level 0).
  Stereo sum;
  sum.l.assign(mix.l.size(), 0.0f);
  sum.r.assign(mix.r.size(), 0.0f);
  std::vector<Stereo> stems(padCount);
  for (int solo : {kick, snare, clap, closedHat, openHat}) {
    std::vector<float> saved(padCount);
    for (int pad = 0; pad < padCount; ++pad) {
      saved[pad] = plainOf(r, padParam(pad, level));
      if (pad != solo)
        setPlain(r, padParam(pad, level), 0.0f);
    }
    stems[solo] = renderHits(r, length, hits);
    for (int pad = 0; pad < padCount; ++pad)
      setPlain(r, padParam(pad, level), saved[pad]);
    check(stems[solo].energy() > 0.05, std::string(padNames[solo]) + " stem is audible");
    for (std::size_t i = 0; i < sum.l.size(); ++i) {
      sum.l[i] += stems[solo].l[i];
      sum.r[i] += stems[solo].r[i];
    }
  }
  check(maxDifference(sum, mix) < 1e-5, "stems sum to the stereo mix (independent voices)");
  const int beat = static_cast<int>(r.rate * 60.0 / 145.0);
  check(crossingRate(stems[kick].l, 2000, beat - 2000, r.rate) < 200.0,
        "kick stem is low-frequency");
  check(crossingRate(stems[closedHat].l, 0, length - 24000, r.rate) > 3000.0,
        "closed hat stem is high-frequency");
  check(stems[openHat].energy() > 0.0 && stems[clap].energy() > 0.0, "open hat and clap stems");
  check(renderHits(r, length, hits) == mix, "same input renders the same groove");
}

void sampleOffsetsAndSimultaneousHits() {
  auto r = makeRig(instrumentsDir);
  for (int offset : {0, 37, 100, 255}) {
    const auto out = renderHits(r, 4096, {{256 + offset, snare}});
    int first = -1;
    for (int i = 0; i < 4096 && first < 0; ++i)
      if (out.l[i] != 0.0f)
        first = i;
    check(first >= 256 + offset && first <= 256 + offset + 1,
          "hit starts at its sample offset " + std::to_string(offset) + " (first " +
              std::to_string(first) + ")");
  }
  // Ordering inside one block: snare at 10, kick at 200.
  const auto both = renderHits(r, 24000, {{200, kick}, {10, snare}});
  const auto snareOnly = renderHits(r, 24000, {{10, snare}});
  const auto kickOnly = renderHits(r, 24000, {{200, kick}});
  Stereo sum = snareOnly;
  for (std::size_t i = 0; i < sum.l.size(); ++i) {
    sum.l[i] += kickOnly.l[i];
    sum.r[i] += kickOnly.r[i];
  }
  check(maxDifference(both, sum) < 1e-6, "out-of-order events in one block keep their offsets");
  // Simultaneous: every pad on the same sample sounds together. Kit volume
  // keeps the sum below full scale so the Rack output stays linear.
  setPlain(r, kitVolume, 0.25f);
  setPlain(r, hatChoke, 0.0f); // with choke, a simultaneous hat pair cuts itself (by design)
  std::vector<Hit> all;
  for (int pad = 0; pad < padCount; ++pad)
    all.push_back({500, pad});
  const auto chord = renderHits(r, 48000, all);
  Stereo parts;
  parts.l.assign(chord.l.size(), 0.0f);
  parts.r.assign(chord.r.size(), 0.0f);
  for (int pad = 0; pad < padCount; ++pad) {
    const auto one = renderHits(r, 48000, {{500, pad}});
    check(one.energy() > 0.01, std::string(padNames[pad]) + " is audible");
    for (std::size_t i = 0; i < parts.l.size(); ++i) {
      parts.l[i] += one.l[i];
      parts.r[i] += one.r[i];
    }
  }
  check(maxDifference(chord, parts) < 1e-5, "eight simultaneous pads all sound (no voice loss)");
  setPlain(r, kitVolume, 0.6f);
  setPlain(r, hatChoke, 1.0f);
}

void chokeAndRelease() {
  auto r = makeRig(instrumentsDir);
  const int at = 12000; // closed hat 250 ms after the open hat
  setPlain(r, padParam(closedHat, level), 0.0f); // only hear the open hat
  const auto choked = renderHits(r, 48000, {{0, openHat}, {at, closedHat}});
  check(choked.energy(0, at) > 0.1, "open hat rings before the choke");
  check(choked.energy(at + 200, 48000) == 0.0, "closed hat chokes the open hat within 3 ms");
  setPlain(r, hatChoke, 0.0f);
  const auto free = renderHits(r, 48000, {{0, openHat}, {at, closedHat}});
  check(free.energy(at + 200, 48000) > 1e-4, "Hat Choke off lets the open hat ring");
  setPlain(r, hatChoke, 1.0f);
  setPlain(r, padParam(closedHat, level), 0.45f);

  // One-shot: NoteOff does not cut a voice.
  const auto withOff = render(r, 24000, {{0, MidiMessage::noteOn(r.channel, 36, (juce::uint8)110)},
                                         {100, MidiMessage::noteOff(r.channel, 36)}});
  const auto withoutOff = render(r, 24000, {{0, MidiMessage::noteOn(r.channel, 36, (juce::uint8)110)}});
  check(withOff == withoutOff && withOff.energy(4800, 24000) > 0.01,
        "NoteOff leaves one-shot drum voices ringing");
  // CC120 stops immediately; CC123 ends within 50 ms.
  const auto cc120 = render(r, 24000, {{0, MidiMessage::noteOn(r.channel, 49, (juce::uint8)110)},
                                       {4800, MidiMessage::allSoundOff(r.channel)}});
  check(cc120.energy(0, 4800) > 0.01 && cc120.energy(4800, 24000) == 0.0, "CC120 hard stop");
  const auto cc123 = render(r, 24000, {{0, MidiMessage::noteOn(r.channel, 49, (juce::uint8)110)},
                                       {4800, MidiMessage::allNotesOff(r.channel)}});
  check(cc123.energy(4800, 4800 + 2000) > 0.0 && cc123.energy(4800 + 2500, 24000) == 0.0,
        "CC123 fades out within 50 ms");
  check(renderHits(r, 2048, {{0, cymbal}}).energy() > 0.01, "cymbal rings");
  r.p->requestPanic();
  check(render(r, 2048, {}, true).energy() == 0.0, "Rack Panic silences the kit");
  check(renderHits(r, 4096, {{0, kick}}, true).energy() > 0.01, "plays again after Panic");
}

void midiOverflowFailsClosed() {
  auto r = makeRig(instrumentsDir, 2, 48000.0, 1024);
  const auto before = native::totalMidiOverflows();
  std::vector<Midi> flood;
  flood.push_back({0, MidiMessage::noteOn(r.channel, 49, (juce::uint8)110)});
  for (int i = 0; i < 700; ++i)
    flood.push_back({1024 + i, MidiMessage::controllerEvent(r.channel, 1, i % 128)});
  const auto out = render(r, 1024 * 4, flood);
  check(native::totalMidiOverflows() > before, "routed MIDI above the kit budget is counted");
  check(out.energy(0, 1024) > 0.01 && out.energy(2048, 4096) == 0.0,
        "overflow fails closed: the kit is silenced, nothing hangs");
  check(renderHits(r, 4096, {{0, kick}}, true).energy() > 0.01, "plays again after an overflow");
}

void parametersAffectOnlyTheirPad() {
  auto r = makeRig(instrumentsDir);
  auto solo = [&](int pad) { return renderHits(r, 36000, {{100, pad}}); };
  std::vector<Stereo> base;
  for (int pad = 0; pad < padCount; ++pad)
    base.push_back(solo(pad));
  for (int pad = 0; pad < padCount; ++pad)
    for (int key = 0; key < 5; ++key) {
      const int index = padParam(pad, key);
      const float before = plainOf(r, index);
      auto *parameter =
          r.p->parameters().getParameter(instrument::hostparams::poolId(r.slot, index));
      const auto range = parameter->getNormalisableRange();
      const float moved = before > 0.5f * (range.start + range.end)
                              ? range.start + 0.2f * (range.end - range.start)
                              : range.start + 0.8f * (range.end - range.start);
      setPlain(r, index, moved);
      const std::string name = std::string(padNames[pad]) + " param " + std::to_string(key);
      for (int other = 0; other < padCount; ++other) {
        const bool changed = !(solo(other) == base[other]);
        check(changed == (other == pad),
              name + (other == pad ? " changes its pad" : " leaves pad ") +
                  (other == pad ? "" : padNames[other]));
      }
      setPlain(r, index, before);
    }
  for (int index : {kickPitch, kickPitchDecay, kickDrive, kickSub, kickTail}) {
    const float before = plainOf(r, index);
    auto *parameter = r.p->parameters().getParameter(instrument::hostparams::poolId(r.slot, index));
    const auto range = parameter->getNormalisableRange();
    setPlain(r, index, range.start + 0.9f * (range.end - range.start));
    for (int other = 0; other < padCount; ++other)
      check(!(solo(other) == base[other]) == (other == kick),
            "kick global " + std::to_string(index) + " only shapes the kick");
    setPlain(r, index, before);
  }
  setPlain(r, kitVolume, 0.3f);
  for (int pad = 0; pad < padCount; ++pad)
    check(solo(pad).energy() < 0.5 * base[pad].energy(), "Kit Volume scales every pad");
  setPlain(r, kitVolume, 0.6f);
  setPlain(r, velocitySens, 0.0f);
  const auto soft = renderHits(r, 36000, {{100, snare, 20}});
  const auto hard = renderHits(r, 36000, {{100, snare, 127}});
  check(soft == hard, "Velocity Sens 0 ignores velocity");
  setPlain(r, velocitySens, 1.0f);
  check(renderHits(r, 36000, {{100, snare, 20}}).energy() <
            0.1 * renderHits(r, 36000, {{100, snare, 127}}).energy(),
        "Velocity Sens 1 follows velocity");
  setPlain(r, velocitySens, 0.8f);
  for (int pad = 0; pad < padCount; ++pad)
    check(solo(pad) == base[pad], std::string(padNames[pad]) + " restores exactly");
}

void instancesAndSlotsAreIndependent() {
  auto alone = makeRig(instrumentsDir, 3);
  auto a = makeRig(instrumentsDir, 3);
  auto b = makeRig(instrumentsDir, 3);
  setPlain(b, padParam(kick, tune), 7.0f);
  const auto hits = groove(48000.0);
  const auto reference = renderHits(alone, 30000, hits);
  const auto ra = renderHits(a, 30000, hits);
  renderHits(b, 30000, {{0, kick}, {5, snare}});
  check(ra == reference, "two Rack instances do not share kit state");
  // Two kits in one Rack on different channels.
  auto two = makeRig(instrumentsDir, 3);
  juce::String diagnostic;
  check(two.p->loadSlotInstrument(4, drumsId, diagnostic), "second kit slot");
  const int otherChannel = channelOf(*two.p, 4);
  check(otherChannel != two.channel, "second kit gets its own MIDI channel");
  auto *tuneB = two.p->parameters().getParameter(instrument::hostparams::poolId(4, padParam(kick, tune)));
  tuneB->setValueNotifyingHost(tuneB->convertTo0to1(-5.0f));
  check(renderHits(two, 30000, hits) == reference,
        "a second kit slot with other settings does not change the first");
  std::vector<Midi> onB = {{0, MidiMessage::noteOn(otherChannel, 36, (juce::uint8)110)}};
  check(render(two, 30000, onB).energy() > 0.01, "second kit plays on its own channel");
}

void projectsRestore() {
  auto first = makeRig(instrumentsDir, 5);
  setPlain(first, padParam(kick, tune), -3.0f);
  setPlain(first, padParam(snare, character), 0.9f);
  setPlain(first, padParam(openHat, decay), 0.8f);
  setPlain(first, padParam(clap, pan), 0.5f);
  setPlain(first, kickDrive, 4.0f);
  setPlain(first, hatChoke, 0.0f);
  render(first, 1024, {});
  juce::MemoryBlock state;
  first.p->getStateInformation(state);

  auto second = makeRig({}, 5);
  second.p = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                       installed(instrumentsDir));
  quietBuiltIns(*second.p);
  second.p->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  second.p->prepareToPlay(48000.0, 256);
  first.p->prepareToPlay(48000.0, 256);
  second.channel = channelOf(*second.p, 5);
  check(second.p->instrumentRack().state()[5].instrumentId == drumsId, "slot identity restored");
  check(second.channel == first.channel, "MIDI channel restored");
  for (int i = 0; i < 59; ++i)
    check(plainOf(second, i) == plainOf(first, i), "endpoint " + std::to_string(i) + " restored");
  const auto hits = groove(48000.0);
  const auto a = renderHits(first, 40000, hits);
  const auto b = renderHits(second, 40000, hits);
  check(a.energy() > 1.0 && a == b, "reopened project renders the groove bit for bit");

  Rig without;
  without.p = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                        native::LoadReport{});
  quietBuiltIns(*without.p);
  without.p->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  without.p->prepareToPlay(48000.0, 256);
  without.slot = 5;
  without.channel = channelOf(*without.p, 5);
  check(without.p->instrumentRack().state()[5].instrumentId == drumsId,
        "missing package keeps the slot identity");
  check(renderHits(without, 20000, hits).energy() == 0.0, "missing package slot is silent");
  juce::MemoryBlock resaved;
  without.p->getStateInformation(resaved);
  Rig back;
  back.p = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                     installed(instrumentsDir));
  quietBuiltIns(*back.p);
  back.p->setStateInformation(resaved.getData(), static_cast<int>(resaved.getSize()));
  back.p->prepareToPlay(48000.0, 256);
  back.slot = 5;
  back.channel = channelOf(*back.p, 5);
  check(renderHits(back, 40000, hits) == a, "reinstalled package recovers the same groove");
}

void deterministicOfflineRender() {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    Stereo reference;
    for (int block : {64, 128, 512}) {
      auto r = makeRig(instrumentsDir, 2, rate, block);
      const auto hits = groove(rate);
      const int length = static_cast<int>(rate * 1.8);
      const auto first = renderHits(r, length, hits);
      r.p->prepareToPlay(rate, block);
      const auto again = renderHits(r, length, hits);
      const std::string where = std::to_string(static_cast<int>(rate)) + " Hz / " +
                                std::to_string(block);
      check(first == again && first.finite() && first.energy() > 1.0,
            "deterministic render at " + where);
      if (reference.l.empty())
        reference = first;
      else
        check(first == reference, "block size does not change the render at " + where);
    }
  }
}

void moduleRejectsInvalidState() {
  check(!cachedModule().empty(), "the drum module is in the instrument cache");
  std::string diagnostic;
  auto module = native::NativeModule::acquire(cachedModule(), diagnostic);
  check(module != nullptr, "module acquired: " + diagnostic);
  const auto &t = module->table();
  const VoxCreateParamsV2 params{sizeof(VoxCreateParamsV2), VOX_MODULE_ABI_V2,
                                 VOX_SLOT_INDEX_NONE, 0};
  VoxInstance s = nullptr;
  check(t.create(drumsId, &params, &s) == VOX_OK, "create");
  std::uint8_t state[512];
  std::uint32_t written = 0, version = 0;
  check(t.saveState(s, state, sizeof(state), &written, &version) == VOX_OK && version == 3 &&
            written == 12 + 59 * 4 + 16,
        "state v3 holds parameters, note map and choke groups (no pattern)");
  const std::uint32_t notesAt = 12 + 59 * 4;
  check(state[notesAt] == 36 && state[notesAt + 3] == 42 && state[notesAt + 8 + 3] == 1 &&
            state[notesAt + 8 + 4] == 1,
        "default note map and hat choke group are saved");
  auto corrupt = [&](std::uint32_t at, std::uint8_t value) {
    std::uint8_t copy[512];
    std::memcpy(copy, state, written);
    copy[at] = value;
    return t.loadState(s, 3, copy, written);
  };
  check(corrupt(notesAt + 1, 36) == VOX_E_STATE_CORRUPT, "duplicate pad note rejected");
  check(corrupt(notesAt + 2, 200) == VOX_E_STATE_CORRUPT, "invalid MIDI note rejected");
  check(corrupt(notesAt + 8, 9) == VOX_E_STATE_CORRUPT, "invalid choke group rejected");
  check(corrupt(0, 0) == VOX_E_STATE_CORRUPT, "bad magic rejected");
  check(t.loadState(s, 4, state, written) == VOX_E_STATE_VERSION, "future version rejected");
  check(t.loadState(s, 2, state, written) == VOX_E_STATE_VERSION,
        "only the current state version loads (no pre-release migration)");
  check(t.loadState(s, 3, state, written) == VOX_OK, "valid state loads");
  float values[59];
  for (int i = 0; i < 59; ++i)
    values[i] = 0.5f;
  check(t.setAllParameters(s, values, 59) == VOX_E_INVALID_ARG,
        "out-of-range snapshot rejected as a whole");
  check(t.destroy(s) == VOX_OK, "destroy");
}

void noAllocationWhilePlaying() {
  auto r = makeRig(instrumentsDir, 7);
  juce::AudioBuffer<float> audio(2, r.block);
  juce::MidiBuffer midi;
  midi.ensureSize(4096);
  renderHits(r, 2048, {{0, kick}}); // warm up
#if defined(VOX_TEST_INTERPOSE_MALLOC)
  voxProbeBegin();
#endif
  for (int b = 0; b < 400; ++b) {
    midi.clear();
    for (int pad = 0; pad < padCount; ++pad)
      if ((b + pad) % 3 == 0)
        midi.addEvent(MidiMessage::noteOn(r.channel, notes[pad], (juce::uint8)(60 + pad * 8)),
                      (pad * 29) % r.block);
    if (auto *c = r.p->parameters().getParameter(
            instrument::hostparams::poolId(r.slot, padParam(b % padCount, tune))))
      c->setValue(static_cast<float>(b % 10) / 10.0f);
    r.p->processBlock(audio, midi);
  }
#if defined(VOX_TEST_INTERPOSE_MALLOC)
  const auto allocations = voxProbeEnd();
  check(allocations == 0, "drum slot processing allocates nothing (" +
                              std::to_string(allocations) + ")");
  std::cout << "allocation probe: 0 allocations in 400 Rack blocks with Full-on Drums\n";
#else
  std::cout << "allocation probe: not available on this platform\n";
#endif
}
// ---------------------------------------------------------------- D2
// The internal 16-step sequencer runs only from the Rack's VoxTransportV2.
class TestPlayHead final : public juce::AudioPlayHead {
public:
  juce::Optional<PositionInfo> getPosition() const override {
    PositionInfo info;
    info.setBpm(bpm);
    info.setPpqPosition(ppq);
    info.setIsPlaying(playing);
    info.setTimeInSamples(static_cast<juce::int64>(samples));
    return info;
  }
  double bpm = 145.0, ppq = 0.0;
  bool playing = true;
  double samples = 0;
};

constexpr int seqMode = 48, seqSwing = 49, seqAccent = 50, seqLane0 = 51;

// The slot's Rack-owned pattern; edits go through the Rack API only.
std::unique_ptr<pattern::Pattern> slotPattern(Rig &r) {
  auto p = std::make_unique<pattern::Pattern>();
  check(r.p->instrumentRack().pattern(r.slot, *p), "slot holds a Rack-owned pattern");
  return p;
}
void setPattern(Rig &r, const pattern::Pattern &p) {
  std::string diagnostic;
  check(r.p->instrumentRack().setPattern(r.slot, p, diagnostic),
        "pattern edit accepted: " + diagnostic);
}
// One bar of 16ths with a single kick on the downbeat of every beat.
std::unique_ptr<pattern::Pattern> kickOnBeats(Rig &r, std::uint32_t steps = 16,
                                              std::uint32_t timing = VOX_TIMING_SIXTEENTH) {
  auto p = std::make_unique<pattern::Pattern>(
      pattern::emptyPattern(r.p->instrumentRack().descriptor(r.slot)->patternContent->caps));
  p->stepsPerBar = steps;
  p->timing = timing;
  for (std::uint32_t st = 0; st < steps; st += 4) {
    p->at(kick, 0, st).flags = VOX_STEP_ON;
    p->at(kick, 0, st).velocity = 120;
  }
  return p;
}

// Renders with a running host transport; the playhead advances per block.
Stereo play(Rig &r, TestPlayHead &head, int samples, const std::vector<Midi> &events = {}) {
  r.p->setPlayHead(&head);
  Stereo out;
  juce::AudioBuffer<float> audio(2, r.block);
  juce::MidiBuffer midi;
  for (int start = 0; start < samples; start += r.block) {
    const int n = std::min(r.block, samples - start);
    midi.clear();
    for (const auto &e : events)
      if (e.sample >= start && e.sample < start + n)
        midi.addEvent(e.message, e.sample - start);
    audio.setSize(2, n, false, false, true);
    audio.clear();
    r.p->processBlock(audio, midi);
    out.l.insert(out.l.end(), audio.getReadPointer(0), audio.getReadPointer(0) + n);
    out.r.insert(out.r.end(), audio.getReadPointer(1), audio.getReadPointer(1) + n);
    if (head.playing) {
      head.ppq += n * head.bpm / (60.0 * r.rate);
      head.samples += n;
    }
  }
  r.p->setPlayHead(nullptr);
  return out;
}

void onlyLanes(Rig &r, std::initializer_list<int> pads) {
  for (int pad = 0; pad < padCount; ++pad)
    setPlain(r, seqLane0 + pad,
             std::find(pads.begin(), pads.end(), pad) != pads.end() ? 1.0f : 0.0f);
}

// First sample at or after `from` whose magnitude exceeds zero, after silence.
std::vector<int> onsets(const std::vector<float> &x, double minGapSamples) {
  std::vector<int> found;
  int last = -1000000;
  for (int i = 0; i < static_cast<int>(x.size()); ++i)
    if (x[i] != 0.0f && (i == 0 || x[i - 1] == 0.0f) && i - last > minGapSamples) {
      found.push_back(i);
      last = i;
    }
  return found;
}

void sequencerPlaysOnTheHostGrid() {
  for (int block : {64, 256, 512}) {
    auto r = makeRig(instrumentsDir, 2, 48000.0, block);
    setPlain(r, seqMode, 1.0f); // Internal
    setPattern(r, *kickOnBeats(r)); // a kick on every beat, every bar
    onlyLanes(r, {kick});
    setPlain(r, padParam(kick, decay), 0.08f); // short, so each onset follows silence
    setPlain(r, kickTail, 0.03f);
    TestPlayHead head;
    const double spb = 60.0 / 145.0 * r.rate;
    const auto out = play(r, head, static_cast<int>(spb * 16));
    const auto hits = onsets(out.l, spb / 2);
    check(hits.size() == 16, "16 kicks in 4 bars at block " + std::to_string(block) + " (got " +
                                 std::to_string(hits.size()) + ")");
    for (std::size_t k = 0; k < hits.size(); ++k)
      check(std::abs(hits[k] - std::ceil(k * spb - 1e-6)) <= 1.0,
            "kick " + std::to_string(k) + " lands on its beat");
  }
  auto a = makeRig(instrumentsDir, 2, 48000.0, 64);
  auto b = makeRig(instrumentsDir, 2, 48000.0, 512);
  for (auto *r : {&a, &b})
    setPlain(*r, seqMode, 1.0f);
  TestPlayHead ha, hb;
  const int fourBars = static_cast<int>(60.0 / 145.0 * 48000.0 * 16);
  const auto ra = play(a, ha, fourBars), rb = play(b, hb, fourBars);
  check(ra.energy() > 10.0 && ra.finite() && ra.peak() < 1.0f,
        "factory pattern is audible, finite and unclipped");
  check(maxDifference(ra, rb) < 1e-6, "pattern render does not depend on block size");
}

void sequencerModesAvoidDoubleTriggers() {
  const double spb = 60.0 / 145.0 * 48000.0;
  const int bar = static_cast<int>(spb * 4);
  auto r = makeRig(instrumentsDir);
  TestPlayHead head;
  check(play(r, head, bar).energy() == 0.0, "Host mode: a running transport alone is silent");
  setPlain(r, seqMode, 1.0f);
  onlyLanes(r, {kick});
  TestPlayHead h1;
  const auto internal = play(r, h1, bar);
  r.p->releaseResources();
  r.p->prepareToPlay(48000.0, 256);
  TestPlayHead h2;
  const std::vector<Midi> kicks = {{0, MidiMessage::noteOn(r.channel, 36, (juce::uint8)120)},
                                   {1000, MidiMessage::noteOn(r.channel, 38, (juce::uint8)120)}};
  const auto internalWithMidi = play(r, h2, bar, kicks);
  check(internalWithMidi == internal, "Internal mode ignores incoming notes (no double kick)");
  setPlain(r, seqMode, 2.0f); // Both
  r.p->releaseResources();
  r.p->prepareToPlay(48000.0, 256);
  TestPlayHead h3;
  check(play(r, h3, bar, kicks).energy() > internal.energy(), "Both mode adds the MIDI hits");

  // Transport stop: no new steps start.
  setPlain(r, seqMode, 1.0f);
  TestPlayHead stopped;
  stopped.playing = false;
  check(play(r, stopped, bar).energy() == 0.0, "stopped transport plays nothing");
}

void sequencerFollowsLoopsSeeksAndSwing() {
  const double spb = 60.0 / 145.0 * 48000.0;
  auto r = makeRig(instrumentsDir);
  setPlain(r, seqMode, 1.0f);
  onlyLanes(r, {kick});
  setPlain(r, padParam(kick, decay), 0.08f);
  setPlain(r, kickTail, 0.03f);
  TestPlayHead head;
  play(r, head, static_cast<int>(spb * 4) - 256); // almost one bar
  head.ppq = 0.0; // the host loops back to the bar start
  const auto looped = play(r, head, static_cast<int>(spb));
  const auto hits = onsets(looped.l, spb / 2);
  check(!hits.empty() && hits.front() <= 1, "a loop back replays the downbeat at once");
  head.ppq = 2.5; // seek between beats
  const auto sought = play(r, head, static_cast<int>(spb));
  const auto after = onsets(sought.l, spb / 2);
  check(!after.empty() && std::abs(after.front() - std::ceil(0.5 * spb - 1e-6)) <= 1.0,
        "after a seek the next beat lands on the grid");

  onlyLanes(r, {closedHat});
  setPlain(r, seqSwing, 0.5f);
  setPlain(r, padParam(closedHat, decay), 0.02f);
  TestPlayHead swung;
  const auto out = play(r, swung, static_cast<int>(spb));
  const auto hats = onsets(out.l, spb / 16);
  check(hats.size() >= 3, "closed hat 16ths play");
  check(std::abs(hats[1] - std::ceil((0.25 + 0.125) * spb - 1e-6)) <= 1.0,
        "swing delays the odd 16th by half a 16th");
  setPlain(r, seqSwing, 0.0f);
}

void factoryPatternsAndInit() {
  auto r = makeRig(instrumentsDir);
  const auto *content = r.p->instrumentRack().descriptor(r.slot)->patternContent.get();
  check(content != nullptr && content->caps.playback == VOX_PATTERN_PLAYBACK_INTERNAL &&
            content->caps.laneCount == padCount && content->caps.maxStepsPerBar == 16 &&
            content->caps.maxBars == 4 &&
            content->caps.stepFields == (VOX_FIELD_VELOCITY | VOX_FIELD_ACCENT),
        "the kit declares 8 lanes x 16 steps x 4 bars, velocity + accent");
  check(content->caps.laneNotes == std::vector<std::uint8_t>(std::begin(notes), std::end(notes)),
        "lane notes are the kit note map");
  check(content->factory.size() == 5 && content->factory[0].name == "Full-on Basic" &&
            content->factory[4].name == "Kick & Offbeat",
        "five factory patterns exported by name");
  const auto loaded = slotPattern(r);
  check(*loaded == *content->factory[0].pattern &&
            r.p->instrumentRack().state()[r.slot].patternPreset == "Full-on Basic" &&
            r.p->instrumentRack().state()[r.slot].patternSchemaVersion == VOX_PATTERN_SCHEMA_V2,
        "a freshly loaded kit holds Full-on Basic as its Rack pattern");
  const auto &basic = *content->factory[0].pattern;
  check(basic.at(kick, 0, 0).flags == (VOX_STEP_ON | VOX_STEP_ACCENT) &&
            basic.at(kick, 0, 4).flags == VOX_STEP_ON && basic.at(kick, 3, 12).flags == 0 &&
            basic.at(tom, 3, 15).flags == (VOX_STEP_ON | VOX_STEP_ACCENT),
        "bar 4 of the factory pattern is the fill");
}

void sequencerPatternEdits() {
  const double spb = 60.0 / 145.0 * 48000.0;
  const int bars4 = static_cast<int>(spb * 16);
  auto render = [&](auto setup) {
    auto r = makeRig(instrumentsDir);
    setPlain(r, seqMode, 1.0f);
    setup(r);
    TestPlayHead head;
    return play(r, head, bars4);
  };
  const auto base = render([](Rig &) {});
  check(base.energy() > 1.0, "the init pattern plays");
  check(!(render([](Rig &r) {
          std::string diagnostic;
          check(r.p->instrumentRack().loadFactoryPattern(r.slot, 2, diagnostic), diagnostic);
        }) == base),
        "another factory pattern sounds different");
  check(!(render([](Rig &r) {
          auto p = slotPattern(r);
          for (int lane = 0; lane < padCount; ++lane)
            for (std::uint32_t st = 0; st < 16; ++st)
              p->at(lane, 3, st) = pattern::defaultStep(); // clear the fill bar
          setPattern(r, *p);
        }) == base),
        "editing bar 4 changes bar 4");
  check(!(render([](Rig &r) { setPlain(r, seqAccent, 0.0f); }) == base), "Seq Accent changes hits");
  check(!(render([](Rig &r) { setPlain(r, seqLane0 + openHat, 0.0f); }) == base),
        "lane switch removes a pad");

  // Editing one cell changes exactly that step.
  auto r = makeRig(instrumentsDir);
  setPlain(r, seqMode, 1.0f);
  onlyLanes(r, {snare});
  setPlain(r, padParam(snare, decay), 0.05f);
  auto p = kickOnBeats(r);
  p->at(snare, 0, 6).flags = VOX_STEP_ON;
  p->at(snare, 0, 6).velocity = 100;
  setPattern(r, *p);
  TestPlayHead head;
  const auto one = play(r, head, static_cast<int>(spb * 4));
  const int stepSamples = static_cast<int>(spb / 4);
  check(one.energy(6 * stepSamples, 7 * stepSamples) > 0.0 &&
            one.energy(0, 6 * stepSamples) == 0.0 &&
            one.energy(8 * stepSamples, 16 * stepSamples) == 0.0,
        "a single edited cell sounds on its step only");

  // Steps per bar and timing come from the pattern.
  onlyLanes(r, {kick});
  setPlain(r, padParam(kick, decay), 0.08f);
  setPlain(r, kickTail, 0.03f);
  setPattern(r, *kickOnBeats(r, 8)); // 8-step bar: kicks on steps 0 and 4
  TestPlayHead h8;
  const auto eight = play(r, h8, static_cast<int>(spb * 4));
  check(onsets(eight.l, spb / 2).size() == 4, "an 8-step bar repeats twice per 4/4 bar");
  setPattern(r, *kickOnBeats(r, 16, VOX_TIMING_EIGHTH)); // steps 0,4,8,12 of 8ths
  TestPlayHead he;
  const auto eighths = play(r, he, static_cast<int>(spb * 8));
  const auto eh = onsets(eighths.l, spb / 2);
  check(eh.size() == 4 && std::abs(eh[1] - std::ceil(2 * spb - 1e-6)) <= 1.0,
        "eighth-note timing doubles the step length");

  // An invalid pattern is rejected and the old one stays.
  auto bad = kickOnBeats(r);
  bad->at(kick, 0, 0).note = 3; // drums do not read NOTE
  std::string diagnostic;
  const auto before = slotPattern(r);
  check(!r.p->instrumentRack().setPattern(r.slot, *bad, diagnostic) && *slotPattern(r) == *before,
        "an invalid edit is rejected without touching the pattern");
}

// An edit made while the transport runs applies from the next block, at the
// current musical position: no restart from step 0, no replayed downbeat.
void editWhilePlaying() {
  const double spb = 60.0 / 145.0 * 48000.0;
  auto r = makeRig(instrumentsDir);
  setPlain(r, seqMode, 1.0f);
  onlyLanes(r, {kick, snare});
  setPlain(r, padParam(kick, decay), 0.08f);
  setPlain(r, kickTail, 0.03f);
  setPlain(r, padParam(snare, decay), 0.05f);
  setPattern(r, *kickOnBeats(r));
  TestPlayHead head;
  const auto first = play(r, head, static_cast<int>(spb * 2)); // beats 1..2
  auto p = kickOnBeats(r);
  for (std::uint32_t st = 0; st < 16; st += 4)
    p->at(kick, 0, st).flags = 0; // kicks out
  p->at(snare, 0, 10).flags = VOX_STEP_ON; // a snare on the "and" of beat 3
  setPattern(r, *p);
  const auto second = play(r, head, static_cast<int>(spb * 2)); // beats 3..4
  const auto kicks = onsets(first.l, spb / 2);
  const auto snares = onsets(second.l, spb / 4);
  check(kicks.size() == 2, "two kicks before the edit");
  check(snares.size() == 1 && std::abs(snares[0] - std::ceil(0.5 * spb - 1e-6)) <= 1.0,
        "after the edit only the new snare plays, at its musical position");
}

void sequencerStateReopensWithoutDuplicates() {
  const int bars4 = static_cast<int>(60.0 / 145.0 * 48000.0 * 16);
  auto first = makeRig(instrumentsDir, 6);
  setPlain(first, seqMode, 1.0f);
  setPlain(first, seqSwing, 0.2f);
  auto edited = slotPattern(first);
  edited->at(rim, 1, 9).flags = VOX_STEP_ON | VOX_STEP_ACCENT;
  edited->at(rim, 1, 9).velocity = 77;
  setPattern(first, *edited);
  check(first.p->instrumentRack().state()[6].patternPreset.empty(),
        "a user edit is no longer the factory pattern");
  render(first, 512, {});
  juce::MemoryBlock state;
  first.p->getStateInformation(state);
  first.p->releaseResources();
  first.p->prepareToPlay(48000.0, 256);
  TestPlayHead h1;
  const auto a = play(first, h1, bars4);

  Rig second;
  second.p = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                       installed(instrumentsDir));
  quietBuiltIns(*second.p);
  second.p->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  second.p->prepareToPlay(48000.0, 256);
  second.slot = 6;
  second.channel = channelOf(*second.p, 6);
  check(*slotPattern(second) == *edited, "the edited pattern reopens exactly");
  TestPlayHead h2;
  const auto b = play(second, h2, bars4);
  check(a.energy() > 1.0 && a == b, "reopened project plays the same pattern, no duplicate beats");

  // A corrupted pattern payload rejects the project state as a whole.
  auto slots = second.p->instrumentRack().snapshotState();
  slots[6].patternPayload.back() ^= std::byte{0x40};
  std::string diagnostic;
  check(!second.p->instrumentRack().replaceState(slots, nullptr, diagnostic) &&
            *slotPattern(second) == *edited,
        "a corrupt pattern payload is rejected transactionally");
}

// MIDI export of the internal pattern is what the kit actually plays: the
// exported file, played back through Host MIDI, renders bit for bit like the
// internal sequencer (145 BPM, swing, accent and edited velocities).
void exportMatchesPlayback() {
  const double rate = 48000.0, bpm = 145.0;
  const double spp = 60.0 * rate / bpm; // samples per quarter
  const int bars4 = static_cast<int>(spp * 16);
  auto setup = [](Rig &r) {
    // 0.25 of a step = 60 ticks: exactly representable, so the replay can be
    // compared bit for bit (other swing values round to the nearest tick).
    setPlain(r, seqSwing, 0.25f);
    setPlain(r, seqAccent, 0.7f);
    setPlain(r, seqLane0 + cymbal, 0.0f); // a muted lane exports nothing
    auto p = slotPattern(r);
    p->at(rim, 1, 9).flags = VOX_STEP_ON | VOX_STEP_ACCENT;
    p->at(rim, 1, 9).velocity = 77;
    p->at(snare, 2, 4).velocity = 41;
    setPattern(r, *p);
  };
  auto internal = makeRig(instrumentsDir, 4);
  setup(internal);
  setPlain(internal, seqMode, 1.0f);
  TestPlayHead h1;
  const auto played = play(internal, h1, bars4);

  juce::MemoryBlock bytes;
  juce::String diagnostic;
  check(internal.p->serializeSlotPatternMidi(internal.slot, bytes, diagnostic),
        "internal pattern exports: " + diagnostic.toStdString());
  juce::MemoryInputStream stream(bytes, false);
  juce::MidiFile file;
  check(file.readFrom(stream) && file.getNumTracks() == padCount &&
            file.getTimeFormat() == 960,
        "type-1 file with one track per lane at 960 ticks per quarter");
  std::vector<Midi> events;
  bool rimAccent = false, softSnare = false, cymbalNotes = false;
  for (int lane = 0; lane < file.getNumTracks(); ++lane) {
    const auto *track = file.getTrack(lane);
    std::string name;
    for (int e = 0; e < track->getNumEvents(); ++e) {
      const auto &m = track->getEventPointer(e)->message;
      if (m.isTrackNameEvent())
        name = m.getTextFromTextMetaEvent().toStdString();
      if (!m.isNoteOn())
        continue;
      check(m.getChannel() == internal.channel && m.getNoteNumber() == notes[lane],
            "lane notes on the slot channel");
      cymbalNotes = cymbalNotes || lane == cymbal;
      const double ppq = m.getTimeStamp() / 960.0;
      rimAccent = rimAccent || (lane == rim && std::abs(ppq - (4.0 + 9 * 0.25 + 0.0625)) < 1e-9 &&
                                m.getVelocity() == 112); // 77 + 0.7 * (127 - 77)
      softSnare = softSnare || (lane == snare && std::abs(ppq - (8.0 + 1.0)) < 1e-9 &&
                                m.getVelocity() == 41);
      events.push_back({static_cast<int>(std::ceil(ppq * spp - 1e-6)),
                        MidiMessage::noteOn(internal.channel, m.getNoteNumber(),
                                            static_cast<juce::uint8>(m.getVelocity()))});
    }
    check(name == padNames[lane], "track named after its lane: " + name);
  }
  check(rimAccent, "edited accented rim exported at its swung position and played velocity");
  check(softSnare, "edited snare velocity exported");
  check(!cymbalNotes, "a muted lane exports no notes");

  auto host = makeRig(instrumentsDir, 4);
  setup(host);
  setPlain(host, seqMode, 0.0f); // Host MIDI: only the exported notes play
  TestPlayHead h2;
  const auto replayed = play(host, h2, bars4, events);
  check(played.energy() > 1.0 && maxDifference(played, replayed) < 1e-6,
        "the exported file replays exactly like the internal sequencer");

  setPlain(internal, seqMode, 0.0f);
  check(!internal.p->serializeSlotPatternMidi(internal.slot, bytes, diagnostic) &&
            diagnostic.contains("plays nothing"),
        "Host MIDI mode has no internal notes to export");
}

// Internal kicks duck another slot at the sample they play, in the same block:
// the real kit (Internal mode, only its kick lane, silent so only the target
// is measured) ducks a held Demo Synth note to zero at every kick, including
// after a host loop back.
void internalKickDucksInTheSameBlock() {
  const double rate = 48000.0, spp = 60.0 * rate / 145.0;
  for (const auto [kitSlot, synthSlot] : {std::pair{2u, 9u}, std::pair{9u, 2u}}) {
    auto r = makeRig(instrumentsDir, kitSlot, rate, 128);
    juce::String diagnostic;
    check(r.p->loadSlotInstrument(synthSlot, "com.ultimavox.demo-synth", diagnostic),
          "Demo Synth loads as the duck target: " + diagnostic.toStdString());
    const int synthChannel = channelOf(*r.p, synthSlot);
    setPlain(r, seqMode, 1.0f);
    setPattern(r, *kickOnBeats(r));
    setPlain(r, kitVolume, 0.0f); // the kit still plays, but adds no audio
    auto param = [&](const juce::String &id, float plain) {
      auto *p = r.p->parameters().getParameter(id);
      check(p != nullptr, "routing parameter " + id.toStdString());
      p->setValueNotifyingHost(p->convertTo0to1(plain));
    };
    param("rack.duck.source", static_cast<float>(kitSlot + 1));
    param("rack.duck.note", 36.0f);
    param("rack.duck.attack", 0.0f);
    param("rack.duck.hold", 0.5f);
    param("rack.duck.release", 1.0f);
    char depth[32];
    std::snprintf(depth, sizeof(depth), "slot%02u.duck.depth", synthSlot + 1);
    param(depth, 1.0f);
    TestPlayHead head;
    head.ppq = 0.25; // start after the first beat so the synth settles
    const std::vector<Midi> hold = {{0, MidiMessage::noteOn(synthChannel, 60, (juce::uint8)110)}};
    const auto beat = static_cast<int>(spp);
    const auto out = play(r, head, beat * 4, hold); // beats 2..5 (ppq 0.25..4.25)
    for (int k = 1; k <= 4; ++k) {
      const int at = static_cast<int>(std::ceil((k - 0.25) * spp - 1e-6));
      check(out.l[at] == 0.0f && out.r[at] == 0.0f && std::abs(out.l[at - 1]) > 0.0f,
            "beat " + std::to_string(k + 1) + " kick ducks the synth at its sample (kit slot " +
                std::to_string(kitSlot + 1) + ")");
    }
    head.ppq = 0.0; // host loops back to the bar start
    const auto looped = play(r, head, beat / 2, hold);
    check(looped.l[0] == 0.0f && looped.r[0] == 0.0f,
          "after a loop back the downbeat kick ducks at once");
  }
}

// The exported file always closes after its last note-off, also for short
// steps with a late, maximally swung last hit.
void exportEndsAfterTheLastNote() {
  for (const auto timing : {VOX_TIMING_THIRTY_SECOND, VOX_TIMING_EIGHTH_TRIPLET}) {
    auto r = makeRig(instrumentsDir, 5);
    setPlain(r, seqMode, 1.0f);
    setPlain(r, seqSwing, 0.5f);
    auto p = std::make_unique<pattern::Pattern>(
        pattern::emptyPattern(r.p->instrumentRack().descriptor(r.slot)->patternContent->caps));
    p->timing = timing;
    p->stepsPerBar = 16;
    p->at(kick, 0, 15).flags = VOX_STEP_ON; // the last, odd (swung) step
    setPattern(r, *p);
    juce::MemoryBlock bytes;
    juce::String diagnostic;
    check(r.p->serializeSlotPatternMidi(r.slot, bytes, diagnostic),
          "short-step pattern exports: " + diagnostic.toStdString());
    juce::MemoryInputStream stream(bytes, false);
    juce::MidiFile file;
    check(file.readFrom(stream), "the exported file re-imports");
    const double step = timing == VOX_TIMING_THIRTY_SECOND ? 0.125 : 1.0 / 3.0;
    for (int lane = 0; lane < file.getNumTracks(); ++lane) {
      const auto *track = file.getTrack(lane);
      double lastOff = 0.0, end = -1.0;
      for (int e = 0; e < track->getNumEvents(); ++e) {
        const auto &m = track->getEventPointer(e)->message;
        if (m.isNoteOff())
          lastOff = std::max(lastOff, m.getTimeStamp());
        if (m.isEndOfTrackMetaEvent())
          end = m.getTimeStamp();
        if (lane == kick && m.isNoteOn())
          check(std::abs(m.getTimeStamp() / 960.0 - (15 + 0.5) * step) < 1.0 / 960.0,
                "the swung last hit keeps its position");
      }
      check(end >= lastOff && end >= 16 * step * 960.0 - 1e-6,
            "End-of-Track follows the last note-off and the pattern length");
    }
  }
}

// ------------------------------------------------- multi-track Host MIDI
// Owner workflow (#72): eight Cubase MIDI tracks (one per pad) all output to
// the same Rack instance on the kit slot's channel. The kit must sound the
// same as one merged clip, whatever order the DAW merges same-tick events in.
struct Part {
  std::string name;
  std::vector<Midi> events;
};

// Reads a Standard MIDI File as per-track events in samples at `rate`, on the
// kit slot's channel. Track 0 of a type-1 file (tempo, metre) has no notes.
std::vector<Part> readMidiFile(const fs::path &file, double rate, int channel) {
  juce::FileInputStream in(juce::File(file.string()));
  check(in.openedOk(), "MIDI file opens: " + file.string());
  juce::MidiFile midi;
  check(midi.readFrom(in), "MIDI file parses: " + file.string());
  midi.convertTimestampTicksToSeconds();
  std::vector<Part> parts;
  for (int t = 0; t < midi.getNumTracks(); ++t) {
    Part part;
    for (const auto *holder : *midi.getTrack(t)) {
      const auto &m = holder->message;
      if (m.isTrackNameEvent())
        part.name = m.getTextFromTextMetaEvent().toStdString();
      if (!m.isNoteOnOrOff())
        continue;
      check(m.getChannel() == 1, part.name + ": notes are on MIDI channel 1");
      auto routed = m;
      routed.setChannel(channel);
      part.events.push_back(
          {static_cast<int>(std::lround(m.getTimeStamp() * rate)), routed});
    }
    parts.push_back(std::move(part));
  }
  return parts;
}

// Merges tracks the way a DAW feeds one plugin input: per sample, tracks in
// the given order (each track's events in its own order).
std::vector<Midi> merge(const std::vector<Part> &parts, const std::vector<int> &order) {
  std::vector<Midi> out;
  for (int t : order)
    out.insert(out.end(), parts[static_cast<std::size_t>(t)].events.begin(),
               parts[static_cast<std::size_t>(t)].events.end());
  std::stable_sort(out.begin(), out.end(),
                   [](const Midi &a, const Midi &b) { return a.sample < b.sample; });
  return out;
}

std::vector<Midi> withoutNote(const std::vector<Midi> &events, int note) {
  std::vector<Midi> out;
  for (const auto &e : events)
    if (e.message.getNoteNumber() != note)
      out.push_back(e);
  return out;
}

void multiTrackHostMidi(const fs::path &midiDir) {
  auto r = makeRig(instrumentsDir);
  check(plainOf(r, seqMode) == 0.0f, "Seq Mode defaults to Host MIDI");
  const auto tracks = readMidiFile(midiDir / "fullon-drums-145bpm-8-tracks.mid", r.rate, r.channel);
  const auto single = readMidiFile(midiDir / "fullon-drums-145bpm-4bar.mid", r.rate, r.channel);

  // The file: a conductor track plus eight named parts, each with only its
  // own fixed note on channel 1; together they are exactly the 1-track groove.
  const std::vector<std::pair<std::string, int>> expected = {
      {"Kick", 36},       {"Rim", 37}, {"Snare", 38},    {"Clap", 39},
      {"Closed Hat", 42}, {"Tom", 45}, {"Open Hat", 46}, {"Cymbal", 49}};
  check(tracks.size() == 9 && tracks[0].events.empty(),
        "type-1 file: conductor track + eight part tracks");
  check(single.size() == 1, "the one-track groove is kept");
  std::size_t total = 0;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const auto &part = tracks[i + 1];
    check(part.name == expected[i].first, "track " + std::to_string(i + 1) + " is named " +
                                              expected[i].first + " (" + part.name + ")");
    bool own = !part.events.empty();
    for (const auto &e : part.events)
      own = own && e.message.getNoteNumber() == expected[i].second;
    check(own, part.name + " holds only note " + std::to_string(expected[i].second));
    total += part.events.size();
  }
  check(total == single[0].events.size(), "the eight parts together are the whole groove");

  const int length = static_cast<int>(std::lround(4 * 4 * 60.0 / 145.0 * r.rate)) + 24000;
  std::vector<int> order = {1, 2, 3, 4, 5, 6, 7, 8};
  const auto groove = render(r, length, single[0].events);
  check(groove.energy() > 1.0 && groove.finite() && groove.peak() < 1.0,
        "the groove is audible, finite and below full scale");
  check(render(r, length, merge(tracks, order)) == groove,
        "eight tracks into one kit slot sound bit for bit like the merged clip");

  // A DAW may merge same-tick events from different tracks in any order.
  std::vector<int> reversed(order.rbegin(), order.rend());
  check(render(r, length, merge(tracks, reversed)) == groove,
        "track merge order does not change the render (reversed)");
  check(render(r, length, merge(tracks, {5, 1, 7, 3, 8, 2, 6, 4})) == groove,
        "track merge order does not change the render (shuffled)");

  // Per-track mute and solo remove or keep exactly that part.
  const auto mutedOpenHat = render(r, length, merge(tracks, {1, 2, 3, 4, 5, 6, 8}));
  check(mutedOpenHat != groove &&
            mutedOpenHat == render(r, length, withoutNote(single[0].events, 46)),
        "muting the Open Hat track removes only the open hat");
  const auto soloKick = render(r, length, merge(tracks, {1}));
  check(soloKick.energy() > 0.1 && soloKick == render(r, length, tracks[1].events),
        "soloing the Kick track plays only the kick");

  // Overlap: the same part on two tracks (a duplicated track) is one hit, not two.
  check(render(r, length, merge({tracks[0], tracks[1], tracks[1]}, {1, 2})) == soloKick,
        "a duplicated Kick track does not double the kick");

  // Coincident closed + open hat (quantized CH and OH lanes). Hat Choke on:
  // the closed hat wins in either track order; the open hat is cut within
  // 3 ms. Hat Choke off: both ring.
  auto hats = [&](int first, int second) {
    return render(r, 24000, {{100, MidiMessage::noteOn(r.channel, first, (juce::uint8)100)},
                             {100, MidiMessage::noteOn(r.channel, second, (juce::uint8)100)}});
  };
  const auto closedAlone =
      render(r, 24000, {{100, MidiMessage::noteOn(r.channel, 42, (juce::uint8)100)}});
  const auto openAlone =
      render(r, 24000, {{100, MidiMessage::noteOn(r.channel, 46, (juce::uint8)100)}});
  const auto hatsA = hats(42, 46), hatsB = hats(46, 42);
  check(hatsA == hatsB, "coincident hats render the same in either track order");
  check(openAlone.energy(100 + 4800, 24000) > 1e-4,
        "the open hat alone still rings 100 ms later");
  // After the 3 ms choke fade only the closed hat is left, sample for sample.
  bool onlyClosed = true;
  for (int i = 100 + 200; i < 24000; ++i)
    onlyClosed = onlyClosed && hatsA.l[i] == closedAlone.l[i] && hatsA.r[i] == closedAlone.r[i];
  check(onlyClosed && closedAlone.energy(100, 24000) > 1e-4,
        "Hat Choke on: the coincident closed hat cuts the open hat (only the closed hat remains)");
  setPlain(r, hatChoke, 0.0f);
  const auto freeA = hats(42, 46), freeB = hats(46, 42);
  check(freeA == freeB && freeA.energy(100 + 4800, 24000) > 1e-4,
        "Hat Choke off: coincident hats both ring, in either order");
  setPlain(r, hatChoke, 1.0f);

  // CC120 / CC123 on one track at the same sample as notes on others.
  for (const auto &cc : {MidiMessage::allSoundOff(r.channel), MidiMessage::allNotesOff(r.channel)}) {
    const auto before = render(r, 24000, {{0, MidiMessage::noteOn(r.channel, 49, (juce::uint8)110)},
                                          {4800, cc},
                                          {4800, MidiMessage::noteOn(r.channel, 36, (juce::uint8)110)}});
    const auto after = render(r, 24000, {{0, MidiMessage::noteOn(r.channel, 49, (juce::uint8)110)},
                                         {4800, MidiMessage::noteOn(r.channel, 36, (juce::uint8)110)},
                                         {4800, cc}});
    check(before == after && before.energy(4800, 24000) > 0.01,
          "a same-sample all-off and NoteOn render the same in either order (the new hit plays)");
  }

  // A stray MIDI track on another channel does not trigger this kit.
  auto stray = tracks[1];
  for (auto &e : stray.events)
    e.message.setChannel(r.channel == 16 ? 1 : r.channel + 1);
  check(render(r, length, merge({tracks[0], tracks[1], tracks[2], tracks[3], tracks[4],
                                 tracks[5], tracks[6], tracks[7], tracks[8], stray},
                                {1, 2, 3, 4, 5, 6, 7, 8, 9})) == groove,
        "a stray track on another MIDI channel is ignored");

  // Host MIDI plays the tracks; Internal ignores them, so nothing doubles.
  TestPlayHead headA, headB, headC;
  setPlain(r, seqMode, 1.0f); // Internal
  fresh(r);
  const auto internalOnly = play(r, headA, length);
  fresh(r);
  const auto internalWithTracks = play(r, headB, length, merge(tracks, order));
  check(internalOnly.energy() > 1.0 && internalWithTracks == internalOnly,
        "Internal mode ignores the eight tracks (no duplicate hits)");
  setPlain(r, seqMode, 0.0f); // Host MIDI
  fresh(r);
  check(play(r, headC, length, merge(tracks, order)) == groove,
        "Host MIDI with a running transport plays exactly the eight tracks");

  // Loop: Cubase sends the 4-bar clips again. The second pass is the first
  // plus only the decaying tails of the first pass's last hits.
  const int bars4 = static_cast<int>(std::lround(4 * 4 * 60.0 / 145.0 * r.rate));
  auto looped = merge(tracks, order);
  for (const auto &e : merge(tracks, order))
    looped.push_back({e.sample + bars4, e.message});
  const auto twice = render(r, 2 * bars4, looped);
  const auto once = render(r, bars4, merge(tracks, order));
  double late = 0;
  for (int i = bars4 / 2; i < bars4; ++i)
    late = std::max({late, std::abs(static_cast<double>(twice.l[bars4 + i]) - once.l[i]),
                     std::abs(static_cast<double>(twice.r[bars4 + i]) - once.r[i])});
  check(late < 1e-4, "a looped second pass matches the first (max difference " +
                         std::to_string(late) + ")");

  // Save and reopen: the same eight tracks render the same.
  juce::MemoryBlock state;
  r.p->getStateInformation(state);
  auto reopened = makeRig({}, r.slot);
  reopened.p = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                         installed(instrumentsDir));
  quietBuiltIns(*reopened.p);
  reopened.p->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  reopened.p->prepareToPlay(r.rate, r.block);
  reopened.channel = channelOf(*reopened.p, r.slot);
  check(reopened.channel == r.channel, "reopened kit keeps its MIDI channel");
  check(render(reopened, length, merge(tracks, order)) == groove,
        "reopened project plays the eight tracks bit for bit");
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "usage: fullon_drums_tests <instruments folder> <scratch dir> <midi dir>\n";
    return 2;
  }
  instrumentsDir = fs::absolute(argv[1]);
  cacheRoot = fs::absolute(argv[2]) / "cache";
  fs::remove_all(cacheRoot);
  juce::ScopedJuceInitialiser_GUI gui;
  loadsFromTheInstrumentsFolder();
  grooveAndStems();
  sampleOffsetsAndSimultaneousHits();
  chokeAndRelease();
  midiOverflowFailsClosed();
  parametersAffectOnlyTheirPad();
  instancesAndSlotsAreIndependent();
  projectsRestore();
  deterministicOfflineRender();
  moduleRejectsInvalidState();
  sequencerPlaysOnTheHostGrid();
  sequencerModesAvoidDoubleTriggers();
  sequencerFollowsLoopsSeeksAndSwing();
  factoryPatternsAndInit();
  sequencerPatternEdits();
  editWhilePlaying();
  exportMatchesPlayback();
  internalKickDucksInTheSameBlock();
  exportEndsAfterTheLastNote();
  sequencerStateReopensWithoutDuplicates();
  multiTrackHostMidi(fs::absolute(argv[3]));
  noAllocationWhilePlaying();
  std::cout << "fullon_drums_tests: " << checks << " checks passed\n";
  return 0;
}
