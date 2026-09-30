#include "VoxLookAndFeel.h"
#include "Tokens.h"
#include "Typography.h"

namespace vox::ui {

VoxLookAndFeel::VoxLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, tokens::colour::background);
    setColour (juce::Label::textColourId, tokens::colour::text);
    setColour (juce::ComboBox::textColourId, tokens::colour::text);
    setColour (juce::ComboBox::backgroundColourId, tokens::colour::control);
    setColour (juce::ComboBox::outlineColourId, tokens::colour::border);
    setColour (juce::TextButton::textColourOffId, tokens::colour::textSecondary);
    setColour (juce::TextButton::textColourOnId, tokens::colour::background);
    setColour (juce::PopupMenu::backgroundColourId, tokens::colour::panel);
    setColour (juce::PopupMenu::textColourId, tokens::colour::textSecondary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, tokens::colour::accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, tokens::colour::text);
    setColour (juce::Slider::textBoxTextColourId, tokens::colour::textSecondary);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void VoxLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                           const juce::Colour&, bool isMouseOverButton,
                                           bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = button.getToggleState() ? tokens::colour::accent : tokens::colour::control;
    auto border = button.getToggleState() ? tokens::colour::accent
                                          : (isMouseOverButton ? tokens::colour::accentDim
                                                               : tokens::colour::border);

    if (button.hasKeyboardFocus (true))
        border = tokens::colour::accent;
    if (isButtonDown)
        fill = fill.darker (0.15f);
    if (! button.isEnabled())
    {
        fill = fill.withMultipliedAlpha (0.35f);
        border = border.withMultipliedAlpha (0.35f);
    }

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, tokens::radius::small);
    g.setColour (border);
    g.drawRoundedRectangle (bounds, tokens::radius::small, 1.0f);
}

void VoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPosProportional, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider& slider)
{
    const auto diameter = static_cast<float> (juce::jmin (width, height));
    const auto radius = diameter * 0.5f;
    const auto strokeW = juce::jmax (1.5f, radius * 0.11f);
    const auto inset = strokeW * 0.8f;
    const auto centre = juce::Point<float> (static_cast<float> (x) + width * 0.5f,
                                            static_cast<float> (y) + height * 0.5f);
    const auto arcRadius = juce::jmax (1.0f, radius - inset - strokeW * 0.5f);
    const auto angle = rotaryStartAngle
                     + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    juce::Path inactive;
    inactive.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (tokens::colour::border);
    g.strokePath (inactive, juce::PathStrokeType (strokeW,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    juce::Path active;
    active.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          rotaryStartAngle, angle, true);
    g.setColour (slider.isEnabled() ? tokens::colour::accent : tokens::colour::textMuted);
    g.strokePath (active, juce::PathStrokeType (strokeW,
                                                juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    const auto bodyRadius = juce::jmax (1.0f, radius * 0.58f);
    g.setColour (tokens::colour::background);
    g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                   bodyRadius * 2.0f, bodyRadius * 2.0f);
    g.setColour (tokens::colour::borderSubtle);
    g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                   bodyRadius * 2.0f, bodyRadius * 2.0f,
                   juce::jmax (1.0f, strokeW * 0.3f));

    const auto markerStart = centre + juce::Point<float> (0.0f, -bodyRadius * 0.25f)
                                        .rotatedAboutOrigin (angle);
    const auto markerEnd = centre + juce::Point<float> (0.0f, -bodyRadius * 0.9f)
                                      .rotatedAboutOrigin (angle);
    g.setColour (slider.isEnabled() ? tokens::colour::text : tokens::colour::textMuted);
    g.drawLine ({ markerStart, markerEnd }, juce::jmax (1.0f, strokeW * 0.45f));
}

void VoxLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                   int buttonX, int buttonY, int buttonW, int buttonH,
                                   juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.5f, 0.5f,
                                                 static_cast<float> (width) - 1.0f,
                                                 static_cast<float> (height) - 1.0f);
    auto fill = tokens::colour::control;
    auto border = box.hasKeyboardFocus (true) ? tokens::colour::accent
                                               : (box.isMouseOverOrDragging()
                                                      ? tokens::colour::accentDim
                                                      : tokens::colour::border);
    if (! box.isEnabled())
    {
        fill = fill.withMultipliedAlpha (0.35f);
        border = border.withMultipliedAlpha (0.35f);
    }

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, tokens::radius::small);
    g.setColour (border);
    g.drawRoundedRectangle (bounds, tokens::radius::small, 1.0f);

    const auto arrowArea = juce::Rectangle<float> (static_cast<float> (buttonX),
                                                    static_cast<float> (buttonY),
                                                    static_cast<float> (buttonW),
                                                    static_cast<float> (buttonH))
                               .reduced (static_cast<float> (tokens::spacing::sm));
    juce::Path arrow;
    arrow.startNewSubPath (arrowArea.getX(), arrowArea.getCentreY() - 2.0f);
    arrow.lineTo (arrowArea.getCentreX(), arrowArea.getCentreY() + 3.0f);
    arrow.lineTo (arrowArea.getRight(), arrowArea.getCentreY() - 2.0f);
    g.setColour (box.isEnabled() ? tokens::colour::textSecondary : tokens::colour::textMuted);
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

void VoxLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                       bool isMouseOverButton, bool isButtonDown)
{
    const auto boxSize = static_cast<float> (juce::jmin (button.getHeight(), tokens::size::controlHeight));
    auto box = juce::Rectangle<float> (0.0f, 0.0f, boxSize, boxSize).reduced (5.0f);
    auto fill = button.getToggleState() ? tokens::colour::accent : tokens::colour::control;
    auto border = button.getToggleState() ? tokens::colour::accent
                                          : (isMouseOverButton ? tokens::colour::accentDim
                                                               : tokens::colour::border);
    if (button.hasKeyboardFocus (true))
        border = tokens::colour::accent;
    if (isButtonDown)
        fill = fill.darker (0.15f);
    if (! button.isEnabled())
    {
        fill = fill.withMultipliedAlpha (0.35f);
        border = border.withMultipliedAlpha (0.35f);
    }

    g.setColour (fill);
    g.fillRoundedRectangle (box, tokens::radius::small);
    g.setColour (border);
    g.drawRoundedRectangle (box, tokens::radius::small, 1.0f);

    if (button.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + box.getWidth() * 0.22f, box.getCentreY());
        tick.lineTo (box.getX() + box.getWidth() * 0.43f, box.getBottom() - box.getHeight() * 0.24f);
        tick.lineTo (box.getRight() - box.getWidth() * 0.18f, box.getY() + box.getHeight() * 0.25f);
        g.setColour (tokens::colour::background);
        g.strokePath (tick, juce::PathStrokeType (1.8f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    g.setFont (typography::controlLabel());
    g.setColour (button.isEnabled() ? tokens::colour::textSecondary : tokens::colour::textMuted);
    g.drawText (button.getButtonText(),
                button.getLocalBounds().withTrimmedLeft (static_cast<int> (boxSize + tokens::spacing::xs)),
                juce::Justification::centredLeft, false);
}

void VoxLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float minSliderPos, float maxSliderPos,
                                       juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool vertical = style == juce::Slider::LinearVertical
                       || style == juce::Slider::LinearBarVertical;
    const auto thickness = static_cast<float> (vertical ? width : height);
    const auto trackThickness = juce::jmax (2.0f, thickness * 0.12f);
    const auto thumbRadius = juce::jmax (4.0f, thickness * 0.22f);

    if (vertical)
    {
        const auto cx = static_cast<float> (x) + width * 0.5f;
        g.setColour (tokens::colour::border);
        g.fillRoundedRectangle (cx - trackThickness * 0.5f, minSliderPos,
                                trackThickness, maxSliderPos - minSliderPos,
                                trackThickness * 0.5f);
        g.setColour (slider.isEnabled() ? tokens::colour::accent : tokens::colour::textMuted);
        g.fillRoundedRectangle (cx - trackThickness * 0.5f, sliderPos,
                                trackThickness, maxSliderPos - sliderPos,
                                trackThickness * 0.5f);
        g.fillEllipse (cx - thumbRadius, sliderPos - thumbRadius,
                       thumbRadius * 2.0f, thumbRadius * 2.0f);
    }
    else
    {
        const auto cy = static_cast<float> (y) + height * 0.5f;
        g.setColour (tokens::colour::border);
        g.fillRoundedRectangle (minSliderPos, cy - trackThickness * 0.5f,
                                maxSliderPos - minSliderPos, trackThickness,
                                trackThickness * 0.5f);
        g.setColour (slider.isEnabled() ? tokens::colour::accent : tokens::colour::textMuted);
        g.fillRoundedRectangle (minSliderPos, cy - trackThickness * 0.5f,
                                sliderPos - minSliderPos, trackThickness,
                                trackThickness * 0.5f);
        g.fillEllipse (sliderPos - thumbRadius, cy - thumbRadius,
                       thumbRadius * 2.0f, thumbRadius * 2.0f);
    }
}

void VoxLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                        bool isSeparator, bool isActive, bool isHighlighted,
                                        bool isTicked, bool hasSubMenu, const juce::String& text,
                                        const juce::String& shortcutKeyText,
                                        const juce::Drawable* icon,
                                        const juce::Colour* textColourToUse)
{
    if (isSeparator)
    {
        g.setColour (tokens::colour::borderSubtle);
        g.fillRect (area.reduced (tokens::spacing::sm, 0).withHeight (1)
                        .withCentre (area.getCentre()));
        return;
    }

    auto item = area.reduced (tokens::spacing::xs, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (tokens::colour::accentDim.withAlpha (0.35f));
        g.fillRoundedRectangle (item.toFloat(), tokens::radius::small);
    }

    const auto textColour = textColourToUse != nullptr ? *textColourToUse
                                                        : (isActive ? tokens::colour::textSecondary
                                                                    : tokens::colour::textMuted);
    auto left = item.removeFromLeft (juce::jmin (item.getHeight(), 24));
    if (icon != nullptr)
    {
        icon->drawWithin (g, left.toFloat().reduced (3.0f), juce::RectanglePlacement::centred, 1.0f);
    }
    else if (isTicked)
    {
        juce::Path tick;
        const auto b = left.toFloat().reduced (5.0f);
        tick.startNewSubPath (b.getX(), b.getCentreY());
        tick.lineTo (b.getCentreX() - 1.0f, b.getBottom());
        tick.lineTo (b.getRight(), b.getY());
        g.setColour (tokens::colour::accent);
        g.strokePath (tick, juce::PathStrokeType (1.7f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    auto right = item.removeFromRight (hasSubMenu ? 22 : 0);
    g.setFont (typography::controlLabel());
    g.setColour (isHighlighted && isActive ? tokens::colour::text : textColour);
    g.drawText (text, item, juce::Justification::centredLeft, true);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont (typography::valueText());
        g.setColour (tokens::colour::textMuted);
        g.drawText (shortcutKeyText, item, juce::Justification::centredRight, true);
    }

    if (hasSubMenu)
    {
        juce::Path arrow;
        const auto b = right.toFloat().reduced (7.0f, 6.0f);
        arrow.startNewSubPath (b.getX(), b.getY());
        arrow.lineTo (b.getRight(), b.getCentreY());
        arrow.lineTo (b.getX(), b.getBottom());
        g.setColour (isActive ? tokens::colour::textSecondary : tokens::colour::textMuted);
        g.strokePath (arrow, juce::PathStrokeType (1.4f));
    }
}

} // namespace vox::ui
