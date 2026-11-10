#pragma once

// VOX Drums MIDI export (DRM-04): a pattern as a Standard MIDI File, from
// the same rule the sequencer plays (kit::stepHits), so what is exported is
// what plays from song position 0 with the same swing, accent, lanes and
// seed. Message thread; allocates.

#include <juce_core/juce_core.h>

#include "engine/KitEngine.h"

#include <array>
#include <vector>

namespace vox::drums {

struct ExportOptions {
    int passes = 1;          // pattern cycles to write (probability differs per pass)
    double bpm = 120.0;      // written as the file's tempo
    int channel = 10;        // 1..16
    int ticksPerQuarter = 960;
};

struct ExportedNote {
    std::int64_t tick = 0, offTick = 0;
    int note = 0, velocity = 0, pad = 0;
};

// The notes of an export, in time order (tick, then lane order): a hit at
// ppq t is a Note On at round(t * ticksPerQuarter); its Note Off is half a
// ratchet step later, or earlier if the same note sounds again before.
// Hits before position 0 are not exported (they do not play from 0).
std::vector<ExportedNote> exportNotes (const kit::Pattern& pattern, const kit::StepContext& context,
                                      const std::array<std::uint8_t, kit::padCount>& notes, const ExportOptions& options);

// SMF format 0, one track: tempo, 4/4, track name, then the notes.
juce::MemoryBlock exportMidi (const kit::Pattern& pattern, const kit::StepContext& context,
                              const std::array<std::uint8_t, kit::padCount>& notes, const ExportOptions& options,
                              const juce::String& trackName);

} // namespace vox::drums
