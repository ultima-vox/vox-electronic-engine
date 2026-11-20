# VOX FX reconciliation and timing/export checkpoint — 19 November 2026

**TL disposition: engineering checkpoint only. Keep PR #156 Draft / HOLD.** No Windows x64 VST3 release candidate, installer, validator result, or DAW acceptance is being submitted.

## Branch reconciliation

| Ref | Verified commit |
| --- | --- |
| [PR #156](https://github.com/ultima-vox/vox-electronic-engine/pull/156), `arena/d9776bd9-vox-electronic-engine` | `f6ed6de9124e70d186542cd098c27f3d286425ba` |
| Source `arena/aba74ad9-vox-electronic-engine` | `67acc69bdfdc2b2878ddda112e0bd4b96b71e9c3` |
| Two-parent merge on `arena/20b960e7-vox-electronic-engine` | `7c79a2f09f0df4e59fe83ae0eec5ea295374bb17` |
| Implementation and test source checkpoint | `6256625ef8d59aae348be9c7991fa095a0df337d` |

Fetched both refs and deepened the shallow clone before merging; `aa65c995cb304e372c9d06a983b2189e889adc47` was their merge base. The merge was conflict-free; its first and second parents are the PR and source SHAs above. The PR's `suite.yml` and `products/fx/parameters.csv` are byte-identical through the implementation checkpoint. `[RUN_FX_SUITE]` still opts in by adding the marker to PR #156's description; draft synchronizations remain gated. No source history was rewritten, no Drums/Synth code changed, and neither base branch was merged. Arena assigned this session a branch other than PR #156's head, so **PR #156 was not updated or triggered**. The permitted handoff is to fast-forward the PR head to the verified checkpoint from a session assigned that head (or have its maintainer do so); no replacement PR is needed.

## Implemented checkpoint

| Scope | Changes and local evidence | Release gap |
| --- | --- | --- |
| Playback and timing | `Timeline.h`, `NoiseRiser.h`, `Transport.h`, `Processor.cpp`: Transition ends on the Nth host bar line at 1/2/4/8/16 bars, including nonzero event offsets, shifted grid and tempo changes. `FxBarEventTests` covers 60/100/128/140/160/200 BPM, clock loss and fixed-tempo WAV. | Real host PPQ/loop/stop behavior and unreported intra-block tempo changes; no key/scale system or musical approval. |
| Ownership and RT | Bounded FIFO MIDI tickets, including stolen and replaced voices; independent pattern/preview sources, finite internal auditions for held modes. `FxPlaybackModeTests` exercises owner pairing, retirement, panic and cycle behavior. | CC64 unsupported; host interleaving, Windows RT deadlines and complete all-modes acceptance remain unverified. |
| WAV export | `OfflineRender.h`, `Effects.h`: bounded fixed-tempo bar audition at PPQ zero, with the serial feedback tail included; no MIDI or host tempo map embedded. `FxOfflineRenderTests` checks delayed WAV audio beyond the old 0.5 s window. | DAW transfers and file-dialog/drag acceptance. No audio-quality endorsement. |
| State and native controls | Existing FPRG v4 and 19 automation IDs unchanged; status/tooltips reflect bar alignment and fixed-tempo WAV. `FxProgramCodecTests`/`FxProgramStoreTests` cover migration and snapshots. | `FxProcessorTests` (including shifted-grid MIDI and 3/4 rejection) compiled as syntax only. No JUCE runtime, project reopen or automation lane acceptance. |

See [timing/export policy](MUSICAL_CLOCK_DESIGN.md) for details and rounding, tempo, ownership and payload limits. Existing developmental `Transition` + bars states gain host alignment without a format change. v1–v3 One Shot migration is unchanged.

## Verification boundary

The merge SHA passed **35/35 JUCE-independent FX executables** on Linux x86-64 (Debian GCC 12.2.0; direct `g++ -std=c++20 -O2 -pthread -Iproducts/fx`). This includes the merged playback and effects tests. The merge-run `summary.log` SHA-256 is `0eadabac8bbe8b3e5e6ba6caba3f87a0b6fc71e569f171782282138fb31efda2`.

The implementation SHA passed **38/38 executable Linux FX regressions**, including three linked against pinned JUCE (file export, sample decode and sample controller). Those runs used a 1 MiB stack limit and the Linux allocation probe where supported. **35/35 JUCE-independent tests** passed with AddressSanitizer and UndefinedBehaviorSanitizer. Processor, editor, and processor test source passed syntax checking against pinned JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`; the runnable processor/editor and full CTest suite were not built or executed here.

[Full local transcript and commands](evidence/linux-20261119.txt), SHA-256 `869a0c2ef7b717373a0df28c2f870c5ab8e8b5888bc1cd59d2558ef81dd8021f`. The transcript records the implementation SHA; the subsequent evidence commit changes documentation only.

Local smoke audio: a repeatable 48 kHz stereo PCM24 render of a four-layer 4-bar Transition (noise, Shepard, laser, impact) at 128 BPM plus feedback delay. The two independent renders were byte-identical, 840,000 frames including the tail; measured peak 0.01836 FS, not normalized. WAV SHA-256: `6d970efc34693eb7568fda71e81c52cc09ee6eea5f74bee0fa2ca3eccd512058`. It is an engineering smoke render, not a certified preset or a listening result.

[Historical Windows run 44172558893](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44172558893) failed at CTest. Large engine objects in test stack frames are a plausible explanation for some faults, not a confirmed Windows root cause; newly merged multi-engine playback/trajectory fixtures now allocate engines on the heap. Later [run 44376476136](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44376476136) also failed at CTest, and the log download was unavailable here. The [latest run for the unchanged PR head](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44459630663) was skipped by design. None is qualification for either checkpoint SHA. This session did not run Windows or the Steinberg validator, and there is no VST3 binary/package hash.

## Release ledger for TL review

| Gate | Engineering evidence | Open release acceptance |
| --- | --- | --- |
| FX-T01 | Twelve standalone family tests | Approved family and style references |
| FX-T02 | Layer/trajectory/lease tests; four-layer smoke render | Source/filter editing, headroom and musical review |
| FX-T03 | FPRG v4 codec, v1–v3 migration and 20 trajectories | Global editor, undo/readback, host restore |
| FX-T04 | Held lifecycle, mode cycles, pattern scheduling and host-bar target engine tests | Executable plugin and listening evidence |
| FX-T05 | Cross-channel and duplicate FIFO, steal/replacement, panic and stress tests | Executed processor CC/velocity-zero and host tests |
| FX-T06 | 1/2/4/8/16 × six-BPM matrix, mid-event tempo changes, clock loss | Host loop/seek/bounce/meter and real transport testing |
| FX-T07 | Existing MIDI-pitched families | Preset-local key/root/scale and tonal acceptance |
| FX-T08 | Existing macro/routing tests | Complete modulation sources/destinations and DAW automation |
| FX-T09 | Effects and repeating delay-tail tests | 4×Drive headroom, anti-aliasing, sync, controls |
| FX-T10 | WAV/hash/immutable lease tests | TSan, native relink and asset recovery |
| FX-T11 | Existing generator/lock and state tests | Full Program generation, persistent A/B and factory/user flow |
| FX-T12 | 48 procedural seed programs and 12 patterns; **0 documented approvals** against cumulative waves **120 / 300 / 600** | Curate, audition and audit each wave; no generated filler |
| FX-T13 | Pattern scheduling/codec/SMF tests | Runtime host parity, looping and DAW transfer |
| FX-T14 | Fixed-tempo WAV, feedback tail, capture and worker tests | Executable editor and native file operations |
| FX-T15 | Pinned JUCE syntax check only | Native workspace, resize/accessibility, DPI and binding runtime |
| FX-T16 | Standalone codec/state-store regressions | Complete host restore, rollback and missing-asset UX |
| FX-T17 | No candidate Windows run | Exact-SHA Windows x64 VST3, CTest, Steinberg validator, package and hashes |
| FX-T18 | Linux engine probes and ASan/UBSan | Hardware-defined Windows deadline and full RT matrix |
| FX-T19 | Smoke render uses no external assets | Factory catalog provenance and license inventory |
| FX-T20 | Not started, intentionally | Final one-time Cubase 12–15/REAPER acceptance |

**Remaining integration order:** (1) transfer the tested history to PR #156 and qualify the current processor/editor and historical crash cases on Windows, preserving the explicit gate; (2) complete source/filter, modulation/FX/headroom, preset/state and native workstation work; (3) curate the 120 → 300 → 600 waves against a licensed manifest and listening references; (4) qualify an installable exact-SHA Windows candidate, archive suite/validator logs and package checksums, then submit to the TL and reserve manual DAW acceptance for that final stage.
