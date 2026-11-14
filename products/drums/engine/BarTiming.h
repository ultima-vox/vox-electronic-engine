#pragma once

// VOX Drums shared bar timing (PR 137 §10, revision 6; AG review 5906187802).
//
// ONE module maps meter, resolution and per-beat subdivision to exact
// integer ticks on the canonical 960-PPQ timeline. The scheduler, the
// generator, the editor and pattern/song MIDI export all take their event
// times from here; none reconstructs timing on its own.
//
//  - A bar has `beats` beats: quarter notes (n/4), dotted quarters (6/8,
//    9/8, 12/8) or eighths (other n/8).
//  - Each beat of a lane is straight or alternative (a triplet in quarter
//    and eighth beats, a duplet in dotted beats). Slots per beat =
//    max(straight, alternative) at the resolution; a bar has at most 64.
//  - A cell starts at an integer tick; an event sounds at cell start +
//    microTicks (|microTicks| <= half a cell).
//  - Ratchet subhits are mapped once to whole ticks (nearest, exact ties to
//    the earlier tick) by subhitTick(); playback and export use it alike.
//
// Pure functions and plain values: no allocation, no locks, no I/O. Safe on
// the audio thread.

#include <array>
#include <cstdint>

namespace vox::drums::timing {

inline constexpr int ticksPerQuarter = 960;
inline constexpr int maxSlotsPerBar = 64;
inline constexpr int maxBeats = 16;

struct TimeSignature {
    int numerator = 4, denominator = 4;
    bool operator== (const TimeSignature& o) const noexcept { return numerator == o.numerator && denominator == o.denominator; }
};

enum class BeatKind : std::uint8_t { quarter, dottedQuarter, eighth };
enum class Resolution : std::uint8_t { eighth, sixteenth, thirtySecond, sixtyFourth };
inline constexpr int resolutionCount = 4;

// Supported: denominator 4 or 8, numerator 1..16 (n/8 with n in {6, 9, 12, 15}
// is compound). False otherwise.
bool supported (TimeSignature) noexcept;
BeatKind beatKind (TimeSignature) noexcept;
int beatCount (TimeSignature) noexcept;
int beatTicks (BeatKind) noexcept;   // 960, 1440, 480

// Cells per beat; 0 when the alternative does not exist at that resolution
// (an eighth beat has no triplet at the 8th resolution).
int straightCells (BeatKind, Resolution) noexcept;
int alternativeCells (BeatKind, Resolution) noexcept;

enum class Availability : std::uint8_t { full, straightOnly, unavailable };
struct Capacity {
    Availability availability = Availability::unavailable;
    int slotsPerBeat = 0;   // as stored (max of straight and alternative when full, straight otherwise)
    int slotsPerBar = 0;
};
// What a (meter, resolution) offers within 64 slots per bar (§10.2).
Capacity capacity (TimeSignature, Resolution) noexcept;
// The user-visible reason for a limitation, "" when fully available.
const char* limitationReason (TimeSignature, Resolution) noexcept;

// One lane's bar: which beats use the alternative subdivision.
struct BarTiming {
    TimeSignature signature;
    Resolution resolution = Resolution::sixteenth;
    std::uint16_t alternativeBeats = 0;   // bit b: beat b uses the alternative

    bool valid() const noexcept;          // supported, available, mask within the beats and allowed
    int beats() const noexcept;
    int barTicks() const noexcept;
    int slotsPerBeat() const noexcept;
    int cells (int beat) const noexcept;  // active cells of `beat`
    int cellTicks (int beat) const noexcept;
    int cellStartTick (int beat, int cell) const noexcept;   // from the bar start
    int slot (int beat, int cell) const noexcept;            // storage slot 0..63
    bool slotActive (int slot) const noexcept;               // false for reserved (inactive) slots
};

// The tick of ratchet subhit `index` (0..count-1) of a cell starting at
// `cellStart` that lasts `cellTicks`: cellStart + index * cellTicks / count,
// rounded to the nearest whole tick, an exact tie to the earlier one.
int subhitTick (int cellStart, int cellTicks, int index, int count) noexcept;
// True when all `count` subhits of a cell of `cellTicks` fall on distinct ticks.
bool subhitsDistinct (int cellTicks, int count) noexcept;

// The sample offset in a block of an event at `eventTick` (absolute, from
// song position 0): floor ((eventTick / 960 - blockStartPpq) * samplesPerQuarter).
std::int64_t sampleOffset (std::int64_t eventTick, double blockStartPpq, double samplesPerQuarter) noexcept;

//==============================================================================
// Switching beats between straight and alternative (§10.3): each event keeps
// its absolute tick; it moves to the cell whose start is nearest (an exact
// tie to the earlier cell) with microTicks = absolute - new cell start. The
// switch is rejected, changing nothing, on a collision, a |microTicks| over
// half the new cell, or an invalid result.
struct LaneEvent {
    bool on = false;
    std::int16_t microTicks = 0;
    bool operator== (const LaneEvent&) const = default;
};
using LaneBar = std::array<LaneEvent, maxSlotsPerBar>;   // indexed by slot

enum class SwitchResult : std::uint8_t { ok, invalidTiming, collision, offsetTooLarge, reservedSlotNotEmpty };
struct SwitchOutcome {
    SwitchResult result = SwitchResult::ok;
    int beat = -1, cell = -1;   // where the rejection happened (in `from`)
};
// Moves the events of `bar` from timing `from` to `to` (same signature and
// resolution; only alternativeBeats differ). On success `bar` holds the new
// layout; on failure it is untouched. `extra` (same indexing) is moved with
// its event (velocity, lock, ratchet... of the caller's cells); may be null.
template <typename Extra>
SwitchOutcome switchSubdivision (const BarTiming& from, const BarTiming& to, LaneBar& bar, std::array<Extra, maxSlotsPerBar>* extra) noexcept;

} // namespace vox::drums::timing

#include "BarTiming.inl"
