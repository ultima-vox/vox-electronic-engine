#include "PluginEditor.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace {
using Page = vstengine::ui::MainNavigation::Page;

bool writeSnapshot(VstEngineAudioProcessorEditor& editor, const juce::File& target)
{
    if (auto parent = target.getParentDirectory(); !parent.exists() && !parent.createDirectory())
        return false;

    const auto image = editor.createComponentSnapshot(editor.getLocalBounds());
    juce::FileOutputStream output { target };
    juce::PNGImageFormat format;
    if (!output.openedOk())
        return false;
    output.setPosition(0);
    output.truncate();
    return format.writeImageToStream(image, output);
}

Page pageFromEnvironment(const juce::String& value)
{
    if (value.equalsIgnoreCase("macros"))
        return Page::macros;
    if (value.equalsIgnoreCase("pattern"))
        return Page::pattern;
    if (value.equalsIgnoreCase("routing"))
        return Page::routing;
    if (value.equalsIgnoreCase("zones"))
        return Page::zones;
    if (value.equalsIgnoreCase("advanced"))
        return Page::advanced;
    return Page::sound;
}

int runSingleSnapshotCapture()
{
    const auto targetPath = juce::SystemStats::getEnvironmentVariable("VOX_UI_CAPTURE_PATH", {});
    if (targetPath.isEmpty())
        return -1;

    const auto instrumentId = juce::SystemStats::getEnvironmentVariable(
        "VOX_UI_CAPTURE_INSTRUMENT", "com.ultimavox.psy-bass");
    const auto pageName = juce::SystemStats::getEnvironmentVariable("VOX_UI_CAPTURE_PAGE", "sound");
    const auto width = juce::jmax(1040, juce::SystemStats::getEnvironmentVariable(
        "VOX_UI_CAPTURE_WIDTH", "1180").getIntValue());
    const auto height = juce::jmax(680, juce::SystemStats::getEnvironmentVariable(
        "VOX_UI_CAPTURE_HEIGHT", "760").getIntValue());

    std::cout << "Visual Gate capture: " << targetPath
              << " instrument=" << instrumentId
              << " page=" << pageName
              << " size=" << width << "x" << height << std::endl;

    VstEngineAudioProcessor processor;
    juce::String diagnostic;
    if (!processor.loadSlotInstrument(0, instrumentId.toStdString(), diagnostic)) {
        std::cerr << "Unable to load visual-gate instrument " << instrumentId
                  << ": " << diagnostic << std::endl;
        return EXIT_FAILURE;
    }
    processor.selectSlot(0);

    std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
    auto* editor = dynamic_cast<VstEngineAudioProcessorEditor*>(base.get());
    if (editor == nullptr)
        return EXIT_FAILURE;

    editor->setSize(width, height);
    editor->showPageForTesting(pageFromEnvironment(pageName));
    const auto ok = writeSnapshot(*editor, juce::File(targetPath));
    base.reset();

    if (!ok)
        return EXIT_FAILURE;

    std::cout << "Visual Gate capture complete: " << targetPath << std::endl;
    return EXIT_SUCCESS;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    // Visual Gate captures run as one editor per process. JUCE GUI teardown on
    // Windows self-hosted CI was unstable when multiple full plugin editors were
    // created and destroyed sequentially inside the normal CTest smoke process.
    if (const auto captureResult = runSingleSnapshotCapture(); captureResult >= 0)
        return captureResult;

    VstEngineAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
    auto* editor = dynamic_cast<VstEngineAudioProcessorEditor*>(base.get());
    if (editor == nullptr)
        return EXIT_FAILURE;

    using Page = vstengine::ui::MainNavigation::Page;
    for (const auto page : { Page::sound, Page::pattern, Page::routing,
                             Page::zones, Page::macros, Page::advanced }) {
        editor->showPageForTesting(page);
        if (editor->currentPageForTesting() != page)
            return EXIT_FAILURE;
    }

    for (const auto size : { juce::Point<int> { 1040, 680 },
                             juce::Point<int> { 1180, 760 },
                             juce::Point<int> { 1500, 920 } }) {
        editor->setSize(size.x, size.y);
        const auto coveredWidth = editor->keyboardKeyWidthForTesting(Page::rack)
                                  * 43.0f;
        const auto componentWidth = static_cast<float>(
            editor->keyboardComponentWidthForTesting(Page::rack));
        if (std::abs(coveredWidth - componentWidth) > 0.5f)
            return EXIT_FAILURE;
        const auto rack = editor->rackBoundsForTesting();
        const auto workspace = editor->workspaceBoundsForTesting();
        const auto keyboard = editor->keyboardBoundsForTesting();
        if (rack.isEmpty() || workspace.isEmpty() || keyboard.isEmpty()
            || rack.intersects(workspace) || rack.intersects(keyboard)
            || workspace.intersects(keyboard))
            return EXIT_FAILURE;
    }

    const auto snapshotPath = juce::SystemStats::getEnvironmentVariable(
        "VOX_UI_SNAPSHOT", {});
    if (snapshotPath.isNotEmpty()) {
        editor->setSize(1240, 800);
        editor->showPageForTesting(Page::sound);
        if (!writeSnapshot(*editor, juce::File(snapshotPath)))
            return EXIT_FAILURE;
    }

    std::cout << "Editor smoke test passed\n";
    return EXIT_SUCCESS;
}
