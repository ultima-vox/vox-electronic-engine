#include "DrumsWorkspace.h"
#include "DrumsDockZone.h"
#include "PatternEditor.h"
#include "DrumsEditor.h"
#include "PadControlSurface.h"
#include "DrumsRolesPanel.h"
#include "StyleRegistry.h"
#include "StyleLibraryPanel.h"
#include "Processor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <mutex>
#include <thread>
#include <set>

namespace {
int failures = 0;
void check (bool ok, const juce::String& message)
{
    if (! ok) { std::printf ("FAIL workspace: %s\n", message.toRawUTF8()); ++failures; }
}
float plain (vox::drums::DrumsProcessor& p, const juce::String& id) { return p.state().getRawParameterValue (id)->load(); }
void block (vox::drums::DrumsProcessor& p)
{
    juce::AudioBuffer<float> audio (p.getTotalNumOutputChannels(), 128);
    audio.clear(); juce::MidiBuffer midi;
    p.processBlock (audio, midi);
}
void press (juce::Button& b) { if (b.onClick) b.onClick(); }
void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil (20); }

void checkToolLayout (juce::Component& page)
{
    std::vector<juce::Component*> inputs;
    const auto collect = [&] (auto&& self, juce::Component& parent) -> void {
        for (auto* child : parent.getChildren())
        {
            if (! child->isVisible()) continue;
            if (dynamic_cast<juce::Slider*> (child) || dynamic_cast<juce::Button*> (child)
                || dynamic_cast<juce::ComboBox*> (child) || dynamic_cast<juce::TextEditor*> (child)
                || dynamic_cast<juce::ListBox*> (child)) inputs.push_back (child);
            else self (self, *child);
        }
    };
    collect (collect, page);
    check (! inputs.empty(), "dock tool contains actual input controls");
    for (std::size_t n = 0; n < inputs.size(); ++n)
    {
        const auto bounds = page.getLocalArea (inputs[n], inputs[n]->getLocalBounds());
        check (! bounds.isEmpty() && page.getLocalBounds().contains (bounds), "narrow tool inputs remain inside scrollable canvas");
        for (std::size_t k = n + 1; k < inputs.size(); ++k)
            check (! bounds.intersects (page.getLocalArea (inputs[k], inputs[k]->getLocalBounds())), "narrow tool inputs do not overlap");
    }
}

void testSurfaces()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    auto donor = std::make_unique<DrumsPanel> (*p);
    pump();
    PadControlSurface sound (*p, PadControlSurface::Group::sound);
    pump();
    PadControlSurface sample (*p, PadControlSurface::Group::sample);
    pump();
    sound.setSize (720, 470); sample.setSize (720, 560);
    check (sound.hostSlider ("level")->getWidth() == 52, "primary Sound dials use 52 logical pixels when space permits");
    for (const auto* name : { "fine", "character", "resonance", "attack", "sustain", "pitch_amount", "pitch_decay", "drive", "sub", "tail",
                              "split", "start", "end", "sample_level" })
    {
        auto* next = sound.structuralSlider (name);
        if (next == nullptr) next = sample.structuralSlider (name);
        auto* old = donor->structuralSlider (name);
        check (next != nullptr && old != nullptr, juce::String ("port retains structural control ") + name);
        if (next && old)
        {
            check (next->getMinimum() == old->getMinimum() && next->getMaximum() == old->getMaximum()
                   && next->getInterval() == old->getInterval(), juce::String ("port retains exact range/step ") + name);
            check (next->getTextFromValue (next->getMaximum()) == old->getTextFromValue (old->getMaximum()),
                   juce::String ("port retains units ") + name);
        }
    }
    for (const auto* name : { "model", "note", "choke", "poly", "filter", "source", "sample_mode" })
    {
        auto* next = sound.structuralCombo (name);
        if (! next) next = sample.structuralCombo (name);
        auto* old = donor->structuralCombo (name);
        check (next && old && next->getNumItems() == old->getNumItems(), juce::String ("port retains choices ") + name);
    }
    for (const auto* name : { "level", "tune", "decay", "pan", "cutoff" })
        check (sound.hostSlider (name) != nullptr, juce::String ("Sound retains host control ") + name);
    for (const auto* name : { "loop", "reverse", "normalize" })
        check (sample.structuralToggle (name) != nullptr, juce::String ("Sample retains toggle ") + name);
    donor.reset();
    pump();
    sound.setContext (4, 0); sample.setContext (4, 1);
    sound.structuralCombo ("model")->setSelectedItemIndex (static_cast<int> (kit::Model::kick), juce::sendNotificationSync);
    sound.structuralSlider ("drive")->setValue (5.4, juce::sendNotificationSync);
    sound.hostSlider ("cutoff")->setValue (4500, juce::sendNotificationSync);
    sample.structuralSlider ("sample_level")->setValue (0.35, juce::sendNotificationSync);
    auto* reverse = sample.structuralToggle ("reverse"); reverse->setToggleState (true, juce::dontSendNotification); press (*reverse);
    const auto settings = p->kitStore().pad (4);
    check (std::abs (settings.drive - 5.4f) < 0.001f && std::abs (plain (*p, "drm.p05.cutoff") - 4500) < 1,
           "Sound writes selected structural and host settings");
    check (std::abs (settings.players[1].level - 0.35f) < 0.001f && settings.players[1].reverse
           && ! settings.players[0].reverse, "Sample controls address player B independently");
    auto external = settings; external.fine = 17; external.players[1].level = 0.7f;
    p->kitStore().setPad (4, external); sound.update(); sample.update();
    check (sound.structuralSlider ("fine")->getValue() == 17 && std::abs (sample.structuralSlider ("sample_level")->getValue() - 0.7) < 0.001,
           "native Sound/Sample reflect external KitStore edits");
    external.model = kit::Model::snare;
    p->kitStore().setPad (4, external); sound.update();
    check (! sound.structuralSlider ("drive")->getParentComponent()->isVisible(), "external model change hides kick-only controls");

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("vox-native-sample", "", false);
    check (folder.createDirectory().wasOk(), "create native Sample fixture folder");
    const auto file = folder.getChildFile ("trim.wav");
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions()
                              .withSampleRate (48000).withNumChannels (1).withBitsPerSample (16));
        check (writer != nullptr, "create native Sample WAV fixture");
        if (writer)
        {
            juce::AudioBuffer<float> audio (1, 1024);
            for (int n = 0; n < 1024; ++n) audio.setSample (0, n, std::sin (static_cast<float> (n) * 0.05f) * 0.4f);
            check (writer->writeFromAudioSampleBuffer (audio, 0, 1024), "write native Sample WAV fixture");
        }
    }
    std::mutex mutex;
    std::vector<std::function<void()>> held;
    p->samples().setCompletionDispatcherForTesting ([&] (std::function<void()> f) { std::lock_guard lock (mutex); held.push_back (std::move (f)); });
    const auto complete = [&] {
        p->samples().waitForPendingDecodes();
        std::vector<std::function<void()>> pending;
        { std::lock_guard lock (mutex); pending.swap (held); }
        for (auto& f : pending) f();
    };
    sample.loadSample (file); complete(); sample.update();
    check (p->samples().pad (slotOf (4, 1)).buffer != nullptr && sample.sampleStatus().contains ("trim.wav"),
           "native Sample Load uses actual asynchronous library decode");
    auto& wave = sample.waveform();
    const auto event = [&] (float x, bool down) {
        const juce::Point<float> at { x, 30 };
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
            down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &wave, &wave, juce::Time::getCurrentTime(), at,
            juce::Time::getCurrentTime(), 1, false);
    };
    wave.mouseDown (event (0, true));
    sample.setContext (6, 0); // must defer while the old sample's handle is held
    wave.mouseDrag (event (static_cast<float> (wave.getWidth()) * 0.4f, true));
    wave.mouseUp (event (static_cast<float> (wave.getWidth()) * 0.4f, false)); sample.update();
    check (std::abs (p->kitStore().pad (4).players[1].start - 0.4f) < 0.002f && p->kitStore().pad (6).players[0].start == 0,
           "waveform trim edits the captured sample across context changes");
    sample.setContext (4, 1);
    juce::MemoryBlock trimProject; p->getStateInformation (trimProject);
    const auto trimX = static_cast<float> (wave.getWidth()) * 0.4f;
    wave.mouseDown (event (trimX, true));
    p->setStateInformation (trimProject.getData(), static_cast<int> (trimProject.getSize()));
    const auto restoredStart = p->kitStore().pad (4).players[1].start;
    const auto restoredPadState = KitStore::encode (p->kitStore().snapshot()).data;
    wave.mouseDrag (event (static_cast<float> (wave.getWidth()) * 0.7f, true)); wave.mouseUp (event (trimX, false));
    check (juce::exactlyEqual (p->kitStore().pad (4).players[1].start, restoredStart),
           "project restore cancels an old waveform trim before it can write into the restored project");
    sample.update();
    p->kitStore().beforeCommitForTesting = [&] {
        p->kitStore().beforeCommitForTesting = {};
        p->setStateInformation (trimProject.getData(), static_cast<int> (trimProject.getSize()));
    };
    wave.mouseDown (event (0, true)); wave.mouseUp (event (0, false));
    check (KitStore::encode (p->kitStore().snapshot()).data == restoredPadState,
           "restore immediately before the waveform commit wins without a stale trim write");
    auto* fine = sound.structuralSlider ("fine");
    if (fine->onDragStart) fine->onDragStart();
    p->setStateInformation (trimProject.getData(), static_cast<int> (trimProject.getSize()));
    fine->setValue (50, juce::sendNotificationSync);
    if (fine->onDragEnd) fine->onDragEnd();
    check (KitStore::encode (p->kitStore().snapshot()).data == restoredPadState,
           "Sound structural drag cannot write an old gesture into the restored project");
    sound.update();
    std::vector<std::byte> externalPadState;
    p->kitStore().beforeCommitForTesting = [&] {
        p->kitStore().beforeCommitForTesting = {};
        auto settings = p->kitStore().pad (6); settings.character = 0.33f; p->kitStore().setPad (6, settings);
        externalPadState = KitStore::encode (p->kitStore().snapshot()).data;
    };
    fine->setValue (51, juce::sendNotificationSync);
    check (KitStore::encode (p->kitStore().snapshot()).data == externalPadState,
           "conditional Sound commit preserves an external pad edit inserted at the store boundary");
    sample.loadSample (folder.getChildFile ("missing.wav")); p->samples().waitForPendingDecodes();
    p->setStateInformation (trimProject.getData(), static_cast<int> (trimProject.getSize())); sample.update();
    const auto restoredMessage = sample.sampleStatus(); complete(); sample.update();
    check (sample.sampleStatus() == restoredMessage && p->samples().pad (slotOf (4, 1)).buffer != nullptr,
           "an old decode completion cannot replace restored sample status or assignment");
    const auto reference = p->samples().pad (slotOf (4, 1)).reference;
    const auto assignedBuffer = p->samples().pad (slotOf (4, 1)).buffer;
    sample.loadSample (folder.getChildFile ("missing.wav")); complete(); sample.update();
    const auto retained = p->samples().pad (slotOf (4, 1)).reference;
    check (retained && reference && retained->sha256 == reference->sha256 && retained->path == reference->path
           && retained->frames == reference->frames && p->samples().pad (slotOf (4, 1)).buffer == assignedBuffer
           && sample.sampleStatus().isNotEmpty(),
           "failed Sample Load preserves existing assignment and shows diagnosis");
    press (*sample.sampleButton ("clear"));
    check (! p->samples().pad (slotOf (4, 1)).reference.has_value(), "native Clear clears only the selected sample slot");
    auto closing = std::make_unique<PadControlSurface> (*p, PadControlSurface::Group::sample);
    pump(); closing->setContext (2, 1); closing->loadSample (file);
    p->samples().waitForPendingDecodes(); closing.reset(); complete();
    check (p->samples().pad (slotOf (2, 1)).buffer != nullptr, "closing Sample view before completion safely retains the requested library load");
    p->samples().setCompletionDispatcherForTesting ({});
    check (folder.deleteRecursively(), "remove owned native sample fixtures");
}

void testRolesUI()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    auto panel = makeGeneratorDock (*p); panel->setSize (220, 2200); pump();
    const auto button = [&] (const char* name) { return dynamic_cast<juce::Button*> (panel->named (name)); };
    const auto choice = [&] (int pad) { return dynamic_cast<juce::ComboBox*> (panel->named ("role." + juce::String (pad + 1))); };
    press (*button ("roles.view")); checkToolLayout (*panel);
    check (choice (0) && choice (15) && choice (0)->getNumItems() == padRoleCount, "all 16 role selectors include the 13 existing semantic roles");
    const auto before = p->padRoles(); const auto note = p->kitStore().pad (0).note;
    choice (0)->setSelectedId (static_cast<int> (PadRole::ride) + 1, juce::sendNotificationSync);
    check (p->padRoles().role[0] == PadRole::ride && p->kitStore().pad (0).note == note, "role editing changes generator metadata, not MIDI note");
    press (*button ("roles.undo")); check (p->padRoles() == before, "manual role edit is undoable");
    choice (1)->setSelectedId (static_cast<int> (PadRole::kick) + 1, juce::sendNotificationSync);
    panel->update();
    auto* useSecond = button ("roles.use.2");
    check (useSecond != nullptr, "duplicate role notice offers explicit Use pad 2 action");
    if (useSecond) press (*useSecond);
    check (p->padRoles().role[0] == PadRole::none && p->padRoles().role[1] == PadRole::kick, "Use pad action transfers the generator role without swapping identical values");
    press (*button ("roles.undo"));
    const auto duplicate = p->padRoles();
    choice (1)->setSelectedId (1, juce::sendNotificationSync);
    choice (2)->setSelectedId (static_cast<int> (PadRole::kick) + 1, juce::sendNotificationSync); panel->update();
    auto* staleThird = button ("roles.use.3"); check (staleThird != nullptr, "second matching role offers a transfer");
    p->setPadRole (1, PadRole::kick); // a new earlier duplicate, before UI refresh
    const auto triple = p->padRoles();
    if (staleThird) press (*staleThird);
    check (p->padRoles() == triple && RoleMap::from (p->padRoles()).laneOfSlot[0] == 0,
           "stale transfer cannot claim the chosen pad plays while a new earlier duplicate would shadow it");
    panel->update();
    auto* currentSecond = button ("roles.use.2"); check (currentSecond != nullptr, "refreshed notice names the current second duplicate");
    if (currentSecond) press (*currentSecond);
    check (RoleMap::from (p->padRoles()).laneOfSlot[0] == 1, "fresh duplicate action makes its named pad play");
    press (*button ("roles.undo")); check (p->padRoles() == triple, "one Undo reverses the duplicate action");
    p->setPadRoles (duplicate.role); panel->update();

    press (*button ("roles.preview"));
    for (int pad = 0; pad < 16; ++pad) choice (pad)->setSelectedId (1, juce::sendNotificationSync);
    check (p->padRoles() == duplicate && button ("roles.apply")->isEnabled(), "resolved role preview is explicit and does not apply itself");
    press (*button ("roles.cancel")); check (p->padRoles() == duplicate, "Cancel leaves actual roles unchanged");
    press (*button ("roles.preview"));
    for (int pad = 0; pad < 16; ++pad) choice (pad)->setSelectedId (1, juce::sendNotificationSync);
    press (*button ("roles.apply"));
    check (p->padRoles().role[0] == PadRole::none && p->padRoles().role[1] == PadRole::none, "Apply commits the resolved proposal");
    press (*button ("roles.undo")); check (p->padRoles() == duplicate, "one Undo reverses the entire proposal");

    press (*button ("roles.preview"));
    for (int pad = 0; pad < 16; ++pad) choice (pad)->setSelectedId (1, juce::sendNotificationSync);
    auto pad = p->kitStore().pad (0); pad.note = 60; p->kitStore().setPad (0, pad);
    press (*button ("roles.apply"));
    check (p->padRoles() == duplicate && ! button ("roles.apply")->isEnabled(), "stale model proposal cannot overwrite roles after kit edits");
    auto sampledHat = p->kitStore().pad (3); sampledHat.source = kit::Source::sample; p->kitStore().setPad (3, sampledHat);
    press (*button ("roles.preview"));
    check (! button ("roles.apply")->isEnabled() && choice (3)->getSelectedId() == 0,
           "sample-derived hat needs an explicit role choice; Apply stays disabled");
    press (*button ("roles.apply")); check (p->padRoles() == duplicate, "unresolved Apply is refused in the action handler too");
    auto definition = std::make_shared<style::Definition> (*style::factoryDefinition (0));
    definition->id = "12345678-1234-4abc-9abc-123456789abc";
    definition->key = "native-custom-test"; definition->name = "Embedded native test";
    auto generator = p->generator(); generator.custom = definition; p->setGenerator (generator); panel->update();
    auto* styleBox = dynamic_cast<juce::ComboBox*> (panel->named ("style"));
    check (styleBox && styleBox->getText().contains (definition->name), "Generator reflects an embedded custom definition even when factory enum and amounts did not change");
    styleBox->setSelectedItemIndex (2, juce::sendNotificationSync);
    check (! p->generator().custom && styleUuidOf (p->generator()) == style::infoById (2)->uuid,
           "explicit factory choice replaces the custom definition the engine actually runs");
}

void test()
{
    auto p = std::make_unique<vox::drums::DrumsProcessor>();
    p->prepareToPlay (48000, 128);
    auto ui = std::make_unique<vox::drums::DrumsWorkspace> (*p);
    pump();
    ui->setSize (1180, 660);
    check (ui->inspectorSlider ("level")->getWidth() == 52, "primary Inspector dials retain 52 px geometry");
    check (ui->hostCombo ("drm.seq.mode") && ui->hostCombo ("drm.seq.pattern") && ui->hostCombo ("drm.fx.delay.time"),
           "persistent sequencer selectors and single-Mixer delay time retain host attachments");
    for (const auto* id : { "drm.fx.delay.feedback", "drm.fx.delay.tone", "drm.fx.delay.level", "drm.fx.reverb.size",
                            "drm.fx.reverb.damp", "drm.fx.reverb.level", "drm.seq.swing", "drm.seq.accent" })
        check (ui->returnSlider (id) != nullptr, juce::String ("single Mixer retains return/macro binding ") + id);
    check (ui->returnSlider ("unknown") == nullptr, "unknown return parameter lookup is refused");
    ui->hostCombo ("drm.seq.pattern")->setSelectedItemIndex (3, juce::sendNotificationSync);
    check (p->currentSlot() == 3, "persistent Pattern selector edits the actual sequencer slot parameter");
    ui->hostCombo ("drm.seq.pattern")->setSelectedItemIndex (0, juce::sendNotificationSync);
    ui->returnSlider ("drm.fx.delay.feedback")->setValue (0.44, juce::sendNotificationSync);
    check (std::abs (plain (*p, "drm.fx.delay.feedback") - 0.44f) < 0.001f, "single Mixer FX view writes actual return automation");
    press (ui->padSelectButton (3));
    block (*p); ui->pollForTesting();
    check (ui->selection().pad() == 3 && ui->telemetry().hits == 0 && p->activeVoices() == 0,
           "selection updates context without auditioning");
    ui->pinInspector (true);
    press (ui->padSelectButton (7));
    check (ui->selection().pad() == 7 && ui->selection().inspectorPad() == 3, "pinned Inspector retains pad 4");
    const auto other = plain (*p, "drm.p08.pan");
    ui->inspectorSlider ("pan")->setValue (0.63, juce::sendNotificationSync);
    check (std::abs (plain (*p, "drm.p04.pan") - 0.63f) < 0.001f && plain (*p, "drm.p08.pan") == other,
           "pinned edit writes captured parameter only");
    const auto otherNote = p->kitStore().pad (7).note;
    ui->inspectorCombo ("note")->setSelectedId (73, juce::sendNotificationSync);
    ui->inspectorCombo ("output")->setSelectedId (9, juce::sendNotificationSync);
    check (p->kitStore().pad (3).note == 72 && p->kitStore().pad (3).output == 8 && p->kitStore().pad (7).note == otherNote,
           "pinned structural controls edit the captured KitStore object");
    auto pad4 = p->kitStore().pad (3); pad4.note = 43; p->kitStore().setPad (3, pad4); ui->pollForTesting();
    check (ui->inspectorCombo ("note")->getSelectedId() == 44, "KitStore changes read back into pinned Inspector");
    ui->pinInspector (false);
    check (ui->selection().inspectorPad() == 7, "unpin immediately follows selection");
    auto* parameter = p->state().getParameter ("drm.p08.pan");
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (-0.47f));
    check (std::abs (ui->inspectorSlider ("pan")->getValue() + 0.47) < 0.001, "host automation reads back into current Inspector");

    struct Listener final : juce::AudioProcessorParameter::Listener {
        int begins = 0, ends = 0, changes = 0;
        void parameterValueChanged (int, float) override { ++changes; }
        void parameterGestureChanged (int, bool begin) override { (begin ? begins : ends)++; }
    } listener;
    parameter->addListener (&listener);
    auto* slider = ui->inspectorSlider ("pan");
    const auto origin = slider->getLocalBounds().getCentre().toFloat();
    const auto event = [&] (juce::Point<float> at, bool down) {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
            down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, slider, slider, juce::Time::getCurrentTime(), origin,
            juce::Time::getCurrentTime(), 1, at != origin);
    };
    slider->mouseDown (event (origin, true));
    ui->selectPad (3);
    const auto pinnedBefore = plain (*p, "drm.p04.pan");
    slider->mouseDrag (event (origin.translated (0, -45), true));
    slider->mouseUp (event (origin.translated (0, -45), false));
    parameter->removeListener (&listener);
    check (listener.begins == 1 && listener.ends == 1 && listener.changes > 0, "one real knob drag is one host gesture");
    check (plain (*p, "drm.p04.pan") == pinnedBefore, "selection during drag never redirects the active gesture");
    ui->pollForTesting();
    check (std::abs (ui->inspectorSlider ("pan")->getValue() - pinnedBefore) < 0.001,
           "deferred Inspector binding follows selection after mouse-up");
    ui->selectPad (7);

    // A separate HIT button does not steal selection; one coordinator drains
    // the real audio telemetry and distributes the same snapshot to all views.
    press (ui->padHitButton (0)); block (*p); ui->pollForTesting();
    check (ui->selection().pad() == 7, "audition preserves selection");
    check ((ui->telemetry().hits & 1u) != 0 && ui->telemetry().pads[0] > 0.0f, "audition produces actual hit and measured audio peak");
    check (p->takeHitMask() == 0 && p->takePadMeter (0) == 0.0f, "coordinator consumes processor telemetry exactly once");
    const auto snapshot = ui->telemetry();
    ui->selectPad (2); ui->repaint();
    check (ui->telemetry().hits == snapshot.hits && ui->telemetry().pads == snapshot.pads, "selection and painting do not consume meters");
    ui->pollForTesting();
    check (ui->telemetry().hits == 0 && ui->telemetry().pads[0] == 0.0f, "next tick has no fabricated audio activity");

    const char* shots = std::getenv ("VOX_WORKSPACE_SCREENSHOTS");
    juce::MemoryBlock stateBeforeLayout;
    p->getStateInformation (stateBeforeLayout);
    for (auto size : { std::pair { 1040, 600 }, std::pair { 1280, 620 }, std::pair { 1440, 800 }, std::pair { 1600, 900 } })
        for (int editor = 0; editor < 4; ++editor)
        for (bool list : { false, true })
            for (float scale : { 1.0f, 1.25f, 1.5f, 2.0f })
            {
                if (editor != 0 && list) continue;
                vox::drums::workspace::Layout layout;
                layout.primary = static_cast<vox::drums::workspace::Editor> (editor);
                ui->setLayout (layout);
                ui->setSize (size.first, size.second); ui->setListView (list);
                for (int pad = 0; pad < 16; ++pad)
                {
                    check (ui->padSelectButton (pad).isVisible() && ui->padHitButton (pad).isVisible(), "single Rack exposes select and audition together");
                    check (ui->padSelectButton (pad).getWidth() > 0 && ui->padHitButton (pad).getHeight() >= 24,
                           "all 16 select/audition targets remain accessible");
                    check (! ui->padSelectButton (pad).getBounds().intersects (ui->padHitButton (pad).getBounds()),
                           "selection and audition never share a hit target");
                    check (list ? ui->padHitButton (pad).getWidth() > ui->padSelectButton (pad).getWidth()
                                : ui->padHitButton (pad).getHeight() > ui->padSelectButton (pad).getHeight(),
                           "large rubber face auditions; separate smaller affordance selects");
                }
                juce::Image image (juce::Image::ARGB, juce::roundToInt (size.first * scale), juce::roundToInt (size.second * scale), true);
                { juce::Graphics g (image); g.addTransform (juce::AffineTransform::scale (scale)); ui->paintEntireComponent (g, false); }
                if (shots != nullptr && juce::exactlyEqual (scale, 1.0f))
                {
                    juce::File folder { juce::String (shots) }; folder.createDirectory();
                    juce::FileOutputStream stream (folder.getChildFile (juce::String (size.first) + "-editor-" + juce::String (editor)
                                                                       + (list ? "-list.png" : "-grid.png")));
                    check (stream.setPosition (0) && stream.truncate().wasOk(), "replace previous native snapshot");
                    check (juce::PNGImageFormat().writeImageToStream (image, stream), "write native workspace snapshot");
                }
                pump();
            }
    for (int split = 0; split < 3; ++split)
        for (int mixer = 0; mixer < 3; ++mixer)
        {
            vox::drums::workspace::Layout layout;
            layout.split = static_cast<vox::drums::workspace::Split> (split);
            layout.mixer = static_cast<vox::drums::workspace::MixerSize> (mixer);
            ui->setSize (1040, 600); ui->setLayout (layout); ui->setListView (true);
            for (int pad = 0; pad < 16; ++pad)
                check (ui->padHitButton (pad).getHeight() >= 24, "list targets retain height with expanded Mixer and fallback");
        }
    const auto dividerEvent = [&] (juce::Point<float> at, bool down) {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
            down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, ui.get(), ui.get(), juce::Time::getCurrentTime(), at,
            juce::Time::getCurrentTime(), 1, false);
    };
    auto requested = ui->layout();
    const float dividerX = static_cast<float> (requested.leftWidth + 2);
    ui->mouseDown (dividerEvent ({ dividerX, 70.0f }, true));
    ui->mouseDrag (dividerEvent ({ dividerX + 30.0f, 70.0f }, true));
    ui->mouseUp (dividerEvent ({ dividerX + 30.0f, 70.0f }, false));
    check (ui->layout().leftWidth == requested.leftWidth + 32, "left dock divider writes bounded requested width");
    for (int tool = 1; tool < 5; ++tool)
    {
        auto layout = ui->layout();
        layout.rightTool = static_cast<vox::drums::workspace::DockTool> (tool);
        ui->setLayout (layout); ui->pollForTesting();
        auto* page = ui->toolPage (layout.rightTool);
        check (page != nullptr && page->getWidth() < 600, "native right dock owns one narrow tool view");
        if (page) checkToolLayout (*page);
    }
    auto* generator = ui->toolPage (vox::drums::workspace::DockTool::generator);
    auto* styles = dynamic_cast<juce::ComboBox*> (generator->named ("style"));
    check (styles && styles->getNumItems() == 23, "native Generator exposes all 23 actual styles");
    auto* routing = ui->toolPage (vox::drums::workspace::DockTool::routing);
    auto* output = dynamic_cast<juce::ComboBox*> (routing->named ("output.16"));
    check (output && output->getNumItems() == 9, "native Routing retains all nine destinations for pad 16");
    for (int pad = 1; pad <= 16; ++pad)
        check (dynamic_cast<juce::Button*> (routing->named ("lane." + juce::String (pad))) != nullptr,
               "native Routing retains every sequencer lane enable");
    juce::MemoryBlock stateAfterLayout;
    p->getStateInformation (stateAfterLayout);
    const auto decode = [&] (const juce::MemoryBlock& state) {
        return vox::preset::decode ({ static_cast<const std::byte*> (state.getData()), state.getSize() }, p->product().stateTag, 1).document;
    };
    const auto before = decode (stateBeforeLayout), after = decode (stateAfterLayout);
    bool musicalUnchanged = true;
    for (const auto& section : before.sections)
    {
        if (section.tag == vox::drums::layoutSectionTag) continue;
        const auto* next = after.find (section.tag);
        musicalUnchanged = musicalUnchanged && next != nullptr && next->data == section.data;
    }
    check (musicalUnchanged && before.sections.size() == after.sections.size(), "committed layout changes only DLAY; all musical/project sections remain exact");
    juce::MemoryBlock afterPaint;
    ui->createComponentSnapshot (ui->getLocalBounds());
    p->getStateInformation (afterPaint);
    check (afterPaint == stateAfterLayout, "rendering alone never changes even the DLAY bytes");
    p->releaseResources();
}

void testLayoutLifecycle()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    p->commitLayout (layout::factory (layout::Preset::composition));
    p->setUiState ({ 0, 5, 12, 3, 0 });
    const auto save = [&] { juce::MemoryBlock b; p->getStateInformation (b); return b; };
    const auto original = save();
    auto ui = std::make_unique<DrumsWorkspace> (*p);
    ui->setSize (1040, 600);
    check (ui->layout().primary == workspace::Editor::song && ui->layout().secondary == workspace::Editor::step
           && ui->layout().split == workspace::Split::vertical && ui->selection().pad() == 5,
           "native open reads the processor's canonical workspace/selection");
    pump();
    check (save() == original, "opening, readback and forced narrow degradation do not rewrite DLAY");
    ui->pinInspector (true); ui->selectPad (9); ui->setListView (true);
    const auto pinned = save();
    ui.reset(); pump();
    check (save() == pinned, "closed-editor save keeps the last committed layout");
    ui = std::make_unique<DrumsWorkspace> (*p); ui->setSize (1040, 600);
    check (ui->selection().pad() == 9 && ui->selection().inspectorPad() == 5 && ui->selection().pinned(),
           "native reopen retains selected object and independent pinned pad identity");
    check (save() == pinned, "reopen is byte-identical");
    std::thread host ([&] { p->setStateInformation (original.getData(), static_cast<int> (original.getSize())); });
    host.join(); pump();
    check (ui->selection().pad() == 5 && ! ui->selection().pinned() && ui->layout().primary == workspace::Editor::song,
           "host-thread restore reaches the open native editor through its message-thread listener");
    ui->applyWorkspace (layout::factory (layout::Preset::beatmaking));
    const auto beat = save();
    const auto event = [&] (float x, bool down) {
        const juce::Point<float> at { x, 70 };
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
            down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, ui.get(), ui.get(), juce::Time::getCurrentTime(), at,
            juce::Time::getCurrentTime(), 1, false);
    };
    ui->mouseDown (event (210, true)); ui->mouseDrag (event (245, true));
    check (ui->layout().leftWidth == 245 && save() == beat, "save during divider drag uses the prior committed layout");
    ui->mouseUp (event (245, false));
    check (save() != beat, "divider release commits layout");
    ui->mouseDown (event (247, true)); ui->mouseDrag (event (265, true));
    p->setStateInformation (beat.getData(), static_cast<int> (beat.getSize())); pump();
    ui->mouseUp (event (265, false));
    check (ui->layout().leftWidth == 208 && save() == beat, "restore cancels a divider drag; stale release cannot overwrite restored state");
    p->setStateInformation (original.getData(), static_cast<int> (original.getSize()));
    ui.reset(); pump();
    ui = std::make_unique<DrumsWorkspace> (*p);
    check (ui->selection().pad() == 5 && save() == original, "close while restore notification is pending is safe and next open uses restored layout");
    auto longSong = std::make_unique<DrumsProcessor>();
    longSong->editSong ([] (SongData& song) { song.length = 64; });
    longSong->setUiState ({ 0, 14, 63, 3, 0 });
    juce::MemoryBlock longState; longSong->getStateInformation (longState);
    std::thread longHost ([&] { p->setStateInformation (longState.getData(), static_cast<int> (longState.getSize())); });
    longHost.join(); pump();
    check (ui->songEditor().selectionStart() == 63 && ui->patternEditor().bar() == 3 && ui->selection().pad() == 14
           && save() == longState, "open-editor restore applies canonical selection against the new Song length and pattern stores");
}

void testProductionBindings()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    auto* frame = dynamic_cast<vox::suite::SuiteEditor*> (editor.get());
    auto* workspace = frame ? dynamic_cast<DrumsWorkspace*> (frame->content()) : nullptr;
    check (workspace != nullptr, "production createEditor instantiates the native workspace");
    if (! workspace) { p->editorBeingDeleted (editor.get()); return; }
    auto visible = workspace->layout();
    visible.primary = vox::drums::workspace::Editor::sound;
    visible.mixer = vox::drums::workspace::MixerSize::expanded;
    workspace->setLayout (visible);
    std::set<std::string> reached;
    for (const auto& entry : pluginParameters())
    {
        if (entry.pad >= 0) workspace->selectPad (entry.pad);
        const juce::String id (entry.id);
        auto* parameter = p->state().getParameter (id);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
        {
            auto* box = workspace->hostCombo (id);
            check (box != nullptr, "production choice reachable: " + id);
            if (! box) continue;
            reached.insert (entry.id);
            const int target = choice->choices.size() - 1;
            box->setSelectedItemIndex (target, juce::sendNotificationSync);
            check (static_cast<int> (plain (*p, id)) == target, "production choice writes actual host parameter: " + id);
            parameter->setValueNotifyingHost (0.0f); pump();
            check (box->getSelectedItemIndex() == 0, "production choice reads host change: " + id);
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (parameter))
        {
            auto* button = workspace->buttonFor (id);
            check (button != nullptr, "production toggle reachable: " + id);
            if (! button) continue;
            reached.insert (entry.id);
            button->setToggleState (true, juce::sendNotificationSync);
            check (plain (*p, id) >= 0.5f, "production toggle writes actual host parameter: " + id);
            parameter->setValueNotifyingHost (0.0f); pump();
            check (! button->getToggleState(), "production toggle reads host change: " + id);
        }
        else
        {
            auto* slider = workspace->sliderFor (id);
            check (slider != nullptr, "production dial/fader reachable: " + id);
            if (! slider) continue;
            reached.insert (entry.id);
            const auto& range = parameter->getNormalisableRange();
            const auto target = range.snapToLegalValue (range.convertFrom0to1 (0.73f));
            slider->setValue (target, juce::sendNotificationSync);
            check (std::abs (plain (*p, id) - target) <= 0.0001f * std::max (1.0f, std::abs (target)),
                   "production dial/fader writes actual host parameter: " + id);
            const auto readback = range.snapToLegalValue (range.convertFrom0to1 (0.27f));
            parameter->setValueNotifyingHost (range.convertTo0to1 (readback)); pump();
            check (std::abs (static_cast<float> (slider->getValue()) - readback) <= 0.0001f * std::max (1.0f, std::abs (readback)),
                   "production dial/fader reads host change: " + id);
        }
    }
    check (reached.size() == 174 && reached.size() == pluginParameters().size(), "all 174 frozen host parameters exercised in the actual production workspace");
    p->editorBeingDeleted (editor.get()); editor.reset(); pump();
    editor.reset (p->createEditorAndMakeActive());
    frame = dynamic_cast<vox::suite::SuiteEditor*> (editor.get());
    check (frame && dynamic_cast<DrumsWorkspace*> (frame->content()), "production close/reopen retains native editor path");
    p->editorBeingDeleted (editor.get());
}

void testNativeDocking()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    const auto save = [&] { juce::MemoryBlock b; p->getStateInformation (b); return b; };
    const auto initial = save();
    auto ui = std::make_unique<DrumsWorkspace> (*p); ui->setSize (1040, 600);
    auto* hit = &ui->padHitButton (0);
    check (ui->moveDockPanel (1, layout::Type::right), "native action moves the existing Rack to the right dock");
    check (! ui->layout().leftVisible && ui->dockZone (layout::Type::left).groupCount() == 0
           && hit->findParentComponentOfClass<DrumsDockZone>() == &ui->dockZone (layout::Type::right),
           "empty side hides and the same Rack component is reparented, never copied");
    ui->selectPad (7);
    ui->dockZone (layout::Type::right).tabs (0)->setCurrentTabIndex (0); pump();
    check (ui->inspectorSlider ("pan")->findParentComponentOfClass<DrumsDockZone>() == &ui->dockZone (layout::Type::right),
           "native tab click after pad selection activates Inspector on first attempt");
    ui->dockZone (layout::Type::right).tabs (0)->setCurrentTabIndex (1); pump();
    check (&ui->padHitButton (0) == hit && hit->findParentComponentOfClass<DrumsDockZone>() == &ui->dockZone (layout::Type::right),
           "native tab reactivation retains the one physical Rack");
    check (ui->moveDockPanel (4, layout::Type::right, 0, true) && ui->moveDockPanel (5, layout::Type::right, 0, true),
           "native actions stack Generator and Routing as distinct dock groups");
    check (ui->dockZone (layout::Type::right).groupCount() == 3 && ui->toolPage (workspace::DockTool::generator)->getParentComponent()
           && ui->toolPage (workspace::DockTool::routing)->getParentComponent(), "all three active groups own their real existing views");
    const auto full = save();
    check (! ui->moveDockPanel (6, layout::Type::right, 0, true) && save() == full, "fourth native group refused without changing project bytes");
    check (ui->setDockRail (layout::Type::right, true) && ui->layout().rightRail
           && ui->dockZone (layout::Type::right).getWidth() == 26, "rail mode is real native geometry and persists independently of expanded width");
    check (ui->setDockRail (layout::Type::right, false) && ui->dockZone (layout::Type::right).groupCount() == 3
           && &ui->padHitButton (0) == hit, "expanding rail restores all groups and existing panel identities");
    auto& zone = ui->dockZone (layout::Type::right);
    const auto second = zone.tabs (1)->getParentComponent()->getBounds();
    const auto event = [&] (float y, bool down) {
        const juce::Point<float> at { 20, y };
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
            down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &zone, &zone, juce::Time::getCurrentTime(), at,
            juce::Time::getCurrentTime(), 1, false);
    };
    const auto beforeDrag = save();
    zone.mouseDown (event (static_cast<float> (second.getY() - 3), true));
    zone.mouseDrag (event (static_cast<float> (second.getY() + 20), true));
    check (save() == beforeDrag, "group-divider drag never writes an in-progress layout");
    zone.mouseUp (event (static_cast<float> (second.getY() + 20), false));
    check (save() != beforeDrag && layout::encode (p->layoutState().document).has_value(), "group-divider release commits bounded canonical heights");
    const auto stacked = save();
    ui.reset(); pump(); ui = std::make_unique<DrumsWorkspace> (*p); ui->setSize (1040, 600);
    check (ui->dockZone (layout::Type::right).groupCount() == 3 && ! ui->layout().leftVisible && save() == stacked,
           "native close/reopen restores full groups/tabs without rewriting DLAY");
    check (ui->hideDockPanel (4) && ui->dockZone (layout::Type::right).groupCount() == 2,
           "Hide removes the empty Generator group while keeping other groups");
    ui->dockZone (layout::Type::right).tabs (0)->setCurrentTabIndex (0);
    p->setStateInformation (initial.getData(), static_cast<int> (initial.getSize())); pump();
    check (save() == initial && ui->layout().leftVisible && ui->dockZone (layout::Type::right).groupCount() == 1,
           "queued tab action from an old project cannot overwrite host restore");
}

void testResponsiveCentral()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    PatternEditor steps (*p, p->product().accent);
    SongStrip song (*p, p->product().accent);
    PadControlSurface sample (*p, PadControlSurface::Group::sample);
    for (int width : { 366, 580, 820 })
    {
        steps.setSize (width, 500); song.setSize (width, 500); sample.setSize (width, 1000);
        checkToolLayout (steps); checkToolLayout (song); checkToolLayout (sample);
        check (sample.sampleButton ("library")->getWidth() >= 70, "narrow Sample library action retains a readable/clickable target");
    }
}

void testMixerAndFocus()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    DrumsWorkspace ui (*p); ui.setSize (1040, 600);
    const auto find = [&] (auto&& self, juce::Component& parent, const juce::String& name) -> juce::Component* {
        if (parent.getName() == name) return &parent;
        for (auto* child : parent.getChildren()) if (auto* found = self (self, *child, name)) return found;
        return nullptr;
    };
    auto requested = ui.layout(); requested.split = workspace::Split::vertical;
    requested.mixer = workspace::MixerSize::expanded; requested.ratio = 0.15f; ui.setLayout (requested);
    auto* focus = dynamic_cast<juce::Button*> (find (find, ui, "Focus central editor"));
    check (focus != nullptr, "native split has a real focus/restore action");
    if (focus)
    {
        press (*focus);
        check (ui.layout().maximized == 0 && layout::find (p->layoutState().document, layout::Type::central)->payload.back() == 0,
               "focus action maximizes primary and persists CENTRAL selection");
        press (*focus); check (ui.layout().maximized == 255, "focus Restore returns the requested split");
    }
    auto* bank = dynamic_cast<juce::ComboBox*> (find (find, ui, "Mixer bank"));
    check (bank != nullptr, "single Mixer exposes its canonical bank preference");
    if (bank)
    {
        bank->setSelectedItemIndex (1, juce::sendNotificationSync);
        check (layout::find (p->layoutState().document, layout::Type::mixer)->payload[5] == 1, "Mixer bank selection persists without adding host parameters");
    }
    const auto event = [&] (juce::Point<float> at, bool down) {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
            down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &ui, &ui, juce::Time::getCurrentTime(), at,
            juce::Time::getCurrentTime(), 1, false);
    };
    ui.mouseDown (event ({ 210, 70 }, true)); ui.mouseDrag (event ({ 242, 70 }, true)); ui.mouseUp (event ({ 242, 70 }, false));
    check (std::abs (ui.layout().ratio - 0.15f) < 0.0001f, "moving a dock divider does not rewrite a valid narrow split ratio");
    juce::MemoryBlock before; p->getStateInformation (before);
    ui.mouseDown (event ({ 500, 317 }, true)); ui.mouseDrag (event ({ 500, 290 }, true));
    juce::MemoryBlock during; p->getStateInformation (during);
    check (during == before, "Mixer height drag saves the last committed preference");
    ui.mouseUp (event ({ 500, 290 }, false));
    check (ui.layout().expandedHeight == 310, "Mixer divider release persists its bounded expanded height");
}

void testNativeMidiScopes()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    DrumsWorkspace ui (*p); ui.setSize (1040, 600);
    auto view = ui.layout(); view.primary = workspace::Editor::song; ui.setLayout (view);
    auto* container = dynamic_cast<juce::DragAndDropContainer*> (&ui);
    check (container && juce::DragAndDropContainer::findParentDragContainerFor (&ui.patternEditor()) == container,
           "production workspace supplies the actual external MIDI drag container");
    auto session = p->uiState(); session.exportScope = 1; p->setUiState (session);
    std::vector<std::shared_ptr<const ExportSnapshot>> captures;
    ui.patternEditor().saveDialogForTesting = [&] (auto frozen) { captures.push_back (std::move (frozen)); };
    press (*ui.patternEditor().button ("midi"));
    check (captures.size() == 1 && captures.back()->scope == ExportSnapshot::Scope::pattern,
           "native Step MIDI has fixed Pattern scope, independent of the donor scope selector");
    auto* songMidi = ui.songEditor().button ("midi");
    check (songMidi != nullptr, "native Song has its own directly reachable MIDI save/drag handle");
    if (songMidi)
    {
        for (int length : { 16, 32, 64 })
        {
            p->editSong ([=] (SongData& song) { song.length = static_cast<std::uint8_t> (length); });
            const auto before = captures.size(); press (*songMidi);
            check (captures.size() == before + 1 && captures.back()->scope == ExportSnapshot::Scope::song
                   && captures.back()->songLength == length, "native Song MIDI captures the actual entire 16/32/64-bar song");
        }
        check (! ui.songEditor().combo ("scope")->isVisible(), "native fixed-scope handles replace the old global export selector");
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("vox-native-midi", {}, false);
        root.createDirectory(); PatternEditor::setDragRootForTesting (root);
        std::vector<juce::File> files;
        ui.patternEditor().externalDragForTesting = [&] (const auto& file) { files.push_back (file); };
        const auto drag = [&] (juce::Component& button) {
            const auto event = [&] (juce::Point<float> at, bool down) {
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                    down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
                    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &button, &button, juce::Time::getCurrentTime(), { 6, 6 },
                    juce::Time::getCurrentTime(), 1, false);
            };
            button.mouseDown (event ({ 6, 6 }, true)); button.mouseDrag (event ({ 24, 6 }, true)); button.mouseUp (event ({ 24, 6 }, false));
        };
        const auto savedClicks = captures.size();
        const auto songBytes = DrumsProcessor::renderExport (*captures.back());
        drag (*songMidi);
        juce::MemoryBlock songFile;
        check (files.size() == 1 && files[0].getFileName() == "VOX Drums Song.mid" && files[0].loadFileAsData (songFile)
               && songFile == songBytes && captures.size() == savedClicks, "native Song drag writes the same frozen bytes as Save and never also clicks");
        drag (*ui.patternEditor().button ("midi"));
        juce::MemoryBlock patternFile, songStill;
        check (files.size() == 2 && files[1] != files[0] && files[1].loadFileAsData (patternFile)
               && patternFile == DrumsProcessor::renderExport (*p->captureExport (ExportSnapshot::Scope::pattern))
               && files[0].loadFileAsData (songStill) && songStill == songBytes, "native Pattern/Song drags keep distinct immutable handoff files");
        const auto blocked = root.getChildFile ("not-a-directory"); blocked.replaceWithText ("blocked");
        PatternEditor::setDragRootForTesting (blocked); drag (*songMidi); ui.songEditor().update();
        juce::Label* status = nullptr;
        for (auto* child : ui.songEditor().getChildren())
            if (child->getName() == "Song MIDI export status") status = dynamic_cast<juce::Label*> (child);
        check (files.size() == 2 && status && status->getText().contains ("MIDI export failed") && status->isVisible(),
               "native Song export failure starts no drag and is visible in the Song view");
        PatternEditor::setDragRootForTesting ({}); root.deleteRecursively();
    }
}
void testStepContextModel()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    auto a = std::make_unique<StoredPattern>(); a->bars = 1;
    auto b = std::make_unique<StoredPattern> (*a); b->mask (2, 0) = 1;
    for (int i : { 0, 1 }) { a->cell (2, 0, i).on = true; a->cell (2, 0, i).velocity = 10; }
    for (int i : { 0, 1, 5, 6 }) { b->cell (2, 0, i).on = true; b->cell (2, 0, i).velocity = 90; }
    check (a->valid() && b->valid(), "A/B fixtures obey actual active/reserved slot rules");
    p->patterns().setSlot (1, *a); p->patterns().setAlternate (1, *b);
    StepTarget target { 1, 1, 0, 2, 0, 1, -1 };
    auto values = inspectSteps (*p, target);
    check (values && values->cells.size() == 2 && values->cells[0].velocity == 90, "explicit step target resolves B without changing playback or A");
    if (! values) return;
    const auto before = p->patterns().encodeCurrent().data;
    p->patternHistory().clear();
    check (editSteps (*p, target, StepField::velocity, 42, *values), "scoped B range edits canonical cells");
    values = inspectSteps (*p, target);
    check (values && editSteps (*p, target, StepField::velocity, 73, *values, true)
           && p->patternHistory().size() == 1, "one B range gesture amends one real history entry");
    auto actualA = std::make_unique<StoredPattern>(), actualB = std::make_unique<StoredPattern>();
    p->patterns().copySlot (1, *actualA); p->patterns().copyAlternate (1, *actualB);
    auto expectedB = std::make_unique<StoredPattern> (*b); expectedB->cell (2, 0, 0).velocity = expectedB->cell (2, 0, 1).velocity = 73;
    check (*actualA == *a && *actualB == *expectedB, "B range edit affects exactly its cells, leaves entire A and rest of B untouched");
    const auto after = p->patterns().encodeCurrent().data;
    p->patternHistory().undo (p->patterns()); check (p->patterns().encodeCurrent().data == before, "B range Undo restores full DSEQ byte-exactly");
    p->patternHistory().redo (p->patterns()); check (p->patterns().encodeCurrent().data == after, "B range Redo restores full DSEQ byte-exactly");
    target.first = 5; target.last = 6; values = inspectSteps (*p, target);
    check (values && values->ticks[0] != values->ticks[1], "range fixture crosses alternative and straight beat subdivisions");
    if (! values) return;
    if (values)
    {
        check (editSteps (*p, target, StepField::micro, 25, *values), "scoped mixed-duration micro edit applies");
        const auto edited = inspectSteps (*p, target);
        check (edited && edited->cells[0].microTicks == kit::percentToTicks (25, values->ticks[0])
               && edited->cells[1].microTicks == kit::percentToTicks (25, values->ticks[1]), "each selected cell uses its own canonical tick duration");
    }
    auto invalid = target; invalid.bar = 3;
    const auto unchanged = p->patterns().encodeCurrent().data;
    check (! inspectSteps (*p, invalid) && ! editSteps (*p, invalid, StepField::velocity, 99, *values)
           && p->patterns().encodeCurrent().data == unchanged, "unavailable pinned bar is refused, never clamped to a different object");
    values = inspectSteps (*p, target);
    std::vector<std::byte> external;
    p->patterns().beforeCommitForTesting = [&] {
        p->patterns().beforeCommitForTesting = {};
        auto changed = std::make_unique<StoredPattern>(); p->patterns().copySlot (0, *changed);
        changed->cell (7, 0, 0).on = true; p->patterns().setSlot (0, *changed); external = p->patterns().encodeCurrent().data;
    };
    check (values && ! editSteps (*p, target, StepField::velocity, 33, *values)
           && p->patterns().encodeCurrent().data == external, "conditional commit refuses an external write inserted immediately before the store lock");
    juce::MemoryBlock saved; p->getStateInformation (saved);
    values = inspectSteps (*p, target);
    p->patterns().beforeCommitForTesting = [&] {
        p->patterns().beforeCommitForTesting = {};
        p->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    };
    check (values && ! editSteps (*p, target, StepField::velocity, 44, *values)
           && p->patterns().encodeCurrent().data == external && p->patternHistory().size() == 0,
           "restore at the commit boundary wins without a stale edit or old-project Undo entry");
    bool sawRestore = false;
    p->patterns().beforeCommitForTesting = [&] {
        sawRestore = p->restoringMusic() && ! inspectSteps (*p, target);
    };
    p->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    p->patterns().beforeCommitForTesting = {};
    check (sawRestore && ! p->restoringMusic(), "scoped Inspector cannot read partially applied musical restore");
    p->patterns().setSlot (0, *a);
    StepTarget songTarget { 0, 0, 0, 2, 0, 0, 0 };
    auto songValues = inspectSteps (*p, songTarget);
    check (songValues.has_value(), "Song step context resolves its actual linked source");
    if (songValues)
    {
        const auto songBefore = p->patterns().song(); const auto sourceBefore = p->patterns().encodeCurrent().data;
        p->patternHistory().clear();
        check (! editSteps (*p, songTarget, StepField::velocity, songValues->cells[0].velocity, *songValues)
               && p->patterns().song() == songBefore && p->patternHistory().size() == 0, "unchanged Song Inspector value never unlinks or creates history");
        check (editSteps (*p, songTarget, StepField::velocity, 74, *songValues), "first actual Song Inspector edit commits");
        const auto songAfter = p->patterns().song();
        check (songAfter.bars[0].kind == SongBar::Kind::unique && p->patterns().encodeCurrent().data == sourceBefore,
               "Song Inspector changes only its bar and preserves source DSEQ");
        p->patternHistory().undo (p->patterns()); check (p->patterns().song() == songBefore, "Song Inspector Undo restores linkage and all Song data");
        p->patternHistory().redo (p->patterns()); check (p->patterns().song() == songAfter, "Song Inspector Redo restores unique bar exactly");
    }
    for (int length : { 16, 32, 64 })
    {
        auto song = SongData::initial(); song.length = static_cast<std::uint8_t> (length); song.bars[static_cast<std::size_t> (length - 1)] = {};
        p->patterns().setSong (song); songTarget.song = length - 1;
        songValues = inspectSteps (*p, songTarget); p->patternHistory().clear();
        check (songValues && editSteps (*p, songTarget, StepField::on, 1, *songValues), "16/32/64-bar Song Inspector can activate a cell in an empty last bar");
        p->patternHistory().undo (p->patterns());
        check (p->patterns().song() == song, "16/32/64-bar Song Inspector Undo restores the exact empty bar");
        songTarget.song = length;
        check (! inspectSteps (*p, songTarget), "Song context outside current length is unavailable, never redirected");
    }
}
void testContextStepInspector()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    auto fixture = std::make_unique<StoredPattern>(); fixture->bars = 1;
    fixture->cell (2, 0, 0).on = true; fixture->cell (2, 0, 0).velocity = 63;
    fixture->cell (2, 0, 1).on = true; fixture->cell (2, 0, 1).velocity = 29;
    fixture->cell (3, 0, 1).on = true; fixture->cell (3, 0, 1).velocity = 17;
    p->patterns().setSlot (0, *fixture);
    DrumsWorkspace ui (*p); ui.setSize (1280, 700);
    ui.patternEditor().selectCell (2, 0); ui.pollForTesting();
    auto* velocity = ui.inspectorSlider ("velocity");
    check (velocity && velocity->isVisible(), "context Inspector follows the actual selected step");
    if (! velocity) return;
    check (juce::roundToInt (velocity->getValue()) == 63, "step Inspector reads canonical step velocity");
    ui.pinInspector (true); ui.patternEditor().selectCell (3, 1); ui.pollForTesting();
    check (juce::roundToInt (velocity->getValue()) == 63, "step pin freezes its object while grid selection changes");
    check (layout::find (p->layoutState().document, layout::Type::inspectorPin)->payload == layout::Bytes { 2, 0, 0, 0, 2, 0, 0 },
           "step pin persists the approved slot/variation/bar/lane/range address");
    velocity->setValue (88, juce::sendNotificationSync);
    auto expected = std::make_unique<StoredPattern> (*fixture); expected->cell (2, 0, 0).velocity = 88;
    auto actual = std::make_unique<StoredPattern>(); p->patterns().copySlot (0, *actual);
    check (*actual == *expected, "pinned step edit affects exactly its address, not the new grid selection");
    ui.pinInspector (false); ui.pollForTesting();
    check (juce::roundToInt (velocity->getValue()) == 17, "unpin follows the current grid step again");
    ui.patternEditor().selectCell (2, 0); ui.patternEditor().selectCell (2, 1, true); ui.pollForTesting();
    check (ui.patternEditor().selectionFirst() == 0 && ui.patternEditor().selectionLast() == 1
           && velocity->getTextFromValue (velocity->getValue()) == "Mixed", "range selection exposes honest mixed values in the one context Inspector");
    check (juce::exactlyEqual (velocity->getValueFromText ("Mixed"), velocity->getValue()), "unchanged Mixed placeholder never parses as a new numeric value");
    ui.patternEditor().setParameterLanes ({ 1, 3 });
    check (ui.patternEditor().selectionFirst() == 0 && ui.patternEditor().selectionLast() == 1,
           "layout-only lane presentation commits preserve ephemeral step range selection");
    const auto rangeBefore = p->patterns().encodeCurrent().data;
    p->patternHistory().clear(); ui.pinInspector (true); ui.patternEditor().selectCell (3, 1); ui.pollForTesting();
    check (layout::find (p->layoutState().document, layout::Type::inspectorPin)->payload == layout::Bytes { 2, 0, 0, 0, 2, 0, 1 },
           "range pin persists its exact first/last address");
    if (const auto* shots = std::getenv ("VOX_WORKSPACE_SCREENSHOTS"))
    {
        ui.setSize (1040, 600); ui.pollForTesting();
        juce::Image image (juce::Image::ARGB, ui.getWidth(), ui.getHeight(), true);
        { juce::Graphics graphics (image); ui.paintEntireComponent (graphics, false); }
        juce::File folder { juce::String (shots) }; folder.createDirectory();
        juce::FileOutputStream stream (folder.getChildFile ("inspector-step-range-pinned.png"));
        check (stream.setPosition (0) && stream.truncate().wasOk() && juce::PNGImageFormat().writeImageToStream (image, stream),
               "write actual native pinned-range Inspector snapshot");
        ui.setSize (1280, 700);
    }
    velocity->setValue (55, juce::sendNotificationSync);
    p->patterns().copySlot (0, *actual);
    check (actual->cell (2, 0, 0).velocity == 55 && actual->cell (2, 0, 1).velocity == 55
           && actual->cell (3, 0, 1).velocity == 17 && p->patternHistory().size() == 1,
           "pinned numeric range edit changes exactly that range in one Undo entry");
    p->patternHistory().undo (p->patterns()); ui.pollForTesting();
    check (p->patterns().encodeCurrent().data == rangeBefore, "Inspector range Undo restores complete DSEQ bytes");
    checkToolLayout (*velocity->getParentComponent()->getParentComponent());
    const auto event = [velocity] (juce::Point<float> at, juce::Point<float> origin, bool down) {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
            juce::ModifierKeys (down ? juce::ModifierKeys::leftButtonModifier : 0), 1.0f, 0, 0, 0, 0,
            velocity, velocity, juce::Time::getCurrentTime(), origin, juce::Time::getCurrentTime(), 1, true);
    };
    const auto origin = velocity->getLocalBounds().getCentre().toFloat(), end = origin.translated (0, -45);
    p->patternHistory().clear();
    velocity->mouseDown (event (origin, origin, true)); velocity->mouseDrag (event (end, origin, true));
    velocity->mouseUp (event (end, origin, false));
    check (p->patternHistory().size() == 1, "real Inspector slider drag is one range Undo gesture");
    p->patternHistory().undo (p->patterns()); ui.pollForTesting();
    check (p->patterns().encodeCurrent().data == rangeBefore, "real Inspector drag Undo restores all musical bytes");
    juce::MemoryBlock saved; p->getStateInformation (saved);
    velocity->mouseDown (event (origin, origin, true));
    p->setStateInformation (saved.getData(), static_cast<int> (saved.getSize())); pump(); ui.pollForTesting();
    juce::MemoryBlock restored; p->getStateInformation (restored);
    velocity->mouseDrag (event (end, origin, true)); velocity->mouseDrag (event (end.translated (0, -15), origin, true));
    velocity->mouseUp (event (end, origin, false));
    juce::MemoryBlock afterRestore; p->getStateInformation (afterRestore);
    check (afterRestore == restored && p->patternHistory().size() == 0, "restore cancels the entire remaining Inspector gesture without new-project writes/Undo");
    ui.pinInspector (false); ui.patternEditor().selectCell (2, 0); ui.pollForTesting();
    p->patterns().copySlot (0, *actual); actual->cell (2, 0, 0).velocity = 0;
    check (actual->valid(), "zero velocity is an accepted canonical DSEQ boundary");
    p->patterns().setSlot (0, *actual); const auto zero = p->patterns().encodeCurrent().data; ui.pollForTesting();
    check (juce::exactlyEqual (velocity->getValue(), 0.0) && p->patterns().encodeCurrent().data == zero,
           "Inspector displays a stored zero velocity without clamping or modifying music");
    check (ui.patternEditor().inspector ("velocity") == nullptr && ui.patternEditor().accentToggle() == nullptr,
           "native Step uses the single context Inspector; donor-only duplicate controls are unavailable");

    auto alternate = std::make_unique<StoredPattern> (*fixture); alternate->cell (2, 0, 0).velocity = 97;
    p->patterns().setAlternate (1, *alternate);
    auto document = p->layoutState().document;
    for (auto& record : document.records)
        if (record.type == static_cast<layout::Byte> (layout::Type::inspectorPin)) record.payload = { 2, 1, 1, 0, 2, 0, 0 };
    check (ui.applyWorkspace (document), "approved B step pin loads into native Inspector");
    ui.pollForTesting();
    check (juce::roundToInt (velocity->getValue()) == 97, "loaded B pin reads its actual alternate, never A");
    juce::MemoryBlock pinnedB; p->getStateInformation (pinnedB);
    auto q = std::make_unique<DrumsProcessor>(); q->setStateInformation (pinnedB.getData(), static_cast<int> (pinnedB.getSize()));
    DrumsWorkspace reopened (*q); reopened.setSize (1040, 600); reopened.pollForTesting();
    auto* reopenedVelocity = reopened.inspectorSlider ("velocity");
    check (reopenedVelocity && juce::roundToInt (reopenedVelocity->getValue()) == 97, "new editor/project restores the exact B pin address");
    const auto aBefore = q->patterns().slot (1);
    if (reopenedVelocity) reopenedVelocity->setValue (48, juce::sendNotificationSync);
    check (q->patterns().slot (1) == aBefore && q->patterns().alternate (1).cell (2, 0, 0).velocity == 48,
           "restored B pin edits only B, without redirecting to canonical A");
    for (auto& record : document.records)
        if (record.type == static_cast<layout::Byte> (layout::Type::inspectorPin)) record.payload = { 2, 1, 1, 3, 2, 0, 0 };
    check (ui.applyWorkspace (document), "structurally valid pin can reference a currently unavailable pattern bar");
    ui.pollForTesting(); const auto unavailableBefore = p->patterns().encodeCurrent().data;
    check (! velocity->isEnabled(), "unavailable pin disables numeric editing while retaining Unpin");
    velocity->setValue (110, juce::sendNotificationSync);
    check (p->patterns().encodeCurrent().data == unavailableBefore, "late numeric callback cannot edit an unavailable pin");
    ui.pinInspector (false); ui.patternEditor().selectCell (2, 0); ui.pollForTesting();
    auto& steps = ui.patternEditor();
    const auto point = steps.cellBounds (2, 1).getCentre();
    auto* grid = steps.getComponentAt (point);
    check (grid && grid->getName() == "pattern grid", "range gesture reaches the actual native grid");
    if (grid)
    {
        const auto local = grid->getLocalPoint (&steps, point).toFloat();
        const auto beforeRange = p->patterns().encodeCurrent().data;
        grid->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), local,
            juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier), 1.0f, 0, 0, 0, 0,
            grid, grid, juce::Time::getCurrentTime(), local, juce::Time::getCurrentTime(), 1, false));
        check (steps.selectionFirst() == 0 && steps.selectionLast() == 1 && p->patterns().encodeCurrent().data == beforeRange,
               "real Shift-click selects a range without toggling music");
    }
}
void testSongContextInspector()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    auto a = std::make_unique<StoredPattern>(); a->bars = 2;
    auto b = std::make_unique<StoredPattern> (*a); b->cell (2, 1, 0).on = true; b->cell (2, 1, 0).velocity = 91;
    p->patterns().setSlot (2, *a); p->patterns().setAlternate (2, *b);
    auto song = SongData::initial();
    for (int index : { 5, 6 }) { auto& bar = song.bars[static_cast<std::size_t> (index)]; bar.kind = SongBar::Kind::pattern; bar.slot = 2; bar.variation = 1; bar.patternBar = 3; }
    p->patterns().setSong (song);
    DrumsWorkspace ui (*p); ui.setSize (1280, 700);
    const auto sourceBefore = p->patterns().encodeCurrent().data;
    ui.songEditor().select (5, 1); ui.pollForTesting();
    auto* source = ui.inspectorButton ("editSource");
    auto* local = ui.inspectorButton ("editThisBar");
    check (source && local && source->isVisible() && local->isVisible(), "Song bar Inspector exposes explicit source and per-bar targets");
    if (! source || ! local) return;
    check (p->patterns().song() == song && p->patterns().encodeCurrent().data == sourceBefore,
           "Song context selection never creates a unique bar or changes source data");
    ui.pinInspector (true); ui.songEditor().select (6, 1); ui.pollForTesting();
    check (layout::find (p->layoutState().document, layout::Type::inspectorPin)->payload == layout::Bytes { 3, 5 },
           "Song Inspector pin records the approved exact bar address");
    press (*source); ui.pollForTesting();
    check (ui.patternEditor().slot() == 2 && ui.patternEditor().bar() == 1 && ui.patternEditor().songBar() == -1
           && juce::roundToInt (plain (*p, patternParameterId)) == 0 && p->patterns().song() == song,
           "Edit Source opens the pinned B source/bar without changing playback slot or Song linkage");
    ui.pinInspector (false); ui.patternEditor().selectCell (2, 0); ui.pollForTesting();
    auto* velocity = ui.inspectorSlider ("velocity");
    check (velocity && juce::roundToInt (velocity->getValue()) == 91, "source Step Inspector reads B, not A");
    if (! velocity) return;
    p->patternHistory().clear(); velocity->setValue (44, juce::sendNotificationSync); ui.pollForTesting();
    auto actual = std::make_unique<StoredPattern>(); p->patterns().copyAlternate (2, *actual);
    check (actual->cell (2, 1, 0).velocity == 44 && p->patterns().slot (2) == *a && p->patterns().song() == song,
           "source edit changes B for all linked occurrences, leaving A and Song references exact");
    auto snapshot = ui.patternEditor().snapshot();
    check (snapshot && snapshot->slot == 2 && snapshot->pattern == *actual, "source MIDI snapshot matches the actual B pattern displayed and edited");
    check (snapshot && snapshot->trackName.endsWith ("P3 B") && ui.patternEditor().fileName().endsWith ("P3 B.mid"),
           "source MIDI names identify the explicit B source rather than an unrelated playback slot");
    const auto sourceAfter = p->patterns().encodeCurrent().data;
    p->patternHistory().undo (p->patterns()); check (p->patterns().encodeCurrent().data == sourceBefore, "source B Undo restores full DSEQ exactly");
    p->patternHistory().redo (p->patterns()); check (p->patterns().encodeCurrent().data == sourceAfter, "source B Redo restores full DSEQ exactly");
    p->patternHistory().clear(); ui.patternEditor().clickCell (3, 1);
    p->patterns().copyAlternate (2, *actual);
    check (actual->cell (3, 1, 1).on && p->patterns().slot (2) == *a && p->patterns().song() == song
           && p->patternHistory().size() == 1, "actual source grid toggle edits B only in one Undo entry");
    p->patternHistory().undo (p->patterns()); ui.pollForTesting();
    check (p->patterns().encodeCurrent().data == sourceAfter, "source grid Undo restores the entire prior DSEQ");
    ui.songEditor().select (6, 1); ui.pollForTesting();
    local = ui.inspectorButton ("editThisBar");
    check (local != nullptr, "Song selection returns contextual bar controls after source edits");
    if (! local) return;
    press (*local); ui.patternEditor().selectCell (2, 0); ui.pollForTesting();
    check (ui.patternEditor().songBar() == 6 && p->patterns().song() == song, "Edit This Bar selects its scope without eagerly unlinking");
    velocity = ui.inspectorSlider ("velocity");
    if (! velocity) { check (false, "bar step context exists"); return; }
    p->patternHistory().clear(); velocity->setValue (35, juce::sendNotificationSync); ui.pollForTesting();
    const auto changed = p->patterns().song();
    check (changed.bars[6].kind == SongBar::Kind::unique && changed.bars[5] == song.bars[5]
           && changed.bars[6].cells->cell (2, 0).velocity == 35 && p->patterns().encodeCurrent().data == sourceAfter,
           "first per-bar edit changes only that occurrence and preserves B source");
    p->patternHistory().undo (p->patterns()); check (p->patterns().song() == song, "bar-target Undo restores all exact Song references");
    p->patternHistory().redo (p->patterns()); check (p->patterns().song() == changed, "bar-target Redo restores unique data exactly");
    ui.songEditor().select (6, 1); ui.pollForTesting();
    source = ui.inspectorButton ("editSource");
    check (source && ! source->isEnabled(), "a unique Song bar has no invented source target");
    ui.songEditor().select (5, 1); ui.pinInspector (true); ui.songEditor().select (6, 1); ui.pollForTesting();
    juce::MemoryBlock pinned; p->getStateInformation (pinned);
    auto q = std::make_unique<DrumsProcessor>(); q->setStateInformation (pinned.getData(), static_cast<int> (pinned.getSize()));
    DrumsWorkspace reopened (*q); reopened.setSize (1040, 600); reopened.pollForTesting();
    auto* reopenedSource = reopened.inspectorButton ("editSource");
    auto* reopenedLocal = reopened.inspectorButton ("editThisBar");
    check (reopenedSource && reopenedLocal && reopenedSource->isEnabled() && reopenedLocal->isEnabled(),
           "Song pin save/new-editor restores linked bar5 despite unique bar6 selection");
    if (reopenedSource) checkToolLayout (*reopenedSource->getParentComponent());
    auto document = q->layoutState().document;
    for (auto& record : document.records)
        if (record.type == static_cast<layout::Byte> (layout::Type::inspectorPin)) record.payload = { 3, 31 };
    check (reopened.applyWorkspace (document), "Song pin can retain a valid-format address outside current Song length");
    reopened.pollForTesting();
    check (reopenedSource && reopenedLocal && ! reopenedSource->isEnabled() && ! reopenedLocal->isEnabled(),
           "out-of-length Song pin disables both editing targets rather than clamping to another bar");
    const auto unavailable = q->patterns().song();
    if (reopenedSource) press (*reopenedSource);
    if (reopenedLocal) press (*reopenedLocal);
    check (q->patterns().song() == unavailable, "late callbacks on an unavailable Song pin cannot write musical data");
    ui.setSize (1040, 600);
    auto composition = ui.layout(); composition.primary = workspace::Editor::song; composition.secondary = workspace::Editor::step;
    composition.split = workspace::Split::horizontal; ui.setLayout (composition);
    checkToolLayout (ui.songEditor());
    if (const auto* destination = std::getenv ("VOX_WORKSPACE_SCREENSHOTS"))
    {
        juce::File folder (destination); folder.createDirectory(); const auto image = ui.createComponentSnapshot (ui.getLocalBounds());
        juce::FileOutputStream stream (folder.getChildFile ("inspector-song-pinned.png"));
        check (stream.setPosition (0) && stream.truncate().wasOk() && juce::PNGImageFormat().writeImageToStream (image, stream),
               "write actual native Song bar Inspector snapshot");
    }
    source = ui.inspectorButton ("editSource");
    if (source) press (*source);
    auto frozenSource = ui.patternEditor().snapshot();
    const auto frozenMidi = frozenSource ? DrumsProcessor::renderExport (*frozenSource) : juce::MemoryBlock {};
    juce::MemoryBlock restored; p->getStateInformation (restored);
    p->setStateInformation (restored.getData(), static_cast<int> (restored.getSize()));
    check (frozenSource && ! ui.patternEditor().snapshot() && DrumsProcessor::renderExport (*frozenSource) == frozenMidi,
           "restore invalidates new source captures while the already frozen source MIDI remains exact");
    ui.patternEditor().saveExport (ui.patternEditor().snapshot());
    check (ui.patternEditor().exportMessage().contains ("context changed"), "rejected source export has a visible reason");
    ui.pollForTesting();
    check (! ui.patternEditor().editingSource() && ui.songEditor().sourceBar() == -1 && ui.songEditor().editedBar() == -1,
           "completed restore clears ephemeral source/bar edit modes before the next action");
}
void testNativeStyleLibrary()
{
    using namespace vox::drums;
    struct FolderScope {
        juce::String old = juce::SystemStats::getEnvironmentVariable ("VOX_DRUMS_STYLES", {});
        juce::File root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vox-native-styles-" + juce::Uuid().toString());
        static void set (const juce::String& value) {
           #if JUCE_WINDOWS
            _wputenv_s (L"VOX_DRUMS_STYLES", value.toWideCharPointer());
           #else
            if (value.isEmpty()) unsetenv ("VOX_DRUMS_STYLES"); else setenv ("VOX_DRUMS_STYLES", value.toRawUTF8(), 1);
           #endif
        }
        FolderScope() { root.createDirectory(); set (root.getChildFile ("styles").getFullPathName()); }
        ~FolderScope() { set (old); root.deleteRecursively(); }
    } folder;
    const bool isolated = StyleLibrary::defaultFolder() == folder.root.getChildFile ("styles");
    check (isolated, "native style test isolates all library writes in its temporary folder");
    if (! isolated) return;
    auto p = std::make_unique<DrumsProcessor>();
    DrumsWorkspace ui (*p); ui.setSize (1040, 600);
    auto* library = ui.toolPage (workspace::DockTool::library);
    auto* category = dynamic_cast<juce::ComboBox*> (library->named ("library.category"));
    check (category != nullptr, "native Library offers the user-style service alongside musical presets");
    if (! category) return;
    category->setSelectedId (2, juce::sendNotificationSync);
    for (const auto* name : { "style.import", "style.export", "style.use", "style.refresh", "style.remove", "style.list", "style.status" })
        check (library->named (name) != nullptr, juce::String ("native style workflow exposes actual control ") + name);
    auto* panel = dynamic_cast<StyleLibraryPanel*> (library->named ("styles"));
    check (panel != nullptr, "production native Library delegates to the actual style-service controller");
    if (! panel) return;
    StyleLibrary service (folder.root.getChildFile ("styles"));
    auto definition = *style::factoryDefinition (0); definition.id = "12345678-1234-4abc-9abc-123456789abc";
    definition.key = "native-library-test"; definition.name = "Native library test"; definition.meta.author = "Native tester";
    const auto input = folder.root.getChildFile ("incoming.voxstyle"); check (input.replaceWithText (style::serialize (definition)), "write native user-style fixture");
    auto generator = p->generator(); generator.density = 0.42f; p->setGenerator (generator);
    const auto save = [&] { juce::MemoryBlock state; p->getStateInformation (state); return state; };
    const auto before = save(); const auto pattern = p->patterns().encodeCurrent().data; const auto song = p->patterns().song();
    const auto imported = panel->importFile (input);
    check (imported.status == StyleLibrary::ImportResult::Status::added && service.list().size() == 1 && save() == before,
           "native import adds a real file without changing project bytes or applying/generating music");
    if (imported.status != StyleLibrary::ImportResult::Status::added) return;
    check (panel->useSelected() && p->generator().custom && p->generator().custom->id == definition.id
           && std::abs (p->generator().density - 0.42f) < 0.001f && p->patterns().encodeCurrent().data == pattern && p->patterns().song() == song,
           "explicit Use embeds the chosen style, preserves amounts and never generates patterns or Song");
    if (! p->generator().custom) return;
    const auto project = save(); const auto originalHash = style::documentHash (*p->generator().custom);
    auto changed = definition; changed.name = "Incoming version"; changed.rules[0].velocity = 0.91f;
    input.replaceWithText (style::serialize (changed));
    const auto conflict = panel->importFile (input);
    check (conflict.status == StyleLibrary::ImportResult::Status::conflict && conflict.incoming
           && style::documentHash (*conflict.incoming) == style::documentHash (changed)
           && save() == project, "native conflict retains the validated incoming snapshot and leaves the project untouched");
    auto later = changed; later.name = "Later disk edit"; later.rules[0].velocity = 0.77f; input.replaceWithText (style::serialize (later));
    press (*dynamic_cast<juce::Button*> (panel->named ("style.replace")));
    auto stored = service.find (definition.id);
    check (stored && style::documentHash (*stored) == style::documentHash (changed)
           && style::documentHash (*p->generator().custom) == originalHash && save() == project,
           "Replace uses the confirmed frozen input, not a later file edit, and projects retain their exact own copy");
    auto* notice = dynamic_cast<juce::Label*> (panel->named ("style.notice"));
    check (notice && notice->getText().contains ("own version"), "native notice distinguishes project and library versions");
    panel->importFile (input); press (*dynamic_cast<juce::Button*> (panel->named ("style.keepBoth")));
    const auto entries = service.list();
    check (entries.size() == 2 && entries[0].definition->id != entries[1].definition->id && save() == project,
           "Keep both creates a distinct canonical UUID without altering the project");
    std::function<void (juce::File)> exportCompleted;
    panel->fileDialogForTesting = [&] (bool isSave, const juce::File&, std::function<void (juce::File)> completed) {
        check (isSave, "Export uses the save-file dialog completion"); exportCompleted = std::move (completed);
    };
    panel->selectLibraryStyle (definition.id);
    press (*dynamic_cast<juce::Button*> (panel->named ("style.export")));
    auto* styleList = dynamic_cast<juce::ListBox*> (panel->named ("style.list")); styleList->selectRow (0);
    const auto frozenOutput = folder.root.getChildFile ("frozen-dialog.voxstyle");
    check (static_cast<bool> (exportCompleted), "native Export freezes its selection before opening the file dialog");
    if (exportCompleted) exportCompleted (frozenOutput);
    style::Definition frozenExport;
    check (style::parse (frozenOutput.loadFileAsString(), frozenExport).wasOk()
           && style::documentHash (frozenExport) == style::documentHash (changed) && save() == project,
           "changing selection during Export cannot replace the frozen library version with the project copy");
    panel->fileDialogForTesting = {};
    juce::String copiedId;
    for (const auto& entry : entries) if (entry.definition->id != definition.id) copiedId = entry.definition->id;
    panel->selectLibraryStyle (copiedId);
    press (*dynamic_cast<juce::Button*> (panel->named ("style.remove")));
    check (service.find (copiedId) != nullptr, "Remove requires an explicit confirmation before deleting its library file");
    press (*dynamic_cast<juce::Button*> (panel->named ("style.cancel")));
    check (service.find (copiedId) != nullptr, "Cancel removal retains the library file");
    panel->selectLibraryStyle (definition.id); press (*dynamic_cast<juce::Button*> (panel->named ("style.remove")));
    service.importText (style::serialize (later), StyleLibrary::Collision::replace);
    press (*dynamic_cast<juce::Button*> (panel->named ("style.confirmRemove")));
    check (service.find (definition.id) && style::documentHash (*service.find (definition.id)) == style::documentHash (later),
           "stale removal confirmation refuses to delete a library entry replaced by another writer");
    for (const auto& id : { copiedId, definition.id })
    {
        panel->selectLibraryStyle (id); press (*dynamic_cast<juce::Button*> (panel->named ("style.remove")));
        press (*dynamic_cast<juce::Button*> (panel->named ("style.confirmRemove")));
    }
    check (service.list().empty() && save() == project, "library deletion never removes the embedded project style or changes project bytes");
    auto* list = dynamic_cast<juce::ListBox*> (panel->named ("style.list")); list->selectRow (0);
    check (! dynamic_cast<juce::Button*> (panel->named ("style.remove"))->isEnabled(), "embedded project copy is exportable but cannot be removed as a library file");
    const auto output = folder.root.getChildFile ("project-copy.voxstyle");
    check (panel->exportSelection (output).wasOk(), "native export writes the selected embedded project copy");
    style::Definition exported;
    check (style::parse (output.loadFileAsString(), exported).wasOk() && style::documentHash (exported) == originalHash,
           "exported project style is canonical and independent of removed library files");
    const auto blocked = folder.root.getChildFile ("blocked"); blocked.replaceWithText ("keep");
    check (panel->exportSelection (blocked.getChildFile ("out.voxstyle")).failed() && blocked.loadFileAsString() == "keep",
           "native export reports an unwritable destination without damaging its existing parent file");
    input.replaceWithText ("{\"not_a_style\":true}");
    check (panel->importFile (input).status == StyleLibrary::ImportResult::Status::refused && save() == project && service.list().empty(),
           "native import rejection leaves project and library unchanged");
    input.replaceWithText (style::serialize (definition));
    std::function<void (juce::File)> closedImport, closedExport;
    {
        auto closing = makeNativeLibraryDock (*p, [] { return 0; }, service.folder());
        auto* closingPanel = dynamic_cast<StyleLibraryPanel*> (closing->named ("styles"));
        closingPanel->fileDialogForTesting = [&] (bool isSave, const juce::File&, std::function<void (juce::File)> completed) {
            (isSave ? closedExport : closedImport) = std::move (completed);
        };
        auto* closingList = dynamic_cast<juce::ListBox*> (closingPanel->named ("style.list")); closingList->selectRow (0);
        press (*dynamic_cast<juce::Button*> (closingPanel->named ("style.import")));
        press (*dynamic_cast<juce::Button*> (closingPanel->named ("style.export")));
    }
    const auto closedOutput = folder.root.getChildFile ("closed-dialog.voxstyle");
    check (closedImport && closedExport, "both native file dialogs use the qualified completion path");
    if (closedImport) closedImport (input);
    if (closedExport) closedExport (closedOutput);
    check (service.list().empty() && ! closedOutput.exists() && save() == project,
           "file dialog completions after editor destruction cannot import, export, or mutate the project");
    input.replaceWithText (style::serialize (definition)); panel->importFile (input);
    service.importText (style::serialize (changed), StyleLibrary::Collision::replace);
    check (! panel->useSelected() && save() == project, "native Use refuses a stale library version instead of silently applying different content");
    const auto oldEpoch = p->patternEpoch(); p->setStateInformation (project.getData(), static_cast<int> (project.getSize()));
    check (! p->useUserStyle (service.find (definition.id), oldEpoch) && style::documentHash (*p->generator().custom) == originalHash,
           "style application with an old project epoch cannot overwrite the restored project definition");
    for (int width : { 196, 220, 376, 640 }) { library->setSize (width, library->dockHeight()); checkToolLayout (*library); }
    ui.pollForTesting(); panel->refresh();
    auto next = ui.layout(); next.rightTool = workspace::DockTool::library; ui.setLayout (next); ui.setSize (1040, 600); ui.pollForTesting();
    check (ui.layout().rightTool == workspace::DockTool::library, "native style capture opens the actual Library dock after restore convergence");
    if (const auto* destination = std::getenv ("VOX_WORKSPACE_SCREENSHOTS"))
    {
        const auto image = ui.createComponentSnapshot (ui.getLocalBounds()); juce::File directory (destination); directory.createDirectory();
        juce::FileOutputStream stream (directory.getChildFile ("native-user-style-library.png"));
        check (stream.setPosition (0) && stream.truncate().wasOk() && juce::PNGImageFormat().writeImageToStream (image, stream),
               "write actual native user-style Library capture");
    }
}
void testKeyboardViewportFocus()
{
    using namespace vox::drums;
    const bool display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr;
   #if JUCE_WINDOWS
    check (display, "Windows native keyboard qualification requires an actual display");
   #endif
    if (! display) { std::printf ("Native keyboard peer: no display, not qualified\n"); return; }
    auto p = std::make_unique<DrumsProcessor>();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    auto* frame = dynamic_cast<vox::suite::SuiteEditor*> (editor.get());
    auto* ui = frame ? dynamic_cast<DrumsWorkspace*> (frame->content()) : nullptr;
    check (ui != nullptr, "keyboard qualification uses the actual production editor");
    if (! ui) return;
    editor->setSize (1040, 700); editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
    editor->setVisible (true); editor->toFront (true); pump();
    check (editor->getPeer() && editor->getPeer()->getNativeHandle(), "production keyboard test attaches a real native peer");
    auto next = ui->layout(); next.rightTool = workspace::DockTool::routing; ui->setLayout (next); ui->pollForTesting(); pump();
    auto* routing = ui->toolPage (workspace::DockTool::routing);
    auto* first = routing->named ("lane.1"); auto* last = routing->named ("lane.16");
    const auto visibleThroughViewports = [] (juce::Component* control) {
        for (auto* ancestor = control->getParentComponent(); ancestor; ancestor = ancestor->getParentComponent())
            if (auto* view = dynamic_cast<juce::Viewport*> (ancestor))
                if (auto* content = view->getViewedComponent())
                    if (! view->getViewArea().contains (content->getLocalArea (control, control->getLocalBounds()))) return false;
        return true;
    };
    juce::MemoryBlock before; p->getStateInformation (before); const auto history = p->patternHistory().size();
    first->grabKeyboardFocus(); pump();
    check (first->hasKeyboardFocus (false), "native peer gives the first Routing lane actual keyboard focus");
    last->grabKeyboardFocus(); pump();
    check (last->hasKeyboardFocus (false) && visibleThroughViewports (last),
           "keyboard focus reveals an offscreen Routing control in its dock viewport");
    first->grabKeyboardFocus(); pump();
    check (visibleThroughViewports (first), "returning keyboard focus reveals the top of the dock again");
    bool reachedLast = false, allVisible = true;
    for (int n = 0; n < 100; ++n)
    {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (! focused || ! ui->isParentOf (focused)) break;
        allVisible = allVisible && visibleThroughViewports (focused);
        if (focused == last) { reachedLast = true; break; }
        focused->moveKeyboardFocusToSibling (true); pump();
    }
    check (reachedLast && allVisible, "programmatic focus traversal reaches all Routing rows with every focused control visible");
    bool reachedFirst = false;
    for (int n = 0; n < 100; ++n)
    {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (! focused || ! ui->isParentOf (focused)) break;
        allVisible = allVisible && visibleThroughViewports (focused);
        if (focused == first) { reachedFirst = true; break; }
        focused->moveKeyboardFocusToSibling (false); pump();
    }
    check (reachedFirst && allVisible, "backward programmatic traversal reveals the previous Routing rows without trapping focus");
    // Deliver Tab and Shift-Tab through the real peer's key dispatch. This
    // exercises JUCE's peer-to-component route; it does not inject OS keys.
    first->grabKeyboardFocus(); pump();
    auto* nativePeer = editor->getPeer();
    bool peerForward = false, peerBackward = false, peerVisible = true, peerConsumed = nativePeer != nullptr;
    const auto traversePeer = [&] (bool backwards, juce::Component* target) {
        for (int n = 0; n < 100 && peerConsumed; ++n)
        {
            auto* focused = juce::Component::getCurrentlyFocusedComponent();
            if (! focused || ! ui->isParentOf (focused)) break;
            peerVisible = peerVisible && visibleThroughViewports (focused);
            if (focused == target) return true;
            const auto modifiers = backwards ? juce::ModifierKeys (juce::ModifierKeys::shiftModifier)
                                             : juce::ModifierKeys();
            peerConsumed = nativePeer->handleKeyPress (juce::KeyPress (juce::KeyPress::tabKey, modifiers, 0));
            pump();
        }
        return false;
    };
    peerForward = traversePeer (false, last);
    peerBackward = peerForward && traversePeer (true, first);
    check (peerConsumed && peerForward && peerBackward && peerVisible,
           "Tab and Shift-Tab dispatched through the native peer reveal all Routing controls in both directions");
    auto* lastFader = ui->sliderFor ("drm.p16.level"); auto* firstFader = ui->sliderFor ("drm.p01.level");
    lastFader->grabKeyboardFocus(); pump();
    check (lastFader->hasKeyboardFocus (true) && visibleThroughViewports (juce::Component::getCurrentlyFocusedComponent()),
           "keyboard focus reveals pad16 in the single horizontal Mixer viewport");
    firstFader->grabKeyboardFocus(); pump();
    check (visibleThroughViewports (juce::Component::getCurrentlyFocusedComponent()), "Mixer keyboard focus returns to pad1 without a saved bank change");
    juce::MemoryBlock after; p->getStateInformation (after);
    check (after == before && p->patternHistory().size() == history, "keyboard reveal is presentation-only and leaves project/Undo exact");
    ui->setListView (true); pump(); p->getStateInformation (before);
    ui->padSelectButton (15).grabKeyboardFocus(); pump();
    check (ui->padSelectButton (15).hasKeyboardFocus (false) && visibleThroughViewports (&ui->padSelectButton (15)),
           "keyboard focus reveals the last Rack list pad through nested dock/Rack viewports");
    p->getStateInformation (after); check (after == before, "nested focus reveal never rewrites the saved Rack presentation");
    editor->removeFromDesktop(); p->editorBeingDeleted (editor.get()); editor.reset(); pump();
}
void testFullSoundFlow()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    PadControlSurface sound (*p, PadControlSurface::Group::sound); sound.setSize (376, 1400);
    auto* source = sound.structuralCombo ("source");
    check (source != nullptr, "Full Sound contains the real Synth/Sample/Layer source binding in its signal flow");
    if (! source) return;
    const auto find = [] (auto&& self, juce::Component& component, const juce::String& name) -> juce::Component* {
        if (component.getName() == name) return &component;
        for (auto* child : component.getChildren()) if (auto* found = self (self, *child, name)) return found;
        return nullptr;
    };
    for (int section = 0; section < 4; ++section)
        check (find (find, sound, "sound.section." + juce::String (section)) != nullptr,
               "Full Sound provides four real collapsible signal-flow sections");
    check (! sound.waveform().isVisible(), "Synth source does not display an unused sample waveform");
    source->setSelectedItemIndex (2, juce::sendNotificationSync); sound.update();
    check (p->kitStore().pad (0).source == kit::Source::layer && sound.waveform().isVisible(),
           "Layer uses the actual source binding and reveals sample shaping in Full Sound");
    press (*sound.sampleButton ("B"));
    check (sound.sampleButton ("B")->getToggleState() && ! sound.sampleButton ("A")->getToggleState(),
           "Full Sound A/B has one immediate and visible selected-player state");
    sound.structuralSlider ("start")->setValue (0.2, juce::sendNotificationSync);
    check (std::abs (p->kitStore().pad (0).players[1].start - 0.2f) < 0.001f
           && juce::exactlyEqual (p->kitStore().pad (0).players[0].start, 0.0f), "Full Sound trim addresses the selected B player, not A");
    const auto file = juce::File::createTempFile (".wav");
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (16));
        check (writer != nullptr, "create Full Sound source fixture");
        if (writer) { juce::AudioBuffer<float> audio (1, 64); audio.clear(); audio.setSample (0, 0, 0.5f); check (writer->writeFromAudioSampleBuffer (audio, 0, 64), "write Full Sound source fixture"); }
    }
    sound.loadSample (file); p->samples().waitForPendingDecodes(); pump(); sound.update();
    check (p->samples().pad (slotOf (0, 1)).buffer != nullptr, "Full Sound Load uses the actual asynchronous sample library");
    source->setSelectedItemIndex (1, juce::sendNotificationSync);
    press (*sound.sampleButton ("A")); sound.loadSample (file); p->samples().waitForPendingDecodes(); pump(); sound.update();
    check (! sound.structuralCombo ("model")->isVisible(), "usable A-only Sample source hides irrelevant synthesis controls");
    auto* mode = sound.structuralCombo ("sample_mode"); mode->setSelectedItemIndex (1, juce::sendNotificationSync);
    press (*sound.sampleButton ("B")); press (*sound.sampleButton ("clear")); sound.update();
    check (sound.structuralCombo ("model")->isVisible(), "velocity A/B exposes real synth fallback when one required player is missing");
    mode->setSelectedItemIndex (2, juce::sendNotificationSync); sound.update();
    check (! sound.structuralCombo ("model")->isVisible(), "A+B with a usable A remains sampled despite missing B");
    source->setSelectedItemIndex (2, juce::sendNotificationSync); sound.update();
    check (sound.structuralCombo ("model")->isVisible(), "Layer always exposes its real synthesis model");
    sound.structuralCombo ("output")->setSelectedItemIndex (8, juce::sendNotificationSync);
    check (p->kitStore().pad (0).output == 8, "Full Sound output selects the existing Out9 structural route");
    for (int width : { 320, 376, 720, 1100 }) { sound.setSize (width, sound.preferredHeight (width)); checkToolLayout (sound); }
    int changes = 0; sound.onSectionsChanged = [&] { ++changes; };
    auto* drive = sound.structuralSlider ("drive");
    if (drive->onDragStart) drive->onDragStart();
    check (! sound.setCollapsedSections (0xa2), "section collapse refuses to hide an active Sound gesture");
    if (drive->onDragEnd) drive->onDragEnd();
    check (sound.setCollapsedSections (0xa2) && changes == 1 && ! drive->getParentComponent()->isVisible(),
           "collapse hides the actual shaping section and retains reserved bits");
    sound.setSize (376, sound.preferredHeight (376)); checkToolLayout (sound);
    DrumsWorkspace ui (*p); ui.setSize (1040, 600);
    const auto full = [] (auto&& self, juce::Component& component) -> PadControlSurface* {
        if (auto* view = dynamic_cast<PadControlSurface*> (&component); view && view->structuralCombo ("model")) return view;
        for (auto* child : component.getChildren()) if (auto* found = self (self, *child)) return found;
        return nullptr;
    };
    auto* production = full (full, ui);
    check (production != nullptr, "production workspace contains the same Full Sound component");
    if (! production) return;
    press (*production->sampleButton ("B")); ui.pollForTesting();
    const auto sampleView = [] (auto&& self, juce::Component& component) -> PadControlSurface* {
        if (auto* view = dynamic_cast<PadControlSurface*> (&component); view && ! view->structuralCombo ("model")) return view;
        for (auto* child : component.getChildren()) if (auto* found = self (self, *child)) return found;
        return nullptr;
    };
    auto* sample = sampleView (sampleView, ui);
    check (sample && sample->sampleButton ("B")->getToggleState() && production->sampleButton ("B")->getToggleState()
           && std::abs (sample->structuralSlider ("start")->getValue() - 0.2) < 0.001,
           "Sound and Sample central views follow one actual player selection and canonical trim");
    const auto save = [&] { juce::MemoryBlock result; p->getStateInformation (result); return result; };
    const auto decode = [&] (const juce::MemoryBlock& data) { return vox::preset::decode ({ static_cast<const std::byte*> (data.getData()), data.getSize() }, p->product().stateTag, 1).document; };
    const auto before = decode (save()); const auto history = p->patternHistory().size();
    check (production->setCollapsedSections (0xa2), "production Sound commits a real collapsed presentation");
    const auto after = decode (save());
    for (const auto& section : before.sections)
        if (section.tag != layoutSectionTag) check (after.find (section.tag) && after.find (section.tag)->data == section.data, "Sound collapse never changes music/sample/UIST sections");
    check (p->patternHistory().size() == history && layout::find (p->layoutState().document, layout::Type::soundSections)->payload == layout::Bytes { 0xa2 },
           "Sound collapse commits only canonical DLAY, never musical Undo");
    const auto committed = save();
    auto q = std::make_unique<DrumsProcessor>(); q->setStateInformation (committed.getData(), static_cast<int> (committed.getSize()));
    DrumsWorkspace reopened (*q); reopened.setSize (1040, 600); reopened.pollForTesting();
    auto* reopenedSound = full (full, reopened);
    check (reopenedSound && reopenedSound->collapsedSections() == 0xa2, "new project/editor restores Sound sections exactly");
    auto futureDocument = decode (committed);
    const std::vector<std::byte> futureBytes { std::byte { 2 }, std::byte {}, std::byte {}, std::byte {}, std::byte { 66 } };
    for (auto& section : futureDocument.sections) if (section.tag == layoutSectionTag) section.data = futureBytes;
    const auto future = vox::preset::encode (futureDocument);
    check (future.has_value(), "Full Sound future-layout fixture fits VOXS");
    if (future && reopenedSound)
    {
        q->setStateInformation (future->data(), static_cast<int> (future->size())); reopened.pollForTesting();
        reopenedSound->setCollapsedSections (0x0f);
        juce::MemoryBlock saved; q->getStateInformation (saved);
        check (q->layoutState().status == layout::Status::future && decode (saved).find (layoutSectionTag)->data == futureBytes
               && reopenedSound->collapsedSections() == 0x0f, "future Sound layout edits stay session-only and preserve every opaque byte");
    }
    if (const auto* destination = std::getenv ("VOX_WORKSPACE_SCREENSHOTS"))
    {
        production->setCollapsedSections (0); auto next = ui.layout(); next.primary = workspace::Editor::sound; next.centralVisible = true; ui.setLayout (next); ui.pollForTesting();
        const auto image = ui.createComponentSnapshot (ui.getLocalBounds()); juce::File folder (destination); folder.createDirectory();
        juce::FileOutputStream stream (folder.getChildFile ("full-sound-flow.png"));
        check (stream.setPosition (0) && stream.truncate().wasOk() && juce::PNGImageFormat().writeImageToStream (image, stream), "write actual Full Sound snapshot");
    }
    file.deleteFile();
}
void testSourcePlayhead()
{
    using namespace vox::drums;
    class Playhead final : public juce::AudioPlayHead {
    public:
        juce::Optional<PositionInfo> getPosition() const override { return info; }
        PositionInfo info;
    } head;
    auto p = std::make_unique<DrumsProcessor>();
    auto b = std::make_unique<StoredPattern>(); b->bars = 2; p->patterns().setAlternate (2, *b);
    auto song = SongData::initial(); song.songMode = true;
    auto& ref = song.bars[5]; ref.kind = SongBar::Kind::pattern; ref.slot = 2; ref.variation = 1; ref.patternBar = 3;
    p->patterns().setSong (song); p->prepareToPlay (48000, 128); p->setPlayHead (&head);
    PatternEditor editor (*p, p->product().accent); editor.editSourcePattern (2, 1, 3);
    const auto play = [&] (double ppq) {
        head.info.setIsPlaying (true); head.info.setBpm (120); head.info.setPpqPosition (ppq);
        head.info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        block (*p); editor.update();
    };
    play (20.125);
    check (editor.playheadBar() == 1 && editor.playheadTick() == 120,
           "source playhead maps the real linked B occurrence and modulo bar to actual audio telemetry");
    play (0.125);
    check (editor.playheadTick() == -1, "unrelated playing Song occurrence is never painted on the source grid");
    song.songMode = false; p->patterns().setSong (song); editor.editSourcePattern (0, 0, 0); play (0.125);
    check (editor.playheadBar() == 0 && editor.playheadTick() == 120, "matching source A follows actual pattern-mode playback");
    editor.editSourcePattern (0, 1, 0); play (0.125);
    check (editor.playheadTick() == -1, "source B never borrows the audible A pattern playhead");
    p->setPlayHead (nullptr);
    juce::MemoryBlock saved; p->getStateInformation (saved);
    const auto before = p->patterns().encodeCurrent().data;
    p->patterns().beforeCommitForTesting = [&] {
        p->patterns().beforeCommitForTesting = {};
        p->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    };
    editor.clickCell (4, 0);
    check (p->patterns().encodeCurrent().data == before && p->patternHistory().size() == 0,
           "restore at a source-grid commit boundary prevents stale writes and old-project history");
    editor.editSourcePattern (0, 1, 0); editor.setEnabled (false); editor.clickCell (4, 0);
    check (! editor.snapshot() && p->patterns().encodeCurrent().data == before && p->patternHistory().size() == 0,
           "unavailable source view refuses late grid callbacks and new MIDI snapshots");
}
void testInspectorOpenSound()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    DrumsWorkspace ui (*p); ui.setSize (1040, 600); ui.selectPad (3); ui.pinInspector (true); ui.selectPad (7);
    auto* open = ui.inspectorButton ("openSound");
    check (open && open->isVisible(), "pad Inspector exposes Open Sound Editor");
    if (! open) return;
    checkToolLayout (*open->getParentComponent());
    const auto before = p->patterns().encodeCurrent().data;
    const auto history = p->patternHistory().size();
    press (*open);
    check (ui.layout().primary == workspace::Editor::sound && ui.selection().pad() == 3 && ui.selection().inspectorPad() == 3,
           "Open Sound uses the pinned Inspector target rather than an unrelated selected pad");
    check (p->patterns().encodeCurrent().data == before && p->patternHistory().size() == history,
           "Open Sound changes only UI context, never musical data/Undo");
    auto hidden = ui.layout(); hidden.centralVisible = false; ui.setLayout (hidden);
    check (layout::find (p->layoutState().document, layout::Type::central) == nullptr,
           "an explicit hidden-central layout commits the actual visibility");
    press (*open);
    juce::MemoryBlock saved; p->getStateInformation (saved);
    const auto decoded = vox::preset::decode ({ static_cast<const std::byte*> (saved.getData()), saved.getSize() }, p->product().stateTag, 1);
    check (ui.layout().centralVisible && decoded.ok() && decoded.document.find (layoutSectionTag),
           "Open Sound restores a hidden central editor persistently");
    auto q = std::make_unique<DrumsProcessor>(); q->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    DrumsWorkspace reopened (*q);
    check (reopened.layout().centralVisible && reopened.layout().primary == workspace::Editor::sound,
           "Open Sound survives project save and a new editor instance");
    p->prepareToPlay (48000, 128); block (*p);
    check (p->takeHitMask() == 0, "Open Sound never auditions");
}
void testParameterLanes()
{
    using namespace vox::drums;
    auto p = std::make_unique<DrumsProcessor>();
    auto fixture = std::make_unique<StoredPattern>();
    fixture->resolution = timing::Resolution::sixtyFourth;
    for (int cell = 0; cell < 3; ++cell) { fixture->cell (2, 0, cell).on = true; fixture->cell (2, 0, cell).velocity = 64; }
    p->patterns().setSlot (0, *fixture);
    DrumsWorkspace ui (*p); ui.setSize (1280, 700); ui.selectPad (2); ui.pollForTesting();
    auto& steps = ui.patternEditor();
    ui.setSize (1040, 600);
    juce::Viewport* centralViewport = nullptr;
    for (auto* parent = steps.getParentComponent(); parent; parent = parent->getParentComponent())
        if (auto* viewport = dynamic_cast<juce::Viewport*> (parent)) { centralViewport = viewport; break; }
    check (centralViewport && steps.getHeight() <= centralViewport->getViewHeight(),
           "at minimum workspace with compact Mixer, Step commands fit without outer editor scrolling");
    ui.setSize (1280, 700);
    auto* lanes = steps.button ("parameters");
    check (lanes && lanes->isVisible() && lanes->getWidth() >= 60,
           "native Step exposes the graphical Parameter Lanes presentation menu");
    check (steps.parameterLanes() == std::vector<int> { 0 }, "factory DLAY displays its real Velocity lane");
    const auto save = [&] { juce::MemoryBlock out; p->getStateInformation (out); return out; };
    const auto decode = [&] (const juce::MemoryBlock& b) {
        return vox::preset::decode ({ static_cast<const std::byte*> (b.getData()), b.getSize() }, p->product().stateTag, 1).document;
    };
    const auto beforeLayout = decode (save());
    const auto history = p->patternHistory().size();
    check (steps.setParameterLanes ({ 1, 3 }) && steps.parameterLanes() == std::vector<int> { 1, 3 }, "choose two graphical lanes");
    const auto afterLayout = decode (save());
    for (const auto& part : beforeLayout.sections)
        if (part.tag != layoutSectionTag)
            check (afterLayout.find (part.tag) && afterLayout.find (part.tag)->data == part.data, "lane presentation never changes music/other sections/UIST");
    check (p->patternHistory().size() == history && layout::find (p->layoutState().document, layout::Type::paramLanes)->payload == layout::Bytes { 2, 1, 3 },
           "Parameter Lanes commits only canonical DLAY, never musical Undo");
    const auto validLayout = save();
    check (! steps.setParameterLanes ({ 4 }) && ! steps.setParameterLanes ({ 0, 1, 2 }) && save() == validLayout,
           "invalid lane kinds/count refuse without changing presentation or project bytes");
    for (const int width : { 376, 900, 1280 })
    {
        steps.setSize (width, 400);
        for (int step = 0; step < 64; ++step)
        {
            const auto cell = steps.cellBounds (2, step);
            for (unsigned row = 0; row < 2; ++row)
            {
                const auto parameter = steps.parameterCellBounds (row, step);
                check (! parameter.isEmpty() && parameter.getX() == cell.getX() && parameter.getWidth() == cell.getWidth(),
                       "all 64 lane cells share exact grid x/width through responsive scrolling");
            }
        }
    }
    check (steps.setParameterLanes ({ 0 }), "Velocity returns as the sole lane");
    steps.setSize (376, 320);
    check (steps.button ("lanes")->isVisible(), "short timeline exposes the pad-row page switch");
    const auto selectedPad = steps.selectedLane();
    press (*steps.button ("lanes"));
    check (steps.selectedLane() == selectedPad && ! steps.parameterCellBounds (0, 0).isEmpty(),
           "viewing the other pad-row page never blanks or redirects the selected pad's parameter lane");
    press (*steps.button ("lanes"));
    const auto findGrid = [] (auto&& self, juce::Component& parent) -> juce::Component* {
        if (parent.getName() == "pattern grid") return &parent;
        for (auto* child : parent.getChildren()) if (auto* found = self (self, *child)) return found;
        return nullptr;
    };
    auto* grid = findGrid (findGrid, steps);
    check (grid != nullptr, "graphical lanes use the actual timeline canvas");
    if (! grid) return;
    const auto point = [&] (unsigned row, int step, bool top) {
        const auto area = steps.parameterCellBounds (row, step);
        return grid->getLocalPoint (&steps, juce::Point<int> { area.getCentreX(), top ? area.getY() : area.getBottom() - 1 }).toFloat();
    };
    const auto event = [grid] (juce::Point<float> at, juce::Point<float> origin, bool down) {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
            juce::ModifierKeys (down ? juce::ModifierKeys::leftButtonModifier : 0), 1.0f, 0, 0, 0, 0,
            grid, grid, juce::Time::getCurrentTime(), origin, juce::Time::getCurrentTime(), 1, true);
    };
    const auto drag = [&] (unsigned row, int from, int to) {
        const auto origin = point (row, from, false), end = point (row, to, true);
        grid->mouseDown (event (origin, origin, true)); grid->mouseDrag (event (end, origin, true)); grid->mouseUp (event (end, origin, false));
    };
    p->patternHistory().clear();
    auto before = std::make_unique<StoredPattern>(); p->patterns().copySlot (0, *before);
    drag (0, 0, 1);
    auto result = std::make_unique<StoredPattern>(); p->patterns().copySlot (0, *result);
    auto expected = std::make_unique<StoredPattern> (*before);
    expected->cell (2, 0, 0).velocity = 1; expected->cell (2, 0, 1).velocity = 127;
    check (*result == *expected && p->patternHistory().size() == 1, "real lane drag edits exactly touched cells in one Undo entry");
    p->patternHistory().undo (p->patterns()); p->patterns().copySlot (0, *result);
    check (*result == *before, "graphical lane Undo restores complete musical bytes");
    p->patternHistory().redo (p->patterns()); p->patterns().copySlot (0, *result);
    check (*result == *expected, "graphical lane Redo restores complete musical bytes");
    steps.setParameterLanes ({ 1, 2 }); drag (0, 2, 2); drag (1, 2, 2);
    p->patterns().copySlot (0, *result);
    check (result->cell (2, 0, 2).probability == 100 && result->cell (2, 0, 2).ratchet == 8, "probability and ratchet graphics use canonical ranges");
    steps.setParameterLanes ({ 3 });
    const auto origin = point (0, 2, false);
    grid->mouseDown (event (origin, origin, true)); grid->mouseUp (event (origin, origin, false));
    p->patterns().copySlot (0, *result);
    check (result->cell (2, 0, 2).microTicks == kit::percentToTicks (-50, result->timing (2, 0).cellTicks (0)),
           "bipolar micro lane uses canonical percent-to-ticks quantisation");
    // Header/ruler clicks are presentation, never a hidden musical edit.
    const auto beforeHeader = save();
    const auto header = point (0, 0, true).translated (0, -8);
    grid->mouseDown (event (header, header, true)); grid->mouseUp (event (header, header, false));
    check (save() == beforeHeader, "lane header never edits musical values");
    grid->mouseDown (event (point (0, 0, true), origin, true));
    p->patterns().copySlot (0, *result); result->cell (7, 0, 0).on = true; p->patterns().setSlot (0, *result);
    const auto external = p->patterns().encodeCurrent().data;
    grid->mouseDrag (event (point (0, 1, false), origin, true)); grid->mouseUp (event (origin, origin, false));
    check (p->patterns().encodeCurrent().data == external, "an external canonical edit cancels the lane gesture instead of absorbing it into Undo");
    const auto restored = save();
    grid->mouseDown (event (point (0, 0, true), point (0, 0, true), true));
    p->setStateInformation (restored.getData(), static_cast<int> (restored.getSize())); ui.pollForTesting();
    const auto unchanged = save();
    grid->mouseDrag (event (point (0, 1, true), origin, true)); grid->mouseUp (event (origin, origin, false));
    check (save() == unchanged && p->patternHistory().size() == 0, "project restore cancels a stale lane gesture without editing the new project");
    // The same editor path keeps Song unlink-on-first-edit and source isolation.
    p->patterns().copySlot (0, *result); result->cell (2, 0, 0).velocity = 127; p->patterns().setSlot (0, *result);
    const auto source = p->patterns().encodeCurrent().data;
    const auto songBefore = p->patterns().song();
    ui.songEditor().select (0, 1);
    ui.songEditor().button ("edit")->setToggleState (true, juce::dontSendNotification); press (*ui.songEditor().button ("edit"));
    check (steps.songBar() == 0, "Song target is active immediately after Edit This Bar");
    steps.selectCell (2, 0); steps.setParameterLanes ({ 0 });
    check (steps.songBar() == 0, "layout-only lane selection preserves the explicit Song edit target");
    p->patternHistory().clear();
    const auto noop = point (0, 0, true);
    grid->mouseDown (event (noop, noop, true)); grid->mouseUp (event (noop, noop, false));
    check (steps.songBar() == 0, "no-op lane gesture preserves the explicit Song edit target");
    check (p->patterns().song() == songBefore && p->patternHistory().size() == 0, "unchanged lane value never unlinks a Song bar or creates Undo");
    const auto changed = point (0, 0, false);
    grid->mouseDown (event (changed, changed, true)); grid->mouseUp (event (changed, changed, false));
    const auto songAfter = p->patterns().song();
    check (songAfter.bars[0].kind == SongBar::Kind::unique && p->patterns().encodeCurrent().data == source
           && p->patternHistory().size() == 1, "first real lane edit makes only this Song bar unique, never modifies the source");
    p->patternHistory().undo (p->patterns());
    check (p->patterns().song() == songBefore, "lane Undo restores Song linkage and all Song bytes");
    p->patternHistory().redo (p->patterns());
    check (p->patterns().song() == songAfter, "lane Redo restores unique Song content exactly");
    auto q = std::make_unique<DrumsProcessor>(); q->setStateInformation (restored.getData(), static_cast<int> (restored.getSize()));
    DrumsWorkspace reopened (*q);
    check (reopened.patternEditor().parameterLanes() == std::vector<int> { 3 }, "closed-editor DLAY lane presentation restores into the native canvas");
    auto futureDocument = decode (restored);
    const std::vector<std::byte> futureLayout { std::byte { 2 }, std::byte {}, std::byte {}, std::byte {}, std::byte { 77 } };
    for (auto& part : futureDocument.sections) if (part.tag == layoutSectionTag) part.data = futureLayout;
    const auto future = vox::preset::encode (futureDocument);
    check (future.has_value(), "future layout UI fixture fits VOXS");
    if (future)
    {
        q->setStateInformation (future->data(), static_cast<int> (future->size())); reopened.pollForTesting();
        reopened.patternEditor().setParameterLanes ({ 0, 1 });
        juce::MemoryBlock futureSaved; q->getStateInformation (futureSaved);
        check (q->layoutState().status == layout::Status::future && decode (futureSaved).find (layoutSectionTag)->data == futureLayout
               && reopened.patternEditor().parameterLanes() == std::vector<int> { 0, 1 },
               "native lane presentation edits stay session-only and retain future layout bytes exactly");
    }
}
} // namespace
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    testSurfaces();
    pump();
    testRolesUI();
    pump();
    test();
    pump();
    testLayoutLifecycle();
    pump();
    testProductionBindings();
    pump();
    testNativeDocking();
    pump();
    testResponsiveCentral();
    pump();
    testMixerAndFocus();
    pump();
    testNativeMidiScopes();
    testParameterLanes();
    testInspectorOpenSound();
    testStepContextModel();
    testContextStepInspector();
    testSongContextInspector();
    testSourcePlayhead();
    testFullSoundFlow();
    testNativeStyleLibrary();
    testKeyboardViewportFocus();
    std::printf ("Native workspace: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
