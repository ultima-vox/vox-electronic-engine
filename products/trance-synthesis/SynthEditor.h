#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "VoxButton.h"
#include "VoxComboBox.h"
#include "VoxKnob.h"

#include <memory>
#include <vector>

namespace vox::syn {

class SynthesisProcessor;

// Host parameters for both parts and the FX chain, plus the program browser.
class SynthPanel final : public juce::Component,
                         private juce::ListBoxModel {
public:
    explicit SynthPanel (SynthesisProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void refresh();
    void loadSelected();
    void saveNamed();
    void showStep();
    void commitStep();
    void exportPhrase();

    struct ProgramRow {
        bool factory = false;
        int index = 0;
    };

    SynthesisProcessor& processor_;
    juce::Label capacity_;
    juce::TextEditor search_, name_;
    vox::ui::VoxComboBox category_;
    juce::ListBox programs_;
    vox::ui::VoxButton load_, save_, play_, stop_, exportSmf_;
    vox::ui::VoxComboBox phrase_;
    juce::Slider step_;
    juce::TextEditor stepNote_, stepVelocity_, stepDuration_;
    juce::ToggleButton stepSlide_, stepAccent_;
    std::shared_ptr<juce::FileChooser> chooser_;
    bool filling_ = false;
    juce::Viewport viewport_;
    juce::Component page_;
    std::vector<std::unique_ptr<juce::Component>> controls_;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliders_;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> combos_;
    std::vector<ProgramRow> visible_;
};

} // namespace vox::syn
