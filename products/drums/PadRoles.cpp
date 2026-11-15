#include "PadRoles.h"

#include <algorithm>

namespace vox::drums {

const char* padRoleName (PadRole role) noexcept
{
    static const char* names[padRoleCount] = { "None", "Kick",      "Snare", "Clap", "Closed hat", "Open hat", "Tom 1",
                                               "Rim",  "Crash",     "Percussion", "Ride", "Tom 2", "Tom 3" };
    const auto i = static_cast<int> (role);
    return i >= 0 && i < padRoleCount ? names[i] : "?";
}

std::array<PadRole, kit::padCount> PadRoles::identity() noexcept
{
    std::array<PadRole, kit::padCount> r {};
    for (int lane = 0; lane < kit::padCount; ++lane)
        r[static_cast<std::size_t> (lane)] = lane < 8 ? static_cast<PadRole> (lane + 1) : PadRole::percussion;
    return r;
}

preset::Section encodeRoles (const PadRoles& roles)
{
    preset::Section s;
    s.tag = rolesSectionTag;
    for (int shift = 0; shift < 32; shift += 8)
        s.data.push_back (static_cast<std::byte> ((2u >> shift) & 0xffu));
    s.data.push_back (static_cast<std::byte> (roles.explicitRoles ? 1 : 0));
    for (auto r : roles.role)
        s.data.push_back (static_cast<std::byte> (r));
    return s;
}

bool decodeRoles (const preset::Section& section, PadRoles& out, juce::String& error)
{
    const auto& d = section.data;
    if (d.size() != 4 + 1 + kit::padCount)
    {
        error = "unsupported pad role section";
        return false;
    }
    std::uint32_t version = 0;
    for (std::size_t i = 0; i < 4; ++i)
        version |= std::to_integer<std::uint32_t> (d[i]) << (8 * i);
    const auto flag = std::to_integer<std::uint8_t> (d[4]);
    if (version != 2 || flag > 1)   // DROL v1 was a development build: not decoded
    {
        error = "unsupported pad role section";
        return false;
    }
    PadRoles roles;
    roles.explicitRoles = flag != 0;
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        const auto v = std::to_integer<std::uint8_t> (d[static_cast<std::size_t> (5 + lane)]);
        if (v >= padRoleCount)
        {
            error = "invalid pad role section";
            return false;
        }
        roles.role[static_cast<std::size_t> (lane)] = static_cast<PadRole> (v);
    }
    out = roles;
    return true;
}

int partOfRole (PadRole r) noexcept
{
    switch (r)
    {
        case PadRole::none:
        case PadRole::percussion: return -1;   // percussion: by pad order (RoleMap::from)
        case PadRole::ride: return 16;
        case PadRole::tom2: return 17;
        case PadRole::tom3: return 18;
        default: return static_cast<int> (r) - 1;   // kick .. crash: parts 0..7
    }
}

RoleMap RoleMap::identity() noexcept
{
    RoleMap m;
    m.laneOfSlot.fill (-1);
    for (int i = 0; i < kit::padCount; ++i)
    {
        m.slotOfLane[static_cast<std::size_t> (i)] = static_cast<std::int8_t> (i);
        m.laneOfSlot[static_cast<std::size_t> (i)] = static_cast<std::int8_t> (i);
    }
    return m;
}

RoleMap RoleMap::from (const PadRoles& roles) noexcept
{
    RoleMap m;
    m.slotOfLane.fill (-1);
    m.laneOfSlot.fill (-1);
    int nextPercussion = style::firstPercussionPart;
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        const auto r = roles.role[static_cast<std::size_t> (lane)];
        int slot = partOfRole (r);
        if (r == PadRole::percussion)
            slot = nextPercussion < style::firstPercussionPart + style::percussionParts ? nextPercussion++ : -1;
        if (slot >= 0 && m.laneOfSlot[static_cast<std::size_t> (slot)] < 0)
        {
            m.laneOfSlot[static_cast<std::size_t> (slot)] = static_cast<std::int8_t> (lane);
            m.slotOfLane[static_cast<std::size_t> (lane)] = static_cast<std::int8_t> (slot);
        }
    }
    return m;
}

bool RoleMap::isIdentity() const noexcept
{
    const auto id = identity();
    return slotOfLane == id.slotOfLane && laneOfSlot == id.laneOfSlot;
}

namespace {
// The instrument role of a non-percussion part.
PadRole roleOfPart (int part) noexcept
{
    if (part >= 0 && part < style::firstPercussionPart)
        return static_cast<PadRole> (part + 1);
    if (part == 16) return PadRole::ride;
    if (part == 17) return PadRole::tom2;
    if (part == 18) return PadRole::tom3;
    return PadRole::percussion;
}

juce::String lower (PadRole r)
{
    return juce::String (padRoleName (r)).toLowerCase();
}

juce::String article (PadRole r)
{
    const auto n = lower (r);
    return (n.startsWithChar ('o') ? "an " : "a ") + n;
}
} // namespace

std::vector<RoleNotice> roleNotices (const PadRoles& roles, const style::Definition* def, const juce::String& styleName)
{
    std::vector<RoleNotice> out;
    std::array<int, padRoleCount> count {}, first {}, second {};
    first.fill (-1);
    second.fill (-1);
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        const auto r = static_cast<std::size_t> (roles.role[static_cast<std::size_t> (lane)]);
        if (count[r]++ == 0)
            first[r] = lane;
        else if (second[r] < 0)
            second[r] = lane;
    }
    const auto has = [&] (PadRole r) { return count[static_cast<std::size_t> (r)] > 0; };
    // The parts that count: the style's, or every instrument part.
    std::uint32_t used = 0;
    std::vector<std::uint32_t> required;
    if (def != nullptr)
    {
        used = def->declaredParts();
        required = def->requiredParts;
    }
    else
        for (int part : { 0, 1, 2, 3, 4, 5, 6, 7 })
        {
            used |= 1u << part;
            required.push_back (1u << part);
        }
    const auto name = styleName.isNotEmpty() ? styleName : juce::String ("This style");
    // Missing required parts (no pad has any role of the group).
    std::uint32_t reported = 0;
    for (auto group : required)
    {
        bool present = false;
        juce::StringArray alternatives;
        PadRole firstRole = PadRole::none;
        for (int part = 0; part < style::partCount; ++part)
            if ((group >> part) & 1u)
            {
                const auto r = roleOfPart (part);
                if (firstRole == PadRole::none)
                    firstRole = r;
                if (r == PadRole::percussion)
                {
                    // perc n needs n percussion pads
                    present = present || count[static_cast<std::size_t> (PadRole::percussion)] > part - style::firstPercussionPart;
                    alternatives.addIfNotAlreadyThere ("percussion");
                }
                else
                {
                    present = present || has (r);
                    alternatives.add (article (r));
                }
            }
        reported |= group;
        if (present)
            continue;
        RoleNotice n;
        n.kind = RoleNotice::Kind::missingRequired;
        n.role = firstRole;
        n.text = name + " needs " + alternatives.joinIntoString (" or ") + ": no pad has the "
               + (alternatives.size() == 1 ? juce::String (padRoleName (firstRole)) + " role" : juce::String ("roles"));
        out.push_back (n);
    }
    // Missing optional parts: one quiet note.
    {
        juce::StringArray unused;
        int percussionNeeded = 0;
        for (int part = 0; part < style::partCount; ++part)
        {
            if (((used & ~reported) >> part & 1u) == 0)
                continue;
            const auto r = roleOfPart (part);
            if (r == PadRole::percussion)
            {
                const int n = part - style::firstPercussionPart + 1;
                percussionNeeded = std::max (percussionNeeded, n);
                if (count[static_cast<std::size_t> (PadRole::percussion)] < n)
                    unused.add ("perc " + juce::String (n));
            }
            else if (! has (r))
                unused.add (lower (r));
        }
        if (! unused.isEmpty())
        {
            RoleNotice n;
            n.kind = RoleNotice::Kind::missingOptional;
            n.text = "Not used: " + unused.joinIntoString (", ");
            out.push_back (n);
        }
    }
    // Duplicate instrument roles the style uses: the lowest pad plays them.
    for (int r = static_cast<int> (PadRole::kick); r < padRoleCount; ++r)
    {
        const auto role = static_cast<PadRole> (r);
        if (role == PadRole::percussion || count[static_cast<std::size_t> (r)] < 2 || ((used >> partOfRole (role)) & 1u) == 0)
            continue;
        RoleNotice n;
        n.kind = RoleNotice::Kind::duplicate;
        n.role = role;
        n.pad = first[static_cast<std::size_t> (r)];
        n.otherPad = second[static_cast<std::size_t> (r)];
        n.text = juce::String (count[static_cast<std::size_t> (r)]) + " " + lower (role) + " pads: pad " + juce::String (n.pad + 1) + " plays generated "
               + lower (role) + " hits";
        out.push_back (n);
    }
    // More percussion pads than percussion parts.
    int percussionParts = 0;
    for (int part = style::firstPercussionPart; part < style::firstPercussionPart + style::percussionParts; ++part)
        percussionParts += ((used >> part) & 1u) != 0 ? 1 : 0;
    if (def == nullptr)
        percussionParts = style::percussionParts;
    const int percussionPads = count[static_cast<std::size_t> (PadRole::percussion)];
    if (percussionPads > percussionParts && (percussionParts > 0 || def == nullptr))
    {
        RoleNotice n;
        n.kind = RoleNotice::Kind::extraPercussion;
        n.role = PadRole::percussion;
        n.text = juce::String (percussionPads) + " percussion pads: the first " + juce::String (percussionParts) + " are used";
        out.push_back (n);
    }
    return out;
}

juce::StringArray roleNotices (const PadRoles& roles)
{
    juce::StringArray out;
    for (const auto& n : roleNotices (roles, nullptr, {}))
        out.add (n.text);
    return out;
}

std::array<PadRole, kit::padCount> usePad (const std::array<PadRole, kit::padCount>& roles, int playing, int chosen) noexcept
{
    auto out = roles;
    if (playing >= 0 && playing < kit::padCount && chosen >= 0 && chosen < kit::padCount && playing != chosen
        && roles[static_cast<std::size_t> (playing)] == roles[static_cast<std::size_t> (chosen)])
        out[static_cast<std::size_t> (playing)] = PadRole::none;
    return out;
}

void RoleProposal::choose (int pad, PadRole r) noexcept
{
    if (pad < 0 || pad >= kit::padCount)
        return;
    role[static_cast<std::size_t> (pad)] = r;
    needsChoice[static_cast<std::size_t> (pad)] = false;
}

bool RoleProposal::complete() const noexcept
{
    for (auto n : needsChoice)
        if (n)
            return false;
    return true;
}

RoleProposal proposeRolesFromModels (const kit::KitSettings& kit) noexcept
{
    RoleProposal p;
    std::array<int, kit::padCount> unresolved {};
    int unresolvedCount = 0;
    bool closedByNote = false, openByNote = false;
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        const auto& pad = kit.pads[static_cast<std::size_t> (lane)];
        auto& role = p.role[static_cast<std::size_t> (lane)];
        switch (pad.model)
        {
            case kit::Model::kick: role = PadRole::kick; break;
            case kit::Model::snare: role = PadRole::snare; break;
            case kit::Model::clap: role = PadRole::clap; break;
            case kit::Model::tom: role = PadRole::tom1; break;      // decided below
            case kit::Model::rim: role = PadRole::rim; break;
            case kit::Model::cymbal: role = PadRole::crash; break;  // decided below
            case kit::Model::perc: role = PadRole::percussion; break;
            case kit::Model::hat:
                if (pad.note == 42 || pad.note == 44)
                {
                    role = PadRole::closedHat;
                    closedByNote = true;
                }
                else if (pad.note == 46)
                {
                    role = PadRole::openHat;
                    openByNote = true;
                }
                else
                {
                    role = PadRole::closedHat;
                    unresolved[static_cast<std::size_t> (unresolvedCount++)] = lane;
                }
                // A hat that plays a sample may not sound like its model: ask.
                if (pad.source != kit::Source::synth)
                    p.needsChoice[static_cast<std::size_t> (lane)] = true;
                break;
        }
    }
    if (unresolvedCount == 2)
    {
        const int a = unresolved[0], b = unresolved[1];
        const float da = kit.pads[static_cast<std::size_t> (a)].decay, db = kit.pads[static_cast<std::size_t> (b)].decay;
        if (juce::exactlyEqual (da, db))
            p.needsChoice[static_cast<std::size_t> (a)] = p.needsChoice[static_cast<std::size_t> (b)] = true;
        else
        {
            p.role[static_cast<std::size_t> (da < db ? a : b)] = PadRole::closedHat;
            p.role[static_cast<std::size_t> (da < db ? b : a)] = PadRole::openHat;
        }
    }
    else if (unresolvedCount == 1)
    {
        const int a = unresolved[0];
        if (closedByNote && ! openByNote)
            p.role[static_cast<std::size_t> (a)] = PadRole::openHat;
        else if (! (openByNote && ! closedByNote))
            p.needsChoice[static_cast<std::size_t> (a)] = true;   // both or neither decided by note: ask
    }
    else
    {
        for (int i = 0; i < unresolvedCount; ++i)
            p.needsChoice[static_cast<std::size_t> (unresolved[static_cast<std::size_t> (i)])] = true;
    }
    // Toms (PR 137 §2.6): by GM note, then the rest by tune, highest first,
    // into the tom roles still free; more than three toms: "?".
    {
        std::array<bool, 3> taken {};
        std::vector<int> byTune;
        for (int lane = 0; lane < kit::padCount; ++lane)
        {
            const auto& pad = kit.pads[static_cast<std::size_t> (lane)];
            if (pad.model != kit::Model::tom)
                continue;
            const int n = pad.note;
            const int t = n == 48 || n == 50 ? 0 : n == 45 || n == 47 ? 1 : n == 41 || n == 43 ? 2 : -1;
            if (t >= 0 && ! taken[static_cast<std::size_t> (t)])
            {
                taken[static_cast<std::size_t> (t)] = true;
                p.role[static_cast<std::size_t> (lane)] = t == 0 ? PadRole::tom1 : t == 1 ? PadRole::tom2 : PadRole::tom3;
            }
            else
                byTune.push_back (lane);
        }
        std::stable_sort (byTune.begin(), byTune.end(), [&] (int a, int b) {
            return kit.pads[static_cast<std::size_t> (a)].tune > kit.pads[static_cast<std::size_t> (b)].tune;
        });
        for (int lane : byTune)
        {
            int t = 0;
            while (t < 3 && taken[static_cast<std::size_t> (t)])
                ++t;
            if (t == 3)
            {
                p.role[static_cast<std::size_t> (lane)] = PadRole::percussion;
                p.needsChoice[static_cast<std::size_t> (lane)] = true;
                continue;
            }
            taken[static_cast<std::size_t> (t)] = true;
            p.role[static_cast<std::size_t> (lane)] = t == 0 ? PadRole::tom1 : t == 1 ? PadRole::tom2 : PadRole::tom3;
        }
    }
    // Cymbals: crash or ride by GM note, otherwise "?"; a second crash
    // candidate becomes percussion.
    {
        bool crash = false, ride = false;
        for (int lane = 0; lane < kit::padCount; ++lane)
        {
            const auto& pad = kit.pads[static_cast<std::size_t> (lane)];
            if (pad.model != kit::Model::cymbal)
                continue;
            const int n = pad.note;
            auto& role = p.role[static_cast<std::size_t> (lane)];
            if (n == 51 || n == 53 || n == 59)
            {
                role = ride ? PadRole::percussion : PadRole::ride;
                ride = true;
            }
            else if (n == 49 || n == 52 || n == 55 || n == 57)
            {
                role = crash ? PadRole::percussion : PadRole::crash;
                crash = true;
            }
            else
            {
                role = crash ? PadRole::percussion : PadRole::crash;
                p.needsChoice[static_cast<std::size_t> (lane)] = true;
            }
        }
    }
    return p;
}

} // namespace vox::drums
