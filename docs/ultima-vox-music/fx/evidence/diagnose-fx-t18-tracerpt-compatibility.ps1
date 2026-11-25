[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,
    [string]$Python = 'python',
    [string]$ParserPath = '',
    [string]$TracerptPath = '',
    [string]$LogmanPath = '',
    [string]$ControlledProcessPath = '',
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string]$ExpectedTracerptVersion,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{64}$')]
    [string]$ExpectedTracerptSha256,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string]$ExpectedLogmanVersion,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{64}$')]
    [string]$ExpectedLogmanSha256,
    [ValidateRange(1, 5)]
    [int]$ObservationSeconds = 3,
    [ValidateNotNullOrEmpty()]
    [string]$ExpectedSignerSubject = 'Microsoft'
)

# Diagnostic-only compatibility probe. It never changes services, accounts,
# groups, ACLs, UAC policy, or installed binaries; it never runs CTest or a DAW.
# It requires an already elevated Windows PowerShell 5.1 console, explicit
# owner-measured four-part versions/SHA-256 values, and checks the exact
# installed bytes plus valid Microsoft Authenticode before invoking either
# logman.exe or tracerpt.exe. A nonzero exit is blocked evidence, not a retry.
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrEmpty($PSScriptRoot)) {
    throw 'PowerShell -File entrypoint did not provide PSScriptRoot'
}

$systemRoot = $env:SystemRoot
$requiredSystemRoot = 'C:\Windows'
if ([string]::IsNullOrEmpty($systemRoot) -or $systemRoot -ine $requiredSystemRoot) {
    throw "This diagnostic requires the exact Windows root $requiredSystemRoot"
}
if ([string]::IsNullOrEmpty($ParserPath)) {
    $ParserPath = Join-Path $PSScriptRoot 'parse-fx-t18-etl.py'
}
if ([string]::IsNullOrEmpty($TracerptPath)) {
    $TracerptPath = Join-Path $requiredSystemRoot 'System32\tracerpt.exe'
}
if ([string]::IsNullOrEmpty($LogmanPath)) {
    $LogmanPath = Join-Path $requiredSystemRoot 'System32\logman.exe'
}
if ([string]::IsNullOrEmpty($ControlledProcessPath)) {
    $ControlledProcessPath = Join-Path $requiredSystemRoot 'System32\timeout.exe'
}

function Write-JsonFile([object]$Value, [string]$Path) {
    $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Normalize-FileVersion([string]$Value) {
    $normalized = [regex]::Replace($Value.Trim(), '(?i)\s*\(WinBuild\.[^)]*\)\s*$', '').Trim()
    if ($normalized -notmatch '^\d+\.\d+\.\d+\.\d+$') {
        throw "Unsupported FileVersion: $Value"
    }
    return $normalized
}

function Assert-Sha256([string]$Value, [string]$Label) {
    if ($Value -notmatch '^[0-9a-fA-F]{64}$') {
        throw "$Label is not an exact SHA-256"
    }
}

function Assert-ExactSystem32Path([string]$Path, [string]$Leaf) {
    $expected = [IO.Path]::GetFullPath((Join-Path $requiredSystemRoot ("System32\" + $Leaf)))
    $actual = [IO.Path]::GetFullPath($Path)
    if ($actual -ine $expected) {
        throw "$Leaf must be the exact System32 path $expected, not $actual"
    }
}

function Get-ToolIdentity([string]$Path, [string]$ExpectedVersion, [string]$ExpectedHash) {
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($item.PSIsContainer -or -not $item.FullName) { throw "Tool path is not a file: $Path" }
    $rawVersion = [string]$item.VersionInfo.FileVersion
    $normalizedVersion = Normalize-FileVersion $rawVersion
    $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $signature = Get-AuthenticodeSignature -FilePath $item.FullName
    $signatureStatus = [string]$signature.Status
    $certificate = $signature.SignerCertificate
    $subject = if ($certificate) { [string]$certificate.Subject } else { '' }
    $thumbprint = if ($certificate) { [string]$certificate.Thumbprint } else { '' }
    if (-not [string]::IsNullOrEmpty($ExpectedVersion) -and
        $normalizedVersion -cne (Normalize-FileVersion $ExpectedVersion)) {
        throw "$Path normalized FileVersion $normalizedVersion does not match expected $ExpectedVersion"
    }
    if (-not [string]::IsNullOrEmpty($ExpectedHash)) {
        Assert-Sha256 $ExpectedHash "$Path expected hash"
        if ($hash -cne $ExpectedHash.ToLowerInvariant()) {
            throw "$Path SHA-256 changed from the exact expected diagnostic bytes"
        }
    }
    if ($signatureStatus -cne 'Valid' -or -not $certificate) {
        throw "$Path does not have a valid Authenticode signature"
    }
    if ($subject -notmatch '(?i)Microsoft' -or
        $subject -notmatch [regex]::Escape($ExpectedSignerSubject)) {
        throw "$Path signer subject is not the expected Microsoft signer: $subject"
    }
    return [ordered]@{
        path = $item.FullName
        file_version_raw = $rawVersion
        file_version_normalized = $normalizedVersion
        sha256 = $hash
        signature_status = $signatureStatus
        signer_subject = $subject
        signer_thumbprint = $thumbprint
    }
}

function Assert-ToolIdentityUnchanged([string]$Path, [object]$ExpectedIdentity,
    [string]$ExpectedVersion, [string]$ExpectedHash) {
    $current = Get-ToolIdentity $Path $ExpectedVersion $ExpectedHash
    foreach ($field in @('path', 'file_version_normalized', 'sha256',
                         'signature_status', 'signer_subject', 'signer_thumbprint')) {
        if ([string]$current[$field] -cne [string]$ExpectedIdentity[$field]) {
            throw "$Path identity changed after preflight ($field)"
        }
    }
}

function Invoke-Native([string]$Path, [string[]]$Arguments) {
    # Windows PowerShell 5.1 promotes redirected native stderr to a
    # RemoteException when ErrorActionPreference is Stop. That must not turn a
    # real native HRESULT/exit code into the synthetic -1 used only for a
    # process-launch failure. Temporarily collect native output with Continue,
    # then read LASTEXITCODE before restoring the caller's preference.
    $previousPreference = $ErrorActionPreference
    $raw = @()
    $exitCode = $null
    try {
        $ErrorActionPreference = 'Continue'
        $raw = @(& $Path @Arguments 2>&1)
        $exitCode = [int]$LASTEXITCODE
    }
    catch {
        $text = ($raw | ForEach-Object { [string]$_ }) -join [Environment]::NewLine
        if ([string]::IsNullOrEmpty($text)) { $text = $_.Exception.ToString() }
        return [pscustomobject]@{ exit_code = -1; text = $text; arguments = $Arguments }
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    $text = ($raw | ForEach-Object { [string]$_ }) -join [Environment]::NewLine
    return [pscustomobject]@{ exit_code = $exitCode; text = $text; arguments = $Arguments }
}

function Save-NativeResult([object]$Result, [string]$Path) {
    @(
        "exit_code=$($Result.exit_code)"
        "arguments=$($Result.arguments -join ' ')"
        '--- RAW OUTPUT ---'
        $Result.text
    ) | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-ControlTimeUtc([string]$Text, [string]$Label) {
    $match = [regex]::Match($Text, '(?im)^\s*' + [regex]::Escape($Label) + '\s*[:=]\s*(?<value>.+?)\s*$')
    if (-not $match.Success) { return $null }
    try {
        $parsed = [DateTimeOffset]::Parse(
            $match.Groups['value'].Value,
            [Globalization.CultureInfo]::CurrentCulture,
            [Globalization.DateTimeStyles]::AssumeLocal)
        return $parsed.ToUniversalTime().ToString('o')
    }
    catch {
        return $null
    }
}

function Get-ActiveQueryDiagnostics([object]$Result, [string]$Session) {
    $text = [string]$Result.text
    $fields = [ordered]@{
        session_name = ($text -match [regex]::Escape($Session))
        status_running = ($text -match '(?im)status\s*[:=]\s*running\b')
        status_inactive = ($text -match '(?im)status\s*[:=]\s*(?:stopped|not\s+running|inactive)\b')
        provider_guid = ($text -match '(?i)22FB2CD6-0E7B-422B-A0C7-2FAD1FD0E716')
        provider_name = ($text -match '(?im)microsoft-windows-kernel-process')
        keyword = ($text -match '(?im)^\s*keywords?(?:\s*all|\s*any)?\s*[:=]\s*0x10\b')
        level = ($text -match '(?im)(?:level|verbosity)\s*[:=]\s*5\b')
        circular_mode = ($text -match '(?i)bincirc|circular')
        maximum_file_size = ($text -match '(?im)maximum\s+file\s+size\s*[:=]\s*16\b')
        start_time = ($text -match '(?im)^\s*start\s+time\s*[:=]')
    }
    $startTimeUtc = if ($fields.start_time) { Get-ControlTimeUtc $text 'Start Time' } else { $null }
    $fields.Add('start_time_parseable', (-not [string]::IsNullOrEmpty($startTimeUtc)))
    $formatCompatible = ($fields.session_name -and $fields.status_running -and $fields.provider_guid -and
        $fields.provider_name -and $fields.keyword -and $fields.level -and
        -not $fields.circular_mode -and $fields.maximum_file_size)
    $outputPresent = -not [string]::IsNullOrEmpty($text.Trim())
    $explicitInactive = [bool]$fields.status_inactive
    $safeToCapture = ($Result.exit_code -eq 0 -and $outputPresent -and
        $fields.session_name -and -not $explicitInactive)
    $formatStatus = if ($Result.exit_code -ne 0) { 'command_failed' }
        elseif (-not $outputPresent) { 'empty_output' }
        elseif (-not $fields.session_name) { 'missing_session_name' }
        elseif ($explicitInactive) { 'reported_inactive' }
        elseif ($formatCompatible) { 'compatible' }
        else { 'unknown_labels' }
    return [ordered]@{
        exit_code = $Result.exit_code
        output_present = $outputPresent
        safe_to_capture = $safeToCapture
        format_status = $formatStatus
        format_compatible = $formatCompatible
        start_time_utc = $startTimeUtc
        field_presence = $fields
    }
}

function Get-PostStopDiagnostics([object]$Result, [object]$StopResult) {
    $text = [string]$Result.text
    $active = ($text -match '(?im)status\s*[:=]\s*running\b')
    $stopSucceeded = ([int]$StopResult.exit_code -eq 0)
    $exactTransientHresult = (@(-2144337918, 2150629378) -contains [int64]$Result.exit_code)
    $absenceVerified = ($stopSucceeded -and $exactTransientHresult -and -not $active)
    return [ordered]@{
        exit_code = $Result.exit_code
        hresult = if ($exactTransientHresult) { '0x80300002' } else { $null }
        output_present = (-not [string]::IsNullOrEmpty($text.Trim()))
        stop_succeeded = $stopSucceeded
        absence_verified = $absenceVerified
        active_marker = $active
        format_status = if (-not $stopSucceeded) { 'stop_failed' }
            elseif ($active) { 'still_active' }
            elseif ($exactTransientHresult) { 'absence_confirmed' }
            elseif ($Result.exit_code -eq 0) { 'unexpected_success' }
            else { 'unexpected_hresult' }
    }
}

function Get-OptionalSha256([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return $null
}

$output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) {
    if (-not (Test-Path -LiteralPath $output -PathType Container)) {
        throw "Diagnostic output root is not a directory: $output"
    }
    if (@(Get-ChildItem -LiteralPath $output -Force).Count -ne 0) {
        throw "Diagnostic output root must be a new empty directory: $output"
    }
}
else {
    New-Item -ItemType Directory -Path $output -Force | Out-Null
}

$preflightPath = Join-Path $output 'preflight.json'
$identityPath = Join-Path $output 'effective-identity.txt'
$fltmcPath = Join-Path $output 'fltmc.txt'
$summaryPath = Join-Path $output 'diagnostic-summary.json'
$preflightError = $null
$tracerptIdentity = $null
$logmanIdentity = $null
$controlledIdentity = $null
$freeBytesBeforeStart = $null
$hostName = $env:COMPUTERNAME
$powerShellVersion = $PSVersionTable.PSVersion.ToString()
$effectiveAdministrator = $false
$preflightPassed = $false

try {
    Assert-ExactSystem32Path $TracerptPath 'tracerpt.exe'
    Assert-ExactSystem32Path $LogmanPath 'logman.exe'
    Assert-ExactSystem32Path $ControlledProcessPath 'timeout.exe'
    Assert-Sha256 $ExpectedTracerptSha256 'expected tracerpt hash'
    Assert-Sha256 $ExpectedLogmanSha256 'expected logman hash'
    $expectedTracerptVersion = Normalize-FileVersion $ExpectedTracerptVersion
    $expectedLogmanVersion = Normalize-FileVersion $ExpectedLogmanVersion
    if (-not (Test-Path -LiteralPath $ParserPath -PathType Leaf)) { throw "Parser path is missing: $ParserPath" }
    if ($PSVersionTable.PSVersion.Major -ne 5 -or
        $PSVersionTable.PSEdition -ne 'Desktop') {
        throw 'Diagnostic requires Windows PowerShell Desktop 5.1'
    }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object -TypeName Security.Principal.WindowsPrincipal -ArgumentList $identity
    $effectiveAdministrator = $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $effectiveAdministrator) { throw 'Diagnostic requires an already elevated administrator console' }
    $tracerptIdentity = Get-ToolIdentity $TracerptPath $expectedTracerptVersion $ExpectedTracerptSha256
    $logmanIdentity = Get-ToolIdentity $LogmanPath $expectedLogmanVersion $ExpectedLogmanSha256
    $controlledIdentity = Get-ToolIdentity $ControlledProcessPath '' ''
    $driveName = (Split-Path -Path $output -Qualifier).TrimEnd(':')
    if ([string]::IsNullOrEmpty($driveName)) { throw 'Diagnostic output must be on a local drive with measurable free space' }
    $driveInfo = New-Object System.IO.DriveInfo -ArgumentList ($driveName + ':\')
    if (-not $driveInfo.IsReady -or $driveInfo.DriveType -ne [IO.DriveType]::Fixed) {
        throw 'Diagnostic destination must be on a ready fixed local drive'
    }
    $freeBytesBeforeStart = [int64]$driveInfo.AvailableFreeSpace
    if ($freeBytesBeforeStart -lt (64 * 1MB)) { throw 'Diagnostic destination has less than 64 MiB free headroom' }
    $whoami = Invoke-Native (Join-Path $systemRoot 'System32\whoami.exe') @('/all')
    $whoami.text | Set-Content -LiteralPath $identityPath -Encoding UTF8
    if ($whoami.exit_code -ne 0) { throw 'whoami /all preflight failed' }
    $fltmc = Invoke-Native (Join-Path $systemRoot 'System32\fltmc.exe') @()
    $fltmc.text | Set-Content -LiteralPath $fltmcPath -Encoding UTF8
    if ($fltmc.exit_code -ne 0) { throw 'fltmc.exe privilege preflight failed' }
    $preflightPassed = $true
}
catch {
    $preflightError = $_.Exception.Message
}

$preflightRecord = [ordered]@{
    format = 'VOX_FX_T18_TRACERPT_COMPATIBILITY_PREFLIGHT_V1'
    status = if ($preflightPassed) { 'PASS' } else { 'BLOCKED' }
    diagnostic_only = $true
    qualification_prohibited = $true
    production_tracerpt_version_pin = '10.0.26100.1'
    expected_diagnostic_tracerpt_version = $ExpectedTracerptVersion
    expected_diagnostic_tracerpt_sha256 = $ExpectedTracerptSha256.ToLowerInvariant()
    expected_diagnostic_logman_version = $ExpectedLogmanVersion
    expected_diagnostic_logman_sha256 = $ExpectedLogmanSha256.ToLowerInvariant()
    host = $hostName
    powershell_version = $powerShellVersion
    powershell_edition = $PSVersionTable.PSEdition
    effective_administrator = $effectiveAdministrator
    free_bytes_before_start = $freeBytesBeforeStart
    free_headroom_required_bytes = (64 * 1MB)
    tracerpt = $tracerptIdentity
    logman = $logmanIdentity
    controlled_process = $controlledIdentity
    diagnostic_approval_required = $false
    error = $preflightError
}
Write-JsonFile $preflightRecord $preflightPath

if (-not $preflightPassed) {
    $blockedSummary = [ordered]@{
        format = 'VOX_FX_T18_TRACERPT_COMPATIBILITY_DIAGNOSTIC_V1'
        status = 'BLOCKED'
        diagnostic_only = $true
        qualification_prohibited = $true
        preflight_passed = $false
        diagnostic_approval_required = $false
        cms_verification_performed_by_this_script = $false
        decoder_invoked = $false
        production_receipt_created = $false
        failure_reasons = @($preflightError)
    }
    Write-JsonFile $blockedSummary $summaryPath
    Write-Host "FX-T18 compatibility diagnostic BLOCKED; see $summaryPath"
    exit 2
}

$session = 'VOX-FX-T18-COMPAT-' + [guid]::NewGuid().ToString('N')
$etl = Join-Path $output 'compat.etl'
$decodedXml = Join-Path $output 'tracerpt-output.xml'
$tracerptSummary = Join-Path $output 'tracerpt-summary.txt'
$parserDiagnostic = Join-Path $output 'diagnostic-parser.json'
$createRaw = Join-Path $output 'logman-create.txt'
$activeQueryRaw = Join-Path $output 'logman-active-query.txt'
$processRaw = Join-Path $output 'controlled-process.txt'
$processReceipt = Join-Path $output 'controlled-process.json'
$stopRaw = Join-Path $output 'logman-stop.txt'
$postStopQueryRaw = Join-Path $output 'logman-post-stop-query.txt'
$tracerptRaw = Join-Path $output 'tracerpt-console.txt'
$parserRaw = Join-Path $output 'diagnostic-parser-console.txt'
$providerGuid = '{22FB2CD6-0E7B-422B-A0C7-2FAD1FD0E716}'
$created = $false
$activeQueryVerified = $false
$activeQueryDiagnostics = [ordered]@{}
$postStopDiagnostics = [ordered]@{}
$postStopAbsent = $false
$processStarted = $false
$processExitCode = -1
$processStartUtc = $null
$processCreationUtc = $null
$processEndUtc = $null
$summaryLossDiagnostics = [ordered]@{
    output_present = $false
    events_lost_field_present = $false
    buffers_lost_field_present = $false
    fields_present = $false
}
$create = [pscustomobject]@{ exit_code = -1; text = ''; arguments = @() }
$activeQuery = [pscustomobject]@{ exit_code = -1; text = ''; arguments = @() }
$stop = [pscustomobject]@{ exit_code = -1; text = ''; arguments = @() }
$postStopQuery = [pscustomobject]@{ exit_code = -1; text = ''; arguments = @() }
$decoder = [pscustomobject]@{ exit_code = -1; text = ''; arguments = @() }
$parser = [pscustomobject]@{ exit_code = -1; text = ''; arguments = @() }
$operationError = $null
$controlledProcess = $null

try {
    Assert-ToolIdentityUnchanged $LogmanPath $logmanIdentity $expectedLogmanVersion $logmanIdentity.sha256
    $create = Invoke-Native $LogmanPath @('create', 'trace', $session, '-o', $etl, '-f', 'bin', '-max', '16', '-p', $providerGuid, '0x10', '5', '-ets')
    Save-NativeResult $create $createRaw
    if ($create.exit_code -ne 0) { throw "logman create failed with exit code $($create.exit_code)" }
    $created = $true
    $activeQuery = Invoke-Native $LogmanPath @('query', $session, '-ets')
    Save-NativeResult $activeQuery $activeQueryRaw
    $activeQueryDiagnostics = Get-ActiveQueryDiagnostics $activeQuery $session
    if (-not $activeQueryDiagnostics.safe_to_capture) {
        throw "active logman query was not safely usable ($($activeQueryDiagnostics.format_status))"
    }
    $activeQueryVerified = $true
    $startControlUtc = $activeQueryDiagnostics.start_time_utc
    Assert-ToolIdentityUnchanged $ControlledProcessPath $controlledIdentity `
        $controlledIdentity.file_version_normalized $controlledIdentity.sha256
    $processStartUtc = [DateTime]::UtcNow.ToString('o')
    $controlledProcess = Start-Process -FilePath $ControlledProcessPath `
        -ArgumentList @('/t', [string]$ObservationSeconds, '/nobreak') `
        -PassThru -WindowStyle Hidden
    $processStarted = $true
    $controlledProcess.Refresh()
    try {
        $processCreationUtc = $controlledProcess.StartTime.ToUniversalTime().ToString('o')
    }
    catch {
        throw "controlled timeout process creation time was not readable: $($_.Exception.Message)"
    }
    $processOutput = @(
        'controlled_process=system32_timeout.exe'
        "observation_seconds=$ObservationSeconds"
        "launch_observed_utc=$processStartUtc"
        "creation_time_utc=$processCreationUtc"
        "process_id=$($controlledProcess.Id)"
    )
    if (-not $controlledProcess.WaitForExit((($ObservationSeconds + 5) * 1000))) {
        Stop-Process -Id $controlledProcess.Id -Force -ErrorAction Stop
        throw 'controlled timeout process exceeded the bounded wait'
    }
    $controlledProcess.Refresh()
    $processExitCode = [int]$controlledProcess.ExitCode
    $processEndUtc = [DateTime]::UtcNow.ToString('o')
    $processOutput += "exit_code=$processExitCode"
    $processOutput += "ended_utc=$processEndUtc"
    $processOutput | Set-Content -LiteralPath $processRaw -Encoding UTF8
    Write-JsonFile ([ordered]@{
        format = 'VOX_FX_T18_CONTROLLED_PROCESS_RECEIPT_V1'
        pid = [int]$controlledProcess.Id
        creation_time_utc = $processCreationUtc
        launch_observed_utc = $processStartUtc
        ended_utc = $processEndUtc
        exit_code = $processExitCode
    }) $processReceipt
}
catch {
    $operationError = $_.Exception.Message
}
finally {
    if ($controlledProcess -and -not $controlledProcess.HasExited) {
        try {
            Stop-Process -Id $controlledProcess.Id -Force -ErrorAction Stop
            $processEndUtc = [DateTime]::UtcNow.ToString('o')
            $processRawAppend = @('cleanup=controlled_process_stopped_after_probe_failure', "ended_utc=$processEndUtc")
            $processRawAppend | Add-Content -LiteralPath $processRaw -Encoding UTF8
        }
        catch {
            $operationError = if ($operationError) { "$operationError; controlled process cleanup: $($_.Exception.Message)" } else { "controlled process cleanup: $($_.Exception.Message)" }
        }
    }
    # One stop and one absence query are retained even when create/query failed.
    # Direct -ets sessions have no applicable removal operation; this script
    # never issues one and never retries a failed control command.
    $stop = Invoke-Native $LogmanPath @('stop', $session, '-ets')
    Save-NativeResult $stop $stopRaw
    $postStopQuery = Invoke-Native $LogmanPath @('query', $session, '-ets')
    Save-NativeResult $postStopQuery $postStopQueryRaw
    $postStopDiagnostics = Get-PostStopDiagnostics $postStopQuery $stop
    $postStopAbsent = $postStopDiagnostics.absence_verified
}

$etlFileCount = 0
$rotationCount = 0
$bytesWritten = 0
$truncationDetected = $false
if (Test-Path -LiteralPath $etl -PathType Leaf) {
    $etlInfo = Get-Item -LiteralPath $etl
    $etlFiles = @(Get-ChildItem -LiteralPath $output -File | Where-Object {
        $_.Extension -ieq '.etl' -or $_.Name.StartsWith('compat.etl.', [StringComparison]::OrdinalIgnoreCase)
    })
    $etlFileCount = [int]$etlFiles.Count
    $rotationCount = [Math]::Max(0, $etlFileCount - 1)
    $bytesWritten = [int64]$etlInfo.Length
    $truncationDetected = $bytesWritten -ge (16 * 1MB) -or $etlFileCount -ne 1
}
$collectionReady = ($create.exit_code -eq 0 -and $activeQueryVerified -and $processStarted -and
    $processExitCode -eq 0 -and $stop.exit_code -eq 0 -and
    $postStopDiagnostics.active_marker -ne $true -and
    (Test-Path -LiteralPath $etl -PathType Leaf) -and $bytesWritten -gt 0 -and
    $etlFileCount -eq 1 -and $rotationCount -eq 0 -and -not $truncationDetected)

$decoderReady = $false
$parserFieldsReady = $false
$parserDiagnosticValue = $null
if ($collectionReady) {
    try {
        Assert-ToolIdentityUnchanged $TracerptPath $tracerptIdentity $expectedTracerptVersion $tracerptIdentity.sha256
        $decoder = Invoke-Native $TracerptPath @($etl, '-o', $decodedXml, '-of', 'XML', '-lr', '-summary', $tracerptSummary, '-y')
        Save-NativeResult $decoder $tracerptRaw
        $decoderReady = ($decoder.exit_code -eq 0 -and
            (Test-Path -LiteralPath $decodedXml -PathType Leaf) -and
            (Test-Path -LiteralPath $tracerptSummary -PathType Leaf))
        if ($decoderReady) {
            $summaryText = Get-Content -LiteralPath $tracerptSummary -Raw
            $eventsLostFieldPresent = ($summaryText -match '(?im)^\s*(?:(?:total|number\s+of)\s+)?events?\s+lost\s*(?:(?::|=)\s*|\s+)[0-9]')
            $buffersLostFieldPresent = ($summaryText -match '(?im)^\s*(?:(?:total|number\s+of)\s+)?(?:log\s+|real\s+time\s+)?buffers?\s+lost\s*(?:(?::|=)\s*|\s+)[0-9]')
            $summaryLossDiagnostics = [ordered]@{
                output_present = (-not [string]::IsNullOrEmpty($summaryText.Trim()))
                events_lost_field_present = $eventsLostFieldPresent
                buffers_lost_field_present = $buffersLostFieldPresent
                fields_present = ($eventsLostFieldPresent -and $buffersLostFieldPresent)
            }
            $parser = Invoke-Native $Python @(
                $ParserPath, '--etl', $etl,
                '--diagnostic-decoded-xml', $decodedXml,
                '--diagnostic-summary', $tracerptSummary,
                '--diagnostic-controlled-process', $processReceipt,
                '--diagnostic-output', $parserDiagnostic)
            Save-NativeResult $parser $parserRaw
            if ($parser.exit_code -eq 0 -and (Test-Path -LiteralPath $parserDiagnostic -PathType Leaf)) {
                try {
                    $parserDiagnosticValue = Get-Content -LiteralPath $parserDiagnostic -Raw | ConvertFrom-Json
                    $parserFieldsReady = ($parserDiagnosticValue.format -eq 'VOX_FX_T18_TRACERPT_COMPATIBILITY_DIAGNOSTIC_V1' -and
                        $parserDiagnosticValue.diagnostic_only -eq $true -and
                        $parserDiagnosticValue.qualification_prohibited -eq $true -and
                        $parserDiagnosticValue.process_event_shape_valid -eq $true -and
                        $parserDiagnosticValue.controlled_process_binding_valid -eq $true -and
                        [int]$parserDiagnosticValue.controlled_start_match_count -eq 1 -and
                        [int]$parserDiagnosticValue.controlled_stop_match_count -eq 1 -and
                        [int]$parserDiagnosticValue.events_received -gt 0 -and
                        [int]$parserDiagnosticValue.process_start_events -gt 0 -and
                        [int]$parserDiagnosticValue.process_stop_events -gt 0 -and
                        [int]$parserDiagnosticValue.lost_events -eq 0 -and
                        [int]$parserDiagnosticValue.buffer_loss_events -eq 0 -and
                        $null -ne $parserDiagnosticValue.eventtrace_header -and
                        [int]$parserDiagnosticValue.eventtrace_header.max_file_size_mb -eq 16 -and
                        [int]$parserDiagnosticValue.eventtrace_header.log_file_mode -eq 0 -and
                        [int]$parserDiagnosticValue.eventtrace_header.buffers_written -gt 0 -and
                        [int]$parserDiagnosticValue.eventtrace_header.events_lost -eq 0 -and
                        [int]$parserDiagnosticValue.eventtrace_header.buffers_lost -eq 0)
                }
                catch {
                    $operationError = if ($operationError) { "$operationError; diagnostic parser JSON: $($_.Exception.Message)" } else { "diagnostic parser JSON: $($_.Exception.Message)" }
                }
            }
        }
    }
    catch {
        $operationError = if ($operationError) { "$operationError; decoder preflight: $($_.Exception.Message)" } else { "decoder preflight: $($_.Exception.Message)" }
    }
}

$failures = @()
if ($operationError) { $failures += $operationError }
if (-not $activeQueryVerified) {
    $failures += "active logman query was not safely usable ($($activeQueryDiagnostics.format_status))"
}
if (-not $postStopAbsent) {
    $failures += "post-stop logman query did not prove absence ($($postStopDiagnostics.format_status))"
}
if (-not $collectionReady) { $failures += 'bounded ETW collection did not produce a clean single retained ETL' }
if ($collectionReady -and -not $decoderReady) { $failures += 'installed tracerpt did not produce XML and summary' }
if ($decoderReady -and -not $parserFieldsReady) { $failures += 'production XML/summary parser field, loss counter, or controlled-process binding diagnostic failed' }
$diagnosticStatus = if ($collectionReady -and $postStopAbsent -and $decoderReady -and $parserFieldsReady) {
    'COMPATIBILITY_EVIDENCE_READY'
}
else {
    'BLOCKED'
}

$sanitizedSummary = [ordered]@{
    format = 'VOX_FX_T18_TRACERPT_COMPATIBILITY_DIAGNOSTIC_V1'
    status = $diagnosticStatus
    diagnostic_only = $true
    qualification_prohibited = $true
    preflight_passed = $preflightPassed
    production_receipt_created = $false
    diagnostic_approval_required = $false
    cms_verification_performed_by_this_script = $false
    expected_tracerpt_version = $ExpectedTracerptVersion
    expected_tracerpt_sha256 = $ExpectedTracerptSha256.ToLowerInvariant()
    expected_logman_version = $ExpectedLogmanVersion
    expected_logman_sha256 = $ExpectedLogmanSha256.ToLowerInvariant()
    production_tracerpt_version_pin = '10.0.26100.1'
    etl_sha256 = Get-OptionalSha256 $etl
    decoded_xml_sha256 = Get-OptionalSha256 $decodedXml
    tracerpt_summary_sha256 = Get-OptionalSha256 $tracerptSummary
    diagnostic_parser_sha256 = Get-OptionalSha256 $parserDiagnostic
    controlled_process_receipt_sha256 = Get-OptionalSha256 $processReceipt
    measured_tracerpt_version = $tracerptIdentity.file_version_normalized
    measured_tracerpt_sha256 = $tracerptIdentity.sha256
    tracerpt_signature_status = $tracerptIdentity.signature_status
    tracerpt_signer_subject = $tracerptIdentity.signer_subject
    measured_logman_version = $logmanIdentity.file_version_normalized
    measured_logman_sha256 = $logmanIdentity.sha256
    logman_signature_status = $logmanIdentity.signature_status
    logman_signer_subject = $logmanIdentity.signer_subject
    controlled_process = 'system32_timeout.exe'
    observation_seconds = $ObservationSeconds
    free_bytes_before_start = $freeBytesBeforeStart
    create_exit_code = $create.exit_code
    active_query_exit_code = $activeQuery.exit_code
    active_query_verified = $activeQueryVerified
    active_query_format_status = $activeQueryDiagnostics.format_status
    active_query_format_compatible = $activeQueryDiagnostics.format_compatible
    active_query_output_present = $activeQueryDiagnostics.output_present
    active_query_fields = $activeQueryDiagnostics.field_presence
    controlled_process_started = $processStarted
    controlled_process_exit_code = $processExitCode
    stop_exit_code = $stop.exit_code
    post_stop_query_exit_code = $postStopQuery.exit_code
    post_stop_query_hresult = $postStopDiagnostics.hresult
    post_stop_absence_verified = $postStopAbsent
    post_stop_format_status = $postStopDiagnostics.format_status
    post_stop_output_present = $postStopDiagnostics.output_present
    post_stop_active_marker = $postStopDiagnostics.active_marker
    summary_loss_fields = $summaryLossDiagnostics
    etl_file_count = $etlFileCount
    bytes_written = $bytesWritten
    rotation_count = $rotationCount
    truncation_detected = $truncationDetected
    decoder_invoked = ($collectionReady)
    decoder_exit_code = $decoder.exit_code
    decoder_outputs_present = $decoderReady
    parser_fields_and_counters_validated = $parserFieldsReady
    controlled_process_binding_valid = if ($parserDiagnosticValue) { [bool]$parserDiagnosticValue.controlled_process_binding_valid } else { $false }
    controlled_pid_start_event_count = if ($parserDiagnosticValue) { [int]$parserDiagnosticValue.controlled_pid_start_event_count } else { $null }
    controlled_pid_stop_event_count = if ($parserDiagnosticValue) { [int]$parserDiagnosticValue.controlled_pid_stop_event_count } else { $null }
    controlled_start_match_count = if ($parserDiagnosticValue) { [int]$parserDiagnosticValue.controlled_start_match_count } else { $null }
    controlled_stop_match_count = if ($parserDiagnosticValue) { [int]$parserDiagnosticValue.controlled_stop_match_count } else { $null }
    controlled_lifecycle_order_valid = if ($parserDiagnosticValue) { [bool]$parserDiagnosticValue.controlled_lifecycle_order_valid } else { $false }
    events_received = if ($parserDiagnosticValue) { [int]$parserDiagnosticValue.events_received } else { $null }
    lost_events = if ($parserDiagnosticValue) { [int]$parserDiagnosticValue.lost_events } else { $null }
    buffer_loss_events = if ($parserDiagnosticValue) { [int]$parserDiagnosticValue.buffer_loss_events } else { $null }
    failure_reasons = $failures
}
Write-JsonFile $sanitizedSummary $summaryPath
Write-Host "FX-T18 tracerpt compatibility diagnostic $diagnosticStatus; see $summaryPath"
if ($diagnosticStatus -eq 'COMPATIBILITY_EVIDENCE_READY') {
    exit 0
}
exit 2
