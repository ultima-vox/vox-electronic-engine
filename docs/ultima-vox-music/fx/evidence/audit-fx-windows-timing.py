#!/usr/bin/env python3
"""Validate one structured FX-T18 Windows timing capture.

The report is emitted by ``suite_fx_runtime_stress_tests`` when
``VOX_FX_REPORT`` and ``VOX_FX_TIMING_THREAD_CPU`` are set. This checker only
validates provenance, matrix completeness and arithmetic reconciliation. It
never turns a measurement into a release approval: deadline misses, coarse
CPU accounting and any non-zero probe count remain visible in its JSON output.

Usage:
    audit-fx-windows-timing.py <report.tsv>

Use ``--allow-nonwindows`` only for a local schema exercise. A qualification
receipt must be a 64-bit Windows Release capture with both probes available.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
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
    "wall_p50_us", "wall_p95_us", "wall_p99_us", "wall_max_us",
    "cpu_p50_us", "cpu_p95_us", "cpu_p99_us", "cpu_max_us",
    "cpu_at_wall_max_us",
)
BLOCK_FIELDS = (
    "case_id", "block", "wall_us", "cpu_us", "budget_us", "wall_miss",
    "cpu_miss", "events", "grains",
)


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


def _percentiles(values: list[float]) -> tuple[float, float, float, float]:
    ordered = sorted(values)
    return tuple(ordered[index] for index in (79, 151, 158, 159))  # type: ignore[return-value]


def _expected_cases(profile: str) -> set[tuple[int, int, int, str]]:
    voices = (1,) if profile == "typical" else VOICES
    patterns = ("off",) if profile == "typical" else PATTERNS
    return {(rate, frames, voice, pattern)
            for rate in RATES for frames in FRAMES for voice in voices for pattern in patterns}


def audit(path: Path, require_windows: bool = True) -> dict[str, Any]:
    metadata: dict[str, str] = {}
    cases: dict[str, dict[str, Any]] = {}
    blocks: dict[str, list[dict[str, Any]]] = {}

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
            for field in ("budget_us", "min_positive_cpu_us", "peak", "dc", "wall_p50_us",
                          "wall_p95_us", "wall_p99_us", "wall_max_us", "cpu_p50_us",
                          "cpu_p95_us", "cpu_p99_us", "cpu_max_us", "cpu_at_wall_max_us"):
                values[field] = _float(values[field], field)
            cases[case_id] = values
        elif fields[0] == "block":
            if len(fields) != 1 + len(BLOCK_FIELDS):
                raise ValueError(f"{path}:{line_number}: malformed block row")
            values = dict(zip(BLOCK_FIELDS, fields[1:]))
            case_id = values["case_id"]
            values["block"] = _int(values["block"], "block")
            for field in ("wall_us", "cpu_us", "budget_us"):
                values[field] = _float(values[field], field)
            for field in ("wall_miss", "cpu_miss", "events", "grains"):
                values[field] = _int(values[field], field)
            blocks.setdefault(case_id, []).append(values)
        else:
            raise ValueError(f"{path}:{line_number}: unknown record {fields[0]!r}")

    required = {"format", "profile", "source_sha", "platform", "pointer_bits", "timing_clock",
                "timing_requested", "allocation_probe", "lock_probe", "wait_probe",
                "blocks_per_case"}
    missing = required - metadata.keys()
    if missing:
        raise ValueError(f"{path}: missing metadata: {', '.join(sorted(missing))}")
    if metadata["format"] != "VOX_FX_T18_REPORT_V1":
        raise ValueError(f"{path}: unsupported report format {metadata['format']!r}")
    if not re.fullmatch(r"[0-9a-fA-F]{40}", metadata["source_sha"]):
        raise ValueError(f"{path}: source_sha is not a full commit SHA")
    profile = metadata["profile"]
    if profile not in {"routed", "authored", "typical"}:
        raise ValueError(f"{path}: unsupported profile {profile!r}")
    if metadata["timing_requested"] != "1" or metadata["blocks_per_case"] != str(BLOCKS):
        raise ValueError(f"{path}: CPU timing or block-count request is not recorded")
    if metadata["pointer_bits"] != "64":
        raise ValueError(f"{path}: not a 64-bit capture")
    if require_windows and "windows" not in metadata["platform"].lower():
        raise ValueError(f"{path}: qualification receipt is not Windows: {metadata['platform']!r}")
    if require_windows and metadata["timing_clock"] != "GetThreadTimes":
        raise ValueError(f"{path}: unexpected Windows timing clock {metadata['timing_clock']!r}")
    for probe in ("allocation_probe", "lock_probe", "wait_probe"):
        if metadata[probe] != "available":
            raise ValueError(f"{path}: {probe} is {metadata[probe]!r}, not available")

    expected = _expected_cases(profile)
    observed: set[tuple[int, int, int, str]] = set()
    totals = {"callbacks": 0, "cpu_pairs": 0, "wall_misses": 0, "cpu_misses": 0,
              "wall_only_misses": 0, "alloc_calls": 0, "lock_entries": 0,
              "wait_entries": 0, "zero_cpu_deltas": 0, "accounting_limited_cells": 0}
    deadline_rows: list[dict[str, Any]] = []

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
        if any(not _near(row["budget_us"], budget, 0.001) for row in rows):
            raise ValueError(f"{path}: block deadline mismatch in {case_id}")
        for row in rows:
            wall_miss = int(row["wall_us"] > budget + 0.001)
            cpu_miss = int(row["cpu_us"] >= 0.0 and row["cpu_us"] > budget + 0.001)
            if row["wall_miss"] != wall_miss or row["cpu_miss"] != cpu_miss:
                raise ValueError(f"{path}: miss flag mismatch in {case_id} block {row['block']}")
            if row["cpu_us"] < -1.0 or row["wall_us"] < 0.0:
                raise ValueError(f"{path}: invalid negative timing in {case_id} block {row['block']}")
            if wall_miss or cpu_miss:
                deadline_rows.append({"case_id": case_id, **row})

        wall_p = _percentiles(wall)
        cpu_p = _percentiles(cpu) if len(cpu) == BLOCKS else None
        if any(not _near(case[name], actual) for name, actual in zip(
                ("wall_p50_us", "wall_p95_us", "wall_p99_us", "wall_max_us"), wall_p)):
            raise ValueError(f"{path}: wall percentile mismatch in {case_id}")
        if cpu_p is None:
            if case["cpu_pairs"] != len(cpu):
                raise ValueError(f"{path}: CPU pair count mismatch in {case_id}")
        elif any(not _near(case[name], actual) for name, actual in zip(
                ("cpu_p50_us", "cpu_p95_us", "cpu_p99_us", "cpu_max_us"), cpu_p)):
            raise ValueError(f"{path}: CPU percentile mismatch in {case_id}")
        if case["cpu_pairs"] != len(cpu) or case["zero_cpu_deltas"] != sum(value == 0.0 for value in cpu):
            raise ValueError(f"{path}: CPU accounting counters do not reconcile in {case_id}")
        positive = [value for value in cpu if value > 0.0]
        expected_min = min(positive, default=0.0)
        if not _near(case["min_positive_cpu_us"], expected_min):
            raise ValueError(f"{path}: minimum positive CPU counter mismatch in {case_id}")
        max_wall_index = max(range(BLOCKS), key=lambda index: wall[index])
        if cpu_by_block[max_wall_index] >= 0.0 and not _near(case["cpu_at_wall_max_us"], cpu_by_block[max_wall_index]):
            raise ValueError(f"{path}: CPU-at-wall-max mismatch in {case_id}")
        wall_misses = sum(row["wall_miss"] for row in rows)
        cpu_misses = sum(row["cpu_miss"] for row in rows)
        wall_only = sum(row["wall_miss"] and not row["cpu_miss"] for row in rows)
        if (case["wall_misses"], case["cpu_misses"], case["wall_only_misses"]) != (wall_misses, cpu_misses, wall_only):
            raise ValueError(f"{path}: deadline counters do not reconcile in {case_id}")
        for name in totals:
            if name == "callbacks":
                totals[name] += BLOCKS
            elif name == "cpu_pairs":
                totals[name] += case["cpu_pairs"]
            elif name == "wall_misses":
                totals[name] += case["wall_misses"]
            elif name == "cpu_misses":
                totals[name] += case["cpu_misses"]
            elif name == "wall_only_misses":
                totals[name] += case["wall_only_misses"]
            elif name == "alloc_calls":
                totals[name] += case["alloc_calls"]
            elif name == "lock_entries":
                totals[name] += case["lock_entries"]
            elif name == "wait_entries":
                totals[name] += case["wait_entries"]
            elif name == "zero_cpu_deltas":
                totals[name] += case["zero_cpu_deltas"]
        if len(cpu) != BLOCKS or case["zero_cpu_deltas"] or case["min_positive_cpu_us"] <= 0.0:
            totals["accounting_limited_cells"] += 1

    if observed != expected:
        missing = sorted(expected - observed)
        extra = sorted(observed - expected)
        raise ValueError(f"{path}: matrix mismatch; missing={missing}, extra={extra}")
    unknown_block_cases = set(blocks) - set(cases)
    if unknown_block_cases:
        raise ValueError(f"{path}: block rows have unknown cases: {sorted(unknown_block_cases)}")

    return {
        "qualification": "diagnostic only; TL must review hardware load, timing resolution and host evidence",
        "report": str(path),
        "profile": profile,
        "cells": len(cases),
        "totals": totals,
        "deadline_rows": deadline_rows,
        "metadata": metadata,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--allow-nonwindows", action="store_true",
                        help="allow a non-Windows report for local schema testing")
    args = parser.parse_args()
    try:
        result = audit(args.report, require_windows=not args.allow_nonwindows)
    except (OSError, ValueError) as error:
        parser.exit(2, f"FX Windows timing audit: {error}\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
