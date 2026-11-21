#pragma once

// Drums-local #135 material treatment. Reuses the family input, formatting,
// accessibility and attachment surface without changing libs/vox-ui's API.
#include "VoxComponents.h"
#include "Tokens.h"

namespace vox::drums {
class DrumsDial final : public vox::ui::VoxKnob {
public:
    explicit DrumsDial (juce::String text = {}, Size size = Size::Normal)
        : VoxKnob (std::move (text), size), primary_ (size != Size::Small)
    {
        getSlider().addMouseListener (this, false);
    }
    ~DrumsDial() override { getSlider().removeMouseListener (this); }
    void setStyle (Style nextStyle) { bipolar_ = nextStyle == Style::Bipolar; VoxKnob::setStyle (nextStyle); }
    void resized() override
    {
        VoxKnob::resized();
        const auto area = getLocalBounds().withTrimmedTop (14).withTrimmedBottom (14);
        const int available = juce::jmin (area.getWidth(), area.getHeight());
        const int requested = primary_ && available >= 52 ? 52 : 34;
        getSlider().setBounds (area.withSizeKeepingCentre (juce::jmin (requested, available), juce::jmin (requested, available)));
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent&) override { repaint(); }
    void paint (juce::Graphics& g) override
    {
        namespace c = vox::ui::tokens::colour;
        auto& input = getSlider();
        const auto r = input.getBounds().toFloat();
        if (r.isEmpty()) return;
        const auto centre = r.getCentre();
        const float radius = r.getWidth() * 0.5f;
        const bool enabled = isEnabled() && input.isEnabled();
        const auto rotary = input.getRotaryParameters();
        const float start = rotary.startAngleRadians, end = rotary.endAngleRadians;
        // Use the actual skew mapping, including logarithmic frequency ranges.
        const float position = static_cast<float> (input.valueToProportionOfLength (input.getValue()));
        const float angle = start + (end - start) * position;
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (r.reduced (2).translated (0, 2));
        g.setGradientFill (juce::ColourGradient (c::background.darker (0.4f), centre.x, r.getY(),
                                                c::border, centre.x, r.getBottom(), false));
        g.fillEllipse (r);
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius - 2, radius - 2, 0, start, end, true);
        g.setColour (c::graph);
        g.strokePath (track, juce::PathStrokeType (3.0f));
        juce::Path value;
        const float origin = bipolar_ ? start + (end - start) * static_cast<float> (input.valueToProportionOfLength (0)) : start;
        value.addCentredArc (centre.x, centre.y, radius - 2, radius - 2, 0, origin, angle, true);
        g.setColour (enabled ? c::accent.withAlpha (input.isMouseOverOrDragging() ? 1.0f : 0.75f) : c::textMuted);
        g.strokePath (value, juce::PathStrokeType (2.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto body = r.reduced (radius * 0.23f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff456071), body.getTopLeft(),
                                                juce::Colour (0xff152832), body.getBottomRight(), false));
        g.fillEllipse (body);
        g.setColour (c::textSecondary.withAlpha (enabled ? 0.5f : 0.2f)); g.drawEllipse (body, 0.8f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff293f4d), body.getTopLeft(),
                                                juce::Colour (0xff0b1922), body.getBottomRight(), false));
        g.fillEllipse (body.reduced (2));
        const auto tip = centre.getPointOnCircumference (radius * 0.61f, angle);
        const auto base = centre.getPointOnCircumference (radius * 0.30f, angle);
        g.setColour (enabled ? c::text : c::textMuted);
        g.drawLine (juce::Line<float> (base, tip), 2.0f);
        if (input.hasKeyboardFocus (true))
        {
            g.setColour (c::accent); g.drawRoundedRectangle (r.expanded (1), 4, 1);
        }
        g.setColour (enabled ? c::textSecondary : c::textMuted);
        g.setFont (juce::FontOptions (11));
        g.drawFittedText (input.getTextFromValue (input.getValue()), getLocalBounds().removeFromBottom (14),
                         juce::Justification::centred, 1);
    }
private:
    bool primary_ = true, bipolar_ = false;
};
} // namespace vox::drums
