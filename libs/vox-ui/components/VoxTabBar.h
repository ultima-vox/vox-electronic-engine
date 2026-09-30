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

    int indexAt (juce::Point<int> position) const noexcept;
    juce::Rectangle<int> boundsForIndex (int index) const noexcept;
    int nextEnabledIndex (int start, int direction) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxTabBar)
};

} // namespace vox::ui
