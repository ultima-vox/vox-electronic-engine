#!/usr/bin/env python3
"""Executable conformance fixtures for the FX-T18 wall-gate evaluator.

These are offline schema fixtures. They deliberately do not claim Windows
qualification or replace the owner-authorized 117-cell run.
"""
from __future__ import annotations

from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent

def load(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

AUDIT_FIXTURES = load("fx_t18_audit_fixtures", "test-audit-fx-windows-timing.py")
EVALUATOR = load("fx_t18_wall_evaluator", "evaluate-fx-t18-wall-gate.py")
AUDIT = EVALUATOR.AUDIT
FINALIZER = load("fx_t18_host_finalizer", "finalize-fx-t18-host-witness.py")


def profile_report(text: str, profile: str) -> str:
    if profile != "typical":
        output = []
        for line in text.splitlines():
            fields = line.split("\t")
            if fields[0] == "meta" and fields[1] == "profile":
                line = f"meta\tprofile\t{profile}"
            elif fields[0] == "case":
                fields[2] = profile
                line = "\t".join(fields)
            output.append(line)
        return "\n".join(output) + "\n"
    keep = {
        f"r{rate}_f{frames}_v1_poff"
        for rate in (44100, 48000, 96000)
        for frames in (64, 128, 512)
    }
    output = []
    for line in text.splitlines():
        fields = line.split("\t")
        if fields[0] == "meta" and fields[1] == "profile":
            output.append("meta\tprofile\ttypical")
        elif fields[0] == "meta" and fields[1] == "sentinel_cases":
            output.append("meta\tsentinel_cases\tnot-applicable")
        elif fields[0] in {"case", "block", "miss", "functional"}:
            if len(fields) > 1 and fields[1] in keep:
                if fields[0] == "case":
                    fields[2] = "typical"
                output.append("\t".join(fields))
        else:
            output.append(line)
    return "\n".join(output) + "\n"


def quantized(text: str) -> str:
    return AUDIT_FIXTURES.quantized_cpu_report_text(text)


def mutate(text: str, target: str, **updates: str) -> str:
    return AUDIT_FIXTURES.mutate_functional_record(text, target, **updates)


def capture_sampler_samples(base: datetime) -> list[dict]:
    """Mirror Add-PhaseSamples: one global monotonic origin and phase offsets."""
    samples: list[dict] = []
    deadline_offset = 0
    for phase, seconds in (("pre", 30), ("during", 60), ("post", 30)):
        for index in range(seconds + 1):
            elapsed = deadline_offset + index
            timestamp = base + timedelta(seconds=elapsed)
            samples.append({
                "phase": phase,
                "sample_ordinal": len(samples),
                "timestamp_utc": timestamp.isoformat().replace("+00:00", "Z"),
                "scheduled_timestamp_utc": timestamp.isoformat().replace("+00:00", "Z"),
                "monotonic_elapsed_ms": elapsed * 1000,
                "processes": [{"pid": 1000, "process_name": "Runner.Listener.exe",
                               "identity_known": True, "approved_by_policy": True}],
                "total_processes": 1, "process_query_error": None,
            })
        deadline_offset += seconds + 1
    return samples


def valid_witness(competitors: list[str] | None = None,
                  samples: list[dict] | None = None,
                  mutate: dict | None = None) -> dict:
    base = datetime(2026, 11, 22, tzinfo=timezone.utc)
    policy = {
        "format": "VOX_FX_T18_IDENTITY_POLICY_V1",
        "policy_id": "fixture-policy",
        "baseline_id": "fixture-baseline",
        "owner_approved": True,
        "policy_sha256": "a" * 64,
        "runner_service_identity_sha256": ["1" * 64, "2" * 64, "3" * 64, "4" * 64],
        "approved_processes": [{"pid": 1000, "process_name": "Runner.Listener.exe", "role": "runner",
                               "executable_path": "C:\\HAMUNAPTRA\\Runner.Listener.exe",
                               "executable_sha256": "5" * 64, "parent_pid": 4,
                               "service_identity_sha256": "1" * 64,
                               "start_time_utc": (base - timedelta(seconds=60)).isoformat().replace("+00:00", "Z"),
                               "allow_dynamic": False}],
        "denied_process_name_regex": ["^(devenv|msbuild|cmake|ctest|ninja|python|cubase|reaper)"],
    }
    process = {
        "pid": 1000, "process_name": "Runner.Listener.exe", "parent_pid": 1,
        "start_time_utc": (base - timedelta(seconds=60)).isoformat().replace("+00:00", "Z"),
        "command_line": "<redacted>", "job_identity": "<redacted-or-not-applicable>",
        "service_identity": "<redacted-or-not-applicable>", "identity_known": True,
        "approved_by_policy": True,
    }
    if samples is None:
        samples = capture_sampler_samples(base)
        for sample in samples:
            sample["processes"] = [dict(process)]
    witness = {
        "format": "VOX_FX_T18_HOST_WITNESS_V1",
        "schema_version": 2,
        "fixture_mode": True,
        "qualification_prohibited": True,
        "host_observation_mode": "process_snapshot_and_event_trace",
        "sample_interval_ms": 1000,
        "pre_seconds": 30, "during_seconds": 60, "post_seconds": 30,
        "capture_start_utc": base.isoformat().replace("+00:00", "Z"),
        "capture_end_utc": (base + timedelta(seconds=122)).isoformat().replace("+00:00", "Z"),
        "monitor_gaps": [], "identity_policy": policy,
        "identity_policy_path": "<fixture-policy>",
        "private_inventory_path": "<fixture-private-inventory>",
        "private_inventory_sha256": "e" * 64,
        "attestation_proofs": {},
        "event_trace": {"available": True, "coverage_complete": 1, "lost_events": 0,
                        "buffer_loss_events": 0, "events_received": 0,
                        "loss_provider": "fixture-loss-provider",
                        "loss_accounting": "fixture_unverified"},
        "runner_lease": {"recognized": True, "owner_verified": True, "owner": "fixture",
                         "host": "fixture", "lease_id": "fixture-lease",
                         "verification_receipt_path": "<fixture-lease>",
                         "verification_receipt_sha256": "b" * 64, "token": "<redacted>"},
        "runner_service_identities": ["1" * 64, "2" * 64, "3" * 64, "4" * 64],
        "execution_binding": {"verified": True, "host": "fixture", "runner_job_id": "fixture-job",
                               "source_head": "a" * 40, "source_tree": "b" * 40,
                               "executable_sha256": "c" * 64, "lease_id": "fixture-lease",
                               "policy_sha256": "a" * 64,
                               "profiles": [
                                   {"profile": "routed", "test_identity": "suite_fx_runtime_stress_tests", "test_start_utc": "2026-11-22T00:01:00Z", "test_end_utc": "2026-11-22T00:01:05Z", "ctest_exit_code": 0},
                                   {"profile": "authored", "test_identity": "suite_fx_runtime_stress_tests", "test_start_utc": "2026-11-22T00:01:08Z", "test_end_utc": "2026-11-22T00:01:13Z", "ctest_exit_code": 0},
                                   {"profile": "typical", "test_identity": "suite_fx_runtime_stress_tests", "test_start_utc": "2026-11-22T00:01:16Z", "test_end_utc": "2026-11-22T00:01:21Z", "ctest_exit_code": 0}],
                               "binding_receipt_path": "<fixture-binding>", "binding_receipt_sha256": "d" * 64},
        "samples": samples, "process_events": [],
        "unrecognized_competitors": competitors or 0, "unknown_pids": 0,
        "pid_reuse_mismatches": 0, "evidence_blocked": False,
    }
    if mutate:
        witness.update(mutate)
    return witness


def _fixture_process(base: datetime, *, pid: int = 1000,
                     name: str = "Runner.Listener.exe", executable_hash: str = "5" * 64,
                     executable_path: str | None = None, parent_pid: int = 4,
                     service_hash: str = "1" * 64, parent_identity: dict | None = None) -> dict:
    start_time = (base - timedelta(seconds=60)).isoformat(timespec="microseconds").replace("+00:00", "Z")
    node = {
        "pid": pid, "process_name": name, "parent_pid": parent_pid,
        "start_time_utc": start_time,
        "executable_path": executable_path or f"C:\\fixture\\{name}",
        "executable_sha256": executable_hash,
        "service_identity_sha256": [service_hash],
    }
    node["identity_digest"] = EVALUATOR._identity_digest(node)
    process = dict(node)
    process.update({
        "service_ancestry_verified": True, "lineage_verification": "runner_service_ancestry_v1",
        "service_ancestry": [node], "identity_known": True, "approved_by_policy": True,
        "decision_reason": None, "parent_identity": parent_identity,
    })
    process["identity_digest"] = EVALUATOR._identity_digest(process)
    process["service_ancestry"][0]["identity_digest"] = EVALUATOR._identity_digest(process["service_ancestry"][0])
    return process


def _fixture_public_sample(raw: dict) -> dict:
    processes = raw["processes"]
    events = raw["events"]
    return {
        "phase": raw["phase"], "sample_ordinal": raw["sample_ordinal"],
        "timestamp_utc": raw["timestamp_utc"], "scheduled_timestamp_utc": raw["scheduled_timestamp_utc"],
        "monotonic_elapsed_ms": raw["monotonic_elapsed_ms"],
        "full_host_process_count": len(processes), "full_host_hash_count": len(processes),
        "full_host_cim_hash_duration_ms": 10.0, "full_host_cim_hash_cadence_met": True,
        "observed_process_count": len(processes),
        "approved_process_count": sum(item["approved_by_policy"] is True for item in processes),
        "unknown_identity_count": sum(item["identity_known"] is not True for item in processes + events),
        "unexpected_identity_count": sum(item["approved_by_policy"] is not True for item in processes + events),
        "pid_reuse_mismatch_count": sum(item.get("decision_reason") == "PID_REUSE_OR_DYNAMIC_IDENTITY_MISMATCH" for item in events),
        "process_query_error": raw.get("query_error"),
        "process_inventory_sha256": EVALUATOR._canonical_sample_digest(raw),
    }


def strict_witness_fixture(directory: Path, etw_events: list[dict] | None = None,
                           wmi_events: list[dict] | None = None) -> tuple[Path, Path]:
    """Build a qualification-prohibited fixture with a real raw replay path."""
    etw_events = list(etw_events or [])
    wmi_events = list(wmi_events or [])
    directory.mkdir(parents=True, exist_ok=True)
    base = datetime(2026, 11, 22, tzinfo=timezone.utc)
    process = _fixture_process(base)
    policy = {
        "format": "VOX_FX_T18_IDENTITY_POLICY_V1", "policy_id": "fixture-policy",
        "host": "fixture", "baseline_id": "fixture-baseline", "owner_approved": True,
        "valid_from_utc": "2026-11-21T23:59:00Z", "valid_until_utc": "2026-11-22T00:03:00Z",
        "runner_service_identity_sha256": ["1" * 64, "2" * 64, "3" * 64, "4" * 64],
        "host_baseline_id": "fixture-host-baseline",
        "host_baseline_processes": [{"identity_scope": "host_baseline", "non_competing": True,
                                     "process_name": "svchost.exe", "lineage_mode": "host_baseline_v1",
                                     "allow_dynamic": True, "executable_path": "C:\\Windows\\System32\\svchost.exe",
                                     "executable_sha256": "6" * 64, "parent_pid": 4,
                                     "service_identity_sha256": "7" * 64,
                                     "parent_identity": {"process_name": "services.exe",
                                                         "executable_path": "C:\\Windows\\System32\\services.exe",
                                                         "executable_sha256": "8" * 64}}],
        "approved_processes": [{"pid": 1000, "process_name": "Runner.Listener.exe", "role": "runner",
                                "lineage_mode": "runner_service_ancestry", "allow_dynamic": False,
                                "start_time_utc": process["start_time_utc"],
                                "executable_path": process["executable_path"],
                                "executable_sha256": process["executable_sha256"],
                                "parent_pid": 4, "service_identity_sha256": "1" * 64}],
        "denied_process_name_regex": ["^(devenv|msbuild|cmake|ctest|ninja|python|cubase|reaper)"],
    }
    policy_file = directory / "identity-policy.json"
    policy_file.write_text(json.dumps(policy, indent=2), encoding="utf-8")
    policy_hash = hashlib.sha256(policy_file.read_bytes()).hexdigest()

    samples: list[dict] = []
    for sample in capture_sampler_samples(base):
        raw = {
            "phase": sample["phase"], "sample_ordinal": sample["sample_ordinal"],
            "timestamp_utc": sample["timestamp_utc"], "scheduled_timestamp_utc": sample["scheduled_timestamp_utc"],
            "monotonic_elapsed_ms": sample["monotonic_elapsed_ms"], "query_error": None,
            "full_host_process_count": 1, "full_host_hash_count": 1,
            "full_host_cim_hash_duration_ms": 10.0, "full_host_cim_hash_cadence_met": True,
            "processes": [json.loads(json.dumps(process))], "events": [],
        }
        sample_time = EVALUATOR._utc(raw["timestamp_utc"], "fixture.sample.timestamp_utc")
        for event in wmi_events:
            event_time = EVALUATOR._utc(event["timestamp_utc"], "fixture.event.timestamp_utc")
            if event_time <= sample_time and not any(item is event for item in raw["events"]):
                # Events are assigned once below; this branch is replaced by
                # the ordinal map after all samples are created.
                pass
        samples.append(raw)
    for event in wmi_events:
        event_time = EVALUATOR._utc(event["timestamp_utc"], "fixture.event.timestamp_utc")
        target = next((item for item in samples if EVALUATOR._utc(item["timestamp_utc"], "fixture.sample.timestamp_utc") >= event_time), samples[-1])
        target["events"].append(json.loads(json.dumps(event)))
    public_samples = [_fixture_public_sample(raw) for raw in samples]
    public_events = [{"event": event["event"], "timestamp_utc": event["timestamp_utc"],
                      "identity_known": event["identity_known"], "approved_by_policy": event["approved_by_policy"],
                      "identity_digest": event["identity_digest"],
                      "decision_reason": event.get("decision_reason") or "APPROVED"}
                     for event in wmi_events]

    lease_file = directory / "lease.json"
    lease_file.write_text(json.dumps({
        "format": "VOX_FX_T18_HOST_LEASE_RECEIPT_V1", "verified": True,
        "owner_approved": True, "attestation_source": "owner_controlled_offline",
        "host": "fixture", "lease_id": "fixture-lease", "owner": "fixture",
        "policy_sha256": policy_hash, "valid_from_utc": "2026-11-21T23:59:00Z",
        "valid_until_utc": "2026-11-22T00:03:00Z",
        "verification_method": "owner_signed_detached_cms",
    }, indent=2), encoding="utf-8")

    collector_file = directory / "collector.etl.json"
    collector_file.write_text(json.dumps({
        "format": "VOX_FX_T18_RAW_ETL_FIXTURE_V1", "provider": "Microsoft-Windows-Kernel-Process/ETW",
        "counters": {"lost_events": 0, "buffer_loss_events": 0}, "process_events": etw_events,
    }, indent=2), encoding="utf-8")
    parse_file = directory / "collector-parse.json"
    parse_value = EVALUATOR.ETL_PARSER.make_receipt(
        collector_file, "2026-11-21T23:59:00Z", "2026-11-22T00:03:00Z",
        "fixture-etw-session", fixture=True)
    parse_file.write_text(json.dumps(parse_value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    collector_hash = hashlib.sha256(collector_file.read_bytes()).hexdigest()
    parse_hash = hashlib.sha256(parse_file.read_bytes()).hexdigest()

    loss_file = directory / "event-loss.json"
    loss_file.write_text(json.dumps({
        "format": "VOX_FX_T18_EVENT_LOSS_RECEIPT_V1", "provider": "Microsoft-Windows-Kernel-Process/ETW",
        "measurement_method": "ETW_PROCESS_TRACE_WITH_LOSS_COUNTERS", "coverage_complete": True,
        "lost_events": 0, "buffer_loss_events": 0, "events_received": len(etw_events),
        "session_name": "fixture-etw-session", "owner_approved": True,
        "attestation_source": "owner_controlled_offline", "verification_method": "owner_signed_detached_cms",
        "trace_id": "fixture-trace", "collector_artifact_path": str(collector_file),
        "collector_artifact_sha256": collector_hash, "collector_artifact_format": "ETL",
        "collector_parse_method": "owner_independent_etw_parser_v1",
        "collector_parse_receipt_path": str(parse_file), "collector_parse_receipt_sha256": parse_hash,
        "process_events": etw_events, "coverage_start_utc": "2026-11-21T23:59:00Z",
        "coverage_end_utc": "2026-11-22T00:03:00Z",
        "parser_provenance": parse_value["parser_provenance"],
    }, indent=2), encoding="utf-8")

    runner_receipt_files = []
    runner_receipt_claims = []
    for profile in ("routed", "authored", "typical"):
        receipt_file = directory / f"{profile}.runner.json"
        receipt_file.write_text(json.dumps({"format": "VOX_FX_T18_RUNNER_RECEIPT_V1", "profile": profile, "fixture": True}, indent=2), encoding="utf-8")
        runner_receipt_files.append(receipt_file)
        runner_receipt_claims.append({"profile": profile, "receipt_path": str(receipt_file), "receipt_sha256": hashlib.sha256(receipt_file.read_bytes()).hexdigest()})
    binding_file = directory / "execution-binding.json"
    binding_value = {
        "format": "VOX_FX_T18_EXECUTION_BINDING_V1", "binding_id": "fixture-binding",
        "owner_approved": True, "attestation_source": "owner_controlled_offline",
        "verification_method": "owner_signed_detached_cms", "host": "fixture", "runner_job_id": "fixture-job",
        "source_head": "a" * 40, "source_tree": "b" * 40, "executable_path": "fixture.exe",
        "executable_sha256": "c" * 64, "lease_id": "fixture-lease", "policy_sha256": policy_hash,
        "runner_receipts": runner_receipt_claims,
        "profiles": [
            {"profile": "routed", "test_identity": "suite_fx_runtime_stress_tests", "test_start_utc": "2026-11-22T00:01:00Z", "test_end_utc": "2026-11-22T00:01:05Z", "ctest_exit_code": 0},
            {"profile": "authored", "test_identity": "suite_fx_runtime_stress_tests", "test_start_utc": "2026-11-22T00:01:08Z", "test_end_utc": "2026-11-22T00:01:13Z", "ctest_exit_code": 0},
            {"profile": "typical", "test_identity": "suite_fx_runtime_stress_tests", "test_start_utc": "2026-11-22T00:01:16Z", "test_end_utc": "2026-11-22T00:01:21Z", "ctest_exit_code": 0},
        ],
    }
    binding_file.write_text(json.dumps(binding_value, indent=2), encoding="utf-8")

    private_file = directory / "private-inventory.json"
    private_value = {
        "format": "VOX_FX_T18_PRIVATE_PROCESS_INVENTORY_V1", "visibility": "owner_private_not_for_public_artifact",
        "fixture_mode": True, "capture_start_utc": "2026-11-22T00:00:00Z", "capture_end_utc": "2026-11-22T00:02:02Z",
        "policy_sha256": policy_hash,
        "canonicalization": "fx-t18-private-sample-canonical-v2-utc-ticks",
        "cadence_contract": "full_host_cim_process_snapshot_and_sha256_each_executable_le_1000ms_every_1000ms",
        "correlation_max_skew_seconds": 2.0,
        "correlation_identity_fields": ["event", "pid", "process_name", "start_time_utc", "identity_digest", "executable_path", "executable_sha256", "parent_pid", "service_identity_sha256"],
        "samples": samples, "process_events": wmi_events,
    }
    private_file.write_text(json.dumps(private_value, indent=2), encoding="utf-8")

    witness = valid_witness()
    witness.update({
        "identity_policy": {**witness["identity_policy"], "policy_sha256": policy_hash},
        "identity_policy_path": str(policy_file), "identity_policy_sha256": policy_hash,
        "private_inventory_path": str(private_file), "private_inventory_sha256": hashlib.sha256(private_file.read_bytes()).hexdigest(),
        "samples": public_samples, "process_events": public_events,
        "event_trace": {"observation_provider": "Win32_ProcessStartTrace/Win32_ProcessStopTrace",
                        "loss_provider": "Microsoft-Windows-Kernel-Process/ETW", "available": True,
                        "coverage_complete": True, "lost_events": 0, "buffer_loss_events": 0,
                        "events_received": len(etw_events), "session_name": "fixture-etw-session",
                        "loss_accounting": "etw_provider_counters", "coverage_start_utc": "2026-11-21T23:59:00Z",
                        "coverage_end_utc": "2026-11-22T00:03:00Z", "loss_receipt_path": str(loss_file),
                        "loss_receipt_sha256": hashlib.sha256(loss_file.read_bytes()).hexdigest(),
                        "collector_artifact_path": str(collector_file), "collector_artifact_sha256": collector_hash,
                        "collector_artifact_format": "ETL", "collector_parse_method": "owner_independent_etw_parser_v1",
                        "collector_parse_receipt_path": str(parse_file), "collector_parse_receipt_sha256": parse_hash,
                        "correlation_max_skew_seconds": 2.0,
                        "correlation_identity_fields": ["event", "pid", "process_name", "start_time_utc", "identity_digest", "executable_path", "executable_sha256", "parent_pid", "service_identity_sha256"]},
        "runner_lease": {**witness["runner_lease"], "verification_receipt_path": str(lease_file),
                         "verification_receipt_sha256": hashlib.sha256(lease_file.read_bytes()).hexdigest(),
                         "host": "fixture", "owner": "fixture"},
        "runner_service_identities": policy["runner_service_identity_sha256"],
        "execution_binding": {**witness["execution_binding"], "binding_receipt_path": str(binding_file),
                              "binding_receipt_sha256": hashlib.sha256(binding_file.read_bytes()).hexdigest(),
                              "policy_sha256": policy_hash, "runner_receipts": runner_receipt_claims},
        "attestation_proofs": {}, "evidence_blocked": False,
    })
    signature_paths = {}
    for name, data_file in (("policy", policy_file), ("lease", lease_file), ("event_loss", loss_file), ("execution_binding", binding_file)):
        signature = directory / f"{name}.p7s"
        signature.write_bytes(b"synthetic detached CMS; not a production signature")
        signature_paths[name] = signature
        witness["attestation_proofs"][name] = {
            "verified": True, "data_sha256": hashlib.sha256(data_file.read_bytes()).hexdigest(),
            "signature_sha256": hashlib.sha256(signature.read_bytes()).hexdigest(),
            "signer_thumbprint": "A" * 40, "verification_method": "detached_cms_owner_certificate_chain_and_thumbprint",
            "data_path": str(data_file), "signature_path": str(signature),
        }
    witness_path = directory / "witness.json"
    witness_path.write_text(json.dumps(witness, indent=2), encoding="utf-8")
    manifest = directory / "host-manifest.json"
    files = [policy_file, lease_file, loss_file, binding_file, collector_file, parse_file, private_file,
             witness_path, *runner_receipt_files, *signature_paths.values()]
    manifest.write_text(json.dumps({"format": "VOX_FX_T18_HOST_ARTIFACTS_V2",
        "files": [{"path": str(item), "sha256": hashlib.sha256(item.read_bytes()).hexdigest()} for item in files]}, indent=2), encoding="utf-8")
    return witness_path, manifest


def etw_event(*, event: str, pid: int, process_name: str, executable_hash: str,
              start_time: str, timestamp: str, approved: bool = True,
              service_hash: str = "1" * 64, executable_path: str | None = None,
              parent_pid: int = 4, lineage: list[dict] | None = None) -> dict:
    if lineage is None:
        node = {"pid": pid, "process_name": process_name, "executable_path": executable_path or f"C:\\fixture\\{process_name}",
                "executable_sha256": executable_hash, "parent_pid": parent_pid,
                "start_time_utc": start_time, "service_identity_sha256": [service_hash]}
        node["identity_digest"] = EVALUATOR._identity_digest(node)
        lineage = [node]
    value = {
        "event": event, "timestamp_utc": timestamp, "pid": pid, "process_name": process_name,
        "executable_path": executable_path or f"C:\\fixture\\{process_name}", "executable_sha256": executable_hash,
        "parent_pid": parent_pid, "start_time_utc": start_time,
        "service_identity_sha256": [service_hash], "service_ancestry_verified": True,
        "lineage_verification": "runner_service_ancestry_v1", "service_ancestry": lineage,
        "identity_known": True, "approved_by_policy": approved,
        "event_process_name": process_name,
    }
    value["identity_digest"] = EVALUATOR._identity_digest(value)
    return value



def approved_fixture_event(base: datetime, event: str, timestamp: str) -> dict:
    value = _fixture_process(base)
    value.update({"event": event, "timestamp_utc": timestamp,
                  "event_process_name": value["process_name"], "decision_reason": None})
    return value


def child_lineage_fixture(base: datetime) -> tuple[dict, dict, dict]:
    root = _fixture_process(base)
    child_start = (base - timedelta(seconds=30)).isoformat(timespec="microseconds").replace("+00:00", "Z")
    child_node = {
        "pid": 2000, "process_name": "ctest.exe", "parent_pid": root["pid"],
        "start_time_utc": child_start, "executable_path": "C:\\fixture\\ctest.exe",
        "executable_sha256": "d" * 64, "service_identity_sha256": [],
    }
    child_node["identity_digest"] = EVALUATOR._identity_digest(child_node)
    child = {
        **child_node, "service_identity_sha256": ["1" * 64],
        "service_ancestry_verified": True, "lineage_verification": "runner_service_ancestry_v1",
        "service_ancestry": [child_node, root["service_ancestry"][0]],
        "identity_known": True, "approved_by_policy": True, "decision_reason": None,
        "parent_identity": root["service_ancestry"][0],
    }
    child["identity_digest"] = EVALUATOR._identity_digest(child)
    policy = {
        "format": "VOX_FX_T18_IDENTITY_POLICY_V1", "policy_id": "lineage-policy",
        "baseline_id": "lineage-baseline", "owner_approved": True,
        "runner_service_identity_sha256": ["1" * 64, "2" * 64, "3" * 64, "4" * 64],
        "approved_processes": [
            {"pid": 1000, "start_time_utc": root["start_time_utc"], "process_name": root["process_name"],
             "role": "runner", "lineage_mode": "runner_service_ancestry", "allow_dynamic": False,
             "executable_path": root["executable_path"], "executable_sha256": root["executable_sha256"],
             "parent_pid": root["parent_pid"], "service_identity_sha256": "1" * 64},
            {"process_name": "ctest.exe", "role": "authorized_child", "lineage_mode": "runner_service_ancestry",
             "allow_dynamic": True, "executable_path": child["executable_path"],
             "executable_sha256": child["executable_sha256"], "parent_pid": root["pid"],
             "parent_identity": {"process_name": root["process_name"], "executable_path": root["executable_path"],
                                 "executable_sha256": root["executable_sha256"]},
             "service_identity_sha256": "1" * 64},
        ],
        "denied_process_name_regex": ["^(devenv|msbuild|cmake|ninja|python|cubase|reaper)$"],
    }
    return child, root, policy


def _write_execution_receipt(directory: Path, profile: str, mode: str,
                             override: dict | None = None) -> Path:
    base = datetime(2026, 11, 22, tzinfo=timezone.utc)
    starts = {"routed": 60, "authored": 68, "typical": 76}
    start = base + timedelta(seconds=starts[profile])
    end = start + timedelta(seconds=5)
    executable = directory / "fixture.exe"
    executable.write_bytes(b"fixture executable")
    outcome_path = directory / f"{profile}.outcome.json"
    junit_path = directory / f"{profile}.junit.xml"
    ctest_raw_path = directory / f"{profile}.ctest.txt"
    cpu_claim_mixed = mode == "cpu_claim_mixed" and profile == "routed"
    cpu_claim_other_test = mode == "cpu_claim_other_test" and profile == "routed"
    cpu_claim_selected = mode == "cpu_claim_selected_mismatch" and profile == "routed"
    cpu_reason = (mode == "coarse" or (mode == "functional" and profile != "routed")
                  or cpu_claim_selected or cpu_claim_other_test)
    red_unrelated = mode in {"functional", "nonzero_unrelated", "junit_failure", "cpu_claim_unrelated"} and profile == "routed"
    override_status = (override or {}).get(profile, {}).get("execution_status")
    raw_failed = cpu_reason or red_unrelated or cpu_claim_mixed or (
        override_status in {"timeout_failure", "skip_failure", "crash_failure"})
    raw_status = "Timeout" if override_status == "timeout_failure" else (
        "Failed" if raw_failed else "Passed")
    # Mirrors the owner-authorized Windows #78 capture: CTest -VV prefixes
    # the executable's terminal marker once, echoes it once on failure, then
    # emits the canonical failed-test listing and stderr wrapper; JUnit reports
    # message="Failed" while retaining the marker in system-out.
    cpu_native_failure = cpu_reason or cpu_claim_mixed
    if cpu_native_failure:
        raw_text = (
            "Test project fixture\n"
            "78: " + EVALUATOR.AUDIT.CPU_REASON_MARKER + "\n"
            "FX JUCE runtime stress: 1 failing cases\n"
            "1/1 Test #78: suite_fx_runtime_stress_tests ....***" + raw_status + " 1.94 sec\n"
            + EVALUATOR.AUDIT.CPU_REASON_MARKER + "\n"
            "The following tests FAILED:\n"
            "     78 - suite_fx_runtime_stress_tests (Failed)            suite\n"
            "ctest.exe : Errors while running CTest\n"
        )
    else:
        raw_text = (
            "Test project fixture\n1/1 Test #1: suite_fx_runtime_stress_tests ... "
            + raw_status + "\n"
        )
    if cpu_claim_mixed:
        raw_text += "FX_T18_NATIVE_FAILURE_REASON: unexpected DSP regression\n"
    if cpu_claim_other_test:
        raw_text += "     79 - unrelated_stress_test (Failed)            suite\n"
    ctest_raw_path.write_text(raw_text, encoding="utf-8")
    junit_failed = cpu_native_failure or red_unrelated
    junit_system_out = ""
    if cpu_native_failure:
        junit_system_out = EVALUATOR.AUDIT.CPU_REASON_MARKER + "\nFX JUCE runtime stress: 1 failing cases\n"
    if cpu_claim_mixed:
        junit_system_out += "FX_T18_NATIVE_FAILURE_REASON: unexpected DSP regression\n"
    junit_message = "Failed" if cpu_native_failure else "native assertion failed"
    junit_path.write_text(
        f'<testsuite tests="1" failures="{int(junit_failed)}" errors="0" skipped="0">'
        '<testcase name="suite_fx_runtime_stress_tests">'
        + (f'<failure message="{junit_message}" />' if junit_failed else '')
        + (f'<system-out>{junit_system_out}</system-out>' if junit_system_out else '')
        + '</testcase></testsuite>', encoding="utf-8")
    outcome = {
        "format": "VOX_FX_T18_POST_TEST_OUTCOME_V1", "scenario": profile,
        "test_identity": "suite_fx_runtime_stress_tests",
        "executable_path": str(executable), "executable_sha256": "c" * 64,
        "test_start_utc": start.isoformat().replace("+00:00", "Z"),
        "test_end_utc": end.isoformat().replace("+00:00", "Z"),
        "ctest_exit_code": 0, "native_exit_code": 0,
        "ctest_outcome": "diagnostic_pass", "execution_complete": True,
        "timed_out": False, "crashed": False, "skipped": False,
        "junit_path": str(junit_path), "junit_tests": 1,
        "junit_failures": int(junit_failed), "junit_errors": 0, "junit_skipped": 0,
        "junit_timeout": False, "cpu_only_failure": False,
        "cpu_only_failure_reason": "NONE",
    }
    if mode == "coarse" or (mode == "functional" and profile != "routed"):
        outcome.update({"ctest_exit_code": 8, "native_exit_code": 8,
                        "ctest_outcome": "test_failure", "cpu_only_failure": True,
                        "cpu_only_failure_reason": "CPU_MEASUREMENT_INSUFFICIENT"})
    if mode == "functional" and profile == "routed":
        outcome.update({"ctest_exit_code": 8, "native_exit_code": 8,
                        "ctest_outcome": "test_failure"})
    if mode in {"nonzero_unrelated", "cpu_claim_unrelated"} and profile == "routed":
        outcome.update({"ctest_exit_code": 8, "native_exit_code": 8,
                        "ctest_outcome": "test_failure"})
    if mode in {"cpu_claim_unrelated", "cpu_claim_mixed", "cpu_claim_other_test", "cpu_claim_selected_mismatch"} and profile == "routed":
        outcome.update({"cpu_only_failure": True,
                        "cpu_only_failure_reason": "CPU_MEASUREMENT_INSUFFICIENT"})
    if mode == "junit_failure" and profile == "routed":
        outcome.update({"junit_failures": 1})
    if mode == "mismatched_outcome" and profile == "routed":
        outcome["scenario"] = "wrong-profile"
    if override and profile in override:
        custom = override[profile]
        status = custom.get("execution_status")
        if status == "timeout_failure":
            outcome.update({"ctest_exit_code": 8, "native_exit_code": 8,
                            "ctest_outcome": status, "execution_complete": False,
                            "timed_out": True})
        elif status == "skip_failure":
            outcome.update({"ctest_exit_code": 8, "native_exit_code": 8,
                            "ctest_outcome": status, "execution_complete": False,
                            "skipped": True})
        elif status == "crash_failure":
            outcome.update({"ctest_exit_code": 1, "native_exit_code": 1,
                            "ctest_outcome": status, "execution_complete": False,
                            "crashed": True})
        for key, value in custom.items():
            if key in outcome:
                outcome[key] = value
    if mode == "missing_outcome" and profile == "routed":
        outcome_path.unlink(missing_ok=True)
    else:
        outcome_path.write_text(json.dumps(outcome, indent=2), encoding="utf-8")
    receipt = {
        "format": "VOX_FX_T18_RUNNER_RECEIPT_V1", "scenario": profile,
        "test_identity": "suite_fx_runtime_stress_tests",
        "test_start_utc": outcome["test_start_utc"], "test_end_utc": "post-test-outcome",
        "machine": "fixture", "host_lease_id": "fixture-lease", "runner_job_id": "fixture-job",
        "source_head": "a" * 40, "source_tree": "b" * 40,
        "host_policy_sha256": "a" * 64, "post_test_outcome_path": str(outcome_path),
        "ctest_raw_path": str(ctest_raw_path), "ctest_inventory_path": str(junit_path),
        "ctest_junit_path": str(junit_path),
        "ctest_selected_executable": str(executable),
        "executable_path": str(executable), "executable_sha256": "c" * 64,
    }
    if mode == "selected_executable_mismatch" and profile == "routed":
        receipt["ctest_selected_executable"] = str(directory / "other.exe")
    receipt_path = directory / f"{profile}.json"
    receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    return receipt_path


def write_case_files(directory: Path, mode: str, witness: dict | None = None,
                     receipts: dict[str, dict] | None = None) -> tuple[dict[str, Path], dict[str, Path], Path]:
    directory.mkdir(parents=True, exist_ok=True)
    base = AUDIT_FIXTURES.report_text()
    reports: dict[str, Path] = {}
    receipt_paths: dict[str, Path] = {}
    for profile in EVALUATOR.PROFILES:
        text = profile_report(base, profile)
        if mode in {"coarse", "functional"} or (mode in {"cpu_claim_selected_mismatch", "cpu_claim_other_test"} and profile == "routed"):
            text = quantized(text)
        if mode in {"functional", "functional_truncated"} and profile == "routed":
            text = mutate(text, "r44100_f64_v1_poff", finite_output="0",
                          functional_pass="0", reasons="FINITE_OUTPUT")
        if mode == "reason_mismatch" and profile == "routed":
            text = mutate(text, "r44100_f64_v1_poff", finite_output="0",
                          functional_pass="0", reasons="NONE")
        if mode == "non8" and profile == "routed":
            text = mutate(text, "r44100_f64_v1_poff", voice8_grain_invariant="0",
                          functional_pass="0", reasons="VOICE8_GRAIN_INVARIANT")
        if mode == "missing_functional" and profile == "routed":
            text = "\n".join(line for line in text.splitlines()
                                if not (line.startswith("functional\t")
                                        and line.split("\t")[1] == "r44100_f64_v1_poff")) + "\n"
        if mode in {"missing_rows", "functional_truncated"} and profile == "routed":
            text = "\n".join(line for line in text.splitlines()
                                if not (line.startswith("block\t")
                                        and line.split("\t")[1] == "r44100_f64_v1_poff"
                                        and line.split("\t")[2] == "159")) + "\n"
        if mode == "failed_iat":
            text = text.replace("meta\tlock_positive_control_passed\t1",
                                "meta\tlock_positive_control_passed\t0", 1)
        if mode == "observed_lock" and profile == "routed":
            lines = []
            for line in text.splitlines():
                fields = line.split("\t")
                if fields[0] == "case" and fields[1] == "r44100_f64_v1_poff":
                    values = dict(zip(AUDIT.CASE_FIELDS, fields[1:]))
                    values["lock_entries"] = "1"
                    line = "case\t" + "\t".join(values[field] for field in AUDIT.CASE_FIELDS)
                lines.append(line)
            text = "\n".join(lines) + "\n"
        if mode == "wall_then_crash" and profile == "routed":
            lines = []; miss_line = None
            for line in text.splitlines():
                fields = line.split("\t")
                if fields[0] == "case" and fields[1] == "r44100_f64_v1_poff":
                    values = dict(zip(AUDIT.CASE_FIELDS, fields[1:]))
                    values["wall_misses"] = "1"
                    line = "case\t" + "\t".join(values[field] for field in AUDIT.CASE_FIELDS)
                elif fields[0] == "block" and fields[1] == "r44100_f64_v1_poff" and fields[2] == "0":
                    fields[3] = f"{float(fields[5]) + 1.0:.17f}"; fields[6] = "1"
                    miss_line = "miss\t" + "\t".join(fields[1:]); line = "\t".join(fields)
                elif fields[0] == "block" and fields[1] == "r44100_f64_v1_poff" and fields[2] == "159":
                    continue
                lines.append(line)
            if miss_line: lines.append(miss_line)
            text = "\n".join(lines) + "\n"
        report = directory / f"{profile}.tsv"; report.write_text(text, encoding="utf-8")
        reports[profile] = report
        receipt_paths[profile] = _write_execution_receipt(directory, profile, mode, receipts)
    witness_path = directory / "witness.json"
    witness_path.write_text(json.dumps(witness or valid_witness(), indent=2), encoding="utf-8")
    return reports, receipt_paths, witness_path


def evaluate(directory: Path, mode: str, **kwargs):
    reports, receipts, witness = write_case_files(directory, mode, **kwargs)
    return EVALUATOR.evaluate_reports(
        reports, receipts=receipts, witness=witness,
        allow_unverified_provenance=True)


def assert_status(result: dict, status: str, name: str) -> None:
    if result["primary_status"] != status:
        raise AssertionError(f"{name}: expected {status}, got {result['primary_status']}: {result}")


def main() -> int:
    with tempfile.TemporaryDirectory() as raw_directory:
        directory = Path(raw_directory)
        names: list[str] = []

        strict_witness, strict_manifest = strict_witness_fixture(directory / "strict-witness")
        base = datetime(2026, 11, 22, tzinfo=timezone.utc)
        intervals = {
            "routed": {"test_start": base + timedelta(seconds=60),
                       "test_end": base + timedelta(seconds=65)},
            "authored": {"test_start": base + timedelta(seconds=68),
                         "test_end": base + timedelta(seconds=73)},
            "typical": {"test_start": base + timedelta(seconds=76),
                        "test_end": base + timedelta(seconds=81)},
        }
        strict = EVALUATOR.validate_witness(
            strict_witness, intervals, "fixture", "fixture-lease",
            json.loads(strict_witness.read_text())["identity_policy"]["policy_sha256"],
            require_external_attestations=True, allow_fixture=True)
        host_manifest = EVALUATOR._verify_host_manifest(strict_manifest, strict_witness)
        assert strict["verified"] and host_manifest["verified"]
        strict_policy = json.loads((strict_witness.parent / "identity-policy.json").read_text())
        summary_metadata = EVALUATOR.ETL_PARSER._parse_tracerpt_summary(
            "Log Start Time: 2026-11-22T00:00:00Z\n"
            "Log End Time: 2026-11-22T00:02:02Z\n"
            "Logger Name: FX-T18-KernelProcess\n"
            "Events Lost: 0\nBuffers Lost: 0\n")
        assert summary_metadata["coverage_start_utc"] == "2026-11-22T00:00:00.0000000Z"
        assert summary_metadata["coverage_end_utc"] == "2026-11-22T00:02:02.0000000Z"
        assert summary_metadata["session_name"] == "FX-T18-KernelProcess"
        legacy_summary_metadata = EVALUATOR.ETL_PARSER._parse_tracerpt_summary(
            "Log Start Time: 11/22/2026 12:00:00.1234567 AM\n"
            "Log End Time: 11/22/2026 12:02:02.1234567 AM\n"
            "Logger Name: FX-T18-KernelProcess\n"
            "Events Lost: 0\nLog Buffers Lost: 0\n")
        assert legacy_summary_metadata["coverage_start_utc"].endswith(".1234567Z")
        collection_etl = directory / "owner-process.etl"
        collection_etl.write_bytes(b"synthetic retained ETL name binding")
        collection_metadata_path = directory / "collection-metadata.json"
        collection_metadata_path.write_text(json.dumps({
            "format": EVALUATOR.ETL_PARSER.COLLECTION_METADATA_FORMAT,
            "provider": EVALUATOR.ETL_PARSER.PROVIDER,
            "session_name": "FX-T18-KernelProcess",
            "coverage_start_utc": "2026-11-22T00:00:00Z",
            "coverage_end_utc": "2026-11-22T00:02:02Z",
            "etl_path": str(collection_etl),
        }), encoding="utf-8")
        collection_summary = EVALUATOR.ETL_PARSER._parse_tracerpt_summary(
            "Total Events Lost 0\nTotal Buffers Lost 0\n")
        collection_result = EVALUATOR.ETL_PARSER._read_collection_metadata(
            collection_metadata_path, collection_etl, collection_summary)
        assert collection_result["session_name"] == "FX-T18-KernelProcess"
        assert collection_result["coverage_end_utc"] == "2026-11-22T00:02:02.0000000Z"
        names.append("collection_metadata_reconciliation")
        tracerpt_xml = directory / "tracerpt-kernel-process-shape.xml"
        tracerpt_xml.write_text(
            """<Events xmlns=\"http://schemas.microsoft.com/win/2004/08/events/event\">
  <Event><System><Provider Name=\"Microsoft-Windows-Kernel-Process\" Guid=\"{22FB2CD6-0E7B-422B-A0C7-2FAD1FD0E716}\"/><EventID>1</EventID><TimeCreated SystemTime=\"2026-11-22T00:00:01.1234567Z\"/></System><EventData><Data Name=\"ProcessID\">240148</Data><Data Name=\"CreateTime\">2026-11-22T00:00:00.0000000Z</Data><Data Name=\"ParentProcessID\">2620</Data><Data Name=\"ImageName\">\\\\Device\\\\HarddiskVolume2\\\\Windows\\\\System32\\\\cmd.exe</Data></EventData></Event>
  <Event><System><Provider Name=\"Microsoft-Windows-Kernel-Process\" Guid=\"{22FB2CD6-0E7B-422B-A0C7-2FAD1FD0E716}\"/><EventID>2</EventID><TimeCreated SystemTime=\"2026-11-22T00:00:02.1234567Z\"/></System><EventData><Data Name=\"ProcessID\">240148</Data><Data Name=\"CreateTime\">2026-11-22T00:00:00.0000000Z</Data><Data Name=\"ExitTime\">2026-11-22T00:00:02.1234567Z</Data><Data Name=\"ExitCode\">0</Data></EventData></Event>
</Events>""",
            encoding="utf-8")
        tracerpt_events, tracerpt_metadata = EVALUATOR.ETL_PARSER._parse_xml(
            tracerpt_xml,
            "Start Time: 2026-11-22T00:00:00Z\n"
            "End Time: 2026-11-22T00:01:00Z\n"
            "Logger Name: FX-T18-KernelProcess\n"
            "Total Events Lost 0\nTotal Buffers Lost 0\n")
        assert len(tracerpt_events) == 2
        assert tracerpt_events[0]["process_name"] == "cmd.exe"
        assert tracerpt_events[1]["parent_pid"] == 2620
        assert tracerpt_metadata["lost_events"] == 0 and tracerpt_metadata["buffer_loss_events"] == 0
        names.append("tracerpt_kernel_process_start_stop_shape")
        fixture_parse = json.loads((strict_witness.parent / "collector-parse.json").read_text())
        assert "--identity-join" not in fixture_parse["parser_provenance"]["command_argv"]
        assert "-summary" not in fixture_parse["parser_provenance"]["command_argv"]
        names.append("tracerpt_summary_metadata_and_wrapper_argv")
        precise_etw = [
            approved_fixture_event(base, "start", "2026-11-22T00:00:30.1234567Z"),
            approved_fixture_event(base, "stop", "2026-11-22T00:00:31.1234567Z"),
        ]
        precise_wmi = [dict(precise_etw[0]), dict(precise_etw[1])]
        precise_wmi[0]["timestamp_utc"] = "2026-11-21T16:00:30.987654-08:00"
        precise_wmi[1]["timestamp_utc"] = "2026-11-21T16:00:31.987654-08:00"
        precise_witness, _ = strict_witness_fixture(directory / "precision-offset-correlation", precise_etw, precise_wmi)
        precise_result = EVALUATOR.validate_witness(
            precise_witness, intervals, "fixture", "fixture-lease",
            json.loads(precise_witness.read_text())["identity_policy"]["policy_sha256"],
            require_external_attestations=True, allow_fixture=True)
        assert precise_result["verified"]
        names.append("etw_wmi_normalized_precision_and_offset_one_to_one")

        raw_core = [{field: event[field] for field in
                     ("event", "timestamp_utc", "pid", "process_name", "parent_pid", "start_time_utc")}
                    for event in precise_etw]
        joined = EVALUATOR.ETL_PARSER._apply_identity_join(raw_core, precise_etw)
        assert joined[0]["executable_path"] == precise_etw[0]["executable_path"]
        assert EVALUATOR.ETL_PARSER.core_event_digest(raw_core)
        try:
            EVALUATOR.ETL_PARSER._apply_identity_join(raw_core, precise_etw + [dict(precise_etw[1])])
        except ValueError as error:
            assert "one-to-one" in str(error).lower() or "ambiguous" in str(error).lower()
            names.append("independent_etl_identity_join_ambiguous_block")
        else:
            raise AssertionError("ambiguous ETW identity join unexpectedly passed")

        beyond_skew = [dict(precise_wmi[0]), dict(precise_wmi[1])]
        beyond_skew[0]["timestamp_utc"] = "2026-11-21T16:00:32.123458-08:00"
        skew_witness, _ = strict_witness_fixture(directory / "correlation-skew-block", precise_etw, beyond_skew)
        try:
            EVALUATOR.validate_witness(
                skew_witness, intervals, "fixture", "fixture-lease",
                json.loads(skew_witness.read_text())["identity_policy"]["policy_sha256"],
                require_external_attestations=True, allow_fixture=True)
        except ValueError as error:
            assert "correlation" in str(error).lower() or "unmatched" in str(error).lower()
            names.append("etw_wmi_max_skew_block")
        else:
            raise AssertionError("ETW/WMI event beyond the explicit skew bound passed")

        extra_wmi = [dict(precise_wmi[0]), dict(precise_wmi[1]), dict(precise_wmi[1])]
        extra_witness, _ = strict_witness_fixture(directory / "correlation-extra-wmi", precise_etw, extra_wmi)
        try:
            EVALUATOR.validate_witness(
                extra_witness, intervals, "fixture", "fixture-lease",
                json.loads(extra_witness.read_text())["identity_policy"]["policy_sha256"],
                require_external_attestations=True, allow_fixture=True)
        except ValueError as error:
            assert "count" in str(error).lower() or "extra" in str(error).lower()
            names.append("etw_wmi_extra_event_block")
        else:
            raise AssertionError("WMI extra event unexpectedly passed")

        try:
            EVALUATOR._correlate_events(
                [dict(precise_etw[0]), dict(precise_etw[0])],
                [dict(precise_wmi[0]), dict(precise_wmi[1])],
                "ambiguous-fixture")
        except ValueError as error:
            assert "ambiguous" in str(error).lower() or "one-to-one" in str(error).lower()
            names.append("etw_wmi_ambiguous_block")
        else:
            raise AssertionError("ambiguous ETW/WMI matching unexpectedly passed")

        child, root, lineage_policy = child_lineage_fixture(base)
        EVALUATOR._validate_identity_policy(lineage_policy, "authorized-child-lineage")
        assert EVALUATOR._policy_approves_event(lineage_policy, child)
        names.append("authorized_non_service_child_real_runner_chain")
        host_start = "2026-11-22T00:00:20Z"
        host_root = {"pid": 2000, "process_name": "svchost.exe", "parent_pid": 4,
                     "start_time_utc": host_start, "executable_path": "C:\\Windows\\System32\\svchost.exe",
                     "executable_sha256": "6" * 64, "service_identity_sha256": ["7" * 64]}
        host_root["identity_digest"] = EVALUATOR._identity_digest(host_root)
        host_parent = {"pid": 4, "process_name": "services.exe", "parent_pid": 0,
                       "start_time_utc": "2026-11-22T00:00:00Z",
                       "executable_path": "C:\\Windows\\System32\\services.exe",
                       "executable_sha256": "8" * 64, "service_identity_sha256": []}
        host_parent["identity_digest"] = EVALUATOR._identity_digest(host_parent)
        host_event = etw_event(event="start", pid=2000, process_name="svchost.exe", executable_hash="6" * 64,
                               start_time=host_start, timestamp="2026-11-22T00:00:30Z",
                               service_hash="7" * 64, executable_path="C:\\Windows\\System32\\svchost.exe",
                               parent_pid=4, lineage=[host_root, host_parent])
        host_event["lineage_verification"] = "host_baseline_v1"
        assert EVALUATOR._policy_approves_event(strict_policy, host_event)
        none_policy = json.loads(json.dumps(strict_policy))
        none_policy["host_baseline_processes"] = [{
            "identity_scope": "host_baseline", "non_competing": True,
            "process_name": "services.exe", "lineage_mode": "host_baseline_v1",
            "allow_dynamic": True, "executable_path": "C:\\Windows\\System32\\services.exe",
            "executable_sha256": "8" * 64, "parent_pid": 0,
            "service_identity_sha256": "none",
        }]
        none_event = {
            "event": "start", "timestamp_utc": "2026-11-22T00:00:30Z", "pid": 4,
            "process_name": "services.exe", "parent_pid": 0,
            "start_time_utc": "2026-11-22T00:00:00Z",
            "executable_path": "C:\\Windows\\System32\\services.exe",
            "executable_sha256": "8" * 64, "service_identity_sha256": [],
            "service_ancestry_verified": True, "lineage_verification": "host_baseline_v1",
            "service_ancestry": [host_parent], "identity_known": True,
            "approved_by_policy": True,
        }
        none_event["identity_digest"] = EVALUATOR._identity_digest(none_event)
        EVALUATOR._validate_identity_policy(none_policy, "owner-baselined-no-service")
        assert EVALUATOR._policy_approves_event(none_policy, none_event)
        names.append("owner_baselined_non_runner_host_process")
        forged_child = json.loads(json.dumps(child))
        forged_child["service_ancestry"][1]["executable_path"] = "C:\\fixture\\forged-services.exe"
        try:
            assert not EVALUATOR._policy_approves_event(lineage_policy, forged_child)
        except ValueError:
            pass
        names.append("forged_runner_ancestry_rejected")

        private_negative_witness, _ = strict_witness_fixture(directory / "private-unauthorized-raw")
        private_negative_value = json.loads(private_negative_witness.read_text())
        private_negative_path = Path(private_negative_value["private_inventory_path"])
        private_negative_inventory = json.loads(private_negative_path.read_text())
        private_negative_inventory["samples"][0]["processes"][0]["approved_by_policy"] = False
        private_negative_path.write_text(json.dumps(private_negative_inventory, indent=2), encoding="utf-8")
        private_negative_value["private_inventory_sha256"] = hashlib.sha256(private_negative_path.read_bytes()).hexdigest()
        private_negative_path_out = private_negative_witness.parent / "private-negative-witness.json"
        private_negative_path_out.write_text(json.dumps(private_negative_value, indent=2), encoding="utf-8")
        try:
            EVALUATOR.validate_witness(
                private_negative_path_out, intervals, "fixture", "fixture-lease",
                private_negative_value["identity_policy"]["policy_sha256"],
                require_external_attestations=False, allow_fixture=True)
        except ValueError as error:
            assert "private" in str(error).lower() or "approved" in str(error).lower()
            names.append("unauthorized_private_raw_vs_approved_public")
        else:
            raise AssertionError("unauthorized private/raw inventory unexpectedly reconciled")

        tampered_etl_witness, _ = strict_witness_fixture(
            directory / "tampered-etl-regenerated-claims", precise_etw, precise_wmi)
        tampered_value = json.loads(tampered_etl_witness.read_text())
        collector_path = Path(tampered_value["event_trace"]["collector_artifact_path"])
        collector_value = json.loads(collector_path.read_text())
        for tampered_event in collector_value["process_events"]:
            tampered_event["executable_path"] = "C:\\fixture\\forged.exe"
            tampered_event["executable_sha256"] = "e" * 64
            tampered_event["service_ancestry"][0]["executable_path"] = tampered_event["executable_path"]
            tampered_event["service_ancestry"][0]["executable_sha256"] = tampered_event["executable_sha256"]
            tampered_event["service_ancestry"][0]["identity_digest"] = EVALUATOR._identity_digest(tampered_event["service_ancestry"][0])
            tampered_event["identity_digest"] = EVALUATOR._identity_digest(tampered_event)
        collector_path.write_text(json.dumps(collector_value, indent=2), encoding="utf-8")
        regenerated_parse = EVALUATOR.ETL_PARSER.make_receipt(
            collector_path, "2026-11-21T23:59:00Z", "2026-11-22T00:03:00Z",
            "fixture-etw-session", fixture=True)
        parse_path = Path(tampered_value["event_trace"]["collector_parse_receipt_path"])
        parse_path.write_text(json.dumps(regenerated_parse, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        loss_path = Path(tampered_value["event_trace"]["loss_receipt_path"])
        loss_value = json.loads(loss_path.read_text())
        loss_value["collector_artifact_sha256"] = hashlib.sha256(collector_path.read_bytes()).hexdigest()
        loss_value["collector_parse_receipt_sha256"] = hashlib.sha256(parse_path.read_bytes()).hexdigest()
        loss_value["process_events"] = regenerated_parse["process_events"]
        loss_value["parser_provenance"] = regenerated_parse["parser_provenance"]
        loss_path.write_text(json.dumps(loss_value, indent=2), encoding="utf-8")
        tampered_value["event_trace"].update({
            "collector_artifact_sha256": loss_value["collector_artifact_sha256"],
            "collector_parse_receipt_sha256": loss_value["collector_parse_receipt_sha256"],
            "loss_receipt_sha256": hashlib.sha256(loss_path.read_bytes()).hexdigest(),
        })
        tampered_value["attestation_proofs"]["event_loss"]["data_sha256"] = hashlib.sha256(loss_path.read_bytes()).hexdigest()
        tampered_witness_path = tampered_etl_witness.parent / "tampered-regenerated-witness.json"
        tampered_witness_path.write_text(json.dumps(tampered_value, indent=2), encoding="utf-8")
        try:
            EVALUATOR.validate_witness(
                tampered_witness_path, intervals, "fixture", "fixture-lease",
                tampered_value["identity_policy"]["policy_sha256"],
                require_external_attestations=True, allow_fixture=True)
        except ValueError as error:
            assert ("policy" in str(error).lower() or "denied" in str(error).lower()
                    or "mismatch" in str(error).lower() or "identity" in str(error).lower())
            names.append("tampered_etl_regenerated_json_claims_blocked")
        else:
            raise AssertionError("tampered ETL with regenerated internally consistent claims unexpectedly passed")

        observation_value = json.loads(strict_witness.read_text())
        observation_value.update({
            "format": "VOX_FX_T18_HOST_OBSERVATION_V1", "schema_version": 1,
            "mode": "observe", "phase": "observe", "finalization_required": True,
            "evidence_blocked": True,
            "diagnostic": {"reason_codes": ["FINALIZATION_REQUIRED"]},
        })
        observation_path = strict_witness.parent / "observation.json"
        observation_path.write_text(json.dumps(observation_value, indent=2), encoding="utf-8")
        observation_hash = hashlib.sha256(observation_path.read_bytes()).hexdigest()
        for proof_name in ("event-loss", "execution-binding"):
            proof_path = strict_witness.parent / f"{proof_name}.json"
            proof_value = json.loads(proof_path.read_text())
            proof_value["observation_sha256"] = observation_hash
            proof_path.write_text(json.dumps(proof_value, indent=2), encoding="utf-8")
        finalized_path = strict_witness.parent / "finalized-witness.json"
        FINALIZER.finalize(
            observation_path, finalized_path,
            strict_witness.parent / "identity-policy.json", strict_witness.parent / "policy.p7s",
            strict_witness.parent / "lease.json", strict_witness.parent / "lease.p7s",
            strict_witness.parent / "event-loss.json", strict_witness.parent / "event_loss.p7s",
            strict_witness.parent / "execution-binding.json", strict_witness.parent / "execution_binding.p7s",
            "A" * 40, allow_fixture=True)
        finalized_value = json.loads(finalized_path.read_text())
        assert finalized_value["format"] == "VOX_FX_T18_HOST_WITNESS_V1"
        assert finalized_value["qualification_prohibited"] is True and finalized_value["evidence_blocked"] is False
        names.append("two_phase_observation_finalization")
        tampered_observation = json.loads(json.dumps(observation_value))
        tampered_observation["private_inventory_sha256"] = "f" * 64
        tampered_path = strict_witness.parent / "tampered-observation.json"
        tampered_path.write_text(json.dumps(tampered_observation, indent=2), encoding="utf-8")
        try:
            FINALIZER.finalize(
                tampered_path, strict_witness.parent / "tampered-final.json",
                strict_witness.parent / "identity-policy.json", strict_witness.parent / "policy.p7s",
                strict_witness.parent / "lease.json", strict_witness.parent / "lease.p7s",
                strict_witness.parent / "event-loss.json", strict_witness.parent / "event_loss.p7s",
                strict_witness.parent / "execution-binding.json", strict_witness.parent / "execution_binding.p7s",
                "A" * 40, allow_fixture=True)
        except ValueError:
            names.append("tampered_observation_rejected")
        else:
            raise AssertionError("tampered observation unexpectedly finalized")
        fake_value = json.loads(strict_witness.read_text())
        try:
            EVALUATOR._verify_external_witness_attestations(
                strict_witness, fake_value, strict["capture_start"], strict["capture_end"],
                fake_value["identity_policy"], fake_value["runner_lease"],
                fake_value["event_trace"], fake_value["execution_binding"],
                "fixture", "fixture-lease", fake_value["identity_policy"]["policy_sha256"],
                "a" * 40, "b" * 40, "c" * 64, allow_fixture=False)
        except ValueError:
            pass
        else:
            raise AssertionError("synthetic detached signatures unexpectedly passed production CMS verification")
        names.extend(["independent_host_attestations", "fake_attestation_cannot_production_pass"])

        brief_competitor = etw_event(
            event="start", pid=4242, process_name="other-runner.exe", executable_hash="6" * 64,
            start_time="2026-11-22T00:00:29Z", timestamp="2026-11-22T00:00:30Z", approved=False)
        competitor_witness, _ = strict_witness_fixture(
            directory / "etw-competitor-absent-wmi", [brief_competitor], [])
        try:
            EVALUATOR.validate_witness(
                competitor_witness, intervals, "fixture", "fixture-lease",
                json.loads(competitor_witness.read_text())["identity_policy"]["policy_sha256"],
                require_external_attestations=True, allow_fixture=True)
        except ValueError:
            names.append("etw_competitor_absent_from_wmi")
        else:
            raise AssertionError("ETW-only competitor unexpectedly passed")

        mixed_start = etw_event(
            event="start", pid=5252, process_name="Runner.Listener.exe", executable_hash="7" * 64,
            start_time="2026-11-22T00:00:39Z", timestamp="2026-11-22T00:00:40Z")
        mixed_stop = etw_event(
            event="stop", pid=5252, process_name="other-runner.exe", executable_hash="8" * 64,
            start_time="2026-11-22T00:00:39Z", timestamp="2026-11-22T00:00:41Z")
        mixed_witness, _ = strict_witness_fixture(
            directory / "mixed-name-same-pid", [mixed_start, mixed_stop],
            [dict(mixed_start), dict(mixed_stop)])
        try:
            EVALUATOR.validate_witness(
                mixed_witness, intervals, "fixture", "fixture-lease",
                json.loads(mixed_witness.read_text())["identity_policy"]["policy_sha256"],
                require_external_attestations=True, allow_fixture=True)
        except ValueError:
            names.append("mixed_name_same_pid_event")
        else:
            raise AssertionError("mixed-name same-PID ETW lifecycle unexpectedly passed")

        pin_witness, _ = strict_witness_fixture(directory / "independent-pin")
        try:
            EVALUATOR.validate_witness(
                pin_witness, intervals, "fixture", "fixture-lease",
                json.loads(pin_witness.read_text())["identity_policy"]["policy_sha256"],
                require_external_attestations=True, allow_fixture=True,
                owner_certificate_thumbprint="B" * 40)
        except ValueError:
            names.append("independent_owner_pin_mismatch")
        else:
            raise AssertionError("wrong independent owner pin unexpectedly passed")

        strict_policy = json.loads((strict_witness.parent / "identity-policy.json").read_text())
        for mutation_name, mutation in (
            ("strict_policy_wildcard_path", lambda value: value["approved_processes"][0].update({"executable_path": "*"})),
            ("strict_policy_missing_service_identity", lambda value: value["approved_processes"][0].pop("service_identity_sha256")),
            ("strict_policy_missing_dynamic_authorization", lambda value: (value["approved_processes"][0].pop("pid"), value["approved_processes"][0].pop("start_time_utc"), value["approved_processes"][0].update({"allow_dynamic": False}))),
            ("strict_policy_deny_allow_conflict", lambda value: value["denied_process_name_regex"].append("^Runner\\.Listener\\.exe$")),
        ):
            mutated_policy = json.loads(json.dumps(strict_policy))
            mutation(mutated_policy)
            try:
                EVALUATOR._validate_identity_policy(mutated_policy, mutation_name)
            except ValueError:
                names.append(mutation_name)
            else:
                raise AssertionError(f"{mutation_name} unexpectedly passed")
        wrong_event = etw_event(
            event="start", pid=6262, process_name="Runner.Listener.exe", executable_hash="9" * 64,
            start_time="2026-11-22T00:00:49Z", timestamp="2026-11-22T00:00:50Z")
        assert not EVALUATOR._policy_approves_event(strict_policy, wrong_event)
        names.append("strict_policy_wrong_executable")

        reset_samples = capture_sampler_samples(base)
        phase_offsets = {"pre": 0, "during": 0, "post": 0}
        for sample in reset_samples:
            sample["monotonic_elapsed_ms"] = phase_offsets[sample["phase"]] * 1000
            phase_offsets[sample["phase"]] += 1
        reset_path = directory / "global-monotonic-reset.json"
        reset_path.write_text(json.dumps(valid_witness(samples=reset_samples), indent=2), encoding="utf-8")
        try:
            EVALUATOR.validate_witness(reset_path, intervals, "fixture", "fixture-lease", "a" * 64, allow_fixture=True)
        except ValueError:
            names.append("capture_sampler_global_monotonic_clock")
        else:
            raise AssertionError("phase-reset monotonic sampler unexpectedly passed")

        result = evaluate(directory / "coarse-functional", "functional")
        assert_status(result, "WALL_GATE_FAIL", "coarse_cpu_plus_functional")
        assert result["cpu_diagnostic_blocked"] and result["functional_failure"]
        names.append("coarse_cpu_plus_functional")

        result = evaluate(directory / "coarse-only", "coarse")
        assert_status(result, "WALL_GATE_BLOCKED", "coarse_cpu_only")
        assert result["cpu_diagnostic_blocked"] and not result["functional_failure"]
        assert result["qualification_prohibited"] and result["mode"] == "fixture"
        assert not result["provenance_verified"] and not result["host_manifest_verified"]
        names.append("all_profiles_unverified_provenance")

        result = evaluate(directory / "reason-mismatch", "reason_mismatch")
        assert_status(result, "WALL_GATE_BLOCKED", "functional_reason_mismatch")
        assert result["evidence_blocked"] and not result["functional_failure"]
        assert result["functional_evidence_invalid"]
        names.append("functional_reason_mismatch")

        result = evaluate(directory / "non8", "non8")
        assert_status(result, "WALL_GATE_BLOCKED", "non8_voice_invariant")
        assert result["evidence_blocked"] and not result["functional_failure"]
        assert result["functional_evidence_invalid"]
        names.append("non8_voice_invariant")

        result = evaluate(directory / "missing-functional", "missing_functional")
        assert_status(result, "WALL_GATE_BLOCKED", "missing_functional_record")
        assert result["evidence_blocked"]
        names.append("missing_functional_record")

        result = evaluate(directory / "missing-rows", "missing_rows")
        assert_status(result, "WALL_GATE_BLOCKED", "missing_rows_or_manifest")
        assert result["evidence_blocked"]
        names.append("missing_rows_or_manifest")

        result = evaluate(directory / "failed-iat", "failed_iat")
        assert_status(result, "WALL_GATE_BLOCKED", "failed_iat_positive_control")
        assert result["instrumentation_blocked"] and result["evidence_blocked"]
        assert not result["safety_failure"]
        names.append("failed_iat_positive_control")

        result = evaluate(directory / "observed-lock", "observed_lock")
        assert_status(result, "WALL_GATE_FAIL", "observed_lock_wait")
        assert result["safety_failure"] and result["performance_failure"]
        names.append("observed_lock_wait")

        for execution_status in ("timeout_failure", "skip_failure"):
            result = evaluate(
                directory / execution_status, "valid",
                receipts={"routed": {"execution_complete": False,
                                      "execution_status": execution_status}},
            )
            assert_status(result, "WALL_GATE_BLOCKED", "timeout_or_skip")
            assert result["execution_failure"] and result["evidence_blocked"]
        names.append("timeout_or_skip")

        result = evaluate(
            directory / "wall-crash", "wall_then_crash",
            receipts={"routed": {"execution_complete": False, "execution_status": "crash_failure",
                                  "crashed": True}},
        )
        assert_status(result, "WALL_GATE_FAIL", "observed_wall_then_crash")
        assert result["wall_failure"] and result["execution_failure"] and result["evidence_blocked"]
        names.append("observed_wall_then_crash")

        result = evaluate(directory / "functional-truncated", "functional_truncated")
        assert_status(result, "WALL_GATE_FAIL", "functional_failure_then_truncation")
        assert result["functional_failure"] and result["evidence_blocked"]
        names.append("functional_failure_then_truncation")

        for mode in ("missing_outcome", "nonzero_unrelated", "junit_failure",
                     "cpu_claim_unrelated", "cpu_claim_mixed", "cpu_claim_other_test",
                     "cpu_claim_selected_mismatch", "mismatched_outcome"):
            result = evaluate(directory / mode, mode)
            assert_status(result, "WALL_GATE_BLOCKED", mode)
            assert result["evidence_blocked"]
            if mode in {"cpu_claim_unrelated", "cpu_claim_mixed", "cpu_claim_other_test",
                        "cpu_claim_selected_mismatch"}:
                assert result["execution_blocked"] and not result["cpu_diagnostic_blocked"]
        names.extend(["absent_native_outcome", "ctest_nonzero_unrelated", "junit_failure_green_tsv",
                      "cpu_claim_unrelated_raw_junit", "cpu_claim_mixed_raw_junit",
                      "cpu_claim_other_test_listing", "cpu_claim_selected_executable_mismatch",
                      "mismatched_native_outcome"])

        short_samples = [
            {"phase": phase, "timestamp_utc": "2026-11-22T00:00:30Z",
             "processes": [], "total_processes": 0, "process_query_error": None}
            for phase in ("pre", "during", "post")
        ]
        result = evaluate(directory / "three-samples", "valid",
                          witness=valid_witness(samples=short_samples))
        assert_status(result, "WALL_GATE_BLOCKED", "three_sample_witness")
        assert result["evidence_blocked"]
        names.append("three_sample_witness")

        result = evaluate(
            directory / "missing-interval", "valid",
            receipts={"routed": {"test_start_utc": "2026-11-22T00:03:00Z",
                                  "test_end_utc": "2026-11-22T00:03:05Z"}},
        )
        assert_status(result, "WALL_GATE_BLOCKED", "witness_missing_ctest_interval")
        assert result["evidence_blocked"]
        names.append("witness_missing_ctest_interval")

        preexisting = valid_witness()
        preexisting["samples"][0]["processes"][0]["approved_by_policy"] = False
        result = evaluate(directory / "preexisting-daw", "valid", witness=preexisting)
        assert_status(result, "WALL_GATE_BLOCKED", "preexisting_daw")
        names.append("preexisting_daw")

        for workload_name in ("other-runner", "manual-build", "daw-workload"):
            unexpected = valid_witness()
            unexpected["samples"][0]["processes"][0].update({
                "process_name": {"other-runner": "Runner.Worker.other.exe",
                                 "manual-build": "msbuild.exe",
                                 "daw-workload": "reaper.exe"}[workload_name],
                "approved_by_policy": False,
            })
            result = evaluate(directory / workload_name, "valid", witness=unexpected)
            assert_status(result, "WALL_GATE_BLOCKED", workload_name)
        names.extend(["other_runner_identity", "manual_build_identity", "daw_workload_identity"])

        forged_policy = valid_witness()
        forged_policy["samples"][0]["processes"][0].update(
            {"pid": 2001, "process_name": "cubase.exe", "approved_by_policy": True})
        result = evaluate(directory / "forged-policy-flag", "valid", witness=forged_policy)
        assert_status(result, "WALL_GATE_BLOCKED", "forged_policy_flag")
        names.append("forged_policy_flag")

        nonlisted = valid_witness()
        nonlisted["process_events"] = [{
            "event": "start", "timestamp_utc": "2026-11-22T00:00:31Z", "pid": 2000,
            "process_name": "ninja.exe", "command_line": "<redacted>",
            "job_identity": "<redacted-or-not-applicable>",
            "service_identity": "<redacted-or-not-applicable>",
            "identity_known": True, "approved_by_policy": False,
        }]
        result = evaluate(directory / "nonlisted-build", "valid", witness=nonlisted)
        assert_status(result, "WALL_GATE_BLOCKED", "nonlisted_build")
        names.append("nonlisted_build")

        forged_lease = valid_witness()
        forged_lease["runner_lease"]["owner_verified"] = False
        result = evaluate(directory / "forged-lease", "valid", witness=forged_lease)
        assert_status(result, "WALL_GATE_BLOCKED", "forged_lease")
        names.append("forged_lease")

        missing_event_coverage = valid_witness()
        missing_event_coverage["event_trace"]["coverage_complete"] = 0
        missing_event_coverage["event_trace"]["lost_events"] = None
        missing_event_coverage["event_trace"]["loss_accounting"] = "unknown"
        result = evaluate(directory / "missing-event-coverage", "valid", witness=missing_event_coverage)
        assert_status(result, "WALL_GATE_BLOCKED", "missing_event_time_coverage")
        names.append("missing_event_time_coverage")

        # A short-lived competitor is a counter in the public receipt; it is
        # never converted to an absent-process/idle-host assumption.
        short_lived = valid_witness()
        short_lived["unrecognized_competitors"] = 1
        result = evaluate(directory / "short-lived-competitor", "valid", witness=short_lived)
        assert_status(result, "WALL_GATE_BLOCKED", "short_lived_competitor")
        assert result["evidence_blocked"]
        names.append("short_lived_competitor")

        pid_reuse = valid_witness()
        pid_reuse["pid_reuse_mismatches"] = 1
        result = evaluate(directory / "pid-reuse", "valid", witness=pid_reuse)
        assert_status(result, "WALL_GATE_BLOCKED", "pid_reuse")
        names.append("dynamic_pid_reuse_mismatch")

        phase_mismatch = valid_witness()
        phase_mismatch["execution_binding"]["profiles"][0]["test_start_utc"] = "2026-11-22T00:00:59Z"
        result = evaluate(directory / "phase-ctest-mismatch", "valid", witness=phase_mismatch)
        assert_status(result, "WALL_GATE_BLOCKED", "phase_ctest_mismatch")
        assert "phase/CTest interval mismatch" in result.get("witness_error", "")
        names.append("phase_ctest_mismatch")

        fixture_guard = evaluate(directory / "fixture-guard", "valid", witness=valid_witness())
        assert_status(fixture_guard, "WALL_GATE_BLOCKED", "fixture_guard")
        assert fixture_guard["qualification_prohibited"] and fixture_guard["mode"] == "fixture"
        assert fixture_guard["primary_status"] != "WALL_GATE_PASS_CPU_UNKNOWN"
        names.append("fixture_mode_qualification_prohibited")

        def expect_witness_failure(witness_path: Path, needle: str, name: str) -> None:
            witness_value = json.loads(witness_path.read_text())
            binding_value = witness_value["execution_binding"]
            try:
                EVALUATOR.validate_witness(
                    witness_path, intervals, "fixture", "fixture-lease",
                    witness_value["identity_policy"]["policy_sha256"],
                    binding_value["source_head"], binding_value["source_tree"],
                    binding_value["executable_sha256"],
                    require_external_attestations=True, allow_fixture=True)
            except ValueError as error:
                if needle not in str(error):
                    raise AssertionError(f"{name}: expected {needle!r}, got {error}")
            else:
                raise AssertionError(f"{name}: forged/missing owner proof unexpectedly passed")

        # Missing proof/signature and failed hash reconciliation are explicit
        # executable negative cases, not assertions about owner trust.
        missing_proof = directory / "missing-owner-proof"
        missing_proof.mkdir()
        missing_value = json.loads(strict_witness.read_text())
        missing_value["fixture_mode"] = False; missing_value["qualification_prohibited"] = False
        missing_value["attestation_proofs"] = {}
        missing_path = missing_proof / "witness.json"
        missing_path.write_text(json.dumps(missing_value), encoding="utf-8")
        expect_witness_failure(missing_path, "signed policy proof is missing", "missing_owner_proof")
        names.append("missing_owner_proof")

        forged_signature_path = strict_witness.parent / "policy.p7s"
        forged_signature_path.write_bytes(b"tampered detached CMS")
        expect_witness_failure(strict_witness, "detached signature hash does not reconcile", "forged_owner_signature")
        names.append("forged_owner_signature")

        unverified_wmi = valid_witness()
        unverified_wmi["fixture_mode"] = False; unverified_wmi["qualification_prohibited"] = False
        unverified_wmi["event_trace"]["loss_provider"] = "Win32_ProcessStartTrace/Win32_ProcessStopTrace"
        wmi_path = directory / "unverified-wmi.json"
        wmi_path.write_text(json.dumps(unverified_wmi), encoding="utf-8")
        try:
            EVALUATOR.validate_witness(wmi_path, intervals, "fixture", "fixture-lease", "a" * 64,
                                       "a" * 40, "b" * 40, "c" * 64, allow_fixture=False)
        except ValueError as error:
            if "ETW event loss" not in str(error):
                raise AssertionError(f"unverified_wmi: unexpected error {error}")
        else:
            raise AssertionError("unverified WMI loss fixture unexpectedly passed")
        names.append("unverified_wmi_loss")

        # No receipt/manifest or root is not an acceptable full-evidence path,
        # even when all three TSV reports are structurally valid.
        reports, _, witness = write_case_files(directory / "missing-manifest", "valid")
        result = EVALUATOR.evaluate_reports(reports, witness=witness)
        assert_status(result, "WALL_GATE_BLOCKED", "missing_manifest_reconciliation")
        assert result["evidence_blocked"]
        names.append("missing_manifest_reconciliation")

        cli_directory = directory / "cli"
        cli_reports, cli_receipts, cli_witness = write_case_files(cli_directory, "coarse")
        command = [sys.executable, str(HERE / "evaluate-fx-t18-wall-gate.py")]
        for profile in EVALUATOR.PROFILES:
            command.extend(["--report", f"{profile}={cli_reports[profile]}"])
            command.extend(["--receipt", f"{profile}={cli_receipts[profile]}"])
        command.extend(["--witness", str(cli_witness), "--allow-unverified-provenance"])
        cli = subprocess.run(command, check=False, capture_output=True, text=True)
        if cli.returncode != 1:
            raise AssertionError(f"CLI fixture unexpectedly returned {cli.returncode}: {cli.stderr}")
        assert_status(json.loads(cli.stdout), "WALL_GATE_BLOCKED", "CLI coarse_cpu_only")

        print("FX-T18 wall-gate executable fixtures: PASS")
        print("fixtures: " + ", ".join(names))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
