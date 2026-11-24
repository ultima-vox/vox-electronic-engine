[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [ValidateSet('Capture', 'OfflineSynthetic')]
    [string]$Mode = 'Capture',
    [ValidateRange(30, 86400)]
    [int]$PreSeconds = 30,
    [ValidateRange(1, 86400)]
    [int]$DuringSeconds = 1,
    [ValidateRange(30, 86400)]
    [int]$PostSeconds = 30,
    [string]$IdentityPolicyPath = '',
    [string]$IdentityPolicySignaturePath = '',
    [string]$LeaseReceiptPath = '',
    [string]$LeaseReceiptSignaturePath = '',
    [string]$EventLossReceiptPath = '',
    [string]$EventLossReceiptSignaturePath = '',
    [string]$ExecutionBindingPath = '',
    [string]$ExecutionBindingSignaturePath = '',
    [string]$PrivateInventoryPath = '',
    [string]$OwnerCertificateThumbprint = '',
    [switch]$FixtureMode,
    [switch]$SyntheticCompetitor,
    [switch]$SyntheticUnknownPid,
    [switch]$SimulateProviderFailure
)

# Safe host-wide observation only. This script never starts, stops, kills,
# disables or reconfigures a service or process. It reads process identities,
# writes raw identity records only to the owner-selected private inventory,
# and subscribes to the Windows process start/stop indication providers. The
# independently owner-signed ETW loss receipt is the only source accepted for
# a production loss claim; WMI events are an observation stream, not proof of
# zero loss by themselves.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$script:FixtureMode = [bool]$FixtureMode
$script:QualificationProhibited = ($Mode -eq 'OfflineSynthetic' -or $script:FixtureMode)
$script:IdentityPolicy = $null
$script:SeenPidIdentities = @{}
$script:KnownPidRecords = @{}
$script:RawSamples = @()
$script:RawEvents = @()
$script:PublicSamples = @()
$script:PublicEvents = @()
$script:MonitorGaps = @()
$script:UnrecognizedCount = 0
$script:UnknownCount = 0
$script:PidReuseCount = 0
$script:QueryErrors = @()
$script:QueryErrorCodes = @()
$script:ObservedServiceHashes = @{}
$script:SampleOrdinal = 0

function Write-WitnessJson([object]$Value, [string]$Path) {
    $parent = Split-Path -Parent $Path
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    $Value | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-UtcNowText {
    return [DateTime]::UtcNow.ToString('o')
}

function Get-MonotonicSeconds {
    return ([Diagnostics.Stopwatch]::GetTimestamp() / [double][Diagnostics.Stopwatch]::Frequency)
}

function Get-OptionalProperty([object]$Object, [string]$Name, [object]$Default) {
    if ($null -eq $Object) { return $Default }
    if ($Object -is [System.Collections.IDictionary] -and $Object.Contains($Name)) {
        if ($null -ne $Object[$Name]) { return $Object[$Name] }
        return $Default
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) { return $Default }
    return $property.Value
}

function Get-Sha256Text([string]$Text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text))) -replace '-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}

function Get-ObjectSha256([object]$Value) {
    return Get-Sha256Text (($Value | ConvertTo-Json -Depth 30 -Compress))
}

function Normalize-Thumbprint([string]$Thumbprint) {
    return (($Thumbprint -replace '[^0-9a-fA-F]', '').ToUpperInvariant())
}

function Get-HashOrUnknown([string]$Path) {
    if ([string]::IsNullOrEmpty($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return '<unknown>'
    }
    try { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
    catch { return '<unknown>' }
}

function New-SafePolicy([string]$Format, [string]$PolicyId) {
    return [ordered]@{
        format = $Format
        policy_id = $PolicyId
        host = '<missing>'
        baseline_id = '<missing>'
        valid_from_utc = '<missing>'
        valid_until_utc = '<missing>'
        runner_service_identity_sha256 = @()
        approved_processes = @()
        denied_process_name_regex = @()
        owner_approved = $false
    }
}

function Get-VerifiedOwnerSignature(
    [string]$DataPath,
    [string]$SignaturePath,
    [string]$ExpectedThumbprint,
    [string]$AttestationName
) {
    if (-not $DataPath -or -not $SignaturePath -or -not $ExpectedThumbprint) {
        return [ordered]@{
            verified = $false; data_hash = '<missing>'; signature_hash = '<missing>'
            signer_thumbprint = '<missing>'; verification_method = '<missing>'
            error_code = ($AttestationName + '_SIGNATURE_MISSING')
            error = ($AttestationName + ' owner detached-CMS signature is missing')
        }
    }
    try {
        if (-not (Test-Path -LiteralPath $DataPath -PathType Leaf) -or
            -not (Test-Path -LiteralPath $SignaturePath -PathType Leaf)) {
            throw 'attestation or signature file is missing'
        }
        $expected = Normalize-Thumbprint $ExpectedThumbprint
        if ($expected -notmatch '^[0-9A-F]{40}$') { throw 'owner certificate thumbprint is not SHA-1' }
        $dataBytes = [IO.File]::ReadAllBytes($DataPath)
        $signatureBytes = [IO.File]::ReadAllBytes($SignaturePath)
        try { Add-Type -AssemblyName System.Security.Cryptography.Pkcs -ErrorAction Stop } catch { Add-Type -AssemblyName System.Security -ErrorAction Stop }
        $contentInfo = New-Object -TypeName System.Security.Cryptography.Pkcs.ContentInfo -ArgumentList (,$dataBytes)
        $signedCms = [System.Security.Cryptography.Pkcs.SignedCms]::new($contentInfo, $true)
        $signedCms.Decode($signatureBytes)
        if ($signedCms.SignerInfos.Count -ne 1) { throw 'detached signature does not have exactly one signer' }
        $certificate = $signedCms.SignerInfos[0].Certificate
        if ($null -eq $certificate) { throw 'detached signature has no signer certificate' }
        $actualThumbprint = Normalize-Thumbprint ([string]$certificate.Thumbprint)
        if ($actualThumbprint -cne $expected) { throw 'signer certificate thumbprint mismatch' }
        # The owner certificate chain and CMS signature are checked here. The
        # pinned thumbprint is supplied out-of-band by the owner runbook; it is
        # never read from the JSON being attested.
        $signedCms.CheckSignature($false)
        return [ordered]@{
            verified = $true
            data_hash = (Get-FileHash -LiteralPath $DataPath -Algorithm SHA256).Hash.ToLowerInvariant()
            signature_hash = (Get-FileHash -LiteralPath $SignaturePath -Algorithm SHA256).Hash.ToLowerInvariant()
            signer_thumbprint = $actualThumbprint
            verification_method = 'detached_cms_owner_certificate_chain_and_thumbprint'
            error_code = $null; error = $null
        }
    } catch {
        return [ordered]@{
            verified = $false; data_hash = '<invalid>'; signature_hash = '<invalid>'
            signer_thumbprint = '<invalid>'; verification_method = '<invalid>'
            error_code = ($AttestationName + '_SIGNATURE_INVALID')
            error = ($AttestationName + ' owner detached-CMS signature is unavailable or unverifiable')
        }
    }
}

function New-OfflinePolicy {
    return [ordered]@{
        format = 'VOX_FX_T18_IDENTITY_POLICY_V1'
        policy_id = 'offline-fixture-policy'
        host = 'offline-fixture'
        baseline_id = 'offline-fixture-baseline'
        valid_from_utc = '2026-11-21T23:00:00.0000000Z'
        valid_until_utc = '2026-11-22T03:00:00.0000000Z'
        owner_approved = $true
        runner_service_identity_sha256 = @('1' * 64, '2' * 64, '3' * 64, '4' * 64)
        approved_processes = @([ordered]@{
            process_name = 'Runner.Listener.exe'; role = 'runner'; allow_dynamic = $true
            executable_sha256 = ('5' * 64); service_identity_sha256 = ('1' * 64)
        })
        denied_process_name_regex = @(
            '^(devenv|msbuild|cmake|ctest|ninja|clang|gcc|python|cubase|reaper|ableton|flstudio|protools)'
        )
    }
}

function Get-IdentityPolicy {
    if (-not $IdentityPolicyPath) { return New-SafePolicy 'missing' '<missing>' }
    try {
        if (-not (Test-Path -LiteralPath $IdentityPolicyPath -PathType Leaf)) {
            return New-SafePolicy 'missing' '<missing>'
        }
        $raw = Get-Content -LiteralPath $IdentityPolicyPath -Raw | ConvertFrom-Json
        return [ordered]@{
            format = [string](Get-OptionalProperty $raw 'format' 'invalid')
            policy_id = [string](Get-OptionalProperty $raw 'policy_id' '<missing>')
            host = [string](Get-OptionalProperty $raw 'host' '<missing>')
            baseline_id = [string](Get-OptionalProperty $raw 'baseline_id' '<missing>')
            valid_from_utc = [string](Get-OptionalProperty $raw 'valid_from_utc' '<missing>')
            valid_until_utc = [string](Get-OptionalProperty $raw 'valid_until_utc' '<missing>')
            runner_service_identity_sha256 = @(Get-OptionalProperty $raw 'runner_service_identity_sha256' @())
            approved_processes = @(Get-OptionalProperty $raw 'approved_processes' @())
            denied_process_name_regex = @(Get-OptionalProperty $raw 'denied_process_name_regex' @())
            owner_approved = [bool](Get-OptionalProperty $raw 'owner_approved' $false)
        }
    } catch { return New-SafePolicy 'invalid' '<invalid>' }
}

function Get-VerifiedLeaseReceipt([object]$SignatureStatus, [bool]$AllowFixtureUnsigned = $false) {
    if (-not $LeaseReceiptPath) {
        return [ordered]@{ verified = $false; receipt = $null; hash = '<missing>'; signature = $SignatureStatus
            error_code = 'LEASE_RECEIPT_MISSING'; error_category = 'ATTESTATION_MISSING'; error = 'owner lease receipt is missing' }
    }
    try {
        if (-not (Test-Path -LiteralPath $LeaseReceiptPath -PathType Leaf)) {
            throw 'lease receipt file is missing'
        }
        if (-not $AllowFixtureUnsigned -and -not [bool](Get-OptionalProperty $SignatureStatus 'verified' $false)) {
            return [ordered]@{ verified = $false; receipt = $null; hash = '<invalid>'; signature = $SignatureStatus
                error_code = [string](Get-OptionalProperty $SignatureStatus 'error_code' 'LEASE_RECEIPT_SIGNATURE_INVALID')
                error_category = 'ATTESTATION_INVALID'; error = 'owner-signed lease receipt is unavailable' }
        }
        $receipt = Get-Content -LiteralPath $LeaseReceiptPath -Raw | ConvertFrom-Json
        foreach ($field in @('format', 'verified', 'host', 'lease_id', 'owner', 'policy_sha256',
                             'valid_from_utc', 'valid_until_utc', 'verification_method')) {
            if ($null -eq (Get-OptionalProperty $receipt $field $null)) { throw "missing $field" }
        }
        if ($receipt.format -ne 'VOX_FX_T18_HOST_LEASE_RECEIPT_V1' -or
            $receipt.verified -ne $true -or
            $receipt.owner_approved -ne $true -or
            $receipt.attestation_source -ne 'owner_controlled_offline' -or
            $receipt.verification_method -ne 'owner_signed_detached_cms') {
            throw 'lease receipt is not an owner-controlled signed receipt'
        }
        if ([string]$receipt.host -ne [string]$env:COMPUTERNAME -and -not $AllowFixtureUnsigned) {
            throw 'host mismatch'
        }
        $validFrom = [DateTime]::Parse([string]$receipt.valid_from_utc).ToUniversalTime()
        $validUntil = [DateTime]::Parse([string]$receipt.valid_until_utc).ToUniversalTime()
        if ($validUntil -le $validFrom -or $validUntil -lt [DateTime]::UtcNow) { throw 'expired or invalid lease interval' }
        if ([string]$receipt.policy_sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'lease policy hash is not SHA-256' }
        $hash = (Get-FileHash -LiteralPath $LeaseReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
        return [ordered]@{ verified = $true; receipt = $receipt; hash = $hash; signature = $SignatureStatus
            error_code = $null; error_category = $null; error = $null }
    } catch {
        return [ordered]@{ verified = $false; receipt = $null; hash = '<invalid>'; signature = $SignatureStatus
            error_code = 'LEASE_RECEIPT_INVALID'; error_category = 'ATTESTATION_INVALID'; error = 'owner lease receipt is malformed or unverifiable' }
    }
}

function Get-VerifiedEventLossReceipt([string]$CaptureStartUtc, [object]$SignatureStatus, [bool]$AllowFixtureUnsigned = $false) {
    if (-not $EventLossReceiptPath) {
        return [ordered]@{ verified = $false; receipt = $null; hash = '<missing>'; signature = $SignatureStatus
            error_code = 'EVENT_LOSS_RECEIPT_MISSING'; error_category = 'ATTESTATION_MISSING'; error = 'measurable ETW event-loss receipt is missing' }
    }
    try {
        if (-not (Test-Path -LiteralPath $EventLossReceiptPath -PathType Leaf)) { throw 'event-loss receipt file is missing' }
        if (-not $AllowFixtureUnsigned -and -not [bool](Get-OptionalProperty $SignatureStatus 'verified' $false)) {
            return [ordered]@{ verified = $false; receipt = $null; hash = '<invalid>'; signature = $SignatureStatus
                error_code = [string](Get-OptionalProperty $SignatureStatus 'error_code' 'EVENT_LOSS_RECEIPT_SIGNATURE_INVALID')
                error_category = 'ATTESTATION_INVALID'; error = 'owner-signed ETW loss receipt is unavailable' }
        }
        $receipt = Get-Content -LiteralPath $EventLossReceiptPath -Raw | ConvertFrom-Json
        foreach ($field in @('format', 'provider', 'measurement_method', 'coverage_complete', 'lost_events',
                             'buffer_loss_events', 'events_received', 'coverage_start_utc', 'coverage_end_utc',
                             'collector_artifact_path', 'collector_artifact_sha256')) {
            if ($null -eq (Get-OptionalProperty $receipt $field $null)) { throw "missing $field" }
        }
        if ($receipt.format -ne 'VOX_FX_T18_EVENT_LOSS_RECEIPT_V1' -or
            $receipt.provider -ne 'Microsoft-Windows-Kernel-Process/ETW' -or
            $receipt.measurement_method -ne 'ETW_PROCESS_TRACE_WITH_LOSS_COUNTERS' -or
            $receipt.coverage_complete -ne $true -or
            $receipt.lost_events -ne 0 -or $receipt.buffer_loss_events -ne 0 -or
            [int]$receipt.events_received -lt 0 -or
            [string]$receipt.collector_artifact_sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
            $receipt.owner_approved -ne $true -or
            $receipt.attestation_source -ne 'owner_controlled_offline' -or
            $receipt.verification_method -ne 'owner_signed_detached_cms') {
            throw 'ETW loss counters are incomplete or not owner-controlled'
        }
        $coverageStart = [DateTime]::Parse([string]$receipt.coverage_start_utc).ToUniversalTime()
        $coverageEnd = [DateTime]::Parse([string]$receipt.coverage_end_utc).ToUniversalTime()
        $captureStart = [DateTime]::Parse($CaptureStartUtc).ToUniversalTime()
        if ($coverageEnd -le $coverageStart -or $coverageStart -gt $captureStart) { throw 'invalid ETW coverage interval' }
        $collectorPath = [string]$receipt.collector_artifact_path
        if (-not [IO.Path]::IsPathRooted($collectorPath)) { $collectorPath = Join-Path (Split-Path -Parent $EventLossReceiptPath) $collectorPath }
        if (-not (Test-Path -LiteralPath $collectorPath -PathType Leaf)) { throw 'ETW collector artifact is missing' }
        $collectorHash = (Get-FileHash -LiteralPath $collectorPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($collectorHash -ine ([string]$receipt.collector_artifact_sha256).ToLowerInvariant()) { throw 'ETW collector artifact hash mismatch' }
        $hash = (Get-FileHash -LiteralPath $EventLossReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
        return [ordered]@{ verified = $true; receipt = $receipt; hash = $hash; signature = $SignatureStatus
            error_code = $null; error_category = $null; error = $null }
    } catch {
        return [ordered]@{ verified = $false; receipt = $null; hash = '<invalid>'; signature = $SignatureStatus
            error_code = 'EVENT_LOSS_RECEIPT_INVALID'; error_category = 'ATTESTATION_INVALID'; error = 'ETW event-loss receipt is malformed or unverifiable' }
    }
}

function Get-VerifiedExecutionBinding([object]$SignatureStatus, [bool]$AllowFixtureUnsigned = $false) {
    if (-not $ExecutionBindingPath) {
        return [ordered]@{ verified = $false; binding = $null; hash = '<missing>'; signature = $SignatureStatus
            error_code = 'EXECUTION_BINDING_MISSING'; error_category = 'ATTESTATION_MISSING'; error = 'trusted CTest execution binding is missing' }
    }
    try {
        if (-not (Test-Path -LiteralPath $ExecutionBindingPath -PathType Leaf)) { throw 'execution binding file is missing' }
        if (-not $AllowFixtureUnsigned -and -not [bool](Get-OptionalProperty $SignatureStatus 'verified' $false)) {
            return [ordered]@{ verified = $false; binding = $null; hash = '<invalid>'; signature = $SignatureStatus
                error_code = [string](Get-OptionalProperty $SignatureStatus 'error_code' 'EXECUTION_BINDING_SIGNATURE_INVALID')
                error_category = 'ATTESTATION_INVALID'; error = 'signed execution binding is unavailable' }
        }
        $binding = Get-Content -LiteralPath $ExecutionBindingPath -Raw | ConvertFrom-Json
        foreach ($field in @('format', 'binding_id', 'host', 'runner_job_id', 'source_head', 'source_tree',
                             'executable_path', 'executable_sha256', 'lease_id', 'policy_sha256', 'profiles', 'runner_receipts')) {
            if ($null -eq (Get-OptionalProperty $binding $field $null)) { throw "missing $field" }
        }
        if ($binding.format -ne 'VOX_FX_T18_EXECUTION_BINDING_V1' -or
            @($binding.runner_receipts).Count -ne 3 -or
            [string]$binding.executable_sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
            [string]$binding.policy_sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
            [string]$binding.source_head -notmatch '^[0-9a-fA-F]{40}$' -or
            [string]$binding.source_tree -notmatch '^[0-9a-fA-F]{40}$' -or
            @($binding.profiles).Count -ne 3 -or
            $binding.owner_approved -ne $true -or
            $binding.attestation_source -ne 'owner_controlled_offline' -or
            $binding.verification_method -ne 'owner_signed_detached_cms') { throw 'execution binding is not owner-controlled' }
        $profiles = @($binding.profiles)
        $names = @($profiles | ForEach-Object { [string]$_.profile } | Sort-Object -Unique)
        if (($names -join ',') -cne 'authored,routed,typical') { throw 'execution binding profiles are not exactly routed/authored/typical' }
        foreach ($profile in $profiles) {
            foreach ($field in @('profile', 'test_identity', 'test_start_utc', 'test_end_utc', 'ctest_exit_code')) {
                if ($null -eq (Get-OptionalProperty $profile $field $null)) { throw "binding profile missing $field" }
            }
            if ([string]$profile.test_identity -ne 'suite_fx_runtime_stress_tests') { throw 'binding test identity mismatch' }
            $start = [DateTime]::Parse([string]$profile.test_start_utc).ToUniversalTime()
            $end = [DateTime]::Parse([string]$profile.test_end_utc).ToUniversalTime()
            if ($end -le $start) { throw 'binding profile interval is not positive' }
        }
        $receiptNames = @($binding.runner_receipts | ForEach-Object { [string]$_.profile } | Sort-Object -Unique)
        if (($receiptNames -join ',') -cne 'authored,routed,typical') { throw 'binding runner receipts are not exactly routed/authored/typical' }
        foreach ($runnerReceipt in @($binding.runner_receipts)) {
            foreach ($field in @('profile', 'receipt_path', 'receipt_sha256')) {
                if ($null -eq (Get-OptionalProperty $runnerReceipt $field $null)) { throw "binding runner receipt missing $field" }
            }
            if ([string]$runnerReceipt.receipt_sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'binding runner receipt hash is not SHA-256' }
            if ([string]$runnerReceipt.profile -notin $names) { throw 'binding runner receipt profile is not recognized' }
            $receiptPath = [string]$runnerReceipt.receipt_path
            if (-not [IO.Path]::IsPathRooted($receiptPath)) { $receiptPath = Join-Path (Split-Path -Parent $ExecutionBindingPath) $receiptPath }
            if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf) -or
                (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash.ToLowerInvariant() -ine ([string]$runnerReceipt.receipt_sha256).ToLowerInvariant()) { throw 'binding runner receipt hash reconciliation failed' }
        }
        $hash = (Get-FileHash -LiteralPath $ExecutionBindingPath -Algorithm SHA256).Hash.ToLowerInvariant()
        return [ordered]@{ verified = $true; binding = $binding; hash = $hash; signature = $SignatureStatus
            error_code = $null; error_category = $null; error = $null }
    } catch {
        return [ordered]@{ verified = $false; binding = $null; hash = '<invalid>'; signature = $SignatureStatus
            error_code = 'EXECUTION_BINDING_INVALID'; error_category = 'ATTESTATION_INVALID'; error = 'trusted CTest execution binding is malformed or unverifiable' }
    }
}

function Test-ProcessAgainstPolicy([object]$Record) {
    $result = [ordered]@{ known = $true; approved = $false; pid_reuse = $false; identity_digest = '<unknown>'; reason = 'UNRECOGNIZED_PROCESS_IDENTITY' }
    $pidKey = [string]$Record.pid
    $digestInput = "$($Record.process_name)|$($Record.executable_sha256)|$($Record.start_time_utc)|$(@($Record.service_identity_sha256) -join ',')"
    $result.identity_digest = Get-Sha256Text $digestInput
    if ($Record.start_time_utc -eq '<unknown>' -or $Record.executable_sha256 -eq '<unknown>') {
        $result.known = $false; $result.reason = 'UNKNOWN_PROCESS_IDENTITY'
    }
    if ($script:SeenPidIdentities.ContainsKey($pidKey)) {
        $previous = $script:SeenPidIdentities[$pidKey]
        if ([string]$previous.start_time_utc -ne [string]$Record.start_time_utc -or
            [string]$previous.identity_digest -ne [string]$result.identity_digest) {
            $result.pid_reuse = $true; $result.reason = 'PID_REUSE_OR_DYNAMIC_IDENTITY_MISMATCH'
        }
    }
    $script:SeenPidIdentities[$pidKey] = [ordered]@{ start_time_utc = $Record.start_time_utc; identity_digest = $result.identity_digest }
    $script:KnownPidRecords[$pidKey] = $Record
    if (-not $result.known -or $result.pid_reuse) { return $result }
    foreach ($approved in @($script:IdentityPolicy.approved_processes)) {
        $name = [string](Get-OptionalProperty $approved 'process_name' '')
        $nameRegex = [string](Get-OptionalProperty $approved 'process_name_regex' '')
        $nameMatches = ($name -and $Record.process_name -ceq $name) -or ($nameRegex -and $Record.process_name -match $nameRegex)
        if (-not $nameMatches) { continue }
        $hash = [string](Get-OptionalProperty $approved 'executable_sha256' '')
        if ($hash -and $hash -notmatch '^<') {
            if ($Record.executable_sha256 -ine $hash) { continue }
        }
        $serviceHash = [string](Get-OptionalProperty $approved 'service_identity_sha256' '')
        if ($serviceHash -and @($Record.service_identity_sha256) -notcontains $serviceHash) { continue }
        $parentPid = Get-OptionalProperty $approved 'parent_pid' $null
        if ($null -ne $parentPid -and [int]$parentPid -ne [int]$Record.parent_pid) { continue }
        $exactPid = Get-OptionalProperty $approved 'pid' $null
        $exactStart = [string](Get-OptionalProperty $approved 'start_time_utc' '')
        if ($null -ne $exactPid -and [int]$exactPid -ne [int]$Record.pid) { continue }
        if ($exactStart -and $exactStart -ne $Record.start_time_utc) { continue }
        $dynamic = [bool](Get-OptionalProperty $approved 'allow_dynamic' $false)
        if (($null -eq $exactPid -and -not $dynamic) -or ($null -ne $exactPid -and $exactStart -and $exactStart -ne $Record.start_time_utc)) { continue }
        $result.approved = $true; $result.reason = $null
        return $result
    }
    return $result
}

function Get-ProcessStartUtc($Process) {
    try {
        return [System.Management.ManagementDateTimeConverter]::ToDateTime(
            [string]$Process.CreationDate).ToUniversalTime().ToString('o')
    } catch { return '<unknown>' }
}

function Get-ServiceIdentityMap {
    $map = @{}
    try {
        foreach ($service in @(Get-CimInstance -ClassName Win32_Service -ErrorAction Stop)) {
            $servicePid = [int](Get-OptionalProperty $service 'ProcessId' 0)
            if ($servicePid -gt 0) {
                if (-not $map.ContainsKey([string]$servicePid)) { $map[[string]$servicePid] = @() }
                $map[[string]$servicePid] += Get-Sha256Text ([string]$service.Name)
            }
        }
    } catch { return $null }
    return $map
}

function Get-ProcessRecord($Process, [hashtable]$ServiceMap) {
    $path = [string](Get-OptionalProperty $Process 'ExecutablePath' '')
    $serviceHashes = @()
    if ($null -ne $ServiceMap -and $ServiceMap.ContainsKey([string]$Process.ProcessId)) {
        $serviceHashes = @($ServiceMap[[string]$Process.ProcessId] | Sort-Object -Unique)
    }
    return [ordered]@{
        pid = [int]$Process.ProcessId
        process_name = [string]$Process.Name
        parent_pid = [int](Get-OptionalProperty $Process 'ParentProcessId' 0)
        start_time_utc = Get-ProcessStartUtc $Process
        executable_path = if ($path) { $path } else { '<unknown>' }
        executable_sha256 = Get-HashOrUnknown $path
        service_identity_sha256 = @($serviceHashes)
    }
}

function Get-ProcessSnapshot {
    $public = @(); $raw = @(); $serviceMap = Get-ServiceIdentityMap
    if ($null -eq $serviceMap) {
        return @{ error = 'SERVICE_IDENTITY_QUERY_FAILURE'; processes = @(); raw = @(); unknown = 0; unexpected = 0; pid_reuse = 0; services = @() }
    }
    try {
        foreach ($process in @(Get-CimInstance -ClassName Win32_Process -ErrorAction Stop)) {
            $record = Get-ProcessRecord $process $serviceMap
            $decision = Test-ProcessAgainstPolicy $record
            $record.identity_known = [bool]$decision.known
            $record.approved_by_policy = [bool]$decision.approved
            $record.identity_digest = $decision.identity_digest
            $record.decision_reason = $decision.reason
            $raw += $record
            $public += [ordered]@{
                identity_known = [bool]$decision.known
                approved_by_policy = [bool]$decision.approved
                identity_digest = $decision.identity_digest
                decision_reason = if ($decision.reason) { $decision.reason } else { 'APPROVED' }
            }
            foreach ($serviceHash in @($record.service_identity_sha256)) {
                if (@($script:IdentityPolicy.runner_service_identity_sha256) -contains $serviceHash) { $script:ObservedServiceHashes[$serviceHash] = $true }
            }
            if (-not $decision.known) { $script:UnknownCount++ }
            if (-not $decision.approved) { $script:UnrecognizedCount++ }
            if ($decision.pid_reuse) { $script:PidReuseCount++ }
        }
    } catch {
        return @{ error = 'PROCESS_QUERY_FAILURE'; processes = @(); raw = @(); unknown = 0; unexpected = 0; pid_reuse = 0; services = @() }
    }
    return @{ error = $null; processes = @($public); raw = @($raw); unknown = @($raw | Where-Object { $_.identity_known -ne $true }).Count
        unexpected = @($raw | Where-Object { $_.approved_by_policy -ne $true }).Count
        pid_reuse = @($raw | Where-Object { $_.decision_reason -eq 'PID_REUSE_OR_DYNAMIC_IDENTITY_MISMATCH' }).Count
        services = @($raw | ForEach-Object { @($_.service_identity_sha256) } | Where-Object { $_ } | Sort-Object -Unique) }
}

function Get-EventTimeUtc($NewEvent) {
    if ($null -eq $NewEvent.TIME_CREATED) { throw 'process event has no provider timestamp' }
    return [DateTime]::FromFileTimeUtc([Int64]$NewEvent.TIME_CREATED).ToString('o')
}

function Get-EventRecords([string]$SourceIdentifier) {
    $public = @(); $raw = @(); $unknown = 0; $unexpected = 0; $reuse = 0
    foreach ($event in @(Get-Event -SourceIdentifier $SourceIdentifier -ErrorAction SilentlyContinue)) {
        try {
            $newEvent = $event.SourceEventArgs.NewEvent
            $pidValue = [int]$newEvent.ProcessID
            $eventName = [string]$newEvent.ProcessName
            $record = $null
            if ($script:KnownPidRecords.ContainsKey([string]$pidValue)) {
                $record = $script:KnownPidRecords[[string]$pidValue].PSObject.Copy()
                $record.event = if ($SourceIdentifier -like '*start*') { 'start' } else { 'stop' }
                $record.event_process_name = $eventName
            }
            if ($null -eq $record -and $SourceIdentifier -like '*start*') {
                $candidate = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId=$pidValue" -ErrorAction SilentlyContinue
                if ($candidate) { $record = Get-ProcessRecord $candidate (Get-ServiceIdentityMap) }
            }
            if ($null -eq $record) { throw 'event identity could not be reconciled to a process start record' }
            $record.timestamp_utc = Get-EventTimeUtc $newEvent
            $decision = Test-ProcessAgainstPolicy $record
            $record.identity_known = [bool]$decision.known
            $record.approved_by_policy = [bool]$decision.approved
            $record.identity_digest = $decision.identity_digest
            $record.decision_reason = $decision.reason
            $raw += $record
            $public += [ordered]@{ event = $record.event; timestamp_utc = $record.timestamp_utc
                identity_known = [bool]$decision.known; approved_by_policy = [bool]$decision.approved
                identity_digest = $decision.identity_digest
                decision_reason = if ($decision.reason) { $decision.reason } else { 'APPROVED' } }
            if (-not $decision.known) { $unknown++ }
            if (-not $decision.approved) { $unexpected++ }
            if ($decision.pid_reuse) { $reuse++ }
        } catch {
            $raw += [ordered]@{ event = 'unparsed'; timestamp_utc = Get-UtcNowText; pid = -1
                process_name = '<unknown>'; executable_path = '<unknown>'; executable_sha256 = '<unknown>'
                service_identity_sha256 = @(); identity_known = $false; approved_by_policy = $false
                decision_reason = 'UNKNOWN_PROCESS_IDENTITY' }
            $public += [ordered]@{ event = 'unparsed'; timestamp_utc = Get-UtcNowText
                identity_known = $false; approved_by_policy = $false; identity_digest = '<unknown>'
                decision_reason = 'UNKNOWN_PROCESS_IDENTITY' }
            $unknown++; $script:UnknownCount++; $script:UnrecognizedCount++
        }
        Remove-Event -EventIdentifier $event.EventIdentifier -ErrorAction SilentlyContinue
    }
    return @{ public = @($public); raw = @($raw); unknown = $unknown; unexpected = $unexpected; reuse = $reuse }
}

function New-PrivateInventory(
    [string]$CaptureStartUtc, [string]$CaptureEndUtc, [object]$PolicyStatus,
    [object]$LeaseStatus, [object]$LossStatus, [object]$BindingStatus
) {
    return [ordered]@{
        format = 'VOX_FX_T18_PRIVATE_PROCESS_INVENTORY_V1'
        visibility = 'owner_private_not_for_public_artifact'
        fixture_mode = $script:FixtureMode
        capture_start_utc = $CaptureStartUtc; capture_end_utc = $CaptureEndUtc
        policy_sha256 = $script:IdentityPolicyHash
        lease_receipt_sha256 = [string](Get-OptionalProperty $LeaseStatus 'hash' '<missing>')
        event_loss_receipt_sha256 = [string](Get-OptionalProperty $LossStatus 'hash' '<missing>')
        execution_binding_sha256 = [string](Get-OptionalProperty $BindingStatus 'hash' '<missing>')
        samples = @($script:RawSamples)
        process_events = @($script:RawEvents)
    }
}

function Save-PrivateInventory([object]$Value) {
    if (-not $PrivateInventoryPath) { return @{ written = $false; hash = '<missing>'; error = 'PRIVATE_INVENTORY_PATH_MISSING' } }
    try {
        Write-WitnessJson $Value $PrivateInventoryPath
        return @{ written = $true; hash = (Get-FileHash -LiteralPath $PrivateInventoryPath -Algorithm SHA256).Hash.ToLowerInvariant(); error = $null }
    } catch { return @{ written = $false; hash = '<invalid>'; error = 'PRIVATE_INVENTORY_WRITE_FAILURE' } }
}

function New-OfflineWitness {
    $base = [DateTime]::Parse('2026-11-22T00:00:00Z').ToUniversalTime()
    $events = @(); $competitors = 0; $unknowns = 0
    if ($SyntheticCompetitor) {
        $events += [ordered]@{ event = 'start'; timestamp_utc = $base.AddSeconds(1.1).ToString('o'); identity_known = $true; approved_by_policy = $false; identity_digest = ('c' * 64); decision_reason = 'UNRECOGNIZED_PROCESS_IDENTITY' }
        $events += [ordered]@{ event = 'stop'; timestamp_utc = $base.AddSeconds(1.7).ToString('o'); identity_known = $true; approved_by_policy = $false; identity_digest = ('c' * 64); decision_reason = 'UNRECOGNIZED_PROCESS_IDENTITY' }
        $competitors = 1
    }
    if ($SyntheticUnknownPid) {
        $events += [ordered]@{ event = 'start'; timestamp_utc = $base.AddSeconds(2.1).ToString('o'); identity_known = $false; approved_by_policy = $false; identity_digest = '<unknown>'; decision_reason = 'UNKNOWN_PROCESS_IDENTITY' }
        $unknowns = 1
    }
    $policy = New-OfflinePolicy
    $samples = @()
    for ($index = 0; $index -le 122; $index++) {
        $phase = if ($index -le 30) { 'pre' } elseif ($index -le 91) { 'during' } else { 'post' }
        $samples += [ordered]@{ phase = $phase; sample_ordinal = $index; timestamp_utc = $base.AddSeconds($index).ToString('o')
            scheduled_timestamp_utc = $base.AddSeconds($index).ToString('o'); monotonic_elapsed_ms = $index * 1000
            observed_process_count = 1; approved_process_count = 1; unknown_identity_count = 0
            unexpected_identity_count = 0; pid_reuse_mismatch_count = 0; process_query_error = $null
            process_inventory_sha256 = ('a' * 64) }
    }
    return [ordered]@{
        format = 'VOX_FX_T18_HOST_WITNESS_V1'; schema_version = 2
        mode = 'offline_synthetic'; fixture_mode = $true; qualification_prohibited = $true
        host_observation_mode = 'process_snapshot_and_event_trace'
        sample_interval_ms = 1000; pre_seconds = 30; during_seconds = 60; post_seconds = 30
        capture_start_utc = $base.ToString('o'); capture_end_utc = $base.AddSeconds(122).ToString('o')
        monitor_gaps = @(); identity_policy = $policy; identity_policy_sha256 = ('f' * 64)
        identity_policy_path = '<fixture-not-an-attestation>'; private_inventory_path = '<fixture-private-not-retained>'
        event_trace = [ordered]@{ observation_provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'; loss_provider = 'fixture-no-loss-proof'
            available = $true; coverage_complete = $false; lost_events = $null; buffer_loss_events = $null
            loss_accounting = 'unverified_fixture'; coverage_start_utc = $base.ToString('o'); coverage_end_utc = $base.AddSeconds(122).ToString('o')
            loss_receipt_path = '<fixture-not-an-attestation>'; loss_receipt_sha256 = '<missing>' }
        runner_lease = [ordered]@{ recognized = $false; owner_verified = $false; owner = '<fixture>'; host = 'offline-fixture'; lease_id = '<fixture>'; verification_receipt_path = '<fixture>'; verification_receipt_sha256 = '<missing>'; token = '<redacted>' }
        runner_service_identities = @('1' * 64, '2' * 64, '3' * 64, '4' * 64)
        execution_binding = [ordered]@{ verified = $false; qualification_prohibited = $true; binding_receipt_path = '<fixture>'; binding_receipt_sha256 = '<missing>' }
        attestation_proofs = [ordered]@{ policy = 'synthetic'; lease = 'synthetic'; event_loss = 'synthetic'; execution_binding = 'synthetic' }
        samples = @($samples); process_events = @($events)
        unrecognized_competitors = $competitors; unknown_pids = $unknowns
        inventory_summary = [ordered]@{ samples_observed = $samples.Count; events_observed = $events.Count; raw_inventory_suppressed = $true }
        evidence_blocked = $true; query_errors = @('FIXTURE_MODE');
        diagnostic = [ordered]@{ error_category = 'FIXTURE_QUALIFICATION_PROHIBITED'; reason_codes = @('FIXTURE_MODE', 'QUALIFICATION_PROHIBITED'); sanitized = $true; recommended_exit_code = 2 }
        fixture_notes = 'Synthetic identities and event timing only; no process or service was launched; never production attestation.'
    }
}

function New-BlockedWitness(
    [string]$CaptureStartUtc, [string]$CaptureEndUtc, [string[]]$ReasonCodes,
    [string]$ErrorCategory, [string[]]$ErrorSummaries, [string]$IdentityPolicyHash,
    [object]$PolicySignature, [object]$LeaseStatus, [object]$LossStatus,
    [object]$BindingStatus, [bool]$EventTraceAvailable, [string]$EventTraceError,
    [object]$PrivateInventoryStatus, [int]$SampleCount, [int]$EventCount,
    [int]$QueryErrorCount, [int]$GapCount
) {
    $safeReasons = @($ReasonCodes | Where-Object { $_ } | Select-Object -Unique)
    $safeSummaries = @($ErrorSummaries | Where-Object { $_ } | Select-Object -Unique)
    if ($safeReasons.Count -eq 0) { $safeReasons = @('CAPTURE_BLOCKED') }
    if ($safeSummaries.Count -eq 0) { $safeSummaries = @('host witness capture was blocked') }
    $leaseVerified = [bool](Get-OptionalProperty $LeaseStatus 'verified' $false)
    $lossVerified = [bool](Get-OptionalProperty $LossStatus 'verified' $false)
    $bindingVerified = [bool](Get-OptionalProperty $BindingStatus 'verified' $false)
    return [ordered]@{
        format = 'VOX_FX_T18_HOST_WITNESS_V1'; schema_version = 2; mode = 'capture'
        fixture_mode = $script:FixtureMode; qualification_prohibited = $script:QualificationProhibited
        host_observation_mode = 'process_snapshot_and_event_trace'; sample_interval_ms = 1000
        pre_seconds = $PreSeconds; during_seconds = $DuringSeconds; post_seconds = $PostSeconds
        capture_start_utc = if ($CaptureStartUtc) { $CaptureStartUtc } else { Get-UtcNowText }
        capture_end_utc = if ($CaptureEndUtc) { $CaptureEndUtc } else { Get-UtcNowText }
        monitor_gaps = @(); identity_policy = [ordered]@{ format = [string](Get-OptionalProperty $script:IdentityPolicy 'format' 'invalid'); policy_id = [string](Get-OptionalProperty $script:IdentityPolicy 'policy_id' '<missing>'); baseline_id = [string](Get-OptionalProperty $script:IdentityPolicy 'baseline_id' '<missing>'); owner_approved = $false; policy_sha256 = $IdentityPolicyHash; runner_service_count = 0; approved_process_count = 0 }
        identity_policy_path = '<redacted>'; identity_policy_sha256 = $IdentityPolicyHash
        private_inventory_path = '<redacted>'; private_inventory_sha256 = [string](Get-OptionalProperty $PrivateInventoryStatus 'hash' '<missing>')
        attestation_proofs = [ordered]@{
            policy = [ordered]@{ verified = [bool](Get-OptionalProperty $PolicySignature 'verified' $false); data_sha256 = [string](Get-OptionalProperty $PolicySignature 'data_hash' '<missing>'); signature_sha256 = [string](Get-OptionalProperty $PolicySignature 'signature_hash' '<missing>'); signer_thumbprint = '<redacted>'; verification_method = [string](Get-OptionalProperty $PolicySignature 'verification_method' '<missing>') }
            lease = [ordered]@{ verified = $leaseVerified; data_sha256 = [string](Get-OptionalProperty (Get-OptionalProperty $LeaseStatus 'signature' $null) 'data_hash' '<missing>'); signature_sha256 = [string](Get-OptionalProperty (Get-OptionalProperty $LeaseStatus 'signature' $null) 'signature_hash' '<missing>'); signer_thumbprint = '<redacted>'; verification_method = '<redacted>' }
            event_loss = [ordered]@{ verified = [bool](Get-OptionalProperty (Get-OptionalProperty $LossStatus 'signature' $null) 'verified' $false); data_sha256 = [string](Get-OptionalProperty (Get-OptionalProperty $LossStatus 'signature' $null) 'data_hash' '<missing>'); signature_sha256 = [string](Get-OptionalProperty (Get-OptionalProperty $LossStatus 'signature' $null) 'signature_hash' '<missing>'); signer_thumbprint = '<redacted>'; verification_method = '<redacted>' }
            execution_binding = [ordered]@{ verified = [bool](Get-OptionalProperty (Get-OptionalProperty $BindingStatus 'signature' $null) 'verified' $false); data_sha256 = [string](Get-OptionalProperty (Get-OptionalProperty $BindingStatus 'signature' $null) 'data_hash' '<missing>'); signature_sha256 = [string](Get-OptionalProperty (Get-OptionalProperty $BindingStatus 'signature' $null) 'signature_hash' '<missing>'); signer_thumbprint = '<redacted>'; verification_method = '<redacted>' }
        }
        event_trace = [ordered]@{ observation_provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'; loss_provider = 'Microsoft-Windows-Kernel-Process/ETW'; available = [bool]$EventTraceAvailable; coverage_complete = $false; lost_events = $null; buffer_loss_events = $null; loss_accounting = 'unknown'; coverage_start_utc = '<unknown>'; coverage_end_utc = '<unknown>'; loss_receipt_path = '<redacted>'; loss_receipt_sha256 = '<redacted>'; error = $EventTraceError; loss_receipt_error = [string](Get-OptionalProperty $LossStatus 'error' 'event-loss attestation unavailable') }
        runner_lease = [ordered]@{ recognized = $leaseVerified; owner_verified = $leaseVerified; owner = if ($leaseVerified) { '<owner-attested>' } else { '<missing>' }; host = [string]$env:COMPUTERNAME; lease_id = if ($leaseVerified) { '<redacted>' } else { '<missing>' }; verification_receipt_path = '<redacted>'; verification_receipt_sha256 = '<redacted>'; token = '<redacted>' }
        runner_service_identities = @(); execution_binding = [ordered]@{ verified = $bindingVerified; qualification_prohibited = $script:QualificationProhibited; binding_receipt_path = '<redacted>'; binding_receipt_sha256 = [string](Get-OptionalProperty $BindingStatus 'hash' '<missing>') }
        samples = @(); process_events = @(); unrecognized_competitors = 0; unknown_pids = 0
        inventory_summary = [ordered]@{ samples_observed = $SampleCount; events_observed = $EventCount; query_error_count = $QueryErrorCount; monitor_gap_count = $GapCount; raw_inventory_suppressed = $true }
        evidence_blocked = $true; query_errors = @($safeReasons)
        diagnostic = [ordered]@{ error_category = $ErrorCategory; reason_codes = @($safeReasons); error_summaries = @($safeSummaries); sanitized = $true; recommended_exit_code = 2 }
        fixture_notes = 'Capture was blocked; raw process inventory and event identities were retained only in the owner-private sidecar when available.'
    }
}

if ($Mode -eq 'OfflineSynthetic') {
    if (-not $FixtureMode) { throw 'OfflineSynthetic requires explicit -FixtureMode; it is qualification-prohibited' }
    if (-not $SyntheticCompetitor -and -not $SyntheticUnknownPid) { throw 'OfflineSynthetic requires -SyntheticCompetitor or -SyntheticUnknownPid' }
    Write-WitnessJson (New-OfflineWitness) $OutputPath
    exit 2
}

# Preflight: every production attestation must be a detached CMS signed file
# verified against a thumbprint supplied out-of-band by the owner. FixtureMode
# is explicit and is never allowed to reach a production evaluator pass.
$script:IdentityPolicy = Get-IdentityPolicy
$script:IdentityPolicyHash = '<missing>'
if ($IdentityPolicyPath -and (Test-Path -LiteralPath $IdentityPolicyPath -PathType Leaf)) {
    try { $script:IdentityPolicyHash = (Get-FileHash -LiteralPath $IdentityPolicyPath -Algorithm SHA256).Hash.ToLowerInvariant() } catch { $script:IdentityPolicyHash = '<invalid>' }
}
$policySignature = Get-VerifiedOwnerSignature $IdentityPolicyPath $IdentityPolicySignaturePath $OwnerCertificateThumbprint 'IDENTITY_POLICY'
$leaseSignature = Get-VerifiedOwnerSignature $LeaseReceiptPath $LeaseReceiptSignaturePath $OwnerCertificateThumbprint 'LEASE_RECEIPT'
$lossSignature = Get-VerifiedOwnerSignature $EventLossReceiptPath $EventLossReceiptSignaturePath $OwnerCertificateThumbprint 'EVENT_LOSS_RECEIPT'
$bindingSignature = Get-VerifiedOwnerSignature $ExecutionBindingPath $ExecutionBindingSignaturePath $OwnerCertificateThumbprint 'EXECUTION_BINDING'

$policyCodes = @(); $policySummaries = @()
$format = [string](Get-OptionalProperty $script:IdentityPolicy 'format' 'invalid')
$policyId = [string](Get-OptionalProperty $script:IdentityPolicy 'policy_id' '<missing>')
$baselineId = [string](Get-OptionalProperty $script:IdentityPolicy 'baseline_id' '<missing>')
$policyServices = @($script:IdentityPolicy.runner_service_identity_sha256)
$policyEntries = @($script:IdentityPolicy.approved_processes)
$policyValid = ($format -eq 'VOX_FX_T18_IDENTITY_POLICY_V1' -and $policyId -ne '<missing>' -and $baselineId -ne '<missing>' -and
    $script:IdentityPolicy.owner_approved -eq $true -and $script:IdentityPolicyHash -match '^[0-9a-fA-F]{64}$' -and
    $policyServices.Count -eq 4 -and @($policyServices | Where-Object { [string]$_ -notmatch '^[0-9a-fA-F]{64}$' }).Count -eq 0 -and
    $policyEntries.Count -gt 0 -and [string]$script:IdentityPolicy.host -ne '<missing>')
if (-not $policyValid) {
    if (-not $IdentityPolicyPath) { $policyCodes += 'IDENTITY_POLICY_MISSING'; $policySummaries += 'owner-signed identity baseline is missing' }
    else { $policyCodes += 'IDENTITY_POLICY_INVALID'; $policySummaries += 'owner-signed identity baseline is malformed or not four-service complete' }
}
if (-not $script:FixtureMode -and -not [bool]$policySignature.verified) { $policyCodes += [string]$policySignature.error_code; $policySummaries += [string]$policySignature.error }
if ($policySignature.verified -and [string]$policySignature.data_hash -ine $script:IdentityPolicyHash) { $policyCodes += 'IDENTITY_POLICY_HASH_RECONCILIATION_FAILURE'; $policySummaries += 'policy signature does not cover the policy bytes hashed by capture' }

$captureStart = Get-UtcNowText
try {
    $policyStart = [DateTime]::Parse([string]$script:IdentityPolicy.valid_from_utc).ToUniversalTime()
    $policyEnd = [DateTime]::Parse([string]$script:IdentityPolicy.valid_until_utc).ToUniversalTime()
    if ($policyStart -gt [DateTime]::Parse($captureStart).ToUniversalTime() -or $policyEnd -le $policyStart -or $policyEnd -lt [DateTime]::Parse($captureStart).ToUniversalTime()) { $policyCodes += 'IDENTITY_POLICY_EXPIRED'; $policySummaries += 'owner identity baseline is outside its validity interval' }
    if (-not $script:FixtureMode -and [string]$script:IdentityPolicy.host -cne [string]$env:COMPUTERNAME) { $policyCodes += 'IDENTITY_POLICY_HOST_MISMATCH'; $policySummaries += 'owner identity baseline targets a different host' }
} catch { $policyCodes += 'IDENTITY_POLICY_INTERVAL_INVALID'; $policySummaries += 'owner identity baseline validity interval is invalid' }
$verifiedLease = Get-VerifiedLeaseReceipt $leaseSignature $script:FixtureMode
$lossReceipt = Get-VerifiedEventLossReceipt $captureStart $lossSignature $script:FixtureMode
$bindingStatus = Get-VerifiedExecutionBinding $bindingSignature $script:FixtureMode
$preflightCodes = @($policyCodes); $preflightSummaries = @($policySummaries)
if (-not [bool](Get-OptionalProperty $verifiedLease 'verified' $false)) { $preflightCodes += [string](Get-OptionalProperty $verifiedLease 'error_code' 'LEASE_RECEIPT_INVALID'); $preflightSummaries += [string](Get-OptionalProperty $verifiedLease 'error' 'owner lease is unavailable') }
if (-not [bool](Get-OptionalProperty $lossReceipt 'verified' $false)) { $preflightCodes += [string](Get-OptionalProperty $lossReceipt 'error_code' 'EVENT_LOSS_RECEIPT_INVALID'); $preflightSummaries += [string](Get-OptionalProperty $lossReceipt 'error' 'measurable event-loss proof is unavailable') }
if (-not [bool](Get-OptionalProperty $bindingStatus 'verified' $false)) { $preflightCodes += [string](Get-OptionalProperty $bindingStatus 'error_code' 'EXECUTION_BINDING_INVALID'); $preflightSummaries += [string](Get-OptionalProperty $bindingStatus 'error' 'CTest execution binding is unavailable') }
$leaseReceipt = Get-OptionalProperty $verifiedLease 'receipt' $null
$leasePolicyHash = [string](Get-OptionalProperty $leaseReceipt 'policy_sha256' '')
if ([bool](Get-OptionalProperty $verifiedLease 'verified' $false) -and $leasePolicyHash -ine $script:IdentityPolicyHash) { $preflightCodes += 'LEASE_POLICY_MISMATCH'; $preflightSummaries += 'owner lease is bound to a different identity baseline' }
$bindingReceiptForCheck = Get-OptionalProperty $bindingStatus 'binding' $null
if ([bool](Get-OptionalProperty $bindingStatus 'verified' $false)) {
    if ([string]$bindingReceiptForCheck.host -cne [string]$env:COMPUTERNAME -and -not $script:FixtureMode) { $preflightCodes += 'EXECUTION_BINDING_HOST_MISMATCH'; $preflightSummaries += 'CTest binding targets a different host' }
    $leaseIdForCheck = [string](Get-OptionalProperty $leaseReceipt 'lease_id' '')
    if ($leaseIdForCheck -and [string]$bindingReceiptForCheck.lease_id -cne $leaseIdForCheck) { $preflightCodes += 'EXECUTION_BINDING_LEASE_MISMATCH'; $preflightSummaries += 'CTest binding is bound to a different host lease' }
    if ([string]$bindingReceiptForCheck.policy_sha256 -ine $script:IdentityPolicyHash) { $preflightCodes += 'EXECUTION_BINDING_POLICY_MISMATCH'; $preflightSummaries += 'CTest binding is bound to a different identity baseline' }
}
if (-not $PrivateInventoryPath) { $preflightCodes += 'PRIVATE_INVENTORY_PATH_MISSING'; $preflightSummaries += 'owner-private raw inventory path is required' }
if ($script:FixtureMode) { $preflightCodes += 'FIXTURE_MODE'; $preflightSummaries += 'fixture mode is qualification-prohibited' }
# The provider failure is an executable negative fixture. It intentionally
# reaches the observation path only when explicitly fixture-marked; production
# callers cannot use this switch to bypass preflight.
if ($SimulateProviderFailure -and $script:FixtureMode) { $preflightCodes = @(); $preflightSummaries = @() }
$preflightCodes = @($preflightCodes | Where-Object { $_ } | Select-Object -Unique)
$preflightSummaries = @($preflightSummaries | Where-Object { $_ } | Select-Object -Unique)
if ($preflightCodes.Count -gt 0) {
    $blocked = New-BlockedWitness $captureStart (Get-UtcNowText) $preflightCodes 'ATTESTATION_BLOCKED' $preflightSummaries $script:IdentityPolicyHash $policySignature $verifiedLease $lossReceipt $bindingStatus $false 'capture did not start because required owner attestations or private retention were unavailable' @{ written = $false; hash = '<missing>' } 0 0 0 0
    try { Write-WitnessJson $blocked $OutputPath } catch { Write-Error 'Unable to persist the blocked host witness receipt' }
    exit 2
}

$eventTraceAvailable = $false; $eventTraceError = $null; $subscriptions = @(); $startSource = $null; $stopSource = $null
$captureFailureCodes = @(); $captureFailureSummaries = @()
$script:PublicSamples = @(); $script:PublicEvents = @(); $script:RawSamples = @(); $script:RawEvents = @(); $script:MonitorGaps = @()
$script:QueryErrors = @(); $script:QueryErrorCodes = @(); $script:UnrecognizedCount = 0; $script:UnknownCount = 0; $script:PidReuseCount = 0; $script:ObservedServiceHashes = @{}
$script:SampleOrdinal = 0
try {
    if ($SimulateProviderFailure) { throw 'simulated provider/query failure' }
    try {
        $startSource = "VOX-FX-T18-ProcessStart-$PID"; $stopSource = "VOX-FX-T18-ProcessStop-$PID"
        $subscriptions += Register-CimIndicationEvent -Namespace 'root/cimv2' -Query 'SELECT * FROM Win32_ProcessStartTrace' -SourceIdentifier $startSource
        $subscriptions += Register-CimIndicationEvent -Namespace 'root/cimv2' -Query 'SELECT * FROM Win32_ProcessStopTrace' -SourceIdentifier $stopSource
        $eventTraceAvailable = $true
    } catch {
        $eventTraceError = 'process event provider registration failed'; $captureFailureCodes += 'EVENT_PROVIDER_UNAVAILABLE'; $captureFailureSummaries += 'process event provider could not be registered'
    }

    function Add-PhaseSamples([string]$Phase, [int]$Seconds) {
        $phaseStart = Get-MonotonicSeconds
        for ($index = 0; $index -le $Seconds; $index++) {
            $expected = $phaseStart + $index
            $before = Get-MonotonicSeconds
            $snapshot = Get-ProcessSnapshot
            $observed = Get-MonotonicSeconds
            if ($observed - $expected -gt 2.0) { $script:MonitorGaps += [ordered]@{ phase = $Phase; sample_ordinal = $script:SampleOrdinal; late_by_ms = (($observed - $expected) * 1000.0) } }
            if ($snapshot.error) { $script:QueryErrors += [string]$snapshot.error; $script:QueryErrorCodes += [string]$snapshot.error }
            $startEvents = @{ public = @(); raw = @(); unknown = 0; unexpected = 0; reuse = 0 }
            $stopEvents = @{ public = @(); raw = @(); unknown = 0; unexpected = 0; reuse = 0 }
            if ($eventTraceAvailable) {
                $startEvents = Get-EventRecords $startSource; $stopEvents = Get-EventRecords $stopSource
            }
            $rawSample = [ordered]@{ phase = $Phase; sample_ordinal = $script:SampleOrdinal; timestamp_utc = Get-UtcNowText; scheduled_timestamp_utc = ([DateTime]::UtcNow.AddSeconds($expected - $observed)).ToString('o'); monotonic_elapsed_ms = (($observed - $phaseStart) * 1000.0); processes = @($snapshot.raw); events = @($startEvents.raw) + @($stopEvents.raw) }
            $inventoryHash = Get-ObjectSha256 $rawSample
            $script:RawSamples += $rawSample
            $script:RawEvents += @($startEvents.raw) + @($stopEvents.raw)
            $script:PublicSamples += [ordered]@{ phase = $Phase; sample_ordinal = $script:SampleOrdinal; timestamp_utc = $rawSample.timestamp_utc; scheduled_timestamp_utc = $rawSample.scheduled_timestamp_utc; monotonic_elapsed_ms = $rawSample.monotonic_elapsed_ms; observed_process_count = @($snapshot.raw).Count; approved_process_count = @($snapshot.raw | Where-Object { $_.approved_by_policy -eq $true }).Count; unknown_identity_count = $snapshot.unknown + $startEvents.unknown + $stopEvents.unknown; unexpected_identity_count = $snapshot.unexpected + $startEvents.unexpected + $stopEvents.unexpected; pid_reuse_mismatch_count = $snapshot.pid_reuse + $startEvents.reuse + $stopEvents.reuse; process_query_error = $snapshot.error; process_inventory_sha256 = $inventoryHash }
            $script:PublicEvents += @($startEvents.public) + @($stopEvents.public)
            $script:SampleOrdinal++
            if ($index -lt $Seconds) {
                $remaining = $expected + 1.0 - (Get-MonotonicSeconds)
                if ($remaining -gt 0.0) { Start-Sleep -Milliseconds ([math]::Max(1, [math]::Floor($remaining * 1000.0))) }
            }
        }
    }
    Add-PhaseSamples 'pre' $PreSeconds
    Add-PhaseSamples 'during' $DuringSeconds
    Add-PhaseSamples 'post' $PostSeconds
} catch {
    $captureFailureCodes += 'CAPTURE_EXCEPTION'; $captureFailureSummaries += 'host process observation failed before a complete witness was collected'
} finally {
    foreach ($subscription in @($subscriptions)) {
        try { $subscriptionId = Get-OptionalProperty $subscription 'SubscriptionId' $null; if ($null -ne $subscriptionId) { Unregister-Event -SubscriptionId $subscriptionId -ErrorAction SilentlyContinue } } catch { }
    }
    try { if ($startSource) { Unregister-Event -SourceIdentifier $startSource -ErrorAction SilentlyContinue } } catch { }
    try { if ($stopSource) { Unregister-Event -SourceIdentifier $stopSource -ErrorAction SilentlyContinue } } catch { }
}

$captureEnd = Get-UtcNowText
$privateInventory = New-PrivateInventory $captureStart $captureEnd $policySignature $verifiedLease $lossReceipt $bindingStatus
$privateStatus = Save-PrivateInventory $privateInventory
$verifiedLeaseAfter = Get-VerifiedLeaseReceipt $leaseSignature $false
$lossAfter = Get-VerifiedEventLossReceipt $captureStart $lossSignature $false
$bindingAfter = Get-VerifiedExecutionBinding $bindingSignature $false
$lossAttested = [bool](Get-OptionalProperty $lossAfter 'verified' $false)
if ($lossAttested) {
    try {
        $lossEnd = [DateTime]::Parse([string]$lossAfter.receipt.coverage_end_utc).ToUniversalTime()
        if ($lossEnd -lt [DateTime]::Parse($captureEnd).ToUniversalTime()) { $lossAttested = $false; $lossAfter.error_code = 'EVENT_LOSS_COVERAGE_SHORT'; $lossAfter.error = 'ETW loss receipt ends before capture completion' }
    } catch { $lossAttested = $false; $lossAfter.error_code = 'EVENT_LOSS_RECEIPT_INVALID'; $lossAfter.error = 'ETW loss receipt completion time is invalid' }
}
$reasonCodes = @($captureFailureCodes + $script:QueryErrorCodes); $reasonSummaries = @($captureFailureSummaries)
try {
    $policyStartAfter = [DateTime]::Parse([string]$script:IdentityPolicy.valid_from_utc).ToUniversalTime()
    $policyEndAfter = [DateTime]::Parse([string]$script:IdentityPolicy.valid_until_utc).ToUniversalTime()
    if ($policyStartAfter -gt [DateTime]::Parse($captureStart).ToUniversalTime() -or $policyEndAfter -lt [DateTime]::Parse($captureEnd).ToUniversalTime()) { $reasonCodes += 'IDENTITY_POLICY_COVERAGE_SHORT'; $reasonSummaries += 'owner identity baseline did not cover the complete witness interval' }
} catch { $reasonCodes += 'IDENTITY_POLICY_INTERVAL_INVALID'; $reasonSummaries += 'owner identity baseline completion interval is invalid' }
if (-not [bool](Get-OptionalProperty $privateStatus 'written' $false)) { $reasonCodes += [string](Get-OptionalProperty $privateStatus 'error' 'PRIVATE_INVENTORY_WRITE_FAILURE'); $reasonSummaries += 'owner-private inventory could not be retained' }
if (-not [bool](Get-OptionalProperty $verifiedLeaseAfter 'verified' $false)) { $reasonCodes += [string](Get-OptionalProperty $verifiedLeaseAfter 'error_code' 'LEASE_RECEIPT_INVALID'); $reasonSummaries += [string](Get-OptionalProperty $verifiedLeaseAfter 'error' 'owner lease became unavailable') }
if ([bool](Get-OptionalProperty $verifiedLeaseAfter 'verified' $false)) {
    try {
        $leaseEndAfter = [DateTime]::Parse([string]$verifiedLeaseAfter.receipt.valid_until_utc).ToUniversalTime()
        if ($leaseEndAfter -lt [DateTime]::Parse($captureEnd).ToUniversalTime()) { $reasonCodes += 'LEASE_COVERAGE_SHORT'; $reasonSummaries += 'owner lease ended before capture completion' }
    } catch { $reasonCodes += 'LEASE_RECEIPT_INVALID'; $reasonSummaries += 'owner lease completion time is invalid' }
}
if (-not [bool](Get-OptionalProperty $bindingAfter 'verified' $false)) { $reasonCodes += [string](Get-OptionalProperty $bindingAfter 'error_code' 'EXECUTION_BINDING_INVALID'); $reasonSummaries += [string](Get-OptionalProperty $bindingAfter 'error' 'CTest binding became unavailable') }
if ([bool](Get-OptionalProperty $bindingAfter 'verified' $false)) {
    try {
        foreach ($boundProfile in @($bindingAfter.binding.profiles)) {
            $boundStart = [DateTime]::Parse([string]$boundProfile.test_start_utc).ToUniversalTime()
            $boundEnd = [DateTime]::Parse([string]$boundProfile.test_end_utc).ToUniversalTime()
            if ($boundStart -lt [DateTime]::Parse($captureStart).ToUniversalTime() -or $boundEnd -gt [DateTime]::Parse($captureEnd).ToUniversalTime()) { throw 'CTest binding lies outside host witness interval' }
        }
    } catch { $reasonCodes += 'EXECUTION_BINDING_INTERVAL_MISMATCH'; $reasonSummaries += 'CTest profile interval was not contained by the host witness' }
}
if (-not $eventTraceAvailable) { $reasonCodes += 'EVENT_PROVIDER_UNAVAILABLE'; $reasonSummaries += 'process event provider was unavailable' }
if ($script:QueryErrors.Count -gt 0) { $reasonCodes += 'PROCESS_QUERY_FAILURE'; $reasonSummaries += 'one or more process/service identity queries failed' }
if (-not $lossAttested) { $reasonCodes += [string](Get-OptionalProperty $lossAfter 'error_code' 'EVENT_LOSS_RECEIPT_INVALID'); $reasonSummaries += [string](Get-OptionalProperty $lossAfter 'error' 'measurable ETW loss proof is unavailable') }
if ($script:MonitorGaps.Count -gt 0) { $reasonCodes += 'MONITOR_GAP_OR_MISSING_1HZ_SAMPLE'; $reasonSummaries += 'one or more monotonic one-Hz deadlines were missed' }
if ($script:UnknownCount -gt 0) { $reasonCodes += 'UNKNOWN_PROCESS_IDENTITY'; $reasonSummaries += 'one or more process identities were unknown' }
if ($script:UnrecognizedCount -gt 0) { $reasonCodes += 'UNRECOGNIZED_PROCESS_IDENTITY'; $reasonSummaries += 'one or more observed identities were not in the owner baseline' }
if ($script:PidReuseCount -gt 0) { $reasonCodes += 'PID_REUSE_OR_DYNAMIC_IDENTITY_MISMATCH'; $reasonSummaries += 'PID reuse or dynamic identity mismatch was observed' }
$policyServiceSet = @($policyServices | Sort-Object -Unique); $observedServiceSet = @($script:ObservedServiceHashes.Keys | Sort-Object -Unique)
if ($policyServiceSet.Count -ne 4 -or (($policyServiceSet | Where-Object { $observedServiceSet -notcontains $_ }).Count -gt 0)) { $reasonCodes += 'HAMUNAPTRA_RUNNER_SERVICE_BASELINE_MISMATCH'; $reasonSummaries += 'the four owner-baselined HAMUNAPTRA runner service identities were not reconciled' }
$reasonCodes = @($reasonCodes | Where-Object { $_ } | Select-Object -Unique); $reasonSummaries = @($reasonSummaries | Where-Object { $_ } | Select-Object -Unique)
$eventTraceComplete = $eventTraceAvailable -and $lossAttested -and $script:QueryErrors.Count -eq 0 -and $script:MonitorGaps.Count -eq 0
if ($reasonCodes.Count -gt 0) {
    $blocked = New-BlockedWitness $captureStart $captureEnd $reasonCodes 'CAPTURE_BLOCKED' $reasonSummaries $script:IdentityPolicyHash $policySignature $verifiedLeaseAfter $lossAfter $bindingAfter $eventTraceAvailable $eventTraceError $privateStatus $script:PublicSamples.Count $script:PublicEvents.Count $script:QueryErrors.Count $script:MonitorGaps.Count
    try { Write-WitnessJson $blocked $OutputPath } catch { Write-Error 'Unable to persist the blocked host witness receipt' }
    exit 2
}

$binding = $bindingAfter.binding; $loss = $lossAfter.receipt
$policyWindowStart = [DateTime]::Parse([string]$script:IdentityPolicy.valid_from_utc).ToUniversalTime().ToString('o')
$policyWindowEnd = [DateTime]::Parse([string]$script:IdentityPolicy.valid_until_utc).ToUniversalTime().ToString('o')
$publicPolicy = [ordered]@{ format = $script:IdentityPolicy.format; policy_id = $script:IdentityPolicy.policy_id; baseline_id = $script:IdentityPolicy.baseline_id; policy_sha256 = $script:IdentityPolicyHash; baseline_valid_from_utc = $policyWindowStart; baseline_valid_until_utc = $policyWindowEnd; runner_service_count = 4; approved_process_count = $policyEntries.Count; policy_decision_method = 'owner_signed_name_path_hash_parent_service_and_pid_start_reconciliation' }
$runnerServiceHashes = @($observedServiceSet | Sort-Object)
$witness = [ordered]@{
    format = 'VOX_FX_T18_HOST_WITNESS_V1'; schema_version = 2; mode = 'capture'; fixture_mode = $false; qualification_prohibited = $false
    host_observation_mode = 'process_snapshot_and_event_trace'; sample_interval_ms = 1000; pre_seconds = $PreSeconds; during_seconds = $DuringSeconds; post_seconds = $PostSeconds
    capture_start_utc = $captureStart; capture_end_utc = $captureEnd; monotonic_capture_start = $script:PublicSamples[0].monotonic_elapsed_ms; monotonic_capture_end = $script:PublicSamples[-1].monotonic_elapsed_ms; monitor_gaps = @($script:MonitorGaps)
    identity_policy = $publicPolicy; identity_policy_path = [IO.Path]::GetFullPath($IdentityPolicyPath); identity_policy_sha256 = $script:IdentityPolicyHash
    private_inventory_path = [IO.Path]::GetFullPath($PrivateInventoryPath); private_inventory_sha256 = $privateStatus.hash
    attestation_proofs = [ordered]@{
        policy = [ordered]@{ verified = $true; data_sha256 = $policySignature.data_hash; signature_sha256 = $policySignature.signature_hash; signer_thumbprint = $policySignature.signer_thumbprint; verification_method = $policySignature.verification_method; data_path = [IO.Path]::GetFullPath($IdentityPolicyPath); signature_path = [IO.Path]::GetFullPath($IdentityPolicySignaturePath) }
        lease = [ordered]@{ verified = $true; data_sha256 = $leaseSignature.data_hash; signature_sha256 = $leaseSignature.signature_hash; signer_thumbprint = $leaseSignature.signer_thumbprint; verification_method = $leaseSignature.verification_method; data_path = [IO.Path]::GetFullPath($LeaseReceiptPath); signature_path = [IO.Path]::GetFullPath($LeaseReceiptSignaturePath) }
        event_loss = [ordered]@{ verified = $true; data_sha256 = $lossSignature.data_hash; signature_sha256 = $lossSignature.signature_hash; signer_thumbprint = $lossSignature.signer_thumbprint; verification_method = $lossSignature.verification_method; data_path = [IO.Path]::GetFullPath($EventLossReceiptPath); signature_path = [IO.Path]::GetFullPath($EventLossReceiptSignaturePath) }
        execution_binding = [ordered]@{ verified = $true; data_sha256 = $bindingSignature.data_hash; signature_sha256 = $bindingSignature.signature_hash; signer_thumbprint = $bindingSignature.signer_thumbprint; verification_method = $bindingSignature.verification_method; data_path = [IO.Path]::GetFullPath($ExecutionBindingPath); signature_path = [IO.Path]::GetFullPath($ExecutionBindingSignaturePath) }
    }
    event_trace = [ordered]@{ observation_provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'; loss_provider = [string]$loss.provider; available = $eventTraceAvailable; coverage_complete = $eventTraceComplete; lost_events = [int]$loss.lost_events; buffer_loss_events = [int]$loss.buffer_loss_events; events_received = [int]$loss.events_received; loss_accounting = 'etw_provider_counters'; coverage_start_utc = [string]$loss.coverage_start_utc; coverage_end_utc = [string]$loss.coverage_end_utc; trace_id = [string]$loss.trace_id; collector_artifact_path = if ([IO.Path]::IsPathRooted([string]$loss.collector_artifact_path)) { [IO.Path]::GetFullPath([string]$loss.collector_artifact_path) } else { [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $EventLossReceiptPath) ([string]$loss.collector_artifact_path))) }; collector_artifact_sha256 = [string]$loss.collector_artifact_sha256; loss_receipt_path = [IO.Path]::GetFullPath($EventLossReceiptPath); loss_receipt_sha256 = $lossAfter.hash; error = $eventTraceError; loss_receipt_error = $lossAfter.error }
    runner_lease = [ordered]@{ recognized = $true; owner_verified = $true; owner = [string]$leaseReceipt.owner; host = [string]$leaseReceipt.host; lease_id = [string]$leaseReceipt.lease_id; verification_receipt_path = [IO.Path]::GetFullPath($LeaseReceiptPath); verification_receipt_sha256 = $verifiedLeaseAfter.hash; token = '<redacted>' }
    runner_service_identities = @($runnerServiceHashes); execution_binding = [ordered]@{ verified = $true; binding_id = [string]$binding.binding_id; host = [string]$binding.host; runner_job_id = [string]$binding.runner_job_id; source_head = [string]$binding.source_head; source_tree = [string]$binding.source_tree; executable_path = [string]$binding.executable_path; executable_sha256 = [string]$binding.executable_sha256; lease_id = [string]$binding.lease_id; policy_sha256 = [string]$binding.policy_sha256; profiles = @($binding.profiles | ForEach-Object { [ordered]@{ profile = $_.profile; test_identity = $_.test_identity; test_start_utc = $_.test_start_utc; test_end_utc = $_.test_end_utc; ctest_exit_code = $_.ctest_exit_code } }); runner_receipts = @($binding.runner_receipts | ForEach-Object { [ordered]@{ profile = $_.profile; receipt_path = $_.receipt_path; receipt_sha256 = $_.receipt_sha256 } }); binding_receipt_path = [IO.Path]::GetFullPath($ExecutionBindingPath); binding_receipt_sha256 = $bindingAfter.hash }
    samples = @($script:PublicSamples); process_events = @($script:PublicEvents); unrecognized_competitors = 0; unknown_pids = 0; pid_reuse_mismatches = 0
    inventory_summary = [ordered]@{ samples_observed = $script:PublicSamples.Count; events_observed = $script:PublicEvents.Count; unknown_identity_count = 0; unexpected_identity_count = 0; pid_reuse_mismatch_count = 0; query_error_count = $script:QueryErrors.Count; monitor_gap_count = $script:MonitorGaps.Count; raw_inventory_suppressed = $true }
    evidence_blocked = $false; query_errors = @(); fixture_notes = 'Owner-signed observation and ETW loss inputs; raw process inventory is retained only in the private sidecar.'
}
try { Write-WitnessJson $witness $OutputPath } catch { $blocked = New-BlockedWitness $captureStart $captureEnd @('RECEIPT_WRITE_FAILURE') 'RECEIPT_WRITE_FAILURE' @('host witness receipt serialization failed') $script:IdentityPolicyHash $policySignature $verifiedLeaseAfter $lossAfter $bindingAfter $eventTraceAvailable $eventTraceError $privateStatus $script:PublicSamples.Count $script:PublicEvents.Count $script:QueryErrors.Count $script:MonitorGaps.Count; try { Write-WitnessJson $blocked $OutputPath } catch { Write-Error 'Unable to persist the host witness receipt' }; exit 2 }
exit 0
