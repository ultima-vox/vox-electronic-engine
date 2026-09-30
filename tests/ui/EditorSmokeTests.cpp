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

bool captureVisualGateSnapshot(const juce::File& directory,
                               const juce::String& fileName,
                               std::string_view instrumentId,
                               Page page,
                               juce::Point<int> size)
{
    std::cout << "Capturing " << fileName << "\n";

    VstEngineAudioProcessor processor;
    juce::String diagnostic;
    if (!processor.loadSlotInstrument(0, instrumentId, diagnostic)) {
        std::cerr << "Unable to load visual-gate instrument " << instrumentId
                  << ": " << diagnostic << "\n";
        return false;
    }
    processor.selectSlot(0);

    std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
    auto* editor = dynamic_cast<VstEngineAudioProcessorEditor*>(base.get());
    if (editor == nullptr)
        return false;

    editor->setSize(size.x, size.y);
    editor->showPageForTesting(page);
    const auto ok = writeSnapshot(*editor, directory.getChildFile(fileName));
    base.reset();

    if (ok)
        std::cout << "Captured " << fileName << "\n";
    return ok;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    VstEngineAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
    auto* editor = dynamic_cast<VstEngineAudioProcessorEditor*>(base.get());
    if (editor == nullptr)
        return EXIT_FAILURE;

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

    const auto snapshotDirectory = juce::SystemStats::getEnvironmentVariable(
        "VOX_UI_SNAPSHOT_DIR", {});
    if (snapshotDirectory.isNotEmpty()) {
        const juce::File directory(snapshotDirectory);
        if (!directory.exists() && !directory.createDirectory())
            return EXIT_FAILURE;

        // Release the smoke-test editor before creating capture editors. Keeping
        // multiple full plugin editors alive in this headless test process caused
        // a JUCE GUI teardown crash on Windows self-hosted CI.
        base.reset();
        editor = nullptr;

        struct Capture {
            const char* fileName;
            std::string_view instrumentId;
            Page page;
            juce::Point<int> size;
        };

        constexpr std::string_view psyBassId = "com.ultimavox.psy-bass";
        constexpr std::string_view acidId = "com.ultimavox.acid";
        const Capture captures[] {
            { "psy-bass-sound-1180x760.png", psyBassId, Page::sound, { 1180, 760 } },
            { "psy-bass-sound-1500x920.png", psyBassId, Page::sound, { 1500, 920 } },
            { "acid-sound-1500x920.png", acidId, Page::sound, { 1500, 920 } },
            { "psy-bass-macros-1500x920.png", psyBassId, Page::macros, { 1500, 920 } }
        };

        for (const auto& capture : captures)
            if (!captureVisualGateSnapshot(directory, capture.fileName,
                                           capture.instrumentId, capture.page,
                                           capture.size))
                return EXIT_FAILURE;
    }

    std::cout << "Editor smoke test passed\n";
    return EXIT_SUCCESS;
}
