#pragma once

#include <cmath>
#include <juce_graphics/juce_graphics.h>
#include "Tokens.h"
#include "Typography.h"

// Shared VOX drawing primitives. Shell, rack and routing surfaces paint
// through these so that one visual decision lives in one place.
namespace vox::ui::paint {

// Canonical panel: flat dark fill, 1 px subtle blue border, small radius,
// no drop shadow.
inline void panel (juce::Graphics& g, juce::Rectangle<float> bounds,
                   bool raised = false)
{
    bounds = bounds.reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (
        (raised ? tokens::colour::panelRaised : tokens::colour::panel).brighter (0.04f),
        bounds.getX(), bounds.getY(),
        raised ? tokens::colour::panelRaised : tokens::colour::panel,
        bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, tokens::radius::medium);
    g.setColour (tokens::colour::border);
    g.drawRoundedRectangle (bounds, tokens::radius::medium, 1.0f);
}

// Selected outline with a restrained single-element glow.
inline void selectedOutline (juce::Graphics& g, juce::Rectangle<float> bounds,
                             float radius = tokens::radius::small)
{
    bounds = bounds.reduced (0.5f);
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (tokens::colour::accent.withAlpha (0.06f * static_cast<float> (4 - i)));
        g.drawRoundedRectangle (bounds.expanded (static_cast<float> (i) * 0.8f),
                                radius + static_cast<float> (i), 1.2f);
    }
    g.setColour (tokens::colour::accent);
    g.drawRoundedRectangle (bounds, radius, 1.4f);
}

// IEC power symbol; colour carries on/off and the stroke stays readable when
// colour is unavailable (the gap is always drawn).
inline void powerIcon (juce::Graphics& g, juce::Rectangle<float> area,
                       juce::Colour colour)
{
    const auto r = juce::jmin (area.getWidth(), area.getHeight()) * 0.36f;
    const auto c = area.getCentre();
    juce::Path arc;
    arc.addCentredArc (c.x, c.y + r * 0.1f, r, r, 0.0f, 0.7f,
                       juce::MathConstants<float>::twoPi - 0.7f, true);
    g.setColour (colour);
    g.strokePath (arc, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.drawLine (c.x, c.y - r * 1.15f, c.x, c.y - r * 0.15f, 1.5f);
}

inline juce::Font spaced (float height, float kerning, bool bold = false)
{
    auto font = typography::makeFont (height, bold ? "SemiBold" : juce::String());
    return font.withExtraKerningFactor (kerning);
}

// Panel header grammar: [power] TITLE ............ [actions]
inline void sectionTitle (juce::Graphics& g, juce::Rectangle<int> area,
                          const juce::String& title, bool hasPower, bool powered)
{
    if (hasPower)
    {
        powerIcon (g, area.removeFromLeft (area.getHeight()).toFloat().reduced (2.0f),
                   powered ? tokens::colour::accent : tokens::colour::textMuted);
        area.removeFromLeft (4);
    }
    g.setColour (tokens::colour::text);
    g.setFont (spaced (12.0f, 0.06f, true));
    g.drawText (title, area, juce::Justification::centredLeft, true);
}

// Vertical or horizontal level bar with peak-dependent colour. Value is a
// linear peak; the scale is -60..0 dBFS.
inline float meterProportion (float linear) noexcept
{
    if (! (linear > 0.0f))
        return 0.0f;
    const auto db = 20.0f * std::log10 (linear);
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

inline void meterBar (juce::Graphics& g, juce::Rectangle<float> area, float linear,
                      bool vertical)
{
    g.setColour (tokens::colour::graph);
    g.fillRect (area);
    const auto p = meterProportion (linear);
    auto lit = vertical ? area.withTrimmedTop (area.getHeight() * (1.0f - p))
                        : area.withWidth (area.getWidth() * p);
    g.setColour (linear >= 1.0f ? tokens::colour::danger : tokens::colour::accent);
    g.fillRect (lit);
}

} // namespace vox::ui::paint
