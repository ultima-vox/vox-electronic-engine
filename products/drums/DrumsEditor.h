#pragma once

// VOX Drums editor content (DRM-01): every control is bound to a real plugin
// parameter through JUCE attachments, so host automation, the editor and the
// saved state stay one source of truth. Pads light on every hit (host MIDI,
// sequencer, audition); the step view shows the selected factory pattern
// with the playhead. The full custom Drums UI is DRM-07.

#include <juce_audio_processors/juce_audio_processors.h>

#include "VoxComponents.h"

#include <array>
#include <memory>
#include <vector>

namespace vox::drums {

class DrumsProcessor;

class DrumsPanel final : public juce::Component, private juce::Timer {
public:
    explicit DrumsPanel (DrumsProcessor& processor);
    ~DrumsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    int selectedPad() const noexcept { return selectedPad_; }
    void selectPad (int pad);

    // For tests: the knob bound to plugin parameter `id`, or null.
    juce::Slider* sliderFor (const juce::String& id);
    juce::Button* buttonFor (const juce::String& id);
    juce::ComboBox* comboFor (const juce::String& id);
    juce::Button* padButton (int pad);

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct BoundKnob {
        juce::String id;
        std::unique_ptr<vox::ui::VoxKnob> knob;
        std::unique_ptr<SliderAttachment> attachment;
    };
    struct BoundToggle {
        juce::String id;
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<ButtonAttachment> attachment;
    };
    struct BoundCombo {
        juce::String id;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<vox::ui::VoxComboBox> combo;
        std::unique_ptr<ComboAttachment> attachment;
    };

    class PadButton;
    class StepView;

    void timerCallback() override;
    BoundKnob makeKnob (const juce::String& id, const juce::String& label);
    void bindPadControls();
    void layoutKnobRow (std::vector<BoundKnob*> knobs, juce::Rectangle<int> area);

    DrumsProcessor& processor_;
    int selectedPad_ = 0;

    vox::ui::VoxPanel padsPanel_ { "PADS" }, padPanel_ { "PAD" }, kitPanel_ { "KIT" },
        sequencerPanel_ { "SEQUENCER" };
    std::array<std::unique_ptr<PadButton>, 8> pads_;

    // Selected pad (rebound on selection).
    std::vector<BoundKnob> padKnobs_;
    BoundCombo padFilter_;
    std::vector<BoundKnob> kickKnobs_;      // kick only (pad 1)

    std::vector<BoundKnob> kitKnobs_;
    BoundToggle hatChoke_;

    BoundCombo seqMode_, seqPattern_;
    std::vector<BoundKnob> seqKnobs_;
    std::vector<BoundToggle> lanes_;
    std::unique_ptr<StepView> steps_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsPanel)
};

} // namespace vox::drums
