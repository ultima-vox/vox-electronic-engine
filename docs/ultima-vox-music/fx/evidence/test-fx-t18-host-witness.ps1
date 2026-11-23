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
    ('Stop' + '-Service'), ('Disable' + '-Service'), ('Set' + '-Service'),
    ('Stop' + '-Process'), ('task' + 'kill.exe')
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

$tempRoot = Join-Path $env:TEMP "vox-fx-t18-host-witness-$PID"
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
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
    Write-Output "FX-T18 host witness PowerShell 5.1 offline fixtures: PASS"
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
