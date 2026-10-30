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
 * by CC120 / panic / reset.
 *
 * Sequencer: plays the Rack-owned pattern snapshot (VoxContentApiV2, INTERNAL
 * playback: 8 lanes, up to 16 steps x 4 bars, velocity + accent), driven only
 * by the Rack's VoxTransportV2 (tempo, ppq, playing, discontinuity). The Rack
 * is the durable owner of the pattern; the module keeps a runtime copy that
 * it refreshes when the snapshot generation changes. Factory patterns are
 * exported through the content API. Seq Mode: Host MIDI (default; sequencer
 * off), Internal (sequencer only, incoming notes ignored so a MIDI clip
 * cannot double it) or Both. */
#define VOX_BUILDING_MODULE 1
#include "instrument/abi/VoxModuleAbiV2.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define DRUMS_ID "com.ultimavox.fullon-drums"
#define PAD_COUNT 8u
#define PAD_PARAMS 5u
#define GLOBAL_PARAMS 8u
#define SEQ_PARAMS 11u
#define PARAM_COUNT (PAD_COUNT * PAD_PARAMS + GLOBAL_PARAMS + SEQ_PARAMS) /* 59 */
#define MAX_VOICES 16
#define STATE_VERSION 3u
#define STATE_MAGIC 0x44464f56u /* "VOFD" */
#define STATE_BYTES_FOR(count) (12u + (count) * 4u + PAD_COUNT * 2u)
#define STATE_BYTES STATE_BYTES_FOR(PARAM_COUNT)
#define MAX_STATE_BYTES 4096u
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
  G_VELOCITY,                        /* 47 */
  S_MODE,                            /* 48 Host / Internal / Both */
  S_SWING,                           /* 49 */
  S_ACCENT,                          /* 50 */
  S_LANE0                            /* 51..58 lane on/off per pad */
};
enum { MODE_HOST, MODE_INTERNAL, MODE_BOTH };
#define PATTERN_COUNT 5
#define BARS 4    /* pattern bars (the factory fill is bar 4) */
#define FILL_BAR 3
#define STEPS 16  /* steps per bar */
#define CELLS (PAD_COUNT * BARS * STEPS)
#define ACCENT 0x80u

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

static const char *const modeChoices[] = {"Host MIDI", "Internal", "Both"};
static const char *const patternNames[PATTERN_COUNT] = {
    "Full-on Basic", "Full-on Driving", "Rolling Offbeat", "Breakdown", "Kick & Offbeat"};
#define C(id, name, choices, n, def, pool)                                                     \
  {sizeof(VoxParameterDescriptorV2), VOX_MODULE_ABI_V2, id, name, "", "Sequencer", choices,    \
   VOX_PARAM_CHOICE, n, 0.0f, (float)((n) - 1), def, 1.0f, pool, EXPOSED}
#define LANE(id, name, pool) P(id, name, "", "Sequencer", VOX_PARAM_BOOLEAN, 0.0f, 1.0f, 1.0f, 1.0f, pool)

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
    /* Sequencer performance controls. The pattern itself (cells, steps, bars,
     * fill) is Rack-owned content, not parameters. */
    C("seq.mode", "Seq Mode", modeChoices, 3, 0.0f, 48),
    P("seq.swing", "Seq Swing", "", "Sequencer", VOX_PARAM_FLOAT, 0.0f, 0.5f, 0.0f, 0.0f, 49),
    P("seq.accent", "Seq Accent", "", "Sequencer", VOX_PARAM_FLOAT, 0.0f, 1.0f, 0.5f, 0.0f, 50),
    LANE("seq.lane.kick", "Lane Kick", 51), LANE("seq.lane.snare", "Lane Snare", 52),
    LANE("seq.lane.clap", "Lane Clap", 53), LANE("seq.lane.chat", "Lane Closed Hat", 54),
    LANE("seq.lane.ohat", "Lane Open Hat", 55), LANE("seq.lane.tom", "Lane Tom", 56),
    LANE("seq.lane.rim", "Lane Rim", 57), LANE("seq.lane.cymbal", "Lane Cymbal", 58),
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
  /* sequencer: runtime copy of the Rack's pattern snapshot */
  VoxPatternStepV2 cells[CELLS]; /* [lane][bar][step], dimensions below */
  uint32_t patternSteps, patternBars, patternTiming;
  uint64_t patternGeneration;
  int havePattern;
  int64_t lastStep;   /* last sequenced 16th (absolute), INT64_MIN = none */
  double lastPpqStart;
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

/* ---------------------------------------------------------- patterns */
/* Factory patterns, 145 BPM full-on idiom: bars 1..3 evolve, bar 4 is the
 * fill. Cell = velocity (1..127) | ACCENT, 0 = rest. Exported to the Rack as
 * V2 patterns through the content API; deterministic, no allocation. */
static void set(uint8_t (*p)[PAD_COUNT][STEPS], int bar, int pad, int step, int vel, int acc) {
  p[bar][pad][step] = (uint8_t)(vel | (acc ? ACCENT : 0));
}

static void buildPattern(int index, uint8_t (*p)[PAD_COUNT][STEPS]) {
  memset(p, 0, sizeof(uint8_t) * BARS * PAD_COUNT * STEPS);
  for (int bar = 0; bar < BARS; ++bar) {
    const int fill = bar == FILL_BAR;
    const int evo = fill ? BARS : bar; /* the fill is not an evolution bar */
    for (int st = 0; st < STEPS; ++st) {
      const int beat = st % 4 == 0, off = st % 4 == 2;
      switch (index) {
      case 0: /* Full-on Basic */
      case 1: /* Full-on Driving */
        if (beat && !(fill && st == 12))
          set(p, bar, PAD_KICK, st, 120, st == 0);
        if (off)
          set(p, bar, PAD_OPEN_HAT, st, 92, 1);
        else if (!(fill && st >= 12))
          set(p, bar, PAD_CLOSED_HAT, st, st % 2 ? 66 : 92, 0);
        if (st == 4 || st == 12) {
          set(p, bar, PAD_SNARE, st, 110, 0);
          set(p, bar, PAD_CLAP, st, 100, 0);
        }
        if (index == 1 && st % 4 == 3)
          set(p, bar, PAD_RIM, st, 58, 0);
        if (evo == 1 && st == 7)
          set(p, bar, PAD_RIM, st, 80, 1);
        if (evo == 0 && st == 0)
          set(p, bar, PAD_CYMBAL, st, 96, 0);
        if (fill && st >= 12)
          set(p, bar, PAD_TOM, st, 84 + 10 * (st - 12), st == 15);
        if (fill && (st == 13 || st == 14 || st == 15))
          set(p, bar, PAD_SNARE, st, 70 + 12 * (st - 13), 0);
        break;
      case 2: /* Rolling Offbeat: e/a hats, clap only, ghost toms */
        if (beat)
          set(p, bar, PAD_KICK, st, 118, st == 0);
        if (off)
          set(p, bar, PAD_OPEN_HAT, st, 96, 1);
        if (st % 2 == 1)
          set(p, bar, PAD_CLOSED_HAT, st, 72, 0);
        if (st == 4 || st == 12)
          set(p, bar, PAD_CLAP, st, 104, 0);
        if ((evo == 2 || fill) && (st == 11 || st == 15))
          set(p, bar, PAD_TOM, st, 64, 0);
        break;
      case 3: /* Breakdown: no kick */
        if (off)
          set(p, bar, PAD_OPEN_HAT, st, 80, 0);
        if (st % 2 == 0 && !off)
          set(p, bar, PAD_CLOSED_HAT, st, 60, 0);
        if (st == 12)
          set(p, bar, PAD_CLAP, st, 90, 0);
        if (evo == 0 && st == 0)
          set(p, bar, PAD_CYMBAL, st, 90, 0);
        if (fill && st >= 8 && st % 2 == 0)
          set(p, bar, PAD_SNARE, st, 60 + 8 * (st - 8), st == 14);
        break;
      default: /* Kick & Offbeat: space for a rolling bass */
        if (beat)
          set(p, bar, PAD_KICK, st, 120, st == 0);
        if (off)
          set(p, bar, PAD_OPEN_HAT, st, 90, 1);
        break;
      }
    }
  }
}

static void resetSequencer(struct VoxInstanceT *s) {
  s->lastStep = INT64_MIN;
  s->lastPpqStart = -HUGE_VAL;
}

static VoxPatternStepV2 restStep(void) {
  VoxPatternStepV2 r;
  memset(&r, 0, sizeof(r));
  r.velocity = VOX_STEP_DEFAULT_VELOCITY;
  r.gate = VOX_STEP_DEFAULT_GATE;
  r.probability = VOX_STEP_DEFAULT_PROBABILITY;
  r.ratchet = VOX_STEP_DEFAULT_RATCHET;
  return r;
}

/* Takes the Rack's snapshot when its generation changes. A snapshot that does
 * not fit this kit (the Rack validates, so only a broken host sends one) is
 * ignored and the previous copy keeps playing. No pattern: the sequencer is
 * silent. Bounded copy (at most CELLS cells), no allocation. */
static void takePattern(struct VoxInstanceT *s, const VoxProcessContextV2 *c) {
  const size_t need = offsetof(VoxProcessContextV2, patternGeneration) + sizeof(uint64_t);
  const VoxPatternV2 *p = c->structSize >= need ? c->pattern : NULL;
  if (p == NULL) {
    s->havePattern = 0;
    return;
  }
  if (s->havePattern && c->patternGeneration == s->patternGeneration)
    return;
  const uint32_t used = p->laneCount * p->stepsPerBar * p->barCount;
  if (p->structSize < sizeof(VoxPatternV2) || p->version != VOX_MODULE_ABI_V2 ||
      p->cells == NULL || p->laneCount != PAD_COUNT || p->stepsPerBar < 1 ||
      p->stepsPerBar > STEPS || p->barCount < 1 || p->barCount > BARS ||
      p->timing > VOX_TIMING_EIGHTH_TRIPLET || p->cellStride < sizeof(VoxPatternStepV2) ||
      p->cellStride > MAX_RECORD_STRIDE || p->cellStride % _Alignof(VoxPatternStepV2) != 0 ||
      used > p->cellCapacity)
    return;
  const VoxPatternStepV2 rest = restStep();
  for (uint32_t i = 0; i < CELLS; ++i)
    s->cells[i] = rest;
  for (uint32_t i = 0; i < used; ++i)
    memcpy(&s->cells[i], (const uint8_t *)p->cells + (size_t)i * p->cellStride,
           sizeof(VoxPatternStepV2));
  s->patternSteps = p->stepsPerBar;
  s->patternBars = p->barCount;
  s->patternTiming = p->timing;
  s->patternGeneration = c->patternGeneration;
  s->havePattern = 1;
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
  resetSequencer(s);
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
  resetSequencer(s);
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
  resetSequencer(s);
  return VOX_OK;
}

/* Order in which pads hit on the same sample fire. The open hat fires before
 * the closed hat, so with Hat Choke on a coincident closed hat cuts the open
 * hat ("closed hat cuts open hat" holds for same-sample hits too). With Hat
 * Choke off both ring. */
static const int fireOrder[PAD_COUNT] = {PAD_KICK,       PAD_SNARE, PAD_CLAP, PAD_OPEN_HAT,
                                         PAD_CLOSED_HAT, PAD_TOM,   PAD_RIM,  PAD_CYMBAL};

/* NoteOns are not triggered here: they are collected per sample into `hits`
 * (the loudest velocity per pad) and triggered afterwards in pad order. */
static void applyMidi(struct VoxInstanceT *s, const VoxMidiEventV2 *e, int hits[PAD_COUNT]) {
  if (e->size < 3)
    return;
  const uint8_t status = (uint8_t)(e->bytes[0] & 0xF0u);
  const int data1 = e->bytes[1] & 0x7F;
  const int data2 = e->bytes[2] & 0x7F;
  if (status == 0x90u && data2 > 0) {
    if ((int)s->values[S_MODE] == MODE_INTERNAL)
      return; /* Internal: the sequencer alone plays; no double triggers */
    const int pad = s->noteToPad[data1];
    if (pad >= 0 && data2 > hits[pad])
      hits[pad] = data2;
  } else if (status == 0xB0u) {
    if (data1 == 120)
      stopAll(s); /* All Sound Off: immediate */
    else if (data1 == 123)
      fadeAll(s, 0.05); /* All Notes Off: bounded 50 ms tail */
  }
  /* NoteOff and sustain do not affect one-shot drum voices. */
}

/* ------------------------------------------------------- sequencer */
#define MAX_SEQ_HITS 256
typedef struct {
  uint32_t offset;
  uint8_t pad, velocity, accent;
} SeqHit;

/* Schedules this block's internal-sequencer hits from VoxTransportV2 only
 * (no wall clock, no sample counting). Stateless apart from lastStep, which
 * prevents a step from playing twice; a backwards jump (loop, seek) clears
 * it. Steps sit on the pattern's grid of the host's ppq (1/16, 1/8, 1/32 or
 * 1/8 triplets); odd steps are delayed by swing. The bar of a step is its
 * position modulo the pattern length, so a new snapshot continues at the
 * current musical position instead of restarting. */
static uint32_t scheduleSequencer(struct VoxInstanceT *s, const VoxProcessContextV2 *c,
                                  SeqHit *hits) {
  const int mode = (int)s->values[S_MODE];
  const VoxTransportV2 *tr = c->transport;
  const uint32_t need = VOX_TRANSPORT_PLAYING | VOX_TRANSPORT_HAS_TEMPO | VOX_TRANSPORT_HAS_PPQ;
  if (mode == MODE_HOST || !s->havePattern || (tr->flags & need) != need ||
      !(tr->tempoBpm > 0.0) || tr->tempoBpm > 1000.0 || !isfinite(tr->ppqPosition) ||
      c->numSamples == 0)
    return 0;
  const double ppq0 = tr->ppqPosition;
  if ((tr->flags & VOX_TRANSPORT_DISCONTINUITY) && ppq0 < s->lastPpqStart)
    s->lastStep = INT64_MIN; /* moved backwards: steps may play again */
  s->lastPpqStart = ppq0;

  static const double stepPpq[4] = {0.25, 0.5, 0.125, 1.0 / 3.0};
  const double step = stepPpq[s->patternTiming];
  const double samplesPerPpq = 60.0 * s->sampleRate / tr->tempoBpm;
  const double swing = s->values[S_SWING] * step;
  const double accent = s->values[S_ACCENT];
  const int64_t steps = (int64_t)s->patternSteps;
  const int64_t bars = (int64_t)s->patternBars;
  uint32_t count = 0;
  for (int64_t g = (int64_t)floor(ppq0 / step) - 1;; ++g) {
    if (g < 0)
      continue;
    const double t = (double)g * step + ((g & 1) ? swing : 0.0);
    const double at = ceil((t - ppq0) * samplesPerPpq - 1e-6);
    if (at >= (double)c->numSamples)
      break;
    if (at < 0.0 || g <= s->lastStep)
      continue;
    s->lastStep = g;
    const uint32_t st = (uint32_t)(g % steps);
    const uint32_t bar = (uint32_t)((g / steps) % bars);
    for (uint32_t lane = 0; lane < PAD_COUNT && count < MAX_SEQ_HITS; ++lane) {
      const VoxPatternStepV2 *cell = &s->cells[(lane * s->patternBars + bar) * s->patternSteps + st];
      if (!(cell->flags & VOX_STEP_ON) || s->values[S_LANE0 + lane] < 0.5f)
        continue;
      double velocity = cell->velocity;
      if (cell->flags & VOX_STEP_ACCENT)
        velocity += accent * (127.0 - velocity);
      hits[count].offset = (uint32_t)at;
      hits[count].pad = (uint8_t)lane;
      hits[count].velocity = (uint8_t)fmin(127.0, floor(velocity + 0.5));
      hits[count].accent = (cell->flags & VOX_STEP_ACCENT) != 0;
      ++count;
    }
  }
  return count;
}

/* Reports this block's sequencer hits (they all sound: lane switches and mode
 * were applied when scheduling) to the host's note output, if it listens, and
 * whether routed host NoteOns could sound in this block (V2.2 hosts only). */
static void reportHits(const VoxProcessContextV2 *c, const SeqHit *hits, uint32_t count,
                       const uint8_t *padNote, int routedIgnored) {
  const size_t need = offsetof(VoxProcessContextV2, noteOutput) + sizeof(VoxNoteOutputV2 *);
  VoxNoteOutputV2 *out = c->structSize >= need ? c->noteOutput : NULL;
  if (out == NULL || out->structSize < VOX_NOTE_OUTPUT_V2_BASE_SIZE ||
      out->version != VOX_MODULE_ABI_V2 || out->stride < sizeof(VoxNoteEventV2) ||
      out->stride > MAX_RECORD_STRIDE || out->stride % _Alignof(VoxNoteEventV2) != 0 ||
      (out->capacity > 0 && out->events == NULL))
    return;
  out->count = 0;
  out->dropped = 0;
  if (out->structSize >= offsetof(VoxNoteOutputV2, reserved) + sizeof(out->reserved)) {
    out->routedNotes = routedIgnored ? VOX_ROUTED_NOTES_ALL_IGNORED : VOX_ROUTED_NOTES_MAY_PLAY;
    out->reserved = 0;
  }
  for (uint32_t i = 0; i < count; ++i) {
    if (out->count == out->capacity) {
      ++out->dropped;
      continue;
    }
    VoxNoteEventV2 e;
    e.sampleOffset = hits[i].offset;
    e.note = padNote[hits[i].pad];
    e.velocity = hits[i].velocity;
    e.lane = hits[i].pad;
    e.flags = hits[i].accent ? VOX_NOTE_EVENT_ACCENT : 0u;
    memcpy((uint8_t *)out->events + (size_t)out->count * out->stride, &e, sizeof(e));
    ++out->count;
  }
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
    stopAll(s);
  const uint8_t *changes = (const uint8_t *)c->paramChanges;
  const uint8_t *events = (const uint8_t *)c->midiEvents;
  uint32_t nextChange = 0, nextEvent = 0, nextHit = 0;
  /* Changes at the block start apply before the sequencer is scheduled, so a
   * mode or pattern switch takes effect on this block's first step. */
  while (nextChange < c->paramChangeCount) {
    VoxParamChangeV2 p;
    memcpy(&p, changes + (size_t)nextChange * c->paramChangeStride, sizeof(p));
    if (p.sampleOffset > 0)
      break;
    if (p.index < PARAM_COUNT && validValue(p.index, p.plain))
      s->values[p.index] = p.plain;
    ++nextChange;
  }
  takePattern(s, c);
  SeqHit seq[MAX_SEQ_HITS];
  const uint32_t seqCount = scheduleSequencer(s, c, seq);
  /* Internal mode drops routed NoteOns (applyMidi). That holds for the whole
   * block only if no later change in it leaves Internal mode; otherwise some
   * routed notes may sound and the block says so. */
  int routedIgnored = (int)s->values[S_MODE] == MODE_INTERNAL;
  for (uint32_t k = nextChange; routedIgnored && k < c->paramChangeCount; ++k) {
    VoxParamChangeV2 p;
    memcpy(&p, changes + (size_t)k * c->paramChangeStride, sizeof(p));
    if (p.index == S_MODE && validValue(p.index, p.plain) && (int)p.plain != MODE_INTERNAL)
      routedIgnored = 0;
  }
  reportHits(c, seq, seqCount, s->padNote, routedIgnored);
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
    /* Hits that land on the same sample (several DAW tracks merged into this
     * slot's channel, or Host MIDI plus the sequencer) do not depend on the
     * order they arrive in: controllers apply first, then each pad fires
     * once, at its loudest velocity, in a fixed order. */
    int hits[PAD_COUNT] = {0};
    while (nextEvent < c->midiEventCount) {
      VoxMidiEventV2 e;
      memcpy(&e, events + (size_t)nextEvent * c->midiEventStride, sizeof(e));
      if (e.sampleOffset > i)
        break;
      applyMidi(s, &e, hits);
      ++nextEvent;
    }
    while (nextHit < seqCount && seq[nextHit].offset <= i) {
      if (seq[nextHit].velocity > hits[seq[nextHit].pad])
        hits[seq[nextHit].pad] = seq[nextHit].velocity;
      ++nextHit;
    }
    for (uint32_t k = 0; k < PAD_COUNT; ++k)
      if (hits[fireOrder[k]] > 0)
        trigger(s, fireOrder[k], hits[fireOrder[k]]);
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

/* State (little endian): "VOFD", parameter count (59), pad count (8), plain
 * f32 values, 8 pad MIDI notes, 8 pad choke groups. Only the current version
 * loads; there is no migration from pre-release formats. */
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
  if (version != STATE_VERSION)
    return VOX_E_STATE_VERSION;
  const uint32_t count = PARAM_COUNT;
  if (size != STATE_BYTES_FOR(count) || get32(bytes) != STATE_MAGIC ||
      get32(bytes + 4) != count || get32(bytes + 8) != PAD_COUNT)
    return VOX_E_STATE_CORRUPT;
  float next[PARAM_COUNT];
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t bits = get32(bytes + 12 + 4 * i);
    memcpy(&next[i], &bits, 4);
    if (!validValue(i, next[i]))
      return VOX_E_STATE_CORRUPT;
  }
  const uint8_t *notes = bytes + 12 + 4 * count;
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
  resetSequencer(s);
  return VOX_OK;
}

static VoxResult VOX_CALL shutdown(void) {
  return liveInstances == 0 ? VOX_OK : VOX_E_INTERNAL;
}

/* ------------------------------------------------------ content API */
static const char *const laneNames[PAD_COUNT] = {"Kick", "Snare", "Clap", "Closed Hat",
                                                 "Open Hat", "Tom", "Rim", "Cymbal"};
static const VoxPatternCapsV2 patternCaps = {
    sizeof(VoxPatternCapsV2), VOX_MODULE_ABI_V2, VOX_PATTERN_PLAYBACK_INTERNAL, PAD_COUNT, STEPS,
    BARS, VOX_FIELD_VELOCITY | VOX_FIELD_ACCENT, PATTERN_COUNT, laneNames, defaultNotes};

static VoxResult VOX_CALL getPatternCaps(const char *id, const VoxPatternCapsV2 **out) {
  if (id == NULL || out == NULL)
    return VOX_E_INVALID_ARG;
  *out = strcmp(id, DRUMS_ID) == 0 ? &patternCaps : NULL;
  return VOX_OK;
}

static VoxResult VOX_CALL getFactoryPattern(const char *id, uint32_t index, const char **name,
                                            VoxPatternV2 *p) {
  if (id == NULL || strcmp(id, DRUMS_ID) != 0 || index >= PATTERN_COUNT || name == NULL ||
      p == NULL || p->structSize < sizeof(VoxPatternV2) || p->version != VOX_MODULE_ABI_V2 ||
      p->cells == NULL || p->cellStride < sizeof(VoxPatternStepV2) ||
      p->cellStride > MAX_RECORD_STRIDE || p->cellCapacity < CELLS)
    return VOX_E_INVALID_ARG;
  uint8_t grid[BARS][PAD_COUNT][STEPS];
  buildPattern((int)index, grid);
  p->laneCount = PAD_COUNT;
  p->stepsPerBar = STEPS;
  p->barCount = BARS;
  p->timing = VOX_TIMING_SIXTEENTH;
  for (uint32_t lane = 0; lane < PAD_COUNT; ++lane)
    for (uint32_t bar = 0; bar < BARS; ++bar)
      for (uint32_t st = 0; st < STEPS; ++st) {
        VoxPatternStepV2 cell = restStep();
        const uint8_t v = grid[bar][lane][st];
        if (v != 0) {
          cell.flags = (uint8_t)(VOX_STEP_ON | ((v & ACCENT) ? VOX_STEP_ACCENT : 0u));
          cell.velocity = (uint8_t)(v & 0x7Fu);
        }
        memcpy((uint8_t *)p->cells + (size_t)((lane * BARS + bar) * STEPS + st) * p->cellStride,
               &cell, sizeof(cell));
      }
  *name = patternNames[index];
  return VOX_OK;
}

static const VoxContentApiV2 contentApi = {sizeof(VoxContentApiV2), VOX_MODULE_ABI_V2,
                                           getPatternCaps, getFactoryPattern};

static const VoxModuleV2 moduleTable = {
    sizeof(VoxModuleV2), VOX_MODULE_ABI_V2, 1, 0, getInstrument, create, destroy, prepare,
    setAllParameters, reset, process, saveState, loadState, &contentApi, NULL, shutdown};

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
