#pragma once

// VOX Drums engine (DRM-01). Wraps the reviewed legacy 8-pad Drums module,
// compiled in statically from a byte-identical copy of
// integration/rack-1.0 f9c8bc6 (engine/legacy8, PROVENANCE.csv). Its C
// interface is private to this wrapper: no other suite code sees it, it is
// not a plugin ABI and nothing is loaded dynamically (FOUND-00 K5 option a).
// The copy is compiled with its entry point renamed, so it never clashes
// with the test-only DRM-00 reference. DRM-03 replaces the wrapped module
// with the general 16-pad engine, with parity proven against the DRM-00
// corpus.
//
// Thread rules: construction, parameters(), prepare() and the factory
// pattern table run off the audio thread; process() is realtime safe (no
// allocation, lock or I/O; bounded loops).

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vox::drums {

inline constexpr int padCount = 8;
inline constexpr std::uint32_t maxEventsPerCall = 512;   // engine limit per process call
inline constexpr int patternBars = 4, patternSteps = 16;

struct EngineParameter {
    std::uint32_t index = 0;          // engine parameter index (stable, append-only)
    std::string legacyId;             // e.g. "kick.level"
    std::string name, unit, group;
    enum class Type { continuous, toggle, choice } type = Type::continuous;
    std::vector<std::string> choices;
    float minimum = 0.0f, maximum = 1.0f, defaultValue = 0.0f, step = 0.0f;
    bool automatable = true;
};

// One short MIDI message at a sample offset inside the block (ascending).
struct MidiEvent {
    std::uint32_t offset = 0;
    std::uint8_t size = 0;            // 1..3
    std::uint8_t bytes[3] {};
};

struct ParameterChange {
    std::uint32_t index = 0;
    float value = 0.0f;               // plain value
};

struct Transport {
    bool playing = false;
    bool hasTempo = false, hasPpq = false, hasBarStart = false, hasTimeSignature = false, hasTime = false;
    double bpm = 120.0, ppq = 0.0, barStartPpq = 0.0;
    std::uint32_t numerator = 4, denominator = 4;
    std::int64_t timeInSamples = 0;
};

struct FactoryStep {
    bool on = false, accent = false;
    std::uint8_t velocity = 0;
};

// One pad's sample as the engine reads it during process(): host-owned,
// immutable while published. id 0 = no sample.
struct SampleView {
    std::uint64_t id = 0;
    const float* const* channels = nullptr;   // channelCount pointers
    std::uint32_t channelCount = 0;
    std::uint64_t frames = 0;
    double sampleRate = 0.0;
    float peak = 0.0f;
};

// Sample buffers voices still read after the last process() call: a buffer
// may be freed only once it is absent from a complete report.
inline constexpr std::uint32_t maxSampleUsage = 32;
struct SampleUsage {
    std::array<std::uint64_t, maxSampleUsage> ids {};
    std::uint32_t count = 0;
    bool overflow = false;   // incomplete: free nothing on this report
};

class DrumEngine {
public:
    DrumEngine();
    ~DrumEngine();
    DrumEngine (const DrumEngine&) = delete;
    DrumEngine& operator= (const DrumEngine&) = delete;

    static const std::vector<EngineParameter>& parameters();
    static std::uint32_t parameterCount();
    static const std::vector<std::string>& factoryPatternNames();
    static FactoryStep factoryStep (int pattern, int lane, int bar, int step) noexcept;
    static const std::array<std::uint8_t, padCount>& padNotes();
    static const std::array<std::string, padCount>& padNames();

    bool prepare (double sampleRate, std::uint32_t maximumBlockSize);
    bool isPrepared() const noexcept { return prepared_; }
    bool setAllParameters (const float* values, std::uint32_t count);

    // Renders `frames` samples (any count; split internally). Parameter
    // changes apply at the first sample, MIDI at its offset. patternIndex
    // selects a factory pattern for the internal sequencer (-1 = none).
    // panic silences every voice before rendering. Returns false (and writes
    // silence) when the engine rejects the call.
    bool process (float* left, float* right, std::uint32_t frames,
                  const ParameterChange* changes, std::uint32_t changeCount,
                  const MidiEvent* events, std::uint32_t eventCount,
                  const Transport& transport, int patternIndex, bool panic) noexcept;

    // As above with the pads' samples (padCount entries; null = none). A
    // voice keeps reading the buffer it started with until it ends.
    bool process (float* left, float* right, std::uint32_t frames,
                  const ParameterChange* changes, std::uint32_t changeCount,
                  const MidiEvent* events, std::uint32_t eventCount,
                  const Transport& transport, int patternIndex, bool panic,
                  const SampleView* samples) noexcept;

    // The buffers voices still hold after the last process() call.
    const SampleUsage& sampleUsage() const noexcept { return usage_; }

    // Pads the internal sequencer triggered during the last process() call
    // (bit n = pad n).
    std::uint32_t sequencerHitMask() const noexcept { return hitMask_; }

    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool prepared_ = false;
    std::uint32_t hitMask_ = 0;
    SampleUsage usage_;
};

} // namespace vox::drums
