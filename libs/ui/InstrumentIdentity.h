#pragma once

#include <juce_graphics/juce_graphics.h>

#include <string_view>

#include "ui/common/UiComponents.h"

namespace vstengine::ui {

// ===========================================================================
// UI-SIDE IDENTITY METADATA  **  OWNER DECISION REQUIRED  **
// ===========================================================================
//
// The accepted SOUND renders show a per-instrument "engine / character" name and
// version in two places:
//
//   * the rack rail descriptor line, e.g. slot 2 `Analog Drive 1.0`;
//   * the instrument-header meta line, e.g. `Ultima Vox 1.0`.
//
// Nothing in the data model carries that name. All four built-in descriptors
// publish `vendor = "Ultima Vox"`, and their `instrumentVersion` is a single
// integer, so there is no field that can render `Analog Drive 1.0`,
// `Supra Lead 1.0` or `Ethereal 1.0`. `InstrumentContract` is a frozen contract,
// so this wave does NOT add a descriptor field and does NOT change any
// descriptor value.
//
// Instead the mapping lives here, in ONE table, on the UI side, and is clearly
// disposable: if the owner decides the engine/character name belongs in the
// descriptor (preferred) or in the content/preset metadata, this table is
// deleted and the two call sites (rack card descriptor, instrument header meta
// line) read the new field instead.
//
// WHAT IS AND IS NOT INVENTED HERE
// --------------------------------
// * `engineName` is transcribed from the accepted render's rack rail.
// * `engineVersion` is the descriptor's own `instrumentVersion` formatted as
//   `<major>.<minor>`, which is the render's own convention (`1.0`), not a new
//   version scheme.
// * `accent` is a UI presentation accent sampled from the accepted render's rack
//   thumbnails. It is NOT a DSP or semantic domain colour; it only tints this
//   slot's identity affordances (rail card edge, thumbnail, hero title). The
//   global interaction accent stays `colours::primary` everywhere.
struct InstrumentIdentity {
    std::string_view instrumentId {};
    juce::String engineName;      // accepted-render character/engine name
    juce::String engineVersion;   // "<major>.<minor>"
    juce::Colour accent { colours::primary };

    [[nodiscard]] bool matches (std::string_view id) const noexcept
    {
        return ! instrumentId.empty() && instrumentId == id;
    }

    // The rack descriptor line: `<engine> <major.minor>`, e.g. `Analog Drive 1.0`.
    [[nodiscard]] juce::String descriptorLine() const
    {
        return engineVersion.isEmpty() ? engineName
                                       : engineName + " " + engineVersion;
    }
};

// Formats a descriptor's integer instrument version as `<major>.<minor>`.
//
// The accepted render only ever shows `1.0`, and the descriptor carries a single
// integer, so the minor component is always 0 today. Keeping the formatter here
// (rather than hardcoding "1.0" per instrument) means a descriptor that starts
// publishing a real minor version renders correctly with no table edit.
[[nodiscard]] inline juce::String formatEngineVersion (int instrumentVersion)
{
    return juce::String (instrumentVersion) + ".0";
}

// The single source of the UI-side identity mapping. Order is the accepted
// render's rack order and is not meaningful beyond determinism.
[[nodiscard]] inline const InstrumentIdentity* instrumentIdentities()
{
    // Function-local static: one construction, no dynamic initialisation order
    // hazard, and no allocation on the paint path.
    static const InstrumentIdentity table[] {
        { "com.ultimavox.psy-bass",      "Ultima Vox",   "1.0",
          juce::Colour::fromRGB (0x36, 0xD6, 0xEE) },
        { "com.ultimavox.acid",          "Analog Drive", "1.0",
          juce::Colour::fromRGB (0x45, 0xD9, 0x6E) },
        { "com.ultimavox.lead",          "Supra Lead",   "1.0",
          juce::Colour::fromRGB (0xA9, 0x6B, 0xF0) },
        { "com.ultimavox.atmos-texture", "Ethereal",     "1.0",
          juce::Colour::fromRGB (0x9F, 0xC4, 0xDC) },
    };

    return table;
}

inline constexpr int instrumentIdentityCount = 4;

// Returns the identity for `instrumentId`, or nullptr when the instrument has no
// table entry (any third-party or future provider).
[[nodiscard]] inline const InstrumentIdentity* identityFor (std::string_view instrumentId)
{
    const auto* table = instrumentIdentities();

    for (int i = 0; i < instrumentIdentityCount; ++i)
        if (table[i].matches (instrumentId))
            return &table[i];

    return nullptr;
}

// Identity accent for a slot. Falls back to the shared interaction accent, so an
// unmapped instrument still renders in the canonical palette rather than in a
// colour nobody chose.
[[nodiscard]] inline juce::Colour identityAccentFor (std::string_view instrumentId)
{
    if (const auto* identity = identityFor (instrumentId))
        return identity->accent;

    return colours::primary;
}

// Rack descriptor line for a slot: `<engine> <major.minor>` when the instrument
// is mapped, otherwise the descriptor's own vendor + formatted version (which is
// what the previous build rendered for every slot). `vendor` and `version` come
// from the caller's descriptor so this function never reads engine state.
[[nodiscard]] inline juce::String rackDescriptorLine (std::string_view instrumentId,
                                                     const juce::String& vendor,
                                                     int instrumentVersion)
{
    if (const auto* identity = identityFor (instrumentId))
        return identity->descriptorLine();

    return vendor.isEmpty() ? formatEngineVersion (instrumentVersion)
                            : vendor + " " + formatEngineVersion (instrumentVersion);
}

} // namespace vstengine::ui
