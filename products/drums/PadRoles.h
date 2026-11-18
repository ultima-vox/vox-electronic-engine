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
#include "StyleDefinition.h"
#include "engine/KitEngine.h"

#include <array>
#include <cstdint>
#include <vector>

namespace vox::drums {

inline constexpr preset::Tag rolesSectionTag = preset::makeTag ("DROL");

// PR 137 §2.2 (roles v2). The role is pad metadata, separate from the pad's
// MIDI note: changing one never changes the other.
enum class PadRole : std::uint8_t { none, kick, snare, clap, closedHat, openHat, tom1, rim, crash, percussion, ride, tom2, tom3 };
inline constexpr int padRoleCount = 13;
const char* padRoleName (PadRole) noexcept;   // "Kick", "Closed hat", "Tom 1", ...

struct PadRoles {
    bool explicitRoles = false;   // set by the user; only then saved
    std::array<PadRole, kit::padCount> role = identity();
    // The default/electronic map: pads 1-8 kick, snare, clap, closed hat,
    // open hat, tom 1, rim, crash; 9-16 percussion.
    static std::array<PadRole, kit::padCount> identity() noexcept;
    bool operator== (const PadRoles& o) const noexcept { return explicitRoles == o.explicitRoles && role == o.role; }
    bool operator!= (const PadRoles& o) const noexcept { return ! (*this == o); }
};

// DROL v2: u32 version (2), u8 explicit (0/1), 16 x u8 role (0..12). 21
// bytes. No v1 decoder (pre-release policy, #136).
preset::Section encodeRoles (const PadRoles&);
// Strict: wrong size, version, flag or value is rejected (error set, false returned).
bool decodeRoles (const preset::Section&, PadRoles& out, juce::String& error);

// The style part a role plays (style::partCount parts): kick..crash are
// parts 0..7, ride 16, tom 2 17, tom 3 18; percussion pads take the
// percussion parts 8..15 in pad order. -1 for none.
int partOfRole (PadRole) noexcept;

// Generator parts -> pads. The identity map is part n = lane n for the 16
// parts of the default layout (ride, tom 2 and tom 3 have no pad there).
struct RoleMap {
    static constexpr int parts = 19;   // style::partCount
    std::array<std::int8_t, kit::padCount> slotOfLane {};   // the part lane n plays; -1: the generator writes nothing for it
    std::array<std::int8_t, parts> laneOfSlot {};           // the pad of part n; -1: no pad has this role
    static RoleMap identity() noexcept;
    // The lowest pad with a role takes it; a second pad with the same
    // instrument role gets nothing; percussion pads take percussion parts in
    // pad order (a ninth percussion pad gets nothing).
    static RoleMap from (const PadRoles&) noexcept;
    bool isIdentity() const noexcept;
};

// PR 137 §2.4/§2.5: Generator panel notices for the selected style (null:
// every instrument role). Style-aware: only parts the style uses are
// reported. Message thread.
struct RoleNotice {
    enum class Kind : std::uint8_t {
        missingRequired,   // warning; action: Assign... (opens the role selector for `role`)
        missingOptional,   // quiet note listing the unused optional parts
        duplicate,         // `pad` plays the part, `otherPad` gets nothing; action: Use pad `otherPad` (usePad)
        extraPercussion    // more percussion pads than the style's percussion parts
    } kind = Kind::missingRequired;
    juce::String text;
    // The action's tooltip (duplicate: what "Use pad N" does, PR 137 §2.5 as approved).
    juce::String actionTip;
    PadRole role = PadRole::none;
    int pad = -1, otherPad = -1;
};
std::vector<RoleNotice> roleNotices (const PadRoles&, const style::Definition* styleDefinition, const juce::String& styleName);
// The texts only (tests, simple panels); without a style every instrument role counts.
juce::StringArray roleNotices (const PadRoles&);
// "Use pad N" for a duplicate role (PR 137 §2.5): both pads have the same
// role, so a literal swap would change nothing. The pad that plays the
// part now (`playing`, the lower one) gets the role None and keeps its
// manual notes; pad `chosen` then plays it. Applied as one undoable step
// (the processor's setPadRoles).
std::array<PadRole, kit::padCount> usePad (const std::array<PadRole, kit::padCount>& roles, int playing, int chosen) noexcept;

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
    // Bit n: the proposal changes pad n's role against `current` (shown before
    // Apply/Cancel; a proposal never changes a kit by itself).
    std::uint16_t changedPads (const PadRoles& current) const noexcept;
};
// Hats are told apart by note (42/44 closed, 46 open), then by decay.
// Toms by GM note (48/50 tom 1, 45/47 tom 2, 41/43 tom 3), otherwise by tune
// (highest = tom 1); cymbals by GM note (49/52/55/57 crash, 51/53/59 ride),
// otherwise "?"; a second crash candidate becomes percussion (PR 137 §2.6).
RoleProposal proposeRolesFromModels (const kit::KitSettings&) noexcept;

} // namespace vox::drums
