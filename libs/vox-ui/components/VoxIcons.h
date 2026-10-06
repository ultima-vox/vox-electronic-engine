#pragma once

#include <juce_graphics/juce_graphics.h>

#include <memory>

namespace vox::ui::icons {

// The single vector icon family for the whole VOX product line.
//
// UI_PRODUCTION_IMPLEMENTATION_STANDARD.md section 9 requires exactly one icon
// grammar: vector, optically aligned, consistent stroke weight, and never a mix
// of emoji, Unicode symbols, raster icons or unrelated SVG styles. This header
// is that one grammar. Nothing in the product may draw a glyph from a character
// code point ("\xE2\x96\xBC", "<", ">", ...) as an icon.
//
// Every glyph is authored as a juce::Path inside a normalised 100 x 100 unit box
// with its origin at the top-left, so a caller can scale it to any size without
// loss and without a second asset. Stroked glyphs share one nominal stroke
// weight expressed as a fraction of the box, so scaling a glyph never changes
// its visual weight relative to its own size.
enum class Icon
{
    chevronLeft,
    chevronRight,
    chevronDown,
    power,
    plus,
    gear,
    pencil,
    heart,
    dice,
    cube,
    keyboardLayout,
    play,
    stop,
    matrixGrid
};

// The glyph inside a normalised 100 x 100 unit box, anchored at (0, 0).
[[nodiscard]] juce::Path make (Icon id);

// The same glyph scaled so the unit box maps to a `size` x `size` square
// anchored at (0, 0).
[[nodiscard]] juce::Path make (Icon id, float size);

// True when the glyph is a solid shape and must be filled instead of stroked.
[[nodiscard]] bool isFilled (Icon id) noexcept;

// Nominal stroke width as a fraction of the box side (1.0 == the whole box).
// Multiply by the drawn box side to obtain the pixel stroke width used by draw().
[[nodiscard]] float nominalStroke (Icon id) noexcept;

// Draws the glyph centred in `area`, mapped onto the largest centred square that
// fits. Stroked glyphs get the family stroke weight for that square; filled
// glyphs (play, stop) are filled.
void draw (juce::Graphics&, Icon id, juce::Rectangle<float> area, juce::Colour colour,
           float alpha = 1.0f);

// A juce::Drawable wrapper for APIs that still take a Drawable (VoxButton's
// Drawable overload, VoxSectionHeader::setIcon). `size` is the box side the
// Drawable is authored at; Drawable::drawWithin scales it from there.
[[nodiscard]] std::unique_ptr<juce::Drawable> makeDrawable (Icon id, juce::Colour colour,
                                                            float size = 100.0f);

} // namespace vox::ui::icons
