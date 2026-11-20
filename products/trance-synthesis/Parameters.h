#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cstddef>

namespace vox::syn {

// Host parameter ids. Stable for ENG1. Do not reorder existing ids.
// A missing id falls back to the default. New ids are appended.
struct Parameter
{
    const char* id;
    const char* name;
    float minimum;
    float maximum;
    float initial;
    bool choice;
};

inline constexpr int engineChoices = 3;

inline const std::array<Parameter, 115> parameterCatalog {{
    { "syn.a.engine", "Part A Engine", 0.0f, 2.0f, 0.0f, true },
    { "syn.a.level", "Part A Level", 0.0f, 1.0f, 0.75f, false },
    { "syn.a.cutoff", "Part A Cutoff", 0.0f, 1.0f, 0.55f, false },
    { "syn.a.resonance", "Part A Resonance", 0.0f, 1.0f, 0.35f, false },
    { "syn.a.slide", "Part A Slide", 0.0f, 1.0f, 0.30f, false },
    { "syn.a.portamento", "Part A Portamento", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.drive", "Part A Drive", 0.0f, 1.0f, 0.25f, false },
    { "syn.b.engine", "Part B Engine", 0.0f, 2.0f, 1.0f, true },
    { "syn.b.level", "Part B Level", 0.0f, 1.0f, 0.55f, false },
    { "syn.b.cutoff", "Part B Cutoff", 0.0f, 1.0f, 0.48f, false },
    { "syn.b.resonance", "Part B Resonance", 0.0f, 1.0f, 0.62f, false },
    { "syn.b.slide", "Part B Slide", 0.0f, 1.0f, 0.35f, false },
    { "syn.b.portamento", "Part B Portamento", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.drive", "Part B Drive", 0.0f, 1.0f, 0.30f, false },
    { "syn.output", "Output", 0.0f, 1.0f, 0.80f, false },
    { "syn.fx.delay", "Delay", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.reverb", "Reverb", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.sub", "Part A Sub", 0.0f, 1.0f, 0.18f, false },
    { "syn.a.pitch", "Part A Pitch", 0.0f, 1.0f, 0.18f, false },
    { "syn.a.wave", "Part A Wave", 0.0f, 1.0f, 0.30f, false },
    { "syn.a.env", "Part A Env", 0.0f, 1.0f, 1.0f, false },
    { "syn.a.detune", "Part A Detune", 0.0f, 1.0f, 0.16f, false },
    { "syn.a.attack", "Part A Attack", 0.0f, 1.0f, 0.035f, false },
    { "syn.a.decay", "Part A Decay", 0.0f, 1.0f, 0.12f, false },
    { "syn.a.sustain", "Part A Sustain", 0.0f, 1.0f, 0.72f, false },
    { "syn.a.release", "Part A Release", 0.0f, 1.0f, 0.26f, false },
    { "syn.a.noise", "Part A Noise", 0.0f, 1.0f, 0.04f, false },
    { "syn.b.sub", "Part B Sub", 0.0f, 1.0f, 0.18f, false },
    { "syn.b.pitch", "Part B Pitch", 0.0f, 1.0f, 0.18f, false },
    { "syn.b.wave", "Part B Wave", 0.0f, 1.0f, 0.30f, false },
    { "syn.b.env", "Part B Env", 0.0f, 1.0f, 1.0f, false },
    { "syn.b.detune", "Part B Detune", 0.0f, 1.0f, 0.16f, false },
    { "syn.b.attack", "Part B Attack", 0.0f, 1.0f, 0.035f, false },
    { "syn.b.decay", "Part B Decay", 0.0f, 1.0f, 0.12f, false },
    { "syn.b.sustain", "Part B Sustain", 0.0f, 1.0f, 0.72f, false },
    { "syn.b.release", "Part B Release", 0.0f, 1.0f, 0.26f, false },
    { "syn.b.noise", "Part B Noise", 0.0f, 1.0f, 0.04f, false },
    { "syn.fx.distortion", "Distortion", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.distortion.mix", "Distortion Mix", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.distortion.amount", "Distortion Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.distortion.rate", "Distortion Rate", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.distortion.character", "Distortion Character", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.wavefolder", "Wavefolder", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.wavefolder.mix", "Wavefolder Mix", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.wavefolder.amount", "Wavefolder Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.wavefolder.rate", "Wavefolder Rate", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.wavefolder.character", "Wavefolder Character", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.phaser", "Phaser", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.phaser.mix", "Phaser Mix", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.phaser.amount", "Phaser Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.phaser.rate", "Phaser Rate", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.phaser.character", "Phaser Character", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.flanger", "Flanger", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.flanger.mix", "Flanger Mix", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.flanger.amount", "Flanger Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.flanger.rate", "Flanger Rate", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.flanger.character", "Flanger Character", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.chorus", "Chorus", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.chorus.mix", "Chorus Mix", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.chorus.amount", "Chorus Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.chorus.rate", "Chorus Rate", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.chorus.character", "Chorus Character", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.bitcrusher", "Bitcrusher", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.bitcrusher.mix", "Bitcrusher Mix", 0.0f, 1.0f, 0.0f, false },
    { "syn.fx.bitcrusher.amount", "Bitcrusher Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.bitcrusher.rate", "Bitcrusher Rate", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.bitcrusher.character", "Bitcrusher Character", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.delay.mix", "Delay Mix", 0.0f, 1.0f, 0.35f, false },
    { "syn.fx.delay.amount", "Delay Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.delay.rate", "Delay Rate", 0.0f, 1.0f, 0.4f, false },
    { "syn.fx.delay.character", "Delay Character", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.reverb.mix", "Reverb Mix", 0.0f, 1.0f, 0.35f, false },
    { "syn.fx.reverb.amount", "Reverb Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.fx.reverb.rate", "Reverb Rate", 0.0f, 1.0f, 0.4f, false },
    { "syn.fx.reverb.character", "Reverb Character", 0.0f, 1.0f, 0.5f, false },
    { "syn.a.osc1.wave", "Part A Osc1 Wave", 0.0f, 4.0f, 1.0f, false },
    { "syn.a.osc1.level", "Part A Osc1 Level", 0.0f, 1.0f, 0.62f, false },
    { "syn.a.osc1.pw", "Part A Osc1 Pulse", 0.05f, 0.95f, 0.5f, false },
    { "syn.a.osc2.wave", "Part A Osc2 Wave", 0.0f, 4.0f, 1.0f, false },
    { "syn.a.osc2.level", "Part A Osc2 Level", 0.0f, 1.0f, 0.30f, false },
    { "syn.a.osc2.pw", "Part A Osc2 Pulse", 0.05f, 0.95f, 0.5f, false },
    { "syn.a.osc3.wave", "Part A Osc3 Wave", 0.0f, 4.0f, 0.0f, false },
    { "syn.a.osc3.level", "Part A Osc3 Level", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.osc3.semi", "Part A Osc3 Semi", 0.0f, 1.0f, 0.5f, false },
    { "syn.a.va.sub", "Part A VA Sub", 0.0f, 1.0f, 0.16f, false },
    { "syn.a.ring", "Part A Ring", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.fm", "Part A FM", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.flt1.type", "Part A Filter 1", 0.0f, 5.0f, 0.0f, false },
    { "syn.a.flt2.type", "Part A Filter 2", 0.0f, 5.0f, 0.0f, false },
    { "syn.a.flt2.cutoff", "Part A Filter 2 Cutoff", 0.0f, 1.0f, 1.0f, false },
    { "syn.a.flt2.resonance", "Part A Filter 2 Resonance", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.flt2.mix", "Part A Filter 2 Mix", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.flt.route", "Part A Filter Route", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.flt.env", "Part A Filter Env", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.flt.key", "Part A Key Track", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.osc1.wave", "Part B Osc1 Wave", 0.0f, 4.0f, 1.0f, false },
    { "syn.b.osc1.level", "Part B Osc1 Level", 0.0f, 1.0f, 0.62f, false },
    { "syn.b.osc1.pw", "Part B Osc1 Pulse", 0.05f, 0.95f, 0.5f, false },
    { "syn.b.osc2.wave", "Part B Osc2 Wave", 0.0f, 4.0f, 1.0f, false },
    { "syn.b.osc2.level", "Part B Osc2 Level", 0.0f, 1.0f, 0.30f, false },
    { "syn.b.osc2.pw", "Part B Osc2 Pulse", 0.05f, 0.95f, 0.5f, false },
    { "syn.b.osc3.wave", "Part B Osc3 Wave", 0.0f, 4.0f, 0.0f, false },
    { "syn.b.osc3.level", "Part B Osc3 Level", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.osc3.semi", "Part B Osc3 Semi", 0.0f, 1.0f, 0.5f, false },
    { "syn.b.va.sub", "Part B VA Sub", 0.0f, 1.0f, 0.16f, false },
    { "syn.b.ring", "Part B Ring", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.fm", "Part B FM", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.flt1.type", "Part B Filter 1", 0.0f, 5.0f, 0.0f, false },
    { "syn.b.flt2.type", "Part B Filter 2", 0.0f, 5.0f, 0.0f, false },
    { "syn.b.flt2.cutoff", "Part B Filter 2 Cutoff", 0.0f, 1.0f, 1.0f, false },
    { "syn.b.flt2.resonance", "Part B Filter 2 Resonance", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.flt2.mix", "Part B Filter 2 Mix", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.flt.route", "Part B Filter Route", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.flt.env", "Part B Filter Env", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.flt.key", "Part B Key Track", 0.0f, 1.0f, 0.0f, false },
}};

inline constexpr std::size_t outputParameterIndex = 14;
inline constexpr std::size_t delayParameterIndex = 15;
inline constexpr std::size_t reverbParameterIndex = 16;
inline constexpr std::size_t partExtraCount = 10;
inline constexpr std::size_t partAExtraIndex = 17;
inline constexpr std::size_t partBExtraIndex = 27;
inline constexpr std::size_t delayMixIndex = 67;
inline constexpr std::size_t reverbMixIndex = 71;

enum class PartExtra : std::size_t {
    sub, pitch, wave, env, detune, attack, decay, sustain, release, noise
};

struct FxBinding {
    int slot;
    int enable;
    int mix;
    int amount;
    int rate;
    int character;
};

inline constexpr std::array<FxBinding, 8> fxBindings {{
    { 0, 37, 38, 39, 40, 41 },
    { 1, 42, 43, 44, 45, 46 },
    { 2, 47, 48, 49, 50, 51 },
    { 3, 52, 53, 54, 55, 56 },
    { 4, 57, 58, 59, 60, 61 },
    { 5, 62, 63, 64, 65, 66 },
    { 6, 15, 67, 68, 69, 70 },
    { 7, 16, 71, 72, 73, 74 },
}};

static_assert (parameterCatalog.size() == 115);
static_assert (fxBindings.back().character + 1 == 75);

// Factory rows and historical UBNK values stop here. Later ids use their defaults.
inline constexpr int frozenParameterCount = 75;
inline constexpr int vaParamCount = 20;
inline constexpr int vaParamBase = frozenParameterCount;

enum class VaKnob : int {
    osc1Wave, osc1Level, osc1Pw,
    osc2Wave, osc2Level, osc2Pw,
    osc3Wave, osc3Level, osc3Semi,
    sub, ring, fm,
    filterType, filter2Type, filter2Cutoff, filter2Resonance, filter2Mix, filterRoute,
    filterEnv, keyTrack,
    count
};

static_assert (static_cast<int> (VaKnob::count) == vaParamCount);
static_assert (vaParamBase + vaParamCount * 2 == static_cast<int> (parameterCatalog.size()));

inline constexpr const char* outputParameterName = "Output";

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace vox::syn
