#include "PluginProcessor.h"
#include "instrument/HostParameterSchema.h"
#include "modules/BuiltInProvider.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
int testsRun = 0;
void require(bool condition, const char* message)
{
    ++testsRun;
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
void setPlain(VstEngineAudioProcessor& processor, const std::string& id,
              float value)
{
    auto* parameter = processor.parameters().getParameter(id);
    require(parameter != nullptr, id.c_str());
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
double energy(const juce::AudioBuffer<float>& audio)
{
    double result = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample) {
            const auto value = audio.getSample(channel, sample);
            require(std::isfinite(value), "render stays finite");
            result += static_cast<double>(value) * value;
        }
    return result;
}
double renderHostNote(VstEngineAudioProcessor& processor, int channel,
                      int note = 36)
{
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(channel, note, 1.0f), 0);
    processor.processBlock(audio, midi);
    return energy(audio);
}
double renderAudition(VstEngineAudioProcessor& processor, std::size_t slot)
{
    processor.selectSlot(slot);
    processor.keyboardState().noteOn(1, 48, 1.0f);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    processor.processBlock(audio, midi);
    processor.keyboardState().noteOff(1, 48, 1.0f);
    return energy(audio);
}
bool sameStep(const vstengine::sequence::Step& left,
              const vstengine::sequence::Step& right)
{
    return left.gate == right.gate && left.noteOffset == right.noteOffset
        && left.velocity == right.velocity && left.accent == right.accent
        && left.probability == right.probability
        && left.ratchetCount == right.ratchetCount
        && left.slideDuration == right.slideDuration
        && left.gateWidth == right.gateWidth;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    constexpr double audible = 1.0e-7;
    constexpr double silent = 1.0e-12;

    auto processor = std::make_unique<VstEngineAudioProcessor>();
    processor->prepareToPlay(48000.0, 512);
    setPlain(*processor, "midiMode", 1.0f);
    for (std::size_t slot = 0; slot < vstengine::instrument::maxSlots; ++slot)
        for (std::size_t macro = 0;
             macro < vstengine::instrument::macrosPerSlot; ++macro) {
            const auto id = vstengine::instrument::hostparams::macroId(slot, macro);
            const auto* parameter = processor->parameters().getParameter(id);
            require(parameter != nullptr && parameter->isAutomatable(),
                    "stable slot macro exposed to host");
        }

    require(renderHostNote(*processor, 1) > audible, "CH1 renders Bass");
    processor->instrumentRack().reset();
    require(renderHostNote(*processor, 2, 48) > audible,
            "CH2 renders independent ReferenceInstrument");
    // Acid-only extended controls and independent play-mode automation must
    // be available to VST3, without replacing any of the 8 Rack macros.
    constexpr std::array<std::string_view, 4> acidFlags {
        "legato", "retrigger", "vel_to_accent", "slide_on_overlap"
    };
    for (std::size_t slot = 0; slot < vstengine::instrument::maxSlots; ++slot) {
        for (std::size_t p = vstengine::instrument::macrosPerSlot;
             p < vstengine::acid::AcidEngine::parameterCount; ++p) {
            if (p == vstengine::acid::AcidEngine::playModeFlags) continue;
            const auto id = vstengine::acid::hostParameterId(
                slot, vstengine::acid::AcidEngine::parameterIds[p]);
            const auto* param = processor->parameters().getParameter(id);
            require(param && param->isAutomatable(),
                    "each extended Acid parameter exposed to host separately");
        }
        for (auto semantic : acidFlags) {
            const auto id = vstengine::acid::hostParameterId(slot, semantic);
            const auto* param = processor->parameters().getParameter(id);
            require(param && param->isAutomatable(),
                    "Acid play-mode toggle has its own VST automation ID");
        }
    }

    processor->instrumentRack().reset();
    require(renderHostNote(*processor, 3) < silent, "unassigned channel silent");

    setPlain(*processor, "slot01MidiIn", 5.0f);
    processor->instrumentRack().reset();
    require(renderHostNote(*processor, 1) < silent, "old Bass channel inactive");
    processor->instrumentRack().reset();
    require(renderHostNote(*processor, 5) > audible, "Bass channel configurable");

    processor->instrumentRack().reset();
    require(renderAudition(*processor, 0) > audible,
            "selected-slot keyboard auditions Bass");
    setPlain(*processor, "slot01Mute", 1.0f);
    processor->instrumentRack().reset();
    require(renderAudition(*processor, 0) < silent, "slot mute blocks audition");
    setPlain(*processor, "slot01Mute", 0.0f);

    juce::String generationDiagnostic;
    require(processor->loadSlotInstrument(2,
                vstengine::modules::leadInstrumentId, generationDiagnostic)
            && processor->loadSlotInstrument(3,
                vstengine::modules::semanticFxInstrumentId,
                generationDiagnostic)
            && processor->loadSlotInstrument(4,
                vstengine::modules::atmosInstrumentId, generationDiagnostic),
            "remaining production generators load into Rack");
    setPlain(*processor, "rngSeed", 9001.0f);
    require(processor->generateAllPatterns(generationDiagnostic),
            "global style generator coordinates unlocked instruments");
    for (std::size_t slot = 0; slot < 5; ++slot)
        require(!processor->instrumentRack().state()[slot].patternPayload.empty(),
                "global generation commits each production slot");

    const auto lockedPattern = processor->instrumentRack().state()[1].patternPayload;
    setPlain(*processor, "slot02Lock", 1.0f);
    setPlain(*processor, "rngSeed", 9002.0f);
    require(processor->generateAllPatterns(generationDiagnostic),
            "global regenerate succeeds with locked slot");
    require(processor->instrumentRack().state()[1].patternPayload
                == lockedPattern,
            "global regenerate preserves locked slot exactly");

    processor->selectSlot(0);
    processor->sequence().setSelectedRange(0, 7);
    const auto beforeSelectedMutation = processor->sequence().copy();
    require(processor->mutateSelectedPattern(true, generationDiagnostic),
            "instrument-owned selected mutation succeeds");
    bool selectedChanged = false;
    for (int step = 0; step < processor->sequence().size(); ++step) {
        if (step <= 7)
            selectedChanged = selectedChanged
                || !sameStep(processor->sequence()[step],
                             beforeSelectedMutation[step]);
        else
            require(sameStep(processor->sequence()[step],
                             beforeSelectedMutation[step]),
                    "Mutate Selected preserves unselected steps");
    }
    require(selectedChanged, "Mutate Selected changes selected musical data");

    const auto lockedBeforeMutate = processor->instrumentRack().state()[1]
        .patternPayload;
    require(processor->mutateAllPatterns(generationDiagnostic),
            "global mutate coordinates unlocked instruments");
    require(processor->instrumentRack().state()[1].patternPayload
                == lockedBeforeMutate,
            "global mutate preserves locked slot exactly");
    setPlain(*processor, "slot02Lock", 0.0f);

    processor->sequence()[3].gate = true;
    processor->sequence()[3].noteOffset = 7;
    processor->publishSequenceForAudio();
    require(processor->applyInternalEffectsPreset("psy-drive"),
            "internal FX preset applies through processor");
    require(processor->internalEffectsState().effects[0].enabled,
            "internal distortion becomes active");
    setPlain(*processor, "kickTune", 43.0f);
    setPlain(*processor, "matchBassTimingOffsetMs", 12.0f);
    // Acid-only independent VST automation is stored in host APVTS state,
    // not another instrument's macro or a non-durable editor component.
    const auto acidFine = vstengine::acid::hostParameterId(1, "osc_fine");
    const auto acidLevel = vstengine::acid::hostParameterId(1, "output");
    const auto acidLegato = vstengine::acid::hostParameterId(1, "legato");
    const auto acidLfo1Sync = vstengine::acid::hostParameterId(1, "lfo1_sync");
    const auto acidLfo2Sync = vstengine::acid::hostParameterId(1, "lfo2_sync");
    const auto acidStep0 = vstengine::acid::hostParameterId(1, "step_01");
    const auto acidStep15 = vstengine::acid::hostParameterId(1, "step_16");
    const auto otherAcidFine = vstengine::acid::hostParameterId(0, "osc_fine");
    const float otherBefore = processor->parameters()
                                  .getRawParameterValue(otherAcidFine.c_str())->load();
    setPlain(*processor, acidFine, 0.27f);
    setPlain(*processor, acidLevel, 0.64f);
    setPlain(*processor, acidLegato, 0.0f);
    setPlain(*processor, acidLfo1Sync, 1.0f);
    setPlain(*processor, acidLfo2Sync, 0.0f);
    setPlain(*processor, acidStep0, 0.87f);
    setPlain(*processor, acidStep15, 0.04f);
    require(std::abs(processor->parameters().getRawParameterValue(
                         otherAcidFine.c_str())->load() - otherBefore) < 0.0001f,
            "Acid host write must not affect another slot before save");
    juce::MemoryBlock state;
    processor->getStateInformation(state);
    require(state.getSize() > 0, "state serialization succeeds");

    auto restored = std::make_unique<VstEngineAudioProcessor>();
    restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    restored->prepareToPlay(48000.0, 512);
    setPlain(*restored, "midiMode", 1.0f);
    require(restored->sequence()[3].gate
                && restored->sequence()[3].noteOffset == 7,
            "Bass sequence round-trips");
    require(std::abs(restored->parameters().getRawParameterValue(
                         acidFine.c_str())->load() - 0.27f) < 0.001f
                && std::abs(restored->parameters().getRawParameterValue(
                         acidLevel.c_str())->load() - 0.64f) < 0.001f
                && restored->parameters().getRawParameterValue(
                         acidLegato.c_str())->load() < 0.5f
                && restored->parameters().getRawParameterValue(
                         acidLfo1Sync.c_str())->load() >= 0.5f
                && restored->parameters().getRawParameterValue(
                         acidLfo2Sync.c_str())->load() < 0.5f
                && std::abs(restored->parameters().getRawParameterValue(
                         acidStep0.c_str())->load() - 0.87f) < 0.001f
                && std::abs(restored->parameters().getRawParameterValue(
                         acidStep15.c_str())->load() - 0.04f) < 0.001f,
            "Acid independent host parameter automation survives project restore");
    require(std::abs(restored->parameters().getRawParameterValue(
                         otherAcidFine.c_str())->load() - otherBefore) < 0.0001f,
            "Acid saved parameter authority never bleeds between slots");
    require(restored->internalEffectsState().effects[0].enabled
                && restored->internalEffectsState().effects[0].mix > 0.0f,
            "internal FX state round-trips");
    require(std::abs(restored->parameters().getRawParameterValue("kickTune")->load()
                     - 43.0f) < 0.01f,
            "dormant Kick value retained for migration");
    require(std::abs(restored->parameters().getRawParameterValue(
                         "matchBassTimingOffsetMs")->load() - 12.0f) < 0.01f,
            "dormant MATCH value retained for migration");
    require(renderHostNote(*restored, 9) < silent,
            "legacy Kick parameters never reactivate audio");

    // Cubase may request project state from a non-audio thread while playback
    // continues. Saving must neither mutate the audio-thread control snapshot
    // nor make Rack::process return a silent block.
    setPlain(*restored, "slot01MidiIn", 1.0f);
    std::atomic<bool> start { false };
    std::atomic<int> saved { 0 };
    std::thread saver([&] {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        for (int iteration = 0; iteration < 2000; ++iteration) {
            juce::MemoryBlock snapshot;
            restored->getStateInformation(snapshot);
            if (snapshot.getSize() != 0)
                saved.fetch_add(1, std::memory_order_relaxed);
        }
    });
    start.store(true, std::memory_order_release);
    int silentBlocks = 0;
    for (int iteration = 0; iteration < 2000; ++iteration) {
        restored->instrumentRack().reset();
        if (renderHostNote(*restored, 1) <= audible)
            ++silentBlocks;
    }
    saver.join();
    require(saved.load(std::memory_order_relaxed) == 2000,
            "concurrent host snapshots remain valid");
    require(silentBlocks == 0,
            "concurrent host snapshots never gate realtime audio");

    std::cout << "Plugin Rack integration tests passed (" << testsRun << ")\n";
    return EXIT_SUCCESS;
}
