/* Render equivalence of the optimised Drums DSP against the frozen
 * pre-optimisation module (tests/drums/reference, 15456c6).
 *
 * Both modules render the same deterministic scenarios through the public
 * ABI: 44.1/48/96 kHz x 64/128/512 frames, mono main only and stereo with
 * all nine aux buses, for six configurations (defaults; tweaked tune,
 * character, pan, all filter types, transients, sends, pad outputs and kick
 * controls; sample and layer sources with loop/reverse/region/normalize;
 * dense retriggering with voice stealing and chokes; the Internal sequencer
 * in Both mode; mid-block automation), each with a release in the middle
 * (tails and restarts). Every output sample of every bus must match the
 * reference within 1e-6 of the scenario's peak (-120 dB), with no NaN or
 * infinity, and a second render must be identical (determinism). */
#include "instrument/abi/VoxModuleAbiV2.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

VoxResult VOX_CALL drumsReferenceEntryV2(uint32_t, const VoxHostServicesV2 *, const VoxModuleV2 **);
VoxResult VOX_CALL drumsCurrentEntryV2(uint32_t, const VoxHostServicesV2 *, const VoxModuleV2 **);

#define DRUMS "com.ultimavox.fullon-drums"
#define AUX 9u
#define MAX_OUT (2u + 2u * AUX)
#define MAX_EVENTS 512u

static int failures = 0;
static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

typedef struct {
  const VoxModuleV2 *module;
  const VoxInstrumentDescriptorV2 *descriptor;
} Module;

static const VoxParameterDescriptorV2 *param(const Module *m, uint32_t i) {
  return (const VoxParameterDescriptorV2 *)((const uint8_t *)m->descriptor->parameters +
                                            (size_t)i * m->descriptor->parameterStride);
}

static uint32_t indexOf(const Module *m, const char *id) {
  for (uint32_t i = 0; i < m->descriptor->parameterCount; ++i)
    if (strcmp(param(m, i)->id, id) == 0)
      return i;
  fprintf(stderr, "FAIL: no parameter %s\n", id);
  exit(1);
}

static const char *const padKeys[8] = {"kick", "snare", "clap", "chat",
                                       "ohat", "tom",   "rim",  "cymbal"};
static const uint8_t padNotes[8] = {36, 38, 39, 42, 46, 45, 37, 49};

static void setPad(const Module *m, float *v, int pad, const char *key, float x) {
  char id[64];
  snprintf(id, sizeof id, "%s.%s", padKeys[pad], key);
  v[indexOf(m, id)] = x;
}

enum { DEFAULTS, TWEAKED, SAMPLES, DENSE, INTERNAL, AUTOMATION, CONFIGS };
static const char *const configNames[CONFIGS] = {"defaults", "tweaked", "samples",
                                                 "dense",    "internal", "automation"};

static void configure(const Module *m, float *v, int config) {
  for (uint32_t i = 0; i < m->descriptor->parameterCount; ++i)
    v[i] = param(m, i)->defaultValue;
  if (config == TWEAKED || config == SAMPLES || config == DENSE || config == AUTOMATION) {
    for (int pad = 0; pad < 8; ++pad) {
      setPad(m, v, pad, "tune", -7.0f + 2.1f * (float)pad);
      setPad(m, v, pad, "character", 0.1f + 0.11f * (float)pad);
      setPad(m, v, pad, "pan", -0.8f + 0.2f * (float)pad);
      setPad(m, v, pad, "filter", (float)(pad % 6));
      setPad(m, v, pad, "cutoff", 300.0f + 900.0f * (float)pad);
      setPad(m, v, pad, "resonance", 0.1f * (float)pad);
      setPad(m, v, pad, "fine", -40.0f + 11.0f * (float)pad);
      setPad(m, v, pad, "transient_attack", pad % 2 ? 0.6f : -0.4f);
      setPad(m, v, pad, "transient_sustain", pad % 3 ? -0.3f : 0.5f);
      setPad(m, v, pad, "output", (float)(pad % 4));
      setPad(m, v, pad, "send1", 0.1f * (float)pad);
      setPad(m, v, pad, "send2", 0.05f * (float)(8 - pad));
    }
    v[indexOf(m, "kick.pitch_amount")] = 36.0f;
    v[indexOf(m, "kick.drive")] = 4.0f;
    v[indexOf(m, "kick.sub")] = 0.7f;
    v[indexOf(m, "kit.velocity")] = 0.5f;
  }
  if (config == SAMPLES || config == DENSE)
    for (int pad = 0; pad < 8; ++pad) {
      setPad(m, v, pad, "source", (float)(1 + pad % 2)); /* Sample / Layer */
      setPad(m, v, pad, "sample_loop", pad % 3 == 0 ? 1.0f : 0.0f);
      setPad(m, v, pad, "sample_reverse", pad % 4 == 1 ? 1.0f : 0.0f);
      setPad(m, v, pad, "sample_start", 0.05f * (float)(pad % 3));
      setPad(m, v, pad, "sample_end", 1.0f - 0.1f * (float)(pad % 2));
      setPad(m, v, pad, "sample_normalize", pad % 2 ? 1.0f : 0.0f);
    }
  if (config == INTERNAL)
    v[indexOf(m, "seq.mode")] = 2.0f; /* Both */
}

#define SAMPLE_FRAMES 30000
static float sampleL[SAMPLE_FRAMES], sampleR[SAMPLE_FRAMES];
static const float *sampleChannels[2] = {sampleL, sampleR};

static uint32_t rng;

/* Wall time spent inside process() by the last render (informational: the
 * same scenarios, same process and machine, reference versus current). */
static double processSeconds;
static double now(void) {
  struct timespec t;
  timespec_get(&t, TIME_UTC);
  return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}
static uint32_t rnd(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

typedef struct {
  int config;
  double rate;
  uint32_t block, channels;
  int aux;
  uint64_t frames;
} Scenario;

/* Renders a scenario; returns the outputs, bus-major: outputs x frames. */
static float *render(const Module *m, const Scenario *sc, uint32_t *outputCount) {
  VoxInstance s = NULL;
  VoxCreateParamsV2 cp = {sizeof cp, VOX_MODULE_ABI_V2, 0, 0};
  check(m->module->create(DRUMS, &cp, &s) == VOX_OK, "create");
  const VoxPrepareSpecV2 ps = {sizeof ps, VOX_MODULE_ABI_V2, sc->rate, sc->block, sc->channels,
                               m->descriptor->parameterCount, MAX_EVENTS};
  check(m->module->prepare(s, &ps) == VOX_OK, "prepare");
  float *values = malloc(sizeof(float) * m->descriptor->parameterCount);
  configure(m, values, sc->config);
  check(m->module->setAllParameters(s, values, m->descriptor->parameterCount) == VOX_OK, "params");
  VoxSampleV2 samples[8];
  memset(samples, 0, sizeof samples);
  for (int p = 0; p < 8; ++p) {
    samples[p].structSize = sizeof(VoxSampleV2);
    samples[p].version = VOX_MODULE_ABI_V2;
    samples[p].id = (uint64_t)(p + 1);
    samples[p].channels = sampleChannels;
    samples[p].channelCount = p % 2 ? 2u : 1u;
    samples[p].frames = SAMPLE_FRAMES - 1000u * (uint32_t)p;
    samples[p].sampleRate = p % 2 ? 44100.0 : 48000.0;
    samples[p].peak = 0.8f;
  }
  static VoxPatternStepV2 cells[8 * 4 * 16];
  VoxPatternV2 pattern = {sizeof pattern, VOX_MODULE_ABI_V2, 0, 0, 0, 0,
                          sizeof(VoxPatternStepV2), 8 * 4 * 16, cells};
  const char *name = NULL;
  check(m->module->content->getFactoryPattern(DRUMS, 1, &name, &pattern) == VOX_OK, "pattern");
  VoxTransportV2 tr;
  memset(&tr, 0, sizeof tr);
  tr.structSize = sizeof tr;
  tr.version = VOX_MODULE_ABI_V2;
  tr.tempoBpm = 145.0;
  tr.timeSigNumerator = 4;
  tr.timeSigDenominator = 4;
  tr.flags = VOX_TRANSPORT_HAS_TEMPO | VOX_TRANSPORT_HAS_PPQ | VOX_TRANSPORT_HAS_BAR_START |
             VOX_TRANSPORT_HAS_TIME_SIGNATURE | VOX_TRANSPORT_HAS_TIME |
             (sc->config == INTERNAL ? VOX_TRANSPORT_PLAYING : 0u);
  static VoxMidiEventV2 events[MAX_EVENTS];
  static VoxParamChangeV2 changes[8];
  uint64_t ids[16];
  VoxSampleUsageV2 usage = {sizeof usage, VOX_MODULE_ABI_V2, 16, 0, ids, 0, 0};
  const uint32_t outputs = sc->channels + (sc->aux ? AUX * sc->channels : 0u);
  float *out = calloc((size_t)outputs * sc->frames, sizeof(float));
  float *block = malloc(sizeof(float) * MAX_OUT * sc->block);
  float *mainOut[2], *aux[2 * AUX];
  for (uint32_t k = 0; k < 2; ++k)
    mainOut[k] = block + (size_t)k * sc->block;
  for (uint32_t k = 0; k < AUX * sc->channels; ++k)
    aux[k] = block + (size_t)(sc->channels + k) * sc->block;
  rng = 12345u + (uint32_t)sc->config * 77u + sc->block;
  processSeconds = 0.0;
  const double every = sc->config == DENSE ? 0.0004 : 0.031; /* seconds between hits */
  const uint64_t period = (uint64_t)(every * sc->rate) + 1;
  for (uint64_t pos = 0; pos < sc->frames;) {
    const uint32_t n = (uint32_t)(sc->frames - pos < sc->block ? sc->frames - pos : sc->block);
    uint32_t ne = 0, np = 0;
    for (uint32_t i = 0; i < n && ne < MAX_EVENTS; ++i)
      if ((pos + i) % period == 0) {
        const int pad = (int)(rnd() % 8);
        memset(&events[ne], 0, sizeof events[ne]);
        events[ne].sampleOffset = i;
        events[ne].size = 3;
        events[ne].bytes[0] = 0x90;
        events[ne].bytes[1] = padNotes[pad];
        events[ne].bytes[2] = (uint8_t)(20 + rnd() % 108);
        ++ne;
      }
    if (sc->config == AUTOMATION && (pos / sc->block) % 3 == 0) {
      changes[np++] = (VoxParamChangeV2){indexOf(m, "kick.tune"), n / 3,
                                         (float)((int)(rnd() % 24) - 12), 0};
      changes[np++] = (VoxParamChangeV2){indexOf(m, "snare.cutoff"), n / 3,
                                         200.0f + (float)(rnd() % 10000), 0};
      changes[np++] = (VoxParamChangeV2){indexOf(m, "chat.decay"), n - 1,
                                         0.02f + (float)(rnd() % 400) / 1000.0f, 0};
    }
    tr.ppqPosition = (double)pos / sc->rate * tr.tempoBpm / 60.0;
    tr.barStartPpq = floor(tr.ppqPosition / 4.0) * 4.0;
    tr.timeInSamples = (int64_t)pos;
    VoxProcessContextV2 c;
    memset(&c, 0, sizeof c);
    c.structSize = sizeof c;
    c.version = VOX_MODULE_ABI_V2;
    c.numSamples = n;
    c.outputChannelCount = sc->channels;
    c.outputs = mainOut;
    c.paramChanges = changes;
    c.paramChangeCount = np;
    c.paramChangeStride = sizeof(VoxParamChangeV2);
    c.midiEvents = events;
    c.midiEventCount = ne;
    c.midiEventStride = sizeof(VoxMidiEventV2);
    c.samplePosition = pos;
    c.transport = &tr;
    c.pattern = &pattern;
    c.patternGeneration = 1;
    c.auxOutputs = sc->aux ? aux : NULL;
    c.auxBusCount = sc->aux ? AUX : 0u;
    const int sampled = sc->config == SAMPLES || sc->config == DENSE;
    c.samples = sampled ? samples : NULL;
    c.sampleSlotCount = sampled ? 8u : 0u;
    c.sampleUsage = &usage;
    if (pos >= sc->frames / 2 && pos < sc->frames / 2 + n)
      c.releaseFlags = VOX_RELEASE_ALL_NOTES_AT_END; /* tails, then new hits */
    const double t0 = now();
    const VoxResult processed = m->module->process(s, &c);
    processSeconds += now() - t0;
    check(processed == VOX_OK, "process");
    for (uint32_t k = 0; k < outputs; ++k)
      memcpy(out + (size_t)k * sc->frames + pos, block + (size_t)k * sc->block,
             sizeof(float) * n);
    pos += n;
  }
  m->module->destroy(s);
  free(values);
  free(block);
  *outputCount = outputs;
  return out;
}

static Module openModule(VoxResult(VOX_CALL *entry)(uint32_t, const VoxHostServicesV2 *,
                                               const VoxModuleV2 **)) {
  static const VoxHostServicesV2 services = {sizeof(VoxHostServicesV2), VOX_MODULE_ABI_V2, NULL,
                                             NULL};
  Module m = {NULL, NULL};
  check(entry(VOX_MODULE_ABI_V2, &services, &m.module) == VOX_OK && m.module != NULL, "entry");
  check(m.module->getInstrument(0, &m.descriptor) == VOX_OK, "descriptor");
  return m;
}

int main(void) {
  rng = 777u;
  for (int i = 0; i < SAMPLE_FRAMES; ++i) {
    sampleL[i] = (float)(0.7 * sin(i * 0.05) * exp(-i / 8000.0) +
                         0.1 * ((int)(rnd() % 2001) - 1000) / 1000.0);
    sampleR[i] = (float)(0.6 * sin(i * 0.031 + 1.0) * exp(-i / 6000.0));
  }
  const Module reference = openModule(drumsReferenceEntryV2);
  const Module current = openModule(drumsCurrentEntryV2);
  check(reference.descriptor->parameterCount == current.descriptor->parameterCount,
        "same descriptor count");
  if (failures)
    return 1;
  const double rates[3] = {44100.0, 48000.0, 96000.0};
  const uint32_t blocks[3] = {64, 128, 512};
  double worstDb = -999.0, auxPeak = 0.0, refTotal = 0.0, curTotal = 0.0;
  double ref96[CONFIGS] = {0}, cur96[CONFIGS] = {0};
  unsigned long long compared = 0, differing = 0;
  int scenarios = 0;
  for (int config = 0; config < CONFIGS; ++config)
    for (int r = 0; r < 3; ++r)
      for (int b = 0; b < 3; ++b)
        for (int layout = 0; layout < 2; ++layout) {
          /* mono main only, or stereo with every aux bus */
          const Scenario sc = {config, rates[r], blocks[b], layout ? 2u : 1u, layout,
                               (uint64_t)(rates[r] * (config == DENSE ? 0.6 : 1.5))};
          uint32_t na = 0, nb = 0, nc = 0;
          /* Interleaved, best of two each, for the timing below. */
          float *a = render(&reference, &sc, &na);
          double tRef = processSeconds;
          float *bb = render(&current, &sc, &nb);
          double tCur = processSeconds;
          free(render(&reference, &sc, &na));
          tRef = processSeconds < tRef ? processSeconds : tRef;
          float *again = render(&current, &sc, &nc);
          tCur = processSeconds < tCur ? processSeconds : tCur;
          refTotal += tRef;
          curTotal += tCur;
          if (r == 2 && b == 0 && layout) {
            ref96[config] = tRef / (double)sc.frames;
            cur96[config] = tCur / (double)sc.frames;
          }
          check(na == nb && nb == nc, "same output count");
          const size_t total = (size_t)na * sc.frames;
          double peak = 0.0, maxd = 0.0;
          int finite = 1;
          for (size_t i = 0; i < total; ++i) {
            if (!isfinite(bb[i]))
              finite = 0;
            const double d = fabs((double)a[i] - (double)bb[i]);
            if (fabs(a[i]) > peak)
              peak = fabs(a[i]);
            if (d > maxd)
              maxd = d;
            differing += d != 0.0;
            if (layout && i >= (size_t)2 * sc.frames && fabs(a[i]) > auxPeak)
              auxPeak = fabs(a[i]);
          }
          compared += total;
          char what[160];
          snprintf(what, sizeof what, "%s %.0f Hz / %u, %s: max |d| %.3g, peak %.3g",
                   configNames[config], sc.rate, sc.block, layout ? "stereo + aux" : "mono",
                   maxd, peak);
          check(finite, what);
          check(peak > 0.0, what);
          check(maxd <= 1e-6 * peak, what);
          check(memcmp(bb, again, total * sizeof(float)) == 0, "deterministic re-render");
          if (maxd > 0.0 && 20.0 * log10(maxd / peak) > worstDb)
            worstDb = 20.0 * log10(maxd / peak);
          free(a);
          free(bb);
          free(again);
          ++scenarios;
        }
  check(auxPeak > 0.0, "aux buses carry signal");
  printf("module process() time, reference -> current (best of 2, same process):\n");
  for (int config = 0; config < CONFIGS; ++config)
    printf("  %-10s 96 kHz/64 stereo+aux: %7.1f -> %7.1f ns/frame (%+.0f%%)\n", configNames[config],
           1e9 * ref96[config], 1e9 * cur96[config], 100.0 * (cur96[config] / ref96[config] - 1.0));
  printf("  all %d scenarios: %.3f s -> %.3f s (%+.0f%%)\n", scenarios, refTotal, curTotal,
         100.0 * (curTotal / refTotal - 1.0));
  printf("drums render equivalence: %d scenarios, %llu samples, %llu differ, worst %.1f dB "
         "re scenario peak\n",
         scenarios, compared, differing, worstDb);
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("drums render equivalence tests passed\n");
  return 0;
}
