#!/usr/bin/env python3
"""Finalize an FX-T18 observation only after post-capture owner proofs exist.

Capture is deliberately two-phase.  The PowerShell observer obtains only the
owner-signed policy/lease, records private WMI observations and writes a
blocked ``VOX_FX_T18_HOST_OBSERVATION_V1`` receipt.  This tool consumes that
immutable observation after an independently parsed ETW trace and the complete
routed/authored/typical execution binding have been signed.  It never signs,
repairs, or upgrades an observation; it either emits a reconciled final
witness or fails closed.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import os
import tempfile
from typing import Any

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "fx_t18_wall_evaluator", HERE / "evaluate-fx-t18-wall-gate.py"
)
assert SPEC and SPEC.loader
EVALUATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EVALUATOR)

PROFILES = ("routed", "authored", "typical")
OWNER_SIGNATURE_METHOD = EVALUATOR.OWNER_SIGNATURE_METHOD
SHA256_RE = EVALUATOR.SHA256_RE
SHA1_RE = EVALUATOR.SHA1_RE


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _read_json(path: Path, label: str) -> dict[str, Any]:
    return EVALUATOR._read_json(path, label)


def _utc(value: Any, name: str) -> datetime:
    return EVALUATOR._utc(value, name)


def _absolute(path: Path) -> str:
    return str(path.resolve())


def _same(left: Path, right: Path) -> bool:
    return left.resolve().samefile(right.resolve()) if left.exists() and right.exists() else left.resolve() == right.resolve()


def _proof(output: Path, name: str, data_path: Path, signature_path: Path,
           thumbprint: str, allow_fixture: bool) -> dict[str, Any]:
    if not data_path.is_file() or not signature_path.is_file():
        raise ValueError(f"{name}: signed data or detached signature is missing")
    proof = {
        "verified": True,
        "data_sha256": _sha256(data_path),
        "signature_sha256": _sha256(signature_path),
        "signer_thumbprint": thumbprint,
        "verification_method": OWNER_SIGNATURE_METHOD,
        "data_path": _absolute(data_path),
        "signature_path": _absolute(signature_path),
    }
    # This performs the CMS/chain/thumbprint check in production.  Fixture
    # mode still checks both file hashes but cannot create qualification proof.
    EVALUATOR._verify_signed_proof(
        output, proof, name, expected_thumbprint=thumbprint,
        verify_cryptography=not allow_fixture,
    )
    return proof


def _require_observation(observation_path: Path, allow_fixture: bool) -> dict[str, Any]:
    value = _read_json(observation_path, "host observation")
    if value.get("format") != "VOX_FX_T18_HOST_OBSERVATION_V1" or value.get("schema_version") != 1:
        raise ValueError(f"{observation_path}: only schema-1 observation receipts can be finalized")
    if value.get("mode") != "observe" or value.get("phase") != "observe" \
            or value.get("finalization_required") is not True \
            or value.get("evidence_blocked") is not True:
        raise ValueError(f"{observation_path}: observation is not an explicitly blocked pre-finalization receipt")
    fixture = value.get("fixture_mode") is True or value.get("qualification_prohibited") is True
    if fixture and not allow_fixture:
        raise ValueError(f"{observation_path}: fixture observation cannot produce production evidence")
    reasons = value.get("diagnostic", {}).get("reason_codes", [])
    if not isinstance(reasons, list) or any(reason != "FINALIZATION_REQUIRED" for reason in reasons):
        raise ValueError(f"{observation_path}: observation contains a capture failure and cannot be finalized")
    if not isinstance(value.get("samples"), list) or not value["samples"]:
        raise ValueError(f"{observation_path}: complete observation samples are missing")
    if not isinstance(value.get("process_events"), list):
        raise ValueError(f"{observation_path}: observation process event list is missing")
    return value


def _intervals(binding: dict[str, Any]) -> dict[str, dict[str, Any]]:
    profiles = binding.get("profiles")
    if not isinstance(profiles, list) or {item.get("profile") for item in profiles if isinstance(item, dict)} != set(PROFILES):
        raise ValueError("execution binding does not contain exactly routed/authored/typical profiles")
    result: dict[str, dict[str, Any]] = {}
    for item in profiles:
        if not isinstance(item, dict):
            raise ValueError("execution binding profile is not an object")
        profile = item.get("profile")
        if profile in result:
            raise ValueError(f"execution binding profile {profile} is duplicated")
        start = _utc(item.get("test_start_utc"), f"binding.{profile}.test_start_utc")
        end = _utc(item.get("test_end_utc"), f"binding.{profile}.test_end_utc")
        if end <= start:
            raise ValueError(f"binding.{profile}: interval is not positive")
        result[profile] = {"test_start": start, "test_end": end}
    return result


def finalize(
    observation_path: Path,
    output_path: Path,
    policy_path: Path,
    policy_signature_path: Path,
    lease_path: Path,
    lease_signature_path: Path,
    event_loss_path: Path,
    event_loss_signature_path: Path,
    execution_binding_path: Path,
    execution_binding_signature_path: Path,
    owner_certificate_thumbprint: str,
    allow_fixture: bool = False,
) -> dict[str, Any]:
    try:
        thumbprint = EVALUATOR._normalize_thumbprint(owner_certificate_thumbprint)
    except ValueError as error:
        raise ValueError("independent owner certificate thumbprint is malformed") from error
    observation = _require_observation(observation_path, allow_fixture)
    capture_start = _utc(observation.get("capture_start_utc"), "capture_start_utc")
    capture_end = _utc(observation.get("capture_end_utc"), "capture_end_utc")
    if capture_end <= capture_start:
        raise ValueError("observation capture interval is not positive")

    private_path_value = observation.get("private_inventory_path")
    if not isinstance(private_path_value, str) or private_path_value.startswith("<"):
        raise ValueError("observation private inventory path is missing")
    private_path = Path(private_path_value)
    private_hash = observation.get("private_inventory_sha256")
    if not private_path.is_file() or not isinstance(private_hash, str) \
            or not SHA256_RE.fullmatch(private_hash) or _sha256(private_path).casefold() != private_hash.casefold():
        raise ValueError("observation private inventory is missing or hash-mismatched")

    policy_path = policy_path.resolve(); lease_path = lease_path.resolve()
    event_loss_path = event_loss_path.resolve(); execution_binding_path = execution_binding_path.resolve()
    if not _same(policy_path, Path(str(observation.get("identity_policy_path", "")))):
        raise ValueError("final policy path does not match the observation policy path")
    observation_lease_path = Path(str(observation.get("runner_lease", {}).get("verification_receipt_path", "")))
    if not _same(lease_path, observation_lease_path):
        raise ValueError("final lease path does not match the observation lease path")
    if observation.get("identity_policy_sha256") != _sha256(policy_path):
        raise ValueError("policy bytes changed after the observation")
    if observation.get("runner_lease", {}).get("verification_receipt_sha256") != _sha256(lease_path):
        raise ValueError("lease bytes changed after the observation")

    policy = _read_json(policy_path, "identity policy")
    lease = _read_json(lease_path, "host lease receipt")
    loss = _read_json(event_loss_path, "ETW loss receipt")
    binding = _read_json(execution_binding_path, "execution binding")
    parse_value = loss.get("collector_parse_receipt_path")
    if not isinstance(parse_value, str) or not parse_value:
        raise ValueError("ETW loss receipt does not name an independent parse receipt")
    parse_path = Path(parse_value)
    if not parse_path.is_absolute():
        parse_path = (event_loss_path.parent / parse_path).resolve()
    parse_receipt = _read_json(parse_path, "ETW parse receipt")
    parser_provenance = parse_receipt.get("parser_provenance")
    summary_value = parser_provenance.get("summary_path") if isinstance(parser_provenance, dict) else None
    summary_hash = parser_provenance.get("summary_sha256") if isinstance(parser_provenance, dict) else None
    collection_value = parser_provenance.get("collection_metadata_path") if isinstance(parser_provenance, dict) else None
    collection_hash = parser_provenance.get("collection_metadata_sha256") if isinstance(parser_provenance, dict) else None
    summary_metadata: dict[str, Any] | None = None
    collection_metadata: dict[str, Any] | None = None
    if not allow_fixture and (not isinstance(summary_value, str) or not summary_value
                              or not isinstance(summary_hash, str) or not SHA256_RE.fullmatch(summary_hash)):
        raise ValueError("ETW parse receipt does not name a retained tracerpt summary artifact")
    if not allow_fixture and (not isinstance(collection_value, str) or not collection_value
                              or not isinstance(collection_hash, str) or not SHA256_RE.fullmatch(collection_hash)):
        raise ValueError("ETW parse receipt does not name retained collection start/stop metadata")
    if isinstance(summary_value, str) and summary_value:
        summary_path = Path(summary_value)
        if not summary_path.is_absolute():
            summary_path = (parse_path.parent / summary_path).resolve()
        if not allow_fixture:
            if not summary_path.is_file() or _sha256(summary_path).casefold() != str(summary_hash).casefold():
                raise ValueError("retained tracerpt summary artifact is missing or hash-mismatched")
            try:
                summary_metadata = EVALUATOR.ETL_PARSER._parse_tracerpt_summary(
                    EVALUATOR.ETL_PARSER._read_summary_text(summary_path)
                )
            except (OSError, ValueError) as error:
                raise ValueError("retained tracerpt summary artifact is not parseable") from error
            for field in ("lost_events", "buffer_loss_events"):
                if summary_metadata[field] != parse_receipt.get(field):
                    raise ValueError("retained tracerpt summary counters do not reconcile to the parse receipt")
            for field in ("coverage_start_utc", "coverage_end_utc", "session_name"):
                if summary_metadata[field] is not None and summary_metadata[field] != parse_receipt.get(field):
                    raise ValueError("retained tracerpt summary metadata does not reconcile to the parse receipt")
        summary_path_text = _absolute(summary_path)
    else:
        summary_path_text = "<fixture-summary>"
    if isinstance(collection_value, str) and collection_value:
        collection_path = Path(collection_value)
        if not collection_path.is_absolute():
            collection_path = (parse_path.parent / collection_path).resolve()
        if not allow_fixture and (not collection_path.is_file()
                                  or _sha256(collection_path).casefold() != str(collection_hash).casefold()):
            raise ValueError("retained collection start/stop metadata is missing or hash-mismatched")
        if not allow_fixture:
            raw_value = parse_receipt.get("collector_artifact_path")
            if not isinstance(raw_value, str) or not raw_value:
                raise ValueError("ETW parse receipt does not name the retained raw ETL")
            raw_path = Path(raw_value)
            if not raw_path.is_absolute():
                raw_path = (parse_path.parent / raw_path).resolve()
            try:
                collection_metadata = EVALUATOR.ETL_PARSER._read_collection_metadata(
                    collection_path, raw_path, summary_metadata or {}
                )
            except (OSError, ValueError) as error:
                raise ValueError("retained collection metadata is not parseable or does not reconcile") from error
            for field in ("coverage_start_utc", "coverage_end_utc", "session_name"):
                if collection_metadata[field] != parse_receipt.get(field):
                    raise ValueError("retained collection metadata does not reconcile to the parse receipt")
        collection_path_text = _absolute(collection_path)
    else:
        collection_path_text = "<fixture-collection-metadata>"
    collection_control_receipts = (
        collection_metadata.get("collection_control_receipts", [])
        if isinstance(collection_metadata, dict)
        else (parser_provenance.get("collection_control_receipts", [])
              if isinstance(parser_provenance, dict) else [])
    )
    if not allow_fixture and (not isinstance(collection_control_receipts, list)
                              or len(collection_control_receipts) != 2):
        raise ValueError("retained logman start/stop control receipts are missing")
    if (not allow_fixture and isinstance(parser_provenance, dict)
            and collection_control_receipts != parser_provenance.get("collection_control_receipts")):
        raise ValueError("retained logman control receipts do not reconcile to the parse provenance")
    observation_hash = _sha256(observation_path)
    if loss.get("observation_sha256") != observation_hash or binding.get("observation_sha256") != observation_hash:
        raise ValueError("post-capture signed proofs are not bound to the immutable observation")
    intervals = _intervals(binding)

    output_path = output_path.resolve()
    proofs = {
        "policy": _proof(output_path, "policy", policy_path, policy_signature_path.resolve(), thumbprint, allow_fixture),
        "lease": _proof(output_path, "lease", lease_path, lease_signature_path.resolve(), thumbprint, allow_fixture),
        "event_loss": _proof(output_path, "event_loss", event_loss_path, event_loss_signature_path.resolve(), thumbprint, allow_fixture),
        "execution_binding": _proof(output_path, "execution_binding", execution_binding_path, execution_binding_signature_path.resolve(), thumbprint, allow_fixture),
    }

    if not isinstance(observation.get("runner_service_identities"), list):
        raise ValueError("observation runner service identities are missing")
    policy_hash = _sha256(policy_path)
    witness: dict[str, Any] = {
        "format": "VOX_FX_T18_HOST_WITNESS_V1",
        "schema_version": 2,
        "mode": "finalize",
        "phase": "finalize",
        "fixture_mode": bool(observation.get("fixture_mode") is True or allow_fixture),
        "qualification_prohibited": bool(observation.get("qualification_prohibited") is True or allow_fixture),
        "host_observation_mode": observation.get("host_observation_mode"),
        "sample_interval_ms": observation.get("sample_interval_ms"),
        "pre_seconds": observation.get("pre_seconds"),
        "during_seconds": observation.get("during_seconds"),
        "post_seconds": observation.get("post_seconds"),
        "capture_start_utc": observation["capture_start_utc"],
        "capture_end_utc": observation["capture_end_utc"],
        "observation_path": _absolute(observation_path),
        "observation_sha256": observation_hash,
        "monitor_gaps": observation.get("monitor_gaps", []),
        "identity_policy": {
            "format": policy.get("format"), "policy_id": policy.get("policy_id"),
            "baseline_id": policy.get("baseline_id"), "policy_sha256": policy_hash,
            "runner_service_identity_sha256": list(policy.get("runner_service_identity_sha256", [])),
            "host_baseline_id": policy.get("host_baseline_id"),
            "runner_service_count": len(policy.get("runner_service_identity_sha256", [])),
            "approved_process_count": len(policy.get("approved_processes", [])),
            "host_baseline_process_count": len(policy.get("host_baseline_processes", [])),
            "policy_decision_method": "owner_signed_name_path_hash_parent_service_and_pid_start_reconciliation",
        },
        "identity_policy_path": _absolute(policy_path),
        "identity_policy_sha256": policy_hash,
        "private_inventory_path": _absolute(private_path),
        "private_inventory_sha256": private_hash,
        "attestation_proofs": proofs,
        "event_trace": {
            "observation_provider": "Win32_ProcessStartTrace/Win32_ProcessStopTrace",
            "loss_provider": loss.get("provider"),
            "available": True,
            "coverage_complete": loss.get("coverage_complete"),
            "lost_events": loss.get("lost_events"),
            "buffer_loss_events": loss.get("buffer_loss_events"),
            "events_received": loss.get("events_received"),
            "session_name": loss.get("session_name"),
            "loss_accounting": "etw_provider_counters",
            "coverage_start_utc": loss.get("coverage_start_utc"),
            "coverage_end_utc": loss.get("coverage_end_utc"),
            "trace_id": loss.get("trace_id"),
            "collector_artifact_path": _absolute((event_loss_path.parent / str(loss.get("collector_artifact_path"))).resolve()) if not Path(str(loss.get("collector_artifact_path"))).is_absolute() else str(Path(str(loss.get("collector_artifact_path"))).resolve()),
            "collector_artifact_sha256": loss.get("collector_artifact_sha256"),
            "collector_artifact_format": loss.get("collector_artifact_format"),
            "collector_parse_method": loss.get("collector_parse_method"),
            "collector_parse_receipt_path": _absolute((event_loss_path.parent / str(loss.get("collector_parse_receipt_path"))).resolve()) if not Path(str(loss.get("collector_parse_receipt_path"))).is_absolute() else str(Path(str(loss.get("collector_parse_receipt_path"))).resolve()),
            "collector_parse_receipt_sha256": loss.get("collector_parse_receipt_sha256"),
            "tracerpt_summary_path": summary_path_text,
            "tracerpt_summary_sha256": summary_hash if isinstance(summary_hash, str) else "<fixture-summary>",
            "collection_metadata_path": collection_path_text,
            "collection_metadata_sha256": collection_hash if isinstance(collection_hash, str) else "<fixture-collection-metadata>",
            "collection_control_receipt_source": parser_provenance.get("collection_control_receipt_source") if isinstance(parser_provenance, dict) else "fixture-declared-metadata",
            "collection_control_receipts": collection_control_receipts,
            "collector_trace_mode": collection_metadata.get("trace_mode") if isinstance(collection_metadata, dict) else "<fixture>",
            "collector_max_file_mb": collection_metadata.get("max_file_mb") if isinstance(collection_metadata, dict) else "<fixture>",
            "collector_bytes_written": collection_metadata.get("bytes_written") if isinstance(collection_metadata, dict) else "<fixture>",
            "collector_rotation_count": collection_metadata.get("rotation_count") if isinstance(collection_metadata, dict) else "<fixture>",
            "collector_etl_file_count": collection_metadata.get("etl_file_count") if isinstance(collection_metadata, dict) else "<fixture>",
            "collector_file_policy": collection_metadata.get("file_policy") if isinstance(collection_metadata, dict) else "<fixture>",
            "collector_file_inventory_source": collection_metadata.get("file_inventory_source") if isinstance(collection_metadata, dict) else "<fixture>",
            "collector_cleanup_mode": collection_metadata.get("cleanup_mode") if isinstance(collection_metadata, dict) else "<fixture>",
            "collector_delete_applicable": collection_metadata.get("delete_applicable") if isinstance(collection_metadata, dict) else "<fixture>",
            "collector_delete_exit_code": collection_metadata.get("delete_exit_code") if isinstance(collection_metadata, dict) else "<fixture>",
            "collector_truncation_detected": collection_metadata.get("truncation_detected") if isinstance(collection_metadata, dict) else "<fixture>",
            "parser_provenance": parse_receipt.get("parser_provenance"),
            "correlation_max_skew_seconds": EVALUATOR.MAX_EVENT_CORRELATION_SKEW_SECONDS,
            "correlation_identity_fields": ["event", "pid", "process_name", "start_time_utc", "identity_digest", "executable_path", "executable_sha256", "parent_pid", "service_identity_sha256"],
            "loss_receipt_path": _absolute(event_loss_path),
            "loss_receipt_sha256": _sha256(event_loss_path),
            "process_event_count": len(loss.get("process_events", [])) if isinstance(loss.get("process_events"), list) else -1,
            "error": None,
        },
        "runner_lease": {
            "recognized": True, "owner_verified": True, "owner": lease.get("owner"),
            "host": lease.get("host"), "lease_id": lease.get("lease_id"),
            "verification_receipt_path": _absolute(lease_path),
            "verification_receipt_sha256": _sha256(lease_path), "token": "<redacted>",
        },
        "runner_service_identities": list(observation["runner_service_identities"]),
        "execution_binding": {
            "verified": True, "binding_id": binding.get("binding_id"), "host": binding.get("host"),
            "runner_job_id": binding.get("runner_job_id"), "source_head": binding.get("source_head"),
            "source_tree": binding.get("source_tree"), "executable_path": binding.get("executable_path"),
            "executable_sha256": binding.get("executable_sha256"), "lease_id": binding.get("lease_id"),
            "policy_sha256": binding.get("policy_sha256"), "profiles": binding.get("profiles"),
            "runner_receipts": binding.get("runner_receipts"),
            "binding_receipt_path": _absolute(execution_binding_path),
            "binding_receipt_sha256": _sha256(execution_binding_path),
        },
        "samples": observation["samples"],
        "process_events": observation["process_events"],
        "unrecognized_competitors": observation.get("unrecognized_competitors", 0),
        "unknown_pids": observation.get("unknown_pids", 0),
        "pid_reuse_mismatches": observation.get("pid_reuse_mismatches", 0),
        "inventory_summary": observation.get("inventory_summary", {}),
        "evidence_blocked": False,
        "query_errors": [],
        "fixture_notes": "Finalized only after immutable post-capture owner proofs and ETW/WMI reconciliation.",
    }

    expected_host = lease.get("host")
    expected_lease_id = lease.get("lease_id")
    expected_executable = binding.get("executable_sha256")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=output_path.parent,
                                     prefix=output_path.name + ".", suffix=".tmp", delete=False) as stream:
        json.dump(witness, stream, indent=2, sort_keys=True)
        stream.write("\n")
        temporary = Path(stream.name)
    try:
        EVALUATOR.validate_witness(
            temporary, intervals, expected_host, expected_lease_id, policy_hash,
            binding.get("source_head"), binding.get("source_tree"), expected_executable,
            require_external_attestations=not allow_fixture,
            allow_fixture=allow_fixture,
            owner_certificate_thumbprint=thumbprint,
        )
        if allow_fixture:
            # validate_witness intentionally skips signed external proofs for
            # qualification-prohibited fixtures; still exercise the structural
            # finalization reconciliation so missing ETW/WMI events cannot pass.
            EVALUATOR._verify_external_witness_attestations(
                temporary, witness, capture_start, capture_end,
                witness["identity_policy"], witness["runner_lease"], witness["event_trace"],
                witness["execution_binding"], expected_host, expected_lease_id, policy_hash,
                binding.get("source_head"), binding.get("source_tree"), expected_executable,
                allow_fixture=True, owner_certificate_thumbprint=thumbprint,
            )
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    os.replace(temporary, output_path)
    return witness


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--observation", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--identity-policy", type=Path, required=True)
    parser.add_argument("--identity-policy-signature", type=Path, required=True)
    parser.add_argument("--lease", type=Path, required=True)
    parser.add_argument("--lease-signature", type=Path, required=True)
    parser.add_argument("--event-loss", type=Path, required=True)
    parser.add_argument("--event-loss-signature", type=Path, required=True)
    parser.add_argument("--execution-binding", type=Path, required=True)
    parser.add_argument("--execution-binding-signature", type=Path, required=True)
    parser.add_argument("--owner-certificate-thumbprint", required=True)
    parser.add_argument("--allow-fixture", action="store_true", help="structural fixture only; output remains qualification-prohibited")
    args = parser.parse_args()
    try:
        finalize(
            args.observation, args.output, args.identity_policy, args.identity_policy_signature,
            args.lease, args.lease_signature, args.event_loss, args.event_loss_signature,
            args.execution_binding, args.execution_binding_signature,
            args.owner_certificate_thumbprint, allow_fixture=args.allow_fixture,
        )
    except (OSError, ValueError) as error:
        parser.exit(2, f"FX-T18 host witness finalizer: {error}\n")
    print(f"FX-T18 host witness finalization: {'FIXTURE_BLOCKED' if args.allow_fixture else 'PRODUCTION_READY_FOR_EVALUATION'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
