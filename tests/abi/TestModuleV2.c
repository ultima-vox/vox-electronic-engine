/* Minimal V2 test module (C11): one fake instrument with four typed
 * parameters. Exists only to prove the ABI boundary through a real shared
 * library; it is not a product instrument. */
#define VOX_BUILDING_MODULE 1
#include "instrument/abi/VoxModuleAbiV2.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ID "test.abi.v2.tone"
#define PARAM_COUNT 4u
#define STATE_VERSION 1u
#define STATE_MAGIC 0x54584f56u /* "VOXT" */
#define STATE_BYTES (8u + PARAM_COUNT * 4u)
#define MAX_BLOCK 4096u
#define MAX_RECORD_STRIDE 1024u
#define MIN_MIDI_EVENTS 16u

enum { P_GAIN, P_OCTAVE, P_MUTE, P_WAVE };

static const char *const waveChoices[] = {"Sine", "Square", "Saw"};
#define EXPOSED (VOX_PARAM_HOST_EXPOSED | VOX_PARAM_AUTOMATABLE)

static const VoxParameterDescriptorV2 parameters[PARAM_COUNT] = {
    {sizeof(VoxParameterDescriptorV2), VOX_MODULE_ABI_V2, "gain", "Gain", "", "Amp",
     NULL, VOX_PARAM_FLOAT, 0, 0.0f, 1.0f, 0.5f, 0.0f, 0, EXPOSED | VOX_PARAM_MODULATABLE},
    {sizeof(VoxParameterDescriptorV2), VOX_MODULE_ABI_V2, "octave", "Octave", "oct",
     "Pitch", NULL, VOX_PARAM_INTEGER, 0, -2.0f, 2.0f, 0.0f, 1.0f, 1, EXPOSED},
    {sizeof(VoxParameterDescriptorV2), VOX_MODULE_ABI_V2, "mute", "Mute", "", "Amp",
     NULL, VOX_PARAM_BOOLEAN, 0, 0.0f, 1.0f, 0.0f, 1.0f, 2, EXPOSED},
    {sizeof(VoxParameterDescriptorV2), VOX_MODULE_ABI_V2, "wave", "Wave", "", "Osc",
     waveChoices, VOX_PARAM_CHOICE, 3, 0.0f, 2.0f, 0.0f, 1.0f, 3, EXPOSED},
};

static const VoxInstrumentDescriptorV2 instrument = {
    sizeof(VoxInstrumentDescriptorV2), VOX_MODULE_ABI_V2, TEST_ID, "ABI Test Tone", "Tests",
    "Test", 1, 1, STATE_VERSION, PARAM_COUNT, parameters, sizeof(VoxParameterDescriptorV2),
    1, 64, STATE_BYTES, 0, 0, 2, 0};

struct VoxInstanceT {
  float values[PARAM_COUNT];
  double sampleRate;
  uint32_t maxBlock;
  uint32_t outputChannels;
  uint32_t maxParamChanges; /* per block, fixed at prepare */
  uint32_t maxMidiEvents;
  double phase;
  int note; /* -1 = no key sounding (one key, retrigger semantics) */
  uint32_t slotIndex; /* create-time Rack slot ordinal, for host tests */
  int prepared;
  int failProcess, failReset; /* one-shot injected faults, for host tests */
};

static int liveInstances;
static struct VoxInstanceT *live[64];

/* Test hook (not part of the ABI): bit n set = a live instance was created
 * for Rack slot n. Lets host tests check the forwarded slot ordinal. */
VOX_EXPORT uint32_t VOX_CALL voxTestLiveSlotMask(void) {
  uint32_t mask = 0;
  for (int i = 0; i < 64; ++i)
    if (live[i] != NULL && live[i]->slotIndex < 32)
      mask |= 1u << live[i]->slotIndex;
  return mask;
}

/* Test hook (not part of the ABI): the next process (bit 0) and/or reset
 * (bit 1) of every live instance created for Rack slot `slotIndex` returns
 * VOX_E_INTERNAL once; afterwards the instance would play normally again. */
#define TEST_FAIL_PROCESS 1u
#define TEST_FAIL_RESET 2u
VOX_EXPORT void VOX_CALL voxTestFailNext(uint32_t slotIndex, uint32_t which) {
  for (int i = 0; i < 64; ++i)
    if (live[i] != NULL && live[i]->slotIndex == slotIndex) {
      live[i]->failProcess = (which & TEST_FAIL_PROCESS) != 0;
      live[i]->failReset = (which & TEST_FAIL_RESET) != 0;
    }
}

static int validValue(uint32_t index, float value) {
  const VoxParameterDescriptorV2 *p = &parameters[index];
  if (!isfinite(value) || value < p->minimum || value > p->maximum)
    return 0;
  return p->type == VOX_PARAM_FLOAT || value == floorf(value);
}

static VoxResult VOX_CALL getInstrument(uint32_t index,
                                        const VoxInstrumentDescriptorV2 **out) {
  if (out == NULL)
    return VOX_E_INVALID_ARG;
  *out = NULL;
  if (index != 0)
    return VOX_E_INVALID_ARG;
  *out = &instrument;
  return VOX_OK;
}

static VoxResult VOX_CALL create(const char *id, const VoxCreateParamsV2 *params,
                                 VoxInstance *out) {
  if (out == NULL)
    return VOX_E_INVALID_ARG;
  *out = NULL;
  if (id == NULL || strcmp(id, TEST_ID) != 0 || params == NULL ||
      params->structSize < sizeof(VoxCreateParamsV2) || params->version != VOX_MODULE_ABI_V2)
    return VOX_E_INVALID_ARG;
  struct VoxInstanceT *instance = calloc(1, sizeof(*instance));
  if (instance == NULL)
    return VOX_E_OUT_OF_MEMORY;
  for (uint32_t i = 0; i < PARAM_COUNT; ++i)
    instance->values[i] = parameters[i].defaultValue;
  instance->note = -1;
  instance->slotIndex = params->slotIndex;
  for (int i = 0; i < 64; ++i)
    if (live[i] == NULL) {
      live[i] = instance;
      break;
    }
  ++liveInstances;
  *out = instance;
  return VOX_OK;
}

static VoxResult VOX_CALL destroy(VoxInstance instance) {
  if (instance == NULL)
    return VOX_E_INVALID_ARG;
  for (int i = 0; i < 64; ++i)
    if (live[i] == instance)
      live[i] = NULL;
  free(instance);
  --liveInstances;
  return VOX_OK;
}

static VoxResult VOX_CALL prepare(VoxInstance instance, const VoxPrepareSpecV2 *spec) {
  if (instance == NULL || spec == NULL || spec->structSize < sizeof(VoxPrepareSpecV2) ||
      spec->version != VOX_MODULE_ABI_V2 || !(spec->sampleRate > 0.0) ||
      spec->sampleRate > 768000.0 || spec->maxBlockSize == 0 ||
      spec->maxBlockSize > MAX_BLOCK || spec->outputChannels == 0 ||
      spec->outputChannels > instrument.outputChannels ||
      spec->maxParamChangesPerBlock > PARAM_COUNT ||
      spec->maxMidiEventsPerBlock < MIN_MIDI_EVENTS ||
      spec->maxMidiEventsPerBlock > instrument.maxMidiEventsPerBlock)
    return VOX_E_INVALID_ARG;
  instance->sampleRate = spec->sampleRate;
  instance->maxBlock = spec->maxBlockSize;
  instance->outputChannels = spec->outputChannels;
  instance->maxParamChanges = spec->maxParamChangesPerBlock;
  instance->maxMidiEvents = spec->maxMidiEventsPerBlock;
  instance->phase = 0.0;
  instance->note = -1;
  instance->prepared = 1;
  return VOX_OK;
}

static VoxResult VOX_CALL setAllParameters(VoxInstance instance, const float *plain,
                                           uint32_t count) {
  if (instance == NULL || plain == NULL || count != PARAM_COUNT)
    return VOX_E_INVALID_ARG;
  for (uint32_t i = 0; i < PARAM_COUNT; ++i)
    if (!validValue(i, plain[i]))
      return VOX_E_INVALID_ARG; /* all or nothing */
  memcpy(instance->values, plain, sizeof(instance->values));
  return VOX_OK;
}

/* Clears DSP state only; parameter values are kept (ABI contract). */
static VoxResult VOX_CALL reset(VoxInstance instance) {
  if (instance == NULL)
    return VOX_E_INVALID_ARG;
  if (instance->failReset) {
    instance->failReset = 0;
    return VOX_E_INTERNAL;
  }
  instance->phase = 0.0;
  instance->note = -1;
  return VOX_OK;
}

static float render(struct VoxInstanceT *s) {
  if (s->note < 0 || s->values[P_MUTE] >= 0.5f)
    return 0.0f;
  const double hz = 440.0 * pow(2.0, (s->note - 69) / 12.0 + s->values[P_OCTAVE]);
  s->phase += hz / s->sampleRate;
  s->phase -= floor(s->phase);
  float v;
  switch ((int)s->values[P_WAVE]) {
  case 1: v = s->phase < 0.5 ? 1.0f : -1.0f; break;
  case 2: v = (float)(2.0 * s->phase - 1.0); break;
  default: v = (float)sin(6.283185307179586 * s->phase); break;
  }
  return v * s->values[P_GAIN];
}

static void applyMidi(struct VoxInstanceT *s, const VoxMidiEventV2 *e) {
  if (e->size < 3)
    return;
  const uint8_t status = (uint8_t)(e->bytes[0] & 0xF0u);
  if (status == 0x90u && e->bytes[2] > 0) {
    s->note = e->bytes[1]; /* repeated key retriggers */
  } else if ((status == 0x80u || status == 0x90u) && e->bytes[1] == s->note) {
    s->note = -1;
  } else if (status == 0xB0u && (e->bytes[1] == 120 || e->bytes[1] == 123)) {
    s->note = -1;
  }
}

static VoxResult VOX_CALL process(VoxInstance s, const VoxProcessContextV2 *c) {
  if (s == NULL || c == NULL || !s->prepared ||
      c->structSize < sizeof(VoxProcessContextV2) || c->version != VOX_MODULE_ABI_V2 ||
      c->numSamples > s->maxBlock || c->outputChannelCount != s->outputChannels ||
      c->outputs == NULL || (c->releaseFlags & ~VOX_KNOWN_RELEASE_FLAGS) != 0 ||
      c->transport == NULL || c->transport->structSize < sizeof(VoxTransportV2) ||
      c->transport->version != VOX_MODULE_ABI_V2 ||
      (c->transport->flags & ~VOX_TRANSPORT_KNOWN_FLAGS) != 0 ||
      /* Budgets and strides are bounded before any record is read. */
      c->paramChangeCount > s->maxParamChanges || c->midiEventCount > s->maxMidiEvents ||
      (c->paramChangeCount > 0 &&
       (c->paramChanges == NULL || c->paramChangeStride < sizeof(VoxParamChangeV2) ||
        c->paramChangeStride > MAX_RECORD_STRIDE ||
        c->paramChangeStride % _Alignof(VoxParamChangeV2) != 0)) ||
      (c->midiEventCount > 0 &&
       (c->midiEvents == NULL || c->midiEventStride < sizeof(VoxMidiEventV2) ||
        c->midiEventStride > MAX_RECORD_STRIDE ||
        c->midiEventStride % _Alignof(VoxMidiEventV2) != 0)))
    return VOX_E_INVALID_ARG;
  for (uint32_t ch = 0; ch < c->outputChannelCount; ++ch)
    if (c->outputs[ch] == NULL)
      return VOX_E_INVALID_ARG;
  if (s->failProcess) {
    s->failProcess = 0;
    return VOX_E_INTERNAL;
  }

  if (c->releaseFlags & VOX_PANIC_AT_START) {
    s->note = -1;
    s->phase = 0.0;
  }
  const uint8_t *changes = (const uint8_t *)c->paramChanges;
  const uint8_t *events = (const uint8_t *)c->midiEvents;
  uint32_t nextChange = 0, nextEvent = 0;
  for (uint32_t i = 0; i < c->numSamples; ++i) {
    while (nextChange < c->paramChangeCount) {
      /* Header-less records are copied out, never dereferenced in place. */
      VoxParamChangeV2 p;
      memcpy(&p, changes + (size_t)nextChange * c->paramChangeStride, sizeof(p));
      if (p.sampleOffset > i)
        break;
      if (p.index < PARAM_COUNT && validValue(p.index, p.plain))
        s->values[p.index] = p.plain;
      ++nextChange;
    }
    while (nextEvent < c->midiEventCount) {
      VoxMidiEventV2 e;
      memcpy(&e, events + (size_t)nextEvent * c->midiEventStride, sizeof(e));
      if (e.sampleOffset > i)
        break;
      applyMidi(s, &e);
      ++nextEvent;
    }
    const float v = render(s);
    for (uint32_t ch = 0; ch < c->outputChannelCount; ++ch)
      c->outputs[ch][i] = v;
  }
  if (c->releaseFlags & (VOX_RELEASE_ALL_NOTES_AT_END | VOX_RELEASE_ALL_SOUND_AT_END))
    s->note = -1;
  return VOX_OK;
}

static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static VoxResult VOX_CALL saveState(VoxInstance s, uint8_t *buffer, uint32_t capacity,
                                    uint32_t *written, uint32_t *stateVersion) {
  if (s == NULL || written == NULL || stateVersion == NULL)
    return VOX_E_INVALID_ARG;
  *written = STATE_BYTES;
  *stateVersion = STATE_VERSION;
  if (buffer == NULL || capacity < STATE_BYTES)
    return VOX_E_BUFFER_TOO_SMALL;
  put32(buffer, STATE_MAGIC);
  put32(buffer + 4, PARAM_COUNT);
  for (uint32_t i = 0; i < PARAM_COUNT; ++i) {
    uint32_t bits;
    memcpy(&bits, &s->values[i], 4);
    put32(buffer + 8 + 4 * i, bits);
  }
  return VOX_OK;
}

static VoxResult VOX_CALL loadState(VoxInstance s, uint32_t version, const uint8_t *bytes,
                                    uint32_t size) {
  if (s == NULL || (bytes == NULL && size > 0))
    return VOX_E_INVALID_ARG;
  if (version > STATE_VERSION)
    return VOX_E_STATE_VERSION;
  if (version != STATE_VERSION || size != STATE_BYTES || get32(bytes) != STATE_MAGIC ||
      get32(bytes + 4) != PARAM_COUNT)
    return VOX_E_STATE_CORRUPT;
  float next[PARAM_COUNT];
  for (uint32_t i = 0; i < PARAM_COUNT; ++i) {
    const uint32_t bits = get32(bytes + 8 + 4 * i);
    memcpy(&next[i], &bits, 4);
    if (!validValue(i, next[i]))
      return VOX_E_STATE_CORRUPT; /* nothing applied */
  }
  memcpy(s->values, next, sizeof(next));
  return VOX_OK;
}

static VoxResult VOX_CALL shutdown(void) {
  return liveInstances == 0 ? VOX_OK : VOX_E_INTERNAL;
}

static const VoxModuleV2 moduleTable = {
    sizeof(VoxModuleV2), VOX_MODULE_ABI_V2, 1, 0, getInstrument, create, destroy, prepare,
    setAllParameters, reset, process, saveState, loadState, NULL, NULL, shutdown};

VOX_EXPORT VoxResult VOX_CALL voxModuleEntryV2(uint32_t hostAbiVersion,
                                               const VoxHostServicesV2 *host,
                                               const VoxModuleV2 **module) {
  if (module == NULL)
    return VOX_E_INVALID_ARG;
  *module = NULL;
  if (hostAbiVersion != VOX_MODULE_ABI_V2)
    return VOX_E_UNSUPPORTED;
  if (host == NULL || host->structSize < sizeof(VoxHostServicesV2) ||
      host->version != VOX_MODULE_ABI_V2)
    return VOX_E_INVALID_ARG;
  *module = &moduleTable;
  return VOX_OK;
}
