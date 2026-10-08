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
    hasVectorIcon = false;
    repaint();
}

void VoxButton::setIcon (const icons::Icon newIcon, const float sizePx)
{
    vectorIcon = newIcon;
    hasVectorIcon = true;
    icon.reset();
    iconSizePx = sizePx;
    repaint();
}

void VoxButton::clearIcon()
{
    icon.reset();
    hasVectorIcon = false;
    repaint();
}

void VoxButton::setBorderVisible (const bool shouldBeVisible)
{
    if (borderVisible == shouldBeVisible)
        return;
    borderVisible = shouldBeVisible;
    repaint();
}

void VoxButton::setIconSize (const float sizePx)
{
    iconSizePx = sizePx;
    repaint();
}

void VoxButton::setIconColour (juce::Colour colour)
{
    iconColour = colour;
    hasIconColour = true;
    repaint();
}

void VoxButton::clearIconColour()
{
    hasIconColour = false;
    repaint();
}

float VoxButton::resolvedIconSize() const noexcept
{
    if (iconSizePx > 0.0f)
        return iconSizePx;

    return juce::jlimit (10.0f, 24.0f,
                         static_cast<float> (juce::jmin (getWidth(), getHeight())) * 0.46f);
}

void VoxButton::paintIcon (juce::Graphics& g, const juce::Colour stateColour) const
{
    const auto area = getLocalBounds().toFloat();
    const auto alpha = isEnabled() ? 1.0f : 0.35f;

    if (hasVectorIcon)
    {
        auto glyphArea = area.withSizeKeepingCentre (resolvedIconSize(), resolvedIconSize());
        icons::draw (g, vectorIcon, glyphArea,
                     hasIconColour ? iconColour : stateColour, alpha);
        return;
    }

    if (icon != nullptr)
        icon->drawWithin (g, area.reduced (static_cast<float> (tokens::spacing::sm)),
                          juce::RectanglePlacement::centred, alpha);
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

    if (isButtonDown && type == Type::Icon)
    {
        // A pressed icon still needs a visible down state even though the rest
        // state is deliberately bare.
        fill = tokens::colour::control;
        border = tokens::colour::accent;
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

    if (! borderVisible)
        border = juce::Colours::transparentBlack;

    if (! fill.isTransparent())
    {
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, tokens::radius::small);
    }

    if (! border.isTransparent())
    {
        g.setColour (border);
        g.drawRoundedRectangle (bounds, tokens::radius::small, 1.0f);
    }

    // Any button carrying a glyph draws that glyph instead of its text label.
    // Type::Toggle is included on purpose: a toggling icon button (a lit power
    // affordance, for example) is the same control in a different state.
    if (hasVectorIcon || icon != nullptr)
    {
        paintIcon (g, text);
        return;
    }

    g.setFont (typography::controlLabel());
    g.setColour (text);
    g.drawText (getButtonText(), getLocalBounds().reduced (tokens::spacing::sm, 0),
                juce::Justification::centred, true);
}

} // namespace vox::ui
