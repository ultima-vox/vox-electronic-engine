#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/ResourceLimits.h"
#include "instrument/HostParameterSchema.h"
#include "midi/GeneratedNoteScheduler.h"
#include "midi/MidiExport.h"
#include "modules/BuiltInProvider.h"
#include "preset/PresetManager.h"
#include "sequence/PatternAdapter.h"
#include <charconv>
#include <utility>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>

namespace {
bool hasVersion(const juce::ValueTree &tree, const char *property,
                unsigned expected) {
  const auto text = tree.getProperty(property).toString().toStdString();
  unsigned version{};
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), version);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size() &&
         version == expected;
}
std::string slotParameterId(std::size_t, std::string_view);
// Project values are plain host units, not normalized automation values.
// Pool endpoints (slotNN.pKKK) only mirror the Rack's named values, which are
// validated strictly against the bound descriptor and win on restore; the
// mirror itself only has to be finite.
bool validateProjectParameters(
    const juce::ValueTree &tree, juce::AudioProcessorValueTreeState &parameters) {
  if (tree.getNumChildren() < 3 || tree.getNumChildren() > 20000)
    return false;
  std::unordered_set<std::string> ids;
  unsigned racks{}, effects{};
  for (const auto child : tree) {
    if (child.hasType("RACK")) {
      if (++racks > 1)
        return false;
      continue;
    }
    if (child.hasType("EFFECTS")) {
      if (++effects > 1)
        return false;
      continue;
    }
    if (!child.hasType("PARAM") || child.getNumChildren() != 0 ||
        !child.hasProperty("id") || !child.hasProperty("value"))
      return false;
    const auto id = child.getProperty("id").toString().toStdString();
    if (id.empty() || id.size() > 512 || !ids.insert(id).second)
      return false;
    const auto scalar = child.getProperty("value");
    if (!(scalar.isDouble() || scalar.isInt() || scalar.isInt64() ||
          scalar.isString()))
      return false;
    const auto text = scalar.toString().toStdString();
    double value{};
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        !std::isfinite(value))
      return false;
    if (const auto *endpoint = parameters.getParameter(id.c_str());
        endpoint && !vstengine::instrument::hostparams::isPoolId(id)) {
      const auto &range = endpoint->getNormalisableRange();
      if (value < range.start || value > range.end)
        return false;
      if (endpoint->isDiscrete() && value != std::round(value))
        return false;
    }
  }
  // Every endpoint of the fixed host layout is present (no legacy projects).
  for (const auto *parameter : parameters.processor.getParameters()) {
    const auto *identified =
        dynamic_cast<const juce::AudioProcessorParameterWithID *>(parameter);
    if (identified == nullptr)
      return false;
    const auto name = identified->paramID.toStdString();
    if (!ids.contains(name))
      return false;
  }
  return racks == 1 && effects == 1;
}
std::string slotParameterId(std::size_t slot, std::string_view suffix)
{
    char prefix[16] {};
    std::snprintf(prefix, sizeof(prefix), "slot%02zu", slot + 1);
    return std::string(prefix) + std::string(suffix);
}
} // namespace

vstengine::instrument::InstrumentRegistry VstEngineAudioProcessor::makeRegistry(
    std::unique_ptr<vstengine::instrument::InstrumentProvider> provider) {
  vstengine::instrument::HostBudgetLimits limits;
  // All shipped synths/returns are zero-latency. Reject a future latent module
  // explicitly until parallel-path compensation is implemented and reviewed.
  limits.maxLatencySamples = 0;
  vstengine::instrument::InstrumentRegistry registry(limits);
  std::string diagnostic;
  if (!registry.registerProvider(std::move(provider), diagnostic))
    throw std::invalid_argument(diagnostic);
  return registry;
}

VstEngineAudioProcessor::VstEngineAudioProcessor()
    : VstEngineAudioProcessor(vstengine::modules::createBuiltInProvider()) {}

VstEngineAudioProcessor::VstEngineAudioProcessor(
    std::unique_ptr<vstengine::instrument::InstrumentProvider> provider)
    : AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      instrumentRegistry(makeRegistry(std::move(provider))),
      apvts(*this, nullptr, "PARAMETERS",
            createParameterLayout(instrumentRegistry.descriptors())),
      rack(instrumentRegistry), presetStore(apvts) {
  std::string diagnostic;
  auto initialRack = rack.state();
  if (instrumentRegistry.resolve(vstengine::modules::bassInstrumentId)) {
    initialRack[0].instrumentId = vstengine::modules::bassInstrumentId;
    initialRack[0].routing = { vstengine::rack::RouteMode::channel, 1,
                               0, 127, 1, 127, 0 };
    initialRack[0].macros = { 0.48f, 0.55f, 0.4f, 0.25f,
                              0.32f, 0.5f, 0.0f, 0.15f };
  }
  if (instrumentRegistry.resolve(vstengine::modules::acidInstrumentId)) {
    initialRack[1].instrumentId = vstengine::modules::acidInstrumentId;
    initialRack[1].routing = { vstengine::rack::RouteMode::channel, 2,
                               0, 127, 1, 127, 0 };
    initialRack[1].macros = { 0.05f, 0.46f, 0.72f, 0.70f,
                              0.48f, 0.58f, 0.30f, 0.28f };
  }
  if (!rack.replaceState(initialRack, nullptr, diagnostic))
    throw std::invalid_argument(diagnostic);
  keyboard.addListener(this);
  cacheRackParameterPointers();
  audioRoutingRefs.cache(apvts);
  directParameterBank.cache(apvts, instrumentRegistry.descriptors());
  syncDirectParametersFromRack();
  masterGainParameter = apvts.getRawParameterValue("outputLevel");
  for (std::size_t slot = 0; slot < rackControls.size(); ++slot)
    rackControls[slot].slotId = initialRack[slot].slotId;

  // Seed initial sequence with darkPsy pattern (canonical seeded operation
  // owned by the sequence module; same generator stream as before).
  seedInitialSequence();
  for (std::size_t slot = 0; slot < patternRuntime->slotSequences.size();
       ++slot) {
    publishPartSequenceForAudio(static_cast<int>(slot));
    (void)patternRuntime->sequenceBridges[slot].read(
        patternRuntime->audioSequences[slot]);
  }

  presetManager_ = std::make_unique<vstengine::PresetManager>(
      presetStore, patternRuntime->slotSequences[0],
      vstengine::PresetManager::FullStateCallbacks{
          {},
          {},
          [this] { return captureProjectState(); },
          [this](const juce::ValueTree &state) {
            return restoreProjectState(state);
          }});
}

VstEngineAudioProcessor::~VstEngineAudioProcessor() {
  keyboard.removeListener(this);
}
void VstEngineAudioProcessor::handleNoteOn(juce::MidiKeyboardState *,
                                           int channel, int note,
                                           float velocity) {
  if (channel < 1 || channel > 16 || note < 0 || note > 127 ||
      !std::isfinite(velocity))
    return;
  VoxMidiEventV1 event{};
  event.size = 3;
  event.data[0] = static_cast<std::uint8_t>(0x90 | (channel - 1));
  event.data[1] = static_cast<std::uint8_t>(note);
  event.data[2] = static_cast<std::uint8_t>(
      std::lround(std::clamp(velocity, 0.0f, 1.0f) * 127.0f));
  (void)auditionQueue.push(event);
}
void VstEngineAudioProcessor::handleNoteOff(juce::MidiKeyboardState *,
                                            int channel, int note, float) {
  if (channel < 1 || channel > 16 || note < 0 || note > 127)
    return;
  VoxMidiEventV1 event{};
  event.size = 3;
  event.data[0] = static_cast<std::uint8_t>(0x80 | (channel - 1));
  event.data[1] = static_cast<std::uint8_t>(note);
  (void)auditionQueue.push(event);
}

void VstEngineAudioProcessor::prepareToPlay(const double sampleRate,
                                            const int samplesPerBlock)
{
  vstengine::rack::RealtimeMutationGate::ControllerScope mutation(mutationGate);
  rackAudioReady.store(false);
  if (!std::isfinite(sampleRate) || sampleRate < 8000.0 ||
      sampleRate > 384000.0 || samplesPerBlock <= 0 ||
      samplesPerBlock > 65536 || getTotalNumOutputChannels() < 1 ||
      getTotalNumOutputChannels() > 2)
    return;
  try {
    currentSampleRate = sampleRate;
    currentMaximumBlockSize = static_cast<std::uint32_t>(samplesPerBlock);
    const vstengine::instrument::PrepareSpec spec{
        sampleRate, currentMaximumBlockSize,
        static_cast<std::uint32_t>(getTotalNumOutputChannels())};
    if (!audioRouting.prepare(spec) || !rack.prepare(spec) ||
        !effectsChain.prepare(sampleRate, currentMaximumBlockSize,
                              spec.outputChannels))
      return;
    masterGain.reset(sampleRate, 0.005);
    masterGain.setCurrentAndTargetValue(masterGainParameter->load());
    for (auto &peak : outputPeaks)
      peak.store(0.0f);
    updateReportedLatencyAndTails();

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

    for (auto& scratch : patternRuntime->generatedScratch)
        scratch.ensureSize(65536);
    wasTransportPlaying = false;
    rackAudioReady.store(true);
  } catch (...) {
    rackAudioReady.store(false);
  }
}

void VstEngineAudioProcessor::releaseResources()
{
  vstengine::rack::RealtimeMutationGate::ControllerScope mutation(mutationGate);
  rackAudioReady.store(false);
  rack.reset();
  effectsChain.reset();
  audioRouting.reset();
  returnTailSamples.store(0);
  duckReduction.store(0);
  for (auto &scheduler : patternRuntime->schedulers)
    scheduler.reset(currentSampleRate);
  wasTransportPlaying = false;
}

bool VstEngineAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono()
        || output == juce::AudioChannelSet::stereo();
}

void VstEngineAudioProcessor::updateReportedLatencyAndTails() noexcept {
  declaredRackTail.store(rack.tailSamples(), std::memory_order_relaxed);
  declaredEffectsTail.store(effectsChain.tailSamples(),
                            std::memory_order_relaxed);
  // Before the next admitted callback, advertise a conservative bound for all
  // armed internal returns. Audio publishes the exact configured chain bound.
  declaredTailSeconds.store(
      currentSampleRate > 0.0
          ? static_cast<double>(
                static_cast<std::uint64_t>(declaredRackTail.load()) +
                declaredEffectsTail.load()) /
                    currentSampleRate +
                8.0
          : 0.0,
      std::memory_order_relaxed);
  setLatencySamples(static_cast<int>(rack.latencySamples()));
}

bool VstEngineAudioProcessor::applyInternalEffectsPreset(
    std::string_view presetId) noexcept
{
  vstengine::rack::RealtimeMutationGate::ControllerScope mutation(mutationGate);
  const auto applied = effectsChain.applyPreset(presetId);
  if (applied)
    updateReportedLatencyAndTails();
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
    if (!state.isValid())
      return false;
    if (!state.hasType("EFFECTS")) return false;
    if (!hasVersion(state, "schemaVersion",
                    vstengine::effects::EffectsChain::stateVersion))
      return false;
    const auto schema = vstengine::effects::EffectsChain::stateVersion;
    if (state.getProperty("payload").toString().getNumBytesAsUTF8() > 512)
      return false;
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
  vstengine::rack::RealtimeMutationGate::AudioScope active(mutationGate);
  if (!active) {
    buffer.clear();
    return;
  }
  if (buffer.getNumChannels() < 1 || buffer.getNumChannels() > 2 ||
      buffer.getNumSamples() == 0) {
    buffer.clear();
    return;
  }
  if (mutationGate.takeRecovery()) {
    // A skipped block may have held the final NoteOff. Clear ownership and
    // voices before accepting new events; never leave an unrelated drone.
    rack.reset();
    auditionQueue.discard();
    keyboardResetRequested.store(true);
    auditionNotes.fill(false);
    for (auto &scheduler : patternRuntime->schedulers)
      scheduler.reset(currentSampleRate);
    audioRouting.reset();
  }
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
    if (!rackAudioReady.load(std::memory_order_relaxed))
      return;
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
    transportPlaying.store(playing, std::memory_order_relaxed);
    if (wasTransportPlaying && !playing) {
        rack.reset();
        for (auto& scheduler : patternRuntime->schedulers)
            scheduler.reset(currentSampleRate);
    }
    wasTransportPlaying = playing;

    std::size_t auditionCount = 0;
    while (auditionCount < auditionEvents.size() &&
           auditionQueue.pop(auditionEvents[auditionCount])) {
      const auto &event = auditionEvents[auditionCount++];
      auditionNotes[event.data[1]] =
          (event.data[0] & 0xf0) == 0x90 && event.data[2] != 0;
    }
    const auto blockSamples =
        static_cast<std::uint32_t>(buffer.getNumSamples());
    bool hostOverflow = false;
    auto hostCount = convertMidi(midi, hostEvents, blockSamples, &hostOverflow);
    bool inputNotes = false;
    for (std::size_t i = 0; i < hostCount; ++i)
      if ((hostEvents[i].data[0] & 0xf0) == 0x80 ||
          (hostEvents[i].data[0] & 0xf0) == 0x90)
        inputNotes = true;
    const auto mode = currentMidiMode();
    for (auto& scratch : patternRuntime->generatedScratch) scratch.clear();
    if (mode == MidiSourceMode::generator) {
      midi.clear();
      hostCount = 0;
    }
    const bool queueOverflow = auditionQueue.takeOverflow();
    const bool panicThisBlock =
        panicRequested.exchange(false) || queueOverflow || hostOverflow;
    if (panicThisBlock) {
        midi.clear();
        auditionQueue.discard();
        keyboardResetRequested.store(true);
        auditionCount = 0;
        hostCount = 0;
        auditionNotes.fill(false);
        for (auto& scheduler : patternRuntime->schedulers)
            scheduler.reset(currentSampleRate);
        rack.reset();
        effectsChain.reset();
        audioRouting.reset();
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
    const auto audioControls = audioRoutingRefs.read(rackControls);
    rack.process({outputs.data(), channels},
                 static_cast<std::uint32_t>(buffer.getNumSamples()),
                 {hostEvents.data(), hostCount}, bpm, ppq, playing,
                 {auditionEvents.data(), auditionCount},
                 rackControls[selected].slotId, rackControls, generatedStreams,
                 &audioRouting, &audioControls);
    returnTailSamples.store(audioRouting.tailSamples(),
                            std::memory_order_relaxed);
    declaredTailSeconds.store(
        currentSampleRate > 0.0
            ? static_cast<double>(
                  static_cast<std::uint64_t>(
                      declaredRackTail.load(std::memory_order_relaxed)) +
                  declaredEffectsTail.load(std::memory_order_relaxed) +
                  audioRouting.tailSamples()) /
                  currentSampleRate
            : 0.0,
        std::memory_order_relaxed);
    duckReduction.store(audioRouting.gainReduction(),
                        std::memory_order_relaxed);
    effectsChain.process({ outputs.data(), channels }, blockSamples);
    const auto requestedGain =
        masterGainParameter->load(std::memory_order_relaxed);
    masterGain.setTargetValue(std::isfinite(requestedGain)
                                  ? std::clamp(requestedGain, 0.0f, 2.0f)
                                  : 0.0f);
    float peaks[2]{};
    for (std::uint32_t sample = 0; sample < blockSamples; ++sample) {
      const float gain = masterGain.getNextValue();
      for (std::size_t c = 0; c < channels; ++c) {
        outputs[c][sample] *= gain;
        peaks[c] = std::max(peaks[c], std::abs(outputs[c][sample]));
      }
    }
    if (channels == 1)
      peaks[1] = peaks[0];
    for (std::size_t c = 0; c < 2; ++c) {
      auto previousPeak = outputPeaks[c].load(std::memory_order_relaxed);
      if (previousPeak < peaks[c])
        (void)outputPeaks[c].compare_exchange_strong(previousPeak, peaks[c],
                                                     std::memory_order_relaxed);
    }
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
  std::size_t scanned = 0;
  for (const auto metadata : midi) {
    if (++scanned > vstengine::rack::RackRouter::eventCapacity)
      break;
    if (metadata.numBytes == 3 && ((metadata.data[0] & 0xf0) == 0x80 ||
                                   (metadata.data[0] & 0xf0) == 0x90))
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
        refs.midiIn = get("MidiIn");
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
    // A host can still write a duplicate channel through state or a raw
    // parameter; the lowest slot keeps it and later duplicates read as Off.
    std::array<bool, 17> claimed {};
    for (std::size_t slot = 0; slot < rackParameterRefs.size(); ++slot) {
        const auto& refs = rackParameterRefs[slot];
        int midiIn = juce::jlimit(0, 16,
            static_cast<int>(std::lround(refs.midiIn->load())));
        if (midiIn > 0 && std::exchange(claimed[static_cast<std::size_t>(midiIn)], true))
            midiIn = 0;
        vstengine::rack::Routing routing;
        routing.mode = midiIn == 0 ? vstengine::rack::RouteMode::off
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
        directParameterBank.read(slot, controls);
    }
}

std::size_t VstEngineAudioProcessor::convertMidi(
    const juce::MidiBuffer &input,
    std::array<VoxMidiEventV1, vstengine::rack::RackRouter::eventCapacity>
        &output,
    std::uint32_t sampleCount, bool *overflow) noexcept {
  std::size_t count = 0, scanned = 0;
  if (overflow)
    *overflow = false;
  for (const auto metadata : input) {
    if (++scanned > output.size()) {
      if (overflow)
        *overflow = true;
      break;
    }
    // Inspect raw metadata first: constructing MidiMessage for SysEx can
    // malloc even though that message is unsupported and will be dropped.
    if (metadata.numBytes < 1 || metadata.numBytes > 3 ||
        metadata.samplePosition < 0 ||
        static_cast<std::uint32_t>(metadata.samplePosition) >= sampleCount)
      continue;
    auto &event = output[count++];
    event.sampleOffset = static_cast<std::uint32_t>(metadata.samplePosition);
    event.size = static_cast<std::uint8_t>(metadata.numBytes);
    std::fill(std::begin(event.data), std::end(event.data), std::uint8_t{});
    std::copy_n(metadata.data, event.size, event.data);
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

VstEngineAudioProcessor::SequenceEdit
VstEngineAudioProcessor::sequenceEditSnapshot() {
  vstengine::rack::RealtimeMutationGate::ReadScope read(mutationGate);
  const auto slot = selectedSlotIndex();
  return {patternRuntime->slotSequences[slot], slot,
          patternRuntime->revisions[slot]};
}

bool VstEngineAudioProcessor::commitSequenceEdit(SequenceEdit &edit) {
  vstengine::rack::RealtimeMutationGate::ReadScope read(mutationGate);
  if (edit.slot >= patternRuntime->slotSequences.size() ||
      selectedSlotIndex() != edit.slot ||
      patternRuntime->revisions[edit.slot] != edit.revision)
    return false;
  patternRuntime->slotSequences[edit.slot] = edit.value;
  publishPartSequenceForAudio(static_cast<int>(edit.slot));
  edit.revision = patternRuntime->revisions[edit.slot];
  return true;
}

void VstEngineAudioProcessor::publishPartSequenceForAudio(const int partIndex) noexcept
{
  if (partIndex >= 0 &&
      partIndex < static_cast<int>(patternRuntime->sequenceBridges.size())) {
    vstengine::rack::RealtimeMutationGate::ReadScope read(mutationGate);
    const auto slot = static_cast<std::size_t>(partIndex);
    patternRuntime->sequenceBridges[slot].publish(
        patternRuntime->slotSequences[slot]);
    ++patternRuntime->revisions[slot];
  }
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
    snapshot = rack.snapshotState(controls);
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
VstEngineAudioProcessor::createParameterLayout(
    std::span<const vstengine::instrument::InstrumentDescriptor *const>
        catalog) {
  std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"drive", 1}, "Drive",
      juce::NormalisableRange<float>{1.0f, 6.0f, 0.01f}, 1.8f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"release", 1}, "Release",
      juce::NormalisableRange<float>{0.005f, 0.250f, 0.001f, 0.5f}, 0.035f,
      "s"));

  params.push_back(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"midiMode", 1}, "MIDI Source",
      juce::StringArray{"Auto", "Piano Roll", "Generator", "Both"}, 0));

  params.push_back(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{"midiChannel", 1}, "Bass MIDI Channel", 1, 16, 1));

  for (auto parameter :
       {std::pair{"bassMute", "Bass Mute"}, std::pair{"bassSolo", "Bass Solo"},
        std::pair{"bassLock", "Bass Lock"}})
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{parameter.first, 1}, parameter.second, false));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"bassLevel", 1}, "Bass Part Level",
      juce::NormalisableRange<float>{0.0f, 2.0f, 0.01f}, 1.0f));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"bassPan", 1}, "Bass Part Pan",
      juce::NormalisableRange<float>{-1.0f, 1.0f, 0.01f}, 0.0f));

  params.push_back(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{"rootNote", 1}, "Generator Root Note", 24, 60, 36));

  // Seed for the deterministic probability RNG used by realtime playback.
  params.push_back(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{"rngSeed", 1}, "Generator Seed", 0,
      static_cast<int>(vstengine::instrument::maxGenerationSeed), 1234));

  // --- Resonant low-pass filter ---
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterCutoff", 1}, "Filter Cutoff",
      juce::NormalisableRange<float>{0.02f, 0.95f, 0.001f, 0.4f}, 0.55f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterResonance", 1}, "Filter Resonance",
      juce::NormalisableRange<float>{0.0f, 1.0f, 0.01f}, 0.7f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"filterDrive", 1}, "Filter Drive",
      juce::NormalisableRange<float>{0.0f, 4.0f, 0.01f}, 1.0f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"keyTracking", 1}, "Key Tracking",
      juce::NormalisableRange<float>{0.0f, 1.0f, 0.01f}, 0.2f));

  // --- Per-parameter amp ADSR (release is the existing "release" knob) ---
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"ampAttack", 1}, "Amp Attack",
      juce::NormalisableRange<float>{0.0005f, 0.100f, 0.0005f, 0.3f}, 0.001f,
      "s"));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"ampDecay", 1}, "Amp Decay",
      juce::NormalisableRange<float>{0.005f, 1.0f, 0.001f, 0.4f}, 0.055f, "s"));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"ampSustain", 1}, "Amp Sustain",
      juce::NormalisableRange<float>{0.0f, 1.0f, 0.01f}, 0.72f));

  // --- Pitch envelope ---
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"pitchEnvAmount", 1}, "Pitch Env Amount",
      juce::NormalisableRange<float>{0.0f, 36.0f, 0.1f}, 12.0f, "st"));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"pitchEnvTime", 1}, "Pitch Env Time",
      juce::NormalisableRange<float>{0.001f, 0.5f, 0.001f, 0.4f}, 0.018f, "s"));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"pitchEnvCurve", 1}, "Pitch Env Curve",
      juce::NormalisableRange<float>{0.5f, 8.0f, 0.1f}, 2.0f));

  // --- Output gain compensation ---
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"outputLevel", 1}, "Output Level",
      juce::NormalisableRange<float>{0.0f, 2.0f, 0.01f, 0.6f}, 1.0f));

  // --- Kick engine (issue #11 PHASE 5, exact parameter set) ---
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickPitchStart", 1}, "Kick Pitch Start",
      juce::NormalisableRange<float>{0.0f, 36.0f, 0.1f}, 12.0f, "st"));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickPitchEnd", 1}, "Kick Pitch End",
      juce::NormalisableRange<float>{-24.0f, 24.0f, 0.1f}, 0.0f, "st"));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickPitchDecay", 1}, "Kick Pitch Decay",
      juce::NormalisableRange<float>{0.001f, 0.5f, 0.001f, 0.4f}, 0.03f, "s"));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickPitchCurve", 1}, "Kick Pitch Curve",
      juce::NormalisableRange<float>{0.5f, 8.0f, 0.1f}, 2.0f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickBodyDecay", 1}, "Kick Body Decay",
      juce::NormalisableRange<float>{0.01f, 2.0f, 0.001f, 0.4f}, 0.16f, "s"));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickTail", 1}, "Kick Tail",
      juce::NormalisableRange<float>{0.0f, 4.0f, 0.001f, 0.5f}, 0.3f, "s"));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickClick", 1}, "Kick Click",
      juce::NormalisableRange<float>{0.0f, 1.0f, 0.01f}, 0.5f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickClickTone", 1}, "Kick Click Tone",
      juce::NormalisableRange<float>{0.0f, 1.0f, 0.01f}, 0.5f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickDrive", 1}, "Kick Drive",
      juce::NormalisableRange<float>{1.0f, 10.0f, 0.01f}, 1.0f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickClip", 1}, "Kick Clip",
      juce::NormalisableRange<float>{0.1f, 1.0f, 0.005f}, 1.0f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickTransient", 1}, "Kick Transient",
      juce::NormalisableRange<float>{0.0f, 1.0f, 0.01f}, 0.5f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickSub", 1}, "Kick Sub",
      juce::NormalisableRange<float>{0.0f, 1.0f, 0.01f}, 0.4f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickTune", 1}, "Kick Tune",
      juce::NormalisableRange<float>{24.0f, 48.0f, 1.0f}, 36.0f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickPhase", 1}, "Kick Phase",
      juce::NormalisableRange<float>{0.0f, 360.0f, 1.0f}, 0.0f, "deg"));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickOutputLevel", 1}, "Kick Output Level",
      juce::NormalisableRange<float>{0.0f, 2.0f, 0.01f, 0.6f}, 1.0f));

  params.push_back(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{"kickMidiChannel", 1}, "Kick MIDI Channel", 1, 16, 2));

  for (auto parameter :
       {std::pair{"kickMute", "Kick Mute"}, std::pair{"kickSolo", "Kick Solo"},
        std::pair{"kickLock", "Kick Lock"}})
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{parameter.first, 1}, parameter.second, false));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickLevel", 1}, "Kick Part Level",
      juce::NormalisableRange<float>{0.0f, 2.0f, 0.01f}, 1.0f));
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"kickPan", 1}, "Kick Part Pan",
      juce::NormalisableRange<float>{-1.0f, 1.0f, 0.01f}, 0.0f));

  // --- Kick/bass match (issue #11 PHASE 6) ---
  // User-visible bounded result of the match analysis: the bass's note
  // events are delayed by this many ms relative to the kick attack
  // (0 = no delay). Bound 16 ms by construction.
  params.push_back(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{"matchBassTimingOffsetMs", 1}, "Bass Timing Offset", 0,
      16, 0));

  // Stable host-facing automation bank. Module-specific parameters map to
  // these fixed IDs; loading another module never changes Cubase automation.
  juce::StringArray midiInputs{"Off"};
  for (int channel = 1; channel <= 16; ++channel)
    midiInputs.add("CH" + juce::String(channel));
  for (std::size_t slot = 0; slot < vstengine::instrument::maxSlots; ++slot) {
    const auto id = [slot](std::string_view suffix) {
      return juce::ParameterID{juce::String(slotParameterId(slot, suffix)), 1};
    };
    const auto label = "Slot " + juce::String(static_cast<int>(slot + 1)) + " ";
    // Channel ownership is configuration, not a performance control.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        id("MidiIn"), label + "MIDI In", midiInputs,
        slot < 2 ? static_cast<int>(slot + 1) : 0,
        juce::AudioParameterChoiceAttributes{}.withAutomatable(false)));
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
        juce::NormalisableRange<float>{0.0f, 2.0f, 0.001f}, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        id("Pan"), label + "Pan",
        juce::NormalisableRange<float>{-1.0f, 1.0f, 0.001f}, 0.0f));
    for (std::size_t macro = 0; macro < vstengine::instrument::macrosPerSlot;
         ++macro)
      params.push_back(std::make_unique<juce::AudioParameterFloat>(
          juce::ParameterID{
              vstengine::instrument::hostparams::macroId(slot, macro), 1},
          vstengine::instrument::hostparams::macroName(slot, macro),
          juce::NormalisableRange<float>{0.0f, 1.0f, 0.0001f}, 0.5f));
  }

  vstengine::host::appendRackAudioParameters(params);
  vstengine::host::appendPoolParameters(params, catalog);
  return {params.begin(), params.end()};
}

bool VstEngineAudioProcessor::preflightDirectState(
    const std::array<vstengine::rack::PersistentSlotState,
                     vstengine::instrument::maxSlots> &state) const noexcept {
  try {
    for (std::size_t slot = 0; slot < state.size(); ++slot) {
      const auto resolution =
          instrumentRegistry.resolve(state[slot].instrumentId);
      if (resolution && resolution.descriptor->directParameters &&
          (state[slot].instrumentVersion == 0 ||
           state[slot].instrumentVersion ==
               resolution.descriptor->instrumentVersion) &&
          (state[slot].contentVersion == 0 ||
           state[slot].contentVersion ==
               resolution.descriptor->contentVersion) &&
          state[slot].instrumentStateVersion ==
              resolution.descriptor->stateVersion &&
          state[slot].patternSchemaVersion <= VOX_PATTERN_SCHEMA_V1 &&
          !directParameterBank.canSelectAndRestore(slot, state[slot]))
        return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

void VstEngineAudioProcessor::syncDirectParametersFromRack(
    std::size_t onlySlot) {
  const auto &snapshot = rack.state();
  const auto runtime = rack.runtimeState();
  for (std::size_t slot = 0; slot < snapshot.size(); ++slot)
    if (onlySlot == vstengine::instrument::maxSlots || onlySlot == slot) {
      if (runtime[slot].resolution !=
          vstengine::instrument::ResolutionStatus::resolved)
        directParameterBank.deactivate(slot);
      else if (!directParameterBank.selectAndRestore(slot, snapshot[slot]))
        throw std::logic_error("validated direct state has no host binding");
    }
}

bool VstEngineAudioProcessor::loadSlotInstrument(
    std::size_t slot, std::string_view instrumentId, juce::String& diagnostic)
{
  vstengine::rack::RealtimeMutationGate::ControllerScope mutation(mutationGate);
  std::string detail;
  const bool accepted = rack.loadModule(slot, instrumentId, nullptr, detail);
  if (accepted) {
    syncDirectParametersFromRack(slot);
    updateReportedLatencyAndTails();
    const auto &values = rack.state()[slot].macros;
    for (std::size_t i = 0; i < values.size(); ++i)
      if (auto *parameter = apvts.getParameter(
              vstengine::instrument::hostparams::macroId(slot, i).c_str())) {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(values[i]);
        parameter->endChangeGesture();
      }
    patternRuntime->slotSequences[slot].clear();
    publishPartSequenceForAudio(static_cast<int>(slot));
    patternRuntime->schedulers[slot].reset(currentSampleRate);
    // An empty slot releases its channel; a new instrument without one takes
    // the next free channel (Off when all sixteen are owned).
    auto* channel = apvts.getParameter(slotParameterId(slot, "MidiIn").c_str());
    const int current = static_cast<int>(std::lround(rackParameterRefs[slot].midiIn->load()));
    const int wanted = instrumentId.empty() ? 0 : current > 0 ? current : nextFreeChannel();
    if (channel != nullptr && wanted != current) {
      channel->beginChangeGesture();
      channel->setValueNotifyingHost(channel->convertTo0to1(static_cast<float>(wanted)));
      channel->endChangeGesture();
    }
  }

    diagnostic = detail;
    return accepted;
}

std::span<const vstengine::instrument::ContentDescriptor>
VstEngineAudioProcessor::selectedContent() const noexcept
{
  vstengine::rack::RealtimeMutationGate::ReadScope read(mutationGate);
  const auto index = selectedSlotIndex();
  const auto &state = rack.state()[index];
  const auto resolution = instrumentRegistry.resolve(state.instrumentId);
  return resolution && resolution.provider != nullptr
             ? resolution.provider->contentDescriptors(state.instrumentId)
             : std::span<const vstengine::instrument::ContentDescriptor>{};
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
    vstengine::rack::RealtimeMutationGate::ControllerScope mutation(
        mutationGate);
    std::string detail;
    const bool accepted = rack.applySoundPreset(index, presetId, nullptr, detail);
    if (accepted) {
      syncDirectParametersFromRack(index);
      updateReportedLatencyAndTails();
      const auto &values = rack.state()[index].macros;
      for (std::size_t macro = 0; macro < values.size(); ++macro)
        if (auto *parameter = apvts.getParameter(
                vstengine::instrument::hostparams::macroId(index, macro)
                    .c_str()))
          parameter->setValueNotifyingHost(values[macro]);
    }

    diagnostic = detail;
    return accepted;
}

bool VstEngineAudioProcessor::generateSelectedPattern(
    std::string_view profileId, juce::String& diagnostic)
{
  vstengine::rack::RealtimeMutationGate::ReadScope read(mutationGate);
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
    vstengine::rack::RealtimeMutationGate::ControllerScope mutation(
        mutationGate);
    if (!rack.updatePatternState(index, std::string(profileId),
                                 VOX_PATTERN_SCHEMA_V1,
                                 std::move(encoded))) {

        diagnostic = "pattern state commit failed";
        return false;
    }
    patternRuntime->slotSequences[index] = std::move(candidate);
    publishPartSequenceForAudio(static_cast<int>(index));
    patternRuntime->schedulers[index].reset(currentSampleRate);

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
  vstengine::rack::RealtimeMutationGate::ReadScope read(mutationGate);
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
    vstengine::rack::RealtimeMutationGate::ControllerScope mutation(
        mutationGate);
    const auto committed = rack.updatePatternState(index, profile,
        VOX_PATTERN_SCHEMA_V1, std::move(encoded));
    if (committed) {
        patternRuntime->slotSequences[index] = std::move(candidate);
        publishPartSequenceForAudio(static_cast<int>(index));
        patternRuntime->schedulers[index].reset(currentSampleRate);
    }

    diagnostic = committed ? juce::String {} : "pattern state commit failed";
    return committed;
}

bool VstEngineAudioProcessor::runGlobalPatternOperation(
    bool mutate, juce::String& diagnostic)
{
  vstengine::rack::RealtimeMutationGate::ReadScope read(mutationGate);
  std::array<vstengine::sequence::Sequence, vstengine::instrument::maxSlots>
      candidates;
  std::array<std::vector<std::byte>, vstengine::instrument::maxSlots> encoded;
  std::array<std::string, vstengine::instrument::maxSlots> profiles;
  std::array<bool, vstengine::instrument::maxSlots> changed{};
  std::size_t changedCount{};
  for (std::size_t index = 0; index < changed.size(); ++index) {
    if (rackParameterRefs[index].locked->load() >= 0.5f)
      continue;
    const auto *descriptor = rack.descriptor(index);
    if (descriptor == nullptr ||
        (descriptor->capabilities &
         vstengine::instrument::Capability::patternGenerator) == 0)
      continue;
    profiles[index] = generatorProfileForSlot(index);
    const auto input =
        vstengine::sequence::toPattern(patternRuntime->slotSequences[index]);
    if (!prepareGeneratedPattern(index, profiles[index],
                                 mutate ? &input : nullptr,
                                 mutate ? 0.35f : 0.0f, -1, -1,
                                 candidates[index], encoded[index], diagnostic))
      return false;
    changed[index] = true;
    ++changedCount;
  }
    if (changedCount == 0) {
        diagnostic = "No unlocked generator slots";
        return false;
    }
    vstengine::rack::RealtimeMutationGate::ControllerScope mutation(
        mutationGate);
    for (std::size_t index = 0; index < changed.size(); ++index) {
        if (!changed[index]) continue;
        if (!rack.updatePatternState(index, profiles[index],
                VOX_PATTERN_SCHEMA_V1, std::move(encoded[index]))) {

            diagnostic = "global pattern commit failed";
            return false;
        }
        patternRuntime->slotSequences[index] = std::move(candidates[index]);
        publishPartSequenceForAudio(static_cast<int>(index));
        patternRuntime->schedulers[index].reset(currentSampleRate);
    }

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

bool VstEngineAudioProcessor::channelAvailable(std::size_t slot,
                                               int channel) const noexcept
{
    if (slot >= rackParameterRefs.size() || channel < 0 || channel > 16)
        return false;
    if (channel == 0)
        return true;
    for (std::size_t i = 0; i < rackParameterRefs.size(); ++i)
        if (i != slot && static_cast<int>(std::lround(
                             rackParameterRefs[i].midiIn->load())) == channel)
            return false;
    return true;
}

bool VstEngineAudioProcessor::assignSlotChannel(std::size_t slot, int channel,
                                                juce::String& diagnostic)
{
  vstengine::rack::RealtimeMutationGate::ControllerScope mutation(mutationGate);
  if (slot >= rackParameterRefs.size() || channel < 0 || channel > 16) {
    diagnostic = "Invalid slot/channel";
    return false;
  }
  if (!channelAvailable(slot, channel)) {
    diagnostic = "CH" + juce::String(channel) + " belongs to another instrument";
    return false;
  }
  if (auto* parameter = apvts.getParameter(slotParameterId(slot, "MidiIn").c_str())) {
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(channel)));
    parameter->endChangeGesture();
  }
  diagnostic.clear();
  return true;
}

bool VstEngineAudioProcessor::serializeSlotPatternMidi(
    std::size_t index, juce::MemoryBlock &destination,
    juce::String &diagnostic) {
  vstengine::rack::RealtimeMutationGate::ReadScope read(mutationGate);
  if (index >= vstengine::instrument::maxSlots ||
      rack.state()[index].instrumentId.empty()) {
    diagnostic = "Choose a populated Rack slot to export";
    return false;
  }
  const auto &slot = rack.state()[index];
  const auto channel = rackParameterRefs[index].midiIn->load();
  const auto root = apvts.getRawParameterValue("rootNote")->load();
  const auto seed = apvts.getRawParameterValue("rngSeed")->load();
  if (!std::isfinite(channel) || channel < 0 || channel > 16 ||
      !std::isfinite(root) || root < 0 || root > 127 || !std::isfinite(seed) ||
      seed < 0 || seed > vstengine::instrument::maxGenerationSeed) {
    diagnostic = "Invalid MIDI export parameters";
    return false;
  }
  vstengine::midiexport::Options options;
  options.channel = std::clamp(static_cast<int>(std::lround(channel)), 1, 16);
  options.rootNote = static_cast<int>(std::lround(root));
  options.seed = vstengine::instrument::generationSubSeed(
      static_cast<std::uint32_t>(seed), slot.slotId, slot.instrumentId);
  const auto track = vstengine::midiexport::buildSequenceTrack(
      patternRuntime->slotSequences[index], options);
  bool hasNotes = false;
  for (int event = 0; event < track.getNumEvents(); ++event)
    hasNotes = hasNotes || track.getEventPointer(event)->message.isNoteOn();
  if (!hasNotes) {
    diagnostic = "Selected slot has no audible pattern notes to export";
    return false;
  }
  juce::MidiFile file;
  file.setTicksPerQuarterNote(static_cast<int>(options.ticksPerQuarter));
  file.addTrack(track);
  juce::MemoryBlock candidate;
  {
    juce::MemoryOutputStream stream(candidate, false);
    if (!file.writeTo(stream)) {
      diagnostic = "Could not encode the MIDI pattern";
      return false;
    }
  } // Trim the external MemoryBlock before publishing its encoded bytes.
  destination = std::move(candidate);
  diagnostic.clear();
  return true;
}

juce::File VstEngineAudioProcessor::createSlotMidiFile(std::size_t index) {
  juce::MemoryBlock bytes;
  juce::String diagnostic;
  if (!serializeSlotPatternMidi(index, bytes, diagnostic))
    return {};
  const auto file =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("Vox-Trance-Engine-Slot-" +
                        juce::String(static_cast<int>(index + 1)) + "-" +
                        juce::Uuid().toString() + ".mid");
  if (writePatternMidiFile(file, bytes, diagnostic))
    return file;
  (void)file.deleteFile();
  return {};
}

bool VstEngineAudioProcessor::writePatternMidiFile(
    const juce::File &destination, const juce::MemoryBlock &bytes,
    juce::String &diagnostic) {
  if (destination == juce::File{} || destination.isDirectory() ||
      bytes.getSize() == 0) {
    diagnostic = "Choose a valid MIDI file destination";
    return false;
  }
  juce::TemporaryFile temporary(destination);
  {
    auto stream = temporary.getFile().createOutputStream();
    if (!stream || !stream->write(bytes.getData(), bytes.getSize())) {
      diagnostic =
          "Could not write MIDI file: " + destination.getFullPathName();
      return false;
    }
    stream->flush();
    if (stream->getStatus().failed()) {
      diagnostic =
          "Could not flush MIDI file: " + destination.getFullPathName();
      return false;
    }
  }
  if (!temporary.overwriteTargetFileWithTemporary()) {
    diagnostic =
        "Could not replace MIDI file: " + destination.getFullPathName();
    return false;
  }
  diagnostic.clear();
  return true;
}

juce::ValueTree VstEngineAudioProcessor::captureProjectState() {
  vstengine::rack::RealtimeMutationGate::ReadScope read(mutationGate);
  auto tree = apvts.copyState();
  for (;;) {
    const auto stale = tree.getChildWithName("PARTS");
    if (!stale.isValid())
      break;
    tree.removeChild(stale, nullptr);
  }
  for (;;) {
    const auto stale = tree.getChildWithName("RACK");
    if (!stale.isValid())
      break;
    tree.removeChild(stale, nullptr);
  }
  for (;;) {
    const auto stale = tree.getChildWithName("EFFECTS");
    if (!stale.isValid())
      break;
    tree.removeChild(stale, nullptr);
  }
  tree.setProperty("stateSchemaVersion", 5, nullptr);
  // Unresolved slots keep their named values in the Rack state; their pool
  // endpoints are unbound, so no PARAM overlay is needed.
  const auto slots = snapshotRackWithPatterns();
  tree.addChild(vstengine::rack::state::serialize(slots), -1, nullptr);
  tree.addChild(captureEffectsState(), -1, nullptr);
  tree.removeProperty("sequenceData", nullptr);
  return tree;
}

void VstEngineAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {
  const auto tree = captureProjectState();
  const auto xml = tree.createXml();
  copyXmlToBinary(*xml, destData);
}

bool VstEngineAudioProcessor::restoreProjectState(const juce::ValueTree &tree) {
  vstengine::rack::RealtimeMutationGate::ReadScope read(mutationGate);
  if (!tree.hasType("PARAMETERS") || !hasVersion(tree, "stateSchemaVersion", 5))
    return false;
  const auto rackTree = tree.getChildWithName("RACK");
  if (!rackTree.isValid() || !hasVersion(rackTree, "schemaVersion",
                                         vstengine::rack::state::schemaVersion))
    return false;
  const auto effectsTree = tree.getChildWithName("EFFECTS");
  vstengine::effects::EffectsChain candidateEffects;
  if (!prepareEffectsState(effectsTree, candidateEffects))
    return false;
  std::array<vstengine::rack::PersistentSlotState,
             vstengine::instrument::maxSlots>
      parsedRack;
  std::string rackDiagnostic;
  if (rackTree.isValid()) {
    if (!vstengine::rack::state::deserialize(rackTree, parsedRack,
                                             rackDiagnostic))
      return false;
  }
  if (!validateProjectParameters(tree, apvts))
    return false;
  if (rackTree.isValid() && !preflightDirectState(parsedRack))
    return false;
  // Decode into local fixed-size sequences; no live publication until
  // every module and scalar has passed validation.
  auto candidateSequences = patternRuntime->slotSequences;
  for (std::size_t slot = 0; slot < parsedRack.size(); ++slot) {
    const auto &saved = parsedRack[slot];
    if (saved.patternPayload.empty() ||
        saved.patternSchemaVersion != VOX_PATTERN_SCHEMA_V1) {
      candidateSequences[slot].clear();
      continue;
    }
    VoxPatternV1 pattern{};
    if (!vstengine::sequence::decodePattern(saved.patternPayload, pattern) ||
        !vstengine::sequence::fromPattern(pattern, candidateSequences[slot]))
      return false;
  }
  auto apvtsState = tree.createCopy();
  for (;;) {
    const auto extra = apvtsState.getChildWithName("PARTS");
    if (!extra.isValid())
      break;
    apvtsState.removeChild(extra, nullptr);
  }
  for (;;) {
    const auto extra = apvtsState.getChildWithName("RACK");
    if (!extra.isValid())
      break;
    apvtsState.removeChild(extra, nullptr);
  }
  for (;;) {
    const auto extra = apvtsState.getChildWithName("EFFECTS");
    if (!extra.isValid())
      break;
    apvtsState.removeChild(extra, nullptr);
  }
  // Pool PARAMs load as saved; syncDirectParametersFromRack() then rebinds
  // every slot and rewrites resolved slots from the Rack's named values, and an
  // unresolved slot keeps its named domain in the Rack state with an unbound pool.
  const auto previousApvts = apvts.copyState();
  vstengine::rack::RealtimeMutationGate::ControllerScope mutation(mutationGate);
  apvts.replaceState(apvtsState);
  if (rackTree.isValid() &&
      !rack.replaceState(parsedRack, nullptr, rackDiagnostic)) {
    apvts.replaceState(previousApvts);

    return false;
  }
  if (rackTree.isValid())
    for (std::size_t slot = 0; slot < rackControls.size(); ++slot)
      rackControls[slot].slotId = parsedRack[slot].slotId;
  patternRuntime->slotSequences = std::move(candidateSequences);
  syncDirectParametersFromRack();
  const auto restoredGain =
      masterGainParameter->load(std::memory_order_relaxed);
  masterGain.setCurrentAndTargetValue(std::isfinite(restoredGain)
                                          ? std::clamp(restoredGain, 0.0f, 2.0f)
                                          : 0.0f);
  effectsChain = std::move(candidateEffects);
  updateReportedLatencyAndTails();
  audioRouting.reset();
  returnTailSamples.store(0);
  duckReduction.store(0);

  for (std::size_t slot = 0; slot < patternRuntime->slotSequences.size();
       ++slot)
    publishPartSequenceForAudio(static_cast<int>(slot));
  for (auto &scheduler : patternRuntime->schedulers)
    scheduler.reset(currentSampleRate);
  return true;
}

void VstEngineAudioProcessor::setStateInformation(const void *data,
                                                  int sizeInBytes) {
  if (data == nullptr || sizeInBytes <= 0 ||
      static_cast<std::size_t>(sizeInBytes) >
          vstengine::core::maxSerializedProjectBytes)
    return;
  if (auto xml = getXmlFromBinary(data, sizeInBytes))
    (void)restoreProjectState(juce::ValueTree::fromXml(*xml));
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
