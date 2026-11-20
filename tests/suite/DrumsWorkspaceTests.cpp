#include "DrumsWorkspace.h"
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
    check (stateBeforeLayout == stateAfterLayout, "workspace layout and rendering never mutate musical/project state");
    p->releaseResources();
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
    std::printf ("Native workspace: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
