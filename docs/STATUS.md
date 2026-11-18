# Development status — ULTIMA VOX MUSIC standalone VST3

**Snapshot:** 2026-11-17, checked against GitHub `ultima-vox/vox-electronic-engine`. Re-query live PR heads and CI when resuming work. This supersedes the Rack-first status snapshot of 2026-10-26 (retained in Git history).

## Product order and boundaries

1. **VOX Drums:** primary active development and first release candidate.
2. **VOX FX:** subsequent independent MIDI-triggered FX instrument, not an audio insert effect or nested Rack plugin.
3. **VOX Trance Synthesis (VOX Synth):** subsequent independent synthesizer, with Bass/Acid donor DSP and further approved scope.

All are Windows x64 VST3, Cubase 12–15 / REAPER target, JUCE 9 / C++20, no shared Rack host. Three frozen identities are recorded in [IDENTITY.md](ultima-vox-music/IDENTITY.md). Existing `main` still contains legacy Rack material; don't treat `main` as the standalone Drums code baseline.

## VOX Drums actual integration checkpoint

- **Primary branch and PR:** `integration/vox-drums-1.0` / [#141](https://github.com/ultima-vox/vox-electronic-engine/pull/141).
- **Exact checked HEAD:** `f9d5446623a558410b0af4652aabf1e918899bec`. The role/style/meter/generator/factory/listening/Trap/gain commits from stacked #143–#150 are in the history; MIDI drag fix #142 was merged in `f9d5446`.
- Existing `products/drums/` includes a 16-pad Synth/Sample/Layer instrument, step and Song 16/32/64-bar editing and MIDI export, generator and style model, presets/state/undo, 9 host-visible stereo output buses and initial JUCE editor. Not all are manually host-qualified.
- The **interim eight-page editor is not the approved release UI**; [#127](https://github.com/ultima-vox/vox-electronic-engine/issues/127) requires a native hardware-inspired dockable workstation, with [#135](https://github.com/ultima-vox/vox-electronic-engine/pull/135) visual-only reference and #130 §14 pre-release state simplification.
- [#151 StyleLibrary](https://github.com/ultima-vox/vox-electronic-engine/pull/151) `1757eec5` remains separate Draft. CI suite succeeded on this branch, but source review found a P1 in Replace and P2 in direct text bounds and duplicate UUID handling; [review](https://github.com/ultima-vox/vox-electronic-engine/pull/151#issuecomment-6927775544).

## Exact Windows CI evidence and current blocker

| Commit | Workflow | Verified result | Interpretation |
| --- | --- | --- | --- |
| `5e1cf357` (#150) | [Windows build](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43805453493) | SUCCESS | Old uncombined #150 tree |
| `5e1cf357` (#150) | [Windows suite](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43805453483) | SUCCESS: 32 serial + 8 gain; validator reported 47/47 per bundle | Gain and tests for #150, **not final integration** |
| `f9d544662` (#141) | [Windows suite](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43943639327) | FAILURE due to runner shutdown/cancellation at 2026-11-16 20:16 UTC after 16/32 serial tests | No assertion failure observed; remaining serial, gain and validator not executed |
| `f9d544662` (#141) | [Windows build](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43943639334) | QUEUED when checked | Not a PASS |
| `1757eec5` (#151) | [Windows suite](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43942191194) | SUCCESS on isolated Draft | Does not resolve independent source review defects |

**Immediate gate:** diagnose self-hosted Windows runner shutdown/service/competing jobs and rerun the **unchanged integrated SHA**. A canceled job is not an audio regression, but it also is not a green integrated candidate. Preserve logs and hashes.

## Open product/release gates

- Apply P1/P2 fixes in #151, retest and incorporate into integrated Drums; do not rebuild StyleLibrary.
- Deliver approved native JUCE UI #127 with genuine controls and dockable layout; correct pad select/audition, context and host automation, persistent layout state; produce real JUCE captures at stated sizes and DPI.
- Confirm style/music quality by actual listening before release; factory acoustic samples need per-file redistribution/provenance (Rock/Metal remain not release-ready).
- Audit 96 kHz callback timing, RT safety, state/undo, sample ownership, factory sample-peak bounded matrix, routing 9 buses, MIDI drag and one-pass Song export.
- Run one final combined Windows CTest + VST3 validator + artifact hashes on exact release candidate, then **one owner Cubase/REAPER manual acceptance**. Green CI is not sound or host acceptance.
- No merge to `main`, no branch deletion and no release-ready claim until gates are closed.

**Handoff:** [DEVELOPER_HANDOFF.md](DEVELOPER_HANDOFF.md). This report is a dated observation, not a promise about future CI state.
