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

- the exact bracket `CPU start -> wall start -> processBlock -> wall end -> CPU end`;
- wall time from `steady_clock` and thread CPU time from Windows
  `GetThreadTimes` user plus kernel counters;
- the separately calibrated empty wall/CPU timer deltas (the deadline values
  remain the raw observed callback intervals, conservatively including the
  boundary API cost);
- the deadline `frames / sample-rate * 1,000,000` in microseconds;
- wall, CPU and wall-only deadline exceedances;
- p50, p95, p99, p99.9 and max for both wall and CPU, plus complete CPU-pair
coverage, zero deltas and the minimum positive CPU delta so coarse Windows
timer resolution is visible instead of being treated as zero CPU. With 160
observations, nearest-rank p99.9 equals max; it is retained as a separate
field, not presented as an independent tail estimate;
- peak, DC, finite-output and active-grain assertions.

The callback fixture also enables the existing Windows IAT allocation probe and
the new Windows synchronization probe on the measured thread. Probe setup,
positive-control lock/wait calls, and probe teardown are outside the timed
bracket. The allocation count is an entry-point count, not a byte count. The
synchronization probe records target, resolved, matched-import, installed-hook
and patch-failure counts separately for lock and wait APIs. A report is not
qualifying when the probe is unavailable, incomplete, or its positive control
did not count both paths; an observed callback count of zero is never allowed
to mask that state.

The new `VOX_FX_T18_REPORT_V1` TSV is an auditable companion to stdout. It keeps
all 160 callback rows for every matrix cell and explicit `miss` rows, not only
an aggregate. The checker `evidence/audit-fx-windows-timing.py` reconciles the
matrix, deadlines, p50/p95/p99/p99.9/max values, CPU pairs, miss flags, probe
attestation and case totals. It independently reads git HEAD/tree, hashes the
executable and runner receipt, and verifies the artifact manifest. It
deliberately reports `diagnostic only`; it cannot grant qualification.

## Proposed controlled scenario

Run the same exact 64-bit Windows Release binary in three separate invocations,
with no DAW or build activity competing for the runner:

| Profile | Environment | Matrix | Purpose |
|---|---|---|---|
| routed | no profile variable | 44.1/48/96 kHz × 64/128/512 frames × 1/4/8 initial voices × Pattern off/on | representative routed graph plus dense MIDI and four Drive slots |
| authored | `VOX_FX_STRESS_AUTHORED=1` | same 54 cells | non-default grain shape, step and held ADSR routes |
| typical | `VOX_FX_STRESS_TYPICAL=1` | 44.1/48/96 kHz × 64/128/512 frames × 1 voice × Pattern off | one curated Firefly Ladder event without randomized MIDI or host Pattern |

Each cell measures 160 callbacks after `prepareToPlay`. The first measured
callback is retained; it must not be hidden by a warm-up policy. Every full
profile report explicitly retains the sentinel cases `r96000_f64_v1_poff` and
`r44100_f512_v8_pon`; the reduced typical report declares them not applicable.
The fixture uses only built-in deterministic data and does not read customer files. The
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
# The orchestrator requires a clean exact-head checkout and a verified pinned
# JUCE directory. It deletes/reconfigures build-t18 and freshly builds only the
# Release FX stress target before reading the CTest-selected executable path.
$env:JUCE_COMMIT = 'e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8'
# Fill these from the owner-approved PR head; do not derive them in the run.
$expectedHead = 'OWNER_APPROVED_EXACT_HEAD_SHA'
$expectedTree = 'OWNER_APPROVED_EXACT_TREE_SHA'
.\docs\ultima-vox-music\fx\evidence\run-fx-windows-t18.ps1 `
  -JuceDir $env:JUCE_DIR -BuildDir build-t18 -OutputDir t18-out `
  -ExpectedHead $expectedHead -ExpectedTree $expectedTree -TimeoutSeconds 180
```

This runs routed, authored and typical independently. It does not stop after
the first failure; each profile retains its own CTest exit, raw text, TSV,
runner receipt, artifact manifest and audit attempt. The final exit is 1 if
any profile is a performance, test, timeout, infrastructure or artifact
failure. A CTest `--show-only=json-v1` inventory is saved for every profile;
the EXE path and SHA-256 used by the report must match the Release executable
selected by that inventory.

The checked-in orchestrator invokes the auditor with `--root`, `--executable`,
`--runner-receipt`, `--artifact-manifest`, `--expected-head` and
`--expected-tree`. The auditor does not trust the report's caller-supplied
source SHA: it independently executes `git rev-parse`, recomputes the EXE and
receipt SHA-256 values, and hashes every artifact listed in the manifest. A
missing report or failed audit is classified as infrastructure/test evidence;
a valid report with deadline/allocation/lock/wait failures is classified as
performance failure. All three profiles still run after any earlier category.

Before the owner-authorized Windows smoke, run the Windows PowerShell 5.1
parser/runtime check. It only parses the orchestrator and round-trips the JSON
and SHA-256 primitives; it does not build or measure:

```powershell
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass `
  -File docs\ultima-vox-music\fx\evidence\test-fx-t18-powershell.ps1
```

For a local schema-only fixture, the auditor may be invoked with both
`--allow-nonwindows --allow-unverified-provenance`; those flags are explicitly
not a qualification receipt and must not be used on a Windows result.

The report paths must be unique per invocation. Preserve the raw CTest text,
three TSV files, three audit outputs (JSON on successful audit, raw text otherwise), `ctest --show-only=json-v1`, exact
source/tree SHA, binary SHA-256, runner manifest and the PowerShell environment
receipt. Do not commit the hardware reports or generated build tree to Git.

## Interpretation and evidence contract

The receipt must report these outcomes independently:

- `wall_miss`: callback wall time exceeded its frame deadline;
- `cpu_miss`: measured thread CPU exceeded its frame deadline;
- `wall_only_miss`: wall exceeded while the measured thread CPU did not;
- p50/p95/p99/p99.9/max for wall and CPU, with every raw callback and explicit
  raw miss rows retained;
- allocation probe unavailable, non-zero, or zero;
- synchronization/wait probe unavailable, incomplete, non-zero, or zero, with
  target/resolved/imported/installed counts and positive-control results;
- incomplete CPU pairs, zero deltas, minimum positive delta and timer/API
  calibration values;
- finite/output/grain assertion failure, timeout, crash, skipped run or missing
  artifact;
- independent git HEAD/tree, executable SHA-256, runner receipt and artifact
  manifest checks.

The orchestrator classifies a valid report with deadline/allocation/lock/wait
failures as `performance_failure`, a bounded CTest timeout as
`timeout_failure`, a valid report with a failed fixture assertion as
`test_failure`, and missing/malformed/unverifiable reports or artifacts as
`infrastructure_failure` (a missing report is additionally
`infrastructure_or_test_failure`). It still runs every profile after a failure
and returns a failing overall status. No aggregate average can erase a
per-block miss. A zero `GetThreadTimes` delta is not evidence of zero work. A
zero IAT count covers only the loaded fixture and documented API entry points;
it is not a proof about all host modules or kernel-internal waits. A clean
fixture result is therefore necessary evidence, not a DAW dropout guarantee.
The negative fixtures in `evidence/test-audit-fx-windows-timing.py` protect the
strict `>` deadline parity at exact budget equality, miss-row reconciliation,
incomplete-probe rejection, dropped/duplicated callback rows, bad percentiles,
spoofed well-formed source/EXE receipts and modified binary hashes against an
independent git checkout.

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
