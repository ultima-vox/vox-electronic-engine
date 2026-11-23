#!/usr/bin/env python3
"""Evaluate the proposed FX-T18 wall-only gate.

The evaluator is deliberately stricter than a report formatter.  It combines
three independently audited profile reports with an independent host witness,
keeps wall/safety/functional/execution/instrumentation/evidence/CPU dispositions
separate, and applies the reviewed status precedence.  A
successful result is diagnostic evidence only; it is not release approval.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "fx_t18_audit", HERE / "audit-fx-windows-timing.py")
assert SPEC and SPEC.loader
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)

PROFILES = {"routed": 54, "authored": 54, "typical": 9}
REQUIRED_PHASES = {"pre", "during", "post"}


def _bool(value: Any, name: str) -> bool:
    if not isinstance(value, bool):
        raise ValueError(f"witness {name} is not boolean")
    return value


def _read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid {label} {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{path}: {label} is not an object")
    return value


def validate_witness(path: Path) -> dict[str, Any]:
    """Validate a redacted, host-wide process observation receipt.

    The witness must contain both one-Hz snapshots and an event-based process
    trace.  Snapshots alone are insufficient because a short-lived competitor
    can start and stop between two samples.
    """
    value = _read_json(path, "host witness")
    if value.get("format") != "VOX_FX_T18_HOST_WITNESS_V1":
        raise ValueError(f"{path}: unsupported host witness format")
    if value.get("sample_interval_ms") != 1000:
        raise ValueError(f"{path}: host witness sample interval is not 1000 ms")
    if int(value.get("pre_seconds", 0)) < 30 or int(value.get("post_seconds", 0)) < 30:
        raise ValueError(f"{path}: host witness lacks 30-second pre/post windows")
    if value.get("host_observation_mode") != "process_snapshot_and_event_trace":
        raise ValueError(f"{path}: host-wide observation mode is not declared")
    if value.get("monitor_gaps") not in ([], None):
        raise ValueError(f"{path}: host witness has polling gaps")
    event_trace = value.get("event_trace")
    if not isinstance(event_trace, dict) \
            or not _bool(event_trace.get("available"), "event_trace.available"):
        raise ValueError(f"{path}: event-based process trace is unavailable")
    if event_trace.get("coverage_complete") not in (True, 1) \
            or event_trace.get("lost_events", 1) != 0:
        raise ValueError(f"{path}: host event trace is incomplete or lost events")
    samples = value.get("samples")
    if not isinstance(samples, list) or not samples:
        raise ValueError(f"{path}: host witness has no samples")
    phases = {sample.get("phase") for sample in samples if isinstance(sample, dict)}
    if not REQUIRED_PHASES.issubset(phases):
        raise ValueError(f"{path}: host witness is missing pre/during/post samples")
    if _bool(value.get("evidence_blocked"), "evidence_blocked"):
        raise ValueError(f"{path}: host witness is already evidence-blocked")
    competitors = value.get("unrecognized_competitors", [])
    unknown_pids = value.get("unknown_pids", [])
    if not isinstance(competitors, list) or not isinstance(unknown_pids, list):
        raise ValueError(f"{path}: malformed competitor or unknown-PID list")
    if competitors:
        raise ValueError(f"{path}: unrecognized host competitors are present")
    if unknown_pids:
        raise ValueError(f"{path}: unknown process PIDs are present")
    lease = value.get("runner_lease")
    if not isinstance(lease, dict) or lease.get("recognized") is not True:
        raise ValueError(f"{path}: runner lease identity is incomplete")
    service_identities = value.get("runner_service_identities")
    if not isinstance(service_identities, list):
        raise ValueError(f"{path}: runner service identities are missing")
    for secret_field in ("token", "lease_token", "command_line"):
        if secret_field in lease and lease[secret_field] not in (None, "<redacted>"):
            raise ValueError(f"{path}: sensitive lease field {secret_field} is not redacted")

    events = value.get("process_events", [])
    if not isinstance(events, list):
        raise ValueError(f"{path}: malformed process event list")
    for event in events:
        if not isinstance(event, dict) or not isinstance(event.get("pid"), int) \
                or not isinstance(event.get("process_name"), str):
            raise ValueError(f"{path}: malformed process event")
        if event.get("identity_known") is not True:
            raise ValueError(f"{path}: process event has unknown identity")
        if event.get("command_line") not in (None, "<redacted>"):
            raise ValueError(f"{path}: process event command-line secret was not redacted")
    for sample in samples:
        if not isinstance(sample, dict) or sample.get("phase") not in REQUIRED_PHASES:
            raise ValueError(f"{path}: malformed host sample")
        processes = sample.get("processes")
        if not isinstance(processes, list):
            raise ValueError(f"{path}: host sample has no process list")
        for process in processes:
            if not isinstance(process, dict) or not isinstance(process.get("pid"), int) \
                    or not isinstance(process.get("process_name"), str):
                raise ValueError(f"{path}: malformed process snapshot")
            if process.get("identity_known") is not True:
                raise ValueError(f"{path}: process snapshot has unknown identity")
            if process.get("command_line") not in (None, "<redacted>"):
                raise ValueError(f"{path}: process snapshot command-line secret was not redacted")
    return {
        "verified": True,
        "path": str(path),
        "samples": len(samples),
        "process_events": len(events),
        "competitors": 0,
    }


def _scan_observed_failures(path: Path) -> dict[str, bool]:
    """Retain observed failures even if a later crash makes a report partial."""
    result = {"wall_failure": False, "safety_failure": False}
    try:
        lines = path.read_text(encoding="utf-8-sig").splitlines()
    except (OSError, UnicodeDecodeError):
        return result
    for line in lines:
        fields = line.split("\t")
        if not fields:
            continue
        if fields[0] == "case" and len(fields) == 1 + len(AUDIT.CASE_FIELDS):
            values = dict(zip(AUDIT.CASE_FIELDS, fields[1:]))
            try:
                result["wall_failure"] |= int(values["wall_misses"]) > 0
                result["safety_failure"] |= any(
                    int(values[field]) > 0 for field in ("alloc_calls", "lock_entries", "wait_entries"))
            except ValueError:
                pass
        elif fields[0] == "block" and len(fields) == 1 + len(AUDIT.BLOCK_FIELDS):
            try:
                result["wall_failure"] |= int(fields[6]) == 1
                # Block rows do not contain allocation counts; safety is
                # intentionally not inferred from event/grain telemetry.
            except ValueError:
                pass
    return result


def _execution_state(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {"execution_failure": False, "execution_blocked": True,
                "evidence_blocked": True, "receipt_missing": True}
    try:
        value = _read_json(path, "execution receipt")
    except ValueError as error:
        return {"execution_failure": True, "execution_blocked": True,
                "evidence_blocked": True, "receipt_error": str(error)}
    status = str(value.get("execution_status", value.get("ctest_outcome", ""))).lower()
    execution_failure = bool(value.get("execution_failure", False)) \
        or bool(value.get("timed_out", False)) \
        or bool(value.get("crashed", False)) \
        or bool(value.get("skipped", False)) \
        or value.get("execution_complete") is False \
        or status in {"timeout_failure", "crash_failure", "skip_failure", "incomplete"}
    evidence_blocked = execution_failure \
        or bool(value.get("evidence_blocked", False)) \
        or bool(value.get("monitor_gap", False))
    return {
        "execution_failure": execution_failure,
        "execution_blocked": execution_failure,
        "evidence_blocked": evidence_blocked,
        "receipt_missing": False,
        "status": status,
    }


def _dispositions(
    wall_failure: bool, safety_failure: bool, functional_failure: bool,
    execution_failure: bool, execution_blocked: bool,
    instrumentation_blocked: bool, evidence_blocked: bool,
    cpu_diagnostic_blocked: bool,
) -> dict[str, str]:
    return {
        "wall": "FAIL" if wall_failure else "BLOCKED" if evidence_blocked else "PASS",
        "safety": "FAIL" if safety_failure else "BLOCKED" if instrumentation_blocked or evidence_blocked else "PASS",
        "functional": "FAIL" if functional_failure else "BLOCKED" if evidence_blocked else "PASS",
        "execution": "FAIL" if execution_failure else "BLOCKED" if execution_blocked else "PASS",
        "instrumentation": "BLOCKED" if instrumentation_blocked else "PASS",
        "evidence": "BLOCKED" if evidence_blocked else "PASS",
        "cpu": "BLOCKED" if cpu_diagnostic_blocked else "PASS",
    }


def evaluate_results(
    results: dict[str, dict[str, Any]], witness: dict[str, Any] | None,
    execution: dict[str, dict[str, Any]] | None = None,
) -> dict[str, Any]:
    execution = execution or {}
    missing_profiles = sorted(set(PROFILES) - set(results))
    extra_profiles = sorted(set(results) - set(PROFILES))
    coverage_error = bool(missing_profiles or extra_profiles)
    wall_failure = False
    safety_failure = False
    functional_failure = False
    execution_failure = False
    execution_blocked = False
    instrumentation_blocked = False
    evidence_blocked = coverage_error or witness is None \
        or (witness is not None and witness.get("verified") is not True)
    cpu_diagnostic_blocked = False
    profile_cells_complete = True
    for profile, result in results.items():
        observed = _scan_observed_failures(Path(result["report"])) if result.get("report") else {}
        wall_failure |= bool(result.get("wall_failure", False) or observed.get("wall_failure", False))
        safety_failure |= bool(result.get("safety_failure", False) or observed.get("safety_failure", False))
        functional_failure |= bool(result.get("functional_failure", False))
        execution_failure |= bool(result.get("execution_failure", False))
        instrumentation_blocked |= bool(result.get("instrumentation_blocked", False))
        evidence_blocked |= bool(result.get("evidence_blocked", False))
        cpu_diagnostic_blocked |= bool(
            result.get("cpu_diagnostic_blocked", False)
            or result.get("cpu_measurement_insufficient", False))
        expected_cells = PROFILES.get(profile)
        if expected_cells is None \
                or result.get("cells") != expected_cells \
                or result.get("totals", {}).get("callbacks") != expected_cells * AUDIT.BLOCKS:
            profile_cells_complete = False
        state = execution.get(profile)
        if state:
            execution_failure |= bool(state.get("execution_failure", False))
            execution_blocked |= bool(state.get("execution_blocked", False))
            execution_blocked |= bool(state.get("receipt_missing", False))
            evidence_blocked |= bool(state.get("evidence_blocked", False))
            evidence_blocked |= bool(state.get("receipt_missing", False))
        else:
            execution_blocked = True
            evidence_blocked = True

    evidence_blocked |= not profile_cells_complete
    performance_failure = wall_failure or safety_failure or functional_failure
    # Required precedence: observed runtime violation first, then incomplete
    # execution/evidence, then the proposed wall-only CPU-unknown diagnostic.
    if performance_failure:
        primary_status = "WALL_GATE_FAIL"
    elif execution_failure or evidence_blocked:
        primary_status = "WALL_GATE_BLOCKED"
    elif cpu_diagnostic_blocked:
        primary_status = "WALL_GATE_PASS_CPU_UNKNOWN"
    else:
        primary_status = "WALL_GATE_PASS"
    return {
        "format": "VOX_FX_WALL_GATE_EVALUATION_V1",
        "primary_status": primary_status,
        "wall_failure": wall_failure,
        "safety_failure": safety_failure,
        "functional_failure": functional_failure,
        "execution_failure": execution_failure,
        "execution_blocked": execution_blocked,
        "performance_failure": performance_failure,
        "instrumentation_blocked": instrumentation_blocked,
        "evidence_blocked": evidence_blocked,
        "cpu_diagnostic_blocked": cpu_diagnostic_blocked,
        "missing_profiles": missing_profiles,
        "extra_profiles": extra_profiles,
        "profile_cells_complete": profile_cells_complete,
        "expected_cells": sum(PROFILES.values()),
        "expected_callbacks": sum(PROFILES.values()) * AUDIT.BLOCKS,
        "observed_callbacks": sum(
            int(result.get("totals", {}).get("callbacks", 0)) for result in results.values()),
        "dispositions": _dispositions(
            wall_failure, safety_failure, functional_failure, execution_failure,
            execution_blocked, instrumentation_blocked, evidence_blocked,
            cpu_diagnostic_blocked),
        "witness": witness,
        "profiles": results,
    }


def evaluate_reports(
    reports: dict[str, Path], root: Path | None = None,
    executable: Path | None = None, receipts: dict[str, Path] | None = None,
    manifests: dict[str, Path] | None = None, expected_head: str | None = None,
    expected_tree: str | None = None, witness: Path | None = None,
    allow_unverified_provenance: bool = False,
) -> dict[str, Any]:
    receipts = receipts or {}
    manifests = manifests or {}
    audited: dict[str, dict[str, Any]] = {}
    execution: dict[str, dict[str, Any]] = {}
    for profile, report in reports.items():
        try:
            if not allow_unverified_provenance and (
                    root is None or profile not in receipts or profile not in manifests):
                raise ValueError("report requires root, runner receipt and artifact manifest")
            audited[profile] = AUDIT.audit(
                report, require_windows=not allow_unverified_provenance,
                root=root, executable=executable,
                runner_receipt=receipts.get(profile), artifact_manifest=manifests.get(profile),
                expected_head=expected_head, expected_tree=expected_tree,
                allow_unverified_provenance=allow_unverified_provenance,
                allow_instrumentation_blocked=True,
            )
        except (OSError, ValueError) as error:
            observed = _scan_observed_failures(report)
            audited[profile] = {
                "profile": profile, "report": str(report), "cells": 0,
                "totals": {}, "wall_failure": observed["wall_failure"],
                "safety_failure": observed["safety_failure"],
                "execution_failure": False, "evidence_blocked": True,
                "audit_error": str(error),
            }
        if profile in receipts:
            execution[profile] = _execution_state(receipts[profile])
        else:
            execution[profile] = {"execution_failure": False, "evidence_blocked": True,
                                  "receipt_missing": True}
    witness_result: dict[str, Any] | None = None
    witness_error: str | None = None
    if witness is not None:
        try:
            witness_result = validate_witness(witness)
        except ValueError as error:
            witness_error = str(error)
    result = evaluate_results(audited, witness_result, execution)
    if witness_error:
        result["evidence_blocked"] = True
        result["primary_status"] = "WALL_GATE_FAIL" if result["performance_failure"] else "WALL_GATE_BLOCKED"
        result["witness_error"] = witness_error
    return result


def _assignments(values: list[str], name: str) -> dict[str, Path]:
    result: dict[str, Path] = {}
    for value in values:
        if "=" not in value:
            raise ValueError(f"{name} requires PROFILE=PATH")
        profile, path = value.split("=", 1)
        if profile not in PROFILES:
            raise ValueError(f"unknown profile {profile!r}")
        if profile in result:
            raise ValueError(f"duplicate {name} profile {profile!r}")
        result[profile] = Path(path)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", action="append", required=True,
                        help="PROFILE=PATH; provide routed, authored and typical")
    parser.add_argument("--receipt", action="append", default=[], help="PROFILE=PATH")
    parser.add_argument("--manifest", action="append", default=[], help="PROFILE=PATH")
    parser.add_argument("--root", type=Path)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--expected-head")
    parser.add_argument("--expected-tree")
    parser.add_argument("--witness", type=Path)
    parser.add_argument("--allow-unverified-provenance", action="store_true",
                        help="schema-fixture mode only; never use for Windows qualification")
    args = parser.parse_args()
    try:
        reports = _assignments(args.report, "--report")
        if set(reports) != set(PROFILES):
            raise ValueError("exactly one routed, authored and typical report is required")
        receipts = _assignments(args.receipt, "--receipt") if args.receipt else {}
        manifests = _assignments(args.manifest, "--manifest") if args.manifest else {}
        result = evaluate_reports(
            reports, root=args.root, executable=args.executable, receipts=receipts,
            manifests=manifests, expected_head=args.expected_head,
            expected_tree=args.expected_tree, witness=args.witness,
            allow_unverified_provenance=args.allow_unverified_provenance,
        )
    except (OSError, ValueError) as error:
        parser.exit(2, f"FX-T18 wall evaluator: {error}\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["primary_status"] not in {"WALL_GATE_FAIL", "WALL_GATE_BLOCKED"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
