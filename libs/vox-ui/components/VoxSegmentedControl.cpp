#include "VoxSegmentedControl.h"
#include "../Tokens.h"
#include "../Typography.h"

namespace vox::ui {

VoxSegmentedControl::VoxSegmentedControl()
{
    setWantsKeyboardFocus (true);
    setSize (145, 26);
}

void VoxSegmentedControl::setSegments (juce::StringArray newSegments)
{
    segments = std::move (newSegments);
    enabled.assign (static_cast<std::size_t> (juce::jmax (0, segments.size())), true);
    selectedIndex = segments.isEmpty() ? -1 : 0;
    hoverIndex = -1;
    repaint();
}

void VoxSegmentedControl::setSelectedIndex (const int index, const bool sendChange)
{
    if (segments.isEmpty())
    {
        selectedIndex = -1;
        return;
    }

    const auto clamped = juce::jlimit (0, segments.size() - 1, index);
    if (selectedIndex == clamped)
        return;

    selectedIndex = clamped;
    repaint();

    if (sendChange && onChanged)
        onChanged (selectedIndex);
}

void VoxSegmentedControl::setSegmentEnabled (const int index, const bool shouldBeEnabled)
{
    if (index < 0 || index >= static_cast<int> (enabled.size()))
        return;

    enabled[static_cast<std::size_t> (index)] = shouldBeEnabled;
    repaint();
}

bool VoxSegmentedControl::isSegmentEnabled (const int index) const noexcept
{
    if (index < 0 || index >= static_cast<int> (enabled.size()))
        return false;
    return enabled[static_cast<std::size_t> (index)];
}

void VoxSegmentedControl::setSegmentGap (const int newGap)
{
    const auto clamped = juce::jmax (0, newGap);
    if (segmentGap == clamped)
        return;
    segmentGap = clamped;
    repaint();
}

juce::Rectangle<int> VoxSegmentedControl::boundsForIndex (const int index) const noexcept
{
    const auto count = static_cast<int> (segments.size());
    if (count <= 0 || index < 0 || index >= count)
        return {};

    const auto total = getWidth() - segmentGap * (count - 1);
    if (total <= 0)
        return {};

    const auto x = (total * index) / count + segmentGap * index;
    const auto nextX = (total * (index + 1)) / count + segmentGap * (index + 1);
    return { x, 0, juce::jmax (1, nextX - x - segmentGap), getHeight() };
}

int VoxSegmentedControl::indexAt (const juce::Point<int> position) const noexcept
{
    for (int i = 0; i < segments.size(); ++i)
        if (boundsForIndex (i).contains (position))
            return i;
    return -1;
}

int VoxSegmentedControl::nextEnabledIndex (const int start, const int direction) const noexcept
{
    const auto count = static_cast<int> (segments.size());
    if (count <= 0)
        return -1;

    auto index = start;
    for (int attempt = 0; attempt < count; ++attempt)
    {
        index += direction;
        if (index < 0)
            index = count - 1;
        else if (index >= count)
            index = 0;

        if (isSegmentEnabled (index))
            return index;
    }
    return start;
}

void VoxSegmentedControl::paint (juce::Graphics& g)
{
    for (int i = 0; i < segments.size(); ++i)
    {
        const auto active = i == selectedIndex;
        const auto usable = isSegmentEnabled (i);
        const auto hovered = i == hoverIndex && usable && ! active;
        const auto bounds = boundsForIndex (i).toFloat().reduced (0.5f);

        auto fill = active ? tokens::colour::accent : tokens::colour::control;
        auto border = active ? tokens::colour::accent
                             : (hovered ? tokens::colour::accentDim : tokens::colour::border);
        auto text = active ? tokens::colour::background : tokens::colour::text;

        if (! usable)
        {
            fill = fill.withMultipliedAlpha (0.4f);
            border = border.withMultipliedAlpha (0.4f);
            text = tokens::colour::textMuted;
        }

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, tokens::radius::small);
        g.setColour (border);
        g.drawRoundedRectangle (bounds, tokens::radius::small, 1.0f);
        g.setColour (text);
        g.setFont (typography::controlLabel());
        g.drawText (segments[i], bounds.toNearestInt(), juce::Justification::centred, true);
    }

    if (hasKeyboardFocus (true) && selectedIndex >= 0)
    {
        g.setColour (tokens::colour::accent);
        g.drawRoundedRectangle (boundsForIndex (selectedIndex).toFloat().reduced (0.5f),
                                tokens::radius::small, 1.0f);
    }
}

void VoxSegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto index = indexAt (e.getPosition());
    if (index >= 0 && isSegmentEnabled (index))
        setSelectedIndex (index, true);
}

void VoxSegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const auto next = indexAt (e.getPosition());
    if (hoverIndex != next)
    {
        hoverIndex = next;
        repaint();
    }
}

void VoxSegmentedControl::mouseExit (const juce::MouseEvent&)
{
    hoverIndex = -1;
    repaint();
}

bool VoxSegmentedControl::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey)
    {
        setSelectedIndex (nextEnabledIndex (selectedIndex, -1), true);
        return true;
    }
    if (key == juce::KeyPress::rightKey)
    {
        setSelectedIndex (nextEnabledIndex (selectedIndex, 1), true);
        return true;
    }
    return false;
}

} // namespace vox::ui
