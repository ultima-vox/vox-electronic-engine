# VOX Drums listening pack (PR 137 section 8), reproducible outside CI (Windows).
# Usage: tools\drums\make-listening-pack.ps1 -Build <build-dir> -Out <new-or-empty-output-folder>
# Builds suite_drums_candidate_tests, runs it with VOX_LISTENING_OUT and leaves
# <Out> (69 MIDI, 23 WAV, README.md, SHA256SUMS.txt) and <Out>.zip.
param([Parameter(Mandatory)][string]$Build, [Parameter(Mandatory)][string]$Out)
$ErrorActionPreference = 'Stop'
$src = Resolve-Path (Join-Path $PSScriptRoot '..\..')
cmake -S $src -B $Build -A x64 -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $Build --config Release --target suite_drums_candidate_tests --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$env:VOX_LISTENING_OUT = [System.IO.Path]::GetFullPath($Out)
ctest --test-dir $Build -C Release -R suite_drums_candidate_tests --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
foreach ($line in Get-Content (Join-Path $Out 'SHA256SUMS.txt')) {
    $hash, $name = $line -split '  ', 2
    if ((Get-FileHash -Algorithm SHA256 (Join-Path $Out $name)).Hash.ToLower() -ne $hash) { throw "checksum mismatch: $name" }
}
Write-Host "verified: $Out and $Out.zip"
