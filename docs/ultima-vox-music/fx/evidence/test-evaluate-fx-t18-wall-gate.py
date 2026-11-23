#!/usr/bin/env python3
"""Executable conformance fixtures for the FX-T18 wall-gate evaluator.

These are offline schema fixtures. They deliberately do not claim Windows
qualification or replace the owner-authorized 117-cell run.
"""
from __future__ import annotations

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


def valid_witness(competitors: list[str] | None = None) -> dict:
    return {
        "format": "VOX_FX_T18_HOST_WITNESS_V1",
        "schema_version": 1,
        "host_observation_mode": "process_snapshot_and_event_trace",
        "sample_interval_ms": 1000,
        "pre_seconds": 30,
        "during_seconds": 10,
        "post_seconds": 30,
        "monitor_gaps": [],
        "event_trace": {"available": True, "coverage_complete": 1, "lost_events": 0},
        "runner_lease": {"recognized": True, "owner": "fixture", "host": "fixture",
                         "token": "<redacted>"},
        "runner_service_identities": ["fixture-runner"],
        "samples": [
            {"phase": phase, "processes": [{
                "pid": 1000, "process_name": "Runner.Listener.exe",
                "command_line": "<redacted>", "identity_known": True,
            }]}
            for phase in ("pre", "during", "post")
        ],
        "process_events": [],
        "unrecognized_competitors": competitors or [],
        "unknown_pids": [],
        "evidence_blocked": False,
    }


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
        if mode == "functional" and profile == "routed":
            text = mutate(text, "r44100_f64_v1_poff", finite_output="0",
                          functional_pass="0", reasons="FINITE_OUTPUT")
        if mode == "reason_mismatch" and profile == "routed":
            text = mutate(text, "r44100_f64_v1_poff", finite_output="0",
                          functional_pass="0", reasons="NONE")
        if mode == "non8" and profile == "routed":
            text = mutate(text, "r44100_f64_v1_poff", voice8_grain_invariant="0",
                          functional_pass="0", reasons="VOICE8_GRAIN_INVARIANT")
        if mode == "missing_functional" and profile == "routed":
            text = "\n".join(
                line for line in text.splitlines()
                if not (line.startswith("functional\t") and line.split("\t")[1] == "r44100_f64_v1_poff")
            ) + "\n"
        if mode == "missing_rows" and profile == "routed":
            text = "\n".join(
                line for line in text.splitlines()
                if not (line.startswith("block\t") and line.split("\t")[1] == "r44100_f64_v1_poff"
                        and line.split("\t")[2] == "159")
            ) + "\n"
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
            lines = []
            miss_line = None
            for line in text.splitlines():
                fields = line.split("\t")
                if fields[0] == "case" and fields[1] == "r44100_f64_v1_poff":
                    values = dict(zip(AUDIT.CASE_FIELDS, fields[1:]))
                    values["wall_misses"] = "1"
                    line = "case\t" + "\t".join(values[field] for field in AUDIT.CASE_FIELDS)
                elif fields[0] == "block" and fields[1] == "r44100_f64_v1_poff" and fields[2] == "0":
                    fields[3] = f"{float(fields[5]) + 1.0:.17f}"
                    fields[6] = "1"
                    miss_line = "miss\t" + "\t".join(fields[1:])
                    line = "\t".join(fields)
                elif fields[0] == "block" and fields[1] == "r44100_f64_v1_poff" and fields[2] == "159":
                    continue
                lines.append(line)
            if miss_line:
                lines.append(miss_line)
            text = "\n".join(lines) + "\n"
        report = directory / f"{profile}.tsv"
        report.write_text(text, encoding="utf-8")
        reports[profile] = report
        receipt = directory / f"{profile}.json"
        receipt.write_text(json.dumps((receipts or {}).get(profile, {
            "execution_complete": True, "execution_status": "complete",
            "evidence_blocked": False,
        }), indent=2), encoding="utf-8")
        receipt_paths[profile] = receipt
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

        result = evaluate(directory / "coarse-functional", "functional")
        assert_status(result, "WALL_GATE_FAIL", "coarse_cpu_plus_functional")
        assert result["cpu_diagnostic_blocked"] and result["functional_failure"]
        names.append("coarse_cpu_plus_functional")

        result = evaluate(directory / "coarse-only", "coarse")
        assert_status(result, "WALL_GATE_PASS_CPU_UNKNOWN", "coarse_cpu_only")
        assert result["cpu_diagnostic_blocked"] and not result["functional_failure"]
        names.append("coarse_cpu_only")

        result = evaluate(directory / "reason-mismatch", "reason_mismatch")
        assert_status(result, "WALL_GATE_BLOCKED", "functional_reason_mismatch")
        assert result["evidence_blocked"] and not result["functional_failure"]
        names.append("functional_reason_mismatch")

        result = evaluate(directory / "non8", "non8")
        assert_status(result, "WALL_GATE_BLOCKED", "non8_voice_invariant")
        assert result["evidence_blocked"] and not result["functional_failure"]
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
        cli = subprocess.run(command, check=True, capture_output=True, text=True)
        assert_status(json.loads(cli.stdout), "WALL_GATE_PASS_CPU_UNKNOWN", "CLI coarse_cpu_only")

        print("FX-T18 wall-gate executable fixtures: PASS")
        print("fixtures: " + ", ".join(names))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
