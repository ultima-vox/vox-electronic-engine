# Install / upgrade / uninstall smoke for the ULTIMA VOX MUSIC suite installer.
# The generated NSIS script's default directory is the standard VST3 location.
# This process never installs there: NSIS /D= points at a directory created for
# this job under RUNNER_TEMP. Nothing under C:\ or a pre-existing install is
# deleted. Registry cleanup is only our own Uninstall.exe, and only after a
# preflight proved the uninstall key was absent.
param(
  [Parameter(Mandatory)] [string] $Installer,
  [Parameter(Mandatory)] [string] $BuildDir,
  [Parameter(Mandatory)] [string] $Zip,
  [Parameter(Mandatory)] [string] $EvidencePath
)

$ErrorActionPreference = 'Stop'
$log = New-Object System.Collections.Generic.List[string]
function Note([string] $text) {
  $log.Add($text)
  [Console]::Out.WriteLine($text)
}
function Fail([string] $text) {
  Note "FAIL: $text"
  $dir = Split-Path $EvidencePath
  if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
  $log | Set-Content -Path $EvidencePath -Encoding utf8
  exit 1
}

$products = @(
  @{ Name = 'VOX Drums'; Id = 'com.ultimavox.vox-drums'; Cid = 'ABCDEF019182FAEB556C567856784472' },
  @{ Name = 'VOX FX'; Id = 'com.ultimavox.vox-fx'; Cid = 'ABCDEF019182FAEB556C567856784658' },
  @{ Name = 'VOX Trance Synthesis'; Id = 'com.ultimavox.vox-trance-synthesis'; Cid = 'ABCDEF019182FAEB556C567856785453' }
)
$stage = $null
$neighbor = $null
$ownedRoot = $null
$ownedToken = [guid]::NewGuid().ToString('n')
$installedByThisTest = $false
$cleanupFailed = $false
$regViews = @(
  'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\ULTIMA VOX MUSIC',
  'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\ULTIMA VOX MUSIC',
  'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\ULTIMA VOX MUSIC'
)

function Uninstall-Keys {
  @($regViews | Where-Object { Test-Path $_ })
}

function Test-UnderOwnedRoot([string] $dir) {
  if (-not $dir -or -not $ownedRoot) { return $false }
  $full = [IO.Path]::GetFullPath($dir).TrimEnd('\')
  $root = [IO.Path]::GetFullPath($ownedRoot).TrimEnd('\')
  if ($full.Equals($root, [StringComparison]::OrdinalIgnoreCase)) { return $true }
  return $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Test-Owned([string] $dir) {
  if (-not (Test-UnderOwnedRoot $dir)) { return $false }
  $marker = Join-Path $ownedRoot 'VOX-SUITE-SMOKE-OWNED.txt'
  if (-not (Test-Path -LiteralPath $marker)) { return $false }
  return (Get-Content -Raw -LiteralPath $marker).Trim() -eq $ownedToken
}

function Remove-Owned([string] $dir) {
  if (-not (Test-Path -LiteralPath $dir)) { return }
  if (-not (Test-Owned $dir)) { Fail "refusing to delete unowned path $dir" }
  Remove-Item -LiteralPath $dir -Recurse -Force
}

# NSIS requires /D= to be the last argument and unquoted, including when the
# path contains spaces. FileName holds the executable; Arguments is one string.
function Invoke-Installer([string] $path, [string] $arguments, [string] $label) {
  if ($arguments -match '/D=') {
    if ($arguments -notmatch '^/S /D=.+') { Fail "$label /D= must be the last argument" }
    if ($arguments.Contains('"')) { Fail "$label /D= path must not be quoted" }
    $dest = $arguments.Substring(6)
    if ($dest -match ' /') { Fail "$label /D= must be the last argument" }
    if (-not (Test-Owned $dest)) { Fail "$label refused /D= outside the job-owned directory" }
  }
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $path
  $psi.Arguments = $arguments
  $psi.UseShellExecute = $false
  $proc = [Diagnostics.Process]::Start($psi)
  if (-not $proc.WaitForExit(180000)) {
    try { $proc.Kill() } catch {}
    Fail "$label timed out (elevation prompt or hang)"
  }
  Note "$label exit $($proc.ExitCode)"
  if ($proc.ExitCode -ne 0) { Fail "$label failed" }
}

function Assert-Bundles([string] $root) {
  $bundles = @(Get-ChildItem -Path $root -Directory -Filter '*.vst3' -ErrorAction SilentlyContinue)
  if ($bundles.Count -ne 3) { Fail "expected 3 VST3 bundles in $root, found $($bundles.Count)" }
  $entries = @()
  foreach ($product in $products) {
    $bundle = Join-Path $root ($product.Name + '.vst3')
    $entries += (($bundle -replace '\\','/') + '|' + $product.Cid + '|' + $product.Name)
    $info = Join-Path $bundle 'Contents\Resources\moduleinfo.json'
    if (-not (Test-Path $info)) { Fail "missing $info" }
    $json = Get-Content -Raw $info
    if ($json -notmatch [regex]::Escape($product.Cid)) { Fail "$($product.Name) moduleinfo is missing $($product.Cid)" }
    if ($json -match 'com\.ultimavox\.vstengine') { Fail "$($product.Name) contains the legacy Rack bundle id" }
  }
  $arg = ($entries -join ';')
  $checker = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..')) 'cmake\suite\CheckModuleInfo.cmake'
  & cmake "-DBUNDLES=$arg" -P $checker
  if ($LASTEXITCODE -ne 0) { Fail 'installed moduleinfo.json does not match the frozen CID' }
}

try {
  $admin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
  Note "elevated: $admin"
  if (-not $admin) { Fail 'smoke requires an elevated runner so the NSIS uninstall registry entry can be created and removed' }
  if (-not $env:RUNNER_TEMP) { Fail 'RUNNER_TEMP is not set; refusing to invent a path outside the job temp directory' }
  $preexisting = @(Uninstall-Keys)
  if ($preexisting.Count -gt 0) {
    Fail "refusing to run: uninstall key already exists ($($preexisting -join '; ')). This smoke will not uninstall or replace it."
  }

  $tempRoot = [IO.Path]::GetFullPath($env:RUNNER_TEMP).TrimEnd('\')
  $ownedRoot = Join-Path $tempRoot ("vox-suite-smoke-" + $(if ($env:GITHUB_RUN_ID) { $env:GITHUB_RUN_ID } else { 'local' }) + "-" + $ownedToken)
  if (Test-Path -LiteralPath $ownedRoot) { Fail "owned staging path already exists: $ownedRoot" }
  New-Item -ItemType Directory -Path $ownedRoot | Out-Null
  Set-Content -LiteralPath (Join-Path $ownedRoot 'VOX-SUITE-SMOKE-OWNED.txt') -Value $ownedToken -Encoding ascii -NoNewline
  if (-not (Test-Owned $ownedRoot)) { Fail "ownership marker was not written for $ownedRoot" }
  $stage = Join-Path $ownedRoot 'stage'
  $neighbor = Join-Path $ownedRoot 'neighbor'
  New-Item -ItemType Directory -Path $stage | Out-Null
  New-Item -ItemType Directory -Path $neighbor | Out-Null
  Set-Content -LiteralPath (Join-Path $neighbor 'keep.txt') -Value 'do not delete' -Encoding ascii -NoNewline
  Note "owned: $ownedRoot"
  Note "stage: $stage"

  $os = Get-CimInstance Win32_OperatingSystem
  Note "os: $($os.Caption) $($os.Version) $($os.OSArchitecture)"
  if ($os.Caption -match 'Windows 11') { Note 'not smoked on this job: Windows 10' }
  elseif ($os.Caption -match 'Windows 10') { Note 'not smoked on this job: Windows 11' }
  else { Note 'not smoked on this job: Windows 10 and Windows 11 (this OS caption matched neither)' }

  if ($Installer -match 'Vox Electronic Engine') { Fail 'refusing the legacy Rack installer name' }
  $sig = Get-AuthenticodeSignature -FilePath $Installer
  Note "authenticode: $($sig.Status)"
  if ($sig.Status -ne 'NotSigned') { Fail "expected an unsigned installer, got $($sig.Status)" }

  $nsi = @(Get-ChildItem -Path $BuildDir -Recurse -Filter '*.nsi' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\NSIS\\' })
  if ($nsi.Count -lt 1) { Fail "no generated NSIS script under $BuildDir" }
  $script = Get-Content -Raw $nsi[0].FullName
  Note "nsi: $($nsi[0].FullName)"
  if ($script -notmatch 'Common Files[/\\]VST3') { Fail 'NSIS script does not install under Common Files/VST3' }
  if ($script -notmatch 'Ultima Vox') { Fail 'NSIS script does not use the Ultima Vox install directory' }
  if ($script -match 'Vox Electronic Engine') { Fail 'NSIS script names the legacy Rack product' }
  if ($script -match 'com\.ultimavox\.vstengine') { Fail 'NSIS script contains the legacy bundle id' }
  foreach ($line in ($script -split "`r?`n")) {
    if ($line -match 'InstallDir|WriteRegStr|DeleteRegKey|DisplayName') { Note "nsi-line: $($line.Trim())" }
  }

  $expand = Join-Path $ownedRoot 'zip-expand'
  Expand-Archive -Path $Zip -DestinationPath $expand
  $zipBundles = @(Get-ChildItem -Path $expand -Recurse -Directory -Filter '*.vst3')
  if ($zipBundles.Count -ne 3) { Fail "ZIP does not contain exactly 3 VST3 bundles ($($zipBundles.Count))" }
  foreach ($product in $products) {
    if (-not ($zipBundles.Name -contains ($product.Name + '.vst3'))) { Fail "ZIP missing $($product.Name).vst3" }
  }
  if (Get-ChildItem -Path $expand -Recurse -Filter 'Vox Electronic Engine*' -ErrorAction SilentlyContinue) {
    Fail 'ZIP contains a legacy Rack payload'
  }
  Note 'zip: 3 suite bundles, no legacy payload'

  Note "install /D=$stage"
  $installedByThisTest = $true
  Invoke-Installer $Installer "/S /D=$stage" 'install'
  Assert-Bundles $stage
  if (-not (Test-Path (Join-Path $stage 'SUITE-MANIFEST.txt'))) { Fail 'SUITE-MANIFEST.txt was not installed' }
  if ((Uninstall-Keys).Count -lt 1) { Fail 'install did not write the ULTIMA VOX MUSIC uninstall key' }
  $manifest = Get-Content -Raw (Join-Path $stage 'SUITE-MANIFEST.txt')
  if ($manifest -notmatch 'human_approved: 0') { Fail 'manifest does not keep human_approved at 0' }
  if ($manifest -notmatch 'release_accepted: no') { Fail 'manifest claims a release' }
  foreach ($product in $products) {
    if ($manifest -notmatch [regex]::Escape($product.Id)) { Fail "manifest is missing $($product.Id)" }
    if ($manifest -notmatch [regex]::Escape($product.Cid)) { Fail "manifest is missing $($product.Cid)" }
  }

  $info = Join-Path $stage 'VOX Drums.vst3\Contents\Resources\moduleinfo.json'
  $bytes = [IO.File]::ReadAllBytes($info)
  [IO.File]::WriteAllBytes($info, $bytes + [byte]0x58)
  Note 'upgrade restores a corrupted bundle'
  Invoke-Installer $Installer "/S /D=$stage" 'upgrade'
  Assert-Bundles $stage
  if ((Get-Content -Raw (Join-Path $neighbor 'keep.txt')) -ne 'do not delete') { Fail 'upgrade touched a neighboring directory' }

  $uninstaller = Join-Path $stage 'Uninstall.exe'
  if (-not (Test-Path $uninstaller)) { Fail 'Uninstall.exe missing' }
  Invoke-Installer $uninstaller '/S' 'uninstall'
  $left = @()
  if (Test-Path $stage) { $left = @(Get-ChildItem -Path $stage -Recurse -Filter '*.vst3' -ErrorAction SilentlyContinue) }
  if ($left.Count -ne 0) { Fail "uninstall left $($left.Count) VST3 bundles" }
  if (Test-Path $uninstaller) { Fail 'uninstall left Uninstall.exe' }
  if ((Uninstall-Keys).Count -ne 0) { Fail 'uninstall left the registry key' }
  if ((Get-Content -Raw (Join-Path $neighbor 'keep.txt')) -ne 'do not delete') { Fail 'uninstall deleted a neighboring directory' }
  Note 'uninstall clean; neighbor kept'

  Invoke-Installer $Installer "/S /D=$stage" 'reinstall'
  Assert-Bundles $stage
  Invoke-Installer (Join-Path $stage 'Uninstall.exe') '/S' 'uninstall-after-reinstall'
  $left = @()
  if (Test-Path $stage) { $left = @(Get-ChildItem -Path $stage -Recurse -Filter '*.vst3' -ErrorAction SilentlyContinue) }
  if ($left.Count -ne 0) { Fail 'reinstall uninstall left a bundle' }
  if ((Uninstall-Keys).Count -ne 0) { Fail 'reinstall uninstall left the registry key' }
  Note 'reinstall and final uninstall passed'
} finally {
  if ($installedByThisTest -and $stage -and (Test-Owned $stage)) {
    $cleanup = Join-Path $stage 'Uninstall.exe'
    if (Test-Path -LiteralPath $cleanup) {
      try { Invoke-Installer $cleanup '/S' 'cleanup-uninstall' } catch { Note "cleanup-uninstall: $_" }
    }
  }
  if ($ownedRoot -and (Test-Path -LiteralPath $ownedRoot)) {
    if (Test-Owned $ownedRoot) {
      try { Remove-Owned $ownedRoot } catch { Note "cleanup-dir: $_" }
      if (Test-Path -LiteralPath $ownedRoot) {
        Note 'FAIL: job-owned directory remains after cleanup'
        $cleanupFailed = $true
      }
    } else {
      Note "leaving $ownedRoot because the ownership marker does not match"
      $cleanupFailed = $true
    }
  }
  $dir = Split-Path $EvidencePath
  if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
  $log | Set-Content -Path $EvidencePath -Encoding utf8
}
if ($cleanupFailed) { exit 1 }
exit 0
