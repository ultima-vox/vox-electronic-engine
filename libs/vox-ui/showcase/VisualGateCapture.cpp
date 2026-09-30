#include "PluginEditor.h"
#include <JuceHeader.h>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
using Page = vstengine::ui::MainNavigation::Page;

Page parsePage(const juce::String& value)
{
    if (value.equalsIgnoreCase("macros")) return Page::macros;
    if (value.equalsIgnoreCase("pattern")) return Page::pattern;
    if (value.equalsIgnoreCase("routing")) return Page::routing;
    if (value.equalsIgnoreCase("zones")) return Page::zones;
    if (value.equalsIgnoreCase("advanced")) return Page::advanced;
    return Page::sound;
}

bool writeSnapshot(VstEngineAudioProcessorEditor& editor, const juce::File& target)
{
    if (auto parent = target.getParentDirectory(); !parent.exists() && !parent.createDirectory())
        return false;

    const auto image = editor.createComponentSnapshot(editor.getLocalBounds());
    juce::FileOutputStream output { target };
    if (!output.openedOk())
        return false;

    output.setPosition(0);
    output.truncate();
    juce::PNGImageFormat format;
    return format.writeImageToStream(image, output);
}
}

int main(int argc, char** argv)
{
    if (argc != 6) {
        std::cerr << "Usage: vox_ui_visual_gate_capture <output.png> <instrument-id> <page> <width> <height>\n";
        return EXIT_FAILURE;
    }

    const juce::String outputPath = argv[1];
    const std::string instrumentId = argv[2];
    const juce::String pageName = argv[3];
    const int width = juce::jmax(1040, juce::String(argv[4]).getIntValue());
    const int height = juce::jmax(680, juce::String(argv[5]).getIntValue());

    juce::ScopedJuceInitialiser_GUI gui;
    VstEngineAudioProcessor processor;

    juce::String diagnostic;
    if (!processor.loadSlotInstrument(0, instrumentId, diagnostic)) {
        std::cerr << "Unable to load instrument " << instrumentId << ": " << diagnostic << "\n";
        return EXIT_FAILURE;
    }
    processor.selectSlot(0);

    std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
    auto* editor = dynamic_cast<VstEngineAudioProcessorEditor*>(base.get());
    if (editor == nullptr)
        return EXIT_FAILURE;

    editor->setSize(width, height);
    editor->showPageForTesting(parsePage(pageName));

    const bool ok = writeSnapshot(*editor, juce::File(outputPath));
    base.reset();

    if (!ok)
        return EXIT_FAILURE;

    std::cout << "Captured " << outputPath << "\n";
    return EXIT_SUCCESS;
}
