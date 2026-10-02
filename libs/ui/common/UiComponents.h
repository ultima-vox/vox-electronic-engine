#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace vstengine::ui {

namespace metrics {
inline constexpr int spaceXs = 4;
inline constexpr int spaceSm = 8;
inline constexpr int spaceMd = 12;
inline constexpr int spaceLg = 18;
inline constexpr float corner = 7.0f;
}

// Compatibility bridge for legacy product widgets that have not yet moved to
// vox::ui components. Keep these values aligned with the canonical VOX design
// tokens so mixed old/new screens do not render two competing palettes during
// the Visual Gate migration.
namespace colours {
inline const auto background = juce::Colour::fromRGB (6, 18, 29);       // #06121D
inline const auto panel = juce::Colour::fromRGB (11, 25, 37);           // #0B1925
inline const auto panelRaised = juce::Colour::fromRGB (16, 34, 48);     // #102230
inline const auto control = juce::Colour::fromRGB (10, 23, 34);         // #0A1722
inline const auto border = juce::Colour::fromRGB (29, 59, 80);          // #1D3B50
inline const auto borderSubtle = juce::Colour::fromRGB (18, 44, 61);    // #122C3D
inline const auto primary = juce::Colour::fromRGB (0, 221, 245);        // #00DDF5
inline const auto status = juce::Colour::fromRGB (50, 210, 150);        // #32D296
inline const auto warning = juce::Colour::fromRGB (227, 179, 65);       // #E3B341
inline const auto text = juce::Colour::fromRGB (233, 243, 250);         // #E9F3FA
inline const auto textSecondary = juce::Colour::fromRGB (154, 178, 197);// #9AB2C5
inline const auto mutedText = juce::Colour::fromRGB (88, 116, 135);     // #587487
inline const auto inactive = control;
inline const auto shadow = juce::Colour::fromRGBA (0, 0, 0, 88);
// Aliases matching vox::ui canonical tokens so extracted components can use the
// semantic names (accent/danger) without pulling in a second palette.
inline const auto accent = primary;
inline const auto danger = juce::Colour::fromRGB (228, 66, 93);          // #E4425D
}

class VoxLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    VoxLookAndFeel();
    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour&, bool, bool) override;
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float,
                          float, float, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int,
                      juce::ComboBox&) override;
};

class ParameterKnob final : public juce::Component {
public:
    ParameterKnob (juce::AudioProcessorValueTreeState&, const char* parameterId,
                   juce::String labelText, juce::String tooltip = {});
    void resized() override;

private:
    juce::Label label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class ParameterSection final : public juce::Component {
public:
    explicit ParameterSection (juce::String titleText);
    ParameterKnob& addKnob (juce::AudioProcessorValueTreeState&, const char* parameterId,
                            juce::String label, juce::String tooltip = {});
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label title;
    std::vector<std::unique_ptr<ParameterKnob>> knobs;
};

void styleButton (juce::Button&, bool primary = false);
void styleLabel (juce::Label&, float size, juce::Justification,
                 juce::Colour colour = colours::text);

} // namespace vstengine::ui
