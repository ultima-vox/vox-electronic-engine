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
               T = PadRole::tom, R = PadRole::rim, CY = PadRole::cymbal, P = PadRole::percussion, N = PadRole::none;

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
constexpr std::array<std::uint64_t, styleCount> c7c29bbDigest = {
    0x658698542ea09551ull, 0x5366bdf7267d606eull, 0x354625583cd12f27ull, 0xb3e1f4f46bbb1050ull,
    0x932bca8aec35fc3full, 0x39d0ed13b56aa19aull, 0x04199d36b4e7e54bull,
};

void empty (StoredPattern& p, int steps, int bars) { digest::emptyPattern (p, steps, bars); }

// `mapped` holds, on lane map.laneOfSlot[s], exactly `identity`'s lane s; a
// lane without a slot is empty.
bool movedEqual (const StoredPattern& identity, const StoredPattern& mapped, const RoleMap& map)
{
    if (identity.steps != mapped.steps || identity.bars != mapped.bars || identity.seed != mapped.seed)
        return false;
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        const int slot = map.slotOfLane[static_cast<std::size_t> (lane)];
        for (int bar = 0; bar < mapped.bars; ++bar)
            for (int step = 0; step < mapped.steps; ++step)
            {
                const auto& got = mapped.cell (lane, bar, step);
                const kit::PatternStep want = slot >= 0 ? identity.cell (slot, bar, step) : kit::PatternStep {};
                if (got.on != want.on || got.accent != want.accent || got.velocity != want.velocity || got.probability != want.probability
                    || got.ratchet != want.ratchet || got.micro != want.micro || got.locked != want.locked)
                    return false;
            }
    }
    return true;
}

bool laneEqual (const StoredPattern& a, const StoredPattern& b, int laneA, int laneB)
{
    for (int bar = 0; bar < a.bars; ++bar)
        for (int step = 0; step < a.steps; ++step)
        {
            const auto& x = a.cell (laneA, bar, step);
            const auto& y = b.cell (laneB, bar, step);
            if (x.on != y.on || x.accent != y.accent || x.velocity != y.velocity || x.probability != y.probability
                || x.ratchet != y.ratchet || x.micro != y.micro || x.locked != y.locked)
                return false;
        }
    return true;
}

bool laneEmpty (const StoredPattern& p, int lane)
{
    for (int bar = 0; bar < p.bars; ++bar)
        for (int step = 0; step < p.steps; ++step)
            if (p.cell (lane, bar, step).on)
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
    s = good; s.data[0] = std::byte { 2 }; rejects (s, "version 2");
    s = good; s.data[3] = std::byte { 1 }; rejects (s, "a version above 2^24");
    s = good; s.data[4] = std::byte { 2 }; rejects (s, "explicit = 2");
    s = good; s.data[5] = std::byte { 10 }; rejects (s, "role 10");
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
                    four = four && (step % 4 == 0 ? p->cell (1, bar, step).on : (! p->cell (1, bar, step).on || (style == 2 && step == 15)));
            check (four, juce::String ("R5: ") + styleName (static_cast<Style> (style)) + " four-on-the-floor on pad 2");
        }
        empty (*p, 16, 4);
        generatePattern (settings (5, 3, 0.8f, 0.0f), *p, map);   // Breaks: snare always on 5 and 13
        bool back = true;
        for (int bar = 0; bar < 4; ++bar)
            back = back && p->cell (0, bar, 4).on && p->cell (0, bar, 12).on;
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
                offbeat = offbeat && (step % 4 != 2 || (p->cell (11, bar, step).on && ! p->cell (8, bar, step).on));
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
        check (roleNotices (explicitRoles (r)).contains ("No closed hat pad: closed hat parts are not generated"), "R7: missing-role notice");
    }
    // R8 / R10: a second kick on pad 9; percussion moves up one pad.
    {
        Roles r = PadRoles::identity();
        r[8] = K;
        const auto map = RoleMap::from (explicitRoles (r));
        check (map.laneOfSlot[0] == 0 && map.slotOfLane[8] < 0 && map.laneOfSlot[8] == 9 && map.laneOfSlot[15] < 0 && map.slotOfLane[15] == 14,
               "R8: the first kick pad is used; pad 9 gets nothing; percussion moves up");
        check (roleNotices (explicitRoles (r)).contains ("2 kick pads: pad 1 used"), "R8: duplicate notice");
        // R10: pre-filled pattern; lane 9 (duplicate) cleared except locked cells.
        bool ok = true;
        for (int style = 0; style < styleCount; ++style)
        {
            empty (*p, 16, 4);
            for (int lane = 0; lane < kit::padCount; ++lane)
                for (int step = 0; step < 16; ++step)
                    p->cell (lane, 0, step) = { true, false, 99, 100, 1, 0, false };
            p->cell (8, 0, 5).locked = true;
            auto g = settings (style, 6);
            generatePattern (g, *p, map);
            ok = ok && p->cell (8, 0, 5).on && p->cell (8, 0, 5).velocity == 99;
            for (int step = 0; step < 16; ++step)
                ok = ok && (step == 5 || ! p->cell (8, 0, step).on);
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
        p->cell (6, 1, 4) = { true, false, 90, 100, 1, 0, false };   // user hit on the none lane of A
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
            p->cell (0, 2, 7).locked = true;
            p->cell (12, 3, 15).locked = true;
            auto before = std::make_unique<StoredPattern> (*p);
            g.laneLocks = 1u << 1;   // pad 2, where the kick now is
            generatePattern (g, *p, map);
            ok = ok && laneEqual (*before, *p, 1, 1) && p->cell (0, 2, 7).on == before->cell (0, 2, 7).on
                 && p->cell (0, 2, 7).velocity == before->cell (0, 2, 7).velocity && p->cell (12, 3, 15).locked
                 && p->cell (12, 3, 15).on == before->cell (12, 3, 15).on;
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
                    base->cell (0, 0, 4).locked = true;
                    base->cell (9, 3, 10).locked = true;
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
                                const auto& got = m->cell (lane, bar, step);
                                const auto& was = base->cell (lane, bar, step);
                                const bool selected = (mask & (1u << lane)) != 0;
                                const auto& want = ! selected || was.locked ? was : s->cell (lane, bar, step);
                                ok = ok && got.on == want.on && got.velocity == want.velocity && got.micro == want.micro
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
        variant ([] (preset::Section& x) { x.data[12] = std::byte { 10 }; }, "role value");
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
        check (q->padRoles().role[5] == PadRole::tom, "undo restores the previous role");
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
            for (int step = 0; step < moved->steps; ++step)
                moved->cell (lane, bar, step) = a->cell (slot, bar, step);
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
    custom[2] = PadRole::tom;

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
            othersNone = pad == 2 ? stored.role[2] == PadRole::tom : stored.role[static_cast<std::size_t> (pad)] == PadRole::none;
        check (othersNone, "R16c: a pad preset stores only its own pad's role");
    }
    auto t = makeProcessor();
    t->setPadRoles (PadRoles::identity());
    const auto before = t->padRoles();
    check (t->presets().apply (modernPad.getData(), modernPad.getSize(), 0).wasOk() && t->padRoles() == before,
           "R16c: loading keeps the target's role");
    const auto offer = t->presets().roleOffer();
    check (offer && offer->pad == 0 && offer->role == PadRole::tom, "R16c: the preset role is offered for the target pad");
    check (t->presets().applyRoleOffer(), "R16c: Apply");
    auto after = before.role;
    after[0] = PadRole::tom;
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
                                                                  PadRole::tom, PadRole::rim, PadRole::cymbal, PadRole::percussion };
            consistent = consistent && (model == kit::Model::hat ? hat : role == byModel[static_cast<int> (model)]);
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
    if (folder.getFileName().startsWith ("vox-drums-roles-") && folder.isDirectory())
        folder.deleteRecursively();
    if (failures == 0)
        std::printf ("VOX Drums pad roles tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
