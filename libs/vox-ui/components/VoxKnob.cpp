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
    label.setBounds (area.removeFromTop (14));
    area.removeFromBottom (14); // value row is painted by this component

    const auto diameter = juce::jmin (getKnobDiameter(),
                                      juce::jmin (area.getWidth(), area.getHeight()));
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
    const auto strokeW = juce::jmax (1.8f, radius * 0.12f);
    const auto arcRadius = juce::jmax (1.0f, radius - strokeW * 0.9f);

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
        g.setColour (tokens::colour::accentHover.withAlpha (enabled ? 0.55f : 0.22f));
        g.strokePath (modulation, juce::PathStrokeType (strokeW * 0.55f,
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

    const auto bodyRadius = radius * 0.62f;
    g.setColour (tokens::colour::background);
    g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                   bodyRadius * 2.0f, bodyRadius * 2.0f);
    g.setColour (tokens::colour::borderSubtle);
    g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                   bodyRadius * 2.0f, bodyRadius * 2.0f,
                   juce::jmax (1.0f, strokeW * 0.3f));

    const auto markerStart = centre + juce::Point<float> (0.0f, -bodyRadius * 0.25f)
                                        .rotatedAboutOrigin (angle);
    const auto markerEnd = centre + juce::Point<float> (0.0f, -bodyRadius * 0.92f)
                                      .rotatedAboutOrigin (angle);
    g.setColour (enabled ? tokens::colour::text : tokens::colour::textMuted);
    g.drawLine ({ markerStart, markerEnd }, juce::jmax (1.2f, strokeW * 0.45f));

    g.setColour (enabled ? tokens::colour::textSecondary : tokens::colour::textMuted);
    g.setFont (typography::valueText());
    g.drawText (slider.getTextFromValue (slider.getValue()),
                getLocalBounds().removeFromBottom (14),
                juce::Justification::centred, false);
}

} // namespace vox::ui
