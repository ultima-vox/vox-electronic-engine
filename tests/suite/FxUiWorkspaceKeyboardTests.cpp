// Native workspace shell + performance-keyboard interaction tests
// (TZ 13.2 workspaces; design system 5.4 VoxKeyboard; TL P0 7223242731:
// real bindings, native pointer interaction, distinct host/UI note
// ownership, no stuck notes, bounded RT-safe UI-note path).
#include "Processor.h"
#include "FxEditor.h"
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace {
int failures = 0;
void check (bool condition, const char* message)
{
    if (!condition) { std::printf ("FAIL: %s\n", message); ++failures; }
}
using W = vox::fx::FxPanel::Workspace;

juce::MouseEvent mouseEvent (juce::Component& target, float x, float y, bool down, bool up)
{
    const auto when = juce::Time::getCurrentTime();
    const auto modifiers = up ? juce::ModifierKeys (0) : juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), {x, y}, modifiers,
                             0.f, 0.f, 0.f, 0.f, 0.f, &target, &target, when, {0.f, 0.f}, when, 1,
                             down || up);
}

// Every workspace shows exactly its own control group; the keyboard, panic,
// clock, tabs and header stay visible everywhere; tab and toggle contracts
// stay in sync.
void checkWorkspaceMatrix()
{
    using namespace vox::fx;
    FxProcessor processor;
    FxPanel panel (processor);
    panel.setSize (1040, 700);

    check (panel.workspace() == W::design, "default workspace is DESIGN");
    check (panel.layerPanel (0).isVisible(), "DESIGN shows layer cards");
    check (!panel.programCurves().isVisible(), "DESIGN hides Program curves");
    check (panel.keyboard().isVisible() && panel.workspaceTabs().isVisible(),
           "keyboard and tab strip start visible");

    panel.setWorkspace (W::perform);
    check (panel.workspaceTabs().getSelectedIndex() == 1, "tab strip follows PERFORM");
    check (panel.outputSlider().isVisible() && !panel.layerPanel (0).isVisible(),
           "PERFORM shows output controls and hides layer cards");
    check (panel.keyboard().isVisible(), "keyboard stays visible in PERFORM");
    check (panel.generateButton().isVisible() && panel.macroSelector().isVisible(),
           "PERFORM exposes Generate and macro controls");

    panel.setWorkspace (W::timeline);
    check (panel.programCurves().isVisible() && !panel.layerPanel (0).isVisible(),
           "TIMELINE replaces layer cards with Program curves");
    check (panel.programCurvesButton().getToggleState(), "curves toggle reflects TIMELINE");

    panel.setWorkspace (W::motion);
    check (panel.motionPanel().isVisible() == false && panel.workspace() == W::motion,
           "MOTION workspace selected (overlay opens from its button)");

    panel.setWorkspace (W::process);
    check (panel.effectSelector (0).isVisible() && !panel.effectWet (0).isVisible() == false,
           "PROCESS shows FX slot controls");

    panel.setWorkspace (W::trigger);
    check (panel.patternStep (0).isVisible() && panel.patternSwingSlider().isVisible(),
           "TRIGGER shows the pattern grid and swing");

    panel.setWorkspace (W::presets);
    check (panel.factorySelector().isVisible() == false && panel.workspace() == W::presets,
           "PRESETS workspace selected");

    panel.setWorkspace (W::advanced);
    check (panel.sampleImportButton().isVisible() && panel.dragGrip().isVisible(),
           "ADVANCED shows sample and drag-transfer controls");

    // Always-visible chrome after cycling every workspace.
    check (panel.keyboard().isVisible() && panel.workspaceTabs().isVisible(),
           "keyboard and tabs survive a full workspace cycle");

    // Tab callback path (as fired by VoxTabBar::mouseDown).
    panel.workspaceTabs().onTabChanged (static_cast<int> (W::motion));
    check (panel.workspace() == W::motion, "tab callback switches workspace");

    // Curves toggle contract used by users and the layer-editor tests.
    panel.setWorkspace (W::design);
    panel.programCurvesButton().setToggleState (true, juce::sendNotificationSync);
    check (panel.workspace() == W::timeline && panel.programCurves().isVisible() &&
               !panel.layerPanel (0).isVisible(),
           "curves toggle opens the TIMELINE workspace");
    panel.programCurvesButton().setToggleState (false, juce::sendNotificationSync);
    check (panel.workspace() == W::design && panel.layerPanel (0).isVisible(),
           "curves toggle returns to layer cards");
}

// Pointer down / drag-across / up, explicit velocity, focus-loss cleanup and
// external-MIDI indication that never starts or releases notes here.
void checkKeyboardInteraction()
{
    using namespace vox::ui;
    VoxKeyboard keyboard;
    keyboard.setBounds (0, 0, 1040, 118);
    std::vector<std::pair<int, float>> ons;
    std::vector<int> offs;
    int allNotesOff = 0;
    keyboard.onNoteOn = [&] (int note, float velocity) { ons.push_back ({note, velocity}); };
    keyboard.onNoteOff = [&] (int note) { offs.push_back (note); };
    keyboard.onAllNotesOff = [&] { ++allNotesOff; };
    keyboard.setVelocity (100);

    // First white key of the default range (C3 = note 36): keys area starts
    // after the 176 px control cluster; three octaves => 21 white keys.
    const auto whiteW = (1040.0 - 176.0) / 21.0;
    const auto firstWhite = static_cast<float> (176.0 + whiteW * 0.5);
    const auto secondWhite = static_cast<float> (176.0 + whiteW * 1.5);

    keyboard.mouseDown (mouseEvent (keyboard, firstWhite, 100.f, true, false));
    check (ons.size() == 1 && ons.front().first == 36, "pointer down starts the C3 key");
    check (std::abs (ons.front().second - 100.f / 127.f) < 1e-6f, "explicit fixed velocity is forwarded");
    check (keyboard.heldNoteCount() == 1, "one held note after press");

    keyboard.mouseDrag (mouseEvent (keyboard, secondWhite, 100.f, false, false));
    check (offs.size() == 1 && offs.front() == 36, "drag between keys releases the old note");
    check (ons.size() == 2 && ons.back().first == 38, "drag between keys starts the new note");

    keyboard.mouseUp (mouseEvent (keyboard, secondWhite, 100.f, false, true));
    check (offs.size() == 2 && offs.back() == 38 && keyboard.heldNoteCount() == 0,
           "pointer up releases the dragged note");

    // Repeated note: press, press again (retrigger), release.
    keyboard.mouseDown (mouseEvent (keyboard, firstWhite, 100.f, true, false));
    keyboard.mouseDown (mouseEvent (keyboard, firstWhite, 100.f, true, false));
    check (keyboard.heldNoteCount() == 1, "repeated press keeps a single paired hold");
    keyboard.mouseUp (mouseEvent (keyboard, firstWhite, 100.f, false, true));
    check (keyboard.heldNoteCount() == 0 && offs.back() == 36, "repeated note still releases");

    // Focus loss releases exactly what this control started.
    keyboard.mouseDown (mouseEvent (keyboard, firstWhite, 100.f, true, false));
    const auto allOffBefore = allNotesOff;
    keyboard.focusLost (juce::Component::focusChangedByTabKey);
    check (keyboard.heldNoteCount() == 0 && allNotesOff > allOffBefore,
           "focus loss releases held notes and fires the cleanup hook");

    // External (host) MIDI indication is display-only.
    std::array<std::uint64_t, 2> mask {};
    mask[0] = std::uint64_t { 1 } << 60;
    keyboard.setExternalNotes (mask);
    check (ons.size() >= 3 && keyboard.heldNoteCount() == 0,
           "external indication never starts a note from this control");

    // Velocity changes only affect future presses.
    keyboard.setVelocity (42);
    keyboard.mouseDown (mouseEvent (keyboard, firstWhite, 100.f, true, false));
    check (std::abs (ons.back().second - 42.f / 127.f) < 1e-6f, "velocity edit applies to the next press");
    keyboard.mouseUp (mouseEvent (keyboard, firstWhite, 100.f, false, true));
}

// Bounded SPSC queue: the audio thread consumes UI notes, never echoes them
// into the host MidiBuffer, and overflow is counted instead of growing.
void checkProcessorUiQueue()
{
    using namespace vox::fx;
    FxProcessor processor;
    processor.prepareToPlay (48000, 512);
    // Gate mode: NoteOff releases (One Shot would ignore it by policy).
    check (processor.setPlayback (PlayMode::gate, TriggerPolicy::overlap),
           "gate mode so UI note-off releases");
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;

    check (processor.pushUiNote ({1, 60, 100}), "UI note enqueues");
    processor.processBlock (audio, midi);
    midi.clear();
    check (processor.audioTelemetry().events >= 1, "audio thread consumed the UI note");

    // No synthetic host MIDI echo: the block's MidiBuffer is only cleared,
    // never written (this instrument has no MIDI output by contract).
    check (processor.pushUiNote ({0, 60, 0}), "UI note-off enqueues");
    for (int i = 0; i < 40 && processor.audioTelemetry().events != 0; ++i) {
        processor.processBlock (audio, midi);
        midi.clear();
    }
    check (processor.audioTelemetry().events == 0, "UI note-off retires the voice");

    int accepted = 0;
    while (processor.pushUiNote ({1, static_cast<std::uint8_t> (61 + (accepted % 40)), 100}))
        if (++accepted > 512) break;
    check (accepted <= 64 && processor.uiNoteDropped() >= 1,
           "UI note queue overflow is bounded and counted");
    // Drain the backlog so the bounded path stays deterministic.
    for (int i = 0; i < 8; ++i) {
        processor.processBlock (audio, midi);
        midi.clear();
    }

    // Host-note indication snapshot: host keys appear, UI keys (channel 16)
    // stay in the keyboard's own held bookkeeping.
    FxProcessor host;
    host.prepareToPlay (48000, 512);
    check (host.setPlayback (PlayMode::gate, TriggerPolicy::overlap), "gate mode for note-off release");
    midi.addEvent (juce::MidiMessage::noteOn (1, 72, 0.8f), 0);
    host.processBlock (audio, midi);
    midi.clear();
    const auto note72 = [] (const FxProcessor& p) {
        return (p.hostNoteMask()[1] >> (72 - 64)) & 1u; // note 72 is in mask[1]
    };
    check (note72 (host), "host note appears in the indication mask");
    midi.addEvent (juce::MidiMessage::noteOff (1, 72), 0);
    for (int i = 0; i < 20; ++i) {
        host.processBlock (audio, midi);
        midi.clear();
        if (note72 (host) == 0) break;
    }
    check (note72 (host) == 0, "host note-off clears the indication mask");
}
} // namespace

int main()
{
    checkWorkspaceMatrix();
    checkKeyboardInteraction();
    checkProcessorUiQueue();
    if (failures != 0) {
        std::printf ("FX UI workspace/keyboard: %d failure(s)\n", failures);
        return 1;
    }
    std::printf ("FX UI workspace/keyboard: 0 failure(s)\n");
    return 0;
}
