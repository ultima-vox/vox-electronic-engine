#!/usr/bin/env python3
"""Small local regression fixtures for the FX-T18 auditor.

These are schema fixtures only. They intentionally use Linux metadata and
unverified provenance, and therefore cannot be qualification evidence.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
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


def make_provenance_fixture(text: str, directory: Path, head: str, tree: str) -> tuple[Path, Path, Path, Path]:
    executable = directory / "fixture.exe"
    executable.write_bytes(b"fixture executable")
    executable_sha = hashlib.sha256(executable.read_bytes()).hexdigest()
    report = directory / "fixture.tsv"
    receipt = directory / "runner.json"
    manifest = directory / "manifest.json"
    configure_log = directory / "configure.txt"
    build_log = directory / "build.txt"
    inventory = directory / "inventory.json"
    juce_dir = directory / ("juce-" + head[:8])
    juce_dir.mkdir()
    subprocess.run(["git", "init", "-q", str(juce_dir)], check=True)
    (juce_dir / "pinned.txt").write_text("fixture JUCE\n", encoding="utf-8")
    subprocess.run(["git", "-C", str(juce_dir), "add", "pinned.txt"], check=True)
    subprocess.run(["git", "-C", str(juce_dir), "-c", "user.name=fixture", "-c",
                    "user.email=fixture@example.invalid", "commit", "-q", "-m", "fixture"], check=True)
    juce_commit = subprocess.check_output(["git", "-C", str(juce_dir), "rev-parse", "HEAD"], text=True).strip()
    configure_log.write_text("configure PASS\n", encoding="utf-8")
    build_log.write_text("build PASS\n", encoding="utf-8")
    inventory.write_text(json.dumps({
        "tests": [{"name": "suite_fx_runtime_stress_tests", "command": [str(executable)]}]
    }), encoding="utf-8")
    text = text.replace("meta\tsource_sha\t" + "a" * 40, "meta\tsource_sha\t" + head)
    text = text.replace("meta\tsource_head\t" + "a" * 40, "meta\tsource_head\t" + head)
    text = text.replace("meta\tsource_tree\t" + "b" * 40, "meta\tsource_tree\t" + tree)
    text = text.replace("meta\texecutable_path\tfixture.exe", "meta\texecutable_path\t" + str(executable))
    text = text.replace("meta\texecutable_sha256\t" + "c" * 64,
                        "meta\texecutable_sha256\t" + executable_sha)
    receipt.write_text(json.dumps({
        "format": "VOX_FX_T18_RUNNER_RECEIPT_V1",
        "scenario": "routed",
        "source_head": head,
        "source_tree": tree,
        "executable_path": str(executable),
        "executable_sha256": executable_sha,
        "report_path": str(report),
        "runner": "fixture-runner",
        "build_succeeded": True,
        "source_tracked_clean_before_build": True,
        "source_tracked_clean_after_build": True,
        "build_config": "Release",
        "configure_exit_code": 0,
        "build_exit_code": 0,
        "build_fresh_clean_first": True,
        "configure_log_path": str(configure_log),
        "build_log_path": str(build_log),
        "ctest_timeout_seconds": 180,
        "ctest_inventory_path": str(inventory),
        "ctest_selected_executable": str(executable),
        "juce_dir": str(juce_dir),
        "juce_commit": juce_commit,
        "juce_tracked_clean": True,
    }), encoding="utf-8")
    receipt_sha = hashlib.sha256(receipt.read_bytes()).hexdigest()
    text = text.replace("meta\trunner_receipt_sha256\t" + "d" * 64,
                        "meta\trunner_receipt_sha256\t" + receipt_sha)
    report.write_text(text, encoding="utf-8")
    files = []
    for artifact in (report, receipt, executable, configure_log, build_log, inventory):
        files.append({"path": str(artifact), "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest()})
    manifest.write_text(json.dumps({"format": "VOX_FX_T18_ARTIFACTS_V1", "files": files}), encoding="utf-8")
    return report, receipt, executable, manifest


def expect_provenance_failure(paths: tuple[Path, Path, Path, Path], root: Path, needle: str) -> None:
    report, receipt, executable, manifest = paths
    try:
        AUDIT.audit(report, require_windows=False, root=root, executable=executable,
                    runner_receipt=receipt, artifact_manifest=manifest)
    except ValueError as error:
        if needle not in str(error):
            raise AssertionError(f"expected {needle!r}, got {error!s}")
    else:
        raise AssertionError(f"provenance fixture unexpectedly passed: {needle}")


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
    fields[3] = f"{float(fields[5]) + 0.001:.17f}"
    expect_failure(valid.replace(first_block, "\t".join(fields), 1), "miss flag mismatch")

    # Instrumentation cannot silently degrade into a qualifying zero.
    expect_failure(valid.replace("meta\tlock_coverage_complete\t1", "meta\tlock_coverage_complete\t0", 1),
                   "instrumentation is incomplete")

    # Explicit raw miss rows must reconcile with the block rows.
    expect_failure(valid + "miss\tr44100_f64_v1_poff\t0\t0\t0\t0\t1\t0\t0\t0\n",
                   "explicit miss rows")

    # Dropped and duplicated callback rows cannot be hidden by a valid-looking
    # case aggregate.
    expect_failure(valid.replace(first_block + "\n", "", 1), "exactly 160 unique block rows")
    expect_failure(valid.replace(first_block, first_block + "\n" + first_block, 1),
                   "exactly 160 unique block rows")

    # A well-formed but wrong percentile is still rejected.
    first_case = next(line for line in valid.splitlines() if line.startswith("case\t"))
    case_fields = first_case.split("\t")
    case_fields[23] = f"{float(case_fields[23]) + 1.0:.17f}"
    expect_failure(valid.replace(first_case, "\t".join(case_fields), 1), "percentile mismatch")

    # Forge a self-consistent receipt/manifest with a spoofed source identity;
    # independent git inspection must reject it. Then verify an independently
    # valid receipt and reject a post-receipt EXE modification.
    root = HERE.parents[3]
    head = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    tree = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD^{tree}"], text=True).strip()
    with tempfile.TemporaryDirectory() as directory:
        directory_path = Path(directory)
        forged = make_provenance_fixture(valid, directory_path, "e" * 40, "f" * 40)
        expect_provenance_failure(forged, root, "report source HEAD/tree")
        valid_provenance = make_provenance_fixture(valid, directory_path, head, tree)
        verified = AUDIT.audit(valid_provenance[0], require_windows=False, root=root,
                               executable=valid_provenance[2], runner_receipt=valid_provenance[1],
                               artifact_manifest=valid_provenance[3], expected_head=head,
                               expected_tree=tree)
        assert verified["provenance"]["verified"]
        valid_provenance[2].write_bytes(b"modified executable")
        expect_provenance_failure(valid_provenance, root, "executable SHA-256")

    print("FX-T18 auditor negative fixtures: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
