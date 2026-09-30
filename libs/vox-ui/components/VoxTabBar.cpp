#include "VoxTabBar.h"
#include "../Tokens.h"
#include "../Typography.h"

namespace vox::ui {

VoxTabBar::VoxTabBar()
{
    setWantsKeyboardFocus (true);
    setSize (360, tokens::size::tabHeight);
}

void VoxTabBar::setTabs (std::vector<Tab> newTabs)
{
    tabs = std::move (newTabs);
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

    const auto count = static_cast<int> (tabs.size());
    const auto baseWidth = getWidth() / count;
    const auto x = index * baseWidth;
    const auto width = index == count - 1 ? getWidth() - x : baseWidth;
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

void VoxTabBar::paint (juce::Graphics& g)
{
    for (int i = 0; i < static_cast<int> (tabs.size()); ++i)
    {
        const auto& tab = tabs[static_cast<std::size_t> (i)];
        const auto active = i == selectedIndex;
        const auto hovered = i == hoverIndex && tab.enabled;
        auto bounds = boundsForIndex (i).toFloat().reduced (0.5f);

        auto fill = active ? tokens::colour::accent : tokens::colour::control;
        auto border = active ? tokens::colour::accent
                             : (hovered ? tokens::colour::accentDim : tokens::colour::border);
        auto text = active ? tokens::colour::background
                           : (hovered ? tokens::colour::text : tokens::colour::textSecondary);

        if (! tab.enabled)
        {
            fill = fill.withMultipliedAlpha (0.35f);
            border = border.withMultipliedAlpha (0.35f);
            text = tokens::colour::textMuted;
        }

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, tokens::radius::small);
        g.setColour (border);
        g.drawRoundedRectangle (bounds, tokens::radius::small, 1.0f);
        g.setFont (typography::sectionTitle());
        g.setColour (text);
        g.drawText (tab.title.toUpperCase(), bounds.toNearestInt(),
                    juce::Justification::centred, true);
    }

    if (hasKeyboardFocus (true))
    {
        g.setColour (tokens::colour::accent);
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
