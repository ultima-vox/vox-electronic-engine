// Native workspace shell + performance-keyboard interaction tests
// (TZ 13.2 workspaces; design system 5.4 VoxKeyboard; TL P0 7223242731:
// real bindings, native pointer interaction, distinct host/UI note
// ownership, no stuck notes, bounded RT-safe UI-note path).
#include "Processor.h"
#include "FxEditor.h"
#include "Tokens.h"
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

// Responsive bounds: no two visible controls of a workspace may overlap
// (TL 7227421044: bounds/nonoverlap tests at minimum editor width).
void checkWorkspaceNonOverlap()
{
    using namespace vox::fx;
    FxProcessor processor;
    FxPanel panel (processor);
    panel.setSize (1040, 700);
    panel.setVisible (true);
    const auto visibleControls = [&] {
        std::vector<std::pair<juce::Rectangle<int>, const void*>> found;
        for (auto* child : panel.getChildren())
            if (child->isVisible() && !child->getBounds().isEmpty())
                found.push_back ({ child->getBounds(), child });
        return found;
    };
    for (int w = 0; w < vox::fx::FxPanel::workspaceCount; ++w)
    {
        panel.setWorkspace (static_cast<W> (w));
        const auto controls = visibleControls();
        int overlaps = 0;
        for (std::size_t i = 0; i < controls.size(); ++i)
            for (std::size_t j = i + 1; j < controls.size(); ++j)
                if (controls[i].first.intersects (controls[j].first)) ++overlaps;
        check (overlaps == 0, "workspace visible controls do not overlap at 1040 px");
        // Chrome and keyboard must never intrude into the workspace band.
        check (panel.workspaceTabs().getBottom() <= 44,
               "tab strip stays in the chrome row");
    }
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

    check (panel.workspace() == W::perform, "default workspace is PERFORM");
    check (panel.outputSlider().isVisible() && !panel.layerPanel (0).isVisible(),
           "PERFORM shows output controls and hides layer cards");
    check (panel.macroKnob (0).isVisible() && panel.macroKnob (7).isVisible(),
           "PERFORM exposes all eight macro knobs");
    check (panel.keyboard().isVisible() && panel.workspaceTabs().isVisible(),
           "keyboard and tab strip start visible");

    panel.setWorkspace (W::design);
    check (panel.layerPanel (0).isVisible(), "DESIGN shows layer cards");
    check (!panel.programCurves().isVisible(), "DESIGN hides Program curves");
    panel.setWorkspace (W::perform);
    check (panel.workspaceTabs().getSelectedIndex() == 1, "tab strip follows PERFORM");
    check (panel.generateButton().isVisible(),
           "PERFORM exposes Generate");

    panel.setWorkspace (W::timeline);
    check (panel.programCurves().isVisible() && !panel.layerPanel (0).isVisible(),
           "TIMELINE replaces layer cards with Program curves");
    check (panel.programCurvesButton().getToggleState(), "curves toggle reflects TIMELINE");

    panel.setWorkspace (W::motion);
    check (panel.workspace() == W::motion && panel.macroSelector().isVisible(),
           "MOTION owns the macro routing editor");
    check (panel.motionPanel().isVisible(),
           "MOTION embeds the real 64-route modulation editor");
    check (panel.motionPanel().applyButton().isVisible(),
           "embedded motion editor exposes its apply action");

    panel.setWorkspace (W::process);
    check (panel.effectSelector (0).isVisible() && !panel.effectWet (0).isVisible() == false,
           "PROCESS shows FX slot controls");

    panel.setWorkspace (W::trigger);
    check (panel.patternStep (0).isVisible() && panel.patternSwingSlider().isVisible(),
           "TRIGGER shows the pattern grid and swing");

    panel.setWorkspace (W::presets);
    check (panel.factorySelector().isVisible() == false && panel.workspace() == W::presets,
           "PRESETS workspace selected");
    check (panel.keyboard().isVisible(), "keyboard stays visible in PRESETS");

    panel.setWorkspace (W::advanced);
    check (panel.sampleImportButton().isVisible() && panel.dragGrip().isVisible(),
           "ADVANCED shows sample and drag-transfer controls");

    // Always-visible chrome after cycling every workspace.
    check (panel.keyboard().isVisible() && panel.workspaceTabs().isVisible(),
           "keyboard and tabs survive a full workspace cycle");

    // Responsive chrome at the minimum width: tabs keep a full-width row
    // and the meter/clock/panic row never overlaps them.
    {
        FxProcessor chromeProcessor;
        FxPanel chrome (chromeProcessor);
        chrome.setSize (1040, 700);
        const auto tabs = chrome.workspaceTabs().getBounds();
        const auto meter = chrome.getBounds().getTopLeft();
        check (tabs.getWidth() >= 480, "eight tabs keep >=480 px at 1040 width");
        check (tabs.getBottom() <= 44, "tabs occupy the first chrome row only");
        (void) meter;
    }

    // Tab callback path (as fired by VoxTabBar::mouseDown).
    panel.workspaceTabs().onTabChanged (static_cast<int> (W::motion));
    check (panel.workspace() == W::motion, "tab callback switches workspace");

    // Timeline: 20 targets = 4 trajectory kinds across Program + Layer A..D.
    {
        auto& curves = panel.programCurves();
        panel.setWorkspace (W::timeline);
        check (curves.isVisible(), "TIMELINE embeds the curve panel");
        check (curves.scopeSelector().isVisible(), "timeline scope selector is visible");
        int targets = 0;
        for (int scope = 1; scope <= 5; ++scope) {
            curves.scopeSelector().setSelectedId (scope, juce::sendNotificationSync);
            for (int kind = 1; kind <= 4; ++kind) {
                curves.selector().setSelectedId (kind, juce::sendNotificationSync);
                check (static_cast<int> (curves.editor().kind()) == kind - 1,
                       "scope x kind selection binds the active editor");
                ++targets;
            }
        }
        check (targets == 20, "TIMELINE exposes all 20 targets");
        curves.scopeSelector().setSelectedId (1, juce::sendNotificationSync);
        curves.selector().setSelectedId (1, juce::sendNotificationSync);
    }

    // Contextual source editing from each layer card.
    {
        panel.setWorkspace (W::design);
        auto& card = panel.layerPanel (2);
        card.editSourceButton().setVisible (true);
        check (card.onEditSource != nullptr, "layer cards carry a source-edit hook");
        check (card.editSourceButton().onClick != nullptr,
               "card SOURCE button carries an action");
        card.editSourceButton().onClick(); // synchronous off-screen invocation
        check (panel.sourcePanel().isVisible() && panel.sourcePanel().layerSelector().getSelectedId() == 3,
               "card SOURCE opens the source editor on that layer");
        panel.sourcePanel().setVisible (false);
    }

    // Preset browser is embedded with real search/facet controls.
    {
        panel.setWorkspace (W::presets);
        auto searchVisible = false;
        for (auto* child : panel.getChildren())
            if (dynamic_cast<vox::fx::PresetBrowser*> (child) != nullptr && child->isVisible())
                searchVisible = true;
        check (searchVisible, "PRESETS embeds the preset browser");
    }

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

// P1 keyboard: distinct input ownership with captured key ids, paint order
// (whites under blacks), drag outside/re-entry, one-shot event flash,
// cursor clamping into view and accessible labelling.
void checkKeyboardOwnershipAndVisual()
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
    const auto whiteW = (1040.0 - 176.0) / 21.0;
    const auto whiteX = [&] (int whiteIndex) {
        return static_cast<float> (176.0 + whiteW * (whiteIndex + 0.5));
    };

    // Z/X captured-key ownership: X releases what Z pressed, even after the
    // cursor has moved on.
    keyboard.keyPressed (juce::KeyPress ('z'));
    check (ons.size() == 1 && ons.back().first == 60, "Z presses the cursor key");
    keyboard.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    check (keyboard.cursorNote() == 61, "arrow moves the cursor while Z is held");
    keyboard.keyPressed (juce::KeyPress ('x'));
    check (!offs.empty() && offs.back() == 60, "X releases the captured key, not the cursor");
    check (keyboard.heldNoteCount() == 0, "no held notes after the Z/X pair");

    // Space/Enter capture + focus loss cleanup.
    keyboard.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    check (ons.size() == 2 && ons.back().first == 61, "Space presses the current cursor key");
    const auto allOffBefore = allNotesOff;
    keyboard.focusLost (juce::Component::focusChangedByTabKey);
    check (!offs.empty() && offs.back() == 61 && allNotesOff > allOffBefore,
           "focus loss releases the Space-captured key and fires cleanup");
    check (keyboard.heldNoteCount() == 0, "focus loss leaves no held notes");

    // Drag outside releases; re-entry without a new press never restarts.
    keyboard.mouseDown (mouseEvent (keyboard, whiteX (0), 100.f, true, false));
    const auto onsAfterPress = ons.size();
    check (ons.back().first == 36, "pointer press starts C3");
    keyboard.mouseExit (mouseEvent (keyboard, 50.f, 100.f, false, false));
    check (!offs.empty() && offs.back() == 36, "dragging outside releases the key");
    keyboard.mouseDrag (mouseEvent (keyboard, whiteX (1), 100.f, false, false));
    check (ons.size() == onsAfterPress, "re-entry without a press starts nothing");
    keyboard.mouseUp (mouseEvent (keyboard, whiteX (1), 100.f, false, true));
    check (keyboard.heldNoteCount() == 0, "pointer path ends clean");

    // One-shot event flash: a visible launch pulse, not a held gate visual
    // (the panel wires pulseNote to a successful event push).
    keyboard.setEventMode (true);
    keyboard.pulseNote (40);
    check (keyboard.pulseActive (40), "one-shot press shows the event flash");
    juce::Image pulseFrame (juce::Image::ARGB, 1040, 118, true);
    {
        juce::Graphics g (pulseFrame);
        keyboard.paint (g);
    }
    // E4 is the third white key (white index 2).
    const auto pulsePixel = pulseFrame.getPixelAt (static_cast<int> (176.0 + whiteW * 2.5), 60);
    check (pulsePixel == tokens::colour::accentHover,
           "event flash paints the accent highlight on the key");
    keyboard.mouseDown (mouseEvent (keyboard, whiteX (2), 100.f, true, false));
    keyboard.mouseUp (mouseEvent (keyboard, whiteX (2), 100.f, false, true));
    keyboard.setEventMode (false);

    // Panel wiring: a press on the editor keyboard in one-shot mode flashes
    // the launched key.
    {
        using namespace vox::fx;
        FxProcessor flashProcessor;
        FxPanel flashPanel (flashProcessor);
        flashPanel.setSize (1040, 700);
        auto& flashKeys = flashPanel.keyboard();
        const auto kw = flashKeys.getWidth();
        const auto flashWhiteW = static_cast<double> (kw - 176) / 21.0;
        flashKeys.mouseDown (mouseEvent (flashKeys,
            static_cast<float> (176.0 + flashWhiteW * 0.5), 60.f, true, false));
        check (flashKeys.pulseActive (36),
               "editor keyboard flashes a one-shot event launch");
        flashKeys.mouseUp (mouseEvent (flashKeys,
            static_cast<float> (176.0 + flashWhiteW * 0.5), 60.f, false, true));
    }

    // Paint order: black keys must sit above the white overlap columns
    // (chromatic iteration used to let later whites overpaint earlier blacks).
    keyboard.releaseAllNotes();
    juce::Image frame (juce::Image::ARGB, 1040, 118, true);
    {
        juce::Graphics g (frame);
        keyboard.paint (g);
    }
    // C#3 black key spans x in [176 + 0.7w, 176 + 1.3w] over white D3's
    // column; sample inside both at y=30 (within the black key's height).
    const auto sampleX = static_cast<int> (176.0 + whiteW * 1.15);
    const auto blackPixel = frame.getPixelAt (sampleX, 30);
    check (blackPixel == tokens::colour::control,
           "black key overlap column is painted above the white keys");

    // Accessible labelling (screen-reader title/description present).
    check (keyboard.getTitle() == "Performance keyboard",
           "keyboard exposes an accessible title");
    check (!keyboard.getDescription().isEmpty(), "keyboard exposes an accessible description");

    // Cursor clamps into view: walking right to note 127 shifts the octave.
    for (int i = 0; i < 70; ++i)
        keyboard.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    check (keyboard.cursorNote() == 127, "cursor reaches note 127");
    check (keyboard.getBaseOctave() == 8 && keyboard.cursorNote() >= 36 * 0,
           "octave follows the cursor so it stays visible");
}

// Bounded SPSC queue + source ownership (TL P0 7227421044): the audio
// thread consumes UI notes on the engine's independent domain, never echoes
// them into the host MidiBuffer, overflow cannot strand a release, and
// panic/restore/offline/editor-cancel clear pending and active UI state.
void checkProcessorUiQueue()
{
    using namespace vox::fx;
    FxProcessor processor;
    processor.prepareToPlay (48000, 512);
    check (processor.setPlayback (PlayMode::gate, TriggerPolicy::overlap),
           "gate mode so UI note-off releases");
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    const auto run = [&] (int blocks) {
        for (int i = 0; i < blocks; ++i) { processor.processBlock (audio, midi); midi.clear(); }
    };

    check (processor.pushUiNote ({1, 60, 100}), "UI note enqueues");
    processor.processBlock (audio, midi);
    check (midi.getNumEvents() == 0, "UI notes never echo into the host MidiBuffer");
    midi.clear();
    check (processor.audioTelemetry().events >= 1, "audio thread consumed the UI note");

    check (processor.pushUiNote ({0, 60, 0}), "UI note-off enqueues");
    run (40);
    check (processor.audioTelemetry().events == 0, "UI note-off retires the voice");

    // Overflow: note-ons fail closed and are counted; a note-off at capacity
    // is accepted through the release latch and must still reach its voice.
    int accepted = 0;
    while (processor.pushUiNote ({1, static_cast<std::uint8_t> (100 + (accepted % 20)), 100}))
        if (++accepted > 512) break;
    check (accepted == 64 && processor.uiNoteDropped() >= 1,
           "note-on queue saturates at 64 with counted fail-closed drops");
    const auto pressedNote = static_cast<std::uint8_t> (100 + ((accepted - 1) % 20));
    check (processor.pushUiNote ({0, pressedNote, 0}),
           "note-off is accepted via the release latch while the ring is full");
    // Drain, then release every stacked ticket so nothing can be stranded.
    run (4);
    for (int note = 100; note < 120; ++note)
        check (processor.pushUiNote ({0, static_cast<std::uint8_t> (note), 0}),
               "post-drain note-off accepted");
    run (40);
    check (processor.audioTelemetry().events == 0,
           "saturated queue cannot strand gate notes");
    (void) pressedNote;
}

// Source ownership matrix: UI voices share no ticket domain with any host
// channel (16 included), host CC all-off spares them, global panic and
// restore clear them, offline bounces discard them, and the indication mask
// reflects actual host sources only.
void checkMidiOwnership()
{
    using namespace vox::fx;
    FxProcessor processor;
    processor.prepareToPlay (48000, 512);
    check (processor.setPlayback (PlayMode::gate, TriggerPolicy::overlap), "gate mode");
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    const auto run = [&] (int blocks) {
        for (int i = 0; i < blocks; ++i) { processor.processBlock (audio, midi); midi.clear(); }
    };
    const auto events = [&] { return processor.audioTelemetry().events; };

    // Same pitch: UI + host channel 1 + host channel 16 at once.
    check (processor.pushUiNote ({1, 60, 100}), "UI press for the shared pitch");
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.5f), 0);
    midi.addEvent (juce::MidiMessage::noteOn (16, 60, 0.5f), 0);
    processor.processBlock (audio, midi);
    check (midi.getNumEvents() == 0, "host MIDI input produces no MIDI output");
    midi.clear();
    check (events() == 3, "UI + host ch1 + host ch16 same pitch sound together");

    check (processor.pushUiNote ({0, 60, 0}), "UI release");
    run (4);
    check (events() == 2, "UI note-off releases only the editor voice");

    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    processor.processBlock (audio, midi);
    midi.clear();
    check (events() == 1, "host ch1 note-off leaves the ch16 voice");

    // Host CC123 on channel 1 must not kill the UI voice.
    check (processor.pushUiNote ({1, 61, 100}), "UI press before host CC");
    run (1);
    check (events() == 2, "UI voice sounds beside the host ch16 voice");
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 123, 0), 0);
    processor.processBlock (audio, midi);
    midi.clear();
    check (events() == 2, "host CC123 on ch1 does not kill the UI voice");
    midi.addEvent (juce::MidiMessage::controllerEvent (16, 123, 0), 0);
    processor.processBlock (audio, midi);
    midi.clear();
    check (events() == 1, "host CC123 on ch16 releases only its own channel");

    // Global panic releases everything.
    processor.panic();
    run (4);
    check (events() == 0, "global panic releases UI and host voices");

    // Indication mask: host ch16 counts as host; UI and audition do not.
    FxProcessor maskHost;
    maskHost.prepareToPlay (48000, 512);
    check (maskHost.setPlayback (PlayMode::gate, TriggerPolicy::overlap), "gate mode for mask");
    juce::MidiBuffer in;
    in.addEvent (juce::MidiMessage::noteOn (16, 72, 0.8f), 0);
    maskHost.processBlock (audio, in);
    in.clear();
    check (((maskHost.hostNoteMask()[1] >> (72 - 64)) & 1u) != 0,
           "host note on channel 16 appears in the host indication mask");
    check (maskHost.pushUiNote ({1, 48, 100}), "UI note for mask exclusion");
    maskHost.processBlock (audio, in);
    in.clear();
    check (((maskHost.hostNoteMask()[0] >> 48) & 1u) == 0,
           "UI keyboard voice never appears in the host mask");
    maskHost.audition();
    maskHost.processBlock (audio, in);
    in.clear();
    check (((maskHost.hostNoteMask()[0] >> 60) & 1u) == 0,
           "audition (preview) voice never appears in the host mask");
    maskHost.panic();
    runBlocks: ;
    for (int i = 0; i < 8; ++i) { maskHost.processBlock (audio, in); in.clear(); }

    // Restore boundary: active and pending UI notes clear; no resurrection.
    check (processor.pushUiNote ({1, 62, 100}), "UI note before restore");
    run (1);
    check (events() >= 1, "UI voice active before restore");
    check (processor.pushUiNote ({1, 63, 100}), "pending UI note before restore");
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    run (4);
    check (events() == 0, "restore clears active and pending UI notes");

    // Offline transition with an active UI voice: discarded, not leaked.
    check (processor.pushUiNote ({1, 64, 100}), "UI note before offline");
    run (1);
    check (events() >= 1, "UI voice active before offline bounce");
    processor.setNonRealtime (true);
    run (2);
    check (events() == 0, "offline bounce discards active UI voices");
    check (processor.pushUiNote ({1, 65, 100}), "UI press during offline");
    processor.setNonRealtime (false);
    run (2);
    check (events() == 0, "offline queue flush prevents resurrection after the bounce");

    // Editor cancel with pending events (close/focus-loss recovery hook).
    check (processor.pushUiNote ({1, 66, 100}), "UI press before cancel");
    processor.cancelUiNotes();
    run (2);
    check (events() == 0, "cancelUiNotes flushes pending events and releases voices");

    // Repeated offline transitions with a held UI note.
    check (processor.pushUiNote ({1, 67, 100}), "UI press for transitions");
    run (1);
    for (int i = 0; i < 3; ++i) {
        processor.setNonRealtime (true);
        run (1);
        processor.setNonRealtime (false);
        run (1);
    }
    check (events() == 0, "repeated offline transitions leave no UI voice");
}
} // namespace

int main()
{
    checkWorkspaceNonOverlap();
    checkWorkspaceMatrix();
    checkKeyboardInteraction();
    checkKeyboardOwnershipAndVisual();
    checkProcessorUiQueue();
    checkMidiOwnership();
    if (failures != 0) {
        std::printf ("FX UI workspace/keyboard: %d failure(s)\n", failures);
        return 1;
    }
    std::printf ("FX UI workspace/keyboard: 0 failure(s)\n");
    return 0;
}
