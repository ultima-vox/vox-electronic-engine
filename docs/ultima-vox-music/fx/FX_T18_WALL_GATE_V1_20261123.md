# FX-T18-WALL-GATE v1.0 — proposed wall-deadline diagnostic contract

**Contract ID:** `FX-T18-WALL-GATE`<br>
**Version:** `1.0-proposed`<br>
**Date:** 2026-11-23<br>
**Status:** Draft for separate owner approval; not a release-acceptance decision<br>
**Scope:** FX-T18 callback completion evidence only

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
approved runner is named. Do not stop or alter runner services without owner
approval. Record the runner's before/during/after load samples: at least a
30-second pre-capture baseline, the complete capture interval and a
30-second post-capture window, sampled once per second. Record total CPU/load
and the relevant test-process load. An unrecorded or unexplained competing load
is an evidence block, not an implicit clean-run assumption.

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

Missing, unavailable or incomplete instrumentation is not a zero and cannot
produce a wall-gate PASS.

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

## 5. Failure and disposition criteria

The following are the proposed machine-checkable outcomes. They are not yet
release criteria:

### `WALL_GATE_FAIL`

Set this status if any measured callback has `wall_deadline_miss=1`, meaning
`wall_us > budget_us`. Any single per-block miss fails the affected profile;
any miss in the complete 117-cell set fails the proposed full gate. Do not
replace this with p95, p99, maximum-only, average, or a ratio threshold.

Also set `WALL_GATE_FAIL` for any of the following independent safety failures:

- callback allocation count is nonzero;
- any observed lock or wait entry is nonzero;
- IAT coverage, module enumeration, positive control or patching is unavailable
  or incomplete;
- finite/output/grain/MIDI/functional assertions fail;
- a callback row, cell, profile, explicit miss row or required artifact is
  missing, duplicated or cannot be reconciled;
- the test crashes, is skipped, exceeds the 180-second bound or returns a
  failure not separately classified as infrastructure.

### `WALL_GATE_BLOCKED`

Set this status when evidence integrity or test observability is insufficient:

- source/tree/EXE/JUCE/runner provenance cannot be independently verified;
- the artifact manifest does not cover and hash every required input/output;
- the report is malformed, incomplete or from the wrong executable/profile;
- the wall clock is unavailable, invalid or its bracket is not the specified
  bracket;
- required runner-load/environment records are absent or unexplained.

CPU clock resolution is deliberately not a wall-observability condition. It is
reported as a separate `CPU_DIAGNOSTIC_BLOCKED` disposition whenever
`cpu_measurement_insufficient=true`. That disposition must remain visible and
blocking for CPU qualification, but it must never be relabeled as a wall miss
or silently converted into a CPU pass. A future approved wall-only evaluator
may report `WALL_GATE_PASS_CPU_UNKNOWN` when all wall, safety and evidence
conditions pass while the separate CPU disposition remains blocked. Until that
evaluator and this contract are approved, the existing nonzero CTest result
remains red.

### `WALL_GATE_PASS_CPU_UNKNOWN` (proposed, not currently emitted)

This is the only proposed result for a wall-only pass with the known coarse CPU
clock. It requires all 117 cells and 18,720 callbacks, zero wall misses, zero
allocations, zero lock/wait entries, complete positive-controlled probes,
functional success, clean environment evidence, and independently verified
artifacts. It explicitly carries `cpu_attribution=UNKNOWN` alongside the
separate `CPU_DIAGNOSTIC_BLOCKED` disposition and cannot be used to claim a CPU
budget pass.

A future high-resolution CPU trace may add a separate CPU disposition; it must
not weaken or replace these wall and safety checks.

## 6. Evidence contract

Every invocation must retain, outside the source tree unless explicitly
approved otherwise:

- exact source HEAD/tree and base reference;
- pinned JUCE SHA and clean-checkout receipt;
- runner/environment/load receipt, PowerShell/toolchain/build versions and
  power/affinity information;
- CTest inventory and actual selected executable path/SHA-256;
- fresh-build configure/build logs;
- one TSV per profile containing all 160 block rows per cell, all raw CPU
  values, wall flags, raw CPU flags and explicit miss rows;
- raw CTest stdout/stderr, native exit code and JUnit output;
- initial and final artifact manifests with SHA-256 entries;
- full auditor JSON on exit 0 or raw auditor output on failure;
- any CPU-resolution/unknown diagnostic and the original red run artifacts.

The auditor must independently verify git HEAD/tree, executable and receipt
hashes, manifest membership, matrix coverage, strict `>` wall parity, all
percentiles, probe coverage, raw CPU quantization and zero-delta semantics. A
successful audit is an integrity result only.

Artifact retention must be longer than the service's short default where the
result is to support an acceptance decision; otherwise an external immutable
copy and its digest must be recorded before expiry. The run from
`45130013544` remains historical diagnostic evidence and must not be overwritten
or relabeled.

## 7. Approval and versioning

This document is `FX-T18-WALL-GATE v1.0-proposed`. Owner approval is required
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
