# Acid Synth — production delivery plan (owner reference, 2026-10-26)

Status: **Acid DSP, controls and SOUND UI in progress; neither Acid nor Rack production-accepted**. Work branch: `fix/acid-contract-ui-integration`; review PR #59. Never merge unfinished Acid into `main`.

## Scope lock — Acid module ONLY (owner clarification, 2026-10-26)

**Rack is not yet Production-ready (owner clarification, 2026-10-26). It is nevertheless OUT OF SCOPE for this Acid module feature branch.** Do not redesign, reconstruct, refactor, restyle, or extend Rack, its slot cards, routing UI, global project header, host transport, or shared keyboard. The red annotated region is precisely **NOT** our task. Our deliverable is only the **selected Acid Synth workspace** shown in the green annotated region, with production-grade independent controls, sound DSP, modulation, pattern and presets.

Permitted integration is minimal: connect `AcidPanel` to the existing editor's selected-instrument workspace and implement necessary per-slot Acid parameter automation/state forwarding through existing host/processor extension points **without redesigning Rack or its contracts**. Rack regression tests remain acceptance guards, not feature-development tasks. Investigate their failures by fixing Acid integration and state/descriptor compatibility **inside Acid first**. If proof identifies a pre-existing Rack defect, document it separately and request explicit scope approval; do not silently touch Rack.

Out-of-scope paths by default: `libs/rack/**`, `libs/instrument/InstrumentContract.h`, `libs/instrument/InstrumentAbi.h`, `libs/sequence/Sequence.*`, generic Rack rail UI, global header, global routing and product navigation. A tightly scoped bridge in `apps/vst3/PluginProcessor.*` or `apps/vst3/PluginEditor.*` is allowed only as necessary to mount and automate Acid, retaining all existing behavior. Prefer `libs/acid/**`, `libs/ui/AcidPanel.*`, Acid-specific adapter and Acid tests. Other instrument modules must remain unaffected.

## Scope and authority

The user provided two 1024×768 Acid reference screenshots on 2026-10-26. The unannotated screenshot is the visual target; the annotated copy makes the boundaries explicit. **Red = existing Rack/shared shell** (not a design stroke); **green = Acid instrument workspace**. Do **not** draw either marker in the product. No replacement of the entire product `PluginEditor`, no fake rack slots, no fabricated host CPU/MIDI status or fake playhead.

The reference is the *target*, not an existing successful VST3 capture. The PR #43 UI Design System and Visual Reference Spec are useful architectural/component references but PR #43 is an unmerged candidate; do not merge it wholesale into this branch.

The shared `instrument/InstrumentContract.h`, `VoxMidiEventV1`, `sequence::Sequence`, routing and 16-slot Rack schemas are authoritative and unchanged.

## Delivery slices

| Slice | Task | Acceptance / evidence |
| --- | --- | --- |
| 1 | Freeze scope, screen map, semantic-control manifest, ID/isolation regression tests | This doc + `ACID_UI_CONTROL_MATRIX.md` committed; every existing Acid DSP ID unique and descriptor-aligned; exact screen layout listed |
| 2 | Acid-only DSP/Provider stabilization | Fix Acid-specific causes of the two failing Rack regression tests without modifying Rack; Windows Release and entire existing test suite green; Acid render, NoteOn/Off, release, state, parameter isolation evidence |
| 3 | Independent parameter authority | 1 knob → exactly 1 semantic parameter; per-slot, stable VST3 host IDs and automation; no reassignment of unrelated macros; transactional/RT-safe publication into current Rack runtime; V1→V3→future version state migration |
| 4 | Working Acid sound path | Oscillator morph/type, tune/fine/PW; filter 4 modes and key tracking; amp/filter ADSR; distortion modes/Tone/Mix; Accent/Slide/Output/Pan; measured audio-difference and envelope tests |
| 5 | Extended modulation + performance | ENV1/ENV2, LFO1–3 and STEP all have independent Acid DSP and host parameter controls (v7; latest Windows CI pending). LFO tempo sync remains outstanding; STEP uses real ProcessBlock BPM/PPQ. Verify audio, state and UI test results before acceptance |
| 6 | Pattern / generation / MIDI | Sequence ownership per selected Rack slot; 16 visible editable cells per lane; accent/slide/octave mapped to existing Step fields; Generate/Mutate/Clear/Play/Stop real; PPQ sync, swing, resolution, length, export and reimport agree |
| 7 | Final Acid workspace UI | Keep existing Rack/outer shell/keyboard exactly as-is; implement only Acid instrument header content, 303 hero/banner, top synth cards, center sequencer, lower Mod/Performance/PlayMode inside selected Acid workspace; VOX shared components and screenshots versus owner source |
| 8 | Production verification | Cubase and REAPER VST3; state restore, automation playback, all 16 slots, multiple Acid instances, release/no stuck notes, CPU, NaN/denorm, 1024×768 baseline and 1180×760/1500×920/125%-DPI screenshots; independent review & owner acceptance |

## Hard technical decisions

1. **Rack ownership (non-negotiable)**: Product Rack is not production-accepted; its routing/slots/host transport, shared keyboard and shell are external dependencies to Acid feature work. No Rack implementation or redesign; all functionality in Acid and thin existing integration points.
2. **Control ownership**: Editor widgets send UI-thread commands to a controller/processor per slot. Do **not** bind an `AcidPanel` directly to an address of `AcidEngine` owned by the Rack: runtime instances can be replaced and processing is concurrent. A change must update host parameter authority, persistent module state and audio-thread snapshot atomically/without callback locking.
3. **Stable host identifiers**: Keep existing eight frozen slot macro IDs. Acid-specific controls require independent host-visible IDs in `PluginProcessor::createParameterLayout()`, with a slot namespace stable across instrument selection and project reload. Do not map all controls to one macro; do not rename old automation IDs. Exposing new Acid-specific host parameters is a host/schema implementation task, **not** an edit to `InstrumentContract.h`.
4. **State**: Every functional selector, knob, toggle, modulation source, sequence edit and generator setting must survive save/load, plugin reopen and preset recall. V1 Acid sound state compatibility must be tested; a migration cannot lose the first nine parameter values. Never store envelope/audio runtime state.
5. **Mod sources**: ENV1 and ENV2 have distinct note-triggered ADSRs. LFO1/2/3 each have independent Rate, Depth, Wave, Target, phase and slot-scoped host automation. **Not complete:** host tempo sync for LFO1–3, final UI/DAW acceptance and transport testing. STEP implements 16 PPQ-synced independent levels. DSP additions are implemented in source but v6 Windows CI is pending; do not mark tested until it finishes.
6. **Choice controls**: Oscillator shape, filter mode, distortion mode, root/length/resolution and play modes must be semantically bound, not look-only. AMP vs FILTER must rebind the knob value, listener and automation attachment to the correct parameter, without mutating only its label or `paramId`.
7. **Real-time correctness**: no mutex/heap allocation or UI Component touching inside `processBlock`; no stale pointers/callbacks after slot replacement; ordered sample-offset MIDI, including velocity-zero NoteOn; test NoteOff, CC120/123 and release across audio block boundaries.
8. **Honest status**: passing compile/CTest is necessary, not sufficient for visual match, audible Acid character or manual DAW tests. Do not claim production-ready until all gates pass.

## Acceptance gate / reference viewport

Primary screenshot `1024×768`: one horizontal global header; left persistent 16-slot Rack rail ~22% width, right selected Acid workspace ~78%. Within the workspace: instrument header and channel/preset, 6 navigation tabs, wide 303 Acid hero banner, seven compact top module cards (OSC/FILTER/ENVELOPE/DIST/ACCENT/SLIDE/OUTPUT), a single full-width 16-step sequencer with Note/Accent/Slide/Gate/Octave lanes, Modulation/Performance/PlayMode bottom cards, fixed keyboard footer. Avoid cropped controls and horizontal scroll. Required responsive checks: 1180×760, 1500×920 and 125% Windows scaling; control touch target, readable values, GPU/text rendering and focus order all reviewed.

Gate output for each review commit: exact SHA, CI URL, failures and screenshot comparisons (target vs JUCE capture), trace evidence for every control, and explicit manual DAW status. Owner approval required before merge.

## Implementation checkpoint — LFO1 slice (2026-10-26)

- Acid-only v4 state adds stable semantic IDs `lfo1_rate`, `lfo1_depth`, `lfo1_wave`, `lfo1_target`; old IDs 0–35 retain ordering. Versions 1 and 3 load with LFO1 depth off, including into a previously active Acid engine. V4 save/load persists all four.
- `AcidEngine::renderStereoSample`: LFO1 is a sample-continuous 0.05–20 Hz bipolar oscillator; four waveforms and six destinations. ENV1 and LFO1 modulation paths sum only at their own chosen destinations; no shared envelope state. No audio-thread allocations introduced.
- Stable per-slot host IDs are registered for LFO1. `AcidParameterBridge` skips packed play-mode bitset index 35 but reads all extended Acid parameters through index 39.
- `AcidPanel`: actual ENV1/LFO1 tab selection swaps attachments; inactive controls have no live host attachment. Waveform and Rate show semantic values, destination combobox switches host ID.
- Tests cover v3→v4 migration, v4 LFO1 persistence, per-slot automation, UI independence, measurable audio effects from depth/rate/wave/target, and finite output. **Do not mark passed until Windows CI verifies head.**
- UI target still owner-provided Acid 1024×768; original Rack remains out of scope. Remaining blockers: full CTest green (existing Bass save/restore diagnostic), actual screenshot comparison, ENV2/LFO2/LFO3/STEP, functional transport, DAW acceptance.


## ENV2 checkpoint and Rack status clarification (2026-10-26)

- Owner: **Rack itself is not Production-ready**. This does not authorize Rack redevelopment within Acid PR #59. Integrating with Rack is necessary for testing, but its final acceptance belongs to a separate platform effort. Acid feature acceptance and whole-plugin Production release are different gates.
- Acid v5 appends six ENV2 params (Target, Amount, A, D, S, R) after the 40 existing params, preserving every historical host ID; default depth is zero. ENV2 is an independent note-triggered ADSR mixed only into its selected modulation destination.
- Supports sound payload schemas v1/v3/v4/v5; older presets disable non-existent ENV2 even when loaded over an active newer instance.
- ENV2 tab in actual AcidPanel reattaches five real parameter controls and the target selector; the existing ENV1 and LFO1 assignments are independent.
- New audio tests for ENV2 sound response and modulation attack, state migration and editor isolation are committed. **Their Windows CI result is pending; do not claim Production acceptance.**
- Actual `acid-actual-editor-1024x768.png` artifact from Windows run 40373368577 is confirmed. It differs materially from the owner's UI reference: 303 hero artwork absent, cramped control/value typography, modulation/pattern spacing and play transport unfinished. This is a functional intermediate UI only.

## LFO2/LFO3 v6 implementation checkpoint (2026-10-26)

- v6 appends eight new Acid semantic IDs at 46–53, preserving all historical parameter IDs and eight Rack host macro assignments. Each of LFO2/LFO3 has its own phase accumulator, Rate, Depth, Wave, Target and a default depth of zero.
- Acid DSP uses one consistent four-wave bipolar generator implementation with independent target routing; VST3 APVTS and per-slot Acid bridge enumerate the new IDs automatically. No shared Rack implementation changes.
- Performance: per-source frequency exponentials are evaluated once per audio block rather than three times per output sample; phase increment is smoothed per sample to avoid discontinuities with host automation.
- State v6 preserves v1/v3/v4/v5, explicitly clearing previously active LFO2/LFO3 on legacy recall. Audio tests check Depth, Rate, Wave, Target and processing continuity over four 4096-frame blocks; UI tests verify independent sliders for LFO1/2/3 and v6 round-trip.
- Five modulation tabs ENV1, ENV2, LFO1, LFO2, LFO3 are linked to actual parameter attachments and target selectors. STEP remains unimplemented; do not add a fake clickable STEP tab.
- Windows runner currently has several older PR #59 runs in progress and the newest v6 run queued. **No full green Windows CI or Production declaration.** Latest completed pre-v6 run passed 29/30, with separate Bass save/restore diagnostics still failing. Rack itself remains not Production-ready.
- Remaining Acid deliverables: STEP DSP/UI, generator transport and DAW synchronization, real 303 hero graphic and pixel-accurate owner UI, preset/automation DAW QA and CTest gate. Require an isolated successful latest-head build before merging any Acid PR.

## STEP v7 implementation checkpoint (2026-10-27)

- Acid sound state v7 appends **20 separate semantic values** at IDs 54–73: STEP Amount, Target, Rate, Smooth, and `step_01` through `step_16`. Old 54 IDs and existing Rack macros remain unchanged. Total Acid parameters: **74**.
- Separate from the existing note/pattern sequencer: STEP produces a bipolar control signal from 16 levels, rate choices 1/8, 1/16, 1/32, 1/8 triplet, musical position supplied through the already-existing `instrument::ProcessBlock` BPM/PPQ/transport flag, and selectable destination. No new shared ABI or Rack API.
- STEP does not advance or influence the sound while host transport is stopped. It is fully disabled (depth zero) in migrated v1/v3/v4/v5/v6 states. New v7 persists the complete curve, rate, smooth and target.
- The selected Acid SOUND/Modulation workspace adds a sixth **STEP** source tab, 16 actual per-step APVTS-attached sliders and Depth/Rate/Smooth knobs, with independent destination dropdown. MIDI pattern StepGrid remains separate.
- Tests added for audible STEP response across Amount/Target/Rate/Smooth/level, stopped transport, finite output, v6→v7 migration, v7 state roundtrip, independent host project save/restore and UI per-step/slot isolation. **Latest Windows CI is in progress, and these tests are not yet accepted.**
- Remaining: green latest CI, actual 1024×768 Acid screenshot vs owner reference, robust host transport/restart/seek validation, host-tempo LFO sync, independent musical sequence Play/Stop, 303 banner artwork, manual Cubase/REAPER evaluation. Rack is independently **not Production-ready** and remains out of scope.
