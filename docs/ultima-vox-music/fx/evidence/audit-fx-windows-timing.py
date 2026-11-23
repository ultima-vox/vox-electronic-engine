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
import xml.etree.ElementTree as ET
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
    "timer_calibration_samples", "cpu_running_time_measured", "cpu_resolution",
    "cpu_attribution", "cpu_measurement_status", "cpu_zero_delta_semantics",
    "allocation_probe", "lock_probe",
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


def _quantized(value: float, resolution: float) -> bool:
    if resolution <= 0.0 or value < 0.0:
        return False
    quotient = value / resolution
    return abs(quotient - round(quotient)) <= 1.0e-6 + abs(quotient) * 1.0e-6


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


def _manifest_contains(hashes: dict[str, str], target: str | Path) -> bool:
    return any(_same_path(path, target) for path in hashes)


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
    if any(not _manifest_contains(verified, item) for item in required):
        raise ValueError(f"artifact manifest {path} does not cover report, receipt and executable")
    return {"path": str(path), "files": len(files), "hashes": verified}


def audit_ctest_inventory(path: Path, executable: Path | None = None) -> dict[str, Any]:
    """Check the UTF-8/BOM CTest inventory without requiring a timing report."""
    try:
        inventory = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid CTest inventory {path}: {error}") from error
    if not isinstance(inventory, dict) or not isinstance(inventory.get("tests"), list):
        raise ValueError(f"{path}: CTest inventory has no tests list")
    tests = [item for item in inventory["tests"]
             if isinstance(item, dict) and item.get("name") == "suite_fx_runtime_stress_tests"]
    if len(tests) != 1:
        raise ValueError(f"{path}: expected exactly one FX stress test")
    command = tests[0].get("command")
    if not isinstance(command, list):
        raise ValueError(f"{path}: FX stress test has no command list")
    commands = [str(value) for value in command]
    executable_commands = [value for value in commands if value.lower().endswith(".exe")]
    if not executable_commands:
        raise ValueError(f"{path}: inventory has no executable command")
    if executable is not None and not any(_same_path(value, executable)
                                          for value in executable_commands):
        raise ValueError(f"{path}: inventory executable does not match {executable}")
    return {"format": "VOX_FX_CTEST_INVENTORY_SMOKE_V1", "path": str(path),
            "tests": len(tests), "executable": executable_commands[0]}


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
                  "executable_sha256", "report_path", "runner", "build_succeeded",
                  "source_tracked_clean_before_build", "source_tracked_clean_after_build",
                  "build_config", "configure_exit_code", "build_exit_code",
                  "build_fresh_clean_first", "configure_log_path", "build_log_path",
                  "ctest_timeout_seconds", "ctest_inventory_path", "ctest_junit_path",
                  "ctest_selected_executable", "juce_dir", "juce_commit",
                  "juce_tracked_clean"):
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
    if receipt["build_succeeded"] is not True \
            or receipt["source_tracked_clean_before_build"] is not True \
            or receipt["source_tracked_clean_after_build"] is not True \
            or receipt["build_fresh_clean_first"] is not True:
        raise ValueError("runner receipt does not attest a clean fresh build")
    if receipt["build_config"] != "Release" \
            or int(receipt["configure_exit_code"]) != 0 \
            or int(receipt["build_exit_code"]) != 0 \
            or int(receipt["ctest_timeout_seconds"]) != 180:
        raise ValueError("runner receipt does not attest a successful fresh Release build with the 180-second CTest bound")
    if receipt["juce_tracked_clean"] is not True \
            or not re.fullmatch(r"[0-9a-fA-F]{40}", str(receipt["juce_commit"])):
        raise ValueError("runner receipt does not attest a clean pinned JUCE checkout")
    juce_dir = Path(receipt["juce_dir"])
    if _git(juce_dir, "rev-parse", "HEAD").casefold() != str(receipt["juce_commit"]).casefold():
        raise ValueError("runner receipt JUCE commit does not match independently read JUCE HEAD")
    if _git(juce_dir, "status", "--porcelain=v1", "--untracked-files=all"):
        raise ValueError("pinned JUCE checkout has unexpected changes")

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
    for field in ("configure_log_path", "build_log_path", "ctest_inventory_path", "ctest_junit_path"):
        artifact = Path(receipt[field])
        if not artifact.is_file() or not _manifest_contains(manifest_result["hashes"], artifact):
            raise ValueError(f"runner receipt artifact is missing or unhashed: {field}")
    try:
        ET.parse(receipt["ctest_junit_path"])
    except (OSError, ET.ParseError) as error:
        raise ValueError(f"invalid CTest JUnit artifact in runner receipt: {error}") from error
    if not _same_path(receipt["ctest_selected_executable"], executable):
        raise ValueError("runner receipt CTest executable does not match the hashed executable")
    audit_ctest_inventory(Path(receipt["ctest_inventory_path"]), executable)
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

    cpu_running_time_measured = _int(metadata["cpu_running_time_measured"], "cpu_running_time_measured")
    if cpu_running_time_measured not in (0, 1):
        raise ValueError(f"{path}: cpu_running_time_measured is not boolean")
    cpu_resolution = _float(metadata["cpu_resolution"], "cpu_resolution")
    if cpu_resolution < 0.0:
        raise ValueError(f"{path}: cpu_resolution is negative")
    cpu_attribution = metadata["cpu_attribution"]
    cpu_measurement_status = metadata["cpu_measurement_status"]
    if cpu_measurement_status not in {"MEASURED", "CPU_MEASUREMENT_INSUFFICIENT"}:
        raise ValueError(f"{path}: unsupported CPU measurement status {cpu_measurement_status!r}")
    if cpu_measurement_status == "MEASURED":
        if cpu_attribution != "MEASURED" or cpu_running_time_measured != 1 or cpu_resolution <= 0.0:
            raise ValueError(f"{path}: measured CPU status lacks a usable resolution/attribution")
    else:
        if cpu_attribution != "UNKNOWN":
            raise ValueError(f"{path}: insufficient CPU status is not marked UNKNOWN attribution")
    if metadata["cpu_zero_delta_semantics"] != "UNKNOWN_NOT_ZERO_WORK":
        raise ValueError(f"{path}: zero CPU deltas must not be labeled zero work")
    cpu_measurement_insufficient = cpu_measurement_status == "CPU_MEASUREMENT_INSUFFICIENT"

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
    cpu_values: list[float] = []
    zero_cpu_work_rows = 0

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
            cpu_values.append(row["cpu_us"])
            if row["cpu_us"] == 0.0 and (row["events"] > 0 or row["grains"] > 0):
                zero_cpu_work_rows += 1
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

    minimum_budget = min(case["budget_us"] for case in cases.values())
    if not cpu_measurement_insufficient and cpu_running_time_measured == 1 \
            and cpu_resolution >= minimum_budget:
        raise ValueError(f"{path}: measured CPU attribution uses a resolution too coarse for the callback budget")
    if cpu_measurement_insufficient:
        positive_cpu = [value for value in cpu_values if value > 0.0]
        if cpu_resolution > 0.0 and positive_cpu:
            if not all(_quantized(value, cpu_resolution) for value in positive_cpu):
                raise ValueError(f"{path}: raw CPU deltas are not quantized at the declared resolution")
            if not _near(min(positive_cpu), cpu_resolution, absolute=0.02):
                raise ValueError(f"{path}: cpu_resolution is not the observed minimum positive raw delta")
            if cpu_running_time_measured == 1 and cpu_resolution < minimum_budget:
                raise ValueError(f"{path}: insufficient CPU status is inconsistent with its callback resolution")
        if totals["zero_cpu_deltas"] > 0 and zero_cpu_work_rows == 0:
            raise ValueError(f"{path}: zero CPU deltas have no retained nonzero-work rows")

    # Raw CPU flags remain in the report, but a coarse/unknown clock cannot
    # turn those flags into demonstrated CPU deadline violations.
    cpu_deadline_failure = totals["cpu_misses"] > 0 and not cpu_measurement_insufficient
    performance_failure = any(totals[name] > 0 for name in (
        "wall_misses", "alloc_calls", "lock_entries", "wait_entries")) or cpu_deadline_failure
    return {
        "qualification": "diagnostic only; TL must review hardware load, timing resolution and host evidence",
        "report": str(path),
        "profile": profile,
        "cells": len(cases),
        "performance_failure": performance_failure,
        "cpu_measurement_insufficient": cpu_measurement_insufficient,
        "diagnostic_failure": cpu_measurement_insufficient,
        "cpu_attribution": cpu_attribution,
        "cpu_resolution": cpu_resolution,
        "totals": totals,
        "deadline_rows": deadline_rows,
        "metadata": metadata,
        "provenance": provenance,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, nargs="?")
    parser.add_argument("--check-ctest-inventory", type=Path,
                        help="audit only a UTF-8/BOM CTest JSON inventory")
    parser.add_argument("--inventory-executable", type=Path)
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
        if args.check_ctest_inventory is not None:
            result = audit_ctest_inventory(args.check_ctest_inventory, args.inventory_executable)
        else:
            if args.report is None:
                parser.error('a report is required unless --check-ctest-inventory is used')
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
