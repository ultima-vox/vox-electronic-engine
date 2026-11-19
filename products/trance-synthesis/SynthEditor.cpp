#include "SynthEditor.h"

#include "Processor.h"

namespace vox::syn {

SynthPanel::SynthPanel (SynthesisProcessor& processor)
    : processor_ (processor),
      levelA_ ("A Level"),
      levelB_ ("B Level"),
      output_ ("Output")
{
    auto& state = processor_.parameters();
    engineA_.addItemList ({ "Psy Bass", "Acid", "VA" }, 1);
    engineB_.addItemList ({ "Psy Bass", "Acid", "VA" }, 1);
    for (auto* box : { &engineA_, &engineB_ })
        addAndMakeVisible (box);
    for (auto* knob : { &levelA_, &levelB_, &output_ })
        addAndMakeVisible (knob);

    engineAAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, "syn.a.engine", engineA_);
    engineBAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, "syn.b.engine", engineB_);
    levelAAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, "syn.a.level", levelA_.getSlider());
    levelBAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, "syn.b.level", levelB_.getSlider());
    outputAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, "syn.output", output_.getSlider());
}

void SynthPanel::resized()
{
    auto row = getLocalBounds().reduced (16);
    auto combos = row.removeFromTop (36);
    engineA_.setBounds (combos.removeFromLeft (220));
    combos.removeFromLeft (12);
    engineB_.setBounds (combos.removeFromLeft (220));
    row.removeFromTop (12);
    auto knobs = row.removeFromTop (120);
    levelA_.setBounds (knobs.removeFromLeft (110));
    levelB_.setBounds (knobs.removeFromLeft (110));
    output_.setBounds (knobs.removeFromLeft (110));
}

} // namespace vox::syn
