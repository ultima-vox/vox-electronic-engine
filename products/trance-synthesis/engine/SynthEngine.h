#pragma once

// SYN-01 voice engine. Two parts, one allocator, 16 sounding notes combined.
// Psy Bass and Acid are mono per part. VA uses the shared pool.
// No JUCE and no Rack / instrument ABI headers.

#include "dspcore/Primitives.h"
#include "engine/VaExtras.h"

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
    // VA extras. Defaults are the previous two-saw mix: osc3 off, one LP12,
    // no FM, no ring, no key track. Wavetable and unison are not in this batch.
    int oscWave[3] = { 1, 1, 0 };
    float oscLevel[3] = { 0.62f, 0.30f, 0.0f };
    float oscPulse[3] = { 0.5f, 0.5f, 0.5f };
    float osc3Semi = 0.0f;
    float vaSub = 0.16f;
    float ring = 0.0f;
    float fm = 0.0f;
    int filterType = 0;
    int filter2Type = 0;
    float filter2Cutoff = 1.0f;
    float filter2Resonance = 0.0f;
    float filter2Mix = 0.0f;
    int filterRoute = 0;
    float filterEnv = 0.0f;
    float keyTrack = 0.0f;
    // Filter envelope is independent of the amp envelope. Defaults match the
    // amp defaults, so an old caller that leaves them alone keeps the old motion.
    double filterAttack = 0.008;
    double filterDecay = 0.25;
    double filterSustain = 0.72;
    double filterRelease = 0.28;
    // Four global LFOs and four one-way routes. Amount 0 is off.
    // Sources: 0 off, 1-4 LFO, 5-12 macro. Dests: 0 off, 1 cutoff, 2 resonance,
    // 3 pitch, 4 FM, 5 filter 2 cutoff. Routes do not read other routes.
    // Free-running LFOs are the default. Their phase is never reset by a note.
    // Retrigger stores a separate phase on the voice that owns the note.
    float lfoRate[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    int lfoShape[4] = {};
    bool lfoSync[4] = {};
    int lfoDiv[4] = { 2, 2, 2, 2 };
    bool lfoRetrig[4] = {};
    int unison = 1;
    float unisonDetune = 0.0f;
    float unisonSpread = 0.0f;
    float wtLevel = 0.0f;
    float wtPos = 0.0f;
    static constexpr int routeSlots = 32;
    int routeSource[routeSlots] = {};
    int routeDest[routeSlots] = {};
    float routeAmount[routeSlots] = {};
};

struct BlockParams {
    PartParams part[partCount] {};
    float output = 0.8f;
    float macro[8] = {};
    double bpm = 120.0;
    bool bpmValid = false;
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
    [[nodiscard]] bool vaNoteHeld (int part, int note) const noexcept;
    [[nodiscard]] int vaFilterStage (int part, int note) const noexcept;
    [[nodiscard]] float vaAccentGain (int part, int note) const noexcept;
    [[nodiscard]] int vaActive (int part) const noexcept;
    [[nodiscard]] double lfoPhase (int part, int index) const noexcept;
    [[nodiscard]] double vaRetrigPhase (int part, int note, int index) const noexcept;
    [[nodiscard]] float modCutoff (int part) const noexcept;
    [[nodiscard]] int vaFadeLeft() const noexcept;

private:
    static constexpr std::uint16_t noKey = 0xffff;

    struct Key {
        std::uint16_t previous = noKey;
        std::uint16_t next = noKey;
        std::uint8_t velocity = 0;
        std::uint8_t stack = 0;
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

    struct LfoState {
        double phase = 0.0;
        float held = 0.0f;
        float smooth = 0.0f;
        float target = 0.0f;
        std::uint32_t noise = 1;
    };

    struct VaVoice {
        VaOsc oscA;
        VaOsc oscB;
        VaOsc oscC;
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
        int unisonIndex = 0;
        int unisonCopies = 1;
        float detuneCents = 0.0f;
        float pan = 0.0f;
        int pendingUnison = 0;
        int pendingCopies = 1;
        float pendingDetune = 0.0f;
        float pendingPan = 0.0f;
        double wtPhase = 0.0;
        double wtInc = -1.0;
        float wtPosKey = -1.0f;
        int wtFrame = 0;
        double noteKey = -1000.0;
        double noteHz = 0.0;
        double detuneKey = -1.0;
        double detuneRatio = 1.0;
        float cutoffKey = -1.0f;
        float cutoffHz = 80.0f;
        float secondKey = -1.0f;
        float secondHz = 80.0f;
        double osc3Key = -100.0;
        double osc3Ratio = 1.0;
        LfoState retrig[4] {};
        vstengine::dspcore::SvfLowpass svf;
        VaFilter flt1;
        VaFilter flt2;
        vstengine::dspcore::LinearAdsr amp;
        vstengine::dspcore::LinearAdsr filterAmp;
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
    void finishVoice (VaVoice& slot, int part, int channel, int note, float velocity, float accent,
                      int index, int copies, float cents, float pan) noexcept;
    void freeMonoRoom() noexcept;
    [[nodiscard]] int used() const noexcept;
    [[nodiscard]] VaVoice* oldestVa() noexcept;
    struct VoiceMod {
        float cutoff = 0.0f;
        float resonance = 0.0f;
        float pitch = 0.0f;
        float fm = 0.0f;
        float filter2 = 0.0f;
    };
    [[nodiscard]] float renderMono (int part, const PartParams& params) noexcept;
    [[nodiscard]] float renderVa (VaVoice& voice, const PartParams& params, const VoiceMod& mod) noexcept;
    [[nodiscard]] float acidFilter (int part, float input, float cutoffHz, float resonance) noexcept;
    void updateMod (int part, const BlockParams& params) noexcept;
    void applyRetrig (VaVoice& voice, const PartParams& params, VoiceMod& mod) noexcept;
    [[nodiscard]] float advanceLfo (LfoState& lfo, double hz, int shape) noexcept;
    [[nodiscard]] double lfoHz (const PartParams& params, int index) const noexcept;

    Part parts_[partCount];
    VaVoice voices_[maxVoices];
    PartParams live_[partCount] {};
    VoiceMod mod_[partCount] {};
    LfoState lfo_[partCount][4] {};
    float lastCutoff_[partCount] {};
    double bpmLive_ = 120.0;
    const Wavetable* table_ = nullptr;
    double sampleRate_ = 48000.0;
    int maximumBlockSize_ = 0;
    bool prepared_ = false;
    std::uint64_t age_ = 1;
};

} // namespace vox::syn::engine
