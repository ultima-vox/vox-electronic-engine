#pragma once

#include <juce_graphics/juce_graphics.h>

namespace vox::ui::typography {

inline constexpr float instrumentTitlePx = 26.0f;
inline constexpr float sectionTitlePx = 14.0f;
inline constexpr float controlLabelPx = 12.0f;
inline constexpr float valueTextPx = 11.0f;

inline juce::Font makeFont (float height, const juce::String& style = {})
{
    auto base = juce::Font (juce::FontOptions (height));
    if (style.isEmpty())
        return base;

    auto styled = juce::Font (juce::FontOptions (height).withStyle (style));
    if (styled.getTypefaceStyle().equalsIgnoreCase (style))
        return styled;

    // System font families do not consistently expose a literal "SemiBold"
    // face. Preserve the requested visual weight deterministically.
    if (style.equalsIgnoreCase ("SemiBold"))
    {
        base.setBold (true);
        return base;
    }

    return styled;
}

inline juce::Font instrumentTitle() { return makeFont (instrumentTitlePx, "SemiBold"); }
inline juce::Font sectionTitle()    { return makeFont (sectionTitlePx, "SemiBold"); }
inline juce::Font controlLabel()    { return makeFont (controlLabelPx); }
inline juce::Font valueText()       { return makeFont (valueTextPx); }

} // namespace vox::ui::typography
