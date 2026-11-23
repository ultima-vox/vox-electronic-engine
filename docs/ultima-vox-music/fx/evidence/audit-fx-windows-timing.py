#!/usr/bin/env python3
"""Audit one FX-T18 callback capture without hiding failed rows.

The auditor is intentionally stricter than a report formatter. It reconciles
all callback rows, explicit miss rows, p50/p95/p99/p99.9/max values, deadline
flags, probe attestation, and independent provenance. A valid audit is still
only diagnostic evidence: it is not DAW acceptance or a release approval.

A Windows receipt should be audited with all of these independent inputs::

    audit-fx-windows-timing.py report.tsv --root . \
      --runner-receipt runner.json --artifact-manifest artifacts.json \
      --executable build-suite/tests/suite/Release/suite_fx_runtime_stress_tests.exe

``--allow-nonwindows`` and ``--allow-unverified-provenance`` are for local
schema fixtures only. They never turn a fixture into Windows qualification.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
from typing import Any

RATES = (44100, 48000, 96000)
FRAMES = (64, 128, 512)
VOICES = (1, 4, 8)
PATTERNS = ("off", "on")
BLOCKS = 160

CASE_FIELDS = (
    "case_id", "profile", "rate_hz", "frames", "initial_voices", "pattern",
    "blocks", "budget_us", "alloc_calls", "lock_entries", "wait_entries",
    "cpu_pairs", "zero_cpu_deltas", "min_positive_cpu_us", "wall_misses",
    "cpu_misses", "wall_only_misses", "max_grains", "peak", "dc",
    "wall_p50_us", "wall_p95_us", "wall_p99_us", "wall_p99_9_us",
    "wall_max_us", "cpu_p50_us", "cpu_p95_us", "cpu_p99_us",
    "cpu_p99_9_us", "cpu_max_us", "cpu_at_wall_max_us",
)
BLOCK_FIELDS = (
    "case_id", "block", "wall_us", "cpu_us", "budget_us", "wall_miss",
    "cpu_miss", "events", "grains",
)
MISS_FIELDS = BLOCK_FIELDS

REQUIRED_METADATA = {
    "format", "profile", "source_sha", "source_head", "source_tree",
    "executable_path", "executable_sha256", "runner_receipt_sha256",
    "platform", "pointer_bits", "timing_clock", "timing_requested",
    "timing_bracket", "wall_timer_overhead_us", "cpu_timer_overhead_us",
    "timer_calibration_samples", "allocation_probe", "lock_probe",
    "wait_probe", "lock_target_count", "lock_resolved_target_count",
    "lock_matched_slot_count", "lock_patched_slot_count", "wait_target_count",
    "wait_resolved_target_count", "wait_matched_slot_count",
    "wait_patched_slot_count", "lock_target_count_total",
    "lock_resolved_target_count_total", "lock_loaded_module_count",
    "lock_matched_slot_count_total", "lock_patched_slot_count_total",
    "lock_patch_failure_count", "lock_module_enumeration_complete",
    "lock_positive_control_passed", "lock_positive_control_entries",
    "wait_positive_control_entries", "lock_coverage_complete",
    "blocks_per_case", "sentinel_cases",
}


def _float(value: str, name: str) -> float:
    try:
        result = float(value)
    except ValueError as error:
        raise ValueError(f"invalid {name}: {value!r}") from error
    if not math.isfinite(result) and result != -1.0:
        raise ValueError(f"non-finite {name}: {value!r}")
    return result


def _int(value: str, name: str) -> int:
    try:
        return int(value)
    except ValueError as error:
        raise ValueError(f"invalid {name}: {value!r}") from error


def _near(actual: float, expected: float, absolute: float = 0.02) -> bool:
    return abs(actual - expected) <= absolute + abs(expected) * 1.0e-6


def _percentiles(values: list[float]) -> tuple[float, float, float, float, float]:
    ordered = sorted(values)
    # 160 observations: nearest-rank p50/p95/p99/p99.9/max are these
    # explicit order statistics. p99.9 and max are both the last sample at
    # this block count, but both fields remain in the evidence schema.
    return tuple(ordered[index] for index in (79, 151, 158, 159, 159))  # type: ignore[return-value]


def _expected_cases(profile: str) -> set[tuple[int, int, int, str]]:
    voices = (1,) if profile == "typical" else VOICES
    patterns = ("off",) if profile == "typical" else PATTERNS
    return {(rate, frames, voice, pattern)
            for rate in RATES for frames in FRAMES for voice in voices for pattern in patterns}


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _same_path(left: str | Path, right: str | Path) -> bool:
    return str(Path(left).absolute()).casefold() == str(Path(right).absolute()).casefold()


def _git(root: Path, *arguments: str) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(root), *arguments],
            check=True, capture_output=True, text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError(f"unable to independently inspect git at {root}: {error}") from error
    return result.stdout.strip()


def _verify_manifest(path: Path, report: Path, receipt: Path, executable: Path) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid artifact manifest {path}: {error}") from error
    if manifest.get("format") != "VOX_FX_T18_ARTIFACTS_V1":
        raise ValueError(f"unsupported artifact manifest format in {path}")
    files = manifest.get("files")
    if not isinstance(files, list):
        raise ValueError(f"artifact manifest {path} has no files list")
    verified: dict[str, str] = {}
    for entry in files:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str) \
                or not isinstance(entry.get("sha256"), str):
            raise ValueError(f"malformed artifact entry in {path}")
        artifact = Path(entry["path"])
        if not artifact.is_absolute():
            artifact = path.parent / artifact
        if not artifact.is_file():
            raise ValueError(f"missing artifact listed by {path}: {artifact}")
        actual = _sha256(artifact)
        if actual.casefold() != entry["sha256"].casefold():
            raise ValueError(f"artifact hash mismatch for {artifact}")
        verified[str(artifact.absolute())] = actual
    required = (report, receipt, executable)
    if any(str(item.absolute()) not in verified for item in required):
        raise ValueError(f"artifact manifest {path} does not cover report, receipt and executable")
    return {"path": str(path), "files": len(files), "hashes": verified}


def _verify_provenance(
    metadata: dict[str, str], report: Path, root: Path | None,
    executable_arg: Path | None, receipt_arg: Path | None,
    manifest_arg: Path | None, expected_head: str | None,
    expected_tree: str | None, allow_unverified: bool,
) -> dict[str, Any]:
    if allow_unverified:
        return {"verified": False, "reason": "explicit local schema-fixture override"}
    if root is None or receipt_arg is None or manifest_arg is None:
        raise ValueError("independent provenance requires --root, --runner-receipt and --artifact-manifest")

    actual_head = _git(root, "rev-parse", "HEAD")
    actual_tree = _git(root, "rev-parse", "HEAD^{tree}")
    if expected_head and actual_head.casefold() != expected_head.casefold():
        raise ValueError(f"git HEAD mismatch: expected {expected_head}, found {actual_head}")
    if expected_tree and actual_tree.casefold() != expected_tree.casefold():
        raise ValueError(f"git tree mismatch: expected {expected_tree}, found {actual_tree}")
    if metadata["source_head"].casefold() != actual_head.casefold() \
            or metadata["source_tree"].casefold() != actual_tree.casefold():
        raise ValueError("report source HEAD/tree do not match independently read git HEAD/tree")
    if metadata["source_sha"].casefold() != actual_head.casefold():
        raise ValueError("legacy source_sha does not match independently read git HEAD")

    try:
        receipt = json.loads(receipt_arg.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid runner receipt {receipt_arg}: {error}") from error
    for field in ("format", "scenario", "source_head", "source_tree", "executable_path",
                  "executable_sha256", "report_path", "runner"):
        if field not in receipt:
            raise ValueError(f"runner receipt is missing {field}")
    if receipt["format"] != "VOX_FX_T18_RUNNER_RECEIPT_V1":
        raise ValueError("unsupported runner receipt format")
    if receipt["scenario"] != metadata["profile"]:
        raise ValueError("runner receipt scenario does not match report profile")
    if receipt["source_head"].casefold() != actual_head.casefold() \
            or receipt["source_tree"].casefold() != actual_tree.casefold():
        raise ValueError("runner receipt source does not match independently read git")
    if not receipt["runner"]:
        raise ValueError("runner receipt has an empty runner identity")

    executable = executable_arg or Path(receipt["executable_path"])
    if not executable.is_file():
        raise ValueError(f"missing executable for independent hash: {executable}")
    actual_executable_sha = _sha256(executable)
    if actual_executable_sha.casefold() != receipt["executable_sha256"].casefold():
        raise ValueError("runner receipt executable SHA-256 does not match the executable")
    if actual_executable_sha.casefold() != metadata["executable_sha256"].casefold():
        raise ValueError("report executable SHA-256 does not match the executable")
    if not _same_path(receipt["executable_path"], executable) \
            or not _same_path(metadata["executable_path"], executable):
        raise ValueError("report/receipt executable paths do not match the audited executable")
    if not _same_path(receipt["report_path"], report):
        raise ValueError("runner receipt report path does not match the audited report")

    receipt_sha = _sha256(receipt_arg)
    if receipt_sha.casefold() != metadata["runner_receipt_sha256"].casefold():
        raise ValueError("report runner receipt SHA-256 does not match the receipt file")
    manifest_result = _verify_manifest(manifest_arg, report, receipt_arg, executable)
    return {
        "verified": True,
        "git_head": actual_head,
        "git_tree": actual_tree,
        "executable": str(executable),
        "executable_sha256": actual_executable_sha,
        "runner_receipt": str(receipt_arg),
        "runner_receipt_sha256": receipt_sha,
        "artifact_manifest": manifest_result,
    }


def _parse_record(values: list[str], fields: tuple[str, ...], line_number: int, path: Path) -> dict[str, Any]:
    if len(values) != 1 + len(fields):
        raise ValueError(f"{path}:{line_number}: malformed record")
    result: dict[str, Any] = dict(zip(fields, values[1:]))
    result["block"] = _int(result["block"], "block")
    for field in ("wall_us", "cpu_us", "budget_us"):
        result[field] = _float(result[field], field)
    for field in ("wall_miss", "cpu_miss", "events", "grains"):
        result[field] = _int(result[field], field)
    if result["wall_miss"] not in (0, 1) or result["cpu_miss"] not in (0, 1) \
            or result["events"] < 0 or result["grains"] < 0:
        raise ValueError(f"{path}:{line_number}: invalid block counters")
    return result


def audit(
    path: Path, require_windows: bool = True, root: Path | None = None,
    executable: Path | None = None, runner_receipt: Path | None = None,
    artifact_manifest: Path | None = None, expected_head: str | None = None,
    expected_tree: str | None = None, allow_unverified_provenance: bool = False,
) -> dict[str, Any]:
    metadata: dict[str, str] = {}
    cases: dict[str, dict[str, Any]] = {}
    blocks: dict[str, list[dict[str, Any]]] = {}
    misses: dict[tuple[str, int], dict[str, Any]] = {}

    try:
        lines = path.read_text(encoding="utf-8-sig").splitlines()
    except UnicodeDecodeError:
        lines = path.read_text(encoding="utf-16").splitlines()

    for line_number, line in enumerate(lines, 1):
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if fields[0] == "meta":
            if len(fields) != 3 or fields[1] in metadata:
                raise ValueError(f"{path}:{line_number}: malformed or duplicate metadata")
            metadata[fields[1]] = fields[2]
        elif fields[0] == "schema":
            continue
        elif fields[0] == "case":
            if len(fields) != 1 + len(CASE_FIELDS):
                raise ValueError(f"{path}:{line_number}: malformed case row")
            values = dict(zip(CASE_FIELDS, fields[1:]))
            case_id = values["case_id"]
            if case_id in cases:
                raise ValueError(f"{path}:{line_number}: duplicate case {case_id}")
            for field in ("rate_hz", "frames", "initial_voices", "blocks", "alloc_calls",
                          "lock_entries", "wait_entries", "cpu_pairs", "zero_cpu_deltas",
                          "wall_misses", "cpu_misses", "wall_only_misses", "max_grains"):
                values[field] = _int(values[field], field)
            if any(values[field] < 0 for field in ("rate_hz", "frames", "initial_voices", "blocks",
                                                   "alloc_calls", "lock_entries", "wait_entries",
                                                   "cpu_pairs", "zero_cpu_deltas", "wall_misses",
                                                   "cpu_misses", "wall_only_misses", "max_grains")):
                raise ValueError(f"{path}:{line_number}: negative case counter")
            for field in ("budget_us", "min_positive_cpu_us", "peak", "dc", "wall_p50_us",
                          "wall_p95_us", "wall_p99_us", "wall_p99_9_us", "wall_max_us",
                          "cpu_p50_us", "cpu_p95_us", "cpu_p99_us", "cpu_p99_9_us",
                          "cpu_max_us", "cpu_at_wall_max_us"):
                values[field] = _float(values[field], field)
            cases[case_id] = values
        elif fields[0] == "block":
            values = _parse_record(fields, BLOCK_FIELDS, line_number, path)
            blocks.setdefault(values["case_id"], []).append(values)
        elif fields[0] == "miss":
            values = _parse_record(fields, MISS_FIELDS, line_number, path)
            key = (values["case_id"], values["block"])
            if key in misses:
                raise ValueError(f"{path}:{line_number}: duplicate miss row {key}")
            misses[key] = values
        else:
            raise ValueError(f"{path}:{line_number}: unknown record {fields[0]!r}")

    missing = REQUIRED_METADATA - metadata.keys()
    if missing:
        raise ValueError(f"{path}: missing metadata: {', '.join(sorted(missing))}")
    if metadata["format"] != "VOX_FX_T18_REPORT_V1":
        raise ValueError(f"{path}: unsupported report format {metadata['format']!r}")
    for name in ("source_sha", "source_head", "source_tree"):
        if not re.fullmatch(r"[0-9a-fA-F]{40}", metadata[name]):
            raise ValueError(f"{path}: {name} is not a full SHA-1")
    if metadata["source_sha"].casefold() != metadata["source_head"].casefold():
        raise ValueError(f"{path}: source_sha and source_head disagree")
    for name in ("executable_sha256", "runner_receipt_sha256"):
        if not re.fullmatch(r"[0-9a-fA-F]{64}", metadata[name]):
            raise ValueError(f"{path}: {name} is not a SHA-256")
    profile = metadata["profile"]
    if profile not in {"routed", "authored", "typical"}:
        raise ValueError(f"{path}: unsupported profile {profile!r}")
    expected_sentinels = "not-applicable" if profile == "typical" else "r96000_f64_v1_poff,r44100_f512_v8_pon"
    if metadata["sentinel_cases"] != expected_sentinels:
        raise ValueError(f"{path}: sentinel case declaration mismatch")
    if metadata["timing_requested"] != "1" \
            or metadata["timing_bracket"] != "cpu_start,wall_start,processBlock,wall_end,cpu_end" \
            or metadata["blocks_per_case"] != str(BLOCKS):
        raise ValueError(f"{path}: CPU timing bracket or block-count request is not recorded")
    if metadata["pointer_bits"] != "64":
        raise ValueError(f"{path}: not a 64-bit capture")
    if require_windows and "windows" not in metadata["platform"].lower():
        raise ValueError(f"{path}: qualification receipt is not Windows: {metadata['platform']!r}")
    if require_windows and metadata["timing_clock"] != "GetThreadTimes":
        raise ValueError(f"{path}: unexpected Windows timing clock {metadata['timing_clock']!r}")
    for probe in ("allocation_probe", "lock_probe", "wait_probe"):
        if metadata[probe] != "available":
            raise ValueError(f"{path}: {probe} is {metadata[probe]!r}, not available")
    for name in ("wall_timer_overhead_us", "cpu_timer_overhead_us"):
        if _float(metadata[name], name) < 0.0:
            raise ValueError(f"{path}: {name} is unavailable")
    if _int(metadata["timer_calibration_samples"], "timer_calibration_samples") <= 0:
        raise ValueError(f"{path}: timer calibration is not recorded")

    # A zero callback count is not qualifying evidence unless both target
    # classes were actually installed and the positive controls counted them.
    coverage_ints = {
        name: _int(metadata[name], name) for name in (
            "lock_target_count", "lock_resolved_target_count", "lock_matched_slot_count",
            "lock_patched_slot_count", "wait_target_count", "wait_resolved_target_count",
            "wait_matched_slot_count", "wait_patched_slot_count", "lock_target_count_total",
            "lock_resolved_target_count_total", "lock_loaded_module_count",
            "lock_matched_slot_count_total", "lock_patched_slot_count_total",
            "lock_patch_failure_count", "lock_positive_control_entries",
            "wait_positive_control_entries")
    }
    if metadata["lock_coverage_complete"] != "1" \
            or metadata["lock_module_enumeration_complete"] != "1" \
            or metadata["lock_positive_control_passed"] != "1":
        raise ValueError(f"{path}: synchronization instrumentation is incomplete")
    if coverage_ints["lock_target_count"] <= 0 \
            or coverage_ints["wait_target_count"] <= 0 \
            or coverage_ints["lock_loaded_module_count"] <= 0 \
            or coverage_ints["lock_positive_control_entries"] <= 0 \
            or coverage_ints["wait_positive_control_entries"] <= 0:
        raise ValueError(f"{path}: synchronization positive control or target coverage is zero")
    for prefix in ("lock", "wait"):
        if coverage_ints[f"{prefix}_target_count"] != coverage_ints[f"{prefix}_resolved_target_count"] \
                or coverage_ints[f"{prefix}_matched_slot_count"] != coverage_ints[f"{prefix}_patched_slot_count"] \
                or coverage_ints[f"{prefix}_matched_slot_count"] <= 0:
            raise ValueError(f"{path}: incomplete {prefix} hook coverage")
    if coverage_ints["lock_target_count_total"] != coverage_ints["lock_resolved_target_count_total"] \
            or coverage_ints["lock_matched_slot_count_total"] != coverage_ints["lock_patched_slot_count_total"] \
            or coverage_ints["lock_patch_failure_count"] != 0:
        raise ValueError(f"{path}: incomplete aggregate hook coverage")

    provenance = _verify_provenance(
        metadata, path, root, executable, runner_receipt, artifact_manifest,
        expected_head, expected_tree, allow_unverified_provenance,
    )

    expected = _expected_cases(profile)
    observed: set[tuple[int, int, int, str]] = set()
    totals = {"callbacks": 0, "cpu_pairs": 0, "wall_misses": 0, "cpu_misses": 0,
              "wall_only_misses": 0, "alloc_calls": 0, "lock_entries": 0,
              "wait_entries": 0, "zero_cpu_deltas": 0}
    deadline_rows: list[dict[str, Any]] = []
    expected_misses: dict[tuple[str, int], dict[str, Any]] = {}

    for case_id, case in cases.items():
        key = (case["rate_hz"], case["frames"], case["initial_voices"], case["pattern"])
        if key in observed or key not in expected:
            raise ValueError(f"{path}: unexpected or duplicate case matrix cell {key}")
        observed.add(key)
        if case["profile"] != profile or case["blocks"] != BLOCKS:
            raise ValueError(f"{path}: mislabeled case {case_id}")
        budget = case["frames"] * 1_000_000.0 / case["rate_hz"]
        if not _near(case["budget_us"], budget, 0.001):
            raise ValueError(f"{path}: bad deadline in {case_id}")
        rows = blocks.get(case_id, [])
        if len(rows) != BLOCKS or {row["block"] for row in rows} != set(range(BLOCKS)):
            raise ValueError(f"{path}: {case_id} does not contain exactly {BLOCKS} unique block rows")
        rows.sort(key=lambda row: row["block"])
        wall = [row["wall_us"] for row in rows]
        cpu_by_block = [row["cpu_us"] for row in rows]
        cpu = [value for value in cpu_by_block if value >= 0.0]
        if len(cpu) != BLOCKS:
            raise ValueError(f"{path}: incomplete CPU timing pairs in {case_id}")
        if any(not _near(row["budget_us"], budget, 0.001) for row in rows):
            raise ValueError(f"{path}: block deadline mismatch in {case_id}")
        for row in rows:
            wall_miss = int(row["wall_us"] > budget)
            cpu_miss = int(row["cpu_us"] > budget)
            if row["wall_miss"] != wall_miss or row["cpu_miss"] != cpu_miss:
                raise ValueError(f"{path}: miss flag mismatch in {case_id} block {row['block']}")
            if row["cpu_us"] < 0.0 or row["wall_us"] < 0.0:
                raise ValueError(f"{path}: invalid timing in {case_id} block {row['block']}")
            if wall_miss or cpu_miss:
                expected_misses[(case_id, row["block"])] = row
                deadline_rows.append({"case_id": case_id, **row})

        wall_p = _percentiles(wall)
        cpu_p = _percentiles(cpu)
        for names, actual in (
            (("wall_p50_us", "wall_p95_us", "wall_p99_us", "wall_p99_9_us", "wall_max_us"), wall_p),
            (("cpu_p50_us", "cpu_p95_us", "cpu_p99_us", "cpu_p99_9_us", "cpu_max_us"), cpu_p),
        ):
            if any(not _near(case[name], value) for name, value in zip(names, actual)):
                raise ValueError(f"{path}: percentile mismatch in {case_id}")
        if case["cpu_pairs"] != len(cpu) or case["zero_cpu_deltas"] != sum(value == 0.0 for value in cpu):
            raise ValueError(f"{path}: CPU accounting counters do not reconcile in {case_id}")
        positive = [value for value in cpu if value > 0.0]
        if not _near(case["min_positive_cpu_us"], min(positive, default=0.0)):
            raise ValueError(f"{path}: minimum positive CPU counter mismatch in {case_id}")
        max_wall_index = max(range(BLOCKS), key=lambda index: wall[index])
        if not _near(case["cpu_at_wall_max_us"], cpu_by_block[max_wall_index]):
            raise ValueError(f"{path}: CPU-at-wall-max mismatch in {case_id}")
        wall_misses = sum(row["wall_miss"] for row in rows)
        cpu_misses = sum(row["cpu_miss"] for row in rows)
        wall_only = sum(row["wall_miss"] and not row["cpu_miss"] for row in rows)
        if (case["wall_misses"], case["cpu_misses"], case["wall_only_misses"]) != (wall_misses, cpu_misses, wall_only):
            raise ValueError(f"{path}: deadline counters do not reconcile in {case_id}")
        totals["callbacks"] += BLOCKS
        totals["cpu_pairs"] += case["cpu_pairs"]
        totals["wall_misses"] += case["wall_misses"]
        totals["cpu_misses"] += case["cpu_misses"]
        totals["wall_only_misses"] += case["wall_only_misses"]
        totals["alloc_calls"] += case["alloc_calls"]
        totals["lock_entries"] += case["lock_entries"]
        totals["wait_entries"] += case["wait_entries"]
        totals["zero_cpu_deltas"] += case["zero_cpu_deltas"]

    if observed != expected:
        missing_cases = sorted(expected - observed)
        extra_cases = sorted(observed - expected)
        raise ValueError(f"{path}: matrix mismatch; missing={missing_cases}, extra={extra_cases}")
    if set(misses) != set(expected_misses):
        raise ValueError(f"{path}: explicit miss rows do not match computed raw misses")
    for key, row in expected_misses.items():
        recorded = misses[key]
        for field in MISS_FIELDS[1:]:
            if recorded[field] != row[field]:
                raise ValueError(f"{path}: miss row mismatch for {key} field {field}")
    unknown_block_cases = set(blocks) - set(cases)
    if unknown_block_cases:
        raise ValueError(f"{path}: block rows have unknown cases: {sorted(unknown_block_cases)}")

    performance_failure = any(totals[name] > 0 for name in (
        "wall_misses", "cpu_misses", "alloc_calls", "lock_entries", "wait_entries"))
    return {
        "qualification": "diagnostic only; TL must review hardware load, timing resolution and host evidence",
        "report": str(path),
        "profile": profile,
        "cells": len(cases),
        "performance_failure": performance_failure,
        "totals": totals,
        "deadline_rows": deadline_rows,
        "metadata": metadata,
        "provenance": provenance,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--runner-receipt", type=Path)
    parser.add_argument("--artifact-manifest", type=Path)
    parser.add_argument("--expected-head")
    parser.add_argument("--expected-tree")
    parser.add_argument("--allow-nonwindows", action="store_true",
                        help="allow a non-Windows report for a local schema fixture")
    parser.add_argument("--allow-unverified-provenance", action="store_true",
                        help="skip independent files/git checks; never use for qualification")
    args = parser.parse_args()
    try:
        result = audit(
            args.report, require_windows=not args.allow_nonwindows, root=args.root,
            executable=args.executable, runner_receipt=args.runner_receipt,
            artifact_manifest=args.artifact_manifest, expected_head=args.expected_head,
            expected_tree=args.expected_tree,
            allow_unverified_provenance=args.allow_unverified_provenance,
        )
    except (OSError, ValueError) as error:
        parser.exit(2, f"FX Windows timing audit: {error}\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
