#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "vox-ui/components/VoxKnob.h"
#include "sequence/Sequence.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

class VstEngineAudioProcessor;

// Dedicated selected-Acid SOUND workspace. The existing Rack, global shell,
// tabs and shared keyboard remain owned by PluginEditor.
class AcidPanel final : public juce::Component, private juce::Timer {
public:
    explicit AcidPanel(VstEngineAudioProcessor&);
    ~AcidPanel() override;
    void bind(std::size_t slot);
    void paint(juce::Graphics&) override;
    void resized() override;
    [[nodiscard]] std::size_t boundSlot() const noexcept { return slot_; }
    [[nodiscard]] std::size_t attachedControlCount() const noexcept;
    bool setKnobForTesting(std::string_view semantic, double normalized);
    void switchEnvelopeForTesting(bool filter);
    void switchModulationForTesting(bool lfo1);
private:
    enum Group : int { osc, filter, envelope, drive, accent, slide,
                       output, modulation, performance, playMode, grid, groupCount };
    struct Knob {
        Group group;
        std::string semantic;
        std::unique_ptr<vox::ui::VoxKnob> widget;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
        bool envelopeStage {};
    };
    class StepGrid final : public juce::Component {
    public:
        explicit StepGrid(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void setHead(int value) noexcept { head_ = value; repaint(); }
    private:
        VstEngineAudioProcessor& processor_;
        int head_ { -1 };
    };
    void timerCallback() override;
    void addKnob(Group, std::string_view, const char*, bool stage = false);
    void attachKnobs();
    void updateEnvelopeAttachments();
    void updateModulationAttachments();
    void changeChoice(const std::string&, int choiceIndex, int choices);
    void refreshChoices();
    [[nodiscard]] std::string hostId(std::string_view) const;
    [[nodiscard]] juce::Rectangle<int> section(Group) const noexcept;
    void layoutKnobs(Group, juce::Rectangle<int>);
    VstEngineAudioProcessor& processor_;
    std::size_t slot_ {};
    bool filterEnvelope_ {};
    bool lfo1Selected_ {};
    std::vector<Knob> knobs_;
    std::array<juce::Rectangle<int>, groupCount> sections_ {};
    juce::ComboBox filterChoice_, distortionChoice_, modulationChoice_;
    juce::TextButton ampButton_ { "AMP" }, filterButton_ { "FILTER" };
    juce::TextButton env1Button_ { "ENV 1" }, lfo1Button_ { "LFO 1" };
    juce::ComboBox profileChoice_, lengthChoice_, timingChoice_;
    juce::TextButton generate_ { "Generate" }, mutate_ { "Mutate" },
                     clear_ { "Clear" };
    std::array<juce::ToggleButton, 4> flags_;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>
        flagAttachments_;
    StepGrid steps_;
    juce::Label status_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AcidPanel)
};
