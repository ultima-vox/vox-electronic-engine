#pragma once
#include <juce_core/juce_core.h>
#include <cstddef>
#include <span>

namespace vox::fx::library {
// User-preset save and tag edits retain their separate, existing policy.
// The WAV/SMF publisher has no route to this .voxfx-only function.
bool writePresetBytes(const juce::File& file,std::span<const std::byte> payload);
}
