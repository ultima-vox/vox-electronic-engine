#include "PluginEditor.h"
#include "instrument/HostParameterSchema.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

// Distinguish independent binding/layout failures in headless Windows CI.
// Every assertion reports its source line instead of an opaque exit code.
#define EDITOR_SMOKE_FAIL() do { \
    std::cerr << "[EDITOR_SMOKE_FAIL] line " << __LINE__ << "\\n"; \
    return EXIT_FAILURE; \
} while (false)

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    VstEngineAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
    auto* editor = dynamic_cast<VstEngineAudioProcessorEditor*>(base.get());
    if (editor == nullptr)
        EDITOR_SMOKE_FAIL();

    // Visual Gate capture is intentionally independent from Editor Smoke
    // assertions. A UI regression must remain red in CTest, while the actual
    // editor PNG remains available for debugging on the same compiled head.
    // This mode is not invoked by CTest and cannot make its result green.
    if (juce::SystemStats::getEnvironmentVariable("VOX_UI_CAPTURE_ONLY", {})
            == "1") {
        const auto acidPath = juce::SystemStats::getEnvironmentVariable(
            "VOX_UI_ACID_SNAPSHOT", {});
        const auto stepPath = juce::SystemStats::getEnvironmentVariable(
            "VOX_UI_ACID_STEP_SNAPSHOT", {});
        if (acidPath.isEmpty() && stepPath.isEmpty())
            EDITOR_SMOKE_FAIL();
        editor->selectSlotForTesting(1);
        editor->showPageForTesting(
            vstengine::ui::MainNavigation::Page::sound);
        editor->setSize(1024, 768);
        if (stepPath.isNotEmpty())
            editor->selectAcidStepForTesting();
        const auto image = editor->createComponentSnapshot(
            editor->getLocalBounds());
        juce::FileOutputStream output { juce::File(
            stepPath.isNotEmpty() ? stepPath : acidPath) };
        juce::PNGImageFormat format;
        if (!output.openedOk()) EDITOR_SMOKE_FAIL();
        output.setPosition(0);
        output.truncate();
        if (!format.writeImageToStream(image, output))
            EDITOR_SMOKE_FAIL();
        std::cout << "[VISUAL_GATE] real Acid editor capture completed\n";
        return EXIT_SUCCESS;
    }

    using Page = vstengine::ui::MainNavigation::Page;
    for (const auto page : { Page::sound, Page::pattern, Page::routing,
                             Page::zones, Page::macros, Page::advanced }) {
        editor->showPageForTesting(page);
        if (editor->currentPageForTesting() != page)
            EDITOR_SMOKE_FAIL();
    }

    // UI integration gate: Acid owns SOUND only when its Rack slot is
    // selected. All other pages and other instruments are unchanged.
    editor->selectSlotForTesting(1);
    editor->showPageForTesting(Page::sound);
    if (!editor->acidWorkspaceVisibleForTesting()
        || editor->acidControlsForTesting() < 25)
        EDITOR_SMOKE_FAIL();
    editor->showPageForTesting(Page::pattern);
    if (editor->acidWorkspaceVisibleForTesting())
        EDITOR_SMOKE_FAIL();
    editor->selectSlotForTesting(0);
    editor->showPageForTesting(Page::sound);
    if (editor->acidWorkspaceVisibleForTesting())
        EDITOR_SMOKE_FAIL();
    editor->selectSlotForTesting(1);
    editor->showPageForTesting(Page::sound);
    auto& params = processor.parameters();
    // True UI->APVTS attachment checks: an Acid knob must not move another
    // physical sound parameter or bleed into the Psy Bass slot.
    if (!editor->acidKnobTextForTesting("osc_tune").contains("st")
        || !editor->acidKnobTextForTesting("cutoff").contains("Hz")
        || !editor->acidKnobTextForTesting("output").contains("dB"))
        EDITOR_SMOKE_FAIL();
    const auto* tune = params.getRawParameterValue("slot02Acid_osc_tune");
    const auto* fine = params.getRawParameterValue("slot02Acid_osc_fine");
    const auto* other = params.getRawParameterValue("slot01Acid_osc_tune");
    if (!tune || !fine || !other) EDITOR_SMOKE_FAIL();
    const float previousFine = fine->load(), previousOther = other->load();
    if (!editor->setAcidKnobForTesting("osc_tune", 0.83)
        || std::abs(tune->load() - 0.83f) > 0.001f
        || std::abs(fine->load() - previousFine) > 0.0001f
        || std::abs(other->load() - previousOther) > 0.0001f)
        EDITOR_SMOKE_FAIL();

    editor->selectAcidFilterEnvelopeForTesting(true);
    const auto* ampDecay = params.getRawParameterValue("slot02Acid_amp_decay");
    const auto* filterDecay = params.getRawParameterValue(
        vstengine::instrument::hostparams::macroId(1, 4).c_str());
    if (!ampDecay || !filterDecay) EDITOR_SMOKE_FAIL();
    const float oldAmpDecay = ampDecay->load();
    if (!editor->setAcidKnobForTesting("decay", 0.67)
        || std::abs(filterDecay->load() - 0.67f) > 0.001f
        || std::abs(ampDecay->load() - oldAmpDecay) > 0.0001f)
        EDITOR_SMOKE_FAIL();
    editor->selectAcidFilterEnvelopeForTesting(false);
    const float oldFilterDecay = filterDecay->load();
    if (!editor->setAcidKnobForTesting("amp_decay", 0.24)
        || std::abs(ampDecay->load() - 0.24f) > 0.001f
        || std::abs(filterDecay->load() - oldFilterDecay) > 0.0001f)
        EDITOR_SMOKE_FAIL();

    // LFO1 and ENV1 must have independent physical controls and state.
    const auto* lfoRate = params.getRawParameterValue("slot02Acid_lfo1_rate");
    const auto* envAmount = params.getRawParameterValue("slot02Acid_mod_amount");
    if (!lfoRate || !envAmount) EDITOR_SMOKE_FAIL();
    const float envBefore = envAmount->load();
    editor->selectAcidLfo1ForTesting(true);
    if (!editor->setAcidKnobForTesting("lfo1_rate", 0.63)
        || std::abs(lfoRate->load() - 0.63f) > 0.001f
        || std::abs(envAmount->load() - envBefore) > 0.0001f)
        EDITOR_SMOKE_FAIL();
    editor->selectAcidLfo1ForTesting(false);
    const float rateBefore = lfoRate->load();
    if (!editor->setAcidKnobForTesting("mod_amount", 0.31)
        || std::abs(envAmount->load() - 0.31f) > 0.001f
        || std::abs(lfoRate->load() - rateBefore) > 0.0001f)
        EDITOR_SMOKE_FAIL();

    // ENV2 is never an alias for either the ENV1 amount or the LFO rate.
    const auto* env2Depth = params.getRawParameterValue("slot02Acid_env2_amount");
    if (!env2Depth) EDITOR_SMOKE_FAIL();
    const float oldEnvAmount = envAmount->load();
    const float oldLfoRate = lfoRate->load();
    editor->selectAcidEnv2ForTesting();
    if (!editor->setAcidKnobForTesting("env2_amount", 0.78)
        || std::abs(env2Depth->load() - 0.78f) > 0.001f
        || std::abs(envAmount->load() - oldEnvAmount) > 0.0001f
        || std::abs(lfoRate->load() - oldLfoRate) > 0.0001f)
        EDITOR_SMOKE_FAIL();
    editor->selectAcidLfo1ForTesting(false);

    // All three real LFO tabs have distinct host parameter attachments.
    const auto* lfo2Rate = params.getRawParameterValue("slot02Acid_lfo2_rate");
    const auto* lfo3Depth = params.getRawParameterValue("slot02Acid_lfo3_depth");
    if (!lfo2Rate || !lfo3Depth) EDITOR_SMOKE_FAIL();
    const float oldLfo1Rate = lfoRate->load();
    const float oldEnv2Depth = env2Depth->load();
    editor->selectAcidLfoForTesting(2);
    if (!editor->setAcidKnobForTesting("lfo2_rate", 0.76)
        || std::abs(lfo2Rate->load() - 0.76f) > 0.001f
        || std::abs(lfoRate->load() - oldLfo1Rate) > 0.0001f
        || std::abs(env2Depth->load() - oldEnv2Depth) > 0.0001f)
        EDITOR_SMOKE_FAIL();
    const float oldLfo2Rate = lfo2Rate->load();
    editor->selectAcidLfoForTesting(3);
    if (!editor->setAcidKnobForTesting("lfo3_depth", 0.58)
        || std::abs(lfo3Depth->load() - 0.58f) > 0.001f
        || std::abs(lfo2Rate->load() - oldLfo2Rate) > 0.0001f
        || std::abs(lfoRate->load() - oldLfo1Rate) > 0.0001f)
        EDITOR_SMOKE_FAIL();
    editor->selectAcidLfoForTesting(1);

    // Each LFO has its own Boolean Sync button attachment, not an aliased
    // ENV/LFO source control. Switching tabs must restore that LFO's value.
    const auto* sync1 = params.getRawParameterValue("slot02Acid_lfo1_sync");
    const auto* sync2 = params.getRawParameterValue("slot02Acid_lfo2_sync");
    const auto* sync3 = params.getRawParameterValue("slot02Acid_lfo3_sync");
    const auto* otherSync = params.getRawParameterValue("slot01Acid_lfo1_sync");
    if (!sync1 || !sync2 || !sync3 || !otherSync) EDITOR_SMOKE_FAIL();
    const float otherSyncBefore = otherSync->load();
    editor->selectAcidLfoForTesting(1);
    if (!editor->setAcidLfoSyncForTesting(true)
        || sync1->load() < 0.5f || sync2->load() > 0.5f
        || std::abs(otherSync->load() - otherSyncBefore) > 0.0001f)
        EDITOR_SMOKE_FAIL();
    if (!editor->acidKnobTextForTesting("lfo1_rate").contains("/"))
        EDITOR_SMOKE_FAIL();
    editor->selectAcidLfoForTesting(2);
    if (!editor->setAcidLfoSyncForTesting(true)
        || sync2->load() < 0.5f || sync1->load() < 0.5f)
        EDITOR_SMOKE_FAIL();
    editor->selectAcidLfoForTesting(3);
    if (!editor->setAcidLfoSyncForTesting(false)
        || sync3->load() > 0.5f || sync2->load() < 0.5f)
        EDITOR_SMOKE_FAIL();
    editor->selectAcidLfoForTesting(1);
    if (!editor->setAcidLfoSyncForTesting(true)
        || sync1->load() < 0.5f)
        EDITOR_SMOKE_FAIL();

    // Acid-only local preview must neither toggle another Rack slot nor
    // persist as a project parameter. A host project loads with preview off.
    if (processor.acidPreviewEnabled(1)
        || processor.acidPreviewEnabled(0))
        EDITOR_SMOKE_FAIL();
    editor->setAcidPreviewForTesting(true);
    if (!processor.acidPreviewEnabled(1)
        || processor.acidPreviewEnabled(0))
        EDITOR_SMOKE_FAIL();
    editor->setAcidPreviewForTesting(false);
    if (processor.acidPreviewEnabled(1))
        EDITOR_SMOKE_FAIL();
    editor->setAcidPreviewForTesting(true);
    editor->selectSlotForTesting(0);
    if (processor.acidPreviewEnabled(1))
        EDITOR_SMOKE_FAIL(); // No invisible Acid preview under Psy Bass.
    editor->selectSlotForTesting(1);
    editor->showPageForTesting(Page::sound);
    if (processor.acidPreviewEnabled(1))
        EDITOR_SMOKE_FAIL(); // Selection return must not restart audition.

    // STEP owns sixteen independent host-automated curve levels.
    const auto* step0 = params.getRawParameterValue("slot02Acid_step_01");
    const auto* step15 = params.getRawParameterValue("slot02Acid_step_16");
    const auto* otherStep0 = params.getRawParameterValue("slot01Acid_step_01");
    if (!step0 || !step15 || !otherStep0) EDITOR_SMOKE_FAIL();
    const float oldFirstSlot = otherStep0->load();
    editor->selectAcidStepForTesting();
    if (editor->acidStepAttachmentCountForTesting() != 16
        || !editor->setAcidStepLevelForTesting(0, 0.91)
        || !editor->setAcidStepLevelForTesting(15, 0.08)
        || std::abs(step0->load() - 0.91f) > 0.001f
        || std::abs(step15->load() - 0.08f) > 0.001f
        || std::abs(otherStep0->load() - oldFirstSlot) > 0.0001f)
        EDITOR_SMOKE_FAIL();
    editor->selectAcidLfoForTesting(1);
    if (editor->acidStepAttachmentCountForTesting() != 0
        || editor->setAcidStepLevelForTesting(0, 0.2))
        EDITOR_SMOKE_FAIL();

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
            EDITOR_SMOKE_FAIL();
        const auto rack = editor->rackBoundsForTesting();
        const auto workspace = editor->workspaceBoundsForTesting();
        const auto keyboard = editor->keyboardBoundsForTesting();
        if (rack.isEmpty() || workspace.isEmpty() || keyboard.isEmpty()
            || rack.intersects(workspace) || rack.intersects(keyboard)
            || workspace.intersects(keyboard))
            EDITOR_SMOKE_FAIL();
        if (editor->acidWorkspaceVisibleForTesting()
            && editor->acidModulationDiameterForTesting() < 17)
            EDITOR_SMOKE_FAIL(); // Never show a cosmetic 1–12 px knob.
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
            EDITOR_SMOKE_FAIL();
        output.setPosition(0);
        output.truncate();
        if (!format.writeImageToStream(image, output))
            EDITOR_SMOKE_FAIL();
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
        if (!output.openedOk()) EDITOR_SMOKE_FAIL();
        output.setPosition(0);
        output.truncate();
        if (!format.writeImageToStream(image, output)) EDITOR_SMOKE_FAIL();
    }
    const auto stepPath = juce::SystemStats::getEnvironmentVariable(
        "VOX_UI_ACID_STEP_SNAPSHOT", {});
    if (stepPath.isNotEmpty()) {
        editor->selectSlotForTesting(1);
        editor->showPageForTesting(Page::sound);
        editor->selectAcidStepForTesting();
        editor->setSize(1024, 768);
        if (editor->acidStepAttachmentCountForTesting() != 16) EDITOR_SMOKE_FAIL();
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        juce::FileOutputStream output { juce::File(stepPath) };
        juce::PNGImageFormat format;
        if (!output.openedOk()) EDITOR_SMOKE_FAIL();
        output.setPosition(0);
        output.truncate();
        if (!format.writeImageToStream(image, output)) EDITOR_SMOKE_FAIL();
    }
    std::cout << "Editor smoke test passed\n";
    return EXIT_SUCCESS;
}
