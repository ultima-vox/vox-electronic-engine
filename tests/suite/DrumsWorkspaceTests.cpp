#include "DrumsWorkspace.h"
#include "DrumsDockZone.h"
#include "PatternEditor.h"
#include "DrumsEditor.h"
#include "PadControlSurface.h"
#include "DrumsRolesPanel.h"
#include "StyleRegistry.h"
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
    std::printf ("Native workspace: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
