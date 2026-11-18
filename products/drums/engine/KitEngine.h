#pragma once

// VOX Drums kit engine (DRM-03): the general 16-pad drum engine in C++.
//
// It replaces the wrapped legacy 8-pad module (DrumEngine). Its synthesis is
// a line-by-line port of the reviewed legacy algorithms (VoxFullOnDrums.c @
// f9c8bc6), generalised so that any pad can play any of the 8 models and
// the kit has 16 pads, 64 voices (up to 8 per pad) and two sample players
// per pad. With the legacy kit configuration (pads 1-8 = the legacy pads,
// 16 voices, one voice per pad) it renders bit-identically to the frozen
// DRM-00 reference on the main bus and all 9 aux buses
// (suite_drums_kit_parity_tests).
//
// Threads: construction, prepare() and setMaxVoices() off the audio thread.
// process() is realtime safe: no allocation, lock or I/O; every loop is
// bounded. Settings are plain structs the caller updates between blocks.

#include "BarTiming.h"

#include <array>
#include <cstdint>
#include <memory>

namespace vox::drums::kit {

inline constexpr int padCount = 16;
inline constexpr int maxVoices = 64;
inline constexpr int maxVoicesPerPad = 8;
inline constexpr int samplesPerPad = 2;                 // players A and B
inline constexpr int sampleSlots = padCount * samplesPerPad;
// Aux buses: Send 1, Send 2, then Out 2..Out 9 (DRM-06: eight assignable
// stereo outputs next to Main; the legacy module had seven).
// Bus contract (DRM-06). Host-visible: Main + auxOutputs = 9 stereo buses
// (18 channels). Internal only: auxSends stereo pairs feeding the shared
// delay/reverb; they are never declared to the host. The engine's aux
// array holds both: pairs [0, auxSends) are the sends, pairs
// [auxSends, auxBuses) are Out 2..Out 9.
inline constexpr int auxSends = 2, auxOutputs = 8, auxBuses = auxSends + auxOutputs;
inline constexpr int hostOutputBuses = 1 + auxOutputs, hostOutputChannels = hostOutputBuses * 2;
static_assert (hostOutputBuses == 9 && hostOutputChannels == 18 && auxBuses == 10);
inline constexpr int laneCount = padCount;

enum class Model : std::uint8_t { kick, snare, clap, hat, tom, rim, cymbal, perc };
inline constexpr int modelCount = 8;
enum class Filter : std::uint8_t { off, lp12, lp24, hp12, hp24, bp };
enum class Source : std::uint8_t { synth, sample, layer };
// How the two sample players share a hit: A only; A below the velocity
// split, B from it; or A and B together.
enum class SampleMode : std::uint8_t { aOnly, velocitySwitch, mix };

struct SamplePlayer {
    float start = 0.0f, end = 1.0f;   // region, fraction of the buffer
    bool loop = false, reverse = false, normalize = false;
    float level = 1.0f;               // 0..2
};

struct PadSettings {
    Model model = Model::kick;
    std::uint8_t note = 36;
    std::uint8_t chokeGroup = 0;      // 0 = none; members fade each other
    std::uint8_t polyphony = 1;       // 1 = a retrigger fades the previous hit; up to 8
    float level = 0.8f, tune = 0.0f, fine = 0.0f, decay = 0.3f, pan = 0.0f, character = 0.5f;
    Filter filter = Filter::off;
    float cutoff = 20000.0f, resonance = 0.0f;
    float attack = 0.0f, sustain = 0.0f;   // transient shaper, -1..1
    std::uint8_t output = 0;               // 0 main, 1..8 = Out 2..Out 9
    float send1 = 0.0f, send2 = 0.0f;
    // Body (kick and the kick-like models): pitch envelope, drive, sub, tail.
    float pitchAmount = 24.0f, pitchDecay = 0.035f, drive = 1.6f, sub = 0.3f, tail = 0.25f;
    Source source = Source::synth;
    SampleMode sampleMode = SampleMode::aOnly;
    std::uint8_t velocitySplit = 64;       // velocitySwitch: B from this velocity
    std::array<SamplePlayer, samplesPerPad> players {};
    bool laneOn = true;                    // internal sequencer lane
    bool mute = false;                     // a muted pad ignores new hits; its running voices fade out in ~3 ms (DRM-06)
};

struct KitSettings {
    float volume = 0.6f;
    float velocitySensitivity = 0.8f;
    bool chokeEnabled = true;          // choke groups (legacy "Hat Choke")
    std::uint8_t sequencerMode = 0;    // 0 Host MIDI, 1 Internal, 2 Both
    float swing = 0.0f, accent = 0.5f;
    std::uint8_t voiceLimit = 64;      // voice pool: 16, 32 or 64 (the caller applies it with setMaxVoices)
    std::array<PadSettings, padCount> pads {};
};

// The legacy 8-pad kit with its defaults on pads 1-8 (pads 9-16 empty Perc
// pads on notes 50..57, level 0.6).
KitSettings legacyKit();


struct Event {
    std::uint32_t offset = 0;   // sample in the block, ascending
    std::uint8_t size = 0;
    std::uint8_t bytes[3] {};
    // >= 0: a direct trigger of this pad (editor audition) with velocity
    // bytes[2]; it bypasses the note map (shared notes) and the sequencer
    // mode. -1: a MIDI message (bytes, size).
    std::int8_t pad = -1;
};

struct Transport {
    bool playing = false, hasTempo = false, hasPpq = false;
    double bpm = 120.0, ppq = 0.0;
    // PR 137 §10.4, read from the host when it gives them: its time
    // signature, and the bar that holds `ppq` (its start and its index).
    bool hasSignature = false, hasBarStart = false, hasBarCount = false;
    int numerator = 4, denominator = 4;
    double barStartPpq = 0.0;
    std::int64_t barCount = 0;
};

// The bar lines the sequencer plays from (PR 137 §10.4): the host's when it
// gives the bar position (bar `firstIndex` starts at tick `originTick`, each
// bar `lengthTicks` long in the host's meter, or the fallback meter without
// one); otherwise bars of that meter counted from song position 0. A
// pattern bar plays from its bar line: cut at the next one when longer,
// resting until it when shorter. Playback only: patterns, songs and the
// export are never changed by it.
struct BarGrid {
    std::int64_t originTick = 0, firstIndex = 0;
    int lengthTicks = 4 * timing::ticksPerQuarter;
    bool hostBars = false;   // the bar position came from the host

    std::int64_t barAt (std::int64_t tick) const noexcept;   // the bar holding `tick`
    std::int64_t startOf (std::int64_t bar) const noexcept { return originTick + (bar - firstIndex) * lengthTicks; }
};
BarGrid barGrid (const Transport& transport, timing::TimeSignature fallback) noexcept;

struct PatternStep {
    bool on = false, accent = false;
    std::uint8_t velocity = 0;
    // DRM-04. Defaults keep the legacy behaviour.
    std::uint8_t probability = 100;   // 1..100 %: plays on this share of passes, decided by
                                      // the seed and the cell's absolute position
    std::uint8_t ratchet = 1;         // 1..8 hits within the cell, at subhitTick
    bool locked = false;              // DRM-05: the generator leaves a locked step as it is (no effect on playback)
    // PR 137 §10.3: the offset from the cell's start in ticks of the
    // 960-PPQ timeline, |microTicks| <= half the cell. A user edit in percent
    // is quantised once (percentToTicks); later operations keep the ticks.
    std::int16_t microTicks = 0;
    bool operator== (const PatternStep&) const = default;
};
static_assert (sizeof (PatternStep) == 8);

// A percent of a cell (the UI's unit, -50..50) as whole ticks: nearest, an
// exact tie to the earlier tick.
int percentToTicks (int percent, int cellTicks) noexcept;

inline constexpr int maxPatternBars = 4;
inline constexpr int maxRatchet = 8;
inline constexpr int slotStride = timing::maxSlotsPerBar;   // storage slots per bar

// One bar of a pattern or a song (PR 137 §10.2): its grid (meter and
// resolution), each lane's alternative-beat mask and its cells. Cell (lane,
// slot) is cells[lane * laneStride + slot]; the lane's mask is
// masks[lane * maskStride] (masks null: every beat straight). `cells` null
// is an empty bar.
struct BarView {
    const PatternStep* cells = nullptr;
    std::uint32_t laneStride = 0;
    const std::uint16_t* masks = nullptr;
    std::uint32_t maskStride = 0;
    timing::TimeSignature signature {};
    timing::Resolution resolution = timing::Resolution::sixteenth;
    std::uint32_t seed = 0;

    timing::BarTiming timing (int lane) const noexcept
    {
        return { signature, resolution,
                 masks != nullptr ? masks[static_cast<std::uint32_t> (lane) * maskStride] : std::uint16_t { 0 } };
    }
    const PatternStep& cell (int lane, int slot) const noexcept
    {
        return cells[static_cast<std::uint32_t> (lane) * laneStride + static_cast<std::uint32_t> (slot)];
    }
};

// A pattern: `bars` bars of one grid. Cell (lane, bar, slot) is
// cells[(lane * maxPatternBars + bar) * slotStride + slot]; the mask of
// (lane, bar) is masks[lane * maxPatternBars + bar] (null: straight).
struct Pattern {
    std::uint32_t lanes = 0, bars = 1;
    timing::TimeSignature signature {};
    timing::Resolution resolution = timing::Resolution::sixteenth;
    const PatternStep* cells = nullptr;
    const std::uint16_t* masks = nullptr;
    std::uint32_t seed = 0;     // probability decisions

    // Lanes, bars and cells present and a valid grid.
    bool playable() const noexcept;
    // Bar `index` (modulo bars). Lanes past `lanes` are not read by the rule.
    BarView bar (std::uint32_t index) const noexcept;
    int barTicks() const noexcept;
};

// One hit of the sequencer. `tick` is its exact integer position on the
// 960-PPQ timeline (bar line + cell start + microTicks; ratchet subhits by
// subhitTick); `ppq` is when it plays: tick / 960 plus the swing delay.
// `gateTicks` is the subhit's length (cell ticks / ratchet, at least 1).
struct ScheduledHit {
    double ppq = 0.0;
    std::int64_t tick = 0;
    std::int32_t gateTicks = 0;
    std::uint8_t pad = 0, velocity = 0;
};

// What, besides the pattern, decides the hits: swing (0..0.5 of a cell; it
// delays the straight cells with an odd index), accent amount and the
// enabled lanes (bit n = lane n).
struct StepContext {
    double swing = 0.0, accent = 0.5;
    std::uint32_t laneMask = 0xffffu;
};

// The canonical sequencer rule (DRM-04, PR 137 §10.5a): the hits of cell
// `cell` of beat `beat` of `lane` in `bar`, whose bar line is at tick
// `barStartTick` and whose absolute bar index is `absoluteBar` (probability:
// the bar's seed, absoluteBar * 64 + slot and the lane). Playback and MIDI
// export both use it, so what is exported is what plays. Writes at most
// maxRatchet hits, in time order; returns their count.
int cellHits (const BarView& bar, const StepContext& context, std::int64_t absoluteBar, std::int64_t barStartTick,
              int lane, int beat, int cell, ScheduledHit* out) noexcept;

// DRM-05 Song mode: a timeline of bars; each bar has its own grid, seed and
// cells (a BarView into a pattern slot or into its own storage).
using SongBarView = BarView;

struct Song {
    std::uint32_t bars = 0;               // 16, 32 or 64 (any 1..64 is played)
    const SongBarView* views = nullptr;   // `bars` entries
    // Loop region [loopStart, loopStart + loopLength) in bars: the host's
    // absolute bar B plays song bar B while B < loopStart (the bars before
    // the region play once), then loopStart + (B - loopStart) mod loopLength:
    // the region is entered at its first bar and repeats in order. 0 length = the
    // whole song. Playback maps the host's absolute bar into the region.
    std::uint32_t loopStart = 0, loopLength = 0;
};

inline constexpr int maxSongBars = 64;

// The song bar that plays at the host's absolute bar (from ppq 0).
std::uint32_t songBarAt (const Song& song, std::int64_t absoluteBar) noexcept;

// Factory patterns (the legacy module's buildPattern, 145 BPM full-on idiom):
// 8 lanes x 16 steps x 4 bars in the legacy layout, cell
// (lane * 4 + bar) * 16 + step.
inline constexpr int factoryPatternCount = 5;
const char* factoryPatternName (int index) noexcept;
void factoryPattern (int index, std::array<PatternStep, 8 * 4 * 16>& cells) noexcept;
// A legacy-layout grid (lanes x bars x 16 steps, 4/4, cell
// (lane * bars + bar) * 16 + step) as a Pattern's storage: each step lands
// on the straight 16th cell of its beat. Returns the view (no masks).
using PatternCells = std::array<PatternStep, padCount * maxPatternBars * slotStride>;
Pattern fromLegacyGrid (const PatternStep* legacy, std::uint32_t lanes, std::uint32_t bars, PatternCells& out) noexcept;

struct Sample {
    std::uint64_t id = 0;
    const float* const* channels = nullptr;
    std::uint32_t channelCount = 0;
    std::uint64_t frames = 0;
    double sampleRate = 0.0;
    float peak = 0.0f;
};

struct Usage {
    std::array<std::uint64_t, maxVoices * samplesPerPad> ids {};
    std::uint32_t count = 0;
    bool overflow = false;   // cannot happen (ids holds every player); kept for the report contract
};

struct Outputs {
    float* left = nullptr;
    float* right = nullptr;
    // Optional: auxBuses stereo pairs (Send 1, Send 2, Out 2..Out 9). When
    // null, every pad plays dry on main and the sends are silent.
    std::array<float*, auxBuses * 2>* aux = nullptr;
};

class KitEngine {
public:
    KitEngine();
    ~KitEngine();
    KitEngine (const KitEngine&) = delete;
    KitEngine& operator= (const KitEngine&) = delete;

    void prepare (double sampleRate);
    void setMaxVoices (int voices);    // 1..64; the legacy kit used 16
    void reset() noexcept;              // silence, sequencer position forgotten

    // Renders `frames` samples. `samples` has sampleSlots entries (pad * 2
    // + player), or null for none; `pattern` may be null (no sequence);
    // patternGeneration changes whenever its content does.
    enum class Release : std::uint8_t { none, fadeAtEnd, stopAtEnd };
    void process (const KitSettings& settings, const Outputs& out, std::uint32_t frames,
                  const Event* events, std::uint32_t eventCount, const Transport& transport,
                  const Pattern* pattern, const Sample* samples, bool panic,
                  Release release = Release::none, const Song* song = nullptr) noexcept;
    // `song` non-null plays the song instead of `pattern` (Song mode).

    // Post-fader peak of each pad in the last process() (mixer meters), and
    // whether it saw All Sound Off (CC120) or a panic (the caller clears its
    // FX tails then).
    float padPeak (int pad) const noexcept { return padPeak_[static_cast<std::size_t> (pad & 15)]; }
    bool soundOffSeen() const noexcept { return soundOff_; }
    // The frame of the last All Sound Off (or 0 for a panic) in the last
    // process(), -1 if none: sends before it belong to the cleared tails.
    int soundOffFrame() const noexcept { return soundOffFrame_; }

    // Pads triggered by the sequencer in the last process() (bit n = pad n).
    std::uint32_t sequencerHits() const noexcept { return sequencerHits_; }
    // The sequencer hits of the last process() call, by frame offset (tests,
    // export parity): count and hit i. A block over 8192 frames reports its
    // last 8192-frame chunk only.
    int scheduledCount() const noexcept;
    void scheduled (int i, std::uint32_t& offset, std::uint8_t& pad, std::uint8_t& velocity) const noexcept;
    // Buffers voices still play after the last process().
    const Usage& usage() const noexcept { return usage_; }
    int activeVoices() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint32_t sequencerHits_ = 0;
    std::array<float, padCount> padPeak_ {};
    bool soundOff_ = false;
    int soundOffFrame_ = -1;
    Usage usage_;
};

} // namespace vox::drums::kit
