# VOX FX — candidate gate and outstanding work

**Draft #156 / HOLD / NO MERGE. No Windows opt-in.** This is the current qualification checklist, not a release certificate. [TL review](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7030720993) accepted `199fb879d352ffd9e09eccaa82f4c4ad631f70b1` for development only. Requirements remain in the [production specification](VOX_FX_UNIFIED_PRODUCTION_TZ.md). The [historical implementation matrix](FULL_IMPLEMENTATION_HANDOFF_20261119.md) describes older source SHAs; it is not evidence for the next Windows binary.

## Candidate blockers

1. **FX-01/02/03 — sound, playback and editing:** maintain the 19 host IDs and backward-compatible state. Check automation/readback, source restore, real clock loss/meter/seek, and musical note and lease ownership. The offscreen native suite exercises the processor/editor, not the Windows wrapper.
2. **FX-04 — modulation, effects and real time:** finish checks for supported route combinations, controls, anti-alias behavior, serial FX tails and modulation in the host. Keep wall deadlines separate from thread CPU. The 96 kHz / 64 Pattern thread-CPU exceedance and separate wall-clock misses are explicit retest cases, not cleared defects.
3. **FX-05 — authoring, catalog and exchange:** preserve the deterministic Program/Pattern, A/B, and user-library regressions. Native Save and [prepared file drag](FILE_DRAG_20261119.md) are implemented for WAV/SMF; real OS and DAW import are unqualified. Verify copy into project, no double-trigger with Save and editable SMF before advertising host support. Catalog curation and licensing remain distinct from preset generation and the technical gate.
4. **FX-06/07 — native UI, build and release:** complete focus/keyboard/DPI checks and the export/bounce/teardown scenarios below, then retain exact-head Windows Release and owner evidence. A clean Linux run cannot authorize shipping.

## Save: owner-approved create-only

[Create-only Save](CREATE_ONLY_SAVE_20261119.md) supersedes the observation-then-replace checkpoint for WAV/SMF. No replacement of any existing destination is allowed, regardless of chooser behavior. Early observation rejects a chosen existing path; the final native no-replace publication protects a competing winner after observation. Unsupported filesystems fail closed. `.voxfx` preset editing and offered drag lifetimes remain separate. Exact-head Windows and host proof remain open. The [Linux receipts](CREATE_ONLY_SAVE_20261119.md#linux-evidence) cover both formats and all seven WAV parity cases on `7a98622`.

## Historical local qualification (before create-only Save)

- File publication implementation `d4c3c3ff702e65d4f980749239121c3a783c31e9`, Debian/GCC 12.2 x86-64, JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`: `VOX_FX_TIMING_THREAD_CPU=1 taskset -c 0 bash docs/ultima-vox-music/fx/evidence/run-linux-native-fx.sh <output>`. [Native processor and file export transcript](evidence/candidate-export-native-20261119.txt): zero failures, seven WAV comparisons with zero sample difference, cross-device fixture executed. The routed stress fixture also had zero functional failures. `FxFileExportTests` and `FxPresetLibraryTests` each passed both an optimized plain build and ASan/UBSan, using pinned JUCE core objects that were not themselves instrumented.
- Timing harness `ed9045ed17ec8f5b3eb99a6e805db535bd8c3a3a`: rebuilt `FxRuntimeStressTests` with the Linux harness flags, linked the unchanged processor objects above, and ran on CPU 0 with a 1 MiB stack. [Routed](evidence/candidate-rt-routed-20261119.txt) and [authored](evidence/candidate-rt-authored-20261119.txt) each recorded 54 × 160 callbacks; [typical](evidence/candidate-rt-typical-20261119.txt) recorded 9 × 160. **18,720 valid CPU pairs**, no zero deltas, zero measured wall or CPU deadline misses, zero scoped allocations or wrapped locks. No Windows timing here. The new Linux run does **not** erase the two prior 96 kHz wall misses (or earlier misses); FX-T18 is still open.

## Current requirements and evidence

Use the [single FX-01…07 / FX-T01…20 engineering matrix](ENGINEERING_DELTA_20261119.md) for exact source, executed tests, partial work and open qualification. See the [Windows engineering-gate runbook](WINDOWS_ENGINEERING_GATE_20261119.md) for exact-head checkout, runner/SDK, historical crash replay and the owner-approved create-only Save proof.

## One consolidated Windows handoff, only when explicitly authorized

Pin the integrated source SHA and record Windows/runner/host versions, compiler, pinned JUCE/VST3 SDK, Release x64 build flags, run URL, full CTest/validator counts (pass/fail/skip), and SHA-256 of the VST3 binary and downloaded artifact. Report a missing check or failed artifact download as such. Do not use the focused diagnostic run in place of full CTest.

| Check group | Required record |
|---|---|
| Loaded callbacks | 44.1/48/96 kHz × 64/128/512 × 1/4/8 voices; dense grains/routing and Pattern off/on, including 96 kHz / 64. Report wall max and p99, thread CPU and wall/CPU miss counts separately, allocation/lock probe coverage, system load and per-miss configuration. Keep HOLD on reproduced wall overruns. |
| Picker and focus | WAV and MIDI Save, cancel, reopen, Paths with spaces/non-ASCII, focus restoration, keyboard traversal and 100/125/150/200% DPI; distinguish actual OS picker from the injected offscreen fixture. |
| Filesystem | New and occupied name, explicit different-name retry, destination created during export, readonly/locked file, denied parent, directory or invalid path, non-local/different-volume behavior, controlled low-space failure, cancel before and during publication. Record old/new sizes and hashes plus temporary files before and after. Never use a real project as the failure fixture. |
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

A process exit of zero indicates functional assertions and clock-read coverage, not a deadline pass. Record the exact source SHA, runner, Windows build, CPU/power plan, the declared competing workload and measured processor utilization during the capture (not only the bench profile). Preserve the before/during/after load samples with the logs. For example, the runner can collect Windows performance-counter samples or an approved ETW/WPR trace alongside the existing run; if short-block `GetThreadTimes` resolution is insufficient, use a validated method that can separate running thread time from elapsed wall rather than extrapolating zero deltas. The synthetic callback loop does not substitute for actual host deadlines.

Reconcile the copied raw logs with:

```bash
python docs/ultima-vox-music/fx/evidence/audit-fx-timing.py suite-out/rt/routed.txt suite-out/rt/authored.txt suite-out/rt/typical.txt
```

The checker reports completeness, per-miss context and coarse-accounting warnings; it does not certify loaded conditions, Windows probes, timer resolution or FX-T18. Review the wall and CPU counts before deciding the gate and retain all raw logs.

## Listening delivery

Use the [single 232-cue working matrix](evidence/fx-review-232-working-matrix.tsv) and [private transfer inventory](ENGINEERING_DELTA_20261119.md#frozen-listening-handoff-no-media-transfer). Freeze the 232/0 inventory until the owner provides recipients, durable private storage and 2–3 real arrangements. No new ZIP or public audio; archived SHA pins stay immutable. Review actual printed trim, headroom, placement and mono masking before deciding what roles warrant expansion. Engineering qualification is not content approval.
