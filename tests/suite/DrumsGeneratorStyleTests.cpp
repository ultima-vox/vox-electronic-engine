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
// (c) Reproducibility: same settings + seed + input give the same pattern;
//     a pattern's previous content (unlocked) does not change the result.
//
// With an argument, writes a Markdown report (statistics and every
// violation found) to that path.
#include "FactoryContent.h"
#include "Generator.h"
#include "PatternExport.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
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

bool hit (const StoredPattern& p, int lane, int bar, int pos)
{
    return p.cell (lane, bar, pos * (p.steps / 16)).on;
}

int fillLength (const GeneratorSettings& g)
{
    return std::clamp (static_cast<int> (std::lround (2.0 + 6.0 * g.fills)), 2, 8);
}

std::string describe (const GeneratorSettings& g, const StoredPattern& p)
{
    char b[256];
    std::snprintf (b, sizeof b, "%s seed %u density %.2f complexity %.2f syncopation %.2f fills %.2f variation %.2f, %d steps x %d bars",
                   styleName (g.style), g.seed, g.density, g.complexity, g.syncopation, g.fills, g.variation, p.steps, p.bars);
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
    const int scale = p.steps / 16;
    for (int bar = 0; bar < p.bars; ++bar)
    {
        ++t.bars;
        for (int lane = 0; lane < kit::padCount; ++lane)
            for (int step = 0; step < p.steps; ++step)
                if (p.cell (lane, bar, step).on)
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

void prefill (StoredPattern& p, std::uint32_t salt)
{
    for (std::size_t i = 0; i < p.cells.size(); ++i)
        p.cells[i].on = ((i * 2654435761u + salt) >> 7) % 3 == 0;
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
                                    a->cells.fill ({});
                                    a->resize (grid[0], grid[1]);
                                    generatePattern (g, *a);
                                    const std::string name = styleName (g.style);
                                    checkGrammar (g.style, d, *a, f > 0.05f && a->bars > 1, fillLength (g), true, tallies[name + " · pattern"], describe (g, *a) + " (pattern)");
                                    b->resize (grid[0], grid[1]);
                                    generateVariation (g, *a, *b);
                                    checkGrammar (g.style, d, *b, true, fillLength (g), true, tallies[name + " · variation B"], describe (g, *b) + " (variation B)");
                                    // (c) the previous content does not leak into an unlocked pattern
                                    if (seed <= 2 && cx == 0.5f && sy == 0.5f)
                                    {
                                        again->resize (grid[0], grid[1]);
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
                    for (auto& s : all->slots) s.cells.fill ({});
                    for (auto& s : all->alternates) s.cells.fill ({});
                    all->song = SongData::initial();
                    all->song.length = static_cast<std::uint8_t> (length);
                    for (auto& s : all->slots) s.resize (16, 4);
                    for (auto& s : all->alternates) s.resize (16, 4);
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
                p->resize (16, 4);
                generatePattern (g, *p);
                g.fills = f;
                generateFill (g, *p, 1);
                g.seed = seed + 1000u;
                const std::string name = styleName (g.style);
                // bar 2 has the fill; check it as the last bar of a 2-bar slice
                auto slice = std::make_unique<StoredPattern>();
                slice->resize (16, 2);
                for (int lane = 0; lane < kit::padCount; ++lane)
                    for (int bar = 0; bar < 2; ++bar)
                        for (int step = 0; step < 16; ++step)
                            slice->cell (lane, bar, step) = p->cell (lane, bar, step);
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
                        p->resize (16, 4);
                        generatePattern (g, *p);
                        bars += p->bars;
                        for (int lane = 0; lane < kit::padCount; ++lane)
                            for (int bar = 0; bar < p->bars; ++bar)
                                for (int step = 0; step < p->steps; ++step)
                                    hits[static_cast<std::size_t> (lane)] += p->cell (lane, bar, step).on ? 1 : 0;
                    }
            for (int lane = 0; lane < kit::padCount; ++lane)
            {
                if (hits[static_cast<std::size_t> (lane)] == 0)
                    continue;
                const auto model = kitSettings.pads[static_cast<std::size_t> (lane)].model;
                const auto role = roleModel[lane];
                const bool coreMismatch = model != role && (core (model) || core (role));
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
    p->resize (16, 4);
    generatePattern (g, *p);
    int lane1 = 0, lane2 = 0;
    for (int bar = 0; bar < p->bars; ++bar)
        for (int step = 0; step < 16; ++step)
        {
            lane1 += p->cell (0, bar, step).on ? 1 : 0;
            lane2 += p->cell (1, bar, step).on ? 1 : 0;
        }
    report << "### A user kit with pads 1 and 2 swapped (reported, not asserted)\n\n"
              "Psy Full-on kit with pad 1 = " << modelName (kitSwapped.pads[0].model) << " model and pad 2 = " << modelName (kitSwapped.pads[1].model)
           << " model; Full-on / Psy, seed 1, 4 bars: lane 1 gets " << lane1 << " hits (the four-on-the-floor), lane 2 gets " << lane2
           << ". The rhythm grammar (a) holds; the audible roles (b) do not: the snare model plays the kick pattern. The generator "
              "receives no kit information. The only role data in the state is the pad model; a user sample on a pad says nothing "
              "about its sound.\n\n";
}

// Listening examples: 3 seeds per style, 4 bars at 16 steps, factory note map.
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
            p->cells.fill ({});
            p->resize (16, 4);
            generatePattern (g, *p);
            kit::StepContext context;
            ExportOptions options;
            options.bpm = bpm[style];
            const auto midi = exportMidi (p->view(), context, notes, options, juce::String (styleName (g.style)) + " seed " + juce::String (seed));
            folder.getChildFile (juce::String (file[style]) + "-seed" + juce::String (seed) + ".mid").replaceWithData (midi.getData(), midi.getSize());
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

int main (int argc, char** argv)
{
    testGrammarMatrix();
    testSongs();
    testFills();
    testKitRoles();
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
