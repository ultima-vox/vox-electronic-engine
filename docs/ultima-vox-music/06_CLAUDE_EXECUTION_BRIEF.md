# CLAUDE CODE MASTER TASK — ULTIMA VOX MUSIC: three production standalone instruments

**Date:** 2026-11-08. **Repository:** `ultima-vox/vox-electronic-engine`. **Status:** development specification, NOT an instruction to merge existing PRs or release anything. **Owner pivot:** retire the single Kontakt-style Rack runtime, reuse audited legacy assets only, and create three independent Windows x64 VST3 plugins with a shared library and consistent ULTIMA VOX MUSIC UI.

## 0. Mandatory reading / source of truth

Read this file and every linked document before any implementation:

1. `00_README_AND_OWNER_DECISIONS.md` — product architecture and deferred format scope.
2. `01_PLATFORM_ARCHITECTURE.md` — build, audio runtime, parameter semantics, state, threading, CI, licensing.
3. `02_VOX_TRANCE_SYNTHESIS_SPEC.md` — bass/acid/synth DSP, generators, osc/filter/envelopes.
4. `03_VOX_DRUMS_SPEC.md` — full 16-pad Drum Engine, sampler and pattern generator.
5. `04_VOX_FX_SPEC.md` — 12-family 4-layer FX sound design generator.
6. `05_ULTIMA_VOX_UI_JUCE_SKIN.md` — custom JUCE skin without losing DAW parameter/gesture behavior.
7. `07_REQUIREMENTS_TRACEABILITY.csv` — acceptance map for every delivered feature.
8. `08_LEGACY_RACK_ARCHIVE_AND_REUSE.md` — safe Git archival plan and code-reuse inventory.

Relevant GitHub evidence: old `integration/rack-1.0` observed `f9c8bc642567ba0edc6b638b64c72f9dc6ccf2e1`, PR #52/#70 Bass, #59/#69/#92 Acid, #72/#94 Drums, #78 Rack, #79 `.vins`, #80 pool, #109 performance, #110 FX-00 documentation. **Re-check remote SHA before relying on any of these.** Historical descriptions may not match code or current CI.

## 1. Change of architecture — binding direction

Products:
- **VOX Trance Synthesis** — one VST3 instrument, Bass + Acid + Hybrid VA/Synth engines, two parts, generation of sound patches and MIDI patterns, presets, custom editor.
- **VOX Drums** — one VST3 instrument, complete 16-pad Synth/Sample/Layer machine with 8 sound algorithms/pad, real sequencing and drum-pattern generation, mixer, multi-out and custom editor.
- **VOX FX** — one VST3 instrument generating layered risers, downlifters, impacts, whooshes, zaps, psy squelches, granular/metallic/texture and glitch from MIDI and patterns, **not an incoming audio insert**.

**Do not create a common Rack host.** No nested VST hosting, no `.vins` runtime, no 16-slot graph, no cross-plugin dll dependency, and no V2.5 extension from #110. Each plugin is a self-contained JUCE `AudioProcessor` / `AudioProcessorEditor`; each has a unique immutable VST3 identity/parameter map/state schema. Shared C++20 code is statically linked as `vox_dsp`, `vox_audio_runtime`, `vox_ui`, `vox_preset`, `vox_testkit` where worthwhile; shared architecture is **build-time reuse**, not host infrastructure. Start from the existing CMake/JUCE project and audit dependencies; do not rewrite JUCE from scratch.

Retain DSP algorithms and tests only after source SHA/license verification and before/after golden render. Do not merge #78, #109 or #110 into new `main` or target the current Rack staging; branch implementation proposals on an approved isolated standalone-suite branch. **Do not delete Rack base branches, close PRs, or move tags without first preparing and independently reviewing the archival procedure**. The owner's intent is to preserve old work as a legacy archive; no destructive operation is needed.

## 2. Architecture gates (P0/P1)

### RT and lifecycle, P0
- `processBlock` and anything called from it: no heap alloc/free, no locks/spin waiting, no disk/network/ZIP/WAV decode/logging, no dynamic module loads/host UI calls. Bound worst-case iterations and event queues; instrument allocation probes on the **actual Windows audio thread** and check blind spots.
- Prepare all oscillators/tables/oversampling/delays/FX/sample buffers/voices at prepare or controller handoff. Snapshot publish/retire must not invoke destructors/free on audio thread. Validate class/device/transport state before process; safe Panic, CC120/123, NoteOff, same-note re-trigger, host Stop/seek/loop; no stuck voices.
- For changing sample, structural parameters, sound models or engine types: prepare on non-RT, apply safe snapshot on deterministic boundary with crossfade/reset policy. No unsafe pointer reuse or UI passing mutable buffers into DSP.
- Handle 44.1/48/96 kHz and 32/64/128/512 frames; mono/stereo semantics documented, variable block sizes including 0 if host sends it. Preserve denormal/NaN/Inf protections and gain headroom.

### Host identity, state, automation, P0
- Three **different** VST3 FUIDs/bundle names; **never** reuse Rack IDs `com.ultimavox.vstengine` and `slotNN.pKKK`. Pin actual generated IDs at foundation stage in a documented file, not this prose.
- Every exposed audio parameter has an immutable ID, semantics, range, choice ordering and unit. JUCE APVTS for base automation; modulation is effective value in DSP and does not rewrite DAW lanes. Audio-rate automation claims require an actual tested host transport path; do not promise it by default.
- Versioned `getStateInformation` / `setStateInformation` restore engine patches, typed curves, patterns, samples references, generator seed, FX/tails and UI state separately; malformed/unrecognized state never leaves an accidentally half-loaded plugin. No silent conversion of old Rack projects.
- Real user waveform editors / MIDI drag/preview and timing must share the exact implementation path with rendering/export. Do not duplicate schedulers for UI and sound.

### Product completeness, P1
- A nominally implemented feature is **not complete** if it has no audio impact or lacks save/restore, automation/gesture/host test, error state, or self-hosted Windows build. No fake UI meters, sample waveforms, placeholder 'Generate', static images of unavailable controls, or silent bypass of required features.
- Original factory patches/kits/sample license manifest must ship; ensure reproducible generation and audible distinction across required genres; no copying commercial competitors' proprietary content.
- CPU: report p50/p95/p99/p99.9/max against block deadlines and missed-block counts on **owner-selected current Windows DAW machine and existing self-hosted Windows runners**. Classify runner/service failures as infrastructure, compile errors as compile, and per-block misses as performance. Old #109 P1 is not solved merely by changing architecture.

## 3. Branches, PR governance and extraction

### FOUNDATION (no sound-code rewrite)

`FOUND-00`: current Git audit and archival inventory. Record current main/staging heads, open stacked PRs, dependencies, reusable source SHA, CI and exact test commands. Propose an immutable archive tag and new standalone branch. **Do not perform destructive Git operations.**

`FOUND-01`: minimal monorepo CMake restructure with three empty-but-loadable JUCE VST3 processors with distinct identities, visible empty editor/sound initialization/test, and shared libs; compile and run VST3 Validator. The first PR should have only necessary files and no wholesale checkout of Rack code.

`FOUND-02`: port independent `Primitives.h`, `EffectsChain` and `vox-ui` with dependency boundaries, golden test harness; no unreviewed DSP optimization or sound change.

`FOUND-03`: common sample ownership, transport/MIDI scheduler, versioned preset/Program codec, RT telemetry and shared UI model interfaces, with host-facing integration tests.

**Keep each PR Draft until independent review**. Explain dependency and base/head SHA, pass/fail CTest, Windows runner ID, uploaded render artifacts and exactly what production gate is closed. Never merge without explicit owner authorization.

### VOX DRUMS

`DRM-00` legacy eight-pad render / kit and test corpus. `DRM-01` VST3 processor/params/state + Main. `DRM-02` tested eight-pad source migration, sample lifetime. `DRM-03` general 16-pad/8-model DSP and 2-sample player per pad. `DRM-04` exact MIDI+audio scheduler/exports. `DRM-05` genre generator and legal content. `DRM-06` mixer+send/return+8 stereo outs (real DAW activation). `DRM-07` complete ULTIMA VOX MUSIC skin. `DRM-08` exact-Windows/release acceptance.

### VOX TRANCE SYNTHESIS

`SYN-00` Bass/Acid old source audit and reference renders; do not import unreviewed MIDI lifecycle failures. `SYN-01` standalone VST3 shell/state and routing. `SYN-02` working Psy Bass voice/NoteOff. `SYN-03` working Acid 303 mono/legato/accent/slide. `SYN-04` bandlimited 3-osc VA/hybrid + 2 filters, 2 parts, voice budgeting. `SYN-05` envelopes/LFO/mod/macros/FX. `SYN-06` seeded patch+pattern generator and MIDI export. `SYN-07` full UI/factory patches. `SYN-08` exact-Windows/release acceptance.

### VOX FX

`FX-00` note that PR #110 describes obsolete Rack ABI restrictions; do not implement V2.5. `FX-01` standalone JUCE shell and real Noise Riser. `FX-02` 4-layer bounded event runner/curves. `FX-03` full other 11 families with audible golden renders. `FX-04` modulation/macros/four real FX slots. `FX-05` granular/sample content, generator, import/export, bank. `FX-06` real custom FX editor including 20 curves and telemetry. `FX-07` Windows sound/performance/host acceptance.

### SHARED UI

`UI-00` reuse audit of old `libs/vox-ui`. `UI-01` shared theme and `LookAndFeel` skins; `UI-02` parameters/gesture-bound knob/button/selector/meter; `UI-03` responsive editor framework/100–200% DPI; `UI-04` custom curve/waveform/step/piano/XY/matrix editors; `UI-05` control-to-DSP evidence and screenshots in the **real** plugin editors. No image mockups or visual generation unless owner explicitly instructs.

## 4. Required code/design deliverable in EVERY PR

1. Problem and scope (product/phase) and **exact owner-approved upstream base HEAD**.
2. Changed-file inventory, reusable source SHA, any new dependencies/licenses, explicit `no ABI/host/ID changes` where true.
3. Implementation diagram (data path, ownership, realtime thread handoff), parameter map, state version diff, CPU/memory expectation labelled estimate vs evidence.
4. Tests and **actual result artifacts**; Windows self-hosted workflow/job link; render sample/listening notes; host regressions.
5. Known limitations and which required features are missing; never call an incomplete prototype production-ready.
6. Open review findings P0/P1/P2 with path/line/repro/risk/verifiable acceptance; only resolve with tests/code proof and reviewer acknowledgement.
7. Build/install/uninstall SHA-256, signing/security decisions, support matrix and exact binaries tested.

**P0 acceptance checklist:** all core controls functional; verified RT/no UAF; no stuck MIDI; valid preset save/load; no arbitrary outside-audio DSP free; correct plugin parameter IDs/host automation; executable Windows VST3. **P1:** sound quality/pattern-generation, three different drum kits, 12 FX families, synth Acid/Bass sound acceptance, 48 FX and 48 Synth presets, 16 real drum pads, all custom UI bindings. Performance/DAW must be measured on actual build before release.

## 5. Decision guidance for the agent

When specifications conflict with real code, **do not silently invent an implementation**. Quote exact path, symbol, SHA, why it conflicts, 2–3 options and your recommended minimal solution. Do not inflate a lightweight C++ DSP primitive into a general plugin host. Do not create a new GUI framework. Stop before material schema/identity or security change if not explicitly approved. After verifying framework license suitability, JUCE remains primary; iPlug2 is evaluated only if licensing or a material build requirement blocks JUCE.

The product owner has chosen the **separate instruments direction** and says old Rack should be kept as archive/donor. Implementation proposals may be scoped to that direction, but archival destructive operations, merging and releases still require separate explicit approval. For all files that do not exist yet, document them as *proposed paths*.

## 6. First expected reply from Claude

Before writing product DSP, deliver one **FOUND-00 architecture report** (no major code):
- exact current remote branches and SHAs; new standalone branch proposal and archival tag procedure;
- source-level reuse matrix with test/CI status, resolved/unresolved defects;
- proposed CMake targets with three distinct products and shared libraries;
- actual full host parameter maps, plugin identity freeze strategy and one migration/state policy per product;
- complete PR DAG with first three Draft PR scopes and Windows CI jobs;
- JUCE licensing / dependency policy and ULTIMA VOX MUSIC UI audit;
- a meaningful auditory benchmark/test-fixture inventory and what cannot yet be verified;
- decisions requiring the owner (only those blocking actual work).

Proceed only with reviewable, measured code; no silent merges, no replacing old main, no changing #77/#78 status until owner-directed migration communication.
