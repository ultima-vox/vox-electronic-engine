#!/usr/bin/env python3
"""Evaluate the proposed FX-T18 wall-only gate.

The evaluator is fail-closed.  It keeps the existing native CTest/JUnit and
functional precedence, but a proposed wall status also requires a complete
host-wide witness, an owner-signed baseline/lease/loss/binding set, and a
manifest that hashes every public and private evidence artifact.  Fixture mode
is deliberately qualification-prohibited and can never emit a production
``WALL_GATE_PASS`` or ``WALL_GATE_PASS_CPU_UNKNOWN``.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from typing import Any

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("fx_t18_audit", HERE / "audit-fx-windows-timing.py")
assert SPEC and SPEC.loader
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
PARSER_SPEC = importlib.util.spec_from_file_location("fx_t18_etl_parser", HERE / "parse-fx-t18-etl.py")
assert PARSER_SPEC and PARSER_SPEC.loader
ETL_PARSER = importlib.util.module_from_spec(PARSER_SPEC)
PARSER_SPEC.loader.exec_module(ETL_PARSER)

PROFILES = {"routed": 54, "authored": 54, "typical": 9}
REQUIRED_PHASES = ("pre", "during", "post")
MAX_SAMPLE_GAP_SECONDS = 2.0
MAX_EVENT_CORRELATION_SKEW_SECONDS = 2.0
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
SHA1_RE = re.compile(r"^[0-9a-fA-F]{40}$")
OWNER_SIGNATURE_METHOD = "detached_cms_owner_certificate_chain_and_thumbprint"
PRIVATE_INVENTORY_FORMAT = "VOX_FX_T18_PRIVATE_PROCESS_INVENTORY_V1"


def _normalize_thumbprint(value: Any) -> str:
    if not isinstance(value, str):
        raise ValueError("owner certificate thumbprint is not a string")
    normalized = re.sub(r"[\s:]", "", value).upper()
    if not SHA1_RE.fullmatch(normalized):
        raise ValueError("owner certificate thumbprint is not a SHA-1 value")
    return normalized


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
    if not isinstance(value, str) or not value:
        raise ValueError(f"{name} is not an explicit timestamp with a UTC offset")
    normalized = value[:-1] + "+00:00" if value.endswith("Z") else value
    try:
        parsed = datetime.fromisoformat(normalized)
    except ValueError as error:
        raise ValueError(f"{name} is not ISO-8601 with a UTC offset: {value!r}") from error
    if parsed.tzinfo is None:
        raise ValueError(f"{name} has no UTC offset: {value!r}")
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


def _resolve(path: Path, value: str) -> Path:
    candidate = Path(value)
    return candidate if candidate.is_absolute() else path.parent / candidate


def _utc_ticks(value: Any, name: str) -> int:
    """Return exact .NET-style UTC ticks, retaining the seventh fraction digit."""
    if not isinstance(value, str) or not value:
        raise ValueError(f"{name} is not a timestamp")
    parsed = _utc(value, name)
    fraction_match = re.search(r"\.(\d+)(?:Z|[+-]\d\d:?\d\d)$", value)
    fraction = fraction_match.group(1) if fraction_match else ""
    if len(fraction) > 7:
        raise ValueError(f"{name} has more than seven fractional-second digits")
    fraction_ticks = int((fraction + "0000000")[:7]) if fraction else 0
    epoch = datetime(1, 1, 1, tzinfo=timezone.utc)
    whole_second = parsed.replace(microsecond=0)
    delta = whole_second - epoch
    return (delta.days * 86400 + delta.seconds) * 10_000_000 + fraction_ticks


def _identity_digest(event: dict[str, Any]) -> str:
    services = event.get("service_identity_sha256")
    if not isinstance(services, list):
        raise ValueError("ETW process event service identity list is malformed")
    payload = "|".join((
        str(event.get("process_name", "")), str(event.get("executable_path", "")),
        str(event.get("executable_sha256", "")), str(event.get("parent_pid", "")),
        str(_utc_ticks(event.get("start_time_utc"), "identity.start_time_utc")),
        ",".join(str(item) for item in services),
    ))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def _event_identity_key(event: dict[str, Any]) -> tuple[Any, ...]:
    """Identity key deliberately excludes provider timestamp formatting.

    PID, process creation time, event type, immutable identity digest and the
    exact executable/parent/service facts identify one lifecycle.  Provider
    timestamps are correlated separately after UTC normalization and are never
    used as string-equality keys.
    """
    return (
        str(event["event"]), int(event["pid"]), _utc_ticks(event["start_time_utc"], "event.start_time_utc"),
        str(event["process_name"]).casefold(), str(event["executable_path"]).casefold(),
        str(event["executable_sha256"]).casefold(), int(event["parent_pid"]),
        str(event["identity_digest"]).casefold(),
        tuple(sorted(str(item).casefold() for item in event["service_identity_sha256"])),
    )


def _same_process_identity(left: dict[str, Any], right: dict[str, Any]) -> bool:
    return _event_identity_key(left) == _event_identity_key(right)


def _validate_lineage(record: dict[str, Any], label: str) -> None:
    if record.get("service_ancestry_verified") is not True:
        raise ValueError(f"{label}: runner-service ancestry was not verified")
    if record.get("lineage_verification") not in {"runner_service_ancestry_v1", "signed_job_lineage_v1"}:
        raise ValueError(f"{label}: lineage verification method is missing")
    ancestry = record.get("service_ancestry")
    if not isinstance(ancestry, list) or not ancestry:
        raise ValueError(f"{label}: verified service ancestry is missing")
    root_services: set[str] = set()
    previous: dict[str, Any] | None = None
    for index, node in enumerate(ancestry):
        prefix = f"{label}.service_ancestry[{index}]"
        if not isinstance(node, dict):
            raise ValueError(f"{prefix}: ancestry node is not an object")
        required = ("pid", "process_name", "parent_pid", "start_time_utc", "executable_path",
                    "executable_sha256", "service_identity_sha256", "identity_digest")
        if any(field not in node for field in required):
            raise ValueError(f"{prefix}: ancestry node is incomplete")
        if (type(node["pid"]) is not int or node["pid"] < 1
                or not isinstance(node["process_name"], str) or not node["process_name"]
                or type(node["parent_pid"]) is not int or node["parent_pid"] < 0
                or not isinstance(node["executable_path"], str) or not node["executable_path"]
                or not SHA256_RE.fullmatch(str(node["executable_sha256"]))
                or not isinstance(node["service_identity_sha256"], list)
                or any(not SHA256_RE.fullmatch(str(item)) for item in node["service_identity_sha256"])
                or not SHA256_RE.fullmatch(str(node["identity_digest"]))):
            raise ValueError(f"{prefix}: ancestry identity is malformed")
        _utc(node["start_time_utc"], f"{prefix}.start_time_utc")
        if _identity_digest(node).casefold() != str(node["identity_digest"]).casefold():
            raise ValueError(f"{prefix}: ancestry identity digest does not reconcile")
        if previous is not None and node["pid"] != previous["parent_pid"]:
            raise ValueError(f"{prefix}: parent PID chain is discontinuous or forged")
        previous = node
        root_services.update(str(item).casefold() for item in node["service_identity_sha256"])
    expected_services = {str(item).casefold() for item in record["service_identity_sha256"]}
    if not expected_services or root_services != expected_services:
        raise ValueError(f"{label}: service identity set does not reconcile with the verified ancestry")
    first = ancestry[0]
    for field in ("pid", "process_name", "parent_pid", "start_time_utc", "executable_path", "executable_sha256"):
        left = _utc_ticks(first[field], f"{label}.service_ancestry[0].start_time_utc") if field == "start_time_utc" else first[field]
        right = _utc_ticks(record[field], f"{label}.start_time_utc") if field == "start_time_utc" else record[field]
        if left != right:
            raise ValueError(f"{label}: ancestry root does not match the process identity")
    if record.get("lineage_verification") == "signed_job_lineage_v1":
        proof = record.get("job_lineage")
        if not isinstance(proof, dict) or proof.get("verified") is not True \
                or not SHA256_RE.fullmatch(str(proof.get("job_identity_sha256", ""))):
            raise ValueError(f"{label}: signed job lineage proof is incomplete")


def _validate_etw_process_events(events: Any, label: str, coverage_start: datetime,
                                 coverage_end: datetime) -> list[dict[str, Any]]:
    if not isinstance(events, list):
        raise ValueError(f"{label}: authoritative ETW process_events must be a list")
    validated: list[dict[str, Any]] = []
    previous_time: datetime | None = None
    active: dict[int, tuple[int, int, str]] = {}
    seen_lifecycles: dict[int, tuple[int, int, str]] = {}
    for index, event in enumerate(events):
        prefix = f"{label}.process_events[{index}]"
        if not isinstance(event, dict) or event.get("event") not in {"start", "stop"}:
            raise ValueError(f"{prefix}: event kind is invalid")
        for field in ("timestamp_utc", "pid", "process_name", "executable_path", "executable_sha256",
                      "parent_pid", "start_time_utc", "service_identity_sha256", "service_ancestry_verified",
                      "lineage_verification", "service_ancestry", "identity_digest", "identity_known",
                      "approved_by_policy"):
            if field not in event:
                raise ValueError(f"{prefix}: missing {field}")
        timestamp = _utc(event["timestamp_utc"], f"{prefix}.timestamp_utc")
        if timestamp < coverage_start or timestamp > coverage_end or (previous_time and timestamp < previous_time):
            raise ValueError(f"{prefix}: timestamp is outside or regresses within ETW coverage")
        previous_time = timestamp
        if (type(event["pid"]) is not int or event["pid"] < 1 or not isinstance(event["process_name"], str)
                or not event["process_name"] or not isinstance(event["executable_path"], str)
                or not event["executable_path"] or not SHA256_RE.fullmatch(str(event["executable_sha256"]))
                or type(event["parent_pid"]) is not int or event["parent_pid"] < 0
                or not SHA256_RE.fullmatch(str(event["identity_digest"]))
                or type(event["identity_known"]) is not bool or type(event["approved_by_policy"]) is not bool
                or event["identity_known"] is not True
                or not isinstance(event["service_identity_sha256"], list) or not event["service_identity_sha256"]
                or any(not SHA256_RE.fullmatch(str(item)) for item in event["service_identity_sha256"])):
            raise ValueError(f"{prefix}: ETW process identity or service decision is incomplete")
        start_time = _utc(event["start_time_utc"], f"{prefix}.start_time_utc")
        if start_time > timestamp:
            raise ValueError(f"{prefix}: process creation time is after its ETW event")
        if _identity_digest(event).casefold() != str(event["identity_digest"]).casefold():
            raise ValueError(f"{prefix}: ETW identity digest does not reconcile")
        _validate_lineage(event, prefix)
        lifecycle = (_utc_ticks(event["start_time_utc"], f"{prefix}.start_time_utc"), event["pid"], str(event["identity_digest"]).casefold())
        if event["event"] == "start":
            if event["pid"] in active:
                raise ValueError(f"{prefix}: PID started again before its prior lifecycle stopped")
            if event["pid"] in seen_lifecycles:
                raise ValueError(f"{prefix}: PID was reused after an earlier lifecycle")
            seen_lifecycles[event["pid"]] = lifecycle
            active[event["pid"]] = lifecycle
        elif event["pid"] in active:
            if active[event["pid"]] != lifecycle:
                raise ValueError(f"{prefix}: PID stop identity disagrees with its start identity")
            del active[event["pid"]]
        elif start_time >= coverage_start:
            raise ValueError(f"{prefix}: PID stop has no matching start lifecycle")
        elif event["pid"] in seen_lifecycles:
            if seen_lifecycles[event["pid"]] != lifecycle:
                raise ValueError(f"{prefix}: PID stop identity disagrees with its earlier lifecycle")
            raise ValueError(f"{prefix}: duplicate stop for a PID lifecycle")
        else:
            seen_lifecycles[event["pid"]] = lifecycle
        validated.append(event)
    return validated


def _correlate_events(etw_events: list[dict[str, Any]], wmi_events: list[dict[str, Any]], label: str) -> None:
    if not isinstance(wmi_events, list):
        raise ValueError(f"{label}: WMI process events are missing")
    if len(etw_events) != len(wmi_events):
        raise ValueError(f"{label}: ETW/WMI event count mismatch (extra or unmatched event)")
    candidate_sets: list[list[int]] = []
    for etw_index, etw_event in enumerate(etw_events):
        etw_time = _utc(etw_event["timestamp_utc"], f"{label}.etw[{etw_index}].timestamp_utc")
        candidates: list[int] = []
        for wmi_index, wmi_event in enumerate(wmi_events):
            if not _same_process_identity(etw_event, wmi_event):
                continue
            wmi_time = _utc(wmi_event["timestamp_utc"], f"{label}.wmi[{wmi_index}].timestamp_utc")
            skew = abs((etw_time - wmi_time).total_seconds())
            if skew <= MAX_EVENT_CORRELATION_SKEW_SECONDS:
                candidates.append(wmi_index)
        candidate_sets.append(candidates)
    if any(len(candidates) == 0 for candidates in candidate_sets):
        raise ValueError(f"{label}: ETW/WMI process event is unmatched beyond the correlation bound")
    if any(len(candidates) > 1 for candidates in candidate_sets):
        raise ValueError(f"{label}: ETW/WMI process event correlation is ambiguous")
    chosen = [candidates[0] for candidates in candidate_sets]
    if len(set(chosen)) != len(chosen):
        raise ValueError(f"{label}: ETW/WMI process event correlation is not one-to-one")
    if set(chosen) != set(range(len(wmi_events))):
        raise ValueError(f"{label}: WMI contains an extra process event")

def _policy_approves_event(policy_file: dict[str, Any], event: dict[str, Any]) -> bool:
    process_name = str(event.get("process_name", ""))
    try:
        _validate_lineage(event, "policy process")
    except ValueError:
        return False
    service_values = event.get("service_identity_sha256")
    if not isinstance(service_values, list) or not service_values:
        return False
    service_set = {str(value).casefold() for value in service_values}
    parent = None
    ancestry = event.get("service_ancestry")
    if isinstance(ancestry, list) and len(ancestry) > 1:
        parent = ancestry[1]
    for denied in policy_file.get("denied_process_name_regex", []):
        if re.search(str(denied), process_name, flags=re.IGNORECASE):
            return False
    for item in policy_file.get("approved_processes", []):
        name = item.get("process_name")
        name_regex = item.get("process_name_regex")
        if name and process_name.casefold() != str(name).casefold():
            continue
        if name_regex and re.search(str(name_regex), process_name, flags=re.IGNORECASE) is None:
            continue
        if str(event.get("executable_path", "")).casefold() != str(item.get("executable_path", "")).casefold():
            continue
        if str(event.get("executable_sha256", "")).casefold() != str(item.get("executable_sha256", "")).casefold():
            continue
        if str(item.get("service_identity_sha256", "")).casefold() not in service_set:
            continue
        exact_parent_pid = item.get("parent_pid")
        if exact_parent_pid is not None and event.get("parent_pid") != exact_parent_pid:
            continue
        parent_rule = item.get("parent_identity")
        if parent_rule is not None:
            if not isinstance(parent_rule, dict) or not isinstance(parent, dict):
                continue
            if parent.get("process_name", "").casefold() != str(parent_rule.get("process_name", "")).casefold():
                continue
            if parent.get("executable_path", "").casefold() != str(parent_rule.get("executable_path", "")).casefold():
                continue
            if parent.get("executable_sha256", "").casefold() != str(parent_rule.get("executable_sha256", "")).casefold():
                continue
            if parent_rule.get("pid") is not None and parent.get("pid") != parent_rule.get("pid"):
                continue
            if parent_rule.get("start_time_utc") is not None \
                    and _utc_ticks(parent.get("start_time_utc"), "parent.start_time_utc") != _utc_ticks(parent_rule.get("start_time_utc"), "policy.parent_identity.start_time_utc"):
                continue
        elif item.get("lineage_mode") == "runner_service_ancestry" and len(ancestry) > 1 \
                and item.get("allow_dynamic") is True:
            # Dynamic non-service children must pin the exact parent image and
            # hash. A PID-only rule is not an ancestry authorization.
            continue
        exact_pid = item.get("pid")
        exact_start = item.get("start_time_utc")
        if exact_pid is not None and event.get("pid") != exact_pid:
            continue
        if exact_start is not None \
                and _utc_ticks(event.get("start_time_utc"), "event.start_time_utc") != _utc_ticks(exact_start, "policy.start_time_utc"):
            continue
        if exact_pid is None and exact_start is None and item.get("allow_dynamic") is not True:
            continue
        return True
    return False

def _validate_identity_policy(policy_file: dict[str, Any], label: str) -> None:
    approved = policy_file.get("approved_processes")
    denied = policy_file.get("denied_process_name_regex")
    runner_services = policy_file.get("runner_service_identity_sha256")
    if not isinstance(approved, list) or not approved or not isinstance(denied, list) or not denied:
        raise ValueError(f"{label}: strict approved and denied process policy is required")
    if not isinstance(runner_services, list) or len(set(runner_services)) != 4 \
            or any(not SHA256_RE.fullmatch(str(item)) for item in runner_services):
        raise ValueError(f"{label}: exactly four runner service identities are required")
    for index, item in enumerate(approved):
        prefix = f"{label}.approved_processes[{index}]"
        if not isinstance(item, dict):
            raise ValueError(f"{prefix}: policy entry is not an object")
        name = item.get("process_name")
        name_regex = item.get("process_name_regex")
        if (name is not None and not isinstance(name, str)) or (name_regex is not None and not isinstance(name_regex, str)):
            raise ValueError(f"{prefix}: process name fields must be strings")
        has_name = isinstance(name, str) and bool(name)
        has_name_regex = isinstance(name_regex, str) and bool(name_regex)
        if has_name == has_name_regex:
            raise ValueError(f"{prefix}: exactly one non-empty process name or regex is required")
        path = item.get("executable_path")
        if not isinstance(path, str) or not path or path.startswith("<") or "*" in path or "?" in path:
            raise ValueError(f"{prefix}: exact executable_path is required")
        if not SHA256_RE.fullmatch(str(item.get("executable_sha256", ""))):
            raise ValueError(f"{prefix}: executable SHA-256 is required")
        if not SHA256_RE.fullmatch(str(item.get("service_identity_sha256", ""))):
            raise ValueError(f"{prefix}: service identity SHA-256 is required")
        if str(item["service_identity_sha256"]).casefold() not in {str(value).casefold() for value in runner_services}:
            raise ValueError(f"{prefix}: process is not rooted in one of the four runner services")
        if type(item.get("parent_pid")) is not int or item["parent_pid"] < 0:
            raise ValueError(f"{prefix}: exact parent_pid identity is required")
        lineage_mode = item.get("lineage_mode", "runner_service_ancestry")
        if lineage_mode not in {"runner_service_ancestry", "signed_job_lineage"}:
            raise ValueError(f"{prefix}: lineage_mode is invalid")
        if lineage_mode == "signed_job_lineage":
            proof = item.get("job_lineage")
            if not isinstance(proof, dict) or not isinstance(proof.get("issuer"), str) \
                    or not isinstance(proof.get("job_identity_sha256"), str) \
                    or not SHA256_RE.fullmatch(proof["job_identity_sha256"]):
                raise ValueError(f"{prefix}: signed job lineage authorization is incomplete")
        parent_rule = item.get("parent_identity")
        if parent_rule is not None:
            if not isinstance(parent_rule, dict):
                raise ValueError(f"{prefix}: parent_identity must be an object")
            for field in ("process_name", "executable_path", "executable_sha256"):
                if not isinstance(parent_rule.get(field), str) or not parent_rule[field] \
                        or (field == "executable_sha256" and not SHA256_RE.fullmatch(parent_rule[field])) \
                        or (field == "executable_path" and (parent_rule[field].startswith("<") or "*" in parent_rule[field] or "?" in parent_rule[field])):
                    raise ValueError(f"{prefix}: exact parent_identity {field} is required")
            if parent_rule.get("pid") is not None and (type(parent_rule["pid"]) is not int or parent_rule["pid"] < 1):
                raise ValueError(f"{prefix}: parent_identity PID is invalid")
            if parent_rule.get("start_time_utc") is not None:
                _utc(parent_rule["start_time_utc"], f"{prefix}.parent_identity.start_time_utc")
        allow_dynamic = item.get("allow_dynamic")
        if type(allow_dynamic) is not bool:
            raise ValueError(f"{prefix}: allow_dynamic must be an explicit boolean")
        exact_pid = item.get("pid")
        exact_start = item.get("start_time_utc")
        if (exact_pid is None) != (exact_start is None):
            raise ValueError(f"{prefix}: exact PID and process creation time must be supplied together")
        if exact_pid is not None:
            if type(exact_pid) is not int or exact_pid < 1:
                raise ValueError(f"{prefix}: exact PID identity is invalid")
            _utc(exact_start, f"{prefix}.start_time_utc")
        elif allow_dynamic is not True:
            raise ValueError(f"{prefix}: a dynamic rule must explicitly set allow_dynamic=true")
        if exact_pid is None and item.get("role") not in {"runner", "runner_service", "direct_service"} \
                and lineage_mode in {"runner_service_ancestry", "signed_job_lineage"} and parent_rule is None:
            raise ValueError(f"{prefix}: dynamic non-service child must pin exact parent executable/path/hash")
    for index, regex in enumerate(denied):
        if not isinstance(regex, str) or not regex:
            raise ValueError(f"{label}.denied_process_name_regex[{index}]: deny regex is invalid")
        try:
            re.compile(regex)
        except re.error as error:
            raise ValueError(f"{label}.denied_process_name_regex[{index}]: deny regex is invalid") from error
    for index, item in enumerate(approved):
        name = item.get("process_name")
        name_regex = item.get("process_name_regex")
        if isinstance(name, str) and any(re.search(str(regex), name, flags=re.IGNORECASE) for regex in denied):
            raise ValueError(f"{label}.approved_processes[{index}]: deny rule conflicts with an allow entry")
        if isinstance(name_regex, str):
            try:
                re.compile(name_regex)
            except re.error as error:
                raise ValueError(f"{label}.approved_processes[{index}]: process name regex is invalid") from error

def _proof(value: dict[str, Any], name: str) -> dict[str, Any]:
    proofs = value.get("attestation_proofs")
    if not isinstance(proofs, dict) or not isinstance(proofs.get(name), dict):
        raise ValueError(f"{value.get('format', 'host witness')}: signed {name} proof is missing")
    return proofs[name]


def _verify_host_manifest(path: Path, witness: Path) -> dict[str, Any]:
    """Verify all files named by a production witness and their SHA-256s.

    This intentionally includes the owner-private inventory and every detached
    signature.  The inventory remains private by content policy; hashing it in
    the public manifest makes deletion, replacement, or a post-capture edit
    visible without publishing process names/PIDs.
    """
    manifest = _read_json(path, "host artifact manifest")
    if manifest.get("format") not in {"VOX_FX_T18_HOST_ARTIFACTS_V1", "VOX_FX_T18_HOST_ARTIFACTS_V2"}:
        raise ValueError(f"unsupported host artifact manifest format in {path}")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise ValueError(f"{path}: host manifest has no files")
    hashes: dict[str, str] = {}
    for entry in files:
        if (not isinstance(entry, dict) or not isinstance(entry.get("path"), str)
                or not isinstance(entry.get("sha256"), str)
                or not SHA256_RE.fullmatch(entry["sha256"])):
            raise ValueError(f"{path}: malformed host artifact entry")
        artifact = _resolve(path, entry["path"])
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
    required: list[Path] = [witness]
    for container, field in (
        (witness_value, "identity_policy_path"),
        (witness_value, "private_inventory_path"),
        (witness_value.get("runner_lease", {}), "verification_receipt_path"),
        (witness_value.get("event_trace", {}), "loss_receipt_path"),
        (witness_value.get("event_trace", {}), "collector_artifact_path"),
        (witness_value.get("event_trace", {}), "collector_parse_receipt_path"),
        (witness_value.get("execution_binding", {}), "binding_receipt_path"),
    ):
        item = container.get(field) if isinstance(container, dict) else None
        if not isinstance(item, str) or not item or item.startswith("<"):
            raise ValueError(f"{witness}: host artifact path {field} is missing or redacted")
        required.append(_resolve(witness, item))
    if witness_value.get("fixture_mode") is not True and witness_value.get("qualification_prohibited") is not True:
        parse_value = witness_value.get("event_trace", {}).get("collector_parse_receipt_path")
        if not isinstance(parse_value, str) or parse_value.startswith("<"):
            raise ValueError(f"{witness}: independent ETW parse receipt path is missing")
        parse_path = _resolve(witness, parse_value)
        parse_value_json = _read_json(parse_path, "ETW parse receipt")
        provenance = parse_value_json.get("parser_provenance")
        join_value = provenance.get("identity_join_path") if isinstance(provenance, dict) else None
        join_hash = provenance.get("identity_join_sha256") if isinstance(provenance, dict) else None
        if (not isinstance(join_value, str) or join_value.startswith("<")
                or not isinstance(join_hash, str) or not SHA256_RE.fullmatch(join_hash)):
            raise ValueError(f"{witness}: independent ETW identity join path/hash is missing")
        join_path = _resolve(parse_path, join_value)
        if not join_path.is_file() or _sha256(join_path).casefold() != join_hash.casefold():
            raise ValueError(f"{witness}: independent ETW identity join hash does not reconcile")
        required.append(join_path)
        observation_value = witness_value.get("observation_path")
        observation_hash = witness_value.get("observation_sha256")
        if not isinstance(observation_value, str) or observation_value.startswith("<") \
                or not isinstance(observation_hash, str) or not SHA256_RE.fullmatch(observation_hash):
            raise ValueError(f"{witness}: immutable observation path/hash is missing")
        observation_path = _resolve(witness, observation_value)
        if not observation_path.is_file() or _sha256(observation_path).casefold() != observation_hash.casefold():
            raise ValueError(f"{witness}: immutable observation hash does not reconcile")
        required.append(observation_path)
    for name in ("policy", "lease", "event_loss", "execution_binding"):
        proof = _proof(witness_value, name)
        for field in ("data_path", "signature_path"):
            item = proof.get(field)
            if not isinstance(item, str) or not item or item.startswith("<"):
                raise ValueError(f"{witness}: signed {name} proof path {field} is missing")
            required.append(_resolve(witness, item))
    runner_receipts = witness_value.get("execution_binding", {}).get("runner_receipts")
    if not isinstance(runner_receipts, list) or len(runner_receipts) != 3:
        raise ValueError(f"{witness}: execution binding does not list all runner receipts")
    for item in runner_receipts:
        if not isinstance(item, dict) or not isinstance(item.get("receipt_path"), str) \
                or not SHA256_RE.fullmatch(str(item.get("receipt_sha256", ""))):
            raise ValueError(f"{witness}: malformed bound runner receipt hash")
        receipt_path = _resolve(witness, item["receipt_path"])
        if not receipt_path.is_file() or _sha256(receipt_path).casefold() != item["receipt_sha256"].casefold():
            raise ValueError(f"{witness}: bound runner receipt hash reconciliation failed")
        required.append(receipt_path)
    for required_path in required:
        if not _manifest_contains(hashes, required_path):
            raise ValueError(f"{path}: host manifest does not cover {required_path}")

    private_hash = witness_value.get("private_inventory_sha256")
    if not isinstance(private_hash, str) or not SHA256_RE.fullmatch(private_hash):
        raise ValueError(f"{witness}: private inventory hash is missing")
    private_path = _resolve(witness, str(witness_value["private_inventory_path"]))
    actual_private = next((digest for key, digest in hashes.items() if _same_path(key, private_path)), None)
    if actual_private is None or actual_private.casefold() != private_hash.casefold():
        raise ValueError(f"{witness}: private inventory hash does not reconcile with the host manifest")
    return {"verified": True, "path": str(path), "files": len(files), "hashes": hashes,
            "required_artifacts": [str(item) for item in required]}


def _verify_cms_with_owner_powershell(data_path: Path, signature_path: Path, thumbprint: str) -> None:
    """Repeat the detached-CMS check outside Capture on the owner host.

    The production evaluator is intentionally not satisfied by a JSON field
    saying that Capture verified a signature. Windows PowerShell 5.1 is the
    reviewed verifier for this contract, so a production evaluation without it
    is BLOCKED rather than silently accepting synthetic bytes.
    """
    powershell = shutil.which("powershell.exe") or shutil.which("powershell")
    if not powershell:
        raise ValueError("independent PowerShell CMS verifier is unavailable")
    command = r"""
$ErrorActionPreference = 'Stop'
try {
    try { Add-Type -AssemblyName System.Security.Cryptography.Pkcs -ErrorAction Stop } catch { Add-Type -AssemblyName System.Security -ErrorAction Stop }
    $dataBytes = [IO.File]::ReadAllBytes($env:FX_T18_VERIFY_DATA)
    $signatureBytes = [IO.File]::ReadAllBytes($env:FX_T18_VERIFY_SIGNATURE)
    $contentInfo = New-Object -TypeName System.Security.Cryptography.Pkcs.ContentInfo -ArgumentList (,$dataBytes)
    $cms = [System.Security.Cryptography.Pkcs.SignedCms]::new($contentInfo, $true)
    $cms.Decode($signatureBytes)
    if ($cms.SignerInfos.Count -ne 1) { throw 'signature signer count is not one' }
    $certificate = $cms.SignerInfos[0].Certificate
    if ($null -eq $certificate) { throw 'signature certificate is missing' }
    $actual = (($certificate.Thumbprint -replace '[^0-9a-fA-F]', '').ToUpperInvariant())
    $expected = (($env:FX_T18_VERIFY_THUMBPRINT -replace '[^0-9a-fA-F]', '').ToUpperInvariant())
    if ($actual -cne $expected) { throw 'signature thumbprint mismatch' }
    $cms.CheckSignature($false)
    $chain = New-Object System.Security.Cryptography.X509Certificates.X509Chain
    try {
        $chain.ChainPolicy.RevocationMode = [System.Security.Cryptography.X509Certificates.X509RevocationMode]::Online
        $chain.ChainPolicy.RevocationFlag = [System.Security.Cryptography.X509Certificates.X509RevocationFlag]::EntireChain
        $chain.ChainPolicy.VerificationFlags = [System.Security.Cryptography.X509Certificates.X509VerificationFlags]::NoFlag
        if (-not $chain.Build($certificate)) { throw 'owner certificate chain or revocation check failed' }
    } finally { $chain.Dispose() }
    exit 0
} catch { [Console]::Error.WriteLine('detached CMS verification failed'); exit 1 }
"""
    environment = os.environ.copy()
    environment.update({
        "FX_T18_VERIFY_DATA": str(data_path),
        "FX_T18_VERIFY_SIGNATURE": str(signature_path),
        "FX_T18_VERIFY_THUMBPRINT": thumbprint,
    })
    try:
        completed = subprocess.run(
            [powershell, "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", command],
            env=environment, check=False, capture_output=True, text=True, timeout=30,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise ValueError(f"independent PowerShell CMS verifier failed to run: {error}") from error
    if completed.returncode != 0:
        raise ValueError("independent detached-CMS signature verification failed")


def _verify_signed_proof(
    witness_path: Path, proof: dict[str, Any], name: str,
    expected_thumbprint: str | None = None,
    verify_cryptography: bool = True,
) -> tuple[Path, Path, str]:
    if proof.get("verified") is not True or proof.get("verification_method") != OWNER_SIGNATURE_METHOD:
        raise ValueError(f"{witness_path}: {name} proof was not verified by the detached-CMS owner verifier")
    try:
        thumbprint = _normalize_thumbprint(proof.get("signer_thumbprint"))
    except ValueError as error:
        raise ValueError(f"{witness_path}: {name} proof has no pinned owner certificate thumbprint") from error
    if expected_thumbprint and thumbprint.casefold() != expected_thumbprint.casefold():
        raise ValueError(f"{witness_path}: {name} proof signer thumbprint differs from the other owner proofs")
    data_path_value = proof.get("data_path")
    signature_path_value = proof.get("signature_path")
    if not isinstance(data_path_value, str) or not isinstance(signature_path_value, str):
        raise ValueError(f"{witness_path}: {name} proof paths are missing")
    data_path = _resolve(witness_path, data_path_value)
    signature_path = _resolve(witness_path, signature_path_value)
    if not data_path.is_file() or not signature_path.is_file():
        raise ValueError(f"{witness_path}: {name} proof file is missing")
    data_hash = proof.get("data_sha256")
    signature_hash = proof.get("signature_sha256")
    if not isinstance(data_hash, str) or not SHA256_RE.fullmatch(data_hash) \
            or _sha256(data_path).casefold() != data_hash.casefold():
        raise ValueError(f"{witness_path}: {name} signed data hash does not reconcile")
    if not isinstance(signature_hash, str) or not SHA256_RE.fullmatch(signature_hash) \
            or _sha256(signature_path).casefold() != signature_hash.casefold():
        raise ValueError(f"{witness_path}: {name} detached signature hash does not reconcile")
    if verify_cryptography:
        _verify_cms_with_owner_powershell(data_path, signature_path, thumbprint)
    return data_path, signature_path, thumbprint


def _verify_external_witness_attestations(
    path: Path, value: dict[str, Any], capture_start: datetime,
    capture_end: datetime, policy: dict[str, Any], lease: dict[str, Any],
    event_trace: dict[str, Any], binding: dict[str, Any],
    expected_host: str | None, expected_lease_id: str | None,
    expected_policy_sha256: str | None, expected_source_head: str | None,
    expected_source_tree: str | None, expected_executable_sha256: str | None,
    allow_fixture: bool = False,
    owner_certificate_thumbprint: str | None = None,
) -> list[Path]:
    """Reconcile owner-signed payloads and their detached signature artifacts.

    PowerShell Capture performs the platform CMS chain check against an
    owner-supplied certificate thumbprint. This second stage independently
    recomputes all payload/signature hashes, checks the pinned signer is the
    same for every attestation, and reconciles claims to CTest receipts. A
    boolean in a JSON payload is never accepted without its signed bytes and
    detached signature.
    """
    artifacts: list[Path] = []
    proof_paths: dict[str, tuple[Path, Path]] = {}
    if owner_certificate_thumbprint is not None:
        try:
            owner_certificate_thumbprint = _normalize_thumbprint(owner_certificate_thumbprint)
        except ValueError as error:
            raise ValueError(f"{path}: independently supplied owner certificate thumbprint is malformed") from error
    if not allow_fixture and not owner_certificate_thumbprint:
        raise ValueError(f"{path}: independent owner certificate thumbprint is required")
    thumbprint: str | None = owner_certificate_thumbprint
    for name in ("policy", "lease", "event_loss", "execution_binding"):
        data_path, signature_path, actual_thumbprint = _verify_signed_proof(
            path, _proof(value, name), name, thumbprint,
            verify_cryptography=not allow_fixture)
        artifacts.extend((data_path, signature_path))
        proof_paths[name] = (data_path, signature_path)
        thumbprint = actual_thumbprint

    policy_path = proof_paths["policy"][0]
    lease_path = proof_paths["lease"][0]
    loss_path = proof_paths["event_loss"][0]
    binding_path = proof_paths["execution_binding"][0]
    policy_file = _read_json(policy_path, "identity policy")
    lease_file = _read_json(lease_path, "host lease receipt")
    loss_file = _read_json(loss_path, "ETW event-loss receipt")
    binding_file = _read_json(binding_path, "execution binding")
    witness_fixture = value.get("fixture_mode") is True or value.get("qualification_prohibited") is True
    observation_path: Path | None = None
    observation_hash: str | None = None
    if not witness_fixture:
        observation_value = value.get("observation_path")
        observation_hash = value.get("observation_sha256")
        if not isinstance(observation_value, str) or observation_value.startswith("<") \
                or not isinstance(observation_hash, str) or not SHA256_RE.fullmatch(observation_hash):
            raise ValueError(f"{path}: immutable observation path/hash is required")
        observation_path = _resolve(path, observation_value)
        if not observation_path.is_file() or _sha256(observation_path).casefold() != observation_hash.casefold():
            raise ValueError(f"{path}: immutable observation hash does not reconcile")
        if (loss_file.get("observation_sha256") != observation_hash
                or binding_file.get("observation_sha256") != observation_hash):
            raise ValueError(f"{path}: post-capture signed proofs are not bound to the immutable observation")

    if (policy_file.get("format") != "VOX_FX_T18_IDENTITY_POLICY_V1"
            or policy_file.get("owner_approved") is not True
            or policy_file.get("policy_id") != policy.get("policy_id")
            or policy_file.get("baseline_id") != policy.get("baseline_id")
            or not isinstance(policy_file.get("runner_service_identity_sha256"), list)
            or len(set(policy_file["runner_service_identity_sha256"])) != 4
            or set(policy_file["runner_service_identity_sha256"]) != set(value.get("runner_service_identities", []))):
        raise ValueError(f"{path}: signed identity baseline does not match the witness")
    _validate_identity_policy(policy_file, f"{path}: signed identity baseline")
    actual_policy_hash = _sha256(policy_path)
    if (actual_policy_hash.casefold() != str(policy.get("policy_sha256", "")).casefold()
            or actual_policy_hash.casefold() != str(value.get("identity_policy_sha256", "")).casefold()
            or actual_policy_hash.casefold() != str(_proof(value, "policy").get("data_sha256", "")).casefold()):
        raise ValueError(f"{path}: identity baseline hash does not match the witness")

    actual_lease_hash = _sha256(lease_path)
    if actual_lease_hash.casefold() != str(lease.get("verification_receipt_sha256", "")).casefold():
        raise ValueError(f"{path}: lease receipt hash does not match the witness")
    actual_loss_hash = _sha256(loss_path)
    if actual_loss_hash.casefold() != str(event_trace.get("loss_receipt_sha256", "")).casefold():
        raise ValueError(f"{path}: ETW loss receipt hash does not match the witness")
    actual_binding_hash = _sha256(binding_path)
    if actual_binding_hash.casefold() != str(binding.get("binding_receipt_sha256", "")).casefold():
        raise ValueError(f"{path}: execution binding hash does not match the witness")

    if (lease_file.get("format") != "VOX_FX_T18_HOST_LEASE_RECEIPT_V1"
            or lease_file.get("verified") is not True
            or lease_file.get("owner_approved") is not True
            or lease_file.get("attestation_source") != "owner_controlled_offline"
            or lease_file.get("verification_method") != "owner_signed_detached_cms"
            or lease_file.get("host") != lease.get("host")
            or lease_file.get("lease_id") != lease.get("lease_id")
            or lease_file.get("owner") != lease.get("owner")
            or lease_file.get("policy_sha256") != policy.get("policy_sha256")):
        raise ValueError(f"{path}: signed host lease does not reconcile with the witness")
    lease_start = _utc(lease_file.get("valid_from_utc"), "lease.valid_from_utc")
    lease_end = _utc(lease_file.get("valid_until_utc"), "lease.valid_until_utc")
    if lease_end < capture_end or lease_start > capture_start:
        raise ValueError(f"{path}: host lease validity does not cover the witness capture")

    if (loss_file.get("format") != "VOX_FX_T18_EVENT_LOSS_RECEIPT_V1"
            or loss_file.get("provider") != "Microsoft-Windows-Kernel-Process/ETW"
            or loss_file.get("measurement_method") != "ETW_PROCESS_TRACE_WITH_LOSS_COUNTERS"
            or loss_file.get("coverage_complete") is not True
            or type(loss_file.get("lost_events")) is not int or loss_file.get("lost_events") != 0
            or type(loss_file.get("buffer_loss_events")) is not int or loss_file.get("buffer_loss_events") != 0
            or type(loss_file.get("events_received")) is not int or loss_file.get("events_received") < 0
            or not isinstance(loss_file.get("session_name"), str) or not loss_file["session_name"]
            or loss_file.get("owner_approved") is not True
            or loss_file.get("attestation_source") != "owner_controlled_offline"
            or loss_file.get("verification_method") != "owner_signed_detached_cms"
            or not isinstance(loss_file.get("parser_provenance"), dict)):
        raise ValueError(f"{path}: signed ETW loss receipt is not measurable and lossless")
    collector_value = loss_file.get("collector_artifact_path")
    collector_hash = loss_file.get("collector_artifact_sha256")
    if not isinstance(collector_value, str) or not isinstance(collector_hash, str) or not SHA256_RE.fullmatch(collector_hash):
        raise ValueError(f"{path}: ETW collector artifact path/hash is missing")
    collector_path = _resolve(loss_path, collector_value)
    if (loss_file.get("collector_artifact_format") != "ETL"
            or loss_file.get("collector_parse_method") != "owner_independent_etw_parser_v1"):
        raise ValueError(f"{path}: ETW collector is not accompanied by the required independent parser contract")
    if not collector_path.is_file() or _sha256(collector_path).casefold() != collector_hash.casefold():
        raise ValueError(f"{path}: ETW collector artifact hash does not reconcile")
    parse_value = loss_file.get("collector_parse_receipt_path")
    parse_hash = loss_file.get("collector_parse_receipt_sha256")
    if not isinstance(parse_value, str) or not isinstance(parse_hash, str) or not SHA256_RE.fullmatch(parse_hash):
        raise ValueError(f"{path}: ETW parse receipt path/hash is missing")
    parse_path = _resolve(loss_path, parse_value)
    if not parse_path.is_file() or _sha256(parse_path).casefold() != parse_hash.casefold():
        raise ValueError(f"{path}: ETW parse receipt hash does not reconcile")
    loss_start = _utc(loss_file.get("coverage_start_utc"), "event_loss.coverage_start_utc")
    loss_end = _utc(loss_file.get("coverage_end_utc"), "event_loss.coverage_end_utc")
    if loss_start > capture_start or loss_end < capture_end:
        raise ValueError(f"{path}: signed ETW loss interval does not cover the witness")
    parse_file = _read_json(parse_path, "ETW parse receipt")
    if (parse_file.get("format") != "VOX_FX_T18_ETW_PARSE_RECEIPT_V1"
            or parse_file.get("provider") != "Microsoft-Windows-Kernel-Process/ETW"
            or parse_file.get("session_name") != loss_file.get("session_name")
            or str(parse_file.get("collector_artifact_sha256", "")).casefold() != collector_hash.casefold()
            or _utc(parse_file.get("coverage_start_utc"), "ETW parse coverage_start_utc") != loss_start
            or _utc(parse_file.get("coverage_end_utc"), "ETW parse coverage_end_utc") != loss_end
            or parse_file.get("events_received") != loss_file.get("events_received")
            or parse_file.get("lost_events") != loss_file.get("lost_events")
            or parse_file.get("buffer_loss_events") != loss_file.get("buffer_loss_events")
            or parse_file.get("process_events") != loss_file.get("process_events")
            or parse_file.get("parser_provenance") != loss_file.get("parser_provenance")):
        raise ValueError(f"{path}: ETW parse receipt does not reconcile to the signed loss counters/events")
    # Do not trust a JSON parse claim merely because its hash is signed.  The
    # independent parser replays the retained raw ETL (fixture mode replays the
    # deterministic raw fixture) and compares the decoded event digest/counters.
    try:
        ETL_PARSER.verify_receipt(collector_path, parse_file, fixture=allow_fixture)
    except (OSError, ValueError) as error:
        raise ValueError(f"{path}: independent raw ETL replay failed: {error}") from error
    parser_provenance = parse_file.get("parser_provenance")
    if not allow_fixture:
        join_value = parser_provenance.get("identity_join_path") if isinstance(parser_provenance, dict) else None
        join_hash = parser_provenance.get("identity_join_sha256") if isinstance(parser_provenance, dict) else None
        if (not isinstance(join_value, str) or not isinstance(join_hash, str)
                or not SHA256_RE.fullmatch(join_hash)):
            raise ValueError(f"{path}: ETW identity join provenance is missing")
        join_path = _resolve(parse_path, join_value)
        if not join_path.is_file() or _sha256(join_path).casefold() != join_hash.casefold():
            raise ValueError(f"{path}: ETW identity join hash does not reconcile")
        artifacts.append(join_path)
    etw_events = parse_file.get("process_events")
    if not isinstance(etw_events, list):
        raise ValueError(f"{path}: ETW parse receipt process-event list is missing")
    if (type(parse_file.get("events_received")) is not int
            or parse_file["events_received"] < 0
            or parse_file["events_received"] != len(etw_events)):
        raise ValueError(f"{path}: ETW received-event count does not equal the complete parsed process-event list")
    validated_etw = _validate_etw_process_events(etw_events, f"{path}: ETW parse receipt", loss_start, loss_end)
    for index, event in enumerate(validated_etw):
        if not _policy_approves_event(policy_file, event):
            raise ValueError(f"{path}: ETW process event {index} is denied or mismatched to the owner policy")
    private_value = value.get("private_inventory_path")
    if not isinstance(private_value, str) or private_value.startswith("<"):
        raise ValueError(f"{path}: private inventory is required for ETW/WMI identity correlation")
    private_path = _resolve(path, private_value)
    private_result = _validate_private_inventory(
        private_path, value, policy_file, value.get("samples", []), value.get("process_events", []),
        capture_start, capture_end)
    _correlate_events(validated_etw, private_result["events_value"], f"{path}: authoritative ETW/WMI")
    artifacts.extend((collector_path, parse_path, private_path))

    if (binding_file.get("format") != "VOX_FX_T18_EXECUTION_BINDING_V1"
            or binding_file.get("owner_approved") is not True
            or binding_file.get("attestation_source") != "owner_controlled_offline"
            or binding_file.get("verification_method") != "owner_signed_detached_cms"
            or binding_file.get("host") != binding.get("host")
            or binding_file.get("lease_id") != binding.get("lease_id")
            or binding_file.get("policy_sha256") != binding.get("policy_sha256")
            or binding_file.get("source_head") != binding.get("source_head")
            or binding_file.get("source_tree") != binding.get("source_tree")
            or binding_file.get("executable_sha256") != binding.get("executable_sha256")):
        raise ValueError(f"{path}: signed execution binding does not reconcile with the witness")
    profiles = binding_file.get("profiles")
    if (not isinstance(profiles, list)
            or {item.get("profile") for item in profiles if isinstance(item, dict)} != set(PROFILES)
            or profiles != binding.get("profiles")):
        raise ValueError(f"{path}: signed execution binding does not cover all three profiles")
    bound_receipts = binding_file.get("runner_receipts")
    if (not isinstance(bound_receipts, list) or len(bound_receipts) != 3
            or {item.get("profile") for item in bound_receipts if isinstance(item, dict)} != set(PROFILES)
            or bound_receipts != binding.get("runner_receipts")):
        raise ValueError(f"{path}: signed execution binding does not hash all runner receipts")
    for item in bound_receipts:
        if not isinstance(item, dict) or not isinstance(item.get("receipt_path"), str) \
                or not SHA256_RE.fullmatch(str(item.get("receipt_sha256", ""))):
            raise ValueError(f"{path}: signed runner receipt hash is malformed")
        receipt_path = _resolve(binding_path, item["receipt_path"])
        if not receipt_path.is_file() or _sha256(receipt_path).casefold() != item["receipt_sha256"].casefold():
            raise ValueError(f"{path}: signed runner receipt hash does not reconcile")
        artifacts.append(receipt_path)

    for expected, actual in ((expected_host, lease_file.get("host")),
                             (expected_lease_id, lease_file.get("lease_id")),
                             (expected_policy_sha256, policy.get("policy_sha256")),
                             (expected_source_head, binding_file.get("source_head")),
                             (expected_source_tree, binding_file.get("source_tree")),
                             (expected_executable_sha256, binding_file.get("executable_sha256"))):
        if expected and str(actual).casefold() != str(expected).casefold():
            raise ValueError(f"{path}: owner-signed host binding disagrees with trusted CTest evidence")
    return artifacts


def _canonical_text(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, float):
        return str(int(value)) if value.is_integer() else repr(value)
    return str(value)


def _canonical_timestamp(value: Any, name: str) -> str:
    return str(_utc_ticks(value, name))


def _canonical_bool(value: Any) -> str:
    return "true" if value is True else "false" if value is False else _canonical_text(value)


def _canonical_node_line(node: dict[str, Any]) -> str:
    fields = (
        node.get("pid"), node.get("process_name"), node.get("parent_pid"),
        _canonical_timestamp(node.get("start_time_utc"), "canonical.node.start_time_utc"),
        str(node.get("executable_path", "")).casefold(), str(node.get("executable_sha256", "")).casefold(),
        ",".join(sorted(str(item).casefold() for item in node.get("service_identity_sha256", []))),
        str(node.get("identity_digest", "")).casefold(),
    )
    return "\x1f".join(_canonical_text(item) for item in fields)


def _canonical_record_line(record: dict[str, Any]) -> str:
    ancestry = "\x1e".join(_canonical_node_line(node) for node in record.get("service_ancestry", []))
    event_timestamp = "" if record.get("timestamp_utc") is None else _canonical_timestamp(record["timestamp_utc"], "canonical.event.timestamp_utc")
    fields = (
        record.get("pid"), record.get("process_name"), record.get("parent_pid"),
        _canonical_timestamp(record.get("start_time_utc"), "canonical.record.start_time_utc"),
        str(record.get("executable_path", "")).casefold(), str(record.get("executable_sha256", "")).casefold(),
        ",".join(sorted(str(item).casefold() for item in record.get("service_identity_sha256", []))),
        _canonical_bool(record.get("service_ancestry_verified")), record.get("lineage_verification"),
        _canonical_bool(record.get("identity_known")), _canonical_bool(record.get("approved_by_policy")),
        str(record.get("identity_digest", "")).casefold(), record.get("decision_reason"),
        record.get("event"), event_timestamp, record.get("event_process_name"), ancestry,
    )
    return "\x1f".join(_canonical_text(item) for item in fields)


def _canonical_sample_digest(sample: dict[str, Any]) -> str:
    process_lines = sorted(_canonical_record_line(item) for item in sample.get("processes", []))
    event_lines = sorted(_canonical_record_line(item) for item in sample.get("events", []))
    monotonic = sample.get("monotonic_elapsed_ms")
    if isinstance(monotonic, float) and monotonic.is_integer():
        monotonic = int(monotonic)
    lines = [
        _canonical_text(sample.get("phase")), _canonical_text(sample.get("sample_ordinal")),
        _canonical_timestamp(sample.get("timestamp_utc"), "canonical.sample.timestamp_utc"),
        _canonical_timestamp(sample.get("scheduled_timestamp_utc"), "canonical.sample.scheduled_timestamp_utc"),
        _canonical_text(monotonic), _canonical_text(sample.get("query_error")),
        _canonical_text(sample.get("full_host_process_count")),
        _canonical_text(sample.get("full_host_hash_count")),
        _canonical_text(sample.get("full_host_cim_hash_duration_ms")),
        _canonical_bool(sample.get("full_host_cim_hash_cadence_met")),
        "processes=" + "\x1e".join(process_lines), "events=" + "\x1e".join(event_lines),
    ]
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest()


def _validate_private_record(record: Any, label: str, policy_file: dict[str, Any],
                             event_record: bool = False) -> dict[str, Any]:
    if not isinstance(record, dict):
        raise ValueError(f"{label}: private process identity is not an object")
    required = ("pid", "process_name", "parent_pid", "start_time_utc", "executable_path",
                "executable_sha256", "service_identity_sha256", "service_ancestry_verified",
                "lineage_verification", "service_ancestry", "identity_known", "approved_by_policy",
                "identity_digest")
    missing = [field for field in required if field not in record]
    if missing:
        raise ValueError(f"{label}: private process identity is missing {', '.join(missing)}")
    if (type(record["pid"]) is not int or record["pid"] < 1 or not isinstance(record["process_name"], str)
            or not record["process_name"] or type(record["parent_pid"]) is not int or record["parent_pid"] < 0
            or not isinstance(record["executable_path"], str) or not record["executable_path"]
            or not SHA256_RE.fullmatch(str(record["executable_sha256"]))
            or type(record["service_ancestry_verified"]) is not bool
            or type(record["identity_known"]) is not bool or type(record["approved_by_policy"]) is not bool
            or not isinstance(record["service_identity_sha256"], list) or not record["service_identity_sha256"]
            or any(not SHA256_RE.fullmatch(str(item)) for item in record["service_identity_sha256"])
            or not SHA256_RE.fullmatch(str(record["identity_digest"]))):
        raise ValueError(f"{label}: private process identity is incomplete")
    _utc(record["start_time_utc"], f"{label}.start_time_utc")
    if event_record:
        if record.get("event") not in {"start", "stop"} or "timestamp_utc" not in record:
            raise ValueError(f"{label}: private process event is malformed")
        event_time = _utc(record["timestamp_utc"], f"{label}.timestamp_utc")
        if event_time < _utc(record["start_time_utc"], f"{label}.start_time_utc"):
            raise ValueError(f"{label}: private process event precedes process creation")
        if record.get("event_process_name") not in (None, record["process_name"]):
            raise ValueError(f"{label}: provider event name disagrees with the process identity")
    if _identity_digest(record).casefold() != str(record["identity_digest"]).casefold():
        raise ValueError(f"{label}: private identity digest does not reconcile")
    _validate_lineage(record, label)
    runner_services = {str(item).casefold() for item in policy_file.get("runner_service_identity_sha256", [])}
    if not {str(item).casefold() for item in record["service_identity_sha256"]}.issubset(runner_services):
        raise ValueError(f"{label}: private ancestry does not terminate at an owner-baselined runner service")
    if record["identity_known"] is not True or record["approved_by_policy"] is not True \
            or not _policy_approves_event(policy_file, record):
        raise ValueError(f"{label}: private inventory contains an unknown or unauthorized process identity")
    return record


def _validate_private_inventory(path: Path, value: dict[str, Any], policy_file: dict[str, Any],
                                public_samples: list[dict[str, Any]], public_events: list[dict[str, Any]],
                                capture_start: datetime, capture_end: datetime) -> dict[str, Any]:
    inventory = _read_json(path, "private process inventory")
    if inventory.get("format") != PRIVATE_INVENTORY_FORMAT:
        raise ValueError(f"{path}: private inventory format is not supported")
    if inventory.get("canonicalization") != "fx-t18-private-sample-canonical-v2-utc-ticks" \
            or inventory.get("cadence_contract") != "full_host_cim_process_snapshot_and_sha256_each_executable_le_1000ms_every_1000ms":
        raise ValueError(f"{path}: private inventory canonicalization/cadence contract is missing")
    if (_utc(inventory.get("capture_start_utc"), f"{path}.capture_start_utc") != capture_start
            or _utc(inventory.get("capture_end_utc"), f"{path}.capture_end_utc") != capture_end):
        raise ValueError(f"{path}: private inventory interval does not match the witness")
    if str(inventory.get("policy_sha256", "")).casefold() != str(value.get("identity_policy_sha256", "")).casefold():
        raise ValueError(f"{path}: private inventory policy hash does not match the witness")
    raw_samples = inventory.get("samples")
    raw_events = inventory.get("process_events")
    if not isinstance(raw_samples, list) or not isinstance(raw_events, list) or len(raw_samples) != len(public_samples):
        raise ValueError(f"{path}: private inventory samples/events are incomplete")
    flattened_events: list[dict[str, Any]] = []
    seen_processes: dict[int, tuple[int, str]] = {}
    for index, (raw, public) in enumerate(zip(raw_samples, public_samples)):
        label = f"{path}.samples[{index}]"
        if not isinstance(raw, dict) or not isinstance(public, dict):
            raise ValueError(f"{label}: sample is not an object")
        for field in ("phase", "sample_ordinal"):
            if raw.get(field) != public.get(field):
                raise ValueError(f"{label}: private/public sample field {field} disagrees")
        for field in ("timestamp_utc", "scheduled_timestamp_utc"):
            if _utc(raw.get(field), f"{label}.{field}") != _utc(public.get(field), f"public.{field}"):
                raise ValueError(f"{label}: private/public sample time {field} disagrees")
        if not isinstance(raw.get("monotonic_elapsed_ms"), (int, float)) \
                or not isinstance(public.get("monotonic_elapsed_ms"), (int, float)) \
                or abs(float(raw["monotonic_elapsed_ms"]) - float(public["monotonic_elapsed_ms"])) > 1e-6:
            raise ValueError(f"{label}: private/public monotonic sample value disagrees")
        for field in ("full_host_process_count", "full_host_hash_count", "full_host_cim_hash_duration_ms", "full_host_cim_hash_cadence_met"):
            if raw.get(field) != public.get(field):
                raise ValueError(f"{label}: private/public cadence summary {field} disagrees")
        sample_time = _utc(raw["timestamp_utc"], f"{label}.timestamp_utc")
        if sample_time < capture_start or sample_time > capture_end:
            raise ValueError(f"{label}: private sample lies outside the witness interval")
        processes = raw.get("processes")
        events = raw.get("events")
        if not isinstance(processes, list) or not isinstance(events, list):
            raise ValueError(f"{label}: private sample process/event arrays are missing")
        if (raw.get("full_host_process_count") != len(processes)
                or raw.get("full_host_hash_count") != len(processes)
                or raw.get("full_host_cim_hash_cadence_met") is not True
                or type(raw.get("full_host_cim_hash_duration_ms")) not in (int, float)
                or raw["full_host_cim_hash_duration_ms"] < 0.0
                or raw["full_host_cim_hash_duration_ms"] > 1000.0):
            raise ValueError(f"{label}: private full-host CIM/SHA-256 cadence measurement is incomplete")
        approved_count = 0
        process_unknown = process_unexpected = event_unknown = event_unexpected = 0
        reuse_count = 0
        for process_index, process in enumerate(processes):
            record = _validate_private_record(process, f"{label}.processes[{process_index}]", policy_file)
            lifecycle = (_utc_ticks(record["start_time_utc"], f"{label}.process.start_time_utc"), str(record["identity_digest"]).casefold())
            previous_lifecycle = seen_processes.get(record["pid"])
            if previous_lifecycle is not None and previous_lifecycle != lifecycle:
                reuse_count += 1
                raise ValueError(f"{label}: private snapshots show PID reuse or identity mutation")
            seen_processes[record["pid"]] = lifecycle
            approved_count += int(record["approved_by_policy"] is True)
            process_unknown += int(record["identity_known"] is not True)
            process_unexpected += int(record["approved_by_policy"] is not True)
        for event_index, event in enumerate(events):
            record = _validate_private_record(event, f"{label}.events[{event_index}]", policy_file, event_record=True)
            flattened_events.append(record)
            event_unknown += int(record["identity_known"] is not True)
            event_unexpected += int(record["approved_by_policy"] is not True)
            reuse_count += int(record.get("decision_reason") == "PID_REUSE_OR_DYNAMIC_IDENTITY_MISMATCH")
        expected_digest = _canonical_sample_digest(raw)
        if str(public.get("process_inventory_sha256", "")).casefold() != expected_digest:
            raise ValueError(f"{label}: public process inventory digest does not match private canonical content")
        if (public.get("observed_process_count") != len(processes)
                or public.get("approved_process_count") != approved_count
                or public.get("unknown_identity_count") != process_unknown + event_unknown
                or public.get("unexpected_identity_count") != process_unexpected + event_unexpected
                or public.get("pid_reuse_mismatch_count") != reuse_count
                or public.get("process_query_error") != raw.get("query_error")):
            raise ValueError(f"{label}: public process summary does not reconcile with private identities")
    for aggregate_index, aggregate in enumerate(raw_events):
        _validate_private_record(aggregate, f"{path}.process_events[{aggregate_index}]", policy_file, event_record=True)
    if Counter(_canonical_record_line(item) for item in raw_events) != Counter(_canonical_record_line(item) for item in flattened_events):
        raise ValueError(f"{path}: private aggregate process events do not reconcile with per-sample events")
    if len(public_events) != len(flattened_events):
        raise ValueError(f"{path}: public process event count does not reconcile with private events")
    unused = set(range(len(flattened_events)))
    for index, public in enumerate(public_events):
        candidates = []
        for event_index in sorted(unused):
            raw = flattened_events[event_index]
            if (public.get("event") == raw.get("event")
                    and str(public.get("identity_digest", "")).casefold() == str(raw.get("identity_digest", "")).casefold()
                    and _utc(public.get("timestamp_utc"), f"process_events[{index}].timestamp_utc")
                    == _utc(raw.get("timestamp_utc"), f"private.events[{event_index}].timestamp_utc")):
                candidates.append(event_index)
        if len(candidates) != 1:
            raise ValueError(f"{path}: public process event {index} is missing, ambiguous, or mismatched")
        unused.remove(candidates[0])
    if unused:
        raise ValueError(f"{path}: private process events are not all publicly accounted for")
    return {"verified": True, "samples": len(raw_samples), "events": len(flattened_events),
            "path": str(path), "samples_value": raw_samples, "events_value": flattened_events}

def _validate_process_identity(process: Any, label: str) -> None:
    if not isinstance(process, dict) or type(process.get("pid")) is not int \
            or not isinstance(process.get("process_name"), str):
        raise ValueError(f"{label}: malformed synthetic process identity")
    if process.get("identity_known") is not True or process.get("approved_by_policy") is not True:
        raise ValueError(f"{label}: synthetic process identity is not approved")
    if process.get("command_line") not in (None, "<redacted>"):
        raise ValueError(f"{label}: command-line secret was not redacted")


def _validate_public_sample(sample: dict[str, Any], label: str, allow_fixture: bool) -> None:
    if "processes" in sample:
        if not allow_fixture:
            raise ValueError(f"{label}: public witness contains raw process inventory")
        for index, process in enumerate(sample.get("processes", [])):
            _validate_process_identity(process, f"{label}.processes[{index}]")
        return
    for field in ("observed_process_count", "approved_process_count", "unknown_identity_count",
                  "unexpected_identity_count", "pid_reuse_mismatch_count", "full_host_process_count",
                  "full_host_hash_count"):
        if type(sample.get(field)) is not int or sample[field] < 0:
            raise ValueError(f"{label}: process summary field {field} is invalid")
    if (sample["full_host_process_count"] != sample["observed_process_count"]
            or sample["full_host_hash_count"] != sample["full_host_process_count"]
            or type(sample.get("full_host_cim_hash_cadence_met")) is not bool
            or sample["full_host_cim_hash_cadence_met"] is not True
            or type(sample.get("full_host_cim_hash_duration_ms")) not in (int, float)
            or sample["full_host_cim_hash_duration_ms"] < 0.0
            or sample["full_host_cim_hash_duration_ms"] > 1000.0):
        raise ValueError(f"{label}: full-host CIM/SHA-256 observation did not meet the measured 1 Hz contract")
    if sample.get("process_query_error") not in (None, ""):
        raise ValueError(f"{label}: process query error is evidence-blocking")
    digest = sample.get("process_inventory_sha256")
    if not isinstance(digest, str) or not SHA256_RE.fullmatch(digest):
        raise ValueError(f"{label}: private process inventory hash is missing")
    if (sample["unknown_identity_count"] or sample["unexpected_identity_count"]
            or sample["pid_reuse_mismatch_count"]):
        raise ValueError(f"{label}: unknown, unexpected, or reused process identity is present")


def validate_witness(
    path: Path,
    execution_intervals: dict[str, dict[str, Any]] | None = None,
    expected_host: str | None = None,
    expected_lease_id: str | None = None,
    expected_policy_sha256: str | None = None,
    expected_source_head: str | None = None,
    expected_source_tree: str | None = None,
    expected_executable_sha256: str | None = None,
    require_external_attestations: bool = False,
    allow_fixture: bool = False,
    owner_certificate_thumbprint: str | None = None,
) -> dict[str, Any]:
    """Validate monotonic/deadline sampling and host-wide identity summaries."""
    value = _read_json(path, "host witness")
    if value.get("format") != "VOX_FX_T18_HOST_WITNESS_V1":
        raise ValueError(f"{path}: unsupported host witness format")
    fixture = value.get("fixture_mode") is True or value.get("qualification_prohibited") is True
    if fixture and not allow_fixture:
        raise ValueError(f"{path}: fixture_mode/qualification_prohibited witness cannot be production evidence")
    if not fixture and value.get("schema_version") != 2:
        raise ValueError(f"{path}: host witness schema_version 2 is required")
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
        raise ValueError(f"{path}: host witness has polling/deadline gaps")
    if (capture_end - capture_start).total_seconds() < pre_seconds + during_seconds + post_seconds:
        raise ValueError(f"{path}: capture interval is shorter than declared witness windows")

    policy = value.get("identity_policy")
    if not isinstance(policy, dict) or policy.get("format") != "VOX_FX_T18_IDENTITY_POLICY_V1" \
            or not isinstance(policy.get("policy_id"), str) or not policy["policy_id"] \
            or not isinstance(policy.get("baseline_id"), str) or not policy["baseline_id"] \
            or not isinstance(policy.get("policy_sha256"), str) or not SHA256_RE.fullmatch(policy["policy_sha256"]):
        raise ValueError(f"{path}: signed owner identity baseline summary is missing")
    service_hashes = policy.get("runner_service_identity_sha256")
    if service_hashes is None:
        service_hashes = value.get("runner_service_identities")
    if not isinstance(service_hashes, list) or len(set(service_hashes)) != 4 \
            or any(not isinstance(item, str) or not SHA256_RE.fullmatch(item) for item in service_hashes):
        raise ValueError(f"{path}: all four owner-baselined HAMUNAPTRA runner service identities are required")
    if expected_policy_sha256 and policy["policy_sha256"].casefold() != expected_policy_sha256.casefold():
        raise ValueError(f"{path}: witness process policy does not match trusted CTest receipts")

    event_trace = value.get("event_trace")
    if not isinstance(event_trace, dict):
        raise ValueError(f"{path}: event trace summary is missing")
    if not fixture:
        required_event = (
            event_trace.get("available") is True,
            event_trace.get("coverage_complete") is True,
            event_trace.get("observation_provider") == "Win32_ProcessStartTrace/Win32_ProcessStopTrace",
            event_trace.get("loss_provider") == "Microsoft-Windows-Kernel-Process/ETW",
            event_trace.get("loss_accounting") == "etw_provider_counters",
            type(event_trace.get("lost_events")) is int and event_trace.get("lost_events") == 0,
            type(event_trace.get("buffer_loss_events")) is int and event_trace.get("buffer_loss_events") == 0,
            type(event_trace.get("events_received")) is int and event_trace.get("events_received") >= 0,
            event_trace.get("collector_artifact_format") == "ETL",
            event_trace.get("collector_parse_method") == "owner_independent_etw_parser_v1",
            type(event_trace.get("correlation_max_skew_seconds")) in (int, float)
            and event_trace.get("correlation_max_skew_seconds") == MAX_EVENT_CORRELATION_SKEW_SECONDS,
            set(event_trace.get("correlation_identity_fields", [])) >= {
                "event", "pid", "process_name", "start_time_utc", "identity_digest", "executable_path",
                "executable_sha256", "parent_pid", "service_identity_sha256"},
            isinstance(event_trace.get("collector_artifact_sha256"), str)
            and SHA256_RE.fullmatch(event_trace["collector_artifact_sha256"]) is not None,
            isinstance(event_trace.get("collector_parse_receipt_sha256"), str)
            and SHA256_RE.fullmatch(event_trace["collector_parse_receipt_sha256"]) is not None,
        )
        if not all(required_event):
            raise ValueError(f"{path}: independently measurable ETW event loss/completeness is absent")
        trace_start = _utc(event_trace.get("coverage_start_utc"), "event_trace.coverage_start_utc")
        trace_end = _utc(event_trace.get("coverage_end_utc"), "event_trace.coverage_end_utc")
        if trace_start > capture_start or trace_end < capture_end or trace_end <= trace_start:
            raise ValueError(f"{path}: event trace interval does not cover the witness")
    elif event_trace.get("coverage_complete") is not True:
        # A fixture may demonstrate an unverified WMI/ETW claim, but it cannot
        # be accepted as a production lossless trace.
        pass

    samples = value.get("samples")
    if not isinstance(samples, list) or not samples:
        raise ValueError(f"{path}: host witness has no samples")
    phase_samples: dict[str, list[tuple[datetime, dict[str, Any]]]] = {phase: [] for phase in REQUIRED_PHASES}
    phase_order = {phase: index for index, phase in enumerate(REQUIRED_PHASES)}
    previous_phase = -1
    all_times: list[datetime] = []
    scheduled_times: list[datetime] = []
    previous_monotonic: float | None = None
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
        _validate_public_sample(sample, f"samples[{index}]", allow_fixture)
        if "scheduled_timestamp_utc" not in sample or "monotonic_elapsed_ms" not in sample:
            raise ValueError(f"{path}: sample {index} is missing its global deadline/monotonic fields")
        scheduled = _utc(sample["scheduled_timestamp_utc"], f"samples[{index}].scheduled_timestamp_utc")
        if scheduled_times and scheduled <= scheduled_times[-1]:
            raise ValueError(f"{path}: sample deadlines are not strictly increasing")
        if abs((timestamp - scheduled).total_seconds()) > MAX_SAMPLE_GAP_SECONDS:
            raise ValueError(f"{path}: sample missed its monotonic/deadline reconciliation")
        monotonic = sample.get("monotonic_elapsed_ms")
        if monotonic is not None:
            if type(monotonic) not in (int, float) or monotonic < 0.0 \
                    or (previous_monotonic is not None and monotonic <= previous_monotonic):
                raise ValueError(f"{path}: monotonic sample clock is not strictly increasing")
            previous_monotonic = float(monotonic)
        phase_samples[phase].append((timestamp, sample))
        all_times.append(timestamp); scheduled_times.append(scheduled)
    if any(right <= left for left, right in zip(all_times, all_times[1:])):
        raise ValueError(f"{path}: sample timestamps are not strictly increasing")
    gaps = [(right - left).total_seconds() for left, right in zip(all_times, all_times[1:])]
    if any(gap > MAX_SAMPLE_GAP_SECONDS for gap in gaps):
        raise ValueError(f"{path}: host sample cadence has a gap larger than 2s")
    for phase, declared_seconds in (("pre", pre_seconds), ("during", during_seconds), ("post", post_seconds)):
        entries = phase_samples[phase]
        if len(entries) < declared_seconds + 1:
            raise ValueError(f"{path}: {phase} is missing one-Hz samples")
        if (entries[-1][0] - entries[0][0]).total_seconds() < declared_seconds:
            raise ValueError(f"{path}: {phase} timestamp span is shorter than its declaration")

    events = value.get("process_events", [])
    if not isinstance(events, list):
        raise ValueError(f"{path}: malformed process event list")
    event_times: list[datetime] = []
    for index, event in enumerate(events):
        if not isinstance(event, dict) or event.get("event") not in {"start", "stop", "unparsed"}:
            raise ValueError(f"{path}: malformed process event {index}")
        if not allow_fixture and ("pid" in event or "process_name" in event or "command_line" in event):
            raise ValueError(f"{path}: public event trace contains raw process identity")
        if event.get("identity_known") is not True or event.get("approved_by_policy") is not True:
            raise ValueError(f"{path}: unknown or unexpected process event is present")
        if not isinstance(event.get("identity_digest"), str) or not SHA256_RE.fullmatch(event["identity_digest"]):
            raise ValueError(f"{path}: event identity digest is missing")
        event_time = _utc(event.get("timestamp_utc"), f"process_events[{index}].timestamp_utc")
        if event_time < capture_start or event_time > capture_end:
            raise ValueError(f"{path}: process event lies outside capture interval")
        event_times.append(event_time)
    if any(right < left for left, right in zip(event_times, event_times[1:])):
        raise ValueError(f"{path}: process event timestamps are not monotonic")

    private_result: dict[str, Any] | None = None
    private_value = value.get("private_inventory_path")
    if isinstance(private_value, str) and not private_value.startswith("<"):
        private_path = _resolve(path, private_value)
        private_hash = value.get("private_inventory_sha256")
        if not private_path.is_file() or not isinstance(private_hash, str)                 or not SHA256_RE.fullmatch(private_hash) or _sha256(private_path).casefold() != private_hash.casefold():
            raise ValueError(f"{path}: private inventory path/hash does not reconcile")
        policy_path_value = value.get("identity_policy_path")
        if not isinstance(policy_path_value, str) or policy_path_value.startswith("<"):
            raise ValueError(f"{path}: identity policy path is required for private reconciliation")
        policy_path = _resolve(path, policy_path_value)
        policy_file = _read_json(policy_path, "identity policy")
        _validate_identity_policy(policy_file, f"{path}: private reconciliation policy")
        if _sha256(policy_path).casefold() != str(value.get("identity_policy_sha256", "")).casefold():
            raise ValueError(f"{path}: private reconciliation policy hash does not reconcile")
        private_result = _validate_private_inventory(
            private_path, value, policy_file, samples, events, capture_start, capture_end)
    elif not fixture:
        raise ValueError(f"{path}: private inventory is required for production evidence")

    for field in ("unrecognized_competitors", "unknown_pids", "pid_reuse_mismatches"):
        if type(value.get(field)) is not int or value[field] < 0:
            raise ValueError(f"{path}: {field} must be a non-identifying counter")
        if value[field] != 0:
            raise ValueError(f"{path}: {field} reports an identity/exclusivity failure")
    if value.get("evidence_blocked") is not False and not allow_fixture:
        raise ValueError(f"{path}: host witness is already evidence-blocked")

    lease = value.get("runner_lease")
    if not isinstance(lease, dict) or lease.get("recognized") is not True \
            or lease.get("owner_verified") is not True \
            or not isinstance(lease.get("lease_id"), str) or not lease["lease_id"] \
            or not isinstance(lease.get("host"), str) or not lease["host"] \
            or not isinstance(lease.get("verification_receipt_sha256"), str) \
            or not SHA256_RE.fullmatch(lease["verification_receipt_sha256"]):
        raise ValueError(f"{path}: runner lease is not owner-verified")
    if expected_host and lease["host"].casefold() != expected_host.casefold():
        raise ValueError(f"{path}: witness host does not match trusted runner receipts")
    if expected_lease_id and lease["lease_id"] != expected_lease_id:
        raise ValueError(f"{path}: witness lease does not match trusted runner receipts")
    for secret_field in ("token", "lease_token", "command_line"):
        if secret_field in lease and lease[secret_field] not in (None, "<redacted>"):
            raise ValueError(f"{path}: sensitive lease field {secret_field} is not redacted")

    services = value.get("runner_service_identities")
    if not isinstance(services, list) or set(services) != set(service_hashes):
        raise ValueError(f"{path}: observed runner service baseline does not reconcile with all four services")
    binding = value.get("execution_binding")
    if not isinstance(binding, dict) or binding.get("verified") is not True:
        raise ValueError(f"{path}: trusted CTest execution binding is missing")
    for field in ("host", "runner_job_id", "source_head", "source_tree", "executable_sha256", "lease_id", "policy_sha256"):
        if not isinstance(binding.get(field), str) or not binding[field]:
            raise ValueError(f"{path}: execution binding field {field} is missing")
    if not SHA1_RE.fullmatch(binding["source_head"]) or not SHA1_RE.fullmatch(binding["source_tree"]) \
            or not SHA256_RE.fullmatch(binding["executable_sha256"]) or not SHA256_RE.fullmatch(binding["policy_sha256"]):
        raise ValueError(f"{path}: execution binding hash is malformed")
    if binding["host"].casefold() != lease["host"].casefold() or binding["lease_id"] != lease["lease_id"] \
            or binding["policy_sha256"].casefold() != policy["policy_sha256"].casefold():
        raise ValueError(f"{path}: execution binding disagrees with lease or policy")
    if expected_source_head and binding["source_head"].casefold() != expected_source_head.casefold():
        raise ValueError(f"{path}: witness source HEAD does not match trusted CTest receipts")
    if expected_source_tree and binding["source_tree"].casefold() != expected_source_tree.casefold():
        raise ValueError(f"{path}: witness source tree does not match trusted CTest receipts")
    if expected_executable_sha256 and binding["executable_sha256"].casefold() != expected_executable_sha256.casefold():
        raise ValueError(f"{path}: witness executable hash does not match trusted CTest receipts")

    binding_profiles = binding.get("profiles")
    if not isinstance(binding_profiles, list) or {item.get("profile") for item in binding_profiles if isinstance(item, dict)} != set(PROFILES):
        raise ValueError(f"{path}: execution binding does not contain all three CTest profiles")
    bound_receipts = binding.get("runner_receipts")
    if not fixture:
        if not isinstance(bound_receipts, list) or len(bound_receipts) != 3 \
                or {item.get("profile") for item in bound_receipts if isinstance(item, dict)} != set(PROFILES):
            raise ValueError(f"{path}: execution binding does not hash all three runner receipts")
        for item in bound_receipts:
            if not isinstance(item, dict) or not isinstance(item.get("receipt_path"), str) \
                    or not SHA256_RE.fullmatch(str(item.get("receipt_sha256", ""))):
                raise ValueError(f"{path}: bound runner receipt hash is malformed")
    intervals = execution_intervals or {}
    if set(intervals) != set(PROFILES):
        raise ValueError(f"{path}: witness is not bound to all routed/authored/typical execution intervals")
    for profile, state in intervals.items():
        start = state.get("test_start"); end = state.get("test_end")
        if not isinstance(start, datetime) or not isinstance(end, datetime) or end <= start:
            raise ValueError(f"{path}: invalid trusted {profile} test interval")
        if start < capture_start or end > capture_end:
            raise ValueError(f"{path}: witness does not contain the complete {profile} CTest interval")
        matching = [item for item in binding_profiles if item.get("profile") == profile]
        if len(matching) != 1:
            raise ValueError(f"{path}: binding profile {profile} is ambiguous")
        bound_start = _utc(matching[0].get("test_start_utc"), f"binding.{profile}.test_start_utc")
        bound_end = _utc(matching[0].get("test_end_utc"), f"binding.{profile}.test_end_utc")
        if bound_start != start or bound_end != end:
            raise ValueError(f"{path}: phase/CTest interval mismatch for {profile}")
        before = [stamp for stamp in all_times if stamp <= start]
        after = [stamp for stamp in all_times if stamp >= end]
        if not before or not after or (start - before[-1]).total_seconds() > MAX_SAMPLE_GAP_SECONDS \
                or (after[0] - end).total_seconds() > MAX_SAMPLE_GAP_SECONDS:
            raise ValueError(f"{path}: sample stream does not cover {profile} CTest interval")
        during_times = [stamp for stamp, _ in phase_samples["during"]]
        if not during_times or during_times[0] > start or during_times[-1] < end:
            raise ValueError(f"{path}: during-phase samples do not cover {profile} CTest interval")

    external_attestations: list[Path] = []
    if require_external_attestations:
        external_attestations = _verify_external_witness_attestations(
            path, value, capture_start, capture_end, policy, lease, event_trace, binding,
            expected_host, expected_lease_id, expected_policy_sha256,
            expected_source_head, expected_source_tree, expected_executable_sha256,
            allow_fixture=allow_fixture, owner_certificate_thumbprint=owner_certificate_thumbprint)

    return {"verified": True, "path": str(path), "samples": len(samples),
            "process_events": len(events), "capture_start": capture_start, "capture_end": capture_end,
            "policy_sha256": policy["policy_sha256"], "host": lease["host"],
            "lease_id": lease["lease_id"], "source_head": binding["source_head"],
            "source_tree": binding["source_tree"], "executable_sha256": binding["executable_sha256"],
            "private_inventory": private_result,
            "external_attestations": [str(item) for item in external_attestations]}


def _scan_observed_failures(path: Path) -> dict[str, bool]:
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
                result["safety_failure"] |= any(int(values[field]) > 0 for field in ("alloc_calls", "lock_entries", "wait_entries"))
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
            expected_codes = [code for flag, (_, code) in zip(flags, AUDIT.FUNCTIONAL_REASON_ORDER) if flag == 0]
            consistent = (functional["reason_codes"] == expected_codes
                          and functional["functional_pass"] == int(not expected_codes)
                          and (initial_voices == 8 or functional["voice8_grain_invariant"] == 1))
        except (KeyError, TypeError, ValueError):
            consistent = False
        if not consistent:
            result["functional_evidence_invalid"] = True
        elif functional["functional_pass"] == 0:
            result["functional_failure"] = True
    return result


def _load_outcome(path: Path) -> dict[str, Any]:
    outcome = _read_json(path, "post-test outcome")
    required = ("format", "scenario", "test_identity", "executable_path", "executable_sha256",
                "test_start_utc", "test_end_utc", "ctest_exit_code", "native_exit_code",
                "ctest_outcome", "execution_complete", "timed_out", "crashed", "skipped",
                "junit_path", "junit_tests", "junit_failures", "junit_errors", "junit_skipped",
                "junit_timeout", "cpu_only_failure", "cpu_only_failure_reason")
    missing = [field for field in required if field not in outcome]
    if missing:
        raise ValueError(f"post-test outcome is missing {', '.join(missing)}")
    if outcome["format"] != "VOX_FX_T18_POST_TEST_OUTCOME_V1":
        raise ValueError("unsupported post-test outcome format")
    for field in ("scenario", "test_identity", "executable_path", "executable_sha256", "junit_path", "ctest_outcome", "cpu_only_failure_reason"):
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
        raise ValueError("cpu-only outcome lacks required reason")
    junit_path = Path(outcome["junit_path"])
    if not junit_path.is_absolute():
        junit_path = path.parent / junit_path
    if not junit_path.is_file():
        raise ValueError(f"post-test outcome JUnit artifact is missing: {junit_path}")
    junit_summary = AUDIT._junit_summary(junit_path)
    for field, actual in (("junit_tests", junit_summary["tests"]), ("junit_failures", junit_summary["failures"]),
                          ("junit_errors", junit_summary["errors"]), ("junit_skipped", junit_summary["skipped"]),
                          ("junit_timeout", junit_summary["timeout"])):
        if outcome[field] != actual:
            raise ValueError(f"post-test outcome {field} does not match JUnit")
    return {"value": outcome, "test_start": start, "test_end": end, "junit": junit_summary}


def _execution_state(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {"execution_failure": False, "execution_blocked": True, "evidence_blocked": True, "receipt_missing": True}
    try:
        receipt = _read_json(path, "runner receipt")
        required = ("format", "scenario", "test_identity", "test_start_utc", "test_end_utc", "machine",
                    "host_lease_id", "host_policy_sha256", "post_test_outcome_path", "ctest_raw_path",
                    "ctest_inventory_path", "ctest_junit_path", "executable_path", "executable_sha256",
                    "ctest_selected_executable", "source_head", "source_tree", "runner_job_id")
        missing = [field for field in required if field not in receipt]
        if missing:
            raise ValueError(f"runner receipt is missing {', '.join(missing)}")
        for field in ("scenario", "test_identity", "test_start_utc", "test_end_utc", "machine", "host_lease_id",
                      "host_policy_sha256", "post_test_outcome_path", "ctest_raw_path", "ctest_inventory_path",
                      "ctest_junit_path", "executable_path", "executable_sha256", "ctest_selected_executable",
                      "source_head", "source_tree", "runner_job_id"):
            if not isinstance(receipt[field], str) or not receipt[field]:
                raise ValueError(f"runner receipt {field} is not a non-empty string")
        if receipt["test_identity"] != "suite_fx_runtime_stress_tests":
            raise ValueError("runner receipt test identity is not the FX stress test")
        if not SHA256_RE.fullmatch(receipt["host_policy_sha256"]) or not SHA256_RE.fullmatch(receipt["executable_sha256"]):
            raise ValueError("runner receipt policy/executable SHA-256 is malformed")
        if not SHA1_RE.fullmatch(receipt["source_head"]) or not SHA1_RE.fullmatch(receipt["source_tree"]):
            raise ValueError("runner receipt source SHA-1 is malformed")
        start = _utc(receipt["test_start_utc"], "receipt.test_start_utc")
        receipt_end = None if receipt["test_end_utc"] in ("post-test-outcome", "") else _utc(receipt["test_end_utc"], "receipt.test_end_utc")
        outcome_path = _resolve(path, receipt["post_test_outcome_path"])
        artifact_paths: dict[str, Path] = {}
        for field in ("ctest_raw_path", "ctest_inventory_path", "ctest_junit_path"):
            artifact = _resolve(path, receipt[field])
            if not artifact.is_file() or artifact.stat().st_size == 0:
                raise ValueError(f"runner receipt {field} is missing or empty")
            artifact_paths[field] = artifact
        outcome_info = _load_outcome(outcome_path); outcome = outcome_info["value"]
        native_evidence = AUDIT.reconcile_native_ctest(artifact_paths["ctest_raw_path"], artifact_paths["ctest_junit_path"], outcome["test_identity"], outcome["ctest_exit_code"])
        if outcome["scenario"] != receipt["scenario"] or outcome["test_identity"] != receipt["test_identity"] \
                or not _same_path(outcome["junit_path"], artifact_paths["ctest_junit_path"]) \
                or outcome_info["test_start"] != start or (receipt_end is not None and outcome_info["test_end"] != receipt_end):
            raise ValueError("post-test outcome does not reconcile with runner receipt")
        end = outcome_info["test_end"]
        if not _same_path(outcome["executable_path"], receipt["executable_path"]) \
                or not _same_path(receipt["ctest_selected_executable"], receipt["executable_path"]) \
                or outcome["executable_sha256"].casefold() != receipt["executable_sha256"].casefold():
            raise ValueError("post-test outcome executable does not match selected CTest executable")
        execution_failure = bool(outcome["timed_out"] or outcome["crashed"] or outcome["skipped"] or not outcome["execution_complete"])
        junit_failure = bool(outcome["junit_failures"] > 0 or outcome["junit_errors"] > 0 or outcome["junit_skipped"] > 0 or outcome["junit_timeout"])
        cpu_only_valid = bool(outcome["cpu_only_failure"] and outcome["ctest_exit_code"] != 0 and native_evidence.get("cpu_reason_verified") and not execution_failure)
        native_failure = bool(outcome["ctest_exit_code"] != 0 or junit_failure) and not cpu_only_valid
        return {"execution_failure": execution_failure, "execution_blocked": native_failure,
                "native_failure": native_failure, "cpu_only_failure": cpu_only_valid,
                "evidence_blocked": execution_failure or native_failure, "receipt_missing": False,
                "status": str(outcome["ctest_outcome"]).lower(), "test_start": start, "test_end": end,
                "host": receipt["machine"], "lease_id": receipt["host_lease_id"],
                "policy_sha256": receipt["host_policy_sha256"], "source_head": receipt["source_head"],
                "source_tree": receipt["source_tree"], "executable_sha256": receipt["executable_sha256"],
                "runner_job_id": receipt["runner_job_id"], "outcome_path": outcome_path,
                "outcome": outcome, "native_execution_evidence": native_evidence}
    except (OSError, ValueError) as error:
        return {"execution_failure": False, "execution_blocked": True, "evidence_blocked": True,
                "receipt_missing": False, "receipt_error": str(error)}


def _dispositions(wall_failure: bool, safety_failure: bool, functional_failure: bool,
                  execution_failure: bool, execution_blocked: bool, instrumentation_blocked: bool,
                  evidence_blocked: bool, cpu_diagnostic_blocked: bool) -> dict[str, str]:
    return {"wall": "FAIL" if wall_failure else "BLOCKED" if evidence_blocked else "PASS",
            "safety": "FAIL" if safety_failure else "BLOCKED" if instrumentation_blocked or evidence_blocked else "PASS",
            "functional": "FAIL" if functional_failure else "BLOCKED" if evidence_blocked else "PASS",
            "execution": "FAIL" if execution_failure else "BLOCKED" if execution_blocked else "PASS",
            "instrumentation": "BLOCKED" if instrumentation_blocked else "PASS",
            "evidence": "BLOCKED" if evidence_blocked else "PASS",
            "cpu": "BLOCKED" if cpu_diagnostic_blocked else "PASS"}


def evaluate_results(results: dict[str, dict[str, Any]], witness: dict[str, Any] | None,
                     execution: dict[str, dict[str, Any]] | None = None,
                     qualification_prohibited: bool = False,
                     provenance_verified: bool = False,
                     host_manifest_verified: bool = False) -> dict[str, Any]:
    execution = execution or {}
    missing_profiles = sorted(set(PROFILES) - set(results)); extra_profiles = sorted(set(results) - set(PROFILES))
    coverage_error = bool(missing_profiles or extra_profiles)
    wall_failure = safety_failure = functional_failure = execution_failure = execution_blocked = False
    instrumentation_blocked = functional_evidence_invalid = False
    witness_fixture = bool(witness and (witness.get("fixture_mode") is True or witness.get("qualification_prohibited") is True))
    evidence_blocked = (coverage_error or witness is None or witness.get("verified") is not True
                        or qualification_prohibited or witness_fixture or not provenance_verified or not host_manifest_verified)
    cpu_diagnostic_blocked = False; profile_cells_complete = True
    for profile, result in results.items():
        observed = _scan_observed_failures(Path(result["report"])) if result.get("report") else {}
        wall_failure |= bool(result.get("wall_failure", False) or observed.get("wall_failure", False))
        safety_failure |= bool(result.get("safety_failure", False) or observed.get("safety_failure", False))
        functional_failure |= bool(result.get("functional_failure", False) or observed.get("functional_failure", False))
        functional_evidence_invalid |= bool(result.get("functional_evidence_invalid", False) or observed.get("functional_evidence_invalid", False))
        execution_failure |= bool(result.get("execution_failure", False)); instrumentation_blocked |= bool(result.get("instrumentation_blocked", False))
        evidence_blocked |= bool(result.get("evidence_blocked", False) or result.get("functional_evidence_invalid", False) or observed.get("functional_evidence_invalid", False))
        report_cpu_diagnostic = bool(result.get("cpu_diagnostic_blocked", False) or result.get("cpu_measurement_insufficient", False))
        expected_cells = PROFILES.get(profile)
        if expected_cells is None or result.get("cells") != expected_cells or result.get("totals", {}).get("callbacks") != expected_cells * AUDIT.BLOCKS:
            profile_cells_complete = False
        state = execution.get(profile)
        if not state:
            execution_blocked = True; evidence_blocked = True; cpu_diagnostic_blocked |= report_cpu_diagnostic; continue
        execution_failure |= bool(state.get("execution_failure", False)); execution_blocked |= bool(state.get("execution_blocked", False) or state.get("receipt_missing", False)); evidence_blocked |= bool(state.get("evidence_blocked", False) or state.get("receipt_missing", False))
        if state.get("cpu_only_failure"):
            report_cpu_only = bool(result.get("cpu_measurement_insufficient", False) and not result.get("wall_failure", False) and not result.get("safety_failure", False) and not result.get("functional_failure", False) and not result.get("functional_evidence_invalid", False))
            if report_cpu_only: cpu_diagnostic_blocked = True
            else: state["cpu_only_failure_reconciled"] = False; state["execution_blocked"] = True; state["evidence_blocked"] = True; execution_blocked = True; evidence_blocked = True
        elif state.get("test_start") and not state.get("execution_blocked") and not state.get("execution_failure"):
            cpu_diagnostic_blocked |= report_cpu_diagnostic
    evidence_blocked |= not profile_cells_complete
    performance_failure = wall_failure or safety_failure or functional_failure
    if performance_failure: primary_status = "WALL_GATE_FAIL"
    elif execution_failure or execution_blocked or evidence_blocked: primary_status = "WALL_GATE_BLOCKED"
    elif cpu_diagnostic_blocked: primary_status = "WALL_GATE_PASS_CPU_UNKNOWN"
    else: primary_status = "WALL_GATE_PASS"
    return {"format": "VOX_FX_WALL_GATE_EVALUATION_V2", "mode": "fixture" if qualification_prohibited or witness_fixture else "qualified",
            "qualification_prohibited": bool(qualification_prohibited or witness_fixture), "primary_status": primary_status,
            "wall_failure": wall_failure, "safety_failure": safety_failure, "functional_failure": functional_failure,
            "functional_evidence_invalid": functional_evidence_invalid, "execution_failure": execution_failure,
            "execution_blocked": execution_blocked, "performance_failure": performance_failure,
            "instrumentation_blocked": instrumentation_blocked, "evidence_blocked": evidence_blocked,
            "cpu_diagnostic_blocked": cpu_diagnostic_blocked, "provenance_verified": provenance_verified,
            "host_manifest_verified": host_manifest_verified, "missing_profiles": missing_profiles, "extra_profiles": extra_profiles,
            "profile_cells_complete": profile_cells_complete, "expected_cells": sum(PROFILES.values()),
            "expected_callbacks": sum(PROFILES.values()) * AUDIT.BLOCKS,
            "observed_callbacks": sum(int(result.get("totals", {}).get("callbacks", 0)) for result in results.values()),
            "dispositions": _dispositions(wall_failure, safety_failure, functional_failure, execution_failure,
                                            execution_blocked, instrumentation_blocked, evidence_blocked, cpu_diagnostic_blocked),
            "witness": witness, "profiles": results}


def evaluate_reports(reports: dict[str, Path], root: Path | None = None, executable: Path | None = None,
                     receipts: dict[str, Path] | None = None, manifests: dict[str, Path] | None = None,
                     expected_head: str | None = None, expected_tree: str | None = None,
                     witness: Path | None = None, host_manifest: Path | None = None,
                     allow_unverified_provenance: bool = False,
                     owner_certificate_thumbprint: str | None = None) -> dict[str, Any]:
    receipts = receipts or {}; manifests = manifests or {}
    audited: dict[str, dict[str, Any]] = {}
    execution = {profile: _execution_state(receipts.get(profile)) for profile in PROFILES}
    for profile, report in reports.items():
        try:
            if not allow_unverified_provenance and (root is None or profile not in receipts or profile not in manifests):
                raise ValueError("report requires root, runner receipt and artifact manifest")
            audited[profile] = AUDIT.audit(report, require_windows=not allow_unverified_provenance, root=root,
                                           executable=executable, runner_receipt=receipts.get(profile), artifact_manifest=manifests.get(profile),
                                           expected_head=expected_head, expected_tree=expected_tree,
                                           allow_unverified_provenance=allow_unverified_provenance,
                                           allow_instrumentation_blocked=True)
            audited[profile]["report"] = str(report)
        except (OSError, ValueError) as error:
            observed = _scan_observed_failures(report)
            audited[profile] = {"profile": profile, "report": str(report), "cells": 0, "totals": {},
                                "wall_failure": observed["wall_failure"], "safety_failure": observed["safety_failure"],
                                "functional_failure": observed["functional_failure"], "functional_evidence_invalid": observed["functional_evidence_invalid"],
                                "execution_failure": False, "evidence_blocked": True, "audit_error": str(error)}

    witness_result: dict[str, Any] | None = None; witness_error: str | None = None; host_manifest_result: dict[str, Any] | None = None
    valid_states = [state for state in execution.values() if state.get("test_start")]
    expected_host = expected_lease_id = expected_policy_hash = expected_source_head = expected_source_tree = expected_executable_sha = None
    if valid_states:
        sets = [{state.get(key) for state in valid_states} for key in ("host", "lease_id", "policy_sha256", "source_head", "source_tree", "executable_sha256")]
        if all(len(items) == 1 for items in sets):
            expected_host, expected_lease_id, expected_policy_hash, expected_source_head, expected_source_tree, expected_executable_sha = [next(iter(items)) for items in sets]
        else: witness_error = "trusted runner receipts disagree on host, lease, source, executable or process policy"
    try:
        if witness is None: raise ValueError("host witness is required")
        witness_result = validate_witness(witness, execution, expected_host, expected_lease_id, expected_policy_hash,
                                          expected_source_head, expected_source_tree, expected_executable_sha,
                                          require_external_attestations=not allow_unverified_provenance,
                                          allow_fixture=allow_unverified_provenance,
                                          owner_certificate_thumbprint=owner_certificate_thumbprint)
        if not allow_unverified_provenance:
            if host_manifest is None: raise ValueError("production evaluation requires a host artifact manifest")
            host_manifest_result = _verify_host_manifest(host_manifest, witness)
    except ValueError as error:
        witness_error = str(error) if witness_error is None else f"{witness_error}; {error}"
    provenance_verified = set(audited) == set(PROFILES) and all(audited[profile].get("provenance", {}).get("verified") is True for profile in PROFILES if profile in audited)
    host_manifest_verified = host_manifest_result is not None if not allow_unverified_provenance else False
    if not allow_unverified_provenance and (not provenance_verified or host_manifest_result is None):
        witness_error = witness_error or "independent artifact provenance is incomplete"
    result = evaluate_results(audited, witness_result, execution, qualification_prohibited=allow_unverified_provenance,
                              provenance_verified=provenance_verified, host_manifest_verified=host_manifest_verified)
    result["execution_intervals"] = {profile: {key: value.isoformat().replace("+00:00", "Z") if isinstance(value, datetime) else value
                                                for key, value in state.items() if key in {"test_start", "test_end", "host", "lease_id", "policy_sha256", "source_head", "source_tree", "executable_sha256"}}
                                      for profile, state in execution.items()}
    if host_manifest_result is not None: result["host_manifest"] = host_manifest_result
    if witness_error:
        result["evidence_blocked"] = True
        result["primary_status"] = "WALL_GATE_FAIL" if result["performance_failure"] else "WALL_GATE_BLOCKED"
        result["witness_error"] = witness_error
    return result


def _assignments(values: list[str], name: str) -> dict[str, Path]:
    result: dict[str, Path] = {}
    for value in values:
        if "=" not in value: raise ValueError(f"{name} requires PROFILE=PATH")
        profile, path = value.split("=", 1)
        if profile not in PROFILES: raise ValueError(f"unknown profile {profile!r}")
        if profile in result: raise ValueError(f"duplicate {name} profile {profile!r}")
        result[profile] = Path(path)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", action="append", required=True, help="PROFILE=PATH; provide routed, authored and typical")
    parser.add_argument("--receipt", action="append", default=[], help="PROFILE=PATH")
    parser.add_argument("--manifest", action="append", default=[], help="PROFILE=PATH")
    parser.add_argument("--root", type=Path); parser.add_argument("--executable", type=Path)
    parser.add_argument("--expected-head"); parser.add_argument("--expected-tree")
    parser.add_argument("--witness", type=Path); parser.add_argument("--host-manifest", type=Path)
    parser.add_argument("--owner-certificate-thumbprint", help="owner-supplied SHA-1 trust anchor; required for production CMS verification")
    parser.add_argument("--allow-unverified-provenance", action="store_true", help="fixture mode only; qualification-prohibited and cannot PASS")
    args = parser.parse_args()
    try:
        reports = _assignments(args.report, "--report")
        if set(reports) != set(PROFILES): raise ValueError("exactly one routed, authored and typical report is required")
        receipts = _assignments(args.receipt, "--receipt") if args.receipt else {}
        manifests = _assignments(args.manifest, "--manifest") if args.manifest else {}
        result = evaluate_reports(reports, root=args.root, executable=args.executable, receipts=receipts, manifests=manifests,
                                  expected_head=args.expected_head, expected_tree=args.expected_tree, witness=args.witness,
                                  host_manifest=args.host_manifest, allow_unverified_provenance=args.allow_unverified_provenance,
                                  owner_certificate_thumbprint=args.owner_certificate_thumbprint)
    except (OSError, ValueError) as error:
        parser.exit(2, f"FX-T18 wall evaluator: {error}\n")
    print(json.dumps(result, indent=2, sort_keys=True, default=str))
    return 0 if result["primary_status"] not in {"WALL_GATE_FAIL", "WALL_GATE_BLOCKED"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
