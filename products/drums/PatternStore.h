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
#include <functional>
#include <vector>

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
    // The slot and the store generation it belongs to, read together (a
    // view caching `generation` then never holds older cells under a newer
    // generation; review 6745483562).
    StoredPattern slot (int index, std::uint64_t& generation) const;
    void setSlot (int index, const StoredPattern& pattern);
    void setAll (const std::array<StoredPattern, slotCount>& slots);
    std::array<StoredPattern, slotCount> all() const;

    // DRM-04b: each slot has a second variation (B). The sequencer plays the
    // slot (A); swapping exchanges A and B. Saved with the slots (DSEQ v2).
    StoredPattern alternate (int index) const;
    void setAlternate (int index, const StoredPattern& pattern);
    std::array<StoredPattern, slotCount> alternates() const;
    void setAlternates (const std::array<StoredPattern, slotCount>& alternates);
    // Changes after the change it counts is committed (under the lock).
    std::uint64_t generation() const noexcept { return generation_.load(); }
    // Tests: runs at the start of setSlot/setAll, before anything is
    // committed (message thread only).
    std::function<void()> beforeCommitForTesting;

    // DSEQ v2: the slots, then their alternates. v1 (DRM-04a) has no
    // alternates: each reads as a copy of its slot.
    static preset::Section encode (const std::array<StoredPattern, slotCount>& slots,
                                   const std::array<StoredPattern, slotCount>& alternates);
    // Strict: an unknown version, a wrong size or slot count, or any value
    // outside its range rejects the section (error set, false returned).
    static bool decode (const preset::Section& section, std::array<StoredPattern, slotCount>& out,
                        std::array<StoredPattern, slotCount>& alternates, juce::String& error);
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
    std::array<StoredPattern, slotCount> alternates_;
    std::array<std::uint64_t, slotCount> slotGeneration_ {};
    std::atomic<std::uint64_t> generation_ { 1 };
};

// Undo/redo of pattern edits (message thread). Each entry is a whole
// slot before and after one edit; at most `capacity` entries are kept.
class PatternHistory {
public:
    static constexpr std::size_t capacity = 100;
    struct Entry {
        int slot = 0;
        bool alternate = false;   // the entry also swapped A and B
        StoredPattern before, after;
        StoredPattern beforeB, afterB;
    };
    void push (Entry entry);
    // Replaces the newest entry's result (one knob drag = one undo step);
    // false if there is none for `slot` or it can be redone instead.
    bool amendLast (int slot, const StoredPattern& after);
    bool canUndo() const noexcept { return ! undo_.empty(); }
    bool canRedo() const noexcept { return ! redo_.empty(); }
    // Applies to `store` and returns the slot it changed (-1 if nothing).
    int undo (PatternStore& store);
    int redo (PatternStore& store);
    void clear() noexcept
    {
        undo_.clear();
        redo_.clear();
    }
    std::size_t size() const noexcept { return undo_.size(); }

private:
    std::vector<Entry> undo_, redo_;
};

} // namespace vox::drums
