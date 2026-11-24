# FX-T18 host-isolation owner runbook

**Status:** owner review/run authorization only. This source packet does not
claim owner attestation; it does not deliver `WALL_GATE_PASS`, dispatch a
Windows job, or authorize qualification.

## 1. Establish exclusive HAMUNAPTRA access

An owner must choose the exact HAMUNAPTRA host and record the four
owner-baselined runner-service identities. The baseline must cover all four
services, not only the service that happens to receive the job. The owner
reviews a complete private process inventory before capture and issues a
short-lived `VOX_FX_T18_IDENTITY_POLICY_V1` containing:

- host name, baseline ID, validity interval, and exactly four SHA-256 service
  identity values;
- every process permitted by the baseline, including executable path/hash,
  process name, parent identity, service identity, and either the reviewed
  PID plus creation time or an explicit reviewed dynamic-spawn rule;
- every ordinary authorized CTest/native child must carry
  `lineage_mode=runner_service_ancestry`, an ordered child-to-service chain,
  and an exact `parent_identity` path/hash/name rule. A PID-only dynamic child
  rule is invalid. The four direct runner-service identities remain explicit
  roots; a signed job-lineage alternative must carry its independent job
  identity hash and signer provenance;
- a deny/decision rule that treats unknown names, paths, hashes, parent
  identities, service identities, PID reuse, manual builds, DAWs, other
  runners, validators, and unrelated tests as unexpected;
- the policy bytes, detached CMS signature, owner certificate thumbprint, and
  storage location in the owner-controlled evidence store.

The host must be exclusive for the whole pre/during/post interval. Do not rely
on a GitHub concurrency group. No DAW, manual compiler/build, other runner
service/job, generic test, validator, or foreground audio workload may be
running unless it is explicitly present in the signed baseline. Do not stop,
kill, disable, restart, or reconfigure services/processes as part of this
capture. If the owner cannot establish this boundary, retain the blocked
receipt and stop.

## 2. Establish the pre-capture signed boundary

The identity policy and host lease are the only signed inputs required before
observation. They must be issued or reviewed outside Capture and signed with
the owner certificate whose SHA-1 thumbprint is supplied out-of-band. A JSON
field such as `owner_approved=true` or `verified=true` is not proof.

The owner stores the policy and lease payloads plus detached signatures in
write-restricted storage. The policy must contain exact path/hash/parent/service
identity rules and deny precedence. The lease must bind the host, lease ID and
policy SHA-256 and remain valid through the entire observation interval.

Do **not** pre-sign an ETW loss summary or execution binding with future
completion times. Those artifacts cannot truthfully exist until the observer
and all three trusted CTest profiles finish.

## 3. Future separately authorized observation command

No command below is authorized by this branch. After separate owner approval,
run it once beside the already-authorized three-profile runner job, using
PowerShell Desktop 5.1 and unique paths in the owner evidence store:

```powershell
$observation = 'D:\owner-evidence\fx-t18\host-observation.json'
$private = 'D:\owner-evidence\fx-t18\private-process-inventory.json'
& powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass `
  -File .\docs\ultima-vox-music\fx\evidence\capture-fx-t18-host-witness.ps1 `
  -Mode Capture -Phase Observe -OutputPath $observation `
  -IdentityPolicyPath 'D:\owner-evidence\fx-t18\identity-policy.json' `
  -IdentityPolicySignaturePath 'D:\owner-evidence\fx-t18\identity-policy.p7s' `
  -LeaseReceiptPath 'D:\owner-evidence\fx-t18\lease.json' `
  -LeaseReceiptSignaturePath 'D:\owner-evidence\fx-t18\lease.p7s' `
  -PrivateInventoryPath $private `
  -OwnerCertificateThumbprint 'OWNER_SUPPLIED_40_HEX_THUMBPRINT' `
  -PreSeconds 30 -DuringSeconds 60 -PostSeconds 30
# Exit 2 is expected: the observation is intentionally blocked until finalization.
$observationExit = $LASTEXITCODE
```

The observer writes `VOX_FX_T18_HOST_OBSERVATION_V1` with
`finalization_required=true`, sanitized WMI samples/events and the private
inventory hash. It must never contain a future ETW loss proof or CTest binding.
If identity, provider, cadence, lease coverage or private retention fails, it
writes a sanitized blocked failure receipt instead. Do not retry or edit any
receipt into a pass.

## 4. Close the interval and finalize

After the observation exits, the owner independently parses the retained ETW
collector. The pinned toolchain is Microsoft Windows SDK `tracerpt.exe`
`10.0.26100.1`; record and verify the actual executable path and file version.
The exact decoder command is:

```powershell
python .\docs\ultima-vox-music\fx\evidence\parse-fx-t18-etl.py `
  --etl 'D:\owner-evidence\fx-t18\process.etl' `
  --output 'D:\owner-evidence\fx-t18\process-parse.json' `
  --coverage-start '<UTC-COVERAGE-START>' `
  --coverage-end '<UTC-COVERAGE-END>' `
  --session-name 'FX-T18-KernelProcess' `
  --tracerpt 'C:\Windows\System32\tracerpt.exe' `
  --identity-join 'D:\owner-evidence\fx-t18\etw-identity-join.json'
python .\docs\ultima-vox-music\fx\evidence\parse-fx-t18-etl.py `
  --verify-receipt --etl 'D:\owner-evidence\fx-t18\process.etl' `
  --receipt 'D:\owner-evidence\fx-t18\process-parse.json' `
  --tracerpt 'C:\Windows\System32\tracerpt.exe'
```

The parser source records the event source (Kernel-Process IDs 1/2), loss
counter source (`tracerpt -lr` Events Lost/Buffers Lost), raw ETL SHA-256,
toolchain/version, exact CLI, decoded core/event digests, identity-join hash,
and replay command. The parse receipt must state the actual provider/session,
collector SHA-256, coverage interval, received/lost/buffer counters and the
complete parsed process-event list. It must include PID/start time, exact
executable path/hash, parent and service context for every event after the
owner identity join.

`etw-identity-join.json` is an owner-private
`VOX_FX_T18_ETW_IDENTITY_JOIN_V1` artifact containing one-to-one start/stop
records joined to the raw ETW PID, parent PID, process name, creation time and
provider timestamp within the same two-second bound. It carries the exact
image path/hash and the verified child-to-runner-service ancestry used by the
policy. The parser retains and hashes this join, but independently replays the
raw ETL core event digest; changing the raw ETL, adding/removing a core event,
or making the join ambiguous fails before correlation. Include the identity
join artifact in the owner host manifest beside the raw ETL and parse receipt.
A raw or arbitrary `.etl` file with a matching hash is not a parse; replay must
be performed from the retained raw bytes. If the ETL is tampered and JSON
claims are regenerated, the evaluator still applies the signed policy and
fails the event/lifecycle/correlation reconciliation.

The trusted runner then retains the native routed/authored/typical CTest
receipts and creates `VOX_FX_T18_EXECUTION_BINDING_V1` with their actual start
and end times, source/tree, selected executable SHA-256, host, lease and policy
hashes. The owner signs the event-loss and execution-binding payloads only
after those facts are immutable.

Run the finalizer with the same independent owner thumbprint:

```powershell
python .\docs\ultima-vox-music\fx\evidence\finalize-fx-t18-host-witness.py `
  --observation 'D:\owner-evidence\fx-t18\host-observation.json' `
  --output 'D:\owner-evidence\fx-t18\host-witness.json' `
  --identity-policy 'D:\owner-evidence\fx-t18\identity-policy.json' `
  --identity-policy-signature 'D:\owner-evidence\fx-t18\identity-policy.p7s' `
  --lease 'D:\owner-evidence\fx-t18\lease.json' `
  --lease-signature 'D:\owner-evidence\fx-t18\lease.p7s' `
  --event-loss 'D:\owner-evidence\fx-t18\event-loss.json' `
  --event-loss-signature 'D:\owner-evidence\fx-t18\event-loss.p7s' `
  --execution-binding 'D:\owner-evidence\fx-t18\execution-binding.json' `
  --execution-binding-signature 'D:\owner-evidence\fx-t18\execution-binding.p7s' `
  --owner-certificate-thumbprint 'OWNER_SUPPLIED_40_HEX_THUMBPRINT'
```

The finalizer refuses a capture failure, a changed observation/private
inventory, missing post-capture proofs, ETW/WMI event mismatch, PID lifecycle
ambiguity, or any interval mismatch. Only its resulting
`VOX_FX_T18_HOST_WITNESS_V1` can proceed to the Python wall evaluator.

The execution binding must also list all three runner-receipt paths and
SHA-256s. The owner then creates one final `VOX_FX_T18_HOST_ARTIFACTS_V2`
manifest covering at least:

- immutable blocked observation and final witness plus owner-private process inventory;
- identity policy, lease, ETW loss and execution-binding payloads, each bound to
  the observation hash where issued after capture;
- all four detached signatures, the raw ETL collector artifact, independent
  ETW parse receipt and the retained ETW identity-join artifact;
- each routed/authored/typical runner receipt, report, CTest raw output, JUnit,
  inventory, post-test outcome, selected executable and audit output.

Recompute every SHA-256 after the manifest is written. A failed reconciliation
or missing manifest entry is `BLOCKED`, including when all timing rows look
clean. Do not publish raw process names, paths, PIDs, command lines, or service
names in public summaries; publish only approved counters, decisions,
intervals, identity hashes, artifact hashes and sanitized diagnostics.

## 5. Cleanup and retention

After the owner confirms the capture interval and final hashes, release the
owner lease through the separately controlled owner procedure. Capture itself
performs only event-subscription cleanup; it never controls a service or
process. Preserve the original signed payloads, detached signatures, private
inventory, collector output, public witness, final manifest, runner receipts,
CTest/JUnit/raw logs, audit output, script hashes, PowerShell identity, host
identity, exit codes and command stdout/stderr in write-restricted storage for
the project evidence-retention period. Keep a redacted public summary separate
from the private inventory. If retention or cleanup cannot be demonstrated,
classify the result `BLOCKED` and retain the failure artifacts.
