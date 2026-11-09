#pragma once

// VOX Drums user patterns (DRM-04): the sequencer plays one of
// `slotCount` editable pattern slots (host parameter drm.seq.pattern). Each
// slot is 16 lanes x 16/32/64 steps per bar x 1-4 bars with per-step
// velocity, accent, probability, ratchet and microtiming, plus a seed for
// the probability decisions. A new instrument starts with the factory
// patterns in the slots. Saved as the versioned state section DSEQ.
//
// Threads: the message/host threads edit under a SpinLock held only for a
// copy. The audio thread pull()s the selected slot with tryEnter when it
// changed (or another slot is selected): it never waits; if the lock is
// busy it keeps playing what it has and takes the change on a later block.

#include <juce_core/juce_core.h>

#include "StateContainer.h"
#include "engine/KitEngine.h"

#include <array>
#include <atomic>

namespace vox::drums {

inline constexpr preset::Tag sequenceSectionTag = preset::makeTag ("DSEQ");

struct StoredPattern {
    static constexpr int maxCells = kit::padCount * kit::maxPatternSteps * kit::maxPatternBars;
    std::uint8_t steps = 16;   // per bar: 16, 32 or 64
    std::uint8_t bars = 4;     // 1..4
    std::uint32_t seed = 1;
    std::array<kit::PatternStep, maxCells> cells {};   // (lane * bars + bar) * steps + step

    kit::PatternStep& cell (int lane, int bar, int step) noexcept
    {
        return cells[static_cast<std::size_t> ((lane * bars + bar) * steps + step)];
    }
    const kit::PatternStep& cell (int lane, int bar, int step) const noexcept
    {
        return cells[static_cast<std::size_t> ((lane * bars + bar) * steps + step)];
    }
    // The engine's view of this pattern (points into `cells`).
    kit::Pattern view() const noexcept;
    // Changes steps/bars keeping what fits: steps scale on the grid (a
    // 16-step cell lands on the matching 32/64-step position), bars beyond
    // the new count are dropped, new bars repeat bar 1.
    void resize (int newSteps, int newBars) noexcept;
    bool operator== (const StoredPattern& other) const noexcept;
};

// Factory pattern `index` as a 16-lane slot (lanes 9-16 empty).
StoredPattern factorySlot (int index) noexcept;

class PatternStore {
public:
    static constexpr int slotCount = kit::factoryPatternCount;

    PatternStore();

    //== message / host threads
    StoredPattern slot (int index) const;
    void setSlot (int index, const StoredPattern& pattern);
    void setAll (const std::array<StoredPattern, slotCount>& slots);
    std::array<StoredPattern, slotCount> all() const;
    std::uint64_t generation() const noexcept { return generation_.load(); }

    static preset::Section encode (const std::array<StoredPattern, slotCount>& slots);
    // Strict: an unknown version, a wrong size or slot count, or any value
    // outside its range rejects the section (error set, false returned).
    static bool decode (const preset::Section& section, std::array<StoredPattern, slotCount>& out, juce::String& error);
    static std::array<StoredPattern, slotCount> factorySlots();

    //== audio thread (wait-free)
    // Copies slot `index` into `live` if it changed or `index` differs from
    // what `live` holds and the lock is free. Returns the pattern to play.
    struct Live {
        StoredPattern pattern;
        int index = -1;
        std::uint64_t generation = 0;
        kit::Pattern view {};
    };
    const kit::Pattern& pull (int index, Live& live) noexcept;

private:
    mutable juce::SpinLock lock_;
    std::array<StoredPattern, slotCount> slots_;
    std::array<std::uint64_t, slotCount> slotGeneration_ {};
    std::atomic<std::uint64_t> generation_ { 1 };
};

} // namespace vox::drums
