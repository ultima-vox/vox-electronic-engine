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

#include <array>
#include <cstdint>
#include <memory>

namespace vox::drums::kit {

inline constexpr int padCount = 16;
inline constexpr int maxVoices = 64;
inline constexpr int maxVoicesPerPad = 8;
inline constexpr int samplesPerPad = 2;                 // players A and B
inline constexpr int sampleSlots = padCount * samplesPerPad;
inline constexpr int auxSends = 2, auxOutputs = 7, auxBuses = auxSends + auxOutputs;
inline constexpr int laneCount = padCount;
inline constexpr int maxStepsPerBar = 16, maxBars = 4;
inline constexpr int maxPatternCells = laneCount * maxBars * maxStepsPerBar;

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
    std::uint8_t output = 0;               // 0 main, 1..7 = Out 2..Out 8
    float send1 = 0.0f, send2 = 0.0f;
    // Body (kick and the kick-like models): pitch envelope, drive, sub, tail.
    float pitchAmount = 24.0f, pitchDecay = 0.035f, drive = 1.6f, sub = 0.3f, tail = 0.25f;
    Source source = Source::synth;
    SampleMode sampleMode = SampleMode::aOnly;
    std::uint8_t velocitySplit = 64;       // velocitySwitch: B from this velocity
    std::array<SamplePlayer, samplesPerPad> players {};
    bool laneOn = true;                    // internal sequencer lane
    bool mute = false;                     // a muted pad ignores new hits (running ones ring out)
};

struct KitSettings {
    float volume = 0.6f;
    float velocitySensitivity = 0.8f;
    bool chokeEnabled = true;          // choke groups (legacy "Hat Choke")
    std::uint8_t sequencerMode = 0;    // 0 Host MIDI, 1 Internal, 2 Both
    float swing = 0.0f, accent = 0.5f;
    std::array<PadSettings, padCount> pads {};
};

// The legacy 8-pad kit with its defaults on pads 1-8 (pads 9-16 empty Perc
// pads on notes 50..57, level 0.6).
KitSettings legacyKit();


struct Event {
    std::uint32_t offset = 0;   // sample in the block, ascending
    std::uint8_t size = 0;
    std::uint8_t bytes[3] {};
};

struct Transport {
    bool playing = false, hasTempo = false, hasPpq = false;
    double bpm = 120.0, ppq = 0.0;
};

struct PatternStep {
    bool on = false, accent = false;
    std::uint8_t velocity = 0;
};

// Lanes x bars x steps, cell (lane * bars + bar) * steps + step.
struct Pattern {
    std::uint32_t lanes = 0, steps = 16, bars = 1;
    std::uint32_t timing = 0;   // 0 1/16, 1 1/8, 2 1/32, 3 1/8 triplets
    const PatternStep* cells = nullptr;
};

// Factory patterns (the legacy module's buildPattern, 145 BPM full-on idiom):
// 8 lanes x 16 steps x 4 bars, cell (lane * 4 + bar) * 16 + step.
inline constexpr int factoryPatternCount = 5;
const char* factoryPatternName (int index) noexcept;
// Fills cells (8 * 4 * 16) for pattern `index`; returns the pattern view.
Pattern factoryPattern (int index, std::array<PatternStep, 8 * 4 * 16>& cells) noexcept;

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
    // Optional: auxBuses stereo pairs (Send 1, Send 2, Out 2..Out 8). When
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
                  Release release = Release::none) noexcept;

    // Pads triggered by the sequencer in the last process() (bit n = pad n).
    std::uint32_t sequencerHits() const noexcept { return sequencerHits_; }
    // Buffers voices still play after the last process().
    const Usage& usage() const noexcept { return usage_; }
    int activeVoices() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint32_t sequencerHits_ = 0;
    Usage usage_;
};

} // namespace vox::drums::kit
