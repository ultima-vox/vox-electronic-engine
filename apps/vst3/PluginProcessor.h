#pragma once
#include "DirectParameterBank.h"
#include "RackAudioParameters.h"
#include "effects/EffectsChain.h"
#include "instrument/InstrumentRegistry.h"
#include "midi/GeneratedNoteScheduler.h"
#include "midi/RealtimeAuditionQueue.h"
#include "midi/SourceSelector.h"
#include "parts/PartState.h"
#include "preset/PresetManager.h"
#include "rack/Rack.h"
#include "rack/RackState.h"
#include "rack/RealtimeMutationGate.h"
#include "sequence/RealtimeSequenceBridge.h"
#include "sequence/Sequence.h"
#include <JuceHeader.h>
#include <array>
#include <memory>

class VstEngineAudioProcessor final : public juce::AudioProcessor,
                                      private juce::MidiKeyboardStateListener {
public:
    // Canonical MIDI source mode (AUTO / PIANO ROLL / GENERATOR / BOTH).
    // The decision rule lives in vstengine::midi::shouldRunGenerator() so the
    // full mode x input-presence matrix is unit-tested in isolation.
    using MidiSourceMode = vstengine::midi::SourceMode;

    VstEngineAudioProcessor();
    // Static C++ composition boundary; no DLL loading or runtime code download.
    explicit VstEngineAudioProcessor(
        std::unique_ptr<vstengine::instrument::InstrumentProvider>);
    ~VstEngineAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override
    {
      return declaredTailSeconds.load(std::memory_order_relaxed);
    }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& parameters() noexcept { return apvts; }
    juce::MidiKeyboardState& bassKeyboardState() noexcept { return keyboard; }
    juce::MidiKeyboardState& keyboardState() noexcept { return keyboard; }
    vstengine::sequence::Sequence& partSequence(int partIndex) noexcept
    {
        return patternRuntime->slotSequences[
            static_cast<std::size_t>(partIndex == 0 ? 0 : 1)];
    }
    vstengine::sequence::Sequence& sequence() noexcept
    {
        return patternRuntime->slotSequences[selectedSlotIndex()];
    }
    // Shared editor adapters own a local model. Never hand the native editor
    // a mutable reference also used by host project restoration.
    struct SequenceEdit {
      vstengine::sequence::Sequence value;
      std::size_t slot{};
      std::uint64_t revision{};
    };
    [[nodiscard]] SequenceEdit sequenceEditSnapshot();
    bool commitSequenceEdit(SequenceEdit &);
    vstengine::PresetManager* presetManager() noexcept { return presetManager_.get(); }
    int getPartPlayHeadStep(int partIndex) const noexcept
    {
        const auto index = static_cast<std::size_t>(partIndex == 0 ? 0 : 1);
        return patternRuntime->schedulers[index].playHeadStep();
    }
    int getCurrentPlayHeadStep() const noexcept
    {
        return patternRuntime->schedulers[selectedSlotIndex()].playHeadStep();
    }
    // Message/controller thread only. Audio merely requests this cleanup.
    void serviceKeyboardUi() {
      if (keyboardResetRequested.exchange(false))
        keyboard.allNotesOff(0);
    }
    void requestPanic() noexcept { panicRequested.store(true); }
    [[nodiscard]] float currentCpuLoad() const noexcept
    {
        return cpuLoad.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool hasRecentMidiActivity() const noexcept
    {
        return midiActivity.load(std::memory_order_relaxed) != 0;
    }
    [[nodiscard]] std::array<float, 2> consumeOutputPeaks() noexcept {
      return {outputPeaks[0].exchange(0.0f), outputPeaks[1].exchange(0.0f)};
    }
    [[nodiscard]] float currentDuckReduction() const noexcept {
      return duckReduction.load();
    }
    [[nodiscard]] bool transportIsPlaying() const noexcept {
      return transportPlaying.load();
    }
    [[nodiscard]] auto rackSlotSummaries() const { return rack.summaries(); }
    void publishPartSequenceForAudio(int partIndex) noexcept;
    void publishSequenceForAudio() noexcept
    {
        publishPartSequenceForAudio(static_cast<int>(selectedSlotIndex()));
    }
    // Controller-only: pattern data at the Rack input, before transpose/zones,
    // fader/FX or ducking. Each export owns a distinct temporary file.
    bool serializeSlotPatternMidi(std::size_t, juce::MemoryBlock &,
                                  juce::String &diagnostic);
    juce::File createSlotMidiFile(std::size_t);
    static bool writePatternMidiFile(const juce::File &,
                                     const juce::MemoryBlock &,
                                     juce::String &diagnostic);
    juce::File createPartMidiFile(int slotIndex) {
      return slotIndex >= 0
                 ? createSlotMidiFile(static_cast<std::size_t>(slotIndex))
                 : juce::File{};
    }
    juce::File createGeneratedMidiFile() {
      return createSlotMidiFile(selectedSlotIndex());
    }
    [[nodiscard]] vstengine::parts::PartRegistry& partRegistry() noexcept
    {
        return parts;
    }
    [[nodiscard]] bool isPartLocked(int partIndex) const noexcept;
    [[nodiscard]] vstengine::rack::Rack& instrumentRack() noexcept { return rack; }
    [[nodiscard]] const vstengine::rack::Rack& instrumentRack() const noexcept { return rack; }
    [[nodiscard]] vstengine::effects::ChainState internalEffectsState() const noexcept
    {
        return effectsChain.state();
    }
    bool applyInternalEffectsPreset(std::string_view presetId) noexcept;
    void selectSlot(std::size_t index) noexcept
    {
        selectedSlot.store(juce::jlimit<std::size_t>(0,
            vstengine::instrument::maxSlots - 1, index));
    }
    [[nodiscard]] std::size_t selectedSlotIndex() const noexcept
    {
        return selectedSlot.load();
    }
    [[nodiscard]] std::span<const vstengine::instrument::InstrumentDescriptor* const>
        availableInstruments() const noexcept
    {
        return instrumentRegistry.descriptors();
    }
    bool loadSlotInstrument(std::size_t, std::string_view,
                            juce::String& diagnostic);
    // One instrument = one MIDI channel. 0 = Off; a channel held by another
    // slot is rejected (no swap/move/layer). Loading an instrument into a slot
    // without a channel takes the next free one.
    bool assignSlotChannel(std::size_t, int channel, juce::String& diagnostic);
    [[nodiscard]] bool channelAvailable(std::size_t slot, int channel) const noexcept;
    [[nodiscard]] std::span<const vstengine::instrument::ContentDescriptor>
        selectedContent() const noexcept;
    [[nodiscard]] std::uint32_t selectedSequenceFieldMask() const noexcept;
    bool applySelectedSoundPreset(std::string_view, juce::String& diagnostic);
    bool generateSelectedPattern(std::string_view, juce::String& diagnostic);
    bool mutateSelectedPattern(bool selectedStepsOnly,
                               juce::String& diagnostic);
    bool generateAllPatterns(juce::String& diagnostic);
    bool mutateAllPatterns(juce::String& diagnostic);
    [[nodiscard]] int nextFreeChannel() const noexcept;

private:
    // APVTS bridge passed to the preset module (vstengine::PresetStateStore).
    class ApvtsPresetStore final : public vstengine::PresetStateStore {
    public:
        explicit ApvtsPresetStore(juce::AudioProcessorValueTreeState& s)
            : apvts(s) {}
        [[nodiscard]] juce::ValueTree copyState() const override
        {
            return apvts.copyState();
        }
        void replaceState(juce::ValueTree newState) override
        {
            apvts.replaceState(std::move(newState));
        }
        [[nodiscard]] float convertTo0to1(const char* parameterId,
                                          float plainValue) const override
        {
            auto* ranged = apvts.getParameter(parameterId);
            return ranged != nullptr ? ranged->convertTo0to1(plainValue)
                                     : plainValue;
        }

    private:
        juce::AudioProcessorValueTreeState& apvts;
    };

    static vstengine::instrument::InstrumentRegistry makeRegistry(
        std::unique_ptr<vstengine::instrument::InstrumentProvider>);
    static juce::AudioProcessorValueTreeState::ParameterLayout
        createParameterLayout(
            std::span<
                const vstengine::instrument::InstrumentDescriptor *const>);
    void seedInitialSequence();
    static bool containsNoteEvents(const juce::MidiBuffer& midi) noexcept;
    MidiSourceMode currentMidiMode() const noexcept;
    bool keyboardHasActiveNotes() const noexcept;
    void syncPartControlsFromParameters(
        vstengine::parts::PartRegistry& destination) noexcept;
    void syncParametersFromParts();
    void syncRackControlsFromParameters() noexcept;
    void readRackControlsFromParameters(
        std::span<vstengine::rack::ProcessSlotControls>) const noexcept;
    void cacheRackParameterPointers();
    juce::ValueTree captureProjectState();
    bool restoreProjectState(const juce::ValueTree &);
    void updateReportedLatencyAndTails() noexcept;
    bool preflightDirectState(
        const std::array<vstengine::rack::PersistentSlotState,
                         vstengine::instrument::maxSlots> &) const noexcept;
    void syncDirectParametersFromRack(
        std::size_t onlySlot = vstengine::instrument::maxSlots);
    [[nodiscard]] std::array<vstengine::rack::PersistentSlotState,
        vstengine::instrument::maxSlots> snapshotRackWithPatterns();
    [[nodiscard]] juce::ValueTree captureEffectsState() const;
    bool prepareEffectsState(const juce::ValueTree&,
                             vstengine::effects::EffectsChain&) const;
    [[nodiscard]] std::string generatorProfileForSlot(
        std::size_t slotIndex) const;
    bool prepareGeneratedPattern(
        std::size_t slotIndex, std::string_view profileId,
        const VoxPatternV1* input, float mutationAmount,
        int selectedStart, int selectedEnd,
        vstengine::sequence::Sequence& sequence,
        std::vector<std::byte>& encoded, juce::String& diagnostic) const;
    bool runGlobalPatternOperation(bool mutate, juce::String& diagnostic);
    bool restoreSlotPatterns(const std::array<vstengine::rack::PersistentSlotState,
        vstengine::instrument::maxSlots>&, bool allowEmpty) noexcept;
    std::size_t convertMidi(
        const juce::MidiBuffer &,
        std::array<VoxMidiEventV1, vstengine::rack::RackRouter::eventCapacity>
            &,
        std::uint32_t sampleCount, bool *overflow = nullptr) noexcept;

    mutable vstengine::rack::RealtimeMutationGate mutationGate;
    void handleNoteOn(juce::MidiKeyboardState *, int, int, float) override;
    void handleNoteOff(juce::MidiKeyboardState *, int, int, float) override;
    juce::MidiKeyboardState keyboard;
    vstengine::midi::RealtimeAuditionQueue auditionQueue;
    std::atomic<bool> keyboardResetRequested{};
    vstengine::instrument::InstrumentRegistry instrumentRegistry;
    juce::AudioProcessorValueTreeState apvts;
    vstengine::rack::Rack rack;
    vstengine::effects::EffectsChain effectsChain;
    vstengine::rack::AudioRouting audioRouting;
    vstengine::host::RackAudioParameterRefs audioRoutingRefs;
    vstengine::host::DirectParameterBank directParameterBank;
    std::atomic<double> declaredTailSeconds{0.0};
    std::atomic<std::uint32_t> declaredRackTail{}, declaredEffectsTail{};
    std::atomic<bool> rackAudioReady{false};
    std::atomic<std::uint32_t> returnTailSamples{};
    std::atomic<float> duckReduction{};

    vstengine::parts::PartRegistry parts;
    struct PatternRuntime {
      std::array<std::uint64_t, vstengine::instrument::maxSlots> revisions{};
      std::array<vstengine::sequence::Sequence, vstengine::instrument::maxSlots>
          slotSequences{};
      std::array<vstengine::sequence::RealtimeSequenceBridge,
                 vstengine::instrument::maxSlots>
          sequenceBridges{};
      std::array<vstengine::sequence::Sequence, vstengine::instrument::maxSlots>
          audioSequences{};
      std::array<vstengine::midi::GeneratedNoteScheduler,
                 vstengine::instrument::maxSlots>
          schedulers{};
      std::array<juce::MidiBuffer, vstengine::instrument::maxSlots>
          generatedScratch{};
    };
    std::unique_ptr<PatternRuntime> patternRuntime {
        std::make_unique<PatternRuntime>()
    };
    ApvtsPresetStore presetStore;
    std::unique_ptr<vstengine::PresetManager> presetManager_;
    // Generated-playback state machine (audio thread, see libs/midi).
    double currentSampleRate { 44100.0 };
    std::uint32_t currentMaximumBlockSize { 512 };
    bool wasTransportPlaying { false };
    std::atomic<bool> panicRequested { false };
    std::atomic<double> latestHostBpm { 145.0 };
    std::array<std::atomic<float>, 2> outputPeaks{};
    std::atomic<bool> transportPlaying{false};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain{
        1.0f};
    std::atomic<float> *masterGainParameter{};
    std::atomic<float> cpuLoad { 0.0f };
    std::atomic<std::uint32_t> midiActivity { 0 };

    // Audio-owned held-note accounting; controller handoff is auditionQueue.
    std::array<bool, 128> auditionNotes {};
    std::array<VoxMidiEventV1, vstengine::rack::RackRouter::eventCapacity>
        hostEvents {};
    std::array<VoxMidiEventV1, vstengine::rack::RackRouter::eventCapacity>
        auditionEvents {};
    struct GeneratedRoutingScratch {
        std::array<std::array<VoxMidiEventV1,
                             vstengine::rack::RackRouter::eventCapacity>,
                   vstengine::instrument::maxSlots> events {};
        std::array<vstengine::rack::RackRouter::GeneratedSlotEvents,
                   vstengine::instrument::maxSlots> streams {};
    };
    std::unique_ptr<GeneratedRoutingScratch> generatedRoutingScratch {
        std::make_unique<GeneratedRoutingScratch>()
    };
    std::atomic<std::size_t> selectedSlot { 0 };
    struct RackParameterRefs {
        std::atomic<float>* midiIn {};
        std::atomic<float>* keyLow {};
        std::atomic<float>* keyHigh {};
        std::atomic<float>* velocityLow {};
        std::atomic<float>* velocityHigh {};
        std::atomic<float>* transpose {};
        std::atomic<float>* enabled {};
        std::atomic<float>* mute {};
        std::atomic<float>* solo {};
        std::atomic<float>* locked {};
        std::atomic<float>* level {};
        std::atomic<float>* pan {};
        std::array<std::atomic<float>*, vstengine::instrument::macrosPerSlot>
            macros {};
    };
    std::array<RackParameterRefs, vstengine::instrument::maxSlots>
        rackParameterRefs {};
    std::array<vstengine::rack::ProcessSlotControls,
               vstengine::instrument::maxSlots> rackControls {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VstEngineAudioProcessor)
};
