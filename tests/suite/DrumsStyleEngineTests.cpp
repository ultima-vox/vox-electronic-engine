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
#include "DrumsPatternTestUtil.h"
#include "GeneratorReference.h"
#include "Presets.h"
#include "Processor.h"
#include "StyleRegistry.h"

#include <juce_cryptography/juce_cryptography.h>
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
    p.seed = 1;
    testutil::emptyGrid (p, steps, bars);
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
                                        testutil::stepAt (*a, 4, 0, 1).locked = true;
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
                        testutil::stepAt (e->slots[1], 0, 2, 4).locked = true;
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
juce::String minimal (const juce::String& rules, const juce::String& extra = {}, const juce::String& meters = R"(["4/4"])")
{
    // Declares 16 parts (all but ride, tom 2 and tom 3; PR 137 §2.4).
    return R"({ "format": "vox-drums-style", "version": 1, "id": "00000000-0000-4000-8000-000000000001", "key": "t", "name": "T", "meters": )"
         + meters
         + R"(, "parts": { "required": ["kick"], "optional": ["snare", "clap", "closedHat", "openHat", "tom1", "rim", "crash", "perc1", "perc2", "perc3", "perc4", "perc5", "perc6", "perc7", "perc8"] })"
         + R"(, "rules": [)" + rules + "]" + extra + R"(, "fill": { "cycle": ["snare"], "ratchet": 2 } })";
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
        // Parts (PR 137 §2.4, roles v2).
        { minimal (R"({ "part": "ride", "at": [1] })"), "rules[0].part: ride is not in parts" },
        { minimal (R"({ "part": "tom", "at": [1] })"), "unknown part 'tom'" },
        { minimal (R"({ "part": "cymbal", "at": [1] })"), "unknown part 'cymbal'" },
        { minimal (R"({ "part": "kick", "at": [1] })").replace (R"("optional": ["snare",)", R"("optional": ["ride", "snare",)"), "parts: at most 16 parts" },
        { minimal (R"({ "part": "kick", "at": [1] })").replace (R"("optional": ["snare",)", R"("optional": ["kick", "snare",)"), "parts.optional: a part is both required and optional" },
        { minimal (R"({ "part": "kick", "at": [1] })").replace (R"("required": ["kick"])", R"("required": ["kick", "kick"])"), "parts.required[1]: a part appears twice" },
        { minimal (R"({ "part": "kick", "at": [1] })").replace (R"("required": ["kick"])", R"("required": ["kick", ["snare"]])"), "parts.required[1]: a list of alternatives needs 2 or more" },
        { minimal (R"({ "part": "kick", "at": [1] })").replace (R"("required": ["kick"])", R"("required": [])"), "parts.required: must be a list of 1..16" },
        { minimal (R"({ "part": "kick", "at": [1] })").replace (R"("required": ["kick"], )", ""), "parts.required: must be a list" },
        { minimal (R"({ "part": "kick", "at": [1] })").replace (R"(, "parts": {)", R"(, "partz": {)"), "unknown key 'partz'" },
        { minimal (R"({ "part": "kick", "at": [1] })", R"(, "assertions": [{ "part": "tom2", "never": [1] }])"), "assertions[0].part: tom2 is not in parts" },
        // Canonical schema (PR 137 §11.1).
        { minimal (R"({ "part": "kick", "at": [1] })").replace ("00000000-0000-4000-8000-000000000001", "not-a-uuid"), "id: must be a lower-case UUID" },
        { minimal (R"({ "part": "kick", "at": [1] })").replace ("00000000-0000-4000-8000-000000000001", "00000000-0000-4000-8000-00000000000A"), "id: must be a lower-case UUID" },
        { minimal (R"({ "part": "kick", "at": [1] })").replace ("\"key\": \"t\"", "\"key\": \"Bad Key\""), "key: must be" },
        { minimal (R"({ "part": "kick", "at": [1] })", {}, R"(["12/8"])"), "meters: must include \"4/4\"" },
        { minimal (R"({ "part": "kick", "at": [1] })", {}, R"(["4/4", "4/4"])"), "meters[1]: 4/4 appears twice" },
        { minimal (R"({ "part": "kick", "at": [1] })", {}, R"(["4/4", "5/4"])"), "unknown meter '5/4'" },
        { minimal (R"({ "part": "kick", "meter": "12/8", "at": [1] })"), "rules[0].meter: 12/8 is not in the style's meters" },
        { minimal (R"({ "part": "kick", "meter": "12/8", "at": [25] })", {}, R"(["4/4", "12/8"])"), "rules[0].at[0]: 25 is outside 1..24" },
        { minimal (R"({ "part": "kick", "meter": "3/4", "at": [13] })", {}, R"(["4/4", "3/4"])"), "rules[0].at[0]: 13 is outside 1..12" },
        { minimal (R"({ "part": "kick", "beats": [4], "at": [1] })"), "go together" },
        { minimal (R"({ "part": "kick", "beats": [4], "subdivision": "duplet", "at": [1] })"), "must be \"triplet\" in 4/4" },
        { minimal (R"({ "part": "kick", "beats": [5], "subdivision": "triplet", "at": [1] })"), "rules[0].beats[0]: 5 is outside 1..4" },
        { minimal (R"({ "part": "kick", "beats": [4], "subdivision": "triplet", "at": [7] })"), "rules[0].at[0]: 7 is outside 1..6" },
        { minimal (R"({ "part": "kick", "meter": "12/8", "beats": [1], "subdivision": "duplet", "at": [5] })", {}, R"(["4/4", "12/8"])"),
          "rules[0].at[0]: 5 is outside 1..4" },
        { minimal (R"({ "part": "kick", "at": [1] })", R"(, "defaults": { "density": 2 })"), "defaults.density: 2 is outside" },
        { minimal (R"({ "part": "kick", "at": [1] })", R"(, "defaults": { "tempo": 1 })"), "unknown key 'tempo'" },
        { minimal (R"({ "part": "kick", "at": [1] })", R"(, "meta": { "tempo": { "min": 140, "max": 120 } })"), "meta.tempo: min is above max" },
        { minimal (R"({ "part": "kick", "at": [1] })", R"(, "variation": { "keep": ["kick", "kick"] })"), "part kick appears twice" },
        { minimal (R"({ "part": "kick", "at": [1] })", R"(, "song": { "breakWithoutKick": 1 })"), "song.breakWithoutKick: must be true or false" },
        { minimal (R"({ "part": "kick", "at": [1] })", R"(, "assertions": [ { "part": "kick", "always": [1], "never": [1] } ])"),
          "a position is both always and never" },
    };
    for (const auto& c : rejects)
    {
        st::Definition out;
        out.key = "untouched";
        const auto r = st::parse (c.json, out);
        check (r.failed() && r.getErrorMessage().contains (c.field) && out.key == "untouched",
               "parser rejects with '" + c.field + "' (got '" + r.getErrorMessage() + "')");
    }
    // Parts (PR 137 §2.4): alternatives and optional parts round-trip canonically; ride, tom 2
    // and tom 3 are parts; 8 percussion parts are allowed.
    {
        st::Definition t;
        const auto r = st::parse (R"({ "format": "vox-drums-style", "version": 1, "id": "00000000-0000-4000-8000-000000000002", "key": "p", "name": "P",)"
                                  R"( "meters": ["4/4"], "parts": { "optional": ["tom3", "perc8", "perc1"], "required": ["ride", ["snare", "clap"], "kick"] },)"
                                  R"( "rules": [{ "part": "ride", "at": [1, 3] }, { "part": "tom3", "at": [16] }], "fill": { "cycle": ["snare"], "ratchet": 2 } })",
                                  t);
        check (r.wasOk() && t.requiredParts.size() == 3 && t.requiredParts[0] == 1u && t.requiredParts[1] == 6u && t.requiredParts[2] == (1u << 16)
                   && t.optionalParts == ((1u << 8) | (1u << 15) | (1u << 18)) && t.rules[0].part == 16 && t.rules[1].part == 18,
               "parser: parts with alternatives, ride and tom 3 (" + r.getErrorMessage() + ")");
        const auto text = st::serialize (t);
        st::Definition back;
        check (text.contains (R"("parts":{"optional":["perc1","perc8","tom3"],"required":["kick",["snare","clap"],"ride"]})")
                   && st::parse (text, back).wasOk() && st::serialize (back) == text,
               "serializer: parts in canonical order (groups by lowest part, parts by index): " + text.fromFirstOccurrenceOf ("\"parts\"", true, false).substring (0, 90));
        st::Definition eight;
        check (st::parse (R"({ "format": "vox-drums-style", "version": 1, "id": "00000000-0000-4000-8000-000000000003", "key": "e", "name": "E", "meters": ["4/4"],)"
                          R"( "parts": { "required": ["kick"], "optional": ["perc1", "perc2", "perc3", "perc4", "perc5", "perc6", "perc7", "perc8", "snare"] },)"
                          R"( "rules": [{ "part": "kick", "at": [1] }], "fill": { "cycle": ["snare"], "ratchet": 2 } })", eight).wasOk(),
               "parser: 8 percussion parts are allowed");
    }
    // Accepted: 12/8 positions 17..24 and tuplet cells survive parse and serialization.
    {
        st::Definition t;
        const auto r = st::parse (minimal (R"({ "part": "kick", "meter": "12/8", "at": [17, 20, 24] },
                                            { "part": "closedHat", "beats": [2, 4], "subdivision": "triplet", "at": [1, 6] })",
                                           {}, R"(["4/4", "12/8"])"),
                                  t);
        check (r.wasOk() && t.rules.size() == 2 && t.rules[0].positions == ((1u << 16) | (1u << 19) | (1u << 23)) && t.rules[0].meter == st::Meter::m12_8
                   && t.rules[1].tupletBeats == 0x000Au && t.rules[1].positions == 0x21u,
               "parser: 12/8 positions 17..24 and tuplet cells are kept exactly (" + r.getErrorMessage() + ")");
        st::Definition back;
        check (st::parse (st::serialize (t), back).wasOk() && back.rules[0].positions == t.rules[0].positions
                   && back.rules[1].tupletBeats == t.rules[1].tupletBeats && st::serialize (back) == st::serialize (t),
               "serializer: 12/8 and tuplet rules round-trip");
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
        testutil::stepAt (*a, 3, 1, 6).locked = true;
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

//==============================================================================
// Canonical serializer: every factory style and the fixture round-trip, and
// a re-parsed factory definition generates byte-identically.
void testRoundTrip (const juce::File& fixture)
{
    std::vector<std::shared_ptr<const st::Definition>> defs;
    for (int id = 0; id < styleCount; ++id)
        if (const auto* d = st::factoryDefinition (id))
            defs.push_back (std::make_shared<st::Definition> (*d));
    const auto text = fixture.loadFileAsString();
    st::Definition example;
    const auto parsed = st::parse (text, example);
    check (parsed.wasOk(), "fixture: example-user-style.voxstyle parses (" + parsed.getErrorMessage() + ")");
    if (parsed.wasOk())
        defs.push_back (std::make_shared<st::Definition> (example));
    for (const auto& d : defs)
    {
        const auto first = st::serialize (*d);
        st::Definition again;
        const auto r = st::parse (first, again);
        check (r.wasOk() && st::serialize (again) == first && st::contentHash (again) == st::contentHash (*d)
                   && st::documentHash (again) == st::documentHash (*d),
               "serializer: " + d->key + " round-trips exactly (" + r.getErrorMessage() + ")");
    }
    // Factory export -> parse -> generate: byte-identical over a matrix.
    auto a = std::make_unique<StoredPattern>(), b = std::make_unique<StoredPattern>();
    auto all = std::make_unique<PatternStore::Everything>(), all2 = std::make_unique<PatternStore::Everything>();
    for (int id = 0; id < styleCount; ++id)
    {
        auto reparsed = std::make_shared<st::Definition>();
        if (! st::parse (st::serialize (*st::factoryDefinition (id)), *reparsed).wasOk())
            continue;
        int bad = 0, n = 0;
        for (std::uint32_t seed = 1; seed <= 4; ++seed)
            for (float density : { 0.1f, 0.6f, 1.0f })
                for (float complexity : { 0.2f, 0.9f })
                    for (int steps : { 16, 64 })
                    {
                        GeneratorSettings g;
                        g.style = static_cast<Style> (id);
                        g.seed = seed * 31u;
                        g.density = density;
                        g.complexity = complexity;
                        g.fills = 0.7f;
                        g.syncopation = 0.6f;
                        GeneratorSettings h = g;
                        h.style = Style::fullOn;   // must not matter: the custom definition runs
                        h.custom = reparsed;
                        empty (*a, steps, 4);
                        empty (*b, steps, 4);
                        generatePattern (g, *a);
                        generatePattern (h, *b);
                        bad += *a == *b ? 0 : 1;
                        auto va = std::make_unique<StoredPattern>(), vb = std::make_unique<StoredPattern>();
                        empty (*va, steps, 4);
                        empty (*vb, steps, 4);
                        generateVariation (g, *a, *va);
                        generateVariation (h, *b, *vb);
                        bad += *va == *vb ? 0 : 1;
                        n += 2;
                    }
        for (auto* e : { all.get(), all2.get() })
        {
            for (int slot = 0; slot < PatternStore::slotCount; ++slot)
            {
                empty (e->slots[static_cast<std::size_t> (slot)], 16, 4);
                empty (e->alternates[static_cast<std::size_t> (slot)], 16, 4);
            }
            e->song = SongData::initial();
            e->song.length = 32;
        }
        GeneratorSettings g;
        g.style = static_cast<Style> (id);
        GeneratorSettings h = g;
        h.custom = reparsed;
        generateSong (g, *all);
        generateSong (h, *all2);
        bad += all->slots == all2->slots && all->alternates == all2->alternates && all->song == all2->song ? 0 : 1;
        ++n;
        check (bad == 0, juce::String ("round trip: ") + styleName (static_cast<Style> (id)) + " exported, re-parsed and generated: "
                             + juce::String (bad) + " of " + juce::String (n) + " differ");
    }
    // The fixture runs: it generates, and its own 4/4 assertions hold.
    if (parsed.wasOk())
    {
        auto def = std::make_shared<st::Definition> (example);
        GeneratorSettings g;
        g.custom = def;
        empty (*a, 16, 4);
        check (generatePattern (g, *a), "fixture: generates");
        int hits = 0;
        for (const auto& c : a->cells)
            hits += c.on ? 1 : 0;
        check (hits > 16, "fixture: writes a pattern (" + juce::String (hits) + " hits)");
        const auto violations = checkStyleAssertions (def);
        check (violations.isEmpty(), "fixture: its assertions hold (" + violations.joinIntoString ("; ") + ")");
        // A broken copy: an assertion the rules contradict is reported.
        auto broken = std::make_shared<st::Definition> (example);
        broken->assertions.push_back ({ st::Meter::m4_4, 0, 0, 1u });   // "kick never on 1"
        check (! checkStyleAssertions (broken).isEmpty(), "fixture: a contradicted assertion is reported");
        // variation.keep and song.breakWithoutKick are honoured.
        GeneratorSettings s;
        s.custom = def;
        s.density = 0.8f;
        for (int slot = 0; slot < PatternStore::slotCount; ++slot)
        {
            empty (all->slots[static_cast<std::size_t> (slot)], 16, 4);
            empty (all->alternates[static_cast<std::size_t> (slot)], 16, 4);
        }
        all->song = SongData::initial();
        generateSong (s, *all);
        bool breakHasKick = false;
        for (int bar = 0; bar < 4; ++bar)
            breakHasKick = breakHasKick || testutil::stepAt (all->slots[3], 0, bar, 0).on;
        check (breakHasKick, "fixture: song.breakWithoutKick = false keeps the kick in the break slot");
    }
    // Registry identities.
    std::set<juce::String> uuids;
    for (const auto& info : st::registry())
    {
        check (st::isCanonicalUuid (info.uuid) && st::infoByUuid (info.uuid) == &info, juce::String ("registry: UUID of ") + info.name);
        uuids.insert (info.uuid);
        if (const auto* d = st::factoryDefinition (info.id))
            check (d->id == info.uuid, juce::String ("registry: factory definition id of ") + info.name);
    }
    check (uuids.size() == st::registrySize, "registry: 23 unique UUIDs");
}

//==============================================================================
// DGEN v2 and DSTL v1 (docs/ultima-vox-music/drums/STYLE_ENGINE.md).
std::unique_ptr<DrumsProcessor> newProcessor()
{
    auto p = std::make_unique<DrumsProcessor>();
    p->setRateAndBufferSizeDetails (48000.0, 512);
    p->prepareToPlay (48000.0, 512);
    return p;
}

juce::MemoryBlock stateOf (DrumsProcessor& p)
{
    juce::MemoryBlock b;
    p.getStateInformation (b);
    return b;
}

vox::preset::StateDocument documentOf (const juce::MemoryBlock& b, DrumsProcessor& p)
{
    auto d = vox::preset::decode ({ static_cast<const std::byte*> (b.getData()), b.getSize() }, p.product().stateTag, p.product().stateSchema);
    check (d.ok(), "state decodes");
    return d.ok() ? std::move (d.document) : vox::preset::StateDocument {};
}

juce::MemoryBlock blobOf (const vox::preset::StateDocument& doc)
{
    juce::MemoryBlock b;
    if (const auto bytes = vox::preset::encode (doc))
        b.replaceAll (bytes->data(), bytes->size());
    return b;
}

void testEmbeddedStyle (const juce::File& fixture)
{
    // DGEN v2 round trip for every registry style, available or not.
    for (int id = 0; id < st::registrySize; ++id)
    {
        GeneratorSettings g;
        g.style = static_cast<Style> (id);
        g.seed = 1234567u + static_cast<std::uint32_t> (id);
        g.density = 0.25f;
        g.laneLocks = 0x8001u;
        const auto s = encodeGenerator (g);
        GeneratorSettings back;
        juce::String error;
        check (s.data.size() == 58u && decodeGenerator (s, back, error) && back == g && back.custom == nullptr,
               "DGEN v2: registry style " + juce::String (id) + " round-trips (" + error + ")");
    }
    st::Definition parsed;
    if (st::parse (fixture.loadFileAsString(), parsed).failed())
        return;
    const auto user = std::make_shared<const st::Definition> (parsed);

    // A project with a user style restores on a fresh instance (empty library).
    auto p = newProcessor();
    auto g = p->generator();
    g.custom = user;
    g.seed = 777;
    p->setGenerator (g);
    check (p->generate (DrumsProcessor::Generate::pattern, 0xffffu, false), "DSTL: a user style generates in the processor");
    const auto saved = stateOf (*p);
    const auto doc = documentOf (saved, *p);
    const auto* dstl = doc.find (styleSectionTag);
    check (dstl != nullptr, "DSTL: written for a user style");
    auto q = newProcessor();
    q->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    check (q->lastStateError().isEmpty() && q->generator().custom != nullptr
               && st::documentHash (*q->generator().custom) == st::documentHash (*user) && stateOf (*q) == saved,
           "DSTL: a fresh instance restores the user style and saves the same bytes (" + q->lastStateError() + ")");
    {
        auto a = std::make_unique<StoredPattern>(), b = std::make_unique<StoredPattern>();
        p->generate (DrumsProcessor::Generate::variation, 0xffffu, false);
        q->generate (DrumsProcessor::Generate::variation, 0xffffu, false);
        p->patterns().copyAlternate (p->currentSlot(), *a);
        q->patterns().copyAlternate (q->currentSlot(), *b);
        check (*a == *b, "DSTL: the restored style generates identically");
    }
    // A factory style writes no DSTL.
    {
        auto f = newProcessor();
        check (documentOf (stateOf (*f), *f).find (styleSectionTag) == nullptr, "DSTL: none for a factory style");
    }
    // Every malformed case is rejected and leaves the live state unchanged.
    const auto rejects = [&] (const std::function<void (vox::preset::StateDocument&)>& damage, const juce::String& what) {
        auto bad = doc;
        damage (bad);
        auto r = newProcessor();
        const auto before = stateOf (*r);
        const auto blob = blobOf (bad);
        r->setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
        check (r->lastStateError().isNotEmpty() && stateOf (*r) == before, "DSTL: " + what + " is rejected atomically (" + r->lastStateError() + ")");
    };
    const auto onStyle = [] (std::function<void (std::vector<std::byte>&)> f) {
        return [f] (vox::preset::StateDocument& d) {
            for (auto& s : d.sections)
                if (s.tag == styleSectionTag)
                    f (s.data);
        };
    };
    rejects (onStyle ([] (std::vector<std::byte>& d) { d[0] = std::byte { 2 }; }), "an unknown version");
    rejects (onStyle ([] (std::vector<std::byte>& d) { d[10] = static_cast<std::byte> (std::to_integer<int> (d[10]) ^ 1); }), "a wrong hash");
    rejects (onStyle ([] (std::vector<std::byte>& d) { d[50] = static_cast<std::byte> (std::to_integer<int> (d[50]) ^ 4); }), "changed text");
    rejects (onStyle ([] (std::vector<std::byte>& d) { d.pop_back(); }), "a truncated text");
    rejects (onStyle ([] (std::vector<std::byte>& d) { d.resize (39); }), "a truncated header");
    const auto replaceStyle = [&] (const st::Definition& def, const juce::String& textOverride = {}) {
        return [def, textOverride] (vox::preset::StateDocument& d) {
            auto section = encodeStyleSection (def);
            if (textOverride.isNotEmpty())
            {
                // Valid hash and length over a non-canonical text.
                const auto t = textOverride.toStdString();
                section.data.resize (4);
                const juce::SHA256 h (t.data(), t.size());
                const auto raw = h.getRawData();
                for (std::size_t i = 0; i < raw.getSize(); ++i)
                    section.data.push_back (static_cast<std::byte> (static_cast<const std::uint8_t*> (raw.getData())[i]));
                for (int shift = 0; shift < 32; shift += 8)
                    section.data.push_back (static_cast<std::byte> ((t.size() >> shift) & 0xffu));
                for (char c : t)
                    section.data.push_back (static_cast<std::byte> (c));
            }
            for (auto& s : d.sections)
                if (s.tag == styleSectionTag)
                    s = section;
        };
    };
    rejects (replaceStyle (parsed, st::serialize (parsed) + " "), "a non-canonical text");
    {
        auto other = parsed;
        other.id = "11111111-2222-4333-8444-555555555555";
        rejects (replaceStyle (other), "a style the generator does not reference");
        auto factory = parsed;
        factory.id = st::registry()[0].uuid;
        rejects (replaceStyle (factory), "an embedded factory UUID");
    }
    rejects ([] (vox::preset::StateDocument& d) {
        d.sections.erase (std::remove_if (d.sections.begin(), d.sections.end(), [] (const auto& s) { return s.tag == styleSectionTag; }),
                          d.sections.end());
    }, "a user style UUID without DSTL");
    {
        auto f = newProcessor();
        auto fdoc = documentOf (stateOf (*f), *f);
        fdoc.sections.push_back (*dstl);
        auto r = newProcessor();
        const auto before = stateOf (*r);
        const auto blob = blobOf (fdoc);
        r->setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
        check (r->lastStateError().isNotEmpty() && stateOf (*r) == before, "DSTL: a DSTL beside a factory-style DGEN is rejected atomically");
    }
    // Song presets carry DGEN with DSTL; a preset without DSTL removes it.
    {
        const auto songPreset = p->presets().capture (PresetType::song, "With user style");
        auto r = newProcessor();
        check (r->presets().apply (songPreset.getData(), songPreset.getSize(), -1, PresetType::song).wasOk() && r->generator().custom != nullptr
                   && st::documentHash (*r->generator().custom) == st::documentHash (*user),
               "DSTL: a song preset brings the user style along");
        auto f = newProcessor();
        const auto factorySong = f->presets().capture (PresetType::song, "Factory style");
        check (r->presets().apply (factorySong.getData(), factorySong.getSize(), -1, PresetType::song).wasOk() && r->generator().custom == nullptr
                   && documentOf (stateOf (*r), *r).find (styleSectionTag) == nullptr,
               "DSTL: a song preset with a factory style removes it");
    }
}

// PR 137 §10.6: the interpreter runs the rules of the pattern's meter and
// tuplet rules (they set the lane's beat to triplets / duplets); meters the
// style does not define are refused; every result is a valid pattern.
void testMetersAndTuplets (const juce::File& fixture)
{
    st::Definition example;
    if (st::parse (fixture.loadFileAsString(), example).failed())
    {
        check (false, "meters: the fixture parses");
        return;
    }
    auto def = std::make_shared<st::Definition> (example);
    auto p = std::make_unique<StoredPattern>();
    const auto fresh = [&] (timing::TimeSignature s, timing::Resolution r) {
        *p = {};
        p->signature = s;
        p->resolution = r;
    };
    const auto on = [&] (int lane, int bar, int tick) {
        const auto t = p->timing (lane, bar);
        for (int b = 0; b < t.beats(); ++b)
            for (int c = 0; c < t.cells (b); ++c)
                if (t.cellStartTick (b, c) == tick)
                    return p->cell (lane, bar, t.slot (b, c)).on;
        return false;
    };
    GeneratorSettings g;
    g.custom = def;
    g.seed = 3;
    g.fills = 0.0f;
    g.complexity = 0.3f;   // the 4/4 triplet hat rule needs complexity > 0.6
    // 12/8: its own rules on the 24-position grid; the tom duplets on beat 4.
    fresh ({ 12, 8 }, timing::Resolution::sixteenth);
    check (generatePattern (g, *p) && p->valid(), "12/8: generates a valid pattern");
    bool kicks = true, snares = true, hats = true;
    for (int bar = 0; bar < p->bars; ++bar)
    {
        for (int pos : { 0, 6, 12, 18 })
            kicks = kicks && on (0, bar, pos * 240);
        for (int pos = 0; pos < 24; ++pos)
        {
            const bool k = on (0, bar, pos * 240);
            kicks = kicks && (k == (pos % 6 == 0));
            hats = hats && (on (3, bar, pos * 240) == (pos % 2 == 0));
        }
        snares = snares && on (1, bar, 6 * 240) && on (1, bar, 18 * 240);
    }
    check (kicks && snares && hats, "12/8: kick on 1/7/13/19 only, snare on 7 and 19, hats on odd 16ths up to 23 (the 12/8 rules, not 4/4)");
    {
        const auto t = p->timing (5, 0);
        bool duplets = t.alternativeBeats == 0b1000 && t.cells (3) == 4;
        for (int k = 0; k < 4; ++k)
            duplets = duplets && on (5, 0, 3 * 1440 + k * 360);
        check (duplets, "12/8: tom 1 plays duplets on beat 4 (mask set, 4 cells of 360 ticks)");
        check (p->timing (0, 0).alternativeBeats == 0 && p->timing (3, 0).alternativeBeats == 0, "12/8: lanes without tuplet rules stay straight");
    }
    // 4/4: the triplet hat rule only when complexity > 0.6.
    fresh ({}, timing::Resolution::sixteenth);
    check (generatePattern (g, *p) && p->timing (3, 0).alternativeBeats == 0, "4/4: below complexity 0.6 the hats stay straight");
    g.complexity = 0.8f;
    check (generatePattern (g, *p) && p->valid() && p->timing (3, 0).alternativeBeats == 0b1000, "4/4: complexity 0.8: hats in triplets on beat 4");
    {
        bool triplets = true;
        for (int k = 0; k < 6; ++k)
            triplets = triplets && on (3, 0, 2880 + k * 160);
        check (triplets && ! on (3, 0, 2880 + 240), "4/4: the six triplet hats of beat 4 (160 ticks apart), no straight 16th there");
    }
    // 32nd resolution: the same rule lands on every second 32nd-triplet cell.
    fresh ({}, timing::Resolution::thirtySecond);
    check (generatePattern (g, *p) && p->valid() && p->timing (3, 0).alternativeBeats == 0b1000 && on (3, 0, 2880 + 160) && ! on (3, 0, 2880 + 80),
           "4/4 at 32nds: triplet hats on the 16th-level triplet cells only");
    // 64th: no tuplets at this resolution: straight, the tuplet rule is skipped.
    fresh ({}, timing::Resolution::sixtyFourth);
    check (generatePattern (g, *p) && p->valid() && p->timing (3, 0).alternativeBeats == 0, "4/4 at 64ths (straight only): no triplets");
    // A locked cell in beat 4 keeps the beat straight and the cell.
    fresh ({}, timing::Resolution::sixteenth);
    p->cell (3, 0, p->timing (3, 0).slot (3, 1)) = { true, false, 99 };
    p->cell (3, 0, p->timing (3, 0).slot (3, 1)).locked = true;
    check (generatePattern (g, *p) && p->timing (3, 0).alternativeBeats == 0 && p->timing (3, 1).alternativeBeats == 0b1000
               && p->cell (3, 0, p->timing (3, 0).slot (3, 1)).velocity == 99,
           "tuplets: a locked cell keeps its beat straight in that bar only; the cell is kept");
    // Back to a style without tuplet rules: the beats go back to straight.
    {
        GeneratorSettings plain;
        plain.style = Style::fullOn;
        plain.seed = 3;
        p->cell (3, 0, p->timing (3, 0).slot (3, 1)).locked = false;
        check (generatePattern (plain, *p) && p->valid() && p->timing (3, 1).alternativeBeats == 0 && p->timing (3, 0).alternativeBeats == 0,
               "tuplets: a style without tuplet rules regenerates the lane straight");
    }
    // Refusals: a meter the style does not define, and one no style has.
    for (const auto& [sig, what] : { std::pair { timing::TimeSignature { 3, 4 }, "3/4" }, std::pair { timing::TimeSignature { 7, 8 }, "7/8" } })
    {
        fresh (sig, timing::Resolution::sixteenth);
        p->cell (0, 0, 0) = { true, false, 77 };
        const auto before = std::make_unique<StoredPattern> (*p);
        const auto why = generatorRefusal (g, *p);
        check (! generatePattern (g, *p) && ! mutateLanes (g, 0xffffu, *p) && ! generateFill (g, *p) && *p == *before
                   && why == juce::String ("the style is written for 4/4, 12/8, not ") + what,
               juce::String ("meters: ") + what + " is refused and changes nothing (" + why + ")");
        GeneratorSettings factory;
        check (! generatePattern (factory, *p) && *p == *before, juce::String ("meters: a 4/4 factory style refuses ") + what);
    }
    {
        auto four = std::make_unique<StoredPattern>();
        check (generatorRefusal (g, *p).isNotEmpty() && generatorRefusal (g, *four).isEmpty(), "meters: no refusal for 4/4");
    }
    // The matrix: every meter the fixture defines x every resolution; fills;
    // every result valid (inside the capacity, unused slots empty).
    int cases = 0, invalid = 0;
    for (const auto sig : { timing::TimeSignature { 4, 4 }, timing::TimeSignature { 12, 8 } })
        for (int r = 0; r < timing::resolutionCount; ++r)
        {
            const auto res = static_cast<timing::Resolution> (r);
            if (timing::capacity (sig, res).availability == timing::Availability::unavailable)
                continue;
            for (std::uint32_t seed = 1; seed <= 4; ++seed)
                for (float cx : { 0.2f, 0.9f })
                {
                    GeneratorSettings m = g;
                    m.seed = seed;
                    m.complexity = cx;
                    m.fills = 1.0f;
                    fresh (sig, res);
                    ++cases;
                    invalid += generatePattern (m, *p) && p->valid() ? 0 : 1;
                    auto b = std::make_unique<StoredPattern>();
                    *b = {};
                    invalid += generateVariation (m, *p, *b) && b->valid() ? 0 : 1;
                }
        }
    check (invalid == 0, "meters: " + juce::String (cases) + " generations (4/4, 12/8 x resolutions x seeds x complexity, fills, variation) are valid patterns");
    // The fixture's own assertions, 12/8 included, hold.
    check (checkStyleAssertions (def).isEmpty(), "meters: the fixture's 4/4 and 12/8 assertions hold");
    // The processor names the refusal and changes nothing.
    {
        auto proc = std::make_unique<DrumsProcessor>();
        auto three = std::make_unique<StoredPattern>();   // heap: ~33 KB
        three->signature = { 3, 4 };
        three->bars = 1;
        proc->patterns().setSlot (proc->currentSlot(), *three);
        auto settings = proc->generator();
        settings.custom = def;
        proc->setGenerator (settings);
        const auto seed = proc->generator().seed;
        check (proc->generateRefusal (DrumsProcessor::Generate::pattern) == "the style is written for 4/4, 12/8, not 3/4"
                   && ! proc->generate (DrumsProcessor::Generate::pattern) && proc->generator().seed == seed && ! proc->patternHistory().canUndo(),
               "processor: a refused meter is named; the seed and history are unchanged");
    }
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: %s <tests/suite/fixtures/example-user-style.voxstyle>\n", argv[0]);
        return EXIT_FAILURE;
    }
    juce::ScopedJuceInitialiser_GUI juce;
    testRoundTrip (juce::File (juce::String (argv[1])));
    testEmbeddedStyle (juce::File (juce::String (argv[1])));
    testRegistry();
    testUnavailableStyle();
    testFormulas();
    testHashes();
    testParser();
    testParity();
    testMetersAndTuplets (juce::File (juce::String (argv[1])));
    if (failures == 0)
        std::printf ("VOX Drums style engine tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
