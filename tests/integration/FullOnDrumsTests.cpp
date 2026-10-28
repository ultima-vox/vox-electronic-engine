// Full-on Drums (#71 D1) through the real Rack processor: the versioned
// package is resolved from a development package root as in a DAW and the kit
// is played from slot MIDI. Not DAW acceptance (see the Cubase guide).
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
      root.empty() ? native::LoadReport{} : native::loadPackages(root));
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
void loadsFromDevelopmentRootLikeADaw() {
  setEnv("VOX_DEV_NATIVE_MODULES", "");
  setEnv("VOX_DEV_INSTRUMENT_ROOT", packageRoot.string());
  auto p = std::make_unique<VstEngineAudioProcessor>(); // the DAW's constructor
  setEnv("VOX_DEV_INSTRUMENT_ROOT", "");
  for (const auto &d : p->nativeModuleDiagnostics())
    std::cerr << "diagnostic: " << d << '\n';
  check(p->nativeModuleDiagnostics().empty(), "drum package loads without diagnostics");
  const instrument::InstrumentDescriptor *kit = nullptr;
  for (const auto *d : p->availableInstruments())
    if (d->id == drumsId)
      kit = d;
  check(kit != nullptr, "Full-on Drums offered in the slot catalog");
  check(kit->providerId == native::providerId && kit->directParameters &&
            kit->parameters.size() == 48,
        "48 typed host-exposed parameters (fits the 64-endpoint pool)");
  for (std::size_t i = 0; i < kit->parameters.size(); ++i)
    check(kit->parameters[i].poolIndex == static_cast<int>(i), "explicit stable pool index");
  check(kit->parameters[padParam(kick, level)].id == "kick.level" &&
            kit->parameters[padParam(cymbal, character)].id == "cymbal.character" &&
            kit->parameters[hatChoke].id == "kit.hat_choke" &&
            kit->parameters[hatChoke].type == instrument::ParameterType::boolean,
        "pad parameter identities");
}

void grooveAndStems() {
  auto r = makeRig(packageRoot);
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
  auto r = makeRig(packageRoot);
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
  auto r = makeRig(packageRoot);
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
  auto r = makeRig(packageRoot, 2, 48000.0, 1024);
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
  auto r = makeRig(packageRoot);
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
  auto alone = makeRig(packageRoot, 3);
  auto a = makeRig(packageRoot, 3);
  auto b = makeRig(packageRoot, 3);
  setPlain(b, padParam(kick, tune), 7.0f);
  const auto hits = groove(48000.0);
  const auto reference = renderHits(alone, 30000, hits);
  const auto ra = renderHits(a, 30000, hits);
  renderHits(b, 30000, {{0, kick}, {5, snare}});
  check(ra == reference, "two Rack instances do not share kit state");
  // Two kits in one Rack on different channels.
  auto two = makeRig(packageRoot, 3);
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
  auto first = makeRig(packageRoot, 5);
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
                                                       native::loadPackages(packageRoot));
  quietBuiltIns(*second.p);
  second.p->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  second.p->prepareToPlay(48000.0, 256);
  first.p->prepareToPlay(48000.0, 256);
  second.channel = channelOf(*second.p, 5);
  check(second.p->instrumentRack().state()[5].instrumentId == drumsId, "slot identity restored");
  check(second.channel == first.channel, "MIDI channel restored");
  for (int i = 0; i < 48; ++i)
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
                                                     native::loadPackages(packageRoot));
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
      auto r = makeRig(packageRoot, 2, rate, block);
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
  const auto selection = native::resolvePackages(packageRoot);
  check(selection.modules.size() == 1 && selection.diagnostics.empty(), "one drum package");
  std::string diagnostic;
  auto module = native::NativeModule::acquire(selection.modules.front(), diagnostic);
  check(module != nullptr, "module acquired: " + diagnostic);
  const auto &t = module->table();
  const VoxCreateParamsV2 params{sizeof(VoxCreateParamsV2), VOX_MODULE_ABI_V2,
                                 VOX_SLOT_INDEX_NONE, 0};
  VoxInstance s = nullptr;
  check(t.create(drumsId, &params, &s) == VOX_OK, "create");
  std::uint8_t state[512];
  std::uint32_t written = 0, version = 0;
  check(t.saveState(s, state, sizeof(state), &written, &version) == VOX_OK && version == 1 &&
            written == 12 + 48 * 4 + 16,
        "state v1 holds parameters, note map and choke groups");
  const std::uint32_t notesAt = 12 + 48 * 4;
  check(state[notesAt] == 36 && state[notesAt + 3] == 42 && state[notesAt + 8 + 3] == 1 &&
            state[notesAt + 8 + 4] == 1,
        "default note map and hat choke group are saved");
  auto corrupt = [&](std::uint32_t at, std::uint8_t value) {
    std::uint8_t copy[512];
    std::memcpy(copy, state, written);
    copy[at] = value;
    return t.loadState(s, 1, copy, written);
  };
  check(corrupt(notesAt + 1, 36) == VOX_E_STATE_CORRUPT, "duplicate pad note rejected");
  check(corrupt(notesAt + 2, 200) == VOX_E_STATE_CORRUPT, "invalid MIDI note rejected");
  check(corrupt(notesAt + 8, 9) == VOX_E_STATE_CORRUPT, "invalid choke group rejected");
  check(corrupt(0, 0) == VOX_E_STATE_CORRUPT, "bad magic rejected");
  check(t.loadState(s, 2, state, written) == VOX_E_STATE_VERSION, "future version rejected");
  check(t.loadState(s, 1, state, written) == VOX_OK, "valid state loads");
  float values[48];
  for (int i = 0; i < 48; ++i)
    values[i] = 0.5f;
  check(t.setAllParameters(s, values, 48) == VOX_E_INVALID_ARG,
        "out-of-range snapshot rejected as a whole");
  check(t.destroy(s) == VOX_OK, "destroy");
}

void noAllocationWhilePlaying() {
  auto r = makeRig(packageRoot, 7);
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
} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: fullon_drums_tests <drum package root> <scratch dir>\n";
    return 2;
  }
  packageRoot = fs::absolute(argv[1]);
  juce::ScopedJuceInitialiser_GUI gui;
  loadsFromDevelopmentRootLikeADaw();
  grooveAndStems();
  sampleOffsetsAndSimultaneousHits();
  chokeAndRelease();
  midiOverflowFailsClosed();
  parametersAffectOnlyTheirPad();
  instancesAndSlotsAreIndependent();
  projectsRestore();
  deterministicOfflineRender();
  moduleRejectsInvalidState();
  noAllocationWhilePlaying();
  std::cout << "fullon_drums_tests: " << checks << " checks passed\n";
  return 0;
}
