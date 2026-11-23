# Extracts pinned NSIS 3.13 into the runner tool cache and prints the directory
# that contains makensis.exe. The zip hash is the official nsis-3.13.zip
# (SHA-256 ba63dffc... / SHA-1 db14f8af...), also published by the Tauri binary
# mirror. A hash mismatch is a failure. This script does not use a random
# makensis already on PATH.
$ErrorActionPreference = 'Stop'

function Show([string] $text) { [Console]::Error.WriteLine($text) }

$version = '3.13'
$sha256 = 'ba63dffc4410ee89193e1cb5a41989991bd77c61068da17e3156d136b7b0b3d8'
$urls = @(
  'https://github.com/tauri-apps/binary-releases/releases/download/nsis-3.13/nsis-3.13.zip',
  'https://downloads.sourceforge.net/project/nsis/NSIS%203/3.13/nsis-3.13.zip'
)
if (-not $env:RUNNER_TOOL_CACHE) { Show 'RUNNER_TOOL_CACHE is not set'; exit 1 }
$cache = Join-Path $env:RUNNER_TOOL_CACHE "vox-pinned\nsis-$version"
$stamp = Join-Path $cache 'SHA256.txt'
$bin = Join-Path $cache "nsis-$version\Bin\makensis.exe"

if ((Test-Path $bin) -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $sha256)) {
  Write-Output (Split-Path $bin)
  exit 0
}

if (Test-Path $cache) { Remove-Item -Recurse -Force $cache }
New-Item -ItemType Directory -Force -Path $cache | Out-Null
$zip = Join-Path $cache 'nsis.zip'
$ok = $false
foreach ($url in $urls) {
  Show "NSIS download $url"
  try {
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    $got = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
    if ($got -ne $sha256) {
      Show "NSIS hash mismatch from $url : $got"
      Remove-Item -Force $zip
      continue
    }
    $ok = $true
    break
  } catch {
    Show "NSIS download failed: $url : $_"
    if (Test-Path $zip) { Remove-Item -Force $zip }
  }
}
if (-not $ok) { Show 'NSIS 3.13 zip was not downloaded with the pinned hash'; exit 1 }

Expand-Archive -Path $zip -DestinationPath $cache -Force
if (-not (Test-Path $bin)) { Show "makensis.exe missing after extract: $bin"; exit 1 }
Set-Content -Path $stamp -Value $sha256 -Encoding ascii
Write-Output (Split-Path $bin)
