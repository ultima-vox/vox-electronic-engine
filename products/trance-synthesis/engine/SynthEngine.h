#pragma once

// SYN-01 voice engine. Two parts, one allocator, 16 sounding notes combined.
// Psy Bass and Acid are mono per part. VA uses the shared pool.
// No JUCE and no Rack / instrument ABI headers.

#include "dspcore/Primitives.h"

#include <cstddef>
#include <cstdint>

namespace vox::syn::engine {

inline constexpr int partCount = 2;
inline constexpr int maxVoices = 16;
inline constexpr int maxEvents = 256;
inline constexpr int maxKeys = 16 * 128;

// Mono amplitude is a 60 ms one-pole and is declared idle below 1e-4,
// so the voice tail is -ln(1e-4) * 0.06 s. VA note-off release is 0.28 s,
// shorter than this bound. Enabled FX add EffectsChain::tailSamples().
inline double voiceTailSeconds() noexcept
{
    return -std::log (1.0e-4) * 0.06;
}

enum class EngineKind : std::uint8_t { psyBass = 0, acid = 1, va = 2 };

struct Event {
    std::uint32_t offset = 0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
    // Bit 0 accent, bit 1 slide-into-this-note. Host MIDI leaves this 0.
    std::uint8_t articulation = 0;
};

inline constexpr std::uint8_t articulateAccent = 1u;
inline constexpr std::uint8_t articulateSlide = 2u;

struct NoteArticulation {
    bool forceSlide = false;
    bool forceAccent = false;
};

struct PartParams {
    EngineKind engine = EngineKind::psyBass;
    float level = 0.75f;
    float cutoff = 0.55f;
    float resonance = 0.35f;
    float slide = 0.30f;
    float portamento = 0.0f;
    float drive = 0.25f;
    // Defaults reproduce the SYN-01 render. Omitted brace fields keep them.
    float sub = 0.18f;
    double pitchSeconds = 0.018;
    float square = 0.30f;
    float env = 1.0f;
    double detuneCents = 8.0;
    double attack = 0.008;
    double decay = 0.25;
    double sustain = 0.72;
    double release = 0.28;
    float noise = 0.04f;
};

struct BlockParams {
    PartParams part[partCount] {};
    float output = 0.8f;
};

class SynthEngine {
public:
    void prepare (double sampleRate, int maximumBlockSize) noexcept;
    void reset() noexcept;

    // Realtime: no allocation. Overwrites `left`/`right`.
    // Events are in non-decreasing offset order, each offset < frames.
    void process (float* left, float* right, int frames,
                  const Event* events, int eventCount,
                  const BlockParams& params) noexcept;

    [[nodiscard]] int activeVoices() const noexcept;
    [[nodiscard]] double monoPhase (int part) const noexcept;
    [[nodiscard]] double monoOscPhase (int part, int osc) const noexcept;
    [[nodiscard]] double monoFrequency (int part) const noexcept;
    [[nodiscard]] int monoNote (int part) const noexcept;
    [[nodiscard]] bool monoGate (int part) const noexcept;
    [[nodiscard]] float monoAccent (int part) const noexcept;
    [[nodiscard]] int glideSource (int part) const noexcept;
    [[nodiscard]] int vaFadeLeft() const noexcept;

private:
    static constexpr std::uint16_t noKey = 0xffff;

    struct Key {
        std::uint16_t previous = noKey;
        std::uint16_t next = noKey;
        std::uint8_t velocity = 0;
        bool alive = false;
        bool down = false;
    };

    struct Mono {
        double frequency = 110.0;
        double targetFrequency = 110.0;
        float amp = 0.0f;
        float filterEnv = 0.0f;
        float accent = 0.0f;
        float velocity = 0.0f;
        float velocityTarget = 0.0f;
        float accentTarget = 0.0f;
        float slideFloor = 0.0f;
        int note = -1;
        int pitchLeft = 0;
        int pitchTotal = 1;
        bool gate = false;
        bool active = false;
        bool glide = false;
        int glideSource = 0;   // 0 snap, 1 slide, 2 portamento
        vstengine::dspcore::BlepOscillator osc;
        vstengine::dspcore::BlepOscillator square;
        vstengine::dspcore::BlepOscillator sub;
        vstengine::dspcore::SvfLowpass svf;
        float ladder[4] {};
    };

    struct VaVoice {
        vstengine::dspcore::BlepOscillator oscA;
        vstengine::dspcore::BlepOscillator oscB;
        vstengine::dspcore::BlepOscillator sub;
        float velocity = 0.0f;
        float accentGain = 1.0f;
        float fade = 1.0f;
        int fadeLeft = 0;
        int fadeTotal = 1;
        int note = -1;
        int pendingNote = -1;
        int pendingPart = 0;
        int channel = 0;
        int part = 0;
        std::uint32_t noise = 1;
        std::uint64_t age = 0;
        bool active = false;
        bool gate = false;
        bool dying = false;
        float pendingVelocity = 0.0f;
        float pendingAccent = 1.0f;
        vstengine::dspcore::SvfLowpass svf;
        vstengine::dspcore::LinearAdsr amp;
    };

    struct Part {
        EngineKind engine = EngineKind::psyBass;
        Mono mono;
        Key keys[maxKeys] {};
        bool sustain[16] {};
        std::uint16_t latest = noKey;
        float bendSemis = 0.0f;
    };

    void latchEngine (int part, EngineKind next) noexcept;
    void silencePart (int part) noexcept;
    void handle (int part, std::uint8_t status, std::uint8_t d1, std::uint8_t d2, std::uint8_t articulation) noexcept;
    void noteOn (int part, int channel, int note, int velocity, NoteArticulation art) noexcept;
    void noteOff (int part, int channel, int note) noexcept;
    void releaseVa (int part, int channel, int note) noexcept;
    void pedal (int part, int channel, bool down) noexcept;
    void allOff (int part, bool hard) noexcept;
    void pushKey (Part& part, int key, int velocity) noexcept;
    void removeKey (Part& part, int key) noexcept;
    void restoreLatest (int part, bool fromSlide) noexcept;
    void startMono (int part, int note, float velocity, bool legato, NoteArticulation art) noexcept;
    void startVa (int part, int channel, int note, float velocity, NoteArticulation art) noexcept;
    void freeMonoRoom() noexcept;
    [[nodiscard]] int used() const noexcept;
    [[nodiscard]] VaVoice* oldestVa() noexcept;
    [[nodiscard]] float renderMono (int part, const PartParams& params) noexcept;
    [[nodiscard]] float renderVa (VaVoice& voice, const PartParams& params) noexcept;
    [[nodiscard]] float acidFilter (int part, float input, float cutoffHz, float resonance) noexcept;

    Part parts_[partCount];
    VaVoice voices_[maxVoices];
    PartParams live_[partCount] {};
    double sampleRate_ = 48000.0;
    int maximumBlockSize_ = 0;
    bool prepared_ = false;
    std::uint64_t age_ = 1;
};

} // namespace vox::syn::engine
