#pragma once

// VOX Drums editor content (DRM-03b). Host parameters are bound through
// JUCE attachments; structural pad settings (model, note, choke, polyphony,
// fine, character, filter, resonance, transient, kick body, sample source,
// mode, split and the players) are bound to the KitStore. Both directions
// stay in sync: a control moves its setting, a changed setting (automation,
// restore) moves its control. The final custom skin is DRM-07.

#include <juce_audio_processors/juce_audio_processors.h>

#include "VoxComponents.h"
#include "engine/KitEngine.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace vox::drums {

class DrumsProcessor;

// What a structural control reads and writes: the selected pad's settings
// and its selected sample player.
struct StructuralContext {
    kit::PadSettings pad;
    int player = 0;
    kit::SamplePlayer& sample() { return pad.players[static_cast<std::size_t> (player)]; }
    const kit::SamplePlayer& sample() const { return pad.players[static_cast<std::size_t> (player)]; }
};

class DrumsPanel final : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer {
public:
    explicit DrumsPanel (DrumsProcessor& processor);
    ~DrumsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    int selectedPad() const noexcept { return selectedPad_; }
    void selectPad (int pad);
    int selectedPlayer() const noexcept { return selectedPlayer_; }
    void selectPlayer (int player);

    // Tests: host-parameter controls by parameter ID; structural controls by
    // name ("fine", "character", "resonance", "attack", "sustain",
    // "pitch_amount", "pitch_decay", "drive", "sub", "tail", "split",
    // "start", "end", "sample_level"; combos "model", "note", "choke",
    // "poly", "filter", "source", "sample_mode"; toggles "loop", "reverse",
    // "normalize").
    juce::Slider* sliderFor (const juce::String& id);
    juce::Button* buttonFor (const juce::String& id);
    juce::ComboBox* comboFor (const juce::String& id);
    juce::Slider* structuralSlider (const juce::String& name);
    juce::ComboBox* structuralCombo (const juce::String& name);
    juce::Button* structuralToggle (const juce::String& name);
    juce::Button* padButton (int pad);
    juce::Button* sampleButton (const juce::String& name);   // "load", "clear", "library", "A", "B"
    juce::String sampleStatus() const;
    bool bodyVisible() const;
    void pollForTesting() { timerCallback(); }   // one editor timer tick

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void loadSample (int pad, int player, const juce::File& file);

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct HostKnob {
        juce::String id;
        std::unique_ptr<vox::ui::VoxKnob> knob;
        std::unique_ptr<SliderAttachment> attachment;
    };
    struct HostToggle {
        juce::String id;
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<ButtonAttachment> attachment;
    };
    struct HostCombo {
        juce::String id;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<vox::ui::VoxComboBox> combo;
        std::unique_ptr<ComboAttachment> attachment;
    };
    // A structural setting of the selected pad (or of its selected player).
    struct Structural {
        juce::String name;
        std::function<float (const StructuralContext&)> get;
        std::function<void (StructuralContext&, float)> set;
        std::unique_ptr<vox::ui::VoxKnob> knob;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<vox::ui::VoxComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
    };

    class PadButton;
    class StepView;
    class WaveformView;

    void timerCallback() override;
    HostKnob makeHostKnob (const juce::String& id, const juce::String& label);
    void bindPadControls();
    void refreshStructure();
    void refreshSample();
    void changeStructure (Structural& s, float value);
    Structural* findStructural (const juce::String& name);
    Structural& addKnob (const juce::String& name, const juce::String& label, double lo, double hi, double step,
                         std::function<float (const StructuralContext&)> get, std::function<void (StructuralContext&, float)> set,
                         bool bipolar = false, double skewCentre = 0.0);
    Structural& addCombo (const juce::String& name, const juce::String& label, const juce::StringArray& items,
                          std::function<float (const StructuralContext&)> get, std::function<void (StructuralContext&, float)> set);
    Structural& addToggle (const juce::String& name, const juce::String& label,
                           std::function<float (const StructuralContext&)> get, std::function<void (StructuralContext&, float)> set);

    DrumsProcessor& processor_;
    int selectedPad_ = 0;
    int selectedPlayer_ = 0;
    std::uint64_t shownStructure_ = 0, shownSamples_ = 0;
    float shownStart_ = -1.0f, shownEnd_ = -1.0f;
    bool refreshing_ = false;

    vox::ui::VoxPanel padsPanel_ { "PADS" }, padPanel_ { "PAD" }, samplePanel_ { "SAMPLE" }, kitPanel_ { "KIT" },
        sequencerPanel_ { "SEQUENCER" };
    std::array<std::unique_ptr<PadButton>, 16> pads_;

    std::vector<HostKnob> padKnobs_;       // level, tune, decay, pan, cutoff (selected pad)
    HostToggle mute_;
    std::vector<std::unique_ptr<Structural>> structural_;

    std::unique_ptr<vox::ui::VoxButton> playerA_, playerB_, loadButton_, clearButton_, libraryButton_;
    std::unique_ptr<juce::Label> sampleStatus_;
    std::unique_ptr<WaveformView> waveform_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String loadMessage_;

    std::vector<HostKnob> kitKnobs_;
    HostToggle choke_;
    HostCombo seqMode_, seqPattern_;
    std::vector<HostKnob> seqKnobs_;
    std::vector<HostToggle> lanes_;
    std::unique_ptr<StepView> steps_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsPanel)
};

} // namespace vox::drums
