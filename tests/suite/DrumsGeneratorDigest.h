#pragma once

// VOX Drums generator digest (issue 132, R4): one FNV-1a 64 value per style
// over a settings matrix of every generator action (Pattern, Variation B,
// Fill, Mutate Lanes with lane and step locks, Song). It calls the generator
// with its default arguments only, so the same code builds against a
// generator from before pad roles; the expected values in the test were
// produced that way from c7c29bb (see DrumsPadRolesTests.cpp), and
// re-pinned for the PR 137 §10 cell model (grid, masks, microTicks), whose
// per-cell equivalence with the previous model is recorded in the PR.

#include "Generator.h"
#include "PatternStore.h"

#include <array>
#include <cstdint>
#include <memory>

namespace vox::drums::digest {

struct Fnv {
    std::uint64_t h = 1469598103934665603ull;
    void byte (std::uint8_t b) noexcept
    {
        h ^= b;
        h *= 1099511628211ull;
    }
    void u32 (std::uint32_t v) noexcept
    {
        for (int i = 0; i < 4; ++i)
            byte (static_cast<std::uint8_t> (v >> (8 * i)));
    }
    void pattern (const StoredPattern& p) noexcept
    {
        byte (static_cast<std::uint8_t> (p.signature.numerator));
        byte (static_cast<std::uint8_t> (p.signature.denominator));
        byte (static_cast<std::uint8_t> (p.resolution));
        byte (p.bars);
        u32 (p.seed);
        for (int lane = 0; lane < kit::padCount; ++lane)
            for (int bar = 0; bar < p.bars; ++bar)
            {
                const auto t = p.timing (lane, bar);
                u32 (t.alternativeBeats);
                for (int beat = 0; beat < t.beats(); ++beat)
                    for (int cell = 0; cell < t.cells (beat); ++cell)
                    {
                        const auto& c = p.cell (lane, bar, t.slot (beat, cell));
                        byte (static_cast<std::uint8_t> ((c.on ? 1 : 0) | (c.accent ? 2 : 0) | (c.locked ? 4 : 0)));
                        byte (c.velocity);
                        byte (c.probability);
                        byte (c.ratchet);
                        u32 (static_cast<std::uint16_t> (c.microTicks));
                    }
            }
    }
    void section (const preset::Section& s) noexcept
    {
        u32 (static_cast<std::uint32_t> (s.data.size()));
        for (auto b : s.data)
            byte (std::to_integer<std::uint8_t> (b));
    }
};

// The slot of straight step `step` of a 4/4 bar with `steps` steps.
inline int stepSlot (int steps, int step) noexcept
{
    const timing::BarTiming t { {}, steps == 64 ? timing::Resolution::sixtyFourth : steps == 32 ? timing::Resolution::thirtySecond
                                                                                                : timing::Resolution::sixteenth, 0 };
    return t.slot (step / (steps / 4), step % (steps / 4));
}

// An empty 4/4 pattern with `steps` straight cells per bar (16, 32, 64).
inline void emptyPattern (StoredPattern& p, int steps, int bars) noexcept
{
    p = {};
    p.resolution = steps == 64 ? timing::Resolution::sixtyFourth : steps == 32 ? timing::Resolution::thirtySecond
                                                                              : timing::Resolution::sixteenth;
    p.bars = static_cast<std::uint8_t> (bars);
    p.seed = 1;
}

inline std::array<std::uint64_t, styleCount> generatorDigest()
{
    std::array<std::uint64_t, styleCount> out {};
    static constexpr int grids[4][2] = { { 16, 4 }, { 64, 4 }, { 16, 1 }, { 32, 2 } };
    static constexpr std::uint32_t masks[4] = { 0x0008u, 0x0130u, 0x0003u, 0xffffu };
    auto a = std::make_unique<StoredPattern>(), b = std::make_unique<StoredPattern>(), m = std::make_unique<StoredPattern>();
    auto all = std::make_unique<PatternStore::Everything>();
    for (int style = 0; style < styleCount; ++style)
    {
        Fnv f;
        for (std::uint32_t seed = 1; seed <= 6; ++seed)
            for (float density : { 0.0f, 0.5f, 1.0f })
                for (float complexity : { 0.0f, 0.5f, 1.0f })
                    for (float syncopation : { 0.0f, 1.0f })
                        for (float fills : { 0.0f, 0.5f, 1.0f })
                            for (float variation : { 0.4f, 1.0f })
                                for (const auto& grid : grids)
                                {
                                    GeneratorSettings g;
                                    g.style = static_cast<Style> (style);
                                    g.seed = seed * 7919u;
                                    g.density = density;
                                    g.complexity = complexity;
                                    g.syncopation = syncopation;
                                    g.fills = fills;
                                    g.variation = variation;
                                    g.swing = 0.25f;
                                    g.humanize = 0.3f;
                                    emptyPattern (*a, grid[0], grid[1]);
                                    generatePattern (g, *a);
                                    f.pattern (*a);
                                    emptyPattern (*b, grid[0], grid[1]);
                                    generateVariation (g, *a, *b);
                                    f.pattern (*b);
                                    *m = *a;
                                    generateFill (g, *m, 0);
                                    f.pattern (*m);
                                    // Mutate over a pattern of the next style, with two
                                    // locked steps and (every other seed) a locked lane.
                                    GeneratorSettings other = g;
                                    other.style = static_cast<Style> ((style + 1) % styleCount);
                                    other.seed = g.seed + 1;
                                    for (auto mask : masks)
                                    {
                                        emptyPattern (*m, grid[0], grid[1]);
                                        generatePattern (other, *m);
                                        m->cell (3, 0, stepSlot (grid[0], 2)).locked = true;
                                        m->cell (8, grid[1] - 1, stepSlot (grid[0], grid[0] - 1)).locked = true;
                                        GeneratorSettings locked = g;
                                        locked.laneLocks = seed % 2 == 0 ? 0x0020u : 0u;
                                        mutateLanes (locked, mask, *m);
                                        f.pattern (*m);
                                    }
                                }
        for (std::uint32_t seed = 1; seed <= 3; ++seed)
            for (float density : { 0.2f, 1.0f })
            {
                GeneratorSettings g;
                g.style = static_cast<Style> (style);
                g.seed = seed;
                g.density = density;
                for (int slot = 0; slot < PatternStore::slotCount; ++slot)
                {
                    emptyPattern (all->slots[static_cast<std::size_t> (slot)], 16, 4);
                    emptyPattern (all->alternates[static_cast<std::size_t> (slot)], 16, 4);
                }
                all->song = SongData::initial();
                generateSong (g, *all);
                for (const auto& p : all->slots)
                    f.pattern (p);
                for (const auto& p : all->alternates)
                    f.pattern (p);
                f.section (encodeSong (all->song));
            }
        out[static_cast<std::size_t> (style)] = f.h;
    }
    return out;
}

} // namespace vox::drums::digest
