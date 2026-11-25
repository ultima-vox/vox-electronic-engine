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
& powershell.exe -NoLogo -NoProfile -NonInteractive `
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
record. Capture does not start or control this session. The mandatory order is:
run the Section 4.1 tool-identity preflight first; only after it passes may the
owner invoke the collector below, then the decoder. Use an elevated Windows
PowerShell 5.1 prompt and a unique session name; the kernel-process keyword is
selected explicitly and the session metadata is retained beside the ETL. The
supported collector is the owner-approved exact
`C:\Windows\System32\logman.exe`; invoke that absolute path rather than a
PATH-resolved `logman.exe`. Its raw/normalized `FileVersion` and exact
SHA-256 must be retained in the tool-identity record and bound to the same
owner approval as the decoder. Run the mandatory Section 4.1 preflight below before executing this collector:

### 4.1 Tool identity preflight (owner-controlled, fail-closed)

The parser accepts only the exact source-pinned normalized tracerpt version
`10.0.26100.1`, plus an owner-approved exact executable SHA-256. The supported
`logman.exe` collector is also selected by an exact approved tuple: the full
path, normalized four-part `FileVersion`, and exact SHA-256 are all retained in
the identity record and must match the detached-CMS-approved manifest. No
version family, PATH-resolved executable, or hash-family substitution is
accepted for either tool; the parser's runtime version/hash gate is
specifically for `tracerpt.exe` because replay does not invoke `logman.exe`.
Normalize PowerShell `FileVersion` by trimming it and removing only a trailing
parenthesized `WinBuild.<build>` label; then require a four-part numeric
version.

The JSON `owner_approved` flag and `verification_method` field are not
cryptographic proof. Before this block, the owner must independently verify the
approval JSON's detached CMS signature with the owner-controlled CMS verifier
and retain a text verification receipt binding the approval JSON hash,
signature hash, exact logman/tracerpt paths, normalized versions and SHA-256s.
This PowerShell block deliberately does **not** verify CMS; it requires and
hash-binds that already-verified receipt, then checks the actual files before
any collector or decoder invocation. The parser's `--tracerpt-sha256` value
must be copied from that independently verified manifest, never discovered and
approved by the probe itself.
The approval record must contain `tracerpt_path`,
`tracerpt_version_normalized`, `tracerpt_sha256`, `logman_path`,
`logman_version_normalized`, and `logman_sha256`; all six values are checked
against the files used by the owner run.

```powershell
$tracerptPath = 'C:\Windows\System32\tracerpt.exe'
$logmanPath = 'C:\Windows\System32\logman.exe'
$approvalPath = 'D:\owner-evidence\fx-t18\tracerpt-identity-approval.json'
$approvalSignaturePath = 'D:\owner-evidence\fx-t18\tracerpt-identity-approval.json.p7s'
$approvalVerificationPath = 'D:\owner-evidence\fx-t18\tracerpt-identity-approval-verification.txt'
$identityPath = 'D:\owner-evidence\fx-t18\tracerpt-tool-identity.json'
function Normalize-FileVersion([string]$Value) {
  $normalized = [regex]::Replace($Value.Trim(), '(?i)\s*\(WinBuild\.[^)]*\)\s*$', '').Trim()
  if ($normalized -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw "Unsupported FileVersion: $Value" }
  return $normalized
}
function Get-TrustedMicrosoftSignature([string]$Path) {
  $signature = Get-AuthenticodeSignature -FilePath $Path
  if ([string]$signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or
      [string]$signature.SignerCertificate.Subject -notmatch '(?i)Microsoft') {
    throw "Authenticode signature is not a valid Microsoft signature: $Path"
  }
  return [ordered]@{
    status = [string]$signature.Status
    subject = [string]$signature.SignerCertificate.Subject
    thumbprint = [string]$signature.SignerCertificate.Thumbprint
  }
}
if (-not (Test-Path -LiteralPath $approvalPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $approvalSignaturePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $approvalVerificationPath -PathType Leaf)) {
  throw 'Approval JSON, detached CMS signature, and independent verification receipt are required'
}
$tracerptSignature = Get-TrustedMicrosoftSignature $tracerptPath
$logmanSignature = Get-TrustedMicrosoftSignature $logmanPath
$approvalHash = (Get-FileHash -LiteralPath $approvalPath -Algorithm SHA256).Hash.ToLowerInvariant()
$approvalSignatureHash = (Get-FileHash -LiteralPath $approvalSignaturePath -Algorithm SHA256).Hash.ToLowerInvariant()
$approvalVerificationHash = (Get-FileHash -LiteralPath $approvalVerificationPath -Algorithm SHA256).Hash.ToLowerInvariant()
$verificationText = (Get-Content -LiteralPath $approvalVerificationPath -Raw).ToLowerInvariant()
foreach ($binding in @(
  "verified=true", "verification_method=owner_signed_detached_cms",
  "approval_sha256=$approvalHash", "signature_sha256=$approvalSignatureHash")) {
  # The receipt is trusted only because the owner verified it out-of-band; this
  # check binds its retained hashes but does not perform CMS verification.
  $bindingText = $binding.ToLowerInvariant()
  if ($verificationText -notmatch ('(?m)^\s*' + [regex]::Escape($bindingText) + '\s*$')) {
    throw "Independent approval verification receipt lacks $binding"
  }
}
$tracerptItem = Get-Item -LiteralPath $tracerptPath -ErrorAction Stop
$logmanItem = Get-Item -LiteralPath $logmanPath -ErrorAction Stop
$tracerptRawVersion = [string]$tracerptItem.VersionInfo.FileVersion
$tracerptVersion = Normalize-FileVersion $tracerptRawVersion
$logmanRawVersion = [string]$logmanItem.VersionInfo.FileVersion
$logmanVersion = Normalize-FileVersion $logmanRawVersion
$tracerptHash = (Get-FileHash -LiteralPath $tracerptPath -Algorithm SHA256).Hash.ToLowerInvariant()
$logmanHash = (Get-FileHash -LiteralPath $logmanPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($tracerptVersion -ne '10.0.26100.1') { throw "Production tracerpt version is not the exact pinned 10.0.26100.1: $tracerptVersion" }
foreach ($binding in @(
  "tracerpt_path=$tracerptPath", "tracerpt_version_normalized=$tracerptVersion",
  "tracerpt_sha256=$tracerptHash", "logman_path=$logmanPath",
  "logman_version_normalized=$logmanVersion", "logman_sha256=$logmanHash")) {
  $bindingText = $binding.ToLowerInvariant()
  if ($verificationText -notmatch ('(?m)^\s*' + [regex]::Escape($bindingText) + '\s*$')) {
    throw "Independent approval verification receipt lacks $binding"
  }
}
$approval = Get-Content -LiteralPath $approvalPath -Raw | ConvertFrom-Json
if ($approval.format -ne 'VOX_FX_T18_TRACERPT_IDENTITY_APPROVAL_V1' -or
    $approval.owner_approved -ne $true -or
    $approval.verification_method -ne 'owner_signed_detached_cms' -or
    $approval.parser_toolchain_id -ne 'microsoft-windows-sdk-tracerpt-10.0.26100.1' -or
    $approval.tracerpt_path -ine $tracerptPath -or
    $approval.tracerpt_version_normalized -ne $tracerptVersion -or
    $approval.tracerpt_sha256 -ine $tracerptHash -or
    $approval.logman_path -ine $logmanPath -or
    $approval.logman_version_normalized -ne $logmanVersion -or
    $approval.logman_sha256 -ine $logmanHash) { throw 'logman/tracerpt identity is not owner-approved for this parser pin' }
[ordered]@{
  format = 'VOX_FX_T18_TRACERPT_TOOL_IDENTITY_V1'; path = $tracerptPath
  file_version_raw = $tracerptRawVersion; file_version_normalized = $tracerptVersion
  sha256 = $tracerptHash; logman_path = $logmanPath
  logman_file_version_raw = $logmanRawVersion; logman_file_version_normalized = $logmanVersion
  logman_sha256 = $logmanHash; parser_toolchain_id = $approval.parser_toolchain_id
  tracerpt_signature = $tracerptSignature; logman_signature = $logmanSignature
  approval_path = $approvalPath; approval_sha256 = $approvalHash
  approval_signature_path = $approvalSignaturePath; approval_signature_sha256 = $approvalSignatureHash
  approval_verification_path = $approvalVerificationPath; approval_verification_sha256 = $approvalVerificationHash
  cms_verification_performed_by_this_block = $false
} | ConvertTo-Json | Set-Content -Path $identityPath -Encoding UTF8
```

A prior owner-controlled preflight observed System32 `tracerpt.exe` and
`logman.exe` as `10.0.26100.8875 (WinBuild.160101.0800)` and recorded tracerpt
SHA-256 `aee3a047976f60e69357eb415d0d69b7b38325a55de67afa77d5b3fd5c28777b`.
That normalizes to `10.0.26100.8875`, not the pinned `10.0.26100.1`, so it is
an explicit compatibility block—not approval to broaden the parser pin or to
run a patched executable. A future owner-approved tool change requires a
separate source review and updated exact version/hash policy.

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
$logmanPath = 'C:\Windows\System32\logman.exe'
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
New-Item -ItemType Directory -Path $etlDirectory -Force | Out-Null
$preexistingEtls = @(Get-ChildItem -LiteralPath $etlDirectory -File | Where-Object {
  $_.Extension -ieq '.etl' -or $_.Name.StartsWith($etlLeaf + '.', [StringComparison]::OrdinalIgnoreCase)
})
if ($preexistingEtls.Count -ne 0) { throw "Refusing to use an ETW evidence directory with pre-existing ETL files: $etlDirectory" }
if (Test-Path -LiteralPath $probeDirectory) { throw "Refusing to reuse existing tracerpt probe directory: $probeDirectory" }
foreach ($path in @($etl, $sessionStart, $sessionStop, $collectionMetadata, $tracerptSummary)) {
  if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite existing ETW evidence path: $path" }
}
function Invoke-LogmanStep([string[]]$Arguments) {
  $raw = @(& $logmanPath @Arguments 2>&1)
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
  $createCommand = "$logmanPath create trace `"$session`" -o `"$etl`" -f $traceMode -max $maxFileMb -p $providerGuid $keywords $level -ets"
  $create = Invoke-LogmanStep $createArgs
  if ($create.exit_code -ne 0 -or $create.text -notmatch '(?i)completed successfully') { throw 'logman create trace failed' }
  $sessionStarted = $true
  $queryArgs = @('query', $session, '-ets')
  $queryCommand = "$logmanPath query `"$session`" -ets"
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
  $stopCommand = "$logmanPath stop `"$session`" -ets"
  $stop = Invoke-LogmanStep $stopArgs
  if ($stop.exit_code -ne 0 -or $stop.text -notmatch '(?i)completed successfully') { throw 'logman stop trace failed' }
  $postQuery = Invoke-LogmanStep $queryArgs
  if ($postQuery.exit_code -eq 0 -or $postQuery.text -notmatch '(?i)not found|does not exist|cannot find|no such') { throw 'post-stop logman query did not prove the session was disabled' }
  # Retain a pinned tracerpt summary as a second, independent ETW-header
  # boundary. If the stop command itself exposes Stop Time, that raw control
  # output remains acceptable when a summary has no coverage labels; otherwise
  # the retained summary header must provide Log End Time.
  # This path/version/hash must already have passed the owner tool-identity preflight in Section 4.1.
  $tracerpt = 'C:\Windows\System32\tracerpt.exe'
  New-Item -ItemType Directory -Path $probeDirectory -Force | Out-Null
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

### 4.2 Decode and replay

The exact decoder command requests a separate `tracerpt -summary` artifact;
`-lr` is the less-restrictive decoder option and is **not** a loss report. The
parser retains the normalized four-part Windows `FileVersion` and exact
tracerpt SHA-256; production parsing requires the owner-approved hash supplied
with `--tracerpt-sha256`. A parenthesized suffix such as `(WinBuild.160101.0800)`
is removed only for normalization; version-family or hash-family matching is
never accepted:

```powershell
python .\docs\ultima-vox-music\fx\evidence\parse-fx-t18-etl.py `
  --etl 'D:\owner-evidence\fx-t18\process.etl' `
  --output 'D:\owner-evidence\fx-t18\process-parse.json' `
  --summary-output 'D:\owner-evidence\fx-t18\process-tracerpt-summary.txt' `
  --collection-metadata 'D:\owner-evidence\fx-t18\etw-collection-metadata.json' `
  --tracerpt 'C:\Windows\System32\tracerpt.exe' `
  --tracerpt-sha256 'OWNER_APPROVED_EXACT_TRACERPT_SHA256' `
  --tracerpt-approval 'D:\owner-evidence\fx-t18\tracerpt-identity-approval.json' `
  --tracerpt-approval-signature 'D:\owner-evidence\fx-t18\tracerpt-identity-approval.json.p7s' `
  --tracerpt-approval-verification 'D:\owner-evidence\fx-t18\tracerpt-identity-approval-verification.txt' `
  --identity-join 'D:\owner-evidence\fx-t18\etw-identity-join.json'
python .\docs\ultima-vox-music\fx\evidence\parse-fx-t18-etl.py `
  --etl 'D:\owner-evidence\fx-t18\process.etl' `
  --verify-receipt 'D:\owner-evidence\fx-t18\process-parse.json'
```

Receipt replay is fail-closed: it requires the retained production
`tool_sha256`, rehashes the exact tracerpt path and external approval
JSON/signature/verification receipt recorded in the receipt, and rechecks both
that trusted tuple and normalized `10.0.26100.1` before decoding. A real receipt
without the owner-supplied expected hash and authenticated approval provenance,
or with a changed executable, is not replayable.

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

## 5. Bounded CLI compatibility diagnostic (separate owner authorization)

The production contract remains fail-closed at tracerpt `10.0.26100.1`; the
measured System32 `10.0.26100.8875` executable is not production approval.
For the compatibility question only, use the parameterized
`evidence/diagnose-fx-t18-tracerpt-compatibility.ps1` script. It is a bounded
owner-local diagnostic, not a collector retry or qualification route. It runs
only from an already elevated Windows PowerShell Desktop 5.1 console, checks
the exact `C:\Windows\System32` paths, normalized versions, exact bytes, valid
Microsoft Authenticode signatures, effective host/administrator/PowerShell
preflight, and the exact measured logman identity **before** invoking either
`logman.exe` or `tracerpt.exe`.

This compatibility-only branch deliberately has no CMS or pre-created approval
file dependency. Do not fabricate or sign diagnostic approval artifacts. The
script records `diagnostic_approval_required=false` and
`cms_verification_performed_by_this_script=false`; this does not change the
production contract. Production parsing still requires the Section 4.1
owner-approved exact `10.0.26100.1` path/version/hash tuple, detached signature,
and independently verified CMS receipt. The diagnostic's installed
`10.0.26100.8875` result can never create that production approval or a
production parse receipt.

The owner must choose a new empty private output directory. No UAC automation,
execution-policy bypass, service/account/group/ACL change, tool installation,
retry, CTest, DAW, or runner-service reconfiguration is allowed.
The controlled workload is only a short-lived System32 `timeout.exe` process;
its process name and PID stay in private raw receipts and are not emitted in
the sanitized summary. The diagnostic also requires a measurable local output
drive with at least 64 MiB free before creating the bounded 16 MiB ETL.

Run exactly once from the repository root in the already elevated Windows
PowerShell 5.1 console. The script's hard-coded guard rejects any other
tracerpt version/hash or Windows root; the explicit arguments below make the
owner review tuple visible in the retained command receipts:

```powershell
# Current directory must be the repository root.
(Get-Location).Path
$diagnosticRoot = 'D:\owner-evidence\fx-t18\tracerpt-compat-' + [guid]::NewGuid().ToString('N')
& powershell.exe -NoLogo -NoProfile -NonInteractive `
  -File .\docs\ultima-vox-music\fx\evidence\diagnose-fx-t18-tracerpt-compatibility.ps1 `
  -OutputRoot $diagnosticRoot `
  -Python 'python' `
  -TracerptPath 'C:\Windows\System32\tracerpt.exe' `
  -LogmanPath 'C:\Windows\System32\logman.exe' `
  -ExpectedTracerptVersion '10.0.26100.8875' `
  -ExpectedTracerptSha256 'aee3a047976f60e69357eb415d0d69b7b38325a55de67afa77d5b3fd5c28777b' `
  -ExpectedLogmanVersion '10.0.26100.8875' `
  -ObservationSeconds 3 `
  -ExpectedSignerSubject 'Microsoft'
$diagnosticExit = $LASTEXITCODE
```

Expected retained outputs include `preflight.json`, `effective-identity.txt`,
`fltmc.txt`, `logman-create.txt`, `logman-active-query.txt`,
`controlled-process.txt`, `controlled-process.json`, `compat.etl`,
`tracerpt-output.xml`, `tracerpt-summary.txt`, parser/decoder console logs,
`diagnostic-parser.json`, `logman-stop.txt`, `logman-post-stop-query.txt`, and
`diagnostic-summary.json`. Inspect only the sanitized summary for review; the
following checks cleanup and decoder evidence without exposing the private PID:

```powershell
$summary = Get-Content -LiteralPath "$diagnosticRoot\diagnostic-summary.json" -Raw | ConvertFrom-Json
[ordered]@{
  status = $summary.status
  preflight_passed = $summary.preflight_passed
  active_query_format_status = $summary.active_query_format_status
  post_stop_format_status = $summary.post_stop_format_status
  post_stop_absence_verified = $summary.post_stop_absence_verified
  decoder_outputs_present = $summary.decoder_outputs_present
  summary_loss_fields = $summary.summary_loss_fields
  controlled_process_binding_valid = $summary.controlled_process_binding_valid
  controlled_start_match_count = $summary.controlled_start_match_count
  controlled_stop_match_count = $summary.controlled_stop_match_count
} | Format-List
```

`post_stop_absence_verified=true`, `decoder_outputs_present=true`,
`summary_loss_fields.fields_present=true`, and exactly one controlled start and
stop match are required for `COMPATIBILITY_EVIDENCE_READY`; any other result is
blocked evidence and must not be retried.

Exit `0` means only that the bounded compatibility artifacts were produced;
it is not `WALL_GATE_PASS`, qualification, or approval of the installed build.
Exit `2` is blocked evidence. The script never invokes production parsing with
an unsupported version and never creates a production parse receipt. Instead,
it invokes the installed, signature-checked System32 decoder exactly once to
retain private `tracerpt-output.xml`, `tracerpt-summary.txt`, and raw decoder
console output, then invokes the production parser's diagnostic-only XML/
summary path:

```powershell
python .\docs\ultima-vox-music\fx\evidence\parse-fx-t18-etl.py `
  --etl "$diagnosticRoot\compat.etl" `
  --diagnostic-decoded-xml "$diagnosticRoot\tracerpt-output.xml" `
  --diagnostic-summary "$diagnosticRoot\tracerpt-summary.txt" `
  --diagnostic-controlled-process "$diagnosticRoot\controlled-process.json" `
  --diagnostic-output "$diagnosticRoot\diagnostic-parser.json"
```

The script performs that parser invocation itself; the command above documents
the exact field/counter/binding test and is not a second decoder run. The
parser diagnostic applies the production XML/summary field shape, binds exactly
one observed start and stop to the private controlled PID plus creation time
(with only a one-second timestamp-representation tolerance), parses `Events
Lost`, `Buffers Lost`, coverage and logger metadata, and emits
only sanitized booleans/counts and artifact hashes with
`qualification_prohibited=true`. The raw controlled-process identity remains
private.

The active-query receipt records safe field-presence booleans and a format
status. A zero exit with nonempty output but unfamiliar localized labels is
retained as `unknown_labels` and the bounded capture may continue so the real
ETL/XML/summary can reveal compatibility; a failed or explicitly inactive
query does not start the controlled process. After one stop, a textual
post-stop format mismatch may still allow one decoder attempt when stop
succeeded and the query does not report an active session, but the final result
remains `BLOCKED` until absence is proven. A failed stop or explicit active
post-stop query prevents decoding. No control command is retried.

Private artifacts retained under `$diagnosticRoot` include `preflight.json`,
effective-token/privilege output, create/active-query/stop/post-stop-query
receipts, `controlled-process.txt`, `controlled-process.json`, ETL, decoder
XML/summary and raw console output, parser diagnostic output, and
`diagnostic-summary.json`. The summary contains tool versions/hashes/signature
status, host/PowerShell/admin preflight, query-format and loss-counter field
presence, exit codes, ETL-file/rotation/truncation results, parser counters,
controlled-process match booleans/counts, and only the sanitized controlled
process label. No removal command is issued for this direct transient session;
the stop and absence query are retained. Preserve the private directory until
its hashes and retention status are recorded, then remove it only under the
separately controlled owner retention procedure.

## 6. Cleanup and retention

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
