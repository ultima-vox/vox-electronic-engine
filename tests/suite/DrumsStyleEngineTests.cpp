// VOX Drums declarative style engine, foundation (issue 136, design PR 137 §11):
//
//  - registry: exactly 23 unique stable ids, keys and names in the order of
//    issue 136, House once; the 7 original styles available, the 16 new
//    ones listed but not yet generated;
//  - parity: the 7 original styles, now data run by one interpreter, give
//    byte-identical results to the switch-based generator they replace
//    (GeneratorReference.cpp, a test reference only) for Pattern, Variation
//    A>B, Fill, Mutate Lanes and Song, with step and lane locks, many seeds,
//    settings and grids, and identity, remapped, duplicate and partial role
//    maps;
//  - content hashes: deterministic and pinned per factory style;
//  - parser: strict, every rejection class names its field, limits hold,
//    random mutations never crash and stay bounded;
//  - formulas evaluate exactly as the equivalent C++ expression;
//  - an unavailable (registered, no definition) or unknown style changes
//    nothing: no cell, seed, lock, A/B or song bar, no history entry
//    (PR 138 review).
#include "GeneratorReference.h"
#include "Processor.h"
#include "StyleRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <set>

namespace {

using namespace vox::drums;
namespace st = vox::drums::style;

int failures = 0;

void check (bool condition, const juce::String& what)
{
    if (! condition)
    {
        std::printf ("FAIL [VOX Drums style engine]: %s\n", what.toRawUTF8());
        ++failures;
    }
}

//==============================================================================
void testRegistry()
{
    const auto& reg = st::registry();
    // Issue 136: the 7 original styles, then the 16 new ones in its order.
    static const char* names[st::registrySize] = {
        "Full-on / Psy", "Goa", "House", "Techno", "Trap", "Breaks", "Ambient",
        "Uplifting Trance", "Anthem Trance", "Dark Psy", "Melodic Trance", "Hard Rock", "Heavy Metal", "Symphonic Metal",
        "Nordic Metal", "Tech House", "Drum & Bass", "Melodic Drum & Bass", "Melodic Techno", "Progressive Trance",
        "Dream Trance", "Hard Trance", "Symphonic Trance",
    };
    std::set<juce::String> keys, lowerNames;
    int houses = 0;
    for (int i = 0; i < st::registrySize; ++i)
    {
        const auto& s = reg[static_cast<std::size_t> (i)];
        check (s.id == i, "registry: id " + juce::String (i) + " is in its place");
        check (juce::String (s.name) == names[i], "registry: id " + juce::String (i) + " is " + names[i] + " (got " + s.name + ")");
        check (juce::String (s.group).isNotEmpty() && s.tempoMin > 0 && s.tempoMin <= s.tempoMax, juce::String ("registry: metadata of ") + s.name);
        keys.insert (s.key);
        lowerNames.insert (juce::String (s.name).toLowerCase());
        houses += juce::String (s.name).equalsIgnoreCase ("House") ? 1 : 0;
        check (st::infoByKey (s.key) == &s && st::infoById (i) == &s, juce::String ("registry: lookup of ") + s.key);
        check (st::available (i) == (i < styleCount), juce::String ("registry: ") + s.name + (i < styleCount ? " is available" : " waits for its rules"));
    }
    check (keys.size() == st::registrySize && lowerNames.size() == st::registrySize, "registry: 23 unique keys and names");
    check (houses == 1, "registry: House appears exactly once");
    check (st::infoById (-1) == nullptr && st::infoById (st::registrySize) == nullptr && st::infoByKey ("polka") == nullptr,
           "registry: unknown ids and keys are not found");
    check (st::factoryParseError().isEmpty(), "registry: every factory style parses (" + st::factoryParseError() + ")");
    for (int i = 0; i < styleCount; ++i)
        check (juce::String (styleName (static_cast<Style> (i))) == names[i], juce::String ("registry: styleName follows the registry for ") + names[i]);
}

//==============================================================================
// Parity with the switch-based generator.
void empty (StoredPattern& p, int steps, int bars)
{
    p.steps = static_cast<std::uint8_t> (steps);
    p.bars = static_cast<std::uint8_t> (bars);
    p.seed = 1;
    p.cells.fill ({});
}

PadRoles explicitRoles (std::array<PadRole, kit::padCount> r)
{
    PadRoles roles;
    roles.explicitRoles = true;
    roles.role = r;
    return roles;
}

std::vector<std::pair<juce::String, RoleMap>> roleMaps()
{
    std::vector<std::pair<juce::String, RoleMap>> maps;
    maps.emplace_back ("identity", RoleMap::identity());
    auto swapped = PadRoles::identity();
    std::swap (swapped[0], swapped[1]);
    std::swap (swapped[3], swapped[10]);
    maps.emplace_back ("remapped", RoleMap::from (explicitRoles (swapped)));
    auto partial = PadRoles::identity();
    partial[2] = PadRole::kick;   // duplicate kick
    partial[5] = PadRole::none;
    partial[7] = PadRole::none;   // no cymbal
    maps.emplace_back ("duplicate and missing", RoleMap::from (explicitRoles (partial)));
    std::mt19937 random (136);
    auto shuffled = PadRoles::identity();
    std::shuffle (shuffled.begin(), shuffled.end(), random);
    maps.emplace_back ("shuffled", RoleMap::from (explicitRoles (shuffled)));
    return maps;
}

void testParity()
{
    static constexpr int grids[4][2] = { { 16, 4 }, { 64, 4 }, { 16, 1 }, { 32, 2 } };
    static constexpr std::uint32_t masks[4] = { 0x0008u, 0x0130u, 0x0003u, 0xffffu };
    auto a = std::make_unique<StoredPattern>(), b = std::make_unique<StoredPattern>();
    auto ra = std::make_unique<StoredPattern>(), rb = std::make_unique<StoredPattern>();
    auto all = std::make_unique<PatternStore::Everything>(), rall = std::make_unique<PatternStore::Everything>();
    const auto maps = roleMaps();
    std::array<int, styleCount> cases {}, bad {};
    for (int style = 0; style < styleCount; ++style)
        for (const auto& [mapName, map] : maps)
            for (std::uint32_t seed = 1; seed <= 4; ++seed)
                for (float density : { 0.0f, 0.4f, 0.8f, 1.0f })
                    for (float complexity : { 0.0f, 0.6f, 1.0f })
                        for (float syncopation : { 0.0f, 1.0f })
                            for (float fills : { 0.0f, 0.7f })
                                for (float variation : { 0.4f, 1.0f })
                                    for (const auto& grid : grids)
                                    {
                                        GeneratorSettings g;
                                        g.style = static_cast<Style> (style);
                                        g.seed = seed * 7919u + 3u;
                                        g.density = density;
                                        g.complexity = complexity;
                                        g.syncopation = syncopation;
                                        g.fills = fills;
                                        g.variation = variation;
                                        g.swing = 0.25f;
                                        g.humanize = 0.3f;
                                        g.groove = 0.6f;
                                        auto& n = cases[static_cast<std::size_t> (style)];
                                        auto& wrong = bad[static_cast<std::size_t> (style)];
                                        // Pattern, from a pre-filled pattern with a locked step and (every other seed) a locked lane.
                                        GeneratorSettings other = g;
                                        other.style = static_cast<Style> ((style + 3) % styleCount);
                                        other.seed = g.seed + 11u;
                                        empty (*a, grid[0], grid[1]);
                                        reference::generatePattern (other, *a);
                                        a->cell (4, 0, 1).locked = true;
                                        g.laneLocks = seed % 2 == 0 ? 0x0042u : 0u;
                                        *ra = *a;
                                        generatePattern (g, *a, map);
                                        reference::generatePattern (g, *ra, map);
                                        wrong += *a == *ra ? 0 : 1;
                                        // Variation B from A.
                                        empty (*b, grid[0], grid[1]);
                                        *rb = *b;
                                        generateVariation (g, *a, *b, map);
                                        reference::generateVariation (g, *a, *rb, map);
                                        wrong += *b == *rb ? 0 : 1;
                                        // Fill into bar 0 and the last bar.
                                        *b = *a;
                                        *rb = *a;
                                        generateFill (g, *b, 0, map);
                                        reference::generateFill (g, *rb, 0, map);
                                        generateFill (g, *b, -1, map);
                                        reference::generateFill (g, *rb, -1, map);
                                        wrong += *b == *rb ? 0 : 1;
                                        // Mutate Lanes with several masks.
                                        for (auto mask : masks)
                                        {
                                            *b = *a;
                                            *rb = *a;
                                            mutateLanes (g, mask, *b, map);
                                            reference::mutateLanes (g, mask, *rb, map);
                                            wrong += *b == *rb ? 0 : 1;
                                        }
                                        n += 7;
                                    }
    // Songs: every style, map, a few seeds and lengths.
    for (int style = 0; style < styleCount; ++style)
        for (const auto& [mapName, map] : maps)
            for (std::uint32_t seed = 1; seed <= 3; ++seed)
                for (int length : { 16, 32, 64 })
                {
                    GeneratorSettings g;
                    g.style = static_cast<Style> (style);
                    g.seed = seed;
                    g.density = 0.3f + 0.3f * static_cast<float> (seed);
                    g.fills = 0.6f;
                    for (auto* e : { all.get(), rall.get() })
                    {
                        for (int slot = 0; slot < PatternStore::slotCount; ++slot)
                        {
                            empty (e->slots[static_cast<std::size_t> (slot)], 16, 4);
                            empty (e->alternates[static_cast<std::size_t> (slot)], 16, 4);
                        }
                        e->song = SongData::initial();
                        e->song.length = static_cast<std::uint8_t> (length);
                        e->slots[1].cell (0, 2, 4).locked = true;
                    }
                    generateSong (g, *all, map);
                    reference::generateSong (g, *rall, map);
                    const bool same = all->slots == rall->slots && all->alternates == rall->alternates && all->song == rall->song;
                    bad[static_cast<std::size_t> (style)] += same ? 0 : 1;
                    cases[static_cast<std::size_t> (style)] += 1;
                }
    for (int style = 0; style < styleCount; ++style)
        check (bad[static_cast<std::size_t> (style)] == 0,
               juce::String ("parity: ") + styleName (static_cast<Style> (style)) + " as data equals the switch-based generator: "
                   + juce::String (bad[static_cast<std::size_t> (style)]) + " of " + juce::String (cases[static_cast<std::size_t> (style)])
                   + " results differ");
    std::printf ("parity: %d results compared per style\n", cases[0]);
}

//==============================================================================
void testHashes()
{
    // Pinned: a change to a factory definition must be deliberate and show
    // up here (and in the factory content manifest when that exists).
    static const char* pinned[styleCount] = {
#include "StyleHashes.inc"
    };
    std::set<juce::String> seen;
    for (int i = 0; i < styleCount; ++i)
    {
        const auto* d = st::factoryDefinition (i);
        if (d == nullptr)
            continue;
        const auto h = st::contentHash (*d);
        check (h.length() == 64 && h == st::contentHash (*d), "hash: deterministic for " + d->key);
        check (h == pinned[i], "hash: " + d->key + " = pinned value (got " + h + ")");
        seen.insert (h);
    }
    check (seen.size() == static_cast<std::size_t> (styleCount), "hash: the seven factory styles differ");
}

//==============================================================================
juce::String minimal (const juce::String& rules, const juce::String& extra = {})
{
    return R"({ "format": "vox-drums-style", "version": 1, "key": "t", "name": "T", "rules": [)" + rules + "]" + extra
         + R"(, "fill": { "cycle": ["snare"], "ratchet": 2 } })";
}

void testParser()
{
    st::Definition d;
    check (st::parse (minimal (R"({ "part": "kick", "at": [1, 5] })"), d).wasOk() && d.rules.size() == 1 && d.rules[0].positions == 0x11u,
           "parser: a minimal style parses");
    struct Case { juce::String json, field; };
    const std::vector<Case> rejects = {
        { "not json", "not valid JSON" },
        { R"({ "format": "x", "version": 1 })", "format: must be \"vox-drums-style\"" },
        { R"({ "format": "vox-drums-style", "version": 1, "colour": 1 })", "style: unknown key 'colour'" },
        { minimal (R"({ "part": "kick", "at": [1] })", R"(, "extra": 1)"), "unknown key 'extra'" },
        { minimal (R"({ "part": "cowbell", "at": [1] })"), "rules[0].part: unknown part 'cowbell'" },
        { minimal (R"({ "part": "kick", "at": [0] })"), "rules[0].at[0]: 0 is outside 1..16" },
        { minimal (R"({ "part": "kick", "at": [17] })"), "rules[0].at[0]: 17 is outside 1..16" },
        { minimal (R"({ "part": "kick", "at": [1, 1] })"), "rules[0].at[1]: position 1 appears twice" },
        { minimal (R"({ "part": "kick", "at": [] })"), "rules[0].at: must be a list" },
        { minimal (R"({ "part": "kick", "at": [1], "velocity": 1.5 })"), "rules[0].velocity: 1.5 is outside" },
        { minimal (R"({ "part": "kick", "at": [1], "velocity": "loud" })"), "rules[0].velocity: must be a number" },
        { minimal (R"({ "part": "kick", "at": [1], "kind": "ghost" })"), "rules[0].chance: a ghost rule needs a chance" },
        { minimal (R"({ "part": "kick", "at": [1], "kind": "flam" })"), "rules[0].kind" },
        { minimal (R"({ "part": "kick", "at": [1], "chance": "0.5 + tempo" })"), "unknown setting 'tempo'" },
        { minimal (R"({ "part": "kick", "at": [1], "chance": "0.5 +" })"), "rules[0].chance" },
        { minimal (R"({ "part": "kick", "at": [1], "chance": "density ^ 2" })"), "rules[0].chance" },
        { minimal (R"({ "part": "kick", "at": [1], "when": ["density"] })"), "rules[0].when[0]" },
        { minimal (R"({ "part": "kick", "at": [1], "bars": "even" })"), "rules[0].bars" },
        { minimal (R"({ "part": "kick", "at": [1], "ratchet": { "chance": "1", "salt": 1, "valueSalt": 2, "otherwise": 9 } })"),
          "rules[0].ratchet.otherwise" },
        { minimal (R"({ "part": "kick", "at": [1] })", R"(, "probability": { "value": "50", "min": 80, "max": 20 })"), "min is above max" },
        { R"({ "format": "vox-drums-style", "version": 2, "key": "t", "name": "T", "rules": [], "fill": {} })", "version" },
    };
    for (const auto& c : rejects)
    {
        st::Definition out;
        out.key = "untouched";
        const auto r = st::parse (c.json, out);
        check (r.failed() && r.getErrorMessage().contains (c.field) && out.key == "untouched",
               "parser rejects with '" + c.field + "' (got '" + r.getErrorMessage() + "')");
    }
    // Limits.
    {
        juce::String big = minimal (R"({ "part": "kick", "at": [1] })");
        big = big.replace ("\"T\"", "\"" + juce::String::repeatedString ("x", 70000) + "\"");
        check (st::parse (big, d).getErrorMessage().contains ("larger than 64 KiB"), "parser: text size limit");
        juce::String deep = "density";
        for (int i = 0; i < 10; ++i)
            deep = "(" + deep + ")";
        check (st::parse (minimal (R"({ "part": "kick", "at": [1], "chance": ")" + deep + "\" }"), d).getErrorMessage().contains ("nested deeper"),
               "parser: formula depth limit");
        juce::StringArray many;
        for (int i = 0; i < st::maxRulesPerPart + 1; ++i)
            many.add (R"({ "part": "kick", "at": [1] })");
        check (st::parse (minimal (many.joinIntoString (",")), d).getErrorMessage().contains ("more than 64 rules for kick"),
               "parser: rules per part limit");
    }
    // Random mutations of a factory text: never crash, bounded time.
    {
        const auto* fullOn = st::factoryDefinition (0);
        check (fullOn != nullptr, "parser: factory text available");
        const juce::String base = minimal (R"({ "part": "closedHat", "at": [2, 4, 6], "chance": "0.35 + 0.65 * density", "velocity": 0.55,
            "ratchet": { "chance": "0.1 + 0.5 * complexity", "salt": 7, "valueSalt": 8, "below": [[0.5, 2]], "otherwise": 4 } })",
                                           R"(, "probability": { "value": "90 - 50 * complexity", "min": 30, "max": 100, "except": ["kick"] })");
        std::mt19937 random (6880518);
        const auto start = std::chrono::steady_clock::now();
        int accepted = 0;
        for (int i = 0; i < 4000; ++i)
        {
            auto s = base.toStdString();
            const int edits = 1 + static_cast<int> (random() % 4);
            for (int e = 0; e < edits && ! s.empty(); ++e)
            {
                const auto at = static_cast<std::size_t> (random() % s.size());
                switch (random() % 3)
                {
                    case 0: s[at] = static_cast<char> (32 + random() % 95); break;
                    case 1: s.erase (at, 1 + random() % 8); break;
                    default: s.insert (at, 1, static_cast<char> (32 + random() % 95)); break;
                }
            }
            st::Definition m;
            accepted += st::parse (juce::String (s), m).wasOk() ? 1 : 0;
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - start).count();
        std::printf ("parser fuzz: 4000 mutations, %d still valid, %lld ms\n", accepted, static_cast<long long> (ms));
        check (ms < 20000, "parser: 4000 mutated texts parse in bounded time");
    }
}

void testFormulas()
{
    st::ParamValues v;
    for (double density : { 0.0, 0.1, 0.33, 0.5, 0.77, 1.0 })
        for (double sy : { 0.0, 0.3, 1.0 })
        {
            v.v = { density, 0.5, sy, 0.7, 0.4, 0.4, 0.25, 0.3 };
            st::Expr e;
            juce::String error;
            const auto same = [&] (const char* text, double cpp) {
                const bool parsed = st::parseExpr (text, e, error);
                const double got = parsed ? e.evaluate (v) : -1.0;
                check (parsed && std::memcmp (&cpp, &got, sizeof cpp) == 0,
                       juce::String ("formula: ") + text + " evaluates exactly like C++");
            };
            const double d = density, cx = 0.7;
            same ("0.15 + 0.55 * syncopation * (0.5 + density)", 0.15 + 0.55 * sy * (0.5 + d));
            same ("0.1 + 0.45 * density * (0.4 + syncopation)", 0.1 + 0.45 * d * (0.4 + sy));
            same ("0.04 + 0.2 * density + 0.1 * syncopation", 0.04 + 0.2 * d + 0.1 * sy);
            same ("90 - 50 * complexity", 90.0 - 50.0 * cx);
            same ("0.15 + 0.45 * syncopation + 0.2 * density", 0.15 + 0.45 * sy + 0.2 * d);
        }
}

//==============================================================================
void testUnavailableStyle()
{
    for (int id : { 7, 22, 255 })
    {
        const auto label = "unavailable style " + juce::String (id) + ": ";
        GeneratorSettings g;
        g.style = static_cast<Style> (id);
        g.seed = 99;
        g.laneLocks = 0x0004u;
        check (! styleAvailable (g.style), label + "reported unavailable");
        // Pre-populated data: a Full-on pattern with a locked step.
        GeneratorSettings fill = g;
        fill.style = Style::fullOn;
        auto a = std::make_unique<StoredPattern>(), b = std::make_unique<StoredPattern>(), keep = std::make_unique<StoredPattern>();
        empty (*a, 16, 4);
        generatePattern (fill, *a);
        a->cell (3, 1, 6).locked = true;
        a->seed = 4242;
        *keep = *a;
        check (! generatePattern (g, *a) && *a == *keep, label + "Pattern changes nothing (cells, seed, locks)");
        check (! mutateLanes (g, 0xffffu, *a) && ! mutateLanes (g, 0x0010u, *a) && *a == *keep, label + "Mutate changes nothing");
        check (! generateFill (g, *a, -1) && ! generateFill (g, *a, 0) && *a == *keep, label + "Fill changes nothing");
        empty (*b, 32, 2);   // another grid, so a copy from A would be visible
        generatePattern (fill, *b);
        const auto bBefore = std::make_unique<StoredPattern> (*b);
        check (! generateVariation (g, *a, *b) && *b == *bBefore && *a == *keep, label + "Variation A>B changes neither A nor B");
        for (int length : { 16, 32, 64 })
        {
            auto all = std::make_unique<PatternStore::Everything>();
            for (int slot = 0; slot < PatternStore::slotCount; ++slot)
            {
                all->slots[static_cast<std::size_t> (slot)] = *keep;
                all->alternates[static_cast<std::size_t> (slot)] = *bBefore;
            }
            all->song = SongData::initial();
            all->song.length = static_cast<std::uint8_t> (length);
            generateSong (fill, *all);   // a real timeline to protect
            const auto before = std::make_unique<PatternStore::Everything> (*all);
            check (! generateSong (g, *all) && all->slots == before->slots && all->alternates == before->alternates && all->song == before->song,
                   label + "Song " + juce::String (length) + " changes no slot, variation or timeline");
        }
    }
    // Through the processor: false, no seed change, no history, state unchanged.
    auto p = std::make_unique<DrumsProcessor>();
    p->setRateAndBufferSizeDetails (48000.0, 512);
    p->prepareToPlay (48000.0, 512);
    p->generate (DrumsProcessor::Generate::pattern);   // some content first
    p->patternHistory().clear();
    auto g = p->generator();
    g.style = static_cast<Style> (9);   // Dark Psy: registered, not yet available
    p->setGenerator (g);
    juce::MemoryBlock before;
    p->getStateInformation (before);
    bool any = false;
    for (auto what : { DrumsProcessor::Generate::pattern, DrumsProcessor::Generate::song, DrumsProcessor::Generate::mutate,
                       DrumsProcessor::Generate::fill, DrumsProcessor::Generate::variation })
        any = p->generate (what) || any;
    juce::MemoryBlock after;
    p->getStateInformation (after);
    check (! any && p->generator().seed == g.seed && ! p->patternHistory().canUndo() && after == before,
           "unavailable style: the processor reports no change, keeps the seed, records no undo step and leaves the state unchanged");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    testRegistry();
    testUnavailableStyle();
    testFormulas();
    testHashes();
    testParser();
    testParity();
    if (failures == 0)
        std::printf ("VOX Drums style engine tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
