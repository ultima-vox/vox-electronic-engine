#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "common/UiComponents.h"

namespace vstengine::ui {

// Instrument identity presented by the hero band.
//
// This is presentation identity only. It selects artwork and copy; it never
// selects behaviour, DSP or routing.
enum class HeroIdentity { generic, psyBass, acid };

// Shallow instrument hero / identity banner.
//
// Extracted from the editor so the shell can own it directly. Per
// UI_PRODUCTION_IMPLEMENTATION_STANDARD.md section 17 the band is LEFT identity,
// CENTRE low-contrast identity artwork, RIGHT capability copy, and it collapses
// at compact breakpoints.
//
// The artwork is generated vector art for Visual Gate A composition review. It
// is a real identity anchor rather than a decorative waveform strip, but it is
// not the final packaged instrument artwork.
class HeroBanner final : public juce::Component {
public:
    HeroBanner();

    void setIdentity (HeroIdentity newIdentity, juce::String instrumentName);
    void setAccent (juce::Colour newAccent);

    // Below this height the artwork is dropped and the band degrades to a
    // compact identity strip instead of clipping.
    static constexpr int compactHeight = 74;

    void paint (juce::Graphics&) override;

private:
    void paintArtwork (juce::Graphics&, juce::Rectangle<float> area);

    HeroIdentity identity { HeroIdentity::generic };
    juce::String name { "INSTRUMENT" };
    juce::Colour accent { colours::primary };
};

} // namespace vstengine::ui