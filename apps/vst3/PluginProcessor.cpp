#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "midi/GeneratedNoteScheduler.h"
#include "midi/MidiExport.h"
#include "instrument/HostParameterSchema.h"
#include "modules/BuiltInProvider.h"
#include "preset/PresetManager.h"
#include "sequence/PatternAdapter.h"
#include "acid/AcidEngine.h"
#include "acid/AcidProvider.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
std::string slotParameterId(std::size_t slot, std::string_view suffix)
{
    char prefix[16] {};
    std::snprintf(prefix, sizeof(prefix), "slot%02zu", slot + 1);
    return std::string(prefix) + std::string(suffix);
}
} // namespace

VstEngineAudioProcessor::VstEngineAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      rack(instrumentRegistry),
      presetStore(apvts)
{
    std::string diagnostic;
    if (!instrumentRegistry.registerProvider(
            vstengine::modules::createBuiltInProvider(&acidHostBridge), diagnostic))
        jassertfalse;

    auto initialRack = rack.state();
    initialRack[0].instrumentId = vstengine::modules::bassInstrumentId;
    initialRack[0].routing = { vstengine::rack::RouteMode::channel, 1,
                               0, 127, 1, 127, 0 };
    initialRack[0].macros = { 0.48f, 0.55f, 0.4f, 0.25f,
                              0.32f, 0.5f, 0.0f, 0.15f };
    initialRack[1].instrumentId = vstengine::modules::acidInstrumentId;
    initialRack[1].routing = { vstengine::rack::RouteMode::channel, 2,
                               0, 127, 1, 127, 0 };
    initialRack[1].macros = { 0.05f, 0.46f, 0.72f, 0.70f,
                              0.48f, 0.58f, 0.30f, 0.28f };
    (void) rack.replaceState(initialRack, nullptr, diagnostic);
    cacheRackParameterPointers();
    // Acid-specific host parameter IDs are additive and slot-stable. The
    // original eight Rack host macros remain their sole DSP controllers.
    for (std::size_t slot = 0; slot < rackControls.size(); ++slot) {
        rackControls[slot].slotId = initialRack[slot].slotId;
        acidHostBridge.setSlotId(slot, initialRack[slot].slotId);
        for (std::size_t p = vstengine::instrument::macrosPerSlot;
             p < vstengine::acid::AcidEngine::parameterCount; ++p) {
            if (p == vstengine::acid::AcidEngine::playModeFlags) continue;
            const auto id = vstengine::acid::hostParameterId(
                slot, vstengine::acid::AcidEngine::parameterIds[p]);
            acidHostBridge.bind(slot, p, apvts.getRawParameterValue(id.c_str()));
        }
        constexpr std::array<std::string_view, 4> flags {
            "legato", "retrigger", "vel_to_accent", "slide_on_overlap"
        };
        for (std::size_t f = 0; f < flags.size(); ++f) {
            const auto id = vstengine::acid::hostParameterId(slot, flags[f]);
            acidHostBridge.bindFlag(slot, f, apvts.getRawParameterValue(id.c_str()));
        }
    }

    // Seed initial sequence with darkPsy pattern (canonical seeded operation
    // owned by the sequence module; same generator stream as before).
    seedInitialSequence();
    for (std::size_t slot = 0; slot < patternRuntime->slotSequences.size(); ++slot) {
        publishPartSequenceForAudio(static_cast<int>(slot));
        (void) patternRuntime->sequenceBridges[slot].read(
            patternRuntime->audioSequences[slot]);
    }

    presetManager_ = std::make_unique<vstengine::PresetManager>(
        presetStore, patternRuntime->slotSequences[0],
        vstengine::PresetManager::FullStateCallbacks {
            [this] {
                juce::ValueTree extra("ENGINE_EXTRA");
                extra.addChild(vstengine::rack::state::serialize(
                    snapshotRackWithPatterns()), -1, nullptr);
                extra.addChild(captureEffectsState(), -1, nullptr);
                return extra;
            },
            [this](const juce::ValueTree& state) {
                const auto rackState = state.hasType("RACK")
                    ? state : state.getChildWithName("RACK");
                std::array<vstengine::rack::PersistentSlotState,
                           vstengine::instrument::maxSlots> parsed;
                std::string diagnostic;
                if (!vstengine::rack::state::deserialize(
                        rackState, parsed, diagnostic))
                    return false;
                vstengine::effects::EffectsChain candidateEffects;
                if (!prepareEffectsState(
                        state.hasType("RACK") ? juce::ValueTree {}
                            : state.getChildWithName("EFFECTS"),
                        candidateEffects))
                    return false;
                const auto previous = patternRuntime->slotSequences;
                suspendProcessing(true);
                if (!restoreSlotPatterns(parsed, true)) {
                    suspendProcessing(false);
                    return false;
                }
                if (rack.replaceState(parsed, nullptr, diagnostic)) {
                    for (std::size_t slot = 0; slot < parsed.size(); ++slot)
                        acidHostBridge.setSlotId(slot, parsed[slot].slotId);
                    effectsChain = std::move(candidateEffects);
                    for (auto& scheduler : patternRuntime->schedulers)
                        scheduler.reset(currentSampleRate);
                    suspendProcessing(false);
                    return true;
                }
                patternRuntime->slotSequences = previous;
                for (std::size_t slot = 0;
                     slot < patternRuntime->slotSequences.size(); ++slot)
                    publishPartSequenceForAudio(static_cast<int>(slot));
                suspendProcessing(false);
                return false;
            }
        });
}

void VstEngineAudioProcessor::prepareToPlay(const double sampleRate,
                                            const int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentMaximumBlockSize = static_cast<std::uint32_t>(
        juce::jmax(1, samplesPerBlock));
    (void) rack.prepare({ sampleRate,
        currentMaximumBlockSize,
        static_cast<std::uint32_t>(juce::jmax(1, getTotalNumOutputChannels())) });
    (void) effectsChain.prepare(sampleRate, currentMaximumBlockSize,
        static_cast<std::uint32_t>(juce::jmax(1, getTotalNumOutputChannels())));
    setLatencySamples(static_cast<int>(rack.latencySamples()));

    // Reset the generated-playback scheduler and reseed the deterministic
    // probability RNG from the rngSeed parameter so the same project state
    // always reproduces the same probability decisions from the same playback
    // start point.
    const auto globalSeed = static_cast<std::uint32_t>(
        apvts.getRawParameterValue("rngSeed")->load());
    for (std::size_t slot = 0; slot < patternRuntime->schedulers.size(); ++slot) {
        patternRuntime->schedulers[slot].reset(sampleRate);
        patternRuntime->schedulers[slot].reseedProbability(
            vstengine::instrument::generationSubSeed(
                globalSeed, rackControls[slot].slotId,
                rack.state()[slot].instrumentId));
    }

    // Preallocate the keyboard MIDI scratch buffer using JUCE's intended API.
    // Budget: 4096 bytes covers a dense block of keyboard events (256 events
    // at ~16 bytes/event sysex headroom) with safety margin. processBlock()
    // then only clears and reuses — no allocation or growth on the realtime
    // thread.
    keyboardScratch.ensureSize(32768);
    for (auto& scratch : patternRuntime->generatedScratch)
        scratch.ensureSize(65536);
    wasTransportPlaying = false;
}

void VstEngineAudioProcessor::releaseResources()
{
    rack.reset();
    effectsChain.reset();
    for (auto& scheduler : patternRuntime->schedulers)
        scheduler.reset(currentSampleRate);
    wasTransportPlaying = false;
}

bool VstEngineAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono()
        || output == juce::AudioChannelSet::stereo();
}

bool VstEngineAudioProcessor::applyInternalEffectsPreset(
    std::string_view presetId) noexcept
{
    suspendProcessing(true);
    const auto applied = effectsChain.applyPreset(presetId);
    suspendProcessing(false);
    return applied;
}

juce::ValueTree VstEngineAudioProcessor::captureEffectsState() const
{
    std::array<std::byte, vstengine::effects::EffectsChain::encodedStateBytes>
        payload {};
    std::uint32_t written {};
    if (!effectsChain.saveState(payload, written)
        || written != payload.size())
        return {};
    juce::MemoryBlock bytes(payload.data(), payload.size());
    juce::ValueTree state("EFFECTS");
    state.setProperty("schemaVersion",
        static_cast<int>(vstengine::effects::EffectsChain::stateVersion),
        nullptr);
    state.setProperty("payload", bytes.toBase64Encoding(), nullptr);
    return state;
}

bool VstEngineAudioProcessor::prepareEffectsState(
    const juce::ValueTree& state,
    vstengine::effects::EffectsChain& destination) const
{
    if (!destination.prepare(currentSampleRate, currentMaximumBlockSize,
            static_cast<std::uint32_t>(
                juce::jmax(1, getTotalNumOutputChannels()))))
        return false;
    if (!state.isValid()) return true; // Legacy state predates internal FX.
    if (!state.hasType("EFFECTS")) return false;
    const auto schema = static_cast<std::uint32_t>(
        static_cast<int>(state.getProperty("schemaVersion", 0)));
    juce::MemoryBlock bytes;
    if (!bytes.fromBase64Encoding(state.getProperty("payload").toString())
        || bytes.getSize() != vstengine::effects::EffectsChain::encodedStateBytes)
        return false;
    return destination.loadState(schema,
        { static_cast<const std::byte*>(bytes.getData()), bytes.getSize() });
}

void VstEngineAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midi)
{
    const auto processStart = juce::Time::getHighResolutionTicks();
    if (!midi.isEmpty()) {
        midiActivity.store(12u, std::memory_order_relaxed);
    } else {
        const auto remaining = midiActivity.load(std::memory_order_relaxed);
        if (remaining != 0u)
            midiActivity.store(remaining - 1u, std::memory_order_relaxed);
    }
#if 0 // Fixed-Part implementation retained in history; generic Rack path below.
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int previousBassChannel = audioParts[0].midiChannel;
    syncPartControlsFromParameters(audioParts);
    audioParts[1].enabled = false;
    if (audioParts[0].midiChannel != previousBassChannel) {
        synth.allNotesOff(0, false);
        bassDelay.reset();
        scheduler.reset(currentSampleRate);
    }
    (void) sequenceBridge.read(audioSequence);

    // Explicit transport safety path, separate from ordinary note-off.
    if (auto* hostPlayHead = getPlayHead()) {
        if (const auto position = hostPlayHead->getPosition()) {
            const bool playing = position->getIsPlaying();
            if (wasTransportPlaying && !playing) {
                synth.allNotesOff(0, false);
                bassDelay.reset();
                scheduler.reset(currentSampleRate);
            }
            wasTransportPlaying = playing;
        }
    }

    // Invalidate glide requests from the previous block before new ones are
    // scheduled (same thread, same block ordering — no synchronization needed).
    clearVoiceGlideRequests();

    bassKeyboardScratch.clear();
    bassKeyboard.processNextMidiBuffer(
        bassKeyboardScratch, 0, buffer.getNumSamples(), true);
    auto trackAudition = [](const juce::MidiBuffer& events,
                            std::array<bool, 128>& notes) {
        for (const auto metadata : events) {
            const auto message = metadata.getMessage();
            if (message.isNoteOn(false))
                notes[static_cast<std::size_t>(message.getNoteNumber())] = true;
            else if (message.isNoteOff())
                notes[static_cast<std::size_t>(message.getNoteNumber())] = false;
            else if (message.isController()
                     && (message.getControllerNumber() == 120
                         || message.getControllerNumber() == 123))
                notes.fill(false);
        }
    };
    trackAudition(bassKeyboardScratch, bassAuditionNotes);

    // MIDI source isolation: the generator decision only sees EXTERNAL host
    // notes (note events present in this block's incoming buffer — computed
    // before anything is appended) and GUI keyboard notes. Internally
    // generated MIDI is never an input observation, so it can never suppress
    // generator playback in AUTO mode.
    const auto incomingHasNotes = containsNoteEvents(midi);
    const auto keyboardHasNotes = keyboardHasActiveNotes();
    const auto mode = currentMidiMode();
    const auto rootNote = juce::jlimit(
        0, 127,
        static_cast<int>(apvts.getRawParameterValue("rootNote")->load()));

    // GENERATOR mode drops incoming host MIDI (only generated material plays).
    // GUI keyboard notes intentionally still work in GENERATOR mode, matching
    // the documented PIANO ROLL / GENERATOR / BOTH semantics.
    if (mode == MidiSourceMode::generator)
        midi.clear();

    if (panicRequested.exchange(false)) {
        midi.clear();
        synth.allNotesOff(0, false);
        bassDelay.reset();
        scheduler.reset(currentSampleRate);
        bassKeyboard.reset();
        bassKeyboardScratch.clear();
        bassAuditionNotes.fill(false);
    }

    // Authoritative host routing happens before internal sources are added.
    partRouter.route(midi, audioParts, partMidiBuffers);
    for (const auto metadata : partMidiBuffers[0])
        bassKeyboard.processNextMidiEvent(metadata.getMessage());

    const bool useGenerator = vstengine::midi::shouldRunGenerator(
        mode, incomingHasNotes, keyboardHasNotes);

    // Generated material: the scheduler (libs/midi) owns the full playback
    // state machine — PPQ-aligned scheduling, the free-running fallback,
    // probability rolls, ratchet tails and flush semantics. The shell only
    // bridges host transport observations (playhead reads are host-specific)
    // and applies the scheduler's glide requests to the bass voices.
    if (useGenerator) {
        vstengine::midi::TransportFrame frame;
        frame.playing = true;
        frame.havePpq = false;
        frame.ppqAtBlockStart = 0.0;
        frame.bpm = 145.0;

        if (auto* playHeadProvider = getPlayHead()) {
            if (const auto position = playHeadProvider->getPosition()) {
                if (const auto hostBpm = position->getBpm())
                    frame.bpm = *hostBpm;
                frame.playing = position->getIsPlaying();
                if (const auto hostPpq = position->getPpqPosition()) {
                    frame.ppqAtBlockStart = *hostPpq;
                    frame.havePpq = true;
                }
            }
        }

        const auto globalSeed = static_cast<std::uint32_t>(
            apvts.getRawParameterValue("rngSeed")->load());
        scheduler.process(partMidiBuffers[0], audioSequence, frame,
                          buffer.getNumSamples(), currentSampleRate,
                          audioParts[0].midiChannel, rootNote,
                          globalSeed ^ 0xB455A11u);
        for (int requestIndex = 0;
             requestIndex < scheduler.numGlideRequests(); ++requestIndex) {
            const auto request = scheduler.glideRequest(requestIndex);
            requestVoiceGlide(request.noteNumber, request.glideSeconds);
        }
        scheduler.clearGlideRequests();
    } else {
        scheduler.flush(partMidiBuffers[0], 0);
    }

    // Audition sources carry an explicit destination, independent of channel.
    for (const auto metadata : bassKeyboardScratch)
        (void) partRouter.routeToPart(metadata.getMessage(),
                                     metadata.samplePosition,
                                     vstengine::parts::PartId::bass,
                                     audioParts, partMidiBuffers);
    auto& bassMidi = partMidiBuffers[0];

    bassDelay.process(bassMidi, bassDelayScratch, buffer.getNumSamples(), 0);
    bassMidi.swapWith(bassDelayScratch);

    syncVoiceParameters();

    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    bassAudioScratch.setSize(channels, samples, false, false, true);
    bassAudioScratch.clear();
    synth.renderNextBlock(bassAudioScratch, bassMidi, 0, samples);

    vstengine::parts::PartMixer::mix(
        audioParts, { &bassAudioScratch, nullptr }, buffer, samples);
#else
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    syncRackControlsFromParameters();
    for (std::size_t slot = 0; slot < patternRuntime->audioSequences.size(); ++slot)
        (void) patternRuntime->sequenceBridges[slot].read(
            patternRuntime->audioSequences[slot]);

    double bpm = 145.0;
    double ppq = 0.0;
    bool playing = true;
    bool havePpq = false;
    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition()) {
            playing = position->getIsPlaying();
            if (const auto value = position->getBpm()) bpm = *value;
            if (const auto value = position->getPpqPosition()) {
                ppq = *value;
                havePpq = true;
            }
        }
    latestHostBpm.store(bpm, std::memory_order_relaxed);
    if (wasTransportPlaying && !playing) {
        rack.reset();
        for (auto& scheduler : patternRuntime->schedulers)
            scheduler.reset(currentSampleRate);
    }
    wasTransportPlaying = playing;

    keyboardScratch.clear();
    keyboard.processNextMidiBuffer(keyboardScratch, 0, buffer.getNumSamples(), true);
    for (const auto metadata : keyboardScratch) {
        const auto message = metadata.getMessage();
        if (message.isNoteOn(false))
            auditionNotes[static_cast<std::size_t>(message.getNoteNumber())] = true;
        else if (message.isNoteOff())
            auditionNotes[static_cast<std::size_t>(message.getNoteNumber())] = false;
    }

    const bool inputNotes = containsNoteEvents(midi);
    const auto mode = currentMidiMode();
    for (auto& scratch : patternRuntime->generatedScratch) scratch.clear();
    if (mode == MidiSourceMode::generator) midi.clear();
    const bool panicThisBlock = panicRequested.exchange(false);
    if (panicThisBlock) {
        midi.clear();
        keyboard.reset();
        keyboardScratch.clear();
        auditionNotes.fill(false);
        for (auto& scheduler : patternRuntime->schedulers)
            scheduler.reset(currentSampleRate);
        rack.reset();
    }

    if (!panicThisBlock && vstengine::midi::shouldRunGenerator(
            mode, inputNotes, keyboardHasActiveNotes())) {
        vstengine::midi::TransportFrame frame;
        frame.playing = playing;
        frame.havePpq = havePpq;
        frame.ppqAtBlockStart = ppq;
        frame.bpm = bpm;
        const auto root = juce::jlimit(0, 127,
            static_cast<int>(apvts.getRawParameterValue("rootNote")->load()));
        const auto seed = static_cast<std::uint32_t>(
            apvts.getRawParameterValue("rngSeed")->load());
        for (std::size_t slot = 0; slot < patternRuntime->schedulers.size(); ++slot) {
            const auto* descriptor = rack.descriptor(slot);
            if (descriptor == nullptr
                || (descriptor->capabilities
                    & vstengine::instrument::Capability::sequence) == 0)
                continue;
            const auto slotSeed = vstengine::instrument::generationSubSeed(
                seed, rackControls[slot].slotId, descriptor->id);
            patternRuntime->schedulers[slot].process(
                patternRuntime->generatedScratch[slot],
                patternRuntime->audioSequences[slot],
                frame, buffer.getNumSamples(), currentSampleRate, 1, root,
                slotSeed);
            patternRuntime->schedulers[slot].clearGlideRequests();
        }
    } else {
        for (std::size_t slot = 0; slot < patternRuntime->schedulers.size(); ++slot)
            patternRuntime->schedulers[slot].flush(
                patternRuntime->generatedScratch[slot], 0);
    }

    const auto blockSamples = static_cast<std::uint32_t>(buffer.getNumSamples());
    const auto hostCount = convertMidi(midi, hostEvents, blockSamples);
    const auto auditionCount = convertMidi(keyboardScratch, auditionEvents,
                                           blockSamples);
    auto& generatedEvents = generatedRoutingScratch->events;
    auto& generatedStreams = generatedRoutingScratch->streams;
    for (std::size_t slot = 0; slot < generatedStreams.size(); ++slot) {
        const auto count = convertMidi(patternRuntime->generatedScratch[slot],
                                       generatedEvents[slot], blockSamples);
        generatedStreams[slot] = { rackControls[slot].slotId,
            { generatedEvents[slot].data(), count } };
    }
    std::array<float*, 2> outputs {
        buffer.getWritePointer(0),
        buffer.getNumChannels() > 1 ? buffer.getWritePointer(1)
                                    : buffer.getWritePointer(0)
    };
    const auto channels = static_cast<std::size_t>(buffer.getNumChannels());
    const auto selected = selectedSlot.load();
    rack.process({ outputs.data(), channels },
        static_cast<std::uint32_t>(buffer.getNumSamples()),
        { hostEvents.data(), hostCount }, bpm, ppq, playing,
        { auditionEvents.data(), auditionCount }, rackControls[selected].slotId,
        rackControls, generatedStreams);
    effectsChain.process({ outputs.data(), channels }, blockSamples);
    const auto elapsed = juce::Time::highResolutionTicksToSeconds(
        juce::Time::getHighResolutionTicks() - processStart);
    const auto budget = currentSampleRate > 0.0
        ? static_cast<double>(blockSamples) / currentSampleRate : 0.0;
    const auto measured = static_cast<float>(juce::jlimit(0.0, 4.0,
        budget > 0.0 ? elapsed / budget : 0.0));
    const auto previous = cpuLoad.load(std::memory_order_relaxed);
    cpuLoad.store(previous * 0.85f + measured * 0.15f,
                  std::memory_order_relaxed);
#endif
}

#if 0
void VstEngineAudioProcessor::syncVoiceParameters()
{
    const auto drive = apvts.getRawParameterValue("drive")->load();
    const auto release = apvts.getRawParameterValue("release")->load();
    const auto ampAttack = apvts.getRawParameterValue("ampAttack")->load();
    const auto ampDecay = apvts.getRawParameterValue("ampDecay")->load();
    const auto ampSustain = apvts.getRawParameterValue("ampSustain")->load();
    const auto pitchEnvAmount =
        apvts.getRawParameterValue("pitchEnvAmount")->load();
    const auto pitchEnvTime = apvts.getRawParameterValue("pitchEnvTime")->load();
    const auto pitchEnvCurve = apvts.getRawParameterValue("pitchEnvCurve")->load();
    const auto filterCutoff = apvts.getRawParameterValue("filterCutoff")->load();
    const auto filterResonance =
        apvts.getRawParameterValue("filterResonance")->load();
    const auto filterDrive = apvts.getRawParameterValue("filterDrive")->load();
    const auto keyTracking = apvts.getRawParameterValue("keyTracking")->load();
    const auto outputLevel = apvts.getRawParameterValue("outputLevel")->load();

    for (int i = 0; i < synth.getNumVoices(); ++i) {
        if (auto* voice =
                dynamic_cast<vstengine::bass::PsyBassVoice*>(synth.getVoice(i))) {
            voice->setDrive(drive);
            voice->setAmpRelease(release);
            voice->setAmpAttack(ampAttack);
            voice->setAmpDecay(ampDecay);
            voice->setAmpSustain(ampSustain);
            voice->setPitchEnvelopeAmount(pitchEnvAmount);
            voice->setPitchEnvelopeTime(pitchEnvTime);
            voice->setPitchEnvelopeCurve(pitchEnvCurve);
            voice->setFilterCutoff(filterCutoff);
            voice->setFilterResonance(filterResonance);
            voice->setFilterDrive(filterDrive);
            voice->setKeyTracking(keyTracking);
            voice->setOutputLevel(outputLevel);
        }
    }
}
#endif

bool VstEngineAudioProcessor::containsNoteEvents(
    const juce::MidiBuffer& midi) noexcept
{
    for (const auto metadata : midi) {
        const auto message = metadata.getMessage();
        if (message.isNoteOnOrOff())
            return true;
    }

    return false;
}

bool VstEngineAudioProcessor::keyboardHasActiveNotes() const noexcept
{
    for (std::size_t note = 0; note < auditionNotes.size(); ++note) {
        if (auditionNotes[note])
            return true;
    }

    return false;
}

void VstEngineAudioProcessor::cacheRackParameterPointers()
{
    for (std::size_t slot = 0; slot < rackParameterRefs.size(); ++slot) {
        auto& refs = rackParameterRefs[slot];
        auto get = [this, slot](std::string_view suffix) {
            const auto id = slotParameterId(slot, suffix);
            return apvts.getRawParameterValue(id.c_str());
        };
        refs.midiIn = get("MidiIn"); refs.layer = get("Layer");
        refs.keyLow = get("KeyLow"); refs.keyHigh = get("KeyHigh");
        refs.velocityLow = get("VelocityLow");
        refs.velocityHigh = get("VelocityHigh"); refs.transpose = get("Transpose");
        refs.enabled = get("Enabled"); refs.mute = get("Mute");
        refs.solo = get("Solo"); refs.locked = get("Lock");
        refs.level = get("Level"); refs.pan = get("Pan");
        for (std::size_t macro = 0; macro < refs.macros.size(); ++macro)
            refs.macros[macro] = apvts.getRawParameterValue(
                vstengine::instrument::hostparams::macroId(slot, macro).c_str());
    }
}

void VstEngineAudioProcessor::syncRackControlsFromParameters() noexcept
{
    readRackControlsFromParameters(rackControls);
}

void VstEngineAudioProcessor::readRackControlsFromParameters(
    std::span<vstengine::rack::ProcessSlotControls> destination) const noexcept
{
    if (destination.size() != rackParameterRefs.size()) return;
    for (std::size_t slot = 0; slot < rackParameterRefs.size(); ++slot) {
        const auto& refs = rackParameterRefs[slot];
        const int midiIn = juce::jlimit(0, 16,
            static_cast<int>(std::lround(refs.midiIn->load())));
        vstengine::rack::Routing routing;
        routing.mode = midiIn == 0 ? vstengine::rack::RouteMode::off
            : refs.layer->load() >= 0.5f ? vstengine::rack::RouteMode::layer
                                        : vstengine::rack::RouteMode::channel;
        routing.channel = static_cast<std::uint8_t>(juce::jmax(1, midiIn));
        routing.keyLow = static_cast<std::uint8_t>(refs.keyLow->load());
        routing.keyHigh = static_cast<std::uint8_t>(refs.keyHigh->load());
        routing.velocityLow = static_cast<std::uint8_t>(refs.velocityLow->load());
        routing.velocityHigh = static_cast<std::uint8_t>(refs.velocityHigh->load());
        routing.transpose = static_cast<std::int8_t>(refs.transpose->load());
        auto& controls = destination[slot];
        controls.routing = routing;
        controls.enabled = refs.enabled->load() >= 0.5f;
        controls.mute = refs.mute->load() >= 0.5f;
        controls.solo = refs.solo->load() >= 0.5f;
        controls.locked = refs.locked->load() >= 0.5f;
        controls.level = refs.level->load();
        controls.pan = refs.pan->load();
        for (std::size_t macro = 0; macro < refs.macros.size(); ++macro)
            controls.macros[macro] = refs.macros[macro]->load();
    }
}

std::size_t VstEngineAudioProcessor::convertMidi(
    const juce::MidiBuffer& input,
    std::array<VoxMidiEventV1,
        vstengine::rack::RackRouter::eventCapacity>& output,
    std::uint32_t sampleCount) noexcept
{
    std::size_t count = 0;
    for (const auto metadata : input) {
        const auto message = metadata.getMessage();
        if (count >= output.size() || message.getRawDataSize() > 3
            || metadata.samplePosition < 0
            || static_cast<std::uint32_t>(metadata.samplePosition) >= sampleCount)
            continue;
        auto& event = output[count++];
        event.sampleOffset = static_cast<std::uint32_t>(juce::jmax(
            0, metadata.samplePosition));
        event.size = static_cast<std::uint8_t>(message.getRawDataSize());
        std::fill(std::begin(event.data), std::end(event.data),
                  static_cast<std::uint8_t>(0));
        std::copy_n(message.getRawData(), event.size, event.data);
    }
    return count;
}

void VstEngineAudioProcessor::syncPartControlsFromParameters(
    vstengine::parts::PartRegistry& destination) noexcept
{
    auto sync = [this](vstengine::parts::Part& part, const char* channel,
                       const char* mute, const char* solo, const char* lock,
                       const char* level, const char* pan) {
        part.midiChannel = juce::jlimit(
            1, 16, static_cast<int>(apvts.getRawParameterValue(channel)->load()));
        part.mute = apvts.getRawParameterValue(mute)->load() >= 0.5f;
        part.solo = apvts.getRawParameterValue(solo)->load() >= 0.5f;
        part.locked = apvts.getRawParameterValue(lock)->load() >= 0.5f;
        part.level = juce::jlimit(0.0f, 2.0f,
                                  apvts.getRawParameterValue(level)->load());
        part.pan = juce::jlimit(-1.0f, 1.0f,
                                apvts.getRawParameterValue(pan)->load());
    };
    sync(destination[0], "midiChannel", "bassMute", "bassSolo", "bassLock",
         "bassLevel", "bassPan");
    // Legacy Kick Part remains persistent migration data only. It is never
    // enabled or routed in active Vox Electronic Engine runtime.
    destination[1].enabled = false;
}

bool VstEngineAudioProcessor::isPartLocked(const int partIndex) const noexcept
{
    return partIndex != 0
        || apvts.getRawParameterValue("bassLock")->load() >= 0.5f;
}

void VstEngineAudioProcessor::syncParametersFromParts()
{
    auto setPlain = [this](const char* id, float plain) {
        if (auto* parameter = apvts.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
    };
    auto sync = [&setPlain](const vstengine::parts::Part& part,
                            const char* channel, const char* mute,
                            const char* solo, const char* lock,
                            const char* level, const char* pan) {
        setPlain(channel, static_cast<float>(part.midiChannel));
        setPlain(mute, part.mute ? 1.0f : 0.0f);
        setPlain(solo, part.solo ? 1.0f : 0.0f);
        setPlain(lock, part.locked ? 1.0f : 0.0f);
        setPlain(level, part.level);
        setPlain(pan, part.pan);
    };
    sync(parts[0], "midiChannel", "bassMute", "bassSolo", "bassLock",
         "bassLevel", "bassPan");
    juce::ignoreUnused(sync);
}

void VstEngineAudioProcessor::publishPartSequenceForAudio(const int partIndex) noexcept
{
    if (partIndex >= 0
        && partIndex < static_cast<int>(patternRuntime->sequenceBridges.size()))
        patternRuntime->sequenceBridges[static_cast<std::size_t>(partIndex)].publish(
            patternRuntime->slotSequences[static_cast<std::size_t>(partIndex)]);
}

std::array<vstengine::rack::PersistentSlotState,
           vstengine::instrument::maxSlots>
VstEngineAudioProcessor::snapshotRackWithPatterns()
{
    auto snapshot = rack.snapshotState();
    std::array<vstengine::rack::ProcessSlotControls,
               vstengine::instrument::maxSlots> controls;
    for (std::size_t slot = 0; slot < snapshot.size(); ++slot)
        controls[slot] = vstengine::rack::processControls(snapshot[slot]);
    readRackControlsFromParameters(controls);
    for (std::size_t slot = 0; slot < snapshot.size(); ++slot) {
        snapshot[slot].routing = controls[slot].routing;
        snapshot[slot].enabled = controls[slot].enabled;
        snapshot[slot].mute = controls[slot].mute;
        snapshot[slot].solo = controls[slot].solo;
        snapshot[slot].locked = controls[slot].locked;
        snapshot[slot].level = controls[slot].level;
        snapshot[slot].pan = controls[slot].pan;
        snapshot[slot].macros = controls[slot].macros;
    }
    for (std::size_t slot = 0; slot < snapshot.size(); ++slot) {
        const auto* descriptor = rack.descriptor(slot);
        if (descriptor == nullptr
            || (descriptor->capabilities
                & vstengine::instrument::Capability::sequence) == 0)
            continue; // Preserve missing/incompatible module opaque pattern.
        snapshot[slot].patternSchemaVersion = VOX_PATTERN_SCHEMA_V1;
        (void) vstengine::sequence::encodePattern(
            vstengine::sequence::toPattern(patternRuntime->slotSequences[slot]),
            snapshot[slot].patternPayload);
    }
    return snapshot;
}

bool VstEngineAudioProcessor::restoreSlotPatterns(
    const std::array<vstengine::rack::PersistentSlotState,
                     vstengine::instrument::maxSlots>& state,
    const bool allowEmpty) noexcept
{
    auto candidate = patternRuntime->slotSequences;
    for (std::size_t slot = 0; slot < state.size(); ++slot) {
        if (state[slot].patternPayload.empty()) {
            if (!allowEmpty) return false;
            candidate[slot].clear();
            continue;
        }
        if (state[slot].patternSchemaVersion != VOX_PATTERN_SCHEMA_V1) {
            candidate[slot].clear();
            continue; // Rack preserves opaque bytes and marks slot unresolved.
        }
        VoxPatternV1 pattern {};
        if (!vstengine::sequence::decodePattern(state[slot].patternPayload,
                                                pattern)
            || !vstengine::sequence::fromPattern(pattern, candidate[slot]))
            return false;
    }
    patternRuntime->slotSequences = std::move(candidate);
    for (std::size_t slot = 0; slot < patternRuntime->slotSequences.size(); ++slot)
        publishPartSequenceForAudio(static_cast<int>(slot));
    return true;
}

VstEngineAudioProcessor::MidiSourceMode
VstEngineAudioProcessor::currentMidiMode() const noexcept
{
    const auto raw = apvts.getRawParameterValue("midiMode")->load();
    const auto index = juce::jlimit(0, 3, static_cast<int>(std::lround(raw)));
    return static_cast<MidiSourceMode>(index);
}

// addGeneratedMidi / addGeneratedMidiPpq / addGeneratedMidiFallback /
// flushGeneratedNoteState / triggerGeneratedNote moved verbatim into
// vstengine::midi::GeneratedNoteScheduler (libs/midi).

#if 0
void VstEngineAudioProcessor::requestVoiceGlide(const int noteNumber,
                                                const float glideSeconds) noexcept
{
    for (int i = 0; i < synth.getNumVoices(); ++i) {
        if (auto* voice =
                dynamic_cast<vstengine::bass::PsyBassVoice*>(synth.getVoice(i)))
            voice->requestGlide(noteNumber, glideSeconds);
    }
}

void VstEngineAudioProcessor::clearVoiceGlideRequests() noexcept
{
    for (int i = 0; i < synth.getNumVoices(); ++i) {
        if (auto* voice =
                dynamic_cast<vstengine::bass::PsyBassVoice*>(synth.getVoice(i)))
            voice->clearPendingGlide();
    }
}
#endif

juce::AudioProcessorValueTreeState::ParameterLayout
VstEngineAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "drive", 1 }, "Drive",
        juce::NormalisableRange<float> { 1.0f, 6.0f, 0.01f }, 1.8f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "release", 1 }, "Release",
        juce::NormalisableRange<float> { 0.005f, 0.250f, 0.001f, 0.5f },
        0.035f, "s"));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "midiMode", 1 },
        "MIDI Source",
        juce::StringArray { "Auto", "Piano Roll", "Generator", "Both" },
        0));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "midiChannel", 1 },
        "Bass MIDI Channel",
        1, 16, 1));

    for (auto parameter : {
             std::pair { "bassMute", "Bass Mute" },
             std::pair { "bassSolo", "Bass Solo" },
             std::pair { "bassLock", "Bass Lock" } })
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { parameter.first, 1 }, parameter.second, false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "bassLevel", 1 }, "Bass Part Level",
        juce::NormalisableRange<float> { 0.0f, 2.0f, 0.01f }, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "bassPan", 1 }, "Bass Part Pan",
        juce::NormalisableRange<float> { -1.0f, 1.0f, 0.01f }, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "rootNote", 1 },
        "Generator Root Note",
        24, 60, 36));

    // Seed for the deterministic probability RNG used by realtime playback.
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "rngSeed", 1 },
        "Generator Seed",
        0, 2147483647, 1234));

    // --- Resonant low-pass filter ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "filterCutoff", 1 }, "Filter Cutoff",
        juce::NormalisableRange<float> { 0.02f, 0.95f, 0.001f, 0.4f },
        0.55f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "filterResonance", 1 }, "Filter Resonance",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
        0.7f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "filterDrive", 1 }, "Filter Drive",
        juce::NormalisableRange<float> { 0.0f, 4.0f, 0.01f },
        1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "keyTracking", 1 }, "Key Tracking",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
        0.2f));

    // --- Per-parameter amp ADSR (release is the existing "release" knob) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "ampAttack", 1 }, "Amp Attack",
        juce::NormalisableRange<float> { 0.0005f, 0.100f, 0.0005f, 0.3f },
        0.001f, "s"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "ampDecay", 1 }, "Amp Decay",
        juce::NormalisableRange<float> { 0.005f, 1.0f, 0.001f, 0.4f },
        0.055f, "s"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "ampSustain", 1 }, "Amp Sustain",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
        0.72f));

    // --- Pitch envelope ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "pitchEnvAmount", 1 }, "Pitch Env Amount",
        juce::NormalisableRange<float> { 0.0f, 36.0f, 0.1f },
        12.0f, "st"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "pitchEnvTime", 1 }, "Pitch Env Time",
        juce::NormalisableRange<float> { 0.001f, 0.5f, 0.001f, 0.4f },
        0.018f, "s"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "pitchEnvCurve", 1 }, "Pitch Env Curve",
        juce::NormalisableRange<float> { 0.5f, 8.0f, 0.1f },
        2.0f));

    // --- Output gain compensation ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "outputLevel", 1 }, "Output Level",
        juce::NormalisableRange<float> { 0.0f, 2.0f, 0.01f, 0.6f },
        1.0f));

    // --- Kick engine (issue #11 PHASE 5, exact parameter set) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickPitchStart", 1 }, "Kick Pitch Start",
        juce::NormalisableRange<float> { 0.0f, 36.0f, 0.1f }, 12.0f, "st"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickPitchEnd", 1 }, "Kick Pitch End",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f, "st"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickPitchDecay", 1 }, "Kick Pitch Decay",
        juce::NormalisableRange<float> { 0.001f, 0.5f, 0.001f, 0.4f },
        0.03f, "s"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickPitchCurve", 1 }, "Kick Pitch Curve",
        juce::NormalisableRange<float> { 0.5f, 8.0f, 0.1f }, 2.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickBodyDecay", 1 }, "Kick Body Decay",
        juce::NormalisableRange<float> { 0.01f, 2.0f, 0.001f, 0.4f },
        0.16f, "s"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickTail", 1 }, "Kick Tail",
        juce::NormalisableRange<float> { 0.0f, 4.0f, 0.001f, 0.5f },
        0.3f, "s"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickClick", 1 }, "Kick Click",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickClickTone", 1 }, "Kick Click Tone",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickDrive", 1 }, "Kick Drive",
        juce::NormalisableRange<float> { 1.0f, 10.0f, 0.01f }, 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickClip", 1 }, "Kick Clip",
        juce::NormalisableRange<float> { 0.1f, 1.0f, 0.005f }, 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickTransient", 1 }, "Kick Transient",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickSub", 1 }, "Kick Sub",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 0.4f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickTune", 1 }, "Kick Tune",
        juce::NormalisableRange<float> { 24.0f, 48.0f, 1.0f }, 36.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickPhase", 1 }, "Kick Phase",
        juce::NormalisableRange<float> { 0.0f, 360.0f, 1.0f }, 0.0f, "deg"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickOutputLevel", 1 }, "Kick Output Level",
        juce::NormalisableRange<float> { 0.0f, 2.0f, 0.01f, 0.6f },
        1.0f));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "kickMidiChannel", 1 },
        "Kick MIDI Channel",
        1, 16, 2));

    for (auto parameter : {
             std::pair { "kickMute", "Kick Mute" },
             std::pair { "kickSolo", "Kick Solo" },
             std::pair { "kickLock", "Kick Lock" } })
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { parameter.first, 1 }, parameter.second, false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickLevel", 1 }, "Kick Part Level",
        juce::NormalisableRange<float> { 0.0f, 2.0f, 0.01f }, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "kickPan", 1 }, "Kick Part Pan",
        juce::NormalisableRange<float> { -1.0f, 1.0f, 0.01f }, 0.0f));

    // --- Kick/bass match (issue #11 PHASE 6) ---
    // User-visible bounded result of the match analysis: the bass's note
    // events are delayed by this many ms relative to the kick attack
    // (0 = no delay). Bound 16 ms by construction.
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "matchBassTimingOffsetMs", 1 },
        "Bass Timing Offset",
        0, 16, 0));

    // Stable host-facing automation bank. Module-specific parameters map to
    // these fixed IDs; loading another module never changes Cubase automation.
    juce::StringArray midiInputs { "Off" };
    for (int channel = 1; channel <= 16; ++channel)
        midiInputs.add("CH" + juce::String(channel));
    for (std::size_t slot = 0; slot < vstengine::instrument::maxSlots; ++slot) {
        const auto id = [slot](std::string_view suffix) {
            return juce::ParameterID {
                juce::String(slotParameterId(slot, suffix)), 1 };
        };
        const auto label = "Slot " + juce::String(static_cast<int>(slot + 1)) + " ";
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            id("MidiIn"), label + "MIDI In", midiInputs,
            slot < 2 ? static_cast<int>(slot + 1) : 0));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            id("Layer"), label + "Layer", false));
        params.push_back(std::make_unique<juce::AudioParameterInt>(
            id("KeyLow"), label + "Key Low", 0, 127, 0));
        params.push_back(std::make_unique<juce::AudioParameterInt>(
            id("KeyHigh"), label + "Key High", 0, 127, 127));
        params.push_back(std::make_unique<juce::AudioParameterInt>(
            id("VelocityLow"), label + "Velocity Low", 1, 127, 1));
        params.push_back(std::make_unique<juce::AudioParameterInt>(
            id("VelocityHigh"), label + "Velocity High", 1, 127, 127));
        params.push_back(std::make_unique<juce::AudioParameterInt>(
            id("Transpose"), label + "Transpose", -48, 48, 0));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            id("Enabled"), label + "Enabled", true));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            id("Mute"), label + "Mute", false));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            id("Solo"), label + "Solo", false));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            id("Lock"), label + "Lock", false));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            id("Level"), label + "Level",
            juce::NormalisableRange<float> { 0.0f, 2.0f, 0.001f }, 1.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            id("Pan"), label + "Pan",
            juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, 0.0f));
        for (std::size_t macro = 0;
             macro < vstengine::instrument::macrosPerSlot; ++macro)
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID {
                    vstengine::instrument::hostparams::macroId(slot, macro), 1 },
                vstengine::instrument::hostparams::macroName(slot, macro),
                juce::NormalisableRange<float> { 0.0f, 1.0f, 0.0001f }, 0.5f));
        // Acid-only additions: independent per-slot automatable parameters.
        // The first eight Acid controls are already stable Rack macros.
        const vstengine::acid::AcidEngine acidDefaults;
        for (std::size_t p = vstengine::instrument::macrosPerSlot;
             p < vstengine::acid::AcidEngine::parameterCount; ++p) {
            if (p == vstengine::acid::AcidEngine::playModeFlags) continue;
            const auto semantic = vstengine::acid::AcidEngine::parameterIds[p];
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID {
                    juce::String(vstengine::acid::hostParameterId(slot, semantic)), 1 },
                label + "Acid " + juce::String(std::string(semantic)),
                juce::NormalisableRange<float> { 0.0f, 1.0f, 0.0001f },
                acidDefaults.parameter(semantic)));
        }
        constexpr std::array<std::pair<std::string_view, bool>, 4> modes {{
            { "legato", true }, { "retrigger", false },
            { "vel_to_accent", true }, { "slide_on_overlap", true }
        }};
        for (const auto& [semantic, enabled] : modes)
            params.push_back(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID {
                    juce::String(vstengine::acid::hostParameterId(slot, semantic)), 1 },
                label + "Acid " + juce::String(std::string(semantic)), enabled));
    }

    return { params.begin(), params.end() };
}

bool VstEngineAudioProcessor::loadSlotInstrument(
    std::size_t slot, std::string_view instrumentId, juce::String& diagnostic)
{
    suspendProcessing(true);
    std::string detail;
    const bool accepted = rack.loadModule(slot, instrumentId, nullptr, detail);
    if (accepted) {
        patternRuntime->slotSequences[slot].clear();
        publishPartSequenceForAudio(static_cast<int>(slot));
        patternRuntime->schedulers[slot].reset(currentSampleRate);
    }
    suspendProcessing(false);
    diagnostic = detail;
    return accepted;
}

std::span<const vstengine::instrument::ContentDescriptor>
VstEngineAudioProcessor::selectedContent() const noexcept
{
    const auto index = selectedSlotIndex();
    const auto& state = rack.state()[index];
    const auto resolution = instrumentRegistry.resolve(state.instrumentId);
    return resolution && resolution.provider != nullptr
        ? resolution.provider->contentDescriptors(state.instrumentId)
        : std::span<const vstengine::instrument::ContentDescriptor> {};
}

std::uint32_t VstEngineAudioProcessor::selectedSequenceFieldMask() const noexcept
{
    std::uint32_t fields {};
    for (const auto& content : selectedContent())
        fields |= content.supportedSequenceFields;
    // Legacy built-in Bass predates content descriptors but supports full
    // canonical Sequence model.
    return fields == 0 ? VOX_SEQUENCE_ALL : fields & VOX_SEQUENCE_ALL;
}

bool VstEngineAudioProcessor::applySelectedSoundPreset(
    std::string_view presetId, juce::String& diagnostic)
{
    const auto index = selectedSlotIndex();
    suspendProcessing(true);
    std::string detail;
    const bool accepted = rack.applySoundPreset(index, presetId, nullptr, detail);
    if (accepted) {
        const auto& values = rack.state()[index].macros;
        for (std::size_t macro = 0; macro < values.size(); ++macro)
            if (auto* parameter = apvts.getParameter(
                    vstengine::instrument::hostparams::macroId(index, macro).c_str()))
                parameter->setValueNotifyingHost(values[macro]);
        if (rack.state()[index].instrumentId == vstengine::acid::instrumentId) {
            // Factory Acid presets are v3 state snapshots with the original
            // nine sound controls. Reset host-side extended defaults as well:
            // otherwise the next process block would replay stale automation
            // values over the just-applied preset sound.
            const vstengine::acid::AcidEngine defaults;
            for (std::size_t p = vstengine::instrument::macrosPerSlot;
                 p < vstengine::acid::AcidEngine::parameterCount; ++p) {
                if (p == vstengine::acid::AcidEngine::playModeFlags) continue;
                const auto id = vstengine::acid::hostParameterId(
                    index, vstengine::acid::AcidEngine::parameterIds[p]);
                if (auto* param = apvts.getParameter(id.c_str())) {
                    float value = defaults.parameter(
                        vstengine::acid::AcidEngine::parameterIds[p]);
                    (void) vstengine::acid::factorySoundParameterValue(
                        presetId, vstengine::acid::AcidEngine::parameterIds[p],
                        value);
                    param->setValueNotifyingHost(param->convertTo0to1(value));
                }
            }
            constexpr std::array<std::pair<std::string_view, float>, 4> flags {{
                { "legato", 1.0f }, { "retrigger", 0.0f },
                { "vel_to_accent", 1.0f }, { "slide_on_overlap", 1.0f }
            }};
            for (const auto& [name, value] : flags) {
                const auto id = vstengine::acid::hostParameterId(index, name);
                if (auto* param = apvts.getParameter(id.c_str()))
                    param->setValueNotifyingHost(value);
            }
        }
    }
    suspendProcessing(false);
    diagnostic = detail;
    return accepted;
}

bool VstEngineAudioProcessor::generateSelectedPattern(
    std::string_view profileId, juce::String& diagnostic)
{
    const auto index = selectedSlotIndex();
    if (rackParameterRefs[index].locked->load() >= 0.5f) {
        diagnostic = "Slot locked";
        return false;
    }
    vstengine::sequence::Sequence candidate;
    std::vector<std::byte> encoded;
    if (!prepareGeneratedPattern(index, profileId, nullptr, 0.0f, -1, -1,
                                 candidate, encoded, diagnostic))
        return false;
    suspendProcessing(true);
    if (!rack.updatePatternState(index, std::string(profileId),
                                 VOX_PATTERN_SCHEMA_V1,
                                 std::move(encoded))) {
        suspendProcessing(false);
        diagnostic = "pattern state commit failed";
        return false;
    }
    patternRuntime->slotSequences[index] = std::move(candidate);
    publishPartSequenceForAudio(static_cast<int>(index));
    patternRuntime->schedulers[index].reset(currentSampleRate);
    suspendProcessing(false);
    diagnostic.clear();
    return true;
}

std::string VstEngineAudioProcessor::generatorProfileForSlot(
    std::size_t index) const
{
    if (index >= vstengine::instrument::maxSlots) return {};
    const auto& slot = rack.state()[index];
    const auto resolution = instrumentRegistry.resolve(slot.instrumentId);
    if (!resolution || resolution.provider == nullptr) return {};
    std::string first;
    for (const auto& content :
         resolution.provider->contentDescriptors(slot.instrumentId)) {
        if (content.kind != vstengine::instrument::ContentKind::generatorProfile)
            continue;
        if (first.empty()) first = content.id;
        if (!slot.patternPreset.empty()
            && content.id == slot.patternPreset)
            return content.id;
    }
    return first;
}

bool VstEngineAudioProcessor::prepareGeneratedPattern(
    std::size_t index, std::string_view profileId,
    const VoxPatternV1* input, float mutationAmount,
    int selectedStart, int selectedEnd,
    vstengine::sequence::Sequence& candidate,
    std::vector<std::byte>& encoded, juce::String& diagnostic) const
{
    if (index >= vstengine::instrument::maxSlots || profileId.empty()) {
        diagnostic = "No generator profile";
        return false;
    }
    const auto& slot = rack.state()[index];
    const auto resolution = instrumentRegistry.resolve(slot.instrumentId);
    if (!resolution || resolution.provider == nullptr) {
        diagnostic = resolution.diagnostic;
        return false;
    }
    VoxGenerationContextV1 context {};
    context.structSize = sizeof(context);
    context.globalSeed = static_cast<std::uint32_t>(
        apvts.getRawParameterValue("rngSeed")->load());
    if (input != nullptr)
        context.globalSeed ^= vstengine::sequence::patternFingerprint(*input)
            ^ 0x9e3779b9u;
    context.slotId = slot.slotId;
    std::snprintf(context.instrumentId.bytes, sizeof(context.instrumentId.bytes),
                  "%s", slot.instrumentId.c_str());
    std::snprintf(context.styleId.bytes, sizeof(context.styleId.bytes), "%.*s",
                  static_cast<int>(profileId.size()), profileId.data());
    context.rootNote = static_cast<std::int32_t>(
        apvts.getRawParameterValue("rootNote")->load());
    context.bpm = latestHostBpm.load(std::memory_order_relaxed);
    context.mutation = mutationAmount;
    VoxPatternV1 pattern { sizeof(VoxPatternV1) };
    const auto status = resolution.provider->generatePattern(
        slot.instrumentId, profileId, context, input, pattern);
    if (status != vstengine::instrument::ContentStatus::ok
        || (input != nullptr
            && !vstengine::sequence::mergePatternMutation(
                *input, pattern, mutationAmount, selectedStart, selectedEnd,
                pattern))
        || !vstengine::sequence::fromPattern(pattern, candidate)
        || !vstengine::sequence::encodePattern(pattern, encoded)) {
        diagnostic = "pattern generation failed";
        return false;
    }
    diagnostic.clear();
    return true;
}

bool VstEngineAudioProcessor::mutateSelectedPattern(
    bool selectedStepsOnly, juce::String& diagnostic)
{
    const auto index = selectedSlotIndex();
    if (rackParameterRefs[index].locked->load() >= 0.5f) {
        diagnostic = "Slot locked";
        return false;
    }
    const auto profile = generatorProfileForSlot(index);
    const auto input = vstengine::sequence::toPattern(
        patternRuntime->slotSequences[index]);
    const auto& current = patternRuntime->slotSequences[index];
    const int start = selectedStepsOnly && current.hasSelection()
        ? current.getSelectedStart() : -1;
    const int end = selectedStepsOnly && current.hasSelection()
        ? current.getSelectedEnd() : -1;
    vstengine::sequence::Sequence candidate;
    std::vector<std::byte> encoded;
    if (!prepareGeneratedPattern(index, profile, &input, 0.35f, start, end,
                                 candidate, encoded, diagnostic))
        return false;
    if (start >= 0) candidate.setSelectedRange(start, end);
    suspendProcessing(true);
    const auto committed = rack.updatePatternState(index, profile,
        VOX_PATTERN_SCHEMA_V1, std::move(encoded));
    if (committed) {
        patternRuntime->slotSequences[index] = std::move(candidate);
        publishPartSequenceForAudio(static_cast<int>(index));
        patternRuntime->schedulers[index].reset(currentSampleRate);
    }
    suspendProcessing(false);
    diagnostic = committed ? juce::String {} : "pattern state commit failed";
    return committed;
}

bool VstEngineAudioProcessor::runGlobalPatternOperation(
    bool mutate, juce::String& diagnostic)
{
    std::array<vstengine::sequence::Sequence,
               vstengine::instrument::maxSlots> candidates;
    std::array<std::vector<std::byte>,
               vstengine::instrument::maxSlots> encoded;
    std::array<std::string, vstengine::instrument::maxSlots> profiles;
    std::array<bool, vstengine::instrument::maxSlots> changed {};
    std::size_t changedCount {};
    for (std::size_t index = 0; index < changed.size(); ++index) {
        if (rackParameterRefs[index].locked->load() >= 0.5f) continue;
        const auto* descriptor = rack.descriptor(index);
        if (descriptor == nullptr
            || (descriptor->capabilities
                & vstengine::instrument::Capability::patternGenerator) == 0)
            continue;
        profiles[index] = generatorProfileForSlot(index);
        const auto input = vstengine::sequence::toPattern(
            patternRuntime->slotSequences[index]);
        if (!prepareGeneratedPattern(index, profiles[index],
                mutate ? &input : nullptr, mutate ? 0.35f : 0.0f, -1, -1,
                candidates[index], encoded[index], diagnostic))
            return false;
        changed[index] = true;
        ++changedCount;
    }
    if (changedCount == 0) {
        diagnostic = "No unlocked generator slots";
        return false;
    }
    suspendProcessing(true);
    for (std::size_t index = 0; index < changed.size(); ++index) {
        if (!changed[index]) continue;
        if (!rack.updatePatternState(index, profiles[index],
                VOX_PATTERN_SCHEMA_V1, std::move(encoded[index]))) {
            suspendProcessing(false);
            diagnostic = "global pattern commit failed";
            return false;
        }
        patternRuntime->slotSequences[index] = std::move(candidates[index]);
        publishPartSequenceForAudio(static_cast<int>(index));
        patternRuntime->schedulers[index].reset(currentSampleRate);
    }
    suspendProcessing(false);
    diagnostic = juce::String(static_cast<int>(changedCount))
        + (mutate ? " slots mutated" : " slots generated");
    return true;
}

bool VstEngineAudioProcessor::generateAllPatterns(juce::String& diagnostic)
{
    return runGlobalPatternOperation(false, diagnostic);
}

bool VstEngineAudioProcessor::mutateAllPatterns(juce::String& diagnostic)
{
    return runGlobalPatternOperation(true, diagnostic);
}

int VstEngineAudioProcessor::nextFreeChannel() const noexcept
{
    std::array<bool, 17> used {};
    for (const auto& refs : rackParameterRefs) {
        const int channel = juce::jlimit(0, 16,
            static_cast<int>(std::lround(refs.midiIn->load())));
        if (channel > 0) used[static_cast<std::size_t>(channel)] = true;
    }
    for (int channel = 1; channel <= 16; ++channel)
        if (!used[static_cast<std::size_t>(channel)]) return channel;
    return 0;
}

bool VstEngineAudioProcessor::assignSlotChannel(
    std::size_t slot, int channel, ChannelConflictAction action,
    juce::String& diagnostic)
{
    if (slot >= rackParameterRefs.size() || channel < 0 || channel > 16) {
        diagnostic = "Invalid slot/channel";
        return false;
    }
    auto setPlain = [this](const std::string& id, float value) {
        if (auto* parameter = apvts.getParameter(id.c_str())) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            parameter->endChangeGesture();
        }
    };
    std::size_t conflict = rackParameterRefs.size();
    if (channel > 0)
        for (std::size_t i = 0; i < rackParameterRefs.size(); ++i)
            if (i != slot && rackParameterRefs[i].enabled->load() >= 0.5f
                && rackParameterRefs[i].layer->load() < 0.5f
                && static_cast<int>(std::lround(
                    rackParameterRefs[i].midiIn->load())) == channel) {
                conflict = i;
                break;
            }
    if (conflict < rackParameterRefs.size()) {
        if (action == ChannelConflictAction::reject) {
            diagnostic = "Channel occupied; choose SWAP, MOVE, or LAYER";
            return false;
        }
        if (action == ChannelConflictAction::swap) {
            const float previous = rackParameterRefs[slot].midiIn->load();
            setPlain(slotParameterId(conflict, "MidiIn"), previous);
        } else if (action == ChannelConflictAction::move) {
            setPlain(slotParameterId(conflict, "MidiIn"), 0.0f);
        } else if (action == ChannelConflictAction::layer) {
            setPlain(slotParameterId(slot, "Layer"), 1.0f);
        }
    } else if (action != ChannelConflictAction::layer) {
        setPlain(slotParameterId(slot, "Layer"), 0.0f);
    }
    setPlain(slotParameterId(slot, "MidiIn"), static_cast<float>(channel));
    diagnostic.clear();
    return true;
}


juce::File VstEngineAudioProcessor::createPartMidiFile(const int partIndex)
{
    // Export goes through the shared MIDI export builder so timing mode,
    // ratchet and slide behave exactly as documented and identically to
    // realtime playback.
    vstengine::midiexport::Options options;
    if (partIndex != 0)
        return {};
    constexpr std::size_t index = 0;
    const auto& slot = rack.state()[index];
    const int inputChannel = static_cast<int>(std::lround(
        rackParameterRefs[index].midiIn->load()));
    options.channel = juce::jlimit(1, 16, inputChannel);
    options.rootNote = juce::jlimit(0, 127, static_cast<int>(
        apvts.getRawParameterValue("rootNote")->load()));
    options.seed = vstengine::instrument::generationSubSeed(
        static_cast<std::uint32_t>(apvts.getRawParameterValue("rngSeed")->load()),
        slot.slotId, slot.instrumentId);

    const auto exportSequence =
        patternRuntime->slotSequences[index];
    const auto track =
        vstengine::midiexport::buildSequenceTrack(exportSequence, options);

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(
        static_cast<int>(options.ticksPerQuarter));
    midiFile.addTrack(track);

    const auto tempFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("Vox-Trance-Engine-Bass.mid");

    tempFile.deleteFile();

    if (auto stream = tempFile.createOutputStream()) {
        if (midiFile.writeTo(*stream))
            return tempFile;
    }

    return {};
}

void VstEngineAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto tree = apvts.copyState();
    for (;;) {
        const auto stale = tree.getChildWithName("PARTS");
        if (!stale.isValid())
            break;
        tree.removeChild(stale, nullptr);
    }
    for (;;) {
        const auto stale = tree.getChildWithName("RACK");
        if (!stale.isValid()) break;
        tree.removeChild(stale, nullptr);
    }
    for (;;) {
        const auto stale = tree.getChildWithName("EFFECTS");
        if (!stale.isValid()) break;
        tree.removeChild(stale, nullptr);
    }
    tree.setProperty("stateSchemaVersion", 4, nullptr);
    tree.addChild(vstengine::rack::state::serialize(
        snapshotRackWithPatterns()), -1, nullptr);
    tree.addChild(captureEffectsState(), -1, nullptr);
    tree.removeProperty("sequenceData", nullptr);
    std::unique_ptr<juce::XmlElement> xml(tree.createXml());
    copyXmlToBinary(*xml, destData);
}

void VstEngineAudioProcessor::setStateInformation(const void* data,
                                                    const int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto tree = juce::ValueTree::fromXml(*xml);
        const auto rackTree = tree.getChildWithName("RACK");
        const auto effectsTree = tree.getChildWithName("EFFECTS");
        vstengine::effects::EffectsChain candidateEffects;
        if (!prepareEffectsState(effectsTree, candidateEffects)) return;
        std::array<vstengine::rack::PersistentSlotState,
                   vstengine::instrument::maxSlots> parsedRack;
        std::string rackDiagnostic;
        if (rackTree.isValid()) {
            if (!vstengine::rack::state::deserialize(
                    rackTree, parsedRack, rackDiagnostic))
                return;
        }
        const auto previousSequences = patternRuntime->slotSequences;
        suspendProcessing(true);
        if (rackTree.isValid() && !restoreSlotPatterns(parsedRack, true)) {
            suspendProcessing(false);
            return;
        }
        const auto partState = tree.getChildWithName("PARTS");
        const bool restoredParts =
            vstengine::parts::PartState::restore(partState, parts);
        auto apvtsState = tree.createCopy();
        for (;;) {
            const auto extra = apvtsState.getChildWithName("PARTS");
            if (!extra.isValid())
                break;
            apvtsState.removeChild(extra, nullptr);
        }
        for (;;) {
            const auto extra = apvtsState.getChildWithName("RACK");
            if (!extra.isValid()) break;
            apvtsState.removeChild(extra, nullptr);
        }
        for (;;) {
            const auto extra = apvtsState.getChildWithName("EFFECTS");
            if (!extra.isValid()) break;
            apvtsState.removeChild(extra, nullptr);
        }
        const auto previousApvts = apvts.copyState();
        apvts.replaceState(apvtsState);
        if (rackTree.isValid()
            && !rack.replaceState(parsedRack, nullptr, rackDiagnostic)) {
            apvts.replaceState(previousApvts);
            patternRuntime->slotSequences = previousSequences;
            for (std::size_t slot = 0;
                 slot < patternRuntime->slotSequences.size(); ++slot)
                publishPartSequenceForAudio(static_cast<int>(slot));
            suspendProcessing(false);
            return;
        }
        if (rackTree.isValid())
            for (std::size_t slot = 0; slot < rackControls.size(); ++slot) {
                rackControls[slot].slotId = parsedRack[slot].slotId;
                acidHostBridge.setSlotId(slot, parsedRack[slot].slotId);
            }
        effectsChain = std::move(candidateEffects);

        if (!rackTree.isValid() && restoredParts) {
            // Keep old Kick data readable, but never reactivate removed product
            // content. Map it to explicit unresolved Rack state.
            parts[1].enabled = false;
            syncParametersFromParts();
            auto migrated = rack.state();
            migrated[0].instrumentId = vstengine::modules::bassInstrumentId;
            migrated[0].routing.mode = vstengine::rack::RouteMode::channel;
            migrated[0].routing.channel = static_cast<std::uint8_t>(parts[0].midiChannel);
            migrated[0].mute = parts[0].mute; migrated[0].solo = parts[0].solo;
            migrated[0].locked = parts[0].locked; migrated[0].level = parts[0].level;
            migrated[0].pan = parts[0].pan;
            migrated[1].instrumentId = "com.ultimavox.legacy-kick";
            migrated[1].routing.mode = vstengine::rack::RouteMode::off;
            migrated[1].routing.channel = static_cast<std::uint8_t>(parts[1].midiChannel);
            migrated[1].enabled = false;
            if (auto legacyXml = partState.createXml()) {
                const auto text = legacyXml->toString().toStdString();
                migrated[1].modulePayload.resize(text.size());
                std::memcpy(migrated[1].modulePayload.data(), text.data(), text.size());
            }
            std::string diagnostic;
            (void) rack.replaceState(migrated, nullptr, diagnostic);
            for (std::size_t slot = 0; slot < rackControls.size(); ++slot)
                rackControls[slot].slotId = migrated[slot].slotId;
            auto setPlain = [this](const std::string& id, float value) {
                if (auto* parameter = apvts.getParameter(id.c_str()))
                    parameter->setValueNotifyingHost(
                        parameter->convertTo0to1(value));
            };
            setPlain(slotParameterId(0, "MidiIn"),
                     static_cast<float>(parts[0].midiChannel));
            setPlain(slotParameterId(0, "Mute"), parts[0].mute ? 1.0f : 0.0f);
            setPlain(slotParameterId(0, "Solo"), parts[0].solo ? 1.0f : 0.0f);
            setPlain(slotParameterId(0, "Lock"), parts[0].locked ? 1.0f : 0.0f);
            setPlain(slotParameterId(0, "Level"), parts[0].level);
            setPlain(slotParameterId(0, "Pan"), parts[0].pan);
            setPlain(slotParameterId(1, "MidiIn"), 0.0f);
            setPlain(slotParameterId(1, "Enabled"), 0.0f);
            const std::array<float, 8> migratedMacros {
                apvts.getRawParameterValue("filterCutoff")->load(),
                apvts.getRawParameterValue("filterResonance")->load(),
                apvts.getRawParameterValue("pitchEnvAmount")->load() / 36.0f,
                juce::jlimit(0.0f, 1.0f,
                    (apvts.getRawParameterValue("ampDecay")->load() - 0.015f)
                    / 0.18f),
                juce::jlimit(0.0f, 1.0f,
                    (apvts.getRawParameterValue("drive")->load() - 1.0f) / 5.0f),
                apvts.getRawParameterValue("ampSustain")->load(), 0.0f,
                apvts.getRawParameterValue("keyTracking")->load()
            };
            for (std::size_t macro = 0; macro < migratedMacros.size(); ++macro)
                setPlain(vstengine::instrument::hostparams::macroId(0, macro),
                         migratedMacros[macro]);
        } else if (!rackTree.isValid()) {
            // PR #18 migration: APVTS routing + legacy Sequence -> Bass Part;
            // Kick keeps canonical defaults and independent empty sequence.
            syncPartControlsFromParameters(parts);
        }
        if (!rackTree.isValid()
            || static_cast<int>(rackTree.getProperty("schemaVersion", 1)) == 1) {
            const auto seqBase64 = tree.getProperty("sequenceData").toString();
            juce::MemoryBlock sequenceData;
            if (sequenceData.fromBase64Encoding(seqBase64)
                && vstengine::sequence::Sequence::isValidSerialization(sequenceData))
                patternRuntime->slotSequences[0] =
                    vstengine::sequence::Sequence::deserialize(sequenceData);
        }
    }
    for (std::size_t slot = 0; slot < patternRuntime->slotSequences.size(); ++slot)
        publishPartSequenceForAudio(static_cast<int>(slot));
    for (auto& scheduler : patternRuntime->schedulers)
        scheduler.reset(currentSampleRate);
    suspendProcessing(false);
}

void VstEngineAudioProcessor::seedInitialSequence()
{
    for (auto& sequence : patternRuntime->slotSequences) sequence.clear();
    patternRuntime->slotSequences[0].regenerateBySeed(0xD4A4u);
    const auto acid = instrumentRegistry.resolve(
        vstengine::modules::acidInstrumentId);
    if (acid && acid.provider != nullptr) {
        VoxGenerationContextV1 context {};
        context.structSize = sizeof(context);
        context.globalSeed = 1234;
        context.slotId = rack.state()[1].slotId;
        std::snprintf(context.instrumentId.bytes,
                      sizeof(context.instrumentId.bytes), "%s",
                      vstengine::modules::acidInstrumentId.data());
        VoxPatternV1 pattern { sizeof(VoxPatternV1) };
        if (acid.provider->generatePattern(vstengine::modules::acidInstrumentId,
                "psy-acid", context, nullptr, pattern)
                == vstengine::instrument::ContentStatus::ok)
            (void) vstengine::sequence::fromPattern(
                pattern, patternRuntime->slotSequences[1]);
    }
    parts[1].enabled = false;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VstEngineAudioProcessor();
}
