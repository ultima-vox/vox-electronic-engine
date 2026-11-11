#pragma once

// VOX Drums presets (message thread): kits, pads, patterns and songs as
// files, the factory content, and the portable kit.
//
// A preset is a VOXS container (the project state format) with a DPTY
// section naming its type and only the sections that type owns:
//   kit      DPAD, DSMP, DPRM (level, tune, decay, pan, cutoff, sends of the
//            pads; kit volume, velocity, choke; delay and reverb)
//   pad      the same for one pad; it loads onto any pad (the target keeps
//            its MIDI note)
//   pattern  DSEQ holding the pattern's A and B in slot 1
//   song     DSEQ, DSNG, DGEN
//
// Loading is transactional: the preset is merged into a copy of the current
// project state and applied through setStateInformation, which validates
// everything before changing anything. A malformed or foreign file leaves
// the instrument as it was. The state before the last load is kept for
// Undo Load.
//
// Samples: a preset stores references (path, name, content hash), never
// audio. A missing file is looked up in the user sample library by hash;
// if it is not there the pad shows it as missing and keeps the reference.
// A portable kit (.voxkit, a ZIP) also carries the sample files; importing
// copies them into the user sample library after checking their hashes.

#include "Processor.h"

namespace vox::drums {

inline constexpr preset::Tag presetTypeSectionTag = preset::makeTag ("DPTY");

enum class PresetType : std::uint8_t { kit, pad, pattern, song };
const char* presetTypeName (PresetType) noexcept;        // "Kit"
const char* presetExtension (PresetType) noexcept;       // ".voxkitpreset" ...

struct PresetInfo {
    PresetType type = PresetType::kit;
    juce::String name;
    int source = 0;   // pad preset: the pad it was saved from
};

class Presets {
public:
    explicit Presets (DrumsProcessor& processor) : processor_ (processor) {}

    // VOX_DRUMS_PRESETS overrides the root (tests, portable setups).
    static juce::File userFolder (PresetType type);
    juce::Array<juce::File> userPresets (PresetType type) const;

    // The preset of `type` from the current state. `index`: the pad of a
    // pad preset, the slot of a pattern preset.
    juce::MemoryBlock capture (PresetType type, const juce::String& name, int index = 0) const;
    static bool describe (const void* data, std::size_t size, const DrumsProcessor& processor, PresetInfo& out);

    // Applies a preset; `target`: the pad a pad preset loads onto, the slot
    // a pattern preset loads into (-1: its source pad / the current slot).
    juce::Result apply (const void* data, std::size_t size, int target = -1);
    juce::Result save (PresetType type, const juce::String& name, const juce::File& file, int index = 0) const;
    juce::Result load (const juce::File& file, int target = -1);

    bool canUndoLoad() const noexcept { return previous_.getSize() > 0; }
    juce::Result undoLoad();

    // Factory content, through the same path as a preset file.
    juce::Result loadFactoryKit (int kit);
    juce::Result loadFactoryPatterns (int kit);

    // Portable kit: the kit preset and every sample file it references.
    juce::Result exportPortableKit (const juce::File& zip, const juce::String& name) const;
    juce::Result importPortableKit (const juce::File& zip);

private:
    juce::Result applyDocument (preset::StateDocument&& preset, int target);
    DrumsProcessor& processor_;
    juce::MemoryBlock previous_;
};

} // namespace vox::drums
