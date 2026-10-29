#pragma once

// Canonical Acid 303 control map for the native module.
// Continuous values use the same curves as AcidEngine; this file does not
// change DSP. Choice indices are converted to the normalized bins the engine
// already decodes. Play-mode booleans are the four flags inside play_mode_flags.

#include "abi/VoxModuleAbiV2.h"
#include "acid/AcidEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vstengine::acid::module {

inline constexpr std::uint32_t parameterCount = 80;
inline constexpr std::uint32_t stateVersion = AcidEngine::stateVersion;
inline constexpr const char* instrumentId = "com.ultimavox.acid";
inline constexpr const char* instrumentName = "Acid 303";

enum class Domain : std::uint8_t {
    unit = 0,
    cutoffHz,
    semitone,
    cent,
    pan,
    slideSec,
    quadSec,
    lfoHz,
    bipolar,
    choiceFloor4,
    choiceRound4,
    choiceFloor6,
    unison,
    booleanNorm,
    playFlag
};

struct Control {
    const char* id;
    const char* name;
    const char* unit;
    const char* group;
    Domain domain;
    float curveA;
    float curveB;
    const char* const* choices;
    std::uint32_t choiceCount;
    int flagIndex;
};

inline const char* const kFilterModes[] = {
    "LP 24 dB", "Diode 18 dB", "BP 12 dB", "HP 12 dB"};
inline const char* const kDistModes[] = {
    "Tube", "Soft", "Hard", "Fold"};
inline const char* const kLfoWaves[] = {
    "Sine", "Triangle", "Square", "Saw"};
inline const char* const kStepRates[] = {
    "1/8", "1/16", "1/32", "Triplet"};
inline const char* const kTargets[] = {
    "Cutoff", "Pitch", "PW", "Resonance", "Drive", "Pan"};

inline const std::array<Control, parameterCount>& controls() noexcept {
    static const std::array<Control, parameterCount> table{{
        {"waveform", "Waveform", "", "Oscillator", Domain::unit, 0, 0, nullptr, 0, -1},
        {"cutoff", "Cutoff", "Hz", "Filter", Domain::cutoffHz, 0, 0, nullptr, 0, -1},
        {"resonance", "Resonance", "", "Filter", Domain::unit, 0, 0, nullptr, 0, -1},
        {"envelope", "Env Amount", "", "Filter", Domain::unit, 0, 0, nullptr, 0, -1},
        {"decay", "Filter Decay", "s", "Filter", Domain::quadSec, 0.03f, 2.2f, nullptr, 0, -1},
        {"accent", "Accent", "", "303", Domain::unit, 0, 0, nullptr, 0, -1},
        {"slide", "Slide", "s", "303", Domain::slideSec, 0, 0, nullptr, 0, -1},
        {"drive", "Drive", "", "Distortion", Domain::unit, 0, 0, nullptr, 0, -1},
        {"output", "Output", "", "Output", Domain::unit, 0, 0, nullptr, 0, -1},
        {"osc_tune", "Tune", "st", "Oscillator", Domain::semitone, 0, 0, nullptr, 0, -1},
        {"osc_fine", "Fine", "cent", "Oscillator", Domain::cent, 0, 0, nullptr, 0, -1},
        {"osc_pw", "Pulse Width", "", "Oscillator", Domain::unit, 0, 0, nullptr, 0, -1},
        {"filter_mode", "Filter Mode", "", "Filter", Domain::choiceFloor4, 0, 0, kFilterModes, 4, -1},
        {"key_tracking", "Key Track", "", "Filter", Domain::unit, 0, 0, nullptr, 0, -1},
        {"amp_attack", "Amp Attack", "s", "Amp", Domain::quadSec, 0.0005f, 0.8f, nullptr, 0, -1},
        {"amp_decay", "Amp Decay", "s", "Amp", Domain::quadSec, 0.015f, 2.2f, nullptr, 0, -1},
        {"amp_sustain", "Amp Sustain", "", "Amp", Domain::unit, 0, 0, nullptr, 0, -1},
        {"amp_release", "Amp Release", "s", "Amp", Domain::quadSec, 0.004f, 1.8f, nullptr, 0, -1},
        {"flt_attack", "Filter Attack", "s", "Filter", Domain::quadSec, 0.0005f, 0.8f, nullptr, 0, -1},
        {"flt_sustain", "Filter Sustain", "", "Filter", Domain::unit, 0, 0, nullptr, 0, -1},
        {"flt_release", "Filter Release", "s", "Filter", Domain::quadSec, 0.010f, 1.5f, nullptr, 0, -1},
        {"dist_mode", "Distortion", "", "Distortion", Domain::choiceFloor4, 0, 0, kDistModes, 4, -1},
        {"dist_tone", "Tone", "", "Distortion", Domain::unit, 0, 0, nullptr, 0, -1},
        {"dist_mix", "Dist Mix", "", "Distortion", Domain::unit, 0, 0, nullptr, 0, -1},
        {"pan", "Pan", "", "Output", Domain::pan, 0, 0, nullptr, 0, -1},
        {"mod_target", "Mod Target", "", "Modulation", Domain::choiceFloor6, 0, 0, kTargets, 6, -1},
        {"mod_amount", "Mod Amount", "", "Modulation", Domain::unit, 0, 0, nullptr, 0, -1},
        {"mod_attack", "Mod Attack", "s", "Modulation", Domain::quadSec, 0.0008f, 1.2f, nullptr, 0, -1},
        {"mod_decay", "Mod Decay", "s", "Modulation", Domain::quadSec, 0.020f, 2.4f, nullptr, 0, -1},
        {"mod_sustain", "Mod Sustain", "", "Modulation", Domain::unit, 0, 0, nullptr, 0, -1},
        {"mod_release", "Mod Release", "s", "Modulation", Domain::quadSec, 0.005f, 2.0f, nullptr, 0, -1},
        {"portamento", "Portamento", "s", "Performance", Domain::slideSec, 0, 0, nullptr, 0, -1},
        {"unison_voices", "Unison", "", "Performance", Domain::unison, 0, 0, nullptr, 0, -1},
        {"unison_detune", "Detune", "", "Performance", Domain::unit, 0, 0, nullptr, 0, -1},
        {"stereo_spread", "Spread", "", "Performance", Domain::unit, 0, 0, nullptr, 0, -1},
        {"lfo1_rate", "LFO 1 Rate", "Hz", "LFO 1", Domain::lfoHz, 0, 0, nullptr, 0, -1},
        {"lfo1_depth", "LFO 1 Depth", "", "LFO 1", Domain::unit, 0, 0, nullptr, 0, -1},
        {"lfo1_wave", "LFO 1 Wave", "", "LFO 1", Domain::choiceRound4, 0, 0, kLfoWaves, 4, -1},
        {"lfo1_target", "LFO 1 Target", "", "LFO 1", Domain::choiceFloor6, 0, 0, kTargets, 6, -1},
        {"env2_target", "Env 2 Target", "", "Env 2", Domain::choiceFloor6, 0, 0, kTargets, 6, -1},
        {"env2_amount", "Env 2 Amount", "", "Env 2", Domain::unit, 0, 0, nullptr, 0, -1},
        {"env2_attack", "Env 2 Attack", "s", "Env 2", Domain::quadSec, 0.0008f, 1.2f, nullptr, 0, -1},
        {"env2_decay", "Env 2 Decay", "s", "Env 2", Domain::quadSec, 0.020f, 2.4f, nullptr, 0, -1},
        {"env2_sustain", "Env 2 Sustain", "", "Env 2", Domain::unit, 0, 0, nullptr, 0, -1},
        {"env2_release", "Env 2 Release", "s", "Env 2", Domain::quadSec, 0.005f, 2.0f, nullptr, 0, -1},
        {"lfo2_rate", "LFO 2 Rate", "Hz", "LFO 2", Domain::lfoHz, 0, 0, nullptr, 0, -1},
        {"lfo2_depth", "LFO 2 Depth", "", "LFO 2", Domain::unit, 0, 0, nullptr, 0, -1},
        {"lfo2_wave", "LFO 2 Wave", "", "LFO 2", Domain::choiceRound4, 0, 0, kLfoWaves, 4, -1},
        {"lfo2_target", "LFO 2 Target", "", "LFO 2", Domain::choiceFloor6, 0, 0, kTargets, 6, -1},
        {"lfo3_rate", "LFO 3 Rate", "Hz", "LFO 3", Domain::lfoHz, 0, 0, nullptr, 0, -1},
        {"lfo3_depth", "LFO 3 Depth", "", "LFO 3", Domain::unit, 0, 0, nullptr, 0, -1},
        {"lfo3_wave", "LFO 3 Wave", "", "LFO 3", Domain::choiceRound4, 0, 0, kLfoWaves, 4, -1},
        {"lfo3_target", "LFO 3 Target", "", "LFO 3", Domain::choiceFloor6, 0, 0, kTargets, 6, -1},
        {"step_amount", "Step Amount", "", "Step", Domain::unit, 0, 0, nullptr, 0, -1},
        {"step_target", "Step Target", "", "Step", Domain::choiceFloor6, 0, 0, kTargets, 6, -1},
        {"step_rate", "Step Rate", "", "Step", Domain::choiceRound4, 0, 0, kStepRates, 4, -1},
        {"step_smooth", "Step Smooth", "", "Step", Domain::unit, 0, 0, nullptr, 0, -1},
        {"step_01", "Step 1", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_02", "Step 2", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_03", "Step 3", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_04", "Step 4", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_05", "Step 5", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_06", "Step 6", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_07", "Step 7", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_08", "Step 8", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_09", "Step 9", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_10", "Step 10", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_11", "Step 11", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_12", "Step 12", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_13", "Step 13", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_14", "Step 14", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_15", "Step 15", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"step_16", "Step 16", "", "Step", Domain::bipolar, 0, 0, nullptr, 0, -1},
        {"lfo1_sync", "LFO 1 Sync", "", "LFO 1", Domain::booleanNorm, 0, 0, nullptr, 0, -1},
        {"lfo2_sync", "LFO 2 Sync", "", "LFO 2", Domain::booleanNorm, 0, 0, nullptr, 0, -1},
        {"lfo3_sync", "LFO 3 Sync", "", "LFO 3", Domain::booleanNorm, 0, 0, nullptr, 0, -1},
        {"legato", "Legato", "", "Play", Domain::playFlag, 0, 0, nullptr, 0, 0},
        {"retrigger", "Retrigger", "", "Play", Domain::playFlag, 0, 0, nullptr, 0, 1},
        {"vel_to_accent", "Vel to Accent", "", "Play", Domain::playFlag, 0, 0, nullptr, 0, 2},
        {"slide_on_overlap", "Slide on Overlap", "", "Play", Domain::playFlag, 0, 0, nullptr, 0, 3},
    }};
    return table;
}

[[nodiscard]] inline float clamp01(float value) noexcept {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] inline int clampIndex(float plain, int last) noexcept {
    if (!std::isfinite(plain)) return 0;
    return std::clamp(static_cast<int>(std::lround(plain)), 0, last);
}

[[nodiscard]] inline float plainToNorm(const Control& control, float plain) noexcept {
    switch (control.domain) {
        case Domain::unit:
        case Domain::booleanNorm:
            return control.domain == Domain::booleanNorm
                ? (plain >= 0.5f ? 1.0f : 0.0f) : clamp01(plain);
        case Domain::cutoffHz:
            return AcidEngine::cutoffHzToNormalized(
                std::isfinite(plain) ? plain : 20.0f);
        case Domain::semitone: {
            const float semi = std::clamp(std::isfinite(plain) ? plain : 0.0f, -24.0f, 24.0f);
            return (semi + 24.0f) / 48.0f;
        }
        case Domain::cent: {
            const float cent = std::clamp(std::isfinite(plain) ? plain : 0.0f, -100.0f, 100.0f);
            return (cent + 100.0f) / 200.0f;
        }
        case Domain::pan:
        case Domain::bipolar: {
            const float value = std::clamp(std::isfinite(plain) ? plain : 0.0f, -1.0f, 1.0f);
            return (value + 1.0f) * 0.5f;
        }
        case Domain::slideSec: {
            const float sec = std::clamp(std::isfinite(plain) ? plain : 0.006f, 0.006f, 0.246f);
            return (sec - 0.006f) / 0.240f;
        }
        case Domain::quadSec: {
            const float hi = control.curveA + control.curveB;
            const float sec = std::clamp(std::isfinite(plain) ? plain : control.curveA,
                                          control.curveA, hi);
            return std::sqrt(std::max(0.0f, (sec - control.curveA) / control.curveB));
        }
        case Domain::lfoHz: {
            const float hz = std::clamp(std::isfinite(plain) ? plain : 0.05f, 0.05f, 20.0f);
            return std::log(hz / 0.05f) / std::log(400.0f);
        }
        case Domain::choiceFloor4: {
            const int index = clampIndex(plain, 3);
            return std::min(1.0f, (static_cast<float>(index) + 0.5f) / 3.99f);
        }
        case Domain::choiceRound4: {
            const int index = clampIndex(plain, 3);
            return static_cast<float>(index) / 3.0f;
        }
        case Domain::choiceFloor6: {
            const int index = clampIndex(plain, 5);
            return std::min(1.0f, (static_cast<float>(index) + 0.5f) / 5.99f);
        }
        case Domain::unison: {
            const int voices = std::clamp(clampIndex(plain, 4), 1, 4);
            return static_cast<float>(voices - 1) / 3.0f;
        }
        case Domain::playFlag:
            return plain >= 0.5f ? 1.0f : 0.0f;
    }
    return 0.0f;
}

[[nodiscard]] inline float normToPlain(const Control& control, float norm) noexcept {
    const float n = clamp01(norm);
    switch (control.domain) {
        case Domain::unit:
        case Domain::booleanNorm:
        case Domain::playFlag:
            return control.domain == Domain::unit ? n : (n >= 0.5f ? 1.0f : 0.0f);
        case Domain::cutoffHz:
            return AcidEngine::cutoffNormalizedToHz(n);
        case Domain::semitone:
            return (n - 0.5f) * 48.0f;
        case Domain::cent:
            return (n - 0.5f) * 200.0f;
        case Domain::pan:
        case Domain::bipolar:
            return n * 2.0f - 1.0f;
        case Domain::slideSec:
            return 0.006f + n * 0.240f;
        case Domain::quadSec:
            return control.curveA + n * n * control.curveB;
        case Domain::lfoHz:
            return 0.05f * std::pow(400.0f, n);
        case Domain::choiceFloor4:
            return static_cast<float>(std::clamp(static_cast<int>(n * 3.99f), 0, 3));
        case Domain::choiceRound4:
            return static_cast<float>(std::clamp(
                static_cast<int>(std::lround(n * 3.0f)), 0, 3));
        case Domain::choiceFloor6:
            return static_cast<float>(std::clamp(static_cast<int>(n * 5.99f), 0, 5));
        case Domain::unison:
            return static_cast<float>(std::clamp(
                1 + static_cast<int>(std::lround(n * 3.0f)), 1, 4));
    }
    return 0.0f;
}

inline void plainRange(const Control& control, float& minimum, float& maximum,
                       float& step, std::uint32_t& type) noexcept {
    step = 0.0f;
    type = VOX_PARAM_FLOAT;
    switch (control.domain) {
        case Domain::unit:
            minimum = 0.0f; maximum = 1.0f; break;
        case Domain::cutoffHz:
            minimum = 20.0f; maximum = 18000.0f; break;
        case Domain::semitone:
            minimum = -24.0f; maximum = 24.0f; break;
        case Domain::cent:
            minimum = -100.0f; maximum = 100.0f; break;
        case Domain::pan:
        case Domain::bipolar:
            minimum = -1.0f; maximum = 1.0f; break;
        case Domain::slideSec:
            minimum = 0.006f; maximum = 0.246f; break;
        case Domain::quadSec:
            minimum = control.curveA;
            maximum = control.curveA + control.curveB;
            break;
        case Domain::lfoHz:
            minimum = 0.05f; maximum = 20.0f; break;
        case Domain::choiceFloor4:
        case Domain::choiceRound4:
        case Domain::choiceFloor6:
            minimum = 0.0f;
            maximum = static_cast<float>(control.choiceCount - 1);
            step = 1.0f;
            type = VOX_PARAM_CHOICE;
            break;
        case Domain::unison:
            minimum = 1.0f; maximum = 4.0f; step = 1.0f;
            type = VOX_PARAM_INTEGER;
            break;
        case Domain::booleanNorm:
        case Domain::playFlag:
            minimum = 0.0f; maximum = 1.0f; step = 1.0f;
            type = VOX_PARAM_BOOLEAN;
            break;
    }
}

[[nodiscard]] inline bool continuous(Domain domain) noexcept {
    return domain != Domain::choiceFloor4 && domain != Domain::choiceRound4
        && domain != Domain::choiceFloor6 && domain != Domain::unison
        && domain != Domain::booleanNorm && domain != Domain::playFlag;
}

} // namespace vstengine::acid::module
