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

// The MIDI button: one press either clicks or drags, never both (issue 131,
// H1). Release without moving more than dragThreshold pixels, over the
// button: onClick (save). Moving further starts onDragStart once and the
// rest of that gesture, wherever it is released, never clicks. A drag that
// fails or whose mouse-up never arrives (the OS drag took the mouse) ends at
// the next mouse-down, so the next ordinary click saves again. The click is
// decided from the event positions, not from juce::Button's hover state,
// which Button::mouseDrag re-arms on every drag event over the button.
// Keyboard and accessibility activation still go through onClick.
class MidiDragButton final : public vox::ui::VoxButton {
public:
    using vox::ui::VoxButton::VoxButton;
    static constexpr int dragThreshold = 6;
    std::function<void()> onDragStart;
    bool dragInProgress() const noexcept { return dragStarted_; }

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool pressed_ = false, dragStarted_ = false;
};

struct ExportSnapshot;

class PatternEditor final : public juce::Component {
public:
    PatternEditor (DrumsProcessor& processor, juce::Colour accent);
    ~PatternEditor() override;

    // Refreshes from the store and the transport (editor timer).
    void update (bool repaintIfChanged = true);

    void paint (juce::Graphics&) override;
    void resized() override;

    //== tests and the panel
    const StoredPattern& shown() const noexcept { return shown_; }
    int slot() const noexcept { return slot_; }
    // DRM-05: edit song bar `bar` instead of the pattern slot (-1: the
    // slot). Any change makes the bar unique (its own cells); the pattern
    // it referenced is not changed. A/B and the bar count do not apply.
    void editSongBar (int bar);
    int songBar() const noexcept { return songBar_; }
    juce::Button* lockToggle();
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
    // Writes the MIDI export of the shown slot to `file`.
    bool writeMidi (const juce::File& file);
    // "" or why the last export failed (also shown next to the toolbar).
    juce::String exportMessage() const { return exportMessage_; }
    // What a click or a drag exports now (the scope chosen in the editor
    // state: Pattern or Entire Song), frozen; both write it with writeSnapshot.
    std::shared_ptr<const ExportSnapshot> snapshot() const;
    static bool writeSnapshot (const ExportSnapshot& snapshot, const juce::File& file);
    // What a drag drops: the export in a new folder unique to this editor
    // instance and this drag (review 6753052108); empty on failure.
    juce::File exportForDrag();
    // Removes this instance's drag folders older than dragLifetime (never
    // its newest) and other instances' folders older than staleLifetime.
    void cleanupDragFiles (juce::Time now);
    static juce::File dragRoot();
    // Tests: a private root, so a test's cleanup (with a simulated later
    // clock) never touches another process's drags in the shared temp folder.
    static void setDragRootForTesting (const juce::File& root);
    static inline const juce::RelativeTime dragLifetime = juce::RelativeTime::minutes (10);
    static inline const juce::RelativeTime staleLifetime = juce::RelativeTime::days (1);
    // Tests: replace the save dialog (gets what a click froze) and the
    // OS drag (gets the written file), so a gesture can be checked
    // without a dialog or a drop target. Empty in the product.
    std::function<void (std::shared_ptr<const ExportSnapshot>)> saveDialogForTesting;
    std::function<void (const juce::File&)> externalDragForTesting;
    juce::String fileName() const;        // "VOX Drums Pattern N.mid"
    juce::File saveAsDefault() const;     // the save dialog's suggestion (Documents)

private:
    class Grid;
    void edit (const juce::String& key, const std::function<void (StoredPattern&)>& change);
    void editBoth (const std::function<void (StoredPattern& a, StoredPattern& b)>& change);
    void refreshControls();
    void checkEpoch();
    void startDrag();
    void openSaveDialog();
    juce::File newDragFile();
    // "<creation ms>-<UUID>": the age of this instance's folder for others' cleanup.
    const juce::String instanceId_ = juce::String (juce::Time::currentTimeMillis()) + "-" + juce::Uuid().toString();
    int dragCount_ = 0;

    DrumsProcessor& processor_;
    juce::Colour accent_;
    StoredPattern shown_;
    std::uint64_t generation_ = 0, epoch_ = 0;
    int slot_ = -1, bar_ = 0, playStep_ = -1, playBar_ = -1, songBar_ = -1, shownSongBar_ = -1;
    int lane_ = 0, step_ = 0, lanePage_ = 0;
    bool refreshing_ = false, gesture_ = false;
    juce::String lastKey_, exportMessage_;
    void exported (bool ok, const juce::File& file);

    std::array<std::unique_ptr<vox::ui::VoxButton>, kit::maxPatternBars> bars_;
    std::unique_ptr<vox::ui::VoxComboBox> steps_, barCount_;
    std::unique_ptr<vox::ui::VoxButton> dup_, clear_, swap_, copyB_, seed_, undo_, redo_, lanes_;
    std::unique_ptr<MidiDragButton> midi_;
    std::unique_ptr<juce::Label> abLabel_;
    std::unique_ptr<Grid> grid_;
    std::unique_ptr<vox::ui::VoxKnob> velocity_, probability_, ratchet_, micro_;
    std::unique_ptr<juce::ToggleButton> accentButton_, lockButton_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternEditor)
};

} // namespace vox::drums
