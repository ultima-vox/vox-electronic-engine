#include "BarTiming.h"

#include <cmath>

namespace vox::drums::timing {

bool supported (TimeSignature s) noexcept
{
    return s.numerator >= 1 && s.numerator <= maxBeats && (s.denominator == 4 || s.denominator == 8);
}

BeatKind beatKind (TimeSignature s) noexcept
{
    if (s.denominator == 4)
        return BeatKind::quarter;
    return s.numerator % 3 == 0 && s.numerator > 3 ? BeatKind::dottedQuarter : BeatKind::eighth;
}

int beatCount (TimeSignature s) noexcept
{
    return beatKind (s) == BeatKind::dottedQuarter ? s.numerator / 3 : s.numerator;
}

int beatTicks (BeatKind k) noexcept
{
    switch (k)
    {
        case BeatKind::quarter: return ticksPerQuarter;
        case BeatKind::dottedQuarter: return ticksPerQuarter * 3 / 2;
        case BeatKind::eighth: return ticksPerQuarter / 2;
    }
    return ticksPerQuarter;
}

int straightCells (BeatKind k, Resolution r) noexcept
{
    static constexpr int quarter[resolutionCount] = { 2, 4, 8, 16 };
    static constexpr int dotted[resolutionCount] = { 3, 6, 12, 24 };
    static constexpr int eighth[resolutionCount] = { 1, 2, 4, 8 };
    const auto i = static_cast<int> (r) & 3;
    return k == BeatKind::quarter ? quarter[i] : k == BeatKind::dottedQuarter ? dotted[i] : eighth[i];
}

int alternativeCells (BeatKind k, Resolution r) noexcept
{
    static constexpr int quarter[resolutionCount] = { 3, 6, 12, 24 };   // triplets
    static constexpr int dotted[resolutionCount] = { 2, 4, 8, 16 };     // duplets
    static constexpr int eighth[resolutionCount] = { 0, 3, 6, 12 };     // triplets (none at the 8th resolution)
    const auto i = static_cast<int> (r) & 3;
    return k == BeatKind::quarter ? quarter[i] : k == BeatKind::dottedQuarter ? dotted[i] : eighth[i];
}

Capacity capacity (TimeSignature s, Resolution r) noexcept
{
    Capacity c;
    if (! supported (s))
        return c;
    const auto k = beatKind (s);
    const int beats = beatCount (s), straight = straightCells (k, r), alt = alternativeCells (k, r);
    const int slots = alt > straight ? alt : straight;
    if (alt > 0 && beats * slots <= maxSlotsPerBar)
        return { Availability::full, slots, beats * slots };
    if (beats * straight <= maxSlotsPerBar)
        return { Availability::straightOnly, straight, beats * straight };
    return c;
}

const char* limitationReason (TimeSignature s, Resolution r) noexcept
{
    if (! supported (s))
        return "this time signature is not supported";
    const auto c = capacity (s, r);
    if (c.availability == Availability::full)
        return "";
    if (c.availability == Availability::unavailable)
        return "too many cells per bar at this resolution (more than 64)";
    if (alternativeCells (beatKind (s), r) == 0)
        return "no triplets at 8th notes in this meter";
    return beatKind (s) == BeatKind::dottedQuarter ? "duplets unavailable at this resolution (more than 64 cells per bar)"
                                                   : "triplets unavailable at this resolution (more than 64 cells per bar)";
}

bool BarTiming::valid() const noexcept
{
    const auto c = capacity (signature, resolution);
    if (c.availability == Availability::unavailable)
        return false;
    const auto beatsMask = static_cast<std::uint32_t> ((1u << beatCount (signature)) - 1u);
    if ((alternativeBeats & ~beatsMask) != 0)
        return false;
    return c.availability == Availability::full || alternativeBeats == 0;
}

int BarTiming::beats() const noexcept
{
    return beatCount (signature);
}

int BarTiming::barTicks() const noexcept
{
    return beats() * beatTicks (beatKind (signature));
}

int BarTiming::slotsPerBeat() const noexcept
{
    return capacity (signature, resolution).slotsPerBeat;
}

int BarTiming::cells (int beat) const noexcept
{
    const auto k = beatKind (signature);
    return (alternativeBeats & (1u << beat)) != 0 ? alternativeCells (k, resolution) : straightCells (k, resolution);
}

int BarTiming::cellTicks (int beat) const noexcept
{
    const int n = cells (beat);
    return n > 0 ? beatTicks (beatKind (signature)) / n : 0;
}

int BarTiming::cellStartTick (int beat, int cell) const noexcept
{
    return beat * beatTicks (beatKind (signature)) + cell * cellTicks (beat);
}

int BarTiming::slot (int beat, int cell) const noexcept
{
    return beat * slotsPerBeat() + cell;
}

bool BarTiming::slotActive (int s) const noexcept
{
    const int per = slotsPerBeat();
    if (per <= 0 || s < 0 || s >= beats() * per)
        return false;
    return s % per < cells (s / per);
}

int subhitTick (int cellStart, int cellTicks, int index, int count) noexcept
{
    if (count <= 1 || index <= 0)
        return cellStart;
    const int q = index * cellTicks;
    const int base = q / count, rem = q % count;
    return cellStart + base + (2 * rem > count ? 1 : 0);
}

bool subhitsDistinct (int cellTicks, int count) noexcept
{
    for (int i = 1; i < count; ++i)
        if (subhitTick (0, cellTicks, i, count) <= subhitTick (0, cellTicks, i - 1, count))
            return false;
    return true;
}

std::int64_t sampleOffset (std::int64_t eventTick, double blockStartPpq, double samplesPerQuarter) noexcept
{
    return static_cast<std::int64_t> (std::floor ((static_cast<double> (eventTick) / ticksPerQuarter - blockStartPpq) * samplesPerQuarter));
}

} // namespace vox::drums::timing
