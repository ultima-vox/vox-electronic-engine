# Host bar clock and measured FX tails

**Draft / HOLD / NO MERGE.** Source `09f4d4e755076282d263ca909ff33549ff3961c7` builds on TL-integrated `86a8e38`. No new controls pretend to alter DSP: this checkpoint adds measured tail feedback and explicit host-clock handling at the native processor/editor boundary. It changes neither the 19 parameter IDs nor state format, Create-only Save, drag ownership, or the frozen 232 review-only / 0 approved catalog.

## What changed

- **Bar clock:** Bar modes require a playing host, explicit 4/4, finite 1–1000 BPM and valid PPQ. The processor uses the same validated `BarGrid` for bar admission and the editor status, so invalid PPQ/origin cannot appear ready. Missing bar origin (not a supplied invalid one) falls back to a PPQ-zero grid; the editor says so for Transition. This does not change the existing Pattern policy or invent time for seconds-mode notes. On clock loss, existing bar voices release; newly clocked playback does not resurrect them. `BarGrid` uses ±1,000,000 PPQ and an 8–384 kHz prepared rate. Its in-block target calculation and the engine's 5 ms forced retirement are unchanged.
- **Tail indicator:** `FX TAIL` is a record of measured live FX-only output, not a prediction, per-slot meter, or time remaining. It requires no active voices at block end, an entirely zero dry block, and post-chain/master output at or above −90 dBFS **before** browser preview mix. The message thread holds a real detection briefly so sparse echoes are visible. In-place bounce still keeps the shared FX history but does not light the live indicator. There is no new render buffer or callback allocation/lock; bounded short-circuit dry and post-chain checks run only on eligible live blocks, not the dense active-voice path. Test the additional quiet-block work under the later loaded gate.
- **Routing and state:** The serial Delay + Reverb fixture checks that a dry follower → second-slot wet route changes the measured tail versus the same unmodulated chain, without writing host lanes or changing serialized state. This does not qualify nonlinear sound quality, all route permutations, or advanced per-effect controls.

## One host procedure, once authorized

On the **same exact-head Windows binary** used by the technical gate: load a tonal 4/8/16-bar Transition and trigger partway into a bar at the supplied matrix tempi (60/100/128/140/160/200 BPM). Change tempo partway through, verify dry cutoff against the next target bar using bypass, then re-enable a routed Delay/Reverb chain and verify wet playback continues. Compare live and offline at matching history and document the fresh-start case separately. Observe clock/diagnostics for stopped, seek/loop, unsupported/missing meter, no PPQ, and recovery, without claiming a missing status unless that host actually reports it. Verify status readability, keyboard/scroll ownership, focus, high DPI and project reopen; check the restored Program, routing, automation, and a real mix. Keep capture evidence (version, fixture, settings, observed locations and hashes) separate from preset approval/media. Do not dispatch or add an opt-in as part of preparation.

## Windows preflight status

Intended Windows runner access is not available from this Linux session. The runner inventory API returned HTTP 403 (resource not accessible by integration); do not treat it as evidence that the runner is offline. Local review of pinned JUCE CMake confirms the source layout is built from `JUCE_PRODUCT_NAME` and `x86_64-win`. That, and the existing three-name static check, are **not** an actual Windows bundle inventory. The intended runner operator should follow the [read-only PowerShell 5.1 procedure](WINDOWS_ENGINEERING_GATE_20261119.md#read-only-preflight-before-requesting-authorization) and supply the exact checkout’s AST/negative fixture receipt plus a listing of any existing exact-source artifacts. Mark absent artifacts as not built/available, never as layout pass. No change to the preflight scripts, workflow eligibility, Draft or full-suite authorization.

## Local evidence and exact test inventory

- [Optimized native processor/export/stress transcript](evidence/transport-tail-native-20261119.txt): all three exited 0; **18 dry Transition cases** (three lengths × six initial BPMs, shifted anchor/negative preroll, variable blocks, a nonzero MIDI offset and a −25% tempo change). Dry output stopped at the computed in-block target sample. The longest 16-bar case ran 578,667 frames at 8 kHz (~72.3 seconds), beyond the seconds parameter's 32-second cap. Bar-clock rejection/recovery, routed and unrouted tails, preview exclusion, no live indicator during bounce, unchanged serialized bytes during playback, and all **seven sample-exact WAV comparisons** passed. The tail fixture measured 63 qualifying blocks, peak 0.018944 and summed sample difference 0.041900 versus the unrouted chain. The fixture uses real wet/amount parameter bases with headroom (a fully wet base would clamp a positive route and mask its effect).
- [Standalone inventory](evidence/transport-tail-inventory-20261119.txt): **45/45** additional optimized FX executable runs; inventory cross-checked with `tests/suite/CMakeLists.txt`, for **48/48** Linux FX executables in total. The standalone run used byte-identical standalone inputs at `03e8446`; only `FxProcessorTests.cpp` differed in the final test-source commit, and the corrected processor test was rebuilt on final source. [Native executable hashes](evidence/transport-tail-binaries-20261119.txt). Linux source inventory is not the Windows CTest or VST3 validation receipt.
- Reproduce: `VOX_FX_TIMING_THREAD_CPU=1 taskset -c 1 bash docs/ultima-vox-music/fx/evidence/run-linux-native-fx.sh build/fx-converge/native`, then `taskset -c 0 bash docs/ultima-vox-music/fx/evidence/run-linux-fx-standalone-inventory.sh`. Runs use pinned JUCE, Linux GCC 12.2 and a 1 MiB thread stack limit. No Windows process or qualifying DAW was run.

### FX-T18 remains open

Single paired diagnostics on Linux, CPU 1. Before uses binary built from `6d8513e` (the accepted `86a8e38` added evidence/docs only); after uses `09f4d4e`. Each raw log preserves **all** deadline-miss rows. Loaded runs use a CPU-1 `yes` contender, not a DAW or Windows qualification. All rows have valid CPU pairs, zero zero-deltas, zero scoped allocation and wrapped lock hits. There is no speedup claim from one order-sensitive pair.

| Capture | Callbacks | Wall misses | CPU misses |
|---|---:|---:|---:|
| [Before routed](evidence/transport-tail-before-routed-20261119.txt) | 8,640 | 0 | 0 |
| [After routed](evidence/transport-tail-after-routed-20261119.txt) | 8,640 | 0 | 0 |
| [Before loaded](evidence/transport-tail-before-loaded-20261119.txt) | 8,640 | 428 | 0 |
| [After loaded](evidence/transport-tail-after-loaded-20261119.txt) | 8,640 | 295 | 0 |
| [After authored](evidence/transport-tail-authored-20261119.txt) | 8,640 | 0 | 0 |
| [After typical](evidence/transport-tail-typical-20261119.txt) | 1,440 | 0 | 0 |
| [After native harness, concurrent standalone work on CPU 0](evidence/transport-tail-native-20261119.txt) | 8,640 | 9 | 0 |

96 kHz/64-frame Pattern still missed under load: a one-voice cell at block 84 recorded **8,217.143 µs wall / 228.205 µs thread CPU vs 666.667 µs**. This does not erase the [historical CPU overruns](ENGINEERING_DELTA_20261119.md#local-regression-and-rt-evidence) or qualify exact-head Windows loaded timing.
