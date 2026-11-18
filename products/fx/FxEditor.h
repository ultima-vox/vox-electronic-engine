#pragma once
#include "components/VoxKnob.h"
#include "components/VoxButton.h"
#include "components/VoxComboBox.h"
#include "LayerPanel.h"
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
    juce::Slider& seedSlider() noexcept { return seed_; }
    juce::Button& generateButton() noexcept { return generate_; }
    juce::Button& mutateButton() noexcept { return mutate_; }
    juce::ComboBox& effectSelector(std::size_t index) noexcept { return effectsType_[index]; }
    juce::Slider& effectWet(std::size_t index) noexcept { return effectsWet_[index]; }
    LayerPanel& layerPanel(std::size_t index) noexcept { return *layers_[index]; }
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
    juce::Slider seed_;
    vox::ui::VoxButton generate_ { "GENERATE" };
    vox::ui::VoxButton mutate_ { "MUTATE" };
    std::array<std::unique_ptr<LayerPanel>, 4> layers_;
    std::array<vox::ui::VoxComboBox,4> effectsType_;
    std::array<juce::Slider,4> effectsWet_, effectsAmount_;
    float shownPeak_ = 0.0f;
};
} // namespace vox::fx
