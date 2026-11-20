#include "DrumsWorkspace.h"
#include "Processor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

void test()
{
    auto p = std::make_unique<vox::drums::DrumsProcessor>();
    p->prepareToPlay (48000, 128);
    auto ui = std::make_unique<vox::drums::DrumsWorkspace> (*p);
    ui->setSize (1180, 660);
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
        for (bool list : { false, true })
            for (float scale : { 1.0f, 1.25f, 1.5f, 2.0f })
            {
                ui->setSize (size.first, size.second); ui->setListView (list);
                for (int pad = 0; pad < 16; ++pad)
                {
                    check (ui->padSelectButton (pad).isVisible() && ui->padHitButton (pad).isVisible(), "single Rack exposes select and audition together");
                    check (ui->padSelectButton (pad).getWidth() > 0 && ui->padHitButton (pad).getHeight() >= 24,
                           "all 16 select/audition targets remain accessible");
                    check (! ui->padSelectButton (pad).getBounds().intersects (ui->padHitButton (pad).getBounds()),
                           "selection and audition never share a hit target");
                }
                juce::Image image (juce::Image::ARGB, juce::roundToInt (size.first * scale), juce::roundToInt (size.second * scale), true);
                { juce::Graphics g (image); g.addTransform (juce::AffineTransform::scale (scale)); ui->paintEntireComponent (g, false); }
                if (shots != nullptr && juce::exactlyEqual (scale, 1.0f))
                {
                    juce::File folder { juce::String (shots) }; folder.createDirectory();
                    juce::FileOutputStream stream (folder.getChildFile (juce::String (size.first) + (list ? "-list.png" : "-grid.png")));
                    check (stream.setPosition (0) && stream.truncate().wasOk(), "replace previous native snapshot");
                    check (juce::PNGImageFormat().writeImageToStream (image, stream), "write native workspace snapshot");
                }
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
    juce::MemoryBlock stateAfterLayout;
    p->getStateInformation (stateAfterLayout);
    check (stateBeforeLayout == stateAfterLayout, "workspace layout and rendering never mutate musical/project state");
    p->releaseResources();
}
} // namespace
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    test();
    std::printf ("Native workspace: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
