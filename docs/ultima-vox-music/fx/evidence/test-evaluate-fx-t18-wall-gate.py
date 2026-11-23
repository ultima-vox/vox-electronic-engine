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


def valid_witness(competitors: list[str] | None = None,
                  samples: list[dict] | None = None,
                  mutate: dict | None = None) -> dict:
    base = datetime(2026, 11, 22, tzinfo=timezone.utc)
    policy = {
        "format": "VOX_FX_T18_IDENTITY_POLICY_V1",
        "policy_id": "fixture-policy",
        "owner_approved": True,
        "policy_sha256": "a" * 64,
        "approved_processes": [{"pid": 1000, "process_name": "Runner.Listener.exe", "role": "runner"}],
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
        samples = []
        for phase, start, count in (("pre", 0, 31), ("during", 31, 61), ("post", 92, 31)):
            for offset in range(count):
                timestamp = base + timedelta(seconds=start + offset)
                samples.append({
                    "phase": phase,
                    "timestamp_utc": timestamp.isoformat().replace("+00:00", "Z"),
                    "processes": [dict(process)], "total_processes": 1,
                    "process_query_error": None,
                })
    witness = {
        "format": "VOX_FX_T18_HOST_WITNESS_V1",
        "schema_version": 1,
        "host_observation_mode": "process_snapshot_and_event_trace",
        "sample_interval_ms": 1000,
        "pre_seconds": 30, "during_seconds": 60, "post_seconds": 30,
        "capture_start_utc": base.isoformat().replace("+00:00", "Z"),
        "capture_end_utc": (base + timedelta(seconds=122)).isoformat().replace("+00:00", "Z"),
        "monitor_gaps": [], "identity_policy": policy,
        "event_trace": {"available": True, "coverage_complete": 1, "lost_events": 0,
                        "loss_accounting": "owner_attested"},
        "runner_lease": {"recognized": True, "owner_verified": True, "owner": "fixture",
                         "host": "fixture", "lease_id": "fixture-lease",
                         "verification_receipt_sha256": "b" * 64, "token": "<redacted>"},
        "runner_service_identities": ["fixture-runner"],
        "samples": samples, "process_events": [],
        "unrecognized_competitors": competitors or [], "unknown_pids": [],
        "evidence_blocked": False,
    }
    if mutate:
        witness.update(mutate)
    return witness


def strict_witness_fixture(directory: Path) -> tuple[Path, Path]:
    directory.mkdir(parents=True, exist_ok=True)
    witness = valid_witness()
    policy_file = directory / "identity-policy.json"
    policy_data = dict(witness["identity_policy"])
    policy_data.pop("policy_sha256", None)
    policy_file.write_text(json.dumps(policy_data, indent=2), encoding="utf-8")
    policy_hash = hashlib.sha256(policy_file.read_bytes()).hexdigest()
    witness["identity_policy"]["policy_sha256"] = policy_hash
    witness["identity_policy_path"] = str(policy_file)
    witness["identity_policy_sha256"] = policy_hash

    lease_file = directory / "lease.json"
    lease_file.write_text(json.dumps({
        "format": "VOX_FX_T18_HOST_LEASE_RECEIPT_V1", "verified": True,
        "host": "fixture", "lease_id": "fixture-lease", "owner": "fixture",
        "policy_sha256": policy_hash,
        "valid_from_utc": "2026-11-21T23:59:00Z",
        "valid_until_utc": "2026-11-22T00:03:00Z",
        "verification_method": "owner_attested_file",
    }, indent=2), encoding="utf-8")
    witness["runner_lease"]["verification_receipt_path"] = str(lease_file)
    witness["runner_lease"]["verification_receipt_sha256"] = hashlib.sha256(lease_file.read_bytes()).hexdigest()

    loss_file = directory / "event-loss.json"
    loss_file.write_text(json.dumps({
        "format": "VOX_FX_T18_EVENT_LOSS_RECEIPT_V1",
        "provider": "Win32_ProcessStartTrace/Win32_ProcessStopTrace",
        "coverage_complete": True, "lost_events": 0, "loss_accounting": "owner_attested",
        "coverage_start_utc": "2026-11-21T23:59:00Z",
        "coverage_end_utc": "2026-11-22T00:03:00Z",
    }, indent=2), encoding="utf-8")
    witness["event_trace"]["loss_receipt_path"] = str(loss_file)
    witness["event_trace"]["loss_receipt_sha256"] = hashlib.sha256(loss_file.read_bytes()).hexdigest()

    witness_path = directory / "witness.json"
    witness_path.write_text(json.dumps(witness, indent=2), encoding="utf-8")
    manifest = directory / "host-manifest.json"
    files = [policy_file, lease_file, loss_file, witness_path]
    manifest.write_text(json.dumps({
        "format": "VOX_FX_T18_HOST_ARTIFACTS_V1",
        "files": [{"path": str(item), "sha256": hashlib.sha256(item.read_bytes()).hexdigest()}
                  for item in files],
    }, indent=2), encoding="utf-8")
    return witness_path, manifest


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
    cpu_reason = mode == "coarse" or (mode == "functional" and profile != "routed")
    red_unrelated = mode in {"functional", "nonzero_unrelated", "junit_failure", "cpu_claim_unrelated"} and profile == "routed"
    override_status = (override or {}).get(profile, {}).get("execution_status")
    raw_failed = cpu_reason or red_unrelated or cpu_claim_mixed or (
        override_status in {"timeout_failure", "skip_failure", "crash_failure"})
    raw_status = "Timeout" if override_status == "timeout_failure" else (
        "Failed" if raw_failed else "Passed")
    raw_text = (
        "Test project fixture\n1/1 Test #1: suite_fx_runtime_stress_tests ... "
        + raw_status + "\n"
    )
    if cpu_reason or cpu_claim_mixed:
        raw_text += EVALUATOR.AUDIT.CPU_REASON_MARKER + "\n"
    if cpu_claim_mixed:
        raw_text += "Assertion failed: PEAK_RANGE\n"
    ctest_raw_path.write_text(raw_text, encoding="utf-8")
    junit_failed = cpu_reason or red_unrelated or cpu_claim_mixed
    junit_reason = EVALUATOR.AUDIT.CPU_REASON_MARKER if cpu_reason else "native assertion failed"
    if cpu_claim_mixed:
        junit_reason = EVALUATOR.AUDIT.CPU_REASON_MARKER + " PEAK_RANGE"
    junit_path.write_text(
        f'<testsuite tests="1" failures="{int(junit_failed)}" errors="0" skipped="0">'
        '<testcase name="suite_fx_runtime_stress_tests">'
        + (f'<failure message="{junit_reason}" />' if junit_failed else '')
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
    if mode in {"cpu_claim_unrelated", "cpu_claim_mixed"} and profile == "routed":
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
        "machine": "fixture", "host_lease_id": "fixture-lease",
        "host_policy_sha256": "a" * 64, "post_test_outcome_path": str(outcome_path),
        "ctest_raw_path": str(ctest_raw_path), "ctest_inventory_path": str(junit_path),
        "ctest_junit_path": str(junit_path),
        "executable_path": str(executable), "executable_sha256": "c" * 64,
    }
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
        if mode in {"coarse", "functional"}:
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
            require_external_attestations=True)
        host_manifest = EVALUATOR._verify_host_manifest(strict_manifest, strict_witness)
        assert strict["verified"] and host_manifest["verified"]
        names.append("independent_host_attestations")

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
                     "cpu_claim_unrelated", "cpu_claim_mixed", "mismatched_outcome"):
            result = evaluate(directory / mode, mode)
            assert_status(result, "WALL_GATE_BLOCKED", mode)
            assert result["evidence_blocked"]
            if mode in {"cpu_claim_unrelated", "cpu_claim_mixed"}:
                assert result["execution_blocked"] and not result["cpu_diagnostic_blocked"]
        names.extend(["absent_native_outcome", "ctest_nonzero_unrelated", "junit_failure_green_tsv",
                      "cpu_claim_unrelated_raw_junit", "cpu_claim_mixed_raw_junit", "mismatched_native_outcome"])

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

        result = evaluate(
            directory / "host-contention", "valid",
            witness=valid_witness(["vox-fx-short-lived-competitor.exe"]),
        )
        assert_status(result, "WALL_GATE_BLOCKED", "host_contention")
        assert result["evidence_blocked"] and "witness_error" in result
        names.append("host_contention")

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
