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
    juce::ComboBox& patternGridSelector() noexcept { return patternGrid_; }
    juce::ComboBox& patternPageSelector() noexcept { return patternPage_; }
    juce::ComboBox& patternStepSelector() noexcept { return patternStepSelector_; }
    juce::Slider& patternSwingSlider() noexcept { return patternSwing_; }
    juce::Slider& patternNoteSlider() noexcept { return patternDetails_[0]; }
    juce::Slider& patternVelocitySlider() noexcept { return patternDetails_[1]; }
    juce::Slider& patternProbabilitySlider() noexcept { return patternDetails_[2]; }
    juce::Slider& patternRatchetSlider() noexcept { return patternDetails_[3]; }
    juce::ComboBox& patternLengthSelector() noexcept { return patternLength_; }
    juce::ComboBox& patternBarSelector() noexcept { return patternBar_; }
    juce::Button& patternStep(std::size_t index) noexcept { return patternSteps_[index]; }
    juce::ComboBox& patternSelector() noexcept { return patternSelector_; }
    juce::Button& patternButton() noexcept { return patternEnabled_; }
    juce::Button& layerLock(std::size_t index) noexcept { return layerLocks_[index]; }
    juce::ComboBox& effectSelector(std::size_t index) noexcept { return effectsType_[index]; }
    juce::Slider& effectWet(std::size_t index) noexcept { return effectsWet_[index]; }
    LayerPanel& layerPanel(std::size_t index) noexcept { return *layers_[index]; }
private:
    void timerCallback() override;
    void refreshPattern();
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
    std::array<juce::ToggleButton,4> layerLocks_;
    vox::ui::VoxComboBox patternSelector_,patternLength_,patternBar_,patternStepSelector_,patternGrid_,patternPage_;
    std::array<juce::Slider,4> patternDetails_;
    juce::Slider patternSwing_;
    std::array<juce::TextButton,16> patternSteps_;
    vox::ui::VoxButton patternEnabled_ { "HOST PATTERN", vox::ui::VoxButton::Type::Toggle };
    std::unique_ptr<juce::ButtonParameterAttachment> patternAttachment_;
    std::array<std::unique_ptr<LayerPanel>, 4> layers_;
    std::array<vox::ui::VoxComboBox,4> effectsType_;
    std::array<juce::Slider,4> effectsWet_, effectsAmount_;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>,4> wetAttachments_,amountAttachments_;
    float shownPeak_ = 0.0f;
};
} // namespace vox::fx
