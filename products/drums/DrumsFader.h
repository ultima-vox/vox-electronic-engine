#pragma once
#include "Tokens.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace vox::drums {
// Paint only: native Slider interaction and APVTS gesture behavior are retained.
class DrumsFader final : public juce::Slider {
public:
    DrumsFader()
    {
        setSliderStyle (juce::Slider::LinearVertical);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setWantsKeyboardFocus (true);
    }
    void paint (juce::Graphics& g) override
    {
        namespace c = vox::ui::tokens::colour;
        const auto bounds = getLookAndFeel().getSliderLayout (*this).sliderBounds.toFloat();
        if (bounds.isEmpty()) return;
        const float x = bounds.getCentreX();
        const auto well = juce::Rectangle<float> (x - 4, bounds.getY() + 3, 8, juce::jmax (0.0f, bounds.getHeight() - 6));
        g.setColour (c::border); g.fillRoundedRectangle (well.expanded (1), 3);
        g.setColour (c::graph); g.fillRoundedRectangle (well, 2);
        const float position = getPositionOfValue (getValue());
        auto cap = juce::Rectangle<float> (24, 14).withCentre ({ x, position });
        g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (cap.translated (1, 2), 2);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff536b7b), cap.getTopLeft(),
                                                juce::Colour (0xff142936), cap.getBottomRight(), false));
        g.fillRoundedRectangle (cap, 2);
        g.setColour (c::textSecondary.withAlpha (0.5f)); g.drawRoundedRectangle (cap, 2, 0.7f);
        g.setColour (isEnabled() ? c::accent : c::textMuted);
        g.drawHorizontalLine (juce::roundToInt (position), cap.getX() + 3, cap.getRight() - 3);
        if (hasKeyboardFocus (true))
        {
            g.setColour (c::accent); g.drawRoundedRectangle (cap.expanded (2), 3, 1);
        }
    }
};
} // namespace vox::drums
