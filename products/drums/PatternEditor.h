#pragma once

// VOX Drums step editor (DRM-04b): edits the pattern slot the sequencer
// plays (drm.seq.pattern).
//
//  - toolbar: bar 1-4, steps per bar (16/32/64), bars (1-4), duplicate bar,
//    clear bar, swap A/B, copy A to B, new seed, undo, redo, MIDI export
//    (click: save; drag: drop the .mid file into the DAW);
//  - grid: 16 lanes x the bar's steps; a click toggles a step and selects
//    it; the playhead step is highlighted;
//  - inspector: velocity, accent, probability, ratchet and microtiming of
//    the selected step.
//
// Every change goes through edit(): it records the slot before and after in
// the processor's PatternHistory (undo/redo) and commits with
// PatternStore::setSlot. A knob drag is one undo step. A project restore
// (patternEpoch) clears the history. Message thread only.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PatternStore.h"
#include "VoxComponents.h"

#include <functional>
#include <memory>

namespace vox::drums {

class DrumsProcessor;

class PatternEditor final : public juce::Component {
public:
    PatternEditor (DrumsProcessor& processor, juce::Colour accent);
    ~PatternEditor() override;

    // Refreshes from the store and the transport (editor timer).
    void update (bool repaintIfChanged = true);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    //== tests and the panel
    const StoredPattern& shown() const noexcept { return shown_; }
    int slot() const noexcept { return slot_; }
    int bar() const noexcept { return bar_; }
    void selectBar (int bar);
    int selectedLane() const noexcept { return lane_; }
    int selectedStep() const noexcept { return step_; }
    void selectCell (int lane, int step);
    void clickCell (int lane, int step);              // as a mouse click on the cell
    juce::Rectangle<int> cellBounds (int lane, int step) const;
    juce::Slider* inspector (const juce::String& name);   // "velocity", "probability", "ratchet", "micro"
    juce::Button* accentToggle();
    juce::Button* button (const juce::String& name);      // "dup", "clear", "swap", "copyB", "seed", "undo", "redo", "midi", "lanes"
    juce::ComboBox* combo (const juce::String& name);     // "steps", "bars"
    juce::Button* barButton (int bar);
    // Writes the MIDI export of the shown slot to `file` (what a drag drops).
    bool writeMidi (const juce::File& file) const;
    juce::File dragFile() const;

private:
    class Grid;
    void edit (const juce::String& key, const std::function<void (StoredPattern&)>& change);
    void editBoth (const std::function<void (StoredPattern& a, StoredPattern& b)>& change);
    void refreshControls();
    void checkEpoch();
    void startDrag();

    DrumsProcessor& processor_;
    juce::Colour accent_;
    StoredPattern shown_;
    std::uint64_t generation_ = 0, epoch_ = 0;
    int slot_ = -1, bar_ = 0, playStep_ = -1, playBar_ = -1;
    int lane_ = 0, step_ = 0, lanePage_ = 0;
    bool refreshing_ = false, gesture_ = false, dragging_ = false;
    juce::String lastKey_;

    std::array<std::unique_ptr<vox::ui::VoxButton>, kit::maxPatternBars> bars_;
    std::unique_ptr<vox::ui::VoxComboBox> steps_, barCount_;
    std::unique_ptr<vox::ui::VoxButton> dup_, clear_, swap_, copyB_, seed_, undo_, redo_, midi_, lanes_;
    std::unique_ptr<juce::Label> abLabel_;
    std::unique_ptr<Grid> grid_;
    std::unique_ptr<vox::ui::VoxKnob> velocity_, probability_, ratchet_, micro_;
    std::unique_ptr<juce::ToggleButton> accentButton_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternEditor)
};

} // namespace vox::drums
