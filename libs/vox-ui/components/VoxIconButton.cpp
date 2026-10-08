#include "VoxIconButton.h"
#include "../Tokens.h"

namespace vox::ui {

VoxIconButton::VoxIconButton (const icons::Icon icon, const bool isToggle)
    : VoxButton ({}, isToggle ? Type::Toggle : Type::Icon),
      glyph (icon)
{
    setType (isToggle ? Type::Toggle : Type::Icon);
    VoxButton::setIcon (glyph);
    setSize (tokens::size::controlHeight, tokens::size::controlHeight);
}

void VoxIconButton::setIcon (const icons::Icon icon)
{
    glyph = icon;
    VoxButton::setIcon (glyph);
}

void VoxIconButton::setToggled (const bool shouldBeToggled)
{
    setToggleState (shouldBeToggled, juce::dontSendNotification);
}

} // namespace vox::ui
