#include "VoxTabBar.h"
#include "../Tokens.h"
#include "../Typography.h"
#include <cmath>

namespace vox::ui {

VoxTabBar::VoxTabBar()
{
    setWantsKeyboardFocus (true);
    setSize (360, tokens::size::tabHeight);
}

void VoxTabBar::setTabs (std::vector<Tab> newTabs)
{
    tabs = std::move (newTabs);
    // Reference .ds-tabs buttons take their natural (icon + text) width and
    // sit left-aligned with a 2px gap; measure once for layout/hit-testing.
    widths.clear();
    widths.reserve (tabs.size());
    for (const auto& tab : tabs)
    {
        int width = 22;
        if (tab.icon.isNotEmpty())
            width += 14 + 7; // vector glyph slot
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (typography::tabText(), tab.title.toUpperCase(), 0.0f, 0.0f);
        width += static_cast<int> (glyphs.getBoundingBox (0, -1, true).getWidth());
        widths.push_back (width);
    }
    selectedIndex = -1;
    hoverIndex = -1;
    for (int i = 0; i < static_cast<int> (tabs.size()); ++i)
    {
        if (tabs[static_cast<std::size_t> (i)].enabled)
        {
            selectedIndex = i;
            break;
        }
    }
    repaint();
}

void VoxTabBar::setSelectedIndex (int index)
{
    if (index < 0 || index >= static_cast<int> (tabs.size()))
        return;
    if (! tabs[static_cast<std::size_t> (index)].enabled || selectedIndex == index)
        return;

    selectedIndex = index;
    repaint();
    if (onTabChanged)
        onTabChanged (selectedIndex);
}

juce::Rectangle<int> VoxTabBar::boundsForIndex (int index) const noexcept
{
    if (tabs.empty() || index < 0 || index >= static_cast<int> (tabs.size()))
        return {};

    // Left-aligned natural-width buttons with a 2px gap (reference .ds-tabs).
    // If the strip overflows, widths compress proportionally so every tab
    // stays reachable.
    int natural = 2;
    for (const auto width : widths)
        natural += width + 2;
    const auto available = getWidth();
    const auto scale = natural <= available ? 1.0f
        : static_cast<float> (available - 2) / static_cast<float> (natural - 2);

    int x = 2;
    for (int i = 0; i < index; ++i)
        x += static_cast<int> (widths[static_cast<std::size_t> (i)] * scale) + 2;
    const auto width = static_cast<int> (widths[static_cast<std::size_t> (index)] * scale);
    return { x, 0, width, getHeight() };
}

int VoxTabBar::indexAt (juce::Point<int> position) const noexcept
{
    for (int i = 0; i < static_cast<int> (tabs.size()); ++i)
        if (boundsForIndex (i).contains (position))
            return i;
    return -1;
}

int VoxTabBar::nextEnabledIndex (int start, int direction) const noexcept
{
    if (tabs.empty())
        return -1;

    auto index = start;
    for (std::size_t attempt = 0; attempt < tabs.size(); ++attempt)
    {
        index += direction;
        if (index < 0)
            index = static_cast<int> (tabs.size()) - 1;
        else if (index >= static_cast<int> (tabs.size()))
            index = 0;

        if (tabs[static_cast<std::size_t> (index)].enabled)
            return index;
    }
    return start;
}

namespace
{
// Compact vector tab glyph (reference lucide panel-header icons, 13px on a
// 14px slot). Keyword-keyed so products stay data-driven without a font
// dependency (the UI mono face does not carry these glyphs).
void drawTabGlyph (juce::Graphics& g, const juce::String& key,
                   juce::Rectangle<float> slot, juce::Colour colour)
{
    if (key.isEmpty() || slot.getWidth() < 10.0f)
        return;
    const auto stroke = juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded);
    const auto cx = slot.getCentreX(), cy = slot.getCentreY();
    auto strokePath = [&] (juce::Path& p)
    {
        g.setColour (colour);
        g.strokePath (p, stroke);
    };
    if (key == "sliders")
    {
        for (int i = 0; i < 3; ++i)
        {
            const auto y = slot.getY() + 3 + i * 4.0f;
            juce::Path line; line.startNewSubPath (slot.getX() + 1, y);
            line.lineTo (slot.getRight() - 1, y);
            strokePath (line);
            g.fillEllipse (slot.getX() + 3 + i * 2.5f - 1.5f, y - 1.5f, 3, 3);
        }
    }
    else if (key == "layers")
    {
        for (int i = 0; i < 3; ++i)
        {
            const auto y = slot.getY() + 3 + i * 3.6f;
            juce::Path line;
            line.startNewSubPath (slot.getX() + 1 + i * 1.2f, y);
            line.lineTo (slot.getRight() - 1 - i * 1.2f, y);
            strokePath (line);
        }
    }
    else if (key == "wave")
    {
        juce::Path wave;
        wave.startNewSubPath (slot.getX() + 1, cy);
        wave.quadraticTo (slot.getX() + 4, slot.getY() + 1, cx, cy);
        wave.quadraticTo (slot.getRight() - 4, slot.getBottom() - 1, slot.getRight() - 1, cy);
        strokePath (wave);
    }
    else if (key == "arrow")
    {
        juce::Path arrow;
        arrow.startNewSubPath (slot.getX() + 1, cy);
        arrow.lineTo (slot.getRight() - 2, cy);
        arrow.startNewSubPath (slot.getRight() - 5, cy - 3);
        arrow.lineTo (slot.getRight() - 2, cy);
        arrow.lineTo (slot.getRight() - 5, cy + 3);
        strokePath (arrow);
    }
    else if (key == "grid")
    {
        g.setColour (colour);
        for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
                g.fillRect (slot.getX() + 1.5f + x * 5.5f, slot.getY() + 1.5f + y * 5.5f, 3.6f, 3.6f);
    }
    else if (key == "star")
    {
        juce::Path star;
        const auto outer = 6.0f, inner = 2.6f;
        for (int i = 0; i < 10; ++i)
        {
            const auto a = -juce::MathConstants<float>::halfPi
                         + i * juce::MathConstants<float>::pi / 5.0f;
            const auto r = i % 2 == 0 ? outer : inner;
            const auto p = juce::Point<float> (cx + std::cos (a) * r, cy + std::sin (a) * r);
            if (i == 0) star.startNewSubPath (p); else star.lineTo (p);
        }
        star.closeSubPath();
        g.setColour (colour);
        g.fillPath (star);
    }
    else if (key == "warning")
    {
        juce::Path tri;
        tri.addTriangle ({ cx, slot.getY() + 1.5f }, { slot.getX() + 1, slot.getBottom() - 1.5f },
                         { slot.getRight() - 1, slot.getBottom() - 1.5f });
        strokePath (tri);
        juce::Path mark;
        mark.startNewSubPath (cx, cy - 2);
        mark.lineTo (cx, cy + 2);
        strokePath (mark);
        g.setColour (colour);
        g.fillEllipse (cx - 0.7f, cy + 3, 1.4f, 1.4f);
    }
}
} // namespace

void VoxTabBar::paint (juce::Graphics& g)
{
    // Reference .ds-tabs: compact icon+text buttons on a transparent strip.
    // Selected = accent text, accent-line border, accent-soft fill; the
    // unselected rest state is transparent with secondary text.
    for (int i = 0; i < static_cast<int> (tabs.size()); ++i)
    {
        const auto& tab = tabs[static_cast<std::size_t> (i)];
        const auto active = i == selectedIndex;
        const auto hovered = i == hoverIndex && tab.enabled;
        const auto bounds = boundsForIndex (i).toFloat().reduced (0.5f);

        if (active)
        {
            g.setColour (tokens::colour::themeAccent.withAlpha (0.13f)); // accent-soft
            g.fillRoundedRectangle (bounds, tokens::radius::medium);
            g.setColour (tokens::colour::themeAccent.interpolatedWith (tokens::colour::border, 0.58f));
            g.drawRoundedRectangle (bounds, tokens::radius::medium, 1.0f);
            g.setColour (tokens::colour::themeAccent);
        }
        else
        {
            if (hovered)
            {
                g.setColour (juce::Colour (0x0affffff));
                g.fillRoundedRectangle (bounds, tokens::radius::medium);
            }
            g.setColour (tab.enabled ? tokens::colour::textSecondary : tokens::colour::textMuted);
        }

        auto content = bounds.toNearestInt().reduced (10, 0);
        if (tab.icon.isNotEmpty())
        {
            const auto slot = juce::Rectangle<float> (content.getX(), bounds.getCentreY() - 7.0f,
                                                      14.0f, 14.0f);
            drawTabGlyph (g, tab.icon, slot,
                          active ? tokens::colour::themeAccent : tokens::colour::textSecondary);
            content.removeFromLeft (14 + 7);
        }
        g.setFont (typography::tabText());
        g.drawText (tab.title.toUpperCase(), content, juce::Justification::centredLeft, true);
    }

    if (hasKeyboardFocus (true))
    {
        g.setColour (tokens::colour::themeAccent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f),
                                tokens::radius::small, 1.0f);
    }
}

void VoxTabBar::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto index = indexAt (e.getPosition());
    if (index >= 0)
        setSelectedIndex (index);
}

void VoxTabBar::mouseMove (const juce::MouseEvent& e)
{
    const auto next = indexAt (e.getPosition());
    if (hoverIndex != next)
    {
        hoverIndex = next;
        repaint();
    }
}

void VoxTabBar::mouseExit (const juce::MouseEvent&)
{
    hoverIndex = -1;
    repaint();
}

bool VoxTabBar::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey)
    {
        setSelectedIndex (nextEnabledIndex (selectedIndex, -1));
        return true;
    }
    if (key == juce::KeyPress::rightKey)
    {
        setSelectedIndex (nextEnabledIndex (selectedIndex, 1));
        return true;
    }
    return false;
}

} // namespace vox::ui
