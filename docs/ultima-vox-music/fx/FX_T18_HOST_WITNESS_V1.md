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
  timestamp, monotonic elapsed value, full-host CIM process count, successfully
  hashed executable count, measured CIM-plus-hash duration and explicit
  one-Hz cadence result, process count, approved count, unknown / unexpected /
  PID-reuse counters, query status and a SHA-256 of the matching raw sample;
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
  parsed ETW loss source and records its interval, collector hash, retained
  `tracerpt -summary` path/hash, retained collection metadata path/hash, parser
  receipt/hash, identity-join provenance,
  received count and loss counters; the parser receipt must reconcile its
  provider, collector hash, summary-derived coverage/session, counters and
  complete process-event list to the signed loss payload, and the host manifest
  must cover the retained summary and identity join;
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

## ETW/WMI correlation and lineage contract

The WMI event stream is retained in the owner-private inventory and the ETW
stream is decoded from the raw ETL by `evidence/parse-fx-t18-etl.py`. The
finalizer/evaluator does not compare provider timestamp strings. Each side is
normalized to UTC ticks and matched one-to-one using this immutable key:

```
event kind, PID, process creation time, process identity digest,
process name, exact executable path, executable SHA-256, parent PID,
owner-baselined runner-service identity set
```

The provider event timestamps are then compared as normalized UTC datetimes
with `MAX_EVENT_CORRELATION_SKEW_SECONDS = 2.0`. A zero-candidate,
multiple-candidate, reused candidate, extra WMI event, extra ETW event, or
identity/lifecycle mismatch blocks. Precision and offset differences are
therefore harmless only when they identify exactly one same lifecycle within
that explicit bound; they are never made harmless by rounding or by set
conversion.

Every raw process/event identity includes `service_ancestry_verified=true`,
`lineage_verification=runner_service_ancestry_v1` (or a separately verified
`signed_job_lineage_v1` proof), and an ordered child-to-runner-service
`service_ancestry`. Every node carries PID, creation time, name, exact path,
exact image SHA-256, parent PID, service identities and its own digest. A
non-service CTest/native child is approved only when its signed policy entry
also pins its exact path/hash and exact parent image path/hash; a PID-only
wildcard is not a child authorization. The four direct HAMUNAPTRA runner
service identities remain the only permitted roots for runner-job processes.

Full-host enumeration also has an explicit owner-signed `host_baseline_id` and
`host_baseline_processes` inventory. A normal Windows service or protected root
that is not part of the runner job is not silently treated as an unknown
competitor: it may be approved only as `identity_scope=host_baseline` with
`non_competing=true`, exact executable path/hash, the complete exact service
identity set (or an explicit `service_identity_sha256=none` for a hashable root
with no service), `lineage_mode=host_baseline_v1`, and an exact parent
image/path/hash when its parent is dynamic. Deny regexes still have precedence,
and the owner must
baseline every observed host process; an omitted host-baseline entry remains a
block. A forged parent node, discontinuous PID chain, changed parent
image/hash, runner process rooted outside the four runner services, or host
process absent from the signed baseline blocks.

## Independent raw ETL parser and replay

`evidence/parse-fx-t18-etl.py` is the owner-independent parser/verifier. The
production toolchain is pinned to Microsoft Windows SDK `tracerpt.exe`
`10.0.26100.1`, invoked without a shell as:

```
tracerpt.exe <raw-etl> -o <decoded-xml> -of XML -lr -summary <tracerpt-summary> -y
```

The parser consumes `Microsoft-Windows-Kernel-Process` process event IDs 1
(ProcessStart) and 2 (ProcessStop). A stop XML record may omit image and parent
fields on some provider builds; the parser reconciles those fields to the
matching retained start record and blocks an unmatched stop rather than
inventing identity. `-lr` means less-restrictive decoding;
loss counters and trace metadata come from the separately requested retained
`tracerpt -summary` artifact (`Events Lost`, `Buffers Lost`, `Log Start Time`,
`Log End Time`, and `Logger Name`/session), not stdout/stderr or a JSON claim.
A receipt records the raw ETL SHA-256, summary path/SHA-256, retained
`VOX_FX_T18_ETW_COLLECTION_METADATA_V1` path/SHA-256, parser/toolchain name
and version, executable path, exact tracerpt argv, derived coverage/session
source, event source, counter source, decoded digests, identity-join artifact
hash, and replay command. Production caller arguments cannot supply coverage or
session metadata.
The production parser requires an owner-private
`VOX_FX_T18_ETW_IDENTITY_JOIN_V1` join for the path/hash/service/ancestry facts
that are not historical fields in the raw Kernel-Process event. `verify_receipt`
reruns the decoder against the retained raw bytes, reapplies the retained join,
and compares the raw core digest, complete enriched event digest and counters
before the wall evaluator accepts the signed receipt. The join is included in
the host manifest. The offline JSON raw-fixture mode is explicitly
qualification-prohibited and is only used to exercise the replay and tamper
negatives.

The owner replay is:

```
python evidence/parse-fx-t18-etl.py --etl D:\\owner-evidence\\fx-t18\\process.etl \\
  --output D:\\owner-evidence\\fx-t18\\process-parse.json \\
  --summary-output D:\\owner-evidence\\fx-t18\\process-tracerpt-summary.txt \\
  --collection-metadata D:\\owner-evidence\\fx-t18\\etw-collection-metadata.json \\
  --tracerpt C:\\Windows\\System32\\tracerpt.exe \\
  --identity-join D:\\owner-evidence\\fx-t18\\etw-identity-join.json
python evidence/parse-fx-t18-etl.py --verify-receipt \\
  --etl D:\\owner-evidence\\fx-t18\\process.etl \\
  --receipt D:\\owner-evidence\\fx-t18\\process-parse.json
```

A regenerated JSON receipt for tampered ETL is not sufficient: replay binds
its raw-byte hash and the evaluator still applies the signed path/hash/parent/
service policy and ETW/WMI one-to-one reconciliation. The conformance suite
includes a tampered raw fixture with internally regenerated parser claims and
requires a block.

## Private inventory semantic reconciliation and cadence

The private sidecar is `VOX_FX_T18_PRIVATE_PROCESS_INVENTORY_V1` with
canonicalization `fx-t18-private-sample-canonical-v2-utc-ticks`. For every
sample, the evaluator recomputes the canonical SHA-256 over normalized sample
times, monotonic/deadline fields, cadence measurements, every raw process
decision, every raw event, and ordered ancestry nodes. It reconciles the
sidecar interval, policy hash, sample fields, process/event records, aggregate
events, per-sample counts, query status, public event summaries and all
public non-identifying counters. An all-approved public witness paired with
an unauthorized or edited private process is blocked even when its sidecar
file hash and regenerated public claims agree.

Each sample also reports the measured full-host CIM process count, number of
successfully hashed executable images, total CIM plus SHA-256 duration and
`full_host_cim_hash_cadence_met`. Production accepts only a completed full
host query/hash operation taking no more than 1000 ms on each 1000 ms deadline;
missing, false, or over-budget measurements block. The bounded Windows smoke
must run the real script under Windows PowerShell Desktop 5.1, and the owner
must retain the measured cadence fields; this source-only environment does
not claim that smoke or benchmark has run.

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
