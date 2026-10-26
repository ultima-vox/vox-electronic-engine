#include "PluginEditor.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
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

    // UI integration gate: Acid owns SOUND only when its Rack slot is
    // selected. All other pages and other instruments are unchanged.
    editor->selectSlotForTesting(1);
    editor->showPageForTesting(Page::sound);
    if (!editor->acidWorkspaceVisibleForTesting()
        || editor->acidControlsForTesting() < 25)
        return EXIT_FAILURE;
    editor->showPageForTesting(Page::pattern);
    if (editor->acidWorkspaceVisibleForTesting())
        return EXIT_FAILURE;
    editor->selectSlotForTesting(0);
    editor->showPageForTesting(Page::sound);
    if (editor->acidWorkspaceVisibleForTesting())
        return EXIT_FAILURE;
    editor->selectSlotForTesting(1);
    editor->showPageForTesting(Page::sound);

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
    // Retain the established Psy Bass screenshot reference: UI snapshots
    // must not silently switch their underlying instrument to Acid.
    editor->selectSlotForTesting(0);
    editor->showPageForTesting(Page::sound);
    const auto snapshotPath = juce::SystemStats::getEnvironmentVariable(
        "VOX_UI_SNAPSHOT", {});
    if (snapshotPath.isNotEmpty()) {
        editor->setSize(1240, 800);
        editor->showPageForTesting(Page::sound);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        juce::FileOutputStream output { juce::File(snapshotPath) };
        juce::PNGImageFormat format;
        if (!output.openedOk())
            return EXIT_FAILURE;
        output.setPosition(0);
        output.truncate();
        if (!format.writeImageToStream(image, output))
            return EXIT_FAILURE;
    }
    const auto acidPath = juce::SystemStats::getEnvironmentVariable(
        "VOX_UI_ACID_SNAPSHOT", {});
    if (acidPath.isNotEmpty()) {
        editor->selectSlotForTesting(1);
        editor->showPageForTesting(Page::sound);
        editor->setSize(1040, 768);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        juce::FileOutputStream output { juce::File(acidPath) };
        juce::PNGImageFormat format;
        if (!output.openedOk()) return EXIT_FAILURE;
        output.setPosition(0);
        output.truncate();
        if (!format.writeImageToStream(image, output)) return EXIT_FAILURE;
    }
    std::cout << "Editor smoke test passed\n";
    return EXIT_SUCCESS;
}
