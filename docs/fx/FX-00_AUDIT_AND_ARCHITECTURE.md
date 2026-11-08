# VOX FX — FX-00 audit and architecture proposal

Status: **proposal for independent Architecture Guard / owner review**. No
production code. This is not a merge authorization and does not touch the
Rack + VOX Drums 1.0 release scope (#77, #78).

- Baseline audited: `integration/rack-1.0` @ `f9c8bc642567ba0edc6b638b64c72f9dc6ccf2e1`.
  The open performance chain #106 → #107 → #109 changes no ABI or host
  contract used below.
- Input: owner brief "VOX FX — Production Trance & Electronic FX Instrument"
  (8 Nov 2026). Section numbers below (§) refer to it.

## 1. Summary

VOX FX can be built as one independent native `.vins` instrument, but **not on
the Rack host as it stands**. The brief requires:

- a curve and timeline editor;
- a modulation matrix and multi-destination macros;
- type-specific FX internals;
- factory sound presets;
- deterministic Generate/Mutate;
- per-layer meters and an event playhead.

The current contracts provide only:

- 128 automatable pool parameters plus at most 128 UI-only scalars per slot;
- a declarative SOUND page of knobs, selectors and toggles;
- a Rack-owned step pattern;
- sample slots.

The editor API is reserved and always NULL. Sections 2 and 3 give the
evidence. Section 4 proposes the smallest additive host/ABI extension. It
mirrors the Rack-owned pattern snapshot path that is already proven in
production. Sections 5–7 give the parameter map, the engine plan and a
dependency-ordered Draft PR plan.

**Release consequence.** Any host extension changes shared Rack code. VOX FX
therefore cannot run in the frozen Rack 1.0 binary; it needs a post-1.0 Rack
host. The module itself (FX-01…FX-03) can be developed and tested
independently before that host exists.

## 2. Verified repository facts

| # | Fact | Evidence |
|---|---|---|
| F1 | Module editor API is reserved and always NULL | `libs/instrument/abi/VoxModuleAbiV2.h:84` (`typedef struct VoxEditorApiV2 … /* reserved; always NULL */`), `:432` |
| F2 | The process context carries outputs, MIDI, parameter changes, transport, pattern snapshot, aux outputs and samples. **No audio input**, no telemetry output | `VoxModuleAbiV2.h:373-409` (`VoxProcessContextV2`) |
| F3 | Content API has pattern caps and factory patterns only; **no sound presets, no program generation** | `VoxModuleAbiV2.h:323-333` (`VoxContentApiV2`) |
| F4 | The host contract already models direct-parameter sound presets (`ContentDescriptor.parameterValues`, `applySoundPreset`), but the native adapter does not expose them | `libs/instrument/InstrumentContract.h:171-185`, `:355-370`; `libs/rack/Rack.h:57` |
| F5 | 128 pool endpoints per slot; named parameters bind to fixed pool indices | `InstrumentContract.h:42` (`poolParametersPerSlot = 128`) |
| F6 | **At most 256 descriptors per direct instrument** (host + UI-only), enforced in 3 places, with Rack arrays sized by it | `InstrumentContract.h:48` (`maxSlotParameters = 256`); `libs/rack/RackState.cpp:133`, `libs/instrument/HostParameterSchema.cpp:100`, `libs/instrument/InstrumentRegistry.cpp:58`; `libs/rack/Rack.h:214`, `:270` |
| F7 | UI-only parameters (pool −1, not automatable) are edited through `Rack::setUiParameter` (controller thread, never waits), reach the module at the next block and are saved with the slot | `libs/rack/Rack.h` (`setUiParameter` comment); `HostParameterSchema.h:19` |
| F8 | Rack slot macros (8 per slot) **cannot map to direct (native) instruments**, and direct parameters must have `preferredMacro == -1` | `libs/rack/Rack.cpp:318-324` ("direct instrument requires … no macro mapping"); `HostParameterSchema.h` `validDirectParameter` |
| F9 | The Rack modulation engine (`Capability::modulation`, `ModulationRuntime`, up to 256 routes) is **not offered to native modules**, which get `notes \| standardEditor` only | `libs/instrument/native/NativeModule.cpp:601`; `libs/rack/Rack.cpp:397-411`; `InstrumentRegistry.h:15` |
| F10 | SOUND layout schema 1 has only knob/selector/toggle. Every direct parameter must appear exactly once, in at most 16 sections. No curves, pages, XY pads, meters or text | `libs/instrument/SoundLayout.h:15-45`; `docs/architecture/SOUND_LAYOUT.md` |
| F11 | Pattern caps: ≤16 lanes, ≤64 steps/bar, ≤4 bars, ≤4096 cells. Fields include velocity, accent, note, octave, slide, tie, gate, probability, ratchet. Playback is HOST (Rack schedules lane 0) or INTERNAL (module sequences the snapshot) | `VoxModuleAbiV2.h:245-278` |
| F12 | Rack publishes immutable pattern snapshots with a generation, acquired lock-free per block. **This is the template for a program snapshot** | `libs/rack/Rack.cpp:726-797`, `:1225` |
| F13 | Sample hand-off: ≤64 sample slots, host decode worker (JUCE formats), controller-thread `setSlotSample`, generation-guarded, `VoxSampleUsageV2` reporting | `VoxModuleAbiV2.h:126`; `libs/instrument/samples/SampleDecoder.cpp`; `Rack.h` (`setSlotSample`, `instanceGeneration`) |
| F14 | Aux buses: up to 4 SEND + 7 OUTPUT per instrument | `VoxModuleAbiV2.h:113-117` |
| F15 | Host budget limits per slot | `InstrumentRegistry.h:8-16`: `maxVoicesPerSlot` 64, `maxStateBytesPerSlot` 1 MiB, `maxResourceBytesPerSlot` 64 MiB, `maxLatencySamples` 192000, **`maxTailSamples` 1 920 000**, `maxScratchBytesPerSlot` 8 MiB |
| F16 | At most 1024 descriptors per module at load. The Rack cap of F6 (256) is lower and binding | `libs/instrument/native/NativeModule.h:23` |
| F17 | Reusable DSP with no JUCE dependency (std only): `EffectsChain` (distortion, wavefolder, phaser, flanger, chorus, bitcrusher, delay, reverb) and `dspcore/Primitives.h` (`Smoothed`, `BlepOscillator`, `SvfLowpass`, `LinearAdsr`) | `libs/effects/EffectsChain.h:12-75`, `libs/effects/EffectsChain.cpp:1-4`, `libs/dspcore/Primitives.h:7-140` |
| F18 | The only shipped native module (Drums) is C. Exported ABI only; C++ inside a module is not excluded by the ABI as long as nothing crosses `voxModuleEntryV2` | `libs/drums/VoxFullOnDrums.c`; `CMakeLists.txt` (`VoxFullOnDrums` target) |
| F19 | Open realtime P1: on the reference Xeon, 96 kHz/64 misses deadlines with 8–16 Drums slots at the declared limit and at musical peak density | #109 comments 6680238172, 6706042558; `docs/testing/callback-headroom-reference-xeon.md` (#109) |

## 3. Conflicts with the brief

Each conflict gives the evidence, the risk and a recommended resolution.

| C | Brief says | Conflict (evidence) | Risk if ignored | Recommended resolution |
|---|---|---|---|---|
| C1 | Real curve/timeline editor, modulation matrix, FX internals (§4, §5) | No module editor (F1). SOUND layout cannot express curves or pages (F10). UI-only scalars are capped at 128 per slot (F6) | Fake UI, or curve data with no path to DSP | **Rack-owned Program content** (§4.1): versioned, typed, bounded. Edited by a host editor model and published to the module as an immutable snapshot, exactly like patterns (F12) |
| C2 | 128-index map with "dynamic type-specific state" in UI-only/module state (§5) | Only 128 UI-only slots remain (F6). The FX program needs far more: about 1000 scalars plus curves (§5.3) | Truncated features or a silent cap | Keep the 128 pool endpoints for automation (§5.1). Put all non-automatable program data in Program content (§4.1), not in UI-only parameters. Do **not** raise `maxSlotParameters` (rejected alternative, §4.4) |
| C3 | 8 macros in pool 120–127 (§5) | The Rack already has 8 slot macro endpoints, but they cannot target direct instruments (F8) | Two "macro" concepts in one UI; the Rack macros do nothing for FX | Use module-owned macros on pool 120–127 as the brief proposes, with multi-destination mappings in Program content. The FX editor hides the inert Rack slot-macro strip for direct instruments (to verify in FX-04). No change to Rack macros |
| C4 | Modulation matrix, LFOs, S&H, envelope follower (§4 MOTION) | The Rack modulation engine is not available to native modules (F9) | — | Implement modulation inside the module, with bases from pool 104–119 and routes in Program content. No change to the Rack engine |
| C5 | ≥48 factory programs, user bank, import/export (§6) | The native ABI cannot provide sound presets (F3, F4). The `.vins` manifest has no preset section (`docs/architecture/VINS_PACKAGES.md`) | No factory content delivery path | Additive content API: `getProgramCaps`, `getFactoryProgram(index)`. Factory programs stay inside the signed module. The user bank is a Rack-owned file store of Program + pool values, following the existing sound-preset model (F4) |
| C6 | Generate / Mutate, seeded and deterministic (§3) | No generation entry point for native modules (F3) | Generator logic ends up in the host, or does not exist | Additive content API `generateProgram(seed, category, complexity, energy, tonality, length, locks, out)`. It runs on the message thread, outside the callback, and its result goes through the normal Program edit transaction |
| C7 | Per-layer meters, event playhead, voice/grain/clip indicators "from genuine diagnostics" (§4) | No module→host telemetry in `VoxProcessContextV2` (F2). Rack diagnostics are per-slot counters only | Fake meters | Additive `VoxTelemetryV2` out-struct: a bounded set of floats/ints the module writes per block. The Rack copies it to a lock-free per-slot snapshot for the UI, the same pattern as the RT-safe slot diagnostics |
| C8 | Event length 0.05–32 s plus FX tails (§2) | Host tail cap is 1 920 000 samples (F15): 20 s at 96 kHz | The descriptor is rejected at load, or tails are cut | Option A (recommended): cap `event length + tail ≤ 1 920 000 / sampleRate`, i.e. 20 s at 96 kHz and 40 s at 48 kHz, enforced and shown in the UI. Option B: raise the host limit (host change, owner decision) |
| C9 | Process audio from other tracks (§0) | No audio input (F2) | — | Agrees with the brief: not supported. Granular and Glitch read only host-owned samples (F13) or the module's own generated audio |
| C10 | Layer "sound family" and FX "type" as automatable pool parameters (§5 table) | Not a conflict with code but with §5's own rule "never reinterpret a parameter by type". Switching family or type changes the DSP topology mid-event | Clicks, stale state, preallocation of all variants | Family is a **Program field, not automatable**; a change applies to the next event only. FX slot type stays automatable as the brief asks (88–103), with every type preallocated per slot and a 10 ms crossfade (§6.3). Owner to confirm the family deviation |
| C11 | Production completeness, Cubase/REAPER acceptance (§0, §9) | Open realtime P1 (F19); FX adds CPU load to the same callback | A FX + Drums set misses deadlines at 96 kHz/64 | Measure FX in the same bench (FX workloads in E2 style) and declare its envelope. No claim beyond measured cells |
| C12 | Develop after checking current HEAD; Rack/Drums 1.0 is not the staging area (§0, §8) | Host changes C1/C5/C6/C7 are shared Rack code | Contamination of #77/#78 | Separate base branch `fx/base` (§7). FX host changes never enter `integration/rack-1.0` without a separate owner decision |

## 4. Proposed host and ABI extension (needs Architecture Guard review)

All extensions are **additive V2.5**. They are gated by `structSize`, the same
way V2.1–V2.4 appended fields. Modules that do not opt in (Drums, Demo) are
unaffected byte for byte. No change to: 16 slots, 2048 events/block, the 128
pool endpoints, plugin FUID, the signing model, the sample hand-off or
pattern semantics.

### 4.1 Program content (Rack-owned, typed, bounded)

The module declares a **program schema**:

```c
typedef struct VoxProgramFieldV2 {   /* scalar field: like a UI-only parameter */
  uint32_t structSize, version;
  const char *id, *name, *unit, *group;
  const char *const *choices; uint32_t type, choiceCount;
  float minimum, maximum, defaultValue, step;
} VoxProgramFieldV2;

typedef struct VoxCurveCapsV2 {      /* one breakpoint lane */
  uint32_t structSize, version;
  const char *id, *name;
  uint32_t maxNodes;                 /* 2..16 */
  float valueMin, valueMax, valueDefault;
  uint32_t shapes;                   /* bitmask: linear, exp, smoothstep, hold */
} VoxCurveCapsV2;

typedef struct VoxProgramCapsV2 {
  uint32_t structSize, version;
  uint32_t schemaVersion;            /* module-defined program schema */
  uint32_t fieldCount;  const VoxProgramFieldV2 *fields;  /* <= 2048 */
  uint32_t curveCount;  const VoxCurveCapsV2 *curves;     /* <= 64 */
  uint32_t routeCapacity;            /* modulation routes, <= 64 */
  uint32_t routeSourceCount; const char *const *routeSources;
  uint32_t routeTargetCount; const char *const *routeTargets;
} VoxProgramCapsV2;
```

The program value is a **host-validated POD snapshot**:

```c
typedef struct VoxCurveNodeV2 { float time, value; uint32_t shape; uint32_t reserved; } VoxCurveNodeV2;
typedef struct VoxRouteV2 { uint16_t source, target; float amount; uint8_t flags; uint8_t reserved[7]; } VoxRouteV2;
typedef struct VoxProgramV2 {
  uint32_t structSize, version, schemaVersion, reserved0;
  const float *fields;            uint32_t fieldCount, reserved1;
  const VoxCurveNodeV2 *nodes;    const uint32_t *curveNodeCounts; uint32_t curveCount, nodeStride;
  const VoxRouteV2 *routes;       uint32_t routeCount, reserved2;
} VoxProgramV2;
```

How it works:

- **Process context.** Appended fields: `const VoxProgramV2 *program;
  uint64_t programGeneration;`. The module copies the snapshot only when the
  generation changes, inside fixed bounds and with no allocation. This is the
  same rule Drums applies to patterns in `takePattern`, `libs/drums/VoxFullOnDrums.c`.
- **Rack side.** One more snapshot channel next to patterns, using the same
  acquire/retire mechanism (F12).
- **Editing.** On the controller thread: validate → build a new immutable
  snapshot → publish. The audio thread never waits.
- **Persistence.** In the slot state as a versioned payload (`VPR1`), parsed
  and validated transactionally like `VPT2`. A malformed payload leaves the
  live slot untouched and keeps the unresolved bytes.
- **Host validation.** Finite values, field ranges, choice indices, curve
  times sorted in `[0, 1]`, node counts within caps, route indices within
  caps. The module re-validates defensively and never reads past the counts.
- **Editor model.** Host-side and generic: Undo/Redo and A/B over (pool values
  + Program), transactional. Curve and timeline widgets are driven by
  `VoxCurveCapsV2`, the matrix widget by the route caps.

### 4.2 Content API additions (message thread, never realtime)

Appended to `VoxContentApiV2`:

```c
VoxResult (VOX_CALL *getProgramCaps)(const char *id, const VoxProgramCapsV2 **out);
VoxResult (VOX_CALL *getFactoryProgram)(const char *id, uint32_t index,
          const char **name, const char **category, VoxProgramV2 *outProgram,
          float *outPoolValues /* 128 */);
VoxResult (VOX_CALL *generateProgram)(const char *id, const VoxGenerateRequestV2 *request,
          const VoxProgramV2 *current, VoxProgramV2 *out, float *outPoolValues);
```

- `VoxGenerateRequestV2` carries: seed, category, complexity, energy,
  tonality, length, lock mask and mutate amount. The function is deterministic
  for identical inputs.
- Output buffers are host-owned and sized from the caps.

### 4.3 Telemetry (module → host, realtime-safe)

Appended to the process context: `VoxTelemetryV2 *telemetry` (host-owned),
with a declared count of up to 32 floats and 8 ints. The module writes them
per block, e.g.:

- per-layer peak/RMS;
- event progress 0–1 for up to 8 events;
- active events and grains;
- limiter gain reduction and clip count.

The Rack copies the values into a per-slot lock-free snapshot that the UI
polls. This is the same pattern as the RT-safe slot diagnostics (#77 P1 item,
done earlier).

### 4.4 Rejected alternative

Rejected: raising `maxSlotParameters` to 1024, encoding curves as UI-only
scalars and adding a "curve" control kind to SOUND layout.

- **For:** fewer ABI structs.
- **Against:**
  - unstructured validation (node order, counts);
  - it grows every slot's arrays (F6) and the APVTS-adjacent state;
  - it still provides no presets, generation or telemetry, so C5–C7 remain;
  - it abuses "parameter" semantics the contract keeps for automation.

### 4.5 Rejected alternative

Rejected: filling in `VoxEditorApiV2` and letting the module draw its own UI.
It would put plugin-in-plugin UI, threading and DPI handling into every module.
It would also break the Rack's single VOX UI system and visual gates.

## 5. Parameter architecture

### 5.1 Pool endpoints (128, automatable)

These are fixed meanings for the life of `com.ultimavox.vox-fx`. The complete
list with ranges and defaults is in `docs/fx/parameter-map.csv`.

| Pool | Count | Group |
|---|---|---|
| 0–23 | 24 | Global: output gain, event length (bars and seconds), sync mode, retrigger mode, NoteOff mode, velocity sensitivity, transpose, key tracking, energy, tilt, low cut, high cut, width, drive, curve bias, release, pump depth, pump division, output protection, stereo drift, global motion, start position, tail time |
| 24–87 | 4 × 16 | Layer A–D: on, gain, pan, width, pitch, fine, attack, release, cutoff, resonance, sweep amount, filter envelope, offset, length, character (the family's primary "colour" control, with a fixed meaning per family; see C10), FX-chain send |
| 88–103 | 4 × 4 | FX slot 1–4: on, type, mix, output trim (as in the brief) |
| 104–119 | 16 | Modulation bases: LFO1 rate, sync, depth, shape; LFO2 rate, sync, depth, shape; step-envelope division, depth, smoothing; random rate, depth, smoothing; envelope-follower amount; global modulation amount |
| 120–127 | 8 | Macro 1–8 (0–1); destinations in Program content |

There are no UI-only parameters. All other state lives in Program content
(§5.2), so the 128 UI-only slots of F6 stay free.

### 5.2 Program content (not automatable, persisted, edited in the FX editor)

| Section | Content | Bound |
|---|---|---|
| Layers A–D | sound family (12 choices), mute/solo, sample slot, family-specific fields | 4 × (4 + 12) fields |
| Curves | per layer: amplitude, pitch, cutoff, pan; global: pump shape, macro response | 4 × 4 + 4 = 20 curves × ≤16 nodes |
| Timeline | length unit, snap, zoom (UI), end-at-bar policy | ~6 fields |
| FX internals | per slot, the type-specific controls (e.g. delay time, sync and feedback; reverb size, damping and pre-delay) and the slot order | 4 × ≤10 fields + order |
| Modulation | routes: source, target, amount, polarity, smoothing, quantize | ≤64 routes |
| Macros | 8 × ≤8 destinations, each with min, max and response curve | ≤64 mappings (as routes) |
| Trigger | note map (variation per key range), transposition policy, CC1/sustain mapping or "unavailable" | ~12 fields |
| Generation | seed, category, complexity, energy, tonality, length, locks, history pointer (UI) | ~10 fields |

The estimated serialized size is under 64 KiB, against a 1 MiB budget (F15).

## 6. DSP plan (module `VoxFX`, C++20 inside, C ABI at the boundary)

### 6.1 Structure

- Event scheduler with at most 8 active events × 4 layers. Allocation and
  stealing are deterministic: the oldest event is released with a 5 ms fade.
- The clock comes from transport ppq and tempo when present, otherwise from a
  fallback BPM held in Program. Durations are in bars or seconds and are
  independent of block size (same discipline as the Drums sequencer and the
  E2 host grid, #109).
- Breakpoint curve evaluator: 2–16 nodes; linear, exponential, smoothstep and
  hold; coincident nodes crossfaded.
- Shared primitives: reuse `dspcore/Primitives.h` (BLEP oscillator, SVF,
  smoothing) and `effects/EffectsChain` processors, statically linked (F17).
  New code goes into `libs/fx/` only where nothing exists yet: seeded PRNG,
  colored noise, comb/resonator bank, formant bank, grain engine, Doppler,
  output protection.

### 6.2 The 12 families

Each family is its own engine with a fixed control set and a golden render
test.

| # | Family | Plan |
|---|---|---|
| 1 | Noise Riser | Colored noise (white/pink/brown/blue) into a multimode SVF with start/end cutoff curves |
| 2 | Downlifter | Falling pitch and cutoff with a braking-time curve |
| 3 | Tonal Sweep | BLEP oscillators plus an octave-recycling Shepard bank |
| 4 | Whoosh / Doppler | Noise and tone, a pass-by delay line with pitch from distance, pan/tilt curves |
| 5 | Impact | Transient, body and sub components |
| 6 | Zap / Laser | Exponential chirp, FM/ring modulation, drive with 2× oversampling |
| 7 | Psy Blip / Squelch | Resonant chirp, comb/formant motion, ratchets |
| 8 | Metallic / Resonant | Exciter into a bounded modal/comb bank (≤16 modes) |
| 9 | Texture / Drone | Oscillator and noise beds with slow LFO, drift and fades |
| 10 | Granular Cloud | Host sample, ≤64 grains, windowed, rate-correct, reverse and scatter; seeded |
| 11 | Glitch / Stutter | Bounded internal buffer (the module's own audio or a sample), tempo-synced slices and repeats |
| 12 | Formant / Alien | Excitation through a formant bank with vowel morph A→B |

### 6.3 Insert chain and output

- **Insert chain:** 4 slots after the layer mix, in a user-set order (from
  Program).
- **Types:** the 8 existing `EffectsChain` processors plus three new ones:
  multimode filter/EQ, ring modulator and trance gate.
- **Preallocation:** every type is preallocated per slot at prepare (estimate
  ≤ 10 MiB at 96 kHz, within the resource and scratch budgets of F15).
- **Type change:** 10 ms crossfade, and the old tail fades out.
- **Output protection:** DC blocker, then a calibrated soft clipper with true
  bypass and no latency.

## 7. Draft PR plan (dependency-ordered)

All PRs are Draft and stacked. Base branch: **`fx/base`**, created from
`integration/rack-1.0` @ f9c8bc6 and later synced by merges only. None targets
`integration/rack-1.0` or `main`.

| PR | Content | Depends on | Gate |
|---|---|---|---|
| **FX-00** (this) | Audit, architecture, `parameter-map.csv` | — | Owner / Architecture Guard approval of §3–§5 |
| **FX-00b** | V2.5 header changes **only** (§4.1–4.3): structs, size asserts, docs `INSTRUMENT_MODULE_ABI.md`; ABI layout tests | FX-00 approved | Architecture Guard review of exact structs |
| **FX-01** | `VoxFX` module: entry, descriptors (128 pool), event scheduler, curve evaluator, families 1–6, output protection; ABI, golden-render and allocation tests; standalone harness (no Rack program yet, compiled-in default program) | FX-00b | Golden renders + 0 allocations |
| **FX-02** | Families 7–12 (resonator, formant, grain, glitch); sample usage reporting; aliasing report | FX-01 | Same as FX-01 |
| **FX-03** | Modulation (LFOs, step, S&H, follower, ≤64 routes), macros, 4-slot insert chain, `generateProgram` / `getFactoryProgram` in the module | FX-02 | Determinism and tail tests |
| **FX-H1** | Rack: Program content store, snapshot publication, `VPR1` persistence, transactional load, validation tests | FX-00b | No regression in the Rack suites (incl. Drums) |
| **FX-H2** | Rack: telemetry snapshot, content API plumbing (factory programs, generate), user program bank | FX-H1 | Same |
| **FX-04** | VOX UI (all 8 pages): header, overview, curve/timeline editor, matrix, process, mix meters (telemetry), presets, advanced; Undo/Redo, A/B, scaling, keyboard | FX-03, FX-H2 | Visual gate + every control → render trace |
| **FX-05** | Factory content: ≥48 programs (≥4 per family), ≥12 trigger patterns, ≥8 transitions; render fingerprints; `VoxFX.vins` packaging and trust tests | FX-04 | Content inventory and licences |
| **FX-06** | Exact-HEAD Windows CI, allocator/CPU bench (FX workloads in the E2 style), Cubase/REAPER acceptance packet on signed binaries | FX-05 | Owner GO |

Estimates are given as review slices, not dates. I do not invent completion
dates.

## 8. Coverage of the brief (nothing silently dropped)

| Brief item | Where |
|---|---|
| 4 layers, 12 families, combinations as factory templates | FX-01, FX-02, FX-05 |
| Breakpoint curves, timeline, bars/seconds, retrigger, NoteOff, Panic, CC120/123 | FX-01 (DSP), FX-04 (editor) |
| Pattern (Rack-owned), Host/Internal/Both, note output, MIDI-export parity | FX-01 (uses existing V2.1–V2.2 pattern ABI) |
| 4 insert FX, Rack sends/returns kept authoritative | FX-03; aux SEND buses only if justified (F14) |
| Motion and macros | FX-03 (DSP), FX-04 (UI) |
| Generate / Mutate / locks / history | FX-03 (module), FX-H2 (plumbing), FX-04 (UI) |
| Meters, playhead, CPU/voice indicators | FX-H2 telemetry, FX-04 |
| Presets: factory, user, import/export, A/B | FX-03, FX-H2, FX-04, FX-05 |
| Samples and granular | FX-02, using the existing hand-off (F13) |
| Signing and packaging | FX-05; production key handling unchanged (`docs/release/RELEASE_SIGNING.md`) |
| Realtime and performance evidence, DAW acceptance | All PRs (allocation probe); FX-06 |
| Audio input from other tracks | Not supported (C9); separate future contract |

## 9. Decisions requested

1. Approve the additive **V2.5 Program / Content / Telemetry** extension
   (§4), or choose an alternative.
2. Approve **family as a non-automatable Program field** (C10).
3. Choose the **tail cap**: event length plus tail limited to the host's
   1 920 000 samples (C8, option A), or raise the host limit (option B).
4. Approve the **`fx/base` branch** and that the VOX FX host work targets a
   post-1.0 Rack.
5. Confirm the identity: `com.ultimavox.vox-fx`, package `VoxFX.vins`,
   module `VoxFX`. No existing instrument uses these
   (`instruments/`, `CMakeLists.txt` checked).
