# Acid Synth — production delivery plan (owner reference, 2026-10-26)

Status: **Acid DSP, controls and SOUND UI in progress; NOT production accepted**. Work branch: `fix/acid-contract-ui-integration`; review PR #59. Never merge unfinished Acid into `main`.

## Scope lock — Acid module ONLY (owner clarification, 2026-10-26)

**The existing Rack is considered implemented / nearly completed and is OUT OF SCOPE for this work.** Do not redesign, reconstruct, refactor, restyle, or extend Rack, its slot cards, routing UI, global project header, host transport, or shared keyboard. The red annotated region is precisely **NOT** our task. Our deliverable is only the **selected Acid Synth workspace** shown in the green annotated region, with production-grade independent controls, sound DSP, modulation, pattern and presets.

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
| 5 | Extended modulation + performance | ENV1/ENV2/LFO1/LFO2/LFO3/STEP each backed by distinct source state and timing; assignable destinations; portamento/unison/detune/spread; discrete legato/retrigger/velocity-accent/slide-on-overlap; host/preset roundtrip |
| 6 | Pattern / generation / MIDI | Sequence ownership per selected Rack slot; 16 visible editable cells per lane; accent/slide/octave mapped to existing Step fields; Generate/Mutate/Clear/Play/Stop real; PPQ sync, swing, resolution, length, export and reimport agree |
| 7 | Final Acid workspace UI | Keep existing Rack/outer shell/keyboard exactly as-is; implement only Acid instrument header content, 303 hero/banner, top synth cards, center sequencer, lower Mod/Performance/PlayMode inside selected Acid workspace; VOX shared components and screenshots versus owner source |
| 8 | Production verification | Cubase and REAPER VST3; state restore, automation playback, all 16 slots, multiple Acid instances, release/no stuck notes, CPU, NaN/denorm, 1024×768 baseline and 1180×760/1500×920/125%-DPI screenshots; independent review & owner acceptance |

## Hard technical decisions

1. **Rack ownership (non-negotiable)**: Product Rack, its routing/slots/host transport, shared keyboard and shell are read-only external dependencies. No Rack implementation or redesign; all functionality in Acid and thin existing integration points.
2. **Control ownership**: Editor widgets send UI-thread commands to a controller/processor per slot. Do **not** bind an `AcidPanel` directly to an address of `AcidEngine` owned by the Rack: runtime instances can be replaced and processing is concurrent. A change must update host parameter authority, persistent module state and audio-thread snapshot atomically/without callback locking.
3. **Stable host identifiers**: Keep existing eight frozen slot macro IDs. Acid-specific controls require independent host-visible IDs in `PluginProcessor::createParameterLayout()`, with a slot namespace stable across instrument selection and project reload. Do not map all controls to one macro; do not rename old automation IDs. Exposing new Acid-specific host parameters is a host/schema implementation task, **not** an edit to `InstrumentContract.h`.
4. **State**: Every functional selector, knob, toggle, modulation source, sequence edit and generator setting must survive save/load, plugin reopen and preset recall. V1 Acid sound state compatibility must be tested; a migration cannot lose the first nine parameter values. Never store envelope/audio runtime state.
5. **Mod sources**: Acid's original 36 parameters implement ENV1. The Acid-only v4 extension adds **LFO1 with four independent, host-automatable controls** and a real sample-continuous DSP signal. The UI switches actual ENV1/LFO1 bindings. **Not complete:** ENV2, LFO2/LFO3, STEP, tempo-sync for LFO1; each requires independent persisted state and actual audio contribution. Never show unsupported sources as working.
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

