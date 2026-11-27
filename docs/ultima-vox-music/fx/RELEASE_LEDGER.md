# VOX FX release evidence ledger

## 2026-11-27 — product-completion checkpoint `1625719fd5755fa1516d058b0b272a963aec33c9`, HOLD retained

- TL review comment
  [#7211163434](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7211163434)
  P1 items answered: shipping Wavefold restored to the original memoryless
  triangle fold (bit-identical to `5c2f122` over 4 M samples), ADAA Wavefold
  kept experimental and unwired, nonlinear regression rewritten with explicit
  success gates and the clamp-inclusive reference, FPRG v10 migration covered
  by tests. See [WAVEFOLD_RESTORE_20261127.md](WAVEFOLD_RESTORE_20261127.md)
  and [PRODUCT_COMPLETION_20261127.md](PRODUCT_COMPLETION_20261127.md).
- Invariants held: host parameter IDs (`parameters.csv`), Wavefold type ID,
  frozen diagnostic parser `1.11.0`, create-only WAV/MIDI export policy,
  frozen `232 review-only / 0 approved` inventory with the `600` target.
- Task-5 audit: persistent A/B, 17 modulation destinations, browser facets
  and Save+drag export confirmed already implemented (earlier status notes
  overstated those gaps); duration lock is intentionally non-serializable in
  FGEN v4 (tested) and has no UI, so no schema change was made.
- Windows x64 engineering candidate **prepared, not dispatched**: preflight
  extraction at this SHA passed (16 PowerShell blocks extracted, not
  executed; bundle names match `SuiteIdentity.cmake`). Exact-HEAD Windows
  suite, Steinberg validator and artifact SHA remain open behind CI
  authorization. This is not Windows, DAW, listening, RC, merge or release
  evidence.
- TL comment
  [#7212562366](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7212562366)
  accepted `8ebdec0` for Windows engineering preparation; nonblocking
  corrections applied (file list, FX-T09/FX-T16/FX-T17/FX-T20 numbering,
  SHA-attributed test evidence with retained CTest/JUnit/provenance logs,
  amount-0 help qualified to nominal ±1, no whole-bounce-identity claim,
  local-audit framing). One exact-source HAMUNAPTRA run proposed in
  [WINDOWS_RUN_PROPOSAL_20261127.md](WINDOWS_RUN_PROPOSAL_20261127.md) —
  **not dispatched, owner authorization required**.
- **Authorized Windows engineering run — FAIL, focused fix** (TL comment
  [#7213197538](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7213197538)):
  HAMUNAPTRA job 140374750304 /
  [run 45959826386](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/45959826386)
  at `8ebdec0` passed host/source, PowerShell Desktop 5.1.26100.9711
  preflight (19 scripts + inventory fixtures), dependency/validator
  preparation and CMake configure, then **build failed MSVC C2397** at
  `FxNonlinearAliasTests.cpp:531,534` — `std::nan("")` (double) narrowed
  into `Settings` float members; CTest and validator skipped; the uploaded
  510-byte archive is environment-only, **not VST3**. Fix: those two inputs
  only, now `std::numeric_limits<float>::quiet_NaN()` (double NaN retained
  for `prepare(double)`; negative checks unchanged). Repo-wide list-init
  audit found **no other** C2397-class sites (all remaining NaN/double uses
  are double-parameter calls; float-aggregate brace inits use float
  variables or representable constants). Focused regression at the corrected
  SHA: FxNonlinearAliasTests (strict `-Werror`, plus ASan/UBSan),
  FxEffectsTests, FxProgramCodecTests, FxGeneratorTests,
  FxPortablePresetTests — **0 failures each**; GCC cannot diagnose this
  narrowing even with `-Wfloat-conversion -Werror` (verified), which is why
  Linux stayed green. Windows evidence does not transfer to the corrected
  SHA; **no re-dispatch until the owner authorizes the corrected SHA**.
  HOLD/no merge/no release unchanged; no ETW work.

## 2026-11-22 — product-completion tranche `a6365ebf88414c4ff5813df396e5b12f2fc8e49b`, HOLD retained

- The assigned Arena branch remains separate from exact PR #156 head
  `5c2f122c0203dde57bda7cd563340164601579b4`; no merge or integrated-candidate
  claim is made. See [exact requirement map](ENGINEERING_REQUIREMENT_MAP_20261122.md).
- Source-family controls now have a real `FamilyControls` binding in
  `NoiseRiser`, `SourcePanel`, `Generator` lock preservation, and forward-only
  Program codec v10 migration. Existing codecs, Wavefold type ID, and host
  parameter IDs are preserved.
- Drive and Wavefold now use bounded first-order ADAA; local nonlinear/effects
  regressions pass. This is not Windows, DAW, loaded-RT, listening, production
  receipt, RC, merge, or release evidence. See
  [NONLINEAR_ALIAS_20261122.md](NONLINEAR_ALIAS_20261122.md).
- The accepted FX-T18 parser package is frozen at `1.11.0` and remains
  diagnostic-only. Counters remain observed `5/4/9`, reconciled `5/3/8`, one
  unmatched noncontrolled stop, zero unmatched controlled joins; no production
  timing qualification is granted.
- Review content remains exactly `232 review-only / 0 approved`; the `600`
  approved-program target is unchanged. No raw audio transfer or catalog
  expansion occurred.

## 2026-11-19 — current engineering evidence, HOLD retained

- [Granular Travel](GRANULAR_TRAVEL_20261119.md): per-layer scan of grain starts, distinct from pitch and reverse, with opt-in FPRG v9 state requiring review. Legacy v6–v8 encodings, host IDs, Save, and offered drag contracts are unchanged. See the [integrated candidate plan](INTEGRATED_CANDIDATE_PLAN_20261119.md).

- [Delay transition boundaries and Source keyboard scope](FREE_DELAY_BOUNDARY_20261119.md): no rounded tap jumps while entering or leaving the moving-read mode in the targeted tests; fixed sound pins unchanged. Source traversal is scoped and tests skip controls hidden by family. [Current engineering candidate evidence and open gates](ENGINEERING_CANDIDATE_EVIDENCE_20261119.md).

- [Free Delay Amount motion](FREE_DELAY_MOTION_20261119.md): fractional moving read reduces measured staircase error while nine stationary fixtures stay byte-identical. Existing Delay automation retains its meaning but can sound different while moving. No state/host-ID change; nonlinear aliasing, FX-T18, Windows and DAWs remain open.

- [Glitch checkpoint](GLITCH_SOURCE_20261119.md), [TL acceptance for development](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7034953740), and [1.0 user compatibility notes](USER_NOTES_1_0.md): **FPRG v8 is forward-only.** The new reader accepts v1–v7; the writer still emits v6/v7 for a Program with all-default Glitch settings. An authored v8 Glitch Program (including nested project/A-B state or portable `.voxfx`) is rejected by readers capped at v7. Do not silently down-convert an edited Program; retain older project/preset backups. The portable size limit is still 64 KiB. No validator, DAW, release or owner approval follows.

- [Engineering delta](ENGINEERING_DELTA_20261119.md): bounded granular admission, layer drag restore guard, a single requirement/evidence map, and the frozen 232-cue private transfer index. [Windows gate](WINDOWS_ENGINEERING_GATE_20261119.md) prepared, not dispatched; FX-T18 remains open. The preceding clock/tail checkpoint adds [measured FX-tail and bar-clock feedback](HOST_CLOCK_AND_TAIL_20261119.md), not Windows/DAW qualification. The owner approved [create-only Save](CREATE_ONLY_SAVE_20261119.md); local implementation is staged, Windows and DAW proof are pending. TL follow-up adds Windows filesystem fixtures and 5.1 syntax/inventory preflight tooling; neither was run on Windows.

- [Motion history and selected-bar Pattern authoring](MOTION_PATTERN_AUTHORING_20261119.md): Linux native history rollover, mixed replay and non-Motion project restore regression; DAW focus and keyboard ownership are not qualified. [Wave B Edit Points](WAVE_B_EDIT_POINTS_20261119.md) adds 32 authored cues, bringing the review-only total to 232 with zero approvals. No Windows dispatch, new audio ZIP or upload.

- [Transfer guards and timing capture](TRANSFER_GUARDS_20261119.md): existing-file recheck (not atomic compare-and-replace), isolated replay/teardown tests, and per-callback wall/CPU miss rows. Linux baseline and deliberate same-core contention retain their misses separately; neither clears FX-T18. No change to the 200 unapproved review candidates.

- [Drag replay](DRAG_REPLAY_20261119.md) and [Wave B Session Cuts](WAVE_B_SESSION_CUTS_20261119.md): same-path drag retry and stale completion guard; 24 new individually authored cues, 200 review-only total, no new ZIP or approvals. Native regressions pass; new wall and thread-CPU miss evidence keeps FX-T18 open.

- [Prepared WAV and MIDI drag](FILE_DRAG_20261119.md): bounded worker staging, one native copy-only offer per gesture, independent Save fallback, and offscreen payload/lifecycle checks. Real Win32 drag, drop-copy behavior, disk/teardown and owner-host acceptance remain unqualified. No audio delivery or new approvals.

- [Candidate gate and outstanding work](CANDIDATE_GATE_20261119.md): native same-directory export commit without copy fallback, no-clobber race and cross-device tests, plus an opt-in Windows thread CPU probe (not executed on Windows). Latest Linux timing was clean; prior wall overruns keep FX-T18 open. No catalog or release approval.

- [Export lifecycle follow-up and gain-staging matrix](EXPORT_QUALIFICATION_FOLLOWUP_20261119.md): active SMF cancellation, chooser reentry protection, host-save and rate-change fixtures, and SHA-verified level estimates for the same 176 unapproved references. Windows, FX-T18, private delivery and owner acceptance remain open.

- [Shared WAV/MIDI export and consolidated listening review](FILE_EXPORT_AND_ARRANGEMENT_REVIEW_20261119.md): immutable MIDI file job, native granular preview/retirement and wet-bounce regression, 176 prior review cues verified in one bundle, no approvals. Three fresh wall-only misses; FX-T18 remains open.

- [In-place bounce history](BOUNCE_HISTORY_20261119.md) and [Wave B 56](WAVE_B_ROOM_CUTS_20261119.md): live audition cannot steal musical voices; identical-history wet-tail and clipboard-restore Linux tests passed. One fresh wall-only deadline miss; FX-T18 remains open. A 120 + B 56 = 176 review candidates, zero approved.

- [Pattern authoring / bounce](PATTERN_BOUNCE_20261119.md) and [Wave B 48](WAVE_B_CROSSINGS_20261119.md): same-grid bar copy/paste and native clock states; offline callbacks exclude audition voices; new diagnostic timing does not supersede prior deadline misses. A 120 + B 48 = 168 review candidates, zero approved.

- [WAV lifecycle and timeline follow-up](WAV_LIFECYCLE_20261119.md) and [Wave B 40](WAVE_B_BRIDGE_SIGNALS_20261119.md): pre-commit cancel preserves the destination, native lifecycle and timeline coverage widened, A 120 + B 40 (160 review candidates, 0 approvals; 140 short of 300 candidates), private transfer and actual mix review still open.

- [Timeline and export lifetime](TIMELINE_EXPORT_20261119.md) and [Wave B 32](WAVE_B_COUNTERPOINTS_20261119.md): native seconds/bars viewport, export through editor close, A 120 + B 32 (152 review candidates; 0 approvals), Windows/owner gates still open.

- [FX-T18 paired Linux callback measurements](FX_T18_PAIRED_20261119.md): regular/authored stress improved; same-core scheduling still causes deadline misses; Windows qualification pending.
- [Wave B interludes review](WAVE_B_INTERLUDES_20261119.md): A 120 + B 24 staged, zero approvals; 300/600 not reached.
- Windows fixture overflow fix `71c5045`, targeted 1/1 at `514e96a`, according to TL/owner. This is not full CTest, Steinberg validation or artifact SHA verification. The historical rows below do not describe the latest P0 status.

For current status and exact qualifications, see the [Wave A review and Windows HOLD](WAVE_A_REVIEW_HANDOFF_20261119.md); the preceding [capture handoff](CAPTURE_WAVE_A_HANDOFF_20261119.md) retains the full FX-01…07 / FX-T01…20 matrix. The table below is historical baseline evidence, not current qualification.

Baseline: `a23b67b02264ca42f2b9bf5f9fed8e233a31fe35` (TZ); implementation parent `f4e407fc379cbc36bd447af24e0626dc1c80fec8`. This is a gap ledger, not a release approval. Any later commit invalidates exact-HEAD qualification until rerun. Exact baseline Windows [suite 44367745359](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44367745359) and [build 44367745537](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44367745537) queued; no bundle SHA or validator result. Prior four Windows SEGFAULTs remain unverified. Linux standalone evidence is historical and partial; see `STATUS.md` and `DSP_BENCHMARK.md`. No manual DAW or listening approval has been performed.

| Test | Status / partial evidence at baseline | Exact-HEAD Windows / audio / manual |
|---|---|---|
| FX-T01 | PARTIAL: twelve preliminary family tests/reference; character approval missing | queued / no approved audio / not done |
| FX-T02 | PARTIAL: four layers and snapshots; independent pitch/filter missing | queued / not approved / not done |
| FX-T03 | OPEN: gain/pan only, not 20 typed curves or migration | queued / not approved / not done |
| FX-T04 | OPEN: seconds one-shot only, no complete modes | queued / not approved / not done |
| FX-T05 | PARTIAL: eight voices, four fades and drop counter standalone; held note ownership missing | queued / not approved / not done |
| FX-T06 | PARTIAL: bar one-shot integrated with host clock and UI; standalone four-layer 16-bar rendering and tempo change tested on Linux; processor test written, not executed; no target-bar Transition, Gate or host evidence | queued / not approved / not done |
| FX-T07 | OPEN: key/scale musical proof absent | queued / not approved / not done |
| FX-T08 | PARTIAL: eight macros, limited routes/sources, standalone tests | queued / not approved / not done |
| FX-T09 | PARTIAL: effects tests; 4-drive saturation and tails require qualification | queued / not approved / not done |
| FX-T10 | PARTIAL: immutable leases, WAV/hash, concurrency and allocation tests; actual host flow unexecuted | queued / not approved / not done |
| FX-T11 | PARTIAL: deterministic generator and session A/B; full Program/curation unavailable | queued / not approved / not done |
| FX-T12 | OPEN: no approved catalog audit | queued / none / not done |
| FX-T13 | PARTIAL: standalone pattern and MIDI tests; host/DAW parity missing | queued / not approved / not done |
| FX-T14 | PARTIAL: WAV/SMF create-only publication and lifecycle tested on Linux; Windows chooser/host transfer missing | queued / not approved / not done |
| FX-T15 | OPEN: editor only syntax checked here; DPI/runtime unexecuted | queued / N/A / not done |
| FX-T16 | PARTIAL: codec tests; full processor and project reopen unexecuted | queued / N/A / not done |
| FX-T17 | BLOCKED: historical Windows crashes; no exact-head Release/validator/artifacts or native create-only results | queued / N/A / not done |
| FX-T18 | PARTIAL: Linux standalone allocation/stress and diagnostic timings only; Windows deadline matrix missing | queued / not approved / not done |
| FX-T19 | OPEN: complete asset/license inventory absent | queued / N/A / not done |
| FX-T20 | NOT READY: single integrated owner acceptance after candidate qualification | queued / not approved / not done |

## Current local work — 2026-11-18 (unqualified checkpoints)

Code checkpoint `5675dd20d51814f80a3c093c587e1dda3ce72ab4` adds Playback Mode/TriggerPolicy state migration (FPRG v4), MIDI NoteOff ownership, six mode choices, and 20 DSP trajectories (per-layer and global). Thirty-five JUCE-independent FX test executables compiled with `g++ -std=c++20 -O2 -pthread` passed at that checkpoint, including codec/store (maximum-size v4 payload), trajectory/render, playback-mode, family/render, pattern, modulation and sample-store primitives. Playback-mode tests also passed AddressSanitizer+UndefinedBehaviorSanitizer.

Follow-up code checkpoint `01685a2` adds delay feedback (50–500 ms, up to 75%, bounded 10 s/slot decay); the updated `FxEffectsTests` passed on this code. The other 34 standalone tests were not rerun after this change, so this is NOT a full 01685a2 regression pass. A 4-second, four-layer synthetic Transition WAV was rendered from `ee15af239e297bd655711867d018cf0b05f994c6` into the ignored local artifact `build/vox-fx-hybrid-transition-preview-normalized.wav`, peak-normalized to -6 dBFS for audition; it has no post-FX, is DSP smoke evidence only, and is not approved audio.

Follow-up P1 code checkpoint `ee15af239e297bd655711867d018cf0b05f994c6` on the current Arena source branch fixes same-note overlap with FIFO NoteOff ownership; Gate holds the final source state, Continuous free-runs a slow procedural trajectory, and Loop alone cycles with edge fades. Updated playback tests cover two same-note NoteOns/two NoteOffs, channel isolation, mode boundaries at 44.1/48/96 kHz and Continuous beyond 32 seconds. `FxPlaybackModeTests`, `FxEffectsTests` (including delay-bank reuse), and `FxNoiseRiserTests` passed on the source tree committed as this checkpoint; playback tests passed ASan+UBSan. This targeted check is not a full suite.

PR reconciliation preview: `git merge-tree --write-tree f6ed6de9124e70d186542cd098c27f3d286425ba 2b4c96e73fc851fe3359bf731372ff07f5c19881` yielded merged **tree** `374bbaa9cb24e57edffad39cd67a4f83d4ce23d5` with no content conflicts. Relative to the PR head, the preview changes only `RELEASE_LEDGER.md`, `STATUS.md`, `FxEditor.cpp`, `NoiseRiser.h`, `FxEffectsTests.cpp`, and `FxPlaybackModeTests.cpp`; the workflow gate is unchanged. From an extracted copy of this tree, `FxPlaybackModeTests`, `FxEffectsTests`, and `FxNoiseRiserTests` passed, and playback tests passed ASan+UBSan. This is a merge-tree snapshot, **not an integrated commit SHA**. Session branch policy keeps this work on `arena/aba74ad9-vox-electronic-engine`; PR #156 remains at `f6ed6de` until its pinned head branch is updated by an authorized session. Do not count this preview as exact-commit or release qualification.

`FxFileExportTests`, sample decode/controller tests and `FxProcessorTests` were not executed because JUCE headers are unavailable; CMake/CTest could not run (`cmake` is not installed). No exact-HEAD Windows run was triggered or claimed. These checks do not establish plugin integration, Windows qualification, production audio approval, or DAW evidence.

| Test | Current local result | Still missing |
|---|---|---|
| FX-T02 | PARTIAL: layer pitch/cutoff controls affect sample render; no full oscillator/filter architecture | independent filter units/UI and exact-head qualification |
| FX-T03 | PARTIAL: codec v4 migration and 20 DSP trajectories; codec/store/render tests passed | global editor/multiselect, musical review, JUCE/Windows tests |
| FX-T04 | PARTIAL: mode persistence, basic Gate/Continuous/Loop lifecycle, NoteOff and retrigger-policy tests passed | Sequence/Transition synchronization semantics, loop seam/transport tests, JUCE/host proof |
| FX-T05 | PARTIAL: same-note channel ownership/release test passed | CC/Panic/steal/tail completeness and Windows host evidence |

## Content waves

| Wave | Target approved unique Programs | Approved / audited | Evidence |
|---|---:|---:|---|
| A | 120 | 0 documented | no catalog/listening sign-off |
| B | 300 cumulative | 0 documented | no catalog/listening sign-off |
| C | 600 cumulative | 0 documented | no catalog/listening sign-off |

48 deterministic procedural seeds and 12 patterns are prototypes, not approved factory content. Counts mean no documented approvals, not a claim that all seeds are unusable. Production 1.0 requires all 600 unless the owner changes the spec.

## Integrated bar one-shot (post-baseline checkpoint)

FPRG v2 migrates v1 to seconds; bar one-shot requires playing host with finite PPQ/tempo and explicit 4/4. Loss of clock fades bar voices; seconds voices remain playable. Native selector, processor host-clock path and processor/editor test are implemented but execution is still pending Windows. Linux standalone bar event tests include audible four-layer 16 bars at 60 BPM/8 kHz, tempo change and invalid-clock fade; this is diagnostic, not a musical approval, target-bar Transition or 16-bar processor/DAW proof. WAV export explicitly refuses bar Programs. Exact commit and run IDs are recorded in the associated PR checkpoint; later changes invalidate exact-HEAD evidence.
