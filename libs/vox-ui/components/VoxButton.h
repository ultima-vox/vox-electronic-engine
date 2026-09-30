#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace vox::ui {

class VoxButton : public juce::Button
{
public:
    enum class Type { Primary, Secondary, Toggle, Danger, Icon };

    explicit VoxButton (juce::String text = {}, Type type = Type::Secondary);

    void setType (Type newType);
    Type getType() const noexcept { return type; }
    void setIcon (std::unique_ptr<juce::Drawable> drawable);
    void clearIcon();

    void paintButton (juce::Graphics&, bool isMouseOverButton, bool isButtonDown) override;

private:
    Type type = Type::Secondary;
    std::unique_ptr<juce::Drawable> icon;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxButton)
};

} // namespace vox::ui
