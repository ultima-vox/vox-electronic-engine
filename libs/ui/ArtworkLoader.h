#pragma once

#include <juce_graphics/juce_graphics.h>

#include <memory>
#include <vector>

namespace vstengine::ui {

// ===========================================================================
// Artwork loading (UI_PRODUCTION_IMPLEMENTATION_STANDARD.md section 19).
// ===========================================================================
//
// The accepted renders use photographic/illustrated identity artwork (the hero
// landscape and the small per-slot thumbnails). The owner supplies those
// original files; until they exist the composition must still render, so this
// loader is strictly ADDITIVE:
//
//   * it never invents geometry. It answers "is there an asset for this role
//     and identity, and if so what image is it";
//   * it caches both hits and misses, so a missing asset costs one directory
//     probe for the lifetime of the process and never re-stats per paint;
//   * when it has no asset the caller keeps its existing procedural painter
//     (`HeroBanner::paintArtwork`, `RackSlotCard::drawThumbnail`). That is the
//     section 19 fallback path and it is what keeps the build working with the
//     artwork directory absent or empty.
//
// ASSET LOCATION
// --------------
// Assets are expected in `<repo>/_pr43-visual-gate/reference/artwork/`, i.e. the
// owner-supplied drop directory next to the accepted reference renders. A plugin
// process has no dependable notion of the source tree, so the directory is
// located in this order:
//
//   1. the directory named by the VOX_UI_ARTWORK_DIR environment variable (this
//      is also how a harness or a future packaging step can point at a bundled
//      asset folder);
//   2. `_pr43-visual-gate/reference/artwork` found by walking up from the
//      current working directory;
//   3. the same walk starting from this translation unit's own location.
//
// Nothing here is a build step: an absent directory or an unreadable file is a
// plain cache miss, not a failure.
namespace artwork {

// One asset slot. The string value is the file stem inside the artwork
// directory, so a role maps to exactly one file name.
enum class Role {
    heroPsyBass,   // "hero-psy-bass"
    heroAcid,      // "hero-acid"
    heroGeneric,   // "hero-generic"
    railFooter,    // "rail-footer"       (SOUND / FOR A DIFFERENT / TOMORROW)
    slotPsyBass,   // "slot-psy-bass"
    slotAcid,      // "slot-acid"
    slotLead,      // "slot-lead"
    slotAtmos,     // "slot-atmos"
    slotEmpty,     // "slot-empty"
    logo           // "logo"
};

// The resolved artwork directory, or an empty file when none was found.
// Exposed for diagnostics; callers do not need it.
[[nodiscard]] juce::File directory();

// The asset for `role`, or an invalid image when the asset does not exist.
//
// Callers must treat an invalid result as "use the procedural fallback" and
// never as "draw nothing". Results are cached; repeated calls do not touch the
// filesystem.
[[nodiscard]] const juce::Image& imageFor (Role role);

// True when the role has a real asset. Use it to decide between the asset path
// and the procedural path without a second lookup.
[[nodiscard]] bool has (Role role);

// Aspect-FILL: scales `source` uniformly so it covers `target` completely and
// crops the overflow, preserving aspect ratio. This is the section 19 rule for
// identity artwork; aspect-fit would letterbox and break the full-bleed band.
void drawFilled (juce::Graphics&, const juce::Image& source, juce::Rectangle<float> target,
                 float cornerRadius = 0.0f);

// Identity artwork is never allowed to compete with the copy drawn over it. This
// lays the section 19 contrast overlay over `area`: a left-to-right and
// bottom-up darkening that keeps the identity readable without hiding the art.
void drawContrastOverlay (juce::Graphics&, juce::Rectangle<float> area,
                          juce::Colour tint = juce::Colour (0xff04101a));

// Forgets every cached image and re-probes the directory. Test/harness hook
// only; the product never needs it.
void clearCache();

// Overrides the resolved directory. Pass an empty file to return to the search
// order above. Test/harness hook only.
void setDirectoryOverride (const juce::File&);

// Absolute candidate directories in resolution order. Diagnostics only.
[[nodiscard]] std::vector<juce::File> candidateDirectories();

} // namespace artwork
} // namespace vstengine::ui
