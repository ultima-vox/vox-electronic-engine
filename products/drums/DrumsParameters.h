#pragma once

// VOX Drums host parameters (DRM-03b). Every parameter has a stable string ID
// ("drm.<scope>.<name>", 03_VOX_DRUMS_SPEC §6) that is its host identity;
// products/drums/parameters.csv is the committed contract and a test checks
// the layout against it. IDs are append-only from this release on.
//
// Host parameters are what a musician automates (03 §6): per pad level,
// tune, decay, pan, cutoff and mute (16 x 6), kit volume, velocity and
// choke, sequencer mode, pattern, swing, accent and the 16 lanes: 119.
// Sends arrive with the FX returns (DRM-06). Everything structural (model,
// note, choke group, polyphony, fine, character, filter type, resonance,
// transient, kick body, sample source and players) is versioned product
// state (section DPAD), edited in the editor.

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/KitEngine.h"

#include <string>
#include <vector>

namespace vox::drums {

inline constexpr int padCount = kit::padCount;

enum class Field { level, tune, decay, pan, cutoff, mute, kitVolume, kitVelocity, kitChoke,
                   seqMode, seqPattern, seqSwing, seqAccent, seqLane };

struct PluginParameter {
    std::string id;        // host identity, e.g. "drm.p01.level"
    std::string name;      // e.g. "Pad 1 Level"
    Field field;
    int pad = -1;          // 0..15 for pad fields and lanes
};

inline const juce::String patternParameterId { "drm.seq.pattern" };

const std::vector<PluginParameter>& pluginParameters();

// Pad 0..15 -> "drm.p01".."drm.p16".
std::string padPrefix (int pad);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// One CSV line per parameter, the format of parameters.csv.
std::string describeLayout (juce::AudioProcessorValueTreeState& state);

// The default kit (pads 1-8: the legacy kit; 9-16: provisional, see
// DRM-03_KIT_ENGINE.md) and pad display names.
const kit::KitSettings& defaultKit();
juce::String padName (int pad);
juce::String modelName (kit::Model model);

} // namespace vox::drums
