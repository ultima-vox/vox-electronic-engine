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

// A fresh Rack starts empty (owner Cubase acceptance); this fixture loads the
// layout it exercises explicitly: Bass in slot 1 (CH1), Acid in slot 2 (CH2).
static bool loadBassAndAcid(VstEngineAudioProcessor &p) {
  juce::String d;
  return p.loadSlotInstrument(0, vstengine::modules::bassInstrumentId, d) &&
         p.loadSlotInstrument(1, vstengine::modules::acidInstrumentId, d);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    constexpr double audible = 1.0e-7;
    constexpr double silent = 1.0e-12;

    auto processor = std::make_unique<VstEngineAudioProcessor>();
    processor->prepareToPlay(48000.0, 512);
    setPlain(*processor, "midiMode", 1.0f);
    require(loadBassAndAcid(*processor), "fixture loads Bass and Acid explicitly");
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
    require(restored->internalEffectsState().effects[0].enabled
                && restored->internalEffectsState().effects[0].mix > 0.0f,
            "internal FX state round-trips");
    for (const auto* removed : { "drive", "filterCutoff", "midiChannel",
                                 "bassLevel", "kickTune", "kickMidiChannel",
                                 "kickLevel", "matchBassTimingOffsetMs" })
        require(restored->parameters().getParameter(removed) == nullptr,
                "POC engine/Part parameter is not a host parameter");
    require(renderHostNote(*restored, 9) < silent,
            "an unrouted channel stays silent");

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
