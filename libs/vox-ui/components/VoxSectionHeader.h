#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace vox::ui {

class VoxSectionHeader : public juce::Component
{
public:
    enum class LeadingType { None, Power, Icon };

    explicit VoxSectionHeader (juce::String title = {});

    void setTitle (juce::String newTitle);
    void setPowerVisible (bool visible);
    void setPowerState (bool on);
    std::function<void (bool)> onPowerToggled;

    void setIcon (std::unique_ptr<juce::Drawable> drawable);
    void clearIcon();

    void setActionComponent (juce::Component* component);
    LeadingType getLeadingType() const noexcept { return leading; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String title;
    LeadingType leading = LeadingType::None;
    bool powerOn = true;
    std::unique_ptr<juce::Drawable> icon;
    juce::Component* action = nullptr;

    juce::Rectangle<int> getLeadingBounds() const;
    juce::Rectangle<int> getActionBounds() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxSectionHeader)
};

} // namespace vox::ui
