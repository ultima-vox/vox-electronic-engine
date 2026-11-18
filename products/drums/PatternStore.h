#pragma once

// VOX Drums user patterns (DRM-04): the sequencer plays one of
// `slotCount` editable pattern slots (host parameter drm.seq.pattern). Each
// slot is 16 lanes x 1-4 bars on one grid (meter, resolution, per-beat
// triplet/duplet masks; PR 137 §10) with per-cell velocity, accent,
// probability, ratchet and microtiming in ticks, plus a seed for the
// probability decisions. A new instrument starts with the factory
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
#include <string>
#include <vector>

namespace vox::drums {

inline constexpr preset::Tag sequenceSectionTag = preset::makeTag ("DSEQ");

// PR 137 §10.2: a pattern has one grid (meter, resolution) for all its bars;
// each lane's bar has an alternative-beat mask (triplets, or duplets in
// compound meters). Cells live in fixed storage slots (BarTiming::slot); the
// slots a bar does not use (past its capacity, or reserved by a beat's
// current subdivision) are always empty (a default PatternStep).
struct StoredPattern {
    static constexpr int laneStride = kit::maxPatternBars * kit::slotStride;
    static constexpr int maxCells = kit::padCount * laneStride;
    timing::TimeSignature signature {};    // 4/4
    timing::Resolution resolution = timing::Resolution::sixteenth;
    // §10.5: the pattern's meter follows the host's (the editor and the
    // generator use it for new content); no effect on playback, which always
    // plays from the host's bar lines.
    bool followHost = true;
    std::uint8_t bars = 4;     // 1..4
    std::uint32_t seed = 1;
    std::array<std::uint16_t, kit::padCount * kit::maxPatternBars> masks {};   // lane * 4 + bar
    kit::PatternCells cells {};   // (lane * 4 + bar) * 64 + slot

    kit::PatternStep& cell (int lane, int bar, int slot) noexcept
    {
        return cells[static_cast<std::size_t> (lane * laneStride + bar * kit::slotStride + slot)];
    }
    const kit::PatternStep& cell (int lane, int bar, int slot) const noexcept
    {
        return cells[static_cast<std::size_t> (lane * laneStride + bar * kit::slotStride + slot)];
    }
    std::uint16_t& mask (int lane, int bar) noexcept { return masks[static_cast<std::size_t> (lane * kit::maxPatternBars + bar)]; }
    std::uint16_t mask (int lane, int bar) const noexcept { return masks[static_cast<std::size_t> (lane * kit::maxPatternBars + bar)]; }
    timing::BarTiming timing (int lane, int bar) const noexcept { return { signature, resolution, mask (lane, bar) }; }
    // The cell of (beat, cell) in the lane's bar.
    kit::PatternStep& at (int lane, int bar, int beat, int cellIndex) noexcept
    {
        return cell (lane, bar, timing (lane, bar).slot (beat, cellIndex));
    }
    const kit::PatternStep& at (int lane, int bar, int beat, int cellIndex) const noexcept
    {
        return cell (lane, bar, timing (lane, bar).slot (beat, cellIndex));
    }
    int slotsPerBar() const noexcept { return timing::capacity (signature, resolution).slotsPerBar; }
    // The engine's view of this pattern (points into `cells` and `masks`).
    kit::Pattern view() const noexcept;
    // Changes the bar count: bars beyond the new count are dropped, new bars
    // repeat bar 1 (cells and masks). The grid is kept. Meter and resolution
    // never change here (PR 137 §10.3: they are lossless-or-reject
    // conversions).
    void setBars (int newBars) noexcept;
    // PR 137 §10.3, lossless or rejected (nothing changes; `why` names the
    // place). convertGrid changes meter and/or resolution: every event keeps
    // its tick from its bar start and all its attributes; the masks are kept
    // and must be allowed by the new grid. switchBeats switches the beats in
    // `beats` (bit b) of one lane's bar between straight and alternative.
    // Never StoredPattern-wide resampling: off-grid events are not dropped.
    bool convertGrid (timing::TimeSignature signature, timing::Resolution resolution, std::string* why = nullptr);
    bool switchBeats (int lane, int bar, std::uint16_t beats, std::string* why = nullptr);
    // Every rule of §10.5: a supported grid within 64 slots, masks inside
    // the beats and allowed by the capacity, values in range, |microTicks|
    // within half the cell, ratchet subhits distinct, unused slots empty.
    // The first violation in `why` when given.
    bool valid (std::string* why = nullptr) const;
    bool operator== (const StoredPattern& other) const noexcept;
};

// The cell rules of PR 137 §10.5 for one lane's bar (`slots`: its 64
// storage slots): a valid timing; in the active slots values in range,
// |microTicks| within half the cell and distinct ratchet subhits; every
// other slot empty (a default PatternStep).
bool validLaneBar (const timing::BarTiming& timing, const kit::PatternStep* slots, std::string* why = nullptr);

// The state codec shared by DSEQ and DSNG (little endian).
namespace gridcodec {
void putU8 (std::vector<std::byte>& out, std::uint8_t v);
void putU16 (std::vector<std::byte>& out, std::uint16_t v);
void putU32 (std::vector<std::byte>& out, std::uint32_t v);
struct Reader {
    const std::vector<std::byte>& data;
    std::size_t at = 0;
    bool ok = true;
    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
};
// u8 numerator, u8 denominator, u8 resolution (0 8th .. 3 64th).
void putGrid (std::vector<std::byte>& out, timing::TimeSignature signature, timing::Resolution resolution);
// False for an unsupported meter, an unknown resolution or one over 64 slots.
bool getGrid (Reader& in, timing::TimeSignature& signature, timing::Resolution& resolution);
// A cell is one 32-bit little-endian word (TL decision 6882837766, option B;
// microTicks stays an int16 in memory):
//   bit 0 on, bit 1 accent, bit 2 locked,
//   bits 3-5 ratchet - 1 (1..8),
//   bits 6-12 velocity (0..127),
//   bits 13-19 probability (7 bits; a valid cell holds 1..100),
//   bits 20-28 microTicks, 9-bit two's complement (-256..255; a valid cell
//              holds at most half its cell, <= 240),
//   bits 29-31 reserved, zero.
std::uint32_t packCell (const kit::PatternStep& c) noexcept;
bool unpackCell (std::uint32_t word, kit::PatternStep& c) noexcept;   // false: reserved bits set
void putCell (std::vector<std::byte>& out, const kit::PatternStep& c);
bool getCell (Reader& in, kit::PatternStep& c);   // false on a short read or reserved bits
} // namespace gridcodec

// A StoredPattern is ~33 KB. Code that may run on a host thread with a small
// stack (constructors, state save/restore, editor actions) copies them into
// existing objects or the heap, never into stack temporaries (Windows CI:
// a 1 MB main-thread stack overflowed).

// Factory pattern `index` as a 16-lane slot (lanes 9-16 empty).
void factorySlot (int index, StoredPattern& out) noexcept;
StoredPattern factorySlot (int index) noexcept;   // tests

//==============================================================================
// DRM-05 Song mode (docs/ultima-vox-music/drums/DRM-05_SONG_MODE.md).
//
// A song is a timeline of 16, 32 or 64 bars. Each bar is empty, a
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

// The cells of one unique bar: 16 lanes x 64 slots (lane * 64 + slot) on
// the bar's grid, with each lane's mask. Immutable once it is in a SongData
// (shared between copies and undo entries); `id` is unique per content
// version in this process.
struct BarCells {
    static constexpr int stride = kit::slotStride;
    std::uint64_t id = 0;
    timing::TimeSignature signature {};
    timing::Resolution resolution = timing::Resolution::sixteenth;
    std::uint32_t seed = 1;
    std::array<std::uint16_t, kit::padCount> masks {};
    std::array<kit::PatternStep, kit::padCount * stride> cells {};

    kit::PatternStep& cell (int lane, int slot) noexcept { return cells[static_cast<std::size_t> (lane * stride + slot)]; }
    const kit::PatternStep& cell (int lane, int slot) const noexcept { return cells[static_cast<std::size_t> (lane * stride + slot)]; }
    timing::BarTiming timing (int lane) const noexcept { return { signature, resolution, masks[static_cast<std::size_t> (lane)] }; }
    kit::BarView view() const noexcept;
    bool valid (std::string* why = nullptr) const;   // the rules of StoredPattern::valid for one bar
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

// DSNG v2: u32 version, u8 songMode, u8 length, u8 loopOn, u8 loopStart,
// u8 loopLength, then 64 bars: u8 kind; pattern: u8 slot, u8 variation,
// u8 patternBar; unique: grid (u8 numerator, u8 denominator, u8
// resolution), u32 seed, 16 u16 masks, then 16 lanes x slotsPerBar cells
// (lane-major; the gridcodec cell). Strict: anything out of range, or
// content in a reserved slot, rejects the section.
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
    // slot (A); swapping exchanges A and B. Saved with the slots (DSEQ).
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

    // DSEQ v4: the slots, then their alternates (layout in PatternStore.cpp).
    // Development versions 1-3 are not decoded.
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
        // A generator settings edit ("Style defaults", PR 137 §4): slot = -1;
        // the eight values in style::Param order, applied through setSettingsSink.
        bool hasSettings = false;
        std::array<float, 8> settingsBefore {}, settingsAfter {};
    };
    // Where undo/redo of a role edit goes (the processor's roles).
    void setRoleSink (std::function<void (const PadRoles&)> sink) { roleSink_ = std::move (sink); }
    // Where undo/redo of a settings edit goes (the processor's generator values).
    void setSettingsSink (std::function<void (const std::array<float, 8>&)> sink) { settingsSink_ = std::move (sink); }
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
    std::function<void (const std::array<float, 8>&)> settingsSink_;
};

} // namespace vox::drums
