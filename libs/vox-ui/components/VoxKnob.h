#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vox::ui {

// VOX rotary control.
//
// UI_PRODUCTION_IMPLEMENTATION_STANDARD.md section 10 defines the production
// anatomy; this class renders exactly that stack (modulation range, inactive
// track, active track, housing ring, recessed body, shading, pointer, focus,
// label, value) and nothing else. It carries no page-specific layout rules: a
// page that wants a control strip composes its own bounds and passes them in.
//
// ACCENT COLOUR
// -------------
// `setColour (juce::Slider::rotarySliderFillColourId, c)` is honoured: the
// active arc is drawn in that colour and defaults to tokens::colour::accent.
// This is the documented hook product code uses to apply an instrument identity
// accent, and it is the same id juce::Slider uses for a rotary fill, so no
// second colour API exists.
class VoxKnob : public juce::Component,
                public juce::SettableTooltipClient
{
public:
    enum class Size  { Small, Normal, Large };
    enum class Style { Standard, Bipolar };

    explicit VoxKnob (juce::String labelText = {}, Size size = Size::Normal);

    juce::Slider& getSlider() noexcept { return slider; }
    const juce::Slider& getSlider() const noexcept { return slider; }

    void setLabel (juce::String text);
    void setKnobSize (Size newSize);
    void setStyle (Style newStyle);
    void setModulationAmount (float amount01);
    float getModulationAmount() const noexcept { return modulationAmount; }
    int getKnobDiameter() const noexcept;

    // Enables double-click reset to the real parameter default supplied by the caller.
    void setDoubleClickResetValue (double value);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class InteractionSlider final : public juce::Slider
    {
    public:
        void paint (juce::Graphics&) override {}
    };

    InteractionSlider slider;
    juce::Label label;
    Size knobSize = Size::Normal;
    Style style = Style::Standard;
    float modulationAmount = 0.0f;
    juce::Rectangle<int> valueBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxKnob)
};

} // namespace vox::ui
