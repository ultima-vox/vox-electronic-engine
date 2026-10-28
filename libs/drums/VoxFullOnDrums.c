/* Vox Full-on Drums (C11): an independent native V2 drum kit for Vox Rack.
 *
 * One instrument in one Rack slot on one MIDI channel; inside the module a
 * bounded pool of one-shot voices plays eight synthesized pads at once:
 *
 *   pad 0 Kick   note 36    pad 4 Open hat    note 46
 *   pad 1 Snare  note 38    pad 5 Tom         note 45
 *   pad 2 Clap   note 39    pad 6 Rim / perc  note 37
 *   pad 3 Closed hat 42     pad 7 Cymbal      note 49
 *
 * Pad identities (0..7) and parameter pool indices are stable and
 * append-only; pads 8..15 may be added later without renumbering.
 * The kick is a C port of the project's frozen Checkpoint 7A KickSynth
 * algorithm (pitch sweep with curve, body + sub tail, filtered click,
 * normalized tanh drive, deterministic noise seed).
 *
 * Realtime: process/reset never allocate, lock, log or touch files. Every
 * host record is validated and copied out with memcpy. Voices are latched at
 * trigger time (parameter moves affect the next hit), are one-shot (NoteOff
 * is ignored), and always end: by their own envelope (-80 dB), by a choke or
 * retrigger fade (3 ms), by CC123 / release-all (50 ms fade) or immediately
 * by CC120 / panic / reset. */
#define VOX_BUILDING_MODULE 1
#include "instrument/abi/VoxModuleAbiV2.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DRUMS_ID "com.ultimavox.fullon-drums"
#define PAD_COUNT 8u
#define PAD_PARAMS 5u
#define GLOBAL_PARAMS 8u
#define PARAM_COUNT (PAD_COUNT * PAD_PARAMS + GLOBAL_PARAMS) /* 48 */
#define MAX_VOICES 16
#define STATE_VERSION 1u
#define STATE_MAGIC 0x44464f56u /* "VOFD" */
#define STATE_BYTES (12u + PARAM_COUNT * 4u + PAD_COUNT * 2u)
#define MAX_STATE_BYTES 4096u /* room for D2 pattern state */
#define MAX_BLOCK 8192u
#define MAX_RECORD_STRIDE 1024u
#define MIN_MIDI_EVENTS 16u
#define MAX_MIDI_EVENTS 512u
#define MAX_CHOKE_GROUP 4u
#define PI 3.14159265358979323846
#define SILENCE 1e-4 /* -80 dB: a voice ends below this */

enum { PAD_KICK, PAD_SNARE, PAD_CLAP, PAD_CLOSED_HAT, PAD_OPEN_HAT, PAD_TOM, PAD_RIM, PAD_CYMBAL };
enum { K_LEVEL, K_TUNE, K_DECAY, K_PAN, K_CHARACTER };
enum {
  G_VOLUME = PAD_COUNT * PAD_PARAMS, /* 40 */
  G_KICK_PITCH,                      /* 41 */
  G_KICK_PITCH_DECAY,                /* 42 */
  G_KICK_DRIVE,                      /* 43 */
  G_KICK_SUB,                        /* 44 */
  G_KICK_TAIL,                       /* 45 */
  G_HAT_CHOKE,                       /* 46 */
  G_VELOCITY                         /* 47 */
};

static const uint8_t defaultNotes[PAD_COUNT] = {36, 38, 39, 42, 46, 45, 37, 49};
static const uint8_t defaultChoke[PAD_COUNT] = {0, 0, 0, 1, 1, 0, 0, 0};

#define EXPOSED (VOX_PARAM_HOST_EXPOSED | VOX_PARAM_AUTOMATABLE)
#define P(id, name, unit, group, type, lo, hi, def, step, pool)                                 \
  {sizeof(VoxParameterDescriptorV2), VOX_MODULE_ABI_V2, id, name, unit, group, NULL, type, 0,  \
   lo, hi, def, step, pool, EXPOSED}
#define PAD(key, label, levelDef, decayLo, decayHi, decayDef, panDef, charName, charDef, base) \
  P(key ".level", label " Level", "", label, VOX_PARAM_FLOAT, 0.0f, 1.0f, levelDef, 0.0f,      \
    base + 0),                                                                                \
  P(key ".tune", label " Tune", "st", label, VOX_PARAM_FLOAT, -12.0f, 12.0f, 0.0f, 0.0f,       \
    base + 1),                                                                                \
  P(key ".decay", label " Decay", "s", label, VOX_PARAM_FLOAT, decayLo, decayHi, decayDef,     \
    0.0f, base + 2),                                                                          \
  P(key ".pan", label " Pan", "", label, VOX_PARAM_FLOAT, -1.0f, 1.0f, panDef, 0.0f,           \
    base + 3),                                                                                \
  P(key ".character", label " " charName, "", label, VOX_PARAM_FLOAT, 0.0f, 1.0f, charDef,     \
    0.0f, base + 4)

/* Descriptor index == poolIndex. Never reorder; only append. */
static const VoxParameterDescriptorV2 parameters[PARAM_COUNT] = {
    PAD("kick", "Kick", 0.85f, 0.08f, 1.5f, 0.38f, 0.0f, "Click", 0.5f, 0),
    PAD("snare", "Snare", 0.6f, 0.05f, 1.0f, 0.25f, 0.0f, "Snappy", 0.6f, 5),
    PAD("clap", "Clap", 0.55f, 0.08f, 1.5f, 0.35f, -0.1f, "Tone", 0.5f, 10),
    PAD("chat", "Closed Hat", 0.5f, 0.02f, 0.5f, 0.07f, 0.2f, "Tone", 0.6f, 15),
    PAD("ohat", "Open Hat", 0.45f, 0.1f, 3.0f, 0.45f, 0.2f, "Tone", 0.6f, 20),
    PAD("tom", "Tom", 0.6f, 0.08f, 2.0f, 0.5f, -0.25f, "Sweep", 0.4f, 25),
    PAD("rim", "Rim", 0.5f, 0.02f, 0.5f, 0.08f, 0.3f, "Tone", 0.5f, 30),
    PAD("cymbal", "Cymbal", 0.4f, 0.3f, 6.0f, 1.8f, -0.3f, "Tone", 0.5f, 35),
    P("kit.volume", "Kit Volume", "", "Kit", VOX_PARAM_FLOAT, 0.0f, 1.0f, 0.6f, 0.0f, 40),
    P("kick.pitch_amount", "Kick Pitch Env", "st", "Kick", VOX_PARAM_FLOAT, 0.0f, 48.0f, 24.0f,
      0.0f, 41),
    P("kick.pitch_decay", "Kick Pitch Decay", "s", "Kick", VOX_PARAM_FLOAT, 0.005f, 0.2f,
      0.035f, 0.0f, 42),
    P("kick.drive", "Kick Drive", "", "Kick", VOX_PARAM_FLOAT, 1.0f, 8.0f, 1.6f, 0.0f, 43),
    P("kick.sub", "Kick Sub", "", "Kick", VOX_PARAM_FLOAT, 0.0f, 1.0f, 0.3f, 0.0f, 44),
    P("kick.tail", "Kick Tail", "s", "Kick", VOX_PARAM_FLOAT, 0.03f, 1.5f, 0.25f, 0.0f, 45),
    P("kit.hat_choke", "Hat Choke", "", "Kit", VOX_PARAM_BOOLEAN, 0.0f, 1.0f, 1.0f, 1.0f, 46),
    P("kit.velocity", "Velocity Sens", "", "Kit", VOX_PARAM_FLOAT, 0.0f, 1.0f, 0.8f, 0.0f, 47),
};

static const VoxInstrumentDescriptorV2 instrument = {
    sizeof(VoxInstrumentDescriptorV2), VOX_MODULE_ABI_V2, DRUMS_ID, "Full-on Drums",
    "Ultima Vox", "Drums", 1, 1, STATE_VERSION, PARAM_COUNT, parameters,
    sizeof(VoxParameterDescriptorV2), MAX_VOICES, MAX_MIDI_EVENTS, MAX_STATE_BYTES, 0, 0, 2, 0};

/* 808-style metallic oscillator ratios (Hz at tune 0). */
static const double metalHz[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};

typedef struct {
  int active;
  int pad;
  int64_t n; /* samples since trigger */
  double gainL, gainR;
  /* latched at trigger */
  double sr, f0, envMul, env, env2Mul, env2, character;
  double kickPitch, kickPitchDecay, kickDrive, kickDriveNorm, kickSub;
  double phase[6];
  uint32_t noise;
  double lp, hp1, hp2, hpPrev1, hpPrev2, bp1, bp2;
  int fadeTotal, fadeLeft;
} Voice;

struct VoxInstanceT {
  float values[PARAM_COUNT];
  uint8_t padNote[PAD_COUNT];
  uint8_t choke[PAD_COUNT];
  int8_t noteToPad[128];
  double sampleRate;
  uint32_t maxBlock, outputChannels, maxParamChanges, maxMidiEvents;
  int prepared;
  Voice voices[MAX_VOICES];
};

static int liveInstances;

static int validValue(uint32_t index, float value) {
  const VoxParameterDescriptorV2 *p = &parameters[index];
  if (!isfinite(value) || value < p->minimum || value > p->maximum)
    return 0;
  return p->type == VOX_PARAM_FLOAT || value == floorf(value);
}

/* Per-sample multiplier that falls 60 dB in `seconds` (decays are T60). */
static double t60(double seconds, double sampleRate) {
  return exp(-6.907755278982137 / (seconds * sampleRate));
}

static void rebuildNoteMap(struct VoxInstanceT *s) {
  memset(s->noteToPad, -1, sizeof(s->noteToPad));
  for (uint32_t p = 0; p < PAD_COUNT; ++p)
    s->noteToPad[s->padNote[p]] = (int8_t)p;
}

static float padValue(const struct VoxInstanceT *s, int pad, int key) {
  return s->values[(uint32_t)pad * PAD_PARAMS + (uint32_t)key];
}

/* ------------------------------------------------------------- voices */
static double noise(uint32_t *state) { /* xorshift32 -> [-1, 1) */
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return (double)x * (2.0 / 4294967296.0) - 1.0;
}

static void startFade(Voice *v, double sampleRate, double seconds) {
  if (!v->active)
    return;
  const int total = (int)(seconds * sampleRate) + 1;
  if (v->fadeTotal > 0 && v->fadeLeft <= total)
    return; /* already ending sooner */
  v->fadeTotal = total;
  v->fadeLeft = total;
}

static void stopAll(struct VoxInstanceT *s) {
  for (int i = 0; i < MAX_VOICES; ++i)
    s->voices[i].active = 0;
}

static void fadeAll(struct VoxInstanceT *s, double seconds) {
  for (int i = 0; i < MAX_VOICES; ++i)
    startFade(&s->voices[i], s->sampleRate, seconds);
}

static void trigger(struct VoxInstanceT *s, int pad, int velocity) {
  const double sr = s->sampleRate;
  /* One sounding voice per pad: a retrigger fades the previous hit (3 ms).
   * A choke group fades the other members (closed hat cuts open hat). */
  const int group = s->values[G_HAT_CHOKE] >= 0.5f ? s->choke[pad] : 0;
  for (int i = 0; i < MAX_VOICES; ++i) {
    Voice *v = &s->voices[i];
    if (v->active && (v->pad == pad || (group != 0 && s->choke[v->pad] == group)))
      startFade(v, sr, 0.003);
  }
  Voice *v = NULL;
  for (int i = 0; i < MAX_VOICES && v == NULL; ++i)
    if (!s->voices[i].active)
      v = &s->voices[i];
  if (v == NULL) { /* bounded polyphony: steal the oldest */
    v = &s->voices[0];
    for (int i = 1; i < MAX_VOICES; ++i)
      if (s->voices[i].n > v->n)
        v = &s->voices[i];
  }
  memset(v, 0, sizeof(*v));
  v->active = 1;
  v->pad = pad;
  v->sr = sr;
  v->noise = 0x9E3779B9u ^ (0x85EBCA6Bu * (uint32_t)(pad + 1)); /* deterministic */

  const double sens = s->values[G_VELOCITY];
  const double vel = (1.0 - sens) + sens * (double)velocity / 127.0;
  const double level = padValue(s, pad, K_LEVEL) * s->values[G_VOLUME] * vel;
  const double angle = (padValue(s, pad, K_PAN) + 1.0) * 0.25 * PI;
  v->gainL = level * cos(angle) * 1.41421356237;
  v->gainR = level * sin(angle) * 1.41421356237;
  const double tune = pow(2.0, padValue(s, pad, K_TUNE) / 12.0);
  const double decay = padValue(s, pad, K_DECAY);
  v->character = padValue(s, pad, K_CHARACTER);
  v->env = 1.0;
  v->envMul = t60(decay, sr);
  v->env2 = 1.0;
  switch (pad) {
  case PAD_KICK:
    v->f0 = 50.0 * tune;
    v->env2Mul = t60(s->values[G_KICK_TAIL], sr);
    v->kickPitch = s->values[G_KICK_PITCH];
    v->kickPitchDecay = s->values[G_KICK_PITCH_DECAY];
    v->kickDrive = s->values[G_KICK_DRIVE];
    v->kickDriveNorm = tanh(v->kickDrive);
    v->kickSub = s->values[G_KICK_SUB] * 0.7;
    break;
  case PAD_SNARE:
    v->f0 = 185.0 * tune;
    v->env2Mul = t60(0.5 * decay, sr); /* tone body */
    break;
  case PAD_CLAP:
    v->f0 = 1200.0 * tune * (0.6 + 0.8 * v->character);
    break;
  case PAD_TOM:
    v->f0 = 120.0 * tune;
    break;
  case PAD_RIM:
    v->f0 = 520.0 * tune;
    v->env2Mul = t60(0.01, sr); /* click */
    break;
  case PAD_CLOSED_HAT:
  case PAD_OPEN_HAT:
    v->f0 = 1.7 * tune;
    break;
  default: /* cymbal */
    v->f0 = 3.1 * tune;
    break;
  }
}

/* One-pole high-pass, coefficient from cutoff. */
static double highPass(double x, double *prev, double *state, double cutoff, double sr) {
  const double a = exp(-2.0 * PI * cutoff / sr);
  *state = a * (*state + x - *prev);
  *prev = x;
  return *state;
}

/* TPT state-variable band-pass. */
static double bandPass(double x, double *ic1, double *ic2, double fc, double k, double sr) {
  const double g = tan(PI * fmin(fc, 0.45 * sr) / sr);
  const double a1 = 1.0 / (1.0 + g * (g + k));
  const double v1 = a1 * *ic1 + g * a1 * (x - *ic2);
  const double v2 = *ic2 + g * v1;
  *ic1 = 2.0 * v1 - *ic1;
  *ic2 = 2.0 * v2 - *ic2;
  return v1;
}

static double metal(Voice *v, double scale) {
  double sum = 0.0;
  for (int i = 0; i < 6; ++i) {
    v->phase[i] += metalHz[i] * v->f0 * scale / v->sr;
    v->phase[i] -= floor(v->phase[i]);
    sum += v->phase[i] < 0.5 ? 1.0 : -1.0;
  }
  return sum / 6.0;
}

/* Renders one sample of a voice; returns 0 when the voice has ended. */
static double renderVoice(Voice *v) {
  const double sr = v->sr;
  const double t = (double)v->n / sr;
  double x = 0.0, level = v->env;
  switch (v->pad) {
  case PAD_KICK: { /* port of KickSynth (Checkpoint 7A) */
    const double attack = fmin(1.0, t / 0.0025);
    const double u = fmin(1.0, t / v->kickPitchDecay);
    const double sweep = v->kickPitch * (1.0 - u) * (1.0 - u);
    const double inc = 2.0 * PI * v->f0 * pow(2.0, sweep / 12.0) / sr;
    v->phase[0] += inc;
    if (v->phase[0] >= 2.0 * PI)
      v->phase[0] -= 2.0 * PI;
    v->phase[1] += 0.5 * inc;
    if (v->phase[1] >= 2.0 * PI)
      v->phase[1] -= 2.0 * PI;
    const double n = noise(&v->noise);
    v->lp += (n - v->lp) * 0.3;
    const double click = t < 0.004 ? (1.0 - t / 0.004) * (0.6 * n + 0.4 * v->lp) : 0.0;
    x = sin(v->phase[0]) * v->env + sin(v->phase[1]) * v->kickSub * v->env2 +
        click * v->character;
    x = tanh(x * attack * v->kickDrive) / v->kickDriveNorm;
    level = fmax(v->env, v->env2 * v->kickSub);
    v->env2 *= v->env2Mul;
    break;
  }
  case PAD_SNARE: {
    const double drop = 1.0 + 0.3 * exp(-t / 0.01);
    v->phase[0] += v->f0 * drop / sr;
    v->phase[0] -= floor(v->phase[0]);
    v->phase[1] += 1.78 * v->f0 * drop / sr;
    v->phase[1] -= floor(v->phase[1]);
    const double tone = 0.6 * sin(2.0 * PI * v->phase[0]) + 0.4 * sin(2.0 * PI * v->phase[1]);
    double n = highPass(noise(&v->noise), &v->hpPrev1, &v->hp1, 900.0, sr);
    v->lp += (n - v->lp) * 0.7;
    n = v->lp;
    x = tone * v->env2 * (1.0 - 0.7 * v->character) + n * v->env * 1.3 * v->character;
    level = fmax(v->env, v->env2);
    v->env2 *= v->env2Mul;
    break;
  }
  case PAD_CLAP: {
    double burst = 0.0;
    for (int i = 0; i < 3; ++i) {
      const double start = 0.010 * i;
      if (t >= start)
        burst = fmax(burst, exp(-(t - start) / 0.008));
    }
    /* Three hand-clap bursts 10 ms apart, then the decaying room tail. */
    const double env = t < 0.03 ? burst : v->env;
    const double n = bandPass(noise(&v->noise), &v->bp1, &v->bp2, v->f0, 0.7, sr);
    x = 2.2 * n * env;
    level = t < 0.03 ? 1.0 : v->env;
    break;
  }
  case PAD_CLOSED_HAT:
  case PAD_OPEN_HAT:
  case PAD_CYMBAL: {
    const double m = metal(v, 1.0);
    const double mix = (1.0 - 0.5 * v->character) * m + 0.5 * v->character * noise(&v->noise);
    const double cut = (v->pad == PAD_CYMBAL ? 3500.0 : 5500.0) + 3000.0 * v->character;
    double y = highPass(mix, &v->hpPrev1, &v->hp1, cut, sr);
    y = highPass(y, &v->hpPrev2, &v->hp2, cut, sr);
    x = 5.0 * y * v->env;
    break;
  }
  case PAD_TOM: {
    const double f = v->f0 * (1.0 + 1.5 * v->character * exp(-t / 0.04));
    v->phase[0] += f / sr;
    v->phase[0] -= floor(v->phase[0]);
    const double click = t < 0.002 ? (1.0 - t / 0.002) * noise(&v->noise) * 0.3 : 0.0;
    x = sin(2.0 * PI * v->phase[0]) * v->env + click;
    break;
  }
  default: { /* rim / perc */
    v->phase[0] += v->f0 / sr;
    v->phase[0] -= floor(v->phase[0]);
    v->phase[1] += 3.3 * v->f0 / sr;
    v->phase[1] -= floor(v->phase[1]);
    const double tone = 0.5 * sin(2.0 * PI * v->phase[0]) + 0.5 * sin(2.0 * PI * v->phase[1]);
    const double click = highPass(noise(&v->noise), &v->hpPrev1, &v->hp1, 2000.0, sr) * v->env2;
    x = (tone * v->character + click * (1.0 - v->character) * 1.5) * v->env;
    v->env2 *= v->env2Mul;
    break;
  }
  }
  v->env *= v->envMul;
  v->n += 1;
  if (level < SILENCE && t > 0.005)
    v->active = 0;
  return x;
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
  if (id == NULL || strcmp(id, DRUMS_ID) != 0 || params == NULL ||
      params->structSize < sizeof(VoxCreateParamsV2) || params->version != VOX_MODULE_ABI_V2)
    return VOX_E_INVALID_ARG;
  struct VoxInstanceT *s = calloc(1, sizeof(*s));
  if (s == NULL)
    return VOX_E_OUT_OF_MEMORY;
  for (uint32_t i = 0; i < PARAM_COUNT; ++i)
    s->values[i] = parameters[i].defaultValue;
  memcpy(s->padNote, defaultNotes, sizeof(defaultNotes));
  memcpy(s->choke, defaultChoke, sizeof(defaultChoke));
  rebuildNoteMap(s);
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
  stopAll(s);
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

/* Clears runtime state only; parameters, note map and chokes are kept. */
static VoxResult VOX_CALL reset(VoxInstance s) {
  if (s == NULL)
    return VOX_E_INVALID_ARG;
  stopAll(s);
  return VOX_OK;
}

static void applyMidi(struct VoxInstanceT *s, const VoxMidiEventV2 *e) {
  if (e->size < 3)
    return;
  const uint8_t status = (uint8_t)(e->bytes[0] & 0xF0u);
  const int data1 = e->bytes[1] & 0x7F;
  const int data2 = e->bytes[2] & 0x7F;
  if (status == 0x90u && data2 > 0) {
    const int pad = s->noteToPad[data1];
    if (pad >= 0)
      trigger(s, pad, data2);
  } else if (status == 0xB0u) {
    if (data1 == 120)
      stopAll(s); /* All Sound Off: immediate */
    else if (data1 == 123)
      fadeAll(s, 0.05); /* All Notes Off: bounded 50 ms tail */
  }
  /* NoteOff and sustain do not affect one-shot drum voices. */
}

static int validRecords(const void *records, uint32_t count, uint32_t stride, size_t size,
                        size_t align) {
  return count == 0 || (records != NULL && stride >= size && stride <= MAX_RECORD_STRIDE &&
                        stride % align == 0);
}

static VoxResult VOX_CALL process(VoxInstance s, const VoxProcessContextV2 *c) {
  if (s == NULL || c == NULL || !s->prepared || c->structSize < sizeof(VoxProcessContextV2) ||
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
    stopAll(s);
  const uint8_t *changes = (const uint8_t *)c->paramChanges;
  const uint8_t *events = (const uint8_t *)c->midiEvents;
  uint32_t nextChange = 0, nextEvent = 0;
  float *left = c->outputs[0];
  float *right = c->outputChannelCount > 1 ? c->outputs[1] : NULL;
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
    double l = 0.0, r = 0.0;
    for (int k = 0; k < MAX_VOICES; ++k) {
      Voice *v = &s->voices[k];
      if (!v->active)
        continue;
      double x = renderVoice(v);
      if (v->fadeTotal > 0) {
        x *= (double)v->fadeLeft / (double)v->fadeTotal;
        if (--v->fadeLeft <= 0)
          v->active = 0;
      }
      l += x * v->gainL;
      r += x * v->gainR;
    }
    if (right != NULL) {
      left[i] = (float)l;
      right[i] = (float)r;
    } else {
      left[i] = (float)(0.5 * (l + r));
    }
  }
  if (c->releaseFlags & VOX_RELEASE_ALL_SOUND_AT_END)
    stopAll(s);
  else if (c->releaseFlags & VOX_RELEASE_ALL_NOTES_AT_END)
    fadeAll(s, 0.05);
  return VOX_OK;
}

static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* State v1 (little endian): "VOFD", parameter count (48), pad count (8),
 * 48 plain f32 values, 8 pad MIDI notes, 8 pad choke groups. */
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
  put32(buffer + 8, PAD_COUNT);
  for (uint32_t i = 0; i < PARAM_COUNT; ++i) {
    uint32_t bits;
    memcpy(&bits, &s->values[i], 4);
    put32(buffer + 12 + 4 * i, bits);
  }
  memcpy(buffer + 12 + 4 * PARAM_COUNT, s->padNote, PAD_COUNT);
  memcpy(buffer + 12 + 4 * PARAM_COUNT + PAD_COUNT, s->choke, PAD_COUNT);
  return VOX_OK;
}

static VoxResult VOX_CALL loadState(VoxInstance s, uint32_t version, const uint8_t *bytes,
                                    uint32_t size) {
  if (s == NULL || (bytes == NULL && size > 0))
    return VOX_E_INVALID_ARG;
  if (version > STATE_VERSION)
    return VOX_E_STATE_VERSION;
  if (version != STATE_VERSION || size != STATE_BYTES || get32(bytes) != STATE_MAGIC ||
      get32(bytes + 4) != PARAM_COUNT || get32(bytes + 8) != PAD_COUNT)
    return VOX_E_STATE_CORRUPT;
  float next[PARAM_COUNT];
  for (uint32_t i = 0; i < PARAM_COUNT; ++i) {
    const uint32_t bits = get32(bytes + 12 + 4 * i);
    memcpy(&next[i], &bits, 4);
    if (!validValue(i, next[i]))
      return VOX_E_STATE_CORRUPT;
  }
  const uint8_t *notes = bytes + 12 + 4 * PARAM_COUNT;
  const uint8_t *chokes = notes + PAD_COUNT;
  uint8_t used[128] = {0};
  for (uint32_t p = 0; p < PAD_COUNT; ++p) {
    if (notes[p] > 127 || used[notes[p]] || chokes[p] > MAX_CHOKE_GROUP)
      return VOX_E_STATE_CORRUPT; /* nothing applied */
    used[notes[p]] = 1;
  }
  memcpy(s->values, next, sizeof(next));
  memcpy(s->padNote, notes, PAD_COUNT);
  memcpy(s->choke, chokes, PAD_COUNT);
  rebuildNoteMap(s);
  stopAll(s);
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
