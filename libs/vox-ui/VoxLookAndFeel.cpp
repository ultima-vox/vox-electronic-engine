#include "VoxLookAndFeel.h"
#include "Tokens.h"
#include "Typography.h"
#include "VoxPaint.h"

namespace vox::ui {
namespace {
using namespace tokens;

juce::Colour withEnabled (juce::Colour c, bool enabled)
{
    return enabled ? c : c.withMultipliedAlpha (0.35f);
}

// Shared look of every clickable surface: off = recessed control, on = the
// VOX cyan "active tab" fill. Focus is drawn as a cyan outline so the state
// never relies on fill colour alone.
void drawSurface (juce::Graphics& g, juce::Rectangle<float> bounds, bool on,
                  bool hover, bool down, bool focused, bool enabled)
{
    bounds = bounds.reduced (0.5f);
    if (on)
    {
        g.setGradientFill (juce::ColourGradient (colour::accent.withAlpha (0.92f),
                                                 bounds.getX(), bounds.getY(),
                                                 colour::accentDim,
                                                 bounds.getX(), bounds.getBottom(), false));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (colour::panelRaised,
                                                 bounds.getX(), bounds.getY(),
                                                 colour::control,
                                                 bounds.getX(), bounds.getBottom(), false));
    }
    if (down)
        g.setOpacity (0.8f);
    if (! enabled)
        g.setOpacity (0.35f);
    g.fillRoundedRectangle (bounds, radius::small);
    g.setOpacity (1.0f);
    auto border = on ? colour::accentHover
                     : (hover ? colour::accentDim : colour::border);
    if (focused)
        border = colour::accent;
    g.setColour (withEnabled (border, enabled));
    g.drawRoundedRectangle (bounds, radius::small, focused ? 1.4f : 1.0f);
}

class SliderStepButton final : public juce::Button {
public:
    explicit SliderStepButton (bool increment)
        : juce::Button (increment ? "+" : "-"), up (increment) {}
    void paintButton (juce::Graphics& g, bool hover, bool down) override
    {
        drawSurface (g, getLocalBounds().toFloat(), false, hover, down, false, isEnabled());
        const auto c = getLocalBounds().toFloat().getCentre();
        g.setColour (withEnabled (hover ? colour::text : colour::textSecondary, isEnabled()));
        g.drawLine (c.x - 3.5f, c.y, c.x + 3.5f, c.y, 1.4f);
        if (up)
            g.drawLine (c.x, c.y - 3.5f, c.x, c.y + 3.5f, 1.4f);
    }

private:
    bool up;
};
} // namespace

VoxLookAndFeel::VoxLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colour::background);
    setColour (juce::DocumentWindow::backgroundColourId, colour::background);
    setColour (juce::Label::textColourId, colour::text);
    setColour (juce::Label::outlineWhenEditingColourId, colour::accent);
    setColour (juce::ComboBox::textColourId, colour::text);
    setColour (juce::ComboBox::backgroundColourId, colour::control);
    setColour (juce::ComboBox::outlineColourId, colour::border);
    setColour (juce::ComboBox::arrowColourId, colour::textSecondary);
    setColour (juce::TextButton::buttonColourId, colour::control);
    setColour (juce::TextButton::buttonOnColourId, colour::accent);
    setColour (juce::TextButton::textColourOffId, colour::textSecondary);
    setColour (juce::TextButton::textColourOnId, colour::text);
    setColour (juce::ToggleButton::textColourId, colour::textSecondary);
    setColour (juce::ToggleButton::tickColourId, colour::accent);
    setColour (juce::PopupMenu::backgroundColourId, colour::panel);
    setColour (juce::PopupMenu::textColourId, colour::textSecondary);
    setColour (juce::PopupMenu::headerTextColourId, colour::textSecondary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, colour::text);
    setColour (juce::Slider::textBoxTextColourId, colour::text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colour::accentDim);
    setColour (juce::Slider::rotarySliderFillColourId, colour::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, colour::borderSubtle);
    setColour (juce::Slider::trackColourId, colour::accent);
    setColour (juce::Slider::backgroundColourId, colour::graph);
    setColour (juce::TextEditor::backgroundColourId, colour::control);
    setColour (juce::TextEditor::textColourId, colour::text);
    setColour (juce::TextEditor::highlightColourId, colour::accentDim);
    setColour (juce::TextEditor::highlightedTextColourId, colour::text);
    setColour (juce::TextEditor::outlineColourId, colour::border);
    setColour (juce::TextEditor::focusedOutlineColourId, colour::accent);
    setColour (juce::CaretComponent::caretColourId, colour::accent);
    setColour (juce::ScrollBar::thumbColourId, colour::accentDim);
    setColour (juce::ScrollBar::trackColourId, colour::graph);
    setColour (juce::TooltipWindow::backgroundColourId, colour::panelRaised);
    setColour (juce::TooltipWindow::textColourId, colour::text);
    setColour (juce::TooltipWindow::outlineColourId, colour::border);
    setColour (juce::AlertWindow::backgroundColourId, colour::panel);
    setColour (juce::AlertWindow::textColourId, colour::text);
    setColour (juce::AlertWindow::outlineColourId, colour::border);

    getCurrentColourScheme().setUIColour (ColourScheme::windowBackground, colour::background);
    getCurrentColourScheme().setUIColour (ColourScheme::widgetBackground, colour::control);
    getCurrentColourScheme().setUIColour (ColourScheme::menuBackground, colour::panel);
    getCurrentColourScheme().setUIColour (ColourScheme::outline, colour::border);
    getCurrentColourScheme().setUIColour (ColourScheme::defaultText, colour::text);
    getCurrentColourScheme().setUIColour (ColourScheme::defaultFill, colour::accent);
    getCurrentColourScheme().setUIColour (ColourScheme::highlightedText, colour::text);
    getCurrentColourScheme().setUIColour (ColourScheme::highlightedFill, colour::accentDim);
    getCurrentColourScheme().setUIColour (ColourScheme::menuText, colour::textSecondary);
}

void VoxLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                           const juce::Colour&, bool isMouseOverButton,
                                           bool isButtonDown)
{
    drawSurface (g, button.getLocalBounds().toFloat(), button.getToggleState(),
                 isMouseOverButton, isButtonDown, button.hasKeyboardFocus (true),
                 button.isEnabled());
}

juce::Font VoxLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return paint::spaced (juce::jlimit (10.0f, 12.5f, buttonHeight * 0.42f), 0.04f, true);
}

void VoxLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                     bool isMouseOverButton, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    auto colour = button.getToggleState() ? colour::text
                                          : (isMouseOverButton ? colour::text
                                                               : colour::textSecondary);
    g.setColour (withEnabled (colour, button.isEnabled()));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0),
                      juce::Justification::centred, 1, 0.8f);
}

void VoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float position, float start, float end,
                                       juce::Slider& slider)
{
    const bool enabled = slider.isEnabled();
    const auto size = static_cast<float> (juce::jmin (width, height));
    const auto c = juce::Point<float> (static_cast<float> (x) + width * 0.5f,
                                       static_cast<float> (y) + height * 0.5f);
    const auto outer = size * 0.5f - 1.0f;
    const auto stroke = juce::jlimit (2.0f, 4.0f, outer * 0.13f);
    const auto arcR = outer - stroke * 0.5f;
    const auto angle = start + position * (end - start);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (colour::borderSubtle);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    const bool bipolar = static_cast<bool> (slider.getProperties()["vox.bipolar"]);
    const auto from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle),
                             juce::jmax (from, angle), true);
        const auto accent = enabled ? colour::accent : colour::textMuted;
        g.setColour (accent.withAlpha (0.18f));
        g.strokePath (value, juce::PathStrokeType (stroke * 2.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // Recessed body with a thin dark-metal rim.
    const auto bodyR = arcR - stroke * 1.25f;
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b4152), c.x, body.getY(),
                                             juce::Colour (0xff0a1520), c.x, body.getBottom(),
                                             false));
    g.fillEllipse (body);
    const auto inner = body.reduced (juce::jmax (1.2f, bodyR * 0.1f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff122433), c.x, inner.getY(),
                                             juce::Colour (0xff050c13), c.x, inner.getBottom(),
                                             false));
    g.fillEllipse (inner);

    const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
    g.setColour (enabled ? colour::text : colour::textMuted);
    g.drawLine ({ c + dir * (bodyR * 0.3f), c + dir * (bodyR * 0.88f) },
                juce::jmax (1.4f, stroke * 0.55f));

    if (slider.hasKeyboardFocus (false))
    {
        g.setColour (colour::accent.withAlpha (0.6f));
        g.drawEllipse (juce::Rectangle<float> (outer * 2.0f, outer * 2.0f).withCentre (c), 1.0f);
    }
}

void VoxLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool down,
                                   int, int, int, int, juce::ComboBox& box)
{
    drawSurface (g, { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) },
                 false, box.isMouseOver (true), down, box.hasKeyboardFocus (true),
                 box.isEnabled());
    const auto cx = static_cast<float> (width) - 13.0f;
    const auto cy = static_cast<float> (height) * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (withEnabled (colour::textSecondary, box.isEnabled()));
    g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

juce::Font VoxLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return typography::makeFont (juce::jlimit (10.5f, 13.0f, box.getHeight() * 0.46f));
}

void VoxLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void VoxLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                       bool isMouseOverButton, bool isButtonDown)
{
    // Text toggles render as segment pills (PRE/POST, Mute, Solo ...); an
    // empty toggle renders as a compact switch.
    const bool on = button.getToggleState();
    const bool enabled = button.isEnabled();
    if (button.getButtonText().isEmpty())
    {
        auto track = button.getLocalBounds().toFloat()
                         .withSizeKeepingCentre (26.0f, 14.0f);
        g.setColour (withEnabled (on ? colour::accentDim : colour::graph, enabled));
        g.fillRoundedRectangle (track, 7.0f);
        g.setColour (withEnabled (on ? colour::accent : colour::border, enabled));
        g.drawRoundedRectangle (track, 7.0f, 1.0f);
        const auto knob = juce::Rectangle<float> (10.0f, 10.0f)
                              .withCentre ({ on ? track.getRight() - 7.0f : track.getX() + 7.0f,
                                             track.getCentreY() });
        g.setColour (withEnabled (on ? colour::text : colour::textSecondary, enabled));
        g.fillEllipse (knob);
        return;
    }

    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f, 2.0f);
    g.setColour (withEnabled (on ? colour::accent.withAlpha (0.22f) : colour::control, enabled));
    if (isButtonDown)
        g.setOpacity (0.8f);
    g.fillRoundedRectangle (bounds, radius::small);
    auto border = on ? colour::accent : (isMouseOverButton ? colour::accentDim : colour::border);
    if (button.hasKeyboardFocus (true))
        border = colour::accentHover;
    g.setColour (withEnabled (border, enabled));
    g.drawRoundedRectangle (bounds, radius::small, 1.0f);
    g.setColour (withEnabled (on ? colour::text : colour::textSecondary, enabled));
    g.setFont (paint::spaced (juce::jlimit (9.0f, 11.5f, bounds.getHeight() * 0.48f), 0.06f, true));
    g.drawFittedText (button.getButtonText().toUpperCase(), bounds.toNearestInt().reduced (3, 0),
                      juce::Justification::centred, 1, 0.75f);
}

void VoxLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float minSliderPos, float maxSliderPos,
                                       juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool enabled = slider.isEnabled();
    const bool vertical = style == juce::Slider::LinearVertical
                       || style == juce::Slider::LinearBarVertical;
    const auto accent = enabled ? colour::accent : colour::textMuted;
    if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical)
    {
        // Value fields (seed, counts) show only their number; bars show the
        // proportion as a restrained fill behind the value text.
        if (static_cast<bool> (slider.getProperties()["vox.valueField"]))
            return;
        auto bounds = slider.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (colour::graph);
        g.fillRoundedRectangle (bounds, radius::small);
        auto fill = vertical ? bounds.withTop (sliderPos) : bounds.withRight (sliderPos);
        g.setColour (accent.withAlpha (0.35f));
        g.fillRoundedRectangle (fill, radius::small);
        g.setColour (colour::border);
        g.drawRoundedRectangle (bounds, radius::small, 1.0f);
        juce::ignoreUnused (x, y, width, height, minSliderPos, maxSliderPos);
        return;
    }
    const auto area = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                              static_cast<float> (width),
                                              static_cast<float> (height));
    const auto groove = 5.0f;
    juce::Rectangle<float> track, fill;
    juce::Point<float> thumb;
    if (vertical)
    {
        track = { area.getCentreX() - groove * 0.5f, maxSliderPos, groove,
                  minSliderPos - maxSliderPos };
        fill = track.withTop (sliderPos);
        thumb = { area.getCentreX(), sliderPos };
    }
    else
    {
        track = { minSliderPos, area.getCentreY() - groove * 0.5f,
                  maxSliderPos - minSliderPos, groove };
        fill = track.withRight (sliderPos);
        thumb = { sliderPos, area.getCentreY() };
    }
    g.setColour (colour::graph);
    g.fillRoundedRectangle (track, groove * 0.5f);
    g.setColour (colour::borderSubtle);
    g.drawRoundedRectangle (track, groove * 0.5f, 1.0f);
    g.setGradientFill (vertical
        ? juce::ColourGradient (accent, fill.getX(), fill.getY(), accent.withAlpha (0.55f),
                                fill.getX(), fill.getBottom(), false)
        : juce::ColourGradient (accent.withAlpha (0.55f), fill.getX(), fill.getY(), accent,
                                fill.getRight(), fill.getY(), false));
    g.fillRoundedRectangle (fill, groove * 0.5f);

    const auto r = 5.5f;
    g.setColour (juce::Colour (0xff0a1520));
    g.fillEllipse (thumb.x - r, thumb.y - r, r * 2.0f, r * 2.0f);
    g.setColour (slider.hasKeyboardFocus (false) ? colour::accentHover : accent);
    g.drawEllipse (thumb.x - r, thumb.y - r, r * 2.0f, r * 2.0f, 1.5f);
    g.setColour (enabled ? colour::text : colour::textMuted);
    g.fillEllipse (thumb.x - 1.6f, thumb.y - 1.6f, 3.2f, 3.2f);
}

juce::Label* VoxLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (typography::valueText().withExtraKerningFactor (0.02f));
    label->setJustificationType (juce::Justification::centred);
    label->setColour (juce::Label::textColourId, colour::text);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::TextEditor::backgroundColourId, colour::control);
    return label;
}

juce::Button* VoxLookAndFeel::createSliderButton (juce::Slider&, bool isIncrement)
{
    return new SliderStepButton (isIncrement);
}

void VoxLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // Slider value boxes are VOX value fields: recessed, rounded, no outline
    // colour taken from the generic label scheme.
    if (auto* slider = dynamic_cast<juce::Slider*> (label.getParentComponent());
        slider != nullptr && ! slider->getProperties().getWithDefault ("vox.valueField", false))
    {
        // A bar slider's value box overlays the bar: text only.
        if (slider->getSliderStyle() == juce::Slider::LinearBar
            || slider->getSliderStyle() == juce::Slider::LinearBarVertical)
        {
            if (! label.isBeingEdited())
            {
                g.setColour (colour::text.withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.45f));
                g.setFont (getLabelFont (label));
                g.drawFittedText (label.getText(), label.getLocalBounds().reduced (3, 0),
                                  juce::Justification::centred, 1, 0.8f);
            }
            return;
        }
        const auto field = label.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (colour::graph);
        g.fillRoundedRectangle (field, radius::small);
        g.setColour (label.hasKeyboardFocus (true) ? colour::accentDim : colour::borderSubtle);
        g.drawRoundedRectangle (field, radius::small, 1.0f);
        if (! label.isBeingEdited())
        {
            g.setColour (colour::text.withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.45f));
            g.setFont (getLabelFont (label));
            g.drawFittedText (label.getText(), label.getLocalBounds().reduced (3, 0),
                              juce::Justification::centred, 1, 0.8f);
        }
        return;
    }
    g.fillAll (label.findColour (juce::Label::backgroundColourId));
    if (label.isBeingEdited())
        return;
    const auto alpha = label.isEnabled() ? 1.0f : 0.45f;
    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
    g.setFont (getLabelFont (label));
    const auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
    g.drawFittedText (label.getText(), area, label.getJustificationType(),
                      juce::jmax (1, static_cast<int> (static_cast<float> (area.getHeight())
                                                        / getLabelFont (label).getHeight())),
                      label.getMinimumHorizontalScale());
    g.setColour (label.findColour (juce::Label::outlineColourId).withMultipliedAlpha (alpha));
    g.drawRect (label.getLocalBounds());
}

void VoxLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                               juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0.0f, 0.0f, static_cast<float> (width),
                            static_cast<float> (height), radius::small);
}

void VoxLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                            juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? colour::accent : colour::border);
    g.drawRoundedRectangle (0.5f, 0.5f, static_cast<float> (width) - 1.0f,
                            static_cast<float> (height) - 1.0f, radius::small, 1.0f);
}

void VoxLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (colour::panel);
    g.setColour (colour::border);
    g.drawRect (0, 0, width, height, 1);
}

juce::Font VoxLookAndFeel::getPopupMenuFont()
{
    return typography::makeFont (12.5f);
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
        g.setColour (colour::borderSubtle);
        g.fillRect (area.reduced (spacing::sm, 0).withHeight (1).withCentre (area.getCentre()));
        return;
    }

    auto item = area.reduced (spacing::xs, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (colour::accent.withAlpha (0.16f));
        g.fillRoundedRectangle (item.toFloat(), radius::small);
        g.setColour (colour::accentDim);
        g.drawRoundedRectangle (item.toFloat().reduced (0.5f), radius::small, 1.0f);
    }

    const auto textColour = textColourToUse != nullptr ? *textColourToUse
                                                       : (isActive ? colour::textSecondary
                                                                   : colour::textMuted);
    auto left = item.removeFromLeft (juce::jmin (item.getHeight(), 24));
    if (icon != nullptr)
    {
        icon->drawWithin (g, left.toFloat().reduced (3.0f), juce::RectanglePlacement::centred, 1.0f);
    }
    else if (isTicked)
    {
        g.setColour (colour::accent);
        g.fillEllipse (left.toFloat().withSizeKeepingCentre (6.0f, 6.0f));
    }

    auto right = item.removeFromRight (hasSubMenu ? 22 : 0);
    g.setFont (getPopupMenuFont());
    g.setColour (isHighlighted && isActive ? colour::text : textColour);
    g.drawText (text, item, juce::Justification::centredLeft, true);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont (typography::valueText());
        g.setColour (colour::textMuted);
        g.drawText (shortcutKeyText, item, juce::Justification::centredRight, true);
    }

    if (hasSubMenu)
    {
        juce::Path arrow;
        const auto b = right.toFloat().reduced (8.0f, 7.0f);
        arrow.startNewSubPath (b.getX(), b.getY());
        arrow.lineTo (b.getRight(), b.getCentreY());
        arrow.lineTo (b.getX(), b.getBottom());
        g.setColour (isActive ? colour::textSecondary : colour::textMuted);
        g.strokePath (arrow, juce::PathStrokeType (1.4f));
    }
}

void VoxLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y,
                                    int width, int height, bool vertical,
                                    int thumbStart, int thumbSize, bool isMouseOver,
                                    bool isMouseDown)
{
    const auto track = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    g.setColour (colour::graph);
    g.fillRoundedRectangle (track, 3.0f);
    const auto thumb = vertical
        ? juce::Rectangle<float> (track.getX(), static_cast<float> (thumbStart), track.getWidth(),
                                  static_cast<float> (thumbSize))
        : juce::Rectangle<float> (static_cast<float> (thumbStart), track.getY(),
                                  static_cast<float> (thumbSize), track.getHeight());
    g.setColour (isMouseDown ? colour::accent : (isMouseOver ? colour::accentDim.brighter (0.2f)
                                                             : colour::accentDim));
    g.fillRoundedRectangle (thumb.reduced (vertical ? 0.0f : 1.0f, vertical ? 1.0f : 0.0f), 3.0f);
}

juce::Rectangle<int> VoxLookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                       juce::Point<int> screenPos,
                                                       juce::Rectangle<int> parentArea)
{
    juce::AttributedString text;
    text.append (tipText, typography::controlLabel(), colour::text);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (text, 320.0f);
    const auto w = static_cast<int> (layout.getWidth() + 20.0f);
    const auto h = static_cast<int> (layout.getHeight() + 12.0f);
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12)
                                                                       : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6)
                                                                       : screenPos.y + 6,
                                 w, h)
        .constrainedWithin (parentArea);
}

void VoxLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text,
                                  int width, int height)
{
    const auto bounds = juce::Rectangle<float> (static_cast<float> (width),
                                                static_cast<float> (height));
    g.setColour (colour::panelRaised);
    g.fillRoundedRectangle (bounds, radius::small);
    g.setColour (colour::accentDim);
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius::small, 1.0f);
    juce::AttributedString s;
    s.append (text, typography::controlLabel(), colour::text);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 320.0f);
    layout.draw (g, bounds.reduced (10.0f, 6.0f));
}

void VoxLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert,
                                   const juce::Rectangle<int>& textArea,
                                   juce::TextLayout& layout)
{
    const auto bounds = alert.getLocalBounds().toFloat();
    paint::panel (g, bounds, true);
    g.setColour (colour::accent);
    g.fillRect (bounds.withHeight (2.0f).reduced (1.0f, 0.0f));
    layout.draw (g, textArea.toFloat());
}

juce::Font VoxLookAndFeel::getAlertWindowTitleFont()
{
    return paint::spaced (15.0f, 0.04f, true);
}

juce::Font VoxLookAndFeel::getAlertWindowMessageFont()
{
    return typography::makeFont (13.0f);
}

void VoxLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver,
                                        bool isMouseDragging)
{
    g.setColour (isMouseOver || isMouseDragging ? colour::accent : colour::accentDim);
    for (int i = 1; i <= 3; ++i)
    {
        const auto d = static_cast<float> (i) * 4.0f;
        g.drawLine (static_cast<float> (w) - d, static_cast<float> (h) - 1.0f,
                    static_cast<float> (w) - 1.0f, static_cast<float> (h) - d, 1.0f);
    }
}

} // namespace vox::ui
