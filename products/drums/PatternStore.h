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

#include "PadRoles.h"
#include "StateContainer.h"
#include "engine/KitEngine.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
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

// A StoredPattern is ~24 KB. Code that may run on a host thread with a small
// stack (constructors, state save/restore, editor actions) copies them into
// existing objects or the heap, never into stack temporaries (Windows CI:
// a 1 MB main-thread stack overflowed).

// Factory pattern `index` as a 16-lane slot (lanes 9-16 empty).
void factorySlot (int index, StoredPattern& out) noexcept;
StoredPattern factorySlot (int index) noexcept;   // tests

//==============================================================================
// DRM-05 Song mode (docs/ultima-vox-music/drums/DRM-05_SONG_MODE.md).
//
// A song is a timeline of 16, 32 or 64 bars in 4/4. Each bar is empty, a
// reference to one bar of a pattern slot's A or B variation (it follows
// later edits of that pattern), or unique: its own cells, grid and seed
// (Make Unique, or any edit of the bar in the song view). Every bar can be
// unique, so all 64 bars can differ.
//
// A pattern placed on the timeline occupies one bar per pattern bar; bars
// past the end of the song are not placed (the same in playback and
// export). A reference to a pattern bar the pattern no longer has plays
// that index modulo the pattern's bar count.
inline constexpr preset::Tag songSectionTag = preset::makeTag ("DSNG");
inline constexpr int slotCountForSong = kit::factoryPatternCount;

// The cells of one unique bar: 16 lanes x up to 64 steps, lane * 64 + step.
// Immutable once it is in a SongData (shared between copies and undo
// entries); `id` is unique per content version in this process.
struct BarCells {
    static constexpr int stride = kit::maxPatternSteps;
    std::uint64_t id = 0;
    std::uint8_t steps = 16;   // 16, 32 or 64
    std::uint32_t seed = 1;
    std::array<kit::PatternStep, kit::padCount * stride> cells {};

    kit::PatternStep& cell (int lane, int step) noexcept { return cells[static_cast<std::size_t> (lane * stride + step)]; }
    const kit::PatternStep& cell (int lane, int step) const noexcept { return cells[static_cast<std::size_t> (lane * stride + step)]; }
    bool sameContent (const BarCells& other) const noexcept;
    static std::uint64_t nextId() noexcept;
};

struct SongBar {
    enum class Kind : std::uint8_t { empty, pattern, unique };
    Kind kind = Kind::empty;
    std::uint8_t slot = 0;         // pattern: 0..4
    std::uint8_t variation = 0;    // pattern: 0 = A, 1 = B
    std::uint8_t patternBar = 0;   // pattern: 0..3
    std::shared_ptr<const BarCells> cells;   // unique

    bool operator== (const SongBar& other) const noexcept;
};

// Message-thread value: cheap to copy (unique bars are shared).
struct SongData {
    bool songMode = false;         // the sequencer plays the song instead of the selected pattern
    std::uint8_t length = 16;      // 16, 32 or 64 bars
    bool loopOn = false;
    std::uint8_t loopStart = 0, loopLength = 4;   // bars; inside [0, length)
    std::array<SongBar, kit::maxSongBars> bars;

    bool operator== (const SongData& other) const noexcept;
    bool operator!= (const SongData& other) const noexcept { return ! (*this == other); }

    // A new instrument: 16 bars cycling the four bars of pattern slot 1 A.
    static SongData initial();
    // Places pattern `slot`/`variation` (with `patternBars` bars) from
    // `bar` on; bars beyond the song are not placed. Returns bars placed.
    int place (int bar, int slot, int variation, int patternBars);
    void clearBar (int bar) { bars[static_cast<std::size_t> (bar)] = {}; }
    static bool validLength (int length) noexcept { return length == 16 || length == 32 || length == 64; }
    // Brings the song into the form every reader (the audio thread, the
    // state decoder) requires: a valid length (else 16), the loop region
    // inside the song (start clamped, then length), slot, variation and
    // pattern bar in range, and a unique bar with valid cells (else empty).
    // PatternStore applies it to everything published.
    void normalise() noexcept;
};

// The unique cells a reference bar would become (Make Unique), or the
// bar's own cells; null for an empty bar.
std::shared_ptr<const BarCells> resolveBar (const SongBar& bar, const std::array<StoredPattern, slotCountForSong>& slots,
                                           const std::array<StoredPattern, slotCountForSong>& alternates);

// The engine's view of a song: one SongBarView per bar, pointing into
// `slots`, `alternates` and `unique` (which must outlive the view).
// `unique[i]` is the cells of bar i when it is unique.
struct SongView {
    std::array<kit::SongBarView, kit::maxSongBars> views {};
    kit::Song song {};
};
struct SongTable {   // a SongData without ownership (what the audio thread keeps)
    struct Bar { SongBar::Kind kind = SongBar::Kind::empty; std::uint8_t slot = 0, variation = 0, patternBar = 0; };
    std::uint8_t length = 16;
    bool loopOn = false;
    std::uint8_t loopStart = 0, loopLength = 4;
    std::array<Bar, kit::maxSongBars> bars {};
};
void buildSongView (const SongTable& table, const StoredPattern* slots, const StoredPattern* alternates,
                    const BarCells* const* unique, bool applyLoop, SongView& out) noexcept;
SongTable tableOf (const SongData& song) noexcept;

// DSNG v1: u32 version, u8 songMode, u8 length, u8 loopOn, u8 loopStart,
// u8 loopLength, then 64 bars: u8 kind; pattern: u8 slot, u8 variation,
// u8 patternBar; unique: u8 steps, u32 seed, 16 x steps cells (lane-major)
// of u8 flags (on, accent, locked), velocity, probability, ratchet, micro.
// Strict: anything out of range rejects the section.
// Timeline edits (message thread). Each returns the edited copy; `from` and
// `count` are clamped to the song. Bars pushed past the end are dropped.
namespace songedit {
SongData insertEmpty (const SongData& song, int from, int count);                 // shifts the rest right
SongData removeBars (const SongData& song, int from, int count);                  // shifts the rest left
SongData duplicate (const SongData& song, int from, int count);                   // copies the range after itself
SongData clear (const SongData& song, int from, int count);
SongData paste (const SongData& song, int at, const std::vector<SongBar>& bars);  // replaces
std::vector<SongBar> copy (const SongData& song, int from, int count);
// Every pattern bar in the range becomes unique: its own copy of the cells
// it references now (later pattern edits no longer reach it).
SongData makeUnique (const SongData& song, int from, int count, const std::array<StoredPattern, slotCountForSong>& slots,
                     const std::array<StoredPattern, slotCountForSong>& alternates);
// One bar as a one-bar pattern (for the step editor) and back.
void barToPattern (const std::shared_ptr<const BarCells>& bar, StoredPattern& out);
std::shared_ptr<const BarCells> patternToBar (const StoredPattern& pattern);
} // namespace songedit

preset::Section encodeSong (const SongData& song);
bool decodeSong (const preset::Section& section, SongData& out, juce::String& error);

class PatternStore {
public:
    static constexpr int slotCount = kit::factoryPatternCount;

    PatternStore();

    //== message / host threads
    StoredPattern slot (int index) const;   // tests and small callers
    void copySlot (int index, StoredPattern& out) const;
    // The slot and the store generation it belongs to, read together (a
    // view caching `generation` then never holds older cells under a newer
    // generation; review 6745483562).
    void copySlot (int index, StoredPattern& out, std::uint64_t& generation) const;
    void setSlot (int index, const StoredPattern& pattern);
    void setAll (const std::array<StoredPattern, slotCount>& slots);
    void copyAll (std::array<StoredPattern, slotCount>& out) const;

    // DRM-04b: each slot has a second variation (B). The sequencer plays the
    // slot (A); swapping exchanges A and B. Saved with the slots (DSEQ v2).
    StoredPattern alternate (int index) const;   // tests
    void copyAlternate (int index, StoredPattern& out) const;
    void setAlternate (int index, const StoredPattern& pattern);
    void copyAlternates (std::array<StoredPattern, slotCount>& out) const;
    void setAlternates (const std::array<StoredPattern, slotCount>& alternates);
    // DSEQ of the current slots and alternates, encoded under the lock (no
    // copy of the 10 patterns).
    preset::Section encodeCurrent() const;
    // Changes after the change it counts is committed (under the lock).
    std::uint64_t generation() const noexcept { return generation_.load(); }

    //== DRM-05 song (message / host threads)
    SongData song() const;
    // Song bar `bar` as it plays now (a reference resolved, empty: 16 empty
    // steps) as a one-bar pattern, with the generation it belongs to.
    void copySongBar (int bar, StoredPattern& out, std::uint64_t& generation) const;
    void setSong (const SongData& song);
    // The slots, alternates and song as they are together (one lock).
    struct Everything {
        std::array<StoredPattern, slotCount> slots, alternates;
        SongData song;
    };
    void copyEverything (Everything& out) const;
    void setEverything (const Everything& in);
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
    static void factorySlots (std::array<StoredPattern, slotCount>& out);

    //== audio thread (wait-free)
    // Copies slot `index` into `live` if it changed or `index` differs from
    // what `live` holds and the lock is free. Returns the pattern to play.
    struct Live {
        // Allocated once with the processor; pull() copies into it.
        std::unique_ptr<StoredPattern> pattern = std::make_unique<StoredPattern>();
        int index = -1;
        std::uint64_t generation = 0;
        kit::Pattern view {};
    };
    const kit::Pattern& pull (int index, Live& live) noexcept;

    // Song mode: the audio thread's own copy of the slots, alternates and
    // unique bars (allocated once, ~0.7 MB), refreshed with tryEnter for
    // what changed. Returns the song to play, or null in Pattern mode.
    struct SongLive {
        SongLive();
        std::unique_ptr<std::array<StoredPattern, slotCount>> slots, alternates;
        std::unique_ptr<std::array<BarCells, kit::maxSongBars>> unique;
        std::array<const BarCells*, kit::maxSongBars> uniquePointers {};
        std::array<std::uint64_t, slotCount> slotGeneration {}, alternateGeneration {};
        std::uint64_t songGeneration = 0;
        bool anySlotCopied = false;
        SongTable table;
        bool songMode = false;
        SongView view;
    };
    const kit::Song* pullSong (SongLive& live) noexcept;

private:
    mutable juce::SpinLock lock_;
    // On the heap (~250 KB): an object holding a PatternStore (the
    // processor) stays small.
    struct Data {
        std::array<StoredPattern, slotCount> slots, alternates;
        SongData song = SongData::initial();
    };
    std::unique_ptr<Data> data_ = std::make_unique<Data>();
    std::array<StoredPattern, slotCount>& slots_ = data_->slots;
    std::array<StoredPattern, slotCount>& alternates_ = data_->alternates;
    std::array<std::uint64_t, slotCount> slotGeneration_ {};
    std::array<std::uint64_t, slotCount> alternateGeneration_ {};
    SongData& song_ = data_->song;          // lock_
    std::uint64_t songGeneration_ = 1;      // lock_
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
        // DRM-05: a song edit (slot = -1 when no pattern slot changed), or
        // everything (Generate Song: all slots, variations and the song).
        bool hasSong = false;
        SongData songBefore, songAfter;
        std::unique_ptr<PatternStore::Everything> allBefore, allAfter;
        // A pad role edit (issue 132): slot = -1, applied through setRoleSink.
        bool hasRoles = false;
        PadRoles rolesBefore, rolesAfter;
    };
    // Where undo/redo of a role edit goes (the processor's roles).
    void setRoleSink (std::function<void (const PadRoles&)> sink) { roleSink_ = std::move (sink); }
    void push (std::unique_ptr<Entry> entry);
    // Replaces the newest entry's result (one knob drag = one undo step);
    // false if there is none for `slot` or it can be redone instead.
    bool amendLast (int slot, const StoredPattern& after);
    bool amendLastSong (const SongData& after);   // the same for a song edit
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
    std::vector<std::unique_ptr<Entry>> undo_, redo_;
    std::function<void (const PadRoles&)> roleSink_;
};

} // namespace vox::drums
