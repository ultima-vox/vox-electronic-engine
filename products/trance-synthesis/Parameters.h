#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <string_view>

namespace vox::syn {

// Host parameter ids. Stable for ENG1. Do not reorder: the state blob is
// written in this order, and a missing id falls back to the default.
struct Parameter
{
    const char* id;
    const char* name;
    float minimum;
    float maximum;
    float initial;
    bool choice;   // engine selector, three steps
};

inline constexpr int engineChoices = 3;

inline const std::array<Parameter, 15> parameterCatalog {{
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
}};

inline constexpr const char* outputParameterName = "Output";

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace vox::syn
