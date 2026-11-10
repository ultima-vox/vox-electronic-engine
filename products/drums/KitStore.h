#pragma once

// VOX Drums structural pad settings (DRM-03b): model, note, choke group,
// polyphony, fine, character, filter, resonance, transient, kick body,
// sample source/mode/split and the two sample players. They are edited in
// the editor and saved as the versioned state section DPAD; host parameters
// (level, tune, decay, pan, cutoff, mute, kit, sequencer) are not here.
//
// Threads: the message/host threads edit under a SpinLock held only for a
// struct copy. The audio thread pull()s with tryEnter: it never waits; if
// the lock is busy it keeps the previous block's settings and takes the new
// ones on the next block.

#include <juce_core/juce_core.h>

#include "StateContainer.h"
#include "engine/KitEngine.h"

#include <atomic>
#include <map>
#include <string>

namespace vox::drums {

inline constexpr preset::Tag padSectionTag = preset::makeTag ("DPAD");

class KitStore {
public:
    KitStore();

    //== message / host threads
    kit::PadSettings pad (int pad) const;
    void setPad (int pad, const kit::PadSettings& settings);   // structural fields only
    kit::KitSettings snapshot() const;
    void setAll (const kit::KitSettings& settings);           // structural fields only
    std::uint64_t generation() const noexcept { return generation_.load(); }

    static preset::Section encode (const kit::KitSettings& settings);
    // Strict: a section with a value outside its range, NaN, an unknown
    // version or a wrong size is rejected (error set, false returned).
    static bool decode (const preset::Section& section, kit::KitSettings& out, juce::String& error);
    // DRM-01/02 projects stored these settings as parameters
    // (drm.p01.character, .fine, .filter, ..., .sample_level): they map onto
    // pads 1-8 and player A.
    static void migrateLegacyParameters (const std::map<std::string, float>& values, kit::KitSettings& out);

    //== audio thread (wait-free)
    // Copies the structural fields into `live` if they changed and the lock
    // is free; otherwise leaves `live` as it is.
    void pull (kit::KitSettings& live) noexcept;

    // Copies the structural fields of `from` into `to` (host fields untouched).
    static void copyStructure (const kit::KitSettings& from, kit::KitSettings& to) noexcept;

private:
    mutable juce::SpinLock lock_;
    kit::KitSettings structure_;
    bool dirty_ = true;
    std::atomic<std::uint64_t> generation_ { 1 };
};

} // namespace vox::drums
