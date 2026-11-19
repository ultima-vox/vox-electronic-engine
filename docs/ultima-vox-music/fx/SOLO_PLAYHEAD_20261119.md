# VOX FX — Layer Solo and rendered timeline cursor

Implementation and tests: `79027c7b3e815ce1c5cd99f9c3bf22b40c585032`, atop the TL-reviewed development checkpoint `a61e5005`. This is development evidence only. It does not close Windows FX-T17/T18, qualify a DAW, approve sounds, or remove HOLD.

## Monitoring and timeline semantics

- Layer Solo is a transient processor-instance monitor, independent of the structural layer ON toggle. Multiple layers can be soloed; clearing the last bit returns to the complete mix. An already-disabled layer is not made audible by Solo. Approximately 5 ms fades change layer mix contributions while existing MIDI/Pattern/preview voice clocks, grain allocation, lease ownership and per-layer filters keep running. The summed monitored signal still feeds the post FX; pre-existing tails decay normally. Rendered preset-browser preview files are a separate bus.
- No host parameter IDs, state encoding or Program fields changed. The mask survives editor close/open, is cleared by a successful project/preset restore, and is ignored in plugin WAV export and when the host declares a non-realtime bounce.
- The curve editor shows the last rendered position of the newest active voice whose snapshot matches the displayed graph. The layer editor uses the same enabled layer window and local phase calculation as DSP. The renderer is the source of truth: no message-thread clock extrapolation. Inactive windows, stale graph generations, expired voices, and host-declared offline bounces show no cursor. A structural edit or undo hides the old graph's cursor until a voice starts on the new publication; it does not restart the old voice. The live cursor is not serialized.

## Executed checks

- Pinned JUCE `FxProcessorTests`, `FxRuntimeStressTests`: both passed on Linux. The native tests exercised the two channel monitor as real DSP (procedural, filtered and unfiltered grain contributions), bounded grain/voice ownership through the audition change, multi-Solo, close/reopen, state and WAV neutrality, host bounce, seek and MIDI offset retrigger, undo and restore, queued `AUDITION` vs `STOP`, stale preset preview ticket, and release/reset of cursor and peak on releaseResources. Actual Windows GUI verification of STOP, preview and metering remains a manual gate. The seven processor↔float32 WAV captures retained exactly zero sample error.
- Host callback tests automate BPM from 84→140 without PPQ and hear a different LFO on the still-sounding seconds voice; a bar trigger without PPQ is rejected. The Transition cursor follows real PPQ at 120→60 BPM and vanishes upon clock loss. Tests use native JUCE mock playhead, not a DAW.
- `FxSampleGranularTests` checks first-grain pitch hold vs the next grain's new pitch, scalar signed-modulo reference for reverse interpolation in a seven-frame external PCM ring, and reproducible scatter at 44.1/48/96 kHz with irregular block partitions. Existing immutable sample/lease and relink tests remain separate. ASan/UBSan passed `FxSampleGranularTests`, `FxPlaybackModeTests`, `FxProgramStoreTests` and `FxCurveTrajectoryTests` (4/4).
- All **120 Wave A solo WAVs** and **24 Wave B WAVs, 24 portable presets and 2 external source WAVs** were freshly exported and verified against the staged SHA-256 manifests. Wave B ZIP was reproducible byte-for-byte from the fresh export using the new [packager](evidence/package-wave-b-review.py); SHA-256 `8cac878444bdfe8b5725142653294cdbb1cdb6ed84134d504b07910190541ac3`. Raw audio is not in Git or published through a public release. Counts remain A 120 + B 24, zero approved.

## Linux callback diagnostics with monitor at default

Used the pinned [native harness](evidence/run-linux-native-fx.sh) with GCC `-O2`, 1 MiB test stack and `VOX_FX_TIMING_THREAD_CPU=1 taskset -c 0`, without an intentional same-core competitor. 160 measured callbacks per case. These are fresh single runs of the current code, not a paired benchmark or hard RT claim.

| Profile | Cases | Wall / CPU overruns | Worst relative wall | Logs |
|---|---:|---:|---:|---|
| Max routed filtered granular + random MIDI, with and without Pattern | 54 | 0 / 0 | 70.96% | [Raw](evidence/solo-playhead-rt-max-20261119.txt) |
| Authored reverse grain / step / ADSR stress | 54 | 1 / 0 | 221.84% | [Raw](evidence/solo-playhead-rt-authored-20261119.txt) |
| Typical Firefly Ladder / 1 voice | 9 | 0 / 0 | 8.21% | [Raw](evidence/solo-playhead-rt-typical-20261119.txt) |

Zero scoped allocations and wrapped locks in **18,720 callbacks**. The single wall-only miss (96 kHz/128, no Pattern, 1 initial note) was **2957.87 µs wall / 385.13 µs CPU** against 1333.33 µs; wall p99 in that case was 428.17 µs. This is consistent with a scheduling delay, not proof of drop-out-free playback. The absolute authored maximum was 3534.08 µs wall / 3540.03 µs CPU at 44.1 kHz/512 with Pattern, below that case's larger deadline. No thresholds were changed.

Draft #156 remains at `a61e5005` with HOLD. No Windows job triggered; no change to the PR branch, CI opt-in, base branches or release status. Private durable owner handoff, listening in a real mix, the 300/600 milestones, and consolidated Windows + last-stage DAW acceptance remain outstanding.
