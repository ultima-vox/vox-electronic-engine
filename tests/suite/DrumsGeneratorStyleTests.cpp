// VOX Drums generator style fidelity (issue 128).
//
// (a) Rhythm grammar: every style's unambiguous idiom (its kick grid, its
//     back-beat, its hat grid) holds for Pattern, Variation B, Fill and the
//     five slots of a generated 16/32/64-bar Song, over a matrix of seeds x
//     density x complexity x syncopation x steps x bars x fills x variation.
//     Fill windows (the last 2..8 sixteenths of the last bar) are excluded
//     only where a fill is allowed to replace the groove (hats, percussion).
// (b) Audible roles: the generator writes lanes by the default pad layout
//     (Generator.h). For each factory kit and style, a lane whose pad model is
//     a different core instrument (kick, snare, clap) than the lane's role must
//     receive no hits. A user kit with swapped pads is reported, not asserted:
//     the generator has no other role information than the pad model.
// (d) Mutate Lanes: only the selected, unlocked lanes change; they get the
//     selected style's lanes; locked steps and lanes, laneMask 0 and a
//     fully locked kit are no-ops; equal input gives equal output.
// (e) MIDI examples: written files are read back with juce::MidiFile and
//     match the exported note list (ticks, notes, velocities, channel 10).
// (c) Reproducibility: same settings + seed + input give the same pattern;
//     a pattern's previous content (unlocked) does not change the result.
//
// With an argument, writes a Markdown report (statistics and every
// violation found) to that path.
#include "FactoryContent.h"
#include "Generator.h"
#include "PadRoles.h"
#include "StyleRegistry.h"
#include "PatternExport.h"
#include "DrumsPatternTestUtil.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace vox::drums;
using kit::Model;

namespace {

int failures = 0;
std::ostringstream report;

void check (bool ok, const std::string& what)
{
    if (! ok)
    {
        ++failures;
        std::printf ("FAIL: %s\n", what.c_str());
    }
}

enum Lane { kick, snare, clap, closedHat, openHat, tom, rim, cymbal, perc1, perc2, perc3, perc4 };

// The roles of the default layout (Generator.h).
constexpr Model roleModel[kit::padCount] = { Model::kick, Model::snare, Model::clap, Model::hat, Model::hat, Model::tom, Model::rim, Model::cymbal,
                                            Model::perc, Model::perc, Model::perc, Model::perc, Model::perc, Model::perc, Model::perc, Model::perc };
const char* modelName (Model m)
{
    static const char* n[] = { "kick", "snare", "clap", "hat", "tom", "rim", "cymbal", "perc" };
    return n[static_cast<int> (m)];
}
bool core (Model m) { return m == Model::kick || m == Model::snare || m == Model::clap; }
// The model a pad role is played by (roles v2).
Model modelOfRole (PadRole r)
{
    switch (r)
    {
        case PadRole::kick: return Model::kick;
        case PadRole::snare: return Model::snare;
        case PadRole::clap: return Model::clap;
        case PadRole::closedHat:
        case PadRole::openHat: return Model::hat;
        case PadRole::tom1:
        case PadRole::tom2:
        case PadRole::tom3: return Model::tom;
        case PadRole::rim: return Model::rim;
        case PadRole::crash:
        case PadRole::ride: return Model::cymbal;
        case PadRole::none:
        case PadRole::percussion: break;
    }
    return Model::perc;
}

bool hit (const StoredPattern& p, int lane, int bar, int pos)
{
    return testutil::stepAt (p, lane, bar, pos * (testutil::stepsOf (p) / 16)).on;
}

int fillLength (const GeneratorSettings& g)
{
    return std::clamp (static_cast<int> (std::lround (2.0 + 6.0 * g.fills)), 2, 8);
}

std::string describe (const GeneratorSettings& g, const StoredPattern& p)
{
    char b[256];
    std::snprintf (b, sizeof b, "%s seed %u density %.2f complexity %.2f syncopation %.2f fills %.2f variation %.2f, %d steps x %d bars",
                   styleName (g.style), g.seed, g.density, g.complexity, g.syncopation, g.fills, g.variation, testutil::stepsOf (p), p.bars);
    return b;
}

struct Tally {
    long patterns = 0, violations = 0;
    std::vector<std::string> examples;
    std::array<long, 16> kickAt {}, backAt {}, hatAt {};
    long bars = 0, hits = 0;
};
std::map<std::string, Tally> tallies;   // key: style + context

void violation (Tally& t, const std::string& what)
{
    ++t.violations;
    if (t.examples.size() < 6)
        t.examples.push_back (what);
}

// Checks the style idiom of `p`. `fill`: the last bar ends in a fill of
// `fillLen` sixteenths. `kickOn`: false for the song's break slot.
void checkGrammar (Style style, float density, const StoredPattern& p, bool fill, int fillLen, bool kickOn, Tally& t, const std::string& ctx)
{
    ++t.patterns;
    const int scale = testutil::stepsOf (p) / 16;
    for (int bar = 0; bar < p.bars; ++bar)
    {
        ++t.bars;
        for (int lane = 0; lane < kit::padCount; ++lane)
            for (int step = 0; step < testutil::stepsOf (p); ++step)
                if (testutil::stepAt (p, lane, bar, step).on)
                {
                    ++t.hits;
                    if (step % scale != 0)
                        violation (t, ctx + ": a hit between the 16ths (lane " + std::to_string (lane + 1) + ")");
                }
        for (int pos = 0; pos < 16; ++pos)
        {
            const bool k = hit (p, kick, bar, pos), quarter = pos % 4 == 0, off = pos % 4 == 2;
            const bool inFill = fill && bar == p.bars - 1 && pos >= 16 - fillLen;
            t.kickAt[static_cast<std::size_t> (pos)] += k ? 1 : 0;
            t.backAt[static_cast<std::size_t> (pos)] += (hit (p, snare, bar, pos) || hit (p, clap, bar, pos)) ? 1 : 0;
            t.hatAt[static_cast<std::size_t> (pos)] += (hit (p, closedHat, bar, pos) || hit (p, openHat, bar, pos)) ? 1 : 0;
            const auto bad = [&] (const char* what) {
                violation (t, ctx + ": bar " + std::to_string (bar + 1) + " 16th " + std::to_string (pos + 1) + ": " + what);
            };
            if (! kickOn)
            {
                if (k) bad ("kick in the break slot");
                continue;
            }
            switch (style)
            {
                case Style::fullOn:
                    if (quarter && ! k) bad ("four-on-the-floor kick missing");
                    if (! quarter && k) bad ("kick off the quarter");
                    if (off && ! inFill && ! hit (p, openHat, bar, pos)) bad ("off-beat open hat missing");
                    if (off && hit (p, closedHat, bar, pos)) bad ("closed hat on the off-beat");
                    if (density > 0.15f && (pos == 4 || pos == 12) && ! hit (p, clap, bar, pos)) bad ("clap on 2/4 missing");
                    break;
                case Style::goa:
                    if (quarter && ! k) bad ("four-on-the-floor kick missing");
                    if (! quarter && k) bad ("kick off the quarter");
                    if (off && ! inFill && ! hit (p, closedHat, bar, pos)) bad ("off-beat closed hat missing");
                    if (pos == 12 && ! hit (p, snare, bar, pos)) bad ("snare on 4 missing");
                    break;
                case Style::house:
                    if (quarter && ! k) bad ("four-on-the-floor kick missing");
                    if (! quarter && pos != 15 && k) bad ("kick off the quarter (other than the pickup)");
                    if ((pos == 4 || pos == 12) && ! hit (p, clap, bar, pos)) bad ("clap on 2/4 missing");
                    if (off && ! inFill && ! hit (p, openHat, bar, pos)) bad ("off-beat open hat missing");
                    break;
                case Style::techno:
                    if (quarter && ! k) bad ("four-on-the-floor kick missing");
                    if (! quarter && k) bad ("kick off the quarter");
                    if (pos % 2 == 0 && ! inFill && ! hit (p, closedHat, bar, pos)) bad ("8th closed hat missing");
                    break;
                case Style::trap:
                    if (pos == 0 && ! k) bad ("kick on 1 missing");
                    if ((pos == 4 || pos == 8 || pos == 12) && k) bad ("kick on a quarter other than 1 (not half-time)");
                    if (pos == 8 && ! hit (p, snare, bar, pos)) bad ("half-time snare on 3 missing");
                    if ((pos == 4 || pos == 12) && ! inFill && hit (p, snare, bar, pos)) bad ("snare on 2/4 (not half-time)");
                    break;
                case Style::breaks:
                    if ((pos == 0 || pos == 10) && ! k) bad ("broken kick (1, and of 3) missing");
                    if ((pos == 4 || pos == 8 || pos == 12) && k) bad ("kick on 2/3/4 (four-on-the-floor)");
                    if ((pos == 4 || pos == 12) && ! hit (p, snare, bar, pos)) bad ("back-beat snare missing");
                    break;
                case Style::ambient:
                    if (pos == 0 && ! k) bad ("kick on 1 missing");
                    if (pos != 0 && pos != 10 && k) bad ("kick outside 1 and the and of 3");
                    if (! inFill && (hit (p, snare, bar, pos) || hit (p, clap, bar, pos))) bad ("back-beat instrument in ambient");
                    break;
            }
        }
    }
}

// Random previous content in every cell a valid pattern can hold (its
// bars' active cells; unused storage is always empty, PR 137 §10.5).
void prefill (StoredPattern& p, std::uint32_t salt)
{
    for (int lane = 0; lane < kit::padCount; ++lane)
        for (int bar = 0; bar < p.bars; ++bar)
            for (int slot = 0; slot < kit::slotStride; ++slot)
                if (p.timing (lane, bar).slotActive (slot))
                {
                    const auto i = static_cast<std::size_t> ((lane * kit::maxPatternBars + bar) * kit::slotStride + slot);
                    p.cells[i].on = ((i * 2654435761u + salt) >> 7) % 3 == 0;
                }
}

void testGrammarMatrix()
{
    const float densities[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    const float amounts[] = { 0.0f, 0.5f, 1.0f };
    const float fills[] = { 0.0f, 0.5f, 1.0f };
    const float variations[] = { 0.4f, 1.0f };
    const int grids[][2] = { { 16, 4 }, { 64, 4 }, { 16, 1 }, { 32, 2 } };
    auto a = std::make_unique<StoredPattern>();
    auto b = std::make_unique<StoredPattern>();
    auto again = std::make_unique<StoredPattern>();
    long deterministic = 0, determinismFailures = 0;
    for (int style = 0; style < styleCount; ++style)
        for (std::uint32_t seed = 1; seed <= 12; ++seed)
            for (float d : densities)
                for (float cx : amounts)
                    for (float sy : amounts)
                        for (float f : fills)
                            for (float v : variations)
                                for (const auto& grid : grids)
                                {
                                    GeneratorSettings g;
                                    g.style = static_cast<Style> (style);
                                    g.seed = seed * 7919u + static_cast<std::uint32_t> (style);
                                    g.density = d;
                                    g.complexity = cx;
                                    g.syncopation = sy;
                                    g.fills = f;
                                    g.variation = v;
                                    g.swing = 0.25f;
                                    g.humanize = 0.3f;
                                    testutil::emptyGrid (*a, grid[0], grid[1]);
                                    generatePattern (g, *a);
                                    const std::string name = styleName (g.style);
                                    checkGrammar (g.style, d, *a, f > 0.05f && a->bars > 1, fillLength (g), true, tallies[name + " · pattern"], describe (g, *a) + " (pattern)");
                                    testutil::emptyGrid (*b, grid[0], grid[1]);
                                    generateVariation (g, *a, *b);
                                    checkGrammar (g.style, d, *b, true, fillLength (g), true, tallies[name + " · variation B"], describe (g, *b) + " (variation B)");
                                    // (c) the previous content does not leak into an unlocked pattern
                                    if (seed <= 2 && cx == 0.5f && sy == 0.5f)
                                    {
                                        testutil::emptyGrid (*again, grid[0], grid[1]);
                                        prefill (*again, seed);
                                        generatePattern (g, *again);
                                        ++deterministic;
                                        determinismFailures += (*again == *a) ? 0 : 1;
                                    }
                                }
    check (determinismFailures == 0, "generator: same settings, seed and unlocked input give the same pattern (" + std::to_string (determinismFailures) + " of " + std::to_string (deterministic) + " differ)");
    report << "Reproducibility: " << deterministic << " pairs (random previous content vs. empty), " << determinismFailures << " differ.\n\n";
}

void testSongs()
{
    static constexpr float densityScale[5] = { 0.45f, 1.0f, 1.2f, 0.6f, 1.4f };   // intro, main, lift, break, peak (Generator.cpp)
    auto all = std::make_unique<PatternStore::Everything>();
    for (int style = 0; style < styleCount; ++style)
        for (std::uint32_t seed = 1; seed <= 6; ++seed)
            for (float d : { 0.2f, 0.6f, 1.0f })
                for (int length : { 16, 32, 64 })
                {
                                                            all->song = SongData::initial();
                    all->song.length = static_cast<std::uint8_t> (length);
                    for (auto& s : all->slots) testutil::emptyGrid (s, 16, 4);
                    for (auto& s : all->alternates) testutil::emptyGrid (s, 16, 4);
                    GeneratorSettings g;
                    g.style = static_cast<Style> (style);
                    g.seed = seed;
                    g.density = d;
                    generateSong (g, *all);
                    const std::string name = styleName (g.style);
                    for (int slot = 0; slot < slotCountForSong; ++slot)
                    {
                        auto s = g;
                        s.density = std::clamp (g.density * densityScale[slot], 0.0f, 1.0f);
                        s.fills = std::clamp (g.fills * (slot == 0 ? 0.6f : slot == 1 ? 1.0f : slot == 2 ? 1.1f : slot == 3 ? 0.8f : 1.25f), 0.0f, 1.0f);
                        const std::string ctx = describe (g, all->slots[static_cast<std::size_t> (slot)]) + ", song " + std::to_string (length) + " bars, slot " + std::to_string (slot + 1);
                        checkGrammar (g.style, s.density, all->slots[static_cast<std::size_t> (slot)], s.fills > 0.05f, fillLength (s), slot != 3, tallies[name + " · song slots"], ctx + " A");
                        checkGrammar (g.style, s.density, all->alternates[static_cast<std::size_t> (slot)], true, fillLength (s), slot != 3, tallies[name + " · song slots"], ctx + " B");
                    }
                    // every bar of the song plays one of the generated slots
                    bool placed = true;
                    for (int bar = 0; bar < length; ++bar)
                        placed = placed && all->song.bars[static_cast<std::size_t> (bar)].kind == SongBar::Kind::pattern && all->song.bars[static_cast<std::size_t> (bar)].slot < slotCountForSong;
                    check (placed, "generator: song " + std::to_string (length) + " bars of " + name + " places a slot on every bar");
                }
}

void testFills()
{
    // A fill written into a bar of a groove keeps the style's kick grid.
    auto p = std::make_unique<StoredPattern>();
    for (int style = 0; style < styleCount; ++style)
        for (std::uint32_t seed = 1; seed <= 20; ++seed)
            for (float f : { 0.0f, 0.5f, 1.0f })
            {
                GeneratorSettings g;
                g.style = static_cast<Style> (style);
                g.seed = seed;
                g.fills = 0.0f;
                testutil::emptyGrid (*p, 16, 4);
                generatePattern (g, *p);
                g.fills = f;
                generateFill (g, *p, 1);
                g.seed = seed + 1000u;
                const std::string name = styleName (g.style);
                // bar 2 has the fill; check it as the last bar of a 2-bar slice
                auto slice = std::make_unique<StoredPattern>();
                testutil::emptyGrid (*slice, 16, 2);
                for (int lane = 0; lane < kit::padCount; ++lane)
                    for (int bar = 0; bar < 2; ++bar)
                        for (int step = 0; step < 16; ++step)
                            testutil::stepAt (*slice, lane, bar, step) = testutil::stepAt (*p, lane, bar, step);
                checkGrammar (g.style, g.density, *slice, true, fillLength (g), true, tallies[name + " · fill"], describe (g, *slice) + " (fill in bar 2)");
            }
}

void testKitRoles()
{
    report << "## (b) Audible roles on the factory kits\n\n"
              "Lanes the generator writes, with the model of the pad on that lane. A **core mismatch** is a lane whose role "
              "and pad model are different core instruments (kick, snare, clap): the rhythm is right but another instrument plays it.\n\n"
              "| Kit | Style | Lane | Role | Pad model | Hits (per 1000 bars) | Core mismatch |\n|---|---|---|---|---|---|---|\n";
    auto p = std::make_unique<StoredPattern>();
    int rows = 0;
    for (int k = 0; k < factoryKitCount; ++k)
    {
        const auto kitSettings = factoryKit (k);
        // The generator plays on the kit's roles (the genre kits store them, PR 137 §5).
        const auto roles = factoryKitRoles (k);
        const auto map = RoleMap::from (roles);
        for (int style = 0; style < styleCount; ++style)
        {
            std::array<long, kit::padCount> hits {};
            long bars = 0;
            for (std::uint32_t seed = 1; seed <= 25; ++seed)
                for (float d : { 0.25f, 0.5f, 1.0f })
                    for (float sy : { 0.0f, 0.5f, 1.0f })
                    {
                        GeneratorSettings g;
                        g.style = static_cast<Style> (style);
                        g.seed = seed;
                        g.density = d;
                        g.syncopation = sy;
                        g.complexity = 0.6f;
                        testutil::emptyGrid (*p, 16, 4);
                        generatePattern (g, *p, map);
                        bars += p->bars;
                        for (int lane = 0; lane < kit::padCount; ++lane)
                            for (int bar = 0; bar < p->bars; ++bar)
                                for (int step = 0; step < testutil::stepsOf (*p); ++step)
                                    hits[static_cast<std::size_t> (lane)] += testutil::stepAt (*p, lane, bar, step).on ? 1 : 0;
                    }
            for (int lane = 0; lane < kit::padCount; ++lane)
            {
                if (hits[static_cast<std::size_t> (lane)] == 0)
                    continue;
                const auto model = kitSettings.pads[static_cast<std::size_t> (lane)].model;
                const auto role = roles.explicitRoles ? modelOfRole (roles.role[static_cast<std::size_t> (lane)]) : roleModel[lane];
                // A genre kit's percussion pad may be a snare layer (§2.3: Trance "snare roll layer",
                // Metal "rim-shot layer", DnB "ghost snare"): there only a core role on another model,
                // or a kick model on another role, is a mismatch.
                const bool percussionLayer = roles.explicitRoles && roles.role[static_cast<std::size_t> (lane)] == PadRole::percussion
                                          && model == Model::snare;
                const bool coreMismatch = model != role && (core (model) || core (role)) && ! percussionLayer;
                rows += model != role ? 1 : 0;
                if (model != role)
                    report << "| " << factoryKitInfo (k).name << " | " << styleName (static_cast<Style> (style)) << " | " << lane + 1 << " | "
                           << modelName (role) << " | " << modelName (model) << " | " << (hits[static_cast<std::size_t> (lane)] * 1000 / std::max (1L, bars))
                           << " | " << (coreMismatch ? "**yes**" : "no") << " |\n";
                check (! coreMismatch, std::string ("generator: ") + styleName (static_cast<Style> (style)) + " on the " + factoryKitInfo (k).name
                                           + " kit writes " + modelName (role) + " hits to lane " + std::to_string (lane + 1) + ", whose pad is a " + modelName (model));
            }
        }
    }
    if (rows == 0)
        report << "| none | | | | | | |\n";
    report << "\nRows appear only where model and role differ and the lane receives hits.\n\n";
    // A user kit with pads 1 and 2 swapped: reported, not asserted.
    auto kitSwapped = factoryKit (0);
    std::swap (kitSwapped.pads[0], kitSwapped.pads[1]);
    GeneratorSettings g;
    g.style = Style::fullOn;
    g.seed = 1;
    testutil::emptyGrid (*p, 16, 4);
    generatePattern (g, *p);
    int lane1 = 0, lane2 = 0;
    for (int bar = 0; bar < p->bars; ++bar)
        for (int step = 0; step < 16; ++step)
        {
            lane1 += testutil::stepAt (*p, 0, bar, step).on ? 1 : 0;
            lane2 += testutil::stepAt (*p, 1, bar, step).on ? 1 : 0;
        }
    report << "### A user kit with pads 1 and 2 swapped (reported, not asserted)\n\n"
              "Psy Full-on kit with pad 1 = " << modelName (kitSwapped.pads[0].model) << " model and pad 2 = " << modelName (kitSwapped.pads[1].model)
           << " model; Full-on / Psy, seed 1, 4 bars: lane 1 gets " << lane1 << " hits (the four-on-the-floor), lane 2 gets " << lane2
           << ". The rhythm grammar (a) holds; the audible roles (b) do not: the snare model plays the kick pattern. The generator "
              "receives no kit information. The only role data in the state is the pad model; a user sample on a pad says nothing "
              "about its sound.\n\n";
}

// Listening examples: 3 seeds per style, 4 bars at 16 steps, factory note map.
bool sameStep (const kit::PatternStep& a, const kit::PatternStep& b)
{
    return a.on == b.on && a.accent == b.accent && a.velocity == b.velocity && a.probability == b.probability
        && a.ratchet == b.ratchet && a.microTicks == b.microTicks && a.locked == b.locked;
}

void testMutate()
{
    auto p = std::make_unique<StoredPattern>();
    auto before = std::make_unique<StoredPattern>();
    auto again = std::make_unique<StoredPattern>();
    auto reference = std::make_unique<StoredPattern>();
    const std::uint32_t masks[] = { 0u, 1u << closedHat, (1u << openHat) | (1u << perc1), (1u << kick) | (1u << snare), 0xffffu };
    const std::uint16_t lockSets[] = { 0, static_cast<std::uint16_t> (1u << snare), 0xffff };
    long cases = 0, wrong = 0;
    std::string first;
    for (int style = 0; style < styleCount; ++style)
        for (std::uint32_t seed = 1; seed <= 6; ++seed)
            for (std::uint32_t mask : masks)
                for (std::uint16_t locks : lockSets)
                {
                    // A baseline in another style and seed, with two locked steps.
                    GeneratorSettings g0;
                    g0.style = static_cast<Style> ((style + 3) % styleCount);
                    g0.seed = seed + 100u;
                    testutil::emptyGrid (*p, 16, 4);
                    generatePattern (g0, *p);
                    testutil::stepAt (*p, closedHat, 1, 3).on = true;
                    testutil::stepAt (*p, closedHat, 1, 3).locked = true;
                    testutil::stepAt (*p, kick, 2, 5).locked = true;
                    GeneratorSettings g;
                    g.style = static_cast<Style> (style);
                    g.seed = seed;
                    g.fills = 0.0f;
                    g.laneLocks = locks;
                    *before = *p;
                    mutateLanes (g, mask, *p);
                    *again = *before;
                    mutateLanes (g, mask, *again);
                    // The selected style's lanes: a fresh pattern with no fill and no locks.
                    auto r = g;
                    r.laneLocks = 0;
                    testutil::emptyGrid (*reference, 16, 4);
                    generatePattern (r, *reference);
                    ++cases;
                    bool ok = *again == *p;
                    for (int lane = 0; lane < kit::padCount; ++lane)
                        for (int bar = 0; bar < p->bars; ++bar)
                            for (int step = 0; step < testutil::stepsOf (*p); ++step)
                            {
                                const auto& now = testutil::stepAt (*p, lane, bar, step);
                                const auto& was = testutil::stepAt (*before, lane, bar, step);
                                const bool selected = (mask & (1u << lane)) != 0 && (locks & (1u << lane)) == 0;
                                ok = ok && (selected && ! was.locked ? sameStep (now, testutil::stepAt (*reference, lane, bar, step)) : sameStep (now, was));
                            }
                    if (! ok)
                    {
                        ++wrong;
                        if (first.empty())
                            first = std::string (styleName (g.style)) + " seed " + std::to_string (seed) + " mask " + std::to_string (mask) + " locks " + std::to_string (locks);
                    }
                }
    check (wrong == 0, "generator: mutate lanes changes only the selected unlocked lanes, in the selected style, deterministically (" + std::to_string (wrong) + " of " + std::to_string (cases) + " wrong" + (first.empty() ? "" : ", first: " + first) + ")");
    report << "Mutate Lanes: " << cases << " cases (7 styles x 6 seeds x masks {none, closed hat, open hat + perc 1, kick + snare, all} x locks {none, snare lane, all}), " << wrong << " wrong.\n\n";
}

// Exports a pattern, writes it, reads it back with juce::MidiFile and compares it with the note list.
void checkMidiRoundTrip (const StoredPattern& p, const std::array<std::uint8_t, kit::padCount>& notes, double bpm, const juce::File& file, const std::string& name)
{
    kit::StepContext context;
    ExportOptions options;
    options.bpm = bpm;
    const auto midi = exportMidi (p.view(), context, notes, options, name);
    auto expected = exportNotes (p.view(), context, notes, options);
    finishNotes (expected);
    const bool written = midi.getSize() > 0 && file.replaceWithData (midi.getData(), midi.getSize());
    juce::MidiFile parsed;
    juce::FileInputStream in (file);
    const bool read = written && in.openedOk() && parsed.readFrom (in) && parsed.getNumTracks() == 1 && parsed.getTimeFormat() == options.ticksPerQuarter;
    std::vector<std::tuple<long long, int, int, int>> ons;
    if (read)
    {
        const auto& track = *parsed.getTrack (0);
        for (int i = 0; i < track.getNumEvents(); ++i)
        {
            const auto& m = track.getEventPointer (i)->message;
            if (m.isNoteOn())
                ons.emplace_back (std::llround (m.getTimeStamp()), m.getNoteNumber(), m.getVelocity(), m.getChannel());
        }
    }
    bool same = read && ! expected.empty() && ons.size() == expected.size();
    const long long end = static_cast<long long> (p.bars) * 4 * options.ticksPerQuarter;
    for (std::size_t i = 0; same && i < ons.size(); ++i)
        same = std::get<0> (ons[i]) == expected[i].tick && std::get<1> (ons[i]) == expected[i].note && std::get<2> (ons[i]) == expected[i].velocity
            && std::get<3> (ons[i]) == 10 && expected[i].tick >= 0 && expected[i].tick < end;
    check (same, "generator: MIDI example " + name + " is written, reads back as SMF and matches the exported notes ("
                     + std::to_string (ons.size()) + " of " + std::to_string (expected.size()) + " notes)");
}

void writeMidiExamples (const juce::File& folder)
{
    folder.createDirectory();
    const auto kitSettings = factoryKit (0);
    std::array<std::uint8_t, kit::padCount> notes {};
    for (int i = 0; i < kit::padCount; ++i)
        notes[static_cast<std::size_t> (i)] = kitSettings.pads[static_cast<std::size_t> (i)].note;
    static constexpr double bpm[styleCount] = { 145.0, 145.0, 126.0, 130.0, 140.0, 92.0, 90.0 };
    static const char* file[styleCount] = { "full-on", "goa", "house", "techno", "trap", "breaks", "ambient" };
    auto p = std::make_unique<StoredPattern>();
    report << "## Listening examples\n\n`generator-audit/midi/<style>-seed<n>.mid`: 3 seeds per style, density 0.6, complexity 0.5, "
              "syncopation 0.4, fills 0.5, 16 steps x 4 bars, factory note map (36 kick, 38 snare, 39 clap, 42/46 hats, ...), channel 10, "
              "one pass. Tempo is written only for listening. Musical quality needs the owner's listening review.\n\n";
    for (int style = 0; style < styleCount; ++style)
        for (std::uint32_t seed = 1; seed <= 3; ++seed)
        {
            GeneratorSettings g;
            g.style = static_cast<Style> (style);
            g.seed = seed;
            g.density = 0.6f;
            g.complexity = 0.5f;
            g.syncopation = 0.4f;
            testutil::emptyGrid (*p, 16, 4);
            generatePattern (g, *p);
            kit::StepContext context;
            ExportOptions options;
            options.bpm = bpm[style];
            const auto midi = exportMidi (p->view(), context, notes, options, juce::String (styleName (g.style)) + " seed " + juce::String (seed));
            const auto out = folder.getChildFile (juce::String (file[style]) + "-seed" + juce::String (seed) + ".mid");
            check (midi.getSize() > 0 && out.replaceWithData (midi.getData(), midi.getSize()), "generator: writes " + out.getFileName().toStdString());
        }
}

void writeReport (const char* path)
{
    std::ostringstream head;
    head << "# VOX Drums generator style audit (issue 128)\n\n"
            "Generated by `suite_drums_generator_style_tests` (tests/suite/DrumsGeneratorStyleTests.cpp). "
            "Event grids only; no audio is rendered and no musical quality is claimed.\n\n"
            "## (a) Rhythm grammar\n\n"
            "| Style / context | Patterns | Bars | Hits per bar | Violations |\n|---|---|---|---|---|\n";
    for (const auto& [key, t] : tallies)
        head << "| " << key << " | " << t.patterns << " | " << t.bars << " | " << (t.bars ? static_cast<double> (t.hits) / t.bars : 0.0) << " | " << t.violations << " |\n";
    head << "\n### Where the hits fall (share of bars with a hit on each 16th, pattern context)\n\n"
            "| Style | Lane | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |\n|---|---|"
         << "---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (int style = 0; style < styleCount; ++style)
    {
        const auto& t = tallies[std::string (styleName (static_cast<Style> (style))) + " · pattern"];
        const auto row = [&] (const char* lane, const std::array<long, 16>& a) {
            head << "| " << styleName (static_cast<Style> (style)) << " | " << lane << " |";
            for (long v : a)
            {
                char b[16];
                std::snprintf (b, sizeof b, " %.2f |", t.bars ? static_cast<double> (v) / t.bars : 0.0);
                head << b;
            }
            head << "\n";
        };
        row ("kick", t.kickAt);
        row ("snare/clap", t.backAt);
        row ("hats", t.hatAt);
    }
    head << "\n### Violations\n\n";
    bool any = false;
    for (const auto& [key, t] : tallies)
        for (const auto& e : t.examples)
        {
            head << "- " << e << "\n";
            any = true;
        }
    if (! any)
        head << "None.\n";
    head << "\n";
    juce::File (juce::String (path)).replaceWithText (juce::String::fromUTF8 ((head.str() + report.str()).c_str()));
}

} // namespace

//==============================================================================
// PR 137 §3: the 16 new styles. Each check is a "Distinctions that tests
// check" line of §3 (or §2.4/§10.6), measured on generated patterns with the
// style's defaults, on the kit map of its genre (§2.3: Trance or Metal for
// the parts ride / tom 2 / tom 3, the default map otherwise).
namespace v2 {
constexpr int pKick = 0, pSnare = 1, pClap = 2, pClosed = 3, pOpen = 4, pTom1 = 5, pRim = 6, pCrash = 7, pPerc1 = 8, pPerc2 = 9,
              pRide = 16, pTom2 = 17, pTom3 = 18;
using Roles = std::array<PadRole, kit::padCount>;
constexpr auto K = PadRole::kick, S = PadRole::snare, C = PadRole::clap, CH = PadRole::closedHat, OH = PadRole::openHat,
               T1 = PadRole::tom1, R = PadRole::rim, CY = PadRole::crash, P = PadRole::percussion, RD = PadRole::ride, T2 = PadRole::tom2,
               T3 = PadRole::tom3;
constexpr Roles tranceMap { K, S, C, CH, OH, T1, R, CY, RD, T2, T3, P, P, P, P, P };
constexpr Roles metalMap { K, S, R, CH, OH, T1, T2, T3, CY, RD, P, P, P, P, P, P };

RoleMap mapFor (int id)
{
    PadRoles roles;
    roles.explicitRoles = true;
    const auto* info = style::infoById (id);
    const juce::String group = info != nullptr ? info->group : "";
    const auto* d = style::factoryDefinition (id);
    const bool extended = d != nullptr && (d->declaredParts() & ((1u << pRide) | (1u << pTom2) | (1u << pTom3))) != 0;
    // A style with ride / tom 2 / tom 3 outside Rock & Metal uses the Trance map.
    roles.role = group == "Rock & Metal" ? metalMap : extended ? tranceMap : PadRoles::identity();
    return RoleMap::from (roles);
}

GeneratorSettings defaultsOf (int id, std::uint32_t seed)
{
    GeneratorSettings g;
    g.style = static_cast<Style> (id);
    const auto* d = style::factoryDefinition (id);
    const auto& v = d->defaults.values;   // density, groove, syncopation, complexity, fills, variation, swing, humanize
    g.density = v[0];
    g.groove = v[1];
    g.syncopation = v[2];
    g.complexity = v[3];
    g.fills = v[4];
    g.variation = v[5];
    g.swing = v[6];
    g.humanize = v[7];
    g.seed = seed;
    return g;
}

// Generated bars of style `id` (4/4, 16ths, 4 bars), seeds 1..n.
struct Bars {
    std::vector<std::unique_ptr<StoredPattern>> patterns;
    RoleMap map;
    int count() const { return static_cast<int> (patterns.size()) * 4; }
    const StoredPattern& pattern (int bar) const { return *patterns[static_cast<std::size_t> (bar / 4)]; }
    const kit::PatternStep* at (int part, int bar, int pos) const
    {
        const int lane = part >= 0 && part < RoleMap::parts ? map.laneOfSlot[static_cast<std::size_t> (part)] : -1;
        if (lane < 0)
            return nullptr;
        const auto& p = pattern (bar);
        const int s = testutil::slotOfStep (16, pos);
        return &p.cell (lane, bar % 4, s);
    }
    bool on (int part, int bar, int pos) const
    {
        const auto* c = at (part, bar, pos);
        return c != nullptr && c->on;
    }
    int hits (int part, int bar) const
    {
        int n = 0;
        for (int pos = 0; pos < 16; ++pos)
            n += on (part, bar, pos) ? 1 : 0;
        return n;
    }
};

Bars generate (int id, int seeds, std::function<void (GeneratorSettings&)> tweak = {})
{
    Bars b;
    b.map = mapFor (id);
    for (int seed = 1; seed <= seeds; ++seed)
    {
        auto g = defaultsOf (id, static_cast<std::uint32_t> (seed));
        g.fills = 0.0f;
        if (tweak)
            tweak (g);
        auto p = std::make_unique<StoredPattern>();
        testutil::emptyGrid (*p, 16, 4);
        generatePattern (g, *p, b.map);
        b.patterns.push_back (std::move (p));
    }
    return b;
}

int idOf (const char* key)
{
    const auto* info = style::infoByKey (key);
    return info != nullptr ? info->id : -1;
}
} // namespace v2

void testNewStyles()
{
    using namespace v2;
    constexpr int seeds = 25;
    // Every factory style runs, its assertions hold, and every meter x
    // resolution it defines generates valid patterns.
    {
        int ran = 0, invalid = 0;
        juce::StringArray broken;
        static constexpr timing::TimeSignature signatures[style::meterCount] = { { 4, 4 }, { 3, 4 }, { 6, 8 }, { 12, 8 } };
        for (int id = 0; id < factoryStyleCount; ++id)
        {
            const auto* d = style::factoryDefinition (id);
            if (d == nullptr)
            {
                broken.add ("style " + juce::String (id) + " has no definition");
                continue;
            }
            const auto violations = checkStyleAssertions (std::make_shared<style::Definition> (*d));
            if (! violations.isEmpty())
                broken.add (juce::String (style::infoById (id)->key) + ": " + violations[0]);
            for (int m = 0; m < style::meterCount; ++m)
                if ((d->meters >> m) & 1u)
                    for (int r = 0; r < timing::resolutionCount; ++r)
                    {
                        const auto res = static_cast<timing::Resolution> (r);
                        if (timing::capacity (signatures[m], res).availability == timing::Availability::unavailable)
                            continue;
                        for (std::uint32_t seed = 1; seed <= 3; ++seed)
                        {
                            auto g = defaultsOf (id, seed);
                            g.fills = 1.0f;
                            g.complexity = seed == 3 ? 1.0f : g.complexity;
                            auto p = std::make_unique<StoredPattern>();
                            p->signature = signatures[m];
                            p->resolution = res;
                            ++ran;
                            invalid += generatePattern (g, *p, mapFor (id)) && p->valid() ? 0 : 1;
                        }
                    }
        }
        check (broken.isEmpty(), "styles v2: all 23 factory styles are available and their assertions hold: " + broken.joinIntoString ("; ").toStdString());
        check (invalid == 0, "styles v2: " + std::to_string (ran) + " generations over every style x meter x resolution are valid patterns ("
                                 + std::to_string (invalid) + " invalid)");
    }
    const auto uplifting = generate (idOf ("uplifting-trance"), seeds), anthem = generate (idOf ("anthem-trance"), seeds);
    {
        bool up = true, an = true;
        for (int bar = 0; bar < uplifting.count(); ++bar)
        {
            up = up && ! uplifting.on (pSnare, bar, 4) && ! uplifting.on (pSnare, bar, 12);
            an = an && anthem.on (pSnare, bar, 4) && anthem.on (pSnare, bar, 12);
        }
        check (up && an, "Uplifting vs Anthem: the snare on 5/13 is always on in Anthem and never in Uplifting (outside fills)");
    }
    {
        const auto melodic = generate (idOf ("melodic-trance"), seeds);
        int maxMelodic = 0, maxUplifting = 0;
        for (int bar = 0; bar < melodic.count(); ++bar)
            for (int part = 1; part < RoleMap::parts; ++part)
                for (int pos = 0; pos < 16; ++pos)
                {
                    if (const auto* c = melodic.at (part, bar, pos); c != nullptr && c->on)
                        maxMelodic = std::max<int> (maxMelodic, c->velocity);
                    if (const auto* c = uplifting.at (part, bar, pos); c != nullptr && c->on)
                        maxUplifting = std::max<int> (maxUplifting, c->velocity);
                }
        check (maxMelodic <= 114 && maxUplifting > 114,
               "Melodic vs Uplifting: non-kick velocity <= 0.9 in Melodic (" + std::to_string (maxMelodic) + "), above in Uplifting ("
                   + std::to_string (maxUplifting) + ")");
    }
    {
        const auto prog = generate (idOf ("progressive-trance"), seeds);
        bool openOnly15 = true;
        int rolling = 0;
        for (int bar = 0; bar < prog.count(); ++bar)
        {
            for (int pos = 0; pos < 16; ++pos)
                openOnly15 = openOnly15 && (pos == 14 || ! prog.on (pOpen, bar, pos));
            for (int pos : { 3, 6, 9, 11, 14 })
                rolling += prog.on (pPerc1, bar, pos) ? 1 : 0;
        }
        check (openOnly15 && rolling > prog.count() * 2, "Progressive: rolling percussion (" + std::to_string (rolling) + " hits); open hat only on 15");
    }
    {
        const auto dream = generate (idOf ("dream-trance"), seeds);
        bool ride = true;
        for (int bar = 0; bar < dream.count(); ++bar)
            for (int pos = 0; pos < 16; pos += 2)
                ride = ride && dream.on (pRide, bar, pos);
        check (ride, "Dream: a ride on every 8th");
    }
    {
        const auto hard = generate (idOf ("hard-trance"), seeds);
        bool hats = true, kick = true;
        for (int bar = 0; bar < hard.count(); ++bar)
            for (int pos = 0; pos < 16; ++pos)
            {
                hats = hats && hard.on (pClosed, bar, pos);
                kick = kick && (pos % 4 == 0) == hard.on (pKick, bar, pos);
            }
        // With fills, the kick leaves the beat only in the fill window of the last bar.
        const auto filled = generate (idOf ("hard-trance"), seeds, [] (GeneratorSettings& g) { g.fills = 1.0f; });
        int roll = 0;
        bool outside = true;
        for (int bar = 0; bar < filled.count(); ++bar)
            for (int pos = 0; pos < 16; ++pos)
                if (pos % 4 != 0 && filled.on (pKick, bar, pos))
                {
                    const bool window = bar % 4 == 3 && pos >= 8;
                    outside = outside && window;
                    roll += window ? 1 : 0;
                }
        check (hats && kick && outside && roll > 0, "Hard Trance: closed hats on all 16ths; the kick leaves the beat only in fills (" + std::to_string (roll) + " roll hits)");
    }
    {
        const auto sym = generate (idOf ("symphonic-trance"), seeds);
        int t1 = 0, t2 = 0, t3 = 0;
        for (int bar = 0; bar < sym.count(); ++bar)
        {
            t1 += sym.hits (pTom1, bar);
            t2 += sym.hits (pTom2, bar);
            t3 += sym.hits (pTom3, bar);
        }
        check (t1 > 0 && t2 > 0 && t3 > sym.count() / 2, "Symphonic Trance: the three toms (" + std::to_string (t1) + "/" + std::to_string (t2) + "/" + std::to_string (t3) + ")");
    }
    // Psy.
    {
        const auto dark = generate (idOf ("dark-psy"), seeds), fullOn = generate (0, seeds), techno = generate (3, seeds);
        int darkClap = 0, fullClap = 0, ratchetBars = 0;
        bool hatOnBeat = false, technoBeats = true, both = false;
        for (int bar = 0; bar < dark.count(); ++bar)
        {
            const bool c5 = dark.on (pClap, bar, 4), c13 = dark.on (pClap, bar, 12);
            darkClap += c5 || c13 ? 1 : 0;
            both = both || (c5 && c13);
            fullClap += fullOn.on (pClap, bar, 4) || fullOn.on (pClap, bar, 12) ? 1 : 0;
            for (int pos : { 0, 4, 8, 12 })
            {
                hatOnBeat = hatOnBeat || dark.on (pClosed, bar, pos);
                technoBeats = technoBeats && techno.on (pClosed, bar, pos);
            }
            bool ratchet = false;
            for (int part : { pPerc1, pRim })
                for (int pos = 0; pos < 16; ++pos)
                    if (const auto* c = dark.at (part, bar, pos); c != nullptr && c->on && c->ratchet > 1)
                        ratchet = true;
            ratchetBars += ratchet ? 1 : 0;
        }
        const int n = dark.count();
        check (darkClap * 4 <= n && fullClap * 10 >= n * 9 && ! both,
               "Dark Psy vs Full-on: clap on 5/13 in " + std::to_string (darkClap) + " of " + std::to_string (n) + " bars (<= 25 %) vs "
                   + std::to_string (fullClap) + " (>= 90 %); never on both");
        check (! hatOnBeat && technoBeats, "Dark Psy vs Techno: Techno has a closed hat on all four beats; Dark Psy never");
        check (ratchetBars * 10 >= n * 3, "Dark Psy: ratcheted percussion in " + std::to_string (ratchetBars) + " of " + std::to_string (n) + " bars (>= 30 %)");
    }
    // House and techno family.
    {
        const auto house = generate (2, seeds), tech = generate (idOf ("tech-house"), seeds), techno = generate (3, seeds),
                   melodic = generate (idOf ("melodic-techno"), seeds);
        int housePickup = 0, techPickup = 0, percBars = 0;
        bool melodicBeats = false;
        for (int bar = 0; bar < house.count(); ++bar)
        {
            housePickup += house.on (pKick, bar, 15) ? 1 : 0;
            techPickup += tech.on (pKick, bar, 15) ? 1 : 0;
            int positions = 0;
            for (int pos = 0; pos < 16; ++pos)
                positions += tech.on (pPerc1, bar, pos) || tech.on (pPerc2, bar, pos) || tech.on (pRim, bar, pos) ? 1 : 0;
            percBars += positions >= 4 ? 1 : 0;
            for (int pos : { 0, 4, 8, 12 })
                melodicBeats = melodicBeats || melodic.on (pClosed, bar, pos);
        }
        check (housePickup > 0 && techPickup == 0, "House vs Tech House: the kick pickup on 16 only in House (" + std::to_string (housePickup) + " bars)");
        check (percBars == tech.count(), "Tech House: percussion on >= 4 positions in every bar at density 0.6");
        check (! melodicBeats, "Techno vs Melodic Techno: Melodic Techno never has a closed hat on the beats");
    }
    // Drum and bass.
    {
        const auto dnb = generate (idOf ("drum-and-bass"), seeds), mel = generate (idOf ("melodic-drum-and-bass"), seeds),
                   breaks = generate (5, seeds), trap = generate (4, seeds);
        bool dnbKick = true, melGhost = true, melRide = true, dnbSnare = true, trapSnare = true;
        int breaksGhost = 0, dnbGhost = 0;
        for (int bar = 0; bar < dnb.count(); ++bar)
        {
            for (int pos : { 3, 6, 13, 4, 12 })
                dnbKick = dnbKick && ! dnb.on (pKick, bar, pos);
            for (int pos : { 3, 6, 13 })
                breaksGhost += breaks.on (pKick, bar, pos) ? 1 : 0;
            int ghosts = 0;
            for (int pos = 0; pos < 16; ++pos)
                if (pos != 4 && pos != 12)
                {
                    ghosts += mel.on (pSnare, bar, pos) ? 1 : 0;
                    dnbGhost += dnb.on (pSnare, bar, pos) ? 1 : 0;
                }
            melGhost = melGhost && ghosts <= 1;
            for (int pos = 0; pos < 16; pos += 2)
                melRide = melRide && mel.on (pRide, bar, pos);
            dnbSnare = dnbSnare && dnb.on (pSnare, bar, 4) && dnb.on (pSnare, bar, 12);
            trapSnare = trapSnare && trap.on (pSnare, bar, 8);
        }
        check (dnbKick && breaksGhost > 0, "Breaks vs DnB: kick ghosts on 4/7/14 in Breaks (" + std::to_string (breaksGhost) + "), never in DnB (nor on 5/13)");
        check (melGhost && melRide && dnbGhost > dnb.count(), "DnB vs Melodic DnB: at most one snare ghost per bar (" + std::string (melGhost ? "yes" : "no")
                                                                  + ") and a ride on every 8th (" + (melRide ? "yes" : "no") + ") in Melodic; DnB ghosts "
                                                                  + std::to_string (dnbGhost));
        check (dnbSnare && trapSnare, "Trap vs DnB: Trap's snare on 9, DnB's on 5 and 13");
    }
    // Rock and metal.
    {
        const auto metal = generate (idOf ("heavy-metal"), seeds), house = generate (2, seeds),
                   rock = generate (idOf ("hard-rock"), seeds), sym = generate (idOf ("symphonic-metal"), seeds),
                   nordic = generate (idOf ("nordic-metal"), seeds);
        bool houseGrid = true, metalKicks = true, metalBackbeat = true, rockKicks = true, symAlternates = true, nordicToms = true;
        int metalClap = 0;
        for (int bar = 0; bar < metal.count(); ++bar)
        {
            int hk = 0;
            for (int pos = 0; pos < 16; ++pos)
                hk += house.on (pKick, bar, pos) && pos != 15 ? 1 : 0;
            houseGrid = houseGrid && hk == 4 && house.on (pClap, bar, 4) && house.on (pClap, bar, 12);
            metalKicks = metalKicks && metal.hits (pKick, bar) >= 8;
            metalBackbeat = metalBackbeat && metal.on (pSnare, bar, 4) && metal.on (pSnare, bar, 12);
            metalClap += metal.hits (pClap, bar);
            rockKicks = rockKicks && rock.hits (pKick, bar) <= 4;
            const bool odd = bar % 2 == 0;   // bars 1, 3
            symAlternates = symAlternates && (odd ? sym.hits (pKick, bar) == 16 && sym.on (pSnare, bar, 4)
                                                  : sym.hits (pKick, bar) == 2 && sym.on (pKick, bar, 10) && sym.on (pSnare, bar, 8) && ! sym.on (pSnare, bar, 4));
            for (int pos : { 0, 4, 8, 12 })
                nordicToms = nordicToms && nordic.on (pTom3, bar, pos);
        }
        check (houseGrid && metalKicks && metalBackbeat && metalClap == 0,
               "Heavy Metal vs House: House 4 kicks on the beats and a clap on 5/13; Heavy Metal >= 8 kicks per bar at density 0.8, snare 5/13, never a clap");
        check (rockKicks, "Hard Rock vs Heavy Metal: at most 4 kicks per bar outside fills in Hard Rock");
        check (symAlternates, "Symphonic Metal: double-bass bars (1, 3) and half-time bars (2, 4: kick 1/11, snare 9) alternate");
        check (nordicToms, "Nordic Metal: tom 3 on the beats");
    }
    // Meters (§10.6): the styles that list them generate there; the rest refuse.
    {
        const std::pair<const char*, timing::TimeSignature> listed[] = {
            { "nordic-metal", { 6, 8 } }, { "nordic-metal", { 12, 8 } }, { "symphonic-metal", { 12, 8 } }, { "symphonic-metal", { 3, 4 } },
            { "heavy-metal", { 12, 8 } }, { "hard-rock", { 12, 8 } }, { "symphonic-trance", { 3, 4 } }, { "dream-trance", { 3, 4 } } };
        bool ok = true;
        for (const auto& [key, sig] : listed)
        {
            auto p = std::make_unique<StoredPattern>();
            p->signature = sig;
            ok = ok && generatePattern (defaultsOf (idOf (key), 1), *p, mapFor (idOf (key))) && p->valid();
        }
        auto p = std::make_unique<StoredPattern>();
        p->signature = { 3, 4 };
        const bool refused = ! generatePattern (defaultsOf (idOf ("dark-psy"), 1), *p) && ! generatePattern (defaultsOf (idOf ("heavy-metal"), 1), *p);
        check (ok && refused, "meters: Nordic 6/8 + 12/8, Symphonic Metal 12/8 + 3/4, Heavy Metal and Hard Rock 12/8, Symphonic and Dream Trance 3/4; "
                              "Dark Psy and Heavy Metal refuse 3/4");
    }
    // Triplet rolls (§10.6) only in the styles that have them.
    {
        juce::StringArray with;
        for (int id = 0; id < factoryStyleCount; ++id)
        {
            auto p = std::make_unique<StoredPattern>();
            testutil::emptyGrid (*p, 16, 4);
            auto g = defaultsOf (id, 1);
            g.fills = 1.0f;
            g.complexity = 1.0f;
            generatePattern (g, *p, mapFor (id));
            bool any = false;
            for (int lane = 0; lane < kit::padCount; ++lane)
                for (int bar = 0; bar < p->bars; ++bar)
                    any = any || p->mask (lane, bar) != 0;
            if (any)
                with.add (style::infoById (id)->key);
        }
        const juce::String expected = "uplifting-trance anthem-trance hard-rock heavy-metal symphonic-metal nordic-metal drum-and-bass symphonic-trance";
        check (with.joinIntoString (" ") == expected, "triplets: only " + expected.toStdString() + " (got " + with.joinIntoString (" ").toStdString() + ")");
    }
}

int main (int argc, char** argv)
{
    testGrammarMatrix();
    testSongs();
    testFills();
    testKitRoles();
    testMutate();
    testNewStyles();
    {
        // Two examples round-trip through a file even without a report folder.
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vox-drums-generator-" + juce::Uuid().toString());
        folder.createDirectory();
        const auto kitSettings = factoryKit (0);
        std::array<std::uint8_t, kit::padCount> notes {};
        for (int i = 0; i < kit::padCount; ++i)
            notes[static_cast<std::size_t> (i)] = kitSettings.pads[static_cast<std::size_t> (i)].note;
        auto p = std::make_unique<StoredPattern>();
        for (const auto style : { Style::fullOn, Style::trap })
        {
            GeneratorSettings g;
            g.style = style;
            g.seed = 1;
            g.density = 0.6f;
            g.complexity = 0.5f;
            g.syncopation = 0.4f;
            testutil::emptyGrid (*p, 16, 4);
            generatePattern (g, *p);
            checkMidiRoundTrip (*p, notes, 140.0, folder.getChildFile (juce::String (styleName (style)).retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") + ".mid"), styleName (style));
        }
        if (folder.getFileName().startsWith ("vox-drums-generator-"))
            folder.deleteRecursively();
    }
    for (const auto& [key, t] : tallies)
        check (t.violations == 0, "generator grammar: " + key + ": " + std::to_string (t.violations) + " violations"
                                      + (t.examples.empty() ? std::string() : " (first: " + t.examples.front() + ")"));
    if (argc > 1)
    {
        if (argc > 2)
            writeMidiExamples (juce::File (juce::String (argv[2])));
        writeReport (argv[1]);
    }
    if (failures == 0)
        std::printf ("VOX Drums generator style tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
