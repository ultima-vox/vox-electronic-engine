function Test-FxT18CtestTimeout {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$RawLog,
        [string]$JunitPath = ''
    )

    # CTest -VV prints "Test timeout computed to be: N" for passing tests.
    # Only the explicit marker or a structured JUnit timeout result counts.
    if ((Test-Path -LiteralPath $RawLog -PathType Leaf) -and
        (Select-String -LiteralPath $RawLog -Pattern '\*\*\*Timeout(?:\s|$)' -Quiet)) {
        return $true
    }
    if (-not [string]::IsNullOrEmpty($JunitPath) -and
        (Test-Path -LiteralPath $JunitPath -PathType Leaf)) {
        try {
            [xml]$junit = Get-Content -LiteralPath $JunitPath -Raw
            foreach ($testcase in @($junit.SelectNodes('//testcase'))) {
                $status = [string]$testcase.GetAttribute('status')
                $result = [string]$testcase.GetAttribute('result')
                if ($status -match '(?i)^(?:timeout|timed[ -]?out)$' -or
                    $result -match '(?i)^(?:timeout|timed[ -]?out)$') {
                    return $true
                }
                foreach ($failure in @($testcase.SelectNodes('./failure|./error'))) {
                    # CTest's JUnit writer emits status="fail" and
                    # <failure message="Timeout"/> for a real timeout.
                    # Do not inspect arbitrary failure text: a test message
                    # containing the word timeout is not a CTest timeout.
                    $type = [string]$failure.GetAttribute('type')
                    $message = [string]$failure.GetAttribute('message')
                    if ($type -match '(?i)^(?:timeout|timed[ -]?out)$' -or
                        $message -match '(?i)^(?:timeout|timed[ -]?out)$') {
                        return $true
                    }
                }
            }
        } catch {
            # A malformed/missing JUnit file is an artifact failure, not a
            # timeout. The orchestrator's auditor will preserve that evidence.
        }
    }
    return $false
}

function Get-FxT18CtestOutcome {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [int]$ExitCode,
        [Parameter(Mandatory = $true)]
        [bool]$TimedOut
    )

    if ($TimedOut) { return 'timeout_failure' }
    if ($ExitCode -ne 0) { return 'test_failure' }
    return 'diagnostic_pass'
}

function Test-CpuOnlyFailure([string]$Report, [string]$CpuStatus) {
    if ($CpuStatus -ne 'CPU_MEASUREMENT_INSUFFICIENT' -or
        -not (Test-Path -LiteralPath $Report -PathType Leaf)) { return $false }
    $cases = @(Get-Content -LiteralPath $Report | Where-Object { $_ -match '^case\t' })
    $functional = @(Get-Content -LiteralPath $Report | Where-Object { $_ -match '^functional\t' })
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

function Test-CpuOnlyNativeEvidence([string]$RawLog, [string]$JunitPath) {
    $marker = 'CPU_MEASUREMENT_INSUFFICIENT: raw GetThreadTimes deltas are too coarse for callback CPU attribution'
    if (-not (Test-Path -LiteralPath $RawLog -PathType Leaf) -or
        -not (Test-Path -LiteralPath $JunitPath -PathType Leaf)) { return $false }
    try {
        $raw = Get-Content -LiteralPath $RawLog -Raw
        if ([regex]::Matches($raw, [regex]::Escape($marker)).Count -ne 1) { return $false }
        $resultLines = @([regex]::Matches(
            $raw, '(?im)^\s*(?:\d+\s*/\s*\d+\s+)?Test\s+#\d+:[^\r\n]*(?:\*{3})?(?:Failed|Passed|Timeout)\b') | ForEach-Object { $_.Value })
        if ($resultLines.Count -ne 1 -or
            $resultLines[0] -notmatch '(?i)suite_fx_runtime_stress_tests' -or
            $resultLines[0] -notmatch '(?i)(?:\*{3})?Failed\b') { return $false }
        if ($raw -match 'Assertion failed|FINITE_OUTPUT|MIDI_NOT_CONSUMED|PEAK_RANGE|DC_OFFSET|MAX_GRAINS|VOICE8_GRAIN_INVARIANT') {
            return $false
        }
        [xml]$junit = Get-Content -LiteralPath $JunitPath -Raw
        $testcases = @($junit.SelectNodes('//testcase'))
        if ($testcases.Count -ne 1) { return $false }
        $testcase = $testcases[0]
        if ([string]$testcase.GetAttribute('name') -ne 'suite_fx_runtime_stress_tests' -and
            [string]$testcase.GetAttribute('classname') -ne 'suite_fx_runtime_stress_tests') { return $false }
        if (@($testcase.SelectNodes('./failure')).Count -ne 1 -or
            @($testcase.SelectNodes('./error')).Count -ne 0 -or
            @($testcase.SelectNodes('./skipped')).Count -ne 0) { return $false }
        $reasonParts = @()
        foreach ($node in @($testcase.SelectNodes('./failure|./error|./system-out|./system-err'))) {
            $reasonParts += [string]$node.GetAttribute('message')
            $reasonParts += [string]$node.GetAttribute('type')
            $reasonParts += [string]$node.InnerText
        }
        $reason = $reasonParts -join ' '
        if ([regex]::Matches($reason, [regex]::Escape($marker)).Count -ne 1 -or
            $reason -match 'Assertion failed|FINITE_OUTPUT|MIDI_NOT_CONSUMED|PEAK_RANGE|DC_OFFSET|MAX_GRAINS|VOICE8_GRAIN_INVARIANT') {
            return $false
        }
        return $true
    } catch { return $false }
}
