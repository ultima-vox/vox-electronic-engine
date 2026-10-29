# Development install of the Vox Full-on Drums package for Vox Rack (no signing,
# no installer). Copies the versioned package to a fixed root and sets the
# per-user VOX_DEV_INSTRUMENT_ROOT variable the Rack reads at startup.
#
#   powershell -ExecutionPolicy Bypass -File install-drums-package.ps1 -PackageZipDir <dir>
#
# <dir> is the extracted "Vox-FullOn-Drums-Package-Windows" artifact, i.e. the
# folder that contains com.ultimavox.fullon-drums.voxinst.
param(
  [Parameter(Mandatory = $true)][string]$PackageZipDir,
  [string]$Root = "C:\VoxDev\instruments"
)
$ErrorActionPreference = "Stop"
$source = Join-Path $PackageZipDir "com.ultimavox.fullon-drums.voxinst"
if (-not (Test-Path (Join-Path $source "0.1.0\manifest.json"))) {
  throw "Not found: $source\0.1.0\manifest.json"
}
New-Item -ItemType Directory -Force -Path $Root | Out-Null
$target = Join-Path $Root "com.ultimavox.fullon-drums.voxinst"
if (Test-Path $target) { Remove-Item -Recurse -Force $target }
Copy-Item -Recurse $source $target

$dll = Join-Path $target "0.1.0\module\x64\VoxFullOnDrums.dll"
$manifest = Get-Content (Join-Path $target "0.1.0\manifest.json") -Raw | ConvertFrom-Json
$actual = (Get-FileHash -Algorithm SHA256 $dll).Hash.ToLower()
if ($actual -ne $manifest.sha256) { throw "SHA-256 mismatch: $actual vs $($manifest.sha256)" }

[Environment]::SetEnvironmentVariable("VOX_DEV_INSTRUMENT_ROOT", $Root, "User")
Write-Host "Installed $dll"
Write-Host "SHA-256 $actual (matches manifest)"
Write-Host "VOX_DEV_INSTRUMENT_ROOT=$Root (user). Sign out/in or restart Cubase from a new shell."
