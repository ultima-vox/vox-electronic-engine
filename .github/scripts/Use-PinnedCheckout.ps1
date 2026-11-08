# Provides a verified, unmodified checkout of a pinned dependency commit from
# the self-hosted runner's tool cache, fetching it (with retries) only when it
# is missing. Prints the directory path as the only pipeline output.
#
# Why: on 2026-11-08 FetchContent's JUCE clone broke off repeatedly on runner
# 1 (RPC failed / early EOF), failing builds before any compilation. CMake
# re-verifies the JUCE commit at configure time (root CMakeLists.txt).
#
# Exit codes are checked explicitly: Windows PowerShell turns native stderr
# into terminating errors under ErrorActionPreference=Stop.
#
# Paths: the VST3 SDK contains files deeper than Windows MAX_PATH (260), so
# git runs with core.longpaths=true (passed on to submodule clones), and the
# cache directory uses a 12-character commit prefix to keep build paths short.
# The full commit is still verified.
param(
  [Parameter(Mandatory)] [string] $Name,
  [Parameter(Mandatory)] [string] $Repository,
  [Parameter(Mandatory)] [string] $Ref,
  [Parameter(Mandatory)] [string] $Commit,
  [string[]] $Submodules = @()
)

$cache = Join-Path $env:RUNNER_TOOL_CACHE 'vox-pinned'
New-Item -ItemType Directory -Force -Path $cache | Out-Null

function Get-Head($dir) {
  if (Test-Path (Join-Path $dir '.git')) { return (git -C $dir rev-parse HEAD 2>$null) }
  return ''
}

$dir = Join-Path $cache "$Name-$($Commit.Substring(0, 12))"
if ((Get-Head $dir) -ne $Commit) {
  if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
  $tmp = "$dir.partial"
  $ok = $false
  for ($attempt = 1; $attempt -le 4 -and -not $ok; $attempt++) {
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    git -c advice.detachedHead=false -c core.longpaths=true clone --quiet --depth 1 --branch $Ref $Repository $tmp | Out-Host
    $cloneExit = $LASTEXITCODE
    if ($cloneExit -eq 0 -and (Get-Head $tmp) -eq $Commit) {
      $ok = $true
      foreach ($module in $Submodules) {
        git -c core.longpaths=true -C $tmp submodule update --quiet --init --depth 1 $module | Out-Host
        if ($LASTEXITCODE -ne 0) { $ok = $false; Write-Host "$Name submodule $module failed (git exit $LASTEXITCODE)" }
      }
    }
    if (-not $ok) {
      Write-Host "$Name fetch attempt $attempt failed (clone exit $cloneExit); retrying"
      Start-Sleep -Seconds ([math]::Pow(2, $attempt))
    }
  }
  if (-not $ok) { throw "could not fetch $Name $Commit from $Repository after 4 attempts (see git errors above: network = infrastructure, anything else = this script)" }
  Move-Item $tmp $dir
  if ((Get-Head $dir) -ne $Commit) { throw "could not place $Name at $dir" }
}

$dirty = git -C $dir status --porcelain --ignore-submodules=none
if ($dirty) { throw "cached $Name copy at $dir has local modifications" }
Write-Host "$Name verified at $(Get-Head $dir) ($dir)"
Write-Output $dir
