#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace vox::ui {

// Synchronized performance keyboard (design system 5.4) — not a piano roll,
// not a picture of keys, and not a fake MIDI output of the VST3. The product
// owns audio note ownership; this control reports pointer/keyboard intent and
// keeps its own held-note bookkeeping so focus loss, mouse leave, editor
// close and panic can release exactly the notes this control started.
// External (host) MIDI notes are an indication snapshot only: they are never
// started or released here. Mouse position is not aftertouch/MPE. Velocity
// is an explicit fixed audition value set by the product.
//
// Input ownership is distinct per source: the pointer, Space/Enter and Z/X
// each capture the key id they started and release only that key, so cursor
// or octave movement while a key is held can never strand or mis-target a
// release. In event mode (FX one-shot) a press shows a short "event
// launched" flash instead of pretending to be a sustained gate.
class VoxKeyboard final : public juce::Component, private juce::Timer,
                           private juce::SettableTooltipClient
{
public:
    VoxKeyboard();

    // UI-thread callbacks; the product forwards them to its own note path.
    std::function<void (int note, float velocity)> onNoteOn;
    std::function<void (int note)> onNoteOff;
    std::function<void ()> onAllNotesOff;

    // Explicit fixed audition velocity (1..127).
    void setVelocity (int midiVelocity);
    int getVelocity() const noexcept { return velocity_; }

    // Visible range: the product selects its supported scale (1..8 octaves).
    void setBaseOctave (int octave);
    int getBaseOctave() const noexcept { return baseOctave_; }
    void setVisibleOctaves (int octaves);
    int getVisibleOctaves() const noexcept { return visibleOctaves_; }

    // Host MIDI indication snapshot (bit per note, audio-thread produced by
    // the product). Display only — never started or released here.
    void setExternalNotes (std::array<std::uint64_t, 2> noteMask);

    // One-shot "event launched" flash (FX one-shot does not pretend to be a
    // held gate).
    void pulseNote (int note);
    bool pulseActive (int note) const noexcept;
    void setEventMode (bool oneShotEventMode) noexcept { eventMode_ = oneShotEventMode; }
    bool eventMode() const noexcept { return eventMode_; }
    // Product accent override (defaults to the shared design token).
    void setAccent (juce::Colour colour) noexcept { accent_ = colour; }

    // Release every note this control holds (focus loss, editor close,
    // panic). Fires onNoteOff per held note, then onAllNotesOff.
    void releaseAllNotes();
    int heldNoteCount() const noexcept;
    std::vector<int> heldNotes() const;
    int cursorNote() const noexcept { return cursorNote_; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void focusLost (juce::Component::FocusChangeType cause) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    static constexpr int lowNote = 0, highNote = 127;
    void timerCallback() override;
    int firstVisibleNote() const noexcept;
    int lastVisibleNote() const noexcept;
    // -1 outside the keys; black keys win over white in the same column.
    int noteAt (juce::Point<int> position) const noexcept;
    juce::Rectangle<int> keyBounds (int note) const noexcept;
    juce::Rectangle<int> keysArea() const noexcept;
    void pressNote (int note);
    void releaseNote (int note);
    void releaseCapture (int& capture);
    void repaintKey (int note);
    void clampCursorIntoView();
    bool isBlack (int note) const noexcept;
    bool external (int note) const noexcept;
    bool held (int note) const noexcept;

    std::array<bool, 128> held_{};
    std::array<std::uint64_t, 2> external_{};
    std::array<juce::int64, 128> pulseUntil_{};
    int velocity_ = 100;
    int baseOctave_ = 3;       // C3 at the left edge by default
    int visibleOctaves_ = 3;
    int pointerNote_ = -1;     // key started by the mouse (own ownership)
    int spaceNote_ = -1;       // key captured by Space/Enter
    int zNote_ = -1;           // key captured by Z (X releases it)
    bool cursorHeld_ = false;  // Space/Enter is physically down
    bool velocityDragging_ = false;
    bool eventMode_ = false;   // one-shot visual policy
    juce::Colour accent_;      // defaults to tokens::colour::accent
    int cursorNote_ = 60;      // keyboard-focus cursor (middle C)
    juce::Rectangle<int> octaveMinusBounds_, octavePlusBounds_, velocityBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxKeyboard)
};

} // namespace vox::ui
