#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/common/UiComponents.h"

namespace vstengine::ui {

// Reusable presentation primitive for one sound module panel (OSCILLATOR,
// FILTER, AMP ENVELOPE, DRIVE, MATRIX, ...).
//
// This owns surface and header chrome plus the content rectangle, nothing else.
// It has no domain knowledge, no parameter state and no instrument-specific
// behaviour, so the Psy Bass, Acid and generic workspaces all compose the same
// primitive instead of sharing one hardcoded panel set.
//
// Visual composition only. Nothing here reads or writes engine state.
class SoundModulePanel final : public juce::Component {
public:
    explicit SoundModulePanel (juce::String titleText = {});

    void setSubtitle (juce::String text);
    void setAccent (juce::Colour newAccent);
    [[nodiscard]] juce::Colour getAccent() const noexcept { return accent; }

    // Compact right-aligned selector text in the header (waveform type, filter
    // type, drive type, ...). Presentation only during Visual Gate A.
    void setHeaderSelector (juce::String text);

    void setStatusDotVisible (bool shouldBeVisible);
    void setStatusDotActive (bool shouldBeActive);

    // Below this height the header chrome stops being drawn so that compact
    // breakpoints degrade gracefully instead of clipping text.
    static constexpr int minimumUsefulHeight = 46;

    [[nodiscard]] juce::Rectangle<int> getHeaderBounds() const;
    [[nodiscard]] juce::Rectangle<int> getContentBounds() const;

    void paint (juce::Graphics&) override;

private:
    juce::String title;
    juce::String subtitle;
    juce::String headerSelector;
    juce::Colour accent { colours::primary };
    bool statusDotVisible { true };
    bool statusDotActive { false };
};

} // namespace vstengine::ui
