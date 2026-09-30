#include "VoxButton.h"
#include "../Tokens.h"
#include "../Typography.h"

namespace vox::ui {

VoxButton::VoxButton (juce::String text, Type buttonType)
    : juce::Button (std::move (text)), type (buttonType)
{
    setWantsKeyboardFocus (true);
    setClickingTogglesState (type == Type::Toggle);
    setSize (120, tokens::size::controlHeight);
}

void VoxButton::setType (Type newType)
{
    type = newType;
    setClickingTogglesState (type == Type::Toggle);
    repaint();
}

void VoxButton::setIcon (std::unique_ptr<juce::Drawable> drawable)
{
    icon = std::move (drawable);
    repaint();
}

void VoxButton::clearIcon()
{
    icon.reset();
    repaint();
}

void VoxButton::paintButton (juce::Graphics& g, bool isMouseOverButton, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    auto fill = tokens::colour::control;
    auto border = tokens::colour::border;
    auto text = tokens::colour::textSecondary;

    switch (type)
    {
        case Type::Primary:
            fill = tokens::colour::accent;
            border = tokens::colour::accent;
            text = tokens::colour::background;
            break;
        case Type::Secondary:
            if (isMouseOverButton)
                border = tokens::colour::accentDim;
            break;
        case Type::Toggle:
            if (getToggleState())
            {
                fill = tokens::colour::accent;
                border = tokens::colour::accent;
                text = tokens::colour::background;
            }
            else if (isMouseOverButton)
            {
                border = tokens::colour::accentDim;
            }
            break;
        case Type::Danger:
            fill = tokens::colour::danger;
            border = tokens::colour::danger;
            text = tokens::colour::text;
            break;
        case Type::Icon:
            fill = juce::Colours::transparentBlack;
            border = isMouseOverButton ? tokens::colour::accentDim
                                       : juce::Colours::transparentBlack;
            text = tokens::colour::textSecondary;
            break;
    }

    if (hasKeyboardFocus (true))
        border = tokens::colour::accent;
    if (isButtonDown)
        fill = fill.darker (0.15f);
    if (! isEnabled())
    {
        fill = fill.withMultipliedAlpha (0.35f);
        border = border.withMultipliedAlpha (0.35f);
        text = tokens::colour::textMuted;
    }

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, tokens::radius::small);
    if (! border.isTransparent())
    {
        g.setColour (border);
        g.drawRoundedRectangle (bounds, tokens::radius::small, 1.0f);
    }

    if (type == Type::Icon && icon != nullptr)
    {
        icon->drawWithin (g, bounds.reduced (static_cast<float> (tokens::spacing::sm)),
                          juce::RectanglePlacement::centred,
                          isEnabled() ? 1.0f : 0.35f);
        return;
    }

    g.setFont (typography::controlLabel());
    g.setColour (text);
    g.drawText (getButtonText(), getLocalBounds().reduced (tokens::spacing::sm, 0),
                juce::Justification::centred, true);
}

} // namespace vox::ui
