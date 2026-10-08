#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace vox::ui {

// N mutually exclusive segments inside one bordered control.
//
// Composition consumer for OSCILLATOR 1/2/3, MODULATION ENV1/ENV2/LFO...,
// Acid ENVELOPE AMP|FILTER and the footer KEYBOARD|CHORDS|SCALE row.
//
// The segments are separated by a real gutter rather than sharing one
// continuous bar, because the accepted render reads them as individual
// affordances. The gutter is part of this primitive, not of the caller's layout.
class VoxSegmentedControl : public juce::Component
{
public:
    VoxSegmentedControl();

    void setSegments (juce::StringArray newSegments);
    [[nodiscard]] const juce::StringArray& getSegments() const noexcept { return segments; }
    [[nodiscard]] int getNumSegments() const noexcept { return segments.size(); }

    // Clamps to the valid range. `sendChange` is false for programmatic
    // selection during layout/binding, so a rebuild cannot re-enter the product.
    void setSelectedIndex (int index, bool sendChange = false);
    [[nodiscard]] int getSelectedIndex() const noexcept { return selectedIndex; }

    void setSegmentEnabled (int index, bool shouldBeEnabled);
    [[nodiscard]] bool isSegmentEnabled (int index) const noexcept;

    void setSegmentGap (int newGap);
    [[nodiscard]] int getSegmentGap() const noexcept { return segmentGap; }

    std::function<void (int)> onChanged;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    [[nodiscard]] juce::Rectangle<int> boundsForIndex (int index) const noexcept;
    [[nodiscard]] int indexAt (juce::Point<int> position) const noexcept;
    [[nodiscard]] int nextEnabledIndex (int start, int direction) const noexcept;

    juce::StringArray segments;
    std::vector<bool> enabled;
    int selectedIndex = -1;
    int hoverIndex = -1;
    int segmentGap = 5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxSegmentedControl)
};

} // namespace vox::ui
