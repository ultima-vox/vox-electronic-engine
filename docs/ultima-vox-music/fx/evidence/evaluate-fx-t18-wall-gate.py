#!/usr/bin/env python3
"""Evaluate the proposed FX-T18 wall-only gate.

This evaluator fails closed.  A wall-only diagnostic status requires complete
native CTest outcome evidence, timestamped host-wide observation covering every
profile interval, independently verified artifacts/provenance, and all 117
cells.  ``--allow-unverified-provenance`` is a schema-fixture mode and is
persistently marked qualification-prohibited; it can never emit a production
PASS status.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re
from typing import Any

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "fx_t18_audit", HERE / "audit-fx-windows-timing.py")
assert SPEC and SPEC.loader
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)

PROFILES = {"routed": 54, "authored": 54, "typical": 9}
REQUIRED_PHASES = ("pre", "during", "post")
MAX_SAMPLE_GAP_SECONDS = 2.0
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")


def _bool(value: Any, name: str) -> bool:
    if type(value) is not bool:
        raise ValueError(f"{name} is not boolean")
    return value


def _int(value: Any, name: str) -> int:
    if type(value) is not int:
        raise ValueError(f"{name} is not an integer")
    return value


def _read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid {label} {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{path}: {label} is not an object")
    return value


def _utc(value: Any, name: str) -> datetime:
    if not isinstance(value, str) or not value.endswith("Z"):
        raise ValueError(f"{name} is not an explicit UTC timestamp")
    try:
        parsed = datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError as error:
        raise ValueError(f"{name} is not ISO-8601 UTC: {value!r}") from error
    if parsed.tzinfo is None or parsed.utcoffset() != timezone.utc.utcoffset(parsed):
        raise ValueError(f"{name} is not UTC: {value!r}")
    return parsed.astimezone(timezone.utc)


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


def _verify_host_manifest(path: Path, witness: Path) -> dict[str, Any]:
    manifest = _read_json(path, "host artifact manifest")
    if manifest.get("format") != "VOX_FX_T18_HOST_ARTIFACTS_V1":
        raise ValueError(f"unsupported host artifact manifest format in {path}")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise ValueError(f"{path}: host manifest has no files")
    hashes: dict[str, str] = {}
    for entry in files:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str) \
                or not isinstance(entry.get("sha256"), str) \
                or not SHA256_RE.fullmatch(entry["sha256"]):
            raise ValueError(f"{path}: malformed host artifact entry")
        artifact = Path(entry["path"])
        if not artifact.is_absolute():
            artifact = path.parent / artifact
        if not artifact.is_file():
            raise ValueError(f"{path}: missing host artifact {artifact}")
        actual = _sha256(artifact)
        if actual.casefold() != entry["sha256"].casefold():
            raise ValueError(f"{path}: host artifact hash mismatch for {artifact}")
        canonical = str(artifact.absolute()).casefold()
        if canonical in hashes:
            raise ValueError(f"{path}: duplicate host artifact {artifact}")
        hashes[canonical] = actual
    witness_value = _read_json(witness, "host witness")
    required_paths: list[Path] = [witness]
    for container, field in (
            (witness_value, "identity_policy_path"),
            (witness_value.get("runner_lease", {}), "verification_receipt_path"),
            (witness_value.get("event_trace", {}), "loss_receipt_path")):
        artifact_path = container.get(field) if isinstance(container, dict) else None
        if not isinstance(artifact_path, str) or not artifact_path:
            raise ValueError(f"{witness}: host attestation path {field} is missing")
        required_paths.append(Path(artifact_path))
    for required_path in required_paths:
        resolved_required = required_path if required_path.is_absolute() else witness.parent / required_path
        if not _manifest_contains(hashes, resolved_required):
            raise ValueError(f"{path}: host manifest does not cover {resolved_required}")
    return {"verified": True, "path": str(path), "files": len(files),
            "hashes": hashes, "required_artifacts": [str(item) for item in required_paths]}


def _validate_process_identity(process: Any, label: str) -> None:
    if not isinstance(process, dict) \
            or type(process.get("pid")) is not int \
            or not isinstance(process.get("process_name"), str):
        raise ValueError(f"{label}: malformed process identity")
    if process.get("identity_known") is not True:
        raise ValueError(f"{label}: process identity is unknown")
    if process.get("approved_by_policy") is not True:
        raise ValueError(f"{label}: process identity is not approved by the owner policy")
    if process.get("command_line") not in (None, "<redacted>"):
        raise ValueError(f"{label}: command-line secret was not redacted")
    if "job_identity" in process and process["job_identity"] not in (None, "<redacted>", "<redacted-or-not-applicable>"):
        raise ValueError(f"{label}: unprotected job identity")
    if "service_identity" in process and process["service_identity"] not in (None, "<redacted>", "<redacted-or-not-applicable>"):
        raise ValueError(f"{label}: unprotected service identity")


def _verify_external_witness_attestations(
    path: Path, value: dict[str, Any], capture_start: datetime,
    capture_end: datetime, policy: dict[str, Any], lease: dict[str, Any],
    event_trace: dict[str, Any], expected_host: str | None,
    expected_lease_id: str | None, expected_policy_sha256: str | None,
) -> list[Path]:
    """Verify the files that make the inline witness claims independently auditable."""
    policy_path_value = value.get("identity_policy_path")
    lease_path_value = lease.get("verification_receipt_path")
    loss_path_value = event_trace.get("loss_receipt_path")
    if not all(isinstance(item, str) and item for item in
               (policy_path_value, lease_path_value, loss_path_value)):
        raise ValueError(f"{path}: external policy, lease and event-loss attestations are required")
    policy_path = Path(policy_path_value)
    lease_path = Path(lease_path_value)
    loss_path = Path(loss_path_value)
    policy_path = policy_path if policy_path.is_absolute() else path.parent / policy_path
    lease_path = lease_path if lease_path.is_absolute() else path.parent / lease_path
    loss_path = loss_path if loss_path.is_absolute() else path.parent / loss_path
    for artifact in (policy_path, lease_path, loss_path):
        if not artifact.is_file():
            raise ValueError(f"{path}: missing host attestation artifact {artifact}")

    actual_policy_hash = _sha256(policy_path)
    if value.get("identity_policy_sha256") != actual_policy_hash:
        raise ValueError(f"{path}: identity policy top-level hash does not match the policy artifact")
    if actual_policy_hash.casefold() != policy["policy_sha256"].casefold():
        raise ValueError(f"{path}: identity policy hash does not match the witness")
    policy_file = _read_json(policy_path, "identity policy")
    if policy_file.get("format") != "VOX_FX_T18_IDENTITY_POLICY_V1" \
            or policy_file.get("owner_approved") is not True \
            or policy_file.get("policy_id") != policy.get("policy_id") \
            or policy_file.get("policy_sha256") not in (None, actual_policy_hash) \
            or policy_file.get("approved_processes") != policy.get("approved_processes") \
            or policy_file.get("denied_process_name_regex") != policy.get("denied_process_name_regex"):
        raise ValueError(f"{path}: identity policy artifact does not match the witness policy")

    actual_lease_hash = _sha256(lease_path)
    if actual_lease_hash.casefold() != str(lease.get("verification_receipt_sha256", "")).casefold():
        raise ValueError(f"{path}: lease verification receipt hash does not match the witness")
    lease_file = _read_json(lease_path, "host lease receipt")
    if lease_file.get("format") != "VOX_FX_T18_HOST_LEASE_RECEIPT_V1" \
            or lease_file.get("verified") is not True \
            or lease_file.get("host") != lease.get("host") \
            or lease_file.get("lease_id") != lease.get("lease_id") \
            or lease_file.get("owner") != lease.get("owner") \
            or lease_file.get("policy_sha256") != policy.get("policy_sha256"):
        raise ValueError(f"{path}: host lease receipt does not reconcile with the witness")
    lease_start = _utc(lease_file.get("valid_from_utc"), "lease.valid_from_utc")
    lease_end = _utc(lease_file.get("valid_until_utc"), "lease.valid_until_utc")
    if lease_end < capture_end or lease_start > capture_start:
        raise ValueError(f"{path}: host lease validity does not cover the witness capture")
    if expected_host and lease_file["host"].casefold() != expected_host.casefold():
        raise ValueError(f"{path}: host lease receipt disagrees with the trusted runner host")
    if expected_lease_id and lease_file["lease_id"] != expected_lease_id:
        raise ValueError(f"{path}: host lease receipt disagrees with the trusted runner lease")
    if expected_policy_sha256 and lease_file["policy_sha256"].casefold() != expected_policy_sha256.casefold():
        raise ValueError(f"{path}: host lease receipt disagrees with the trusted process policy")

    actual_loss_hash = _sha256(loss_path)
    if actual_loss_hash.casefold() != str(event_trace.get("loss_receipt_sha256", "")).casefold():
        raise ValueError(f"{path}: event-loss receipt hash does not match the witness")
    loss_file = _read_json(loss_path, "event-loss receipt")
    if loss_file.get("format") != "VOX_FX_T18_EVENT_LOSS_RECEIPT_V1" \
            or loss_file.get("provider") != "Win32_ProcessStartTrace/Win32_ProcessStopTrace" \
            or loss_file.get("coverage_complete") is not True \
            or loss_file.get("loss_accounting") != "owner_attested" \
            or type(loss_file.get("lost_events")) is not int \
            or loss_file.get("lost_events") != 0:
        raise ValueError(f"{path}: event-loss receipt is incomplete")
    loss_start = _utc(loss_file.get("coverage_start_utc"), "event_loss.coverage_start_utc")
    loss_end = _utc(loss_file.get("coverage_end_utc"), "event_loss.coverage_end_utc")
    if loss_start > capture_start or loss_end < capture_end:
        raise ValueError(f"{path}: event-loss receipt does not cover the witness capture")
    return [policy_path, lease_path, loss_path]


def validate_witness(
    path: Path,
    execution_intervals: dict[str, dict[str, Any]] | None = None,
    expected_host: str | None = None,
    expected_lease_id: str | None = None,
    expected_policy_sha256: str | None = None,
    require_external_attestations: bool = False,
) -> dict[str, Any]:
    """Validate timestamped host-wide snapshots and event evidence.

    A phase label and a declared duration are not evidence of duration.  The
    validator independently checks UTC monotonicity, one-Hz cadence, maximum
    gap, phase sample counts/spans, and overlap of every trusted CTest interval.
    """
    value = _read_json(path, "host witness")
    if value.get("format") != "VOX_FX_T18_HOST_WITNESS_V1":
        raise ValueError(f"{path}: unsupported host witness format")
    capture_start = _utc(value.get("capture_start_utc"), "capture_start_utc")
    capture_end = _utc(value.get("capture_end_utc"), "capture_end_utc")
    if capture_end <= capture_start:
        raise ValueError(f"{path}: host capture interval is not positive")
    if _int(value.get("sample_interval_ms"), "sample_interval_ms") != 1000:
        raise ValueError(f"{path}: host witness sample interval is not 1000 ms")
    pre_seconds = _int(value.get("pre_seconds"), "pre_seconds")
    during_seconds = _int(value.get("during_seconds"), "during_seconds")
    post_seconds = _int(value.get("post_seconds"), "post_seconds")
    if pre_seconds < 30 or post_seconds < 30 or during_seconds < 1:
        raise ValueError(f"{path}: host witness lacks declared phase windows")
    if value.get("host_observation_mode") != "process_snapshot_and_event_trace":
        raise ValueError(f"{path}: host-wide observation mode is not declared")
    if value.get("monitor_gaps") not in ([], None):
        raise ValueError(f"{path}: host witness has polling gaps")
    declared_duration = pre_seconds + during_seconds + post_seconds
    if (capture_end - capture_start).total_seconds() < declared_duration:
        raise ValueError(f"{path}: capture interval is shorter than the declared witness windows")

    policy = value.get("identity_policy")
    if not isinstance(policy, dict) \
            or policy.get("format") != "VOX_FX_T18_IDENTITY_POLICY_V1" \
            or policy.get("owner_approved") is not True \
            or not isinstance(policy.get("policy_id"), str) or not policy["policy_id"] \
            or not isinstance(policy.get("approved_processes"), list) \
            or not isinstance(policy.get("denied_process_name_regex"), list) \
            or not isinstance(policy.get("policy_sha256"), str) \
            or not SHA256_RE.fullmatch(policy["policy_sha256"]):
        raise ValueError(f"{path}: explicit owner-approved process identity policy is missing")
    approved_processes = policy["approved_processes"]
    approved_keys: set[tuple[int, str]] = set()
    for policy_index, approved in enumerate(approved_processes):
        if not isinstance(approved, dict) or type(approved.get("pid")) is not int \
                or not isinstance(approved.get("process_name"), str) \
                or not approved["process_name"]:
            raise ValueError(f"{path}: malformed approved process policy entry {policy_index}")
        approved_keys.add((approved["pid"], approved["process_name"]))
    if not approved_keys:
        raise ValueError(f"{path}: approved process identity policy is empty")
    denied_patterns = []
    for pattern_index, pattern in enumerate(policy["denied_process_name_regex"]):
        if not isinstance(pattern, str):
            raise ValueError(f"{path}: malformed denied process policy pattern {pattern_index}")
        try:
            denied_patterns.append(re.compile(pattern, re.IGNORECASE))
        except re.error as error:
            raise ValueError(f"{path}: invalid denied process policy pattern {pattern!r}: {error}") from error
    if expected_policy_sha256 and policy["policy_sha256"].casefold() != expected_policy_sha256.casefold():
        raise ValueError(f"{path}: witness process policy does not match the trusted receipts")

    event_trace = value.get("event_trace")
    if not isinstance(event_trace, dict) \
            or event_trace.get("available") is not True \
            or not (event_trace.get("coverage_complete") is True
                    or (type(event_trace.get("coverage_complete")) is int
                        and event_trace.get("coverage_complete") == 1)) \
            or type(event_trace.get("lost_events")) is not int \
            or event_trace.get("lost_events") != 0 \
            or event_trace.get("loss_accounting") != "owner_attested":
        raise ValueError(f"{path}: event loss/completeness is not independently attested")

    samples = value.get("samples")
    if not isinstance(samples, list) or not samples:
        raise ValueError(f"{path}: host witness has no samples")
    phase_samples: dict[str, list[tuple[datetime, dict[str, Any]]]] = {phase: [] for phase in REQUIRED_PHASES}
    phase_order = {phase: index for index, phase in enumerate(REQUIRED_PHASES)}
    previous_phase = -1
    all_times: list[datetime] = []
    for index, sample in enumerate(samples):
        if not isinstance(sample, dict) or sample.get("phase") not in phase_samples:
            raise ValueError(f"{path}: malformed host sample {index}")
        phase = sample["phase"]
        if phase_order[phase] < previous_phase:
            raise ValueError(f"{path}: host phases are out of order")
        previous_phase = phase_order[phase]
        timestamp = _utc(sample.get("timestamp_utc"), f"samples[{index}].timestamp_utc")
        if timestamp < capture_start or timestamp > capture_end:
            raise ValueError(f"{path}: sample lies outside capture interval")
        processes = sample.get("processes")
        if not isinstance(processes, list):
            raise ValueError(f"{path}: sample {index} has no process list")
        for process_index, process in enumerate(processes):
            label = f"samples[{index}].processes[{process_index}]"
            _validate_process_identity(process, label)
            identity = (process["pid"], process["process_name"])
            if identity not in approved_keys or any(
                    pattern.search(process["process_name"]) for pattern in denied_patterns):
                raise ValueError(f"{path}: {label} is not covered by the owner identity policy")
        phase_samples[phase].append((timestamp, sample))
        all_times.append(timestamp)
    if any(right <= left for left, right in zip(all_times, all_times[1:])):
        raise ValueError(f"{path}: sample timestamps are not strictly increasing")
    gaps = [(right - left).total_seconds() for left, right in zip(all_times, all_times[1:])]
    if any(gap > MAX_SAMPLE_GAP_SECONDS for gap in gaps):
        raise ValueError(f"{path}: host sample cadence has a gap larger than {MAX_SAMPLE_GAP_SECONDS:g}s")
    if any(gap <= 0.0 for gap in gaps):
        raise ValueError(f"{path}: host sample cadence is not positive")
    for phase, declared_seconds in (("pre", pre_seconds), ("during", during_seconds), ("post", post_seconds)):
        entries = phase_samples[phase]
        if len(entries) < declared_seconds:
            raise ValueError(f"{path}: {phase} has {len(entries)} samples, expected at least {declared_seconds}")
        span = (entries[-1][0] - entries[0][0]).total_seconds()
        if span < declared_seconds:
            raise ValueError(f"{path}: {phase} timestamp span is shorter than its declaration")

    events = value.get("process_events", [])
    if not isinstance(events, list):
        raise ValueError(f"{path}: malformed process event list")
    event_times: list[datetime] = []
    for index, event in enumerate(events):
        if not isinstance(event, dict) or event.get("event") not in {"start", "stop", "unparsed"}:
            raise ValueError(f"{path}: malformed process event {index}")
        label = f"process_events[{index}]"
        _validate_process_identity(event, label)
        identity = (event["pid"], event["process_name"])
        if identity not in approved_keys or any(
                pattern.search(event["process_name"]) for pattern in denied_patterns):
            raise ValueError(f"{path}: {label} is not covered by the owner identity policy")
        event_time = _utc(event.get("timestamp_utc"), f"process_events[{index}].timestamp_utc")
        if event_time < capture_start or event_time > capture_end:
            raise ValueError(f"{path}: process event lies outside capture interval")
        event_times.append(event_time)
    if any(right < left for left, right in zip(event_times, event_times[1:])):
        raise ValueError(f"{path}: process event timestamps are not monotonic")

    competitors = value.get("unrecognized_competitors")
    unknown_pids = value.get("unknown_pids")
    if not isinstance(competitors, list) or not isinstance(unknown_pids, list):
        raise ValueError(f"{path}: malformed competitor or unknown-PID list")
    if competitors:
        raise ValueError(f"{path}: unrecognized host processes/jobs are present")
    if unknown_pids:
        raise ValueError(f"{path}: unknown process PIDs are present")
    if _bool(value.get("evidence_blocked"), "evidence_blocked"):
        raise ValueError(f"{path}: host witness is already evidence-blocked")

    lease = value.get("runner_lease")
    if not isinstance(lease, dict) \
            or lease.get("recognized") is not True \
            or lease.get("owner_verified") is not True \
            or not isinstance(lease.get("lease_id"), str) or not lease["lease_id"] \
            or not isinstance(lease.get("host"), str) or not lease["host"] \
            or not isinstance(lease.get("verification_receipt_sha256"), str) \
            or not SHA256_RE.fullmatch(lease["verification_receipt_sha256"]):
        raise ValueError(f"{path}: runner lease is not owner-verified")
    for secret_field in ("token", "lease_token", "command_line"):
        if secret_field in lease and lease[secret_field] not in (None, "<redacted>"):
            raise ValueError(f"{path}: sensitive lease field {secret_field} is not redacted")
    if expected_host and lease["host"].casefold() != expected_host.casefold():
        raise ValueError(f"{path}: witness host does not match the trusted runner receipts")
    if expected_lease_id and lease["lease_id"] != expected_lease_id:
        raise ValueError(f"{path}: witness lease does not match the trusted runner receipts")
    services = value.get("runner_service_identities")
    if not isinstance(services, list) or not services:
        raise ValueError(f"{path}: runner service identities are missing")
    if execution_intervals and lease.get("host") is None:
        raise ValueError(f"{path}: execution-bound witness has no host identity")
    external_attestations: list[Path] = []
    if require_external_attestations:
        external_attestations = _verify_external_witness_attestations(
            path, value, capture_start, capture_end, policy, lease, event_trace,
            expected_host, expected_lease_id, expected_policy_sha256)

    intervals = execution_intervals or {}
    if set(intervals) != set(PROFILES):
        raise ValueError(f"{path}: witness is not bound to all routed/authored/typical execution intervals")
    for profile, state in intervals.items():
        start = state.get("test_start")
        end = state.get("test_end")
        if not isinstance(start, datetime) or not isinstance(end, datetime) or end <= start:
            raise ValueError(f"{path}: invalid trusted {profile} test interval")
        if start < capture_start or end > capture_end:
            raise ValueError(f"{path}: witness does not contain the complete {profile} test interval")
        before = [stamp for stamp in all_times if stamp <= start]
        after = [stamp for stamp in all_times if stamp >= end]
        if not before or not after \
                or (start - before[-1]).total_seconds() > MAX_SAMPLE_GAP_SECONDS \
                or (after[0] - end).total_seconds() > MAX_SAMPLE_GAP_SECONDS:
            raise ValueError(f"{path}: sample stream does not cover {profile} CTest interval")
        during_times = [stamp for stamp, _ in phase_samples["during"]]
        if not during_times or during_times[0] > start or during_times[-1] < end:
            raise ValueError(f"{path}: during-phase samples do not cover {profile} CTest interval")

    return {
        "verified": True,
        "path": str(path),
        "samples": len(samples),
        "process_events": len(events),
        "capture_start": capture_start,
        "capture_end": capture_end,
        "policy_sha256": policy["policy_sha256"],
        "host": lease["host"],
        "lease_id": lease["lease_id"],
        "external_attestations": [str(item) for item in external_attestations],
    }


def _scan_observed_failures(path: Path) -> dict[str, bool]:
    """Retain only internally consistent observed failures from a partial report."""
    result = {"wall_failure": False, "safety_failure": False,
              "functional_failure": False, "functional_evidence_invalid": False}
    cases: dict[str, dict[str, Any]] = {}
    functional_rows: dict[str, dict[str, Any]] = {}
    try:
        lines = path.read_text(encoding="utf-8-sig").splitlines()
    except (OSError, UnicodeDecodeError):
        result["functional_evidence_invalid"] = True
        return result
    for line_number, line in enumerate(lines, 1):
        fields = line.split("\t")
        if not fields:
            continue
        if fields[0] == "case" and len(fields) == 1 + len(AUDIT.CASE_FIELDS):
            values = dict(zip(AUDIT.CASE_FIELDS, fields[1:]))
            if values["case_id"] in cases:
                result["functional_evidence_invalid"] = True
                continue
            cases[values["case_id"]] = values
            try:
                result["wall_failure"] |= int(values["wall_misses"]) > 0
                result["safety_failure"] |= any(
                    int(values[field]) > 0 for field in ("alloc_calls", "lock_entries", "wait_entries"))
            except (TypeError, ValueError):
                pass
        elif fields[0] == "functional":
            try:
                functional = AUDIT._parse_functional(fields, line_number, path)
            except ValueError:
                result["functional_evidence_invalid"] = True
                continue
            case_id = functional["case_id"]
            if case_id in functional_rows:
                result["functional_evidence_invalid"] = True
            functional_rows[case_id] = functional
        elif fields[0] == "block" and len(fields) == 1 + len(AUDIT.BLOCK_FIELDS):
            try:
                result["wall_failure"] |= int(fields[6]) == 1
            except (TypeError, ValueError):
                pass

    for case_id, functional in functional_rows.items():
        case = cases.get(case_id)
        if case is None:
            result["functional_evidence_invalid"] = True
            continue
        try:
            initial_voices = int(case["initial_voices"])
            flags = [functional[field] for field, _ in AUDIT.FUNCTIONAL_REASON_ORDER]
            expected_codes = [code for flag, (_, code) in zip(flags, AUDIT.FUNCTIONAL_REASON_ORDER)
                              if flag == 0]
            consistent = (
                functional["reason_codes"] == expected_codes
                and functional["functional_pass"] == int(not expected_codes)
                and (initial_voices == 8 or functional["voice8_grain_invariant"] == 1)
            )
        except (KeyError, TypeError, ValueError):
            consistent = False
        if not consistent:
            result["functional_evidence_invalid"] = True
        elif functional["functional_pass"] == 0:
            result["functional_failure"] = True
    return result


def _load_outcome(path: Path) -> dict[str, Any]:
    outcome = _read_json(path, "post-test outcome")
    required = (
        "format", "scenario", "test_identity", "executable_path", "executable_sha256",
        "test_start_utc", "test_end_utc", "ctest_exit_code", "native_exit_code",
        "ctest_outcome", "execution_complete", "timed_out", "crashed", "skipped",
        "junit_path", "junit_tests", "junit_failures", "junit_errors", "junit_skipped",
        "junit_timeout", "cpu_only_failure", "cpu_only_failure_reason",
    )
    missing = [field for field in required if field not in outcome]
    if missing:
        raise ValueError(f"post-test outcome is missing {', '.join(missing)}")
    if outcome["format"] != "VOX_FX_T18_POST_TEST_OUTCOME_V1":
        raise ValueError("unsupported post-test outcome format")
    for field in ("scenario", "test_identity", "executable_path", "executable_sha256",
                  "junit_path", "ctest_outcome", "cpu_only_failure_reason"):
        if not isinstance(outcome[field], str) or not outcome[field]:
            raise ValueError(f"outcome.{field} is not a non-empty string")
    if not SHA256_RE.fullmatch(outcome["executable_sha256"]):
        raise ValueError("outcome executable SHA-256 is malformed")
    start = _utc(outcome["test_start_utc"], "outcome.test_start_utc")
    end = _utc(outcome["test_end_utc"], "outcome.test_end_utc")
    if end <= start:
        raise ValueError("post-test outcome interval is not positive")
    for field in ("ctest_exit_code", "native_exit_code", "junit_tests", "junit_failures", "junit_errors", "junit_skipped"):
        if type(outcome[field]) is not int:
            raise ValueError(f"outcome.{field} is not an integer")
    for field in ("execution_complete", "timed_out", "crashed", "skipped", "junit_timeout", "cpu_only_failure"):
        if type(outcome[field]) is not bool:
            raise ValueError(f"outcome.{field} is not boolean")
    if outcome["native_exit_code"] != outcome["ctest_exit_code"]:
        raise ValueError("native and CTest exit codes disagree")
    if outcome["cpu_only_failure"] and outcome["cpu_only_failure_reason"] != "CPU_MEASUREMENT_INSUFFICIENT":
        raise ValueError("cpu-only outcome lacks the required reason")
    junit_path = Path(outcome["junit_path"])
    if not junit_path.is_absolute():
        junit_path = path.parent / junit_path
    if not junit_path.is_file():
        raise ValueError(f"post-test outcome JUnit artifact is missing: {junit_path}")
    junit_summary = AUDIT._junit_summary(junit_path)
    for field, actual in (("junit_tests", junit_summary["tests"]),
                          ("junit_failures", junit_summary["failures"]),
                          ("junit_errors", junit_summary["errors"]),
                          ("junit_skipped", junit_summary["skipped"]),
                          ("junit_timeout", junit_summary["timeout"])):
        if outcome[field] != actual:
            raise ValueError(f"post-test outcome {field} does not match JUnit")
    return {"value": outcome, "test_start": start, "test_end": end,
            "junit": junit_summary}


def _execution_state(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {"execution_failure": False, "execution_blocked": True,
                "evidence_blocked": True, "receipt_missing": True}
    try:
        receipt = _read_json(path, "runner receipt")
        required = ("format", "scenario", "test_identity", "test_start_utc", "test_end_utc", "machine",
                    "host_lease_id", "host_policy_sha256", "post_test_outcome_path",
                    "ctest_raw_path", "ctest_inventory_path", "ctest_junit_path",
                    "executable_path", "executable_sha256", "ctest_selected_executable")
        missing = [field for field in required if field not in receipt]
        if missing:
            raise ValueError(f"runner receipt is missing {', '.join(missing)}")
        for field in ("scenario", "test_identity", "test_start_utc", "test_end_utc", "machine",
                      "host_lease_id", "host_policy_sha256", "post_test_outcome_path",
                      "ctest_raw_path", "ctest_inventory_path", "ctest_junit_path",
                      "executable_path", "executable_sha256", "ctest_selected_executable"):
            if not isinstance(receipt[field], str) or not receipt[field]:
                raise ValueError(f"runner receipt {field} is not a non-empty string")
        if receipt["test_identity"] != "suite_fx_runtime_stress_tests":
            raise ValueError("runner receipt test identity is not the FX stress test")
        if not SHA256_RE.fullmatch(receipt["host_policy_sha256"]):
            raise ValueError("runner receipt host policy SHA-256 is malformed")
        if not SHA256_RE.fullmatch(receipt["executable_sha256"]):
            raise ValueError("runner receipt executable SHA-256 is malformed")
        start = _utc(receipt["test_start_utc"], "receipt.test_start_utc")
        receipt_end = None if receipt["test_end_utc"] in ("post-test-outcome", "") \
            else _utc(receipt["test_end_utc"], "receipt.test_end_utc")
        outcome_path = Path(receipt["post_test_outcome_path"])
        if not outcome_path.is_absolute():
            outcome_path = path.parent / outcome_path
        artifact_paths: dict[str, Path] = {}
        for field in ("ctest_raw_path", "ctest_inventory_path", "ctest_junit_path"):
            artifact = Path(receipt[field])
            if not artifact.is_absolute():
                artifact = path.parent / artifact
            if not artifact.is_file() or artifact.stat().st_size == 0:
                raise ValueError(f"runner receipt {field} is missing or empty")
            artifact_paths[field] = artifact
        outcome_info = _load_outcome(outcome_path)
        outcome = outcome_info["value"]
        native_evidence = AUDIT.reconcile_native_ctest(
            artifact_paths["ctest_raw_path"], artifact_paths["ctest_junit_path"],
            outcome["test_identity"], outcome["ctest_exit_code"])
        if outcome["scenario"] != receipt["scenario"] \
                or outcome["test_identity"] != receipt["test_identity"] \
                or not _same_path(outcome["junit_path"], artifact_paths["ctest_junit_path"]) \
                or outcome_info["test_start"] != start \
                or (receipt_end is not None and outcome_info["test_end"] != receipt_end):
            raise ValueError("post-test outcome does not reconcile with runner receipt")
        end = outcome_info["test_end"]
        if not _same_path(outcome["executable_path"], receipt["executable_path"]) \
                or not _same_path(receipt["ctest_selected_executable"], receipt["executable_path"]) \
                or outcome["executable_sha256"].casefold() != receipt["executable_sha256"].casefold():
            raise ValueError("post-test outcome executable does not match the selected CTest executable")
        status = str(outcome["ctest_outcome"]).lower()
        execution_failure = bool(outcome["timed_out"] or outcome["crashed"] or outcome["skipped"]
                                 or not outcome["execution_complete"])
        junit_failure = bool(outcome["junit_failures"] > 0 or outcome["junit_errors"] > 0
                             or outcome["junit_skipped"] > 0 or outcome["junit_timeout"])
        cpu_only_valid = bool(outcome["cpu_only_failure"] and outcome["ctest_exit_code"] != 0
                              and native_evidence.get("cpu_reason_verified")
                              and not execution_failure)
        native_failure = bool(outcome["ctest_exit_code"] != 0 or junit_failure) and not cpu_only_valid
        execution_blocked = native_failure
        evidence_blocked = execution_failure or native_failure
        return {
            "execution_failure": execution_failure,
            "execution_blocked": execution_blocked,
            "native_failure": native_failure,
            "cpu_only_failure": cpu_only_valid,
            "evidence_blocked": evidence_blocked,
            "receipt_missing": False,
            "status": status,
            "test_start": start,
            "test_end": end,
            "host": str(receipt["machine"]),
            "lease_id": str(receipt["host_lease_id"]),
            "policy_sha256": str(receipt["host_policy_sha256"]),
            "outcome_path": outcome_path,
            "outcome": outcome,
            "native_execution_evidence": native_evidence,
        }
    except (OSError, ValueError) as error:
        return {"execution_failure": False, "execution_blocked": True,
                "evidence_blocked": True, "receipt_missing": False,
                "receipt_error": str(error)}


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
    qualification_prohibited: bool = False,
    provenance_verified: bool = False,
    host_manifest_verified: bool = False,
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
    functional_evidence_invalid = False
    evidence_blocked = coverage_error or witness is None \
        or (witness is not None and witness.get("verified") is not True) \
        or qualification_prohibited or not provenance_verified or not host_manifest_verified
    cpu_diagnostic_blocked = False
    profile_cells_complete = True
    for profile, result in results.items():
        observed = _scan_observed_failures(Path(result["report"])) if result.get("report") else {}
        wall_failure |= bool(result.get("wall_failure", False) or observed.get("wall_failure", False))
        safety_failure |= bool(result.get("safety_failure", False) or observed.get("safety_failure", False))
        functional_failure |= bool(result.get("functional_failure", False) or observed.get("functional_failure", False))
        functional_evidence_invalid |= bool(
            result.get("functional_evidence_invalid", False)
            or observed.get("functional_evidence_invalid", False))
        execution_failure |= bool(result.get("execution_failure", False))
        instrumentation_blocked |= bool(result.get("instrumentation_blocked", False))
        evidence_blocked |= bool(result.get("evidence_blocked", False))
        evidence_blocked |= bool(result.get("functional_evidence_invalid", False)
                                  or observed.get("functional_evidence_invalid", False))
        report_cpu_diagnostic = bool(
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
            state_cpu_only = bool(state.get("cpu_only_failure", False))
            if state_cpu_only:
                report_cpu_only = bool(result.get("cpu_measurement_insufficient", False)) \
                    and not bool(result.get("wall_failure", False)) \
                    and not bool(result.get("safety_failure", False)) \
                    and not bool(result.get("functional_failure", False)) \
                    and not bool(result.get("functional_evidence_invalid", False))
                if report_cpu_only:
                    cpu_diagnostic_blocked = True
                else:
                    state["cpu_only_failure_reconciled"] = False
                    state["execution_blocked"] = True
                    state["evidence_blocked"] = True
                    execution_blocked = True
                    evidence_blocked = True
            elif state.get("test_start") and not state.get("execution_blocked") \
                    and not state.get("execution_failure"):
                cpu_diagnostic_blocked |= report_cpu_diagnostic
        else:
            execution_blocked = True
            evidence_blocked = True
            cpu_diagnostic_blocked |= report_cpu_diagnostic
    evidence_blocked |= not profile_cells_complete
    performance_failure = wall_failure or safety_failure or functional_failure
    if performance_failure:
        primary_status = "WALL_GATE_FAIL"
    elif execution_failure or execution_blocked or evidence_blocked:
        primary_status = "WALL_GATE_BLOCKED"
    elif cpu_diagnostic_blocked:
        primary_status = "WALL_GATE_PASS_CPU_UNKNOWN"
    else:
        primary_status = "WALL_GATE_PASS"
    return {
        "format": "VOX_FX_WALL_GATE_EVALUATION_V2",
        "mode": "fixture" if qualification_prohibited else "qualified",
        "qualification_prohibited": qualification_prohibited,
        "primary_status": primary_status,
        "wall_failure": wall_failure,
        "safety_failure": safety_failure,
        "functional_failure": functional_failure,
        "functional_evidence_invalid": functional_evidence_invalid,
        "execution_failure": execution_failure,
        "execution_blocked": execution_blocked,
        "performance_failure": performance_failure,
        "instrumentation_blocked": instrumentation_blocked,
        "evidence_blocked": evidence_blocked,
        "cpu_diagnostic_blocked": cpu_diagnostic_blocked,
        "provenance_verified": provenance_verified,
        "host_manifest_verified": host_manifest_verified,
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
    host_manifest: Path | None = None,
    allow_unverified_provenance: bool = False,
) -> dict[str, Any]:
    receipts = receipts or {}
    manifests = manifests or {}
    audited: dict[str, dict[str, Any]] = {}
    execution = {profile: _execution_state(receipts.get(profile)) for profile in PROFILES}
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
            audited[profile]["report"] = str(report)
        except (OSError, ValueError) as error:
            observed = _scan_observed_failures(report)
            audited[profile] = {
                "profile": profile, "report": str(report), "cells": 0,
                "totals": {}, "wall_failure": observed["wall_failure"],
                "safety_failure": observed["safety_failure"],
                "functional_failure": observed["functional_failure"],
                "functional_evidence_invalid": observed["functional_evidence_invalid"],
                "execution_failure": False, "evidence_blocked": True,
                "audit_error": str(error),
            }
    witness_result: dict[str, Any] | None = None
    witness_error: str | None = None
    host_manifest_result: dict[str, Any] | None = None
    expected_host = None
    expected_lease_id = None
    expected_policy_hash = None
    valid_states = [state for state in execution.values() if state.get("test_start")]
    if valid_states:
        hosts = {state.get("host") for state in valid_states}
        leases = {state.get("lease_id") for state in valid_states}
        policies = {state.get("policy_sha256") for state in valid_states}
        if len(hosts) == len(leases) == len(policies) == 1:
            expected_host = next(iter(hosts))
            expected_lease_id = next(iter(leases))
            expected_policy_hash = next(iter(policies))
        else:
            witness_error = "trusted runner receipts disagree on host, lease or process policy"
    try:
        if witness is None:
            raise ValueError("host witness is required")
        intervals = {profile: execution[profile] for profile in PROFILES}
        witness_result = validate_witness(
            witness, intervals, expected_host, expected_lease_id, expected_policy_hash,
            require_external_attestations=not allow_unverified_provenance)
        if not allow_unverified_provenance:
            if host_manifest is None:
                raise ValueError("production evaluation requires a host artifact manifest")
            host_manifest_result = _verify_host_manifest(host_manifest, witness)
    except ValueError as error:
        witness_error = str(error) if witness_error is None else f"{witness_error}; {error}"
    provenance_verified = set(audited) == set(PROFILES) and all(
        audited[profile].get("provenance", {}).get("verified") is True
        for profile in PROFILES if profile in audited)
    host_manifest_verified = host_manifest_result is not None if not allow_unverified_provenance else False
    if not allow_unverified_provenance and (not provenance_verified or host_manifest_result is None):
        witness_error = witness_error or "independent artifact provenance is incomplete"
    result = evaluate_results(
        audited, witness_result, execution,
        qualification_prohibited=allow_unverified_provenance,
        provenance_verified=provenance_verified,
        host_manifest_verified=host_manifest_verified,
    )
    result["execution_intervals"] = {
        profile: {
            key: value.isoformat().replace("+00:00", "Z") if isinstance(value, datetime) else value
            for key, value in state.items() if key in {"test_start", "test_end", "host", "lease_id", "policy_sha256"}
        }
        for profile, state in execution.items()
    }
    if host_manifest_result is not None:
        result["host_manifest"] = host_manifest_result
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
    parser.add_argument("--host-manifest", type=Path)
    parser.add_argument("--allow-unverified-provenance", action="store_true",
                        help="fixture mode only; output is qualification-prohibited and cannot PASS")
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
            host_manifest=args.host_manifest,
            allow_unverified_provenance=args.allow_unverified_provenance,
        )
    except (OSError, ValueError) as error:
        parser.exit(2, f"FX-T18 wall evaluator: {error}\n")
    print(json.dumps(result, indent=2, sort_keys=True, default=str))
    return 0 if result["primary_status"] not in {"WALL_GATE_FAIL", "WALL_GATE_BLOCKED"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
