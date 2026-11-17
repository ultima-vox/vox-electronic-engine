#pragma once
#include "components/VoxKnob.h"
#include "components/VoxButton.h"
#include "components/VoxComboBox.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace vox::fx {
class FxProcessor;

// An honest FX-01 panel: only controls with working processor bindings are
// exposed. Additional workspaces are added as their DSP/controller exists.
class FxPanel final : public juce::Component, private juce::Timer {
public:
    explicit FxPanel (FxProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Slider& outputSlider() noexcept { return output_.getSlider(); }
    juce::Slider& durationSlider() noexcept { return duration_.getSlider(); }
    juce::ComboBox& familySelector() noexcept { return family_; }
private:
    void timerCallback() override;
    FxProcessor& processor_;
    vox::ui::VoxKnob output_ { "OUTPUT", vox::ui::VoxKnob::Size::Large };
    juce::SliderParameterAttachment attachment_;
    vox::ui::VoxKnob duration_ { "DURATION (S)", vox::ui::VoxKnob::Size::Large };
    juce::SliderParameterAttachment durationAttachment_;
    vox::ui::VoxComboBox family_;
    vox::ui::VoxButton audition_ { "AUDITION" };
    vox::ui::VoxButton panic_ { "PANIC" };
    float shownPeak_ = 0.0f;
};
} // namespace vox::fx
