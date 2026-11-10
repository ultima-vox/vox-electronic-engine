# 03 — VOX Drums: самостоятельная драм-машина

**Статус:** целевая спецификация самостоятельного VST3, не характеристика текущего `.vins`. **Цель:** один полноценный жанрово-нейтральный инструмент с профессиональным синтезом/сэмплированием ударных и быстрым генератором грувов. Первый формат — Windows x64 VST3, C++20/JUCE 9.

## 1. Продуктовая концепция

**VOX Drums** запускается как отдельный инструмент, без Rack, без отдельного «хоста пэдов», без вложенной загрузки VST. Зафиксировать один уникальный plugin ID/FUID (не старый `com.ultimavox.fullon-drums`) при создании процессора. Стартовая цель — **16 полностью рабочих пэдов**, совместимых с GM-ориентированной раскладкой по умолчанию, но с переназначаемыми MIDI-нотами. Пользователь может создать любой kit: psy/full-on, Goa, house, techno, trance, hip-hop/trap, breakbeat, cinematic. Жанр — набор алгоритмов, пресетов/паттернов и звуков, **не разная программная архитектура**.

Сохранить и перепроверить **восемь существующих пэдов** из `libs/drums/VoxFullOnDrums.c` (PR #72/#94/#95–#104) как опорную sound-parity базу. Текущие 8 пэдов/16 голосов — факт кодовой базы, новые 16/64 — **требование будущего инструмента**. Приоритет: 8 пэдов без потерь → 16 законченных пэдов → генератор и редактор → acceptance. Не объявлять полные 16 до тестов всех 16.

## 2. DSP сигнал и слои

```text
MIDI / internal step sequencer
   → Note/Choke/Voice ownership → Pad[1..16]
         Pad synth voice (selected model, 2 tonal generators + noise/transient)
         Sample A/B (velocity switching or layer, tuning and bounded interpolation)
         Per-pad chain: envelope → transient → multimode filter → color/saturation → pan
       ├─ MAIN / individual stereo out assignment (predefined Buses)
       ├─ Send 1 (delay)
       └─ Send 2 (reverb)
  → per-pad mixer / mute/solo → stereo MAIN + up to 8 stereo outs
             → shared returns 1/2 → master output and metering
```

Every pad can play **Synth**, **Sample** or **Layer** (Synth+sample A/B), selected per-pad without loading DSP structures in process. Separate FX send values are not duplicated by internal direct routing; establish a single tap-point map and tests for every output. FX tail policy separate from voices; muted input does not necessarily kill already-running shared tails.

## 3. Сколько генераторов и фильтров

| Узел | Целевые параметры | Технический смысл |
|---|---|---|
| Pads | **16**, all real | Note map 0..127, label, color, choke group; full automation of assigned pad levels/essentials |
| Pad synth engine | **8 selectable algorithms/pad** | Kick, Snare, Clap, Hat/Metal, Tom, Rim/Click, Cymbal, General Perc; algorithms available on any pad |
| Oscillators | **до 2 tonal oscillators/pad voice** | Sine / triangle / BLEP saw / metallic partial oscillator as model-appropriate; do not run unused oscillators |
| Noise/excitation | **1 noise source + 1 attack/transient generator/voice** | White/pink/filtered, click/burst; timbre from energy and envelopes |
| Kick model | body sine + separate sub and click | fast pitch envelope (linear/log/exponential), sub amount, click HP/LP, soft clipping, attack/release; precise phase reset |
| Snare | 2 resonant pitched components + noise | spectral body/tuning, snappy, noise decay, transient timing |
| Hats/cymbal | 4–6 inharmonic partials + noise | metallic excitation + HP and short/long metallic decay; enforce alias-safe frequencies |
| Claps | noise + **3–4 microbursts** | burst intervals seed-stable; energy, tone, width |
| Toms | 1–2 osc with pitch envelope | tune/decay/filter saturation |
| Perc | noise + tonal resonator | unusual blips, zaps and textured percussion with bounded FM |
| Sampler | **2 sample players per pad/voice** | stereo/mono WAV, sample start/end, reverse (prepared indexed), loop (optional bounded), pitch resample, sample gain, velocity layering, zero crossfade |
| Per-pad filter | **1 multimode state-variable filter** | LP12/24, HP12/24, BP, off, resonance, tracking; topology select at safe boundary |
| Per-pad envelopes | **3** | AMP, pitch and filter; transient shaper after source, always audible modulation path |
| Humanization | deterministic per-hit | velocity, tiny bounded offset, tune drift and sample alternation, correlated with seed |
| Voice budget | **64 active total max, 8 active per pad** as design target | deterministic steal, voice fade, note lifecycle and choke across pads; benchmark at max |
| FX | **2 shared returns, one post/master processor** | Delay and reverb as send destinations; optional short saturator/compressor; no per-pad reverb buffers |
| Outputs | Main stereo + **8 stereo discrete outputs** | host activation negotiation, unconnected buses safe; no untested unlimited buses |
| Sample memory | budget target configurable at prepare/load | decide max decoded MiB in D-00 after measurement (do not claim 64 MiB arbitrarily); user files loaded off RT, atomically published |

**Synth sound goal by role:** Kick — tight 50–120 Hz fundamental, controlled high click; Psy kick strong fast transient, no unnecessary ringing; house kick deeper and softer; techno kick distortion/tail; snare 180–300 Hz body with broadband snap; hats high-frequency sparkling without overwhelming aliasing; trap hats tiny ratchets; claps layered pulses; toms clean pitch fall; impact perc controlled sub. Frequencies are illustrative timbral targets, not rigid limits or measurements of existing code. Tune control must work continuously throughout supported range without unstable filter state.

## 4. Секвенсор / генератор паттернов

Target one timing engine for playback, MIDI preview and export. **Pattern mode: 16 lanes × 16/32/64 steps per bar × up to 4 bars** in v1; lane-specific subdivisions and time signature require explicit normalization and test cases. Each step has velocity, accent, probability, ratchet 1–8, microtiming offset, per-step pitch/sample variation where supported, and conditional rules only if verified. Swing is a timing transform in shared scheduler; never regenerate audio independently of MIDI export.

**Generative dimensions:**
- Style: Full-on Psy (four-on-the-floor kick, offbeat bass companion, 16th hats), Goa (rolling and layered percussion), House (classic 4/4 with syncopated open hats), Techno (industrial hats/rim/percussion), Trap (half-time kicks/snares and hi-hat roll subdivisions), Breaks (syncopated kick/snare with swing), Ambient (sparse textures).
- Density 0..1, groove 0..1, humanize amount, syncopation, velocity dynamics, complexity, fill frequency, swing, variation between bars, seed. All knobs have bounded, audible behavior.
- **Generate entire pattern**, **Mutate selected lane**, **Fill last beat/half bar**, **Variation A/B**, **Regenerate locked-safe**, **Euclidean gate** for suitable lanes (k-in-n pattern with rotation), **probability layer** but deterministic under chosen regeneration pass.
- Kick remains intentional when user locks kick lane; snare backbeat rules are genre-specific, not universal. Regeneration must not delete locked cells and must not silently exceed 2048 events or voice bounds; new standalone event budgets are documented after benchmark rather than inheriting Rack internals.
- MIDI export: standard SMF with NoteOn+Off, accurate channels/notes/ticks/gate/velocity/ratchets/microtiming; what is exported equals the canonical triggered event list under same seed/tempo. In Both/Host modes do not double-trigger existing notes; user may explicitly choose Host-only or Internal-only when ambiguity exists.
- **DRM-05 Song Mode (owner-approved, [#123](https://github.com/ultima-vox/vox-electronic-engine/issues/123)):** compose **16/32/64 independently editable bars** as one timeline of pattern placements and unique per-occurrence edits; export **the entire arrangement as one editable MIDI file** either by clicking an Export button or dragging MIDI into Cubase/REAPER. This is additive to DRM-05 groove generation and curated content, not a replacement. See [DRM-05_SONG_MODE.md](drums/DRM-05_SONG_MODE.md) for the state, realtime safety, parity and host-acceptance contract.
- Audio drag/export optional after core acceptance: offline bounce as WAV generated by DSP with same seed/patch and tail; rendered from non-realtime job thread, cancellation supported.

## 5. Sample editor and ownership

Support user WAV PCM/float mono/stereo at common sample rates (44.1/48/96 at minimum), robust limits, predictable failure on corrupt files. Sample decode/resample/index/waveform build **off audio**. The GUI may draw waveform from a prepared peak pyramid, not from the audio thread. Controls: start/end, gain, tuning, fine, reverse, fade-in/out, normalize (prepared change), loop (only if reliable), sample A/B velocity split/mix, choke group. Sample reference stores content hash, optional relative path/factory id, missing-asset policy and an explicit Copy into Kit action for portability. Report missing user assets at load; never silently substitute another file.

**Ownership:** Sample pointers remain valid across active voices and program swaps. Use a generation-guarded immutable sample table and deferred retirement off RT (revisit `SampleExchange.*`). No `shared_ptr` destructor freeing audio memory on callback. Test replacing samples while 64 voices, 8 outs, FX tails and transport loops are active.

**Factory content:** target 3 complete and different licensed kits/pattern packs at minimum (Full-on/Psy, House/Techno, Hip-hop/Breaks) plus ≥5 broader styles; target ≥16 pattern presets per core style. Every factory sample must have provenance, author, license, redistribution permission, source hash; if this is absent, render own synthesized sample/audio or omit sample. Do not bundle Freesound audio blindly: inspect each exact file license and attribution/redistribution conditions.

## 6. Параметры и пресеты

Automated parameters: pad level/tune/decay/pan/cutoff/send1/send2, mute where safe (≈8 ×16=128), plus global transport/performance and master/mix parameters (target total **160–240**, final map at D-00). UI-only structural data: sample path, pad algorithm, full note map, routing topology, patterns, sample slice markers, kit categories, waveform cache; store versioned state rather than hundreds of APVTS endpoints. Stable `ParameterID` and type/range across releases; nonlinear curves normalized consistently. User presets: kit, pad-only, pattern-only, all-instrument; independent save/restore and explicit override choices. No hidden collision with old Rack slot parameter IDs.

Host project recall stores all programmed pad values, samples' logical identity, MIDI notes, scenes/patterns, generator seed, FX tails, user path metadata and multibus topology. Reopening works offline with no network. Missing sample must report a deterministic diagnostic and preserve the user’s assignment.

## 7. UI Ultima VOX Music

**Performance:** 4×4 pressure/velocity-sensitive pad matrix, 8 macros, transport indicator, kit browser, live output meters, selected pad indication. No gesture triggers twice.

**SOUND:** per-pad Synth/Sample/Layer, model selector (all 8 models usable), oscillator+noise mixer, filter, amp/pitch/filter envelopes, click/sub, layer velocity, audition/play; editable sample waveform.

**PATTERN:** 16-lane step grid with 16/32/64 steps, bar selector, velocity/probability/ratchet/microtiming editor, swing, duplication, A/B, piano roll if needed, safe undo/redo.

**SONG (DRM-05):** switchable 16/32/64-bar arrangement timeline; edit an individual bar without changing other uses of its pattern; navigate, duplicate, clear and reorder clips; click-save and drag-export the **full composition**, not just the selected four-bar pattern.

**MIXER:** 16 pad strips with actual level/pan/mute/solo, sends, Main/Out 2–9, return tails and peak meter (polled from DSP). All buttons connected; no visual fake VU meters.

**GENERATE:** style, density, groove, syncopation, fills, humanize, seed, locks; audition + MIDI export.

**ROUTING:** MIDI note/channel map, outputs, choke groups, velocity zones; host bus activation help and diagnostic when DAW doesn't enable outs.

**PRESETS:** Factory/User, kits/pads/pattern categories, compare A/B, no destructive load without undo, content hashes.

**ADVANCED:** sample loading status/missing files, voice statistics, oversampling quality, diagnostics, versions and real CI build metadata.

Default tokens from `libs/vox-ui/Tokens.h`; custom skin implementation in `05_ULTIMA_VOX_UI_JUCE_SKIN.md`.

## 8. Validation / release gate

- Re-render parity of current eight pads at exact reference SHA before improvements. Nine sample-rate/frame combinations; A/B sonic regression with declared tolerances.
- All **16** pads tested via MIDI: each pad sounds, unique routing, note map, multiple voices, choke and velocity, Sample/Synth/Layer, mute/solo, alternate outputs, all send returns.
- Distinct audited reference audio from at least three style families; preset names alone do not count.
- Deterministic generated groove and real MIDI-export parity across stop/loop/seek/reopen, nested probability and ratchet cases; no double hits in Host/Internal/Both.
- Cubase/REAPER: Main and 8 stereo outs, offline bounce same binary, bus activation, project save/reload, automation, sample asset pack/unpack, installer/uninstaller.
- Realtime: 0 audio-thread allocation/lock/IO/decode; bounded voices and sample retirement; NaN/Inf/denorm checks. Do controlled callback benchmarks at 44.1/48/96 kHz and 64/128/512, typical/peak/dense stress, multi-instance and DAW host.
- A green CI does not prove realtime budget. Old Rack Xeon results in #109 remain relevant as warning, **not** a measurement of this standalone engine.
- Visual Gate: 1180×760 / 1500×920 + Windows 100/125/150/200% scaling, readable all 16 pads, no dummy UI; each visible control covered by a binding or functional integration test.

## 9. PR order

`DRM-00` capture old 8-pad golden renders and map ownership; `DRM-01` independent JUCE VST3 shell/state; `DRM-02` port/core integration of 8 pads; `DRM-03` new 16-pad general algorithm/model plus sample/layer; `DRM-04` sequencer & MIDI export; `DRM-05` generator, curated content **and 16/32/64-bar Song Mode with one-file MIDI click/drag export** ([contract](drums/DRM-05_SONG_MODE.md), [#123](https://github.com/ultima-vox/vox-electronic-engine/issues/123)); `DRM-06` mixer, sends, 8 outs, voice/tail; `DRM-07` complete custom UI; `DRM-08` Windows exact-SHA CI, musical sound review, host installation/acceptance and release package.

**Open design decisions:** final drum note map/16-pad labels, first bundle's legally distributable samples, decoded memory budget, pad FX CPU limits, multibus policy for hosts supporting fewer enabled outputs. None should be silently chosen at coding time.
