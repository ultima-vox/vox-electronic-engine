#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vox::ui {

class VoxPanel : public juce::Component {
public:
    explicit VoxPanel (juce::String title = {});

    void setTitle (juce::String newTitle);
    void paint (juce::Graphics&) override;
    juce::Rectangle<int> getContentBounds() const;

private:
    juce::String title;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxPanel)
};

} // namespace vox::ui
