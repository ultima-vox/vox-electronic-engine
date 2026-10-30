/* Vox Demo Synth (C11): a small, real, playable V2 native instrument used to
 * prove the Rack's native connector end to end in a DAW. One monophonic voice
 * (last-note priority), sine/saw/square oscillator, linear ADSR, velocity,
 * sustain pedal and a TPT state-variable low-pass filter. Not a product synth.
 *
 * Realtime: process/reset never allocate, lock, log or touch files. Every
 * record coming from the host is validated and copied out with memcpy. */
#define VOX_BUILDING_MODULE 1
#include "instrument/abi/VoxModuleAbiV2.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEMO_ID "com.ultimavox.demo-synth"
#define PARAM_COUNT 8u
#define STATE_VERSION 1u
#define STATE_MAGIC 0x53444f56u /* "VODS" */
#define STATE_BYTES (8u + PARAM_COUNT * 4u)
#define MAX_BLOCK 8192u
#define MAX_RECORD_STRIDE 1024u
#define MIN_MIDI_EVENTS 16u
#define MAX_MIDI_EVENTS 512u
#define HELD_NOTES 16
#define PI 3.14159265358979323846

enum { P_WAVE, P_GAIN, P_ATTACK, P_DECAY, P_SUSTAIN, P_RELEASE, P_CUTOFF, P_RESONANCE };
enum { WAVE_SINE, WAVE_SAW, WAVE_SQUARE };
enum { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

static const char *const waveChoices[] = {"Sine", "Saw", "Square"};
#define EXPOSED (VOX_PARAM_HOST_EXPOSED | VOX_PARAM_AUTOMATABLE)
#define PD(id, name, unit, group, choices, type, choiceCount, lo, hi, def, step, pool, flags)  \
  {sizeof(VoxParameterDescriptorV2), VOX_MODULE_ABI_V2, id, name, unit, group, choices, type, \
   choiceCount, lo, hi, def, step, pool, flags}

/* poolIndex == descriptor index here; both are stable and append-only. */
static const VoxParameterDescriptorV2 parameters[PARAM_COUNT] = {
    PD("wave", "Wave", "", "Oscillator", waveChoices, VOX_PARAM_CHOICE, 3, 0.0f, 2.0f, 1.0f,
       1.0f, 0, EXPOSED),
    PD("gain", "Gain", "", "Amp", NULL, VOX_PARAM_FLOAT, 0, 0.0f, 1.0f, 0.7f, 0.0f, 1,
       EXPOSED | VOX_PARAM_MODULATABLE),
    PD("attack", "Attack", "s", "Envelope", NULL, VOX_PARAM_FLOAT, 0, 0.001f, 5.0f, 0.005f, 0.0f,
       2, EXPOSED),
    PD("decay", "Decay", "s", "Envelope", NULL, VOX_PARAM_FLOAT, 0, 0.001f, 5.0f, 0.25f, 0.0f, 3,
       EXPOSED),
    PD("sustain", "Sustain", "", "Envelope", NULL, VOX_PARAM_FLOAT, 0, 0.0f, 1.0f, 0.7f, 0.0f, 4,
       EXPOSED),
    PD("release", "Release", "s", "Envelope", NULL, VOX_PARAM_FLOAT, 0, 0.001f, 10.0f, 0.3f, 0.0f,
       5, EXPOSED),
    PD("cutoff", "Cutoff", "Hz", "Filter", NULL, VOX_PARAM_FLOAT, 0, 20.0f, 20000.0f, 4000.0f,
       0.0f, 6, EXPOSED | VOX_PARAM_MODULATABLE),
    PD("resonance", "Resonance", "", "Filter", NULL, VOX_PARAM_FLOAT, 0, 0.0f, 1.0f, 0.2f, 0.0f,
       7, EXPOSED | VOX_PARAM_MODULATABLE),
};

static const VoxInstrumentDescriptorV2 instrument = {
    sizeof(VoxInstrumentDescriptorV2), VOX_MODULE_ABI_V2, DEMO_ID, "Vox Demo Synth", "Ultima Vox",
    "Synth", 1, 1, STATE_VERSION, PARAM_COUNT, parameters, sizeof(VoxParameterDescriptorV2),
    1, MAX_MIDI_EVENTS, STATE_BYTES, 0, 0, 2, 0};

struct VoxInstanceT {
  float values[PARAM_COUNT];
  double sampleRate;
  uint32_t maxBlock, outputChannels, maxParamChanges, maxMidiEvents;
  int prepared;
  /* voice */
  int held[HELD_NOTES]; /* keys physically down, oldest first */
  int heldCount;
  int note;             /* sounding pitch, -1 = none */
  int sustainPedal, sustained;
  float velocity;
  int stage;
  double env, releaseStep;
  double phase;
  /* filter and smoothing */
  double ic1, ic2, gainSmooth, cutoffSmooth, smoothCoeff;
};

static int liveInstances;

static int validValue(uint32_t index, float value) {
  const VoxParameterDescriptorV2 *p = &parameters[index];
  if (!isfinite(value) || value < p->minimum || value > p->maximum)
    return 0;
  return p->type == VOX_PARAM_FLOAT || value == floorf(value);
}

/* ------------------------------------------------------------- voice */
static void hardSilence(struct VoxInstanceT *s) {
  s->heldCount = 0;
  s->note = -1;
  s->sustained = 0;
  s->stage = ENV_IDLE;
  s->env = 0.0;
  s->ic1 = s->ic2 = 0.0;
}

static void startRelease(struct VoxInstanceT *s) {
  s->sustained = 0;
  if (s->stage == ENV_IDLE || s->stage == ENV_RELEASE)
    return;
  s->stage = ENV_RELEASE;
  s->releaseStep = s->env / (s->values[P_RELEASE] * s->sampleRate);
}

static void noteOn(struct VoxInstanceT *s, int key, int velocity) {
  int i, j;
  for (i = j = 0; i < s->heldCount; ++i) /* a repeated key moves to the top */
    if (s->held[i] != key)
      s->held[j++] = s->held[i];
  s->heldCount = j;
  if (s->heldCount == HELD_NOTES) { /* drop the oldest */
    memmove(s->held, s->held + 1, sizeof(int) * (HELD_NOTES - 1));
    --s->heldCount;
  }
  s->held[s->heldCount++] = key;
  if (s->stage == ENV_IDLE) { /* from silence: deterministic onset */
    s->phase = 0.0;
    s->ic1 = s->ic2 = 0.0;
    s->gainSmooth = s->values[P_GAIN]; /* nothing audible to smooth from */
    s->cutoffSmooth = s->values[P_CUTOFF];
  }
  s->note = key;
  s->velocity = (float)velocity / 127.0f;
  s->sustained = 0;
  /* A sounding voice retriggers from its current level: no click. */
  s->stage = ENV_ATTACK;
}

static void noteOff(struct VoxInstanceT *s, int key) {
  int i, j;
  for (i = j = 0; i < s->heldCount; ++i)
    if (s->held[i] != key)
      s->held[j++] = s->held[i];
  s->heldCount = j;
  if (key != s->note)
    return;
  if (s->heldCount > 0) {
    s->note = s->held[s->heldCount - 1]; /* legato back to the last held key */
    return;
  }
  if (s->sustainPedal)
    s->sustained = 1;
  else
    startRelease(s);
}

static void allNotesOff(struct VoxInstanceT *s) {
  s->heldCount = 0;
  startRelease(s);
}

static void applyMidi(struct VoxInstanceT *s, const VoxMidiEventV2 *e) {
  if (e->size < 3)
    return; /* valid but unused short messages are ignored */
  const uint8_t status = (uint8_t)(e->bytes[0] & 0xF0u);
  const int data1 = e->bytes[1] & 0x7F;
  const int data2 = e->bytes[2] & 0x7F;
  if (status == 0x90u && data2 > 0) {
    noteOn(s, data1, data2);
  } else if (status == 0x80u || status == 0x90u) {
    noteOff(s, data1);
  } else if (status == 0xB0u) {
    if (data1 == 64) {
      s->sustainPedal = data2 >= 64;
      if (!s->sustainPedal && s->sustained && s->heldCount == 0)
        startRelease(s);
    } else if (data1 == 120) {
      hardSilence(s); /* All Sound Off */
    } else if (data1 == 123) {
      allNotesOff(s); /* All Notes Off: normal release, pedal ignored */
    }
  }
}

static double polyBlep(double t, double dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0;
  }
  if (t > 1.0 - dt) {
    t = (t - 1.0) / dt;
    return t * t + t + t + 1.0;
  }
  return 0.0;
}

/* One-pole smoothing that lands exactly on the target (no ulp limbo). */
static double smooth(double current, double target, double coeff) {
  const double next = current + coeff * (target - current);
  return fabs(target - next) <= 1e-9 * fmax(1.0, fabs(target)) ? target : next;
}

static float renderSample(struct VoxInstanceT *s) {
  const double sr = s->sampleRate;
  s->gainSmooth = smooth(s->gainSmooth, s->values[P_GAIN], s->smoothCoeff);
  s->cutoffSmooth = smooth(s->cutoffSmooth, s->values[P_CUTOFF], s->smoothCoeff);

  switch (s->stage) {
  case ENV_ATTACK:
    s->env += 1.0 / (s->values[P_ATTACK] * sr);
    if (s->env >= 1.0) {
      s->env = 1.0;
      s->stage = ENV_DECAY;
    }
    break;
  case ENV_DECAY: {
    const double sustain = s->values[P_SUSTAIN];
    s->env -= (1.0 - sustain) / (s->values[P_DECAY] * sr);
    if (s->env <= sustain) {
      s->env = sustain;
      s->stage = ENV_SUSTAIN;
    }
    break;
  }
  case ENV_SUSTAIN:
    s->env = s->values[P_SUSTAIN];
    break;
  case ENV_RELEASE:
    s->env -= s->releaseStep;
    if (s->env <= 0.0) {
      s->env = 0.0;
      s->stage = ENV_IDLE;
      s->note = -1;
    }
    break;
  default:
    s->env = 0.0;
    break;
  }
  if (s->stage == ENV_IDLE || s->note < 0) {
    s->ic1 = s->ic2 = 0.0;
    return 0.0f;
  }

  const double hz = 440.0 * pow(2.0, (s->note - 69) / 12.0);
  const double dt = hz / sr;
  double osc;
  switch ((int)s->values[P_WAVE]) {
  case WAVE_SINE:
    osc = sin(2.0 * PI * s->phase);
    break;
  case WAVE_SQUARE:
    osc = (s->phase < 0.5 ? 1.0 : -1.0) + polyBlep(s->phase, dt) -
          polyBlep(fmod(s->phase + 0.5, 1.0), dt);
    break;
  default:
    osc = 2.0 * s->phase - 1.0 - polyBlep(s->phase, dt);
    break;
  }
  s->phase += dt;
  s->phase -= floor(s->phase);

  /* TPT state-variable low-pass (Zavalishin). */
  const double fc = fmin(s->cutoffSmooth, 0.45 * sr);
  const double g = tan(PI * fc / sr);
  const double k = 2.0 - 1.9 * s->values[P_RESONANCE];
  const double a1 = 1.0 / (1.0 + g * (g + k));
  const double v3 = osc - s->ic2;
  const double v1 = a1 * s->ic1 + g * a1 * v3;
  const double v2 = s->ic2 + g * v1;
  s->ic1 = 2.0 * v1 - s->ic1;
  s->ic2 = 2.0 * v2 - s->ic2;

  return (float)(v2 * s->env * s->velocity * s->gainSmooth);
}

/* --------------------------------------------------------------- ABI */
static VoxResult VOX_CALL getInstrument(uint32_t index, const VoxInstrumentDescriptorV2 **out) {
  if (out == NULL)
    return VOX_E_INVALID_ARG;
  *out = index == 0 ? &instrument : NULL;
  return index == 0 ? VOX_OK : VOX_E_INVALID_ARG;
}

static VoxResult VOX_CALL create(const char *id, const VoxCreateParamsV2 *params,
                                 VoxInstance *out) {
  if (out == NULL)
    return VOX_E_INVALID_ARG;
  *out = NULL;
  if (id == NULL || strcmp(id, DEMO_ID) != 0 || params == NULL ||
      params->structSize < sizeof(VoxCreateParamsV2) || params->version != VOX_MODULE_ABI_V2)
    return VOX_E_INVALID_ARG;
  struct VoxInstanceT *s = calloc(1, sizeof(*s));
  if (s == NULL)
    return VOX_E_OUT_OF_MEMORY;
  for (uint32_t i = 0; i < PARAM_COUNT; ++i)
    s->values[i] = parameters[i].defaultValue;
  s->note = -1;
  ++liveInstances;
  *out = s;
  return VOX_OK;
}

static VoxResult VOX_CALL destroy(VoxInstance s) {
  if (s == NULL)
    return VOX_E_INVALID_ARG;
  free(s);
  --liveInstances;
  return VOX_OK;
}

static VoxResult VOX_CALL prepare(VoxInstance s, const VoxPrepareSpecV2 *spec) {
  if (s == NULL || spec == NULL || spec->structSize < sizeof(VoxPrepareSpecV2) ||
      spec->version != VOX_MODULE_ABI_V2 || !(spec->sampleRate >= 8000.0) ||
      spec->sampleRate > 768000.0 || spec->maxBlockSize == 0 || spec->maxBlockSize > MAX_BLOCK ||
      spec->outputChannels == 0 || spec->outputChannels > instrument.outputChannels ||
      spec->maxParamChangesPerBlock > PARAM_COUNT ||
      spec->maxMidiEventsPerBlock < MIN_MIDI_EVENTS ||
      spec->maxMidiEventsPerBlock > MAX_MIDI_EVENTS)
    return VOX_E_INVALID_ARG;
  s->sampleRate = spec->sampleRate;
  s->maxBlock = spec->maxBlockSize;
  s->outputChannels = spec->outputChannels;
  s->maxParamChanges = spec->maxParamChangesPerBlock;
  s->maxMidiEvents = spec->maxMidiEventsPerBlock;
  s->smoothCoeff = 1.0 - exp(-1.0 / (0.005 * spec->sampleRate)); /* ~5 ms */
  hardSilence(s);
  s->sustainPedal = 0;
  s->phase = 0.0;
  s->gainSmooth = s->values[P_GAIN];
  s->cutoffSmooth = s->values[P_CUTOFF];
  s->prepared = 1;
  return VOX_OK;
}

static VoxResult VOX_CALL setAllParameters(VoxInstance s, const float *plain, uint32_t count) {
  if (s == NULL || plain == NULL || count != PARAM_COUNT)
    return VOX_E_INVALID_ARG;
  for (uint32_t i = 0; i < PARAM_COUNT; ++i)
    if (!validValue(i, plain[i]))
      return VOX_E_INVALID_ARG; /* all or nothing */
  memcpy(s->values, plain, sizeof(s->values));
  return VOX_OK;
}

/* Clears runtime state only; parameter values are kept (ABI contract). */
static VoxResult VOX_CALL reset(VoxInstance s) {
  if (s == NULL)
    return VOX_E_INVALID_ARG;
  hardSilence(s);
  s->sustainPedal = 0;
  s->phase = 0.0;
  s->gainSmooth = s->values[P_GAIN];
  s->cutoffSmooth = s->values[P_CUTOFF];
  return VOX_OK;
}

static int validRecords(const void *records, uint32_t count, uint32_t stride, size_t size,
                        size_t align) {
  return count == 0 || (records != NULL && stride >= size && stride <= MAX_RECORD_STRIDE &&
                        stride % align == 0);
}

static VoxResult VOX_CALL process(VoxInstance s, const VoxProcessContextV2 *c) {
  if (s == NULL || c == NULL || !s->prepared || c->structSize < VOX_PROCESS_CONTEXT_V2_BASE_SIZE ||
      c->version != VOX_MODULE_ABI_V2 || c->numSamples > s->maxBlock ||
      c->outputChannelCount != s->outputChannels || c->outputs == NULL ||
      (c->releaseFlags & ~VOX_KNOWN_RELEASE_FLAGS) != 0 || c->transport == NULL ||
      c->transport->structSize < sizeof(VoxTransportV2) ||
      c->transport->version != VOX_MODULE_ABI_V2 ||
      (c->transport->flags & ~VOX_TRANSPORT_KNOWN_FLAGS) != 0 ||
      c->paramChangeCount > s->maxParamChanges || c->midiEventCount > s->maxMidiEvents ||
      !validRecords(c->paramChanges, c->paramChangeCount, c->paramChangeStride,
                    sizeof(VoxParamChangeV2), _Alignof(VoxParamChangeV2)) ||
      !validRecords(c->midiEvents, c->midiEventCount, c->midiEventStride, sizeof(VoxMidiEventV2),
                    _Alignof(VoxMidiEventV2)))
    return VOX_E_INVALID_ARG;
  for (uint32_t ch = 0; ch < c->outputChannelCount; ++ch)
    if (c->outputs[ch] == NULL)
      return VOX_E_INVALID_ARG;

  if (c->releaseFlags & VOX_PANIC_AT_START)
    hardSilence(s);
  const uint8_t *changes = (const uint8_t *)c->paramChanges;
  const uint8_t *events = (const uint8_t *)c->midiEvents;
  uint32_t nextChange = 0, nextEvent = 0;
  for (uint32_t i = 0; i < c->numSamples; ++i) {
    while (nextChange < c->paramChangeCount) {
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
    const float v = renderSample(s);
    for (uint32_t ch = 0; ch < c->outputChannelCount; ++ch)
      c->outputs[ch][i] = v;
  }
  if (c->releaseFlags & VOX_RELEASE_ALL_SOUND_AT_END)
    hardSilence(s);
  else if (c->releaseFlags & VOX_RELEASE_ALL_NOTES_AT_END)
    allNotesOff(s);
  return VOX_OK;
}

static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* State v1: "VODS", parameter count, then the eight plain values (LE f32). */
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
