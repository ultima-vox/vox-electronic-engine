# VOX FX — export lifecycle follow-up and gain-staging matrix

Follow-up to [TL review 7030552725](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7030552725). **Development only. Draft / HOLD / NO MERGE.** The underlying [shared export and pinned listening archive](FILE_EXPORT_AND_ARRANGEMENT_REVIEW_20261119.md) remains the previous integrated checkpoint.

## Lifecycle and host fixtures

- SMF encoding checks cancellation before scheduling and after each complete event. A stopped encoder returns before the temporary file is opened. The default serializer still produces the same Type 0 bytes; no changes to WAV DSP, the state schema or the 19 host parameter IDs.
- Both export actions use one asynchronous picker ticket. While it is open, both entry points are disabled immediately. Cancellation re-enables controls; stale, duplicate and destroyed-editor completions cannot start a job or release a newer chooser. Confirmation uses the pre-picker snapshot. Native offscreen coverage injects the picker callback; it does not launch a native Windows dialog.
- The processor test holds the MIDI worker after its first encoded event. A separate host thread must return from state save while the worker is still held and the old destination still exists. The export then completes byte-exactly after editor recreation and Pattern edits; the serialized state excludes the job. The file-worker test cancels a separate maximal Pattern at the same checkpoint and verifies that no temporary file exists and the old file survives.
- The pressure regression exercises the eight active owners plus four retirement slots at 44.1, 48 and 96 kHz, including granular sample relink, audition-first reclaim, Pattern/MIDI collision, true musical overflow, FIFO NoteOff and stop/restart. The wet-history test verifies there is wet signal but no active voice immediately before an explicit rate change (48→44.1 kHz for delay, 96→44.1 kHz for reverb). That prepare resets the DSP; an online/offline mode change without prepare retains matched history. Neither establishes fresh-bounce parity.

## Linux evidence

Source tested: `b4ba2db92b164e8f29932e59b5ee7c37f602efe7`, pinned JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`, Debian GCC 12.2 x86-64. The [offscreen native run](evidence/export-followup-native-20261119.txt) passed processor, stress and file-export tests with a 1 MiB test stack. All seven hosted/WAV capture comparisons stayed sample-exact. The focused Pattern, grid, codec, playback and offline render tests also passed. The file-worker fixture passed both plain and ASan/UBSan builds (prebuilt JUCE not instrumented).

Timing used `VOX_FX_TIMING_THREAD_CPU=1 taskset -c 0`: routed and authored profiles each exercise 44.1/48/96 kHz × 64/128/512 × 1/4/8 voices, with and without Pattern. All cases had 160 callbacks. The stress assertions passed across all profiles; this does **not** mean deadlines all passed.

| Profile | Callbacks | Wall / thread-CPU misses | Absolute max wall / wall p99 / thread CPU (µs) |
|---|---:|---:|---:|
| [Routed](evidence/export-followup-rt-routed-20261119.txt) | 8,640 | 2 / 0 | 3418.24 / 2851.57 / 2852.92 |
| [Authored](evidence/export-followup-rt-authored-20261119.txt) | 8,640 | 0 / 0 | 2931.27 / 2742.53 / 2932.70 |
| [Typical](evidence/export-followup-rt-typical-20261119.txt) | 1,440 | 0 / 0 | 315.85 / 279.87 / 283.15 |

The two missed callbacks were 96 kHz / 64 with Pattern, at 4 and 8 voices (965.08 and 1038.57 µs wall vs. a 666.67 µs deadline; 395.52 and 411.14 µs thread CPU at those same callbacks). Across 18,720 callbacks the probes recorded zero scoped allocations and zero wrapped lock calls. Earlier wall misses are not superseded, and no cause is assigned to the timing gap. **FX-T18 remains open.**

## Working arrangement matrix

Use the [working matrix](evidence/fx-review-176-working-matrix.tsv) as the **single editable review sheet** for the 176 pinned references. It keeps the existing placement, provenance, paths and decision fields and fills in the missing active RMS measurements. The older matrix is a frozen member of the verified ZIP, not a separate copy to review or edit. The ZIP and raw WAVs were not modified or repackaged.

| Wave | SHA-verified solo WAVs | Active RMS range (dBFS) | Below −60 dBFS |
|---|---:|---:|---:|
| A | 120 | −76.384 to −41.799 | 39 |
| B | 56 | −75.062 to −43.430 | 24 |

Active RMS uses 20 ms stereo windows within 30 dB of the strongest window; `active-window-s` is their aggregate duration, not a contiguous range. This is not LUFS or a recommendation to add a set amount of gain. The −60 dBFS flag only helps sort the gain-staging workload. Start with Tangent Iris, Glass Estuary and Dust Shutter, at a fixed monitor setting in owner-provided arrangements. Record the private mix reference, actual FX trim, mix peak, intended placement and mono masking before a reviewer assigns a decision. Do not normalize or overwrite the raw references.

The [audit script](evidence/measure-fx-review-levels.py) checks each raw WAV against the ZIP matrix before publishing a row. Two runs with NumPy 2.2.6 produced the same working matrix SHA-256 `5c0dd88e916a8c403b00ce823f5cd296f7b71713f1260e2b601b25712307d80a`; a corrupt WAV was rejected without emitting a matrix. Previously published B measurements match at their published precision.

## Remaining gates

- **176 candidates, zero approvals.** 124 short of 300 and 424 short of 600. No new cues in this checkpoint. Group subsequent individually authored cues into larger tranches; require per-cue intent, unique ID, source/portable/render fingerprints and deterministic rerender before adding them to this review inventory. Approval still requires arrangement listening.
- **Private delivery is blocked on owner authorization.** Need a durable destination, intended listeners/access rules and actual mixes. No public Git audio, deployment or substitute synthetic arrangements.
- **FX-T18 OPEN.** Do not conflate thread CPU with wall deadlines or discard the earlier wall misses. At a coherent product candidate only: one expressly authorized exact-head Windows x64 Release full CTest + Steinberg validator with checks/skips, artifact SHA/provenance, and loaded wall and CPU timing; then one owner Cubase/REAPER round. Native picker/focus/DPI, locked or unwritable destinations, disk-full/overwrite/cancel near replacement, blocked I/O during unload, long wet tails and real host bounce remain unqualified. Blocked OS file I/O still has no bounded teardown guarantee.
