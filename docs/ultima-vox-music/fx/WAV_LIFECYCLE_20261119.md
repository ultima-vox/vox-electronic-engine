# VOX FX — WAV export lifecycle and timeline follow-up

Development source: `ebadc56e8ec0114ee581eeb5ee38f66ef9667342`. Follows the [timeline/export checkpoint](TIMELINE_EXPORT_20261119.md) on Draft #156. **HOLD / NO MERGE.**

## Behavior

- The processor owns the confirmed job, so closing or recreating an editor does not discard it. Only the message thread can start it, poll the status, request cancellation, or reap completion. The worker owns its capture and uses atomic status/progress; it does not call the processor or editor. Host project save does not serialize the job or join its worker. WAV rendering and file writes stay off the audio callback; the editor still captures state and runs the save picker on the message thread.
- A successful cancellation request before replacement prevents publication and preserves the original target. Once the worker starts replacement it rejects cancellation and reports the real filesystem outcome. An early failure also seals the job against late cancellation. The worker checks the closed temporary's size before publication and releases the captured sample before reporting completion. The processor destructor cancels and joins; it does not promise a bounded unload time if the OS blocks in a filesystem syscall.
- Clicking a curve node without moving it no longer creates an undo transaction or invalidates redo. `[` / `]` move selection and reveal offscreen nodes without relying on OS Alt+Tab behavior. The curve parameters, state codecs and 19 host parameter IDs did not change.

## Development checks

Pinned JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8` using the [native Linux harness](evidence/run-linux-native-fx.sh) with a 1 MiB test stack: both `FxProcessorTests` and `FxRuntimeStressTests` exited 0. `FxFileExportTests` exited 0 plain and with ASan+UBSan on the test/worker code (linked JUCE objects were not instrumented). The separate preset-library check exited 0. All seven measured capture parity cases reported zero sample error.

- `FxFileExportTests`: existing-target success with byte-identical reference WAV, pre-cancel and deterministic cancel after the first 16 KiB chunk, temp cleanup, directory collision, missing parent, invalid capture, and cancel rejection after commit starts or a failure seals. Worker success/failure and cancellation are tested without driving an OS picker.
- `FxProcessorTests`: immutable bytes despite subsequent edits and repeated editor recreation, single-shot completion, byte-identical host-thread save while pending, non-message-thread control refusal, cancellation preserving a target, processor destruction before publication, bracket reveal and click-only redo preservation. The existing seven render-capture cases still compare against the measured callback PCM fixtures.
- `FxPresetLibraryTests`: existing preset library and preview checks, including portable preset file behavior. No Program identity or preset-format change in this checkpoint.

## Linux callback timing — diagnostic only

With `VOX_FX_TIMING_THREAD_CPU=1 taskset -c 0`, the two stress profiles cover 44.1/48/96 kHz × 64/128/512 × 1/4/8 initial voices, with and without Pattern, 160 blocks per case. Both use granular voices, four Drive slots and 64 routes; the authored version also exercises authored grain shapes and event modulation. The typical profile uses one curated riser without Pattern. Across 18,720 callbacks the scoped allocation and wrapped-lock counters were zero.

| Profile | Cases | Wall / CPU misses | Absolute max wall / p99 / CPU (µs) | Worst relative max wall / p99 / CPU |
|---|---:|---:|---:|---:|
| [Routed stress](evidence/wav-lifecycle-rt-stress-20261119.txt) | 54 | 9 / 0 | 5251.05 / 4171.75 / 2209.37 | 677.67% / 218.99% / 91.55% |
| [Authored stress](evidence/wav-lifecycle-rt-authored-20261119.txt) | 54 | 7 / 0 | 18299.21 / 5942.47 / 4578.38 | 573.41% / 211.19% / 58.32% |
| [Typical](evidence/wav-lifecycle-rt-typical-20261119.txt) | 9 | 0 / 0 | 231.14 / 225.38 / 231.74 | 6.15% / 5.75% / 6.22% |

Columns take independent maxima across matrix cases. The routed 96 kHz / 64 / 4 miss reached 4517.82 µs wall and 610.34 µs thread CPU against 666.67 µs. The largest absolute miss was authored 44.1 kHz / 512 / 4: 18299.21 µs wall and 4578.38 µs CPU against 11609.98 µs. Functional stress success does not waive a deadline miss. The measurements do not isolate the source of wall delay or establish a realtime guarantee.

## Still open

The consolidated exact-head Windows Release x64 full CTest, timing and Steinberg validator run still needs counts/skips, logs and binary/artifact SHA. It must be followed by the owner Cubase/REAPER pass. Native save picker, focus and HiDPI, Windows overwrite/permissions/disk-full and blocked filesystem behavior, host unload latency, and online/offline bounce remain unqualified. FX-T18 stays open; prior measured wall-only misses can underrun even where thread CPU fits. No intermediate Windows qualification run was dispatched.

[Wave B bridge signals](WAVE_B_BRIDGE_SIGNALS_20261119.md) are separate review-only content, not installed factory presets.
