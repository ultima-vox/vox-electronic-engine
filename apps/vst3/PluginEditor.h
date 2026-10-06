#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/ArtworkLoader.h"
#include "ui/GlobalHeader.h"
#include "ui/InstrumentIdentity.h"
#include "ui/sound/SoundPage.h"
#include "ui/InstrumentRack.h"
#include "ui/MainNavigation.h"
#include "ui/StepSequencer.h"
#include "ui/ZoneRangeEditor.h"
#include "vox-ui/components/VoxButton.h"
#include "vox-ui/components/VoxComboBox.h"
#include "vox-ui/components/VoxIconButton.h"
#include "vox-ui/components/VoxKnob.h"
#include "vox-ui/components/VoxPanel.h"
#include "vox-ui/components/VoxTabBar.h"

// ===========================================================================
// Shell band geometry
// ===========================================================================
//
// The accepted 1448x1086 renders define the shell as a single set of bands. The
// reference values are exact at that size and are expressed here as one table
// plus one proportional mapper, so:
//
//   * at height 1086 / width 1448 every band is pixel-identical to the brief;
//   * at any other size the bands keep their proportions and are clamped from
//     below, so the shell degrades instead of collapsing or overlapping.
//
// The keyboard/footer band is INSIDE the content column (x from the rail's right
// edge to the right margin). The rail is full height. That is the structural
// change from the previous build, where the keyboard spanned the window and the
// rail stopped above it.
namespace vstengine::ui {

struct ShellLayout {
    juce::Rectangle<int> header;
    juce::Rectangle<int> rail;
    juce::Rectangle<int> instrumentHeader;
    juce::Rectangle<int> navigation;
    juce::Rectangle<int> hero;
    juce::Rectangle<int> row1;
    juce::Rectangle<int> row2;
    juce::Rectangle<int> row3;
    juce::Rectangle<int> footer;
    // The page (workspace) rectangle: the content column above the footer. It is
    // deliberately disjoint from `rail` and `footer` so the shell's regions stay
    // non-overlapping, and the module panels' bottom edge is exactly the footer's
    // top edge, which is what the accepted render shows.
    juce::Rectangle<int> workspace;
};

// The reference canvas the band table is expressed in.
inline constexpr int shellReferenceWidth = 1448;
inline constexpr int shellReferenceHeight = 1086;

// Pure function of the editor size, so the layout can be asserted in a test and
// reasoned about without instantiating components.
[[nodiscard]] ShellLayout makeShellLayout (int width, int height);

} // namespace vstengine::ui

class VstEngineAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer {
public:
    explicit VstEngineAudioProcessorEditor(VstEngineAudioProcessor&);
    ~VstEngineAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void showPageForTesting(vstengine::ui::MainNavigation::Page);
    [[nodiscard]] vstengine::ui::MainNavigation::Page currentPageForTesting() const noexcept;
    [[nodiscard]] juce::Rectangle<int> rackBoundsForTesting() const noexcept;
    [[nodiscard]] juce::Rectangle<int> workspaceBoundsForTesting() const noexcept;
    [[nodiscard]] juce::Rectangle<int> keyboardBoundsForTesting() const noexcept;

    // The shell band rectangles this editor is currently laid out with. Exposed
    // so the smoke test asserts the geometry the shell actually used rather than
    // recomputing it independently.
    [[nodiscard]] vstengine::ui::ShellLayout shellLayoutForTesting() const noexcept;

    // Keyboard geometry, exposed so the smoke test can prove the piano really
    // covers the key area it was given.
    //
    // `keyboardKeySpanForTesting()` is the total pixel width the rendered keys
    // occupy: the left edge of the lowest key to the right edge of the highest,
    // measured from the keyboard's own key geometry. It is NOT
    // `keyWidth * noteCount` recomputed here; it is read back from the component.
    [[nodiscard]] float keyboardKeySpanForTesting() const noexcept;
    [[nodiscard]] int keyboardLowestNoteForTesting() const noexcept;
    [[nodiscard]] int keyboardHighestNoteForTesting() const noexcept;
    [[nodiscard]] bool keyboardKeysFitComponentForTesting() const noexcept;
    [[nodiscard]] float keyboardKeyWidthForTesting() const noexcept;
    [[nodiscard]] juce::Rectangle<float> keyboardKeyBoundsForTesting(int note) const noexcept;

    // The footer band rectangle (editor space). The piano lives inside it: this
    // is the shell property that the old full-width keyboard violated.
    [[nodiscard]] juce::Rectangle<int> footerBoundsForTesting() const noexcept;

    // The piano's own bounds in its parent's (footer) coordinate space.
    [[nodiscard]] juce::Rectangle<int> keyboardLocalBoundsForTesting() const noexcept;

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

    // -----------------------------------------------------------------------
    // Footer: tabs, velocity curve, MIDI learn, keyboard layout, Pitch/Mod
    // wheels and the piano.
    //
    // The Pitch/Mod faders are self-painting. The installed product LookAndFeel
    // has no drawLinearSlider override, so a juce::Slider here would render with
    // stock JUCE chrome, which is explicitly not production acceptance.
    // -----------------------------------------------------------------------
    class WheelFader final : public juce::Slider {
    public:
        explicit WheelFader (juce::String labelText);
        void paint (juce::Graphics&) override;

    private:
        juce::String label;
    };

    class FooterBar final : public juce::Component {
    public:
        explicit FooterBar (VstEngineAudioProcessor&);

        // The instrument range the piano shows. Six octaves, which is the span
        // the accepted render draws (it labels its own range C0..C6; JUCE derives
        // printed octave numbers from its own middle-C convention, so the same
        // range prints C1..C7 here -- see the wave report). The span is what
        // matters for geometry: 24..96 is 73 semitones, which is also JUCE's own
        // default available range.
        static constexpr int keyboardLowestNote = 24;   // C1
        static constexpr int keyboardHighestNote = 96;  // C7

        // The piano is scaled to exactly fill the space the footer leaves for it,
        // so `keyboardKeysFitComponentForTesting()` always holds. This floor only
        // exists so a pathologically small footer cannot produce an unplayable
        // piano; it is never reached at the supported minimum size.
        static constexpr float minKeyWidth = 6.0f;

        void paint (juce::Graphics&) override;
        void resized() override;

        [[nodiscard]] const juce::MidiKeyboardComponent& getKeyboard() const noexcept { return keyboard; }

    private:
        // The KEYBOARD / CHORDS / SCALE strip is presentation chrome at Gate A:
        // the accepted render shows three tabs and only KEYBOARD is composed. The
        // control is a real VoxTabBar so it cannot drift from the main nav tabs.
        vox::ui::VoxTabBar modeTabs;
        juce::Label velocityCurveLabel;
        vox::ui::VoxComboBox velocityCurve;
        vox::ui::VoxButton midiLearn { "MIDI Learn", vox::ui::VoxButton::Type::Secondary };
        vox::ui::VoxIconButton keyboardLayout { vox::ui::icons::Icon::keyboardLayout };
        WheelFader pitch { "Pitch" };
        WheelFader mod { "Mod" };
        juce::MidiKeyboardComponent keyboard;
    };

    class InstrumentHeader final : public juce::Component {
    public:
        explicit InstrumentHeader(VstEngineAudioProcessor&);
        void paint(juce::Graphics&) override; void resized() override;
        void bind(std::size_t); void refresh();
        std::function<void()> onModelChanged;
    private:
        VstEngineAudioProcessor& processor;
        juce::Label title, meta;
        vox::ui::VoxIconButton rename { vox::ui::icons::Icon::pencil };
        vox::ui::VoxInlineSelector instrument { vox::ui::VoxInlineSelector::Form::Stepper };
        vox::ui::VoxIconButton favourite { vox::ui::icons::Icon::heart, true };
        vox::ui::VoxComboBox preset;
        juce::Label channelLabel;
        vox::ui::VoxComboBox midiIn;
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
    void refreshRack();
    VstEngineAudioProcessor& processor;
    vstengine::ui::VoxLookAndFeel lookAndFeel;
    vstengine::ui::GlobalHeader header;
    vstengine::ui::InstrumentRack rackRail;
    InstrumentHeader instrumentHeader;
    vstengine::ui::MainNavigation navigation;
    vstengine::ui::SoundPage soundPage;
    PatternPage patternPage;
    RoutingPage routingPage;
    ZonesPage zonesPage;
    MacroPage macrosPage;
    AdvancedPage advancedPage;
    std::array<juce::Component*, 6> pages;
    FooterBar footer;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VstEngineAudioProcessorEditor)
};
