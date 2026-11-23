# FX-T18 host-wide witness schema v1

`capture-fx-t18-host-witness.ps1` is a standalone Windows PowerShell 5.1
observation tool for the proposed wall gate. It does not launch, stop, kill,
disable or reconfigure a process or service. It observes the whole host while an
already-authorized runner performs a capture.

## Receipt and independent attestations

The UTF-8 JSON receipt has `format=VOX_FX_T18_HOST_WITNESS_V1` and records:

- `sample_interval_ms=1000` plus independently timestamped `pre`, `during` and
  `post` samples. Capture mode records `seconds + 1` samples for each phase so
  the timestamp span, not just the phase label, proves the declared duration;
  pre/post windows are each at least 30 seconds;
- `host_observation_mode=process_snapshot_and_event_trace`;
- `samples`, with a UTC timestamp, PID/name/parent PID, process start time,
  owner-policy decision, protected `service_identity`, and literal
  `command_line=<redacted>`; command lines are never queried or serialized;
- `process_events` from the `Win32_ProcessStartTrace` and
  `Win32_ProcessStopTrace` indication providers. Each event uses the provider's
  `TIME_CREATED` timestamp, PID/name, known-identity flag, policy decision and
  redacted command-line field. An unparsed event is evidence-blocking;
- `event_trace.available`, `coverage_complete`, `lost_events`, exact provider,
  `loss_accounting`, and an externally supplied
  `VOX_FX_T18_EVENT_LOSS_RECEIPT_V1`. Capture mode refuses to claim complete
  event coverage when that owner-attested loss receipt is absent, malformed,
  expired for the capture interval, or reports any loss;
- `identity_policy` and its SHA-256 plus the external policy path. The
  evaluator recomputes the hash and checks every process/event identity against
  the owner-approved policy; a boolean `approved_by_policy` field alone is not
  trusted;
- `runner_lease` with host, owner, lease ID, and a redacted token, plus the
  external `VOX_FX_T18_HOST_LEASE_RECEIPT_V1` path and SHA-256. Lease proof is
  never obtained from environment strings. The receipt must be an owner-
  attested file whose validity interval covers the witness capture;
- `runner_service_identities` for the observed runner/job identities,
  `monitor_gaps`, `unrecognized_competitors`, `unknown_pids`, query errors and
  `evidence_blocked`.

The evaluator requires an exact `VOX_FX_T18_HOST_ARTIFACTS_V1` host manifest in
production mode. That manifest must hash the witness, identity policy, lease
receipt and event-loss receipt. It also requires timestamp overlap with every
trusted routed/authored/typical CTest interval, complete monotonic one-Hz
snapshots, no unknown identities, no unrecognized competitors, a verified lease,
and a lossless event trace. A quiet one-Hz snapshot or an environment variable
cannot produce a production wall-gate pass.

Capture mode writes a witness even when evidence is blocked, then exits with
code 2. It never repairs contention or controls the host to make evidence look
clean. Missing `-IdentityPolicyPath`, `-LeaseReceiptPath` or
`-EventLossReceiptPath` therefore produces a retained blocked witness rather
than a pass.

## Safe offline fixtures

`test-fx-t18-host-witness.ps1` executes two no-process, no-service-control
fixtures through the same script:

```powershell
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass `
  -File docs\ultima-vox-music\fx\evidence\test-fx-t18-host-witness.ps1
```

`-SyntheticCompetitor` models a competitor that starts and stops between two
one-Hz snapshots; its event trace is retained and
`unrecognized_competitors` is nonempty. `-SyntheticUnknownPid` models an event
whose identity cannot be verified; `unknown_pids` is retained and the event's
`identity_known` is false. Both fixtures redact command lines. The evaluator
must classify either receipt as `evidence_blocked`/`WALL_GATE_BLOCKED`, never as
an idle-host or wall-gate pass.
