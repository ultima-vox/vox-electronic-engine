# FX-T18 host-wide witness schema v1.2

`capture-fx-t18-host-witness.ps1` is a Windows PowerShell 5.1 observation
program for the proposed wall gate. It never starts, stops, kills, disables,
restarts, or reconfigures a process or service. It observes the whole host
beside an already-authorized runner and exits `2` when the evidence boundary is
not established.

This branch is **READY_FOR_OWNER_ATTESTATION** only. No live positive Capture,
Windows dispatch, qualification result, or `WALL_GATE_PASS` is claimed.

## Trust boundary

A production Capture requires four separately retained payloads and detached
CMS signatures:

- `VOX_FX_T18_IDENTITY_POLICY_V1`: a time-bound owner-reviewed baseline with
  exactly four SHA-256 HAMUNAPTRA runner-service identities, all approved
  process identities, dynamic-spawn rules, executable/path hashes, parent and
  service identities, and PID plus process-creation-time reconciliation;
- `VOX_FX_T18_HOST_LEASE_RECEIPT_V1`: owner-issued host/lease/policy binding
  with a validity interval covering the whole capture;
- `VOX_FX_T18_EVENT_LOSS_RECEIPT_V1`: an independently retained
  `Microsoft-Windows-Kernel-Process/ETW` trace with provider/buffer loss
  counters, received-event count, collector hash and a trace interval covering
  the capture. WMI subscription success and a JSON `lost_events=0` assertion do
  not satisfy this contract;
- `VOX_FX_T18_EXECUTION_BINDING_V1`: owner-issued binding of all routed,
  authored and typical trusted CTest start/end intervals to the host, runner
  job, source HEAD/tree, selected executable SHA-256, lease and policy.

Each payload is signed as detached CMS by an owner certificate. The owner
passes the certificate SHA-1 thumbprint out-of-band; it is not read from a
payload or environment variable. Capture verifies the certificate chain,
signature and pinned thumbprint, then records data/signature hashes and the
verification method. The Python evaluator independently re-hashes the payloads
and signatures, requires the same signer thumbprint, reconciles signed claims
with trusted CTest receipts, and requires every artifact in the host manifest.
No inline `owner_approved`, `verified`, or `lost_events=0` field is accepted
without that signed provenance.

If the owner cannot produce a trustworthy signed lease, identity baseline,
execution binding, or measurable ETW loss trace, the result is `BLOCKED`.
There is no fallback to owner self-attestation, a WMI-only zero-loss claim, or
an idle-host assumption.

## Public witness and private inventory

The public `VOX_FX_T18_HOST_WITNESS_V1` receipt uses `schema_version=2` and
contains only non-identifying summaries, timestamps, decisions, identity
hashes and artifact hashes:

- `samples` has a monotonic/deadline-reconciled UTC timestamp, scheduled
  timestamp, monotonic elapsed value, process count, approved count, unknown /
  unexpected / PID-reuse counters, query status and a SHA-256 of the matching
  raw sample;
- `process_events` has provider timestamp, start/stop kind, known/approved
  decision, identity digest and decision code. It never serializes a PID,
  process name, executable path or command line;
- `identity_policy`, `runner_service_identities`, `runner_lease` and
  `execution_binding` expose only the fields needed to bind the host, job,
  source, executable, lease and policy. Command-line and lease tokens are
  literal `<redacted>` values;
- `event_trace` distinguishes the WMI observation stream from the independently
  measurable ETW loss source and records its interval, collector hash, received
  count and loss counters;
- `attestation_proofs` records each payload/signature path and SHA-256 plus
  the pinned signer thumbprint and method;
- `execution_binding.runner_receipts` lists all three trusted runner-receipt
  paths and SHA-256s; the signed binding and final host manifest must reconcile
  them before CTest intervals are trusted;
- `private_inventory_path` and `private_inventory_sha256` identify a
  write-restricted sidecar. The sidecar retains raw process/service identity
  data for owner audit, but is not a public artifact. The final host manifest
  hashes it without publishing its contents.

The policy matches every observed process by name/path/executable hash,
parent/service identity and either an owner-reviewed PID/start time or an
explicit owner-reviewed dynamic rule. Unknown identities, unexpected runner
or DAW/manual-build work, a short-lived competitor, PID reuse, dynamic identity
mismatch, provider/query failure, or any unexplained observation gap blocks.
No unknown process is ignored and no policy may be represented as “allow all”.
The four service hashes must be present in the observed reconciliation.

Capture samples on monotonic one-second deadlines across at least 30 seconds
pre, the complete three-profile CTest interval, and 30 seconds post. A sample
stream must retain `seconds + 1` samples per phase, strictly increasing UTC and
monotonic times, no interval over two seconds, and no recorded deadline gap.
The signed execution binding must match each trusted CTest start/end; a phase,
CTest, host, source, executable, lease or policy mismatch is blocked.

## Sanitized blocked receipts

Missing or malformed policy/signature, lease, ETW loss proof, execution binding,
private inventory, four-service baseline, dynamic identity, PID-reuse check,
provider registration, query, cadence, or hash reconciliation writes a
`VOX_FX_T18_HOST_WITNESS_V1` receipt with:

- `evidence_blocked=true`, `qualification_prohibited` as applicable,
  `diagnostic.reason_codes`, sanitized summaries and recommended exit `2`;
- no raw samples, event identities, names, PIDs or command lines;
- only approved counters and hashes, with `lost_events=null` unless a verified
  signed ETW receipt actually supplies the zero.

If serialization fails, Capture makes a second minimal sanitized persistence
attempt and still exits `2`. It never edits a failed receipt into a positive
receipt.

## Fixtures

`test-fx-t18-host-witness.ps1` runs the real Capture entrypoint for missing
policy/lease/loss, forged or unsigned fixture proofs, and simulated provider
failure. It also runs `OfflineSynthetic` short-lived-competitor and unknown-PID
fixtures. Every fixture requires explicit `-FixtureMode`, emits
`fixture_mode=true` and `qualification_prohibited=true`, retains no public raw
inventory, and exits blocked. A fake signature or fixture JSON can therefore
exercise parser and negative behavior but cannot yield
`WALL_GATE_PASS_CPU_UNKNOWN` or any production wall pass.

The evaluator fixture script additionally covers dynamic/PID-reuse mismatch,
other runner/DAW/manual-build identities, short-lived competitors, missing
one-Hz samples, phase/CTest mismatch, missing/forged owner proofs, unverified
WMI loss, and failed artifact/signature hash reconciliation. Run only local
schema fixtures; no command here authorizes a Windows run.
