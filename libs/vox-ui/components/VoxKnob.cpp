#include "VoxKnob.h"
#include "../Tokens.h"
#include "../Typography.h"
#include <cmath>

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
    label.setFont (typography::parameterLabel());
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
}

void VoxKnob::setLabel (juce::String text)
{
    label.setText (std::move (text), juce::dontSendNotification);
    repaint();
}

void VoxKnob::setLabelSmall (bool smallLabel)
{
    if (labelSmall == smallLabel)
        return;
    labelSmall = smallLabel;
    label.setFont (smallLabel ? typography::badge() : typography::parameterLabel());
    resized();
    repaint();
}

void VoxKnob::setCustomDiameter (int pixels)
{
    pixels = juce::jlimit (0, 200, pixels);
    if (customDiameter == pixels)
        return;
    customDiameter = pixels;
    resized();
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
    if (customDiameter > 0)
        return customDiameter;
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
    area.removeFromBottom (16); // value chip row is painted by this component

    const auto diameter = juce::jmin (getKnobDiameter(),
                                      juce::jmin (area.getWidth(), area.getHeight()));
    slider.setBounds (area.withSizeKeepingCentre (diameter, diameter));
}

void VoxKnob::paint (juce::Graphics& g)
{
    const auto knobBounds = slider.getBounds().toFloat();
    if (knobBounds.isEmpty())
        return;

    // Reference geometry is authored in a 64x64 viewBox (prototype
    // controls.tsx Knob): 13 ticks r29.7..31.2 (majors at both ends and
    // centre, r29), track/value arcs r25.9 stroke 2.4, cap r20.6 with a
    // vertical metal gradient, hairline ring r17.7, pointer r10..16.5, and
    // a dashed modulation ring r28.8. All radii scale with the rendered
    // diameter so every knob size keeps the reference proportions.
    const auto diameter = juce::jmin (knobBounds.getWidth(), knobBounds.getHeight());
    const auto k = diameter / 64.0f;
    const auto centre = knobBounds.getCentre();

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

    const auto polar = [&] (float radius, float a)
    {
        return centre + juce::Point<float> (std::cos (a) * radius, std::sin (a) * radius);
    };

    // Tick ring: 13 ticks across the 270-degree sweep.
    for (int n = 0; n < 13; ++n)
    {
        const auto tickAngle = startAngle + angleRange * (static_cast<float> (n) / 12.0f);
        const auto inner = (n % 6 == 0 ? 29.0f : 29.7f) * k;
        const auto outer = 31.2f * k;
        g.setColour (juce::Colour (0xff525965).withAlpha (enabled ? 1.0f : 0.35f));
        g.drawLine ({ polar (inner, tickAngle), polar (outer, tickAngle) },
                    juce::jmax (0.75f, 0.85f * k * 1.6f));
    }

    const auto arcRadius = juce::jmax (1.0f, 25.9f * k);
    const auto strokeW = juce::jmax (1.5f, 2.4f * k);
    const auto stroke = juce::PathStrokeType (strokeW, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff383f4a).withAlpha (enabled ? 1.0f : 0.35f));
    g.strokePath (track, stroke);

    // Value arc starts at the bipolar centre (normalized zero) when the
    // parameter is bipolar, matching the reference bipolar knob.
    const auto centreAngle = startAngle + angleRange * 0.5f;
    juce::Path valueArc;
    if (style == Style::Bipolar)
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                juce::jmin (centreAngle, angle),
                                juce::jmax (centreAngle, angle), true);
    else
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                startAngle, angle, true);
    g.setColour (enabled ? tokens::colour::themeAccent : tokens::colour::textMuted);
    g.strokePath (valueArc, stroke);

    // Dashed modulation ring (r28.8, dash 1.3 / gap 2.4 in view units).
    if (modulationAmount > 0.001f)
    {
        const auto modRadius = juce::jmax (1.0f, 28.8f * k);
        const auto mid = startAngle + angleRange * 0.5f;
        const auto modFrom = style == Style::Bipolar
            ? juce::jlimit (startAngle, endAngle, mid - modulationAmount * angleRange * 0.5f)
            : angle;
        const auto modTo = style == Style::Bipolar
            ? juce::jlimit (startAngle, endAngle, mid + modulationAmount * angleRange * 0.5f)
            : juce::jmin (endAngle, angle + modulationAmount * angleRange);
        const auto dash = juce::jmax (1.0f, 1.3f * k);
        const auto gap = juce::jmax (1.6f, 2.4f * k);
        const auto step = (dash + gap) / modRadius;
        g.setColour (enabled ? tokens::colour::themeAccentHover.withAlpha (0.55f)
                             : tokens::colour::themeAccentHover.withAlpha (0.22f));
        for (auto a = modFrom; a < modTo; a += step)
            g.drawLine ({ polar (modRadius, a), polar (modRadius, juce::jmin (modTo, a + dash)) },
                        juce::jmax (1.0f, 1.3f * k));
    }

    // Cap: dark base, vertical metal gradient, hairline ring, pointer.
    const auto capRadius = 20.6f * k;
    g.setColour (juce::Colour (0xff101116));
    g.fillEllipse (centre.x - capRadius, centre.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);

    const auto metalRadius = 20.1f * k;
    juce::ColourGradient metal (juce::Colour (0xff646a72), centre.x, centre.y - metalRadius,
                                juce::Colour (0xff222630), centre.x, centre.y + metalRadius, true);
    metal.addColour (0.43, juce::Colour (0xff373b43));
    g.setGradientFill (metal);
    g.fillEllipse (centre.x - metalRadius, centre.y - metalRadius,
                   metalRadius * 2.0f, metalRadius * 2.0f);

    const auto ringRadius = 17.7f * k;
    g.setColour (juce::Colour (0xffa6b0bb).withAlpha (0.12f));
    g.drawEllipse (centre.x - ringRadius, centre.y - ringRadius,
                   ringRadius * 2.0f, ringRadius * 2.0f, juce::jmax (1.0f, 1.2f * k));

    const auto marker = juce::Line<float> (polar (10.0f * k, angle), polar (16.5f * k, angle));
    g.setColour (enabled ? juce::Colour (0xfff5f2ed) : tokens::colour::textMuted);
    g.drawLine (marker, juce::jmax (1.4f, 2.2f * k));

    // Value chip below the dial (reference NumericValue well).
    const auto text = slider.getTextFromValue (slider.getValue());
    auto valueArea = getLocalBounds().removeFromBottom (16).toFloat();
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (typography::valueText(), text, 0.0f, 0.0f);
    const auto chipWidth = juce::jmin (valueArea.getWidth() - 2.0f,
                                       glyphs.getBoundingBox (0, -1, true).getWidth() + 14.0f);
    const auto chip = valueArea.withSizeKeepingCentre (juce::jmax (33.0f, chipWidth), 15.0f);
    g.setColour (tokens::colour::wellGroup);
    g.fillRoundedRectangle (chip, 4.0f);
    g.setColour (tokens::colour::borderSubtle);
    g.drawRoundedRectangle (chip.reduced (0.5f), 4.0f, 1.0f);
    g.setColour (enabled ? tokens::colour::textSecondary : tokens::colour::textMuted);
    g.setFont (typography::valueText());
    g.drawText (text, chip.toNearestInt(), juce::Justification::centred, false);
}

} // namespace vox::ui
