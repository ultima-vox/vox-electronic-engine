#include "PadRoles.h"

namespace vox::drums {

const char* padRoleName (PadRole role) noexcept
{
    static const char* names[padRoleCount] = { "None", "Kick", "Snare", "Clap", "Closed hat", "Open hat", "Tom", "Rim", "Cymbal", "Percussion" };
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
        s.data.push_back (static_cast<std::byte> ((1u >> shift) & 0xffu));
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
    if (version != 1 || flag > 1)
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

RoleMap RoleMap::identity() noexcept
{
    RoleMap m;
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
    int nextPercussion = 8;
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        const auto r = roles.role[static_cast<std::size_t> (lane)];
        int slot = -1;
        if (r == PadRole::percussion)
            slot = nextPercussion < kit::padCount ? nextPercussion++ : -1;
        else if (r != PadRole::none)
            slot = static_cast<int> (r) - 1;
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

juce::StringArray roleNotices (const PadRoles& roles)
{
    juce::StringArray out;
    std::array<int, padRoleCount> count {}, first {};
    first.fill (-1);
    for (int lane = 0; lane < kit::padCount; ++lane)
    {
        const auto r = static_cast<std::size_t> (roles.role[static_cast<std::size_t> (lane)]);
        if (count[r]++ == 0)
            first[r] = lane;
    }
    for (int r = static_cast<int> (PadRole::kick); r <= static_cast<int> (PadRole::cymbal); ++r)
    {
        const auto name = juce::String (padRoleName (static_cast<PadRole> (r))).toLowerCase();
        if (count[static_cast<std::size_t> (r)] == 0)
            out.add ("No " + name + " pad: " + name + " parts are not generated");
        else if (count[static_cast<std::size_t> (r)] > 1)
            out.add (juce::String (count[static_cast<std::size_t> (r)]) + " " + name + " pads: pad " + juce::String (first[static_cast<std::size_t> (r)] + 1) + " used");
    }
    const auto perc = count[static_cast<std::size_t> (PadRole::percussion)];
    if (perc > 8)
        out.add (juce::String (perc) + " percussion pads: the first 8 are used");
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
            case kit::Model::tom: role = PadRole::tom; break;
            case kit::Model::rim: role = PadRole::rim; break;
            case kit::Model::cymbal: role = PadRole::cymbal; break;
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
    return p;
}

} // namespace vox::drums
