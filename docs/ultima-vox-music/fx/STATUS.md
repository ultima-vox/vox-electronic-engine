# VOX FX development status — Draft PR #156

**Current engineering checkpoint:** the Windows x64 FX-T18 measurement
scenario is documented in
[FX_T18_WINDOWS_MEASUREMENT_20261122.md](FX_T18_WINDOWS_MEASUREMENT_20261122.md).
One owner-authorized typical-only diagnostic run completed at source
`348bb2c`; its full audit passed integrity checks, but CTest remains blocking
with `CPU_MEASUREMENT_INSUFFICIENT` because `GetThreadTimes` quantized at
15,625 microseconds. No CPU qualification or release claim is made, and the
Wavefold decision is unchanged. The separate method proposal is
[FX_T18_CPU_METHOD_PROPOSAL_20261123.md](FX_T18_CPU_METHOD_PROPOSAL_20261123.md).
The next owner-review artifact is the draft
[FX-T18-WALL-GATE v1.2](FX_T18_WALL_GATE_V1_20261123.md); no full matrix is
authorized until that contract is approved separately.

This is a resume record, not release acceptance. Exact-head Windows suite,
validator and binary SHA are pending; HOLD prohibits merge and release. See the
[reconciliation and timing/export checkpoint](RECONCILIATION_20261119.md)
for exact SHAs, local test evidence and the remaining release gates. The next
[Program trajectory editor checkpoint](evidence/program-trajectories.md) is on the Arena development branch. See the [consolidated implementation handoff](IMPLEMENTATION_PASS_20261119.md) for the subsequent work.

**Current handoff:** [Engineering delta](ENGINEERING_DELTA_20261119.md) is the live requirement map; [Windows engineering-gate plan](WINDOWS_ENGINEERING_GATE_20261119.md) is staged but unauthorized. [TL accepted the first checkpoint for development](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7033415367); staging adds Windows test fixtures/preflight and keyboard curve insertion. The TL has integrated that tranche for development. The TL accepted [measured post-event FX tails and bar-clock diagnostics](HOST_CLOCK_AND_TAIL_20261119.md) for development. The TL also accepted [per-layer Glitch slice authoring and production-rate synthetic regressions](GLITCH_SOURCE_20261119.md) for development; the follow-up adds a historical-project regression, malformed-v8 rollback checks, and [1.0 compatibility notes](USER_NOTES_1_0.md). The TL accepted [fractional moving free Delay](FREE_DELAY_MOTION_20261119.md) for development. The TL also integrated the [transition-boundary fix and Source keyboard scope](FREE_DELAY_BOUNDARY_20261119.md). Current staging adds [Granular Travel](GRANULAR_TRAVEL_20261119.md), [family depth](FAMILY_DEPTH_20261119.md), [antialiased Drive](NONLINEAR_ALIAS_20261119.md), [measured effect tails](EFFECT_TAILS_20261119.md), the [automation contract](AUTOMATION_CONTRACT_20261119.md) and the [TL order](INTEGRATED_CANDIDATE_PLAN_20261119.md). Intended-runner PowerShell 5.1 is a separate, mandatory qualification item, not on the development critical path. Draft/HOLD/NO MERGE. FX-T18, Windows and host proof of the implemented [owner-approved create-only Save](CREATE_ONLY_SAVE_20261119.md), private delivery and listening remain open. The 232-cue catalog is frozen, zero approved. The current exact source/test/host reconciliation is [ENGINEERING_REQUIREMENT_MAP_20261122.md](ENGINEERING_REQUIREMENT_MAP_20261122.md); it records that PR #156 head `5c2f122c0203dde57bda7cd563340164601579b4` is not merged into the assigned Arena branch. The table and resume notes below are historical.

**2026-11-22 product-completion tranche:** the current Arena source adds three
signed, family-named source controls for all twelve families, persisted as a
forward-only FPRG v10 extension with older codecs unchanged; Generator/Mutate
preserves them under the existing Character lock, and Source/Key exposes the
actual controls with revision-guarded edits. The current four-slot FX engine
retains all original IDs, adds explicit type-specific Amount semantics in the
native UI, and applies first-order ADAA to Drive and Wavefold. Local standalone
codec, source-family and FX/nonlinear regressions pass; this does not establish
Windows, DAW, loaded RT, timing, listening, approval, RC, merge or release.

| Milestone | Status |
|---|---|
| FX-01 | In progress: playable noise riser code, gain/duration parameters, v1→v2 state migration, interim bound UI and processor tests. Processor/UI tests await an executable JUCE suite build. |
| FX-02 | Partial: One Shot, Gate, Continuous, Loop, Host Pattern Sequence and bar-aligned Transition have DSP/processor paths. A 4/4 Transition in bars ends on a host bar line; seconds stay free-running. MIDI ownership is FIFO by channel/key, including stolen, replaced and rejected presses. Preview and pattern triggers use isolated finite auditions for held modes. Linux engine tests cover the bar/BPM matrix, tempo changes, clock loss, click-managed cycle seams and voice expiry. Host seek/loop, tempo behavior and the native bindings still need executable plugin verification. |
| FX-03 | Partial: all twelve source families remain selectable. Program codec v4–v9 migrations remain byte-compatible, and the tranche adds signed type-specific `FamilyControls` as forward-only FPRG v10; Source/Key exposes family-named controls, dual filters, transpose, Glitch slices and Granular length/start/travel/reverse with revision guards. `FxSourceSoundTests` and `FxProgramCodecTests` exercise bounded family behavior and v10 migration. The JUCE editor exposes all four per-layer and all four Program-wide trajectories, with guarded undo/reset, multi-node editing, numeric entry and whole-curve clipboard. A bounded WAV decoder/SHA-256 and a three-version immutable lease store connect to opt-in granular playback; FSMP external/pending references have explicit missing-content behavior but processor/host execution remains unverified. Native runtime, DAW restore, sample relink and owner listening remain open. |
| FX-04 | Partial: standalone typed 64-route matrix, LFO and seeded sample-and-hold primitives; four serial shared slots include bypass, lowpass, drive, wavefold, bitcrush, ring, gate, fixed-rate stereo phaser/flanger/chorus, feedback delay and bounded multitap reverb. Existing wet/amount host IDs remain; the native UI now names Amount by effect type (including distinct free Delay and Tempo Delay semantics). Drive and Wavefold use first-order ADAA with zero-amount transparency; tests cover nonlinear folded energy, four-slot tails and block invariance. Eight host macros, two fixed-rate LFOs and seeded S&H route to master gain or stereo balance through up to 64 routes. Loaded RT, broader modulation/quality permutations, host automation and listening remain open. |
| FX-05 | Partial: deterministic standalone Generate/Mutate/A-B and a 48-program procedural seed corpus with locks and audible smoke tests. Generate/Mutate now publish to the plugin Program and have basic seed controls; seed, layer-lock masks, style-seed variant, active-layer count, intensity, energy and stereo are saved in FGEN v3 (v1/v2 migrate) and exposed in an interim settings panel; an interim selector loads the 48 uncurated procedural seed Programs without overwriting automation; session-only full-state A/B store/recall is exposed with explicit host-value recall; per-field locks are exposed for the nine current layer fields; remaining generator settings and locks for future content, persistent A/B and a curated preset browser remain; 1/4–1/32 straight/triplet 4/4 host-PPQ trigger playback, 12 seed patterns and canonical SMF export now have a processor controller and FPAT v3/FPEN project state; basic 1–4-bar length/grid/page and 16-step on/off plus note/velocity/probability/ratchet and swing editing exist; a save-dialog action exports the selected 4/4 trigger pattern to a .mid file using the latest observed host tempo (120 BPM when unavailable; export supports 10–1000 BPM); full workspace, musical curation, full settings, sample handling and host-sync validation remain; a configurable 44.1/48/96 kHz PCM24/float32 WAV save action snapshots Program and FX/host base values and renders on a cancellable editor-owned worker with bounded filter/modulation and serial delay tails, including feedback; bar auditions and one Sequence cycle export at fixed captured tempo; drag export and Windows dialog/worker validation remain. |
| FX-06 | In progress: truthful interim Perform panel with block-snapshot active event/grain counters, saturated retirement-exhaustion drop counter and latched clip indicator; other workspaces and controls remain. |
| FX-07 | Not complete: Windows exact-head suite/validator and binary hash, host, performance, safety, licensing and packaging qualification. |

Resume: Qualify playback, transport, trigger ownership and state in the native plugin. Finish Sequence/Loop transport workflows, advanced trajectory editing, typed filter controls, full modulation destinations/sources and FX algorithms/quality/headroom. Complete granular controls and ownership qualification; wire remaining generator settings, field locks, persistent A/B, curated factory/user preset browser and 120→300→600 content waves; finish WAV/MIDI mode-specific export and all workspace/DAW workflows. The 48 procedural programs and 12 patterns remain uncurated seed corpora. Eight separate authored review candidates are not approved factory content. The checkpoint reports local engine tests and a pinned-JUCE syntax check; it does not establish processor/editor runtime behavior. Windows exact-head suite, VST3 validator, binary SHA, performance/safety/package evidence and Cubase/REAPER acceptance are all unverified; no host or installation acceptance is claimed.

Windows evidence: older runs 44172558893, 44171848293 and 44171634526
failed with four reproducible SEGFAULT test processes (noise riser, processor,
layer and Shepard), not runner outages. Large test objects were moved to heap
as a plausible fix but the hypothesis is not verified until an exact-head
Windows suite passes; isolate surviving failures with crash traces. The existing PR #156 `[RUN_FX_SUITE]` workflow is preserved unchanged.

Skills bridge: inspected the three Markdown adaptations and Arena instructions
at `agents/arena-skill-bridge-20261118@d92d322a0` read-only, plus root
`AGENTS.md`, the agent contract and FX specification. No native skill runtime
or subagent facility was available or claimed; bounded implementation and
independent verification were performed sequentially.
