[CmdletBinding()]
param(
    [string]$Python = 'python'
)

# Windows PowerShell 5.1 offline conformance fixtures for the safe host
# witness. They do not launch a competitor, inspect a command line, or alter a
# service. The Python wall evaluator must classify both receipts as blocked.
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrEmpty($PSScriptRoot)) { throw 'PSScriptRoot is required' }
$witnessScript = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'capture-fx-t18-host-witness.ps1')).Path
$auditScript = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'evaluate-fx-t18-wall-gate.py')).Path

$tokens = $null
$parseErrors = $null
[System.Management.Automation.Language.Parser]::ParseFile(
    $witnessScript, [ref]$tokens, [ref]$parseErrors) | Out-Null
if ($parseErrors.Count -ne 0) {
    throw "Host witness PowerShell parser rejected the script: $($parseErrors -join '; ')"
}
$source = Get-Content -LiteralPath $witnessScript -Raw
$forbiddenTokens = @(
    ('Start' + '-Service'), ('Stop' + '-Service'), ('Restart' + '-Service'),
    ('Disable' + '-Service'), ('Set' + '-Service'), ('Stop' + '-Process'),
    ('task' + 'kill.exe'), ('sc.exe')
)
foreach ($forbidden in $forbiddenTokens) {
    if ($source.IndexOf($forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Unsafe service/process control token found: $forbidden"
    }
}
if ($source -match '\$env:FX_T18_LEASE_' -or
    $source -notmatch 'Get-VerifiedOwnerSignature' -or
    $source -notmatch 'Get-VerifiedLeaseReceipt' -or
    $source -notmatch 'Microsoft-Windows-Kernel-Process/ETW' -or
    $source -match 'Get-VerifiedEventLossReceipt' -or
    $source -match 'Get-VerifiedExecutionBinding' -or
    $source -notmatch 'Get-MonotonicSeconds' -or
    $source -notmatch 'Get-EventTimeUtc' -or
    $source -notmatch 'RevocationMode' -or
    $source -notmatch 'VOX-FX-T18-ProcessStart' -or
    $source -notmatch 'VOX_FX_T18_HOST_OBSERVATION_V1' -or
    $source -notmatch "\$Phase -eq 'Observe'") {
    throw 'Host witness two-phase CMS, ETW, lifecycle and monotonic verification is incomplete'
}

function Get-SanitizedChildSummary([string]$Path) {
    $lines = @()
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $lines = @(Get-Content -LiteralPath $Path | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    }
    return [ordered]@{
        line_count = $lines.Count
        summary = if ($lines.Count -eq 0) { 'child stderr was empty' } else { 'child stderr was present and redacted' }
    }
}

$tempRoot = Join-Path $env:TEMP "vox-fx-t18-host-witness-$PID"
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
$script:invocations = @()

function Invoke-CaptureFixture {
    param(
        [string]$Name,
        [string]$ExpectedReason,
        [string]$PolicyPath = '',
        [string]$LeasePath = '',
        [switch]$SimulateProviderFailure
    )
    $outputPath = Join-Path $tempRoot ($Name + '.json')
    $stdoutPath = Join-Path $tempRoot ($Name + '.stdout.txt')
    $stderrPath = Join-Path $tempRoot ($Name + '.stderr.txt')
    $arguments = @(
        '-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
        '-File', $witnessScript, '-Mode', 'Capture', '-FixtureMode', '-OutputPath', $outputPath,
        '-PreSeconds', '30', '-DuringSeconds', '1', '-PostSeconds', '30'
    )
    if ($PolicyPath) { $arguments += @('-IdentityPolicyPath', $PolicyPath) }
    if ($LeasePath) { $arguments += @('-LeaseReceiptPath', $LeasePath) }
    if ($SimulateProviderFailure) { $arguments += '-SimulateProviderFailure' }

    & powershell.exe @arguments 1> $stdoutPath 2> $stderrPath
    $exitCode = $LASTEXITCODE
    $stderrSummary = Get-SanitizedChildSummary $stderrPath
    $invocation = [ordered]@{
        name = $Name
        exit_code = $exitCode
        stderr_line_count = $stderrSummary.line_count
        stderr_summary = $stderrSummary.summary
    }
    $script:invocations += $invocation
    Write-Output ("capture fixture {0}: exit_code={1}; {2}" -f $Name, $exitCode, $stderrSummary.summary)

    if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
        throw "Capture fixture $Name did not persist a witness JSON receipt"
    }
    $raw = Get-Content -LiteralPath $outputPath -Raw
    $receipt = $raw | ConvertFrom-Json
    if ($receipt.format -ne 'VOX_FX_T18_HOST_WITNESS_V1' -or
        $receipt.evidence_blocked -ne $true -or
        @($receipt.samples).Count -ne 0 -or
        @($receipt.process_events).Count -ne 0) {
        throw "Capture fixture $Name did not produce a sanitized blocked receipt"
    }
    if ($raw -match '"pid"\s*:' -or $raw -match '"command_line"\s*:') {
        throw "Capture fixture $Name leaked raw process identity fields"
    }
    if ($receipt.diagnostic.reason_codes -notcontains $ExpectedReason) {
        throw "Capture fixture $Name omitted expected reason $ExpectedReason"
    }
    if ($exitCode -ne 2) {
        throw "Capture fixture $Name returned $exitCode instead of blocked exit code 2"
    }
    return $invocation
}

$failure = $null
try {
    $competitorPath = Join-Path $tempRoot 'short-lived-competitor.json'
    & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $witnessScript `
        -Mode OfflineSynthetic -FixtureMode -SyntheticCompetitor -OutputPath $competitorPath
    if ($LASTEXITCODE -ne 2) { throw "Synthetic competitor fixture exited $LASTEXITCODE instead of qualification-prohibited 2" }
    if ($competitorPath -and (Get-Content -LiteralPath $competitorPath -Raw) -notmatch 'qualification_prohibited') { throw 'Synthetic competitor fixture was not qualification-prohibited' }
    $competitor = Get-Content -LiteralPath $competitorPath -Raw | ConvertFrom-Json
    if ($competitor.format -ne 'VOX_FX_T18_HOST_WITNESS_V1' -or
        @($competitor.unrecognized_competitors).Count -ne 1 -or
        @($competitor.process_events).Count -ne 2) {
        throw 'Short-lived competitor event fixture did not retain start/stop evidence'
    }
    foreach ($event in @($competitor.process_events)) {
        if ($event.PSObject.Properties['command_line']) { throw 'Competitor command line was exposed' }
        if ($event.PSObject.Properties['pid']) { throw 'Synthetic competitor PID was exposed' }
    }

    $unknownPath = Join-Path $tempRoot 'unknown-pid.json'
    & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $witnessScript `
        -Mode OfflineSynthetic -FixtureMode -SyntheticUnknownPid -OutputPath $unknownPath
    if ($LASTEXITCODE -ne 2) { throw "Synthetic unknown-PID fixture exited $LASTEXITCODE instead of qualification-prohibited 2" }
    $unknown = Get-Content -LiteralPath $unknownPath -Raw | ConvertFrom-Json
    if (@($unknown.unknown_pids).Count -ne 1 -or
        @($unknown.process_events).Count -ne 1 -or
        $unknown.process_events[0].identity_known -ne $false) {
        throw 'Unknown-PID fixture did not preserve the unknown identity'
    }
    foreach ($sample in @($unknown.samples)) {
        foreach ($process in @($sample.processes)) {
            if ($process.command_line -ne '<redacted>') { throw 'Snapshot command line was exposed' }
        }
    }

    # Owner-attestation fixtures are private and synthetic. They let the real
    # Capture path reach each failure mode without claiming owner evidence.
    $now = [DateTime]::UtcNow
    $policyPath = Join-Path $tempRoot 'fixture-policy.json'
    $policy = [ordered]@{
        format = 'VOX_FX_T18_IDENTITY_POLICY_V1'
        policy_id = 'fixture-policy'
        baseline_id = 'fixture-baseline'
        host = [string]$env:COMPUTERNAME
        valid_from_utc = $now.AddMinutes(-10).ToString('o')
        valid_until_utc = $now.AddHours(2).ToString('o')
        owner_approved = $true
        runner_service_identity_sha256 = @('1' * 64, '2' * 64, '3' * 64, '4' * 64)
        approved_processes = @([ordered]@{ pid = 1000; process_name = 'Runner.Listener.exe'; role = 'runner'; allow_dynamic = $false; start_time_utc = $now.AddMinutes(-5).ToString('o'); executable_path = 'C:\HAMUNAPTRA\Runner.Listener.exe'; executable_sha256 = ('5' * 64); parent_pid = 4; service_identity_sha256 = ('1' * 64) })
        denied_process_name_regex = @('^(devenv|msbuild|cmake|ctest|ninja|python|cubase|reaper)')
    }
    $policy | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $policyPath -Encoding UTF8
    $policyHash = (Get-FileHash -LiteralPath $policyPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $validFrom = $now.AddMinutes(-10).ToString('o')
    $validUntil = $now.AddHours(2).ToString('o')

    $leasePath = Join-Path $tempRoot 'fixture-lease.json'
    [ordered]@{
        format = 'VOX_FX_T18_HOST_LEASE_RECEIPT_V1'; verified = $true
        owner_approved = $true; attestation_source = 'owner_controlled_offline'
        host = [string]$env:COMPUTERNAME; lease_id = 'fixture-lease'; owner = 'fixture-owner'
        policy_sha256 = $policyHash; valid_from_utc = $validFrom; valid_until_utc = $validUntil
        verification_method = 'owner_signed_detached_cms'
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $leasePath -Encoding UTF8

    $collectorPath = Join-Path $tempRoot 'fixture-etw-collector.etl'
    [IO.File]::WriteAllBytes($collectorPath, [Text.Encoding]::UTF8.GetBytes('synthetic ETW collector artifact'))
    $collectorHash = (Get-FileHash -LiteralPath $collectorPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $parsePath = Join-Path $tempRoot 'fixture-etw-parse.json'
    [ordered]@{
        format = 'VOX_FX_T18_ETW_PARSE_RECEIPT_V1'; provider = 'Microsoft-Windows-Kernel-Process/ETW'
        session_name = 'fixture-etw-session'; coverage_start_utc = $validFrom; coverage_end_utc = $validUntil
        events_received = 0; lost_events = 0; buffer_loss_events = 0
        collector_artifact_sha256 = $collectorHash; process_events = @()
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $parsePath -Encoding UTF8
    $parseHash = (Get-FileHash -LiteralPath $parsePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $lossPath = Join-Path $tempRoot 'fixture-event-loss.json'
    [ordered]@{
        format = 'VOX_FX_T18_EVENT_LOSS_RECEIPT_V1'
        provider = 'Microsoft-Windows-Kernel-Process/ETW'
        measurement_method = 'ETW_PROCESS_TRACE_WITH_LOSS_COUNTERS'
        coverage_complete = $true; lost_events = 0; buffer_loss_events = 0; events_received = 0
        session_name = 'fixture-etw-session'
        owner_approved = $true; attestation_source = 'owner_controlled_offline'
        verification_method = 'owner_signed_detached_cms'; trace_id = 'fixture-trace'
        collector_artifact_path = $collectorPath
        collector_artifact_sha256 = $collectorHash; collector_artifact_format = 'ETL'
        collector_parse_method = 'owner_independent_etw_parser_v1'
        collector_parse_receipt_path = $parsePath; collector_parse_receipt_sha256 = $parseHash
        process_events = @()
        coverage_start_utc = $validFrom; coverage_end_utc = $validUntil
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $lossPath -Encoding UTF8

    # Each missing attestation is exercised through the real Capture entrypoint.
    Invoke-CaptureFixture 'missing-policy' 'IDENTITY_POLICY_MISSING' -LeasePath $leasePath
    Invoke-CaptureFixture 'missing-lease' 'LEASE_RECEIPT_MISSING' -PolicyPath $policyPath
    Invoke-CaptureFixture 'missing-event-loss-before-finalization' 'FIXTURE_MODE' -PolicyPath $policyPath -LeasePath $leasePath
    Invoke-CaptureFixture 'provider-failure' 'CAPTURE_EXCEPTION' -PolicyPath $policyPath -LeasePath $leasePath -SimulateProviderFailure

    $summaryPath = Join-Path $tempRoot 'child-invocations.json'
    $script:invocations | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    Write-Output 'FX-T18 host witness PowerShell 5.1 offline fixtures: PASS'
}
catch {
    $failure = $_.Exception.Message
    foreach ($invocation in @($script:invocations)) {
        Write-Error ("child {0}: exit_code={1}; {2}" -f $invocation.name, $invocation.exit_code, $invocation.stderr_summary) -ErrorAction Continue
    }
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
if ($failure) { throw $failure }
