#pragma once

// VOX Drums plugin parameters (DRM-01). Every plugin parameter has a stable
// string ID ("drm.<scope>.<name>", 02_ULTIMA_VOX_DRUMS_SPEC.md) that is its
// host identity; products/drums/parameters.csv is the committed contract and
// a test checks the layout against it. IDs are append-only.
//
// Engine parameters are exposed except the ones whose feature is not in this
// build yet (pad source/sample settings: DRM-02; sends and outputs: DRM-06);
// those stay at their engine defaults.

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/DrumEngine.h"

#include <string>
#include <vector>

namespace vox::drums {

struct PluginParameter {
    std::string id;                   // host identity, e.g. "drm.p01.level"
    int engineIndex = -1;             // DrumEngine parameter index; -1 = plugin-only
    const EngineParameter* engine = nullptr;
};

inline const juce::String patternParameterId { "drm.seq.pattern" };

// Plugin parameters in host order (engine order, then plugin-only ones).
const std::vector<PluginParameter>& pluginParameters();

// "kick.level" -> "drm.p01.level"; empty when the engine parameter is not
// exposed in this build.
std::string pluginIdFor (const std::string& legacyId);

// Pad 0..7 -> "drm.p01".."drm.p08" prefix, for the editor.
std::string padPrefix (int pad);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// One CSV line per plugin parameter, the format of parameters.csv.
std::string describeLayout (juce::AudioProcessorValueTreeState& state);

} // namespace vox::drums
