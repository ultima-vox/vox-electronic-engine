#pragma once

// Test helpers for the PR 137 §10 cell model: address the straight cells of
// a 4/4 grid by their step index (16th, 32nd or 64th), as the tests did
// before per-beat slots, and collect the canonical hits (kit::cellHits) of a
// step or a bar.

#include "engine/KitEngine.h"

#include <vector>

namespace vox::drums::testutil {

inline timing::Resolution resolutionOfSteps (int steps) noexcept
{
    return steps == 64 ? timing::Resolution::sixtyFourth : steps == 32 ? timing::Resolution::thirtySecond : timing::Resolution::sixteenth;
}

// The storage slot of straight step `step` (0..steps-1) of a 4/4 bar.
inline int slotOfStep (int steps, int step) noexcept
{
    const timing::BarTiming t { {}, resolutionOfSteps (steps), 0 };
    const int per = steps / 4;
    return t.slot (step / per, step % per);
}

// An engine pattern in 4/4 with `steps` straight cells per bar.
struct GridPattern {
    kit::PatternCells cells {};
    kit::Pattern pattern;
    int steps = 16;

    GridPattern (std::uint32_t lanes, int stepsPerBar, std::uint32_t bars, std::uint32_t seed = 0)
    {
        steps = stepsPerBar;
        pattern.lanes = lanes;
        pattern.bars = bars;
        pattern.resolution = resolutionOfSteps (stepsPerBar);
        pattern.cells = cells.data();
        pattern.seed = seed;
    }
    GridPattern (const GridPattern&) = delete;
    kit::PatternStep& at (int lane, int bar, int step)
    {
        return cells[static_cast<std::size_t> ((lane * kit::maxPatternBars + bar) * kit::slotStride + slotOfStep (steps, step))];
    }
};

// The hits of straight step `g` (absolute, from ppq 0) of an all-straight
// 4/4 pattern, lanes in order (what DRM-04's stepHits returned).
inline int stepHits (const kit::Pattern& p, const kit::StepContext& ctx, std::int64_t g, int steps, kit::ScheduledHit* out)
{
    if (g < 0)
        return 0;
    const std::int64_t bar = g / steps;
    const int within = static_cast<int> (g % steps), per = steps / 4;
    const auto view = p.bar (static_cast<std::uint32_t> (bar % p.bars));
    int n = 0;
    for (std::uint32_t lane = 0; lane < p.lanes; ++lane)
        n += kit::cellHits (view, ctx, bar, bar * p.barTicks(), static_cast<int> (lane), within / per, within % per, out + n);
    return n;
}

// Every hit of one bar: lanes, beats, cells in order.
inline void barHits (const kit::BarView& view, const kit::StepContext& ctx, std::int64_t absoluteBar, std::int64_t barStart,
                     std::vector<kit::ScheduledHit>& out)
{
    if (view.cells == nullptr)
        return;
    kit::ScheduledHit hits[kit::maxRatchet];
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        const auto t = view.timing (lane);
        if (! t.valid())
            continue;
        for (int beat = 0; beat < t.beats(); ++beat)
            for (int cell = 0; cell < t.cells (beat); ++cell)
            {
                const int n = kit::cellHits (view, ctx, absoluteBar, barStart, lane, beat, cell, hits);
                out.insert (out.end(), hits, hits + n);
            }
    }
}

} // namespace vox::drums::testutil
