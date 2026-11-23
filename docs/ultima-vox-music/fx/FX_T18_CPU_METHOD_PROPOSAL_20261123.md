# FX-T18 CPU measurement method proposal

**Status: proposal for separate TL review only.** This document does not change
FX-T18 acceptance thresholds, authorize a Windows run, or make a CPU/realtime
qualification claim. It preserves the decision boundary exposed by the
owner-authorized typical-only run `45130013544`.

## Evidence requiring a method decision

The run completed the reduced typical profile at source
`348bb2c807b3abd7ca75efc7b4a8e5bd8f007d8b` and produced a full provenance/audit
PASS, but CTest correctly remained red with `CPU_MEASUREMENT_INSUFFICIENT`:

- 9 cells × 160 callbacks, 0/1,440 wall deadline misses;
- 17 raw CPU flags, all exactly 15,625 microseconds;
- 1,423 raw CPU deltas exactly zero;
- observed `GetThreadTimes` quantum 15,625 microseconds, larger than every
  reduced-profile callback budget;
- `cpu_attribution=UNKNOWN`, `performance_failure=false`, and no green CPU
  claim.

The raw TSV, receipt, manifest, EXE hash and auditor result remain the source
of truth; the run metadata is recorded in
[`evidence/windows-45130013544.txt`](evidence/windows-45130013544.txt).

## Option A — validated ETW/WPR scheduler evidence (preferred proposal)

Use a separately reviewed Windows trace capture that can establish when the
measured test thread was actually running, rather than infer running time from
coarse cumulative `GetThreadTimes` reads. The exact provider/event contract is
not selected by this proposal; it must be verified on the approved runner
before implementation.

A conforming method would need to:

1. Identify the callback thread unambiguously and emit a monotonic callback
   sequence marker at the same two boundaries as the existing report. The
   marker must be correlation data only; it must not add allocation, locking or
   blocking to the measured callback.
2. Capture scheduler context-switch/running intervals for that thread with a
   documented timestamp source and calibrated relationship to the wall clock.
   The trace configuration must record provider version, buffer settings,
   timestamp mode, CPU affinity and any lost-event counters.
3. Reconstruct the thread-running interval intersecting each callback bracket.
   A running interval crossing a callback boundary is normal: after the two
   time domains are calibrated, clip/account its overlap with the callback
   bracket rather than rejecting it. A missing marker, ambiguous thread
   identity, lost event, timestamp discontinuity or unresolved overlap must
   make the CPU attribution UNKNOWN; do not interpolate those cases silently.
4. Validate resolution and attribution with a positive-control fixture on the
   same runner: deliberately separated known-running and known-not-running
   intervals, repeated enough to expose event loss and timestamp quantization.
   The control must be independently checked against the trace metadata.
5. Extend the TSV with the trace identity, callback correlation status,
   observed resolution, lost-event count and reconstructed running interval,
   while retaining every existing raw `GetThreadTimes` value and raw CPU flag.
   The existing wall deadline field remains independent and authoritative for
   callback completion.

The implementation gate for this option is therefore not merely a small
measured number. TL must first approve the trace contract, then review a
Windows fixture showing complete correlation, calibrated timestamps, no lost
events and a resolution materially below the strictest callback budget. Until
those conditions are demonstrated, `cpu_attribution` remains UNKNOWN and
`CPU_MEASUREMENT_INSUFFICIENT` remains blocking.

## Option B — explicitly wall-only deadline contract

If the release question is callback completion rather than attributable thread
CPU consumption, TL may instead define an explicit wall-only diagnostic gate:

- `wall_deadline_miss` remains the sole realtime metric;
- all raw CPU deltas and flags remain retained as non-attributable diagnostics;
- `cpu_measurement_insufficient=true` remains visible and blocking for any
  CPU-based claim;
- the owner separately defines the wall-miss threshold, sampling/profile scope,
  load conditions and evidence-retention requirement;
- zero CPU deltas never lower the wall threshold or imply zero work.

This is a contract decision, not a threshold relaxation and not a qualification
pass. It must not be mixed with language claiming that the callback's CPU use
fit its budget. The concrete draft for separate review is
[`FX-T18-WALL-GATE v1.0`](FX_T18_WALL_GATE_V1_20261123.md).

## Explicitly excluded

- No unverified `QueryThreadCycleTime` substitution.
- No extrapolation from zero `GetThreadTimes` deltas.
- No discard or relabeling of the original red run or the 17 raw CPU flags.
- No full 117-cell/three-profile run as a substitute for method validation.
- No FX DSP, Wavefold, parameter, state, UI or host-contract changes.

**Requested TL decision:** select Option A and approve its trace contract for a
separate instrumentation change, or explicitly adopt Option B for the relevant
wall-only diagnostic gate. Neither option authorizes another Windows run in
this proposal.
