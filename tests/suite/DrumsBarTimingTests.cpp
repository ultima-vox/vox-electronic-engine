// VOX Drums shared bar timing (PR 137 §10 revision 6):
//
//  - the capacity table of §10.2 exactly, and an exhaustive sweep of every
//    supported time signature and resolution: integer cell ticks, strictly
//    increasing cell starts, at most 64 slots, reserved slots inactive;
//  - ratchet subhits rounded once to the nearest tick, exact ties earlier
//    (40/3 and 30/4 cases), distinct for every cell length and count 1..8;
//  - the tick-to-sample conversion;
//  - subdivision switches: absolute ticks and attributes kept, the TL tie
//    example (a reverse switch may choose another cell), compound duplets,
//    collision / offset / reserved-slot / invalid rejections that change
//    nothing, and a randomized property test.
#include "engine/BarTiming.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>

namespace {

using namespace vox::drums::timing;

int failures = 0;

void check (bool condition, const std::string& what)
{
    if (! condition)
    {
        std::printf ("FAIL [VOX Drums bar timing]: %s\n", what.c_str());
        ++failures;
    }
}

std::string name (TimeSignature s, Resolution r)
{
    static const char* res[4] = { "8th", "16th", "32nd", "64th" };
    return std::to_string (s.numerator) + "/" + std::to_string (s.denominator) + " " + res[static_cast<int> (r)];
}

void testCapacityTable()
{
    struct Row { TimeSignature s; Resolution r; Availability a; int slots; };
    using A = Availability;
    using R = Resolution;
    const Row rows[] = {
        { { 4, 4 }, R::eighth, A::full, 12 }, { { 4, 4 }, R::sixteenth, A::full, 24 }, { { 4, 4 }, R::thirtySecond, A::full, 48 },
        { { 4, 4 }, R::sixtyFourth, A::straightOnly, 64 },
        { { 3, 4 }, R::eighth, A::full, 9 }, { { 3, 4 }, R::sixteenth, A::full, 18 }, { { 3, 4 }, R::thirtySecond, A::full, 36 },
        { { 3, 4 }, R::sixtyFourth, A::straightOnly, 48 },
        { { 6, 8 }, R::eighth, A::full, 6 }, { { 6, 8 }, R::sixteenth, A::full, 12 }, { { 6, 8 }, R::thirtySecond, A::full, 24 },
        { { 6, 8 }, R::sixtyFourth, A::full, 48 },
        { { 12, 8 }, R::eighth, A::full, 12 }, { { 12, 8 }, R::sixteenth, A::full, 24 }, { { 12, 8 }, R::thirtySecond, A::full, 48 },
        { { 12, 8 }, R::sixtyFourth, A::unavailable, 0 },
        { { 7, 8 }, R::eighth, A::straightOnly, 7 }, { { 7, 8 }, R::sixteenth, A::full, 21 }, { { 7, 8 }, R::thirtySecond, A::full, 42 },
        { { 7, 8 }, R::sixtyFourth, A::straightOnly, 56 },
    };
    for (const auto& row : rows)
    {
        const auto c = capacity (row.s, row.r);
        check (c.availability == row.a && c.slotsPerBar == row.slots,
               "capacity " + name (row.s, row.r) + ": " + std::to_string (c.slotsPerBar) + " slots, availability " + std::to_string (static_cast<int> (c.availability)));
        check ((std::string (limitationReason (row.s, row.r)).empty()) == (row.a == A::full), "limitation reason shown exactly when limited: " + name (row.s, row.r));
    }
    check (! supported ({ 5, 2 }) && ! supported ({ 17, 4 }) && ! supported ({ 0, 4 }), "unsupported signatures are refused");
}

void testSweep()
{
    int combos = 0;
    for (int d : { 4, 8 })
        for (int n = 1; n <= maxBeats; ++n)
            for (int ri = 0; ri < resolutionCount; ++ri)
            {
                const TimeSignature s { n, d };
                const auto r = static_cast<Resolution> (ri);
                const auto cap = capacity (s, r);
                if (cap.availability == Availability::unavailable)
                    continue;
                const int beats = beatCount (s);
                const std::uint16_t all = static_cast<std::uint16_t> ((1u << beats) - 1u);
                for (std::uint16_t mask : { std::uint16_t (0), all, std::uint16_t (0x5555u & all) })
                {
                    BarTiming t { s, r, cap.availability == Availability::full ? mask : std::uint16_t (0) };
                    if (! t.valid())
                    {
                        check (false, "valid timing " + name (s, r));
                        continue;
                    }
                    ++combos;
                    int previous = -1, active = 0;
                    bool ok = t.barTicks() == beats * beatTicks (beatKind (s));
                    for (int b = 0; b < beats; ++b)
                    {
                        ok = ok && t.cells (b) > 0 && t.cells (b) <= t.slotsPerBeat() && beatTicks (beatKind (s)) % t.cells (b) == 0;
                        for (int c = 0; c < t.cells (b); ++c)
                        {
                            ok = ok && t.cellStartTick (b, c) > previous && t.slot (b, c) < maxSlotsPerBar && t.slotActive (t.slot (b, c));
                            previous = t.cellStartTick (b, c);
                            ++active;
                        }
                    }
                    ok = ok && previous < t.barTicks();
                    int counted = 0;
                    for (int sl = 0; sl < maxSlotsPerBar; ++sl)
                        counted += t.slotActive (sl) ? 1 : 0;
                    check (ok && counted == active, "sweep " + name (s, r) + " mask " + std::to_string (mask));
                }
            }
    std::printf ("bar timing: %d (meter, resolution, mask) layouts checked\n", combos);
    // Mask outside the beats, or alternatives where only straight is allowed: invalid.
    check (! BarTiming { { 3, 4 }, Resolution::sixteenth, 0x0008u }.valid(), "a mask bit beyond the beats is invalid");
    check (! BarTiming { { 4, 4 }, Resolution::sixtyFourth, 0x0001u }.valid(), "triplets at 4/4 64th are invalid (straight only)");
    check (! BarTiming { { 12, 8 }, Resolution::sixtyFourth, 0 }.valid(), "12/8 64th is unavailable");
}

void testSubhits()
{
    check (subhitTick (0, 40, 0, 3) == 0 && subhitTick (0, 40, 1, 3) == 13 && subhitTick (0, 40, 2, 3) == 27, "ratchet: 40/3 -> 0, 13, 27");
    check (subhitTick (100, 30, 1, 4) == 107 && subhitTick (100, 30, 2, 4) == 115 && subhitTick (100, 30, 3, 4) == 122,
           "ratchet: exact ties go to the earlier tick (7.5 -> 7, 22.5 -> 22)");
    bool distinct = true;
    for (int cellTicks : { 30, 40, 60, 80, 90, 120, 160, 180, 240, 320, 360, 480 })
        for (int count = 1; count <= 8; ++count)
            distinct = distinct && subhitsDistinct (cellTicks, count);
    check (distinct, "ratchet: every supported cell length gives distinct subhits for 1..8");
}

// Tick -> sample: the first sample at or after the event (the legacy
// ceil (x - 1e-6), TL decision 6882837766), for integer-tick and swung
// (fractional) times, at 44.1/48/96 kHz and blocks of 64/333/512.
void testSamples()
{
    check (sampleOffset (1.0, 0.5, 22050.0) == 11025, "samples: an exact sample (tick 960 from ppq 0.5 at 22050 per quarter)");
    check (sampleOffset (1000.0 / 960.0, 1.0, 24000.0) == 1000, "samples: 40 ticks at 24000 per quarter (exact up to rounding error)");
    check (sampleOffset (959.0 / 960.0, 1.0, 44100.0) == -45, "samples: before the block start: negative, the first sample at or after (-45.94 -> -45)");
    check (sampleOffset (0.5 + 0.3 / 44100.0, 0.5, 44100.0) == 1, "samples: a fractional position plays on the next sample");
    check (sampleOffset (0.5 + 0.5e-6 / 44100.0, 0.5, 44100.0) == 0, "samples: within 1e-6 of a sample it plays on that sample");
    check (sampleOffset (0.5 + 2.0e-6 / 44100.0, 0.5, 44100.0) == 1, "samples: 2e-6 past a sample it plays on the next");
    // A sweep: every event of a 1-bar 16th grid (swing 0.21) lands in exactly
    // one block, on the sample the formula gives, whatever the block size.
    bool partition = true;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int block : { 64, 333, 512 })
        {
            const double spq = 60.0 * rate / 137.0;
            const BarTiming t { {}, Resolution::sixteenth, 0 };
            for (int beat = 0; beat < 4; ++beat)
                for (int cell = 0; cell < 4; ++cell)
                {
                    const double ppq = t.cellStartTick (beat, cell) / 960.0 + swingDelay (t, beat, cell, 0.21);
                    const auto global = sampleOffset (ppq, 0.0, spq);
                    int owners = 0;
                    for (std::int64_t start = 0; start < static_cast<std::int64_t> (4 * spq) + block; start += block)
                    {
                        const auto at = sampleOffset (ppq, static_cast<double> (start) / spq, spq);
                        if (at >= 0 && at < block)
                        {
                            ++owners;
                            partition = partition && std::llabs (start + at - global) <= 1;   // block-start rounding
                        }
                    }
                    partition = partition && owners == 1;
                }
        }
    check (partition, "samples: each event lands in exactly one block at 44.1/48/96 kHz x 64/333/512");
}

// Swing: odd straight cells by swing x cell, the phase restarting every bar
// line; tuplet cells never; exportTick rounds once, ties earlier.
void testSwing()
{
    const BarTiming s16 { {}, Resolution::sixteenth, 0 };
    check (swingDelay (s16, 0, 0, 0.21) == 0.0 && swingDelay (s16, 0, 1, 0.21) == 0.21 * 0.25 && swingDelay (s16, 3, 3, 0.5) == 0.125,
           "swing: 4/4 16ths, odd cells late by swing x 1/16 (the legacy value exactly)");
    const BarTiming s32 { {}, Resolution::thirtySecond, 0b0100 };
    check (swingDelay (s32, 0, 1, 0.5) == 0.5 * 0.125 && swingDelay (s32, 2, 1, 0.5) == 0.0 && swingDelay (s32, 2, 0, 0.5) == 0.0,
           "swing: 32nds swing odd 32nds; a triplet beat is never swung");
    const BarTiming s64 { {}, Resolution::sixtyFourth, 0 };
    check (swingDelay (s64, 1, 15, 0.21) == 0.21 * 0.0625, "swing: 64ths");
    const BarTiming three { { 3, 4 }, Resolution::sixteenth, 0 };
    check (swingDelay (three, 2, 3, 0.5) == 0.125 && swingDelay (three, 2, 2, 0.5) == 0.0, "swing: 3/4 keeps the phase per beat");
    const BarTiming six { { 6, 8 }, Resolution::sixteenth, 0b10 };
    // 6/8 at 16ths: 6 cells per dotted beat (even), so the phase is the cell's.
    check (swingDelay (six, 0, 1, 0.5) == 0.5 * 0.25 && swingDelay (six, 0, 2, 0.5) == 0.0 && swingDelay (six, 1, 1, 0.5) == 0.0,
           "swing: 6/8 straight beat swings odd 16ths; its duplet beat does not");
    const BarTiming seven { { 7, 8 }, Resolution::eighth, 0 };
    check (swingDelay (seven, 1, 0, 0.5) == 0.5 * 0.5 && swingDelay (seven, 2, 0, 0.5) == 0.0,
           "swing: one cell per beat (7/8 at 8ths): the bar's straight index decides (odd eighths)");
    check (swingDelay (s16, 0, 1, 0.0) == 0.0, "swing 0: no delay");
    // Exact ties with exactly representable values (2 ticks per quarter).
    check (exportTick (0.25, 2) == 0 && exportTick (0.75, 2) == 1 && exportTick (-0.25, 2) == -1,
           "exportTick: an exact tie goes to the earlier tick");
    check (exportTick (50.4 / 960.0, 960) == 50 && exportTick (50.6 / 960.0, 960) == 51 && exportTick (1000.0 / 960.0, 960) == 1000
               && exportTick (0.25 + 0.21 * 0.25, 960) == 290,
           "exportTick: nearest tick; whole ticks unchanged; swing 0.21 on a 16th: 240 + 50.4 -> 290");
}

// Absolute event ticks (with the extra value) of a lane bar.
std::map<int, int> absolutes (const BarTiming& t, const LaneBar& bar, const std::array<int, maxSlotsPerBar>& extra)
{
    std::map<int, int> out;
    for (int b = 0; b < t.beats(); ++b)
        for (int c = 0; c < t.cells (b); ++c)
        {
            const auto s = static_cast<std::size_t> (t.slot (b, c));
            if (bar[s].on)
                out[t.cellStartTick (b, c) + bar[s].microTicks] = extra[s];
        }
    return out;
}

void testSwitch()
{
    using std::array;
    const BarTiming straight { { 4, 4 }, Resolution::sixteenth, 0 };
    const BarTiming beat1Triplet { { 4, 4 }, Resolution::sixteenth, 0x0002u };
    const BarTiming beat0Triplet { { 4, 4 }, Resolution::sixteenth, 0x0001u };
    // Four straight 16ths of beat 2 -> triplets -> straight again: same ticks.
    {
        LaneBar bar {};
        array<int, maxSlotsPerBar> extra {};
        for (int c = 0; c < 4; ++c)
        {
            bar[static_cast<std::size_t> (straight.slot (1, c))] = { true, 0 };
            extra[static_cast<std::size_t> (straight.slot (1, c))] = 100 + c;
        }
        const auto before = absolutes (straight, bar, extra);
        auto out = switchSubdivision (straight, beat1Triplet, bar, &extra);
        check (out.result == SwitchResult::ok && absolutes (beat1Triplet, bar, extra) == before, "switch: 16ths to 16th triplets keep ticks and attributes");
        out = switchSubdivision (beat1Triplet, straight, bar, &extra);
        check (out.result == SwitchResult::ok && absolutes (straight, bar, extra) == before, "switch: and back again");
    }
    // The TL example: tick 480 - 120 = 360; triplet 320 + 40; back to 240 + 120.
    {
        LaneBar bar {};
        bar[static_cast<std::size_t> (straight.slot (0, 2))] = { true, -120 };
        check (switchSubdivision<int> (straight, beat0Triplet, bar, nullptr).result == SwitchResult::ok
                   && bar[static_cast<std::size_t> (beat0Triplet.slot (0, 2))].on && bar[static_cast<std::size_t> (beat0Triplet.slot (0, 2))].microTicks == 40,
               "switch: tick 360 becomes triplet cell 3 (320) + 40");
        check (switchSubdivision<int> (beat0Triplet, straight, bar, nullptr).result == SwitchResult::ok
                   && bar[static_cast<std::size_t> (straight.slot (0, 1))].on && bar[static_cast<std::size_t> (straight.slot (0, 1))].microTicks == 120,
               "switch: back to straight, the exact tie goes to the earlier cell (240 + 120): same tick, other cell");
    }
    // A collision: two events that land in one cell. Nothing changes.
    {
        LaneBar bar {};
        bar[static_cast<std::size_t> (straight.slot (0, 1))] = { true, -60 };   // 180
        bar[static_cast<std::size_t> (straight.slot (0, 0))] = { true, 110 };   // 110
        const auto keep = bar;
        const auto out = switchSubdivision<int> (straight, beat0Triplet, bar, nullptr);
        check (out.result == SwitchResult::collision && bar == keep, "switch: a collision is rejected and nothing changes");
    }
    // An offset that would exceed half the new cell: rejected.
    {
        LaneBar bar {};
        bar[static_cast<std::size_t> (straight.slot (1, 0))] = { true, -110 };   // 850, near the end of straight beat 0
        const auto keep = bar;
        const auto out = switchSubdivision<int> (straight, beat1Triplet, bar, nullptr);
        check (out.result == SwitchResult::offsetTooLarge && bar == keep, "switch: an offset over half the new cell is rejected and nothing changes");
    }
    // Reserved slots must be empty; invalid timings are refused.
    {
        LaneBar bar {};
        bar[static_cast<std::size_t> (beat0Triplet.slot (0, 5))] = { true, 0 };   // triplet cell 6 of beat 1 is a reserved slot in straight
        const auto keep = bar;
        check (switchSubdivision<int> (straight, beat0Triplet, bar, nullptr).result == SwitchResult::reservedSlotNotEmpty && bar == keep,
               "switch: content in a reserved slot is rejected");
        check (switchSubdivision<int> (straight, BarTiming { { 3, 4 }, Resolution::sixteenth, 0 }, bar, nullptr).result == SwitchResult::invalidTiming
                   && bar == keep,
               "switch: another signature is refused");
    }
    // Compound duplets: 12/8 16th, dotted beat of 6 cells <-> 4 cells.
    {
        const BarTiming s128 { { 12, 8 }, Resolution::sixteenth, 0 };
        const BarTiming d128 { { 12, 8 }, Resolution::sixteenth, 0x000Fu };
        LaneBar bar {};
        array<int, maxSlotsPerBar> extra {};
        for (int b = 0; b < 4; ++b)
        {
            bar[static_cast<std::size_t> (s128.slot (b, 0))] = { true, 0 };
            bar[static_cast<std::size_t> (s128.slot (b, 3))] = { true, 0 };
            extra[static_cast<std::size_t> (s128.slot (b, 3))] = b + 1;
        }
        const auto before = absolutes (s128, bar, extra);
        check (switchSubdivision (s128, d128, bar, &extra).result == SwitchResult::ok && absolutes (d128, bar, extra) == before
                   && switchSubdivision (d128, s128, bar, &extra).result == SwitchResult::ok && absolutes (s128, bar, extra) == before,
               "switch: 12/8 straight <-> duplets keep ticks both ways");
    }
    // Property: random humanised bars and random switches keep every absolute
    // tick and attribute, or change nothing.
    std::mt19937 random (1013);
    int accepted = 0, rejected = 0;
    const TimeSignature sigs[] = { { 4, 4 }, { 3, 4 }, { 6, 8 }, { 12, 8 }, { 7, 8 }, { 5, 4 } };
    for (int trial = 0; trial < 20000; ++trial)
    {
        const auto sig = sigs[random() % 6];
        const auto res = static_cast<Resolution> (random() % 3);   // 8th..32nd: alternatives available
        const auto cap = capacity (sig, res);
        if (cap.availability != Availability::full)
            continue;
        const auto beatsMask = static_cast<std::uint16_t> ((1u << beatCount (sig)) - 1u);
        const BarTiming from { sig, res, static_cast<std::uint16_t> (random() & beatsMask) };
        const BarTiming to { sig, res, static_cast<std::uint16_t> (random() & beatsMask) };
        LaneBar bar {};
        array<int, maxSlotsPerBar> extra {};
        for (int b = 0; b < from.beats(); ++b)
            for (int c = 0; c < from.cells (b); ++c)
                if (random() % 3 == 0)
                {
                    const int half = from.cellTicks (b) / 2;
                    const auto s = static_cast<std::size_t> (from.slot (b, c));
                    bar[s] = { true, static_cast<std::int16_t> (random() % 4 == 0 ? static_cast<int> (random() % static_cast<unsigned> (2 * half + 1)) - half : 0) };
                    extra[s] = static_cast<int> (s) * 7 + 1;
                }
        const auto beforeBar = bar;
        const auto beforeExtra = extra;
        const auto ticks = absolutes (from, bar, extra);
        const auto out = switchSubdivision (from, to, bar, &extra);
        if (out.result == SwitchResult::ok)
        {
            ++accepted;
            bool bounded = true;
            for (int b = 0; b < to.beats(); ++b)
                for (int c = 0; c < to.cells (b); ++c)
                {
                    const auto& e = bar[static_cast<std::size_t> (to.slot (b, c))];
                    bounded = bounded && (! e.on || 2 * std::abs (static_cast<int> (e.microTicks)) <= to.cellTicks (b));
                }
            bool reservedEmpty = true;
            for (int sl = 0; sl < maxSlotsPerBar; ++sl)
                reservedEmpty = reservedEmpty && (to.slotActive (sl) || ! bar[static_cast<std::size_t> (sl)].on);
            if (! (absolutes (to, bar, extra) == ticks && bounded && reservedEmpty))
            {
                check (false, "switch property: trial " + std::to_string (trial) + " lost or moved an event");
                break;
            }
        }
        else
        {
            ++rejected;
            if (! (bar == beforeBar && extra == beforeExtra))
            {
                check (false, "switch property: a rejected switch changed the bar (trial " + std::to_string (trial) + ")");
                break;
            }
        }
    }
    std::printf ("bar timing: %d switches accepted, %d rejected atomically\n", accepted, rejected);
    check (accepted > 1000 && rejected > 10, "switch property: both outcomes exercised");
}

} // namespace

int main()
{
    testCapacityTable();
    testSweep();
    testSubhits();
    testSamples();
    testSwing();
    testSwitch();
    if (failures == 0)
        std::printf ("VOX Drums bar timing tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
