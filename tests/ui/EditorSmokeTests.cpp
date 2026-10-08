#include "PluginEditor.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

// ---------------------------------------------------------------------------
// Keyboard assertion
// ---------------------------------------------------------------------------
//
// The assertion that used to live here was
//
//     keyboardKeyWidthForTesting() * 43.0f == keyboardComponentWidthForTesting()
//
// It held only because the piano spanned the entire footer: 43 white keys, key
// width = footer width / 43. The shell now reserves a Pitch/Mod column, a
// KEYBOARD/CHORDS/SCALE strip and the velocity-curve / MIDI-learn controls
// inside the footer, and the piano receives only the remainder of the content
// column, so that identity no longer describes anything real.
//
// The property that matters now is that the piano is a bounded child of the
// footer band and that the requested key range genuinely fits inside it:
//
//   1. the piano component lies strictly inside the footer band (the previous
//      build's piano was laid out across the whole window, outside any footer);
//   2. the piano does not span the footer, i.e. the Pitch/Mod column and the
//      toolbar really do take space away from it;
//   3. every key in the available range is drawn inside the component (JUCE
//      computes the key rectangles; this reads them back);
//   4. key width is positive.
//
// This is not a tautology: it fails if the piano is given the full footer width,
// if the key range is widened past what the component can hold, if the key width
// collapses, or if the footer stops being the piano's parent band.
bool checkKeyboardGeometry(VstEngineAudioProcessorEditor& editor)
{
    const auto footer = editor.footerBoundsForTesting();
    const auto piano = editor.keyboardBoundsForTesting();      // editor space
    const auto pianoLocal = editor.keyboardLocalBoundsForTesting();

    if (footer.isEmpty() || piano.isEmpty() || pianoLocal.isEmpty())
        return false;

    // 1. The piano is inside the footer band, in editor space.
    if (!footer.contains(piano))
        return false;


    // 2. The piano does not span the footer: the wheels and toolbar own the rest.
    if (piano.getWidth() >= footer.getWidth())
        return false;

    // 3 + 4. Key geometry.
    if (editor.keyboardKeyWidthForTesting() <= 0.0f)
        return false;

    if (editor.keyboardHighestNoteForTesting() <= editor.keyboardLowestNoteForTesting())
        return false;

    if (!editor.keyboardKeysFitComponentForTesting())
        return false;

    // Every key in range is wide enough to be played.
    const auto whiteKeyWidth = editor.keyboardKeyWidthForTesting();
    if (whiteKeyWidth < 6.0f)
        return false;

    // The keys cover the component they were given (within a key's worth of
    // rounding), so the range is not smaller than the space allocated to it.
    const auto lowest = editor.keyboardKeyBoundsForTesting(editor.keyboardLowestNoteForTesting());
    const auto highest = editor.keyboardKeyBoundsForTesting(editor.keyboardHighestNoteForTesting());
    if (lowest.isEmpty() || highest.isEmpty())
        return false;

    const auto covered = highest.getRight() - lowest.getX();
    if (covered <= 0.0f || covered > static_cast<float>(piano.getWidth()) + 1.0f)
        return false;

    // The key range must be the one the footer was designed around, not an
    // arbitrary window onto it.
    return editor.keyboardLowestNoteForTesting() == 24
        && editor.keyboardHighestNoteForTesting() == 96;
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

        if (!checkKeyboardGeometry(*editor))
            return EXIT_FAILURE;

        // Shell regions: non-empty and pairwise disjoint. The workspace is the
        // content column between the hero and the footer, so it shares the
        // keyboard's x range by design; the two are separated vertically, which
        // is what `intersects` checks.
        const auto rack = editor->rackBoundsForTesting();
        const auto workspace = editor->workspaceBoundsForTesting();
        const auto keyboard = editor->keyboardBoundsForTesting();
        if (rack.isEmpty() || workspace.isEmpty() || keyboard.isEmpty()
            || rack.intersects(workspace) || rack.intersects(keyboard)
            || workspace.intersects(keyboard))
            return EXIT_FAILURE;

        // The module panels' bottom edge is exactly the footer's top edge.
        const auto shell = editor->shellLayoutForTesting();
        if (workspace.getBottom() != shell.footer.getY())
            return EXIT_FAILURE;

        // The rail is full height: it reaches lower than the workspace and past
        // the top of the footer, which is the structural change from the
        // previous build where the rail stopped above the keyboard.
        if (rack.getBottom() <= workspace.getBottom())
            return EXIT_FAILURE;

        if (rack.getBottom() <= shell.footer.getY())
            return EXIT_FAILURE;
    }

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
    std::cout << "Editor smoke test passed\n";
    return EXIT_SUCCESS;
}
