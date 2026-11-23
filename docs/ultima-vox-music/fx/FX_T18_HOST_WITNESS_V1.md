# FX-T18 host-wide witness schema v1

`capture-fx-t18-host-witness.ps1` is a standalone Windows PowerShell 5.1
observation tool for the proposed wall gate. It does not launch, stop, kill,
disable or reconfigure a process or service. It observes the whole host while an
already-authorized runner performs a capture.

## Receipt

The UTF-8 JSON receipt has `format=VOX_FX_T18_HOST_WITNESS_V1` and records:

- `sample_interval_ms=1000` plus `pre_seconds`, `during_seconds` and
  `post_seconds`; the pre/post windows are each at least 30 seconds;
- `host_observation_mode=process_snapshot_and_event_trace`;
- `samples`, with `phase` equal to `pre`, `during` or `post`, a UTC timestamp,
  PID/name/parent PID, a protected `service_identity` field and the literal
  `command_line=<redacted>`; command lines are never queried or serialized;
- `process_events` from the `Win32_ProcessStartTrace` and
  `Win32_ProcessStopTrace` indication providers, including PID, process name,
  UTC timestamp, known-identity flag and a redacted command-line field;
- `event_trace.available`, `coverage_complete`, `lost_events`, provider and
  any subscription error;
- `runner_lease` identity and owner fields, with the lease token always
  redacted, plus `runner_service_identities` for the observed runner/job
  identities. A runner lease is a coordination record only; it is not host
  exclusivity by itself;
- `monitor_gaps`, `unrecognized_competitors`, `unknown_pids`,
  `evidence_blocked` and query errors.

The versioned evaluator in
`evidence/evaluate-fx-t18-wall-gate.py` requires complete pre/during/post
samples, a lossless event trace, a recognized lease, redacted identity fields,
no polling gaps, no unknown PIDs and no unrecognized competitor. It therefore
cannot treat a quiet one-Hz snapshot as proof that the host was idle.

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

Capture mode is intentionally explicit about missing owner coordination. Set
`FX_T18_LEASE_OWNER` only when the owner-approved host lease is active; the
script records only the owner name and a redacted token. A missing lease,
failed event subscription, query error, event gap or competing process is
recorded as evidence blockage. The script does not repair any of those states
and does not control the host to make the evidence look clean.
