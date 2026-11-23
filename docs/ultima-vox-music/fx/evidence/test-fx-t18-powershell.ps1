[CmdletBinding()]
param(
    [string]$OrchestratorPath = (Join-Path $PSScriptRoot 'run-fx-windows-t18.ps1'),
    [string]$Python = 'python',
    [string]$CtestBuildDir = '',
    [string]$InventoryPath = ''
)

# Windows PowerShell 5.1 smoke only. This parses the real orchestrator and
# timeout helper, exercises UTF-8 Out-File -> Python inventory auditing, and
# does not build the product or run the timing suite itself. If CtestBuildDir
# is supplied, --show-only runs CTest inventory without executing the test.
$ErrorActionPreference = 'Stop'
$path = (Resolve-Path -LiteralPath $OrchestratorPath).Path
$helperPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'FxT18CtestHelpers.ps1')).Path
$auditScript = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'audit-fx-windows-timing.py')).Path

function Assert-PowerShellParses([string]$File) {
    $tokens = $null
    $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($File, [ref]$tokens, [ref]$parseErrors) | Out-Null
    if ($parseErrors.Count -ne 0) {
        throw "PowerShell parser rejected ${File}: $($parseErrors -join '; ')"
    }
}

Assert-PowerShellParses $path
Assert-PowerShellParses $helperPath
. $helperPath

$tempRoot = Join-Path $env:TEMP "vox-fx-t18-powershell-$PID"
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
try {
    $probePath = Join-Path $tempRoot 'probe.json'
    $probe = [ordered]@{
        format = 'VOX_FX_T18_POWERSHELL_SMOKE_V2'
        powershell = $PSVersionTable.PSVersion.ToString()
        orchestrator_sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        created_utc = [DateTime]::UtcNow.ToString('o')
    }
    $probe | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $probePath -Encoding UTF8
    $roundTrip = Get-Content -LiteralPath $probePath -Raw | ConvertFrom-Json
    if ($roundTrip.format -ne 'VOX_FX_T18_POWERSHELL_SMOKE_V2' -or
        [string]::IsNullOrEmpty($roundTrip.orchestrator_sha256)) {
        throw 'PowerShell JSON/hash round trip failed'
    }

    # Positive fixture: verbose CTest may report the configured timeout while
    # the test passes. It must not be classified as a real timeout.
    $passLog = Join-Path $tempRoot 'passing.ctest.txt'
    $passJunit = Join-Path $tempRoot 'passing.junit.xml'
    $passExitCode = 0
    "Test timeout computed to be: 180`r`n100% tests passed, 0 tests failed out of 1" |
        Set-Content -LiteralPath $passLog -Encoding UTF8
    '<testsuite tests="1" failures="0"><testcase name="pass" status="run" /></testsuite>' |
        Set-Content -LiteralPath $passJunit -Encoding UTF8
    $passTimedOut = Test-FxT18CtestTimeout -RawLog $passLog -JunitPath $passJunit
    if ($passTimedOut -or $passExitCode -ne 0 -or
        (Get-FxT18CtestOutcome -ExitCode $passExitCode -TimedOut $passTimedOut) -ne 'diagnostic_pass') {
        throw 'Passing CTest timeout-configuration fixture was classified as a timeout or nonzero result'
    }
    if ((Get-FxT18CtestOutcome -ExitCode 8 -TimedOut $false) -ne 'test_failure') {
        throw 'Nonzero non-timeout CTest exit code was not preserved as a test failure'
    }

    # A failed assertion whose free-form message contains the word timeout is
    # not a structured CTest timeout result.
    $decoyJunit = Join-Path $tempRoot 'decoy.junit.xml'
    '<testsuite tests="1" failures="1"><testcase name="failure" status="fail"><failure message="assertion mentions timeout but did not time out" /></testcase></testsuite>' |
        Set-Content -LiteralPath $decoyJunit -Encoding UTF8
    if (Test-FxT18CtestTimeout -RawLog $passLog -JunitPath $decoyJunit) {
        throw 'Free-form JUnit failure text was classified as a timeout'
    }

    # CTest uses exit code 8 (TEST_ERRORS) for a timed-out test. Require the
    # explicit marker/structured result and preserve the nonzero exit parity.
    $timeoutLog = Join-Path $tempRoot 'timeout.ctest.txt'
    $timeoutJunit = Join-Path $tempRoot 'timeout.junit.xml'
    $timeoutExitCode = 8
    "Test timeout computed to be: 180`r`n1/1 Test #1: timeout ......................***Timeout 180.00 sec" |
        Set-Content -LiteralPath $timeoutLog -Encoding UTF8
    '<testsuite tests="1" failures="1"><testcase name="timeout" status="fail"><failure message="Timeout" /></testcase></testsuite>' |
        Set-Content -LiteralPath $timeoutJunit -Encoding UTF8
    $timeoutDetected = Test-FxT18CtestTimeout -RawLog $timeoutLog -JunitPath $timeoutJunit
    if (-not $timeoutDetected -or $timeoutExitCode -ne 8 -or
        (Get-FxT18CtestOutcome -ExitCode $timeoutExitCode -TimedOut $timeoutDetected) -ne 'timeout_failure') {
        throw 'Genuine CTest timeout fixture did not preserve timeout/exit-code parity'
    }
    $structuredOnlyLog = Join-Path $tempRoot 'structured-timeout.ctest.txt'
    'Test timeout computed to be: 180' | Set-Content -LiteralPath $structuredOnlyLog -Encoding UTF8
    if (-not (Test-FxT18CtestTimeout -RawLog $structuredOnlyLog -JunitPath $timeoutJunit)) {
        throw 'Structured JUnit timeout result was not recognized without a raw marker'
    }

    if ($CtestBuildDir) {
        if (-not $InventoryPath) { $InventoryPath = Join-Path $tempRoot 'ctest.inventory.json' }
        & ctest --test-dir $CtestBuildDir -C Release -R '^suite_fx_runtime_stress_tests$' '--show-only=json-v1' 2>&1 | Out-File -LiteralPath $InventoryPath -Encoding UTF8
        $ctestExit = $LASTEXITCODE
        if ($ctestExit -ne 0) { throw "CTest inventory smoke failed with exit code $ctestExit" }
    }
    if ($InventoryPath) {
        $auditOutput = Join-Path $tempRoot 'inventory.audit.txt'
        & $Python $auditScript '--check-ctest-inventory' $InventoryPath 2>&1 | Out-File -LiteralPath $auditOutput -Encoding UTF8 -Width 4096
        $auditExit = $LASTEXITCODE
        if ($auditExit -ne 0) {
            throw "Python CTest inventory auditor failed with exit code ${auditExit}: $(Get-Content $auditOutput -Raw)"
        }
    } elseif ($CtestBuildDir) {
        throw 'CTest inventory smoke did not produce an inventory path'
    }
    Write-Output "FX-T18 Windows PowerShell 5.1 smoke: PASS ($($PSVersionTable.PSVersion))"
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
