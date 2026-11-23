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
    [switch]$SyntheticCompetitor,
    [switch]$SyntheticUnknownPid
)

# Safe host-wide observation only. This script never starts, stops, kills,
# disables or reconfigures a service or process. It reads process identities,
# records redacted command-line fields, and subscribes to the Windows process
# start/stop indication providers. The capture mode is intended to run beside
# an already-authorized measurement runner; it does not launch that runner.
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

function Get-ProcessSnapshot {
    $items = @()
    try {
        $processes = Get-CimInstance -ClassName Win32_Process -ErrorAction Stop
        foreach ($process in $processes) {
            $items += [ordered]@{
                pid = [int]$process.ProcessId
                process_name = [string]$process.Name
                parent_pid = [int]$process.ParentProcessId
                # Never read or serialize Win32_Process.CommandLine.
                command_line = '<redacted>'
                job_identity = '<redacted-or-not-applicable>'
                service_identity = '<redacted-or-not-applicable>'
                identity_known = $true
            }
        }
    } catch {
        return @{ error = $_.Exception.ToString(); processes = @() }
    }
    return @{ error = $null; processes = @($items) }
}

function Get-EventRecords([string]$SourceIdentifier) {
    $records = @()
    foreach ($event in @(Get-Event -SourceIdentifier $SourceIdentifier -ErrorAction SilentlyContinue)) {
        try {
            $newEvent = $event.SourceEventArgs.NewEvent
            $pidValue = [int]$newEvent.ProcessID
            $name = [string]$newEvent.ProcessName
            $records += [ordered]@{
                event = if ($SourceIdentifier -like '*start*') { 'start' } else { 'stop' }
                timestamp_utc = Get-UtcNowText
                pid = $pidValue
                process_name = $name
                command_line = '<redacted>'
                job_identity = '<redacted-or-not-applicable>'
                service_identity = '<redacted-or-not-applicable>'
                identity_known = $true
            }
        } catch {
            $records += [ordered]@{
                event = 'unparsed'
                timestamp_utc = Get-UtcNowText
                pid = -1
                process_name = '<unknown>'
                command_line = '<redacted>'
                job_identity = '<unknown>'
                service_identity = '<unknown>'
                identity_known = $false
            }
        }
        Remove-Event -EventIdentifier $event.EventIdentifier -ErrorAction SilentlyContinue
    }
    return @($records)
}

function New-OfflineWitness {
    $events = @()
    $competitors = @()
    $unknownPids = @()
    if ($SyntheticCompetitor) {
        # The synthetic process is deliberately absent from every one-Hz
        # snapshot. Only the event trace proves that it lived briefly.
        $events += [ordered]@{
            event = 'start'; timestamp_utc = '2026-11-22T00:00:01.1000000Z'
            pid = 42421; process_name = 'vox-fx-short-lived-competitor.exe'
            command_line = '<redacted>'; job_identity = '<redacted-or-not-applicable>'
            service_identity = '<redacted-or-not-applicable>'; identity_known = $true
        }
        $events += [ordered]@{
            event = 'stop'; timestamp_utc = '2026-11-22T00:00:01.7000000Z'
            pid = 42421; process_name = 'vox-fx-short-lived-competitor.exe'
            command_line = '<redacted>'; job_identity = '<redacted-or-not-applicable>'
            service_identity = '<redacted-or-not-applicable>'; identity_known = $true
        }
        $competitors += 'vox-fx-short-lived-competitor.exe'
    }
    if ($SyntheticUnknownPid) {
        $events += [ordered]@{
            event = 'start'; timestamp_utc = '2026-11-22T00:00:02.1000000Z'
            pid = 42422; process_name = '<unknown>'; command_line = '<redacted>'
            job_identity = '<unknown>'; service_identity = '<unknown>'
            identity_known = $false
        }
        $unknownPids += 42422
    }
    $samplePhases = @('pre', 'during', 'post')
    $samples = @()
    foreach ($phase in $samplePhases) {
        $samples += [ordered]@{
            phase = $phase
            timestamp_utc = '2026-11-22T00:00:30.0000000Z'
            processes = @([ordered]@{
                pid = 1000; process_name = 'Runner.Listener.exe'; parent_pid = 1
                command_line = '<redacted>'; job_identity = '<redacted-or-not-applicable>'
                service_identity = '<redacted-or-not-applicable>'
                identity_known = $true
            })
            total_processes = 1
            process_query_error = $null
        }
    }
    return [ordered]@{
        format = 'VOX_FX_T18_HOST_WITNESS_V1'
        schema_version = 1
        mode = 'offline_synthetic'
        host_observation_mode = 'process_snapshot_and_event_trace'
        sample_interval_ms = 1000
        pre_seconds = 30
        during_seconds = 1
        post_seconds = 30
        capture_start_utc = '2026-11-22T00:00:00.0000000Z'
        capture_end_utc = '2026-11-22T00:01:01.0000000Z'
        monitor_gaps = @()
        event_trace = [ordered]@{
            provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'
            available = $true; coverage_complete = 1; lost_events = 0
        }
        runner_lease = [ordered]@{
            recognized = $true; owner = 'offline-fixture'; host = 'offline-fixture'
            token = '<redacted>'
        }
        runner_service_identities = @('offline-fixture')
        samples = @($samples)
        process_events = @($events)
        unrecognized_competitors = @($competitors)
        unknown_pids = @($unknownPids)
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

$startSource = "VOX-FX-T18-ProcessStart-$PID"
$stopSource = "VOX-FX-T18-ProcessStop-$PID"
$subscriptions = @()
$events = @()
$samples = @()
$gaps = @()
$queryErrors = @()
$captureStart = Get-UtcNowText
try {
    try {
        $subscriptions += Register-CimIndicationEvent -Namespace 'root/cimv2' `
            -Query 'SELECT * FROM Win32_ProcessStartTrace' -SourceIdentifier $startSource
        $subscriptions += Register-CimIndicationEvent -Namespace 'root/cimv2' `
            -Query 'SELECT * FROM Win32_ProcessStopTrace' -SourceIdentifier $stopSource
        $eventTraceAvailable = $true
        $eventTraceError = $null
    } catch {
        $eventTraceAvailable = $false
        $eventTraceError = $_.Exception.ToString()
    }

    function Add-PhaseSamples([string]$Phase, [int]$Seconds) {
        for ($index = 0; $index -lt $Seconds; $index++) {
            $before = [DateTime]::UtcNow
            $snapshot = Get-ProcessSnapshot
            if ($snapshot.error) { $script:queryErrors += $snapshot.error }
            $script:samples += [ordered]@{
                phase = $Phase
                timestamp_utc = Get-UtcNowText
                processes = @($snapshot.processes)
                total_processes = @($snapshot.processes).Count
                process_query_error = $snapshot.error
            }
            if ($eventTraceAvailable) {
                $script:events += Get-EventRecords $startSource
                $script:events += Get-EventRecords $stopSource
            }
            $elapsed = ([DateTime]::UtcNow - $before).TotalMilliseconds
            if ($elapsed -gt 1100) { $script:gaps += [ordered]@{ phase = $Phase; observed_ms = $elapsed } }
            if ($index + 1 -lt $Seconds) { Start-Sleep -Milliseconds 1000 }
        }
    }

    Add-PhaseSamples 'pre' $PreSeconds
    Add-PhaseSamples 'during' $DuringSeconds
    Add-PhaseSamples 'post' $PostSeconds
    if ($eventTraceAvailable) {
        $events += Get-EventRecords $startSource
        $events += Get-EventRecords $stopSource
    }
} finally {
    foreach ($subscription in $subscriptions) {
        Unregister-Event -SubscriptionId $subscription.SubscriptionId -ErrorAction SilentlyContinue
    }
    Unregister-Event -SourceIdentifier $startSource -ErrorAction SilentlyContinue
    Unregister-Event -SourceIdentifier $stopSource -ErrorAction SilentlyContinue
}

$unrecognized = @()
$unknownPids = @()
$runnerServiceIdentities = @()
if ($env:RUNNER_NAME) { $runnerServiceIdentities += "runner:$($env:RUNNER_NAME)" }
if ($env:GITHUB_JOB) { $runnerServiceIdentities += "job:$($env:GITHUB_JOB)" }
foreach ($event in $events) {
    if ($event.identity_known -ne $true -or $event.pid -lt 0) {
        $unknownPids += $event.pid
        continue
    }
    $lowerName = $event.process_name.ToLowerInvariant()
    if ($lowerName -match '^(devenv|msbuild|cmake|ctest|cubase|reaper|ableton|flstudio|protools|vox-fx-short-lived-competitor)') {
        $unrecognized += $event.process_name
    }
}
$unrecognized = @($unrecognized | Sort-Object -Unique)
$unknownPids = @($unknownPids | Sort-Object -Unique)
$eventTraceComplete = $eventTraceAvailable -and $queryErrors.Count -eq 0
$witness = [ordered]@{
    format = 'VOX_FX_T18_HOST_WITNESS_V1'
    schema_version = 1
    mode = 'capture'
    host_observation_mode = 'process_snapshot_and_event_trace'
    sample_interval_ms = 1000
    pre_seconds = $PreSeconds
    during_seconds = $DuringSeconds
    post_seconds = $PostSeconds
    capture_start_utc = $captureStart
    capture_end_utc = Get-UtcNowText
    monitor_gaps = @($gaps)
    event_trace = [ordered]@{
        provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'
        available = $eventTraceAvailable
        coverage_complete = $eventTraceComplete
        lost_events = 0
        error = $eventTraceError
    }
    runner_lease = [ordered]@{
        recognized = [bool]$env:FX_T18_LEASE_OWNER
        owner = if ($env:FX_T18_LEASE_OWNER) { $env:FX_T18_LEASE_OWNER } else { '<missing>' }
        host = $env:COMPUTERNAME
        token = '<redacted>'
    }
    runner_service_identities = @($runnerServiceIdentities | Sort-Object -Unique)
    samples = @($samples)
    process_events = @($events)
    unrecognized_competitors = @($unrecognized)
    unknown_pids = @($unknownPids)
    evidence_blocked = (-not $eventTraceComplete) -or $gaps.Count -gt 0 -or
        $unknownPids.Count -gt 0 -or $unrecognized.Count -gt 0 -or
        (-not [bool]$env:FX_T18_LEASE_OWNER)
    query_errors = @($queryErrors)
    fixture_notes = 'Observation only; no service or process control operation is performed.'
}
Write-WitnessJson $witness $OutputPath
exit 0
