# VOX FX — In-place bounce history and audition priority

Development source `4352a19035bfbeb2c96da90ae46c35f325007462`; follows the [TL review of the previous checkpoint](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7011298644). **Draft / HOLD / NO MERGE.**

## Behavior and limits

- A live synth audition requires a free active voice slot; at capacity it cannot steal a MIDI or Pattern voice and does not increment the musical event-drop counter. A new musical voice uses a free slot first, otherwise retires an audition ahead of the oldest musical owner. Under saturation it reclaims an active audition or its retirement slot if necessary, sacrificing only the audition fade. Musical stealing, MIDI NoteOff ownership, and drop behavior remain as before when all slots are musical. The reclaim path only clears fixed slots and decrements sample-reader tokens; it never frees sample buffers or waits on I/O. Audition is still a live sound using the shared engine, not a separate FX graph.
- In-place offline entry still discards monitored synth/browser preview sources, not the shared delay/reverb history. It does not reset held MIDI or Pattern voices to manufacture parity. An already-running instance can have audible residual audition tails, different prior gain/voice history, and different output from a fresh bounce. Explicit `prepareToPlay` at a changed rate resets DSP history; no project/preset state, host parameter IDs, or scheduling format changed.
- The selected-bar copy buffer lasts until the editor closes. During host restore, a stale paste against the prior structural snapshot cannot overwrite the new Pattern. After refresh, a deliberate same-grid paste uses the current editor's buffer and can be undone to the restored Pattern. Recreating the editor does not recover the buffer from project state.

## Measured checks

- [Pinned Linux native harness](evidence/run-linux-native-fx.sh), JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`, 1 MiB test stack: `FxProcessorTests` and `FxRuntimeStressTests` exited 0. The run reached the new in-place bounce and clipboard phases, and existing processor/WAV comparison remained 7/7 exact.
- The in-place test compared both channels with an uninterrupted instance with **identical synth/FX history**, once at 48 kHz with feedback delay and once at 96 kHz with reverb. At offline entry the finite synth audition had ended while the wet tail was still nonzero, a browser sample lease had been active, and a new synth audition was queued. The wet tail matched exactly, browser playback and pending publication were invalidated, and queued synth audition did not replay. Tests also admit offset held MIDI, change tempo and 64/128/512 block sizes, remove PPQ and recover one Pattern trigger, leave offline mode, verify NoteOff ownership, and explicitly reprepare at 44.1 kHz. This is not a comparison against a fresh offline renderer.
- A separate processor test filled all eight held slots before requesting audition, then compared both channels during offline entry and NoteOff against a never-auditioned reference. Standalone tests also cover a newer audition preempted by Pattern, reassignment of a retiring audition slot, hard eviction of an active audition when all retirement slots are occupied, and the existing bounded/drop behavior for all-musical overload.
- Six standalone executables passed: Playback Mode, Sample Granular, Effects, Transport, Pattern Grid and Offline Render. Playback Mode and Sample Granular also passed ASan/UBSan. The granular test used the existing scoped allocation probe in the instrumented plain build.

## Linux callback timing — FX-T18 remains open

`VOX_FX_TIMING_THREAD_CPU=1 taskset -c 0`, 160 blocks per case: 54 routed and 54 authored stress cases covering 44.1/48/96 kHz × 64/128/512 × 1/4/8 initial voices with and without Pattern, four granular layers, four Drive slots and 64 routes; plus 9 curated typical cases. Across 18,720 measured callbacks, there were zero scoped allocations, zero wrapped locks, **one wall-clock miss** and zero thread-CPU misses. A wall-only miss is still a missed callback deadline.

| Profile | Wall / CPU misses | Absolute max wall / wall p99 / CPU (µs) | Max relative wall / wall p99 / CPU |
|---|---:|---:|---:|
| [Routed](evidence/bounce-history-rt-stress-20261119.txt) | 1 / 0 | 2729.84 / 2672.31 / 2731.10 | 185.45% / 79.16% / 54.79% |
| [Authored](evidence/bounce-history-rt-authored-20261119.txt) | 0 / 0 | 2547.50 / 2510.91 / 2549.15 | 47.77% / 47.08% / 47.80% |
| [Typical](evidence/bounce-history-rt-typical-20261119.txt) | 0 / 0 | 304.87 / 301.08 / 305.47 | 9.01% / 6.14% / 10.05% |

The maxima are independent across matrix rows. The miss was at 44.1 kHz / 64 / 1 without Pattern: 2691.30 µs wall, 606.16 µs CPU, 1451.25 µs deadline, wall p99 1148.81 µs. This does not attribute the elapsed-time spike to a specific cause or supersede the previous 16 wall-only misses. The matrix is a diagnostic for host callback workload; the functional tests exercise offline mode and audition capacity separately.

## Outstanding gates

No intermediate Windows CI was dispatched. The consolidated exact-head Windows x64 Release CTest, Steinberg validator, FX-T18, logs/counts/skips and VST3/artifact SHA still precede the single Cubase/REAPER owner round. Native picker/DPI and blocked/denied/disk-full/overwrite, host unload and real bounce across tails remain unqualified. See [Wave B Room Cuts](WAVE_B_ROOM_CUTS_20261119.md) for the next review-only content tranche, real-mix gain staging and the private-delivery requirement.
