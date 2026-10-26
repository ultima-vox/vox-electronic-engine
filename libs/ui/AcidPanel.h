#pragma once

#include "acid/AcidEngine.h"
#include "acid/AcidGenerator.h"
#include "sequence/Sequence.h"
#include "vox-ui/Tokens.h"
#include "vox-ui/Typography.h"
#include "vox-ui/VoxComponents.h"
#include "vox-ui/VoxLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace vstengine::ui {

class AcidPanel final : public juce::Component,
                        private juce::Timer {
public:
    enum class SubTab : std::uint8_t {
        sound = 0,
        pattern,
        routing,
        zones,
        macros,
        advanced
    };

    enum class EnvelopeTab : std::uint8_t {
        amp = 0,
        filter
    };

    enum class ModSourceTab : std::uint8_t {
        env1 = 0,
        env2,
        lfo1,
        lfo2,
        lfo3,
        step
    };

    enum classKnobFormat : std::uint8_t {
        normalized = 0,
        frequencyHz,
        seconds,
        decibels,
        pan,
        semitones,
        percent
    };

    using SequenceChangedCallback =
        std::function<void(const sequence::Sequence&)>;
    using TransportCallback = std::function<void(bool isPlaying)>;

    explicit AcidPanel(
        juce::AudioProcessorValueTreeState* apvts = nullptr,
        std::size_t slotIndex = 1,
        std::uint32_t initialGlobalSeed = 1234u);
    ~AcidPanel() override;

    void bindEngine(acid::AcidEngine* engine) noexcept;
    void setGlobalSeed(std::uint32_t seed) noexcept;
    void setSlotIndex(std::size_t slotIndex);
    void setPlayheadStep(int stepIndex) noexcept;

    void setSequence(const sequence::Sequence& sequence);
    [[nodiscard]] const sequence::Sequence& sequence() const noexcept {
        return sequence_;
    }

    void onSequenceChanged(SequenceChangedCallback callback) {
        sequenceChangedCallback_ = std::move(callback);
    }

    void onTransportChanged(TransportCallback callback) {
        transportCallback_ = std::move(callback);
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // Interactive ADSR Curve Visualizer (used in both ENVELOPE and MODULATION cards)
    class EnvelopeCurveView final : public juce::Component {
    public:
        void setParameters(float a, float d, float s, float r) noexcept;
        void paint(juce::Graphics& g) override;

    private:
        float attack_ { 0.00f };
        float decay_ { 0.25f };
        float sustain_ { 0.80f };
        float release_ { 0.35f };
    };

    // 5-Lane x 16-Step Matrix matching the V2 mockup:
    // Top step header 1..16, Note ("C3" / "--"), Accent (cyan squares),
    // Slide (green/teal bars), Gate (vertical cyan meters), Octave ("0" / "+1" / "-1"),
    // plus right-hand lane assignment selectors.
    class StepMatrixComponent final : public juce::Component {
    public:
        explicit StepMatrixComponent(AcidPanel& owner);

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;

    private:
        [[nodiscard]] int stepAtPosition(float x) const noexcept;
        [[nodiscard]] int laneAtPosition(float y) const noexcept;
        [[nodiscard]] static juce::String noteName(std::uint8_t midiNote,
                                                   std::int8_t octaveShift);

        AcidPanel& owner_;
        std::array<juce::ComboBox, 5> laneTargetBoxes_;
        int dragStep_ { -1 };
        int dragLane_ { -1 };
        int dragStartY_ {};
        std::uint8_t dragStartNote_ { 48 };
    };

    struct LabeledKnob {
        juce::Slider slider;
        juce::Label titleLabel;
        juce::Label valueLabel;
        std::string paramId;
        enum classKnobFormat format { enum classKnobFormat::normalized };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            attachment;
    };

    void timerCallback() override;
    void setupHeaderAndBanner();
    void setupRow1SynthModules();
    void setupRow2StepSequencer();
    void setupRow3ModAndPerformance();

    void configureRotaryKnob(LabeledKnob& knob, const juce::String& title,
                             const std::string& paramId, float defaultVal,
                             enum classKnobFormat format = enum classKnobFormat::normalized);
    [[nodiscard]] static juce::String formatKnobValue(
        double normValue, enum classKnobFormat format);

    void syncKnobToEngine(const std::string& paramId, float value);
    void updateActiveSubTab(SubTab tab);
    void updateEnvelopeTab(EnvelopeTab tab);
    void updateModSourceTab(ModSourceTab tab);
    void syncPlayModeFlags();

    void triggerGenerate();
    void triggerMutate();
    void triggerClear();
    void notifySequenceChanged();

    void drawModuleCard(juce::Graphics& g, juce::Rectangle<float> bounds,
                        const juce::String& title, bool drawPowerIcon = true) const;
    void drawHeroBanner(juce::Graphics& g, juce::Rectangle<float> bounds) const;

    juce::AudioProcessorValueTreeState* apvts_ {};
    acid::AcidEngine* engine_ {};
    std::size_t slotIndex_ { 1 };
    std::uint32_t globalSeed_ { 1234u };
    std::uint32_t mutationCounter_ { 1u };
    int playheadStep_ { -1 };
    bool isPlaying_ { false };
    SubTab activeTab_ { SubTab::sound };
    EnvelopeTab activeEnvTab_ { EnvelopeTab::amp };
    ModSourceTab activeModTab_ { ModSourceTab::env1 };

    // Header & Sub-Tabs
    juce::ComboBox presetSelector_;
    juce::TextButton prevPresetButton_ { "<" };
    juce::TextButton nextPresetButton_ { ">" };
    juce::TextButton favoriteButton_ { "♡" };
    juce::ComboBox midiChannelSelector_;
    std::array<juce::TextButton, 6> subTabButtons_ {
        juce::TextButton("SOUND"),
        juce::TextButton("PATTERN"),
        juce::TextButton("ROUTING"),
        juce::TextButton("ZONES"),
        juce::TextButton("MACROS"),
        juce::TextButton("ADVANCED")
    };

    // Row 1: OSCILLATOR, FILTER, ENVELOPE, DISTORTION / DRIVE, ACCENT, SLIDE, OUTPUT
    juce::ComboBox oscWaveBox_;
    std::array<LabeledKnob, 4> oscKnobs_; // Tune, Fine, Wave, PW

    juce::ComboBox filterModeBox_;
    std::array<LabeledKnob, 4> filterKnobs_; // Cutoff, Resonance, Env Amt, Key Track

    juce::TextButton envAmpTabBtn_ { "AMP" };
    juce::TextButton envFilterTabBtn_ { "FILTER" };
    EnvelopeCurveView envCurveView_;
    std::array<LabeledKnob, 4> envKnobs_; // A, D, S, R

    juce::ComboBox distModeBox_;
    std::array<LabeledKnob, 3> distKnobs_; // Drive, Tone, Mix

    LabeledKnob accentKnob_; // Amount
    LabeledKnob slideKnob_;  // Time
    std::array<LabeledKnob, 2> outputKnobs_; // Volume, Pan

    // Row 2: STEP SEQUENCER Header & Matrix
    juce::TextButton playButton_ { "▶" };
    juce::TextButton stopButton_ { "■" };
    juce::TextButton loopStepsButton_ { "16 Steps ↻" };
    juce::TextButton generateButton_ { "Generate" };
    juce::TextButton mutateButton_ { "Mutate" };
    juce::TextButton clearButton_ { "Clear" };

    juce::ComboBox patternPresetBox_;
    LabeledKnob swingKnob_;
    juce::ComboBox resolutionBox_;
    LabeledKnob densityKnob_;
    LabeledKnob variationKnob_;
    juce::ComboBox lengthBox_;

    StepMatrixComponent stepMatrix_;

    // Row 3: MODULATION, PERFORMANCE, PLAY MODE
    std::array<juce::TextButton, 6> modTabButtons_ {
        juce::TextButton("ENV 1"),
        juce::TextButton("ENV 2"),
        juce::TextButton("LFO 1"),
        juce::TextButton("LFO 2"),
        juce::TextButton("LFO 3"),
        juce::TextButton("STEP")
    };
    EnvelopeCurveView modCurveView_;
    juce::ComboBox modTargetBox_;
    std::array<LabeledKnob, 5> modKnobs_; // Amount, Attack, Decay, Sustain, Release

    std::array<LabeledKnob, 4> perfKnobs_; // Portamento, Unison, Detune, Spread
    juce::ComboBox unisonCountBox_;

    juce::ComboBox playModeBox_;
    juce::ToggleButton legatoToggle_ { "Legato" };
    juce::ToggleButton retriggerToggle_ { "Retrigger" };
    juce::ToggleButton velAccentToggle_ { "Vel -> Accent" };
    juce::ToggleButton slideOverlapToggle_ { "Slide on Overlap" };

    // Cached card rectangles for paint()
    juce::Rectangle<float> heroBannerBounds_;
    juce::Rectangle<float> oscCardBounds_;
    juce::Rectangle<float> filterCardBounds_;
    juce::Rectangle<float> envCardBounds_;
    juce::Rectangle<float> distCardBounds_;
    juce::Rectangle<float> accentCardBounds_;
    juce::Rectangle<float> slideCardBounds_;
    juce::Rectangle<float> outputCardBounds_;
    juce::Rectangle<float> sequencerCardBounds_;
    juce::Rectangle<float> modCardBounds_;
    juce::Rectangle<float> perfCardBounds_;
    juce::Rectangle<float> playModeCardBounds_;

    sequence::Sequence sequence_ {};
    acid::AcidPatternParams patternParams_ {};
    SequenceChangedCallback sequenceChangedCallback_;
    TransportCallback transportCallback_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AcidPanel)
};

} // namespace vstengine::ui