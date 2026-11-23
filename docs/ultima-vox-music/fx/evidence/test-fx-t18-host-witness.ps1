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
    $source -notmatch 'Get-VerifiedLeaseReceipt' -or
    $source -notmatch 'Get-VerifiedEventLossReceipt' -or
    $source -notmatch 'Get-EventTimeUtc') {
    throw 'Host witness lease/event verification is not file- and provider-attested'
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
        [string]$EventLossPath = '',
        [switch]$SimulateProviderFailure
    )
    $outputPath = Join-Path $tempRoot ($Name + '.json')
    $stdoutPath = Join-Path $tempRoot ($Name + '.stdout.txt')
    $stderrPath = Join-Path $tempRoot ($Name + '.stderr.txt')
    $arguments = @(
        '-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
        '-File', $witnessScript, '-Mode', 'Capture', '-OutputPath', $outputPath,
        '-PreSeconds', '30', '-DuringSeconds', '1', '-PostSeconds', '30'
    )
    if ($PolicyPath) { $arguments += @('-IdentityPolicyPath', $PolicyPath) }
    if ($LeasePath) { $arguments += @('-LeaseReceiptPath', $LeasePath) }
    if ($EventLossPath) { $arguments += @('-EventLossReceiptPath', $EventLossPath) }
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
        -Mode OfflineSynthetic -SyntheticCompetitor -OutputPath $competitorPath
    if ($LASTEXITCODE -ne 0) { throw "Synthetic competitor fixture exited $LASTEXITCODE" }
    $competitor = Get-Content -LiteralPath $competitorPath -Raw | ConvertFrom-Json
    if ($competitor.format -ne 'VOX_FX_T18_HOST_WITNESS_V1' -or
        @($competitor.unrecognized_competitors).Count -ne 1 -or
        @($competitor.process_events).Count -ne 2) {
        throw 'Short-lived competitor event fixture did not retain start/stop evidence'
    }
    foreach ($event in @($competitor.process_events)) {
        if ($event.command_line -ne '<redacted>') { throw 'Competitor command line was exposed' }
    }

    $unknownPath = Join-Path $tempRoot 'unknown-pid.json'
    & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $witnessScript `
        -Mode OfflineSynthetic -SyntheticUnknownPid -OutputPath $unknownPath
    if ($LASTEXITCODE -ne 0) { throw "Synthetic unknown-PID fixture exited $LASTEXITCODE" }
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
    $policyPath = Join-Path $tempRoot 'fixture-policy.json'
    $policy = [ordered]@{
        format = 'VOX_FX_T18_IDENTITY_POLICY_V1'
        policy_id = 'fixture-policy'
        owner_approved = $true
        approved_processes = @([ordered]@{ pid = 1000; process_name = 'Runner.Listener.exe'; role = 'runner' })
        denied_process_name_regex = @('^(devenv|msbuild|cmake|ctest|ninja|python|cubase|reaper)')
    }
    $policy | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $policyPath -Encoding UTF8
    $policyHash = (Get-FileHash -LiteralPath $policyPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $now = [DateTime]::UtcNow
    $validFrom = $now.AddMinutes(-10).ToString('o')
    $validUntil = $now.AddHours(2).ToString('o')

    $leasePath = Join-Path $tempRoot 'fixture-lease.json'
    [ordered]@{
        format = 'VOX_FX_T18_HOST_LEASE_RECEIPT_V1'; verified = $true
        host = [string]$env:COMPUTERNAME; lease_id = 'fixture-lease'; owner = 'fixture-owner'
        policy_sha256 = $policyHash; valid_from_utc = $validFrom; valid_until_utc = $validUntil
        verification_method = 'owner_attested_file'
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $leasePath -Encoding UTF8

    $lossPath = Join-Path $tempRoot 'fixture-event-loss.json'
    [ordered]@{
        format = 'VOX_FX_T18_EVENT_LOSS_RECEIPT_V1'
        provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'
        coverage_complete = $true; lost_events = 0; loss_accounting = 'owner_attested'
        coverage_start_utc = $validFrom; coverage_end_utc = $validUntil
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $lossPath -Encoding UTF8

    # Each missing attestation is exercised through the real Capture entrypoint.
    Invoke-CaptureFixture 'missing-policy' 'IDENTITY_POLICY_MISSING' -LeasePath $leasePath -EventLossPath $lossPath
    Invoke-CaptureFixture 'missing-lease' 'LEASE_RECEIPT_MISSING' -PolicyPath $policyPath -EventLossPath $lossPath
    Invoke-CaptureFixture 'missing-event-loss' 'EVENT_LOSS_RECEIPT_MISSING' -PolicyPath $policyPath -LeasePath $leasePath
    Invoke-CaptureFixture 'provider-failure' 'CAPTURE_EXCEPTION' -PolicyPath $policyPath -LeasePath $leasePath -EventLossPath $lossPath -SimulateProviderFailure

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
