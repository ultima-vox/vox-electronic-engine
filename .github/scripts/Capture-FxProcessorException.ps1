# Replay one allowlisted existing Release FX CTest executable. This is a
# diagnostic capture, never a replacement for CTest's result. No runner-wide
# WER/debug settings are changed and no debugger is installed by this script.
[CmdletBinding()]
param(
    [string]$BuildDir = 'build-suite',
    [string]$OutputDir = 'suite-diagnostic',
    [string]$CdbPath,
    [ValidateRange(5,180)][int]$TimeoutSeconds = 90,
    [ValidateSet('suite_fx_processor_tests', 'suite_fx_noise_riser_tests', 'suite_fx_layer_tests', 'suite_fx_shepard_tests')]
    [string]$TestName = 'suite_fx_processor_tests'
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDir).Path
$out = (New-Item -ItemType Directory -Force -Path $OutputDir).FullName
$label = $TestName -creplace '^suite_', '' -creplace '_tests$', '' -replace '_', '-'
$report = Join-Path $out "$label-diagnostic.txt"

function Note([string]$Message) {
    $Message | Add-Content -LiteralPath $report
    Write-Host $Message
}

if (-not $CdbPath) {
    $sdkRoot = ${env:ProgramFiles(x86)}
    $sdkCdb = if ($sdkRoot) { Join-Path $sdkRoot 'Windows Kits\10\Debuggers\x64\cdb.exe' }
    if ($sdkCdb -and (Test-Path -LiteralPath $sdkCdb -PathType Leaf)) {
        $CdbPath = $sdkCdb
    } else {
        $CdbPath = (Get-Command cdb.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    }
}

$metadata = & ctest --test-dir $build -C Release -R "^$testName$" '--show-only=json-v1'
if ($LASTEXITCODE -ne 0) { throw 'Unable to discover the Release FX CTest executable.' }
$test = @((($metadata -join "`n") | ConvertFrom-Json).tests | Where-Object { $_.name -ceq $testName })
if ($test.Count -ne 1 -or @($test[0].command).Count -ne 1) {
    throw 'Expected exactly one named FX test with a no-argument Release command.'
}
$exe = (Resolve-Path -LiteralPath $test[0].command[0]).Path
$working = ($test[0].properties | Where-Object { $_.name -ceq 'WORKING_DIRECTORY' } | Select-Object -First 1).value
if (-not $working) { throw 'CTest did not provide the working directory.' }
$working = (Resolve-Path -LiteralPath $working).Path
$exeDir = Split-Path -Parent $exe
$pdb = [IO.Path]::ChangeExtension($exe, '.pdb')
$module = [IO.Path]::GetFileNameWithoutExtension($exe)
if ($module -cne $TestName) { throw "Unexpected executable basename for ${TestName}: $module" }
Note "Checkout: $(& git rev-parse HEAD)"
Note "Tree: $(& git rev-parse 'HEAD^{tree}')"
Note "Test: $testName"
Note "Test EXE SHA-256: $((Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash.ToLowerInvariant())"
if (Test-Path -LiteralPath $pdb -PathType Leaf) {
    Note "Matching build PDB candidate SHA-256: $((Get-FileHash -Algorithm SHA256 -LiteralPath $pdb).Hash.ToLowerInvariant())"
} else {
    Note 'No adjacent link PDB found. Stack quality may be limited; retain matching runner symbols.'
}
if (-not $CdbPath) {
    Note 'No installed cdb.exe found. Retain original CTest logs; use an authorized x64 debugger on the runner.'
    return
}

$dump = Join-Path $out "$label.dmp"
$commands = Join-Path $out "$label-cdb-commands.txt"
$debugLog = Join-Path $out "$label-cdb.log"
# The dump command is inside an event-command string; use a relative ASCII
# filename (in the CTest working directory) rather than nested path quotes.
# Use a unique name so a stale crash cannot be mistaken for this replay.
$dumpName = 'vox-fx-' + [Guid]::NewGuid().ToString('N') + '.dmp'
$tempDump = Join-Path $working $dumpName
$onException = ".lastevent; .exr -1; .ecxr; kP; .dump /m $dumpName; q"
@(
    # Reject mismatched symbols and leave the module's loaded-symbol status
    # in the transcript. The presence of an adjacent PDB is not a match.
    '.symopt+ 0x400'
    ".reload /f $module.exe"
    "lmv m $module"
    "sxd -c2 `"$onException`" av"
    "sxd -c2 `"$onException`" sov"
    "sxd -c2 `"$onException`" eh"
    # STATUS_FAIL_FAST_EXCEPTION and STATUS_STACK_BUFFER_OVERRUN
    "sxd -c2 `"$onException`" c0000602"
    "sxd -c2 `"$onException`" c0000409"
    'g'
) | Set-Content -LiteralPath $commands -Encoding ASCII

# This list contains only flags or absolute file paths (no embedded quotes or
# trailing slashes). -cf avoids quoting debugger commands through PowerShell.
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = (Resolve-Path -LiteralPath $CdbPath).Path
$start.Arguments = '-G -lines -sins -y "{0}" -logo "{1}" -cf "{2}" "{3}"' -f $exeDir,$debugLog,$commands,$exe
$start.WorkingDirectory = $working
$start.UseShellExecute = $false
$start.RedirectStandardInput = $true
$process = [System.Diagnostics.Process]::Start($start)
try {
    $process.StandardInput.Close()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Note "Debugger replay did not exit within $TimeoutSeconds seconds; killing its process tree. No crash is inferred."
        & "$env:SystemRoot\System32\taskkill.exe" /PID $process.Id /T /F | Out-Null
        [void]$process.WaitForExit(5000)
    } else {
        Note "CDB exit code: $($process.ExitCode) (not a CTest status)."
    }
} finally {
    $process.Dispose()
    if (Test-Path -LiteralPath $tempDump -PathType Leaf) {
        Move-Item -LiteralPath $tempDump -Destination $dump -Force
    }
}
if (Test-Path -LiteralPath $dump -PathType Leaf) {
    Note "Minidump SHA-256: $((Get-FileHash -Algorithm SHA256 -LiteralPath $dump).Hash.ToLowerInvariant())"
    Copy-Item -LiteralPath $exe -Destination $out
    if (Test-Path -LiteralPath $pdb -PathType Leaf) { Copy-Item -LiteralPath $pdb -Destination $out }
} else {
    Note 'No minidump captured. Inspect debugger output; do not infer that the original failure is fixed.'
}
