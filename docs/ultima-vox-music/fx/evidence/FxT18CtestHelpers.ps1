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
