# VOX FX — candidate gate and outstanding work

**Draft #156 / HOLD / NO MERGE. No Windows opt-in.** This is the current qualification checklist, not a release certificate. [TL review](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7030720993) accepted `199fb879d352ffd9e09eccaa82f4c4ad631f70b1` for development only. Requirements remain in the [production specification](VOX_FX_UNIFIED_PRODUCTION_TZ.md). The [historical implementation matrix](FULL_IMPLEMENTATION_HANDOFF_20261119.md) describes older source SHAs; it is not evidence for the next Windows binary.

## Candidate blockers

1. **FX-01/02/03 — sound, playback and editing:** maintain the 19 host IDs and backward-compatible state. Check automation/readback, source restore, real clock loss/meter/seek, and musical note and lease ownership. The offscreen native suite exercises the processor/editor, not the Windows wrapper.
2. **FX-04 — modulation, effects and real time:** finish checks for supported route combinations, controls, anti-alias behavior, serial FX tails and modulation in the host. Keep wall deadlines separate from thread CPU. Two prior misses at 96 kHz / 64 with Pattern are an explicit retest case, not a cleared defect.
3. **FX-05 — authoring, catalog and exchange:** preserve the deterministic Program/Pattern, A/B, and user-library regressions. Native Save for WAV/SMF exists; OS file drag is still a functional gap. The candidate must not advertise drag until it uses a real exported file, doesn't double-trigger Save, and is qualified. Catalog curation and licensing remain distinct from preset generation and the technical gate.
4. **FX-06/07 — native UI, build and release:** complete focus/keyboard/DPI checks and the export/bounce/teardown scenarios below, then retain exact-head Windows Release and owner evidence. A clean Linux run cannot authorize shipping.

## File publication checkpoint

The pinned JUCE `TemporaryFile` replacement path can retry a failed rename via a copy on POSIX, and its move path can delete a destination first. FX no longer uses that replacement path. The worker writes and closes a staging file beside the destination and checks byte count, then commits using a single platform primitive: POSIX rename for replacement, POSIX hard link for no-replace creation, or `MoveFileExW` on Windows without `MOVEFILE_COPY_ALLOWED`. There is no delete-first or copy fallback. On POSIX, a destination filesystem that cannot hard-link a new file fails closed. This is not a crash-durability or arbitrary network-filesystem guarantee.

The WAV/MIDI worker observes whether the path exists **before** rendering/encoding. If absent, the native commit rejects any entry that appears later, leaving its bytes untouched. A path already present when the worker observes it is still treated as an overwrite under the save chooser policy. This is not a guarantee about changes before that observation or about the identity of an already-existing target. The new tests pause during encoding or writing, create competing files, then verify those originals and staging cleanup. A direct cross-device failure fixture verifies no copy, source deletion, or overwrite on Linux. Windows file behavior remains unqualified. No DSP, host IDs, preset/state format, or WAV/MIDI encoding changes.

## Local qualification of this checkpoint

- File publication implementation `d4c3c3ff702e65d4f980749239121c3a783c31e9`, Debian/GCC 12.2 x86-64, JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`: `VOX_FX_TIMING_THREAD_CPU=1 taskset -c 0 bash docs/ultima-vox-music/fx/evidence/run-linux-native-fx.sh <output>`. [Native processor and file export transcript](evidence/candidate-export-native-20261119.txt): zero failures, seven WAV comparisons with zero sample difference, cross-device fixture executed. The routed stress fixture also had zero functional failures. `FxFileExportTests` and `FxPresetLibraryTests` each passed both an optimized plain build and ASan/UBSan, using pinned JUCE core objects that were not themselves instrumented.
- Timing harness `ed9045ed17ec8f5b3eb99a6e805db535bd8c3a3a`: rebuilt `FxRuntimeStressTests` with the Linux harness flags, linked the unchanged processor objects above, and ran on CPU 0 with a 1 MiB stack. [Routed](evidence/candidate-rt-routed-20261119.txt) and [authored](evidence/candidate-rt-authored-20261119.txt) each recorded 54 × 160 callbacks; [typical](evidence/candidate-rt-typical-20261119.txt) recorded 9 × 160. **18,720 valid CPU pairs**, no zero deltas, zero measured wall or CPU deadline misses, zero scoped allocations or wrapped locks. No Windows timing here. The new Linux run does **not** erase the two prior 96 kHz wall misses (or earlier misses); FX-T18 is still open.

## FX-T01…20 evidence map

“Local evidence” identifies an executable test or report, not a qualification pass. A later implementation should update the affected rows and retain source SHA, command/run URL, environment, counts/skips, raw output and open exceptions.

| ID | Local evidence | What still gates acceptance |
|---|---|---|
| FX-T01 | Family render and SourceSound regressions across 12 families | Accepted characteristic references and human listen |
| FX-T02 | Layer, ProgramStore and SoundGraph tests; independent layer snapshot coverage | Target-host readback and dense four-layer quality |
| FX-T03 | Layer/Program 20-curve codec, DSP and native undo/stale-gesture tests | Full real-host edit, keyboard and reopen workflow |
| FX-T04 | PlaybackMode and native six-mode/transport tests | Host loop seams, Transition timing and long holds |
| FX-T05 | Native FIFO, CC120/123, panic, retire-slot and audition-pressure tests | Real MIDI stress and audible click review |
| FX-T06 | Timeline/bar/Transport tests and synthetic PlayHead | Actual DAW tempo, start/stop/seek/loop/meter sequences |
| FX-T07 | SourceSound root/scale/transpose checks and non-tonal tags | Musical listen in pitched and keyless arrangements |
| FX-T08 | MacroRouting, SoundGraph, native route editor and scoped stress | Complete supported route/automation permutations |
| FX-T09 | Four-slot and Drive regression; paired wet-tail history and offline render | Full spectral, long-tail and host bounce qualification |
| FX-T10 | 64-grain stress; import/hash/lease/relink, supersession and sample restore | Host restore/relink through real paths and OS file failures |
| FX-T11 | Deterministic generator and locks, persistent A/B and native preset browse/save | Workflow and automation readback in real host, broader lock and library stress |
| FX-T12 | 176 unique pinned review references, deterministic provenance and working matrix | 124/424 more candidates to reach 300/600, real arrangement listening, zero current approvals |
| FX-T13 | Canonical SMF/Pattern grid, playback, seek and native clipboard regressions | Host transfer, cross-host boundary and editable SMF import |
| FX-T14 | Seven sample-exact capture fixtures, cancellation and closed-file publication | Native chooser, filesystem matrix, OS drag gap and actual bounce/teardown |
| FX-T15 | Native offscreen editor, timeline and chooser callback regressions | Real OS focus/tab, scale, resizing and accessibility |
| FX-T16 | Native closed-editor state, restore/rejection, sample and A/B fixtures | Actual project reopen, path changes and compatibility in both hosts |
| FX-T17 | Historical diagnostics only; no current exact-head Windows result | Full Windows Release CTest, Steinberg validation, VST3/artifact SHA and provenance |
| FX-T18 | Probed 44.1/48/96 × 64/128/512 Linux stress, wall and CPU independently reported | **OPEN:** Windows loaded runner and actual host deadlines, with every miss retained |
| FX-T19 | Recipe/source provenance and pinned WAV and portable hashes | Rights/distribution sign-off and per-asset review on the full catalog |
| FX-T20 | No owner acceptance yet | Once-only Cubase 12–15 and REAPER owner round after technical evidence |

## One consolidated Windows handoff, only when explicitly authorized

Pin the integrated source SHA and record Windows/runner/host versions, compiler, pinned JUCE/VST3 SDK, Release x64 build flags, run URL, full CTest/validator counts (pass/fail/skip), and SHA-256 of the VST3 binary and downloaded artifact. Report a missing check or failed artifact download as such. Do not use the focused diagnostic run in place of full CTest.

| Check group | Required record |
|---|---|
| Loaded callbacks | 44.1/48/96 kHz × 64/128/512 × 1/4/8 voices; dense grains/routing and Pattern off/on, including 96 kHz / 64. Report wall max and p99, thread CPU and wall/CPU miss counts separately, allocation/lock probe coverage, system load and per-miss configuration. Keep HOLD on reproduced wall overruns. |
| Picker and focus | WAV and MIDI Save, cancel, reopen, Paths with spaces/non-ASCII, focus restoration, keyboard traversal and 100/125/150/200% DPI; distinguish actual OS picker from the injected offscreen fixture. |
| Filesystem | New and authorized overwrite, destination created during export, readonly/locked file, denied parent, directory or invalid path, non-local/different-volume behavior, controlled low-space failure, cancel before and during publication. Record old/new sizes and hashes plus temporary files before and after. Never use a real project as the failure fixture. |
| Host lifetime and bounce | Save during work; reopen project; cancel; closed editor versus destroyed processor; controlled blocked I/O on unload; separately compare matching-history online/offline tails and fresh-start bounce. Report stalls or hangs, not just rendered sample checks. |
| Owner acceptance | Only after technical verification: one Cubase/REAPER round for install/load/uninstall, automation, MIDI/Pattern/transport, state, samples/restore, editable MIDI, WAV and long tails. Log version, preset, settings, outcome and reviewer for each result. |

### Capture thread CPU without changing the Windows gate

The opt-in `suite_fx_runtime_stress_tests` records `GetThreadTimes` kernel+user time on Windows and `CLOCK_THREAD_CPUTIME_ID` on Linux. CPU readings are outside the callback and allocation/lock probes. Wall misses are counted even when the CPU reading is unavailable. The timing opt-in reports valid pairs, zero deltas, and minimum positive CPU delta separately; it fails if the clock cannot provide all requested pairs. Windows accounting may be coarser than short blocks: a valid pair with zero delta is not evidence that the callback used no CPU. If accounting is too coarse to assess deadlines, retain the raw log and obtain sufficiently resolved CPU evidence on the authorized runner/host; do not mark FX-T18 passed. Probe availability is reported; the Linux pthread wrap does not qualify Windows locks.

In the **authorized** Windows session, after the full Release CTest, run this targeted evidence capture using the same build (not a second dispatch), recording the representative runner load separately:

```powershell
$env:VOX_FX_TIMING_THREAD_CPU = '1'
New-Item -ItemType Directory -Force suite-out/rt | Out-Null
try {
    foreach ($profile in 'routed', 'authored', 'typical') {
        Remove-Item Env:VOX_FX_STRESS_AUTHORED,Env:VOX_FX_STRESS_TYPICAL -ErrorAction SilentlyContinue
        if ($profile -eq 'authored') { $env:VOX_FX_STRESS_AUTHORED = '1' }
        if ($profile -eq 'typical') { $env:VOX_FX_STRESS_TYPICAL = '1' }
        ctest --test-dir build-suite -C Release -R '^suite_fx_runtime_stress_tests$' --no-tests=error -VV 2>&1 |
            Tee-Object -FilePath "suite-out/rt/$profile.txt"
        if ($LASTEXITCODE -ne 0) { throw "FX timing profile failed: $profile ($LASTEXITCODE)" }
    }
} finally {
    Remove-Item Env:VOX_FX_TIMING_THREAD_CPU,Env:VOX_FX_STRESS_AUTHORED,Env:VOX_FX_STRESS_TYPICAL -ErrorAction SilentlyContinue
}
```

A process exit of zero indicates functional assertions and clock-read coverage, not a deadline pass. Review the wall and CPU counts before deciding the gate and retain all raw logs.

## Listening delivery

Use the [single working matrix](evidence/fx-review-176-working-matrix.tsv); it links each untouched raw reference to the portable recipe and an arrangement decision. Keep the archived matrix and [pinned ZIP hash](evidence/fx-review-176-archive.sha256) immutable. The durable handoff requires an owner-authorized private destination, permitted recipients and actual arrangements. Give reviewers the ZIP checksum and the working sheet, not public audio. Start with Tangent Iris, Glass Estuary and Dust Shutter; log actual printed trim, headroom, placement and mono masking before deciding. Further catalog development should arrive as larger, individually authored tranches with render and portable-preset provenance, not more eight-cue ZIPs or generated-seed counts.
