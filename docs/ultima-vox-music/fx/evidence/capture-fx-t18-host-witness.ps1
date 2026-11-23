[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [ValidateSet('Capture', 'OfflineSynthetic')]
    [string]$Mode = 'Capture',
    [ValidateRange(30, 86400)]
    [int]$PreSeconds = 30,
    [ValidateRange(1, 86400)]
    [int]$DuringSeconds = 1,
    [ValidateRange(30, 86400)]
    [int]$PostSeconds = 30,
    [string]$IdentityPolicyPath = '',
    [string]$LeaseReceiptPath = '',
    [string]$EventLossReceiptPath = '',
    [switch]$SyntheticCompetitor,
    [switch]$SyntheticUnknownPid,
    [switch]$SimulateProviderFailure
)

# Safe host-wide observation only. This script never starts, stops, kills,
# disables or reconfigures a service or process. It reads process identities,
# records redacted command-line fields, and subscribes to the Windows process
# start/stop indication providers. Capture mode runs beside an already-
# authorized measurement runner; it does not launch that runner.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Write-WitnessJson([object]$Value, [string]$Path) {
    $parent = Split-Path -Parent $Path
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    $Value | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-UtcNowText {
    return [DateTime]::UtcNow.ToString('o')
}

function Get-OptionalProperty([object]$Object, [string]$Name, [object]$Default) {
    if ($null -eq $Object) { return $Default }
    if ($Object -is [System.Collections.IDictionary] -and $Object.Contains($Name)) {
        if ($null -ne $Object[$Name]) { return $Object[$Name] }
        return $Default
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) { return $Default }
    return $property.Value
}

function New-SafeIdentityPolicy([string]$Format, [string]$PolicyId) {
    return [ordered]@{
        format = $Format
        policy_id = $PolicyId
        owner_approved = $false
        policy_sha256 = '<missing>'
        approved_processes = @()
        denied_process_name_regex = @()
    }
}

function New-OfflinePolicy {
    return [ordered]@{
        format = 'VOX_FX_T18_IDENTITY_POLICY_V1'
        policy_id = 'offline-fixture-policy'
        owner_approved = $true
        policy_sha256 = ('a' * 64)
        approved_processes = @([ordered]@{
            pid = 1000; process_name = 'Runner.Listener.exe'; role = 'runner'
        })
        denied_process_name_regex = @(
            '^(devenv|msbuild|cmake|ctest|ninja|clang|gcc|python|cubase|reaper|ableton|flstudio|protools)'
        )
    }
}

function Get-IdentityPolicy {
    if (-not $IdentityPolicyPath) {
        return New-SafeIdentityPolicy 'missing' '<missing>'
    }
    try {
        if (-not (Test-Path -LiteralPath $IdentityPolicyPath -PathType Leaf)) {
            return New-SafeIdentityPolicy 'missing' '<missing>'
        }
        $raw = Get-Content -LiteralPath $IdentityPolicyPath -Raw | ConvertFrom-Json
        $format = [string](Get-OptionalProperty $raw 'format' 'invalid')
        $policyId = [string](Get-OptionalProperty $raw 'policy_id' '<missing>')
        $ownerApproved = [bool](Get-OptionalProperty $raw 'owner_approved' $false)
        $policyHash = [string](Get-OptionalProperty $raw 'policy_sha256' '<missing>')
        $approved = @(Get-OptionalProperty $raw 'approved_processes' @())
        $denied = @(Get-OptionalProperty $raw 'denied_process_name_regex' @())
        return [ordered]@{
            format = $format
            policy_id = $policyId
            owner_approved = $ownerApproved
            policy_sha256 = $policyHash
            approved_processes = $approved
            denied_process_name_regex = $denied
        }
    } catch {
        return New-SafeIdentityPolicy 'invalid' '<invalid>'
    }
}

function Get-VerifiedLeaseReceipt {
    if (-not $LeaseReceiptPath) {
        return @{ verified = $false; receipt = $null; hash = '<missing>'; error_code = 'LEASE_RECEIPT_MISSING'; error_category = 'ATTESTATION_MISSING'; error = 'owner-verified lease receipt is missing' }
    }
    try {
        if (-not (Test-Path -LiteralPath $LeaseReceiptPath -PathType Leaf)) {
            return @{ verified = $false; receipt = $null; hash = '<missing>'; error_code = 'LEASE_RECEIPT_MISSING'; error_category = 'ATTESTATION_MISSING'; error = 'owner-verified lease receipt is missing' }
        }
        $receipt = Get-Content -LiteralPath $LeaseReceiptPath -Raw | ConvertFrom-Json
        $required = @('format', 'verified', 'host', 'lease_id', 'owner', 'policy_sha256',
                      'valid_from_utc', 'valid_until_utc', 'verification_method')
        foreach ($field in $required) {
            if ($null -eq (Get-OptionalProperty $receipt $field $null)) { throw "missing $field" }
        }
        if ($receipt.format -ne 'VOX_FX_T18_HOST_LEASE_RECEIPT_V1' -or
            $receipt.verified -ne $true -or
            $receipt.verification_method -ne 'owner_attested_file') {
            throw 'not owner-attested'
        }
        if ([string]$receipt.host -ne [string]$env:COMPUTERNAME) {
            throw 'host mismatch'
        }
        $validFrom = [DateTime]::Parse([string]$receipt.valid_from_utc).ToUniversalTime()
        $validUntil = [DateTime]::Parse([string]$receipt.valid_until_utc).ToUniversalTime()
        if ($validUntil -le $validFrom -or $validUntil -lt [DateTime]::UtcNow) {
            throw 'expired or invalid interval'
        }
        if ([string]$receipt.policy_sha256 -notmatch '^[0-9a-fA-F]{64}$') {
            throw 'policy hash is not SHA-256'
        }
        $hash = (Get-FileHash -LiteralPath $LeaseReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
        return @{ verified = $true; receipt = $receipt; hash = $hash; error_code = $null; error_category = $null; error = $null }
    } catch {
        return @{ verified = $false; receipt = $null; hash = '<invalid>'; error_code = 'LEASE_RECEIPT_INVALID'; error_category = 'ATTESTATION_INVALID'; error = 'owner-verified lease receipt is malformed or unverifiable' }
    }
}

function Get-VerifiedEventLossReceipt([string]$CaptureStartUtc) {
    if (-not $EventLossReceiptPath) {
        return @{ verified = $false; receipt = $null; hash = '<missing>'; error_code = 'EVENT_LOSS_RECEIPT_MISSING'; error_category = 'ATTESTATION_MISSING'; error = 'owner-attested event-loss receipt is missing' }
    }
    try {
        if (-not (Test-Path -LiteralPath $EventLossReceiptPath -PathType Leaf)) {
            return @{ verified = $false; receipt = $null; hash = '<missing>'; error_code = 'EVENT_LOSS_RECEIPT_MISSING'; error_category = 'ATTESTATION_MISSING'; error = 'owner-attested event-loss receipt is missing' }
        }
        $receipt = Get-Content -LiteralPath $EventLossReceiptPath -Raw | ConvertFrom-Json
        foreach ($field in @('format', 'provider', 'coverage_complete', 'lost_events',
                             'loss_accounting', 'coverage_start_utc', 'coverage_end_utc')) {
            if ($null -eq (Get-OptionalProperty $receipt $field $null)) { throw "missing $field" }
        }
        if ($receipt.format -ne 'VOX_FX_T18_EVENT_LOSS_RECEIPT_V1' -or
            $receipt.provider -ne 'Win32_ProcessStartTrace/Win32_ProcessStopTrace' -or
            $receipt.coverage_complete -ne $true -or $receipt.lost_events -ne 0 -or
            $receipt.loss_accounting -ne 'owner_attested') {
            throw 'incomplete lossless provider coverage'
        }
        $coverageStart = [DateTime]::Parse([string]$receipt.coverage_start_utc).ToUniversalTime()
        $coverageEnd = [DateTime]::Parse([string]$receipt.coverage_end_utc).ToUniversalTime()
        $captureStart = [DateTime]::Parse($CaptureStartUtc).ToUniversalTime()
        if ($coverageEnd -le $coverageStart -or $coverageStart -gt $captureStart) {
            throw 'invalid coverage interval'
        }
        $hash = (Get-FileHash -LiteralPath $EventLossReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
        return @{ verified = $true; receipt = $receipt; hash = $hash; error_code = $null; error_category = $null; error = $null }
    } catch {
        return @{ verified = $false; receipt = $null; hash = '<invalid>'; error_code = 'EVENT_LOSS_RECEIPT_INVALID'; error_category = 'ATTESTATION_INVALID'; error = 'owner-attested event-loss receipt is malformed or unverifiable' }
    }
}

function Test-ApprovedProcess([int]$ProcessId, [string]$ProcessName) {
    foreach ($approved in @($script:IdentityPolicy.approved_processes)) {
        if ([int]$approved.pid -eq $ProcessId -and [string]$approved.process_name -ceq $ProcessName) {
            return $true
        }
    }
    foreach ($pattern in @($script:IdentityPolicy.denied_process_name_regex)) {
        if ($ProcessName -match [string]$pattern) { return $false }
    }
    return $false
}

function Get-ProcessStartUtc($Process) {
    try {
        return [System.Management.ManagementDateTimeConverter]::ToDateTime(
            [string]$Process.CreationDate).ToUniversalTime().ToString('o')
    } catch {
        return '<unknown>'
    }
}

function Get-EventTimeUtc($NewEvent) {
    if ($null -eq $NewEvent.TIME_CREATED) { throw 'process event has no provider timestamp' }
    return [DateTime]::FromFileTimeUtc([Int64]$NewEvent.TIME_CREATED).ToString('o')
}

function Get-ProcessSnapshot {
    $items = @()
    $unrecognized = @()
    $unknown = @()
    try {
        $processes = Get-CimInstance -ClassName Win32_Process -ErrorAction Stop
        foreach ($process in $processes) {
            $pidValue = [int]$process.ProcessId
            $name = [string]$process.Name
            $startUtc = Get-ProcessStartUtc $process
            $identityKnown = $startUtc -ne '<unknown>'
            $approved = $identityKnown -and (Test-ApprovedProcess $pidValue $name)
            $items += [ordered]@{
                pid = $pidValue
                process_name = $name
                parent_pid = [int]$process.ParentProcessId
                start_time_utc = $startUtc
                # Never read or serialize Win32_Process.CommandLine.
                command_line = '<redacted>'
                job_identity = '<redacted-or-not-applicable>'
                service_identity = '<redacted-or-not-applicable>'
                identity_known = $identityKnown
                approved_by_policy = $approved
            }
            if (-not $identityKnown) { $unknown += $pidValue }
            elseif (-not $approved) { $unrecognized += "$name#$pidValue" }
        }
    } catch {
        return @{ error = 'PROCESS_QUERY_FAILURE'; processes = @(); unrecognized = @(); unknown = @() }
    }
    return @{ error = $null; processes = @($items); unrecognized = @($unrecognized); unknown = @($unknown) }
}

function Get-EventRecords([string]$SourceIdentifier) {
    $records = @()
    $unrecognized = @()
    $unknown = @()
    foreach ($event in @(Get-Event -SourceIdentifier $SourceIdentifier -ErrorAction SilentlyContinue)) {
        try {
            $newEvent = $event.SourceEventArgs.NewEvent
            $pidValue = [int]$newEvent.ProcessID
            $name = [string]$newEvent.ProcessName
            $eventTimestamp = Get-EventTimeUtc $newEvent
            $approved = Test-ApprovedProcess $pidValue $name
            $records += [ordered]@{
                event = if ($SourceIdentifier -like '*start*') { 'start' } else { 'stop' }
                timestamp_utc = $eventTimestamp
                pid = $pidValue
                process_name = $name
                command_line = '<redacted>'
                job_identity = '<redacted-or-not-applicable>'
                service_identity = '<redacted-or-not-applicable>'
                identity_known = $true
                approved_by_policy = $approved
            }
            if (-not $approved) { $unrecognized += "$name#$pidValue" }
        } catch {
            $records += [ordered]@{
                event = 'unparsed'; timestamp_utc = Get-UtcNowText; pid = -1
                process_name = '<unknown>'; command_line = '<redacted>'
                job_identity = '<unknown>'; service_identity = '<unknown>'
                identity_known = $false; approved_by_policy = $false
            }
            $unknown += -1
        }
        Remove-Event -EventIdentifier $event.EventIdentifier -ErrorAction SilentlyContinue
    }
    return @{ records = @($records); unrecognized = @($unrecognized); unknown = @($unknown) }
}

function New-OfflineWitness {
    $base = [DateTime]::Parse('2026-11-22T00:00:00Z').ToUniversalTime()
    $events = @(); $competitors = @(); $unknownPids = @()
    if ($SyntheticCompetitor) {
        # The synthetic process is absent from every one-Hz snapshot. Only its
        # event start/stop proves that it lived briefly between samples.
        $events += [ordered]@{
            event = 'start'; timestamp_utc = $base.AddSeconds(1.1).ToString('o')
            pid = 42421; process_name = 'vox-fx-short-lived-competitor.exe'
            command_line = '<redacted>'; job_identity = '<redacted-or-not-applicable>'
            service_identity = '<redacted-or-not-applicable>'; identity_known = $true
            approved_by_policy = $false
        }
        $events += [ordered]@{
            event = 'stop'; timestamp_utc = $base.AddSeconds(1.7).ToString('o')
            pid = 42421; process_name = 'vox-fx-short-lived-competitor.exe'
            command_line = '<redacted>'; job_identity = '<redacted-or-not-applicable>'
            service_identity = '<redacted-or-not-applicable>'; identity_known = $true
            approved_by_policy = $false
        }
        $competitors += 'vox-fx-short-lived-competitor.exe#42421'
    }
    if ($SyntheticUnknownPid) {
        $events += [ordered]@{
            event = 'start'; timestamp_utc = $base.AddSeconds(2.1).ToString('o')
            pid = 42422; process_name = '<unknown>'; command_line = '<redacted>'
            job_identity = '<unknown>'; service_identity = '<unknown>'
            identity_known = $false; approved_by_policy = $false
        }
        $unknownPids += 42422
    }
    $policy = New-OfflinePolicy
    $samples = @()
    for ($index = 0; $index -le 30; $index++) {
        $samples += [ordered]@{
            phase = 'pre'; timestamp_utc = $base.AddSeconds($index).ToString('o')
            processes = @([ordered]@{
                pid = 1000; process_name = 'Runner.Listener.exe'; parent_pid = 1
                start_time_utc = $base.AddSeconds(-60).ToString('o')
                command_line = '<redacted>'; job_identity = '<redacted-or-not-applicable>'
                service_identity = '<redacted-or-not-applicable>'; identity_known = $true
                approved_by_policy = $true
            }); total_processes = 1; process_query_error = $null
        }
    }
    for ($index = 31; $index -le 91; $index++) {
        $samples += [ordered]@{
            phase = 'during'; timestamp_utc = $base.AddSeconds($index).ToString('o')
            processes = @([ordered]@{
                pid = 1000; process_name = 'Runner.Listener.exe'; parent_pid = 1
                start_time_utc = $base.AddSeconds(-60).ToString('o')
                command_line = '<redacted>'; job_identity = '<redacted-or-not-applicable>'
                service_identity = '<redacted-or-not-applicable>'; identity_known = $true
                approved_by_policy = $true
            }); total_processes = 1; process_query_error = $null
        }
    }
    for ($index = 92; $index -le 122; $index++) {
        $samples += [ordered]@{
            phase = 'post'; timestamp_utc = $base.AddSeconds($index).ToString('o')
            processes = @([ordered]@{
                pid = 1000; process_name = 'Runner.Listener.exe'; parent_pid = 1
                start_time_utc = $base.AddSeconds(-60).ToString('o')
                command_line = '<redacted>'; job_identity = '<redacted-or-not-applicable>'
                service_identity = '<redacted-or-not-applicable>'; identity_known = $true
                approved_by_policy = $true
            }); total_processes = 1; process_query_error = $null
        }
    }
    return [ordered]@{
        format = 'VOX_FX_T18_HOST_WITNESS_V1'; schema_version = 1
        mode = 'offline_synthetic'; host_observation_mode = 'process_snapshot_and_event_trace'
        sample_interval_ms = 1000; pre_seconds = 30; during_seconds = 60; post_seconds = 30
        capture_start_utc = $base.ToString('o'); capture_end_utc = $base.AddSeconds(122).ToString('o')
        monitor_gaps = @()
        identity_policy = $policy
        event_trace = [ordered]@{
            provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'
            available = $true; coverage_complete = 1; lost_events = 0
            loss_accounting = 'owner_attested'
        }
        runner_lease = [ordered]@{
            recognized = $true; owner_verified = $true; owner = 'offline-fixture'
            host = 'offline-fixture'; lease_id = 'offline-lease'
            verification_receipt_sha256 = ('b' * 64); token = '<redacted>'
        }
        runner_service_identities = @('offline-fixture')
        samples = @($samples); process_events = @($events)
        unrecognized_competitors = @($competitors); unknown_pids = @($unknownPids)
        evidence_blocked = $false
        fixture_notes = 'Synthetic short-lived and unknown-PID cases; no process was launched.'
    }
}

if ($Mode -eq 'OfflineSynthetic') {
    if (-not $SyntheticCompetitor -and -not $SyntheticUnknownPid) {
        throw 'OfflineSynthetic requires -SyntheticCompetitor or -SyntheticUnknownPid'
    }
    Write-WitnessJson (New-OfflineWitness) $OutputPath
    exit 0
}

function New-BlockedWitness(
    [string]$CaptureStartUtc,
    [string]$CaptureEndUtc,
    [string[]]$ReasonCodes,
    [string]$ErrorCategory,
    [string[]]$ErrorSummaries,
    [string]$IdentityPolicyHash,
    [object]$LeaseStatus,
    [object]$LossStatus,
    [bool]$EventTraceAvailable,
    [string]$EventTraceError,
    [int]$SampleCount,
    [int]$EventCount,
    [int]$QueryErrorCount,
    [int]$GapCount
) {
    $policy = $script:IdentityPolicy
    $policyId = [string](Get-OptionalProperty $policy 'policy_id' '<missing>')
    $policyFormat = [string](Get-OptionalProperty $policy 'format' 'invalid')
    $leaseVerified = [bool](Get-OptionalProperty $LeaseStatus 'verified' $false)
    $lossVerified = [bool](Get-OptionalProperty $LossStatus 'verified' $false)
    $safeReasons = @($ReasonCodes | Where-Object { $_ } | Select-Object -Unique)
    $safeSummaries = @($ErrorSummaries | Where-Object { $_ } | Select-Object -Unique)
    if ($safeReasons.Count -eq 0) { $safeReasons = @('CAPTURE_BLOCKED') }
    if ($safeSummaries.Count -eq 0) { $safeSummaries = @('host witness capture was blocked') }
    return [ordered]@{
        format = 'VOX_FX_T18_HOST_WITNESS_V1'
        schema_version = 1
        mode = 'capture'
        host_observation_mode = 'process_snapshot_and_event_trace'
        sample_interval_ms = 1000
        pre_seconds = $PreSeconds
        during_seconds = $DuringSeconds
        post_seconds = $PostSeconds
        capture_start_utc = if ($CaptureStartUtc) { $CaptureStartUtc } else { Get-UtcNowText }
        capture_end_utc = if ($CaptureEndUtc) { $CaptureEndUtc } else { Get-UtcNowText }
        monitor_gaps = @()
        identity_policy = [ordered]@{
            format = $policyFormat
            policy_id = $policyId
            owner_approved = $false
            policy_sha256 = $IdentityPolicyHash
            # Blocked receipts intentionally suppress the raw approved PID list.
            approved_processes = @()
            denied_process_name_regex = @()
        }
        identity_policy_path = '<redacted>'
        identity_policy_sha256 = $IdentityPolicyHash
        event_trace = [ordered]@{
            provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'
            available = [bool]$EventTraceAvailable
            coverage_complete = $false
            lost_events = $null
            loss_accounting = 'unknown'
            loss_receipt_path = '<redacted>'
            loss_receipt_sha256 = '<redacted>'
            error = $EventTraceError
            loss_receipt_error = [string](Get-OptionalProperty $LossStatus 'error' 'event-loss attestation unavailable')
        }
        runner_lease = [ordered]@{
            recognized = $leaseVerified
            owner_verified = $leaseVerified
            owner = if ($leaseVerified) { '<owner-attested>' } else { '<missing>' }
            host = [string]$env:COMPUTERNAME
            lease_id = if ($leaseVerified) { '<redacted>' } else { '<missing>' }
            verification_receipt_path = '<redacted>'
            verification_receipt_sha256 = '<redacted>'
            token = '<redacted>'
        }
        runner_service_identities = @()
        # Do not persist raw process/event records, names or PIDs in a blocked receipt.
        samples = @()
        process_events = @()
        unrecognized_competitors = @()
        unknown_pids = @()
        inventory_summary = [ordered]@{
            samples_observed = $SampleCount
            events_observed = $EventCount
            query_error_count = $QueryErrorCount
            monitor_gap_count = $GapCount
            raw_inventory_suppressed = $true
        }
        evidence_blocked = $true
        query_errors = @($safeReasons)
        diagnostic = [ordered]@{
            error_category = $ErrorCategory
            reason_codes = @($safeReasons)
            error_summaries = @($safeSummaries)
            sanitized = $true
            recommended_exit_code = 2
        }
        fixture_notes = 'Capture was blocked; raw process inventory and event identities were suppressed.'
    }
}

$script:IdentityPolicy = New-SafeIdentityPolicy 'missing' '<missing>'
$identityPolicyHash = '<missing>'
$identityPolicyValid = $false
$policyReasonCodes = @()
$policySummaries = @()
$script:IdentityPolicy = Get-IdentityPolicy

if ($IdentityPolicyPath) {
    try {
        if (Test-Path -LiteralPath $IdentityPolicyPath -PathType Leaf) {
            $identityPolicyHash = (Get-FileHash -LiteralPath $IdentityPolicyPath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    } catch {
        $policyReasonCodes += 'IDENTITY_POLICY_HASH_FAILURE'
        $policySummaries += 'identity policy hash could not be computed'
    }
}
$policyFormat = [string](Get-OptionalProperty $script:IdentityPolicy 'format' 'invalid')
$policyId = [string](Get-OptionalProperty $script:IdentityPolicy 'policy_id' '<missing>')
$policyOwnerApproved = [bool](Get-OptionalProperty $script:IdentityPolicy 'owner_approved' $false)
$approvedEntries = @(Get-OptionalProperty $script:IdentityPolicy 'approved_processes' @())
$deniedEntries = @(Get-OptionalProperty $script:IdentityPolicy 'denied_process_name_regex' @())
$policyEntriesValid = $approvedEntries.Count -gt 0
foreach ($approved in $approvedEntries) {
    $approvedPid = Get-OptionalProperty $approved 'pid' $null
    $approvedName = [string](Get-OptionalProperty $approved 'process_name' '')
    if ($null -eq $approvedPid -or $approvedName -eq '') { $policyEntriesValid = $false }
}
$identityPolicyValid = ($policyFormat -eq 'VOX_FX_T18_IDENTITY_POLICY_V1' -and
    $policyOwnerApproved -and $policyId -ne '' -and $policyId -ne '<missing>' -and
    $policyEntriesValid -and $deniedEntries.Count -ge 0 -and $identityPolicyHash -match '^[0-9a-fA-F]{64}$')
if (-not $identityPolicyValid) {
    if ($policyFormat -eq 'missing') {
        $policyReasonCodes += 'IDENTITY_POLICY_MISSING'
        $policySummaries += 'owner-approved identity policy is missing'
    } else {
        $policyReasonCodes += 'IDENTITY_POLICY_INVALID'
        $policySummaries += 'owner-approved identity policy is malformed or unverifiable'
    }
}

$captureStart = Get-UtcNowText
$verifiedLease = Get-VerifiedLeaseReceipt
$lossReceipt = Get-VerifiedEventLossReceipt $captureStart
$preflightCodes = @($policyReasonCodes)
$preflightSummaries = @($policySummaries)
if (-not [bool](Get-OptionalProperty $verifiedLease 'verified' $false)) {
    $preflightCodes += [string](Get-OptionalProperty $verifiedLease 'error_code' 'LEASE_RECEIPT_INVALID')
    $preflightSummaries += [string](Get-OptionalProperty $verifiedLease 'error' 'owner-verified lease receipt is unavailable')
}
if (-not [bool](Get-OptionalProperty $lossReceipt 'verified' $false)) {
    $preflightCodes += [string](Get-OptionalProperty $lossReceipt 'error_code' 'EVENT_LOSS_RECEIPT_INVALID')
    $preflightSummaries += [string](Get-OptionalProperty $lossReceipt 'error' 'event-loss attestation is unavailable')
}
$leaseReceiptForCheck = Get-OptionalProperty $verifiedLease 'receipt' $null
$leasePolicyHash = [string](Get-OptionalProperty $leaseReceiptForCheck 'policy_sha256' '')
if ([bool](Get-OptionalProperty $verifiedLease 'verified' $false) -and
    $identityPolicyHash -match '^[0-9a-fA-F]{64}$' -and
    $leasePolicyHash -ine $identityPolicyHash) {
    $preflightCodes += 'LEASE_POLICY_MISMATCH'
    $preflightSummaries += 'owner lease is bound to a different process policy'
}
$preflightCodes = @($preflightCodes | Where-Object { $_ } | Select-Object -Unique)
$preflightSummaries = @($preflightSummaries | Where-Object { $_ } | Select-Object -Unique)
if ($preflightCodes.Count -gt 0) {
    $blocked = New-BlockedWitness $captureStart (Get-UtcNowText) $preflightCodes 'ATTESTATION_BLOCKED' `
        $preflightSummaries $identityPolicyHash $verifiedLease $lossReceipt $false `
        'capture did not start because required owner attestations were unavailable' 0 0 0 0
    try {
        Write-WitnessJson $blocked $OutputPath
    } catch {
        try {
            $minimal = [ordered]@{
                format = 'VOX_FX_T18_HOST_WITNESS_V1'; schema_version = 1; mode = 'capture'
                evidence_blocked = $true; capture_start_utc = $captureStart; capture_end_utc = Get-UtcNowText
                reason_codes = @($preflightCodes); error_category = 'RECEIPT_WRITE_FAILURE'
                diagnostic = [ordered]@{ error_category = 'RECEIPT_WRITE_FAILURE'; reason_codes = @('RECEIPT_WRITE_FAILURE'); sanitized = $true; recommended_exit_code = 2 }
                samples = @(); process_events = @(); unrecognized_competitors = @(); unknown_pids = @()
            }
            Write-WitnessJson $minimal $OutputPath
        } catch {
            Write-Error 'Unable to persist the blocked host witness receipt'
        }
    }
    exit 2
}

$eventTraceAvailable = $false
$eventTraceError = $null
$subscriptions = @()
$events = @()
$samples = @()
$gaps = @()
$unrecognized = @()
$unknownPids = @()
$captureFailureCodes = @()
$captureFailureSummaries = @()
$startSource = $null
$stopSource = $null
$script:events = @()
$script:samples = @()
$script:gaps = @()
$script:unrecognized = @()
$script:unknownPids = @()
$script:queryErrors = @()
$script:queryErrorCodes = @()

try {
    if ($SimulateProviderFailure) { throw 'simulated provider/query failure' }
    try {
        $startSource = "VOX-FX-T18-ProcessStart-$PID"
        $stopSource = "VOX-FX-T18-ProcessStop-$PID"
        $subscriptions += Register-CimIndicationEvent -Namespace 'root/cimv2' `
            -Query 'SELECT * FROM Win32_ProcessStartTrace' -SourceIdentifier $startSource
        $subscriptions += Register-CimIndicationEvent -Namespace 'root/cimv2' `
            -Query 'SELECT * FROM Win32_ProcessStopTrace' -SourceIdentifier $stopSource
        $eventTraceAvailable = $true
    } catch {
        $eventTraceAvailable = $false
        $eventTraceError = 'process event provider registration failed'
        $captureFailureCodes += 'EVENT_PROVIDER_UNAVAILABLE'
        $captureFailureSummaries += 'process event provider could not be registered'
    }

    function Add-PhaseSamples([string]$Phase, [int]$Seconds) {
        for ($index = 0; $index -le $Seconds; $index++) {
            $before = [DateTime]::UtcNow
            $snapshot = Get-ProcessSnapshot
            if ($snapshot.error) {
                $script:queryErrors += [string]$snapshot.error
                $script:queryErrorCodes += 'PROCESS_QUERY_FAILURE'
            }
            $script:samples += [ordered]@{
                phase = $Phase; timestamp_utc = Get-UtcNowText
                processes = @($snapshot.processes); total_processes = @($snapshot.processes).Count
                process_query_error = $snapshot.error
            }
            $script:unrecognized += @($snapshot.unrecognized)
            $script:unknownPids += @($snapshot.unknown)
            if ($eventTraceAvailable) {
                $startEvents = Get-EventRecords $startSource
                $stopEvents = Get-EventRecords $stopSource
                $script:events += @($startEvents.records) + @($stopEvents.records)
                $script:unrecognized += @($startEvents.unrecognized) + @($stopEvents.unrecognized)
                $script:unknownPids += @($startEvents.unknown) + @($stopEvents.unknown)
            }
            $elapsed = ([DateTime]::UtcNow - $before).TotalMilliseconds
            if ($elapsed -gt 1100) { $script:gaps += [ordered]@{ phase = $Phase; observed_ms = $elapsed } }
            if ($index -lt $Seconds) { Start-Sleep -Milliseconds 1000 }
        }
    }
    Add-PhaseSamples 'pre' $PreSeconds
    Add-PhaseSamples 'during' $DuringSeconds
    Add-PhaseSamples 'post' $PostSeconds
} catch {
    $captureFailureCodes += 'CAPTURE_EXCEPTION'
    $captureFailureSummaries += 'host process observation failed before a complete witness was collected'
} finally {
    foreach ($subscription in @($subscriptions)) {
        try {
            $subscriptionId = Get-OptionalProperty $subscription 'SubscriptionId' $null
            if ($null -ne $subscriptionId) {
                Unregister-Event -SubscriptionId $subscriptionId -ErrorAction SilentlyContinue
            }
        } catch { }
    }
    try { if ($startSource) { Unregister-Event -SourceIdentifier $startSource -ErrorAction SilentlyContinue } } catch { }
    try { if ($stopSource) { Unregister-Event -SourceIdentifier $stopSource -ErrorAction SilentlyContinue } } catch { }
}

$captureEnd = Get-UtcNowText
$samples = @($script:samples)
$events = @($script:events)
$gaps = @($script:gaps)
$unrecognized = @($script:unrecognized)
$unknownPids = @($script:unknownPids)
$verifiedLease = Get-VerifiedLeaseReceipt
$lossReceipt = Get-VerifiedEventLossReceipt $captureStart
$lossAttested = [bool](Get-OptionalProperty $lossReceipt 'verified' $false)
if ($lossAttested) {
    try {
        $lossEnd = [DateTime]::Parse([string]$lossReceipt.receipt.coverage_end_utc).ToUniversalTime()
        if ($lossEnd -lt [DateTime]::Parse($captureEnd).ToUniversalTime()) {
            $lossAttested = $false
            $lossReceipt.error_code = 'EVENT_LOSS_COVERAGE_SHORT'
            $lossReceipt.error = 'event-loss receipt ends before capture completion'
        }
    } catch {
        $lossAttested = $false
        $lossReceipt.error_code = 'EVENT_LOSS_RECEIPT_INVALID'
        $lossReceipt.error = 'event-loss receipt completion time is invalid'
    }
}
$eventTraceComplete = $eventTraceAvailable -and $script:queryErrors.Count -eq 0 -and $lossAttested
$events = @($events | Sort-Object timestamp_utc)
$unrecognized = @($unrecognized | Sort-Object -Unique)
$unknownPids = @($unknownPids | Sort-Object -Unique)
$reasonCodes = @($captureFailureCodes + $script:queryErrorCodes)
$reasonSummaries = @($captureFailureSummaries)
if (-not [bool](Get-OptionalProperty $verifiedLease 'verified' $false)) {
    $reasonCodes += [string](Get-OptionalProperty $verifiedLease 'error_code' 'LEASE_RECEIPT_INVALID')
    $reasonSummaries += [string](Get-OptionalProperty $verifiedLease 'error' 'owner-verified lease receipt became unavailable')
}
$leaseReceiptAfter = Get-OptionalProperty $verifiedLease 'receipt' $null
$leasePolicyHashAfter = [string](Get-OptionalProperty $leaseReceiptAfter 'policy_sha256' '')
if ([bool](Get-OptionalProperty $verifiedLease 'verified' $false) -and
    $identityPolicyHash -match '^[0-9a-fA-F]{64}$' -and
    $leasePolicyHashAfter -ine $identityPolicyHash) {
    $reasonCodes += 'LEASE_POLICY_MISMATCH'
    $reasonSummaries += 'owner lease is bound to a different process policy'
}
if (-not $eventTraceComplete) {
    if (-not $eventTraceAvailable) { $reasonCodes += 'EVENT_PROVIDER_UNAVAILABLE'; $reasonSummaries += 'process event provider was unavailable' }
    if ($script:queryErrors.Count -gt 0) { $reasonCodes += 'PROCESS_QUERY_FAILURE'; $reasonSummaries += 'one or more process queries failed' }
    if (-not $lossAttested) { $reasonCodes += [string](Get-OptionalProperty $lossReceipt 'error_code' 'EVENT_LOSS_RECEIPT_INVALID'); $reasonSummaries += [string](Get-OptionalProperty $lossReceipt 'error' 'event-loss attestation is unavailable') }
}
if ($gaps.Count -gt 0) { $reasonCodes += 'MONITOR_GAP'; $reasonSummaries += 'host observation sampling had a gap' }
if ($unknownPids.Count -gt 0) { $reasonCodes += 'UNKNOWN_PROCESS_IDENTITY'; $reasonSummaries += 'one or more process identities were unknown' }
if ($unrecognized.Count -gt 0) { $reasonCodes += 'UNRECOGNIZED_PROCESS_IDENTITY'; $reasonSummaries += 'one or more observed process identities were not approved' }
$reasonCodes = @($reasonCodes | Where-Object { $_ } | Select-Object -Unique)
$reasonSummaries = @($reasonSummaries | Where-Object { $_ } | Select-Object -Unique)
if ($reasonCodes.Count -gt 0) {
    $blocked = New-BlockedWitness $captureStart $captureEnd $reasonCodes 'CAPTURE_BLOCKED' `
        $reasonSummaries $identityPolicyHash $verifiedLease $lossReceipt $eventTraceAvailable `
        $eventTraceError $samples.Count $events.Count $script:queryErrors.Count $gaps.Count
    try {
        Write-WitnessJson $blocked $OutputPath
    } catch {
        Write-Error 'Unable to persist the blocked host witness receipt'
    }
    exit 2
}

$runnerServiceIdentities = @()
if ($env:RUNNER_NAME) { $runnerServiceIdentities += "runner:$($env:RUNNER_NAME)" }
if ($env:GITHUB_JOB) { $runnerServiceIdentities += "job:$($env:GITHUB_JOB)" }
$witnessPolicy = [ordered]@{
    format = $script:IdentityPolicy.format
    policy_id = $script:IdentityPolicy.policy_id
    owner_approved = [bool]$script:IdentityPolicy.owner_approved
    policy_sha256 = $identityPolicyHash
    approved_processes = @($script:IdentityPolicy.approved_processes)
    denied_process_name_regex = @($script:IdentityPolicy.denied_process_name_regex)
}
$leaseReceipt = $verifiedLease.receipt
$witness = [ordered]@{
    format = 'VOX_FX_T18_HOST_WITNESS_V1'; schema_version = 1; mode = 'capture'
    host_observation_mode = 'process_snapshot_and_event_trace'; sample_interval_ms = 1000
    pre_seconds = $PreSeconds; during_seconds = $DuringSeconds; post_seconds = $PostSeconds
    capture_start_utc = $captureStart; capture_end_utc = $captureEnd
    monitor_gaps = @($gaps); identity_policy = $witnessPolicy
    identity_policy_path = [IO.Path]::GetFullPath($IdentityPolicyPath)
    identity_policy_sha256 = $identityPolicyHash
    event_trace = [ordered]@{
        provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'
        available = $eventTraceAvailable; coverage_complete = $eventTraceComplete
        lost_events = 0; loss_accounting = 'owner_attested'
        loss_receipt_path = [IO.Path]::GetFullPath($EventLossReceiptPath)
        loss_receipt_sha256 = $lossReceipt.hash
        error = $eventTraceError
        loss_receipt_error = $lossReceipt.error
    }
    runner_lease = [ordered]@{
        recognized = $true; owner_verified = $true
        owner = [string]$leaseReceipt.owner; host = $env:COMPUTERNAME
        lease_id = [string]$leaseReceipt.lease_id
        verification_receipt_path = [IO.Path]::GetFullPath($LeaseReceiptPath)
        verification_receipt_sha256 = $verifiedLease.hash
        token = '<redacted>'
    }
    runner_service_identities = @($runnerServiceIdentities | Sort-Object -Unique)
    samples = @($samples); process_events = @($events)
    unrecognized_competitors = @($unrecognized); unknown_pids = @($unknownPids)
    evidence_blocked = $false
    query_errors = @($script:queryErrors)
    fixture_notes = 'Observation only; no service or process control operation is performed.'
}
try {
    Write-WitnessJson $witness $OutputPath
} catch {
    $blocked = New-BlockedWitness $captureStart $captureEnd @('RECEIPT_WRITE_FAILURE') 'RECEIPT_WRITE_FAILURE' `
        @('host witness receipt serialization failed') $identityPolicyHash $verifiedLease $lossReceipt $eventTraceAvailable `
        $eventTraceError $samples.Count $events.Count $script:queryErrors.Count $gaps.Count
    try { Write-WitnessJson $blocked $OutputPath } catch { Write-Error 'Unable to persist the host witness receipt' }
    exit 2
}
exit 0
