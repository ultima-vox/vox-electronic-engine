# FX-T18 host-isolation owner runbook

**Status:** owner review/run authorization only. This branch delivers
`READY_FOR_OWNER_ATTESTATION`; it does not deliver `WALL_GATE_PASS`, dispatch a
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

## 2. Produce independent owner attestations

The lease, event-loss proof, and CTest binding are separate artifacts. They
must be issued or reviewed outside the Capture process and signed with the
owner certificate whose SHA-1 thumbprint is supplied out-of-band. A JSON field
such as `owner_approved=true`, `verified=true`, or `lost_events=0` is not proof.

Required signed payloads:

1. **Lease** — `VOX_FX_T18_HOST_LEASE_RECEIPT_V1`, with host, lease ID, owner,
   policy SHA-256, and a validity interval covering the complete capture.
2. **Event loss** — `VOX_FX_T18_EVENT_LOSS_RECEIPT_V1`, produced by an
   independently retained `Microsoft-Windows-Kernel-Process/ETW` collector.
   It must include the trace interval, a `collector_artifact_path` and
   matching SHA-256, received event count, provider/buffer loss counters, and
   `lost_events=0` only when the measurable counters reconcile. A WMI
   subscription success or a self-written zero is insufficient and blocks.
3. **Execution binding** — `VOX_FX_T18_EXECUTION_BINDING_V1`, binding the
   routed/authored/typical trusted CTest start/end timestamps to the host,
   runner job, source HEAD/tree, selected Release executable SHA-256, lease
   ID, and policy SHA-256.

The owner stores payloads and detached signatures in write-restricted storage.
The Capture script verifies each CMS signature against the out-of-band
thumbprint and records the payload/signature hashes. The Python evaluator
repeats the CMS check with the owner-host PowerShell verifier, recomputes all
hashes, requires the same signer thumbprint for all four proofs, reconciles the
signed claims to each runner receipt, and requires the host manifest to include
every payload, signature, ETW collector artifact, private inventory, witness,
runner receipt and test artifact.

## 3. Future separately authorized Capture command

No command below is authorized by this branch. After separate owner approval,
run it once beside the already-authorized three-profile runner job, using
PowerShell Desktop 5.1 and unique paths in the owner evidence store:

```powershell
$witness = 'D:\owner-evidence\fx-t18\host-witness.json'
$private = 'D:\owner-evidence\fx-t18\private-process-inventory.json'
& powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass `
  -File .\docs\ultima-vox-music\fx\evidence\capture-fx-t18-host-witness.ps1 `
  -Mode Capture -OutputPath $witness `
  -IdentityPolicyPath 'D:\owner-evidence\fx-t18\identity-policy.json' `
  -IdentityPolicySignaturePath 'D:\owner-evidence\fx-t18\identity-policy.p7s' `
  -LeaseReceiptPath 'D:\owner-evidence\fx-t18\lease.json' `
  -LeaseReceiptSignaturePath 'D:\owner-evidence\fx-t18\lease.p7s' `
  -EventLossReceiptPath 'D:\owner-evidence\fx-t18\event-loss.json' `
  -EventLossReceiptSignaturePath 'D:\owner-evidence\fx-t18\event-loss.p7s' `
  -ExecutionBindingPath 'D:\owner-evidence\fx-t18\execution-binding.json' `
  -ExecutionBindingSignaturePath 'D:\owner-evidence\fx-t18\execution-binding.p7s' `
  -PrivateInventoryPath $private `
  -OwnerCertificateThumbprint 'OWNER_SUPPLIED_40_HEX_THUMBPRINT' `
  -PreSeconds 30 -DuringSeconds 60 -PostSeconds 30
# The signed execution binding must cover the full interval above; do not
# shorten Capture or substitute a locally rebuilt/manual workload.
$captureExit = $LASTEXITCODE
```

`-FixtureMode`, `-Mode OfflineSynthetic`, and all synthetic switches are
qualification-prohibited and must never be used for an owner run. Capture
returns exit `2` and writes a sanitized blocked receipt when any proof,
identity, baseline, service, timing, private-retention, or provider condition
is unavailable. Do not retry or edit a blocked receipt into a pass.

The execution binding must also list all three runner-receipt paths and
SHA-256s. The runner must retain the trusted CTest receipts and raw artifacts
with their native exits, exact starts/ends, source/tree, executable hash and job
identity. The owner then creates one final `VOX_FX_T18_HOST_ARTIFACTS_V2`
manifest covering at least:

- witness and owner-private process inventory;
- identity policy, lease, ETW loss and execution-binding payloads;
- all four detached signatures and the ETW collector artifact;
- each routed/authored/typical runner receipt, report, CTest raw output, JUnit,
  inventory, post-test outcome, selected executable and audit output.

Recompute every SHA-256 after the manifest is written. A failed reconciliation
or missing manifest entry is `BLOCKED`, including when all timing rows look
clean. Do not publish raw process names, paths, PIDs, command lines, or service
names in public summaries; publish only approved counters, decisions,
intervals, identity hashes, artifact hashes and sanitized diagnostics.

## 4. Cleanup and retention

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
