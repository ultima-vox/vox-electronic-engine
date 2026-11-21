#pragma once

// Canonical existing structural UI bindings, shared by the donor and native
// workspace. Ranges, models and KitStore setters are unchanged.
#include "DrumsParameters.h"

namespace vox::drums {

// What a structural control reads and writes: the selected pad's settings
// and its selected sample player.
struct StructuralContext {
    kit::PadSettings pad;
    int player = 0;
    kit::SamplePlayer& sample() { return pad.players[static_cast<std::size_t> (player)]; }
    const kit::SamplePlayer& sample() const { return pad.players[static_cast<std::size_t> (player)]; }
};


template <typename Sink>
void populateSoundBindings (Sink& sink)
{
    // Structural controls of the selected pad.
    juce::StringArray models;
    for (int m = 0; m < kit::modelCount; ++m)
        models.add (modelName (static_cast<kit::Model> (m)));
    sink.addCombo ("model", "Model", models, [] (const StructuralContext& c) { return static_cast<float> (c.pad.model); },
              [] (StructuralContext& c, float v) { c.pad.model = static_cast<kit::Model> (juce::roundToInt (v)); });
    juce::StringArray notes;
    for (int n = 0; n < 128; ++n)
        notes.add (juce::MidiMessage::getMidiNoteName (n, true, true, 3) + " (" + juce::String (n) + ")");
    sink.addCombo ("note", "Note", notes, [] (const StructuralContext& c) { return static_cast<float> (c.pad.note); },
              [] (StructuralContext& c, float v) { c.pad.note = static_cast<std::uint8_t> (juce::roundToInt (v)); });
    sink.addCombo ("choke", "Choke", { "Off", "Group 1", "Group 2", "Group 3", "Group 4", "Group 5", "Group 6", "Group 7", "Group 8" },
              [] (const StructuralContext& c) { return static_cast<float> (c.pad.chokeGroup); },
              [] (StructuralContext& c, float v) { c.pad.chokeGroup = static_cast<std::uint8_t> (juce::roundToInt (v)); });
    sink.addCombo ("poly", "Voices", { "Mono", "2", "3", "4", "5", "6", "7", "8" },
              [] (const StructuralContext& c) { return static_cast<float> (c.pad.polyphony - 1); },
              [] (StructuralContext& c, float v) { c.pad.polyphony = static_cast<std::uint8_t> (juce::roundToInt (v) + 1); });
    sink.addKnob ("fine", "Fine", -100.0, 100.0, 1.0, [] (const StructuralContext& c) { return c.pad.fine; },
             [] (StructuralContext& c, float v) { c.pad.fine = v; }, true);
    sink.addKnob ("character", "Character", 0.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.pad.character; },
             [] (StructuralContext& c, float v) { c.pad.character = v; });
    sink.addCombo ("filter", "Filter", { "Off", "LP 12", "LP 24", "HP 12", "HP 24", "BP" },
              [] (const StructuralContext& c) { return static_cast<float> (c.pad.filter); },
              [] (StructuralContext& c, float v) { c.pad.filter = static_cast<kit::Filter> (juce::roundToInt (v)); });
    sink.addKnob ("resonance", "Resonance", 0.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.pad.resonance; },
             [] (StructuralContext& c, float v) { c.pad.resonance = v; });
    sink.addKnob ("attack", "Attack", -1.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.pad.attack; },
             [] (StructuralContext& c, float v) { c.pad.attack = v; }, true);
    sink.addKnob ("sustain", "Sustain", -1.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.pad.sustain; },
             [] (StructuralContext& c, float v) { c.pad.sustain = v; }, true);
    sink.addKnob ("pitch_amount", "Pitch Env", 0.0, 48.0, 0.0, [] (const StructuralContext& c) { return c.pad.pitchAmount; },
             [] (StructuralContext& c, float v) { c.pad.pitchAmount = v; });
    sink.addKnob ("pitch_decay", "Pitch Decay", 0.005, 0.2, 0.0, [] (const StructuralContext& c) { return c.pad.pitchDecay; },
             [] (StructuralContext& c, float v) { c.pad.pitchDecay = v; }, false, 0.0316);
    sink.addKnob ("drive", "Drive", 1.0, 8.0, 0.0, [] (const StructuralContext& c) { return c.pad.drive; },
             [] (StructuralContext& c, float v) { c.pad.drive = v; });
    sink.addKnob ("sub", "Sub", 0.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.pad.sub; },
             [] (StructuralContext& c, float v) { c.pad.sub = v; });
    sink.addKnob ("tail", "Tail", 0.03, 1.5, 0.0, [] (const StructuralContext& c) { return c.pad.tail; },
             [] (StructuralContext& c, float v) { c.pad.tail = v; }, false, 0.212);
}

template <typename Sink>
void populateSampleBindings (Sink& sink)
{
    sink.addCombo ("source", "Source", { "Synth", "Sample", "Layer" }, [] (const StructuralContext& c) { return static_cast<float> (c.pad.source); },
              [] (StructuralContext& c, float v) { c.pad.source = static_cast<kit::Source> (juce::roundToInt (v)); });
    sink.addCombo ("sample_mode", "Players", { "A only", "Velocity A/B", "A + B" },
              [] (const StructuralContext& c) { return static_cast<float> (c.pad.sampleMode); },
              [] (StructuralContext& c, float v) { c.pad.sampleMode = static_cast<kit::SampleMode> (juce::roundToInt (v)); });
    sink.addKnob ("split", "Split", 1.0, 127.0, 1.0, [] (const StructuralContext& c) { return static_cast<float> (c.pad.velocitySplit); },
             [] (StructuralContext& c, float v) { c.pad.velocitySplit = static_cast<std::uint8_t> (juce::roundToInt (v)); });
    sink.addKnob ("start", "Start", 0.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.sample().start; },
             [] (StructuralContext& c, float v) { c.sample().start = v; });
    sink.addKnob ("end", "End", 0.0, 1.0, 0.0, [] (const StructuralContext& c) { return c.sample().end; },
             [] (StructuralContext& c, float v) { c.sample().end = v; });
    sink.addKnob ("sample_level", "Level", 0.0, 2.0, 0.0, [] (const StructuralContext& c) { return c.sample().level; },
             [] (StructuralContext& c, float v) { c.sample().level = v; });
    sink.addToggle ("loop", "Loop", [] (const StructuralContext& c) { return c.sample().loop ? 1.0f : 0.0f; },
               [] (StructuralContext& c, float v) { c.sample().loop = v >= 0.5f; });
    sink.addToggle ("reverse", "Reverse", [] (const StructuralContext& c) { return c.sample().reverse ? 1.0f : 0.0f; },
               [] (StructuralContext& c, float v) { c.sample().reverse = v >= 0.5f; });
    sink.addToggle ("normalize", "Normalize", [] (const StructuralContext& c) { return c.sample().normalize ? 1.0f : 0.0f; },
               [] (StructuralContext& c, float v) { c.sample().normalize = v >= 0.5f; });

}

template <typename Sink>
void configurePadValueText (Sink& sink)
{
    // Value text of the structural knobs.
    const auto percent = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    const auto signedPercent = [] (double v) {
        const int n = juce::roundToInt (v * 100.0);
        return (n > 0 ? "+" : "") + juce::String (n) + " %";
    };
    const auto seconds = [] (double v) { return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms" : juce::String (v, 2) + " s"; };
    const std::pair<const char*, std::function<juce::String (double)>> formats[] = {
        { "fine", [] (double v) { const int n = juce::roundToInt (v); return (n > 0 ? "+" : "") + juce::String (n) + " ct"; } },
        { "character", percent },
        { "resonance", percent },
        { "attack", signedPercent },
        { "sustain", signedPercent },
        { "pitch_amount", [] (double v) { return juce::String (v, 1) + " st"; } },
        { "pitch_decay", seconds },
        { "drive", [] (double v) { return juce::String (v, 2) + "x"; } },
        { "sub", percent },
        { "tail", seconds },
        { "split", [] (double v) { return "vel " + juce::String (juce::roundToInt (v)); } },
        { "start", percent },
        { "end", percent },
        { "sample_level", percent },
    };
    for (const auto& [name, format] : formats)
        if (auto* st = sink.findStructural (name))
            st->knob->getSlider().textFromValueFunction = format;

}

} // namespace vox::drums
