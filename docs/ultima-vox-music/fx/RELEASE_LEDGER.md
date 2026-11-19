# VOX FX release evidence ledger

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
| FX-T14 | PARTIAL: offline WAV, MIDI and file worker tests; host file dialogs/transfer missing | queued / not approved / not done |
| FX-T15 | OPEN: editor only syntax checked here; DPI/runtime unexecuted | queued / N/A / not done |
| FX-T16 | PARTIAL: codec tests; full processor and project reopen unexecuted | queued / N/A / not done |
| FX-T17 | BLOCKED: historical Windows crashes; no current binary or validator evidence | queued / N/A / not done |
| FX-T18 | PARTIAL: Linux standalone allocation/stress and diagnostic timings only; Windows deadline matrix missing | queued / not approved / not done |
| FX-T19 | OPEN: complete asset/license inventory absent | queued / N/A / not done |
| FX-T20 | NOT READY: single integrated owner acceptance after candidate qualification | queued / not approved / not done |

## Current local work — 2026-11-18 (unqualified checkpoints)

Code checkpoint `5675dd20d51814f80a3c093c587e1dda3ce72ab4` adds Playback Mode/TriggerPolicy state migration (FPRG v4), MIDI NoteOff ownership, six mode choices, and 20 DSP trajectories (per-layer and global). Thirty-five JUCE-independent FX test executables compiled with `g++ -std=c++20 -O2 -pthread` passed at that checkpoint, including codec/store (maximum-size v4 payload), trajectory/render, playback-mode, family/render, pattern, modulation and sample-store primitives. Playback-mode tests also passed AddressSanitizer+UndefinedBehaviorSanitizer.

Follow-up code checkpoint `01685a2` adds delay feedback (50–500 ms, up to 75%, bounded 10 s/slot decay); the updated `FxEffectsTests` passed on this code. The other 34 standalone tests were not rerun after this change, so this is NOT a full 01685a2 regression pass. A 4-second, four-layer synthetic Transition WAV was rendered from `ee15af239e297bd655711867d018cf0b05f994c6` into the ignored local artifact `build/vox-fx-hybrid-transition-preview-normalized.wav`, peak-normalized to -6 dBFS for audition; it has no post-FX, is DSP smoke evidence only, and is not approved audio.

Follow-up P1 code checkpoint `ee15af239e297bd655711867d018cf0b05f994c6` on the current Arena source branch fixes same-note overlap with FIFO NoteOff ownership; Gate holds the final source state, Continuous free-runs a slow procedural trajectory, and Loop alone cycles with edge fades. Updated playback tests cover two same-note NoteOns/two NoteOffs, channel isolation, mode boundaries at 44.1/48/96 kHz and Continuous beyond 32 seconds. `FxPlaybackModeTests`, `FxEffectsTests` (including delay-bank reuse), and `FxNoiseRiserTests` passed on the source tree committed as this checkpoint; playback tests passed ASan+UBSan. This targeted check is not a full suite.

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
