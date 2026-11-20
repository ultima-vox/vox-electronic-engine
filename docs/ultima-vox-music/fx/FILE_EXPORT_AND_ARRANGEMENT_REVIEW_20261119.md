# VOX FX — file export, voice-pressure regression and consolidated arrangement review

Development source `cc3d32e5eabdee10aff042516dfb1cac9da7fea1`, following [the TL development review](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7020035414). **Draft / HOLD / NO MERGE.** No intermediate Windows CI dispatched.

## File export

The MIDI save dialog captures the Pattern and fixed BPM before opening the picker. A confirmed export now encodes and writes on the processor-owned file worker, not the editor thread. WAV and MIDI share one slot, a message-thread-only value status and completion, same-directory temporary replacement, and the same atomic cancellation/commit decision. The job does not retain an editor pointer; editor close/recreation does not cancel a confirmed export. A cancel accepted before replacement leaves the original file untouched; once replacement begins, its actual success or failure wins. There is still no guarantee of bounded processor teardown if the OS blocks in a file syscall. Completion is published after filesystem work; host project save does not join or serialize the job. This pass does not modify `.mid` encoding rules, WAV rendering, project/preset schema or the 19 host parameter IDs.

## Hosted audio regressions

The native pressure test uses two original external sample generations. MIDI and Pattern collide at the same sample while the old generation is still leased by an audition. Repeated steals fill all four retirement slots, reclaim the retiring audition, and then count a truly musical overflow. The fixture repeats after host stop, checks finite and deterministic stereo against an independently configured instance, and tests FIFO NoteOff for a stolen note versus a surviving held one. The wet transition test now goes back online while both delay and reverb are still audibly ringing, comparing with an uninterrupted instance with matching history. Existing audition tails are retained rather than reset; this is not parity against a fresh offline render.

## Linux validation

- Pinned JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`, [native harness](evidence/run-linux-native-fx.sh) with a 1 MiB test stack: processor and runtime stress executables exited 0. The MIDI job fixture verified exact captured SMF bytes across editor reopen and later Pattern edits, wrong-kind cancellation refusal, exclusive WAV/MIDI admission, one completion, and a missing-directory failure. Seven existing WAV/callback comparisons stayed sample-exact.
- `FxFileExportTests` passed plain and with ASan/UBSan on test/worker code linked to prebuilt, uninstrumented JUCE. Tests cancelled both WAV and MIDI deterministically after temporary bytes had been written but before replacement and verified original file preservation and cleanup.
- Linux timing used `VOX_FX_TIMING_THREAD_CPU=1 taskset -c 0`, two 54-case granular/64-route stress profiles (44.1/48/96 kHz × 64/128/512 × 1/4/8 with and without Pattern) and nine typical cases, 160 blocks each. Across 18,720 callbacks: zero scoped allocations/wrapped locks and zero thread-CPU deadline misses, but **three wall-clock misses**, all in routed stress. FX-T18 is open; the run does not identify a cause or invalidate previous misses.

| Profile | Wall / CPU misses | Absolute max wall / wall p99 / CPU (µs) | Max relative wall / wall p99 / CPU |
|---|---:|---:|---:|
| [Routed](evidence/shared-export-rt-stress-20261119.txt) | 3 / 0 | 23494.92 / 11378.16 / 3477.24 | 202.37% / 98.00% / 70.00% |
| [Authored](evidence/shared-export-rt-authored-20261119.txt) | 0 / 0 | 2633.77 / 2542.01 / 2635.07 | 55.33% / 54.97% / 55.44% |
| [Typical](evidence/shared-export-rt-typical-20261119.txt) | 0 / 0 | 263.48 / 216.28 / 222.36 | 5.94% / 4.71% / 6.01% |

Maxima are independent. At the worst wall miss, 44.1 kHz / 512 / 8 with Pattern, wall was 23494.92 µs against 11609.98 µs, and thread CPU was 2500.13 µs for that callback.

## A120 + B56: organized review, not content approval

This pass consolidates existing material; it does not add cues. The tally remains **176 review candidates, zero approved, 124 more to 300**. [Arrangement matrix](evidence/fx-review-176-arrangements.tsv) gives every cue a role, placement, reference WAV, portable preset, provenance and editable review fields. [README](evidence/fx-review-176-readme.txt) sets out fixed monitor level, documented effects gain, headroom, mono masking and playback-mode checks. Prioritize Tangent Iris, Glass Estuary and Dust Shutter in actual mixes. No raw WAV was normalized, no real owner arrangements were available, and nothing was moved into an approved factory catalog.

The local ZIP (124,402,788 bytes; 368 verified members plus `SHA256SUMS`) contains 176 unaltered solo WAVs, 176 portable files, the two original procedural source WAVs, the review matrix and supporting metadata. It does not contain synthetic comparison arrangements. The packager checks pinned file SHA-256s, exact catalog metadata, cross-wave uniqueness, sample references, and every written archive entry. See [member hashes](evidence/fx-review-176-members.sha256) and [archive SHA](evidence/fx-review-176-archive.sha256). Two builds produced byte-identical ZIPs. A corrupt-reference test refused to create an archive; an existing destination was refused. Audio stays out of Git and was not deployed. Durable transfer requires the owner's authorized private destination.

### Rebuild locally

Render the A catalog and portable presets using the first two C++ build/run steps in [the A review script](evidence/rebuild-wave-a-review.sh). Synthetic comparison renders are not needed. Render B with the pinned exporter and `--room-cuts` as in [the B56 report](WAVE_B_ROOM_CUTS_20261119.md). Then run:

```bash
python docs/ultima-vox-music/fx/evidence/package-fx-review.py <A-output> <B-output> <new-archive.zip>
```

The verifier refuses a file that differs from its published reference and does not overwrite an existing ZIP. Compiler/platform differences can change reference audio bytes; use the pinned toolchain or the preserved WAVs when this happens.

## Gates

Exact-head consolidated Windows Release x64 CTest, Steinberg validation, FX-T18 wall/CPU evidence, artifact hashes and skip counts, then one owner Cubase/REAPER acceptance, remain open. Windows picker/focus/DPI, permissions/locked/invalid target, disk-full/overwrite and blocked-I/O host teardown were not tested here. More authored catalog content and actual arrangement decisions remain necessary.
