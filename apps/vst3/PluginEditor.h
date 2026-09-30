#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/GlobalHeader.h"
#include "ui/MainNavigation.h"
#include "ui/StepSequencer.h"
#include "ui/ZoneRangeEditor.h"
#include "vox-ui/components/VoxButton.h"
#include "vox-ui/components/VoxComboBox.h"
#include "vox-ui/components/VoxKnob.h"
#include "vox-ui/components/VoxPanel.h"

class VstEngineAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer {
public:
    explicit VstEngineAudioProcessorEditor(VstEngineAudioProcessor&);
    ~VstEngineAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void showPageForTesting(vstengine::ui::MainNavigation::Page);
    [[nodiscard]] vstengine::ui::MainNavigation::Page currentPageForTesting() const noexcept;
    [[nodiscard]] float keyboardKeyWidthForTesting(vstengine::ui::MainNavigation::Page) const noexcept;
    [[nodiscard]] int keyboardComponentWidthForTesting(vstengine::ui::MainNavigation::Page) const noexcept;
    [[nodiscard]] juce::Rectangle<int> rackBoundsForTesting() const noexcept;
    [[nodiscard]] juce::Rectangle<int> workspaceBoundsForTesting() const noexcept;
    [[nodiscard]] juce::Rectangle<int> keyboardBoundsForTesting() const noexcept;

private:
    struct SequenceCallbacks final : vstengine::ui::StepSequencer::Callbacks {
        explicit SequenceCallbacks(VstEngineAudioProcessor& p) : processor(p) {}
        void onCopy() override; void onPaste() override;
        void onRotateLeft() override; void onRotateRight() override;
        void onReverse() override; void onShiftLeft() override; void onShiftRight() override;
        void onTransposeUp() override; void onTransposeDown() override;
        void onOctaveUp() override; void onOctaveDown() override;
        void onMutate() override; void onClear() override;
        void onSequenceChanged() override;
        VstEngineAudioProcessor& processor;
        vstengine::sequence::Sequence clipboard;
        bool copied {};
    };

    class RackRail final : public juce::Component {
    public:
        explicit RackRail(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override; void resized() override; void refresh();
        std::function<void(std::size_t)> onSelected;
    private:
        class RackSlotButton final : public vox::ui::VoxButton {
        public:
            RackSlotButton() : VoxButton({}, vox::ui::VoxButton::Type::Toggle)
            {
                setClickingTogglesState(false);
            }
        };

        VstEngineAudioProcessor& processor;
        juce::Label title;
        std::array<RackSlotButton, vstengine::instrument::maxSlots> slots;
    };

    class InstrumentHeader final : public juce::Component {
    public:
        explicit InstrumentHeader(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override; void resized() override;
        void bind(std::size_t); void refresh();
        std::function<void()> onModelChanged;
    private:
        VstEngineAudioProcessor& processor;
        juce::Label title, subtitle;
        vox::ui::VoxComboBox instrument, preset, midiIn;
        vox::ui::VoxButton presetPrevious { "<", vox::ui::VoxButton::Type::Icon };
        vox::ui::VoxButton presetNext { ">", vox::ui::VoxButton::Type::Icon };
        std::vector<std::string> presetIds;
        std::size_t selected {};
    };

    class SoundPage final : public juce::Component {
    public:
        explicit SoundPage(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override;
        void resized() override;
        void bind(std::size_t);

    private:
        enum class Layout { generic, psyBass, acid };
        enum class VisualRole {
            oscillator, filter, envelope, character, accent, performance,
            modulation, matrix, sequencer, playMode, output
        };

        class HeroBanner final : public juce::Component {
        public:
            void setLayout(Layout newLayout, juce::String instrumentName);
            void paint(juce::Graphics&) override;
        private:
            Layout layout { Layout::generic };
            juce::String name { "INSTRUMENT" };
        };

        class VisualPanel final : public juce::Component {
        public:
            VisualPanel(juce::String titleText, VisualRole visualRole);
            void setAccent(juce::Colour newAccent);
            void setSubtitle(juce::String text);
            void paint(juce::Graphics&) override;
        private:
            juce::String title;
            juce::String subtitle;
            VisualRole role;
            juce::Colour accent;
        };

        void configureLayout(Layout newLayout, juce::String instrumentName);
        void hideAllPanels();
        void hideAllKnobs();
        void placeKnobs(juce::Rectangle<int> panelBounds,
                        std::initializer_list<std::size_t> indices);

        VstEngineAudioProcessor& processor;
        HeroBanner hero;
        VisualPanel oscillator { "OSCILLATOR", VisualRole::oscillator };
        VisualPanel filter { "FILTER", VisualRole::filter };
        VisualPanel envelope { "AMP ENVELOPE", VisualRole::envelope };
        VisualPanel character { "DRIVE / CHARACTER", VisualRole::character };
        VisualPanel accentPanel { "ACCENT", VisualRole::accent };
        VisualPanel performance { "PERFORMANCE", VisualRole::performance };
        VisualPanel modulation { "MODULATION", VisualRole::modulation };
        VisualPanel matrix { "MOD MATRIX", VisualRole::matrix };
        VisualPanel sequencer { "STEP SEQUENCER", VisualRole::sequencer };
        VisualPanel playMode { "PLAY MODE", VisualRole::playMode };
        VisualPanel output { "OUTPUT", VisualRole::output };
        std::array<std::unique_ptr<vox::ui::VoxKnob>, vstengine::instrument::macrosPerSlot> knobs;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
        Layout layout { Layout::generic };
        std::size_t selected {};
    };

    class MacroPage final : public juce::Component {
    public:
        MacroPage(VstEngineAudioProcessor&, juce::String title, std::size_t visibleCount);
        void paint(juce::Graphics&) override; void resized() override; void bind(std::size_t);
    private:
        VstEngineAudioProcessor& processor;
        juce::Label title, description;
        std::array<std::unique_ptr<vox::ui::VoxKnob>, vstengine::instrument::macrosPerSlot> knobs;
        std::array<std::unique_ptr<vox::ui::VoxPanel>, vstengine::instrument::macrosPerSlot> cards;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
        std::size_t count;
    };

    class PatternPage final : public juce::Component {
    public:
        explicit PatternPage(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override; void resized() override;
        void bind(std::size_t); void refresh(); void setPlayHead(int);
    private:
        VstEngineAudioProcessor& processor;
        SequenceCallbacks callbacks;
        vstengine::ui::StepSequencer sequencer;
        juce::ComboBox profile;
        juce::TextButton generate { "Generate" }, mutate { "Mutate Selected" };
        juce::Label status;
        std::vector<std::string> profileIds;
    };

    class RoutingPage final : public juce::Component {
    public:
        explicit RoutingPage(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override; void resized() override; void bind(std::size_t);
    private:
        void assign(VstEngineAudioProcessor::ChannelConflictAction);
        VstEngineAudioProcessor& processor;
        juce::Label title, status;
        juce::ComboBox midiIn;
        juce::ToggleButton layer { "Layer" }, enabled { "Enabled" }, mute { "Mute" }, solo { "Solo" }, locked { "Lock" };
        juce::Slider level, pan;
        juce::TextButton swap { "SWAP" }, move { "MOVE" }, layerAction { "LAYER" };
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
        std::size_t selected {};
        int pendingChannel {};
    };

    class ZonesPage final : public juce::Component {
    public:
        explicit ZonesPage(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override; void resized() override; void bind(std::size_t);
    private:
        VstEngineAudioProcessor& processor;
        juce::Label title, transposeLabel;
        vstengine::ui::ZoneRangeEditor ranges;
        juce::Slider transpose;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    class AdvancedPage final : public juce::Component {
    public:
        explicit AdvancedPage(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override; void resized() override; void bind(std::size_t);
    private:
        VstEngineAudioProcessor& processor;
        juce::Label title, description, status;
        juce::ComboBox midiMode, effectsPreset;
        juce::Slider seed;
        juce::TextButton generateAll { "Generate All" }, mutateAll { "Mutate All" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> seedAttachment;
    };

    void timerCallback() override;
    void bindSelection(std::size_t);
    void showPage(vstengine::ui::MainNavigation::Page);
    void cycleGlobalPreset(int delta);
    void loadGlobalPreset(int index);
    void saveGlobalPreset();
    void refreshGlobalPresets();
    VstEngineAudioProcessor& processor;
    vstengine::ui::VoxLookAndFeel lookAndFeel;
    vstengine::ui::GlobalHeader header;
    RackRail rackRail;
    InstrumentHeader instrumentHeader;
    vstengine::ui::MainNavigation navigation;
    SoundPage soundPage;
    PatternPage patternPage;
    RoutingPage routingPage;
    ZonesPage zonesPage;
    MacroPage macrosPage;
    AdvancedPage advancedPage;
    std::array<juce::Component*, 6> pages;
    juce::MidiKeyboardComponent keyboard;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VstEngineAudioProcessorEditor)
};
