#pragma once

// VOX Drums editor pages (DRM-07): MIXER, GENERATE, ROUTING, PRESETS,
// ADVANCED and the song timeline of PATTERN/SONG. PERFORMANCE, SOUND and
// the step grid are in DrumsEditor / PatternEditor.
//
// Every control here is bound: host parameters through JUCE attachments
// (automation and gestures), structural settings through the stores, with
// the editor timer reflecting changes made elsewhere. Nothing on a page is
// decoration. Message thread only; the audio thread's state is reached
// only through the processor's thread-safe accessors.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PatternStore.h"
#include "VoxComponents.h"

#include <functional>
#include <memory>

namespace vox::drums {

class DrumsProcessor;

class Page : public juce::Component {
public:
    // Editor timer (30 Hz), while the page is shown; `force` after a page change.
    virtual void update() {}
    // Host-parameter controls by parameter ID.
    virtual juce::Slider* slider (const juce::String&) { return nullptr; }
    virtual juce::Button* button (const juce::String&) { return nullptr; }
    virtual juce::ComboBox* combo (const juce::String&) { return nullptr; }
    // Other controls by name (tests): see each page.
    virtual juce::Component* named (const juce::String&) { return nullptr; }
};

// MIXER: 16 strips (meter, level, pan, send 1, send 2, mute, solo, output)
// and the returns (delay, reverb) with the Main meter.
// named: "meter.<pad 1..16>", "meter.main", "out.<pad>" (label).
std::unique_ptr<Page> makeMixerPage (DrumsProcessor&);

// GENERATE: style, eight amounts, seed, lane locks, lanes to mutate and the
// actions. named: "style" (combo), "density", "groove", "syncopation",
// "swing", "humanize", "complexity", "fills", "variation" (sliders),
// "seed" (label), "lock.<lane>", "mutate.<lane>" (buttons), "pattern",
// "song", "mutate", "fill", "variation.ab", "undo", "redo" (buttons),
// "status" (label).
std::unique_ptr<Page> makeGeneratePage (DrumsProcessor&);

// ROUTING: per pad MIDI note and output; sequencer mode and sync state;
// which outputs the host enabled. named: "note.<pad>", "output.<pad>"
// (combos), "state.<pad>" (label), "sync" (label).
std::unique_ptr<Page> makeRoutingPage (DrumsProcessor&);

// PRESETS: factory and user kits, pads, patterns and songs; save, load,
// Undo Load, portable kit import/export. named: "type" (combo), "list"
// (ListBox), "name" (TextEditor), "load", "save", "undo", "import",
// "export" (buttons), "status" (label).
std::unique_ptr<Page> makePresetsPage (DrumsProcessor&, std::function<int()> selectedPad);

// ADVANCED: voice limit, velocity sensitivity, choke groups, panic and
// diagnostics. named: "voices" (combo), "panic" (button), "diagnostics"
// (label).
std::unique_ptr<Page> makeAdvancedPage (DrumsProcessor&);

// The song timeline (PATTERN/SONG): bars in rows of 16, selection, playhead
// and loop region; mode, length, placement and bar editing.
class SongStrip final : public juce::Component {
public:
    SongStrip (DrumsProcessor&, juce::Colour accent);
    ~SongStrip() override;

    void update();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    // The height the strip needs for the song's length at this width.
    int preferredHeight() const;
    int selectionStart() const noexcept { return selFrom_; }
    int selectionLength() const noexcept { return selCount_; }
    void select (int from, int count);
    juce::Rectangle<int> barBounds (int bar) const;
    // "mode", "loop", "edit" (toggles), "place", "insert", "delete", "dup",
    // "clear", "copy", "paste", "unique", "loopsel" (buttons).
    juce::Button* button (const juce::String& name);
    // "length", "slot", "variation", "scope".
    juce::ComboBox* combo (const juce::String& name);
    // The bar to edit in the step grid (-1: the pattern slot).
    std::function<void (int bar)> onEditBar;
    int editedBar() const noexcept { return editing_ ? selFrom_ : -1; }

private:
    int barAt (juce::Point<int> p) const;
    void apply (const std::function<void (SongData&)>& change);
    void refresh();

    DrumsProcessor& processor_;
    juce::Colour accent_;
    SongData song_;
    std::uint64_t generation_ = 0;
    int selFrom_ = 0, selCount_ = 1, anchor_ = 0, playing_ = -1;
    bool editing_ = false, refreshing_ = false;
    std::vector<SongBar> clipboard_;
    juce::Rectangle<int> grid_;
    std::unique_ptr<vox::ui::VoxButton> mode_, loop_, edit_, place_, insert_, delete_, dup_, clear_, copy_, paste_, unique_, loopSel_;
    std::unique_ptr<vox::ui::VoxComboBox> length_, slot_, variation_, scope_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongStrip)
};

} // namespace vox::drums
