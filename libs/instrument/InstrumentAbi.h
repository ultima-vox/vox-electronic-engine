#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

// External-module-ready C ABI. No STL, JUCE, exceptions, ownership-bearing
// pointers or compiler-specific classes may cross this boundary.
extern "C" {

enum : std::uint32_t {
    VOX_INSTRUMENT_ABI_V1 = 1,
    VOX_INSTRUMENT_DESCRIPTOR_SCHEMA_V1 = 1,
    VOX_INSTRUMENT_STATE_SCHEMA_V1 = 1,
    VOX_INSTRUMENT_CONTENT_API_V1 = 1,
    VOX_PATTERN_SCHEMA_V1 = 1,
    // V2 (pattern/Pattern.h, VPT2): Rack-owned pattern of an instrument that
    // declares INTERNAL playback through VoxContentApiV2.
    VOX_PATTERN_SCHEMA_V2 = 2,
    VOX_PATTERN_MAX_STEPS = 64,
    VOX_INSTRUMENT_MAX_ID_BYTES = 64,
    VOX_INSTRUMENT_MAX_NAME_BYTES = 96
};

struct VoxInstrumentIdV1 {
    char bytes[VOX_INSTRUMENT_MAX_ID_BYTES];
};

struct VoxInstrumentBudgetV1 {
    std::uint32_t maxVoices;
    std::uint32_t maxMidiEventsPerBlock;
    std::uint32_t maxStateBytes;
    std::uint32_t maxResourceBytes;
    std::uint32_t maxLatencySamples;
    std::uint32_t maxTailSamples;
    std::uint32_t maxPatternEvents;
    std::uint32_t maxModulationRoutes;
    std::uint32_t scratchBytes;
};

struct VoxParameterDescriptorV1 {
    const char* parameterId;
    const char* displayName;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    float step;
    std::uint32_t type;
    const char* group;
    std::int32_t preferredMacro;
    std::uint32_t flags;
};

struct VoxInstrumentDescriptorV1 {
    std::uint32_t structSize;
    std::uint32_t abiVersion;
    std::uint32_t descriptorSchemaVersion;
    std::uint32_t stateSchemaVersion;
    VoxInstrumentIdV1 instrumentId;
    VoxInstrumentIdV1 providerId;
    const char* displayName;
    const char* vendor;
    std::uint32_t instrumentVersion;
    std::uint32_t minimumHostVersion;
    std::uint32_t contentVersion;
    std::uint64_t capabilityFlags;
    std::uint32_t tailPolicy;
    const std::uint32_t* supportedPresetSchemaVersions;
    std::uint32_t supportedPresetSchemaVersionCount;
    VoxInstrumentBudgetV1 budget;
    const VoxParameterDescriptorV1* parameters;
    std::uint32_t parameterCount;
};

struct VoxMidiEventV1 {
    std::uint32_t sampleOffset;
    std::uint8_t size;
    std::uint8_t data[3];
};

struct VoxProcessContextV1 {
    std::uint32_t structSize;
    float** outputs;
    std::uint32_t outputChannels;
    std::uint32_t sampleCount;
    const VoxMidiEventV1* midiEvents;
    std::uint32_t midiEventCount;
    double sampleRate;
    double bpm;
    double ppqPosition;
    std::uint32_t transportFlags;
};

struct VoxPrepareSpecV1 {
    std::uint32_t structSize;
    double sampleRate;
    std::uint32_t maximumBlockSize;
    std::uint32_t outputChannels;
};

using VoxInstrumentHandleV1 = void*;

// Optional musical-content ABI. Kept separate from VoxInstrumentApiV1 so the
// accepted realtime DSP ABI remains byte-stable. All buffers are caller-owned;
// generation/preset operations run on the control thread.
enum VoxContentKindV1 : std::uint32_t {
    VOX_CONTENT_SOUND_PRESET = 1,
    VOX_CONTENT_PATTERN_PRESET = 2,
    VOX_CONTENT_GENERATOR_PROFILE = 3
};

enum VoxContentStatusV1 : std::uint32_t {
    VOX_CONTENT_OK = 0,
    VOX_CONTENT_UNSUPPORTED = 1,
    VOX_CONTENT_NOT_FOUND = 2,
    VOX_CONTENT_INCOMPATIBLE = 3,
    VOX_CONTENT_INVALID_ARGUMENT = 4,
    VOX_CONTENT_BUDGET_EXCEEDED = 5
};

enum VoxSequenceFieldV1 : std::uint32_t {
    VOX_SEQUENCE_GATE = 1u << 0,
    VOX_SEQUENCE_NOTE = 1u << 1,
    VOX_SEQUENCE_VELOCITY = 1u << 2,
    VOX_SEQUENCE_ACCENT = 1u << 3,
    VOX_SEQUENCE_PROBABILITY = 1u << 4,
    VOX_SEQUENCE_RATCHET = 1u << 5,
    VOX_SEQUENCE_SLIDE = 1u << 6,
    VOX_SEQUENCE_GATE_WIDTH = 1u << 7,
    VOX_SEQUENCE_ALL = 0xffu
};

enum VoxPresetClassV1 : std::uint32_t {
    VOX_PRESET_CLASS_SOUND = 1u << 0,
    VOX_PRESET_CLASS_PATTERN = 1u << 1,
    VOX_PRESET_CLASS_GENERATOR = 1u << 2
};

struct VoxContentDescriptorV1 {
    std::uint32_t structSize;
    std::uint32_t kind;
    VoxInstrumentIdV1 instrumentId;
    VoxInstrumentIdV1 contentId;
    const char* displayName;
    std::uint32_t schemaVersion;
    std::uint32_t contentVersion;
    std::uint32_t supportedSequenceFields;
    std::uint32_t supportedPresetClasses;
    std::uint32_t macroValueMask;
    float macroValues[8];
};

struct VoxPatternStepV1 {
    std::int16_t noteOffset;
    std::uint8_t gate;
    std::uint8_t accent;
    float velocity;
    float probability;
    std::uint8_t ratchetCount;
    std::uint8_t reserved[3];
    float slideDuration;
    float gateWidth;
};

struct VoxPatternV1 {
    std::uint32_t structSize;
    std::uint32_t schemaVersion;
    std::uint32_t stepCount;
    std::uint32_t timingMode;
    VoxPatternStepV1 steps[VOX_PATTERN_MAX_STEPS];
};

struct VoxGenerationContextV1 {
    std::uint32_t structSize;
    std::uint32_t globalSeed;
    std::uint64_t slotId;
    VoxInstrumentIdV1 instrumentId;
    VoxInstrumentIdV1 styleId;
    std::int32_t rootNote;
    std::int32_t scale;
    double bpm;
    float density;
    float darkness;
    float energy;
    float movement;
    float complexity;
    float mutation;
};

struct VoxInstrumentContentApiV1 {
    std::uint32_t structSize;
    std::uint32_t apiVersion;
    std::uint32_t (*descriptorCount)(const VoxInstrumentIdV1*) noexcept;
    const VoxContentDescriptorV1* (*descriptorAt)(
        const VoxInstrumentIdV1*, std::uint32_t) noexcept;
    std::uint32_t (*generatePattern)(const VoxInstrumentIdV1*,
        const VoxInstrumentIdV1*, const VoxGenerationContextV1*,
        const VoxPatternV1*, VoxPatternV1*) noexcept;
    std::uint32_t (*applySoundPreset)(VoxInstrumentHandleV1,
        const VoxInstrumentIdV1*) noexcept;
};

using VoxCreateInstrumentV1 = VoxInstrumentHandleV1 (*)(
    const VoxInstrumentIdV1*, const void* hostServices) noexcept;
using VoxDestroyInstrumentV1 = void (*)(VoxInstrumentHandleV1) noexcept;
using VoxPrepareInstrumentV1 = bool (*)(VoxInstrumentHandleV1,
                                        const VoxPrepareSpecV1*) noexcept;
using VoxResetInstrumentV1 = void (*)(VoxInstrumentHandleV1) noexcept;
using VoxSuspendInstrumentV1 = void (*)(VoxInstrumentHandleV1) noexcept;
using VoxResumeInstrumentV1 = void (*)(VoxInstrumentHandleV1) noexcept;
using VoxSetInstrumentBypassedV1 = void (*)(VoxInstrumentHandleV1,
                                            bool) noexcept;
using VoxProcessInstrumentV1 = void (*)(VoxInstrumentHandleV1,
                                        const VoxProcessContextV1*) noexcept;
using VoxSetParameterV1 = bool (*)(VoxInstrumentHandleV1, const char*,
                                   float) noexcept;
using VoxLoadStateV1 = bool (*)(VoxInstrumentHandleV1, std::uint32_t,
                                const std::uint8_t*, std::uint32_t) noexcept;
using VoxSaveStateV1 = bool (*)(VoxInstrumentHandleV1, std::uint8_t*,
                                std::uint32_t, std::uint32_t*) noexcept;
using VoxGetSampleCountV1 = std::uint32_t (*)(VoxInstrumentHandleV1) noexcept;

struct VoxInstrumentApiV1 {
    std::uint32_t structSize;
    std::uint32_t abiVersion;
    const VoxInstrumentDescriptorV1* (*descriptorAt)(std::uint32_t) noexcept;
    std::uint32_t (*descriptorCount)() noexcept;
    VoxCreateInstrumentV1 create;
    VoxDestroyInstrumentV1 destroy;
    VoxPrepareInstrumentV1 prepare;
    VoxResetInstrumentV1 reset;
    VoxSuspendInstrumentV1 suspend;
    VoxResumeInstrumentV1 resume;
    VoxSetInstrumentBypassedV1 setBypassed;
    VoxProcessInstrumentV1 process;
    VoxSetParameterV1 setParameter;
    VoxLoadStateV1 loadState;
    VoxSaveStateV1 saveState;
    VoxGetSampleCountV1 latencySamples;
    VoxGetSampleCountV1 tailSamples;
};

using VoxGetInstrumentApiV1 = const VoxInstrumentApiV1* (*)() noexcept;

} // extern "C"

static_assert(sizeof(VoxMidiEventV1) == 8);
static_assert(std::is_standard_layout_v<VoxInstrumentDescriptorV1>);
static_assert(std::is_trivially_copyable_v<VoxProcessContextV1>);
static_assert(std::is_trivially_copyable_v<VoxPatternStepV1>);
static_assert(std::is_trivially_copyable_v<VoxPatternV1>);
static_assert(std::is_trivially_copyable_v<VoxGenerationContextV1>);
