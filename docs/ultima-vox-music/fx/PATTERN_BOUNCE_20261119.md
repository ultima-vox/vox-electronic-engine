# VOX FX — Pattern authoring and host bounce isolation

Development source `21422001dec206d4162c5ebbaf789c786242eaae`, following the development-only [WAV lifecycle review](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7010956384). **Draft / HOLD / NO MERGE.**

## Changes and compatibility

- The Pattern **Edit…** menu can copy all pages of the selected bar into an editor-local buffer, then paste it into a bar on the **same** grid. Note, velocity, probability, ratchets and enable state survive. Paste is one undo transaction; identical, invalid and cross-grid pastes do not add a transaction or change the Pattern. The destination seed and swing are unchanged, so copied probabilities are evaluated at the destination positions. The buffer does not enter project state. Whole-Pattern clipboard operations are unchanged. Bar and page selectors stay valid after shortening a Pattern or changing grids, and step labels use bar-local step numbers across pages.
- Pattern and Sequence show why clock admission failed: no playhead, stopped transport, invalid PPQ/tempo, unsupported meter, pending coherent state, or a block rejected by the scheduler. Missing signature retains the existing Pattern-only 4/4 fallback and displays it explicitly. A reported non-4/4 meter still rejects Pattern playback. The fallback does not change the explicit-4/4 requirement for bar-aligned transitions. Pattern timing remains PPQ-zero based at 10–1000 BPM with one tempo snapshot per block; MIDI export remains a fixed-tempo trigger clip, not a tempo map.
- Non-realtime callbacks now drop live synth audition voices (including retirement slots) and reject audition starts while processing offline, in addition to discarding browser previews and bypassing Solo. Preview leases and grain slots are released before MIDI/Pattern admission can reuse their slots. MIDI and Pattern voices are not discarded. The change does **not** purge existing shared post-FX history, undo voice stealing by a prior live audition, or guarantee sample equality across arbitrary in-place host bounce transitions. Effect-tail behavior when a DAW bounces on an already-running instance remains a host check.
- No state section/schema, portable preset identity, 19 host IDs, or canonical trigger scheduling rules changed.

## Linux development checks

Pinned JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`, [native harness](evidence/run-linux-native-fx.sh) with 1 MiB test stack:

- `FxProcessorTests` and `FxRuntimeStressTests` exited 0. Native tests drove the bar menu and verified 48-step paste, undo/redo, no-op/cross-grid behavior, bar/page selection, and clock-loss/recovery. Bounce tests verified removal of active dry audition/grains, suppression of queued synth/browser audition, sample-exact left/right output against an unmonitored reference with Pattern plus offset MIDI, and continued parity at 512→128 samples with tempo/output changes and multiple Solo bits. The existing WAV/callback parity assertions remained 7/7 sample-exact. Previous WAV lifecycle and timeline regressions also ran.
- Seven standalone test executables passed: Pattern, Pattern Grid, Pattern Codec, Pattern Playback, Pattern Swing, Playback Modes, and Offline Render. Pattern Grid also passed ASan/UBSan. The focused `FxSampleGranularTests` passed with scoped allocation instrumentation and ASan/UBSan; a stolen preview's external sample lease and grains are reclaimed without dropping the eight remaining MIDI voices.

## FX-T18 — still open

Linux/KVM, `VOX_FX_TIMING_THREAD_CPU=1 taskset -c 0`: 54 default and 54 authored stress cases (44.1/48/96 kHz × 64/128/512 × 1/4/8 initial voices, with and without Pattern, 4 Drive slots/64 routes) and 9 typical cases. 160 blocks per case. No scoped allocations, wrapped mutex/rwlock calls, or wall/CPU deadline misses in these **18,720** callbacks. This sample is diagnostic and is not a realtime qualification. The prior [16 wall-only misses](WAV_LIFECYCLE_20261119.md) remain valid evidence; these runs do not identify or remove their cause.

| Profile | Absolute worst wall / wall p99 / thread CPU (µs) | Largest respective fractions of block deadline |
|---|---:|---:|
| [Routed](evidence/pattern-bounce-rt-stress-20261119.txt) | 3433.09 / 2617.64 / 2650.85 | 61.01% / 48.70% / 48.74% |
| [Authored](evidence/pattern-bounce-rt-authored-20261119.txt) | 2814.31 / 2027.67 / 2806.25 | 83.10% / 70.54% / 83.22% |
| [Typical](evidence/pattern-bounce-rt-typical-20261119.txt) | 209.79 / 203.90 / 204.40 | 8.67% / 5.11% / 8.74% |

Independent maxima, not necessarily the same callback or matrix case. Authored 96 kHz/64/1 reached 554.03 µs wall and 554.82 µs thread CPU against 666.67 µs.

## Remaining qualification

One gated exact-head Windows Release x64 full CTest/stress/Steinberg validator run with artifact and binary SHA, logs and counts/skips; then one owner Cubase/REAPER round. Windows GUI/focus/DPI, file chooser and real overwrite/locked/readonly/invalid paths/disk-full, blocked-I/O teardown and host bounce transitions remain open. The prior export destructor still joins on processor unload; blocked filesystem calls can delay it. No intermediate Windows run was triggered. [Wave B Crossings](WAVE_B_CROSSINGS_20261119.md) is explicitly review-only, with durable authorized private delivery and real-mix listening still required.
