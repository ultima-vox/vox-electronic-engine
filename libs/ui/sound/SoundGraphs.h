#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/common/UiComponents.h"

namespace vstengine::ui {

// Domain of the graph a SoundGraph draws. Each kind has its own recognisable
// grammar so a filter panel never shows a decorative sine wave.
enum class SoundGraphKind {
    none,
    oscillator,
    filter,
    envelope,
    modulation
};

// Waveform identity used by the oscillator graph.
//
// UI_PRODUCTION_IMPLEMENTATION_STANDARD.md section 12 requires the graph to
// reflect the selected oscillator rather than always drawing a generic sine.
enum class PreviewWaveShape { sine, saw, square, triangle, noise };

// A graph-led visualisation primitive for a sound module.
//
// Visual composition only. During Visual Gate A the shape is generated
// deterministically from a preview value; it is NOT sampled from DSP and does
// not claim to represent live engine output. At Functional Gate B the same
// component consumes a state snapshot instead, so the presentation does not
// have to be rewritten.
class SoundGraph final : public juce::Component {
public:
    explicit SoundGraph (SoundGraphKind kind = SoundGraphKind::none);

    void setAccent (juce::Colour newAccent);
    void setPreviewValue (float value01);
    void setWaveShape (PreviewWaveShape shape);
    void setCaption (juce::String text);

    // Fraction of the panel height the graph itself occupies. Filter and
    // envelope panels keep a control strip beneath the graph.
    void setVerticalWeight (float weight01);

    void paint (juce::Graphics&) override;

    // Shared plot-grid painter, reused by the matrix and sequencer primitives
    // so all graph surfaces share one grammar.
    static void paintGrid (juce::Graphics& g, juce::Rectangle<float> plot,
                           int columns, int rows, juce::Colour accent);

    // Dark graph surface with a subtle border, per the canonical surface stack.
    static void paintSurface (juce::Graphics& g, juce::Rectangle<float> area);

private:
    juce::Path buildPath (juce::Rectangle<float> plot) const;

    SoundGraphKind kind { SoundGraphKind::none };
    PreviewWaveShape waveShape { PreviewWaveShape::saw };
    juce::Colour accent { colours::primary };
    juce::String caption;
    float previewValue01 { 0.5f };
    float verticalWeight { 0.55f };
};

} // namespace vstengine::ui
