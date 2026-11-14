// Template part of BarTiming.h (switchSubdivision). Included by BarTiming.h only.

namespace vox::drums::timing {

template <typename Extra>
SwitchOutcome switchSubdivision (const BarTiming& from, const BarTiming& to, LaneBar& bar, std::array<Extra, maxSlotsPerBar>* extra) noexcept
{
    if (! from.valid() || ! to.valid() || ! (from.signature == to.signature) || from.resolution != to.resolution)
        return { SwitchResult::invalidTiming, -1, -1 };
    for (int s = 0; s < maxSlotsPerBar; ++s)
        if (bar[static_cast<std::size_t> (s)].on && ! from.slotActive (s))
            return { SwitchResult::reservedSlotNotEmpty, s / from.slotsPerBeat(), s % from.slotsPerBeat() };

    LaneBar next {};
    std::array<Extra, maxSlotsPerBar> nextExtra {};
    const auto changed = static_cast<std::uint16_t> (from.alternativeBeats ^ to.alternativeBeats);
    // Beats that keep their subdivision keep their events where they are.
    for (int b = 0; b < from.beats(); ++b)
    {
        if ((changed & (1u << b)) != 0)
            continue;
        for (int c = 0; c < from.cells (b); ++c)
        {
            const auto s = static_cast<std::size_t> (from.slot (b, c));
            next[s] = bar[s];
            if (extra != nullptr)
                nextExtra[s] = (*extra)[s];
        }
    }
    // Events of switched beats keep their absolute tick and go to the
    // nearest cell start of the new layout (exact tie: the earlier cell).
    for (int b = 0; b < from.beats(); ++b)
    {
        if ((changed & (1u << b)) == 0)
            continue;
        for (int c = 0; c < from.cells (b); ++c)
        {
            const auto s = static_cast<std::size_t> (from.slot (b, c));
            if (! bar[s].on)
                continue;
            const int absolute = from.cellStartTick (b, c) + bar[s].microTicks;
            int bestBeat = -1, bestCell = -1, bestDistance = 0;
            for (int tb = 0; tb < to.beats(); ++tb)
                for (int tc = 0; tc < to.cells (tb); ++tc)
                {
                    const int d = absolute - to.cellStartTick (tb, tc);
                    const int distance = d < 0 ? -d : d;
                    if (bestBeat < 0 || distance < bestDistance)   // strict: an exact tie keeps the earlier cell
                    {
                        bestBeat = tb;
                        bestCell = tc;
                        bestDistance = distance;
                    }
                }
            const int micro = absolute - to.cellStartTick (bestBeat, bestCell);
            if (2 * (micro < 0 ? -micro : micro) > to.cellTicks (bestBeat))
                return { SwitchResult::offsetTooLarge, b, c };
            const auto t = static_cast<std::size_t> (to.slot (bestBeat, bestCell));
            if (next[t].on)
                return { SwitchResult::collision, b, c };
            next[t] = { true, static_cast<std::int16_t> (micro) };
            if (extra != nullptr)
                nextExtra[t] = (*extra)[s];
        }
    }
    bar = next;
    if (extra != nullptr)
        *extra = nextExtra;
    return {};
}

} // namespace vox::drums::timing
