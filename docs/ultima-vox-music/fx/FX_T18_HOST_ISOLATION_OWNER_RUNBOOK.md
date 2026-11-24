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
identity rules and deny precedence. Because Capture inventories the whole host,
it must also contain an owner-signed `host_baseline_id` and a complete
`host_baseline_processes` list. Normal Windows services that are not runner
job processes belong there only with `identity_scope=host_baseline`,
`non_competing=true`, `lineage_mode=host_baseline_v1`, exact image/path hash,
the complete exact service-identity set for that process (or explicit `none`
for a hashable root with no service), and an exact parent image/path/hash when
dynamic. A missing baseline entry is an evidence block; it is not ignored as a
normal OS process. The lease must bind the host, lease ID and policy SHA-256
and remain valid through the entire observation interval.

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
Before parsing, collect the ETW session with an owner-retained start/stop
record. Capture does not start or control this session. Use an elevated Windows
PowerShell 5.1 prompt and a unique session name; the kernel-process keyword is
selected explicitly and the session metadata is retained beside the ETL:

```powershell
$session = 'VOX-FX-T18-KernelProcess-' + [guid]::NewGuid().ToString('N')
$etl = 'D:\owner-evidence\fx-t18\process.etl'
$sessionStart = 'D:\owner-evidence\fx-t18\etw-session-start.txt'
$sessionStop = 'D:\owner-evidence\fx-t18\etw-session-stop.txt'
$collectionMetadata = 'D:\owner-evidence\fx-t18\etw-collection-metadata.json'
$tracerptSummary = 'D:\owner-evidence\fx-t18\process-tracerpt-summary.txt'
$etlDirectory = Split-Path -Parent $etl
$etlLeaf = Split-Path -Leaf $etl
$probeDirectory = Join-Path $etlDirectory 'tracerpt-probe'
$probeXml = Join-Path $probeDirectory 'decoded.xml'
$providerName = 'Microsoft-Windows-Kernel-Process'
$providerGuid = '{22FB2CD6-0E7B-422B-A0C7-2FAD1FD0E716}'
$keywords = '0x10'
$level = 5
$traceMode = 'bin'
$maxFileMb = 4096
$freeHeadroomMb = 512
$maxFileBytes = [int64]$maxFileMb * 1MB
$freeHeadroomBytes = [int64]$freeHeadroomMb * 1MB
$minimumFreeBytes = $maxFileBytes + $freeHeadroomBytes
$etlDrive = Get-PSDrive -Name (Split-Path -Path $etl -Qualifier).TrimEnd(':') -ErrorAction Stop
$freeBytesBeforeStart = [int64]$etlDrive.Free
if ($freeBytesBeforeStart -lt $minimumFreeBytes) { throw 'ETW destination does not have the required non-circular trace capacity' }
New-Item -ItemType Directory -LiteralPath $etlDirectory -Force | Out-Null
$preexistingEtls = @(Get-ChildItem -LiteralPath $etlDirectory -File | Where-Object {
  $_.Extension -ieq '.etl' -or $_.Name.StartsWith($etlLeaf + '.', [StringComparison]::OrdinalIgnoreCase)
})
if ($preexistingEtls.Count -ne 0) { throw "Refusing to use an ETW evidence directory with pre-existing ETL files: $etlDirectory" }
if (Test-Path -LiteralPath $probeDirectory) { throw "Refusing to reuse existing tracerpt probe directory: $probeDirectory" }
foreach ($path in @($etl, $sessionStart, $sessionStop, $collectionMetadata, $tracerptSummary)) {
  if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite existing ETW evidence path: $path" }
}
function Invoke-LogmanStep([string[]]$Arguments) {
  $raw = @(& logman.exe @Arguments 2>&1)
  $exitCode = [int]$LASTEXITCODE
  $text = ($raw | ForEach-Object { [string]$_ }) -join [Environment]::NewLine
  [pscustomobject]@{ exit_code = $exitCode; text = $text }
}
function Get-ControlTimeUtc([string]$Text, [string]$Label) {
  $match = [regex]::Match($Text, '(?im)^\s*' + [regex]::Escape($Label) + '\s*[:=]\s*(?<value>.+?)\s*$')
  if (-not $match.Success) { throw "Trusted control output has no $Label" }
  try {
    $parsed = [DateTimeOffset]::Parse($match.Groups['value'].Value, [Globalization.CultureInfo]::CurrentCulture, [Globalization.DateTimeStyles]::AssumeLocal)
    return $parsed.ToUniversalTime().ToString('o')
  }
  catch { throw "Trusted control output $Label is not parseable" }
}
$sessionStarted = $false
try {
  $createArgs = @('create', 'trace', $session, '-o', $etl, '-f', $traceMode, '-max', [string]$maxFileMb, '-p', $providerGuid, $keywords, [string]$level, '-ets')
  $createCommand = "logman.exe create trace `"$session`" -o `"$etl`" -f $traceMode -max $maxFileMb -p $providerGuid $keywords $level -ets"
  $create = Invoke-LogmanStep $createArgs
  if ($create.exit_code -ne 0 -or $create.text -notmatch '(?i)completed successfully') { throw 'logman create trace failed' }
  $sessionStarted = $true
  $queryArgs = @('query', $session, '-ets')
  $queryCommand = "logman.exe query `"$session`" -ets"
  $startQuery = Invoke-LogmanStep $queryArgs
  if ($startQuery.exit_code -ne 0 -or $startQuery.text -notmatch '(?im)status\s*:\s*running' -or
      $startQuery.text -notmatch [regex]::Escape($providerGuid) -or
      $startQuery.text -notmatch '(?im)microsoft-windows-kernel-process' -or
      $startQuery.text -notmatch '(?im)^\s*keywords?(?:\s*all|\s*any)?\s*[:=]\s*0x10\b' -or
      $startQuery.text -notmatch '(?im)(?:level|verbosity)\s*[:=]\s*5\b' -or
      $startQuery.text -notmatch '(?im)log\s+mode\s*[:=]\s*(?:sequential|file|normal)\b' -or
      $startQuery.text -match '(?i)bincirc|circular' -or
      $startQuery.text -notmatch '(?im)maximum\s+file\s+size\s*[:=]\s*4096\b') { throw 'logman query did not prove the requested running non-circular provider session' }
  $startControlUtc = Get-ControlTimeUtc $startQuery.text 'Start Time'
  # Start Capture immediately after this verified query, then run the
  # separately authorized routed/authored/typical job without changing services.
  @(
    'format=VOX_FX_T18_LOGMAN_RECEIPT_V1'
    'operation=start'
    "session_name=$session"
    "control_time_utc=$startControlUtc"
    'control_time_source=logman_query_output'
    "trace_mode=$traceMode"
    "provider_guid=$providerGuid"
    "keywords_hex=$keywords"
    "level=$level"
    "max_file_mb=$maxFileMb"
    'cleanup_mode=direct_ets_transient'
    "free_bytes_before_start=$freeBytesBeforeStart"
    "minimum_free_bytes=$minimumFreeBytes"
    "free_headroom_bytes=$freeHeadroomBytes"
    'initial_file_size=0'
    'rotation_count=0'
    'truncation_detected=false'
    "etl_path=$etl"
    "create_exit_code=$($create.exit_code)"
    "query_exit_code=$($startQuery.exit_code)"
    "command_create=$createCommand"
    "command_query=$queryCommand"
    '--- RAW LOGMAN OUTPUT ---'
    '[create]'
    $create.text
    '[query]'
    $startQuery.text
    '[capacity]'
    "Free Bytes Before Start: $freeBytesBeforeStart"
    "Minimum Free Bytes: $minimumFreeBytes"
    "Free Headroom Bytes: $freeHeadroomBytes"
    'Initial File Size: 0'
    'Rotation Count: 0'
    'Truncation Detected: false'
  ) | Set-Content -LiteralPath $sessionStart -Encoding UTF8
  Read-Host 'Owner: after Capture and the post interval are complete, press Enter to stop ETW'
  $stopArgs = @('stop', $session, '-ets')
  $stopCommand = "logman.exe stop `"$session`" -ets"
  $stop = Invoke-LogmanStep $stopArgs
  if ($stop.exit_code -ne 0 -or $stop.text -notmatch '(?i)completed successfully') { throw 'logman stop trace failed' }
  $postQuery = Invoke-LogmanStep $queryArgs
  if ($postQuery.exit_code -eq 0 -or $postQuery.text -notmatch '(?i)not found|does not exist|cannot find|no such') { throw 'post-stop logman query did not prove the session was disabled' }
  # Retain a pinned tracerpt summary as a second, independent ETW-header
  # boundary. If the stop command itself exposes Stop Time, that raw control
  # output remains acceptable when a summary has no coverage labels; otherwise
  # the retained summary header must provide Log End Time.
  $tracerpt = 'C:\Windows\System32\tracerpt.exe'
  New-Item -ItemType Directory -LiteralPath $probeDirectory -Force | Out-Null
  $probeOutput = @(& $tracerpt $etl -o $probeXml -of XML -lr -summary $tracerptSummary -y 2>&1)
  $probeExit = [int]$LASTEXITCODE
  $probeText = ($probeOutput | ForEach-Object { [string]$_ }) -join [Environment]::NewLine
  if ($probeExit -ne 0 -or -not (Test-Path -LiteralPath $tracerptSummary)) { throw 'pinned tracerpt summary probe failed' }
  $summaryText = Get-Content -LiteralPath $tracerptSummary -Raw
  $stopControlSource = $null
  $stopControlUtc = $null
  try {
    $stopControlUtc = Get-ControlTimeUtc $stop.text 'Stop Time'
    $stopControlSource = 'logman_stop_output'
  }
  catch {
    $stopControlUtc = Get-ControlTimeUtc $summaryText 'Log End Time'
    $stopControlSource = 'tracerpt_summary_header'
  }
  $etlFiles = @(Get-ChildItem -LiteralPath $etlDirectory -File | Where-Object {
    $_.Extension -ieq '.etl' -or $_.Name.StartsWith($etlLeaf + '.', [StringComparison]::OrdinalIgnoreCase)
  })
  $etlFileCount = [int]$etlFiles.Count
  $rotationCount = [Math]::Max(0, $etlFileCount - 1)
  $etlInfo = Get-Item -LiteralPath $etl -ErrorAction Stop
  $bytesWritten = [int64]$etlInfo.Length
  $truncationDetected = $bytesWritten -ge $maxFileBytes -or $etlFileCount -ne 1
  if ($bytesWritten -le 0 -or $truncationDetected) { throw 'ETW artifact is empty, rotated, or reached the bounded non-circular capacity' }
  if ($etlFileCount -ne 1 -or $rotationCount -ne 0) { throw 'Independent ETL directory inventory violated the single-file zero-rotation policy' }
  $fileInventoryLines = @(
    "ETL File Count: $etlFileCount"
    foreach ($etlFile in $etlFiles) { "ETL File: $($etlFile.Name)" }
  )
  @(
    'format=VOX_FX_T18_LOGMAN_RECEIPT_V1'
    'operation=stop'
    "session_name=$session"
    "control_time_utc=$stopControlUtc"
    "control_time_source=$stopControlSource"
    "trace_mode=$traceMode"
    "provider_guid=$providerGuid"
    "keywords_hex=$keywords"
    "level=$level"
    "max_file_mb=$maxFileMb"
    'cleanup_mode=direct_ets_transient'
    'delete_applicable=false'
    'delete_exit_code=not_applicable'
    "bytes_written=$bytesWritten"
    "rotation_count=$rotationCount"
    "etl_file_count=$etlFileCount"
    'file_policy=single_retained_etl_file_zero_rotation'
    'file_inventory_source=independent_etl_directory_inventory'
    "truncation_detected=$($truncationDetected.ToString().ToLowerInvariant())"
    "etl_path=$etl"
    "stop_exit_code=$($stop.exit_code)"
    "post_stop_query_exit_code=$($postQuery.exit_code)"
    "tracerpt_probe_exit_code=$probeExit"
    "command_stop=$stopCommand"
    "command_post_stop_query=$queryCommand"
    "command_tracerpt_probe=$tracerpt $etl -o $probeXml -of XML -lr -summary $tracerptSummary -y"
    '--- RAW LOGMAN OUTPUT ---'
    '[stop]'
    $stop.text
    '[post-stop-query]'
    $postQuery.text
    '[tracerpt-summary-header]'
    $summaryText
    '[file-stat]'
    "Bytes Written: $bytesWritten"
    "Rotation Count: $rotationCount"
    "ETL File Count: $etlFileCount"
    "Truncation Detected: $($truncationDetected.ToString().ToLowerInvariant())"
    '[file-inventory]'
    $fileInventoryLines
  ) | Set-Content -LiteralPath $sessionStop -Encoding UTF8
  $sessionStarted = $false
  $metadata = [ordered]@{
    format = 'VOX_FX_T18_ETW_COLLECTION_METADATA_V1'; provider = "$providerName/ETW"; provider_guid = $providerGuid
    session_name = $session; etl_path = $etl; trace_mode = $traceMode; capacity_policy = 'max_file_mb_plus_free_headroom'
    file_policy = 'single_retained_etl_file_zero_rotation'; file_inventory_source = 'independent_etl_directory_inventory'
    cleanup_mode = 'direct_ets_transient'; delete_applicable = $false; delete_exit_code = $null
    max_file_mb = $maxFileMb; free_headroom_bytes = $freeHeadroomBytes; minimum_free_bytes = $minimumFreeBytes
    free_bytes_before_start = $freeBytesBeforeStart; bytes_written = $bytesWritten; rotation_count = $rotationCount; etl_file_count = $etlFileCount
    truncation_detected = $truncationDetected; keywords_hex = $keywords; level = $level
    start_record_path = $sessionStart; start_record_sha256 = (Get-FileHash -LiteralPath $sessionStart -Algorithm SHA256).Hash.ToLowerInvariant()
    stop_record_path = $sessionStop; stop_record_sha256 = (Get-FileHash -LiteralPath $sessionStop -Algorithm SHA256).Hash.ToLowerInvariant()
  }
  $metadata | ConvertTo-Json | Set-Content -LiteralPath $collectionMetadata -Encoding UTF8
}
finally {
  if ($sessionStarted) {
    [void](Invoke-LogmanStep @('stop', $session, '-ets'))
  }
}
```

This collector is deliberately noncircular: `-f bin` is required and
`bincirc`, rotations, and truncation are rejected. The 4096 MiB maximum file
size plus 512 MiB free headroom is an explicit capacity policy; the run aborts
before creation when the destination has less than
`max_file_bytes + free_headroom_bytes`, and aborts after stop if the retained
ETL reaches the maximum. The start receipt retains the successful `logman
create` and active `logman query` output, exact provider GUID, keyword `0x10`,
level `5`, mode, maximum, free-space calculation and zero initial
rotation/truncation state. The stop receipt retains successful `logman stop`,
the failed post-stop `logman query` proving the session is disabled, the
explicit `direct_ets_transient` cleanup mode with delete marked not applicable,
the pinned tracerpt probe result, control-time source, an independently
enumerated ETL-directory inventory, ETL bytes, file count, rotation count and
truncation state. The single-retained-file/zero-rotation rule is a declared
policy invariant; any independent inventory variation blocks before witness
finalization. A persisted DCS variant must instead retain and validate its
applicable delete command and exit code, without `-ets`. Both receipts are
hashed in the metadata; the parser reads and validates their raw output rather
than accepting the metadata timestamps. If neither a raw `logman` `Stop Time`
nor a `tracerpt` `Log End Time` is retained, the collection is blocked.

The exact decoder command requests a separate `tracerpt -summary` artifact;
`-lr` is the less-restrictive decoder option and is **not** a loss report:

```powershell
python .\docs\ultima-vox-music\fx\evidence\parse-fx-t18-etl.py `
  --etl 'D:\owner-evidence\fx-t18\process.etl' `
  --output 'D:\owner-evidence\fx-t18\process-parse.json' `
  --summary-output 'D:\owner-evidence\fx-t18\process-tracerpt-summary.txt' `
  --collection-metadata 'D:\owner-evidence\fx-t18\etw-collection-metadata.json' `
  --tracerpt 'C:\Windows\System32\tracerpt.exe' `
  --identity-join 'D:\owner-evidence\fx-t18\etw-identity-join.json'
python .\docs\ultima-vox-music\fx\evidence\parse-fx-t18-etl.py `
  --verify-receipt --etl 'D:\owner-evidence\fx-t18\process.etl' `
  --receipt 'D:\owner-evidence\fx-t18\process-parse.json'
```

The parser source records the event source (Kernel-Process IDs 1/2), the
explicit `tracerpt -summary` loss-counter source, summary path/hash, raw ETL
SHA-256, toolchain/version, exact tracerpt argv, independently derived
`Log Start Time`/`Log End Time` coverage when tracerpt emits those fields,
reconciles the retained logman start/stop metadata for the final interval and
session, and records decoded core/event digests, identity-join hash, and replay
command. Caller-supplied coverage/session arguments are fixture-only and are
not used for production.
The parse receipt must state the actual provider/session, collector SHA-256,
summary SHA-256, derived coverage interval, received/lost/buffer counters and
the complete parsed process-event list. It must include PID/start time, exact
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
- all four detached signatures, the raw ETL collector artifact, retained
  `tracerpt -summary` artifact, retained logman collection metadata and both
  hashed start/stop control receipts, independent ETW parse receipt and the
  retained ETW identity-join artifact;
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
