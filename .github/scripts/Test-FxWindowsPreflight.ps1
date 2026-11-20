# Parse only, apart from the extracted validator inventory check exercised on
# disposable dummy paths. Never invokes a workflow command, CTest, or CDB.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PreparedDir)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -ne 'Desktop' -or $PSVersionTable.PSVersion -lt [version]'5.1') {
    throw 'Run this preflight in Windows PowerShell 5.1 on the intended runner.'
}
$root = (Resolve-Path -LiteralPath $PreparedDir).Path
$source = (Get-Content -LiteralPath (Join-Path $root 'source.txt') -Raw).Trim()
if ($source -cne (& git rev-parse HEAD)) { throw 'Extracted blocks do not match the checked-out SHA.' }
$files = @(Get-ChildItem -LiteralPath $root -Filter '*.ps1' -File)
if ($files.Count -lt 4) { throw 'Incomplete PowerShell preflight input.' }
foreach ($file in $files) {
    $tokens = $null
    $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$errors)
    if ($errors.Count -ne 0) { throw "PowerShell parser rejected $($file.Name): $($errors -join '; ')" }
}
$check = Join-Path $root 'inventory-only.ps1'
$fixture = Join-Path $root ('inventory-' + [Guid]::NewGuid().ToString('N'))
try {
    $names = @(Get-Content -LiteralPath (Join-Path $root 'bundle-names.txt'))
    if ($names.Count -ne 3) { throw 'Expected three target names.' }
    $bundles = @(foreach ($name in $names) {
        $bundle = New-Item -ItemType Directory -Path (Join-Path $fixture "$name.vst3\Contents\x86_64-win") -Force
        New-Item -ItemType File -Path (Join-Path $bundle.FullName "$name.vst3") | Out-Null
        $bundle.Parent.Parent
    })
    & $check -bundles $bundles
    foreach ($absent in $names) {
        $subset = @($bundles | Where-Object { [IO.Path]::GetFileNameWithoutExtension($_.Name) -cne $absent })
        $failedForMissing = $false
        try { & $check -bundles $subset } catch { $failedForMissing = $_.Exception.Message.Contains("missing: $absent;") }
        if (-not $failedForMissing) { throw "Inventory accepted missing $absent or did not name it." }
    }
    $first = $bundles[0]
    Remove-Item -LiteralPath (Join-Path $first.FullName (Join-Path 'Contents\x86_64-win' $first.Name))
    $failedForBinary = $false
    try { & $check -bundles $bundles } catch { $failedForBinary = $_.Exception.Message.StartsWith('Missing x64 VST3 binary:') }
    if (-not $failedForBinary) { throw 'Inventory accepted a bundle without its x64 binary.' }
    Write-Output "PASS PowerShell $($PSVersionTable.PSVersion), SHA $source, $($files.Count) scripts; three bundles and negative inventory cases. No Windows suite executed."
} finally {
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
