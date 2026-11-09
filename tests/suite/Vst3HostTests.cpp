// Mini-host test (DRM-01): loads each built .vst3 bundle the way a DAW does,
// through JUCE's VST3 host (the Steinberg SDK hosting code), not through the
// plugin's C++ classes:
//   - scan: exactly one instrument class with the expected name and VST3 UID;
//   - instance: no audio input, stereo output, MIDI input;
//   - audio: with sound engine, a MIDI note sounds and the host-visible
//     level parameter silences it; without, output is silent;
//   - state: IComponent state saved by one instance loads into a second one
//     and saves back byte-identically; a changed parameter survives it;
//   - editor: opens and closes (only where a display exists).
//
// Usage: suite_vst3_host_tests <bundle>|<expected name>|<has engine 0/1>|<level parameter name> ...
// This proves loading and the VST3 contract in a real host implementation;
// it does not replace the Cubase / REAPER acceptance (DRM-08).
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

int failures = 0;

void check (bool ok, const juce::String& what)
{
    if (! ok)
    {
        std::printf ("FAIL: %s\n", what.toRawUTF8());
        ++failures;
    }
}

float renderPeak (juce::AudioPluginInstance& plugin, int blocks, int block, bool noteFirst)
{
    juce::AudioBuffer<float> buffer (juce::jmax (2, plugin.getTotalNumOutputChannels()), block);
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (noteFirst && b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (110)), 3);
        buffer.clear();
        plugin.processBlock (buffer, midi);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            for (int i = 0; i < block; ++i)
                if (! std::isfinite (buffer.getSample (ch, i)))
                    return -1.0f;
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, block));
        }
    }
    return peak;
}

juce::AudioProcessorParameter* parameterNamed (juce::AudioPluginInstance& plugin, const juce::String& name)
{
    for (auto* p : plugin.getParameters())
        if (p->getName (128) == name)
            return p;
    return nullptr;
}

void testBundle (juce::VST3PluginFormat& format, const juce::String& bundle, const juce::String& name,
                 bool hasEngine, const juce::String& levelName)
{
    const auto label = "[" + name + "] ";
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, bundle);
    check (found.size() == 1, label + "one plugin class in " + bundle + " (found " + juce::String (found.size()) + ")");
    if (found.isEmpty())
        return;
    const auto& description = *found[0];
    check (description.name == name, label + "name '" + description.name + "'");
    check (description.manufacturerName == VOX_TEST_COMPANY, label + "manufacturer '" + description.manufacturerName + "'");
    check (description.isInstrument, label + "is an instrument");

    juce::String error;
    std::unique_ptr<juce::AudioPluginInstance> plugin (format.createInstanceFromDescription (description, 48000.0, 512, error));
    check (plugin != nullptr, label + "instantiates: " + error);
    if (plugin == nullptr)
        return;
    check (plugin->getTotalNumInputChannels() == 0, label + "no audio input");
    check (plugin->getTotalNumOutputChannels() == 2, label + "stereo output");
    check (plugin->acceptsMidi(), label + "accepts MIDI");

    plugin->setRateAndBufferSizeDetails (48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);
    const float peak = renderPeak (*plugin, 20, 512, true);
    check (peak >= 0.0f, label + "finite output");
    if (hasEngine)
    {
        check (peak > 1.0e-3f, label + "a MIDI note sounds (peak " + juce::String (peak) + ")");
        auto* level = parameterNamed (*plugin, levelName);
        check (level != nullptr, label + "host sees parameter '" + levelName + "'");
        if (level != nullptr)
        {
            level->setValueNotifyingHost (0.0f);
            renderPeak (*plugin, 400, 512, false);   // let the previous hit ring out (4 s)
            check (renderPeak (*plugin, 20, 512, true) < 1.0e-6f, label + "level 0 from the host silences the pad");
        }
    }
    else
    {
        check (peak <= 0.0f, label + "shell output is silent");
    }

    // State through the VST3 IComponent/IEditController state calls.
    juce::MemoryBlock saved;
    plugin->getStateInformation (saved);
    check (saved.getSize() > 0, label + "state saves");
    std::unique_ptr<juce::AudioPluginInstance> second (format.createInstanceFromDescription (description, 48000.0, 512, error));
    check (second != nullptr, label + "second instance: " + error);
    if (second != nullptr)
    {
        second->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        juce::MemoryBlock again;
        second->getStateInformation (again);
        check (again == saved, label + "state loads into a new instance and saves back identically");
        if (hasEngine)
        {
            auto* level = parameterNamed (*second, levelName);
            check (level != nullptr && level->getValue() < 1.0e-6f, label + "the host-changed parameter survives the state");
        }
    }

    const bool display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr;
    if (display && plugin->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorAndMakeActive());
        check (editor != nullptr, label + "editor opens");
        if (editor != nullptr)
        {
            check (editor->getWidth() >= 1040 && editor->getHeight() >= 700, label + "editor size");
            plugin->editorBeingDeleted (editor.get());
            editor.reset();
        }
    }
    else
    {
        std::printf ("%sno display: editor not opened through the host\n", label.toRawUTF8());
    }
    plugin->releaseResources();
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::VST3PluginFormat format;
    for (int i = 1; i < argc; ++i)
    {
        const auto parts = juce::StringArray::fromTokens (juce::String (argv[i]), "|", "");
        if (parts.size() != 4)
        {
            std::printf ("bad argument: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
        const auto bundle = juce::File (parts[0]).getFullPathName();
        testBundle (format, bundle, parts[1], parts[2] == "1", parts[3]);
    }
    if (failures == 0)
        std::printf ("VST3 host tests passed (%d bundles)\n", argc - 1);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
