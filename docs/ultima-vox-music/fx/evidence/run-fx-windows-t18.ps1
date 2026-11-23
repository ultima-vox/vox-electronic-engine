[CmdletBinding()]
param(
    [string]$BuildDir = 'build-t18',
    [string]$OutputDir = 't18-out',
    [string]$Python = 'python',
    [string]$ExpectedHead = '',
    [string]$ExpectedTree = ''
)

# This script is an authorized-runner orchestrator, not an Arena execution
# request. It deliberately runs all three profiles and returns only after every
# profile has an exit code, raw CTest capture, report and audit attempt.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$build = (Resolve-Path (Join-Path $repo $BuildDir)).Path
$out = Join-Path $repo $OutputDir
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Get-GitValue([string[]]$Arguments) {
    $value = & git -C $repo @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE" }
    return ($value | Out-String).Trim()
}

$head = Get-GitValue @('rev-parse', 'HEAD')
$tree = Get-GitValue @('rev-parse', 'HEAD^{tree}')
if ($ExpectedHead -and $head -cne $ExpectedHead) { throw "Expected HEAD $ExpectedHead, found $head" }
if ($ExpectedTree -and $tree -cne $ExpectedTree) { throw "Expected tree $ExpectedTree, found $tree" }

$exeCandidates = @(Get-ChildItem -LiteralPath $build -Filter 'suite_fx_runtime_stress_tests.exe' -File -Recurse)
if ($exeCandidates.Count -ne 1) {
    throw "Expected exactly one Release suite_fx_runtime_stress_tests.exe below $build; found $($exeCandidates.Count)"
}
$exe = $exeCandidates[0].FullName
$exeHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
$auditScript = Join-Path $repo 'docs\ultima-vox-music\fx\evidence\audit-fx-windows-timing.py'

function Clear-FxEnvironment {
    foreach ($name in 'VOX_FX_STRESS_AUTHORED', 'VOX_FX_STRESS_TYPICAL', 'VOX_FX_REPORT') {
        Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
    }
    $env:VOX_FX_TIMING_THREAD_CPU = '1'
    $env:VOX_FX_SOURCE_SHA = $head
    $env:VOX_FX_SOURCE_TREE = $tree
    $env:VOX_FX_EXECUTABLE = $exe
    $env:VOX_FX_EXE_SHA256 = $exeHash
}

function Write-JsonFile([object]$Value, [string]$Path) {
    $Value | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-ArtifactEntry([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        path = $item.FullName
        sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Test-ArtifactEntries([object[]]$Entries) {
    foreach ($entry in $Entries) {
        if (-not (Test-Path -LiteralPath $entry.path -PathType Leaf)) {
            throw "Missing artifact after scenario: $($entry.path)"
        }
        $actual = (Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -cne $entry.sha256) { throw "Artifact changed after hashing: $($entry.path)" }
    }
}

function Invoke-FxScenario([string]$Scenario) {
    $report = Join-Path $out "$Scenario.tsv"
    $ctestRaw = Join-Path $out "$Scenario.ctest.txt"
    $inventory = Join-Path $out "$Scenario.inventory.json"
    $auditRaw = Join-Path $out "$Scenario.audit.txt"
    $auditJson = Join-Path $out "$Scenario.audit.json"
    $receiptPath = Join-Path $out "$Scenario.runner.json"
    $manifestPath = Join-Path $out "$Scenario.artifacts.json"
    foreach ($path in @($report, $ctestRaw, $inventory, $auditRaw, $auditJson, $receiptPath, $manifestPath)) {
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }

    Clear-FxEnvironment
    if ($Scenario -ceq 'authored') { $env:VOX_FX_STRESS_AUTHORED = '1' }
    if ($Scenario -ceq 'typical') { $env:VOX_FX_STRESS_TYPICAL = '1' }
    $report = [IO.Path]::GetFullPath($report)
    $env:VOX_FX_REPORT = $report

    $os = Get-CimInstance Win32_OperatingSystem
    $computer = Get-CimInstance Win32_ComputerSystem
    $processor = Get-CimInstance Win32_Processor | Select-Object -First 1
    $runner = $env:RUNNER_NAME
    if (-not $runner) { $runner = $env:COMPUTERNAME }
    $receipt = [ordered]@{
        format = 'VOX_FX_T18_RUNNER_RECEIPT_V1'
        scenario = $Scenario
        source_head = $head
        source_tree = $tree
        executable_path = $exe
        executable_sha256 = $exeHash
        report_path = $report
        runner = $runner
        machine = $env:COMPUTERNAME
        os_caption = $os.Caption
        os_version = $os.Version
        powershell = $PSVersionTable.PSVersion.ToString()
        process_architecture = $env:PROCESSOR_ARCHITECTURE
        cpu_name = $processor.Name
        logical_processors = $computer.NumberOfLogicalProcessors
        ram_bytes = $computer.TotalPhysicalMemory
        created_utc = [DateTime]::UtcNow.ToString('o')
    }
    Write-JsonFile $receipt $receiptPath
    $receiptPath = [IO.Path]::GetFullPath($receiptPath)
    $env:VOX_FX_RUNNER_RECEIPT_SHA256 = (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash.ToLowerInvariant()

    $inventoryExit = 125
    $ctestExit = 125
    $auditExit = 125
    $exceptionText = $null
    try {
        & ctest --test-dir $build -C Release -R '^suite_fx_runtime_stress_tests$' '--show-only=json-v1' *> $inventory
        $inventoryExit = $LASTEXITCODE
    } catch {
        $_ | Out-String | Set-Content -LiteralPath $inventory -Encoding UTF8
    }
    try {
        & ctest --test-dir $build -C Release -R '^suite_fx_runtime_stress_tests$' '--no-tests=error' '--output-on-failure' '-VV' *> $ctestRaw
        $ctestExit = $LASTEXITCODE
    } catch {
        $exceptionText = $_.Exception.ToString()
        $_ | Out-String | Set-Content -LiteralPath $ctestRaw -Encoding UTF8
    }
    if (-not (Test-Path -LiteralPath $ctestRaw -PathType Leaf)) {
        "CTest did not produce a capture." | Set-Content -LiteralPath $ctestRaw -Encoding UTF8
    }
    if (-not (Test-Path -LiteralPath $inventory -PathType Leaf)) {
        "CTest did not produce an inventory." | Set-Content -LiteralPath $inventory -Encoding UTF8
    }

    $entries = @(
        Get-ArtifactEntry $report
        Get-ArtifactEntry $ctestRaw
        Get-ArtifactEntry $inventory
        Get-ArtifactEntry $receiptPath
        Get-ArtifactEntry $exe
    ) | Where-Object { $null -ne $_ }
    Write-JsonFile ([ordered]@{
        format = 'VOX_FX_T18_ARTIFACTS_V1'
        scenario = $Scenario
        files = @($entries)
    }) $manifestPath

    $auditExit = 125
    try {
        & $Python $auditScript $report '--root' $repo '--executable' $exe '--runner-receipt' $receiptPath '--artifact-manifest' $manifestPath '--expected-head' $head '--expected-tree' $tree *> $auditRaw
        $auditExit = $LASTEXITCODE
    } catch {
        $_ | Out-String | Set-Content -LiteralPath $auditRaw -Encoding UTF8
    }
    if ($auditExit -eq 0) {
        Copy-Item -LiteralPath $auditRaw -Destination $auditJson -Force
    }

    $auditObject = $null
    if ($auditExit -eq 0) {
        try { $auditObject = Get-Content -LiteralPath $auditJson -Raw | ConvertFrom-Json }
        catch { $auditExit = 125; $exceptionText = "Successful auditor output was not JSON: $($_.Exception)" }
    }

    if ($auditExit -ne 0) {
        $classification = if (Test-Path -LiteralPath $report -PathType Leaf) { 'infrastructure_failure' } else { 'infrastructure_or_test_failure' }
    } elseif ($inventoryExit -ne 0) {
        $classification = 'infrastructure_failure'
    } elseif ([bool]$auditObject.performance_failure) {
        $classification = 'performance_failure'
    } elseif ($ctestExit -ne 0) {
        $classification = 'test_failure'
    } else {
        $classification = 'diagnostic_pass'
    }

    # Add the derived audit text to the final artifact manifest and re-check
    # every recorded digest. The auditor itself verified the raw inputs above.
    $finalEntries = @(
        Get-ArtifactEntry $report
        Get-ArtifactEntry $ctestRaw
        Get-ArtifactEntry $inventory
        Get-ArtifactEntry $receiptPath
        Get-ArtifactEntry $exe
        Get-ArtifactEntry $auditRaw
        Get-ArtifactEntry $auditJson
    ) | Where-Object { $null -ne $_ }
    Write-JsonFile ([ordered]@{
        format = 'VOX_FX_T18_ARTIFACTS_V1'
        scenario = $Scenario
        files = @($finalEntries)
    }) $manifestPath
    Test-ArtifactEntries $finalEntries

    return [ordered]@{
        scenario = $Scenario
        inventory_exit_code = $inventoryExit
        ctest_exit_code = $ctestExit
        audit_exit_code = $auditExit
        classification = $classification
        report = $report
        inventory = $inventory
        ctest_raw = $ctestRaw
        audit_raw = $auditRaw
        audit_json = if (Test-Path -LiteralPath $auditJson -PathType Leaf) { $auditJson } else { $null }
        runner_receipt = $receiptPath
        artifact_manifest = $manifestPath
        exception = $exceptionText
    }
}

$results = @()
foreach ($scenario in 'routed', 'authored', 'typical') {
    try {
        $results += Invoke-FxScenario $scenario
    } catch {
        $results += [ordered]@{
            scenario = $scenario
            ctest_exit_code = 125
            audit_exit_code = 125
            classification = 'infrastructure_failure'
            error = $_.Exception.ToString()
        }
    }
}

$summary = [ordered]@{
    format = 'VOX_FX_T18_ORCHESTRATION_V1'
    source_head = $head
    source_tree = $tree
    executable = $exe
    executable_sha256 = $exeHash
    scenarios = @($results)
    overall_failure = @($results | Where-Object { $_.classification -ne 'diagnostic_pass' }).Count -gt 0
    created_utc = [DateTime]::UtcNow.ToString('o')
}
Write-JsonFile $summary (Join-Path $out 'summary.json')
$summary | ConvertTo-Json -Depth 10
if ($summary.overall_failure) { exit 1 }
exit 0
