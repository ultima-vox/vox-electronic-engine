[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedHead,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedTree,
    [Parameter(Mandatory = $true)]
    [string]$JuceDir,
    [string]$BuildDir = 'build-t18',
    [string]$OutputDir = 't18-out',
    [string]$Python = 'python',
    [int]$TimeoutSeconds = 180
)

# This script is an authorized-runner orchestrator, not an Arena execution
# request. It verifies a clean exact-head checkout, configures and freshly
# builds the Release target, resolves the executable from CTest's own JSON
# inventory, then runs all three profiles independently. It returns only after
# every profile has an exit code, raw capture, report and audit attempt.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$build = Join-Path $repo $BuildDir
$out = Join-Path $repo $OutputDir
$outputRelative = $OutputDir.Replace('\', '/').TrimStart('./').TrimEnd('/') + '/'
$testName = 'suite_fx_runtime_stress_tests'
. (Join-Path $PSScriptRoot 'FxT18CtestHelpers.ps1')

function Get-GitValue([string]$Directory, [string[]]$Arguments) {
    $value = & git -C $Directory @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git -C $Directory $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
    return ($value | Out-String).Trim()
}

function Get-SourceStatus {
    return @(& git -C $repo status --porcelain=v1 --untracked-files=all)
}

function Assert-CleanCheckout {
    $status = Get-SourceStatus
    if ($status.Count -ne 0) {
        throw "Checkout is not clean before configure/build: $($status -join '; ')"
    }
}

function Assert-CleanTrackedSource {
    param([switch]$AllowOutput)
    $status = Get-SourceStatus
    $outputPrefix = $outputRelative
    foreach ($line in $status) {
        if ($line.Length -lt 3) { continue }
        $code = $line.Substring(0, 2)
        $path = $line.Substring(3).Replace('\', '/')
        if ($code -eq '??' -and $AllowOutput -and $path.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        throw "Unexpected source-tree change after configure/build: $line"
    }
}

function Write-JsonFile([object]$Value, [string]$Path) {
    $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Write-CommandFailure([string]$Path, [string]$Message) {
    $Message | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-ArtifactEntry([string]$Path) {
    if ([string]::IsNullOrEmpty($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
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

function Clear-FxEnvironment {
    foreach ($name in 'VOX_FX_STRESS_AUTHORED', 'VOX_FX_STRESS_TYPICAL', 'VOX_FX_REPORT',
                         'VOX_FX_EXECUTABLE', 'VOX_FX_EXE_SHA256', 'VOX_FX_RUNNER_RECEIPT_SHA256') {
        Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
    }
    $env:VOX_FX_TIMING_THREAD_CPU = '1'
    $env:VOX_FX_SOURCE_SHA = $sourceHead
    $env:VOX_FX_SOURCE_TREE = $sourceTree
}

function Get-CtestSelectedExecutable([string]$InventoryPath) {
    $inventory = Get-Content -LiteralPath $InventoryPath -Raw | ConvertFrom-Json
    $tests = @($inventory.tests | Where-Object { $_.name -ceq $testName })
    if ($tests.Count -ne 1) { throw "CTest inventory does not contain exactly one $testName test" }
    $commands = @($tests[0].command | ForEach-Object { [string]$_ })
    $candidate = $commands | Where-Object { $_ -match '(?i)\.exe$' } | Select-Object -First 1
    if (-not $candidate) { throw "CTest inventory has no executable command for $testName" }
    $candidate = $candidate.Trim().Trim('"')
    if ($candidate -notmatch '(?i)[\\/]Release[\\/]') {
        throw "CTest selected command is not the Release executable: $candidate"
    }
    if (-not [IO.Path]::IsPathRooted($candidate)) { $candidate = Join-Path $build $candidate }
    $resolved = Resolve-Path -LiteralPath $candidate
    if (-not (Test-Path -LiteralPath $resolved.Path -PathType Leaf)) {
        throw "CTest selected executable does not exist: $($resolved.Path)"
    }
    return $resolved.Path
}

function Get-ReportMeta([string]$Report, [string]$Key) {
    if (-not (Test-Path -LiteralPath $Report -PathType Leaf)) { return $null }
    $line = Get-Content -LiteralPath $Report | Where-Object {
        $_ -match ('^meta\t' + [Regex]::Escape($Key) + '\t')
    } | Select-Object -First 1
    if (-not $line) { return $null }
    return ($line -split "`t", 3)[2]
}

function Test-CpuOnlyFailure([string]$Report, [string]$CpuStatus) {
    if ($CpuStatus -ne 'CPU_MEASUREMENT_INSUFFICIENT' -or
        -not (Test-Path -LiteralPath $Report -PathType Leaf)) { return $false }
    $cases = @(Get-Content -LiteralPath $Report | Where-Object { $_ -like 'case`t*' })
    $functional = @(Get-Content -LiteralPath $Report | Where-Object { $_ -like 'functional`t*' })
    if ($cases.Count -eq 0 -or $functional.Count -ne $cases.Count) { return $false }
    foreach ($line in $cases) {
        $fields = $line -split "`t"
        if ($fields.Count -lt 18 -or [int]$fields[9] -ne 0 -or [int]$fields[10] -ne 0 -or
            [int]$fields[11] -ne 0 -or [int]$fields[15] -ne 0) { return $false }
    }
    foreach ($line in $functional) {
        $fields = $line -split "`t"
        if ($fields.Count -lt 9 -or [int]$fields[8] -ne 1) { return $false }
    }
    return $true
}

function Get-JunitSummary([string]$JunitPath) {
    $summary = [ordered]@{ tests = 0; failures = 0; errors = 0; skipped = 0; timeout = $false }
    if (-not (Test-Path -LiteralPath $JunitPath -PathType Leaf)) { return $summary }
    try {
        [xml]$junit = Get-Content -LiteralPath $JunitPath -Raw
        $testcases = @($junit.SelectNodes('//testcase'))
        $summary.tests = $testcases.Count
        foreach ($testcase in $testcases) {
            $summary.failures += @($testcase.SelectNodes('./failure')).Count
            $summary.errors += @($testcase.SelectNodes('./error')).Count
            $summary.skipped += @($testcase.SelectNodes('./skipped')).Count
            $status = [string]$testcase.GetAttribute('status')
            $result = [string]$testcase.GetAttribute('result')
            if ($status -match '(?i)^(timeout|timed[ -]?out)$' -or
                $result -match '(?i)^(timeout|timed[ -]?out)$') { $summary.timeout = $true }
        }
    } catch { }
    return $summary
}

function Invoke-FxScenario([string]$Scenario) {
    $report = Join-Path $out "$Scenario.tsv"
    $ctestRaw = Join-Path $out "$Scenario.ctest.txt"
    $inventoryPath = Join-Path $out "$Scenario.inventory.json"
    $junitPath = Join-Path $out "$Scenario.junit.xml"
    $auditRaw = Join-Path $out "$Scenario.audit.txt"
    $auditJson = Join-Path $out "$Scenario.audit.json"
    $receiptPath = Join-Path $out "$Scenario.runner.json"
    $outcomePath = Join-Path $out "$Scenario.outcome.json"
    $manifestPath = Join-Path $out "$Scenario.artifacts.json"
    foreach ($path in @($report, $ctestRaw, $inventoryPath, $junitPath, $auditRaw, $auditJson, $receiptPath, $outcomePath, $manifestPath)) {
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }

    Clear-FxEnvironment
    if ($Scenario -ceq 'authored') { $env:VOX_FX_STRESS_AUTHORED = '1' }
    if ($Scenario -ceq 'typical') { $env:VOX_FX_STRESS_TYPICAL = '1' }
    $report = [IO.Path]::GetFullPath($report)
    $env:VOX_FX_REPORT = $report

    $inventoryExit = 125
    $ctestExit = 125
    $auditExit = 125
    $ctestTimedOut = $false
    $exceptionText = $null
    $selectedExe = $null
    $selectedExeHash = $null

    if ($buildSucceeded) {
        try {
            & ctest --test-dir $build -C Release -R "^$testName$" '--show-only=json-v1' 2>&1 | Out-File -LiteralPath $inventoryPath -Encoding UTF8 -Width 4096
            $inventoryExit = $LASTEXITCODE
        } catch {
            $exceptionText = $_.Exception.ToString()
            Write-CommandFailure $inventoryPath $exceptionText
        }
        if ($inventoryExit -eq 0) {
            try {
                $selectedExe = Get-CtestSelectedExecutable $inventoryPath
                $selectedExeHash = (Get-FileHash -LiteralPath $selectedExe -Algorithm SHA256).Hash.ToLowerInvariant()
            } catch {
                $inventoryExit = 125
                $exceptionText = $_.Exception.ToString()
            }
        }
    } else {
        Write-CommandFailure $inventoryPath "CTest inventory not run because configure/build failed."
    }
    if (-not (Test-Path -LiteralPath $inventoryPath -PathType Leaf)) {
        Write-CommandFailure $inventoryPath 'CTest did not produce an inventory.'
    }

    $selectedExeValue = 'not-available'
    $selectedExeHashValue = 'not-available'
    if ($selectedExe) { $selectedExeValue = $selectedExe }
    if ($selectedExeHash) { $selectedExeHashValue = $selectedExeHash }
    $testStartUtc = [DateTime]::UtcNow.ToString('o')
    $receipt = [ordered]@{
        format = 'VOX_FX_T18_RUNNER_RECEIPT_V1'
        scenario = $Scenario
        test_identity = $testName
        test_start_utc = $testStartUtc
        test_end_utc = 'post-test-outcome'
        machine = $env:COMPUTERNAME
        host_lease_id = if ($env:FX_T18_LEASE_ID) { $env:FX_T18_LEASE_ID } else { '<missing>' }
        host_policy_sha256 = if ($env:FX_T18_POLICY_SHA256) { $env:FX_T18_POLICY_SHA256 } else { '<missing>' }
        post_test_outcome_path = [IO.Path]::GetFullPath($outcomePath)
        source_head = $sourceHead
        source_tree = $sourceTree
        source_tracked_clean_before_build = $true
        source_tracked_clean_after_build = $true
        build_succeeded = $buildSucceeded
        build_dir = (Resolve-Path -LiteralPath $build -ErrorAction SilentlyContinue).Path
        build_config = 'Release'
        configure_exit_code = $configureExit
        build_exit_code = $buildExit
        build_fresh_clean_first = $true
        configure_log_path = [IO.Path]::GetFullPath($configureLog)
        build_log_path = [IO.Path]::GetFullPath($buildLog)
        ctest_timeout_seconds = $TimeoutSeconds
        ctest_raw_path = [IO.Path]::GetFullPath($ctestRaw)
        ctest_inventory_path = [IO.Path]::GetFullPath($inventoryPath)
        ctest_junit_path = [IO.Path]::GetFullPath($junitPath)
        ctest_selected_executable = $selectedExeValue
        executable_path = $selectedExeValue
        executable_sha256 = $selectedExeHashValue
        report_path = $report
        juce_dir = $JuceDir
        juce_commit = $juceCommit
        juce_tracked_clean = $true
        runner = $runnerName
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
    $receiptHash = (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $env:VOX_FX_RUNNER_RECEIPT_SHA256 = $receiptHash
    $env:VOX_FX_EXECUTABLE = $selectedExeValue
    $env:VOX_FX_EXE_SHA256 = $selectedExeHashValue

    if ($buildSucceeded) {
        try {
            & ctest --test-dir $build -C Release -R "^$testName$" '--no-tests=error' '--timeout' $TimeoutSeconds '--output-on-failure' '--output-junit' $junitPath '-VV' 2>&1 | Out-File -LiteralPath $ctestRaw -Encoding UTF8 -Width 4096
            $ctestExit = $LASTEXITCODE
        } catch {
            $exceptionText = $_.Exception.ToString()
            Write-CommandFailure $ctestRaw $exceptionText
        }
    } else {
        Write-CommandFailure $ctestRaw 'CTest not run because configure/build failed.'
    }
    if (-not (Test-Path -LiteralPath $ctestRaw -PathType Leaf)) {
        Write-CommandFailure $ctestRaw 'CTest did not produce a capture.'
    }
    $junitAvailable = Test-Path -LiteralPath $junitPath -PathType Leaf
    if (-not $junitAvailable) {
        Write-CommandFailure $junitPath 'CTest did not produce JUnit output.'
    }
    $testEndUtc = [DateTime]::UtcNow.ToString('o')
    $ctestTimedOut = Test-FxT18CtestTimeout -RawLog $ctestRaw -JunitPath $junitPath
    $ctestOutcome = Get-FxT18CtestOutcome -ExitCode $ctestExit -TimedOut $ctestTimedOut
    $junitSummary = Get-JunitSummary $junitPath
    $cpuStatus = Get-ReportMeta $report 'cpu_measurement_status'
    $cpuOnlyFailure = ($ctestExit -ne 0) -and (Test-CpuOnlyFailure $report $cpuStatus)
    $cpuOnlyReason = if ($cpuOnlyFailure) { 'CPU_MEASUREMENT_INSUFFICIENT' } else { 'NONE' }
    Write-JsonFile ([ordered]@{
        format = 'VOX_FX_T18_POST_TEST_OUTCOME_V1'
        scenario = $Scenario
        test_identity = $testName
        executable_path = $selectedExeValue
        executable_sha256 = $selectedExeHashValue
        test_start_utc = $testStartUtc
        test_end_utc = $testEndUtc
        ctest_exit_code = $ctestExit
        native_exit_code = $ctestExit
        ctest_outcome = $ctestOutcome
        execution_complete = ($junitAvailable -and $ctestExit -ne 125 -and -not $ctestTimedOut)
        timed_out = $ctestTimedOut
        crashed = (-not $junitAvailable -and $ctestExit -ne 125 -and -not $ctestTimedOut)
        skipped = ($ctestOutcome -eq 'test_failure' -and $junitSummary.skipped -gt 0)
        junit_path = [IO.Path]::GetFullPath($junitPath)
        junit_tests = [int]$junitSummary.tests
        junit_failures = [int]$junitSummary.failures
        junit_errors = [int]$junitSummary.errors
        junit_skipped = [int]$junitSummary.skipped
        junit_timeout = [bool]$junitSummary.timeout
        cpu_only_failure = [bool]$cpuOnlyFailure
        cpu_only_failure_reason = $cpuOnlyReason
    }) $outcomePath

    $entries = @(
        Get-ArtifactEntry $report
        Get-ArtifactEntry $ctestRaw
        Get-ArtifactEntry $inventoryPath
        Get-ArtifactEntry $junitPath
        Get-ArtifactEntry $receiptPath
        Get-ArtifactEntry $outcomePath
        Get-ArtifactEntry $selectedExe
        Get-ArtifactEntry $configureLog
        Get-ArtifactEntry $buildLog
    ) | Where-Object { $null -ne $_ }
    Write-JsonFile ([ordered]@{
        format = 'VOX_FX_T18_ARTIFACTS_V1'
        scenario = $Scenario
        files = @($entries)
    }) $manifestPath

    if ((Test-Path -LiteralPath $report -PathType Leaf) -and $selectedExe -and $buildSucceeded) {
        try {
            & $Python $auditScript $report '--root' $repo '--executable' $selectedExe '--runner-receipt' $receiptPath '--artifact-manifest' $manifestPath '--expected-head' $sourceHead '--expected-tree' $sourceTree 2>&1 | Out-File -LiteralPath $auditRaw -Encoding UTF8 -Width 4096
            $auditExit = $LASTEXITCODE
        } catch {
            $exceptionText = $_.Exception.ToString()
            Write-CommandFailure $auditRaw $exceptionText
        }
    } else {
        Write-CommandFailure $auditRaw 'Auditor not run because the report, selected executable or fresh build was unavailable.'
    }
    if ($auditExit -eq 0) { Copy-Item -LiteralPath $auditRaw -Destination $auditJson -Force }

    $auditObject = $null
    $auditPerformanceFailure = $false
    $auditCpuMeasurementInsufficient = $false
    if ($auditExit -eq 0) {
        try {
            $auditObject = Get-Content -LiteralPath $auditJson -Raw | ConvertFrom-Json
            $auditPerformanceFailure = [bool]$auditObject.performance_failure
            $auditCpuMeasurementInsufficient = [bool]$auditObject.cpu_measurement_insufficient
        }
        catch { $auditExit = 125; $exceptionText = "Successful auditor output was not JSON: $($_.Exception)" }
    }

    $classification = 'diagnostic_pass'
    if ($configureExit -ne 0 -or $buildExit -ne 0) {
        $classification = 'infrastructure_failure'
    } elseif ($ctestOutcome -eq 'timeout_failure') {
        $classification = 'timeout_failure'
    } elseif ($auditExit -ne 0) {
        if (Test-Path -LiteralPath $report -PathType Leaf) {
            $classification = 'infrastructure_failure'
        } else {
            $classification = 'infrastructure_or_test_failure'
        }
    } elseif ($inventoryExit -ne 0) {
        $classification = 'infrastructure_failure'
    } elseif (-not $junitAvailable) {
        $classification = 'infrastructure_failure'
    } elseif ($auditPerformanceFailure) {
        $classification = 'performance_failure'
    } elseif ($auditCpuMeasurementInsufficient) {
        $classification = 'cpu_measurement_insufficient'
    } elseif ($ctestOutcome -eq 'test_failure') {
        $classification = 'test_failure'
    }

    # Add derived audit text to the final manifest and re-check every recorded
    # digest. The auditor itself verified the raw inputs above.
    $finalEntries = @(
        Get-ArtifactEntry $report
        Get-ArtifactEntry $ctestRaw
        Get-ArtifactEntry $inventoryPath
        Get-ArtifactEntry $junitPath
        Get-ArtifactEntry $receiptPath
        Get-ArtifactEntry $outcomePath
        Get-ArtifactEntry $selectedExe
        Get-ArtifactEntry $configureLog
        Get-ArtifactEntry $buildLog
        Get-ArtifactEntry $auditRaw
        Get-ArtifactEntry $auditJson
    ) | Where-Object { $null -ne $_ }
    Write-JsonFile ([ordered]@{
        format = 'VOX_FX_T18_ARTIFACTS_V1'
        scenario = $Scenario
        files = @($finalEntries)
    }) $manifestPath
    Test-ArtifactEntries $finalEntries
    $auditJsonValue = $null
    if (Test-Path -LiteralPath $auditJson -PathType Leaf) { $auditJsonValue = $auditJson }
    $selectedExeReturn = $null
    if ($selectedExe) { $selectedExeReturn = $selectedExe }

    return [ordered]@{
        scenario = $Scenario
        inventory_exit_code = $inventoryExit
        ctest_exit_code = $ctestExit
        audit_exit_code = $auditExit
        timeout_seconds = $TimeoutSeconds
        ctest_outcome = $ctestOutcome
        timed_out = $ctestTimedOut
        classification = $classification
        performance_failure = $auditPerformanceFailure
        cpu_measurement_insufficient = $auditCpuMeasurementInsufficient
        report = $report
        inventory = $inventoryPath
        junit = $junitPath
        ctest_raw = $ctestRaw
        audit_raw = $auditRaw
        audit_json = $auditJsonValue
        runner_receipt = $receiptPath
        post_test_outcome = [IO.Path]::GetFullPath($outcomePath)
        artifact_manifest = $manifestPath
        ctest_selected_executable = $selectedExeReturn
        exception = $exceptionText
    }
}

if ($TimeoutSeconds -ne 180) { throw 'TimeoutSeconds must remain the reviewed 180-second CTest bound' }
$sourceHead = Get-GitValue $repo @('rev-parse', 'HEAD')
$sourceTree = Get-GitValue $repo @('rev-parse', 'HEAD^{tree}')
if ($sourceHead -cne $ExpectedHead) { throw "Expected HEAD $ExpectedHead, found $sourceHead" }
if ($sourceTree -cne $ExpectedTree) { throw "Expected tree $ExpectedTree, found $sourceTree" }
Assert-CleanCheckout

$JuceDir = (Resolve-Path -LiteralPath $JuceDir).Path
if (-not $env:JUCE_COMMIT) { throw 'JUCE_COMMIT must be set to the owner-approved pinned commit' }
$juceCommit = Get-GitValue $JuceDir @('rev-parse', 'HEAD')
if ($juceCommit -cne $env:JUCE_COMMIT) {
    throw "Expected pinned JUCE $env:JUCE_COMMIT, found $juceCommit"
}
$juceStatus = @(& git -C $JuceDir status --porcelain=v1 --untracked-files=all)
if ($juceStatus.Count -ne 0) { throw "Pinned JUCE checkout is not clean: $($juceStatus -join '; ')" }
$runnerName = $env:RUNNER_NAME
if (-not $runnerName) { $runnerName = $env:COMPUTERNAME }
$os = Get-CimInstance Win32_OperatingSystem
$computer = Get-CimInstance Win32_ComputerSystem
$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
$auditScript = Join-Path $repo 'docs\ultima-vox-music\fx\evidence\audit-fx-windows-timing.py'

New-Item -ItemType Directory -Force -Path $out | Out-Null
$configureLog = Join-Path $out 'configure.txt'
$buildLog = Join-Path $out 'build.txt'
Remove-Item -LiteralPath $configureLog, $buildLog -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $build) { Remove-Item -LiteralPath $build -Recurse -Force }

$configureExit = 125
$buildExit = 125
try {
    & cmake -S $repo -B $build -A x64 -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF -DVOX_STRICT_DEPENDENCIES=ON "-DFETCHCONTENT_SOURCE_DIR_JUCE=$JuceDir" 2>&1 | Out-File -LiteralPath $configureLog -Encoding UTF8 -Width 4096
    $configureExit = $LASTEXITCODE
} catch {
    Write-CommandFailure $configureLog $_.Exception.ToString()
}
if ($configureExit -eq 0) {
    try {
        & cmake --build $build --config Release --clean-first --target $testName --parallel 2>&1 | Out-File -LiteralPath $buildLog -Encoding UTF8 -Width 4096
        $buildExit = $LASTEXITCODE
    } catch {
        Write-CommandFailure $buildLog $_.Exception.ToString()
    }
} else {
    Write-CommandFailure $buildLog 'Build not run because configure failed.'
}
Assert-CleanTrackedSource -AllowOutput
$sourceHeadAfterBuild = Get-GitValue $repo @('rev-parse', 'HEAD')
$sourceTreeAfterBuild = Get-GitValue $repo @('rev-parse', 'HEAD^{tree}')
if ($sourceHeadAfterBuild -cne $sourceHead -or $sourceTreeAfterBuild -cne $sourceTree) {
    throw 'Source HEAD/tree changed during configure/build'
}
$buildSucceeded = $configureExit -eq 0 -and $buildExit -eq 0

$results = @()
foreach ($scenario in 'routed', 'authored', 'typical') {
    try {
        $results += Invoke-FxScenario $scenario
    } catch {
        $results += [ordered]@{
            scenario = $scenario
            inventory_exit_code = 125
            ctest_exit_code = 125
            audit_exit_code = 125
            classification = 'infrastructure_failure'
            error = $_.Exception.ToString()
        }
    }
}

$summary = [ordered]@{
    format = 'VOX_FX_T18_ORCHESTRATION_V2'
    source_head = $sourceHead
    source_tree = $sourceTree
    source_tracked_clean_before_build = $true
    source_tracked_clean_after_build = $true
    juce_dir = $JuceDir
    juce_commit = $juceCommit
    build_dir = $build
    configure_exit_code = $configureExit
    build_exit_code = $buildExit
    timeout_seconds = $TimeoutSeconds
    scenarios = @($results)
    overall_failure = @($results | Where-Object { $_.classification -ne 'diagnostic_pass' }).Count -gt 0
    created_utc = [DateTime]::UtcNow.ToString('o')
}
Write-JsonFile $summary (Join-Path $out 'summary.json')
$summary | ConvertTo-Json -Depth 12
if ($summary.overall_failure) { exit 1 }
exit 0
