#pragma once

// Test helpers for StoredPattern in the PR 137 §10 cell model: tests written
// for 4/4 grids of 16/32/64 straight steps address cells by step index.

#include "DrumsTimingTestUtil.h"
#include "PatternStore.h"


namespace vox::drums::testutil {

// Steps per bar of a 4/4 pattern (its straight cells).
inline int stepsOf (const StoredPattern& p) noexcept
{
    return timing::BarTiming { p.signature, p.resolution, 0 }.beats() * timing::straightCells (timing::beatKind (p.signature), p.resolution);
}

inline kit::PatternStep& stepAt (StoredPattern& p, int lane, int bar, int step) noexcept
{
    return p.cell (lane, bar, slotOfStep (stepsOf (p), step));
}

inline const kit::PatternStep& stepAt (const StoredPattern& p, int lane, int bar, int step) noexcept
{
    return p.cell (lane, bar, slotOfStep (stepsOf (p), step));
}

// An empty 4/4 pattern with `steps` straight cells per bar (seed kept).
inline void emptyGrid (StoredPattern& p, int steps, int bars) noexcept
{
    const auto seed = p.seed;
    p = {};
    p.resolution = resolutionOfSteps (steps);
    p.bars = static_cast<std::uint8_t> (bars);
    p.seed = seed;
}

// Sets a pattern's grid (4/4, `steps` straight cells per bar) and bar count
// keeping its content (StoredPattern::convertGrid, then setBars). False if
// the content cannot be converted losslessly (the pattern is unchanged).
[[nodiscard]] inline bool setGrid (StoredPattern& p, int steps, int bars)
{
    if (! p.convertGrid ({}, resolutionOfSteps (steps)))
        return false;
    p.setBars (bars);
    return true;
}

// A unique song bar on a 4/4 grid of `steps` straight cells.
inline void setSteps (BarCells& c, int steps) noexcept
{
    c.signature = {};
    c.resolution = resolutionOfSteps (steps);
    c.masks.fill (0);
}

inline int stepsOf (const BarCells& c) noexcept
{
    return timing::BarTiming { c.signature, c.resolution, 0 }.beats() * timing::straightCells (timing::beatKind (c.signature), c.resolution);
}

inline kit::PatternStep& stepAt (BarCells& c, int lane, int step) noexcept
{
    return c.cell (lane, slotOfStep (stepsOf (c), step));
}

inline const kit::PatternStep& stepAt (const BarCells& c, int lane, int step) noexcept
{
    return c.cell (lane, slotOfStep (stepsOf (c), step));
}

} // namespace vox::drums::testutil
