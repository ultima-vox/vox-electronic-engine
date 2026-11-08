# 04 — VOX FX: самостоятельный генератор музыкальных звуковых эффектов

**Статус:** будущий standalone-instrument VST3, проектная спецификация. **Не Rack `.vins`** и не обработчик входного аудио. Phase 1 Windows x64, C++20 DSP / JUCE 9 wrapper. Первичная референсная документация: `VOX_FX_Claude_Production_Task.md` (локальное ТЗ владельца), [PR #110 FX-00](https://github.com/ultima-vox/vox-electronic-engine/pull/110), HEAD `60929c206bd2b8d5cd8789cbcfa7fdfa17ee7059`. При новой стратегии **не реализовывать V2.5, Rack-owned Program, `fx/base` или 128-slot pool**. Из аудита #110 использовать только подтверждённые DSP, UI и sound-design требования.

## 1. Для чего нужен инструмент

ВОX FX исполняет **по MIDI-ноте или внутреннему триггеру** готовое по времени событие: riser, downlifter, impact, whoosh, zap, chirp/squelch, атмосферный дрон и т.п. Одна нота может запустить сложное четырёхслойное событие с независимыми траекториями тона, фильтра, громкости, панорамы, эффектами и хвостами. Без сетей и нейросетей. Пользователь должен получить воспроизводимый результат из выбора категории, длительности и кнопки `GENERATE`, затем редактировать каждый элемент.

**Инструмент 1.0** принимает MIDI и использует встроенные осцилляторы/шум/переданные пользователем сэмплы. **Обработка входящего аудио другой дорожки DAW** — не часть этой версии. В перспективе отдельный `VOX FX Processor.vst3` с аудиовходом, иным VST3 ID, отдельной политикой sidechain/bypass/latency, может переиспользовать эффекты, но не должен превращаться в скрытый второй продукт.

## 2. Схема звука

```text
MIDI NoteOn / Generated Trigger
    → bounded event allocator (max 8 overlapping events per instance)
    → event timeline 50 ms .. 32 s, seconds/bars, DAW transport
    ├─ Layer A: sound family 01..12 → amp/pitch/cutoff/pan curves ─┐
    ├─ Layer B: sound family 01..12 → amp/pitch/cutoff/pan curves ─┤
    ├─ Layer C: sound family 01..12 → amp/pitch/cutoff/pan curves ─┤→ stereo sum
    └─ Layer D: sound family 01..12 → amp/pitch/cutoff/pan curves ─┘   │
                                     macro + LFO + modulation matrix ──┤
                          → dry / 4-slot shared FX chain → master safe output
                                      → real diagnostic snapshots → custom UI
```

Fixed bounds: 4 layers/event, max 8 simultaneously active events, up to **64 global granular voices**, shared buffers and FX tails. Thus worst case 32 active layer instances (some families consume multiple oscillators); do not imply 64 grains per event. Allocators and DSP state prepared outside RT. Voice stealing oldest/released with configurable bounded 2–10 ms fade; provide deterministic order.

## 3. Exact synthesis inventory

| Component | Target count | Behavior |
|---|---|---|
| Layer generators | **4**, selectable family per layer | each layer has own gain/pan/width/pitch, offset/duration and curves |
| Oscillators | **up to 2 melodic bandlimited oscillators per layer** | Sine/Triangle/Saw/Pulse, detune, phase; separate FM carrier/mod where applicable; expensive models limit themselves |
| Sub/source | **1 additional sub and 1 filtered noise source** when family uses it | sub pitch/body for Impact, noise color for Riser/Whoosh; no idle compute |
| Filters | **2 per layer** | SVF LP/HP/BP/notch 12/24 dB, resonance, drive; strict stability at high cutoff |
| Curves | **4 per layer + global 4** = 20 | normalized time 0..1, 2–16 nodes per curve; shapes linear/exponential/smoothstep/hold, duplicate time policy explicit, proper anti-zipper |
| Mod sources | **2 LFO + step envelope + random S&H + triggered ADSR + event phase + velocity + follower of own signal** | free Hz or tempo-sync, seeded random, correct reset/discontinuity semantics |
| Mod matrix | **up to 64 routes**, type-aware | source→destination depth/polarity/range/smoothing; cycles prohibited, exact route processing order |
| Macros | **8 automatable** | multi-destination min/max/curve, edits stored with Program; macro never destroys user DAW automation base values |
| Granular | **64 grains total per instance** | Hann/raised cosine windows, pitch/speed, reverse, random position, stereo cloud, deterministic seed, bounded duration and buffer ownership |
| Modal/resonators | **up to 16 modes** for Metallic | clamped resonant feedback, damping and finite output |
| FX processors | **4 serial post-layer slots** | actual signal processing, true bypass/wet/dry, shared buffers; types: EQ/filter, drive, wavefold, phaser, flanger, chorus, delay, reverb, bitcrush, ring modulation, trance gate |
| Oversampling | **2× default option for nonlinear** (off by default until measured), optional 4× | precomputed coefficients; quality mode prepared off RT, host latency updated |
| Trigger mode | One-shot, Gate, Retrigger, Overlap, Ignore | NoteOff/Panic correct; tail behavior documented |
| Duration | **0.05–32 seconds or bar-quantized** | independent event+FX tail; proposed max active tail 60 s, verified host reporting and memory budget |

**Rate limit is by prepared memory/CPU, not by Rack's historical 1,920,000 samples.** Avoid huge per-event buffers: shared delays/reverb allocated once in `prepareToPlay`; granular slots use bounded decoded samples and pointer-safe tables. Make tails finite; `getTailLengthSeconds` must truthfully reflect supported maximum, and host bounce at Stop must render/terminate according to documented policy. Set cap after measured proof; 60 s is a proposed ceiling not achieved behavior.

## 4. Twelve family-specific algorithms and audible acceptance

| ID | Family | Signal recipe | Required aural distinction |
|---|---|---|---|
| 01 | **Noise Riser** | seeded white/pink/brown/blue noise, envelope + start/end log cutoff, optional tonal oscillator; stereo widening | smooth rising spectral centroid/loudness over 1–8 bars, no DC rumble, controllable transient ending |
| 02 | **Downlifter** | falling BLEP/sine pitch plus falling bandpassed noise, controlled slowing tail | clearly descending tonality and brightness, not simply a reverse sample |
| 03 | **Tonal Sweep / Shepard** | 2 clean oscillators + multi-octave Shepard-like overlap (≤8 bounded partials), gain normalization | smooth sweep without an audible hard octave discontinuity and no excess upper aliasing |
| 04 | **Whoosh / Doppler** | noise/tonal transient, high/low-pass trajectory, small bounded variable delay + pan/width movement | believable pass-by with pre/post movement and repeatable stereo position |
| 05 | **Impact / Boom** | click/exciter + 1 body sine with fast pitch fall + sub oscillator + noisy decay | sub impact supports short/long tails, attack stays present on small speakers, no DC/overload |
| 06 | **Zap / Laser** | exponential frequency chirp, optional FM/ring, drive, short impact noise, oversampling where needed | short clean or harsh chirps with clear pitch trajectory, alias check at high sweep frequencies |
| 07 | **Psy Blip / Squelch** | resonant chirp, comb/filter modulation, fast envelope, optional ratchet | rubbery/acidic chirps with controlled resonance and exact rhythmic offsets |
| 08 | **Metallic / Resonant** | impulse/noise exciter into ≤16 resonant damped modes or combs | bell/clang/space metallic textures with controlled decay and no self-excited runaway |
| 09 | **Texture / Drone** | 2 VA oscillators, colored noise, slow LFO/detune, controlled unison/stereo | slowly evolving atmosphere, frequency balance and continuous phase under automation |
| 10 | **Granular Cloud** | sample source, ≤64 global grains, bounded grain scheduler, window, rate-correct pitch/time | cloudy stutters, atmos/riser textures without clicks at grain boundaries or bad sample retirement |
| 11 | **Glitch / Stutter** | internal preallocated own-output buffer or user sample, quantized slice/hold/retrigger/gate | audible gated repeats and syncopated cuts, deterministic exact-grid behavior, no external audio input claim |
| 12 | **Formant / Alien** | oscillator/noise exciter through 2–4 moving formant/bandpass filters, optional ring/FM | vowel-like talking, morphing or alien motion; distinct spectra without prerecorded speech |

**Musical factory examples:** 1-/2-/4-/8-bar Uplifter (Psy), Deep Reverse Downlifter (Progressive), Goa Laser, Dark Squelch, Trance Snare Rise (synthesized), Stereo Doppler Whoosh, Sub Impact, Metallic Stab, Forest Texture, Granular Stutter, Space Formant Sweep. Each example needs true rendered audio and provenance, not label-only metadata.

## 5. Trigger, timing, patterns, output

- MIDI events carry absolute block offsets; re-note triggers stable event ID. Velocity scales intensity and, by design, optional event duration; note pitch transposes tonal layer and optionally changes recipe within key zone. Zero-velocity NoteOn = NoteOff; CC120/123/Stop/Seek/Panic clear ownership without clicks.
- A NoteOn launched before a block boundary remains phase continuous across blocks; loop seek can restart aligned bar-based segments only under an explicit preset setting.
- Time model: bars based on PPQ/time signature/tempo, seconds based on sample clock; BPM automation produces continuous phase for bar-based events, avoiding reinterpretation of already-played samples. Specify mid-note tempo-change behavior in test.
- Internal `FX PATTERN` may trigger events on 1/4–1/32, triplets, bars 1–4. Probability and variations are seeded; same scheduler drives actual events and MIDI export; note-output has no fictitious host source.
- Optional WAV bounce/drag for generated event: **offline non-callback render** via same DSP and complete event+tail, 24-bit PCM or float WAV, chosen SR, stereo; cancelable with progress/correct sample count. MIDI drag exports trigger notes, not a fictitious representation of the resulting waveform.
- Source sample import decode on worker, content hash; missing files audible fallback only by explicit user choice. Existing old `.vins` sample handoff approach may be studied, not imported wholesale as host dependency.

## 6. Generator design

`Generate` request: `{category, style, intensity, complexity, length (bars or seconds), energy, pitch key/tonality, stereo motion, seed, locks}`. Seed/version/content hash determines deterministic output. Generation returns full 4-layer program with selected families, parameter values, 20 curves, 64-route maximum matrix, four internal effects, macro mapping and optional trigger pattern. No audio-network inference or opaque neural model. `Mutate` requires explicit lock semantics preserving user locked fields (including patterns/samples/routing), bounded change; `Variation A/B` separate full program snapshots. Previews use same engine, no fake waveform.

**Factory deliverable target:** ≥48 truly different audible programs across all 12 categories, ≥4 per family; ≥12 musical event patterns. Categories Psy, Goa, Techno, Progressive, Ambient, Cinematic (tags not hard-coded engine variations). Validate silence check, spectral difference, clipping, duration/FX tails, reproducible seed and correct program recall.

## 7. UI Ultima VOX Music

**PERFORM:** category/style browser, selected Program, `GENERATE`, seed/lock, 8 macros, stereo output, real event progress and meters, audition.

**DESIGN:** four vertical layer cards A–D with family selection, active/solo, independent source/amplitude/pitch/filter/pan; embedded thumbnail of **actual evaluated** 4 time curves. Family-specific editor shows only applicable controls, no generic `CHARACTER` whose meaning silently changes under automation.

**TIMELINE:** horizontal duration timeline (0–1 normalized + bars/seconds), 20 envelopes max; draggable nodes, multi-select, snap, curve shapes, zoom, copy/paste, undo/redo; edited curve stored as typed Program not as flat 128 host automation points.

**MOTION:** 2 LFO, one step shape, S&H, envelope follower, typed modulation matrix source/target mapping with real evaluated depth overlays; route cycle policy clear; macro assignment drag/drop plus accessibility alternatives.

**PROCESS:** 4 actual FX stages with precise type/bypass/wet/dry/extra controls, tail view and level gain, non-RT structural switch.

**TRIGGER/PATTERN:** 4-bar trigger grid, Note/Velocity/Probability/Ratchet, host tempo indicator, MIDI output preview, correct interval/transport.

**PRESETS:** Factory/User, search, category, favorites, compare A/B, sample reference status and content import/export.

**ADVANCED:** accurate event/grain count, clip meter, CPU/memory in nonblocking snapshots, quality, MIDI diagnostics, saved state version, end-to-end profiler.

The editor is a normal JUCE `AudioProcessorEditor`, not a `VoxEditorApiV2` module extension. Structural state travels through the plugin's own controller and is immutably published; GUI is JUCE-driven and entirely custom VOX look. Implementation guide in `05_ULTIMA_VOX_UI_JUCE_SKIN.md`.

## 8. Parameters and state

No fixed 128 pool. Create an **explicit stable host automatable map** for audible base parameters, e.g. global 24 + 4 layers × 16 + 4 FX slots × 4 + modulation bases 16 + macro 8 ≈128 (legacy FX-00 map is a convenient starting point, **not** an imposed Rack contract). Keep effect-type and source-family *structural* selectors outside fast automation unless all variants preallocated and crossfade tested. Further FX internals and 20 curves/routes stored in typed/versioned Program snapshot. Program load never silently overwrites an unrelated host automation lane; document preset/automation precedence.

Prepare: oscillator tables, filters/FX memory, grain pools, sample tables; processBlock: bounded no allocations/locks/IO. Program replace: UI builds and validates immutable prepared Program off RT; safe handoff with retirement off RT. Snapshot includes sample hashes/paths, routes, curves, layers, modes, seed, render defaults; `getStateInformation` and `setStateInformation` round-trip with transaction guarantees. A compact binary chunk + schema, explicit endianness bounds and CRC if used, not raw C++ struct dump. User programs portable via manifest and licensed assets.

## 9. Verification / performance / acceptance

- Separate golden renders per family and automation path; measurement of spectral centroid vs time for riser, fundamental descent downlifter, envelope and low-frequency peak for impact, Doppler/panning trajectory, grain discontinuity, formant motion, stereo correlation. Reference should be generated internally (not ripped competitors).
- Test all 12 families × 44.1/48/96 kHz × 64/128/512 blocks and stress multi events/layers; no allocations, NaN/Inf, dropped notes, runaway output, click on retrigger/panic.
- Tempo-change, transport seek/loop/stop, repeated same pitch notes, One-Shot vs Gate, bounce and project recall with sample assets; offline render matches real-time within defined floating-point tolerance.
- CPU on current DAW Windows PC **and** previous Xeon; 4 concurrent FX events, 8 maximum, 64 grains maximum; compare with Drums+Synth in same Cubase session and headroom budget. Record actual p50/p95/p99/max, no invented universal CPU guarantees.
- Every 48 factory preset renders non-silent distinctive audio; preview and saved WAV use real DSP; all editor knobs, curves, modules and meters have verified data path.
- Windows exact-head CI, VST3 validator, pluginval where feasible, Cubase 12–15/REAPER host acceptance, silent install/uninstall, repeatable SHA/binary identity.

## 10. Implementation plan

`FX-00` use PR #110 audit as **read-only input only**, rebase specification to standalone. `FX-01` independent JUCE VST3 shell/IDs/parameters/state and playable first Noise Riser. `FX-02` 4-layer voice engine and timeline with 4 curves/layer. `FX-03` remaining 11 family algorithms. `FX-04` modulation/macro matrix and four true FX slots. `FX-05` sample/granular state, program generator, presets and WAV/MIDI export. `FX-06` completed custom GUI, telemetry and diagnostics. `FX-07` exact-head Windows tests, musical audio review, acceptance/installer and release manifest.

Do **not** merge `#110` (V2.5 Rack extension proposal) as a foundation for these independent VST3s. The old docs' `fx/base` is preserved for history; no need for it in the new project.
