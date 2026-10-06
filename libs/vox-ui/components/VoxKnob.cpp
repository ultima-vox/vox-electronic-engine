#include "VoxKnob.h"
#include "../Tokens.h"
#include "../Typography.h"

namespace vox::ui {

namespace {

// The accepted render stacks the control as knob / label / value: the caption
// sits directly under the knob and the formatted value under the caption. Both
// bands are always reserved, so a short cell degrades by shrinking the knob
// rather than by letting text drift over the control.
constexpr int labelBandHeight = 16;
constexpr int valueBandHeight = 14;

// Measured from the accepted render (arc ring outer diameter ~52 px including
// the housing ring, ~4.5 px track weight, body at ~0.74 of the arc radius,
// pointer from ~0.26 to 1.0 of the arc radius).
constexpr float trackWeightFraction = 0.088f;
constexpr float bodyRadiusFraction = 0.74f;
constexpr float pointerInnerFraction = 0.26f;
constexpr float pointerOuterFraction = 1.0f;
constexpr float pointerWeightFraction = 0.045f;
constexpr float pointerToTextMix = 0.82f;

} // namespace

VoxKnob::VoxKnob (juce::String labelText, Size size)
    : knobSize (size)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    // The accepted render has a gap at the bottom of the track, which is the
    // JUCE rotary default span (1.2 pi to 2.8 pi measured clockwise from 12).
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f,
                                true);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (180);
    slider.onValueChange = [this] { repaint(); };

    // Documented accent hook. Product code overrides this per instrument
    // identity; the default is the shared interaction accent.
    setColour (juce::Slider::rotarySliderFillColourId, tokens::colour::accent);

    addAndMakeVisible (slider);

    label.setText (std::move (labelText), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, tokens::colour::text);
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
    valueBounds = {};

    const auto nominal = getKnobDiameter();
    const auto responsiveCap = knobSize == Size::Small ? tokens::size::knobNormal
                                                       : tokens::size::knobLarge;
    const auto bandHeight = labelBandHeight + valueBandHeight;

    if (area.getWidth() <= 0 || area.getHeight() <= 0)
    {
        label.setBounds ({});
        slider.setBounds ({});
        return;
    }

    const auto knobSpace = juce::jmax (0, area.getHeight() - bandHeight);
    const auto available = juce::jmin (area.getWidth(), knobSpace);
    const auto scaled = static_cast<int> (static_cast<float> (available) * 0.88f);
    const auto diameter = available <= 0
        ? 0
        : juce::jmin (available, juce::jlimit (nominal, responsiveCap, scaled));

    // The caption/value bands always travel with the knob, so a tall cell centres
    // one control block instead of stranding the caption at the cell edge. No
    // page-specific placement rule lives here: callers that want a control strip
    // size the component and this stays centred inside it.
    const auto blockHeight = juce::jmin (area.getHeight(), diameter + bandHeight);
    auto block = area.withHeight (blockHeight)
                     .withY (area.getCentreY() - blockHeight / 2);

    valueBounds = block.removeFromBottom (valueBandHeight);
    label.setBounds (block.removeFromBottom (labelBandHeight));

    slider.setBounds (block.withSizeKeepingCentre (
        juce::jmin (diameter, block.getWidth()),
        juce::jmin (diameter, block.getHeight())));
}

void VoxKnob::paint (juce::Graphics& g)
{
    const auto knobBounds = slider.getBounds().toFloat();

    const auto accent = findColour (juce::Slider::rotarySliderFillColourId);
    const auto enabled = slider.isEnabled() && isEnabled();

    if (! knobBounds.isEmpty())
    {
        const auto diameter = juce::jmin (knobBounds.getWidth(), knobBounds.getHeight());
        const auto radius = diameter * 0.5f;
        const auto centre = knobBounds.getCentre();
        const auto trackWeight = juce::jmax (2.4f, diameter * trackWeightFraction);
        const auto arcRadius = juce::jmax (1.0f, radius - trackWeight * 0.5f);

        const auto params = slider.getRotaryParameters();
        const auto startAngle = params.startAngleRadians;
        const auto angleRange = params.endAngleRadians - params.startAngleRadians;

        const auto range = slider.getRange();
        const auto proportion = range.getLength() > 0.0
            ? juce::jlimit (0.0f, 1.0f,
                            static_cast<float> ((slider.getValue() - range.getStart())
                                                / range.getLength()))
            : 0.0f;
        const auto angle = startAngle + proportion * angleRange;

        const auto stroke = juce::PathStrokeType (trackWeight,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::butt);

        // 4. outer housing ring: the shallow lip the track is cut into.
        const auto housingRadius = arcRadius + trackWeight;
        g.setColour (tokens::colour::borderSubtle.withAlpha (enabled ? 0.55f : 0.25f));
        g.drawEllipse (juce::Rectangle<float> (housingRadius * 2.0f, housingRadius * 2.0f)
                           .withCentre (centre),
                       1.0f);

        // 1. modulation range, drawn under the value track so it reads as a range
        //    rather than as a second value.
        if (modulationAmount > 0.001f)
        {
            juce::Path modulation;
            if (style == Style::Bipolar)
            {
                const auto mid = startAngle + angleRange * 0.5f;
                const auto half = modulationAmount * angleRange * 0.5f;
                modulation.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                          juce::jmax (startAngle, mid - half),
                                          juce::jmin (startAngle + angleRange, mid + half),
                                          true);
            }
            else
            {
                modulation.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                          angle,
                                          juce::jmin (startAngle + angleRange,
                                                      angle + modulationAmount * angleRange),
                                          true);
            }

            g.setColour (accent.withAlpha (enabled ? 0.34f : 0.16f));
            g.strokePath (modulation, juce::PathStrokeType (trackWeight * 1.6f,
                                                            juce::PathStrokeType::curved,
                                                            juce::PathStrokeType::butt));
        }

        // 2. inactive value track.
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             startAngle, startAngle + angleRange, true);
        g.setColour (tokens::colour::border);
        g.strokePath (track, stroke);

        // 3. active value track.
        if (proportion > 0.0f)
        {
            juce::Path active;
            active.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                  startAngle, angle, true);
            g.setColour (enabled ? accent : tokens::colour::textMuted);
            g.strokePath (active, stroke);
        }

        // 5. recessed body, lit from the rim inwards: the accepted render is
        //    darkest at the centre.
        const auto bodyRadius = arcRadius * bodyRadiusFraction;
        const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f)
                              .withCentre (centre);
        juce::ColourGradient bodyFill (tokens::colour::background.darker (0.55f),
                                       centre.x, centre.y,
                                       tokens::colour::panelRaised,
                                       centre.x + bodyRadius, centre.y, true);
        g.setGradientFill (bodyFill);
        g.fillEllipse (body);

        // 6. body shading: rim, inner shadow and the small machined centre pin
        //    visible in the accepted render.
        g.setColour (tokens::colour::borderSubtle.withAlpha (0.9f));
        g.drawEllipse (body, 1.0f);
        g.setColour (tokens::colour::background.withAlpha (0.5f));
        g.drawEllipse (body.reduced (bodyRadius * 0.16f), 1.0f);

        const auto pinRadius = juce::jmax (1.4f, bodyRadius * 0.15f);
        const auto pin = juce::Rectangle<float> (pinRadius * 2.0f, pinRadius * 2.0f)
                             .withCentre (centre);
        g.setColour (tokens::colour::background);
        g.fillEllipse (pin);
        g.setColour (tokens::colour::borderSubtle.withAlpha (0.85f));
        g.drawEllipse (pin, 1.0f);

        // 7. position indicator: one bright pointer from near the centre out to
        //    the track, tinted towards the text colour exactly like the render.
        const auto pointerColour = enabled
            ? accent.interpolatedWith (tokens::colour::text, pointerToTextMix)
            : tokens::colour::textMuted;
        const auto pointerStart = centre
            + juce::Point<float> (0.0f, -arcRadius * pointerInnerFraction)
                  .rotatedAboutOrigin (angle);
        const auto pointerEnd = centre
            + juce::Point<float> (0.0f, -arcRadius * pointerOuterFraction)
                  .rotatedAboutOrigin (angle);

        g.setColour (pointerColour);
        g.drawLine ({ pointerStart, pointerEnd },
                    juce::jmax (2.0f, diameter * pointerWeightFraction));

        // 8. focus state on its own ring so it never overloads the value colour.
        if (hasKeyboardFocus (true))
        {
            g.setColour (accent.withAlpha (0.65f));
            g.drawEllipse (juce::Rectangle<float> ((housingRadius + 2.0f) * 2.0f,
                                                   (housingRadius + 2.0f) * 2.0f)
                               .withCentre (centre),
                           1.0f);
        }
    }

    // 10. formatted value. Measured from the accepted render at ~#F3F8FF, i.e.
    //     the primary text colour rather than a second accent.
    if (valueBounds.getHeight() >= 8)
    {
        g.setColour (enabled ? tokens::colour::text : tokens::colour::textMuted);
        g.setFont (typography::valueText());
        g.drawText (slider.getTextFromValue (slider.getValue()), valueBounds,
                    juce::Justification::centred, false);
    }
}

} // namespace vox::ui
