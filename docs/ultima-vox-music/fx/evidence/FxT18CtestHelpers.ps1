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

function Test-FxT18NativeFailureText([string]$Text, [string]$SelectedTestNumber, [bool]$RequireCanonicalListing = $false) {
    $marker = 'CPU_MEASUREMENT_INSUFFICIENT: raw GetThreadTimes deltas are too coarse for callback CPU attribution'
    $canonicalListingCount = 0
    foreach ($line in ($Text -split '\r?\n')) {
        $normalized = [regex]::Replace($line.Trim(), '^\s*\d+\s*:\s*', '')
        if ($normalized -notmatch '(?i)\b(?:assert(?:ion)?|fail(?:ed|ure|ing)?|error|regression|unexpected|native[ _-]?(?:failure|reason))\b') {
            continue
        }
        if ($normalized -ceq $marker -or
            $normalized -match '(?i)^FX JUCE runtime stress:\s*1\s+failing cases$' -or
            $normalized -match '(?i)^\d+% tests passed, \d+ tests failed out of \d+$' -or
            $normalized -match '(?i)^The following tests FAILED:?$' -or
            $normalized -match '(?i)^ctest(?:\.exe)?\s*:\s*Errors while running CTest$') {
            continue
        }
        if ($normalized -match '(?i)^(?:\d+\s*/\s*\d+\s+)?Test\s+#(\d+):.*(?:\*{3})?(?:Failed|Passed|Timeout)\b') {
            $resultNumber = $Matches[1]
            if ($normalized -match '(?i)suite_fx_runtime_stress_tests' -and
                $resultNumber -eq $SelectedTestNumber) {
                continue
            }
            return $true
        }
        if ($normalized -match '(?i)^(\d+)\s*-\s*(\S+)\s+\(Failed\)(?:\s+suite)?\s*$') {
            if ($Matches[1] -eq $SelectedTestNumber -and
                $Matches[2] -ceq 'suite_fx_runtime_stress_tests') {
                $canonicalListingCount++
                if ($canonicalListingCount -gt 1) { return $true }
                continue
            }
            return $true
        }
        return $true
    }
    return $RequireCanonicalListing -and $canonicalListingCount -ne 1
}

function Test-CpuOnlyNativeEvidence([string]$RawLog, [string]$JunitPath) {
    $marker = 'CPU_MEASUREMENT_INSUFFICIENT: raw GetThreadTimes deltas are too coarse for callback CPU attribution'
    if (-not (Test-Path -LiteralPath $RawLog -PathType Leaf) -or
        -not (Test-Path -LiteralPath $JunitPath -PathType Leaf)) { return $false }
    try {
        $raw = Get-Content -LiteralPath $RawLog -Raw
        $markerRegex = [regex]::Escape($marker)
        $markerLines = @($raw -split '\r?\n' | ForEach-Object { $_.Trim() } |
            Where-Object { $_ -match $markerRegex })
        $prefixedMarkers = @($markerLines | Where-Object {
            $_ -match ('^\d+\s*:\s*' + $markerRegex + '$')
        }).Count
        $plainMarkers = @($markerLines | Where-Object { $_ -ceq $marker }).Count
        if ($markerLines.Count -ne 2 -or $prefixedMarkers -ne 1 -or $plainMarkers -ne 1) { return $false }
        $resultLines = @([regex]::Matches(
            $raw, '(?im)^\s*(?:\d+\s*/\s*\d+\s+)?Test\s+#\d+:[^\r\n]*(?:\*{3})?(?:Failed|Passed|Timeout)\b') |
            ForEach-Object { $_.Value })
        if ($resultLines.Count -ne 1 -or
            $resultLines[0] -notmatch '(?i)suite_fx_runtime_stress_tests' -or
            $resultLines[0] -notmatch '(?i)(?:\*{3})?Failed\b') { return $false }
        if ($resultLines[0] -notmatch '(?i)(?:^|\s)(?:\d+\s*/\s*\d+\s+)?Test\s+#(\d+):') {
            return $false
        }
        $selectedTestNumber = $Matches[1]

        $summaryCount = 0
        foreach ($line in ($raw -split '\r?\n')) {
            $normalized = [regex]::Replace($line.Trim(), '^\s*\d+\s*:\s*', '')
            if ($normalized -match '(?i)^FX JUCE runtime stress:\s*(\d+)\s+failing cases$') {
                $summaryCount++
                if ([int]$Matches[1] -ne 1) { return $false }
            }
        }
        if ($summaryCount -eq 0 -or
            (Test-FxT18NativeFailureText $raw $selectedTestNumber $true)) { return $false }

        $junit = [xml](Get-Content -LiteralPath $JunitPath -Raw)
        $testcases = @($junit.SelectNodes('//testcase'))
        if ($testcases.Count -ne 1) { return $false }
        $testcase = $testcases[0]
        if ([string]$testcase.GetAttribute('name') -ne 'suite_fx_runtime_stress_tests' -and
            [string]$testcase.GetAttribute('classname') -ne 'suite_fx_runtime_stress_tests') { return $false }
        $failures = @($testcase.SelectNodes('./failure'))
        if ($failures.Count -ne 1 -or
            @($testcase.SelectNodes('./error')).Count -ne 0 -or
            @($testcase.SelectNodes('./skipped')).Count -ne 0 -or
            @($testcase.SelectNodes('./system-err')).Count -ne 0) { return $false }
        if ([string]$failures[0].GetAttribute('message') -cne 'Failed' -or
            ([string]$failures[0].GetAttribute('type') -cne '' -and
             [string]$failures[0].GetAttribute('type') -cne 'Failed') -or
            ([string]$failures[0].InnerText.Trim() -ne '' -and
             [string]$failures[0].InnerText.Trim() -cne 'Failed')) { return $false }
        $systemOut = @($testcase.SelectNodes('./system-out'))
        if ($systemOut.Count -ne 1) { return $false }
        $systemOutText = [string]$systemOut[0].InnerText
        if ([regex]::Matches($systemOutText, $markerRegex).Count -ne 1) { return $false }
        $junitSummaryCount = 0
        foreach ($line in ($systemOutText -split '\r?\n')) {
            $normalized = [regex]::Replace($line.Trim(), '^\s*\d+\s*:\s*', '')
            if ($normalized -match '(?i)^FX JUCE runtime stress:\s*(\d+)\s+failing cases$') {
                $junitSummaryCount++
                if ([int]$Matches[1] -ne 1) { return $false }
            }
        }
        if ($junitSummaryCount -eq 0 -or (Test-FxT18NativeFailureText $systemOutText $selectedTestNumber)) { return $false }
        return $true
    } catch { return $false }
}
