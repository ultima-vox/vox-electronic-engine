#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

#include "ui/common/UiComponents.h"

namespace vstengine::ui {

// ===========================================================================
// GATE A PREVIEW DATA
//
// Everything in this file is Visual Gate A preview presentation.
//
// The matrix and sequencer below show the information density the accepted
// renders require, but their content is generated preview data. It does not
// enter persisted instrument state, is not written to any preset or sequence
// payload, and is not the production routing/sequencer implementation.
//
// Functional Gate B replaces the data source (setRoutes / setPattern), not the
// visual component.
// ===========================================================================

// One preview modulation route. At Gate B this is fed from the real
// modulation descriptor rather than from previewDefaults().
struct PreviewModRoute {
    juce::String source;
    juce::String destination;
    float amount {};
    bool enabled { true };
};

// Table-like modulation matrix: number, source, amount, destination.
//
// Deliberately not a decorative dot grid (UI_COMPONENT_CATALOG.md 9.4).
class GateAVisualMatrix final : public juce::Component {
public:
    GateAVisualMatrix();

    void setAccent (juce::Colour newAccent);
    void setRoutes (std::vector<PreviewModRoute> newRoutes);

    [[nodiscard]] static std::vector<PreviewModRoute> previewDefaults();

    void paint (juce::Graphics&) override;

private:
    std::vector<PreviewModRoute> routes;
    juce::Colour accent { colours::primary };
};

// One preview sequencer step across the 16-step Acid pattern.
struct PreviewSequencerStep {
    bool noteOn { true };
    juce::String noteName { "C3" };
    int octaveOffset {};
    bool accent {};
    bool slide {};
    float gate { 0.75f };
};

// The five Acid lanes, in render order.
enum class PreviewSequencerLane { note, accent, slide, gate, octave, count };

class GateAVisualSequencer final : public juce::Component {
public:
    static constexpr int stepCount = 16;

    GateAVisualSequencer();

    void setAccent (juce::Colour newAccent);
    void setPattern (std::vector<PreviewSequencerStep> newPattern);

    [[nodiscard]] static std::vector<PreviewSequencerStep> previewPattern();

    void paint (juce::Graphics&) override;

private:
    void paintToolbar (juce::Graphics&, juce::Rectangle<float> area);
    void paintGrid (juce::Graphics&, juce::Rectangle<float> area);

    std::vector<PreviewSequencerStep> pattern;
    juce::Colour accent { colours::primary };
};

// Compact radio-style option list used by Acid PLAY MODE and by target/mode
// selectors.
//
// Visual Gate A preview only: it displays a selection but holds no state and is
// not interactive, because the underlying parameters do not exist yet.
class GateAVisualOptionList final : public juce::Component {
public:
    GateAVisualOptionList();

    void setOptions (std::vector<juce::String> newOptions);
    void setSelectedIndex (int index);
    void setAccent (juce::Colour newAccent);

    void paint (juce::Graphics&) override;

private:
    std::vector<juce::String> options;
    int selectedIndex { 0 };
    juce::Colour accent { colours::primary };
};

// Compact strip of labelled tabs used by the modulation panels
// (ENV 1 / ENV 2 / LFO 1 / LFO 2 / LFO 3 / STEP).
//
// Presentation only at Gate A: it selects which modulation graph is shown and
// holds no modulation state of its own.
class SoundTabStrip final : public juce::Component {
public:
    SoundTabStrip();

    void setTabs (std::vector<juce::String> newTabs);
    void setSelectedIndex (int index);
    void setAccent (juce::Colour newAccent);

    void paint (juce::Graphics&) override;

private:
    std::vector<juce::String> tabs;
    int selectedIndex { 0 };
    juce::Colour accent { colours::primary };
};

} // namespace vstengine::ui
