[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [ValidateSet('Capture', 'OfflineSynthetic')]
    [string]$Mode = 'Capture',
    [ValidateSet('Observe')]
    [string]$Phase = 'Observe',
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
# and subscribes to the Windows process start/stop indication providers. This
# phase verifies only the already-issued policy and lease; it never claims ETW
# losslessness or CTest completion. The post-capture finalizer independently
# verifies the signed ETW parse/loss receipt and execution binding; WMI events
# are an observation stream, not proof of zero loss by themselves.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$script:FixtureMode = [bool]$FixtureMode
$script:QualificationProhibited = ($Mode -eq 'OfflineSynthetic' -or $script:FixtureMode)
$script:IdentityPolicy = $null
$script:SeenPidIdentities = @{}
$script:KnownPidRecords = @{}
$script:LastProcessMap = @{}
$script:ExecutableHashCache = @{}
$script:ActivePidRecords = @{}
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

function Format-UtcText([DateTime]$Value) {
    return $Value.ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffffffZ', [Globalization.CultureInfo]::InvariantCulture)
}

function Get-UtcTicks([string]$Value) {
    return [DateTimeOffset]::Parse($Value, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AssumeUniversal).UtcDateTime.Ticks
}

function Get-UtcNowText {
    return Format-UtcText ([DateTime]::UtcNow)
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

function Get-CanonicalText([object]$Value) {
    if ($null -eq $Value) { return '' }
    if ($Value -is [double] -or $Value -is [decimal]) {
        if ([double]$Value -eq [math]::Truncate([double]$Value)) { return ([int64]$Value).ToString([Globalization.CultureInfo]::InvariantCulture) }
        return ([double]$Value).ToString('R', [Globalization.CultureInfo]::InvariantCulture)
    }
    return [string]$Value
}

function Get-CanonicalBool([object]$Value) {
    if ($Value -is [bool]) { if ($Value) { return 'true' } else { return 'false' } }
    return Get-CanonicalText $Value
}

function Get-CanonicalNodeLine([object]$Node) {
    $services = @((Get-OptionalProperty $Node 'service_identity_sha256' @()) | ForEach-Object { ([string]$_).ToLowerInvariant() } | Sort-Object) -join ','
    $fields = @(
        (Get-OptionalProperty $Node 'pid' $null),
        (Get-OptionalProperty $Node 'process_name' $null),
        (Get-OptionalProperty $Node 'parent_pid' $null),
        (Get-UtcTicks ([string](Get-OptionalProperty $Node 'start_time_utc' '<invalid>'))),
        ([string](Get-OptionalProperty $Node 'executable_path' '')).ToLowerInvariant(),
        ([string](Get-OptionalProperty $Node 'executable_sha256' '')).ToLowerInvariant(),
        $services,
        ([string](Get-OptionalProperty $Node 'identity_digest' '')).ToLowerInvariant()
    )
    return (($fields | ForEach-Object { Get-CanonicalText $_ }) -join [char]0x1f)
}

function Get-CanonicalRecordLine([object]$Record) {
    $ancestry = @((Get-OptionalProperty $Record 'service_ancestry' @()) | ForEach-Object { Get-CanonicalNodeLine $_ }) -join [char]0x1e
    $eventTimestamp = ''
    $recordTimestamp = Get-OptionalProperty $Record 'timestamp_utc' $null
    if ($null -ne $recordTimestamp) { $eventTimestamp = [string](Get-UtcTicks ([string]$recordTimestamp)) }
    $fields = @(
        (Get-OptionalProperty $Record 'pid' $null),
        (Get-OptionalProperty $Record 'process_name' $null),
        (Get-OptionalProperty $Record 'parent_pid' $null),
        (Get-UtcTicks ([string](Get-OptionalProperty $Record 'start_time_utc' '<invalid>'))),
        ([string](Get-OptionalProperty $Record 'executable_path' '')).ToLowerInvariant(),
        ([string](Get-OptionalProperty $Record 'executable_sha256' '')).ToLowerInvariant(),
        (@((Get-OptionalProperty $Record 'service_identity_sha256' @()) | ForEach-Object { ([string]$_).ToLowerInvariant() } | Sort-Object) -join ','),
        (Get-CanonicalBool (Get-OptionalProperty $Record 'service_ancestry_verified' $null)),
        (Get-OptionalProperty $Record 'lineage_verification' $null),
        (Get-CanonicalBool (Get-OptionalProperty $Record 'identity_known' $null)),
        (Get-CanonicalBool (Get-OptionalProperty $Record 'approved_by_policy' $null)),
        ([string](Get-OptionalProperty $Record 'identity_digest' '')).ToLowerInvariant(),
        (Get-OptionalProperty $Record 'decision_reason' $null),
        (Get-OptionalProperty $Record 'event' $null), $eventTimestamp,
        (Get-OptionalProperty $Record 'event_process_name' $null), $ancestry
    )
    return (($fields | ForEach-Object { Get-CanonicalText $_ }) -join [char]0x1f)
}

function Get-CanonicalSampleDigest([object]$Sample) {
    $processes = @((Get-OptionalProperty $Sample 'processes' @()) | ForEach-Object { Get-CanonicalRecordLine $_ } | Sort-Object) -join [char]0x1e
    $events = @((Get-OptionalProperty $Sample 'events' @()) | ForEach-Object { Get-CanonicalRecordLine $_ } | Sort-Object) -join [char]0x1e
    $monotonic = Get-OptionalProperty $Sample 'monotonic_elapsed_ms' $null
    if ($monotonic -is [double] -and $monotonic -eq [math]::Truncate($monotonic)) { $monotonic = [int64]$monotonic }
    elseif ($monotonic -is [double]) { $monotonic = $monotonic.ToString('R', [Globalization.CultureInfo]::InvariantCulture) }
    $lines = @(
        (Get-OptionalProperty $Sample 'phase' $null),
        (Get-OptionalProperty $Sample 'sample_ordinal' $null),
        (Get-UtcTicks ([string](Get-OptionalProperty $Sample 'timestamp_utc' '<invalid>'))),
        (Get-UtcTicks ([string](Get-OptionalProperty $Sample 'scheduled_timestamp_utc' '<invalid>'))),
        $monotonic,
        (Get-OptionalProperty $Sample 'query_error' $null),
        (Get-OptionalProperty $Sample 'full_host_process_count' $null),
        (Get-OptionalProperty $Sample 'full_host_hash_count' $null),
        (Get-OptionalProperty $Sample 'full_host_cim_hash_duration_ms' $null),
        (Get-CanonicalBool (Get-OptionalProperty $Sample 'full_host_cim_hash_cadence_met' $null)),
        ('processes=' + $processes), ('events=' + $events)
    )
    return Get-Sha256Text (($lines | ForEach-Object { Get-CanonicalText $_ }) -join "`n")
}

function Normalize-Thumbprint([string]$Thumbprint) {
    return (($Thumbprint -replace '[\s:]', '').ToUpperInvariant())
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
        # The owner certificate chain, revocation status and CMS signature are
        # checked here. The pinned thumbprint is supplied out-of-band by the
        # owner runbook; it is never read from the JSON being attested.
        $signedCms.CheckSignature($false)
        $chain = New-Object System.Security.Cryptography.X509Certificates.X509Chain
        try {
            $chain.ChainPolicy.RevocationMode = [System.Security.Cryptography.X509Certificates.X509RevocationMode]::Online
            $chain.ChainPolicy.RevocationFlag = [System.Security.Cryptography.X509Certificates.X509RevocationFlag]::EntireChain
            $chain.ChainPolicy.VerificationFlags = [System.Security.Cryptography.X509Certificates.X509VerificationFlags]::NoFlag
            if (-not $chain.Build($certificate)) { throw 'owner certificate chain or revocation check failed' }
        } finally { $chain.Dispose() }
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
            executable_path = 'C:\HAMUNAPTRA\Runner.Listener.exe'; executable_sha256 = ('5' * 64)
            parent_pid = 4; service_identity_sha256 = ('1' * 64)
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

function Test-IdentityPolicyShape([object]$Policy) {
    try {
        $runnerServices = @($Policy.runner_service_identity_sha256)
        if ($null -eq $Policy -or [string]$Policy.format -ne 'VOX_FX_T18_IDENTITY_POLICY_V1' -or
            -not [string]$Policy.host -or [string]$Policy.host -eq '<missing>' -or
            -not [string]$Policy.baseline_id -or [string]$Policy.baseline_id -eq '<missing>' -or
            $runnerServices.Count -ne 4 -or @($runnerServices | Sort-Object -Unique).Count -ne 4 -or
            @($runnerServices | Where-Object { [string]$_ -notmatch '^[0-9a-fA-F]{64}$' }).Count -gt 0 -or
            @($Policy.approved_processes).Count -eq 0 -or @($Policy.denied_process_name_regex).Count -eq 0) { return $false }
        foreach ($approved in @($Policy.approved_processes)) {
            $name = [string](Get-OptionalProperty $approved 'process_name' '')
            $nameRegex = [string](Get-OptionalProperty $approved 'process_name_regex' '')
            if ((-not $name -and -not $nameRegex) -or ($name -and $nameRegex)) { return $false }
            $path = [string](Get-OptionalProperty $approved 'executable_path' '')
            $hash = [string](Get-OptionalProperty $approved 'executable_sha256' '')
            $serviceHash = [string](Get-OptionalProperty $approved 'service_identity_sha256' '')
            $parentPid = Get-OptionalProperty $approved 'parent_pid' $null
            $allowDynamic = Get-OptionalProperty $approved 'allow_dynamic' $null
            $exactPid = Get-OptionalProperty $approved 'pid' $null
            $exactStart = Get-OptionalProperty $approved 'start_time_utc' $null
            $hasPid = $null -ne $exactPid
            $hasStart = $null -ne $exactStart
            $lineageMode = [string](Get-OptionalProperty $approved 'lineage_mode' 'runner_service_ancestry')
            $parentRule = Get-OptionalProperty $approved 'parent_identity' $null
            if (-not $path -or $path.StartsWith('<') -or $path.Contains('*') -or $path.Contains('?') -or
                $hash -notmatch '^[0-9a-fA-F]{64}$' -or $serviceHash -notmatch '^[0-9a-fA-F]{64}$' -or
                $runnerServices -notcontains $serviceHash -or $null -eq $parentPid -or [int]$parentPid -lt 0 -or
                $allowDynamic -isnot [bool] -or $hasPid -ne $hasStart -or $lineageMode -notin @('runner_service_ancestry', 'signed_job_lineage') -or
                ($hasPid -and ([int]$exactPid -lt 1)) -or ((-not $hasPid) -and $allowDynamic -ne $true)) { return $false }
            if ($hasStart) { try { Get-UtcTicks ([string]$exactStart) | Out-Null } catch { return $false } }
            if ($lineageMode -eq 'signed_job_lineage') {
                $job = Get-OptionalProperty $approved 'job_lineage' $null
                if ($null -eq $job -or -not [string](Get-OptionalProperty $job 'issuer' '') -or
                    [string](Get-OptionalProperty $job 'job_identity_sha256' '') -notmatch '^[0-9a-fA-F]{64}$') { return $false }
            }
            if ($null -ne $parentRule) {
                $parentPath = [string](Get-OptionalProperty $parentRule 'executable_path' '')
                $parentHash = [string](Get-OptionalProperty $parentRule 'executable_sha256' '')
                $parentRulePid = Get-OptionalProperty $parentRule 'pid' $null
                $parentRuleStart = Get-OptionalProperty $parentRule 'start_time_utc' $null
                $parentStartInvalid = $false
                if ($null -ne $parentRuleStart) {
                    try { Get-UtcTicks ([string]$parentRuleStart) | Out-Null } catch { $parentStartInvalid = $true }
                }
                if (-not ($parentRule -is [object]) -or
                    -not [string](Get-OptionalProperty $parentRule 'process_name' '') -or
                    -not $parentPath -or $parentPath.StartsWith('<') -or $parentPath.Contains('*') -or $parentPath.Contains('?') -or
                    $parentHash -notmatch '^[0-9a-fA-F]{64}$' -or
                    ($null -ne $parentRulePid -and ([int]$parentRulePid -lt 1)) -or $parentStartInvalid) { return $false }
            } elseif (-not $hasPid -and $lineageMode -in @('runner_service_ancestry', 'signed_job_lineage') -and
                      [string](Get-OptionalProperty $approved 'role' '') -notin @('runner', 'runner_service', 'direct_service')) {
                return $false
            }
        }
        foreach ($deniedRegex in @($Policy.denied_process_name_regex)) {
            if ([string]::IsNullOrWhiteSpace([string]$deniedRegex)) { return $false }
            try { [regex]::new([string]$deniedRegex).Dispose() } catch { return $false }
        }
        foreach ($approved in @($Policy.approved_processes)) {
            $name = [string](Get-OptionalProperty $approved 'process_name' '')
            $nameRegex = [string](Get-OptionalProperty $approved 'process_name_regex' '')
            if ($nameRegex) { try { [regex]::new($nameRegex).Dispose() } catch { return $false } }
            if ($name -and @($Policy.denied_process_name_regex | Where-Object { $name -match [string]$_ }).Count -gt 0) { return $false }
        }
        return $true
    } catch { return $false }
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

function Test-ProcessAgainstPolicy([object]$Record) {
    $result = [ordered]@{ known = $true; approved = $false; pid_reuse = $false; identity_digest = '<unknown>'; reason = 'UNRECOGNIZED_PROCESS_IDENTITY' }
    $pidKey = [string]$Record.pid
    try {
        $digestInput = "$($Record.process_name)|$($Record.executable_path)|$($Record.executable_sha256)|$($Record.parent_pid)|$(Get-UtcTicks ([string]$Record.start_time_utc))|$(@($Record.service_identity_sha256) -join ',')"
        $result.identity_digest = Get-Sha256Text $digestInput
    } catch { $result.known = $false; $result.reason = 'UNKNOWN_PROCESS_IDENTITY' }
    if ($Record.start_time_utc -eq '<unknown>' -or $Record.executable_sha256 -eq '<unknown>' -or
        $Record.service_ancestry_verified -ne $true) {
        $result.known = $false; $result.reason = if ($Record.service_ancestry_verified -ne $true) { 'UNVERIFIED_RUNNER_SERVICE_ANCESTRY' } else { 'UNKNOWN_PROCESS_IDENTITY' }
    }
    if ($script:SeenPidIdentities.ContainsKey($pidKey)) {
        $previous = $script:SeenPidIdentities[$pidKey]
        if ([string]$previous.start_time_ticks -ne [string](Get-UtcTicks ([string]$Record.start_time_utc)) -or
            [string]$previous.identity_digest -ne [string]$result.identity_digest) {
            $result.pid_reuse = $true; $result.reason = 'PID_REUSE_OR_DYNAMIC_IDENTITY_MISMATCH'
        }
    }
    try { $startTicks = Get-UtcTicks ([string]$Record.start_time_utc) } catch { $startTicks = -1 }
    $script:SeenPidIdentities[$pidKey] = [ordered]@{ start_time_ticks = $startTicks; identity_digest = $result.identity_digest }
    $script:KnownPidRecords[$pidKey] = $Record
    if (-not $result.known -or $result.pid_reuse) { return $result }
    foreach ($deniedRegex in @($script:IdentityPolicy.denied_process_name_regex)) {
        if ([string]$Record.process_name -match [string]$deniedRegex) {
            $result.reason = 'DENIED_PROCESS_NAME'
            return $result
        }
    }
    foreach ($approved in @($script:IdentityPolicy.approved_processes)) {
        $name = [string](Get-OptionalProperty $approved 'process_name' '')
        $nameRegex = [string](Get-OptionalProperty $approved 'process_name_regex' '')
        $nameMatches = ($name -and $Record.process_name -ceq $name) -or ($nameRegex -and $Record.process_name -match $nameRegex)
        if (-not $nameMatches) { continue }
        $approvedPath = [string](Get-OptionalProperty $approved 'executable_path' '')
        if ($Record.executable_path -ine $approvedPath) { continue }
        $hash = [string](Get-OptionalProperty $approved 'executable_sha256' '')
        if ($Record.executable_sha256 -ine $hash) { continue }
        $serviceHash = [string](Get-OptionalProperty $approved 'service_identity_sha256' '')
        if (-not (@($Record.service_identity_sha256) -contains $serviceHash)) { continue }
        $parentPid = Get-OptionalProperty $approved 'parent_pid' $null
        if ($null -ne $parentPid -and [int]$parentPid -ne [int]$Record.parent_pid) { continue }
        $parentRule = Get-OptionalProperty $approved 'parent_identity' $null
        if ($null -ne $parentRule) {
            $parentIdentity = Get-OptionalProperty $Record 'parent_identity' $null
            if ($null -eq $parentIdentity -or
                [string](Get-OptionalProperty $parentIdentity 'process_name' '') -ine [string](Get-OptionalProperty $parentRule 'process_name' '') -or
                [string](Get-OptionalProperty $parentIdentity 'executable_path' '') -ine [string](Get-OptionalProperty $parentRule 'executable_path' '') -or
                [string](Get-OptionalProperty $parentIdentity 'executable_sha256' '') -ine [string](Get-OptionalProperty $parentRule 'executable_sha256' '')) { continue }
            $parentRulePid = Get-OptionalProperty $parentRule 'pid' $null
            if ($null -ne $parentRulePid -and [int]$parentRulePid -ne [int](Get-OptionalProperty $parentIdentity 'pid' 0)) { continue }
            $parentRuleStart = Get-OptionalProperty $parentRule 'start_time_utc' $null
            if ($null -ne $parentRuleStart -and (Get-UtcTicks ([string]$parentRuleStart)) -ne (Get-UtcTicks ([string](Get-OptionalProperty $parentIdentity 'start_time_utc' '<unknown>')))) { continue }
        } elseif ([string](Get-OptionalProperty $approved 'lineage_mode' 'runner_service_ancestry') -eq 'runner_service_ancestry' -and
                  $null -ne (Get-OptionalProperty $Record 'parent_identity' $null) -and
                  [bool](Get-OptionalProperty $approved 'allow_dynamic' $false)) {
            # A dynamic native child may not be approved by a PID-only rule.
            continue
        }
        $exactPid = Get-OptionalProperty $approved 'pid' $null
        $exactStart = Get-OptionalProperty $approved 'start_time_utc' $null
        if ($null -ne $exactPid -and [int]$exactPid -ne [int]$Record.pid) { continue }
        if ($null -ne $exactStart -and (Get-UtcTicks ([string]$exactStart)) -ne (Get-UtcTicks ([string]$Record.start_time_utc))) { continue }
        $dynamic = [bool](Get-OptionalProperty $approved 'allow_dynamic' $false)
        if (($null -eq $exactPid -and -not $dynamic) -or ($null -ne $exactPid -and $null -eq $exactStart)) { continue }
        $result.approved = $true; $result.reason = $null
        return $result
    }
    return $result
}

function Get-ProcessStartUtc($Process) {
    try {
        return Format-UtcText ([System.Management.ManagementDateTimeConverter]::ToDateTime(
            [string]$Process.CreationDate).ToUniversalTime())
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

function Get-ProcessNode($Process, [hashtable]$ServiceMap) {
    $path = [string](Get-OptionalProperty $Process 'ExecutablePath' '')
    $serviceHashes = @()
    if ($null -ne $ServiceMap -and $ServiceMap.ContainsKey([string]$Process.ProcessId)) {
        $serviceHashes = @($ServiceMap[[string]$Process.ProcessId] | Sort-Object -Unique)
    }
    $executableHash = '<unknown>'
    if ($path) {
        if ($script:ExecutableHashCache.ContainsKey($path)) {
            $executableHash = $script:ExecutableHashCache[$path]
        } else {
            $executableHash = Get-HashOrUnknown $path
            $script:ExecutableHashCache[$path] = $executableHash
        }
    }
    $node = [ordered]@{
        pid = [int]$Process.ProcessId
        process_name = [string](Get-OptionalProperty $Process 'Name' '<unknown>')
        parent_pid = [int](Get-OptionalProperty $Process 'ParentProcessId' 0)
        start_time_utc = Get-ProcessStartUtc $Process
        executable_path = if ($path) { $path } else { '<unknown>' }
        executable_sha256 = $executableHash
        service_identity_sha256 = @($serviceHashes)
    }
    try {
        $digestInput = "$($node.process_name)|$($node.executable_path)|$($node.executable_sha256)|$($node.parent_pid)|$(Get-UtcTicks ([string]$node.start_time_utc))|$(@($node.service_identity_sha256) -join ',')"
        $node.identity_digest = Get-Sha256Text $digestInput
    } catch { $node.identity_digest = '<unknown>' }
    return $node
}

function Get-ProcessRecord($Process, [hashtable]$ServiceMap, [hashtable]$ProcessMap) {
    $chain = @(); $cursor = $Process; $visited = @{}; $root = $null
    while ($null -ne $cursor) {
        $pidKey = [string]$cursor.ProcessId
        if ($visited.ContainsKey($pidKey)) { throw 'process parent chain contains a cycle' }
        $visited[$pidKey] = $true
        $node = Get-ProcessNode $cursor $ServiceMap
        $chain += $node
        $runnerSet = @($script:IdentityPolicy.runner_service_identity_sha256)
        if (@($node.service_identity_sha256 | Where-Object { $runnerSet -contains $_ }).Count -gt 0) {
            $root = $node
            break
        }
        $parentPid = [int]$node.parent_pid
        if ($parentPid -lt 1 -or $null -eq $ProcessMap -or -not $ProcessMap.ContainsKey([string]$parentPid)) {
            break
        }
        $cursor = $ProcessMap[[string]$parentPid]
    }
    $verified = $null -ne $root
    if (-not $verified) {
        $rootServices = @()
    } else {
        $rootServices = @($root.service_identity_sha256 | Sort-Object -Unique)
    }
    $record = [ordered]@{
        pid = [int]$Process.ProcessId
        process_name = [string](Get-OptionalProperty $Process 'Name' '<unknown>')
        parent_pid = [int](Get-OptionalProperty $Process 'ParentProcessId' 0)
        start_time_utc = Get-ProcessStartUtc $Process
        executable_path = [string](Get-OptionalProperty $Process 'ExecutablePath' '<unknown>')
        executable_sha256 = Get-HashOrUnknown ([string](Get-OptionalProperty $Process 'ExecutablePath' ''))
        service_identity_sha256 = @($rootServices)
        service_ancestry_verified = [bool]$verified
        lineage_verification = 'runner_service_ancestry_v1'
        service_ancestry = @($chain)
    }
    if ($chain.Count -gt 1) { $record.parent_identity = $chain[1] }
    else { $record.parent_identity = $null }
    return $record
}

function Get-ProcessSnapshot {
    $public = @(); $raw = @(); $queryStart = Get-MonotonicSeconds; $serviceMap = Get-ServiceIdentityMap
    $script:ExecutableHashCache = @{}
    if ($null -eq $serviceMap) {
        $elapsed = [math]::Round(((Get-MonotonicSeconds) - $queryStart) * 1000.0, 3)
        return @{ error = 'SERVICE_IDENTITY_QUERY_FAILURE'; processes = @(); raw = @(); unknown = 0; unexpected = 0; pid_reuse = 0; services = @()
            full_host_process_count = 0; full_host_hash_count = 0; full_host_cim_hash_duration_ms = $elapsed; full_host_cim_hash_cadence_met = $false }
    }
    try {
        $allProcesses = @(Get-CimInstance -ClassName Win32_Process -ErrorAction Stop)
        $processMap = @{}
        foreach ($candidate in $allProcesses) { $processMap[[string]$candidate.ProcessId] = $candidate }
        $script:LastProcessMap = $processMap
        foreach ($process in $allProcesses) {
            $record = Get-ProcessRecord $process $serviceMap $processMap
            $decision = Test-ProcessAgainstPolicy $record
            $record.identity_known = [bool]$decision.known
            $record.approved_by_policy = [bool]$decision.approved
            $record.identity_digest = $decision.identity_digest
            $record.decision_reason = $decision.reason
            # Retain every resolved lifecycle, including denied competitors, so
            # a later stop event proves the same PID/start identity rather than
            # degrading into an unparsed stop.
            $script:ActivePidRecords[[string]$record.pid] = $record
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
        $elapsed = [math]::Round(((Get-MonotonicSeconds) - $queryStart) * 1000.0, 3)
    } catch {
        $elapsed = [math]::Round(((Get-MonotonicSeconds) - $queryStart) * 1000.0, 3)
        return @{ error = 'PROCESS_QUERY_FAILURE'; processes = @(); raw = @(); unknown = 0; unexpected = 0; pid_reuse = 0; services = @()
            full_host_process_count = 0; full_host_hash_count = 0; full_host_cim_hash_duration_ms = $elapsed; full_host_cim_hash_cadence_met = $false }
    }
    $hashCount = @($raw | Where-Object { [string]$_.executable_sha256 -match '^[0-9a-fA-F]{64}$' }).Count
    return @{ error = $null; processes = @($public); raw = @($raw); unknown = @($raw | Where-Object { $_.identity_known -ne $true }).Count
        unexpected = @($raw | Where-Object { $_.approved_by_policy -ne $true }).Count
        pid_reuse = @($raw | Where-Object { $_.decision_reason -eq 'PID_REUSE_OR_DYNAMIC_IDENTITY_MISMATCH' }).Count
        services = @($raw | ForEach-Object { @($_.service_identity_sha256) } | Where-Object { $_ } | Sort-Object -Unique)
        full_host_process_count = $allProcesses.Count; full_host_hash_count = $hashCount
        full_host_cim_hash_duration_ms = $elapsed; full_host_cim_hash_cadence_met = ($elapsed -le 1000.0) }
}

function Get-EventTimeUtc($NewEvent) {
    if ($null -eq $NewEvent.TIME_CREATED) { throw 'process event has no provider timestamp' }
    return Format-UtcText ([DateTime]::FromFileTimeUtc([Int64]$NewEvent.TIME_CREATED))
}

function Get-EventRecords([string]$SourceIdentifier) {
    $public = @(); $raw = @(); $unknown = 0; $unexpected = 0; $reuse = 0
    foreach ($event in @(Get-Event -SourceIdentifier $SourceIdentifier -ErrorAction SilentlyContinue)) {
        try {
            $newEvent = $event.SourceEventArgs.NewEvent
            $pidValue = [int]$newEvent.ProcessID
            $eventName = [string]$newEvent.ProcessName
            $record = $null
            $eventKind = if ($SourceIdentifier -like '*start*') { 'start' } else { 'stop' }
            $cached = if ($script:KnownPidRecords.ContainsKey([string]$pidValue)) { $script:KnownPidRecords[[string]$pidValue].PSObject.Copy() } else { $null }
            if ($cached -and [string]$cached.process_name -cne $eventName) {
                throw 'process event name disagrees with cached PID identity'
            }
            if ($eventKind -eq 'start') {
                $candidate = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId=$pidValue" -ErrorAction SilentlyContinue
                if (-not $candidate) { throw 'process start identity could not be resolved while the PID was live' }
                $candidateRecord = Get-ProcessRecord $candidate (Get-ServiceIdentityMap) $script:LastProcessMap
                if ($cached -and ([string]$candidateRecord.start_time_utc -cne [string]$cached.start_time_utc -or
                                  [string]$candidateRecord.executable_sha256 -ine [string]$cached.executable_sha256 -or
                                  [string]$candidateRecord.executable_path -ine [string]$cached.executable_path -or
                                  [int]$candidateRecord.parent_pid -ne [int]$cached.parent_pid)) {
                    throw 'process start reused a PID with a different identity'
                }
                $record = $candidateRecord
                $script:ActivePidRecords[[string]$pidValue] = $record
            } elseif ($cached -and $script:ActivePidRecords.ContainsKey([string]$pidValue)) {
                $record = $cached
                [void]$script:ActivePidRecords.Remove([string]$pidValue)
            }
            if ($null -eq $record) { throw 'process event identity could not be reconciled to a live PID lifecycle' }
            $record.event = $eventKind
            $record.event_process_name = $eventName
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
        canonicalization = 'fx-t18-private-sample-canonical-v2-utc-ticks'
        cadence_contract = 'full_host_cim_process_snapshot_and_sha256_each_executable_le_1000ms_every_1000ms'
        correlation_max_skew_seconds = 2.0
        correlation_identity_fields = @('event','pid','process_name','start_time_utc','identity_digest','executable_path','executable_sha256','parent_pid','service_identity_sha256')
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

function New-ObservationReceipt(
    [string]$CaptureStartUtc, [string]$CaptureEndUtc, [object]$PolicySignature,
    [object]$LeaseStatus, [object]$PrivateInventoryStatus, [string[]]$ReasonCodes,
    [string[]]$ReasonSummaries, [bool]$EventTraceAvailable, [string]$EventTraceError
) {
    $leaseReceipt = Get-OptionalProperty $LeaseStatus 'receipt' $null
    $serviceSet = @($script:ObservedServiceHashes.Keys | Sort-Object)
    return [ordered]@{
        format = 'VOX_FX_T18_HOST_OBSERVATION_V1'; schema_version = 1; mode = 'observe'; phase = 'observe'
        fixture_mode = $script:FixtureMode; qualification_prohibited = $script:QualificationProhibited
        finalization_required = $true; evidence_blocked = $true
        host_observation_mode = 'process_snapshot_and_event_trace'; sample_interval_ms = 1000
        pre_seconds = $PreSeconds; during_seconds = $DuringSeconds; post_seconds = $PostSeconds
        capture_start_utc = $CaptureStartUtc; capture_end_utc = $CaptureEndUtc
        identity_policy = [ordered]@{ format = $script:IdentityPolicy.format; policy_id = $script:IdentityPolicy.policy_id; baseline_id = $script:IdentityPolicy.baseline_id; policy_sha256 = $script:IdentityPolicyHash; runner_service_count = @($script:IdentityPolicy.runner_service_identity_sha256).Count; approved_process_count = @($script:IdentityPolicy.approved_processes).Count }
        identity_policy_path = [IO.Path]::GetFullPath($IdentityPolicyPath); identity_policy_sha256 = $script:IdentityPolicyHash
        private_inventory_path = [IO.Path]::GetFullPath($PrivateInventoryPath); private_inventory_sha256 = [string](Get-OptionalProperty $PrivateInventoryStatus 'hash' '<missing>')
        attestation_proofs = [ordered]@{
            policy = [ordered]@{ verified = [bool]$PolicySignature.verified; data_sha256 = $PolicySignature.data_hash; signature_sha256 = $PolicySignature.signature_hash; signer_thumbprint = $PolicySignature.signer_thumbprint; verification_method = $PolicySignature.verification_method; data_path = [IO.Path]::GetFullPath($IdentityPolicyPath); signature_path = [IO.Path]::GetFullPath($IdentityPolicySignaturePath) }
            lease = [ordered]@{ verified = [bool](Get-OptionalProperty $LeaseStatus 'verified' $false); data_sha256 = [string](Get-OptionalProperty (Get-OptionalProperty $LeaseStatus 'signature' $null) 'data_hash' '<missing>'); signature_sha256 = [string](Get-OptionalProperty (Get-OptionalProperty $LeaseStatus 'signature' $null) 'signature_hash' '<missing>'); signer_thumbprint = '<pending-finalization>'; verification_method = 'pending_post_capture_owner_cms' }
            event_loss = [ordered]@{ verified = $false; status = 'pending_post_capture_owner_parse_and_signature'; verification_method = 'pending_post_capture_owner_cms' }
            execution_binding = [ordered]@{ verified = $false; status = 'pending_post_capture_owner_binding_and_signature'; verification_method = 'pending_post_capture_owner_cms' }
        }
        event_trace = [ordered]@{ observation_provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'; loss_provider = 'pending_post_capture_owner_etw_parse'; available = $EventTraceAvailable; coverage_complete = $false; loss_accounting = 'pending_post_capture_owner_etw_parse'; coverage_start_utc = $CaptureStartUtc; coverage_end_utc = $CaptureEndUtc; collector_artifact_path = '<pending-finalization>'; collector_artifact_sha256 = '<pending>'; collector_parse_receipt_path = '<pending-finalization>'; collector_parse_receipt_sha256 = '<pending>'; loss_receipt_path = '<pending-finalization>'; loss_receipt_sha256 = '<pending>'; correlation_max_skew_seconds = 2.0; correlation_identity_fields = @('event','pid','process_name','start_time_utc','identity_digest','executable_path','executable_sha256','parent_pid','service_identity_sha256'); error = $EventTraceError }
        runner_lease = [ordered]@{ recognized = [bool](Get-OptionalProperty $LeaseStatus 'verified' $false); owner_verified = [bool](Get-OptionalProperty $LeaseStatus 'verified' $false); owner = if ($leaseReceipt) { [string]$leaseReceipt.owner } else { '<missing>' }; host = if ($leaseReceipt) { [string]$leaseReceipt.host } else { [string]$env:COMPUTERNAME }; lease_id = if ($leaseReceipt) { [string]$leaseReceipt.lease_id } else { '<missing>' }; verification_receipt_path = [IO.Path]::GetFullPath($LeaseReceiptPath); verification_receipt_sha256 = [string](Get-OptionalProperty $LeaseStatus 'hash' '<missing>'); token = '<redacted>' }
        runner_service_identities = @($serviceSet)
        execution_binding = [ordered]@{ verified = $false; qualification_prohibited = $script:QualificationProhibited; binding_receipt_path = '<pending-finalization>'; binding_receipt_sha256 = '<pending>' }
        samples = @($script:PublicSamples); process_events = @($script:PublicEvents)
        unrecognized_competitors = $script:UnrecognizedCount; unknown_pids = $script:UnknownCount; pid_reuse_mismatches = $script:PidReuseCount
        inventory_summary = [ordered]@{ samples_observed = $script:PublicSamples.Count; events_observed = $script:PublicEvents.Count; unknown_identity_count = $script:UnknownCount; unexpected_identity_count = $script:UnrecognizedCount; pid_reuse_mismatch_count = $script:PidReuseCount; query_error_count = $script:QueryErrors.Count; monitor_gap_count = $script:MonitorGaps.Count; raw_inventory_suppressed = $true }
        query_errors = @($ReasonCodes); diagnostic = [ordered]@{ error_category = 'FINALIZATION_REQUIRED'; reason_codes = @($ReasonCodes + 'FINALIZATION_REQUIRED'); error_summaries = @($ReasonSummaries + 'capture observation is intentionally blocked until post-capture ETW and CTest proofs are finalized'); sanitized = $true; recommended_exit_code = 2 }
        fixture_notes = 'Observation receipt is not qualification evidence; owner post-capture ETW parsing, binding, detached signatures and final reconciliation are required.'
    }
}

function New-OfflineWitness {
    $base = [DateTime]::Parse('2026-11-22T00:00:00Z').ToUniversalTime()
    $events = @(); $competitors = 0; $unknowns = 0
    if ($SyntheticCompetitor) {
        $events += [ordered]@{ event = 'start'; timestamp_utc = Format-UtcText ($base.AddSeconds(1.1)); identity_known = $true; approved_by_policy = $false; identity_digest = ('c' * 64); decision_reason = 'UNRECOGNIZED_PROCESS_IDENTITY' }
        $events += [ordered]@{ event = 'stop'; timestamp_utc = Format-UtcText ($base.AddSeconds(1.7)); identity_known = $true; approved_by_policy = $false; identity_digest = ('c' * 64); decision_reason = 'UNRECOGNIZED_PROCESS_IDENTITY' }
        $competitors = 1
    }
    if ($SyntheticUnknownPid) {
        $events += [ordered]@{ event = 'start'; timestamp_utc = Format-UtcText ($base.AddSeconds(2.1)); identity_known = $false; approved_by_policy = $false; identity_digest = '<unknown>'; decision_reason = 'UNKNOWN_PROCESS_IDENTITY' }
        $unknowns = 1
    }
    $policy = New-OfflinePolicy
    $samples = @()
    for ($index = 0; $index -le 122; $index++) {
        $phase = if ($index -le 30) { 'pre' } elseif ($index -le 91) { 'during' } else { 'post' }
        $samples += [ordered]@{ phase = $phase; sample_ordinal = $index; timestamp_utc = Format-UtcText ($base.AddSeconds($index))
            scheduled_timestamp_utc = Format-UtcText ($base.AddSeconds($index)); monotonic_elapsed_ms = $index * 1000
            full_host_process_count = 1; full_host_hash_count = 1; full_host_cim_hash_duration_ms = 10.0; full_host_cim_hash_cadence_met = $true
            observed_process_count = 1; approved_process_count = 1; unknown_identity_count = 0
            unexpected_identity_count = 0; pid_reuse_mismatch_count = 0; process_query_error = $null
            process_inventory_sha256 = ('a' * 64) }
    }
    return [ordered]@{
        format = 'VOX_FX_T18_HOST_WITNESS_V1'; schema_version = 2
        mode = 'offline_synthetic'; fixture_mode = $true; qualification_prohibited = $true
        host_observation_mode = 'process_snapshot_and_event_trace'
        sample_interval_ms = 1000; pre_seconds = 30; during_seconds = 60; post_seconds = 30
        capture_start_utc = Format-UtcText $base; capture_end_utc = Format-UtcText ($base.AddSeconds(122))
        monitor_gaps = @(); identity_policy = $policy; identity_policy_sha256 = ('f' * 64)
        identity_policy_path = '<fixture-not-an-attestation>'; private_inventory_path = '<fixture-private-not-retained>'
        event_trace = [ordered]@{ observation_provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'; loss_provider = 'fixture-no-loss-proof'
            available = $true; coverage_complete = $false; lost_events = $null; buffer_loss_events = $null
            loss_accounting = 'unverified_fixture'; coverage_start_utc = Format-UtcText $base; coverage_end_utc = Format-UtcText ($base.AddSeconds(122))
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
        event_trace = [ordered]@{ observation_provider = 'Win32_ProcessStartTrace/Win32_ProcessStopTrace'; loss_provider = 'Microsoft-Windows-Kernel-Process/ETW'; available = [bool]$EventTraceAvailable; coverage_complete = $false; lost_events = $null; buffer_loss_events = $null; loss_accounting = 'unknown'; coverage_start_utc = '<unknown>'; coverage_end_utc = '<unknown>'; correlation_max_skew_seconds = 2.0; correlation_identity_fields = @('event','pid','process_name','start_time_utc','identity_digest','executable_path','executable_sha256','parent_pid','service_identity_sha256'); loss_receipt_path = '<redacted>'; loss_receipt_sha256 = '<redacted>'; error = $EventTraceError; loss_receipt_error = [string](Get-OptionalProperty $LossStatus 'error' 'event-loss attestation unavailable') }
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

# Preflight: only the already-issued policy and lease are verified before
# observation. ETW loss and execution-binding facts are deliberately absent
# until the interval closes. Every pre-capture CMS proof is checked against a
# thumbprint supplied out-of-band by the owner. FixtureMode is explicit and is
# never allowed to reach a production evaluator pass.
$script:IdentityPolicy = Get-IdentityPolicy
$script:IdentityPolicyHash = '<missing>'
if ($IdentityPolicyPath -and (Test-Path -LiteralPath $IdentityPolicyPath -PathType Leaf)) {
    try { $script:IdentityPolicyHash = (Get-FileHash -LiteralPath $IdentityPolicyPath -Algorithm SHA256).Hash.ToLowerInvariant() } catch { $script:IdentityPolicyHash = '<invalid>' }
}
$policySignature = Get-VerifiedOwnerSignature $IdentityPolicyPath $IdentityPolicySignaturePath $OwnerCertificateThumbprint 'IDENTITY_POLICY'
$leaseSignature = Get-VerifiedOwnerSignature $LeaseReceiptPath $LeaseReceiptSignaturePath $OwnerCertificateThumbprint 'LEASE_RECEIPT'

$policyCodes = @(); $policySummaries = @()
$format = [string](Get-OptionalProperty $script:IdentityPolicy 'format' 'invalid')
$policyId = [string](Get-OptionalProperty $script:IdentityPolicy 'policy_id' '<missing>')
$baselineId = [string](Get-OptionalProperty $script:IdentityPolicy 'baseline_id' '<missing>')
$policyServices = @($script:IdentityPolicy.runner_service_identity_sha256)
$policyEntries = @($script:IdentityPolicy.approved_processes)
$policyValid = ($format -eq 'VOX_FX_T18_IDENTITY_POLICY_V1' -and $policyId -ne '<missing>' -and $baselineId -ne '<missing>' -and
    $script:IdentityPolicy.owner_approved -eq $true -and $script:IdentityPolicyHash -match '^[0-9a-fA-F]{64}$' -and
    $policyServices.Count -eq 4 -and @($policyServices | Where-Object { [string]$_ -notmatch '^[0-9a-fA-F]{64}$' }).Count -eq 0 -and
    (Test-IdentityPolicyShape $script:IdentityPolicy))
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
$lossReceipt = [ordered]@{ verified = $false; pending = $true; receipt = $null; hash = '<pending>'; signature = $null; error_code = 'EVENT_LOSS_FINALIZATION_PENDING'; error = 'post-capture owner ETW parse and signature are pending' }
$bindingStatus = [ordered]@{ verified = $false; pending = $true; binding = $null; hash = '<pending>'; signature = $null; error_code = 'EXECUTION_BINDING_FINALIZATION_PENDING'; error = 'post-capture owner CTest binding and signature are pending' }
$preflightCodes = @($policyCodes); $preflightSummaries = @($policySummaries)
if (-not [bool](Get-OptionalProperty $verifiedLease 'verified' $false)) { $preflightCodes += [string](Get-OptionalProperty $verifiedLease 'error_code' 'LEASE_RECEIPT_INVALID'); $preflightSummaries += [string](Get-OptionalProperty $verifiedLease 'error' 'owner lease is unavailable') }
$leaseReceipt = Get-OptionalProperty $verifiedLease 'receipt' $null
$leasePolicyHash = [string](Get-OptionalProperty $leaseReceipt 'policy_sha256' '')
if ([bool](Get-OptionalProperty $verifiedLease 'verified' $false) -and $leasePolicyHash -ine $script:IdentityPolicyHash) { $preflightCodes += 'LEASE_POLICY_MISMATCH'; $preflightSummaries += 'owner lease is bound to a different identity baseline' }
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
$script:ActivePidRecords = @{}
$script:QueryErrors = @(); $script:QueryErrorCodes = @(); $script:UnrecognizedCount = 0; $script:UnknownCount = 0; $script:PidReuseCount = 0; $script:ObservedServiceHashes = @{}
$script:SampleOrdinal = 0
$script:CaptureMonotonicOrigin = Get-MonotonicSeconds
$script:CaptureUtcOrigin = [DateTime]::UtcNow
$script:NextDeadlineSeconds = 0.0
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
        $phaseOffset = $script:NextDeadlineSeconds
        for ($index = 0; $index -le $Seconds; $index++) {
            $expectedElapsed = $phaseOffset + $index
            $expected = $script:CaptureMonotonicOrigin + $expectedElapsed
            $snapshot = Get-ProcessSnapshot
            $observed = Get-MonotonicSeconds
            if ($observed - $expected -gt 2.0) { $script:MonitorGaps += [ordered]@{ phase = $Phase; sample_ordinal = $script:SampleOrdinal; late_by_ms = (($observed - $expected) * 1000.0) } }
            if ($snapshot.error) { $script:QueryErrors += [string]$snapshot.error; $script:QueryErrorCodes += [string]$snapshot.error }
            $startEvents = @{ public = @(); raw = @(); unknown = 0; unexpected = 0; reuse = 0 }
            $stopEvents = @{ public = @(); raw = @(); unknown = 0; unexpected = 0; reuse = 0 }
            if ($eventTraceAvailable) {
                $startEvents = Get-EventRecords $startSource; $stopEvents = Get-EventRecords $stopSource
            }
            $monotonicElapsed = [math]::Round(($observed - $script:CaptureMonotonicOrigin) * 1000.0, 3)
            $rawSample = [ordered]@{ phase = $Phase; sample_ordinal = $script:SampleOrdinal; timestamp_utc = Get-UtcNowText; scheduled_timestamp_utc = Format-UtcText ($script:CaptureUtcOrigin.AddSeconds($expectedElapsed)); monotonic_elapsed_ms = $monotonicElapsed; query_error = $snapshot.error; full_host_process_count = [int]$snapshot.full_host_process_count; full_host_hash_count = [int]$snapshot.full_host_hash_count; full_host_cim_hash_duration_ms = [double]$snapshot.full_host_cim_hash_duration_ms; full_host_cim_hash_cadence_met = [bool]$snapshot.full_host_cim_hash_cadence_met; processes = @($snapshot.raw); events = @($startEvents.raw) + @($stopEvents.raw) }
            $inventoryHash = Get-CanonicalSampleDigest $rawSample
            $script:RawSamples += $rawSample
            $script:RawEvents += @($startEvents.raw) + @($stopEvents.raw)
            $script:PublicSamples += [ordered]@{ phase = $Phase; sample_ordinal = $script:SampleOrdinal; timestamp_utc = $rawSample.timestamp_utc; scheduled_timestamp_utc = $rawSample.scheduled_timestamp_utc; monotonic_elapsed_ms = $rawSample.monotonic_elapsed_ms; full_host_process_count = [int]$snapshot.full_host_process_count; full_host_hash_count = [int]$snapshot.full_host_hash_count; full_host_cim_hash_duration_ms = [double]$snapshot.full_host_cim_hash_duration_ms; full_host_cim_hash_cadence_met = [bool]$snapshot.full_host_cim_hash_cadence_met; observed_process_count = @($snapshot.raw).Count; approved_process_count = @($snapshot.raw | Where-Object { $_.approved_by_policy -eq $true }).Count; unknown_identity_count = $snapshot.unknown + $startEvents.unknown + $stopEvents.unknown; unexpected_identity_count = $snapshot.unexpected + $startEvents.unexpected + $stopEvents.unexpected; pid_reuse_mismatch_count = $snapshot.pid_reuse + $startEvents.reuse + $stopEvents.reuse; process_query_error = $snapshot.error; process_inventory_sha256 = $inventoryHash }
            $script:PublicEvents += @($startEvents.public) + @($stopEvents.public)
            $script:SampleOrdinal++
            if ($index -lt $Seconds) {
                $nextExpected = $script:CaptureMonotonicOrigin + $expectedElapsed + 1.0
                $remaining = $nextExpected - (Get-MonotonicSeconds)
                if ($remaining -gt 0.0) { Start-Sleep -Milliseconds ([math]::Max(1, [math]::Floor($remaining * 1000.0))) }
            }
        }
        $script:NextDeadlineSeconds += $Seconds + 1
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
if ($Phase -eq 'Observe') {
    $observationCodes = @($captureFailureCodes + $script:QueryErrorCodes)
    $observationSummaries = @($captureFailureSummaries)
    $leaseAfterObserve = Get-VerifiedLeaseReceipt $leaseSignature $false
    if (-not [bool](Get-OptionalProperty $privateStatus 'written' $false)) { $observationCodes += 'PRIVATE_INVENTORY_WRITE_FAILURE'; $observationSummaries += 'owner-private inventory could not be retained' }
    if (-not [bool](Get-OptionalProperty $leaseAfterObserve 'verified' $false)) { $observationCodes += [string](Get-OptionalProperty $leaseAfterObserve 'error_code' 'LEASE_RECEIPT_INVALID'); $observationSummaries += [string](Get-OptionalProperty $leaseAfterObserve 'error' 'owner lease became unavailable') }
    if ([bool](Get-OptionalProperty $leaseAfterObserve 'verified' $false)) {
        try {
            if ([DateTime]::Parse([string]$leaseAfterObserve.receipt.valid_until_utc).ToUniversalTime() -lt [DateTime]::Parse($captureEnd).ToUniversalTime()) { $observationCodes += 'LEASE_COVERAGE_SHORT'; $observationSummaries += 'owner lease ended before observation completion' }
        } catch { $observationCodes += 'LEASE_RECEIPT_INVALID'; $observationSummaries += 'owner lease completion time is invalid' }
    }
    try {
        $policyStartObserve = [DateTime]::Parse([string]$script:IdentityPolicy.valid_from_utc).ToUniversalTime()
        $policyEndObserve = [DateTime]::Parse([string]$script:IdentityPolicy.valid_until_utc).ToUniversalTime()
        if ($policyStartObserve -gt [DateTime]::Parse($captureStart).ToUniversalTime() -or $policyEndObserve -lt [DateTime]::Parse($captureEnd).ToUniversalTime()) { $observationCodes += 'IDENTITY_POLICY_COVERAGE_SHORT'; $observationSummaries += 'owner identity baseline did not cover the observation interval' }
    } catch { $observationCodes += 'IDENTITY_POLICY_INTERVAL_INVALID'; $observationSummaries += 'owner identity baseline interval is invalid' }
    if (-not $eventTraceAvailable) { $observationCodes += 'EVENT_PROVIDER_UNAVAILABLE'; $observationSummaries += 'process event provider was unavailable' }
    if ($script:MonitorGaps.Count -gt 0) { $observationCodes += 'MONITOR_GAP_OR_MISSING_1HZ_SAMPLE'; $observationSummaries += 'one or more monotonic one-Hz deadlines were missed' }
    if ($script:UnknownCount -gt 0) { $observationCodes += 'UNKNOWN_PROCESS_IDENTITY'; $observationSummaries += 'one or more process identities were unknown' }
    if ($script:UnrecognizedCount -gt 0) { $observationCodes += 'UNRECOGNIZED_PROCESS_IDENTITY'; $observationSummaries += 'one or more observed identities were not in the owner baseline' }
    if ($script:PidReuseCount -gt 0) { $observationCodes += 'PID_REUSE_OR_DYNAMIC_IDENTITY_MISMATCH'; $observationSummaries += 'PID reuse or dynamic identity mismatch was observed' }
    $policyServiceSetObserve = @($policyServices | Sort-Object -Unique); $observedServiceSetObserve = @($script:ObservedServiceHashes.Keys | Sort-Object -Unique)
    if ($policyServiceSetObserve.Count -ne 4 -or (($policyServiceSetObserve | Where-Object { $observedServiceSetObserve -notcontains $_ }).Count -gt 0)) { $observationCodes += 'HAMUNAPTRA_RUNNER_SERVICE_BASELINE_MISMATCH'; $observationSummaries += 'the four owner-baselined HAMUNAPTRA runner service identities were not reconciled' }
    $observationCodes = @($observationCodes | Where-Object { $_ } | Select-Object -Unique)
    $observationSummaries = @($observationSummaries | Where-Object { $_ } | Select-Object -Unique)
    if ($observationCodes.Count -gt 0) {
        $blocked = New-BlockedWitness $captureStart $captureEnd $observationCodes 'CAPTURE_BLOCKED' $observationSummaries $script:IdentityPolicyHash $policySignature $leaseAfterObserve $lossReceipt $bindingStatus $eventTraceAvailable $eventTraceError $privateStatus $script:PublicSamples.Count $script:PublicEvents.Count $script:QueryErrors.Count $script:MonitorGaps.Count
        try { Write-WitnessJson $blocked $OutputPath } catch { Write-Error 'Unable to persist the blocked host observation receipt' }
        exit 2
    }
    $observation = New-ObservationReceipt $captureStart $captureEnd $policySignature $leaseAfterObserve $privateStatus @() @() $eventTraceAvailable $eventTraceError
    try { Write-WitnessJson $observation $OutputPath } catch { Write-Error 'Unable to persist the host observation receipt'; exit 2 }
    exit 2
}
# Production Capture is intentionally observation-only. Post-capture ETW/CTest
# proofs are verified and assembled by finalize-fx-t18-host-witness.py.
exit 2
