#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/GlobalHeader.h"
#include "ui/MainNavigation.h"
#include "ui/StepSequencer.h"
#include "ui/ZoneRangeEditor.h"
#include "vox-ui/components/VoxButton.h"
#include "vox-ui/components/VoxComboBox.h"
#include "vox-ui/VoxPaint.h"

class VstEngineAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer {
public:
    explicit VstEngineAudioProcessorEditor(VstEngineAudioProcessor&);
    ~VstEngineAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    [[nodiscard]] bool headerControlsFitForTesting() const noexcept {
      return header.controlsFit();
    }
    void showPageForTesting(vstengine::ui::MainNavigation::Page);
    [[nodiscard]] vstengine::ui::MainNavigation::Page currentPageForTesting() const noexcept;
    [[nodiscard]] float keyboardKeyWidthForTesting(vstengine::ui::MainNavigation::Page) const noexcept;
    [[nodiscard]] int keyboardComponentWidthForTesting(vstengine::ui::MainNavigation::Page) const noexcept;
    [[nodiscard]] juce::Rectangle<int> rackBoundsForTesting() const noexcept;
    [[nodiscard]] juce::Rectangle<int> workspaceBoundsForTesting() const noexcept;
    [[nodiscard]] juce::Rectangle<int> keyboardBoundsForTesting() const noexcept;
    void refreshForTesting() { timerCallback(); }
    [[nodiscard]] int patternLengthForTesting() const noexcept {
      return patternPage.displayedLength();
    }

private:
    // Raised sub-panels painted behind a page's controls: bounds + title.
    using Sections = std::vector<std::pair<juce::Rectangle<int>, juce::String>>;
    struct SequenceCallbacks final : vstengine::ui::StepSequencer::Callbacks {
      explicit SequenceCallbacks(VstEngineAudioProcessor &p)
          : processor(p), edit(p.sequenceEditSnapshot()) {}
      void onCopy() override;
      void onPaste() override;
      void onRotateLeft() override;
      void onRotateRight() override;
      void onReverse() override;
      void onShiftLeft() override;
      void onShiftRight() override;
      void onTransposeUp() override;
      void onTransposeDown() override;
      void onOctaveUp() override;
      void onOctaveDown() override;
      void onMutate() override;
      void onClear() override;
      void onSequenceChanged() override;
      VstEngineAudioProcessor &processor;
      VstEngineAudioProcessor::SequenceEdit edit;
      vstengine::sequence::Sequence clipboard;
      bool copied{};
    };

    class RackRail final : public juce::Component {
    public:
        explicit RackRail(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override; void resized() override; void refresh();
        std::function<void(std::size_t)> onSelected;
    private:
      class SlotRow final : public juce::Component {
      public:
        SlotRow();
        void paint(juce::Graphics &) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent &) override;
        bool keyPressed(const juce::KeyPress &key) override {
          if (key == juce::KeyPress::returnKey ||
              key == juce::KeyPress::spaceKey) {
            if (onSelect)
              onSelect(false);
            return true;
          }
          return false;
        }
        std::function<void(bool)> onSelect;
        class PowerButton final : public juce::Button {
        public:
          PowerButton() : juce::Button("Slot power") {}
          void paintButton(juce::Graphics &g, bool hover, bool) override {
            const auto on = getToggleState() && isEnabled();
            auto c = on ? juce::Colour(0xff00ddf5) : juce::Colour(0xff587487);
            if (!isEnabled())
              c = c.withAlpha(0.45f);
            else if (hover)
              c = c.brighter(0.3f);
            vox::ui::paint::powerIcon(
                g, getLocalBounds().toFloat().withSizeKeepingCentre(18.0f, 18.0f),
                c);
          }
        } power;
        vstengine::rack::Rack::SlotSummary summary;
        juce::String channel;
        std::size_t index{};
        bool selected{}, enabled{}, mute{}, solo{};
      };
      void showCatalog(std::size_t);
      void showSlotMenu(std::size_t);
      void loadInstrument(std::size_t, const std::string &);
      VstEngineAudioProcessor &processor;
      juce::Rectangle<int> headingBounds, artBounds;
      juce::TextButton add{"+"};
      std::array<SlotRow, vstengine::instrument::maxSlots> slots;
      std::vector<
          std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>
          powerAttachments;
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

    class MacroPage final : public juce::Component {
    public:
        MacroPage(VstEngineAudioProcessor&, juce::String title, std::size_t visibleCount);
        void paint(juce::Graphics&) override; void resized() override; void bind(std::size_t);
    private:
        VstEngineAudioProcessor& processor;
        juce::Label title, description;
        std::array<juce::Slider, vstengine::instrument::macrosPerSlot> knobs;
        std::array<juce::Label, vstengine::instrument::macrosPerSlot> labels;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
        juce::Rectangle<int> groupBounds;
        std::size_t count;
    };

    class PatternPage final : public juce::Component {
    public:
        explicit PatternPage(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override; void resized() override;
        void bind(std::size_t); void refresh(); void setPlayHead(int);
        void synchronize();
        [[nodiscard]] int displayedLength() const noexcept {
          return callbacks.edit.value.size();
        }

    private:
        VstEngineAudioProcessor& processor;
        SequenceCallbacks callbacks;
        vstengine::ui::StepSequencer sequencer;
        juce::ComboBox profile;
        juce::TextButton generate { "Generate" }, mutate { "Mutate Selected" };
        juce::Label status;
        std::vector<std::string> profileIds;
        Sections sections;
    };

    class RoutingPage final : public juce::Component {
    public:
        explicit RoutingPage(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override; void resized() override; void bind(std::size_t);
    private:
        void assign(int channel);
        [[nodiscard]] int currentChannel() const;
        VstEngineAudioProcessor& processor;
        juce::Label title, status, levelLabel, panLabel, channelHint;
        juce::ComboBox midiIn;
        juce::ToggleButton enabled { "Enabled" }, mute { "Mute" }, solo { "Solo" }, locked { "Lock" };
        juce::Slider level, pan;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
        std::size_t selected {};
        Sections sections;
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
        Sections sections;
    };

    class AdvancedPage final : public juce::Component {
    public:
        explicit AdvancedPage(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override; void resized() override; void bind(std::size_t);
    private:
        VstEngineAudioProcessor& processor;
        juce::Label title, description, status, seedLabel;
        juce::ComboBox midiMode, effectsPreset;
        juce::Slider seed;
        juce::TextButton generateAll { "Generate All" }, mutateAll { "Mutate All" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> seedAttachment;
        Sections sections;
    };

    void timerCallback() override;
    void bindSelection(std::size_t);
    void showPage(vstengine::ui::MainNavigation::Page);
    void showRackSettings();
    void exportSelectedPattern();
    std::unique_ptr<juce::FileChooser> exportChooser;
    bool exportChooserActive{};
    void showOperationError(const juce::String &);
    void cycleGlobalPreset(int delta);
    void loadGlobalPreset(int index);
    void saveGlobalPreset();
    void refreshGlobalPresets();
    juce::Component::SafePointer<juce::DialogWindow> routingWindow;
    std::size_t boundSlot{vstengine::instrument::maxSlots};
    vstengine::instrument::SlotId boundSlotId{};
    std::string boundInstrument;
    const vstengine::instrument::InstrumentDescriptor *boundDescriptor{};
    VstEngineAudioProcessor& processor;
    vstengine::ui::VoxLookAndFeel lookAndFeel;
    vstengine::ui::GlobalHeader header;
    RackRail rackRail;
    InstrumentHeader instrumentHeader;
    vstengine::ui::MainNavigation navigation;
    MacroPage soundPage;
    PatternPage patternPage;
    RoutingPage routingPage;
    ZonesPage zonesPage;
    MacroPage macrosPage;
    AdvancedPage advancedPage;
    std::array<juce::Component*, 6> pages;
    juce::MidiKeyboardComponent keyboard;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VstEngineAudioProcessorEditor)
};
