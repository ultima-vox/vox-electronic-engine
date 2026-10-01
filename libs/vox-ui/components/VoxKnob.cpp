#include "VoxKnob.h"
#include "../Tokens.h"
#include "../Typography.h"

namespace vox::ui {

VoxKnob::VoxKnob (juce::String labelText, Size size)
    : knobSize (size)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                juce::MathConstants<float>::pi * 2.25f,
                                true);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (180);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);

    label.setText (std::move (labelText), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, tokens::colour::textSecondary);
    label.setFont (typography::controlLabel());
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
}

void VoxKnob::setLabel (juce::String text)
{
    label.setText (std::move (text), juce::dontSendNotification);
    repaint();
}

void VoxKnob::setKnobSize (Size newSize)
{
    if (knobSize == newSize)
        return;
    knobSize = newSize;
    resized();
    repaint();
}

void VoxKnob::setStyle (Style newStyle)
{
    if (style == newStyle)
        return;
    style = newStyle;
    repaint();
}

void VoxKnob::setModulationAmount (float amount01)
{
    modulationAmount = juce::jlimit (0.0f, 1.0f, amount01);
    repaint();
}

void VoxKnob::setDoubleClickResetValue (double value)
{
    slider.setDoubleClickReturnValue (true, value);
}

int VoxKnob::getKnobDiameter() const noexcept
{
    switch (knobSize)
    {
        case Size::Small:  return tokens::size::knobSmall;
        case Size::Large:  return tokens::size::knobLarge;
        case Size::Normal: return tokens::size::knobNormal;
    }
    return tokens::size::knobNormal;
}

void VoxKnob::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom (18);

    // Small knobs placed into tall instrument panels are control strips beneath the
    // visual display, matching the accepted reference renders. Normal/large knobs
    // (notably the dedicated MACROS page) retain the centred composition.
    if (knobSize == Size::Small && area.getHeight() >= 86)
    {
        const auto stripHeight = juce::jmin (76, area.getHeight());
        auto strip = area.removeFromBottom (stripHeight);
        label.setBounds (strip.removeFromTop (16));

        const auto nominal = getKnobDiameter();
        const auto available = juce::jmin (strip.getWidth(), strip.getHeight());
        const auto desired = juce::jlimit (nominal, tokens::size::knobNormal,
                                           static_cast<int> (static_cast<float> (available) * 0.78f));
        const auto diameter = juce::jmin (desired, available);
        slider.setBounds (strip.withSizeKeepingCentre (diameter, diameter));
        return;
    }

    label.setBounds (area.removeFromTop (16));
    const auto nominal = getKnobDiameter();
    const auto responsiveCap = knobSize == Size::Small
        ? tokens::size::knobNormal
        : tokens::size::knobLarge;
    const auto available = juce::jmin (area.getWidth(), area.getHeight());
    const auto desired = juce::jlimit (nominal, responsiveCap,
                                       static_cast<int> (static_cast<float> (available) * 0.64f));
    const auto diameter = juce::jmin (desired, available);
    slider.setBounds (area.withSizeKeepingCentre (diameter, diameter));
}

void VoxKnob::paint (juce::Graphics& g)
{
    const auto knobBounds = slider.getBounds().toFloat();
    if (knobBounds.isEmpty())
        return;

    const auto diameter = juce::jmin (knobBounds.getWidth(), knobBounds.getHeight());
    const auto radius = diameter * 0.5f;
    const auto centre = knobBounds.getCentre();
    const auto strokeW = juce::jmax (1.7f, radius * 0.105f);
    const auto arcRadius = juce::jmax (1.0f, radius - strokeW * 1.05f);

    const auto params = slider.getRotaryParameters();
    const auto startAngle = params.startAngleRadians;
    const auto endAngle = params.endAngleRadians;
    const auto angleRange = endAngle - startAngle;

    const auto range = slider.getRange();
    const auto proportion = range.getLength() > 0.0
        ? juce::jlimit (0.0f, 1.0f,
                       static_cast<float> ((slider.getValue() - range.getStart()) / range.getLength()))
        : 0.0f;
    const auto angle = startAngle + proportion * angleRange;
    const auto enabled = slider.isEnabled() && isEnabled();

    g.setColour (tokens::colour::borderSubtle.withAlpha (0.72f));
    for (int tick = 0; tick <= 10; ++tick)
    {
        const auto a = startAngle + angleRange * static_cast<float> (tick) / 10.0f;
        const auto inner = centre + juce::Point<float> (0.0f, -(arcRadius + 2.0f)).rotatedAboutOrigin (a);
        const auto outer = centre + juce::Point<float> (0.0f, -(arcRadius + (tick % 5 == 0 ? 5.5f : 4.0f)))
                                              .rotatedAboutOrigin (a);
        g.drawLine ({ inner, outer }, tick % 5 == 0 ? 1.0f : 0.7f);
    }

    juce::Path inactive;
    inactive.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            startAngle, endAngle, true);
    g.setColour (tokens::colour::border);
    g.strokePath (inactive, juce::PathStrokeType (strokeW,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    if (modulationAmount > 0.001f)
    {
        juce::Path modulation;
        if (style == Style::Bipolar)
        {
            const auto mid = startAngle + angleRange * 0.5f;
            const auto half = modulationAmount * angleRange * 0.5f;
            modulation.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                      juce::jmax (startAngle, mid - half),
                                      juce::jmin (endAngle, mid + half), true);
        }
        else
        {
            modulation.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                      angle,
                                      juce::jmin (endAngle, angle + modulationAmount * angleRange),
                                      true);
        }
        g.setColour (tokens::colour::accentHover.withAlpha (enabled ? 0.42f : 0.18f));
        g.strokePath (modulation, juce::PathStrokeType (juce::jmax (1.0f, strokeW * 0.5f),
                                                        juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
    }

    juce::Path active;
    active.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          startAngle, angle, true);
    g.setColour (enabled ? tokens::colour::accent : tokens::colour::textMuted);
    g.strokePath (active, juce::PathStrokeType (strokeW,
                                                juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    const auto bodyRadius = radius * 0.60f;
    juce::ColourGradient bodyGradient (tokens::colour::panelRaised.brighter (0.08f),
                                       centre.x, centre.y - bodyRadius,
                                       tokens::colour::background,
                                       centre.x, centre.y + bodyRadius, false);
    bodyGradient.addColour (0.48, tokens::colour::control);
    g.setGradientFill (bodyGradient);
    g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                   bodyRadius * 2.0f, bodyRadius * 2.0f);

    g.setColour (tokens::colour::borderSubtle);
    g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                   bodyRadius * 2.0f, bodyRadius * 2.0f,
                   juce::jmax (1.0f, strokeW * 0.30f));
    g.setColour (tokens::colour::border.withAlpha (0.44f));
    g.drawEllipse (centre.x - bodyRadius * 0.78f, centre.y - bodyRadius * 0.78f,
                   bodyRadius * 1.56f, bodyRadius * 1.56f, 1.0f);

    const auto markerStart = centre + juce::Point<float> (0.0f, -bodyRadius * 0.28f)
                                        .rotatedAboutOrigin (angle);
    const auto markerEnd = centre + juce::Point<float> (0.0f, -bodyRadius * 0.88f)
                                      .rotatedAboutOrigin (angle);
    g.setColour (enabled ? tokens::colour::text : tokens::colour::textMuted);
    g.drawLine ({ markerStart, markerEnd }, juce::jmax (1.2f, strokeW * 0.40f));

    const auto valueBounds = getLocalBounds().removeFromBottom (17).reduced (6, 0);
    const auto valueText = slider.getTextFromValue (slider.getValue());
    g.setColour (tokens::colour::background.withAlpha (0.64f));
    const auto pill = valueBounds.withSizeKeepingCentre (juce::jmin (valueBounds.getWidth(), 68), 16).toFloat();
    g.fillRoundedRectangle (pill, 3.0f);
    g.setColour (tokens::colour::borderSubtle);
    g.drawRoundedRectangle (pill, 3.0f, 1.0f);
    g.setColour (enabled ? tokens::colour::textSecondary : tokens::colour::textMuted);
    g.setFont (typography::valueText());
    g.drawText (valueText, valueBounds, juce::Justification::centred, false);
}

} // namespace vox::ui
