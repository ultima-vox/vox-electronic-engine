param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$OutputDir
)
$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture
$build = Join-Path $env:GITHUB_WORKSPACE 'drums-rt-build'
$source = (& git -C $SourceDir rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $source -cne $env:RT_SOURCE_SHA) { throw 'Source checkout does not match requested SHA.' }
$tree = (& git -C $SourceDir rev-parse 'HEAD^{tree}').Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot read source tree.' }
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$os = Get-CimInstance Win32_OperatingSystem
$machine = Get-CimInstance Win32_ComputerSystem
@(
    "Source: $source", "Tree: $tree", "Controller: $env:GITHUB_SHA",
    "Run: $env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID",
    "Runner: $env:RUNNER_NAME ($env:RT_RUNNER_KIND)",
    "OS: $($os.Caption) $($os.Version)", "CPU: $($cpu.Name), $($cpu.NumberOfCores) cores / $($cpu.NumberOfLogicalProcessors) logical processors",
    "Memory bytes: $($machine.TotalPhysicalMemory)", "JUCE: $env:JUCE_COMMIT", 'Configuration: Visual Studio 2022 x64 Release',
    'Affinity: inherited CPU 0 mask 0x1, checked after process start',
    'CPU load snapshots are coarse; unrelated machine activity cannot be excluded.'
) | Set-Content (Join-Path $OutputDir 'provenance.txt')
$juce = & (Join-Path $SourceDir '.github\scripts\Use-PinnedCheckout.ps1') -Name juce -Repository $env:JUCE_REPOSITORY -Ref 9.0.1 -Commit $env:JUCE_COMMIT
if (-not $juce) { throw 'Pinned JUCE checkout failed.' }
& cmake -S $SourceDir -B $build -A x64 -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF -DVOX_STRICT_DEPENDENCIES=ON "-DFETCHCONTENT_SOURCE_DIR_JUCE=$juce"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& cmake --build $build --config Release --target suite_drums_callback_bench --parallel
if ($LASTEXITCODE -ne 0) { throw 'Benchmark build failed.' }
$exe = Join-Path $build 'tests\suite\Release\suite_drums_callback_bench.exe'
if (-not (Test-Path $exe)) { throw "Benchmark executable not found: $exe" }
"Benchmark SHA-256: $((Get-FileHash $exe -Algorithm SHA256).Hash.ToLower())" | Add-Content (Join-Path $OutputDir 'provenance.txt')

function Record-Environment([string]$name) {
    $path = Join-Path $OutputDir "$name.txt"
    "UTC: $([DateTime]::UtcNow.ToString('o'))" | Set-Content $path
    Get-CimInstance Win32_Processor | Select-Object Name,LoadPercentage | Format-List | Out-String | Add-Content $path
    Get-Process | Sort-Object CPU -Descending | Select-Object -First 30 ProcessName,Id,CPU | Format-Table -AutoSize | Out-String | Add-Content $path
    $raw = & gh api "repos/$env:GITHUB_REPOSITORY/actions/runs?status=in_progress&per_page=100"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot check concurrent repository runs; study is inconclusive.' }
    $runs = ($raw | ConvertFrom-Json).workflow_runs | Where-Object { [string]$_.id -ne $env:GITHUB_RUN_ID }
    $runs | Select-Object id,name,head_sha,html_url | ConvertTo-Json | Add-Content $path
    return @($runs).Count
}

if ((Record-Environment 'before-trials') -ne 0) { throw 'Concurrent repository run detected before measurement; study is inconclusive.' }
$results = @()
$contaminated = $false
$parent = [Diagnostics.Process]::GetCurrentProcess()
for ($trial = 1; $trial -le 3; $trial++) {
    $csv = Join-Path $OutputDir "trial-$trial.csv"
    $out = Join-Path $OutputDir "trial-$trial.stdout.txt"
    $err = Join-Path $OutputDir "trial-$trial.stderr.txt"
    $original = $parent.ProcessorAffinity
    if (($original.ToInt64() -band 1) -eq 0) { throw 'CPU 0 is outside the available process affinity; study is inconclusive.' }
    try {
        $parent.ProcessorAffinity = [IntPtr]1
        $process = Start-Process -FilePath $exe -ArgumentList ('"' + $csv + '"') -NoNewWindow -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
    } finally { $parent.ProcessorAffinity = $original }
    $observed = $process.ProcessorAffinity.ToInt64()
    "Trial $trial observed affinity mask: $observed" | Add-Content (Join-Path $OutputDir 'provenance.txt')
    $process.WaitForExit()
    if ($observed -ne 1 -or $process.ExitCode -ne 0 -or -not (Test-Path $csv)) { throw "Trial $trial failed or its affinity could not be verified." }
    $rows = @(Import-Csv $csv)
    if ($rows.Count -ne 12) { throw "Trial $trial did not produce 12 cells." }
    $seen = @{}
    foreach ($row in $rows) {
        $key = "$($row.sample_rate)/$($row.block)"
        if ($row.sample_rate -notin @('44100','48000','96000') -or $row.block -notin @('64','128','333','512') -or $seen.ContainsKey($key)) { throw "Unexpected or duplicate cell: $key" }
        $seen[$key] = $true
        $p99 = [double]::Parse($row.p99_us, $invariant)
        $deadline = [double]::Parse($row.deadline_us, $invariant)
        if ([double]::IsNaN($p99) -or [double]::IsInfinity($p99) -or $deadline -le 0 -or [double]::IsNaN($deadline) -or [double]::IsInfinity($deadline)) { throw "Invalid values in cell: $key" }
        $results += [pscustomobject]@{trial=$trial; sample_rate=$row.sample_rate; block=$row.block; deadline_us=$row.deadline_us; p50_us=$row.p50_us; p95_us=$row.p95_us; p99_us=$row.p99_us; worst_us=$row.worst_us; strict_pass=($p99 -lt $deadline)}
    }
    if ((Record-Environment "after-trial-$trial") -ne 0) { $contaminated = $true }
}
$results | Export-Csv (Join-Path $OutputDir 'strict-verdict.csv') -NoTypeInformation
$misses = @($results | Where-Object { -not $_.strict_pass }).Count
"Strict deadline misses: $misses / $($results.Count); concurrent repository run observed: $contaminated" | Add-Content (Join-Path $OutputDir 'provenance.txt')
if ($contaminated) { throw 'Concurrent repository run observed during study; timing is inconclusive.' }
if ($misses -ne 0) { throw "Strict p99 deadline missed in $misses cells." }
