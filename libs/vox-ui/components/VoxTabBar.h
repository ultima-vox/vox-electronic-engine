#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace vox::ui {

class VoxTabBar : public juce::Component
{
public:
    struct Tab
    {
        juce::String id;
        juce::String title;
        bool enabled = true;
    };

    VoxTabBar();

    void setTabs (std::vector<Tab> newTabs);
    void setSelectedIndex (int index);
    int getSelectedIndex() const noexcept { return selectedIndex; }

    // Gap kept between adjacent tabs. The accepted render leaves ~5 px of panel
    // between one tab body and the next, so the six nav tabs read as six
    // controls rather than one continuous bar.
    void setTabGutter (int newGutter);
    int getTabGutter() const noexcept { return tabGutter; }

    std::function<void (int)> onTabChanged;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    std::vector<Tab> tabs;
    int selectedIndex = -1;
    int hoverIndex = -1;
    int pressedIndex = -1;
    int tabGutter = 5;

    int indexAt (juce::Point<int> position) const noexcept;
    juce::Rectangle<int> boundsForIndex (int index) const noexcept;
    int nextEnabledIndex (int start, int direction) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxTabBar)
};

} // namespace vox::ui
