# VOX FX — consolidated implementation handoff, 2026-11-19

**Keep #156 Draft / HOLD.** This is a development batch, not an installable release or an audio-quality approval. Verified starting refs: PR #156 `f042f955aee724636037471647a7c3be664e18bc`; assigned branch `arena/20b960e7-vox-electronic-engine` at `b19b7f60258d43ff549cd4ec92cb1dfe9aad0f69`. Tested implementation: `cdd138f54a85ba9c1e974a2e7f5d258a971d46c1`. Subsequent documentation does not alter that tree's product or test sources. No shared product code, CI workflow, plugin identity or automation IDs changed.

## Implemented as end-to-end slices

- **Trajectory authoring and Generator:** Both Layer and Program graphs use shift-click group selection, group move/delete and segment shape, keyboard movement, optional 1/16 snapping (Alt disables it), validated whole-curve clipboard, and numeric node entry (Enter). Endpoints remain pinned; numeric commits, clipboard and undo share the existing revision guard. Generate preserves authored Program curves, bars and playback mode, adds layer trajectory shaping, and Mutate honors four curve locks. FGEN v3 adds pitch/cutoff lock bits without changing FPRG; v1/v2 full-layer masks migrate to the new full-layer lock, and partial masks retain their meaning.
- **Drive and stress:** Gain compensation is inside each Drive slot, not a master limiter. Amount zero is neutral after smoothing; the old ceiling-normalized high-gain transfer repeatedly magnified and flattened the same input across the four slots. The new wet transfer blends input with `tanh((1+7a)x)/sqrt(1+7a)` by amount `a`; existing slot Wet and amount automation keep their IDs/ranges. This intentionally changes the sound of previously authored Drive chains. No oversampling or alias-quality claim. Stress also exercises the granular engine across 44.1/48/96 kHz, blocks 64/128/512 and 1/4/8 voices with the allocation probe and both-channel finite/peak/DC checks, including 64 shared grains.
- **WAV and sample workflow:** Choose 44.1/48/96 kHz, PCM24/float32 and audition note/velocity. Sequence captures the saved pattern and uses the same fixed scheduler as live PPQ, plays one cycle at the captured tempo, then drains pending voices and effects. Empty Sequence and unsupported tempo reject before replacing the output. Other modes export one bounded pass. The asynchronous Relink verifies the original SHA-256, preserves the old reference and ready generation on mismatch, and refuses a chooser aimed at a superseded reference. Decode and file writes remain non-RT.
- **Review library:** Eight immutable, hand-authored recipes with IDs, revisions, roles, style/mood, tempo, mode and audition metadata, in `products/fx/content/FactoryCandidates.h`. They are separate from the procedural Seed selector and are not shipped as an approved catalog. Wave A/B/C remain 120/300/600; no requirement was reduced.

## Scope and acceptance ledger

All executable results below are for `cdd138f` on Linux except the baseline Drive comparison. “Local” is not release acceptance. Audio column refers to review renders, not a listening verdict.

| FX scope | Requirements | Implemented / executed locally | Audio candidate | Missing |
| --- | --- | --- | --- | --- |
| FX-01 | T01, T15, T16 | 12 family regressions; processor/editor source compiles against pinned JUCE | family mixtures | executable native/host state and UI/DPI validation |
| FX-02 | T04–T07 | Playback ownership, host-bar engine matrix, tempo changes, clock loss, pattern scheduler/SMF and transport tracker tests | all 6 modes represented; held/loop renders are finite auditions | host try-out for stop/seek/loop and meter, key/root/scale, sustain pedal |
| FX-03 | T02, T03, T10 | Revision-safe scoped curve/store tests, curve DSP and codec migration, linked sample worker with digest/lease/relink tests | multi-layer recipes at all 3 rates | typed dual filters, source controls/smoothing, native gestures and host restore |
| FX-04 | T08, T09, T18 | route, FX, tail and Drive stress regressions; granular 27-case allocation/finite/peak/DC matrix | Drive used on impact and zap | full sources/destinations and native matrix; sync, anti-alias/listening, random MIDI/lock instrumentation, Windows deadlines |
| FX-05 | T11–T14, T19 | Generator/lock migration, scheduler/SMF, byte-exact offline Sequence parity, linked file worker, factory-review renders plus hashes | eight distinct review recipes, no external assets | complete generator and persistent A/B, Factory/User pack/browser and provenance approval, pattern editing history, export/drag in hosts, 120/300/600 curation |
| FX-06 | T15, T16 | Native grouped trajectory/WAV controls compiled; standalone state/store rollback tests | preview workflow; no GUI audio run | runnable editor, complete workstation, closed-editor project restore, focus/DPI/accessibility qualification |
| FX-07 | T17, T20 | No Windows build/validator or DAW run was requested for this incomplete batch | none approved | exact-head Windows suite, validator, hashes/installer, TL release and final owner-controlled acceptance |

## Executed evidence

**39 optimized standalone executables** (36 FX tests and 3 diagnostic benchmarks) plus **3 runnable pinned-JUCE-linked worker tests** passed. Selected **10 ASan/UBSan regressions** passed; the **ProgramStore TSan** regression passed. Pinned-JUCE processor/editor/test syntax checking passed. Full Linux CTest and executable processor/editor remain blocked at CMake configure. The [retained transcript](evidence/implementation-20261119.txt) includes compile/run commands, regression and sanitizer results, the configure failure, before/after measurements and all audio hashes.

## Four serial Drive slots: reproduced, then corrected

Same benchmark source workload: four-bar 32nd-triplet pattern with 4 ratchets/step at 1000 BPM, eight initial four-layer Shepard events, 400 × 128-sample blocks and four fully wet Drive slots at amount 0.7. Harmful samples defined as `abs(sample) >= 1`, unchanged before/after; new automated assertion also requires peak between 0.01 and 0.9 (not silence). Full-wet amount 1.0 unit fixture checks nontrivial waveform change, peak under 0.45 and unity at zero after smoothing.

| Rate | Baseline `b19b7f6` left peak / full-scale samples | New left peak / full-scale samples |
| --- | ---: | ---: |
| 44.1 kHz | 1.000 / 38,558 | 0.278 / 0 |
| 48 kHz | 1.000 / 38,297 | 0.278 / 0 |
| 96 kHz | 1.000 / 38,200 | 0.278 / 0 |

Bench timings and measurements are in the transcript. Environment: sandbox Linux x86-64, Intel Xeon 2.60 GHz, GCC 12.2; concurrent processes can contaminate timing. No Windows timing measured, and no Linux deadline pass asserted.

## Review counts and recipe coverage

| Role | Original candidate | Current candidates / owner-approved | Final target |
| --- | --- | ---: | ---: |
| Impact | Iron Drop | 1 / 0 | 110 |
| Riser | First Light | 1 / 0 | 95 |
| Fall | Blue Hour | 1 / 0 | 65 |
| Sweep | Salt Air | 1 / 0 | 70 |
| Zap | Acid Spore | 1 / 0 | 105 |
| Rhythm | Neon Fault | 1 / 0 | 75 |
| Atmosphere | Root System | 1 / 0 | 55 |
| Stinger | Sun Glass | 1 / 0 | 25 |
| **Total** | | **8 / 0** | **600** |

24 renders exercised the existing bounded WAV renderer at 44.1/48/96 kHz. The review test checks non-silence (`peak > 0.0001`), no full-scale clipping (`peak < 0.98` plus renderer rejection), `abs(channel mean) < 0.01` and repeat bytes at 48 kHz. The 8 exported PCM24 files have different SHA-256 hashes (exact duplicates only, not a perceptual duplicate audit). They are not loudness-normalized or approved. No third-party audio files in these recipes; release distribution rights/owner approval remain unrecorded. `build/fx-cdd138f/review-wavs.zip` packages all 8 local files; SHA-256 `b54797c01fa819a0035cd1c91807be076075a1932efb8713336fbb8f85a9a968`. Rebuild on this SHA with `g++ -std=c++20 -O2 -pthread -Iproducts/fx tests/suite/FxFactoryCandidatesTests.cpp -o /tmp/vox-fx-review && /tmp/vox-fx-review ./build/review-wavs`.

## Genuine external blockers and handoff

1. **P0 native qualification:** `cmake -S . -B build/fx-native-suite -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF -DVOX_STRICT_DEPENDENCIES=ON -DFETCHCONTENT_SOURCE_DIR_JUCE="$PWD/build/juce-fx"` fails in pinned JUCE's `JUCEModuleSupport.cmake` because the environment lacks `pkg-config`; native Linux GUI development headers are also absent. The linked core/cryptography tests do not execute the native processor/editor.
2. **P0 Windows evidence:** no Windows runner session for this new source SHA, no validator and no historical Windows exception trace. Reproduce on the Windows x64 CI/runner with stack/exception evidence; heap changes alone do not establish the historical cause. Do not trigger per-commit Windows CI.
3. **P1 listening and release sign-off:** all eight recipes still require independent audio review, then larger curated waves and distribution approval. Final Cubase 12–15/REAPER acceptance remains owner-controlled and has not begun.

**Next unfinished package:** execute the native processor/editor suite in a provisioned environment, then continue typed source/filter and musical key controls with migrations and functional UI; in parallel finish the real deployable preset library/user-bank schema and A/B persistence before expanding the curation waves. Do not infer release readiness from this batch.
