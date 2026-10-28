/* Vox instrument module ABI V2 — PROPOSED, NOT FROZEN.
 *
 * Pure C header, valid as C11 and C++20. Contract and rationale:
 * docs/architecture/INSTRUMENT_MODULE_ABI.md. Slice 1 only proves the binary
 * boundary with a test module; no production loader uses this yet.
 *
 * Rules (summary):
 * - Every function pointer returns VoxResult; nothing unwinds across it.
 * - Versioned structs start with structSize/version. Readers accept a larger
 *   structSize (newer minor version) and reject a smaller one.
 * - VoxParamChangeV2 and VoxMidiEventV2 are header-less records; their arrays
 *   carry an element stride in the versioned parent.
 * - Fixed-width types, natural alignment, no padding on 64-bit targets.
 * - Memory is freed by whoever allocated it. Host arguments are valid only for
 *   the call; module descriptors stay valid until shutdown returns.
 */
#ifndef VOX_MODULE_ABI_V2_H
#define VOX_MODULE_ABI_V2_H

#include <stddef.h>
#include <stdint.h>

#if UINTPTR_MAX != 0xFFFFFFFFFFFFFFFFu
#error "Vox module ABI V2 is defined for 64-bit targets only"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define VOX_CALL __cdecl
#define VOX_EXPORT __declspec(dllexport)
#else
#define VOX_CALL
#define VOX_EXPORT __attribute__((visibility("default")))
#endif

#define VOX_MODULE_ABI_V2 2u
#define VOX_MODULE_ENTRY_V2_NAME "voxModuleEntryV2"

typedef int32_t VoxResult;
#define VOX_OK 0
#define VOX_E_INVALID_ARG (-1)
#define VOX_E_UNSUPPORTED (-2)
#define VOX_E_BUFFER_TOO_SMALL (-3)
#define VOX_E_STATE_VERSION (-4)
#define VOX_E_STATE_CORRUPT (-5)
#define VOX_E_OUT_OF_MEMORY (-6)
#define VOX_E_INTERNAL (-7)

/* Parameter types and flags. */
#define VOX_PARAM_FLOAT 0u
#define VOX_PARAM_INTEGER 1u
#define VOX_PARAM_BOOLEAN 2u
#define VOX_PARAM_CHOICE 3u

#define VOX_PARAM_HOST_EXPOSED 0x1u
#define VOX_PARAM_AUTOMATABLE 0x2u
#define VOX_PARAM_MODULATABLE 0x4u
#define VOX_PARAM_KNOWN_FLAGS 0x7u

/* VoxProcessContextV2.releaseFlags. */
#define VOX_PANIC_AT_START 0x1u
#define VOX_RELEASE_ALL_NOTES_AT_END 0x2u
#define VOX_RELEASE_ALL_SOUND_AT_END 0x4u
#define VOX_KNOWN_RELEASE_FLAGS 0x7u

/* VoxTransportV2.flags: a value is meaningful only with its HAS_* flag. */
#define VOX_TRANSPORT_HAS_TEMPO 0x1u
#define VOX_TRANSPORT_HAS_PPQ 0x2u
#define VOX_TRANSPORT_HAS_BAR_START 0x4u
#define VOX_TRANSPORT_HAS_TIME_SIGNATURE 0x8u
#define VOX_TRANSPORT_HAS_TIME 0x10u
#define VOX_TRANSPORT_PLAYING 0x20u
#define VOX_TRANSPORT_LOOPING 0x40u
#define VOX_TRANSPORT_DISCONTINUITY 0x80u
#define VOX_TRANSPORT_KNOWN_FLAGS 0xFFu

typedef struct VoxInstanceT *VoxInstance; /* opaque, owned by the module */
typedef struct VoxContentApiV2 VoxContentApiV2; /* reserved; NULL in slice 1 */
typedef struct VoxEditorApiV2 VoxEditorApiV2;   /* reserved; NULL in slice 1 */

typedef struct VoxHostServicesV2 {
  uint32_t structSize, version;
  void *hostContext;
  /* Message thread only. `message` is valid for the call. */
  VoxResult(VOX_CALL *log)(void *hostContext, uint32_t level, const char *message);
} VoxHostServicesV2;

typedef struct VoxParameterDescriptorV2 {
  uint32_t structSize, version;
  const char *id;
  const char *name;
  const char *unit;
  const char *group;
  const char *const *choices; /* choiceCount entries for VOX_PARAM_CHOICE */
  uint32_t type;
  uint32_t choiceCount;
  float minimum, maximum, defaultValue, step;
  int32_t poolIndex; /* 0..pool-1 with VOX_PARAM_HOST_EXPOSED, else -1 */
  uint32_t flags;
} VoxParameterDescriptorV2;

typedef struct VoxInstrumentDescriptorV2 {
  uint32_t structSize, version;
  const char *id;
  const char *name;
  const char *vendor;
  const char *category;
  uint32_t instrumentVersion, contentVersion, stateVersion, parameterCount;
  const VoxParameterDescriptorV2 *parameters; /* stepped by parameterStride */
  uint32_t parameterStride;
  uint32_t maxVoices;
  uint32_t maxMidiEventsPerBlock;
  uint32_t maxStateBytes;
  uint32_t latencySamples;
  uint32_t tailSamples;
  uint32_t outputChannels;
  uint32_t reserved0;
} VoxInstrumentDescriptorV2;

typedef struct VoxCreateParamsV2 {
  uint32_t structSize, version;
  uint32_t slotIndex;
  uint32_t reserved0;
} VoxCreateParamsV2;

typedef struct VoxPrepareSpecV2 {
  uint32_t structSize, version;
  double sampleRate;
  uint32_t maxBlockSize;
  uint32_t outputChannels;
  uint32_t maxParamChangesPerBlock;
  uint32_t maxMidiEventsPerBlock;
} VoxPrepareSpecV2;

/* Header-less records. */
typedef struct VoxParamChangeV2 {
  uint32_t index; /* descriptor index */
  uint32_t sampleOffset;
  float plain;
  uint32_t reserved;
} VoxParamChangeV2;

typedef struct VoxMidiEventV2 {
  uint32_t sampleOffset;
  uint32_t size;
  uint8_t bytes[8];
} VoxMidiEventV2;

/* Host musical transport at sample 0 of the block (block-rate tempo).
 * Modules never infer tempo or position from samplePosition or wall clock.
 * DISCONTINUITY: first block after prepare/reset/load, start/stop, or a
 * position jump (seek, loop wrap) larger than half a sample. */
typedef struct VoxTransportV2 {
  uint32_t structSize, version;
  uint32_t flags;
  uint32_t timeSigNumerator;
  uint32_t timeSigDenominator;
  uint32_t reserved0;
  double tempoBpm;
  double ppqPosition;
  double barStartPpq;
  double loopStartPpq;
  double loopEndPpq;
  int64_t timeInSamples;
} VoxTransportV2;

typedef struct VoxProcessContextV2 {
  uint32_t structSize, version;
  uint32_t numSamples;
  uint32_t outputChannelCount;
  float *const *outputs; /* host-owned, overwritten by the module */
  const VoxParamChangeV2 *paramChanges;
  uint32_t paramChangeCount;
  uint32_t paramChangeStride;
  const VoxMidiEventV2 *midiEvents;
  uint32_t midiEventCount;
  uint32_t midiEventStride;
  uint32_t releaseFlags;
  uint32_t reserved0;
  uint64_t samplePosition;
  const VoxTransportV2 *transport; /* host-owned, never NULL */
} VoxProcessContextV2;

typedef struct VoxModuleV2 {
  uint32_t structSize, version;
  uint32_t instrumentCount, reserved0;
  VoxResult(VOX_CALL *getInstrument)(uint32_t index,
                                     const VoxInstrumentDescriptorV2 **out);
  VoxResult(VOX_CALL *create)(const char *instrumentId, const VoxCreateParamsV2 *params,
                              VoxInstance *out);
  VoxResult(VOX_CALL *destroy)(VoxInstance instance);
  VoxResult(VOX_CALL *prepare)(VoxInstance instance, const VoxPrepareSpecV2 *spec);
  VoxResult(VOX_CALL *setAllParameters)(VoxInstance instance, const float *plain,
                                        uint32_t count);
  VoxResult(VOX_CALL *reset)(VoxInstance instance);
  VoxResult(VOX_CALL *process)(VoxInstance instance, const VoxProcessContextV2 *context);
  VoxResult(VOX_CALL *saveState)(VoxInstance instance, uint8_t *buffer, uint32_t capacity,
                                 uint32_t *written, uint32_t *stateVersion);
  VoxResult(VOX_CALL *loadState)(VoxInstance instance, uint32_t stateVersion,
                                 const uint8_t *bytes, uint32_t size);
  const VoxContentApiV2 *content;
  const VoxEditorApiV2 *editor;
  VoxResult(VOX_CALL *shutdown)(void);
} VoxModuleV2;

/* The only export of a module. Called once per loaded module per process. */
typedef VoxResult(VOX_CALL *VoxModuleEntryV2Fn)(uint32_t hostAbiVersion,
                                                const VoxHostServicesV2 *host,
                                                const VoxModuleV2 **module);
#if defined(VOX_BUILDING_MODULE)
VOX_EXPORT VoxResult VOX_CALL voxModuleEntryV2(uint32_t hostAbiVersion,
                                               const VoxHostServicesV2 *host,
                                               const VoxModuleV2 **module);
#endif

/* Layout is part of the contract; checked by every C and C++ includer. */
#if defined(__cplusplus)
#define VOX_ABI_ASSERT(expr, msg) static_assert(expr, msg)
#else
#define VOX_ABI_ASSERT(expr, msg) _Static_assert(expr, msg)
#endif
VOX_ABI_ASSERT(sizeof(VoxHostServicesV2) == 24, "VoxHostServicesV2 size");
VOX_ABI_ASSERT(sizeof(VoxParameterDescriptorV2) == 80, "VoxParameterDescriptorV2 size");
VOX_ABI_ASSERT(offsetof(VoxParameterDescriptorV2, type) == 48, "parameter type offset");
VOX_ABI_ASSERT(offsetof(VoxParameterDescriptorV2, minimum) == 56, "parameter minimum offset");
VOX_ABI_ASSERT(offsetof(VoxParameterDescriptorV2, poolIndex) == 72, "parameter pool offset");
VOX_ABI_ASSERT(sizeof(VoxInstrumentDescriptorV2) == 96, "VoxInstrumentDescriptorV2 size");
VOX_ABI_ASSERT(offsetof(VoxInstrumentDescriptorV2, parameters) == 56, "parameters offset");
VOX_ABI_ASSERT(offsetof(VoxInstrumentDescriptorV2, outputChannels) == 88, "outputs offset");
VOX_ABI_ASSERT(sizeof(VoxCreateParamsV2) == 16, "VoxCreateParamsV2 size");
VOX_ABI_ASSERT(sizeof(VoxPrepareSpecV2) == 32, "VoxPrepareSpecV2 size");
VOX_ABI_ASSERT(offsetof(VoxPrepareSpecV2, sampleRate) == 8, "sampleRate offset");
VOX_ABI_ASSERT(sizeof(VoxParamChangeV2) == 16, "VoxParamChangeV2 size");
VOX_ABI_ASSERT(sizeof(VoxMidiEventV2) == 16, "VoxMidiEventV2 size");
VOX_ABI_ASSERT(sizeof(VoxTransportV2) == 72, "VoxTransportV2 size");
VOX_ABI_ASSERT(offsetof(VoxTransportV2, tempoBpm) == 24, "tempo offset");
VOX_ABI_ASSERT(offsetof(VoxTransportV2, timeInSamples) == 64, "timeInSamples offset");
VOX_ABI_ASSERT(sizeof(VoxProcessContextV2) == 80, "VoxProcessContextV2 size");
VOX_ABI_ASSERT(offsetof(VoxProcessContextV2, transport) == 72, "transport offset");
VOX_ABI_ASSERT(offsetof(VoxProcessContextV2, outputs) == 16, "outputs pointer offset");
VOX_ABI_ASSERT(offsetof(VoxProcessContextV2, midiEvents) == 40, "midiEvents offset");
VOX_ABI_ASSERT(offsetof(VoxProcessContextV2, releaseFlags) == 56, "releaseFlags offset");
VOX_ABI_ASSERT(offsetof(VoxProcessContextV2, samplePosition) == 64, "samplePosition offset");
VOX_ABI_ASSERT(sizeof(VoxModuleV2) == 112, "VoxModuleV2 size");
VOX_ABI_ASSERT(offsetof(VoxModuleV2, getInstrument) == 16, "getInstrument offset");
VOX_ABI_ASSERT(offsetof(VoxModuleV2, content) == 88, "content offset");
VOX_ABI_ASSERT(offsetof(VoxModuleV2, shutdown) == 104, "shutdown offset");
#undef VOX_ABI_ASSERT

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VOX_MODULE_ABI_V2_H */
