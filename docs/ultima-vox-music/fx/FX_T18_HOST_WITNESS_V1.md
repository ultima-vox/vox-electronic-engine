# FX-T18 host-wide witness schema v1.3

`capture-fx-t18-host-witness.ps1` is a Windows PowerShell 5.1 observation
program for the proposed wall gate. It never starts, stops, kills, disables,
restarts, or reconfigures a process or service. It observes the whole host
beside an already-authorized runner and exits `2` with a blocked
`VOX_FX_T18_HOST_OBSERVATION_V1` receipt. It never requires future ETW/CTest
facts or signs a post-capture claim. `finalize-fx-t18-host-witness.py` is the
separate post-capture reconciliation step that consumes the immutable blocked
observation only after owner proofs exist.

This source packet is **PENDING_OWNER_REVIEW**. No live positive Capture,
Windows dispatch, qualification result, owner attestation, or `WALL_GATE_PASS`
is claimed.

## Trust boundary

The protocol has two non-interchangeable phases. Observation requires only the
already-issued, owner-signed identity policy and host lease. It records private
WMI observations and sanitized public counters while remaining blocked.
After the interval closes, an independent owner ETW parser and the trusted
runner produce the remaining signed payloads. `finalize-fx-t18-host-witness.py`
then verifies and reconciles all four payloads before writing the only receipt
that the production evaluator accepts:

- `VOX_FX_T18_IDENTITY_POLICY_V1` (verified before observation): a time-bound owner-reviewed baseline with
  exactly four SHA-256 HAMUNAPTRA runner-service identities, all approved
  process identities, dynamic-spawn rules, executable/path hashes, parent and
  service identities, and PID plus process-creation-time reconciliation;
- `VOX_FX_T18_HOST_LEASE_RECEIPT_V1`: owner-issued host/lease/policy binding
  with a validity interval covering the whole capture;
- `VOX_FX_T18_EVENT_LOSS_RECEIPT_V1` (issued after observation): an
  independently retained `Microsoft-Windows-Kernel-Process/ETW` trace with
  provider/session/buffer loss counters, received-event count, collector hash,
  parser-receipt hash, and a trace interval covering the capture. Its
  `process_events` are parsed ETW identities, including PID/start time,
  executable/path, parent and service context, and the payload carries the
  immutable observation SHA-256. WMI subscription success, an arbitrary `.etl`
  byte file, and a JSON `lost_events=0` assertion do not satisfy this contract;
- `VOX_FX_T18_EXECUTION_BINDING_V1` (issued after the three profiles finish):
  owner-issued binding of all routed, authored and typical trusted CTest
  start/end intervals to the host, runner job, source HEAD/tree, selected
  executable SHA-256, lease, policy and immutable observation SHA-256.

Each payload is signed as detached CMS by an owner certificate. The owner
passes the certificate SHA-1 thumbprint out-of-band; it is not read from a
payload, witness, runner receipt or environment variable. Observation verifies
only policy and lease, including the certificate chain and online revocation
policy. Finalization verifies all four CMS signatures against that independent
thumbprint, re-hashes every payload/signature, reconciles
signed claims with parsed ETW and trusted CTest receipts, and requires every
artifact in the host manifest. The production evaluator requires the same
independently supplied thumbprint again; a thumbprint learned from the mutable
witness is never a trust anchor. No inline `owner_approved`, `verified`, or
`lost_events=0` field is accepted without that signed provenance.

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
- `process_events` has WMI provider timestamp, start/stop kind, known/approved
  decision, identity digest and decision code. It never serializes a PID,
  process name, executable path or command line. Finalization requires exact
  event-kind/timestamp/digest reconciliation with the independently parsed
  signed ETW `process_events`; an ETW-only competitor or WMI-only event blocks;
- `identity_policy`, `runner_service_identities`, `runner_lease` and
  `execution_binding` expose only the fields needed to bind the host, job,
  source, executable, lease and policy. Command-line and lease tokens are
  literal `<redacted>` values;
- `event_trace` distinguishes the WMI observation stream from the independently
  parsed ETW loss source and records its interval, collector hash, parser
  receipt/hash, received count and loss counters; the parser receipt must
  reconcile its provider, collector hash, counters and complete process-event
  list to the signed loss payload;
- `attestation_proofs` records each payload/signature path and SHA-256 plus
  the pinned signer thumbprint and method;
- `execution_binding.runner_receipts` lists all three trusted runner-receipt
  paths and SHA-256s; the signed binding and final host manifest must reconcile
  them before CTest intervals are trusted;
- `observation_path` and `observation_sha256` bind the final witness to the
  immutable blocked observation. The post-capture ETW and execution-binding
  payloads must carry the same observation hash; changing samples after the
  interval therefore cannot be finalized;
- `private_inventory_path` and `private_inventory_sha256` identify a
  write-restricted sidecar. The sidecar retains raw process/service identity
  data for owner audit, but is not a public artifact. The final host manifest
  hashes it without publishing its contents.

The policy matches every observed process by exact name or reviewed regex,
exact executable path and SHA-256, parent PID identity, service identity and
either an owner-reviewed PID/start time or an explicit owner-reviewed dynamic
rule. Deny regexes have precedence over allow entries. Dynamic rules that omit
path/hash/parent/service identity are invalid. Unknown identities, unexpected
runner or DAW/manual-build work, a short-lived competitor, PID reuse, dynamic
identity mismatch, provider/query failure, or any unexplained observation gap
blocks. No unknown process is ignored and no policy may be represented as
“allow all”.
The four service hashes must be present in the observed reconciliation.

Capture samples on monotonic one-second deadlines from one global stopwatch
origin across at least 30 seconds pre, the complete three-profile CTest
interval, and 30 seconds post. A sample stream must retain `seconds + 1`
samples per phase, strictly increasing UTC and global monotonic times, no
interval over two seconds, and no recorded deadline gap. Resetting the origin
at a phase boundary is invalid.
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

`test-fx-t18-host-witness.ps1` runs the real observation entrypoint for missing
policy/lease, forged or unsigned fixture proofs, lifecycle/provider failure,
and explicit `OfflineSynthetic` short-lived-competitor and unknown-PID
fixtures. Every fixture requires explicit `-FixtureMode`, emits
`fixture_mode=true` and `qualification_prohibited=true`, retains no public raw
inventory, and exits blocked. `test-evaluate-fx-t18-wall-gate.py` exercises
blocked observation-to-finalization, global sampler timing, ETW-only
competitors absent from WMI, mixed-name same-PID lifecycle, strict deny/path/
hash/parent/service policy, independent owner-pin mismatch, parsed-ETW hash
reconciliation and fixture prohibition. A fake signature or fixture JSON can
therefore exercise parser and negative behavior but cannot yield
`WALL_GATE_PASS_CPU_UNKNOWN` or any production wall pass.

Run only local schema fixtures; no command here authorizes a Windows run.
