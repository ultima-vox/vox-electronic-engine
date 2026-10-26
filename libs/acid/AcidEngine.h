#pragma once

#include "instrument/InstrumentAbi.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace vstengine::acid {

class AcidEngine final {
public:
    static constexpr std::uint32_t stateVersion = 6;
    static constexpr std::uint32_t env2StateVersion = 5;
    static constexpr std::size_t env2ParameterCount = 46;
    static constexpr std::uint32_t lfo1StateVersion = 4;
    static constexpr std::size_t lfo1ParameterCount = 40;
    static constexpr std::uint32_t previousStateVersion = 3;
    static constexpr std::size_t previousParameterCount = 36;
    static constexpr std::uint32_t legacyStateVersion = 1;
    static constexpr std::size_t legacyParameterCount = 9;
    static constexpr std::size_t parameterCount = 54;
    static constexpr std::size_t encodedStateBytes = 8 + parameterCount * sizeof(float);
    static constexpr std::size_t maxUnisonVoices = 4;

    enum Parameter : std::size_t {
        // Core 9 parameters (indices 0..8 kept identical for ABI & test compatibility)
        waveform = 0,       // Oscillator Wave Morph / Type
        cutoff,             // Filter Cutoff (20 Hz .. 18 kHz)
        resonance,          // Filter Resonance (0.00 .. 1.00)
        envelopeAmount,     // Filter Env Amt (0.00 .. 1.00)
        decay,              // Filter Decay (0.01 s .. 2.50 s)
        accent,             // Accent Amount (0.00 .. 1.00)
        slide,              // Slide Time (0.00 .. 1.00)
        drive,              // Distortion Drive (0.00 .. 1.00)
        output,             // Output Volume (0.00 .. 1.00)

        // Oscillator Extended Controls
        oscTune,            // Semitone tune (-24 .. +24 st, 0.5 = 0.00)
        oscFine,            // Fine tune (-100 .. +100 ct, 0.5 = 0.00)
        oscPulseWidth,      // Pulse Width PW (0.05 .. 0.95)

        // Filter Extended Controls
        filterMode,         // 0=Low Pass 24 dB, 1=Diode 18 dB (303), 2=Band Pass 12 dB, 3=High Pass 12 dB
        keyTracking,        // Key Track (0.00 .. 1.00)

        // Envelope Section: AMP ADSR & FILTER ADSR
        ampAttack,
        ampDecay,
        ampSustain,
        ampRelease,
        fltAttack,
        fltSustain,
        fltRelease,

        // Distortion / Drive Extended Controls
        distMode,           // 0=Analog Tube, 1=Soft Clip, 2=Hard Clip, 3=Wavefold
        distTone,           // Post-distortion Tone filter (0.00 .. 1.00, 0.40 on mockup)
        distMix,            // Dry/Wet Mix (0.00 .. 1.00, 0.70 on mockup)

        // Output Pan
        pan,                // Stereo Pan (0.5 = C)

        // Modulation Section (ENV 1/2, LFO 1..3, STEP)
        modTarget,          // 0=Filter Cutoff, 1=Pitch, 2=Pulse Width, 3=Resonance, 4=Drive, 5=Pan
        modAmount,
        modAttack,
        modDecay,
        modSustain,
        modRelease,

        // Performance Section (Unison & Portamento)
        portamento,         // Additional performance portamento time
        unisonVoices,       // Normalized 1..4 unison voices
        unisonDetune,       // Unison detune amount (0.10 on mockup)
        stereoSpread,       // Stereo unison spread (0.40 on mockup)

        // Play Mode Flags (packed or individual normalized toggle: legato, retrigger, vel->accent, slide_on_overlap)
        playModeFlags,
        // Independent LFO 1 — additive IDs, never renumber the original 36.
        lfo1Rate,
        lfo1Depth,
        lfo1Wave,
        lfo1Target,

        // ENV2: fully independent destination, amount and ADSR stages.
        env2Target,
        env2Amount,
        env2Attack,
        env2Decay,
        env2Sustain,
        env2Release,

        // LFO2 & LFO3 append independently addressable control groups.
        lfo2Rate,
        lfo2Depth,
        lfo2Wave,
        lfo2Target,
        lfo3Rate,
        lfo3Depth,
        lfo3Wave,
        lfo3Target,

        count
    };

    enum class FilterMode : std::uint8_t {
        lowPass24dB = 0,
        diode18dB = 1,
        bandPass12dB = 2,
        highPass12dB = 3
    };

    enum class DistortionMode : std::uint8_t {
        analogTube = 0,
        softClip = 1,
        hardClip = 2,
        wavefold = 3
    };

    enum class ModTarget : std::uint8_t {
        filterCutoff = 0,
        pitch = 1,
        pulseWidth = 2,
        resonance = 3,
        drive = 4,
        pan = 5
    };

    static constexpr std::array<std::string_view, parameterCount> parameterIds {
        "waveform",
        "cutoff",
        "resonance",
        "envelope",
        "decay",
        "accent",
        "slide",
        "drive",
        "output",
        "osc_tune",
        "osc_fine",
        "osc_pw",
        "filter_mode",
        "key_tracking",
        "amp_attack",
        "amp_decay",
        "amp_sustain",
        "amp_release",
        "flt_attack",
        "flt_sustain",
        "flt_release",
        "dist_mode",
        "dist_tone",
        "dist_mix",
        "pan",
        "mod_target",
        "mod_amount",
        "mod_attack",
        "mod_decay",
        "mod_sustain",
        "mod_release",
        "portamento",
        "unison_voices",
        "unison_detune",
        "stereo_spread",
        "play_mode_flags",
        "lfo1_rate",
        "lfo1_depth",
        "lfo1_wave",
        "lfo1_target",
        "env2_target",
        "env2_amount",
        "env2_attack",
        "env2_decay",
        "env2_sustain",
        "env2_release",
        "lfo2_rate",
        "lfo2_depth",
        "lfo2_wave",
        "lfo2_target",
        "lfo3_rate",
        "lfo3_depth",
        "lfo3_wave",
        "lfo3_target"
    };

    bool prepare(double sampleRate, std::uint32_t maximumBlockSize,
                 std::uint32_t outputChannels) noexcept;
    void reset() noexcept;
    void process(std::span<float*> outputs, std::uint32_t sampleCount,
                 std::span<const VoxMidiEventV1> midi) noexcept;

    bool setParameter(std::string_view id, float normalizedValue) noexcept;
    [[nodiscard]] float parameter(std::string_view id) const noexcept;

    void setPlayModeOptions(bool legato, bool retrigger,
                            bool velToAccent, bool slideOnOverlap) noexcept;
    [[nodiscard]] bool isLegatoEnabled() const noexcept { return legatoEnabled_; }
    [[nodiscard]] bool isRetriggerEnabled() const noexcept { return retriggerEnabled_; }
    [[nodiscard]] bool isVelToAccentEnabled() const noexcept { return velToAccentEnabled_; }
    [[nodiscard]] bool isSlideOnOverlapEnabled() const noexcept { return slideOnOverlapEnabled_; }

    bool loadState(std::uint32_t schemaVersion,
                   std::span<const std::byte> payload) noexcept;
    bool saveState(std::span<std::byte> destination,
                   std::uint32_t& bytesWritten) const noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] bool isActive() const noexcept { return active_; }
    [[nodiscard]] std::uint32_t latencySamples() const noexcept { return 0; }
    [[nodiscard]] std::uint32_t tailSamples() const noexcept;

    [[nodiscard]] static float cutoffNormalizedToHz(float norm) noexcept;
    [[nodiscard]] static float cutoffHzToNormalized(float hz) noexcept;

private:
    enum class EnvStage : std::uint8_t {
        idle = 0,
        attack,
        decay,
        sustain,
        release
    };

    struct AdsrState {
        EnvStage stage { EnvStage::idle };
        float value {};

        void reset() noexcept {
            stage = EnvStage::idle;
            value = 0.0f;
        }

        void trigger(bool retriggerFromZero) noexcept {
            if (retriggerFromZero) {
                value = 0.0f;
            }
            stage = EnvStage::attack;
        }

        void release() noexcept {
            if (stage != EnvStage::idle) {
                stage = EnvStage::release;
            }
        }

        float advance(float attackSec, float decaySec, float sustainLevel,
                      float releaseSec, bool gateOpen, double sampleRate) noexcept;
    };

    void handleMidiEvent(const VoxMidiEventV1& event) noexcept;
    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;
    void allNotesOff() noexcept;
    void pitchBend(std::uint8_t lsb, std::uint8_t msb) noexcept;

    void renderStereoSample(float& outLeft, float& outRight) noexcept;
    [[nodiscard]] float renderVoiceOscillator(std::size_t voiceIdx,
                                              double baseIncrement,
                                              float waveMorph,
                                              float pulseWidth) noexcept;
    [[nodiscard]] float processFilter(float input, float cutoffHz,
                                      float resonanceAmount,
                                      FilterMode mode) noexcept;
    [[nodiscard]] float processDistortion(float drySignal, float driveNorm,
                                          float toneNorm, float mixNorm,
                                          DistortionMode mode) noexcept;

    [[nodiscard]] static double midiFrequency(float note) noexcept;
    [[nodiscard]] static float polyBlep(double phase, double increment) noexcept;
    [[nodiscard]] static FilterMode decodeFilterMode(float normalized) noexcept;
    [[nodiscard]] static DistortionMode decodeDistortionMode(float normalized) noexcept;
    [[nodiscard]] static ModTarget decodeModTarget(float normalized) noexcept;

    // Default normalized values matching the V2 Acid UI mockup:
    // Cutoff 250 Hz (~0.37), Res 0.75, Env Amt 0.60, Key Track 0.20,
    // AMP ADSR: A 0.00s, D 0.10s, S 0.80, R 0.25s
    // Distortion: Analog Tube, Drive 0.65, Tone 0.40, Mix 0.70
    // Accent: 0.58, Slide Time: 0.30, Output: 0.75 (-3.0 dB), Pan: 0.50 (C)
    // Mod Env1: Filter Cutoff, Amount 0.70, A 0.00s, D 0.35s, S 0.80, R 0.45s
    // Performance: Portamento 0.00s, Unison 1, Detune 0.10, Spread 0.40
    std::array<float, parameterCount> targets_ {
        0.50f, // 0: waveform (Saw)
        0.37f, // 1: cutoff (250 Hz)
        0.75f, // 2: resonance (0.75)
        0.60f, // 3: envelope (Env Amt 0.60)
        0.35f, // 4: decay (Filter Decay)
        0.58f, // 5: accent
        0.30f, // 6: slide (Time 0.30)
        0.65f, // 7: drive (Drive 0.65)
        0.75f, // 8: output (-3.0 dB)
        0.50f, // 9: osc_tune (0.00)
        0.50f, // 10: osc_fine (0.00)
        0.50f, // 11: osc_pw (0)
        0.00f, // 12: filter_mode (Low Pass 24 dB)
        0.20f, // 13: key_tracking (0.20)
        0.00f, // 14: amp_attack (0.00 s)
        0.22f, // 15: amp_decay (0.10 s)
        0.80f, // 16: amp_sustain (0.80)
        0.35f, // 17: amp_release (0.25 s)
        0.00f, // 18: flt_attack (0.00 s)
        0.00f, // 19: flt_sustain (0.00)
        0.25f, // 20: flt_release (0.15 s)
        0.00f, // 21: dist_mode (Analog Tube)
        0.40f, // 22: dist_tone (0.40)
        0.70f, // 23: dist_mix (0.70)
        0.50f, // 24: pan (C)
        0.00f, // 25: mod_target (Filter Cutoff)
        0.70f, // 26: mod_amount (0.70)
        0.00f, // 27: mod_attack (0.00 s)
        0.38f, // 28: mod_decay (0.35 s)
        0.80f, // 29: mod_sustain (0.80)
        0.42f, // 30: mod_release (0.45 s)
        0.00f, // 31: portamento (0.00 s)
        0.00f, // 32: unison_voices (1 voice)
        0.10f, // 33: unison_detune (0.10)
        0.40f, // 34: stereo_spread (0.40)
        (13.0f / 15.0f), // 35: play_mode_flags: Legato, Vel->Accent, Slide on Overlap
        0.35f, // 36: LFO1 Rate, 0.05 .. 20 Hz exponential
        0.00f, // 37: LFO1 Depth; silence-compatible default
        0.00f, // 38: LFO1 Wave: sine / triangle / square / saw
        0.00f, // 39: LFO1 Target: cutoff/pitch/PW/resonance/drive/pan
        0.00f, // 40: ENV2 target: cutoff
        0.00f, // 41: ENV2 depth: off for backward-compatible factory sound
        0.00f, // 42: ENV2 attack
        0.35f, // 43: ENV2 decay
        0.80f, // 44: ENV2 sustain
        0.40f, // 45: ENV2 release
        0.35f, // 46: LFO2 rate
        0.00f, // 47: LFO2 depth off by default
        0.00f, // 48: LFO2 waveform sine
        0.00f, // 49: LFO2 cutoff target
        0.35f, // 50: LFO3 rate
        0.00f, // 51: LFO3 depth off by default
        0.00f, // 52: LFO3 waveform sine
        0.00f  // 53: LFO3 cutoff target
    };
    std::array<float, parameterCount> smoothed_ { targets_ };

    double sampleRate_ { 48000.0 };
    double lfo1Phase_ {};
    double lfo2Phase_ {};
    double lfo3Phase_ {};
    // Hz mapping is computed once per audio block (3 exp/pow operations),
    // increments are smoothed every sample to avoid rate automation zipper.
    std::array<double, 3> lfoPhaseIncrement_ {};
    std::array<double, 3> lfoTargetIncrement_ {};
    double lfoIncrementSmoothing_ {};
    std::uint32_t maximumBlockSize_ {};
    std::uint32_t outputChannels_ {};

    std::array<double, maxUnisonVoices> phases_ {};
    std::array<double, maxUnisonVoices> subPhases_ {};
    std::array<double, maxUnisonVoices> triIntegrators_ {};

    double currentFrequency_ { 130.81 };
    double targetFrequency_ { 130.81 };
    float pitchBendSemitones_ {};

    // Envelopes: VCA, Filter (303 MEG + ADSR), and Mod Env 1
    AdsrState ampEnv_ {};
    AdsrState fltEnv_ {};
    AdsrState modEnv_ {};
    AdsrState env2State_ {};

    float accentCapacitor_ {};
    float accentGain_ {};
    float velocityGain_ { 1.0f };
    bool isAccentedNote_ {};

    // TPT/ZDF 4-pole ladder filter state + Post-Distortion Tone LP state
    std::array<float, 4> filterState_ {};
    float postToneState_ {};

    // Monophonic held-note stack for seamless legato & slide
    static constexpr std::size_t maxHeldNotes = 8;
    std::array<std::uint8_t, maxHeldNotes> heldNotes_ {};
    std::size_t heldCount_ {};

    std::uint8_t currentNote_ { 255 };
    bool gate_ {};
    enum class GlideSource : std::uint8_t { none, slide, portamento };
    GlideSource glideSource_ { GlideSource::none };
    bool slideActive_ {};
    bool ccPortamento_ {};
    bool legatoEnabled_ { true };
    bool retriggerEnabled_ { false };
    bool velToAccentEnabled_ { true };
    bool slideOnOverlapEnabled_ { true };
    bool active_ {};
    bool prepared_ {};
};

} // namespace vstengine::acid