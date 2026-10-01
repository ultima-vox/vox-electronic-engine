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

            void paintButton(juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override
            {
                auto bounds = getLocalBounds().toFloat().reduced(0.5f);
                const bool selected = getToggleState();
                const auto accent = vstengine::ui::colours::primary;

                auto fill = selected ? vstengine::ui::colours::panelRaised.brighter(0.08f)
                                     : vstengine::ui::colours::panel;
                if (isMouseOverButton)
                    fill = fill.brighter(0.045f);
                if (isButtonDown)
                    fill = fill.darker(0.08f);

                g.setColour(fill);
                g.fillRoundedRectangle(bounds, 5.0f);
                g.setColour(selected ? accent : vstengine::ui::colours::border);
                g.drawRoundedRectangle(bounds, 5.0f, selected ? 1.6f : 1.0f);

                if (selected) {
                    g.setColour(accent);
                    g.fillRoundedRectangle(bounds.getX(), bounds.getY() + 4.0f,
                                           3.0f, bounds.getHeight() - 8.0f, 1.5f);
                }

                juce::StringArray tokens;
                tokens.addTokens(getButtonText(), " ", "");
                tokens.removeEmptyStrings();

                const auto index = tokens.isEmpty() ? juce::String("--") : tokens[0];
                int routeIndex = -1;
                for (int i = 1; i < tokens.size(); ++i)
                    if (tokens[i].startsWithIgnoreCase("CH") || tokens[i].equalsIgnoreCase("OFF")) {
                        routeIndex = i;
                        break;
                    }

                juce::String name;
                if (routeIndex > 1)
                    for (int i = 1; i < routeIndex; ++i)
                        name += (name.isEmpty() ? juce::String() : juce::String(" ")) + tokens[i];
                else if (tokens.size() > 1)
                    name = tokens[1];
                if (name.isEmpty())
                    name = "Empty";

                const auto route = routeIndex >= 0 ? tokens[routeIndex] : juce::String("OFF");
                const bool empty = name.startsWithIgnoreCase("Empty");

                auto content = getLocalBounds().reduced(7, 4);
                auto indexArea = content.removeFromLeft(25);
                auto thumb = content.removeFromLeft(34).reduced(2);
                content.removeFromLeft(5);
                auto routeArea = content.removeFromRight(42);

                g.setColour(selected ? vstengine::ui::colours::text : vstengine::ui::colours::mutedText);
                g.setFont(juce::Font(10.0f, juce::Font::bold));
                g.drawText(index, indexArea, juce::Justification::centred, false);

                g.setColour(vstengine::ui::colours::background);
                g.fillRoundedRectangle(thumb.toFloat(), 4.0f);
                g.setColour(selected ? accent.withAlpha(0.52f) : vstengine::ui::colours::borderSubtle);
                g.drawRoundedRectangle(thumb.toFloat(), 4.0f, 1.0f);

                if (!empty) {
                    juce::Path waveform;
                    const auto plot = thumb.toFloat().reduced(4.0f);
                    const auto seed = static_cast<float>((name.hashCode() & 0x0f) + 7);
                    for (int i = 0; i <= 18; ++i) {
                        const auto t = static_cast<float>(i) / 18.0f;
                        const auto x = plot.getX() + plot.getWidth() * t;
                        const auto y = plot.getCentreY()
                            + std::sin(t * juce::MathConstants<float>::twoPi * (1.2f + seed * 0.03f))
                                  * plot.getHeight() * (0.17f + 0.18f * std::sin(t * 4.7f + seed));
                        if (i == 0) waveform.startNewSubPath(x, y); else waveform.lineTo(x, y);
                    }
                    g.setColour(accent.withAlpha(selected ? 0.96f : 0.58f));
                    g.strokePath(waveform, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                                juce::PathStrokeType::rounded));
                } else {
                    g.setColour(vstengine::ui::colours::mutedText.withAlpha(0.45f));
                    g.drawLine(thumb.getCentreX() - 5.0f, thumb.getCentreY(),
                               thumb.getCentreX() + 5.0f, thumb.getCentreY(), 1.2f);
                    g.drawLine(thumb.getCentreX(), thumb.getCentreY() - 5.0f,
                               thumb.getCentreX(), thumb.getCentreY() + 5.0f, 1.2f);
                }

                auto nameArea = content;
                auto subArea = nameArea.removeFromBottom(13);
                g.setColour(empty ? vstengine::ui::colours::mutedText : vstengine::ui::colours::text);
                g.setFont(juce::Font(empty ? 10.0f : 11.0f, empty ? juce::Font::plain : juce::Font::bold));
                g.drawText(name, nameArea, juce::Justification::centredLeft, true);
                g.setColour(vstengine::ui::colours::mutedText);
                g.setFont(8.5f);
                g.drawText(empty ? "ADD INSTRUMENT" : "ULTIMA VOX",
                           subArea, juce::Justification::centredLeft, true);

                g.setColour(selected ? accent : vstengine::ui::colours::textSecondary);
                g.setFont(9.0f);
                g.drawText(route, routeArea, juce::Justification::centred, false);

                const auto led = juce::Rectangle<float>(bounds.getRight() - 9.0f,
                                                        bounds.getY() + 6.0f, 3.0f, 3.0f);
                g.setColour(empty ? vstengine::ui::colours::mutedText.withAlpha(0.35f)
                                  : accent.withAlpha(selected ? 1.0f : 0.62f));
                g.fillEllipse(led);
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
