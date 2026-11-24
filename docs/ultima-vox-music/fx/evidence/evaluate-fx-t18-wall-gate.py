#!/usr/bin/env python3
"""Evaluate the proposed FX-T18 wall-only gate.

The evaluator is fail-closed.  It keeps the existing native CTest/JUnit and
functional precedence, but a proposed wall status also requires a complete
host-wide witness, an owner-signed baseline/lease/loss/binding set, and a
manifest that hashes every public and private evidence artifact.  Fixture mode
is deliberately qualification-prohibited and can never emit a production
``WALL_GATE_PASS`` or ``WALL_GATE_PASS_CPU_UNKNOWN``.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from typing import Any

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("fx_t18_audit", HERE / "audit-fx-windows-timing.py")
assert SPEC and SPEC.loader
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)

PROFILES = {"routed": 54, "authored": 54, "typical": 9}
REQUIRED_PHASES = ("pre", "during", "post")
MAX_SAMPLE_GAP_SECONDS = 2.0
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
SHA1_RE = re.compile(r"^[0-9a-fA-F]{40}$")
OWNER_SIGNATURE_METHOD = "detached_cms_owner_certificate_chain_and_thumbprint"


def _bool(value: Any, name: str) -> bool:
    if type(value) is not bool:
        raise ValueError(f"{name} is not boolean")
    return value


def _int(value: Any, name: str) -> int:
    if type(value) is not int:
        raise ValueError(f"{name} is not an integer")
    return value


def _read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid {label} {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{path}: {label} is not an object")
    return value


def _utc(value: Any, name: str) -> datetime:
    if not isinstance(value, str) or not value.endswith("Z"):
        raise ValueError(f"{name} is not an explicit UTC timestamp")
    try:
        parsed = datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError as error:
        raise ValueError(f"{name} is not ISO-8601 UTC: {value!r}") from error
    if parsed.tzinfo is None or parsed.utcoffset() != timezone.utc.utcoffset(parsed):
        raise ValueError(f"{name} is not UTC: {value!r}")
    return parsed.astimezone(timezone.utc)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _same_path(left: str | Path, right: str | Path) -> bool:
    return str(Path(left).absolute()).casefold() == str(Path(right).absolute()).casefold()


def _manifest_contains(hashes: dict[str, str], target: str | Path) -> bool:
    return any(_same_path(path, target) for path in hashes)


def _resolve(path: Path, value: str) -> Path:
    candidate = Path(value)
    return candidate if candidate.is_absolute() else path.parent / candidate


def _proof(value: dict[str, Any], name: str) -> dict[str, Any]:
    proofs = value.get("attestation_proofs")
    if not isinstance(proofs, dict) or not isinstance(proofs.get(name), dict):
        raise ValueError(f"{value.get('format', 'host witness')}: signed {name} proof is missing")
    return proofs[name]


def _verify_host_manifest(path: Path, witness: Path) -> dict[str, Any]:
    """Verify all files named by a production witness and their SHA-256s.

    This intentionally includes the owner-private inventory and every detached
    signature.  The inventory remains private by content policy; hashing it in
    the public manifest makes deletion, replacement, or a post-capture edit
    visible without publishing process names/PIDs.
    """
    manifest = _read_json(path, "host artifact manifest")
    if manifest.get("format") not in {"VOX_FX_T18_HOST_ARTIFACTS_V1", "VOX_FX_T18_HOST_ARTIFACTS_V2"}:
        raise ValueError(f"unsupported host artifact manifest format in {path}")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise ValueError(f"{path}: host manifest has no files")
    hashes: dict[str, str] = {}
    for entry in files:
        if (not isinstance(entry, dict) or not isinstance(entry.get("path"), str)
                or not isinstance(entry.get("sha256"), str)
                or not SHA256_RE.fullmatch(entry["sha256"])):
            raise ValueError(f"{path}: malformed host artifact entry")
        artifact = _resolve(path, entry["path"])
        if not artifact.is_file():
            raise ValueError(f"{path}: missing host artifact {artifact}")
        actual = _sha256(artifact)
        if actual.casefold() != entry["sha256"].casefold():
            raise ValueError(f"{path}: host artifact hash mismatch for {artifact}")
        canonical = str(artifact.absolute()).casefold()
        if canonical in hashes:
            raise ValueError(f"{path}: duplicate host artifact {artifact}")
        hashes[canonical] = actual

    witness_value = _read_json(witness, "host witness")
    required: list[Path] = [witness]
    for container, field in (
        (witness_value, "identity_policy_path"),
        (witness_value, "private_inventory_path"),
        (witness_value.get("runner_lease", {}), "verification_receipt_path"),
        (witness_value.get("event_trace", {}), "loss_receipt_path"),
        (witness_value.get("event_trace", {}), "collector_artifact_path"),
        (witness_value.get("execution_binding", {}), "binding_receipt_path"),
    ):
        item = container.get(field) if isinstance(container, dict) else None
        if not isinstance(item, str) or not item or item.startswith("<"):
            raise ValueError(f"{witness}: host artifact path {field} is missing or redacted")
        required.append(_resolve(witness, item))
    for name in ("policy", "lease", "event_loss", "execution_binding"):
        proof = _proof(witness_value, name)
        for field in ("data_path", "signature_path"):
            item = proof.get(field)
            if not isinstance(item, str) or not item or item.startswith("<"):
                raise ValueError(f"{witness}: signed {name} proof path {field} is missing")
            required.append(_resolve(witness, item))
    runner_receipts = witness_value.get("execution_binding", {}).get("runner_receipts")
    if not isinstance(runner_receipts, list) or len(runner_receipts) != 3:
        raise ValueError(f"{witness}: execution binding does not list all runner receipts")
    for item in runner_receipts:
        if not isinstance(item, dict) or not isinstance(item.get("receipt_path"), str) \
                or not SHA256_RE.fullmatch(str(item.get("receipt_sha256", ""))):
            raise ValueError(f"{witness}: malformed bound runner receipt hash")
        receipt_path = _resolve(witness, item["receipt_path"])
        if not receipt_path.is_file() or _sha256(receipt_path).casefold() != item["receipt_sha256"].casefold():
            raise ValueError(f"{witness}: bound runner receipt hash reconciliation failed")
        required.append(receipt_path)
    for required_path in required:
        if not _manifest_contains(hashes, required_path):
            raise ValueError(f"{path}: host manifest does not cover {required_path}")

    private_hash = witness_value.get("private_inventory_sha256")
    if not isinstance(private_hash, str) or not SHA256_RE.fullmatch(private_hash):
        raise ValueError(f"{witness}: private inventory hash is missing")
    private_path = _resolve(witness, str(witness_value["private_inventory_path"]))
    actual_private = next((digest for key, digest in hashes.items() if _same_path(key, private_path)), None)
    if actual_private is None or actual_private.casefold() != private_hash.casefold():
        raise ValueError(f"{witness}: private inventory hash does not reconcile with the host manifest")
    return {"verified": True, "path": str(path), "files": len(files), "hashes": hashes,
            "required_artifacts": [str(item) for item in required]}


def _verify_cms_with_owner_powershell(data_path: Path, signature_path: Path, thumbprint: str) -> None:
    """Repeat the detached-CMS check outside Capture on the owner host.

    The production evaluator is intentionally not satisfied by a JSON field
    saying that Capture verified a signature. Windows PowerShell 5.1 is the
    reviewed verifier for this contract, so a production evaluation without it
    is BLOCKED rather than silently accepting synthetic bytes.
    """
    powershell = shutil.which("powershell.exe") or shutil.which("powershell")
    if not powershell:
        raise ValueError("independent PowerShell CMS verifier is unavailable")
    command = r"""
$ErrorActionPreference = 'Stop'
try {
    try { Add-Type -AssemblyName System.Security.Cryptography.Pkcs -ErrorAction Stop } catch { Add-Type -AssemblyName System.Security -ErrorAction Stop }
    $dataBytes = [IO.File]::ReadAllBytes($env:FX_T18_VERIFY_DATA)
    $signatureBytes = [IO.File]::ReadAllBytes($env:FX_T18_VERIFY_SIGNATURE)
    $contentInfo = New-Object -TypeName System.Security.Cryptography.Pkcs.ContentInfo -ArgumentList (,$dataBytes)
    $cms = [System.Security.Cryptography.Pkcs.SignedCms]::new($contentInfo, $true)
    $cms.Decode($signatureBytes)
    if ($cms.SignerInfos.Count -ne 1) { throw 'signature signer count is not one' }
    $certificate = $cms.SignerInfos[0].Certificate
    if ($null -eq $certificate) { throw 'signature certificate is missing' }
    $actual = (($certificate.Thumbprint -replace '[^0-9a-fA-F]', '').ToUpperInvariant())
    $expected = (($env:FX_T18_VERIFY_THUMBPRINT -replace '[^0-9a-fA-F]', '').ToUpperInvariant())
    if ($actual -cne $expected) { throw 'signature thumbprint mismatch' }
    $cms.CheckSignature($false)
    exit 0
} catch { [Console]::Error.WriteLine('detached CMS verification failed'); exit 1 }
"""
    environment = os.environ.copy()
    environment.update({
        "FX_T18_VERIFY_DATA": str(data_path),
        "FX_T18_VERIFY_SIGNATURE": str(signature_path),
        "FX_T18_VERIFY_THUMBPRINT": thumbprint,
    })
    try:
        completed = subprocess.run(
            [powershell, "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", command],
            env=environment, check=False, capture_output=True, text=True, timeout=30,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise ValueError(f"independent PowerShell CMS verifier failed to run: {error}") from error
    if completed.returncode != 0:
        raise ValueError("independent detached-CMS signature verification failed")


def _verify_signed_proof(
    witness_path: Path, proof: dict[str, Any], name: str,
    expected_thumbprint: str | None = None,
    verify_cryptography: bool = True,
) -> tuple[Path, Path, str]:
    if proof.get("verified") is not True or proof.get("verification_method") != OWNER_SIGNATURE_METHOD:
        raise ValueError(f"{witness_path}: {name} proof was not verified by the detached-CMS owner verifier")
    thumbprint = proof.get("signer_thumbprint")
    if not isinstance(thumbprint, str) or not re.fullmatch(r"[0-9A-Fa-f]{40}", thumbprint):
        raise ValueError(f"{witness_path}: {name} proof has no pinned owner certificate thumbprint")
    if expected_thumbprint and thumbprint.casefold() != expected_thumbprint.casefold():
        raise ValueError(f"{witness_path}: {name} proof signer thumbprint differs from the other owner proofs")
    data_path_value = proof.get("data_path")
    signature_path_value = proof.get("signature_path")
    if not isinstance(data_path_value, str) or not isinstance(signature_path_value, str):
        raise ValueError(f"{witness_path}: {name} proof paths are missing")
    data_path = _resolve(witness_path, data_path_value)
    signature_path = _resolve(witness_path, signature_path_value)
    if not data_path.is_file() or not signature_path.is_file():
        raise ValueError(f"{witness_path}: {name} proof file is missing")
    data_hash = proof.get("data_sha256")
    signature_hash = proof.get("signature_sha256")
    if not isinstance(data_hash, str) or not SHA256_RE.fullmatch(data_hash) \
            or _sha256(data_path).casefold() != data_hash.casefold():
        raise ValueError(f"{witness_path}: {name} signed data hash does not reconcile")
    if not isinstance(signature_hash, str) or not SHA256_RE.fullmatch(signature_hash) \
            or _sha256(signature_path).casefold() != signature_hash.casefold():
        raise ValueError(f"{witness_path}: {name} detached signature hash does not reconcile")
    if verify_cryptography:
        _verify_cms_with_owner_powershell(data_path, signature_path, thumbprint)
    return data_path, signature_path, thumbprint


def _verify_external_witness_attestations(
    path: Path, value: dict[str, Any], capture_start: datetime,
    capture_end: datetime, policy: dict[str, Any], lease: dict[str, Any],
    event_trace: dict[str, Any], binding: dict[str, Any],
    expected_host: str | None, expected_lease_id: str | None,
    expected_policy_sha256: str | None, expected_source_head: str | None,
    expected_source_tree: str | None, expected_executable_sha256: str | None,
    allow_fixture: bool = False,
) -> list[Path]:
    """Reconcile owner-signed payloads and their detached signature artifacts.

    PowerShell Capture performs the platform CMS chain check against an
    owner-supplied certificate thumbprint. This second stage independently
    recomputes all payload/signature hashes, checks the pinned signer is the
    same for every attestation, and reconciles claims to CTest receipts. A
    boolean in a JSON payload is never accepted without its signed bytes and
    detached signature.
    """
    artifacts: list[Path] = []
    proof_paths: dict[str, tuple[Path, Path]] = {}
    thumbprint: str | None = None
    for name in ("policy", "lease", "event_loss", "execution_binding"):
        data_path, signature_path, actual_thumbprint = _verify_signed_proof(
            path, _proof(value, name), name, thumbprint,
            verify_cryptography=not allow_fixture)
        artifacts.extend((data_path, signature_path))
        proof_paths[name] = (data_path, signature_path)
        thumbprint = actual_thumbprint

    policy_path = proof_paths["policy"][0]
    lease_path = proof_paths["lease"][0]
    loss_path = proof_paths["event_loss"][0]
    binding_path = proof_paths["execution_binding"][0]
    policy_file = _read_json(policy_path, "identity policy")
    lease_file = _read_json(lease_path, "host lease receipt")
    loss_file = _read_json(loss_path, "ETW event-loss receipt")
    binding_file = _read_json(binding_path, "execution binding")

    if (policy_file.get("format") != "VOX_FX_T18_IDENTITY_POLICY_V1"
            or policy_file.get("owner_approved") is not True
            or policy_file.get("policy_id") != policy.get("policy_id")
            or policy_file.get("baseline_id") != policy.get("baseline_id")
            or not isinstance(policy_file.get("runner_service_identity_sha256"), list)
            or len(set(policy_file["runner_service_identity_sha256"])) != 4
            or set(policy_file["runner_service_identity_sha256"]) != set(value.get("runner_service_identities", []))):
        raise ValueError(f"{path}: signed identity baseline does not match the witness")
    actual_policy_hash = _sha256(policy_path)
    if (actual_policy_hash.casefold() != str(policy.get("policy_sha256", "")).casefold()
            or actual_policy_hash.casefold() != str(value.get("identity_policy_sha256", "")).casefold()
            or actual_policy_hash.casefold() != str(_proof(value, "policy").get("data_sha256", "")).casefold()):
        raise ValueError(f"{path}: identity baseline hash does not match the witness")

    actual_lease_hash = _sha256(lease_path)
    if actual_lease_hash.casefold() != str(lease.get("verification_receipt_sha256", "")).casefold():
        raise ValueError(f"{path}: lease receipt hash does not match the witness")
    actual_loss_hash = _sha256(loss_path)
    if actual_loss_hash.casefold() != str(event_trace.get("loss_receipt_sha256", "")).casefold():
        raise ValueError(f"{path}: ETW loss receipt hash does not match the witness")
    actual_binding_hash = _sha256(binding_path)
    if actual_binding_hash.casefold() != str(binding.get("binding_receipt_sha256", "")).casefold():
        raise ValueError(f"{path}: execution binding hash does not match the witness")

    if (lease_file.get("format") != "VOX_FX_T18_HOST_LEASE_RECEIPT_V1"
            or lease_file.get("verified") is not True
            or lease_file.get("owner_approved") is not True
            or lease_file.get("attestation_source") != "owner_controlled_offline"
            or lease_file.get("verification_method") != "owner_signed_detached_cms"
            or lease_file.get("host") != lease.get("host")
            or lease_file.get("lease_id") != lease.get("lease_id")
            or lease_file.get("owner") != lease.get("owner")
            or lease_file.get("policy_sha256") != policy.get("policy_sha256")):
        raise ValueError(f"{path}: signed host lease does not reconcile with the witness")
    lease_start = _utc(lease_file.get("valid_from_utc"), "lease.valid_from_utc")
    lease_end = _utc(lease_file.get("valid_until_utc"), "lease.valid_until_utc")
    if lease_end < capture_end or lease_start > capture_start:
        raise ValueError(f"{path}: host lease validity does not cover the witness capture")

    if (loss_file.get("format") != "VOX_FX_T18_EVENT_LOSS_RECEIPT_V1"
            or loss_file.get("provider") != "Microsoft-Windows-Kernel-Process/ETW"
            or loss_file.get("measurement_method") != "ETW_PROCESS_TRACE_WITH_LOSS_COUNTERS"
            or loss_file.get("coverage_complete") is not True
            or loss_file.get("lost_events") != 0
            or loss_file.get("buffer_loss_events") != 0
            or type(loss_file.get("events_received")) is not int
            or loss_file.get("owner_approved") is not True
            or loss_file.get("attestation_source") != "owner_controlled_offline"
            or loss_file.get("verification_method") != "owner_signed_detached_cms"):
        raise ValueError(f"{path}: signed ETW loss receipt is not measurable and lossless")
    collector_value = loss_file.get("collector_artifact_path")
    collector_hash = loss_file.get("collector_artifact_sha256")
    if not isinstance(collector_value, str) or not isinstance(collector_hash, str) or not SHA256_RE.fullmatch(collector_hash):
        raise ValueError(f"{path}: ETW collector artifact path/hash is missing")
    collector_path = _resolve(loss_path, collector_value)
    if not collector_path.is_file() or _sha256(collector_path).casefold() != collector_hash.casefold():
        raise ValueError(f"{path}: ETW collector artifact hash does not reconcile")
    artifacts.append(collector_path)
    loss_start = _utc(loss_file.get("coverage_start_utc"), "event_loss.coverage_start_utc")
    loss_end = _utc(loss_file.get("coverage_end_utc"), "event_loss.coverage_end_utc")
    if loss_start > capture_start or loss_end < capture_end:
        raise ValueError(f"{path}: signed ETW loss interval does not cover the witness")

    if (binding_file.get("format") != "VOX_FX_T18_EXECUTION_BINDING_V1"
            or binding_file.get("owner_approved") is not True
            or binding_file.get("attestation_source") != "owner_controlled_offline"
            or binding_file.get("verification_method") != "owner_signed_detached_cms"
            or binding_file.get("host") != binding.get("host")
            or binding_file.get("lease_id") != binding.get("lease_id")
            or binding_file.get("policy_sha256") != binding.get("policy_sha256")
            or binding_file.get("source_head") != binding.get("source_head")
            or binding_file.get("source_tree") != binding.get("source_tree")
            or binding_file.get("executable_sha256") != binding.get("executable_sha256")):
        raise ValueError(f"{path}: signed execution binding does not reconcile with the witness")
    profiles = binding_file.get("profiles")
    if (not isinstance(profiles, list)
            or {item.get("profile") for item in profiles if isinstance(item, dict)} != set(PROFILES)
            or profiles != binding.get("profiles")):
        raise ValueError(f"{path}: signed execution binding does not cover all three profiles")
    bound_receipts = binding_file.get("runner_receipts")
    if (not isinstance(bound_receipts, list) or len(bound_receipts) != 3
            or {item.get("profile") for item in bound_receipts if isinstance(item, dict)} != set(PROFILES)
            or bound_receipts != binding.get("runner_receipts")):
        raise ValueError(f"{path}: signed execution binding does not hash all runner receipts")
    for item in bound_receipts:
        if not isinstance(item, dict) or not isinstance(item.get("receipt_path"), str) \
                or not SHA256_RE.fullmatch(str(item.get("receipt_sha256", ""))):
            raise ValueError(f"{path}: signed runner receipt hash is malformed")
        receipt_path = _resolve(binding_path, item["receipt_path"])
        if not receipt_path.is_file() or _sha256(receipt_path).casefold() != item["receipt_sha256"].casefold():
            raise ValueError(f"{path}: signed runner receipt hash does not reconcile")
        artifacts.append(receipt_path)

    for expected, actual in ((expected_host, lease_file.get("host")),
                             (expected_lease_id, lease_file.get("lease_id")),
                             (expected_policy_sha256, policy.get("policy_sha256")),
                             (expected_source_head, binding_file.get("source_head")),
                             (expected_source_tree, binding_file.get("source_tree")),
                             (expected_executable_sha256, binding_file.get("executable_sha256"))):
        if expected and str(actual).casefold() != str(expected).casefold():
            raise ValueError(f"{path}: owner-signed host binding disagrees with trusted CTest evidence")
    return artifacts


def _validate_process_identity(process: Any, label: str) -> None:
    if not isinstance(process, dict) or type(process.get("pid")) is not int \
            or not isinstance(process.get("process_name"), str):
        raise ValueError(f"{label}: malformed synthetic process identity")
    if process.get("identity_known") is not True or process.get("approved_by_policy") is not True:
        raise ValueError(f"{label}: synthetic process identity is not approved")
    if process.get("command_line") not in (None, "<redacted>"):
        raise ValueError(f"{label}: command-line secret was not redacted")


def _validate_public_sample(sample: dict[str, Any], label: str, allow_fixture: bool) -> None:
    if "processes" in sample:
        if not allow_fixture:
            raise ValueError(f"{label}: public witness contains raw process inventory")
        for index, process in enumerate(sample.get("processes", [])):
            _validate_process_identity(process, f"{label}.processes[{index}]")
        return
    for field in ("observed_process_count", "approved_process_count", "unknown_identity_count",
                  "unexpected_identity_count", "pid_reuse_mismatch_count"):
        if type(sample.get(field)) is not int or sample[field] < 0:
            raise ValueError(f"{label}: process summary field {field} is invalid")
    if sample.get("process_query_error") not in (None, ""):
        raise ValueError(f"{label}: process query error is evidence-blocking")
    digest = sample.get("process_inventory_sha256")
    if not isinstance(digest, str) or not SHA256_RE.fullmatch(digest):
        raise ValueError(f"{label}: private process inventory hash is missing")
    if (sample["unknown_identity_count"] or sample["unexpected_identity_count"]
            or sample["pid_reuse_mismatch_count"]):
        raise ValueError(f"{label}: unknown, unexpected, or reused process identity is present")


def validate_witness(
    path: Path,
    execution_intervals: dict[str, dict[str, Any]] | None = None,
    expected_host: str | None = None,
    expected_lease_id: str | None = None,
    expected_policy_sha256: str | None = None,
    expected_source_head: str | None = None,
    expected_source_tree: str | None = None,
    expected_executable_sha256: str | None = None,
    require_external_attestations: bool = False,
    allow_fixture: bool = False,
) -> dict[str, Any]:
    """Validate monotonic/deadline sampling and host-wide identity summaries."""
    value = _read_json(path, "host witness")
    if value.get("format") != "VOX_FX_T18_HOST_WITNESS_V1":
        raise ValueError(f"{path}: unsupported host witness format")
    fixture = value.get("fixture_mode") is True or value.get("qualification_prohibited") is True
    if fixture and not allow_fixture:
        raise ValueError(f"{path}: fixture_mode/qualification_prohibited witness cannot be production evidence")
    if not fixture and value.get("schema_version") != 2:
        raise ValueError(f"{path}: host witness schema_version 2 is required")
    capture_start = _utc(value.get("capture_start_utc"), "capture_start_utc")
    capture_end = _utc(value.get("capture_end_utc"), "capture_end_utc")
    if capture_end <= capture_start:
        raise ValueError(f"{path}: host capture interval is not positive")
    if _int(value.get("sample_interval_ms"), "sample_interval_ms") != 1000:
        raise ValueError(f"{path}: host witness sample interval is not 1000 ms")
    pre_seconds = _int(value.get("pre_seconds"), "pre_seconds")
    during_seconds = _int(value.get("during_seconds"), "during_seconds")
    post_seconds = _int(value.get("post_seconds"), "post_seconds")
    if pre_seconds < 30 or post_seconds < 30 or during_seconds < 1:
        raise ValueError(f"{path}: host witness lacks declared phase windows")
    if value.get("host_observation_mode") != "process_snapshot_and_event_trace":
        raise ValueError(f"{path}: host-wide observation mode is not declared")
    if value.get("monitor_gaps") not in ([], None):
        raise ValueError(f"{path}: host witness has polling/deadline gaps")
    if (capture_end - capture_start).total_seconds() < pre_seconds + during_seconds + post_seconds:
        raise ValueError(f"{path}: capture interval is shorter than declared witness windows")

    policy = value.get("identity_policy")
    if not isinstance(policy, dict) or policy.get("format") != "VOX_FX_T18_IDENTITY_POLICY_V1" \
            or not isinstance(policy.get("policy_id"), str) or not policy["policy_id"] \
            or not isinstance(policy.get("baseline_id"), str) or not policy["baseline_id"] \
            or not isinstance(policy.get("policy_sha256"), str) or not SHA256_RE.fullmatch(policy["policy_sha256"]):
        raise ValueError(f"{path}: signed owner identity baseline summary is missing")
    service_hashes = policy.get("runner_service_identity_sha256")
    if service_hashes is None:
        service_hashes = value.get("runner_service_identities")
    if not isinstance(service_hashes, list) or len(set(service_hashes)) != 4 \
            or any(not isinstance(item, str) or not SHA256_RE.fullmatch(item) for item in service_hashes):
        raise ValueError(f"{path}: all four owner-baselined HAMUNAPTRA runner service identities are required")
    if expected_policy_sha256 and policy["policy_sha256"].casefold() != expected_policy_sha256.casefold():
        raise ValueError(f"{path}: witness process policy does not match trusted CTest receipts")

    event_trace = value.get("event_trace")
    if not isinstance(event_trace, dict):
        raise ValueError(f"{path}: event trace summary is missing")
    if not fixture:
        required_event = (
            event_trace.get("available") is True,
            event_trace.get("coverage_complete") is True,
            event_trace.get("observation_provider") == "Win32_ProcessStartTrace/Win32_ProcessStopTrace",
            event_trace.get("loss_provider") == "Microsoft-Windows-Kernel-Process/ETW",
            event_trace.get("loss_accounting") == "etw_provider_counters",
            type(event_trace.get("lost_events")) is int and event_trace.get("lost_events") == 0,
            type(event_trace.get("buffer_loss_events")) is int and event_trace.get("buffer_loss_events") == 0,
            type(event_trace.get("events_received")) is int and event_trace.get("events_received") >= 0,
            isinstance(event_trace.get("collector_artifact_sha256"), str)
            and SHA256_RE.fullmatch(event_trace["collector_artifact_sha256"]) is not None,
        )
        if not all(required_event):
            raise ValueError(f"{path}: independently measurable ETW event loss/completeness is absent")
        trace_start = _utc(event_trace.get("coverage_start_utc"), "event_trace.coverage_start_utc")
        trace_end = _utc(event_trace.get("coverage_end_utc"), "event_trace.coverage_end_utc")
        if trace_start > capture_start or trace_end < capture_end or trace_end <= trace_start:
            raise ValueError(f"{path}: event trace interval does not cover the witness")
    elif event_trace.get("coverage_complete") is not True:
        # A fixture may demonstrate an unverified WMI/ETW claim, but it cannot
        # be accepted as a production lossless trace.
        pass

    samples = value.get("samples")
    if not isinstance(samples, list) or not samples:
        raise ValueError(f"{path}: host witness has no samples")
    phase_samples: dict[str, list[tuple[datetime, dict[str, Any]]]] = {phase: [] for phase in REQUIRED_PHASES}
    phase_order = {phase: index for index, phase in enumerate(REQUIRED_PHASES)}
    previous_phase = -1
    all_times: list[datetime] = []
    scheduled_times: list[datetime] = []
    previous_monotonic: float | None = None
    for index, sample in enumerate(samples):
        if not isinstance(sample, dict) or sample.get("phase") not in phase_samples:
            raise ValueError(f"{path}: malformed host sample {index}")
        phase = sample["phase"]
        if phase_order[phase] < previous_phase:
            raise ValueError(f"{path}: host phases are out of order")
        previous_phase = phase_order[phase]
        timestamp = _utc(sample.get("timestamp_utc"), f"samples[{index}].timestamp_utc")
        if timestamp < capture_start or timestamp > capture_end:
            raise ValueError(f"{path}: sample lies outside capture interval")
        _validate_public_sample(sample, f"samples[{index}]", allow_fixture)
        scheduled = _utc(sample.get("scheduled_timestamp_utc", sample.get("timestamp_utc")),
                         f"samples[{index}].scheduled_timestamp_utc")
        if scheduled_times and scheduled <= scheduled_times[-1]:
            raise ValueError(f"{path}: sample deadlines are not strictly increasing")
        if abs((timestamp - scheduled).total_seconds()) > MAX_SAMPLE_GAP_SECONDS:
            raise ValueError(f"{path}: sample missed its monotonic/deadline reconciliation")
        monotonic = sample.get("monotonic_elapsed_ms")
        if monotonic is not None:
            if not isinstance(monotonic, (int, float)) or monotonic < 0.0 \
                    or (previous_monotonic is not None and monotonic <= previous_monotonic):
                raise ValueError(f"{path}: monotonic sample clock is not strictly increasing")
            previous_monotonic = float(monotonic)
        phase_samples[phase].append((timestamp, sample))
        all_times.append(timestamp); scheduled_times.append(scheduled)
    if any(right <= left for left, right in zip(all_times, all_times[1:])):
        raise ValueError(f"{path}: sample timestamps are not strictly increasing")
    gaps = [(right - left).total_seconds() for left, right in zip(all_times, all_times[1:])]
    if any(gap > MAX_SAMPLE_GAP_SECONDS for gap in gaps):
        raise ValueError(f"{path}: host sample cadence has a gap larger than 2s")
    for phase, declared_seconds in (("pre", pre_seconds), ("during", during_seconds), ("post", post_seconds)):
        entries = phase_samples[phase]
        if len(entries) < declared_seconds + 1:
            raise ValueError(f"{path}: {phase} is missing one-Hz samples")
        if (entries[-1][0] - entries[0][0]).total_seconds() < declared_seconds:
            raise ValueError(f"{path}: {phase} timestamp span is shorter than its declaration")

    events = value.get("process_events", [])
    if not isinstance(events, list):
        raise ValueError(f"{path}: malformed process event list")
    event_times: list[datetime] = []
    for index, event in enumerate(events):
        if not isinstance(event, dict) or event.get("event") not in {"start", "stop", "unparsed"}:
            raise ValueError(f"{path}: malformed process event {index}")
        if not allow_fixture and ("pid" in event or "process_name" in event or "command_line" in event):
            raise ValueError(f"{path}: public event trace contains raw process identity")
        if event.get("identity_known") is not True or event.get("approved_by_policy") is not True:
            raise ValueError(f"{path}: unknown or unexpected process event is present")
        if not isinstance(event.get("identity_digest"), str) or not SHA256_RE.fullmatch(event["identity_digest"]):
            raise ValueError(f"{path}: event identity digest is missing")
        event_time = _utc(event.get("timestamp_utc"), f"process_events[{index}].timestamp_utc")
        if event_time < capture_start or event_time > capture_end:
            raise ValueError(f"{path}: process event lies outside capture interval")
        event_times.append(event_time)
    if any(right < left for left, right in zip(event_times, event_times[1:])):
        raise ValueError(f"{path}: process event timestamps are not monotonic")

    for field in ("unrecognized_competitors", "unknown_pids", "pid_reuse_mismatches"):
        if type(value.get(field)) is not int or value[field] < 0:
            raise ValueError(f"{path}: {field} must be a non-identifying counter")
        if value[field] != 0:
            raise ValueError(f"{path}: {field} reports an identity/exclusivity failure")
    if value.get("evidence_blocked") is not False and not allow_fixture:
        raise ValueError(f"{path}: host witness is already evidence-blocked")

    lease = value.get("runner_lease")
    if not isinstance(lease, dict) or lease.get("recognized") is not True \
            or lease.get("owner_verified") is not True \
            or not isinstance(lease.get("lease_id"), str) or not lease["lease_id"] \
            or not isinstance(lease.get("host"), str) or not lease["host"] \
            or not isinstance(lease.get("verification_receipt_sha256"), str) \
            or not SHA256_RE.fullmatch(lease["verification_receipt_sha256"]):
        raise ValueError(f"{path}: runner lease is not owner-verified")
    if expected_host and lease["host"].casefold() != expected_host.casefold():
        raise ValueError(f"{path}: witness host does not match trusted runner receipts")
    if expected_lease_id and lease["lease_id"] != expected_lease_id:
        raise ValueError(f"{path}: witness lease does not match trusted runner receipts")
    for secret_field in ("token", "lease_token", "command_line"):
        if secret_field in lease and lease[secret_field] not in (None, "<redacted>"):
            raise ValueError(f"{path}: sensitive lease field {secret_field} is not redacted")

    services = value.get("runner_service_identities")
    if not isinstance(services, list) or set(services) != set(service_hashes):
        raise ValueError(f"{path}: observed runner service baseline does not reconcile with all four services")
    binding = value.get("execution_binding")
    if not isinstance(binding, dict) or binding.get("verified") is not True:
        raise ValueError(f"{path}: trusted CTest execution binding is missing")
    for field in ("host", "runner_job_id", "source_head", "source_tree", "executable_sha256", "lease_id", "policy_sha256"):
        if not isinstance(binding.get(field), str) or not binding[field]:
            raise ValueError(f"{path}: execution binding field {field} is missing")
    if not SHA1_RE.fullmatch(binding["source_head"]) or not SHA1_RE.fullmatch(binding["source_tree"]) \
            or not SHA256_RE.fullmatch(binding["executable_sha256"]) or not SHA256_RE.fullmatch(binding["policy_sha256"]):
        raise ValueError(f"{path}: execution binding hash is malformed")
    if binding["host"].casefold() != lease["host"].casefold() or binding["lease_id"] != lease["lease_id"] \
            or binding["policy_sha256"].casefold() != policy["policy_sha256"].casefold():
        raise ValueError(f"{path}: execution binding disagrees with lease or policy")
    if expected_source_head and binding["source_head"].casefold() != expected_source_head.casefold():
        raise ValueError(f"{path}: witness source HEAD does not match trusted CTest receipts")
    if expected_source_tree and binding["source_tree"].casefold() != expected_source_tree.casefold():
        raise ValueError(f"{path}: witness source tree does not match trusted CTest receipts")
    if expected_executable_sha256 and binding["executable_sha256"].casefold() != expected_executable_sha256.casefold():
        raise ValueError(f"{path}: witness executable hash does not match trusted CTest receipts")

    binding_profiles = binding.get("profiles")
    if not isinstance(binding_profiles, list) or {item.get("profile") for item in binding_profiles if isinstance(item, dict)} != set(PROFILES):
        raise ValueError(f"{path}: execution binding does not contain all three CTest profiles")
    bound_receipts = binding.get("runner_receipts")
    if not fixture:
        if not isinstance(bound_receipts, list) or len(bound_receipts) != 3 \
                or {item.get("profile") for item in bound_receipts if isinstance(item, dict)} != set(PROFILES):
            raise ValueError(f"{path}: execution binding does not hash all three runner receipts")
        for item in bound_receipts:
            if not isinstance(item, dict) or not isinstance(item.get("receipt_path"), str) \
                    or not SHA256_RE.fullmatch(str(item.get("receipt_sha256", ""))):
                raise ValueError(f"{path}: bound runner receipt hash is malformed")
    intervals = execution_intervals or {}
    if set(intervals) != set(PROFILES):
        raise ValueError(f"{path}: witness is not bound to all routed/authored/typical execution intervals")
    for profile, state in intervals.items():
        start = state.get("test_start"); end = state.get("test_end")
        if not isinstance(start, datetime) or not isinstance(end, datetime) or end <= start:
            raise ValueError(f"{path}: invalid trusted {profile} test interval")
        if start < capture_start or end > capture_end:
            raise ValueError(f"{path}: witness does not contain the complete {profile} CTest interval")
        matching = [item for item in binding_profiles if item.get("profile") == profile]
        if len(matching) != 1:
            raise ValueError(f"{path}: binding profile {profile} is ambiguous")
        bound_start = _utc(matching[0].get("test_start_utc"), f"binding.{profile}.test_start_utc")
        bound_end = _utc(matching[0].get("test_end_utc"), f"binding.{profile}.test_end_utc")
        if bound_start != start or bound_end != end:
            raise ValueError(f"{path}: phase/CTest interval mismatch for {profile}")
        before = [stamp for stamp in all_times if stamp <= start]
        after = [stamp for stamp in all_times if stamp >= end]
        if not before or not after or (start - before[-1]).total_seconds() > MAX_SAMPLE_GAP_SECONDS \
                or (after[0] - end).total_seconds() > MAX_SAMPLE_GAP_SECONDS:
            raise ValueError(f"{path}: sample stream does not cover {profile} CTest interval")
        during_times = [stamp for stamp, _ in phase_samples["during"]]
        if not during_times or during_times[0] > start or during_times[-1] < end:
            raise ValueError(f"{path}: during-phase samples do not cover {profile} CTest interval")

    external_attestations: list[Path] = []
    if require_external_attestations:
        external_attestations = _verify_external_witness_attestations(
            path, value, capture_start, capture_end, policy, lease, event_trace, binding,
            expected_host, expected_lease_id, expected_policy_sha256,
            expected_source_head, expected_source_tree, expected_executable_sha256,
            allow_fixture=allow_fixture)

    return {"verified": True, "path": str(path), "samples": len(samples),
            "process_events": len(events), "capture_start": capture_start, "capture_end": capture_end,
            "policy_sha256": policy["policy_sha256"], "host": lease["host"],
            "lease_id": lease["lease_id"], "source_head": binding["source_head"],
            "source_tree": binding["source_tree"], "executable_sha256": binding["executable_sha256"],
            "external_attestations": [str(item) for item in external_attestations]}


def _scan_observed_failures(path: Path) -> dict[str, bool]:
    result = {"wall_failure": False, "safety_failure": False,
              "functional_failure": False, "functional_evidence_invalid": False}
    cases: dict[str, dict[str, Any]] = {}
    functional_rows: dict[str, dict[str, Any]] = {}
    try:
        lines = path.read_text(encoding="utf-8-sig").splitlines()
    except (OSError, UnicodeDecodeError):
        result["functional_evidence_invalid"] = True
        return result
    for line_number, line in enumerate(lines, 1):
        fields = line.split("\t")
        if not fields:
            continue
        if fields[0] == "case" and len(fields) == 1 + len(AUDIT.CASE_FIELDS):
            values = dict(zip(AUDIT.CASE_FIELDS, fields[1:]))
            if values["case_id"] in cases:
                result["functional_evidence_invalid"] = True
                continue
            cases[values["case_id"]] = values
            try:
                result["wall_failure"] |= int(values["wall_misses"]) > 0
                result["safety_failure"] |= any(int(values[field]) > 0 for field in ("alloc_calls", "lock_entries", "wait_entries"))
            except (TypeError, ValueError):
                pass
        elif fields[0] == "functional":
            try:
                functional = AUDIT._parse_functional(fields, line_number, path)
            except ValueError:
                result["functional_evidence_invalid"] = True
                continue
            case_id = functional["case_id"]
            if case_id in functional_rows:
                result["functional_evidence_invalid"] = True
            functional_rows[case_id] = functional
        elif fields[0] == "block" and len(fields) == 1 + len(AUDIT.BLOCK_FIELDS):
            try:
                result["wall_failure"] |= int(fields[6]) == 1
            except (TypeError, ValueError):
                pass
    for case_id, functional in functional_rows.items():
        case = cases.get(case_id)
        if case is None:
            result["functional_evidence_invalid"] = True
            continue
        try:
            initial_voices = int(case["initial_voices"])
            flags = [functional[field] for field, _ in AUDIT.FUNCTIONAL_REASON_ORDER]
            expected_codes = [code for flag, (_, code) in zip(flags, AUDIT.FUNCTIONAL_REASON_ORDER) if flag == 0]
            consistent = (functional["reason_codes"] == expected_codes
                          and functional["functional_pass"] == int(not expected_codes)
                          and (initial_voices == 8 or functional["voice8_grain_invariant"] == 1))
        except (KeyError, TypeError, ValueError):
            consistent = False
        if not consistent:
            result["functional_evidence_invalid"] = True
        elif functional["functional_pass"] == 0:
            result["functional_failure"] = True
    return result


def _load_outcome(path: Path) -> dict[str, Any]:
    outcome = _read_json(path, "post-test outcome")
    required = ("format", "scenario", "test_identity", "executable_path", "executable_sha256",
                "test_start_utc", "test_end_utc", "ctest_exit_code", "native_exit_code",
                "ctest_outcome", "execution_complete", "timed_out", "crashed", "skipped",
                "junit_path", "junit_tests", "junit_failures", "junit_errors", "junit_skipped",
                "junit_timeout", "cpu_only_failure", "cpu_only_failure_reason")
    missing = [field for field in required if field not in outcome]
    if missing:
        raise ValueError(f"post-test outcome is missing {', '.join(missing)}")
    if outcome["format"] != "VOX_FX_T18_POST_TEST_OUTCOME_V1":
        raise ValueError("unsupported post-test outcome format")
    for field in ("scenario", "test_identity", "executable_path", "executable_sha256", "junit_path", "ctest_outcome", "cpu_only_failure_reason"):
        if not isinstance(outcome[field], str) or not outcome[field]:
            raise ValueError(f"outcome.{field} is not a non-empty string")
    if not SHA256_RE.fullmatch(outcome["executable_sha256"]):
        raise ValueError("outcome executable SHA-256 is malformed")
    start = _utc(outcome["test_start_utc"], "outcome.test_start_utc")
    end = _utc(outcome["test_end_utc"], "outcome.test_end_utc")
    if end <= start:
        raise ValueError("post-test outcome interval is not positive")
    for field in ("ctest_exit_code", "native_exit_code", "junit_tests", "junit_failures", "junit_errors", "junit_skipped"):
        if type(outcome[field]) is not int:
            raise ValueError(f"outcome.{field} is not an integer")
    for field in ("execution_complete", "timed_out", "crashed", "skipped", "junit_timeout", "cpu_only_failure"):
        if type(outcome[field]) is not bool:
            raise ValueError(f"outcome.{field} is not boolean")
    if outcome["native_exit_code"] != outcome["ctest_exit_code"]:
        raise ValueError("native and CTest exit codes disagree")
    if outcome["cpu_only_failure"] and outcome["cpu_only_failure_reason"] != "CPU_MEASUREMENT_INSUFFICIENT":
        raise ValueError("cpu-only outcome lacks required reason")
    junit_path = Path(outcome["junit_path"])
    if not junit_path.is_absolute():
        junit_path = path.parent / junit_path
    if not junit_path.is_file():
        raise ValueError(f"post-test outcome JUnit artifact is missing: {junit_path}")
    junit_summary = AUDIT._junit_summary(junit_path)
    for field, actual in (("junit_tests", junit_summary["tests"]), ("junit_failures", junit_summary["failures"]),
                          ("junit_errors", junit_summary["errors"]), ("junit_skipped", junit_summary["skipped"]),
                          ("junit_timeout", junit_summary["timeout"])):
        if outcome[field] != actual:
            raise ValueError(f"post-test outcome {field} does not match JUnit")
    return {"value": outcome, "test_start": start, "test_end": end, "junit": junit_summary}


def _execution_state(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {"execution_failure": False, "execution_blocked": True, "evidence_blocked": True, "receipt_missing": True}
    try:
        receipt = _read_json(path, "runner receipt")
        required = ("format", "scenario", "test_identity", "test_start_utc", "test_end_utc", "machine",
                    "host_lease_id", "host_policy_sha256", "post_test_outcome_path", "ctest_raw_path",
                    "ctest_inventory_path", "ctest_junit_path", "executable_path", "executable_sha256",
                    "ctest_selected_executable", "source_head", "source_tree", "runner_job_id")
        missing = [field for field in required if field not in receipt]
        if missing:
            raise ValueError(f"runner receipt is missing {', '.join(missing)}")
        for field in ("scenario", "test_identity", "test_start_utc", "test_end_utc", "machine", "host_lease_id",
                      "host_policy_sha256", "post_test_outcome_path", "ctest_raw_path", "ctest_inventory_path",
                      "ctest_junit_path", "executable_path", "executable_sha256", "ctest_selected_executable",
                      "source_head", "source_tree", "runner_job_id"):
            if not isinstance(receipt[field], str) or not receipt[field]:
                raise ValueError(f"runner receipt {field} is not a non-empty string")
        if receipt["test_identity"] != "suite_fx_runtime_stress_tests":
            raise ValueError("runner receipt test identity is not the FX stress test")
        if not SHA256_RE.fullmatch(receipt["host_policy_sha256"]) or not SHA256_RE.fullmatch(receipt["executable_sha256"]):
            raise ValueError("runner receipt policy/executable SHA-256 is malformed")
        if not SHA1_RE.fullmatch(receipt["source_head"]) or not SHA1_RE.fullmatch(receipt["source_tree"]):
            raise ValueError("runner receipt source SHA-1 is malformed")
        start = _utc(receipt["test_start_utc"], "receipt.test_start_utc")
        receipt_end = None if receipt["test_end_utc"] in ("post-test-outcome", "") else _utc(receipt["test_end_utc"], "receipt.test_end_utc")
        outcome_path = _resolve(path, receipt["post_test_outcome_path"])
        artifact_paths: dict[str, Path] = {}
        for field in ("ctest_raw_path", "ctest_inventory_path", "ctest_junit_path"):
            artifact = _resolve(path, receipt[field])
            if not artifact.is_file() or artifact.stat().st_size == 0:
                raise ValueError(f"runner receipt {field} is missing or empty")
            artifact_paths[field] = artifact
        outcome_info = _load_outcome(outcome_path); outcome = outcome_info["value"]
        native_evidence = AUDIT.reconcile_native_ctest(artifact_paths["ctest_raw_path"], artifact_paths["ctest_junit_path"], outcome["test_identity"], outcome["ctest_exit_code"])
        if outcome["scenario"] != receipt["scenario"] or outcome["test_identity"] != receipt["test_identity"] \
                or not _same_path(outcome["junit_path"], artifact_paths["ctest_junit_path"]) \
                or outcome_info["test_start"] != start or (receipt_end is not None and outcome_info["test_end"] != receipt_end):
            raise ValueError("post-test outcome does not reconcile with runner receipt")
        end = outcome_info["test_end"]
        if not _same_path(outcome["executable_path"], receipt["executable_path"]) \
                or not _same_path(receipt["ctest_selected_executable"], receipt["executable_path"]) \
                or outcome["executable_sha256"].casefold() != receipt["executable_sha256"].casefold():
            raise ValueError("post-test outcome executable does not match selected CTest executable")
        execution_failure = bool(outcome["timed_out"] or outcome["crashed"] or outcome["skipped"] or not outcome["execution_complete"])
        junit_failure = bool(outcome["junit_failures"] > 0 or outcome["junit_errors"] > 0 or outcome["junit_skipped"] > 0 or outcome["junit_timeout"])
        cpu_only_valid = bool(outcome["cpu_only_failure"] and outcome["ctest_exit_code"] != 0 and native_evidence.get("cpu_reason_verified") and not execution_failure)
        native_failure = bool(outcome["ctest_exit_code"] != 0 or junit_failure) and not cpu_only_valid
        return {"execution_failure": execution_failure, "execution_blocked": native_failure,
                "native_failure": native_failure, "cpu_only_failure": cpu_only_valid,
                "evidence_blocked": execution_failure or native_failure, "receipt_missing": False,
                "status": str(outcome["ctest_outcome"]).lower(), "test_start": start, "test_end": end,
                "host": receipt["machine"], "lease_id": receipt["host_lease_id"],
                "policy_sha256": receipt["host_policy_sha256"], "source_head": receipt["source_head"],
                "source_tree": receipt["source_tree"], "executable_sha256": receipt["executable_sha256"],
                "runner_job_id": receipt["runner_job_id"], "outcome_path": outcome_path,
                "outcome": outcome, "native_execution_evidence": native_evidence}
    except (OSError, ValueError) as error:
        return {"execution_failure": False, "execution_blocked": True, "evidence_blocked": True,
                "receipt_missing": False, "receipt_error": str(error)}


def _dispositions(wall_failure: bool, safety_failure: bool, functional_failure: bool,
                  execution_failure: bool, execution_blocked: bool, instrumentation_blocked: bool,
                  evidence_blocked: bool, cpu_diagnostic_blocked: bool) -> dict[str, str]:
    return {"wall": "FAIL" if wall_failure else "BLOCKED" if evidence_blocked else "PASS",
            "safety": "FAIL" if safety_failure else "BLOCKED" if instrumentation_blocked or evidence_blocked else "PASS",
            "functional": "FAIL" if functional_failure else "BLOCKED" if evidence_blocked else "PASS",
            "execution": "FAIL" if execution_failure else "BLOCKED" if execution_blocked else "PASS",
            "instrumentation": "BLOCKED" if instrumentation_blocked else "PASS",
            "evidence": "BLOCKED" if evidence_blocked else "PASS",
            "cpu": "BLOCKED" if cpu_diagnostic_blocked else "PASS"}


def evaluate_results(results: dict[str, dict[str, Any]], witness: dict[str, Any] | None,
                     execution: dict[str, dict[str, Any]] | None = None,
                     qualification_prohibited: bool = False,
                     provenance_verified: bool = False,
                     host_manifest_verified: bool = False) -> dict[str, Any]:
    execution = execution or {}
    missing_profiles = sorted(set(PROFILES) - set(results)); extra_profiles = sorted(set(results) - set(PROFILES))
    coverage_error = bool(missing_profiles or extra_profiles)
    wall_failure = safety_failure = functional_failure = execution_failure = execution_blocked = False
    instrumentation_blocked = functional_evidence_invalid = False
    witness_fixture = bool(witness and (witness.get("fixture_mode") is True or witness.get("qualification_prohibited") is True))
    evidence_blocked = (coverage_error or witness is None or witness.get("verified") is not True
                        or qualification_prohibited or witness_fixture or not provenance_verified or not host_manifest_verified)
    cpu_diagnostic_blocked = False; profile_cells_complete = True
    for profile, result in results.items():
        observed = _scan_observed_failures(Path(result["report"])) if result.get("report") else {}
        wall_failure |= bool(result.get("wall_failure", False) or observed.get("wall_failure", False))
        safety_failure |= bool(result.get("safety_failure", False) or observed.get("safety_failure", False))
        functional_failure |= bool(result.get("functional_failure", False) or observed.get("functional_failure", False))
        functional_evidence_invalid |= bool(result.get("functional_evidence_invalid", False) or observed.get("functional_evidence_invalid", False))
        execution_failure |= bool(result.get("execution_failure", False)); instrumentation_blocked |= bool(result.get("instrumentation_blocked", False))
        evidence_blocked |= bool(result.get("evidence_blocked", False) or result.get("functional_evidence_invalid", False) or observed.get("functional_evidence_invalid", False))
        report_cpu_diagnostic = bool(result.get("cpu_diagnostic_blocked", False) or result.get("cpu_measurement_insufficient", False))
        expected_cells = PROFILES.get(profile)
        if expected_cells is None or result.get("cells") != expected_cells or result.get("totals", {}).get("callbacks") != expected_cells * AUDIT.BLOCKS:
            profile_cells_complete = False
        state = execution.get(profile)
        if not state:
            execution_blocked = True; evidence_blocked = True; cpu_diagnostic_blocked |= report_cpu_diagnostic; continue
        execution_failure |= bool(state.get("execution_failure", False)); execution_blocked |= bool(state.get("execution_blocked", False) or state.get("receipt_missing", False)); evidence_blocked |= bool(state.get("evidence_blocked", False) or state.get("receipt_missing", False))
        if state.get("cpu_only_failure"):
            report_cpu_only = bool(result.get("cpu_measurement_insufficient", False) and not result.get("wall_failure", False) and not result.get("safety_failure", False) and not result.get("functional_failure", False) and not result.get("functional_evidence_invalid", False))
            if report_cpu_only: cpu_diagnostic_blocked = True
            else: state["cpu_only_failure_reconciled"] = False; state["execution_blocked"] = True; state["evidence_blocked"] = True; execution_blocked = True; evidence_blocked = True
        elif state.get("test_start") and not state.get("execution_blocked") and not state.get("execution_failure"):
            cpu_diagnostic_blocked |= report_cpu_diagnostic
    evidence_blocked |= not profile_cells_complete
    performance_failure = wall_failure or safety_failure or functional_failure
    if performance_failure: primary_status = "WALL_GATE_FAIL"
    elif execution_failure or execution_blocked or evidence_blocked: primary_status = "WALL_GATE_BLOCKED"
    elif cpu_diagnostic_blocked: primary_status = "WALL_GATE_PASS_CPU_UNKNOWN"
    else: primary_status = "WALL_GATE_PASS"
    return {"format": "VOX_FX_WALL_GATE_EVALUATION_V2", "mode": "fixture" if qualification_prohibited or witness_fixture else "qualified",
            "qualification_prohibited": bool(qualification_prohibited or witness_fixture), "primary_status": primary_status,
            "wall_failure": wall_failure, "safety_failure": safety_failure, "functional_failure": functional_failure,
            "functional_evidence_invalid": functional_evidence_invalid, "execution_failure": execution_failure,
            "execution_blocked": execution_blocked, "performance_failure": performance_failure,
            "instrumentation_blocked": instrumentation_blocked, "evidence_blocked": evidence_blocked,
            "cpu_diagnostic_blocked": cpu_diagnostic_blocked, "provenance_verified": provenance_verified,
            "host_manifest_verified": host_manifest_verified, "missing_profiles": missing_profiles, "extra_profiles": extra_profiles,
            "profile_cells_complete": profile_cells_complete, "expected_cells": sum(PROFILES.values()),
            "expected_callbacks": sum(PROFILES.values()) * AUDIT.BLOCKS,
            "observed_callbacks": sum(int(result.get("totals", {}).get("callbacks", 0)) for result in results.values()),
            "dispositions": _dispositions(wall_failure, safety_failure, functional_failure, execution_failure,
                                            execution_blocked, instrumentation_blocked, evidence_blocked, cpu_diagnostic_blocked),
            "witness": witness, "profiles": results}


def evaluate_reports(reports: dict[str, Path], root: Path | None = None, executable: Path | None = None,
                     receipts: dict[str, Path] | None = None, manifests: dict[str, Path] | None = None,
                     expected_head: str | None = None, expected_tree: str | None = None,
                     witness: Path | None = None, host_manifest: Path | None = None,
                     allow_unverified_provenance: bool = False) -> dict[str, Any]:
    receipts = receipts or {}; manifests = manifests or {}
    audited: dict[str, dict[str, Any]] = {}
    execution = {profile: _execution_state(receipts.get(profile)) for profile in PROFILES}
    for profile, report in reports.items():
        try:
            if not allow_unverified_provenance and (root is None or profile not in receipts or profile not in manifests):
                raise ValueError("report requires root, runner receipt and artifact manifest")
            audited[profile] = AUDIT.audit(report, require_windows=not allow_unverified_provenance, root=root,
                                           executable=executable, runner_receipt=receipts.get(profile), artifact_manifest=manifests.get(profile),
                                           expected_head=expected_head, expected_tree=expected_tree,
                                           allow_unverified_provenance=allow_unverified_provenance,
                                           allow_instrumentation_blocked=True)
            audited[profile]["report"] = str(report)
        except (OSError, ValueError) as error:
            observed = _scan_observed_failures(report)
            audited[profile] = {"profile": profile, "report": str(report), "cells": 0, "totals": {},
                                "wall_failure": observed["wall_failure"], "safety_failure": observed["safety_failure"],
                                "functional_failure": observed["functional_failure"], "functional_evidence_invalid": observed["functional_evidence_invalid"],
                                "execution_failure": False, "evidence_blocked": True, "audit_error": str(error)}

    witness_result: dict[str, Any] | None = None; witness_error: str | None = None; host_manifest_result: dict[str, Any] | None = None
    valid_states = [state for state in execution.values() if state.get("test_start")]
    expected_host = expected_lease_id = expected_policy_hash = expected_source_head = expected_source_tree = expected_executable_sha = None
    if valid_states:
        sets = [{state.get(key) for state in valid_states} for key in ("host", "lease_id", "policy_sha256", "source_head", "source_tree", "executable_sha256")]
        if all(len(items) == 1 for items in sets):
            expected_host, expected_lease_id, expected_policy_hash, expected_source_head, expected_source_tree, expected_executable_sha = [next(iter(items)) for items in sets]
        else: witness_error = "trusted runner receipts disagree on host, lease, source, executable or process policy"
    try:
        if witness is None: raise ValueError("host witness is required")
        witness_result = validate_witness(witness, execution, expected_host, expected_lease_id, expected_policy_hash,
                                          expected_source_head, expected_source_tree, expected_executable_sha,
                                          require_external_attestations=not allow_unverified_provenance,
                                          allow_fixture=allow_unverified_provenance)
        if not allow_unverified_provenance:
            if host_manifest is None: raise ValueError("production evaluation requires a host artifact manifest")
            host_manifest_result = _verify_host_manifest(host_manifest, witness)
    except ValueError as error:
        witness_error = str(error) if witness_error is None else f"{witness_error}; {error}"
    provenance_verified = set(audited) == set(PROFILES) and all(audited[profile].get("provenance", {}).get("verified") is True for profile in PROFILES if profile in audited)
    host_manifest_verified = host_manifest_result is not None if not allow_unverified_provenance else False
    if not allow_unverified_provenance and (not provenance_verified or host_manifest_result is None):
        witness_error = witness_error or "independent artifact provenance is incomplete"
    result = evaluate_results(audited, witness_result, execution, qualification_prohibited=allow_unverified_provenance,
                              provenance_verified=provenance_verified, host_manifest_verified=host_manifest_verified)
    result["execution_intervals"] = {profile: {key: value.isoformat().replace("+00:00", "Z") if isinstance(value, datetime) else value
                                                for key, value in state.items() if key in {"test_start", "test_end", "host", "lease_id", "policy_sha256", "source_head", "source_tree", "executable_sha256"}}
                                      for profile, state in execution.items()}
    if host_manifest_result is not None: result["host_manifest"] = host_manifest_result
    if witness_error:
        result["evidence_blocked"] = True
        result["primary_status"] = "WALL_GATE_FAIL" if result["performance_failure"] else "WALL_GATE_BLOCKED"
        result["witness_error"] = witness_error
    return result


def _assignments(values: list[str], name: str) -> dict[str, Path]:
    result: dict[str, Path] = {}
    for value in values:
        if "=" not in value: raise ValueError(f"{name} requires PROFILE=PATH")
        profile, path = value.split("=", 1)
        if profile not in PROFILES: raise ValueError(f"unknown profile {profile!r}")
        if profile in result: raise ValueError(f"duplicate {name} profile {profile!r}")
        result[profile] = Path(path)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", action="append", required=True, help="PROFILE=PATH; provide routed, authored and typical")
    parser.add_argument("--receipt", action="append", default=[], help="PROFILE=PATH")
    parser.add_argument("--manifest", action="append", default=[], help="PROFILE=PATH")
    parser.add_argument("--root", type=Path); parser.add_argument("--executable", type=Path)
    parser.add_argument("--expected-head"); parser.add_argument("--expected-tree")
    parser.add_argument("--witness", type=Path); parser.add_argument("--host-manifest", type=Path)
    parser.add_argument("--allow-unverified-provenance", action="store_true", help="fixture mode only; qualification-prohibited and cannot PASS")
    args = parser.parse_args()
    try:
        reports = _assignments(args.report, "--report")
        if set(reports) != set(PROFILES): raise ValueError("exactly one routed, authored and typical report is required")
        receipts = _assignments(args.receipt, "--receipt") if args.receipt else {}
        manifests = _assignments(args.manifest, "--manifest") if args.manifest else {}
        result = evaluate_reports(reports, root=args.root, executable=args.executable, receipts=receipts, manifests=manifests,
                                  expected_head=args.expected_head, expected_tree=args.expected_tree, witness=args.witness,
                                  host_manifest=args.host_manifest, allow_unverified_provenance=args.allow_unverified_provenance)
    except (OSError, ValueError) as error:
        parser.exit(2, f"FX-T18 wall evaluator: {error}\n")
    print(json.dumps(result, indent=2, sort_keys=True, default=str))
    return 0 if result["primary_status"] not in {"WALL_GATE_FAIL", "WALL_GATE_BLOCKED"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
