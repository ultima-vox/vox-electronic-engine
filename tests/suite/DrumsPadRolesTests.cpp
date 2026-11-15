// VOX Drums semantic pad roles (issue 132, option B; design:
// docs/ultima-vox-music/drums/PAD_ROLES_DESIGN.md §5, R1-R19):
//
//  - codec: DROL round trip, every malformed variant rejected with the live
//    state unchanged, missing DROL = identity, explicit = 0 = identity;
//  - identity parity: the generator digest over a matrix of every action
//    equals the one produced by the c7c29bb generator (before roles);
//  - mapping: for role permutations (kick/snare swapped, hats moved,
//    percussion reordered, random) every action writes exactly the identity
//    result moved to the mapped lanes; missing and duplicate roles; lanes
//    without a role are cleared except locked cells; locks;
//  - Mutate Lanes: physical-lane mask only, against a scratch result;
//  - processor: generate() uses the roles, MIDI export parity, undo/redo;
//  - presets: kit with and without DROL (notice, Undo Load), legacy and
//    modern pad presets (target keeps its role, explicit Apply offer);
//  - old builds: a state with DROL keeps the c7c29bb schema and sections;
//  - "Set roles from pad models": factory kits, hats by note and decay,
//    "?" rows block Apply, Apply is one undo step.
#include "DrumsPatternTestUtil.h"
#include "DrumsGeneratorDigest.h"
#include "DrumsParameters.h"
#include "FactoryContent.h"
#include "PadRoles.h"
#include "PatternExport.h"
#include "Presets.h"
#include "Processor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>

namespace {

using namespace vox::drums;
namespace preset = vox::preset;

int failures = 0;

void check (bool condition, const juce::String& what)
{
    if (! condition)
    {
        std::printf ("FAIL [VOX Drums pad roles]: %s\n", what.toRawUTF8());
        ++failures;
    }
}

void setEnvironment (const char* name, const juce::String& value)
{
#if JUCE_WINDOWS
    _putenv_s (name, value.toRawUTF8());
#else
    setenv (name, value.toRawUTF8(), 1);
#endif
}

using Roles = std::array<PadRole, kit::padCount>;
constexpr auto K = PadRole::kick, S = PadRole::snare, C = PadRole::clap, CH = PadRole::closedHat, OH = PadRole::openHat,
               T = PadRole::tom1, R = PadRole::rim, CY = PadRole::crash, P = PadRole::percussion, N = PadRole::none;

PadRoles explicitRoles (const Roles& r)
{
    PadRoles roles;
    roles.explicitRoles = true;
    roles.role = r;
    return roles;
}

// Expected digests of digest::generatorDigest() per style, produced by the
// generator of c7c29bb (PR #125, before roles) with the same header.
// Integration with #129 (Ambient percussion fix, drm/genre-fidelity @
// 567d696) changes two values, both from that fix alone: Ambient
// (0x574cb8aac3e13957) and Breaks (0x178afe258efca0bf), whose Mutate case
// runs over an Ambient baseline. The other five stay.
// Integrated with #129 (drm/genre-fidelity @ 567d696): Breaks and Ambient
// take the values computed from that branch (design §8.4); the other five
// are unchanged from c7c29bb.
// Re-pinned for the PR 137 §10 cell model (the digest hashes the grid, the
// masks and microTicks; it was 0x658698542ea09551, 0x5366bdf7267d606e,
// 0x354625583cd12f27, 0xb3e1f4f46bbb1050, 0x932bca8aec35fc3f,
// 0x178afe258efca0bf, 0x574cb8aac3e13957). Before re-pinning, every
// generated cell was checked equal to the previous model's (percent micro
// converted by percentToTicks) over the digest's matrix and more (one-off
// check recorded in the step 2 PR); only the representation changed.
constexpr std::array<std::uint64_t, styleCount> c7c29bbDigest = {
    0x0caecfb4787de2c0ull, 0xc7e2dde7a2d3ee8cull, 0x947f76cf829132b0ull, 0x239015bd0543c117ull,
    0xcacc9831a82e874eull, 0x6b856fad4f3b17f8ull, 0x08643b40ef3f94e0ull,
};

void empty (StoredPattern& p, int steps, int bars) { digest::emptyPattern (p, steps, bars); }

// `mapped` holds, on lane map.laneOfSlot[s], exactly `identity`'s lane s; a
// lane without a slot is empty.
bool movedEqual (const StoredPattern& identity, const StoredPattern& mapped, const RoleMap& map)
{
    if (identity.resolution != mapped.resolution || identity.masks != mapped.masks || identity.bars != mapped.bars || identity.seed != mapped.seed)
        return false;
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        const int slot = map.slotOfLane[static_cast<std::size_t> (lane)];
        for (int bar = 0; bar < mapped.bars; ++bar)
            for (int step = 0; step < testutil::stepsOf (mapped); ++step)
            {
                const auto& got = testutil::stepAt (mapped, lane, bar, step);
                const kit::PatternStep want = slot >= 0 ? testutil::stepAt (identity, slot, bar, step) : kit::PatternStep {};
                if (got.on != want.on || got.accent != want.accent || got.velocity != want.velocity || got.probability != want.probability
                    || got.ratchet != want.ratchet || got.microTicks != want.microTicks || got.locked != want.locked)
                    return false;
            }
    }
    return true;
}

bool laneEqual (const StoredPattern& a, const StoredPattern& b, int laneA, int laneB)
{
    for (int bar = 0; bar < a.bars; ++bar)
        for (int step = 0; step < testutil::stepsOf (a); ++step)
        {
            const auto& x = testutil::stepAt (a, laneA, bar, step);
            const auto& y = testutil::stepAt (b, laneB, bar, step);
            if (x.on != y.on || x.accent != y.accent || x.velocity != y.velocity || x.probability != y.probability
                || x.ratchet != y.ratchet || x.microTicks != y.microTicks || x.locked != y.locked)
                return false;
        }
    return true;
}

bool laneEmpty (const StoredPattern& p, int lane)
{
    for (int bar = 0; bar < p.bars; ++bar)
        for (int step = 0; step < testutil::stepsOf (p); ++step)
            if (testutil::stepAt (p, lane, bar, step).on)
                return false;
    return true;
}

GeneratorSettings settings (int style, std::uint32_t seed, float density = 0.7f, float fills = 0.6f)
{
    GeneratorSettings g;
    g.style = static_cast<Style> (style);
    g.seed = seed;
    g.density = density;
    g.complexity = 0.7f;
    g.syncopation = 0.6f;
    g.fills = fills;
    g.variation = 0.8f;
    g.swing = 0.25f;
    g.humanize = 0.3f;
    return g;
}

//==============================================================================
void testCodec()
{
    // R1: round trip, every value and both flags.
    for (int v = 0; v < padRoleCount; ++v)
        for (bool flag : { false, true })
        {
            PadRoles roles;
            roles.explicitRoles = flag;
            roles.role.fill (static_cast<PadRole> (v));
            roles.role[3] = PadRole::openHat;
            const auto s = encodeRoles (roles);
            PadRoles back;
            juce::String error;
            check (s.tag == rolesSectionTag && s.data.size() == 21u && decodeRoles (s, back, error) && back == roles,
                   "R1: DROL round trip, role " + juce::String (v));
        }
    // R2 (codec level): wrong size, version, flag or role value.
    const auto good = encodeRoles (explicitRoles (PadRoles::identity()));
    const auto rejects = [&] (preset::Section s, const char* what) {
        PadRoles out = explicitRoles (PadRoles::identity());
        const auto before = out;
        juce::String error;
        check (! decodeRoles (s, out, error) && error.isNotEmpty() && out == before, juce::String ("R2: decode rejects ") + what);
    };
    auto s = good; s.data.pop_back(); rejects (s, "20 bytes");
    s = good; s.data.push_back (std::byte { 0 }); rejects (s, "22 bytes");
    s = good; s.data.clear(); rejects (s, "an empty section");
    s = good; s.data[0] = std::byte { 1 }; rejects (s, "version 1 (development build, not decoded)");
    s = good; s.data[0] = std::byte { 3 }; rejects (s, "version 3");
    s = good; s.data[3] = std::byte { 1 }; rejects (s, "a version above 2^24");
    s = good; s.data[4] = std::byte { 2 }; rejects (s, "explicit = 2");
    s = good; s.data[5] = std::byte { 13 }; rejects (s, "role 13");
    s = good; s.data[20] = std::byte { 255 }; rejects (s, "role 255");

    // Identity and the map.
    check (RoleMap::from (PadRoles {}).isIdentity() && RoleMap::from (explicitRoles (PadRoles::identity())).isIdentity()
               && RoleMap::identity().isIdentity(),
           "identity roles give the identity map");
    check (roleNotices (PadRoles {}).isEmpty(), "identity roles: no notice");
}

void testIdentityParity()
{
    // R4: the generator with identity roles is byte-identical to c7c29bb.
    const auto d = digest::generatorDigest();
    for (int style = 0; style < styleCount; ++style)
    {
        char text[64];
        std::snprintf (text, sizeof text, "0x%016llx", static_cast<unsigned long long> (d[static_cast<std::size_t> (style)]));
        check (d[static_cast<std::size_t> (style)] == c7c29bbDigest[static_cast<std::size_t> (style)],
               juce::String ("R4: generator digest of ") + styleName (static_cast<Style> (style)) + " equals c7c29bb (got " + juce::String (text) + ")");
    }
    // An explicit identity map is the same as the default argument.
    auto a = std::make_unique<StoredPattern>(), b = std::make_unique<StoredPattern>();
    const auto map = RoleMap::from (explicitRoles (PadRoles::identity()));
    for (int style = 0; style < styleCount; ++style)
    {
        const auto g = settings (style, 77);
        empty (*a, 16, 4);
        empty (*b, 16, 4);
        generatePattern (g, *a);
        generatePattern (g, *b, map);
        check (*a == *b, "R4: explicit identity roles generate the default output");
    }
}

// Every action, with roles `r`, writes the identity result moved to the mapped lanes.
void checkMoved (const Roles& r, const juce::String& name)
{
    const auto map = RoleMap::from (explicitRoles (r));
    auto ia = std::make_unique<StoredPattern>(), ma = std::make_unique<StoredPattern>();
    auto ib = std::make_unique<StoredPattern>(), mb = std::make_unique<StoredPattern>();
    auto iAll = std::make_unique<PatternStore::Everything>(), mAll = std::make_unique<PatternStore::Everything>();
    static constexpr int grids[3][2] = { { 16, 4 }, { 64, 2 }, { 32, 1 } };
    int bad = 0, cases = 0;
    for (int style = 0; style < styleCount; ++style)
        for (std::uint32_t seed = 1; seed <= 4; ++seed)
            for (float density : { 0.2f, 0.7f, 1.0f })
                for (float fills : { 0.0f, 1.0f })
                    for (const auto& grid : grids)
                    {
                        const auto g = settings (style, seed * 31u, density, fills);
                        empty (*ia, grid[0], grid[1]);
                        empty (*ma, grid[0], grid[1]);
                        generatePattern (g, *ia);
                        generatePattern (g, *ma, map);
                        bad += movedEqual (*ia, *ma, map) ? 0 : 1;
                        empty (*ib, grid[0], grid[1]);
                        empty (*mb, grid[0], grid[1]);
                        generateVariation (g, *ia, *ib);
                        generateVariation (g, *ma, *mb, map);
                        bad += movedEqual (*ib, *mb, map) ? 0 : 1;
                        *ib = *ia;
                        *mb = *ma;
                        generateFill (g, *ib, 0);
                        generateFill (g, *mb, 0, map);
                        bad += movedEqual (*ib, *mb, map) ? 0 : 1;
                        cases += 3;
                    }
    for (int style = 0; style < styleCount; ++style)
    {
        const auto g = settings (style, 5);
        for (auto* all : { iAll.get(), mAll.get() })
        {
            for (int slot = 0; slot < PatternStore::slotCount; ++slot)
            {
                empty (all->slots[static_cast<std::size_t> (slot)], 16, 4);
                empty (all->alternates[static_cast<std::size_t> (slot)], 16, 4);
            }
            all->song = SongData::initial();
        }
        generateSong (g, *iAll);
        generateSong (g, *mAll, map);
        bool same = iAll->song == mAll->song;
        for (int slot = 0; slot < PatternStore::slotCount; ++slot)
            same = same && movedEqual (iAll->slots[static_cast<std::size_t> (slot)], mAll->slots[static_cast<std::size_t> (slot)], map)
                   && movedEqual (iAll->alternates[static_cast<std::size_t> (slot)], mAll->alternates[static_cast<std::size_t> (slot)], map);
        bad += same ? 0 : 1;
        ++cases;
    }
    check (bad == 0, "R5/R6/R9/R13/R14 " + name + ": " + juce::String (bad) + " of " + juce::String (cases)
                         + " Pattern/Variation/Fill/Song results differ from the identity result on the mapped lanes");
}

void testMapping()
{
    // R5: kick and snare swapped.
    Roles swapped = PadRoles::identity();
    std::swap (swapped[0], swapped[1]);
    checkMoved (swapped, "kick/snare swapped");
    {
        const auto map = RoleMap::from (explicitRoles (swapped));
        auto p = std::make_unique<StoredPattern>();
        for (int style : { 0, 1, 2, 3 })   // four-on-the-floor styles
        {
            empty (*p, 16, 4);
            generatePattern (settings (style, 3, 0.8f, 0.0f), *p, map);
            bool four = true;
            for (int bar = 0; bar < 4; ++bar)
                for (int step = 0; step < 16; ++step)
                    four = four && (step % 4 == 0 ? testutil::stepAt (*p, 1, bar, step).on : (! testutil::stepAt (*p, 1, bar, step).on || (style == 2 && step == 15)));
            check (four, juce::String ("R5: ") + styleName (static_cast<Style> (style)) + " four-on-the-floor on pad 2");
        }
        empty (*p, 16, 4);
        generatePattern (settings (5, 3, 0.8f, 0.0f), *p, map);   // Breaks: snare always on 5 and 13
        bool back = true;
        for (int bar = 0; bar < 4; ++bar)
            back = back && testutil::stepAt (*p, 0, bar, 4).on && testutil::stepAt (*p, 0, bar, 12).on;
        check (back, "R5: the Breaks back-beat is on pad 1");
    }
    // R6: closed hat on pad 9, open hat on pad 12 (pads 4/5 percussion).
    Roles hats = PadRoles::identity();
    hats[3] = P;
    hats[4] = P;
    hats[8] = CH;
    hats[11] = OH;
    checkMoved (hats, "hats on pads 9 and 12");
    {
        const auto map = RoleMap::from (explicitRoles (hats));
        // R9: percussion slots follow pad order: pads 4, 5, 10, 11, 13..16.
        const int expected[8] = { 3, 4, 9, 10, 12, 13, 14, 15 };
        bool order = true;
        for (int i = 0; i < 8; ++i)
            order = order && map.laneOfSlot[static_cast<std::size_t> (8 + i)] == expected[i];
        check (order && map.laneOfSlot[3] == 8 && map.laneOfSlot[4] == 11, "R9: percussion ordinals follow pad order");
        auto p = std::make_unique<StoredPattern>();
        empty (*p, 16, 4);
        generatePattern (settings (0, 9, 0.8f, 0.0f), *p, map);   // Full-on: open hat on every off-beat 8th
        bool offbeat = true;
        for (int bar = 0; bar < 4; ++bar)
            for (int step = 0; step < 16; ++step)
                offbeat = offbeat && (step % 4 != 2 || (testutil::stepAt (*p, 11, bar, step).on && ! testutil::stepAt (*p, 8, bar, step).on));
        check (offbeat, "R6: Full-on open hat on the off-beats of pad 12, closed hat never there on pad 9");
    }
    // Random full permutations (seeded, reproducible).
    std::mt19937 random (132);
    for (int i = 0; i < 4; ++i)
    {
        Roles r = PadRoles::identity();
        std::shuffle (r.begin(), r.end(), random);
        checkMoved (r, "random permutation " + juce::String (i + 1));
    }

    auto id = std::make_unique<StoredPattern>(), p = std::make_unique<StoredPattern>();
    // R7: no closed-hat pad.
    {
        Roles r = PadRoles::identity();
        r[3] = N;
        const auto map = RoleMap::from (explicitRoles (r));
        bool ok = map.laneOfSlot[3] < 0;
        for (int style = 0; style < styleCount; ++style)
        {
            empty (*id, 16, 4);
            empty (*p, 16, 4);
            generatePattern (settings (style, 4), *id);
            generatePattern (settings (style, 4), *p, map);
            ok = ok && laneEmpty (*p, 3);
            for (int lane = 0; lane < kit::padCount; ++lane)
                ok = ok && (lane == 3 || laneEqual (*id, *p, lane, lane));
        }
        check (ok, "R7: a missing role gets no hits, the other roles are unchanged");
        check (roleNotices (explicitRoles (r)).contains ("This style needs a closed hat: no pad has the Closed hat role"), "R7: missing-role notice");
    }
    // R8 / R10: a second kick on pad 9; percussion moves up one pad.
    {
        Roles r = PadRoles::identity();
        r[8] = K;
        const auto map = RoleMap::from (explicitRoles (r));
        check (map.laneOfSlot[0] == 0 && map.slotOfLane[8] < 0 && map.laneOfSlot[8] == 9 && map.laneOfSlot[15] < 0 && map.slotOfLane[15] == 14,
               "R8: the first kick pad is used; pad 9 gets nothing; percussion moves up");
        check (roleNotices (explicitRoles (r)).contains ("2 kick pads: pad 1 plays generated kick hits"), "R8: duplicate notice");
        // R10: pre-filled pattern; lane 9 (duplicate) cleared except locked cells.
        bool ok = true;
        for (int style = 0; style < styleCount; ++style)
        {
            empty (*p, 16, 4);
            for (int lane = 0; lane < kit::padCount; ++lane)
                for (int step = 0; step < 16; ++step)
                    testutil::stepAt (*p, lane, 0, step) = { true, false, 99, 100, 1, false, 0 };
            testutil::stepAt (*p, 8, 0, 5).locked = true;
            auto g = settings (style, 6);
            generatePattern (g, *p, map);
            ok = ok && testutil::stepAt (*p, 8, 0, 5).on && testutil::stepAt (*p, 8, 0, 5).velocity == 99;
            for (int step = 0; step < 16; ++step)
                ok = ok && (step == 5 || ! testutil::stepAt (*p, 8, 0, step).on);
        }
        check (ok, "R10: lanes with a duplicate role or no slot are cleared, locked cells kept");
    }
    // R10 with role none, and Variation clears such lanes as well.
    {
        Roles r = PadRoles::identity();
        r[6] = N;
        const auto map = RoleMap::from (explicitRoles (r));
        auto b = std::make_unique<StoredPattern>();
        empty (*p, 16, 4);
        generatePattern (settings (3, 8), *p, map);
        testutil::stepAt (*p, 6, 1, 4) = { true, false, 90, 100, 1, false, 0 };   // user hit on the none lane of A
        empty (*b, 16, 4);
        generateVariation (settings (3, 8), *p, *b, map);
        check (laneEmpty (*b, 6), "R10: Variation B clears a lane with role none");
    }
    // R11: lane and step locks with remapped roles.
    {
        const auto map = RoleMap::from (explicitRoles (swapped));
        bool ok = true;
        for (int style = 0; style < styleCount; ++style)
        {
            auto g = settings (style, 12);
            empty (*p, 16, 4);
            generatePattern (settings ((style + 3) % styleCount, 40), *p);
            testutil::stepAt (*p, 0, 2, 7).locked = true;
            testutil::stepAt (*p, 12, 3, 15).locked = true;
            auto before = std::make_unique<StoredPattern> (*p);
            g.laneLocks = 1u << 1;   // pad 2, where the kick now is
            generatePattern (g, *p, map);
            ok = ok && laneEqual (*before, *p, 1, 1) && testutil::stepAt (*p, 0, 2, 7).on == testutil::stepAt (*before, 0, 2, 7).on
                 && testutil::stepAt (*p, 0, 2, 7).velocity == testutil::stepAt (*before, 0, 2, 7).velocity && testutil::stepAt (*p, 12, 3, 15).locked
                 && testutil::stepAt (*p, 12, 3, 15).on == testutil::stepAt (*before, 12, 3, 15).on;
        }
        check (ok, "R11: lane and step locks are kept with remapped roles");
    }
}

void testMutate()
{
    // R12: physical-lane masks against the scratch result S.
    const Roles reordered = [] { Roles r = PadRoles::identity(); std::swap (r[0], r[1]); std::swap (r[3], r[9]); return r; }();
    const Roles duplicate = [] { Roles r = PadRoles::identity(); r[2] = K; return r; }();
    const Roles missing = [] { Roles r = PadRoles::identity(); r[3] = P; return r; }();
    const Roles withNone = [] { Roles r = PadRoles::identity(); r[5] = N; r[14] = N; return r; }();
    const std::pair<Roles, const char*> sets[] = { { PadRoles::identity(), "identity" }, { reordered, "reordered" },
                                                   { duplicate, "duplicate" }, { missing, "missing" }, { withNone, "with none" } };
    const std::uint32_t masks[] = { 0u, 1u << 0, (1u << 2) | (1u << 9), 0xffffu };
    auto base = std::make_unique<StoredPattern>(), s = std::make_unique<StoredPattern>(), m = std::make_unique<StoredPattern>(),
         again = std::make_unique<StoredPattern>();
    int bad = 0, cases = 0;
    for (const auto& [roles, name] : sets)
    {
        const auto map = RoleMap::from (explicitRoles (roles));
        for (int style = 0; style < styleCount; ++style)
            for (auto mask : masks)
                for (std::uint32_t seed : { 3u, 11u })
                {
                    auto g = settings (style, seed);
                    empty (*base, 16, 4);
                    generatePattern (settings ((style + 2) % styleCount, seed + 100), *base);
                    testutil::stepAt (*base, 0, 0, 4).locked = true;
                    testutil::stepAt (*base, 9, 3, 10).locked = true;
                    // S: the full result for these roles, without locks.
                    empty (*s, 16, 4);
                    mutateLanes (g, 0xffffu, *s, map);
                    *m = *base;
                    mutateLanes (g, mask, *m, map);
                    *again = *base;
                    mutateLanes (g, mask, *again, map);
                    bool ok = *m == *again;   // deterministic
                    for (int lane = 0; lane < kit::padCount; ++lane)
                        for (int bar = 0; bar < 4; ++bar)
                            for (int step = 0; step < 16; ++step)
                            {
                                const auto& got = testutil::stepAt (*m, lane, bar, step);
                                const auto& was = testutil::stepAt (*base, lane, bar, step);
                                const bool selected = (mask & (1u << lane)) != 0;
                                const auto& want = ! selected || was.locked ? was : testutil::stepAt (*s, lane, bar, step);
                                ok = ok && got.on == want.on && got.velocity == want.velocity && got.microTicks == want.microTicks
                                     && got.probability == want.probability && got.ratchet == want.ratchet && got.locked == want.locked;
                            }
                    if (mask == 0)
                        ok = ok && *m == *base;
                    bad += ok ? 0 : 1;
                    ++cases;
                }
        if (bad != 0)
            std::printf ("  mutate failures after role set %s: %d\n", name, bad);
    }
    check (bad == 0, "R12: Mutate Lanes: " + juce::String (bad) + " of " + juce::String (cases) + " cases differ");

    // Negative: kick on pad 2; selecting physical lane 1 changes only lane 1
    // (it takes the snare part), never the kick's pad.
    Roles r = PadRoles::identity();
    std::swap (r[0], r[1]);
    const auto map = RoleMap::from (explicitRoles (r));
    empty (*base, 16, 4);
    generatePattern (settings (6, 50), *base);
    *m = *base;
    mutateLanes (settings (0, 51), 1u << 0, *m, map);
    empty (*s, 16, 4);
    mutateLanes (settings (0, 51), 0xffffu, *s, map);
    bool only = laneEqual (*m, *s, 0, 0);
    for (int lane = 1; lane < kit::padCount; ++lane)
        only = only && laneEqual (*m, *base, lane, lane);
    check (only, "R12: the mask is physical lanes; the default layout is never used");
}

//==============================================================================
juce::MemoryBlock stateOf (DrumsProcessor& p)
{
    juce::MemoryBlock block;
    p.getStateInformation (block);
    return block;
}

std::unique_ptr<DrumsProcessor> makeProcessor()
{
    auto p = std::make_unique<DrumsProcessor>();
    p->setRateAndBufferSizeDetails (48000.0, 512);
    p->prepareToPlay (48000.0, 512);
    return p;
}

preset::StateDocument documentOf (DrumsProcessor& p)
{
    const auto block = stateOf (p);
    auto decoded = preset::decode ({ static_cast<const std::byte*> (block.getData()), block.getSize() }, p.product().stateTag, p.product().stateSchema);
    check (decoded.ok(), "state decodes");
    return decoded.ok() ? std::move (decoded.document) : preset::StateDocument {};
}

juce::MemoryBlock encodeDocument (const preset::StateDocument& doc)
{
    juce::MemoryBlock block;
    if (const auto bytes = preset::encode (doc))
        block.replaceAll (bytes->data(), bytes->size());
    return block;
}

std::set<preset::Tag> tagsOf (const preset::StateDocument& doc)
{
    std::set<preset::Tag> tags;
    for (const auto& s : doc.sections)
        tags.insert (s.tag);
    return tags;
}

void testState()
{
    // R3 / R16d: no explicit roles -> no DROL, the same sections as before.
    auto p = makeProcessor();
    {
        const auto doc = documentOf (*p);
        check (doc.find (rolesSectionTag) == nullptr, "R16d: a project without explicit roles saves without DROL");
        check (p->padRoles() == PadRoles {}, "R3: a new project has identity roles");
    }
    // R1 through the processor: explicit roles save and restore.
    Roles r = PadRoles::identity();
    std::swap (r[0], r[1]);
    check (p->setPadRoles (r) && ! p->setPadRoles (r), "setPadRoles changes once");
    const auto saved = stateOf (*p);
    {
        const auto doc = documentOf (*p);
        const auto* s = doc.find (rolesSectionTag);
        check (s != nullptr && s->data.size() == 21u, "R1: explicit roles are saved as DROL (21 bytes)");
        auto q = makeProcessor();
        q->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        check (q->lastStateError().isEmpty() && q->padRoles() == explicitRoles (r) && stateOf (*q) == saved,
               "R1: DROL restores and saves back byte for byte");
        // R3: a state without DROL restores identity roles.
        auto old = doc;
        old.sections.erase (std::remove_if (old.sections.begin(), old.sections.end(), [] (const preset::Section& x) { return x.tag == rolesSectionTag; }),
                            old.sections.end());
        const auto block = encodeDocument (old);
        q->setStateInformation (block.getData(), static_cast<int> (block.getSize()));
        check (q->lastStateError().isEmpty() && q->padRoles() == PadRoles {}, "R3: a state without DROL gives identity roles");
        // explicit = 0: identity, and not written back.
        auto derived = doc;
        for (auto& x : derived.sections)
            if (x.tag == rolesSectionTag)
                x = encodeRoles ([&] { PadRoles d; d.role = r; return d; }());
        const auto derivedBlock = encodeDocument (derived);
        q->setStateInformation (derivedBlock.getData(), static_cast<int> (derivedBlock.getSize()));
        check (q->lastStateError().isEmpty() && q->padRoles() == PadRoles {} && documentOf (*q).find (rolesSectionTag) == nullptr,
               "DROL with explicit = 0 gives identity roles and is not written back");
    }
    // R2: malformed DROL rejects the restore; nothing changes.
    {
        const auto before = stateOf (*p);
        const auto doc = documentOf (*p);
        const auto variant = [&] (const std::function<void (preset::Section&)>& damage, const char* what) {
            auto bad = doc;
            for (auto& x : bad.sections)
                if (x.tag == rolesSectionTag)
                    damage (x);
            const auto block = encodeDocument (bad);
            p->setStateInformation (block.getData(), static_cast<int> (block.getSize()));
            check (p->lastStateError().isNotEmpty() && stateOf (*p) == before && p->padRoles() == explicitRoles (r),
                   juce::String ("R2: malformed DROL (") + what + ") is rejected with the live state unchanged");
        };
        variant ([] (preset::Section& x) { x.data.pop_back(); }, "short");
        variant ([] (preset::Section& x) { x.data.push_back (std::byte { 1 }); }, "long");
        variant ([] (preset::Section& x) { x.data[0] = std::byte { 9 }; }, "version");
        variant ([] (preset::Section& x) { x.data[4] = std::byte { 7 }; }, "flag");
        variant ([] (preset::Section& x) { x.data[12] = std::byte { 13 }; }, "role value");
    }
    // R17: the state keeps the c7c29bb container: same product tag and
    // schema, and every section c7c29bb writes is still there. c7c29bb keeps
    // the unknown DROL byte for byte (base-class rule, tested in
    // suite_drums_candidate_tests "unknown sections"); the cross-build run is
    // in the PR evidence.
    {
        auto plain = makeProcessor();
        const auto before = tagsOf (documentOf (*plain));
        auto withRoles = tagsOf (documentOf (*p));
        withRoles.erase (rolesSectionTag);
        check (before == withRoles && p->product().stateSchema == 1, "R17: DROL is the only added section; schema 1 unchanged");
    }
    // Undo/redo of a role edit, and the history ends at a restore.
    {
        auto q = makeProcessor();
        check (q->setPadRole (5, PadRole::rim) && q->padRoles().role[5] == PadRole::rim && q->padRoles().explicitRoles, "setPadRole");
        q->patternHistory().undo (q->patterns());
        check (q->padRoles().role[5] == PadRole::tom1, "undo restores the previous role");
        q->patternHistory().redo (q->patterns());
        check (q->padRoles().role[5] == PadRole::rim, "redo reapplies the role");
    }
}

void testProcessor()
{
    // generate() uses the roles; R15 MIDI export parity.
    Roles r = PadRoles::identity();
    std::swap (r[0], r[1]);
    std::swap (r[3], r[10]);
    const auto map = RoleMap::from (explicitRoles (r));
    auto id = makeProcessor(), mapped = makeProcessor();
    mapped->setPadRoles (r);
    for (auto* p : { id.get(), mapped.get() })
    {
        auto g = p->generator();
        g.style = Style::techno;
        p->setGenerator (g);
        p->generate (DrumsProcessor::Generate::pattern, 0xffffu, false);
    }
    auto a = std::make_unique<StoredPattern>(), b = std::make_unique<StoredPattern>();
    id->patterns().copySlot (id->currentSlot(), *a);
    mapped->patterns().copySlot (mapped->currentSlot(), *b);
    check (movedEqual (*a, *b, map), "processor: Generate Pattern writes the roles' lanes");

    auto snapshot = mapped->captureExport (ExportSnapshot::Scope::pattern);
    // Expected: the identity pattern moved to the mapped lanes, exported with the pads' own notes.
    auto moved = std::make_unique<StoredPattern> (*a);
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        const int slot = map.slotOfLane[static_cast<std::size_t> (lane)];
        for (int bar = 0; bar < moved->bars; ++bar)
            for (int step = 0; step < testutil::stepsOf (*moved); ++step)
                testutil::stepAt (*moved, lane, bar, step) = testutil::stepAt (*a, slot, bar, step);
    }
    ExportOptions options;
    options.bpm = snapshot->bpm;
    const auto expected = exportMidi (moved->view(), snapshot->context, snapshot->notes, options, snapshot->trackName);
    check (DrumsProcessor::renderExport (*snapshot) == expected, "R15: export of a remapped pattern = the moved pattern on the pads' notes");

    // R13 through the processor: Song break slot has no kick on the kick pad.
    mapped->generate (DrumsProcessor::Generate::song, 0xffffu, false);
    mapped->patterns().copySlot (3, *b);
    check (laneEmpty (*b, 1), "R13: the Song break slot clears the mapped kick pad");
}

void testPresets (const juce::File& folder)
{
    setEnvironment ("VOX_DRUMS_PRESETS", folder.getFullPathName());
    Roles custom = PadRoles::identity();
    std::swap (custom[0], custom[1]);
    custom[2] = PadRole::tom1;

    // R16a: a kit preset with DROL replaces the roles.
    auto source = makeProcessor();
    source->setPadRoles (custom);
    const auto kitPreset = source->presets().capture (PresetType::kit, "Roles kit");
    auto p = makeProcessor();
    check (p->presets().apply (kitPreset.getData(), kitPreset.getSize()).wasOk() && p->padRoles() == explicitRoles (custom),
           "R16a: a kit preset with DROL sets its roles");
    // A legacy kit (factory) sets identity roles with the notice; Undo Load restores.
    check (p->presets().loadFactoryKit (0).wasOk() && p->padRoles() == PadRoles {}
               && p->presets().lastNotice() == "Roles set to the default layout of this kit",
           "R16a: a legacy kit sets identity roles and says so");
    check (p->presets().undoLoad().wasOk() && p->padRoles() == explicitRoles (custom), "R16a: Undo Load restores the previous roles");
    check (p->presets().loadFactoryKit (1).wasOk() && p->presets().loadFactoryKit (2).wasOk() && p->presets().lastNotice().isEmpty(),
           "R16a: no notice when no explicit roles are replaced");

    // R16b: a legacy pad preset (no DROL) keeps the target's role.
    auto plain = makeProcessor();
    const auto legacyPad = plain->presets().capture (PresetType::pad, "Legacy pad", 2);
    {
        preset::StateDocument doc;
        auto decoded = preset::decode ({ static_cast<const std::byte*> (legacyPad.getData()), legacyPad.getSize() }, plain->product().stateTag, 1);
        check (decoded.ok() && decoded.document.find (rolesSectionTag) == nullptr, "R16b: a pad preset without explicit roles has no DROL");
    }
    auto q = makeProcessor();
    q->setPadRoles (custom);
    check (q->presets().apply (legacyPad.getData(), legacyPad.getSize(), 0).wasOk() && q->padRoles() == explicitRoles (custom)
               && ! q->presets().roleOffer(),
           "R16b: a legacy pad preset from pad 3 onto pad 1 keeps every role; no offer");

    // R16c: a modern pad preset: pad 3 of `source` is a tom.
    const auto modernPad = source->presets().capture (PresetType::pad, "Tom pad", 2);
    {
        auto decoded = preset::decode ({ static_cast<const std::byte*> (modernPad.getData()), modernPad.getSize() }, source->product().stateTag, 1);
        PadRoles stored;
        juce::String error;
        const auto* s = decoded.ok() ? decoded.document.find (rolesSectionTag) : nullptr;
        bool othersNone = s != nullptr && decodeRoles (*s, stored, error);
        for (int pad = 0; pad < kit::padCount && othersNone; ++pad)
            othersNone = pad == 2 ? stored.role[2] == PadRole::tom1 : stored.role[static_cast<std::size_t> (pad)] == PadRole::none;
        check (othersNone, "R16c: a pad preset stores only its own pad's role");
    }
    auto t = makeProcessor();
    t->setPadRoles (PadRoles::identity());
    const auto before = t->padRoles();
    check (t->presets().apply (modernPad.getData(), modernPad.getSize(), 0).wasOk() && t->padRoles() == before,
           "R16c: loading keeps the target's role");
    const auto offer = t->presets().roleOffer();
    check (offer && offer->pad == 0 && offer->role == PadRole::tom1, "R16c: the preset role is offered for the target pad");
    check (t->presets().applyRoleOffer(), "R16c: Apply");
    auto after = before.role;
    after[0] = PadRole::tom1;
    check (t->padRoles().role == after && ! t->presets().roleOffer(), "R16c: Apply sets only the target pad");
    t->patternHistory().undo (t->patterns());
    check (t->padRoles() == before, "R16c: undo restores the role");
    t->patternHistory().redo (t->patterns());
    check (t->padRoles().role == after, "R16c: redo reapplies it");
    // Same role: no offer. Onto pad 6 (a tom already): nothing to offer.
    check (t->presets().apply (modernPad.getData(), modernPad.getSize(), 5).wasOk() && ! t->presets().roleOffer(),
           "R16c: no offer when the roles are equal");

    // R16d: factory manifest hashes are checked by suite_drums_candidate_tests;
    // factory content carries no DROL.
    auto f = makeProcessor();
    f->presets().loadFactoryKit (0);
    f->presets().loadFactoryPatterns (0);
    check (documentOf (*f).find (rolesSectionTag) == nullptr, "R16d: factory content writes no DROL");
}

void testProposal()
{
    // PR 137 §2.6 as approved on #143: GM note 45 proposes tom 2, but the
    // legacy/default layout keeps pad 6 (note 45) = tom 1 until the user applies
    // the proposal; the difference is shown, Cancel changes nothing, Apply is one
    // undoable step.
    {
        auto p = makeProcessor();
        p->presets().loadFactoryKit (0);
        check (p->kitStore().pad (5).note == 45 && ! p->padRoles().explicitRoles && p->padRoles().role[5] == PadRole::tom1,
               "GM 45: the default kit loads with pad 6 (note 45) = tom 1");
        const auto before = stateOf (*p);
        auto proposal = p->proposeRoles();
        check (proposal.role[5] == PadRole::tom2 && (proposal.changedPads (p->padRoles()) & (1u << 5)) != 0,
               "GM 45: the proposal shows pad 6 tom 1 -> tom 2 as a difference");
        check (stateOf (*p) == before && p->padRoles().role[5] == PadRole::tom1 && ! p->patternHistory().canUndo(),
               "GM 45: preview / Cancel changes nothing");
        for (int pad = 0; pad < kit::padCount; ++pad)
            if (proposal.needsChoice[static_cast<std::size_t> (pad)])
                proposal.choose (pad, p->padRoles().role[static_cast<std::size_t> (pad)]);
        check (p->applyRoleProposal (proposal) && p->padRoles().explicitRoles && p->padRoles().role[5] == PadRole::tom2,
               "GM 45: explicit Apply makes pad 6 tom 2");
        p->patternHistory().undo (p->patterns());
        check (p->padRoles().role[5] == PadRole::tom1, "GM 45: undo returns pad 6 to tom 1");
        p->patternHistory().redo (p->patterns());
        check (p->padRoles().role[5] == PadRole::tom2, "GM 45: redo applies tom 2 again");
    }
    // R18: factory kits; nothing is written by the proposal.
    for (int k = 0; k < factoryKitCount; ++k)
    {
        auto p = makeProcessor();
        p->presets().loadFactoryKit (k);
        const auto before = stateOf (*p);
        const auto proposal = p->proposeRoles();
        const auto kitNow = p->kitStore().snapshot();
        bool consistent = true;
        for (int pad = 0; pad < kit::padCount; ++pad)
        {
            const auto model = kitNow.pads[static_cast<std::size_t> (pad)].model;
            const auto role = proposal.role[static_cast<std::size_t> (pad)];
            const bool hat = role == PadRole::closedHat || role == PadRole::openHat;
            static constexpr PadRole byModel[kit::modelCount] = { PadRole::kick, PadRole::snare, PadRole::clap, PadRole::closedHat,
                                                                  PadRole::tom1, PadRole::rim, PadRole::crash, PadRole::percussion };
            // Roles v2 (PR 137 §2.6): a tom is tom 1, 2 or 3 (a fourth: percussion, asked); a
            // cymbal is crash or ride (a second crash: percussion).
            const bool tom = role == PadRole::tom1 || role == PadRole::tom2 || role == PadRole::tom3
                          || (role == PadRole::percussion && proposal.needsChoice[static_cast<std::size_t> (pad)]);
            const bool cymbal = role == PadRole::crash || role == PadRole::ride || role == PadRole::percussion;
            consistent = consistent
                      && (model == kit::Model::hat ? hat
                          : model == kit::Model::tom ? tom
                          : model == kit::Model::cymbal ? cymbal
                          : role == byModel[static_cast<int> (model)]);
        }
        check (consistent, "R18: factory kit " + juce::String (k + 1) + ": proposed roles follow the pad models");
        check (proposal.role[3] == PadRole::closedHat && proposal.role[4] == PadRole::openHat && ! proposal.needsChoice[3]
                   && ! proposal.needsChoice[4],
               "R18: factory hats on notes 42/46 are closed/open without a question");
        check (stateOf (*p) == before && p->padRoles() == PadRoles {}, "R18: the preview writes nothing (Cancel = no-op)");
    }
    // R19: hats by note, then decay; ties and samples ask.
    auto kitWith = [] (std::initializer_list<std::tuple<int, int, float, kit::Source>> hats) {
        auto k = defaultKit();
        for (auto [pad, note, decay, source] : hats)
        {
            auto& s = k.pads[static_cast<std::size_t> (pad)];
            s.model = kit::Model::hat;
            s.note = static_cast<std::uint8_t> (note);
            s.decay = decay;
            s.source = source;
        }
        return k;
    };
    const auto synth = kit::Source::synth;
    auto proposal = proposeRolesFromModels (kitWith ({ { 3, 44, 0.1f, synth }, { 4, 46, 0.05f, synth } }));
    check (proposal.role[3] == CH && proposal.role[4] == OH && proposal.complete(), "R19: notes 44/46 decide over decay");
    proposal = proposeRolesFromModels (kitWith ({ { 3, 60, 0.6f, synth }, { 4, 61, 0.1f, synth } }));
    check (proposal.role[3] == OH && proposal.role[4] == CH && proposal.complete(), "R19: other notes: the shorter decay is closed");
    proposal = proposeRolesFromModels (kitWith ({ { 3, 60, 0.3f, synth }, { 4, 61, 0.3f, synth } }));
    check (proposal.needsChoice[3] && proposal.needsChoice[4] && ! proposal.complete(), "R19: equal decays: both rows are \"?\"");
    proposal = proposeRolesFromModels (kitWith ({ { 3, 42, 0.1f, kit::Source::sample }, { 4, 46, 0.4f, synth } }));
    check (proposal.needsChoice[3] && ! proposal.needsChoice[4], "R19: a hat playing a sample is \"?\"");
    proposal = proposeRolesFromModels (kitWith ({ { 3, 42, 0.1f, synth }, { 4, 60, 0.4f, synth } }));
    check (proposal.role[4] == OH && proposal.complete(), "R19: one closed hat by note, the other hat is open");

    auto p = makeProcessor();
    p->kitStore().setAll (kitWith ({ { 3, 60, 0.3f, synth }, { 4, 61, 0.3f, synth } }));
    for (int pad : { 3, 4 })   // the decay the user hears is the host parameter
        if (auto* decay = p->state().getParameter (padPrefix (pad) + ".decay"))
            decay->setValueNotifyingHost (decay->convertTo0to1 (0.3f));
    auto preview = p->proposeRoles();
    const auto before = stateOf (*p);
    check (! preview.complete() && ! p->applyRoleProposal (preview) && stateOf (*p) == before, "R19: Apply is blocked while a row is \"?\"");
    preview.choose (3, CH);
    preview.choose (4, OH);
    check (preview.complete() && p->applyRoleProposal (preview) && p->padRoles().role == preview.role, "R19: Apply after the choice");
    p->patternHistory().undo (p->patterns());
    check (p->padRoles() == PadRoles {} && ! p->patternHistory().canUndo(), "R19: Apply is one undo step");
}

} // namespace

// PR 137 §2 (roles v2): the 16-pad maps of §2.3, the part map with ride /
// tom 2 / tom 3, style-aware notices (§2.4, §2.5), Use pad N, generation to
// the ride pad, and the GM hints of the proposal (§2.6).
namespace {
constexpr auto RD = PadRole::ride, T2 = PadRole::tom2, T3 = PadRole::tom3;
// §2.3 columns (pads 1..16).
constexpr Roles tranceMap { K, S, C, CH, OH, T, R, CY, RD, T2, T3, P, P, P, P, P };
constexpr Roles rockMap { K, S, R, CH, OH, T, T2, T3, CY, RD, P, P, P, P, P, P };
constexpr Roles metalMap = rockMap;

style::Definition partsOnly (std::vector<std::uint32_t> required, std::uint32_t optional)
{
    style::Definition d;
    d.requiredParts = std::move (required);
    d.optionalParts = optional;
    return d;
}
constexpr std::uint32_t bit (int part) { return 1u << part; }
constexpr int pKick = 0, pSnare = 1, pClap = 2, pClosed = 3, pOpen = 4, pTom1 = 5, pRim = 6, pCrash = 7, pPerc1 = 8, pRide = 16, pTom2 = 17, pTom3 = 18;

bool hasNotice (const std::vector<RoleNotice>& notices, RoleNotice::Kind kind, const juce::String& text)
{
    for (const auto& n : notices)
        if (n.kind == kind && n.text == text)
            return true;
    return false;
}
} // namespace

void testRolesV2()
{
    // Parts of roles; identity: no pad for ride, tom 2, tom 3.
    check (partOfRole (PadRole::kick) == 0 && partOfRole (PadRole::tom1) == 5 && partOfRole (PadRole::crash) == 7 && partOfRole (RD) == 16
               && partOfRole (T2) == 17 && partOfRole (T3) == 18 && partOfRole (P) == -1 && partOfRole (N) == -1,
           "roles v2: the part of each role");
    const auto id = RoleMap::identity();
    check (id.laneOfSlot[16] < 0 && id.laneOfSlot[17] < 0 && id.laneOfSlot[18] < 0, "roles v2: the default map has no ride, tom 2 or tom 3 pad");
    // §2.3 maps: valid, no duplicate instrument role, the parts land on the stated pads.
    for (const auto& [name, map] : { std::pair { "Trance", tranceMap }, std::pair { "Rock", rockMap }, std::pair { "Metal", metalMap } })
    {
        const auto m = RoleMap::from (explicitRoles (map));
        std::array<int, padRoleCount> count {};
        for (auto r : map)
            ++count[static_cast<std::size_t> (r)];
        bool unique = true;
        for (int r = 1; r < padRoleCount; ++r)
            unique = unique && (r == static_cast<int> (P) || count[static_cast<std::size_t> (r)] <= 1);
        PadRoles back;
        juce::String error;
        check (unique && decodeRoles (encodeRoles (explicitRoles (map)), back, error) && back.role == map,
               juce::String ("roles v2: the ") + name + " map is valid, round trips and has no duplicate instrument role");
        bool lanes = true;
        for (int lane = 0; lane < kit::padCount; ++lane)
        {
            const int part = m.slotOfLane[static_cast<std::size_t> (lane)];
            lanes = lanes && part >= -1 && part < RoleMap::parts && (part < 0 || m.laneOfSlot[static_cast<std::size_t> (part)] == lane);
        }
        check (lanes, juce::String ("roles v2: the ") + name + " map sends every part to one pad 0-15");
    }
    {
        const auto m = RoleMap::from (explicitRoles (tranceMap));
        check (m.laneOfSlot[pRide] == 8 && m.laneOfSlot[pTom2] == 9 && m.laneOfSlot[pTom3] == 10 && m.laneOfSlot[pPerc1] == 11
                   && m.laneOfSlot[pPerc1 + 4] == 15 && m.laneOfSlot[pPerc1 + 5] < 0,
               "roles v2: Trance map: ride pad 9, tom 2 pad 10, tom 3 pad 11, perc 1-5 on pads 12-16");
        const auto r = RoleMap::from (explicitRoles (rockMap));
        check (r.laneOfSlot[pClap] < 0 && r.laneOfSlot[pRim] == 2 && r.laneOfSlot[pTom2] == 6 && r.laneOfSlot[pTom3] == 7 && r.laneOfSlot[pCrash] == 8
                   && r.laneOfSlot[pRide] == 9,
               "roles v2: Rock map: no clap; rim pad 3, tom 2/3 pads 7/8, crash pad 9, ride pad 10");
    }
    // Style-aware notices (§2.4).
    const auto metal = partsOnly ({ bit (pKick), bit (pSnare), bit (pCrash), bit (pRide), bit (pTom1), bit (pTom3) },
                                  bit (pClosed) | bit (pOpen) | bit (pTom2) | bit (pRim) | bit (pPerc1) | bit (pPerc1 + 6));
    const auto rock = partsOnly ({ bit (pKick), bit (pSnare), bit (pClosed), bit (pCrash) }, bit (pOpen) | bit (pRide) | bit (pTom1));
    const auto house = partsOnly ({ bit (pKick), bit (pClap) | bit (pSnare), bit (pClosed), bit (pOpen) }, bit (pRim));
    {
        const auto notices = roleNotices (explicitRoles (metalMap), &metal, "Heavy Metal");
        bool anyMissing = false;
        for (const auto& n : notices)
            anyMissing = anyMissing || n.kind == RoleNotice::Kind::missingRequired;
        check (! anyMissing, "notices: the Metal map has every part Heavy Metal requires");
        check (hasNotice (notices, RoleNotice::Kind::missingOptional, "Not used: perc 7"),
               "notices: an unused optional part (perc 7: the map has 6 percussion pads) is one quiet note");
    }
    {
        auto noRide = metalMap;
        noRide[9] = P;
        const auto notices = roleNotices (explicitRoles (noRide), &metal, "Heavy Metal");
        bool found = false;
        for (const auto& n : notices)
            found = found || (n.kind == RoleNotice::Kind::missingRequired && n.role == RD && n.text == "Heavy Metal needs a ride: no pad has the Ride role");
        check (found, "notices: a missing required ride is a warning naming the role (Assign... opens the Ride role)");
    }
    {
        // No clap warning for rock: a rock style does not use a clap.
        bool clap = false;
        for (const auto& n : roleNotices (explicitRoles (rockMap), &rock, "Hard Rock"))
            clap = clap || n.text.containsIgnoreCase ("clap");
        check (! clap, "notices: no clap warning on a rock kit for a rock style");
    }
    {
        // Alternatives: clap or snare.
        Roles r = PadRoles::identity();
        r[1] = P;   // no snare: the clap satisfies it
        bool missing = false;
        for (const auto& n : roleNotices (explicitRoles (r), &house, "House"))
            missing = missing || n.kind == RoleNotice::Kind::missingRequired;
        check (! missing, "notices: a required clap-or-snare is satisfied by the clap alone");
        r[2] = P;   // neither
        check (hasNotice (roleNotices (explicitRoles (r), &house, "House"), RoleNotice::Kind::missingRequired,
                          "House needs a snare or a clap: no pad has the roles"),
               "notices: neither clap nor snare: one warning naming both");
    }
    {
        // Duplicates (§2.5): the lowest pad plays; Use pad N swaps in one step.
        auto two = metalMap;
        two[13] = RD;   // a second ride on pad 14
        const auto notices = roleNotices (explicitRoles (two), &metal, "Heavy Metal");
        const RoleNotice* dup = nullptr;
        for (const auto& n : notices)
            if (n.kind == RoleNotice::Kind::duplicate && n.role == RD)
                dup = &n;
        check (dup != nullptr && dup->pad == 9 && dup->otherPad == 13 && dup->text == "2 ride pads: pad 10 plays generated ride hits",
               "notices: two ride pads: pad 10 plays, Use pad 14 offered");
        check (dup != nullptr
                   && dup->actionTip == "Use pad 14: pad 10 gets the role None and keeps its notes; pad 14 plays the generated ride hits. One undo step.",
               "notices: the Use pad N tooltip says what it does (no swap claimed)");
        check (RoleMap::from (explicitRoles (two)).laneOfSlot[pRide] == 9, "duplicate ride: the lowest pad plays it");
        if (dup != nullptr)
        {
            const auto swapped = usePad (two, dup->pad, dup->otherPad);
            check (swapped[9] == N && swapped[13] == RD && RoleMap::from (explicitRoles (swapped)).laneOfSlot[pRide] == 13,
                   "Use pad 14: pad 10 gets the role None, pad 14 plays the ride");
            check (usePad (two, 9, 3) == two, "Use pad N: only between two pads with the same role");
            auto p = makeProcessor();
            check (p->setPadRoles (two) && p->setPadRoles (swapped) && p->padRoles().role == swapped, "Use pad N through the processor");
            // Pad 10's manual notes stay: roles never touch cells.
            auto withNotes = std::make_unique<StoredPattern>();
            p->patterns().copySlot (0, *withNotes);
            testutil::stepAt (*withNotes, 9, 0, 0).on = true;
            p->patterns().setSlot (0, *withNotes);
            p->patternHistory().undo (p->patterns());
            check (p->padRoles().role == two, "Use pad N is one undo step");
            p->patternHistory().redo (p->patterns());
            auto after = std::make_unique<StoredPattern>();
            p->patterns().copySlot (0, *after);
            check (p->padRoles().role == swapped && testutil::stepAt (*after, 9, 0, 0).on, "Use pad N: redo applies it again; pad 10 keeps its notes");
        }
    }
    // Generation to the ride pad: a user style with a ride part.
    {
        auto def = std::make_shared<style::Definition>();
        const auto ok = style::parse (R"({ "format": "vox-drums-style", "version": 1, "id": "7a1d2c3b-4e5f-4a6b-8c7d-9e0f1a2b3c4d", "key": "ride-test",
            "name": "Ride test", "meters": ["4/4"], "parts": { "required": ["kick", "ride"], "optional": ["tom3"] },
            "rules": [ { "part": "kick", "at": [1, 9] }, { "part": "ride", "at": [1, 3, 5, 7, 9, 11, 13, 15] }, { "part": "tom3", "at": [16] } ],
            "fill": { "cycle": ["tom3"], "ratchet": 2 } })", *def);
        check (ok.wasOk(), "a style with ride and tom 3 parts parses (" + ok.getErrorMessage() + ")");
        auto p = makeProcessor();
        auto g = p->generator();
        g.custom = def;
        g.fills = 0.0f;
        p->setGenerator (g);
        check (p->setPadRoles (metalMap), "metal roles set");
        p->generate (DrumsProcessor::Generate::pattern, 0xffffu, false);
        StoredPattern a;
        p->patterns().copySlot (0, a);
        const auto lane = [&] (int l) {
            int hits = 0;
            for (int bar = 0; bar < a.bars; ++bar)
                for (int slot = 0; slot < kit::slotStride; ++slot)
                    hits += a.cell (l, bar, slot).on ? 1 : 0;
            return hits;
        };
        check (lane (9) == 8 * a.bars && lane (7) == a.bars && lane (0) == 2 * a.bars,
               "generation: ride hits on the ride pad (10), tom 3 on pad 8, kick on pad 1");
        int others = 0;
        for (int l : { 1, 2, 3, 4, 5, 6, 8, 10, 11, 12, 13, 14, 15 })
            others += lane (l);
        check (others == 0, "generation: no other pad gets anything");
        auto noRide = metalMap;
        noRide[9] = P;
        check (p->setPadRoles (noRide), "roles without a ride");
        p->generate (DrumsProcessor::Generate::pattern, 0xffffu, false);
        p->patterns().copySlot (0, a);
        int total = 0;
        for (int l = 0; l < kit::padCount; ++l)
            total += l == 0 || l == 7 ? 0 : lane (l);
        check (total == 0 && lane (0) == 2 * a.bars, "generation: no ride pad: the ride part is not written anywhere (no substitute)");
    }
    // Proposal hints (§2.6): toms by GM note, then tune; cymbals by GM note.
    {
        auto k = vox::drums::defaultKit();
        const auto pad = [&] (int i, kit::Model m, int note, float tune) {
            auto& x = k.pads[static_cast<std::size_t> (i)];
            x.model = m;
            x.note = static_cast<std::uint8_t> (note);
            x.tune = tune;
        };
        pad (5, kit::Model::tom, 41, 0.0f);    // tom 3 by note
        pad (8, kit::Model::tom, 48, 0.0f);    // tom 1 by note
        pad (9, kit::Model::tom, 60, 5.0f);    // no GM tom note, lower: no tom role left -> asked
        pad (10, kit::Model::tom, 61, 9.0f);   // no GM tom note, higher: the free role (tom 2)
        pad (7, kit::Model::cymbal, 51, 0.0f); // ride by note
        pad (11, kit::Model::cymbal, 49, 0.0f);   // crash by note
        pad (12, kit::Model::cymbal, 57, 0.0f);   // a second crash: percussion
        pad (13, kit::Model::cymbal, 70, 0.0f);   // no GM cymbal note: asked (here percussion, a crash exists)
        const auto prop = proposeRolesFromModels (k);
        check (prop.role[5] == T3 && prop.role[8] == T && prop.role[10] == T2 && prop.role[9] == P && prop.needsChoice[9] && ! prop.needsChoice[10],
               "proposal: toms by GM note (41 tom 3, 48 tom 1), the rest by tune, highest first, into the free role; a fourth asked");
        check (prop.role[7] == RD && prop.role[11] == CY && prop.role[12] == P && prop.needsChoice[13],
               "proposal: cymbals by GM note (51 ride, 49 crash), a second crash is percussion, an unknown note asked");
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vox-drums-roles-" + juce::Uuid().toString());
    folder.createDirectory();
    testCodec();
    testIdentityParity();
    testMapping();
    testMutate();
    testState();
    testProcessor();
    testPresets (folder);
    testProposal();
    testRolesV2();
    if (folder.getFileName().startsWith ("vox-drums-roles-") && folder.isDirectory())
        folder.deleteRecursively();
    if (failures == 0)
        std::printf ("VOX Drums pad roles tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
