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
    [switch]$SyntheticUnknownPid
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
    if ($IdentityPolicyPath -and (Test-Path -LiteralPath $IdentityPolicyPath -PathType Leaf)) {
        try {
            return Get-Content -LiteralPath $IdentityPolicyPath -Raw | ConvertFrom-Json
        } catch {
            return [ordered]@{ format = 'invalid'; owner_approved = $false; approved_processes = @(); denied_process_name_regex = @() }
        }
    }
    return [ordered]@{ format = 'missing'; owner_approved = $false; approved_processes = @(); denied_process_name_regex = @() }
}

function Get-VerifiedLeaseReceipt {
    if (-not $LeaseReceiptPath -or -not (Test-Path -LiteralPath $LeaseReceiptPath -PathType Leaf)) {
        return @{ verified = $false; error = 'owner-verified lease receipt is missing' }
    }
    try {
        $receipt = Get-Content -LiteralPath $LeaseReceiptPath -Raw | ConvertFrom-Json
        $required = @('format', 'verified', 'host', 'lease_id', 'owner', 'policy_sha256',
                      'valid_from_utc', 'valid_until_utc', 'verification_method')
        foreach ($field in $required) {
            if ($null -eq $receipt.$field) { throw "lease receipt is missing $field" }
        }
        if ($receipt.format -ne 'VOX_FX_T18_HOST_LEASE_RECEIPT_V1' -or
            $receipt.verified -ne $true -or
            $receipt.verification_method -ne 'owner_attested_file') {
            throw 'lease receipt is not an independently owner-attested file'
        }
        if ([string]$receipt.host -ne [string]$env:COMPUTERNAME) {
            throw 'lease receipt host does not match this host'
        }
        $validFrom = [DateTime]::Parse([string]$receipt.valid_from_utc).ToUniversalTime()
        $validUntil = [DateTime]::Parse([string]$receipt.valid_until_utc).ToUniversalTime()
        if ($validUntil -le $validFrom -or $validUntil -lt [DateTime]::UtcNow) {
            throw 'lease receipt is expired or has an invalid interval'
        }
        if ([string]$receipt.policy_sha256 -notmatch '^[0-9a-fA-F]{64}$') {
            throw 'lease receipt policy hash is not a SHA-256'
        }
        $hash = (Get-FileHash -LiteralPath $LeaseReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
        return @{ verified = $true; receipt = $receipt; hash = $hash; error = $null }
    } catch {
        return @{ verified = $false; error = $_.Exception.ToString() }
    }
}

function Get-VerifiedEventLossReceipt([string]$CaptureStartUtc) {
    if (-not $EventLossReceiptPath -or -not (Test-Path -LiteralPath $EventLossReceiptPath -PathType Leaf)) {
        return @{ verified = $false; error = 'owner-attested event-loss receipt is missing' }
    }
    try {
        $receipt = Get-Content -LiteralPath $EventLossReceiptPath -Raw | ConvertFrom-Json
        foreach ($field in @('format', 'provider', 'coverage_complete', 'lost_events',
                             'loss_accounting', 'coverage_start_utc', 'coverage_end_utc')) {
            if ($null -eq $receipt.$field) { throw "event-loss receipt is missing $field" }
        }
        if ($receipt.format -ne 'VOX_FX_T18_EVENT_LOSS_RECEIPT_V1' -or
            $receipt.provider -ne 'Win32_ProcessStartTrace/Win32_ProcessStopTrace' -or
            $receipt.coverage_complete -ne $true -or $receipt.lost_events -ne 0 -or
            $receipt.loss_accounting -ne 'owner_attested') {
            throw 'event-loss receipt does not attest complete lossless provider coverage'
        }
        $coverageStart = [DateTime]::Parse([string]$receipt.coverage_start_utc).ToUniversalTime()
        $coverageEnd = [DateTime]::Parse([string]$receipt.coverage_end_utc).ToUniversalTime()
        $captureStart = [DateTime]::Parse($CaptureStartUtc).ToUniversalTime()
        if ($coverageEnd -le $coverageStart -or $coverageStart -gt $captureStart) {
            throw 'event-loss receipt coverage interval is invalid or begins too late'
        }
        $hash = (Get-FileHash -LiteralPath $EventLossReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
        return @{ verified = $true; receipt = $receipt; hash = $hash; error = $null }
    } catch {
        return @{ verified = $false; error = $_.Exception.ToString() }
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
        return @{ error = $_.Exception.ToString(); processes = @(); unrecognized = @(); unknown = @() }
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

$script:IdentityPolicy = Get-IdentityPolicy
$identityPolicyHash = '<missing>'
$identityPolicyValid = $false
if ($IdentityPolicyPath -and (Test-Path -LiteralPath $IdentityPolicyPath -PathType Leaf)) {
    try {
        $identityPolicyHash = (Get-FileHash -LiteralPath $IdentityPolicyPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $identityPolicyValid = ($script:IdentityPolicy.format -eq 'VOX_FX_T18_IDENTITY_POLICY_V1' -and
            $script:IdentityPolicy.owner_approved -eq $true -and
            [string]$script:IdentityPolicy.policy_id -ne '' -and
            @($script:IdentityPolicy.approved_processes).Count -gt 0)
    } catch { $identityPolicyValid = $false }
}
$verifiedLease = Get-VerifiedLeaseReceipt
$startSource = "VOX-FX-T18-ProcessStart-$PID"
$stopSource = "VOX-FX-T18-ProcessStop-$PID"
$subscriptions = @(); $events = @(); $samples = @(); $gaps = @(); $queryErrors = @()
$unrecognized = @(); $unknownPids = @()
$captureStart = Get-UtcNowText
try {
    try {
        $subscriptions += Register-CimIndicationEvent -Namespace 'root/cimv2' `
            -Query 'SELECT * FROM Win32_ProcessStartTrace' -SourceIdentifier $startSource
        $subscriptions += Register-CimIndicationEvent -Namespace 'root/cimv2' `
            -Query 'SELECT * FROM Win32_ProcessStopTrace' -SourceIdentifier $stopSource
        $eventTraceAvailable = $true; $eventTraceError = $null
    } catch {
        $eventTraceAvailable = $false; $eventTraceError = $_.Exception.ToString()
    }

    function Add-PhaseSamples([string]$Phase, [int]$Seconds) {
        for ($index = 0; $index -le $Seconds; $index++) {
            $before = [DateTime]::UtcNow
            $snapshot = Get-ProcessSnapshot
            if ($snapshot.error) { $script:queryErrors += $snapshot.error }
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
} finally {
    foreach ($subscription in $subscriptions) {
        Unregister-Event -SubscriptionId $subscription.SubscriptionId -ErrorAction SilentlyContinue
    }
    Unregister-Event -SourceIdentifier $startSource -ErrorAction SilentlyContinue
    Unregister-Event -SourceIdentifier $stopSource -ErrorAction SilentlyContinue
}

$captureEnd = Get-UtcNowText
$lossReceipt = Get-VerifiedEventLossReceipt $captureStart
$lossAttested = [bool]$lossReceipt.verified
if ($lossAttested) {
    try {
        $lossEnd = [DateTime]::Parse([string]$lossReceipt.receipt.coverage_end_utc).ToUniversalTime()
        if ($lossEnd -lt [DateTime]::Parse($captureEnd).ToUniversalTime()) {
            $lossAttested = $false
            $lossReceipt.error = 'event-loss receipt ends before capture completion'
        }
    } catch {
        $lossAttested = $false
        $lossReceipt.error = $_.Exception.ToString()
    }
}
$lostEvents = if ($lossAttested) { 0 } else { $null }
$leaseVerified = [bool]$verifiedLease.verified
$leaseReceipt = if ($leaseVerified) { $verifiedLease.receipt } else { $null }
$runnerOwner = if ($leaseReceipt) { [string]$leaseReceipt.owner } else { '<missing>' }
$leaseId = if ($leaseReceipt) { [string]$leaseReceipt.lease_id } else { '<missing>' }
$eventTraceComplete = $eventTraceAvailable -and $queryErrors.Count -eq 0 -and $lossAttested
$events = @($events | Sort-Object timestamp_utc)
$unrecognized = @($unrecognized | Sort-Object -Unique)
$unknownPids = @($unknownPids | Sort-Object -Unique)
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
$witness = [ordered]@{
    format = 'VOX_FX_T18_HOST_WITNESS_V1'; schema_version = 1; mode = 'capture'
    host_observation_mode = 'process_snapshot_and_event_trace'; sample_interval_ms = 1000
    pre_seconds = $PreSeconds; during_seconds = $DuringSeconds; post_seconds = $PostSeconds
    capture_start_utc = $captureStart; capture_end_utc = $captureEnd
    monitor_gaps = @($gaps); identity_policy = $witnessPolicy
    identity_policy_path = if ($IdentityPolicyPath) { [IO.Path]::GetFullPath($IdentityPolicyPath) } else { '<missing>' }
    identity_policy_sha256 = $identityPolicyHash
    event_trace = [ordered]@{
        provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'
        available = $eventTraceAvailable; coverage_complete = $eventTraceComplete
        lost_events = $lostEvents; loss_accounting = if ($lossAttested) { 'owner_attested' } else { 'unknown' }
        loss_receipt_path = if ($EventLossReceiptPath) { [IO.Path]::GetFullPath($EventLossReceiptPath) } else { '<missing>' }
        loss_receipt_sha256 = if ($lossAttested) { $lossReceipt.hash } else { '<missing>' }
        error = $eventTraceError
        loss_receipt_error = $lossReceipt.error
    }
    runner_lease = [ordered]@{
        recognized = [bool]$leaseVerified; owner_verified = [bool]$leaseVerified
        owner = $runnerOwner; host = $env:COMPUTERNAME; lease_id = $leaseId
        verification_receipt_path = if ($LeaseReceiptPath) { [IO.Path]::GetFullPath($LeaseReceiptPath) } else { '<missing>' }
        verification_receipt_sha256 = if ($leaseVerified) { $verifiedLease.hash } else { '<missing>' }
        token = '<redacted>'
    }
    runner_service_identities = @($runnerServiceIdentities | Sort-Object -Unique)
    samples = @($samples); process_events = @($events)
    unrecognized_competitors = @($unrecognized); unknown_pids = @($unknownPids)
    evidence_blocked = (-not $eventTraceComplete) -or $gaps.Count -gt 0 -or
        $unknownPids.Count -gt 0 -or $unrecognized.Count -gt 0 -or
        (-not $leaseVerified) -or (-not $identityPolicyValid)
    query_errors = @($queryErrors)
    fixture_notes = 'Observation only; no service or process control operation is performed.'
}
Write-WitnessJson $witness $OutputPath
if ($witness.evidence_blocked) { exit 2 }
exit 0
