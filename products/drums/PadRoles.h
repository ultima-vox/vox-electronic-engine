#pragma once

// VOX Drums semantic pad roles (issue 128, option B; design:
// docs/ultima-vox-music/drums/PAD_ROLES_DESIGN.md). A role says which
// instrument a pad plays for the generator: the generator writes each of its
// style roles to the pad that has that role. Saved as the state section DROL
// only when the user set roles; without DROL the roles are the identity of
// the default layout and generation is exactly as before.
//
// Message thread only (state, presets, generation); never the audio thread.

#include <juce_core/juce_core.h>

#include "StateContainer.h"
#include "engine/KitEngine.h"

#include <array>
#include <cstdint>

namespace vox::drums {

inline constexpr preset::Tag rolesSectionTag = preset::makeTag ("DROL");

enum class PadRole : std::uint8_t { none, kick, snare, clap, closedHat, openHat, tom, rim, cymbal, percussion };
inline constexpr int padRoleCount = 10;
const char* padRoleName (PadRole) noexcept;   // "Kick", "Closed hat", ...

struct PadRoles {
    bool explicitRoles = false;   // set by the user; only then saved
    std::array<PadRole, kit::padCount> role = identity();
    // Pads 1-8: kick, snare, clap, closed hat, open hat, tom, rim, cymbal; 9-16: percussion.
    static std::array<PadRole, kit::padCount> identity() noexcept;
    bool operator== (const PadRoles& o) const noexcept { return explicitRoles == o.explicitRoles && role == o.role; }
    bool operator!= (const PadRoles& o) const noexcept { return ! (*this == o); }
};

// DROL: u32 version (1), u8 explicit (0/1), 16 x u8 role (0..9). 21 bytes.
preset::Section encodeRoles (const PadRoles&);
// Strict: wrong size, version or value is rejected (error set, false returned).
bool decodeRoles (const preset::Section&, PadRoles& out, juce::String& error);

// Generator role slots: 0..7 kick, snare, clap, closed hat, open hat, tom,
// rim, cymbal; 8..15 percussion #1..#8. The identity map is slot n = lane n.
struct RoleMap {
    std::array<std::int8_t, kit::padCount> slotOfLane {};   // -1: the generator writes nothing for this lane
    std::array<std::int8_t, kit::padCount> laneOfSlot {};   // -1: no pad has this role
    static RoleMap identity() noexcept;
    // The first pad (in pad order) with a role takes it; a second pad with the
    // same core role gets nothing; percussion pads take percussion slots in
    // pad order (a ninth percussion pad gets nothing).
    static RoleMap from (const PadRoles&) noexcept;
    bool isIdentity() const noexcept;
};

// Generator panel notices: missing roles a style needs, duplicates.
juce::StringArray roleNotices (const PadRoles&);

// "Set roles from pad models...": a proposal, never applied by itself.
// needsChoice marks rows the user must decide (hat ties, or a hat whose
// sound comes from a sample).
struct RoleProposal {
    std::array<PadRole, kit::padCount> role {};
    std::array<bool, kit::padCount> needsChoice {};
    // The user's choice for a row (clears its "?").
    void choose (int pad, PadRole r) noexcept;
    // Apply is possible only when no row is still "?".
    bool complete() const noexcept;
};
// Hats are told apart by note (42/44 closed, 46 open), then by decay.
RoleProposal proposeRolesFromModels (const kit::KitSettings&) noexcept;

} // namespace vox::drums
