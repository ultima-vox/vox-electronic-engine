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

inline const std::array<Parameter, 205> parameterCatalog {{
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
    { "syn.a.flt.a.attack", "Part A Filter Attack", 0.0f, 1.0f, 0.035f, false },
    { "syn.a.flt.a.decay", "Part A Filter Decay", 0.0f, 1.0f, 0.12f, false },
    { "syn.a.flt.a.sustain", "Part A Filter Sustain", 0.0f, 1.0f, 0.72f, false },
    { "syn.a.flt.a.release", "Part A Filter Release", 0.0f, 1.0f, 0.26f, false },
    { "syn.b.flt.a.attack", "Part B Filter Attack", 0.0f, 1.0f, 0.035f, false },
    { "syn.b.flt.a.decay", "Part B Filter Decay", 0.0f, 1.0f, 0.12f, false },
    { "syn.b.flt.a.sustain", "Part B Filter Sustain", 0.0f, 1.0f, 0.72f, false },
    { "syn.b.flt.a.release", "Part B Filter Release", 0.0f, 1.0f, 0.26f, false },
    { "syn.a.lfo1.rate", "Part A LFO1 Rate", 0.0f, 1.0f, 0.25f, false },
    { "syn.a.lfo1.shape", "Part A LFO1 Shape", 0.0f, 6.0f, 0.0f, false },
    { "syn.a.lfo2.rate", "Part A LFO2 Rate", 0.0f, 1.0f, 0.25f, false },
    { "syn.a.lfo2.shape", "Part A LFO2 Shape", 0.0f, 6.0f, 0.0f, false },
    { "syn.a.lfo3.rate", "Part A LFO3 Rate", 0.0f, 1.0f, 0.25f, false },
    { "syn.a.lfo3.shape", "Part A LFO3 Shape", 0.0f, 6.0f, 0.0f, false },
    { "syn.a.lfo4.rate", "Part A LFO4 Rate", 0.0f, 1.0f, 0.25f, false },
    { "syn.a.lfo4.shape", "Part A LFO4 Shape", 0.0f, 6.0f, 0.0f, false },
    { "syn.b.lfo1.rate", "Part B LFO1 Rate", 0.0f, 1.0f, 0.25f, false },
    { "syn.b.lfo1.shape", "Part B LFO1 Shape", 0.0f, 6.0f, 0.0f, false },
    { "syn.b.lfo2.rate", "Part B LFO2 Rate", 0.0f, 1.0f, 0.25f, false },
    { "syn.b.lfo2.shape", "Part B LFO2 Shape", 0.0f, 6.0f, 0.0f, false },
    { "syn.b.lfo3.rate", "Part B LFO3 Rate", 0.0f, 1.0f, 0.25f, false },
    { "syn.b.lfo3.shape", "Part B LFO3 Shape", 0.0f, 6.0f, 0.0f, false },
    { "syn.b.lfo4.rate", "Part B LFO4 Rate", 0.0f, 1.0f, 0.25f, false },
    { "syn.b.lfo4.shape", "Part B LFO4 Shape", 0.0f, 6.0f, 0.0f, false },
    { "syn.a.mod1.src", "Part A Mod1 Source", 0.0f, 12.0f, 0.0f, false },
    { "syn.a.mod1.dst", "Part A Mod1 Dest", 0.0f, 5.0f, 0.0f, false },
    { "syn.a.mod1.amt", "Part A Mod1 Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.a.mod2.src", "Part A Mod2 Source", 0.0f, 12.0f, 0.0f, false },
    { "syn.a.mod2.dst", "Part A Mod2 Dest", 0.0f, 5.0f, 0.0f, false },
    { "syn.a.mod2.amt", "Part A Mod2 Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.a.mod3.src", "Part A Mod3 Source", 0.0f, 12.0f, 0.0f, false },
    { "syn.a.mod3.dst", "Part A Mod3 Dest", 0.0f, 5.0f, 0.0f, false },
    { "syn.a.mod3.amt", "Part A Mod3 Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.a.mod4.src", "Part A Mod4 Source", 0.0f, 12.0f, 0.0f, false },
    { "syn.a.mod4.dst", "Part A Mod4 Dest", 0.0f, 5.0f, 0.0f, false },
    { "syn.a.mod4.amt", "Part A Mod4 Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.b.mod1.src", "Part B Mod1 Source", 0.0f, 12.0f, 0.0f, false },
    { "syn.b.mod1.dst", "Part B Mod1 Dest", 0.0f, 5.0f, 0.0f, false },
    { "syn.b.mod1.amt", "Part B Mod1 Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.b.mod2.src", "Part B Mod2 Source", 0.0f, 12.0f, 0.0f, false },
    { "syn.b.mod2.dst", "Part B Mod2 Dest", 0.0f, 5.0f, 0.0f, false },
    { "syn.b.mod2.amt", "Part B Mod2 Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.b.mod3.src", "Part B Mod3 Source", 0.0f, 12.0f, 0.0f, false },
    { "syn.b.mod3.dst", "Part B Mod3 Dest", 0.0f, 5.0f, 0.0f, false },
    { "syn.b.mod3.amt", "Part B Mod3 Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.b.mod4.src", "Part B Mod4 Source", 0.0f, 12.0f, 0.0f, false },
    { "syn.b.mod4.dst", "Part B Mod4 Dest", 0.0f, 5.0f, 0.0f, false },
    { "syn.b.mod4.amt", "Part B Mod4 Amount", 0.0f, 1.0f, 0.5f, false },
    { "syn.macro.1", "Macro 1", 0.0f, 1.0f, 0.0f, false },
    { "syn.macro.2", "Macro 2", 0.0f, 1.0f, 0.0f, false },
    { "syn.macro.3", "Macro 3", 0.0f, 1.0f, 0.0f, false },
    { "syn.macro.4", "Macro 4", 0.0f, 1.0f, 0.0f, false },
    { "syn.macro.5", "Macro 5", 0.0f, 1.0f, 0.0f, false },
    { "syn.macro.6", "Macro 6", 0.0f, 1.0f, 0.0f, false },
    { "syn.macro.7", "Macro 7", 0.0f, 1.0f, 0.0f, false },
    { "syn.macro.8", "Macro 8", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.unison", "Part A Unison", 0.0f, 3.0f, 0.0f, false },
    { "syn.a.uni.detune", "Part A Unison Detune", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.uni.spread", "Part A Unison Spread", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.wt.level", "Part A Wavetable", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.wt.pos", "Part A Wavetable Pos", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.lfo1.sync", "Part A LFO1 Sync", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.lfo1.div", "Part A LFO1 Division", 0.0f, 8.0f, 2.0f, false },
    { "syn.a.lfo1.retrig", "Part A LFO1 Retrigger", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.lfo2.sync", "Part A LFO2 Sync", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.lfo2.div", "Part A LFO2 Division", 0.0f, 8.0f, 2.0f, false },
    { "syn.a.lfo2.retrig", "Part A LFO2 Retrigger", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.lfo3.sync", "Part A LFO3 Sync", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.lfo3.div", "Part A LFO3 Division", 0.0f, 8.0f, 2.0f, false },
    { "syn.a.lfo3.retrig", "Part A LFO3 Retrigger", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.lfo4.sync", "Part A LFO4 Sync", 0.0f, 1.0f, 0.0f, false },
    { "syn.a.lfo4.div", "Part A LFO4 Division", 0.0f, 8.0f, 2.0f, false },
    { "syn.a.lfo4.retrig", "Part A LFO4 Retrigger", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.unison", "Part B Unison", 0.0f, 3.0f, 0.0f, false },
    { "syn.b.uni.detune", "Part B Unison Detune", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.uni.spread", "Part B Unison Spread", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.wt.level", "Part B Wavetable", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.wt.pos", "Part B Wavetable Pos", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.lfo1.sync", "Part B LFO1 Sync", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.lfo1.div", "Part B LFO1 Division", 0.0f, 8.0f, 2.0f, false },
    { "syn.b.lfo1.retrig", "Part B LFO1 Retrigger", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.lfo2.sync", "Part B LFO2 Sync", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.lfo2.div", "Part B LFO2 Division", 0.0f, 8.0f, 2.0f, false },
    { "syn.b.lfo2.retrig", "Part B LFO2 Retrigger", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.lfo3.sync", "Part B LFO3 Sync", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.lfo3.div", "Part B LFO3 Division", 0.0f, 8.0f, 2.0f, false },
    { "syn.b.lfo3.retrig", "Part B LFO3 Retrigger", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.lfo4.sync", "Part B LFO4 Sync", 0.0f, 1.0f, 0.0f, false },
    { "syn.b.lfo4.div", "Part B LFO4 Division", 0.0f, 8.0f, 2.0f, false },
    { "syn.b.lfo4.retrig", "Part B LFO4 Retrigger", 0.0f, 1.0f, 0.0f, false },
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

static_assert (parameterCatalog.size() == 205);
static_assert (fxBindings.back().character + 1 == 75);

// Factory rows and historical UBNK values stop here. Later ids use their defaults.
inline constexpr int frozenParameterCount = 75;
inline constexpr int vaParamCount = 20;
inline constexpr int vaParamBase = frozenParameterCount;
inline constexpr int filterAdsrBase = 115;
inline constexpr int lfoBase = 123;
inline constexpr int routeBase = 139;
inline constexpr int macroBase = 163;
inline constexpr int lfoCount = 4;
inline constexpr int routeCount = 4;
inline constexpr int extraRouteCount = 28;
inline constexpr int featureBase = 171;
inline constexpr int featureCount = 17;
inline constexpr int macroCount = 8;

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
static_assert (vaParamBase + vaParamCount * 2 == filterAdsrBase);
static_assert (macroBase + macroCount == featureBase);
static_assert (featureBase + featureCount * 2 == static_cast<int> (parameterCatalog.size()));

inline constexpr const char* outputParameterName = "Output";

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace vox::syn
