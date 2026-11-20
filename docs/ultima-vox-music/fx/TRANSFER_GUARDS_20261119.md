# VOX FX — transfer guards and timing capture

Follow-up to [TL review 7032059198](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7032059198), integrated base `c306ea08dbd9dcbd727556a1b76767b0361a3b2c`. **Development / Draft / HOLD / NO MERGE.** No Windows job or owner DAW round was dispatched.

## Save destination changed during export

Before rendering/encoding, the worker records whether the selected leaf is absent, an inspectable writable regular file, or blocked. New files use the prior atomic no-replace path. For an overwrite it records native file identity plus size and modification metadata; before claiming commit it rechecks those fields and fails if they differ, the old path vanished, or it can no longer inspect the file. A final-component symlink/reparse point, non-regular file, or marked-readonly incumbent is refused. The native move still has no copy/delete-first fallback, and cancellation can still win before commit. Nothing moves these operations onto the callback.

This is **not an atomic compare-and-replace**. It does not protect changes before the initial worker observation (including after the Save dialog), changes that those metadata do not distinguish, changes to parent-directory resolution, or a writer racing the final check and native move. Those cases, along with Windows-specific identity, locked/denied/non-ASCII/readonly/low-space paths, still need controlled qualification. Use a distinct filename if the selected path is being actively edited; do not use an owner project as a test target. State and the 19 host IDs are unchanged.

## Focused replay fixtures

An isolated test scratch root contains only fixture files, so the test can count directories and files deterministically. It replays the same staged MIDI 12 times without directory growth; stages a distinct WAV while the previously offered MIDI exists and compares its bytes; verifies cleanup of a never-offered replacement; then exercises a separate pending drop across editor **and** processor destruction. A delayed duplicate completion from a thread that owns only the callback must preserve both retained files. The existing nested synchronous, rejected retry and stale-ticket regressions remain. This is not OLE or host-copy proof. Keep the visible **Copy into project** warning and no eviction of offered files.

## Callback diagnostic capture

The opt-in test logs OS/CPU information and a row for **every** missed callback (case, Pattern mode, zero-based block, events, grains, wall time, thread CPU time and deadline). It buffers this data during the test and prints after the cell; no formatting or file I/O on the measured callback. [The evidence checker](evidence/audit-fx-timing.py) accepts direct or `ctest -VV` logs and checks the complete 54/54/9 matrix, valid CPU pairs, counters and per-miss rows. It flags apparently coarse CPU accounting but cannot prove effective timer resolution or qualify a run. Keep the unabridged logs, probes, load description, OS and source SHA. A zero process exit is a functional/stability result, not a deadline pass.

## Executed Linux evidence

Source `9f08899b18b711c256daec66be0df1d9754cf6af`, pinned JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`, GCC 12.2 x86-64. [Pinned native transcript](evidence/transfer-guards-native-20261119.txt): zero processor/file-export failures; seven WAV capture comparisons had zero sample error; Linux cross-device fixture executed. File-export and preset-library programs also passed standalone plain and ASan/UBSan; prebuilt JUCE objects were not instrumented. File fixtures tested a same-byte replacement with a different identity, in-place edit while the worker was paused, deletion before commit, readonly and leaf symlink refusal, and staging cleanup. Offscreen and direct C++ injection do not qualify native dialog or OLE behavior.

| Run | Callbacks | Wall misses | Thread-CPU misses |
|---|---:|---:|---:|
| [Routed](evidence/transfer-guards-rt-routed-20261119.txt) | 8,640 | 1 | 0 |
| [Authored](evidence/transfer-guards-rt-authored-20261119.txt) | 8,640 | 0 | 0 |
| [Typical](evidence/transfer-guards-rt-typical-20261119.txt) | 1,440 | 0 | 0 |
| [Routed with deliberate same-CPU competitor](evidence/transfer-guards-rt-loaded-20261119.txt) | 8,640 | 261 | 0 |

The competitor was one `yes` writer to `/dev/null` pinned to the same Linux CPU. It was used to exercise per-callback logging, **not** to model Windows/DAW load. Authored and typical were not run with that competitor. All runs reported full CPU pairs, zero scoped allocations and zero wrapped locks. In the baseline, the 96 kHz / 512 / 8 Pattern-off case missed at block 109: 26,599.688 µs wall / 1,845.272 µs CPU vs. a 5,333.333 µs budget. In the deliberately contended run, 96 kHz / 64 / 1 with Pattern had three wall misses and no measured CPU misses. This is a separate run and does not clear the earlier measured CPU exceedance. **FX-T18 remains open.**

The evidence checker reconciled both sets of routed logs and reproduced the same result with UTF-16 CTest-style prefixes. Removing one per-miss row made it reject the inconsistent log. None of the checks attributes a timing cause or grants Windows qualification.

Use the [candidate gate](CANDIDATE_GATE_20261119.md) for the authorized Windows run. Include 96 kHz/64/1 and 44.1 kHz/512/8 with Pattern in the declared representative load, preserve the prior thread CPU exceedance and all wall misses, and get sufficiently resolved Windows CPU evidence before any deadline decision. The actual Windows Release CTest/validator/provenance and one owner Cubase/REAPER round remain gated. The [200-cue review matrix](evidence/fx-review-200-working-matrix.tsv) is unchanged, zero approved; no audio was deployed, repackaged or delivered.
