[CmdletBinding()]
param(
    [string]$OrchestratorPath = (Join-Path $PSScriptRoot 'run-fx-windows-t18.ps1'),
    [string]$InventoryPath = ''
)

# Windows PowerShell 5.1 smoke only. This parses the real orchestrator with the
# installed Windows parser and exercises the JSON/hash/file primitives it uses;
# it does not build the product or run the timing suite.
$ErrorActionPreference = 'Stop'
$path = (Resolve-Path -LiteralPath $OrchestratorPath).Path
$tokens = $null
$parseErrors = $null
[System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$parseErrors) | Out-Null
if ($parseErrors.Count -ne 0) {
    throw "PowerShell parser rejected ${path}: $($parseErrors -join '; ')"
}

$temp = Join-Path $env:TEMP "vox-fx-t18-powershell-$PID.json"
try {
    $probe = [ordered]@{
        format = 'VOX_FX_T18_POWERSHELL_SMOKE_V1'
        powershell = $PSVersionTable.PSVersion.ToString()
        orchestrator_sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        created_utc = [DateTime]::UtcNow.ToString('o')
    }
    $probe | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $temp -Encoding UTF8
    $roundTrip = Get-Content -LiteralPath $temp -Raw | ConvertFrom-Json
    if ($roundTrip.format -ne 'VOX_FX_T18_POWERSHELL_SMOKE_V1' -or
        [string]::IsNullOrEmpty($roundTrip.orchestrator_sha256)) {
        throw 'PowerShell JSON/hash round trip failed'
    }
    if ($InventoryPath) {
        $inventory = Get-Content -LiteralPath (Resolve-Path -LiteralPath $InventoryPath) -Raw | ConvertFrom-Json
        $tests = @($inventory.tests | Where-Object { $_.name -eq 'suite_fx_runtime_stress_tests' })
        if ($tests.Count -ne 1) { throw 'CTest inventory smoke did not contain exactly one FX stress test' }
    }
    Write-Output "FX-T18 Windows PowerShell smoke: PASS ($($PSVersionTable.PSVersion))"
}
finally {
    Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
}
