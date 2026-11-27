#include "VoxKeyboard.h"
#include "../Tokens.h"
#include "../Typography.h"

namespace vox::ui {

namespace {
constexpr int controlClusterWidth = 176;
constexpr juce::int64 pulseMs = 180;

int whiteIndexInOctave (int note) noexcept
{
    switch (note % 12)
    {
        case 0: return 0; case 2: return 1; case 4: return 2; case 5: return 3;
        case 7: return 4; case 9: return 5; case 11: return 6;
        default: return -1;
    }
}
} // namespace

VoxKeyboard::VoxKeyboard()
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("Performance keyboard: click or drag across keys; arrows move the cursor key; "
                "Space/Enter or Z presses it, X releases it; OCT-/+ shift the range; VEL is the "
                "explicit fixed velocity. Host MIDI is indication only and is never released here.");
    for (auto& until : pulseUntil_) until = 0;
}

void VoxKeyboard::setVelocity (int midiVelocity)
{
    const auto clamped = juce::jlimit (1, 127, midiVelocity);
    if (clamped == velocity_) return;
    velocity_ = clamped;
    repaint (velocityBounds_);
}

void VoxKeyboard::setBaseOctave (int octave)
{
    const auto clamped = juce::jlimit (0, 8, octave);
    if (clamped == baseOctave_) return;
    // Never leave held notes stranded on a shifted range.
    releaseAllNotes();
    baseOctave_ = clamped;
    repaint();
}

void VoxKeyboard::setVisibleOctaves (int octaves)
{
    const auto clamped = juce::jlimit (1, 8, octaves);
    if (clamped == visibleOctaves_) return;
    releaseAllNotes();
    visibleOctaves_ = clamped;
    resized();
    repaint();
}

void VoxKeyboard::setExternalNotes (std::array<std::uint64_t, 2> noteMask)
{
    if (noteMask == external_) return;
    for (int note = lowNote; note <= highNote; ++note)
    {
        const auto bit = std::uint64_t { 1 } << (note & 63);
        const bool was = (external_[note >> 6] & bit) != 0;
        const bool now = (noteMask[note >> 6] & bit) != 0;
        if (was != now) repaintKey (note);
    }
    external_ = noteMask;
}

void VoxKeyboard::pulseNote (int note)
{
    if (note < lowNote || note > highNote) return;
    pulseUntil_[static_cast<std::size_t> (note)] = juce::Time::getMillisecondCounter() + pulseMs;
    repaintKey (note);
    startTimer (30);
}

void VoxKeyboard::releaseAllNotes()
{
    for (int note = lowNote; note <= highNote; ++note)
        if (held_[static_cast<std::size_t> (note)]) releaseNote (note);
    dragNote_ = -1;
    if (onAllNotesOff) onAllNotesOff();
}

int VoxKeyboard::heldNoteCount() const noexcept
{
    int count = 0;
    for (bool h : held_) count += h ? 1 : 0;
    return count;
}

std::vector<int> VoxKeyboard::heldNotes() const
{
    std::vector<int> out;
    for (int note = lowNote; note <= highNote; ++note)
        if (held_[static_cast<std::size_t> (note)]) out.push_back (note);
    return out;
}

int VoxKeyboard::firstVisibleNote() const noexcept { return juce::jlimit (0, 127, baseOctave_ * 12); }
int VoxKeyboard::lastVisibleNote() const noexcept
{
    return juce::jlimit (0, 127, firstVisibleNote() + visibleOctaves_ * 12 - 1);
}

bool VoxKeyboard::isBlack (int note) const noexcept
{
    switch (note % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}
bool VoxKeyboard::external (int note) const noexcept
{
    return (external_[note >> 6] & (std::uint64_t { 1 } << (note & 63))) != 0;
}
bool VoxKeyboard::held (int note) const noexcept
{
    return note >= lowNote && note <= highNote && held_[static_cast<std::size_t> (note)];
}

juce::Rectangle<int> VoxKeyboard::keysArea() const noexcept
{
    return getLocalBounds().removeFromRight (juce::jmax (70, getWidth() - controlClusterWidth));
}

juce::Rectangle<int> VoxKeyboard::keyBounds (int note) const noexcept
{
    const auto area = keysArea();
    const int first = firstVisibleNote(), last = lastVisibleNote();
    if (note < first || note > last || area.isEmpty()) return {};
    const int whites = visibleOctaves_ * 7;
    const double whiteW = static_cast<double> (area.getWidth()) / whites;
    const int octave = (note - first) / 12;
    if (isBlack (note))
    {
        const int wi = whiteIndexInOctave (note - 1);
        const double x = area.getX() + (octave * 7 + wi + 1) * whiteW - whiteW * 0.3;
        return juce::Rectangle<double> (x, area.getY() + 1.0, whiteW * 0.6, area.getHeight() * 0.62)
            .toNearestInt();
    }
    const int wi = whiteIndexInOctave (note);
    const double x = area.getX() + (octave * 7 + wi) * whiteW;
    return juce::Rectangle<double> (x, area.getY(), whiteW + 0.5, area.getHeight()).toNearestInt();
}

int VoxKeyboard::noteAt (juce::Point<int> position) const noexcept
{
    // Black keys are checked first: they overlap the white columns.
    for (int note = firstVisibleNote(); note <= lastVisibleNote(); ++note)
        if (isBlack (note) && keyBounds (note).contains (position)) return note;
    for (int note = firstVisibleNote(); note <= lastVisibleNote(); ++note)
        if (!isBlack (note) && keyBounds (note).contains (position)) return note;
    return -1;
}

void VoxKeyboard::pressNote (int note)
{
    if (note < lowNote || note > highNote || held_[static_cast<std::size_t> (note)]) return;
    held_[static_cast<std::size_t> (note)] = true;
    repaintKey (note);
    if (onNoteOn) onNoteOn (note, static_cast<float> (velocity_) / 127.0f);
}

void VoxKeyboard::releaseNote (int note)
{
    if (note < lowNote || note > highNote || !held_[static_cast<std::size_t> (note)]) return;
    held_[static_cast<std::size_t> (note)] = false;
    repaintKey (note);
    if (onNoteOff) onNoteOff (note);
}

void VoxKeyboard::repaintKey (int note)
{
    const auto bounds = keyBounds (note);
    if (!bounds.isEmpty()) repaint (bounds.expanded (2));
}

void VoxKeyboard::resized()
{
    auto area = getLocalBounds().reduced (6);
    octaveMinusBounds_ = area.removeFromLeft (46).reduced (2);
    octavePlusBounds_ = area.removeFromLeft (46).reduced (2);
    area.removeFromLeft (6);
    velocityBounds_ = area.removeFromLeft (72).reduced (2);
}

void VoxKeyboard::mouseDown (const juce::MouseEvent& e)
{
    if (octaveMinusBounds_.contains (e.getPosition()))
    {
        setBaseOctave (baseOctave_ - 1);
        return;
    }
    if (octavePlusBounds_.contains (e.getPosition()))
    {
        setBaseOctave (baseOctave_ + 1);
        return;
    }
    if (velocityBounds_.contains (e.getPosition()))
    {
        velocityDragging_ = true;
        const auto t = juce::jlimit (0.0, 1.0, (e.x - velocityBounds_.getX()) /
                                              static_cast<double> (juce::jmax (1, velocityBounds_.getWidth())));
        setVelocity (static_cast<int> (std::lround (t * 126.0)) + 1);
        return;
    }
    const auto note = noteAt (e.getPosition());
    if (note < 0) return;
    if (held_[static_cast<std::size_t> (note)])
    {
        // Repeated note: close the previous press, then start a fresh one so
        // every press keeps a matching release.
        releaseNote (note);
    }
    pressNote (note);
    dragNote_ = note;
}

void VoxKeyboard::mouseDrag (const juce::MouseEvent& e)
{
    if (velocityDragging_)
    {
        const auto t = juce::jlimit (0.0, 1.0, (e.x - velocityBounds_.getX()) /
                                              static_cast<double> (juce::jmax (1, velocityBounds_.getWidth())));
        setVelocity (static_cast<int> (std::lround (t * 126.0)) + 1);
        return;
    }
    if (dragNote_ < 0) return;
    const auto note = noteAt (e.getPosition());
    if (note >= 0 && note != dragNote_)
    {
        // Drag between keys: finish the old note, start the new one.
        const auto previous = dragNote_;
        dragNote_ = -1;
        releaseNote (previous);
        pressNote (note);
        dragNote_ = note;
    }
}

void VoxKeyboard::mouseUp (const juce::MouseEvent&)
{
    velocityDragging_ = false;
    if (dragNote_ >= 0) releaseNote (dragNote_);
    dragNote_ = -1;
}

void VoxKeyboard::mouseExit (const juce::MouseEvent&)
{
    // Leaving the keyboard with the button down never strands a note.
    velocityDragging_ = false;
    if (dragNote_ >= 0) releaseNote (dragNote_);
    dragNote_ = -1;
}

void VoxKeyboard::focusLost (juce::Component::FocusChangeType)
{
    velocityDragging_ = false;
    releaseAllNotes();
    cursorHeld_ = false;
}

bool VoxKeyboard::keyPressed (const juce::KeyPress& key)
{
    // Only receives keys while this component has focus — never globally.
    if (key == juce::KeyPress::leftKey)
    {
        cursorNote_ = juce::jmax (lowNote, cursorNote_ - 1);
        repaint();
        return true;
    }
    if (key == juce::KeyPress::rightKey)
    {
        cursorNote_ = juce::jmin (highNote, cursorNote_ + 1);
        repaint();
        return true;
    }
    if (key == juce::KeyPress::upKey)
    {
        setBaseOctave (baseOctave_ + 1);
        return true;
    }
    if (key == juce::KeyPress::downKey)
    {
        setBaseOctave (baseOctave_ - 1);
        return true;
    }
    if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
    {
        if (!cursorHeld_)
        {
            cursorHeld_ = true;
            if (held_[static_cast<std::size_t> (cursorNote_)]) releaseNote (cursorNote_);
            pressNote (cursorNote_);
            dragNote_ = cursorNote_;
            startTimer (30); // poll for key release (JUCE has no keyUp event)
        }
        return true;
    }
    if (key == juce::KeyPress ('z') || key == juce::KeyPress ('Z'))
    {
        if (!held_[static_cast<std::size_t> (cursorNote_)]) pressNote (cursorNote_);
        return true;
    }
    if (key == juce::KeyPress ('x') || key == juce::KeyPress ('X'))
    {
        releaseNote (cursorNote_);
        return true;
    }
    return false;
}

void VoxKeyboard::paint (juce::Graphics& g)
{
    using namespace tokens::colour;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (bounds, tokens::radius::medium);
    g.setColour (border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), tokens::radius::medium, 1.0f);

    // Control cluster: octave buttons + explicit fixed velocity.
    g.setColour (control);
    g.fillRoundedRectangle (octaveMinusBounds_.toFloat(), tokens::radius::small);
    g.fillRoundedRectangle (octavePlusBounds_.toFloat(), tokens::radius::small);
    g.fillRoundedRectangle (velocityBounds_.toFloat(), tokens::radius::small);
    g.setColour (text);
    g.setFont (typography::sectionTitle());
    g.drawText ("OCT-", octaveMinusBounds_, juce::Justification::centred);
    g.drawText ("OCT+", octavePlusBounds_, juce::Justification::centred);
    g.setColour (velocityDragging_ ? accent : textSecondary);
    g.setFont (typography::makeFont (typography::controlLabelPx, "SemiBold"));
    g.drawText ("VEL " + juce::String (velocity_), velocityBounds_, juce::Justification::centred);

    // Piano keys.
    const auto now = juce::Time::getMillisecondCounter();
    const auto white = text.interpolatedWith (panel, 0.18); // light rubber faces
    const auto black = control;
    for (int note = firstVisibleNote(); note <= lastVisibleNote(); ++note)
    {
        auto kb = keyBounds (note);
        if (kb.isEmpty()) continue;
        const bool isHeld = held (note);
        const bool isExternal = external (note);
        const bool pulsing = pulseUntil_[static_cast<std::size_t> (note)] > now;
        if (isBlack (note))
        {
            auto face = black;
            if (pulsing) face = accentHover;
            else if (isHeld) face = accent;
            else if (isExternal) face = accentDim;
            g.setColour (face);
            g.fillRoundedRectangle (kb.toFloat(), 2.0f);
            g.setColour (border);
            g.drawRoundedRectangle (kb.toFloat().reduced (0.5f), 2.0f, 1.0f);
        }
        else
        {
            auto face = white;
            if (pulsing) face = accentHover;
            else if (isHeld) face = accent;
            else if (isExternal) face = white.interpolatedWith (accentDim, 0.45);
            g.setColour (face);
            g.fillRoundedRectangle (kb.toFloat().reduced (0.5f, 0.5f), 2.0f);
            g.setColour (isHeld || pulsing ? border : borderSubtle);
            g.drawRoundedRectangle (kb.toFloat().reduced (0.5f), 2.0f, 1.0f);
            if (note % 12 == 0)
            {
                g.setColour (isHeld || pulsing ? background : textMuted);
                g.setFont (typography::valueText());
                g.drawText ("C" + juce::String (note / 12), kb.removeFromBottom (14),
                            juce::Justification::centred);
            }
        }
    }

    // Keyboard-focus cursor outline.
    const auto cursor = keyBounds (cursorNote_);
    if (cursor.isEmpty() && hasKeyboardFocus (true))
    {
        // Cursor outside the visible range: no outline until shifted back.
    }
    else if (hasKeyboardFocus (true))
    {
        g.setColour (accent);
        g.drawRect (cursor.expanded (1), 2);
    }
}

void VoxKeyboard::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    bool any = false;
    for (auto until : pulseUntil_)
        if (until > now) { any = true; break; }
    // Space/Enter press is released when the host key comes back up (JUCE
    // delivers no keyUp event, so the held state is polled while focused).
    if (cursorHeld_ && !juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey) &&
        !juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::returnKey))
    {
        cursorHeld_ = false;
        if (dragNote_ == cursorNote_)
        {
            releaseNote (cursorNote_);
            dragNote_ = -1;
        }
        repaint();
    }
    if (any) repaint();
    if (!any && !cursorHeld_) stopTimer();
}

} // namespace vox::ui
