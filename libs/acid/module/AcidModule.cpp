#include "AcidModuleMap.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
#include <vector>

using vstengine::acid::AcidEngine;
using vstengine::acid::module::Control;
using vstengine::acid::module::Domain;
using vstengine::acid::module::controls;
using vstengine::acid::module::continuous;
using vstengine::acid::module::instrumentId;
using vstengine::acid::module::instrumentName;
using vstengine::acid::module::parameterCount;
using vstengine::acid::module::plainToNorm;
using vstengine::acid::module::stateVersion;

VoxHostServicesV2 gHost{};
constexpr std::uint32_t kFaultMidiOverflow = 1;
constexpr std::uint32_t kFaultNonFiniteOutput = 2;
constexpr std::uint32_t kFaultNonFiniteParam = 3;
constexpr std::uint32_t kFaultParamOverflow = 4;
VoxParameterDescriptorV2 gParameters[parameterCount]{};
VoxInstrumentDescriptorV2 gInstrument{};
bool gReady = false;

struct Instance {
    AcidEngine engine;
    std::atomic<std::uint32_t> fault{0};
    bool prepared = false;
    std::uint32_t maxBlock = 0;
    std::uint32_t maxMidi = 0;
    std::uint32_t maxParam = 0;
    std::uint32_t channels = 0;
    std::vector<VoxMidiEventV1> midi;
    std::vector<VoxMidiEventV1> segment;
    std::vector<std::uint32_t> midiOrder;
    std::vector<std::uint32_t> paramOrder;
    std::vector<float*> outputs;
};

void silence(const VoxProcessContextV2* context) noexcept {
    if (context == nullptr || context->outputs == nullptr || context->numSamples == 0)
        return;
    for (std::uint32_t ch = 0; ch < context->outputChannelCount; ++ch) {
        if (context->outputs[ch] != nullptr)
            std::memset(context->outputs[ch], 0, sizeof(float) * context->numSamples);
    }
}

bool versioned(std::uint32_t structSize, std::uint32_t version, std::size_t known) noexcept {
    return structSize >= known && version == VOX_MODULE_ABI_V2;
}

void publishDescriptors() noexcept {
    if (gReady) return;
    AcidEngine fresh;
    const auto& table = controls();
    for (std::uint32_t i = 0; i < parameterCount; ++i) {
        const Control& control = table[i];
        float minimum = 0.0f, maximum = 1.0f, step = 0.0f;
        std::uint32_t type = VOX_PARAM_FLOAT;
        vstengine::acid::module::plainRange(control, minimum, maximum, step, type);
        float plainDefault = 0.0f;
        if (control.domain == Domain::playFlag) {
            const bool enabled = control.flagIndex == 0 ? fresh.isLegatoEnabled()
                : control.flagIndex == 1 ? fresh.isRetriggerEnabled()
                : control.flagIndex == 2 ? fresh.isVelToAccentEnabled()
                                          : fresh.isSlideOnOverlapEnabled();
            plainDefault = enabled ? 1.0f : 0.0f;
        } else {
            plainDefault = vstengine::acid::module::normToPlain(
                control, fresh.parameter(control.id));
        }
        plainDefault = std::clamp(plainDefault, minimum, maximum);
        auto& parameter = gParameters[i];
        parameter = {};
        parameter.structSize = sizeof(VoxParameterDescriptorV2);
        parameter.version = VOX_MODULE_ABI_V2;
        parameter.id = control.id;
        parameter.name = control.name;
        parameter.unit = control.unit;
        parameter.group = control.group;
        parameter.choices = control.choices;
        parameter.type = type;
        parameter.choiceCount = type == VOX_PARAM_CHOICE ? control.choiceCount : 0;
        parameter.minimum = minimum;
        parameter.maximum = maximum;
        parameter.defaultValue = plainDefault;
        parameter.step = step;
        parameter.poolIndex = static_cast<std::int32_t>(i);
        parameter.flags = VOX_PARAM_HOST_EXPOSED | VOX_PARAM_AUTOMATABLE
            | (continuous(control.domain) ? VOX_PARAM_MODULATABLE : 0u);
    }
    gInstrument = {};
    gInstrument.structSize = sizeof(VoxInstrumentDescriptorV2);
    gInstrument.version = VOX_MODULE_ABI_V2;
    gInstrument.id = instrumentId;
    gInstrument.name = instrumentName;
    gInstrument.vendor = "Ultima Vox";
    gInstrument.category = "Synth";
    gInstrument.instrumentVersion = 1;
    gInstrument.contentVersion = 1;
    gInstrument.stateVersion = stateVersion;
    gInstrument.parameterCount = parameterCount;
    gInstrument.parameters = gParameters;
    gInstrument.parameterStride = sizeof(VoxParameterDescriptorV2);
    gInstrument.maxVoices = 1;
    gInstrument.maxMidiEventsPerBlock = 512;
    gInstrument.maxStateBytes = static_cast<std::uint32_t>(AcidEngine::encodedStateBytes);
    gInstrument.latencySamples = 0;
    gInstrument.tailSamples = AcidEngine::maximumTailSamples;
    gInstrument.outputChannels = 2;
    gReady = true;
}

void currentPlayFlags(const AcidEngine& engine, bool enabled[4]) noexcept {
    enabled[0] = engine.isLegatoEnabled();
    enabled[1] = engine.isRetriggerEnabled();
    enabled[2] = engine.isVelToAccentEnabled();
    enabled[3] = engine.isSlideOnOverlapEnabled();
}

bool applyPlain(AcidEngine& engine, std::uint32_t index, float plain) noexcept {
    if (index >= parameterCount || !std::isfinite(plain)) return false;
    const Control& control = controls()[index];
    if (control.domain == Domain::playFlag) {
        bool enabled[4];
        currentPlayFlags(engine, enabled);
        if (control.flagIndex >= 0 && control.flagIndex < 4)
            enabled[control.flagIndex] = plain >= 0.5f;
        engine.setPlayModeOptions(enabled[0], enabled[1], enabled[2], enabled[3]);
        return true;
    }
    return engine.setParameter(control.id, plainToNorm(control, plain));
}

const VoxParamChangeV2* paramAt(const VoxProcessContextV2* context, std::uint32_t index) noexcept {
    return reinterpret_cast<const VoxParamChangeV2*>(
        reinterpret_cast<const std::uint8_t*>(context->paramChanges)
        + static_cast<std::size_t>(index) * context->paramChangeStride);
}

const VoxMidiEventV2* midiAt(const VoxProcessContextV2* context, std::uint32_t index) noexcept {
    return reinterpret_cast<const VoxMidiEventV2*>(
        reinterpret_cast<const std::uint8_t*>(context->midiEvents)
        + static_cast<std::size_t>(index) * context->midiEventStride);
}

VoxResult VOX_CALL getInstrument(std::uint32_t index, const VoxInstrumentDescriptorV2** out) {
    if (out == nullptr) return VOX_E_INVALID_ARG;
    *out = nullptr;
    if (!gReady || index != 0) return VOX_E_INVALID_ARG;
    *out = &gInstrument;
    return VOX_OK;
}

VoxResult VOX_CALL create(const char* id, const VoxCreateParamsV2* params, VoxInstance* out) {
    if (out == nullptr) return VOX_E_INVALID_ARG;
    *out = nullptr;
    if (!gReady || id == nullptr || params == nullptr
        || !versioned(params->structSize, params->version, sizeof(VoxCreateParamsV2))
        || std::strcmp(id, instrumentId) != 0)
        return VOX_E_INVALID_ARG;
    try {
        *out = reinterpret_cast<VoxInstance>(new Instance);
        return VOX_OK;
    } catch (const std::bad_alloc&) {
        return VOX_E_OUT_OF_MEMORY;
    }
}

VoxResult VOX_CALL destroy(VoxInstance handle) {
    if (handle == nullptr) return VOX_E_INVALID_ARG;
    delete reinterpret_cast<Instance*>(handle);
    return VOX_OK;
}

VoxResult VOX_CALL prepare(VoxInstance handle, const VoxPrepareSpecV2* spec) {
    if (handle == nullptr || spec == nullptr
        || !versioned(spec->structSize, spec->version, sizeof(VoxPrepareSpecV2)))
        return VOX_E_INVALID_ARG;
    auto* instance = reinterpret_cast<Instance*>(handle);
    instance->fault.store(0, std::memory_order_relaxed);
    if (spec->outputChannels == 0 || spec->maxBlockSize == 0
        || spec->maxMidiEventsPerBlock > 4096 || spec->maxParamChangesPerBlock > 4096
        || !instance->engine.prepare(spec->sampleRate, spec->maxBlockSize, spec->outputChannels)) {
        instance->prepared = false;
        return VOX_E_INVALID_ARG;
    }
    try {
        instance->maxBlock = spec->maxBlockSize;
        instance->maxMidi = spec->maxMidiEventsPerBlock;
        instance->maxParam = spec->maxParamChangesPerBlock;
        instance->channels = spec->outputChannels;
        instance->midi.resize(static_cast<std::size_t>(instance->maxMidi) + 48u);
        instance->segment.resize(instance->midi.size());
        instance->midiOrder.resize(instance->midi.size());
        instance->paramOrder.resize(instance->maxParam);
        instance->outputs.resize(instance->channels);
        instance->prepared = true;
        return VOX_OK;
    } catch (const std::bad_alloc&) {
        instance->prepared = false;
        return VOX_E_OUT_OF_MEMORY;
    }
}

VoxResult VOX_CALL setAllParameters(VoxInstance handle, const float* plain, std::uint32_t count) {
    if (handle == nullptr || plain == nullptr || count != parameterCount)
        return VOX_E_INVALID_ARG;
    auto* instance = reinterpret_cast<Instance*>(handle);
    std::uint8_t saved[AcidEngine::encodedStateBytes]{};
    std::uint32_t written = 0;
    if (!instance->engine.saveState(
            {reinterpret_cast<std::byte*>(saved), sizeof(saved)}, written))
        return VOX_E_INTERNAL;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!applyPlain(instance->engine, i, plain[i])) {
            if (!instance->engine.loadState(
                    stateVersion, {reinterpret_cast<const std::byte*>(saved), written}))
                return VOX_E_INTERNAL;
            return VOX_E_INVALID_ARG;
        }
    }
    return VOX_OK;
}

VoxResult VOX_CALL reset(VoxInstance handle) {
    if (handle == nullptr) return VOX_E_INVALID_ARG;
    auto* instance = reinterpret_cast<Instance*>(handle);
    instance->engine.reset();
    instance->fault.store(0, std::memory_order_relaxed);
    return VOX_OK;
}

void pushCc(Instance& instance, std::uint32_t& count, std::uint32_t offset,
            std::uint8_t channel, std::uint8_t cc) noexcept {
    if (count >= instance.midi.size()) return;
    auto& event = instance.midi[count];
    event.sampleOffset = offset;
    event.size = 3;
    event.data[0] = static_cast<std::uint8_t>(0xB0u | (channel & 0x0fu));
    event.data[1] = cc;
    event.data[2] = 0;
    ++count;
}

VoxResult VOX_CALL process(VoxInstance handle, const VoxProcessContextV2* context) {
    if (handle == nullptr || context == nullptr
        || !versioned(context->structSize, context->version,
                    VOX_PROCESS_CONTEXT_V2_BASE_SIZE)
        || context->transport == nullptr
        || !versioned(context->transport->structSize, context->transport->version,
                      sizeof(VoxTransportV2)))
        return VOX_E_INVALID_ARG;
    auto* instance = reinterpret_cast<Instance*>(handle);
    // Above the prepared cap the block is silenced and not rendered.
    // 128 samples at 48 kHz last 2.67 ms; this reject is the bound, not a
    // claim that the maximum 4096-event prepare fits that callback.
    if (!instance->prepared || context->numSamples > instance->maxBlock
        || context->outputChannelCount == 0
        || context->outputChannelCount > instance->channels
        || context->outputs == nullptr
        || (context->paramChangeCount > 0 && context->paramChanges == nullptr)
        || (context->midiEventCount > 0 && context->midiEvents == nullptr)
        || context->paramChangeCount > instance->maxParam
        || context->midiEventCount > instance->maxMidi
        || (context->paramChangeCount > 0
            && context->paramChangeStride < sizeof(VoxParamChangeV2))
        || (context->midiEventCount > 0
            && context->midiEventStride < sizeof(VoxMidiEventV2))
        || (context->releaseFlags & ~VOX_KNOWN_RELEASE_FLAGS) != 0
        || (context->transport->flags & ~VOX_TRANSPORT_KNOWN_FLAGS) != 0) {
        if (instance->prepared && context->midiEventCount > instance->maxMidi)
            instance->fault.store(kFaultMidiOverflow, std::memory_order_release);
        else if (instance->prepared && context->paramChangeCount > instance->maxParam)
            instance->fault.store(kFaultParamOverflow, std::memory_order_release);
        silence(context);
        return VOX_E_INVALID_ARG;
    }

    const VoxTransportV2* transport = context->transport;
    const bool playing = (transport->flags & VOX_TRANSPORT_PLAYING) != 0;
    const double bpm = (transport->flags & VOX_TRANSPORT_HAS_TEMPO) != 0
        ? transport->tempoBpm : 120.0;
    const double ppq = (transport->flags & VOX_TRANSPORT_HAS_PPQ) != 0
        ? transport->ppqPosition : 0.0;
    instance->engine.setTransport(bpm, ppq, playing);

    std::uint32_t midiCount = 0;
    if ((context->releaseFlags & VOX_PANIC_AT_START) != 0) {
        for (std::uint8_t channel = 0; channel < 16; ++channel)
            pushCc(*instance, midiCount, 0, channel, 120);
    }
    for (std::uint32_t i = 0; i < context->midiEventCount; ++i) {
        const VoxMidiEventV2* event = midiAt(context, i);
        if (event->size < 1 || event->size > 3 || event->sampleOffset >= context->numSamples)
            continue;
        if (midiCount >= instance->midi.size()) {
            silence(context);
            instance->fault.store(kFaultMidiOverflow, std::memory_order_release);
            return VOX_E_INVALID_ARG;
        }
        auto& slot = instance->midi[midiCount++];
        slot.sampleOffset = event->sampleOffset;
        slot.size = static_cast<std::uint8_t>(event->size);
        std::memcpy(slot.data, event->bytes, event->size);
    }
    const std::uint32_t endOffset = context->numSamples > 0 ? context->numSamples - 1 : 0;
    if ((context->releaseFlags & VOX_RELEASE_ALL_NOTES_AT_END) != 0 && context->numSamples > 0) {
        for (std::uint8_t channel = 0; channel < 16; ++channel)
            pushCc(*instance, midiCount, endOffset, channel, 123);
    }
    if ((context->releaseFlags & VOX_RELEASE_ALL_SOUND_AT_END) != 0 && context->numSamples > 0) {
        for (std::uint8_t channel = 0; channel < 16; ++channel)
            pushCc(*instance, midiCount, endOffset, channel, 120);
    }

    for (std::uint32_t i = 0; i < context->paramChangeCount; ++i)
        instance->paramOrder[i] = i;
    for (std::uint32_t i = 0; i < midiCount; ++i)
        instance->midiOrder[i] = i;
    // Index sort with an original-index tie break. std::sort does not allocate.
    // Equal offsets keep input order. Counts cannot exceed the prepare() cap.
    std::sort(instance->paramOrder.begin(),
              instance->paramOrder.begin() + context->paramChangeCount,
              [&](std::uint32_t a, std::uint32_t b) {
                  const std::uint32_t oa = paramAt(context, a)->sampleOffset;
                  const std::uint32_t ob = paramAt(context, b)->sampleOffset;
                  return oa < ob || (oa == ob && a < b);
              });
    std::sort(instance->midiOrder.begin(),
              instance->midiOrder.begin() + midiCount,
              [&](std::uint32_t a, std::uint32_t b) {
                  const std::uint32_t oa = instance->midi[a].sampleOffset;
                  const std::uint32_t ob = instance->midi[b].sampleOffset;
                  return oa < ob || (oa == ob && a < b);
              });

    std::uint32_t paramCursor = 0;
    std::uint32_t midiCursor = 0;
    std::uint32_t sample = 0;
    while (sample < context->numSamples) {
        while (paramCursor < context->paramChangeCount) {
            const VoxParamChangeV2* change = paramAt(context, instance->paramOrder[paramCursor]);
            if (change->sampleOffset > sample) break;
            if (change->sampleOffset == sample) {
                if (!std::isfinite(change->plain))
                    instance->fault.store(kFaultNonFiniteParam, std::memory_order_release);
                else
                    (void) applyPlain(instance->engine, change->index, change->plain);
            }
            ++paramCursor;
        }
        std::uint32_t next = context->numSamples;
        if (paramCursor < context->paramChangeCount) {
            const std::uint32_t offset = paramAt(context, instance->paramOrder[paramCursor])->sampleOffset;
            if (offset > sample && offset < next) next = offset;
        }
        if (midiCursor < midiCount) {
            const std::uint32_t offset = instance->midi[instance->midiOrder[midiCursor]].sampleOffset;
            if (offset > sample && offset < next) next = offset;
        }
        std::uint32_t segmentMidi = 0;
        while (midiCursor < midiCount
               && instance->midi[instance->midiOrder[midiCursor]].sampleOffset < next) {
            const std::uint32_t source = instance->midiOrder[midiCursor++];
            auto event = instance->midi[source];
            event.sampleOffset -= sample;
            instance->segment[segmentMidi++] = event;
        }
        for (std::uint32_t ch = 0; ch < context->outputChannelCount; ++ch)
            instance->outputs[ch] = context->outputs[ch] == nullptr
                ? nullptr : context->outputs[ch] + sample;
        instance->engine.process(
            {instance->outputs.data(), context->outputChannelCount},
            next - sample,
            {instance->segment.data(), segmentMidi});
        sample = next;
    }
    while (paramCursor < context->paramChangeCount) {
        const VoxParamChangeV2* change = paramAt(context, instance->paramOrder[paramCursor++]);
        if (change->sampleOffset >= context->numSamples) break;
    }

    for (std::uint32_t ch = 0; ch < context->outputChannelCount; ++ch) {
        const float* data = context->outputs[ch];
        if (data == nullptr) continue;
        for (std::uint32_t i = 0; i < context->numSamples; ++i) {
            if (!std::isfinite(data[i])) {
                silence(context);
                instance->fault.store(kFaultNonFiniteOutput, std::memory_order_release);
                return VOX_E_INTERNAL;
            }
        }
    }
    return VOX_OK;
}

VoxResult VOX_CALL saveState(VoxInstance handle, std::uint8_t* buffer, std::uint32_t capacity,
                             std::uint32_t* written, std::uint32_t* version) {
    if (handle == nullptr || written == nullptr || version == nullptr) return VOX_E_INVALID_ARG;
    *written = 0;
    *version = stateVersion;
    if (buffer == nullptr) return VOX_E_INVALID_ARG;
    auto* instance = reinterpret_cast<Instance*>(handle);
    std::uint32_t bytes = 0;
    if (capacity < AcidEngine::encodedStateBytes) return VOX_E_BUFFER_TOO_SMALL;
    if (!instance->engine.saveState({reinterpret_cast<std::byte*>(buffer), capacity}, bytes))
        return VOX_E_BUFFER_TOO_SMALL;
    *written = bytes;
    return VOX_OK;
}

VoxResult VOX_CALL loadState(VoxInstance handle, std::uint32_t version,
                             const std::uint8_t* bytes, std::uint32_t size) {
    if (handle == nullptr) return VOX_E_INVALID_ARG;
    if (version != stateVersion)
        return VOX_E_STATE_VERSION;
    if (bytes == nullptr || size != AcidEngine::encodedStateBytes) return VOX_E_STATE_CORRUPT;
    auto* instance = reinterpret_cast<Instance*>(handle);
    if (!instance->engine.loadState(version, {reinterpret_cast<const std::byte*>(bytes), size}))
        return VOX_E_STATE_CORRUPT;
    return VOX_OK;
}

VoxResult VOX_CALL shutdown() {
    return VOX_OK;
}

// One HOST note lane. NOTE / OCTAVE / ACCENT / SLIDE / GATE are fields of
// that lane, not five pattern stores. The Rack owns the pattern and schedules
// the MIDI; this module does not keep a second copy.
VoxResult VOX_CALL getPatternCaps(const char* id, const VoxPatternCapsV2** out) {
    if (out == nullptr) return VOX_E_INVALID_ARG;
    *out = nullptr;
    if (id == nullptr || std::strcmp(id, instrumentId) != 0) return VOX_E_INVALID_ARG;
    static const char* const laneNames[] = {"NOTE"};
    static const VoxPatternCapsV2 caps{
        sizeof(VoxPatternCapsV2),
        VOX_MODULE_ABI_V2,
        VOX_PATTERN_PLAYBACK_HOST,
        1,
        16,
        4,
        VOX_FIELD_NOTE | VOX_FIELD_OCTAVE | VOX_FIELD_ACCENT | VOX_FIELD_SLIDE |
            VOX_FIELD_GATE | VOX_FIELD_VELOCITY | VOX_FIELD_TIE,
        1,
        laneNames,
        nullptr};
    *out = &caps;
    return VOX_OK;
}

VoxResult VOX_CALL getFactoryPattern(const char* id, std::uint32_t index,
                                    const char** name, VoxPatternV2* pattern) {
    if (id == nullptr || std::strcmp(id, instrumentId) != 0 || name == nullptr ||
        pattern == nullptr || pattern->cells == nullptr || pattern->cellCapacity < 16 ||
        pattern->cellStride < sizeof(VoxPatternStepV2))
        return VOX_E_INVALID_ARG;
    if (index != 0) return VOX_E_INVALID_ARG;
    static const char* patternName = "Classic";
    // 16 steps, root-relative. Accent and slide are flags; unused fields stay
    // at the canonical defaults so the Rack can store the pattern.
    static constexpr std::int8_t notes[16] = {
        0, 0, 12, 0, 3, 3, 0, 7, 0, 10, 12, 0, 7, 3, 0, 0};
    static constexpr std::uint8_t gates[16] = {
        1, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 1};
    static constexpr std::uint8_t accents[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0};
    static constexpr std::uint8_t slides[16] = {
        0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    auto* bytes = reinterpret_cast<std::uint8_t*>(pattern->cells);
    for (std::uint32_t step = 0; step < 16; ++step) {
        auto& cell = *reinterpret_cast<VoxPatternStepV2*>(
            bytes + static_cast<std::size_t>(step) * pattern->cellStride);
        cell = {};
        cell.velocity = VOX_STEP_DEFAULT_VELOCITY;
        cell.gate = VOX_STEP_DEFAULT_GATE;
        cell.probability = VOX_STEP_DEFAULT_PROBABILITY;
        cell.ratchet = VOX_STEP_DEFAULT_RATCHET;
        if (gates[step] == 0) continue;
        cell.flags = static_cast<std::uint8_t>(
            VOX_STEP_ON | (accents[step] ? VOX_STEP_ACCENT : 0) |
            (slides[step] ? VOX_STEP_SLIDE : 0));
        cell.note = notes[step];
    }
    pattern->laneCount = 1;
    pattern->stepsPerBar = 16;
    pattern->barCount = 1;
    pattern->timing = VOX_TIMING_SIXTEENTH;
    *name = patternName;
    return VOX_OK;
}

const VoxContentApiV2 gContent{
    sizeof(VoxContentApiV2), VOX_MODULE_ABI_V2, getPatternCaps, getFactoryPattern};

const VoxModuleV2 gModule{
    sizeof(VoxModuleV2),
    VOX_MODULE_ABI_V2,
    1,
    0,
    getInstrument,
    create,
    destroy,
    prepare,
    setAllParameters,
    reset,
    process,
    saveState,
    loadState,
    &gContent,
    nullptr,
    shutdown
};

extern "C" VOX_EXPORT VoxResult VOX_CALL voxModuleEntryV2(
    std::uint32_t hostAbiVersion, const VoxHostServicesV2* host, const VoxModuleV2** module) {
    if (module == nullptr) return VOX_E_INVALID_ARG;
    *module = nullptr;
    if (hostAbiVersion != VOX_MODULE_ABI_V2 || host == nullptr
        || host->structSize < sizeof(VoxHostServicesV2) || host->version != VOX_MODULE_ABI_V2)
        return VOX_E_UNSUPPORTED;
    gHost = *host;
    if (gHost.version != host->version)
        return VOX_E_UNSUPPORTED;
    publishDescriptors();
    *module = &gModule;
    return VOX_OK;
}

#if defined(VOX_ACID_TEST_HOOKS)
extern "C" VOX_EXPORT std::uint32_t voxAcid303TestFault(VoxInstance handle) {
    if (handle == nullptr) return 0xffffffffu;
    return reinterpret_cast<Instance*>(handle)->fault.load(std::memory_order_acquire);
}
#endif
