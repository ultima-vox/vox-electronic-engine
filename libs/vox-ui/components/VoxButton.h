#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "VoxIcons.h"

namespace vox::ui {

class VoxButton : public juce::Button
{
public:
    enum class Type { Primary, Secondary, Toggle, Danger, Icon };

    explicit VoxButton (juce::String text = {}, Type type = Type::Secondary);

    void setType (Type newType);
    Type getType() const noexcept { return type; }

    // Drawable icon. Retained for callers that already own an art asset.
    void setIcon (std::unique_ptr<juce::Drawable> drawable);

    // Vector icon from the single shared family. This is the path the product
    // should use: it needs no asset, scales cleanly and cannot drift from the
    // rest of the iconography.
    void setIcon (icons::Icon icon, float sizePx = 0.0f);

    void clearIcon();

    // When false the button never paints a border or hover tint, which is what
    // the accepted render's header affordances look like at rest.
    void setBorderVisible (bool shouldBeVisible);
    bool isBorderVisible() const noexcept { return borderVisible; }

    // 0 derives the glyph size from the button height.
    void setIconSize (float sizePx);

    // Overrides the state-derived glyph colour. Left unset, the glyph follows
    // the button's own text colour for the current state.
    void setIconColour (juce::Colour colour);
    void clearIconColour();

    void paintButton (juce::Graphics&, bool isMouseOverButton, bool isButtonDown) override;

private:
    void paintIcon (juce::Graphics&, juce::Colour stateColour) const;
    float resolvedIconSize() const noexcept;

    Type type = Type::Secondary;
    std::unique_ptr<juce::Drawable> icon;
    icons::Icon vectorIcon { icons::Icon::plus };
    bool hasVectorIcon = false;
    bool borderVisible = true;
    float iconSizePx = 0.0f;
    bool hasIconColour = false;
    juce::Colour iconColour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxButton)
};

} // namespace vox::ui
