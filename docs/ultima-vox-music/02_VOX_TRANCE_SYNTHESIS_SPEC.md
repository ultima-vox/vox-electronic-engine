# 02 — VOX Trance Synthesis: инструментальная и DSP-спецификация

**Статус:** проект v1.0, не подтверждение реализации. **Платформа первой поставки:** Windows x64 VST3; C++20 DSP без JUCE, JUCE 9 для VST3/UI. **Музыкальная задача:** один полноценный синтезатор для Psytrance / Full-on / Goa / Progressive / Techno / Trance с проверяемыми характерными тембрами, встроенным генератором партий и обычным DAW MIDI.

## 1. Продукт и принцип исполнения

Один **VOX Trance Synthesis.vst3** (instrument), один уникальный VST3 class ID и manufacturer code, которые фиксируются перед первой публичной сборкой. **Не Rack, не оболочка с загружаемыми VST, не набор несовместимых инструментов.** В интерфейсе звуковые страницы `BASS`, `ACID`, `SYNTH`, `SEQUENCE`, `MOD`, `FX`, `PRESETS`, `ADVANCED`; это редакторы одного движка, а не отдельные VST.

Две независимые синтезаторные части A/B с зоны/velocity/слои, общая секция FX. У каждой части выбирается один из **трёх адаптеров движка**: `VA/HYBRID`, `PSY BASS`, `ACID 303`. Алгоритмы переключаются не в середине голоса: только подготовленная конфигурация/следующая нота либо безопасный crossfade. Допустимы комбинации Bass+Lead, Acid+Pad, 2×Lead; отдельная DAW-инстанция предпочтительнее избыточного мультитембрального режима. Phase 1 имеет один MIDI-приёмник, без внутреннего плагин-хостинга.

**Типы звуков:** тёплый и плотный offbeat psybass, короткий rolling bass, acid резонансные фразы со слайдами и акцентом, supersaw lead, сдержанный Goa lead, FM/hollow lead, plucks, bells/electric keys, dub/trance stabs, широкие pads, evolving atmos, hypnotic arps. Название пресета не заменяет измеримое звучание.

## 2. Сигнальная схема (план)

```text
MIDI + Host transport ─> note owner/voice allocator ─> Part A ─┐
                                   │          │ oscillators    │
                                   │          │ mixer/noise    │
                                   │          │ filter 1/2     ├─> part mixer/pan ─> EQ/drive ─> chorus/phaser ─> delay ─> reverb ─> master
                                   └─────────> Part B ────────┘                           └─> optional 2 FX sends
Global clock/seed -> pattern generator / arp -> SAME MIDI scheduler -> voices
Host automation/base ────────────────> smoothing + modulation ─> DSP (never overwrites host base)
```

No program allocation/loader/sample decode in the callback. Shared reverb and delays are per-instance, not per voice. Aux output and DAW external sidechain are out of first scope.

## 3. Целевой DSP-пакет

| Узел | Количество / режим | Конкретное требование |
|---|---|---|
| Части | **2 A/B** | Mode `VA/HYBRID`, `PSY BASS`, `ACID 303`; independent level, key range, transpose; engine-specific panel |
| VA oscillators | **3/voice/part** | OSC1/OSC2: bandlimited saw/pulse/triangle/sine/wavetable; OSC3: sine/triangle/saw/FM source. Phase/reset/detune/pan/gain; hard sync/ring/FM only when tested |
| Additional sources | **1 sub + 1 noise per voice** | sub sine/square −1/−2 octaves; filtered white/pink noise; high-pass noise in snare-like transient patches |
| Wavetable | **64–256 frames/table (target content)** | interpolation/crossfades, anti-alias via mipmaps; content prepared off audio; fixed per-preset memory budget documented after prototype |
| Unison | **1–4 voices per oscillator** initially | Per-osc phase/width/detune, stereo spread; explicitly count CPU multiplied by active voices. Increase to 8 only with measurements |
| Polyphony | **16 combined notes default, configurable up to 32** | voice budget counts musical voices; note stack; deterministic stealing with short fade; bass/acid modes force mono per active part |
| Filters | **2 per VA voice** | serial / parallel; LP12/24, HP12/24, BP12, notch; resonance, drive, keytracking. Ladder/acid filter separate carefully tested algorithm |
| Filter parameter smoothing | continuous | exponential Hz mapping, resonance bounded, no unstable coefficient jumps, no NaN/Inf near Nyquist |
| Envelopes | **4 per part** | AMP ADSR, FILTER ADSR, MOD1/2 ADSR; curve/slope control, retrigger/free modes, gate/release tested |
| LFO | **4 per part** | sine, triangle, saw up/down, square, sample-and-hold, smooth random; tempo-sync/free Hz; retrigger/global phase |
| Mod matrix | **32 routes per part + 8 global macros** | destination typed/range bounded; per route bipolar amount, curve/smoothing; no modulation→host base writes; cyclic modulation rejected or explicitly ordered |
| Arpeggiator | **1 globally + optional per-part enable** | Up/down/up-down/random/order/chord; 1/1–1/64, triplets, swing, accent, octave, gate, ratchet; release/Stop correct |
| Sequencer | **2 lanes × up to 64 steps × up to 4 bars** | per note pitch, gate, tie, slide (when supported), velocity, probability, ratchet, microshift; stored snapshots |
| FX | **4 serial post-mix slots plus 2 optional sends** | distortion/waveshaper, chorus/flanger/phaser, tempo delay, reverb; true wet/dry and tail policy; if four-slot budget not met, state explicit not fake |
| Oversampling | selectively 2×/4× | nonlinear stages only; quality switch prepared off-RT, correct reported latency; not globally always-on |
| Output | stereo | DC blocker, optional true bypass protection/limiter, controllable trim; no mandatory hard limiter that hides defects |

**Acid adapter:** 1 principal oscillator saw or square + optional sub (not a blanket 3-osc path), monophonic note priority, legato/glide, accent envelope, decay, resonance and filter sweep, 303-like 18 dB nonlinear resonant response as an engineering target, not a claim of exact TB-303 component modeling. Track NoteOn and NoteOff precisely; stop/seek/panic cannot leave a held voice. Slides/ties share a single event scheduler, not a second speculative playback simulation.

**Psy Bass adapter:** transfer/revalidate tested `libs/bass/FullOnBass.*` and `PsyBassVoice.*` from #52/#70. Goal: phase-coherent note-on attack, transient/click+body/sub balance, tune/pitch decay, envelope, saturation, discrete short note events, correct NoteOff/release; avoid permanent beep caused by gates not terminating. Original module render should pass snapshot parity before any timbral redesign.

**VA Synth adapter:** not a feature list without DSP. Require demonstrable saw lead at high notes without gross aliasing, stable resonant sweeps under automation, pluck with fast transient and decay, keys with velocity, evolving pad with controlled stereo correlation. Waveform code reuses and extends `libs/dspcore/Primitives.h` after audited extraction.

## 4. Musical generation and deterministic playback

**Sound Generate** uses seeded structured profiles, not AI: style (Full-on, Goa, Progressive, Classic Trance, Techno), role (Bass, Acid, Lead, Pluck, Pad, Keys, Atmos), energy, brightness, complexity, density, seed, locked parameters. It generates a complete *audible* patch, including oscillator model/mix, filter, envelopes, mod matrix, FX and output compensation. `Mutate` changes unlocked parameters only; `A/B` retains both snapshots. Reject invalid parameter ranges. Fixed seed + engine version + factory content hash ⇒ reproducible result.

**Pattern Generate** uses same scheduling path for preview and MIDI export: style, scale/key/mode, root, phrase length, chord progression optional, density, octave, rests, octave jumps, swing, tie/slide/legato, accent, ratchet, call-and-response, controlled pitch contours. Styles must have rules: Psybass offbeat 1/16 or 1/8 with clear kick space; Acid patterns combine accent/slide/rest; leads use motifs/variation rather than uniformly random notes; pads use long chord tones with voice-leading. Notes outside declared pitch, velocity/gate and maximum events are rejected. Export SMF type 0/1 as explicitly selected; channel, NoteOn/Off and tick positions match **the same scheduling source** used by internal audition. Document deterministic probability semantics: one seeded result per render pass / pattern generation, not free random every block.

**Patch factory acceptance (minimum target):** 48 complete curated presets: Bass ≥12, Acid ≥8, Lead ≥10, Pluck/Keys ≥8, Pad/Atmos ≥10; at least 20 generated/hand-reviewed MIDI phrases covering styles. These are target deliverables, not existing facts. Every factory preset has an audio render, an explicit gain check and a metadata/content license record.

## 5. MIDI semantics

- One-note and overlapping-note ownership, channel 1–16 or Omni with well-documented filtering; MIDI note number, pitch bend, velocity, CC1, CC64, CC120, CC123, Program Change only if specified; no reinterpretation of CC as transport.
- Zero-velocity NoteOn is NoteOff; repeated same pitch with overlapping note IDs uses deterministic stacks; retrigger doesn't orphan old releases.
- Legato and slides render on intended samples; no audio gaps/continuous beep. NoteOff is honored for Gate; One-shot controls explicitly differ.
- DAW Stop / seek / loop policies configurable but safe: stop all current sequencers, clear stuck ownership and tails according to documented Panic vs graceful stop semantics.
- All outgoing MIDI (if supported) uses host capabilities; export-to-file always via controller thread, not `processBlock`.

## 6. Parameter / patch / preset contracts

**Automatable host parameters** target ~130–190 for complete product, to be finalized in one canonical `Parameters.cpp` with stable IDs, min/max/unit/default/normalization, no fixed Rack 128 cap. Do not expose per-step data, wavetable arrays, modulation routes as hundreds of poorly named flat DAW parameters. Store structural patch data in typed/versioned state; preserve compatibility through field IDs + explicit schema migrations. All smoothable host controls exposed through APVTS/parameter attachments; host base changes never silently rewrite other bases. Filter type or engine mode change requires non-RT prepare and safe swap, not dynamic callback allocations.

`getStateInformation` snapshot: schema, product identity, engine state A/B, pattern data, macro routes, FX state, sample/wavetable reference manifests, generator seed and normalized base values. `setStateInformation`: parse/validate/prepare/commit off audio; malformed/newer state retained or rejected with diagnostic; no partial restore. Old Rack projects **not** silently compatible.

Factory bank separate from user bank (`%APPDATA%/Ultima VOX Music/VOX Trance Synthesis/` is a proposed path to confirm at implementation, not existing path). Pack hash/license, version and missing-file states. User program import/export off audio; never load from filesystem in callback.

## 7. UI information architecture (without creating drawings)

- **Performance:** patch name/category, A/B blend, 8 macros, keyboard, live voice/meter and meaningful synth mode, seed/dice/lock controls.
- **BASS:** kick-gap aligned bass gate, oscillator/waveform, amp and pitch curves, phase reset, sub/click, note length/ADSR, velocity and saturation.
- **ACID:** waveform, cutoff/resonance/drive, filter envelope, accent/slide graph, mono/legato, phrase editor. No generic UI substituted for real accent semantics.
- **SYNTH:** A/B sources, 3 osc + sub/noise, mixer, two filters, ADSR and mod depth. Each editor section appears based on active engine capabilities.
- **SEQUENCE:** piano-roll with true MIDI data, 4-bar transport, probability/velocity/accent/tie; drag MIDI with export-parity test.
- **MOD:** 4 LFOs/part, routing table with source/destination/audio range; modulation overlays on knobs separate from host base.
- **FX:** four actual effect slots, chain routing, tail indicator, send/return gains where implemented.
- **PRESETS:** search/category/favorites/User/Factory, compare A/B, save, import/export, reliable undo/redo.
- **ADVANCED:** voice count, CPU/memory, oversample quality, MIDI diagnostics, tuning, plugin/version/hash, realtime status.

Use shared `vox::ui` LookAndFeel and actual widgets; theme specification in `05_ULTIMA_VOX_UI_JUCE_SKIN.md`. No fake waveforms, static meters, dummy buttons or disconnected controls.

## 8. Release acceptance / measurable tests

1. **DSP golden render:** per engine, per family, every osc/filter mode and key edge, 44.1/48/96 kHz and 32/64/128/512 blocks; compare frequency/cycle metrics and acceptable numerical drift, not raw bit-identical across CPU/compilers.
2. **Sound quality:** transient sample alignment, pitch stability in cents measured, filter stability with 100% resonance, spectral alias checks at high fundamental frequencies, no unexplained DC, no clipped default patches; subjective blind A/B for Psy bass and Acid against approved internal references (not copyrighted reproduction).
3. **Host:** Cubase 12–15 / REAPER test NOTE ON/OFF, sustain, CC120/123, transport Seek/Loop, bouncing, preset automation, offline vs realtime match; reopened projects restore exact patch and pattern. Includes baseline VST3 Validator and pluginval where applicable.
4. **Realtime:** zero malloc/new/delete/lock/IO/log in callback by instrumentation; no event drop under documented bounded capacity; CPU on selected owner DAW CPU, p50/p95/p99/p99.9/max, calibrated 96k/64 and other scenarios; multinstance test with Drums+FX.
5. **Preset completeness:** 48 different audible factory patches; 20 usable phrases; A/B comparison, stable seed, locked parameters unchanged by Generate/Mutate.
6. **UI:** all visible controls have data→DSP path, DAW automation tests, logical 1180×760 /1500×920 and Windows 100/125/150/200% DPI, focus/keyboard and mouse gesture; owner visual signoff.

## 9. Dependency-ordered implementation PRs

`SYN-00` audited extraction and golden reference for Bass/Acid; `SYN-01` independent VST3 shell + IDs/state/automation + instrument smoke; `SYN-02` Bass DSP; `SYN-03` Acid DSP; `SYN-04` VA hybrid and filters; `SYN-05` modulation/macros/FX; `SYN-06` musical generator, sequencer and MIDI export; `SYN-07` complete VOX UI and factory bank; `SYN-08` Windows acceptance/security/perf/install. Each PR separately reviewed, no cross-merge into retired Rack.

**Owner decisions:** final plugin identity; minimum factory content quality; whether phase-1 A/B can mix two Acid modes (default: yes, bounded); oversampling modes after CPU measurements. Counts are proposed specification, not proven CPU budget.
