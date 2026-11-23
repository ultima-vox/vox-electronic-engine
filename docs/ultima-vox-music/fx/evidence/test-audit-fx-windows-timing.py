#!/usr/bin/env python3
"""Small local regression fixtures for the FX-T18 auditor.

These are schema fixtures only. They intentionally use Linux metadata and
unverified provenance, and therefore cannot be qualification evidence.
"""
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("fx_t18_audit", HERE / "audit-fx-windows-timing.py")
assert SPEC and SPEC.loader
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def report_text() -> str:
    metadata = {
        "format": "VOX_FX_T18_REPORT_V1",
        "profile": "routed",
        "source_sha": "a" * 40,
        "source_head": "a" * 40,
        "source_tree": "b" * 40,
        "executable_path": "fixture.exe",
        "executable_sha256": "c" * 64,
        "runner_receipt_sha256": "d" * 64,
        "platform": "Linux fixture only",
        "pointer_bits": "64",
        "timing_clock": "CLOCK_THREAD_CPUTIME_ID",
        "timing_requested": "1",
        "timing_bracket": "cpu_start,wall_start,processBlock,wall_end,cpu_end",
        "wall_timer_overhead_us": "0",
        "cpu_timer_overhead_us": "0",
        "timer_calibration_samples": "128",
        "allocation_probe": "available",
        "lock_probe": "available",
        "wait_probe": "available",
        "lock_target_count": "6",
        "lock_resolved_target_count": "6",
        "lock_matched_slot_count": "1",
        "lock_patched_slot_count": "1",
        "wait_target_count": "8",
        "wait_resolved_target_count": "8",
        "wait_matched_slot_count": "1",
        "wait_patched_slot_count": "1",
        "lock_target_count_total": "14",
        "lock_resolved_target_count_total": "14",
        "lock_loaded_module_count": "1",
        "lock_matched_slot_count_total": "2",
        "lock_patched_slot_count_total": "2",
        "lock_patch_failure_count": "0",
        "lock_module_enumeration_complete": "1",
        "lock_positive_control_passed": "1",
        "lock_positive_control_entries": "1",
        "wait_positive_control_entries": "1",
        "lock_coverage_complete": "1",
        "blocks_per_case": "160",
        "sentinel_cases": "r96000_f64_v1_poff,r44100_f512_v8_pon",
    }
    lines = ["# fixture", *(f"meta\t{k}\t{v}" for k, v in metadata.items())]
    lines += [
        "schema\tcase\tfixture",
        "schema\tblock\tfixture",
        "schema\tmiss\tfixture",
    ]
    for rate in (44100, 48000, 96000):
        for frames in (64, 128, 512):
            for voices in (1, 4, 8):
                for pattern in ("off", "on"):
                    case_id = f"r{rate}_f{frames}_v{voices}_p{pattern}"
                    budget = frames * 1_000_000.0 / rate
                    values = [
                        case_id, "routed", str(rate), str(frames), str(voices), pattern,
                        "160", f"{budget:.17f}", "0", "0", "0", "160", "0", f"{budget:.17f}",
                        "0", "0", "0", "0", "0", "0",
                        f"{budget:.17f}", f"{budget:.17f}", f"{budget:.17f}", f"{budget:.17f}", f"{budget:.17f}",
                        f"{budget:.17f}", f"{budget:.17f}", f"{budget:.17f}", f"{budget:.17f}", f"{budget:.17f}", f"{budget:.17f}",
                    ]
                    lines.append("case\t" + "\t".join(values))
                    for block in range(160):
                        lines.append(
                            f"block\t{case_id}\t{block}\t{budget:.17f}\t{budget:.17f}\t"
                            f"{budget:.17f}\t0\t0\t0\t0"
                        )
    return "\n".join(lines) + "\n"


def expect_failure(text: str, needle: str) -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "fixture.tsv"
        path.write_text(text, encoding="utf-8")
        try:
            AUDIT.audit(path, require_windows=False, allow_unverified_provenance=True)
        except ValueError as error:
            if needle not in str(error):
                raise AssertionError(f"expected {needle!r}, got {error!s}")
        else:
            raise AssertionError(f"fixture unexpectedly passed: {needle}")


def main() -> int:
    valid = report_text()
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "valid.tsv"
        path.write_text(valid, encoding="utf-8")
        result = AUDIT.audit(path, require_windows=False, allow_unverified_provenance=True)
        assert not result["performance_failure"]

    # Exact equality is not a miss; the auditor and C++ fixture both use >.
    first_block = next(line for line in valid.splitlines() if line.startswith("block\t"))
    fields = first_block.split("\t")
    fields[3] = f"{float(fields[5]) + 0.001:.9f}"
    expect_failure(valid.replace(first_block, "\t".join(fields), 1), "miss flag mismatch")

    # Instrumentation cannot silently degrade into a qualifying zero.
    expect_failure(valid.replace("meta\tlock_coverage_complete\t1", "meta\tlock_coverage_complete\t0", 1),
                   "instrumentation is incomplete")

    # Explicit raw miss rows must reconcile with the block rows.
    expect_failure(valid + "miss\tr44100_f64_v1_poff\t0\t0\t0\t0\t1\t0\t0\t0\n",
                   "explicit miss rows")
    print("FX-T18 auditor negative fixtures: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
