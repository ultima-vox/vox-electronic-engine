#pragma once

// VOX Drums presets (message thread): kits, pads, patterns and songs as
// files, the factory content, and the portable kit.
//
// A preset is a VOXS container (the project state format) with a DPTY
// section naming its type and only the sections that type owns:
//   kit      DPAD, DSMP, DPRM (level, tune, decay, pan, cutoff, sends of the
//            pads; kit volume, velocity, choke; delay and reverb), and DROL
//            when the kit has roles the user set
//   pad      the same for one pad; it loads onto any pad (the target keeps
//            its MIDI note, its output and its role). DROL, when written,
//            holds only the source pad's role; loading offers it
//            (roleOffer) and never applies it by itself
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

#include <optional>

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
    // `expected`: the kind the user chose (the list or dialog the file came
    // from). A file of another kind is refused, whatever its name: a song
    // preset renamed to a pad preset cannot replace the song.
    juce::Result apply (const void* data, std::size_t size, int target = -1, std::optional<PresetType> expected = {});
    juce::Result save (PresetType type, const juce::String& name, const juce::File& file, int index = 0) const;
    juce::Result load (const juce::File& file, PresetType expected, int target = -1);
    // Whether `document` holds everything a preset of `type` needs.
    static bool complete (const preset::StateDocument& document, PresetType type);

    // Undo Load belongs to the project state the load produced: a later
    // project restore by the host ends it.
    bool canUndoLoad() const noexcept;
    juce::Result undoLoad();

    // Factory content, through the same path as a preset file.
    juce::Result loadFactoryKit (int kit);
    juce::Result loadFactoryPatterns (int kit);

    // Pad roles (issue 132). The status note of the last successful load
    // ("Roles set to the default layout of this kit"), empty if none.
    juce::String lastNotice() const { return notice_; }
    // After a pad preset with a role other than the target pad's: that role,
    // offered for the target pad until the project state changes. Applying
    // it is one undoable edit of that pad's role only.
    struct RoleOffer { int pad = 0; PadRole role = PadRole::none; };
    std::optional<RoleOffer> roleOffer() const;
    bool applyRoleOffer();

    // Portable kit: the kit preset and every sample file it references.
    juce::Result exportPortableKit (const juce::File& zip, const juce::String& name) const;
    juce::Result importPortableKit (const juce::File& zip);

private:
    juce::Result applyDocument (preset::StateDocument&& preset, int target, std::optional<PresetType> expected = {});
    DrumsProcessor& processor_;
    juce::MemoryBlock previous_;
    std::uint64_t previousEpoch_ = 0;   // the processor's restore epoch right after the load
    juce::String notice_;
    std::optional<RoleOffer> offer_;
    std::uint64_t offerEpoch_ = 0;
};

} // namespace vox::drums
