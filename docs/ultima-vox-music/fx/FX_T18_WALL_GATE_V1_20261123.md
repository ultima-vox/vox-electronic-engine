# FX-T18-WALL-GATE v1.1 — proposed wall-deadline diagnostic contract

**Contract ID:** `FX-T18-WALL-GATE`<br>
**Version:** `1.1-proposed`<br>
**Date:** 2026-11-23<br>
**Status:** Draft for separate owner approval; not a release-acceptance decision<br>
**Scope:** FX-T18 callback completion evidence only

Revision `1.1` adds independently reason-coded functional assertions, deterministic
FAIL/BLOCKED precedence, and host-wide four-service isolation requirements. It
preserves the v1.0 wall metric and does not change any existing threshold.

This contract proposes a wall-time criterion for realtime callback completion.
It does not retroactively change the red CTest result from run `45130013544`,
does not convert `CPU_MEASUREMENT_INSUFFICIENT` to PASS, and does not authorize
a Windows run or the full matrix. Any approved revision must retain the version,
exact source, raw artifacts and prior failure evidence.

## 1. Decision boundary

The current Windows result established that `GetThreadTimes` is too coarse for
per-callback CPU attribution on the tested runner: 17 raw 15,625-us flags and
1,423 zero deltas, with a 15,625-us quantum larger than every reduced typical
callback budget. Therefore this proposal evaluates the independently measured
callback wall interval as the realtime completion metric.

The existing CPU fields remain mandatory telemetry:

- retain every raw `cpu_us` value and every raw `cpu_miss` flag;
- retain `cpu_running_time_measured`, `cpu_resolution`,
  `cpu_attribution=UNKNOWN`, `cpu_measurement_status` and
  `cpu_zero_delta_semantics`;
- preserve `CPU_MEASUREMENT_INSUFFICIENT` as blocking for any CPU-based claim;
- never interpret a zero CPU delta as zero work.

`FX-T18-WALL-GATE` may only produce a proposed wall-gate result after a future
owner approval of this contract and an implementation that reports wall-gate
status independently of the existing CTest CPU exit. Until then, a CTest exit
of 8 with `CPU_MEASUREMENT_INSUFFICIENT` remains red and must not be called a
wall-gate PASS.

## 2. Exact measured metric

For every callback block, use the existing wall bracket:

```text
wall_start = steady_clock::now()
processBlock(...)
wall_end   = steady_clock::now()
wall_us    = duration_microseconds(wall_end - wall_start)
```

The CPU reads are outside this wall bracket. `wall_us` is the raw observed
interval; do not subtract timer overhead, percentile padding or CPU time.
The existing calibrated wall-timer overhead is recorded as evidence only.

For a cell with `frames` samples at `rate_hz`:

```text
budget_us = frames / rate_hz * 1,000,000
wall_deadline_miss = (wall_us > budget_us)
```

The comparison is strict `>`: equality is not a miss. The wall metric is
per-block, not an average or a percentile. The report's existing `wall_miss`
field is the serialized `wall_deadline_miss` value; both names refer to the
same realtime metric.

## 3. Exact load and environment conditions

### 3.1 Runner and build

The approved run must record, and the evidence auditor must independently bind:

- exact PR/source HEAD SHA and tree SHA under test;
- clean x64 Windows checkout and a fresh **Release** build of
  `suite_fx_runtime_stress_tests` only;
- actual CTest-selected Release executable path and SHA-256;
- pinned JUCE commit, compiler/toolchain, CMake/CTest, Windows SDK and
  PowerShell Desktop 5.1 versions;
- runner name/number, Windows version/build, CPU model, logical processors,
  RAM, power plan, process affinity and process architecture;
- the CTest timeout bound of exactly 180 seconds per invocation;
- no build activity, DAW, foreground audio host, generic suite, validator or
  unrelated test process competing with the capture.

Use the existing approved self-hosted Windows runner unless a separately
approved runner is named. HAMUNAPTRA currently exposes four Windows runner
services; a GitHub workflow concurrency group serializes only jobs that share
that group and is not host-wide isolation.

The measurement must hold one auditable host-wide capture lease covering the
pre-capture baseline, all measured profiles and the post-capture window. The
preferred mechanism is an owner-managed lock recognized by all four runner
services, with a lease token, owner, host, acquisition time, release time and
service/job identities recorded in the receipt. If such a shared lock is not
available, use an independent host-wide contention monitor instead: snapshot
all runner-service jobs and processes before capture, poll once per second
through capture, and snapshot again afterward. Record process identity,
command line or job identity, PID, service identity and start/stop timestamps.
Any unrecognized overlapping build, DAW, test or workflow process blocks the
wall result. A missing lock, incomplete monitor, ambiguous identity or monitor
gap is `evidence_blocked`; it must never be treated as an idle host.

Do not stop or alter runner services without owner approval. Record the
runner's before/during/after load samples: at least a 30-second pre-capture
baseline, the complete capture interval and a 30-second post-capture window,
sampled once per second. Record total CPU/load and the relevant test-process
load. An unrecorded or unexplained competing load is an evidence block, not an
implicit clean-run assumption.

### 3.2 Fixture load

The fixture uses deterministic built-in data only; it reads no customer or
project files. Each matrix cell runs exactly 160 measured callbacks, including
the first callback; there is no warm-up discard. The following profile names,
environment variables and cells are normative:

| Profile | Environment | Cells | Callback load |
|---|---|---:|---|
| `routed` | neither `VOX_FX_STRESS_AUTHORED` nor `VOX_FX_STRESS_TYPICAL` | 54 | 44.1/48/96 kHz × 64/128/512 frames × 1/4/8 initial voices × Pattern off/on; dense deterministic MIDI, four granular layers, four serial Drive slots and the existing 64-route configuration |
| `authored` | `VOX_FX_STRESS_AUTHORED=1` | 54 | Same rate/frame/voice/Pattern matrix; non-default grain shapes plus the existing step and held-ADSR routes |
| `typical` | `VOX_FX_STRESS_TYPICAL=1` | 9 | 44.1/48/96 kHz × 64/128/512 frames × one initial voice × Pattern off; curated `voxfx.riser.firefly-ladder`, with no randomized MIDI or host Pattern |

The proposed full wall gate is therefore **117 cells × 160 = 18,720 measured
callbacks**, split into three independently identified profile reports. The
reduced typical run is 9 cells × 160 = 1,440 callbacks and is diagnostic only;
it cannot satisfy full-gate coverage.

### 3.3 Instrumentation safety checks

Every profile must retain the existing controls and report them independently:

- allocation probe available and total callback allocation count exactly zero;
- Windows IAT lock and wait probes available;
- lock/wait target, resolved, matched-import and patched-hook counts complete,
  with zero patch failures and complete module enumeration;
- lock and wait positive controls present and counted; expected counts are
  recorded rather than silently assumed;
- every callback consumes its MIDI buffer and satisfies the existing finite,
  peak, DC and active-grain assertions;
- no output, state, UI, host-contract, DSP or Wavefold changes are part of the
  gate.

Before a wall-only evaluator can emit any PASS status, each case must also emit
exactly one independently auditable functional record:

```text
functional  case_id  finite_output  midi_consumed  peak_in_range  dc_in_range  grain_limit  voice8_grain_invariant  functional_pass  reasons
```

The seven assertion fields and `functional_pass` are `0` or `1`. `reasons` is
`NONE` only when all fields are `1`; otherwise it is a semicolon-separated list
from the closed set `FINITE_OUTPUT`, `MIDI_NOT_CONSUMED`, `PEAK_RANGE`,
`DC_OFFSET`, `MAX_GRAINS`, `VOICE8_GRAIN_INVARIANT`. The auditor must reject
missing, duplicate, unknown-reason or contradictory records and recompute
`functional_pass` as the conjunction of the seven assertion fields. A CTest
exit code alone is never a functional reason.

Missing, unavailable or incomplete instrumentation is not a zero and cannot
produce a wall-gate PASS. A missing functional record is evidence blockage;
an observed `functional_pass=0` is a functional failure.

## 4. Proposed test procedure

This procedure is proposal-only until the contract is approved:

1. Run the Windows PowerShell 5.1 parser/runtime fixtures before measurement.
2. Verify the exact clean source/tree and pinned JUCE checkout.
3. Configure and freshly build only the Release stress target.
4. Capture CTest `--show-only=json-v1`; resolve the actual executable from the
   inventory and hash it.
5. Run each of `routed`, `authored` and `typical` exactly once, independently,
   with unique `VOX_FX_REPORT` paths, `VOX_FX_TIMING_THREAD_CPU=1`, raw CTest
   output, JUnit output and a 180-second CTest bound. Do not stop the remaining
   profiles because an earlier profile failed; do not retry a profile.
6. Run the full TSV/provenance/manifest auditor for each profile. Inventory-only
   validation is insufficient. The auditor's exit 0 means structural integrity
   only; it is not a wall or CPU pass.
7. Preserve every profile's native CTest exit, report, raw log, JUnit,
   inventory, receipt, manifest, auditor output and environment record. Create
   a final manifest after auditor output is produced and recheck all hashes.
8. Publish a summary that reports wall-gate status, CPU status, safety status,
   provenance status and native exits as separate fields. Never collapse a
   coarse CPU diagnostic into a wall PASS or hide a real wall/safety failure.

A future implementation may use the checked-in three-profile orchestrator only
when it preserves these exact one-attempt, independent-profile semantics. A
full qualification run still requires separate owner authorization after this
contract is approved.

### 4.1 Evaluator conformance fixtures required before any wall PASS

The wall evaluator must pass these schema/negative fixtures before it can be
used on Windows evidence. The fixtures are not substitutes for the Windows
run:

| Fixture | Required input | Expected independent flags and primary status |
|---|---|---|
| `coarse_cpu_plus_functional` | One case has 0/15,625-us CPU deltas, `wall_miss=0`, and a functional record with `finite_output=0` or `midi_consumed=0` | `cpu_diagnostic_blocked=true`, `functional_failure=true`, `performance_failure=true`, primary `WALL_GATE_FAIL`; never CPU-only |
| `coarse_cpu_only` | Coarse CPU fields, complete functional records, zero wall/safety violations | `cpu_diagnostic_blocked=true`, `functional_failure=false`; only the proposed `WALL_GATE_PASS_CPU_UNKNOWN` may apply after full coverage |
| `missing_functional_record` | A case has complete wall rows but no functional record | `evidence_blocked=true`, primary `WALL_GATE_BLOCKED`; no wall PASS |
| `missing_rows_or_manifest` | No observed wall violation but callback rows, provenance or required manifest entries are missing | `evidence_blocked=true`, primary `WALL_GATE_BLOCKED` |
| `incomplete_iat` | No observed lock/wait entries, but hook coverage or positive control is incomplete | `evidence_blocked=true`, primary `WALL_GATE_BLOCKED`; zero entries are not accepted as proof |
| `timeout_or_skip` | Timeout, crash or skipped execution with no complete wall result | `execution_failure=true`, `evidence_blocked=true`, primary `WALL_GATE_BLOCKED` |
| `observed_wall_then_crash` | A retained wall miss precedes a crash or incomplete tail | `wall_failure=true`, `execution_failure=true`, `evidence_blocked=true`, primary `WALL_GATE_FAIL`; both conditions remain visible |
| `host_contention` | Missing host lease, monitor gap or unrecognized concurrent runner process | `evidence_blocked=true`, primary `WALL_GATE_BLOCKED`; never infer an idle host |

These fixtures must exercise both the report parser and the final status
precedence, not merely assert a process exit code. The mixed functional/CPU
fixture is mandatory because the current C++ fixture shares an `errors` counter
between functional assertions and CPU insufficiency; the future report must
separate those causes before a wall-only PASS evaluator exists.

## 5. Failure and disposition criteria

The following are proposed machine-checkable semantics, not approved release
criteria. The evaluator must emit the orthogonal flags before choosing one
primary status:

```text
wall_failure              = any wall_deadline_miss == 1
safety_failure            = observed allocation, lock or wait entry, or failed IAT positive control
functional_failure        = any audited functional_pass == 0
execution_failure         = crash, timeout, skip or incomplete execution
performance_failure       = wall_failure or safety_failure or functional_failure
evidence_blocked          = missing, incomplete or unverifiable measurement/evidence
cpu_diagnostic_blocked    = cpu_measurement_insufficient == true
```

An unavailable probe is not an observed zero: it sets `evidence_blocked`, not
`safety_failure`. A missing functional record is not `functional_pass=1`: it
sets `evidence_blocked`. A CTest exit code is never used by itself to infer a
functional or wall cause.

### Deterministic primary-status precedence

1. If `wall_failure`, `safety_failure` or `functional_failure` is true, the
   primary status is `WALL_GATE_FAIL`.
2. Otherwise, if `execution_failure` or `evidence_blocked` is true, the primary
   status is `WALL_GATE_BLOCKED`.
3. Otherwise, if all full-matrix wall/safety/functional conditions pass while
   `cpu_diagnostic_blocked` is true, the proposed primary status is
   `WALL_GATE_PASS_CPU_UNKNOWN`.
4. No other status may claim a wall PASS without complete rows, functional
   records, host-isolation evidence and provenance.

This precedence makes mixed cases deterministic: an observed wall/safety/
functional violation remains FAIL even when a crash or evidence gap also sets
`execution_failure` or `evidence_blocked`; both flags must be retained. If no
runtime violation is observed and the run cannot be fully evaluated, it is
BLOCKED, never PASS.

### `WALL_GATE_FAIL`

Set this status if any measured callback has `wall_deadline_miss=1`, meaning
`wall_us > budget_us`. Any single per-block miss fails the affected profile;
any miss in the complete 117-cell set fails the proposed full gate. Do not
replace this with p95, p99, maximum-only, average, or a ratio threshold.

Also set `WALL_GATE_FAIL` for observed safety or functional violations:

- callback allocation count is nonzero;
- an observed lock or wait entry is nonzero;
- an audited functional assertion is false, including finite/MIDI/peak/DC/
  grain reason codes;
- a retained wall miss exists before a later crash or incomplete tail.

### `WALL_GATE_BLOCKED`

Set this status when the wall result cannot be trusted or the execution is not
complete, without an independently observed runtime violation:

- source/tree/EXE/JUCE/runner provenance cannot be independently verified;
- the artifact manifest does not cover and hash every required input/output;
- the report is malformed, incomplete, duplicated or from the wrong
  executable/profile;
- callback rows, functional records, explicit miss rows or required profiles
  are missing;
- the wall clock is unavailable, invalid or its bracket is not the specified
  bracket;
- IAT coverage, module enumeration, positive control or hook patching is
  unavailable/incomplete;
- a timeout, crash or skipped test leaves no complete wall result;
- required runner-load, host-lease or contention-monitor records are absent,
  ambiguous, show a gap or show an unrecognized competing process.

These are evidence/observability failures, not zeroes and not wall passes.

### `WALL_GATE_PASS_CPU_UNKNOWN` (proposed, not currently emitted)

This is the only proposed result for a wall-only pass with the known coarse CPU
clock. It requires all 117 cells and 18,720 callbacks, zero wall misses, zero
allocations, zero observed lock/wait entries, complete positive-controlled
probes, complete per-case functional records, clean host-wide isolation
evidence and independently verified artifacts. It explicitly carries
`cpu_attribution=UNKNOWN` alongside the separate `CPU_DIAGNOSTIC_BLOCKED`
disposition and cannot be used to claim a CPU budget pass.

A future high-resolution CPU trace may add a separate CPU disposition; it must
not weaken or replace these wall, safety, functional or evidence checks.

## 6. Evidence contract

Every invocation must retain, outside the source tree unless explicitly
approved otherwise:

- exact source HEAD/tree and base reference;
- pinned JUCE SHA and clean-checkout receipt;
- runner/environment/load receipt, PowerShell/toolchain/build versions and
  power/affinity information;
- host-wide lease token or contention-monitor snapshots, process/job/service
  identities, timestamps, polling gaps and the explicit no-contention result;
- CTest inventory and actual selected executable path/SHA-256;
- fresh-build configure/build logs;
- one TSV per profile containing all 160 block rows per cell, all raw CPU
  values, wall flags, raw CPU flags and explicit miss rows;
- exactly one functional assertion record per case with all seven assertion
  fields, `functional_pass` and reason codes;
- raw CTest stdout/stderr, native exit code and JUnit output;
- initial and final artifact manifests with SHA-256 entries;
- full auditor JSON on exit 0 or raw auditor output on failure;
- any CPU-resolution/unknown diagnostic and the original red run artifacts.

The wall evaluator/auditor must independently verify git HEAD/tree, executable
and receipt hashes, manifest membership, matrix coverage, strict `>` wall
parity, all percentiles, probe coverage, raw CPU quantization and zero-delta
semantics. It must also reconcile exactly one functional assertion record per
case, reject unknown/missing reason codes, compute `functional_failure`, and
reconcile the host-wide lease or contention-monitor evidence into
`evidence_blocked`. A successful audit is an integrity result only.

Artifact retention must be longer than the service's short default where the
result is to support an acceptance decision; otherwise an external immutable
copy and its digest must be recorded before expiry. The run from
`45130013544` remains historical diagnostic evidence and must not be overwritten
or relabeled.

## 7. Approval and versioning

This document is `FX-T18-WALL-GATE v1.1-proposed`. Owner approval is required
for the contract itself before any full 117-cell run is used to assess it.
Approval of the contract is distinct from authorization to dispatch a Windows
job. Any change to profile coverage, load, deadline comparison, safety checks,
failure status, evidence retention or CPU-unknown semantics requires a version
bump and a new owner review.

Until approval:

- no full matrix is dispatched;
- no existing threshold is relaxed or changed;
- no prior red CTest or CPU diagnostic is renamed PASS;
- no qualification, release, DAW/host acceptance or merge follows from this
  proposal.
