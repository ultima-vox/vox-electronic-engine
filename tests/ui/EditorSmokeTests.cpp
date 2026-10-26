#include "PluginEditor.h"
#include "instrument/HostParameterSchema.h"
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
    auto& params = processor.parameters();
    // True UI->APVTS attachment checks: an Acid knob must not move another
    // physical sound parameter or bleed into the Psy Bass slot.
    const auto* tune = params.getRawParameterValue("slot02Acid_osc_tune");
    const auto* fine = params.getRawParameterValue("slot02Acid_osc_fine");
    const auto* other = params.getRawParameterValue("slot01Acid_osc_tune");
    if (!tune || !fine || !other) return EXIT_FAILURE;
    const float previousFine = fine->load(), previousOther = other->load();
    if (!editor->setAcidKnobForTesting("osc_tune", 0.83)
        || std::abs(tune->load() - 0.83f) > 0.001f
        || std::abs(fine->load() - previousFine) > 0.0001f
        || std::abs(other->load() - previousOther) > 0.0001f)
        return EXIT_FAILURE;

    editor->selectAcidFilterEnvelopeForTesting(true);
    const auto* ampDecay = params.getRawParameterValue("slot02Acid_amp_decay");
    const auto* filterDecay = params.getRawParameterValue(
        vstengine::instrument::hostparams::macroId(1, 4).c_str());
    if (!ampDecay || !filterDecay) return EXIT_FAILURE;
    const float oldAmpDecay = ampDecay->load();
    if (!editor->setAcidKnobForTesting("decay", 0.67)
        || std::abs(filterDecay->load() - 0.67f) > 0.001f
        || std::abs(ampDecay->load() - oldAmpDecay) > 0.0001f)
        return EXIT_FAILURE;
    editor->selectAcidFilterEnvelopeForTesting(false);
    const float oldFilterDecay = filterDecay->load();
    if (!editor->setAcidKnobForTesting("amp_decay", 0.24)
        || std::abs(ampDecay->load() - 0.24f) > 0.001f
        || std::abs(filterDecay->load() - oldFilterDecay) > 0.0001f)
        return EXIT_FAILURE;

    // LFO1 and ENV1 must have independent physical controls and state.
    const auto* lfoRate = params.getRawParameterValue("slot02Acid_lfo1_rate");
    const auto* envAmount = params.getRawParameterValue("slot02Acid_mod_amount");
    if (!lfoRate || !envAmount) return EXIT_FAILURE;
    const float envBefore = envAmount->load();
    editor->selectAcidLfo1ForTesting(true);
    if (!editor->setAcidKnobForTesting("lfo1_rate", 0.63)
        || std::abs(lfoRate->load() - 0.63f) > 0.001f
        || std::abs(envAmount->load() - envBefore) > 0.0001f)
        return EXIT_FAILURE;
    editor->selectAcidLfo1ForTesting(false);
    const float rateBefore = lfoRate->load();
    if (!editor->setAcidKnobForTesting("mod_amount", 0.31)
        || std::abs(envAmount->load() - 0.31f) > 0.001f
        || std::abs(lfoRate->load() - rateBefore) > 0.0001f)
        return EXIT_FAILURE;

    for (const auto size : { juce::Point<int> { 1024, 768 },
                             juce::Point<int> { 1040, 680 },
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
        editor->setSize(1024, 768);
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
