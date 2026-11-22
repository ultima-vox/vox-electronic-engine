# FX-T18 — Windows x64 callback measurement proposal

**Status: Draft PR preparation, measurement not run.** This document defines the
reviewable scenario for the next FX qualification step. It does not authorize a
Windows job, a release decision, or a merge into `next/fx-development`.

The source baseline for this checkpoint is `c7ee608a0879087d9dade1f2471c69a1bde92732`.
The change is test and evidence tooling only: no FX DSP, parameter identity,
state schema, Wavefold implementation, or host contract is changed. The
original Wavefold remains the shipping implementation; ADAA1/ADAA2 experiments
are not part of this work.

## What this checkpoint measures

The existing `suite_fx_runtime_stress_tests` fixture drives the actual
`FxProcessor::processBlock` path, rather than a stand-alone DSP function. With
`VOX_FX_TIMING_THREAD_CPU=1` it records, for every measured callback:

- wall time from `steady_clock`;
- thread CPU time from the Windows `GetThreadTimes` user plus kernel counters;
- the deadline `frames / sample-rate * 1,000,000` in microseconds;
- wall, CPU and wall-only deadline exceedances;
- CPU-pair coverage, zero deltas and the minimum positive CPU delta so coarse
  Windows timer resolution is visible instead of being treated as zero CPU;
- peak, DC, finite-output and active-grain assertions.

The callback fixture also enables the existing Windows IAT allocation probe and
the new Windows synchronization probe on the measured thread. The probes are
started before `processBlock` and stopped after it; their installation and
reporting are outside the timed region. The allocation count is an entry-point
count, not a byte count. The synchronization count includes observed critical
section/SRW-lock entries and Win32 wait/condition-variable entries, reported as
lock and wait columns separately.

The new `VOX_FX_T18_REPORT_V1` TSV is an auditable companion to stdout. It keeps
all 160 callback rows for every matrix cell, not only misses. The checker
`evidence/audit-fx-windows-timing.py` reconciles the matrix, deadlines,
percentiles, CPU pairs, miss flags and case totals. It deliberately reports
`diagnostic only`; it cannot grant qualification.

## Proposed controlled scenario

Run the same exact 64-bit Windows Release binary in three separate invocations,
with no DAW or build activity competing for the runner:

| Profile | Environment | Matrix | Purpose |
|---|---|---|---|
| routed | no profile variable | 44.1/48/96 kHz × 64/128/512 frames × 1/4/8 initial voices × Pattern off/on | representative routed graph plus dense MIDI and four Drive slots |
| authored | `VOX_FX_STRESS_AUTHORED=1` | same 54 cells | non-default grain shape, step and held ADSR routes |
| typical | `VOX_FX_STRESS_TYPICAL=1` | 44.1/48/96 kHz × 64/128/512 frames × 1 voice × Pattern off | one curated Firefly Ladder event without randomized MIDI or host Pattern |

Each cell measures 160 callbacks after `prepareToPlay`. The first measured
callback is retained; it must not be hidden by a warm-up policy. The fixture
uses only built-in deterministic data and does not read customer files. The
stress profiles use the existing four granular layers, four serial Drive slots
and 64-route configuration. Pattern-on cells use the existing max-ratchet
four-bar grid.

Before dispatch, TL should confirm:

- exact PR head and tree SHA, base branch, and pinned JUCE commit;
- native x64 Windows 10/11, CPU model, logical CPUs, RAM, power plan, process
  affinity and runner load;
- VS 2022/CMake/CTest versions and Windows SDK;
- that the fixture is run on an idle machine, separately from foreground DAW
  testing;
- whether `GetThreadTimes` resolution is sufficient for the selected 64-frame
  cases, or whether an approved Windows trace is required as a supplement;
- whether the observed lock/wait API coverage is sufficient for this gate.

## Build and run after approval

These are the proposed commands for the authorized Windows runner. They are
not to be run from this Arena checkpoint:

```powershell
cmake -S . -B build-t18 -A x64 `
  -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF `
  -DVOX_STRICT_DEPENDENCIES=ON `
  "-DFETCHCONTENT_SOURCE_DIR_JUCE=$env:JUCE_DIR"
cmake --build build-t18 --config Release --clean-first --parallel

$env:VOX_FX_TIMING_THREAD_CPU = '1'
$env:VOX_FX_SOURCE_SHA = (git rev-parse HEAD)
$env:VOX_FX_REPORT = (Join-Path (Resolve-Path .).Path 't18-out\routed.tsv')
New-Item -ItemType Directory -Force t18-out | Out-Null
ctest --test-dir build-t18 -C Release `
  -R '^suite_fx_runtime_stress_tests$' --no-tests=error `
  --output-on-failure -VV
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$env:VOX_FX_STRESS_AUTHORED = '1'
$env:VOX_FX_REPORT = (Join-Path (Resolve-Path .).Path 't18-out\authored.tsv')
ctest --test-dir build-t18 -C Release `
  -R '^suite_fx_runtime_stress_tests$' --no-tests=error `
  --output-on-failure -VV
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Remove-Item Env:VOX_FX_STRESS_AUTHORED
$env:VOX_FX_STRESS_TYPICAL = '1'
$env:VOX_FX_REPORT = (Join-Path (Resolve-Path .).Path 't18-out\typical.tsv')
ctest --test-dir build-t18 -C Release `
  -R '^suite_fx_runtime_stress_tests$' --no-tests=error `
  --output-on-failure -VV
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

python docs/ultima-vox-music/fx/evidence/audit-fx-windows-timing.py `
  t18-out\routed.tsv > t18-out\routed.audit.json
python docs/ultima-vox-music/fx/evidence/audit-fx-windows-timing.py `
  t18-out\authored.tsv > t18-out\authored.audit.json
python docs/ultima-vox-music/fx/evidence/audit-fx-windows-timing.py `
  t18-out\typical.tsv > t18-out\typical.audit.json
```

The report paths must be unique per invocation. Preserve the raw CTest text,
three TSV files, three JSON audit outputs, `ctest --show-only=json-v1`, exact
source/tree SHA, binary SHA-256, runner manifest and the PowerShell environment
receipt. Do not commit the hardware reports or generated build tree to Git.

## Interpretation and evidence contract

The receipt must report these outcomes independently:

- `wall_miss`: callback wall time exceeded its frame deadline;
- `cpu_miss`: measured thread CPU exceeded its frame deadline;
- `wall_only_miss`: wall exceeded while the measured thread CPU did not;
- allocation probe unavailable, non-zero, or zero;
- synchronization/wait probe unavailable, non-zero, or zero;
- incomplete CPU pairs, zero deltas, and minimum positive delta;
- finite/output/grain assertion failure, timeout, crash, skipped run or missing
  artifact.

No aggregate average can erase a per-block miss. A zero `GetThreadTimes` delta
is not evidence of zero work. A zero IAT count covers only the loaded fixture
and documented API entry points; it is not a proof about all host modules or
kernel-internal waits. A clean fixture result is therefore necessary evidence,
not a DAW dropout guarantee.

The proposed engineering interpretation is **not a pass/fail threshold** until
TL approves the scenario and target hardware. For the later qualification gate,
any measured deadline exceedance, callback allocation or observed lock/wait
entry remains a review item. This fixture does not establish Cubase/REAPER
hosting, UI, listening, validator, packaging or release readiness.

## Deliberately not done in this checkpoint

- no `[RUN_FX_SUITE]` opt-in or heavy Windows job;
- no Windows result, CPU claim, lock claim or release qualification claim;
- no change to PR #156, its Draft/HOLD/NO MERGE state, or `next/fx-development`;
- no ADAA/Wavefold experiment, oversampling, PDC or DSP optimization;
- no host DAW run and no customer/project asset collection.

TL approval of this scenario is required before the exact-head Windows run is
requested. After the run, the separate Draft PR should be updated with the
immutable raw artifacts and the independent review should classify each result
before any integration decision.
