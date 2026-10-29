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
 * - Appended fields: VoxProcessContextV2 grows only at its end. Its readers
 *   accept VOX_PROCESS_CONTEXT_V2_BASE_SIZE and read a later field only when
 *   structSize covers it.
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
typedef struct VoxEditorApiV2 VoxEditorApiV2; /* reserved; always NULL */

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

#define VOX_SLOT_INDEX_NONE 0xFFFFFFFFu

typedef struct VoxCreateParamsV2 {
  uint32_t structSize, version;
  uint32_t slotIndex; /* 0-based Rack slot ordinal, or VOX_SLOT_INDEX_NONE.
                         Positional and informational; never an identity. */
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

/* ---- Patterns (content API) ----------------------------------------------
 * One bounded pattern model for every instrument. The Rack owns the durable
 * copy and its edits; a module only reads the snapshot it is handed. Lanes are
 * independent rows (drum pads, or one note lane); cells are stored
 * [lane][bar][step]. Fields an instrument does not declare in
 * VoxPatternCapsV2.stepFields must hold the VOX_STEP_DEFAULT_* values. */
#define VOX_PATTERN_V2_MAX_LANES 16u
#define VOX_PATTERN_V2_MAX_STEPS_PER_BAR 64u
#define VOX_PATTERN_V2_MAX_BARS 4u
#define VOX_PATTERN_V2_MAX_CELLS 4096u /* lanes * steps per bar * bars */

/* VoxPatternStepV2.flags */
#define VOX_STEP_ON 0x1u     /* the step sounds; otherwise it is a rest */
#define VOX_STEP_ACCENT 0x2u
#define VOX_STEP_SLIDE 0x4u  /* glide from this step into the next sounding one */
#define VOX_STEP_TIE 0x8u    /* continue the previous note, no new attack */
#define VOX_STEP_KNOWN_FLAGS 0xFu

/* Canonical values of fields an instrument does not read. */
#define VOX_STEP_DEFAULT_VELOCITY 100u
#define VOX_STEP_DEFAULT_GATE 192u /* 0.75 step */
#define VOX_STEP_DEFAULT_PROBABILITY 100u
#define VOX_STEP_DEFAULT_RATCHET 1u
#define VOX_STEP_GATE_UNIT 256u /* gate and slide are in 1/256 step */

/* VoxPatternCapsV2.stepFields */
#define VOX_FIELD_VELOCITY 0x1u
#define VOX_FIELD_ACCENT 0x2u
#define VOX_FIELD_NOTE 0x4u
#define VOX_FIELD_OCTAVE 0x8u
#define VOX_FIELD_SLIDE 0x10u
#define VOX_FIELD_TIE 0x20u
#define VOX_FIELD_GATE 0x40u
#define VOX_FIELD_PROBABILITY 0x80u
#define VOX_FIELD_RATCHET 0x100u
#define VOX_FIELD_KNOWN 0x1FFu

/* VoxPatternCapsV2.playback */
#define VOX_PATTERN_PLAYBACK_HOST 1u     /* the Rack schedules notes from lane 0 */
#define VOX_PATTERN_PLAYBACK_INTERNAL 2u /* the module sequences the snapshot */

/* VoxPatternV2.timing: step length. */
#define VOX_TIMING_SIXTEENTH 0u
#define VOX_TIMING_EIGHTH 1u
#define VOX_TIMING_THIRTY_SECOND 2u
#define VOX_TIMING_EIGHTH_TRIPLET 3u

/* Header-less record; arrays carry their stride in VoxPatternV2. */
typedef struct VoxPatternStepV2 {
  uint8_t flags;       /* VOX_STEP_* */
  uint8_t velocity;    /* 1..127 */
  int8_t note;         /* semitones from the slot root, -48..48 */
  int8_t octave;       /* -4..4; pitch = root + note + 12 * octave */
  uint16_t gate;       /* sounding length, 1..2048 (VOX_STEP_GATE_UNIT) */
  uint16_t slide;      /* glide time, 0..2048 (VOX_STEP_GATE_UNIT) */
  uint8_t probability; /* 0..100 percent */
  uint8_t ratchet;     /* 1..8 retriggers inside the step */
  uint8_t reserved[6]; /* zero */
} VoxPatternStepV2;

typedef struct VoxPatternV2 {
  uint32_t structSize, version;
  uint32_t laneCount;    /* 1..caps.laneCount */
  uint32_t stepsPerBar;  /* 1..caps.maxStepsPerBar */
  uint32_t barCount;     /* 1..caps.maxBars */
  uint32_t timing;       /* VOX_TIMING_* */
  uint32_t cellStride;   /* >= sizeof(VoxPatternStepV2) */
  uint32_t cellCapacity; /* cells behind `cells`; >= lane * step * bar count */
  VoxPatternStepV2 *cells; /* host-owned; read-only for the module in process */
} VoxPatternV2;

typedef struct VoxPatternCapsV2 {
  uint32_t structSize, version;
  uint32_t playback;            /* VOX_PATTERN_PLAYBACK_* (exactly one) */
  uint32_t laneCount;           /* lanes the instrument plays, 1..16 */
  uint32_t maxStepsPerBar;      /* 1..64 */
  uint32_t maxBars;             /* 1..4 */
  uint32_t stepFields;          /* VOX_FIELD_* the instrument reads */
  uint32_t factoryPatternCount; /* 0..64 */
  const char *const *laneNames; /* laneCount names */
  const uint8_t *laneNotes;     /* laneCount distinct MIDI notes for INTERNAL
                                   playback (export and host MIDI); NULL for HOST */
} VoxPatternCapsV2;

typedef struct VoxContentApiV2 {
  uint32_t structSize, version;
  /* Any thread. *out is NULL when the instrument has no pattern; otherwise it
   * stays valid until shutdown. */
  VoxResult(VOX_CALL *getPatternCaps)(const char *instrumentId, const VoxPatternCapsV2 **out);
  /* Message thread. The host sets structSize, version, cellStride, cellCapacity
   * and cells; the module writes the dimensions, timing and cells of factory
   * pattern `index` and its display name (valid until shutdown). */
  VoxResult(VOX_CALL *getFactoryPattern)(const char *instrumentId, uint32_t index,
                                         const char **name, VoxPatternV2 *pattern);
} VoxContentApiV2;

/* ---- Note output ----------------------------------------------------------
 * Notes a module generates itself (an INTERNAL sequencer hit that sounds),
 * reported per block so the Rack can duck, export and display them. Host MIDI
 * the module merely receives is never reported: the Rack already knows it. */
#define VOX_NOTE_EVENT_ACCENT 0x1u
#define VOX_NOTE_EVENT_KNOWN_FLAGS 0x1u

/* Header-less record; arrays carry their stride in VoxNoteOutputV2. */
typedef struct VoxNoteEventV2 {
  uint32_t sampleOffset; /* within this block, ascending */
  uint8_t note;          /* MIDI note */
  uint8_t velocity;      /* 1..127, as played (accent applied) */
  uint8_t lane;          /* pattern lane */
  uint8_t flags;         /* VOX_NOTE_EVENT_* */
} VoxNoteEventV2;

typedef struct VoxNoteOutputV2 {
  uint32_t structSize, version;
  uint32_t capacity;      /* host: events that fit in `events` */
  uint32_t stride;        /* host: >= sizeof(VoxNoteEventV2) */
  uint32_t count;         /* module: events written, <= capacity */
  uint32_t dropped;       /* module: events that did not fit */
  VoxNoteEventV2 *events; /* host-owned */
} VoxNoteOutputV2;

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
  /* Appended in V2.1. A module reads a field only when structSize covers it;
   * every host and module accepts VOX_PROCESS_CONTEXT_V2_BASE_SIZE. */
  const VoxPatternV2 *pattern; /* host-owned snapshot for this call, or NULL */
  uint64_t patternGeneration;  /* changes whenever the snapshot content changes */
  VoxNoteOutputV2 *noteOutput; /* host-owned; NULL = the host does not listen.
                                  The module resets count/dropped every call. */
} VoxProcessContextV2;

/* Size of the original V2.0 process context. */
#define VOX_PROCESS_CONTEXT_V2_BASE_SIZE 80u

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
VOX_ABI_ASSERT(sizeof(VoxProcessContextV2) == 104, "VoxProcessContextV2 size");
VOX_ABI_ASSERT(offsetof(VoxProcessContextV2, noteOutput) == 96, "noteOutput offset");
VOX_ABI_ASSERT(sizeof(VoxNoteEventV2) == 8, "VoxNoteEventV2 size");
VOX_ABI_ASSERT(sizeof(VoxNoteOutputV2) == 32, "VoxNoteOutputV2 size");
VOX_ABI_ASSERT(offsetof(VoxNoteOutputV2, events) == 24, "note output events offset");
VOX_ABI_ASSERT(offsetof(VoxProcessContextV2, pattern) == VOX_PROCESS_CONTEXT_V2_BASE_SIZE,
               "pattern offset");
VOX_ABI_ASSERT(offsetof(VoxProcessContextV2, patternGeneration) == 88, "generation offset");
VOX_ABI_ASSERT(sizeof(VoxPatternStepV2) == 16, "VoxPatternStepV2 size");
VOX_ABI_ASSERT(offsetof(VoxPatternStepV2, gate) == 4, "step gate offset");
VOX_ABI_ASSERT(offsetof(VoxPatternStepV2, probability) == 8, "step probability offset");
VOX_ABI_ASSERT(sizeof(VoxPatternV2) == 40, "VoxPatternV2 size");
VOX_ABI_ASSERT(offsetof(VoxPatternV2, cells) == 32, "pattern cells offset");
VOX_ABI_ASSERT(sizeof(VoxPatternCapsV2) == 48, "VoxPatternCapsV2 size");
VOX_ABI_ASSERT(offsetof(VoxPatternCapsV2, laneNames) == 32, "caps laneNames offset");
VOX_ABI_ASSERT(sizeof(VoxContentApiV2) == 24, "VoxContentApiV2 size");
VOX_ABI_ASSERT(offsetof(VoxContentApiV2, getFactoryPattern) == 16, "factory offset");
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
