#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "VoxComboBox.h"
#include "VoxKnob.h"

#include <array>
#include <memory>

namespace vox::syn {

class SynthesisProcessor;

// Real bindings only: part engines, levels, and the master output.
class SynthPanel final : public juce::Component {
public:
    explicit SynthPanel (SynthesisProcessor&);
    void resized() override;

private:
    SynthesisProcessor& processor_;
    vox::ui::VoxComboBox engineA_, engineB_;
    vox::ui::VoxKnob levelA_, levelB_, output_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineAAttach_, engineBAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAAttach_, levelBAttach_, outputAttach_;
};

} // namespace vox::syn
