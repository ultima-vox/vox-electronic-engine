#!/usr/bin/env python3
"""Decode and verify the raw FX-T18 process ETL outside the observer.

The production path is deliberately a thin, reproducible wrapper around the
pinned Microsoft Windows SDK ``tracerpt.exe`` tool.  Capture never creates a
parse receipt and an owner-signed JSON claim is not a parser.  The wrapper
records the raw ETL hash, the exact tracerpt path/version/command, the
retained ``tracerpt -summary`` artifact that supplies loss counters and
trace metadata, plus retained collection metadata whose hashed start/stop
control receipts prove the bounded noncircular logger, and digests of both the
raw ETW core and the enriched identity-joined event stream. Production parsing
requires a retained
`VOX_FX_T18_ETW_IDENTITY_JOIN_V1` artifact because historical image hashes and
verified service ancestry are not guaranteed to be fields in the raw event.
A replay reruns tracerpt against the retained bytes, reapplies that join and
compares the complete decoded result before the evaluator accepts the receipt.

The ``--fixture-raw`` path is only for offline conformance tests.  It uses a
JSON representation of the tracerpt event/counter result, is marked
qualification-prohibited, and exercises the same raw-byte hash and replay
checks.  It is not a substitute for a Windows ETL or a genuine tracerpt XML
and summary artifact.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from typing import Any

FORMAT = "VOX_FX_T18_ETW_PARSE_RECEIPT_V1"
PARSER_VERSION = "1.3.0"
PROVIDER = "Microsoft-Windows-Kernel-Process/ETW"
PROVIDER_GUID = "22fb2cd6-0e7b-422b-a0c7-2fad1fd0e716"
PROCESS_PROVIDER_NAMES = {
    "microsoft-windows-kernel-process",
    "microsoft-windows-kernel-process/etw",
    "microsoft-windows-kernel-process/analytic",
}
PROCESS_EVENT_IDS = {1: "start", 2: "stop"}
TOOLCHAIN_ID = "microsoft-windows-sdk-tracerpt-10.0.26100.1"
PINNED_TRACERPT_VERSION = "10.0.26100.1"
TRACERPT_COMMAND = "tracerpt.exe <raw-etl> -o <decoded-xml> -of XML -lr -summary <tracerpt-summary> -y"
EVENT_SOURCE = "Microsoft-Windows-Kernel-Process event IDs 1(ProcessStart) and 2(ProcessStop)"
LOSS_SOURCE = "tracerpt -summary <tracerpt-summary>: Events Lost and Buffers Lost"
SUMMARY_SOURCE = "tracerpt -summary artifact; counters are not read from stdout/stderr or JSON claims"
COLLECTION_METADATA_FORMAT = "VOX_FX_T18_ETW_COLLECTION_METADATA_V1"
COLLECTION_RECEIPT_FORMAT = "VOX_FX_T18_LOGMAN_RECEIPT_V1"
COLLECTION_METADATA_SOURCE = "owner-retained logman start/stop metadata and hashed control receipts"
COLLECTION_CONTROL_RECEIPT_SOURCE = "hashed owner-retained logman control receipts"
COLLECTION_TRACE_MODE = "bin"
COLLECTION_KEYWORDS_HEX = "0x10"
COLLECTION_LEVEL = 5
COLLECTION_CAPACITY_POLICY = "max_file_mb_plus_free_headroom"
COLLECTION_MAX_FILE_MB = 4096
COLLECTION_FREE_HEADROOM_BYTES = 512 * 1024 * 1024
COLLECTION_CLEANUP_DIRECT = "direct_ets_transient"
COLLECTION_CLEANUP_PERSISTED = "persisted_dcs"
COLLECTION_FILE_POLICY = "single_retained_etl_file_zero_rotation"
COLLECTION_INVENTORY_SOURCE = "independent_etl_directory_inventory"
COLLECTION_PROVIDER_GUID = "{22FB2CD6-0E7B-422B-A0C7-2FAD1FD0E716}"
COVERAGE_SOURCE = "tracerpt summary or retained validated logman start/stop receipts"
SESSION_SOURCE = "tracerpt summary or retained validated logman session receipt"
SUMMARY_AND_CONTROL_COVERAGE_SOURCE = "tracerpt -summary and validated logman control receipts"
CONTROL_COVERAGE_SOURCE = "validated logman control receipts"
SUMMARY_AND_CONTROL_SESSION_SOURCE = "tracerpt -summary and validated logman session receipt"
CONTROL_SESSION_SOURCE = "validated logman session receipt"
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
MAX_EVENT_CORRELATION_SKEW_SECONDS = 2.0


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: raw fixture is not an object")
    return value


def utc(value: Any) -> datetime:
    if not isinstance(value, str):
        raise ValueError("timestamp is not a string")
    normalized = value[:-1] + "+00:00" if value.endswith("Z") else value
    parsed = datetime.fromisoformat(normalized)
    if parsed.tzinfo is None:
        raise ValueError("timestamp has no UTC offset")
    return parsed.astimezone(timezone.utc)


def utc_text(value: Any) -> str:
    text = value if isinstance(value, str) else ""
    parsed = utc(text)
    fraction_match = re.search(r"\.(\d+)(?:Z|[+-]\d\d:?\d\d)$", text)
    fraction = fraction_match.group(1) if fraction_match else ""
    if len(fraction) > 7:
        raise ValueError("timestamp has more than seven fractional-second digits")
    fraction_ticks = (fraction + "0000000")[:7]
    return parsed.strftime("%Y-%m-%dT%H:%M:%S") + "." + fraction_ticks + "Z"


def _filetime_text(value: str, name: str) -> str:
    try:
        ticks = int(value, 0)
    except ValueError:
        try:
            ticks = int(value)
        except ValueError as error:
            raise ValueError(f"{name} is not an ISO timestamp or Windows FILETIME") from error
    if ticks < 0:
        raise ValueError(f"{name} is a negative Windows FILETIME")
    epoch = datetime(1601, 1, 1, tzinfo=timezone.utc)
    seconds, remainder = divmod(ticks, 10_000_000)
    try:
        parsed = epoch + timedelta(seconds=seconds)
    except OverflowError as error:
        raise ValueError(f"{name} Windows FILETIME is out of range") from error
    return parsed.strftime("%Y-%m-%dT%H:%M:%S") + "." + f"{remainder:07d}" + "Z"


def _timestamp_text(value: Any, name: str) -> str:
    text = value if isinstance(value, str) else ""
    try:
        return utc_text(text)
    except ValueError as iso_error:
        try:
            return _filetime_text(text, name)
        except ValueError:
            raise iso_error


def utc_ticks(value: Any) -> int:
    text = value if isinstance(value, str) else ""
    parsed = utc(text)
    fraction_match = re.search(r"\.(\d+)(?:Z|[+-]\d\d:?\d\d)$", text)
    fraction = fraction_match.group(1) if fraction_match else ""
    if len(fraction) > 7:
        raise ValueError("timestamp has more than seven fractional-second digits")
    fraction_ticks = int((fraction + "0000000")[:7]) if fraction else 0
    epoch = datetime(1, 1, 1, tzinfo=timezone.utc)
    delta = parsed.replace(microsecond=0) - epoch
    return (delta.days * 86400 + delta.seconds) * 10_000_000 + fraction_ticks


def canonical_event(event: dict[str, Any]) -> str:
    """Canonical event bytes shared by parse and replay verification."""
    fields = {
        key: event.get(key)
        for key in (
            "event", "timestamp_utc", "pid", "process_name", "executable_path",
            "executable_sha256", "parent_pid", "start_time_utc",
            "service_identity_sha256", "service_ancestry_verified", "lineage_verification",
            "identity_digest", "event_process_name",
        )
    }
    for key in ("timestamp_utc", "start_time_utc"):
        if fields[key] is not None:
            fields[key] = utc_text(fields[key])
    if isinstance(fields.get("service_identity_sha256"), list):
        fields["service_identity_sha256"] = sorted(str(item).casefold() for item in fields["service_identity_sha256"])
    return json.dumps(fields, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def event_digest(events: list[dict[str, Any]]) -> str:
    payload = "\n".join(canonical_event(event) for event in events).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def canonical_core_event(event: dict[str, Any]) -> str:
    """Canonical bytes that must come directly from raw ETL decoding.

    ETW does not itself provide the historical executable hash and verified
    runner ancestry. Those fields are supplied by a separately retained owner
    identity join, while this core remains independently replayable from raw
    ETL bytes.
    """
    fields = {
        key: event.get(key)
        for key in ("event", "timestamp_utc", "pid", "process_name", "parent_pid", "start_time_utc")
    }
    for key in ("timestamp_utc", "start_time_utc"):
        if fields[key] is not None:
            fields[key] = utc_text(fields[key])
    return json.dumps(fields, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def core_event_digest(events: list[dict[str, Any]]) -> str:
    payload = "\n".join(canonical_core_event(event) for event in events).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _required_identity_join_shape(events: Any) -> list[dict[str, Any]]:
    result = _required_event_shape(events)
    required = (
        "executable_path", "executable_sha256", "service_identity_sha256",
        "service_ancestry_verified", "lineage_verification", "service_ancestry",
        "identity_digest", "identity_known", "approved_by_policy",
    )
    for index, event in enumerate(result):
        if any(field not in event for field in required):
            raise ValueError(f"identity join process_events[{index}] is missing immutable identity fields")
        if (not isinstance(event["executable_path"], str) or not event["executable_path"]
                or not SHA256_RE.fullmatch(str(event["executable_sha256"]))
                or not isinstance(event["service_identity_sha256"], list)
                or (event["lineage_verification"] != "host_baseline_v1" and not event["service_identity_sha256"])
                or any(not SHA256_RE.fullmatch(str(item)) for item in event["service_identity_sha256"])
                or event["service_ancestry_verified"] is not True
                or event["lineage_verification"] not in {"runner_service_ancestry_v1", "signed_job_lineage_v1", "host_baseline_v1"}
                or not isinstance(event["service_ancestry"], list) or not event["service_ancestry"]
                or not SHA256_RE.fullmatch(str(event["identity_digest"]))
                or event["identity_known"] is not True
                or not isinstance(event["approved_by_policy"], bool)):
            raise ValueError(f"identity join process_events[{index}] has an invalid identity record")
    return result


def _read_identity_join(path: Path) -> list[dict[str, Any]]:
    value = read_json(path)
    if value.get("format") != "VOX_FX_T18_ETW_IDENTITY_JOIN_V1":
        raise ValueError("ETW identity join format is not supported")
    return _required_identity_join_shape(value.get("process_events"))


def _apply_identity_join(raw_events: list[dict[str, Any]],
                         identity_events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Attach owner identity facts to exactly one independently decoded event."""
    candidates_by_raw: list[list[int]] = []
    for raw_index, raw in enumerate(raw_events):
        candidates: list[int] = []
        for identity_index, identity in enumerate(identity_events):
            if (raw.get("event") != identity.get("event")
                    or int(raw.get("pid")) != int(identity.get("pid"))
                    or int(raw.get("parent_pid")) != int(identity.get("parent_pid"))
                    or str(raw.get("process_name", "")).casefold() != str(identity.get("process_name", "")).casefold()
                    or utc_ticks(raw.get("start_time_utc")) != utc_ticks(identity.get("start_time_utc"))):
                continue
            if abs((utc(raw["timestamp_utc"]) - utc(identity["timestamp_utc"])).total_seconds()) \
                    <= MAX_EVENT_CORRELATION_SKEW_SECONDS:
                candidates.append(identity_index)
        candidates_by_raw.append(candidates)
    if any(len(candidates) == 0 for candidates in candidates_by_raw):
        raise ValueError("ETW identity join has an unmatched raw process event")
    if any(len(candidates) > 1 for candidates in candidates_by_raw):
        raise ValueError("ETW identity join is ambiguous")
    chosen = [candidates[0] for candidates in candidates_by_raw]
    if len(set(chosen)) != len(chosen) or set(chosen) != set(range(len(identity_events))):
        raise ValueError("ETW identity join is not one-to-one")
    merged: list[dict[str, Any]] = []
    for raw, identity_index in zip(raw_events, chosen):
        value = dict(identity_events[identity_index])
        # Raw ETW owns the provider event kind/time and lifecycle fields. The
        # join contributes only the identity facts that ETW cannot prove alone.
        for field in ("event", "timestamp_utc", "pid", "process_name", "parent_pid", "start_time_utc"):
            value[field] = raw[field]
        merged.append(value)
    return merged


def _required_event_shape(events: Any) -> list[dict[str, Any]]:
    if not isinstance(events, list):
        raise ValueError("decoded process_events is not a list")
    result: list[dict[str, Any]] = []
    previous: datetime | None = None
    for index, event in enumerate(events):
        if not isinstance(event, dict) or event.get("event") not in {"start", "stop"}:
            raise ValueError(f"decoded process_events[{index}] has no start/stop kind")
        for field in ("timestamp_utc", "pid", "process_name", "parent_pid", "start_time_utc"):
            if field not in event:
                raise ValueError(f"decoded process_events[{index}] is missing {field}")
        stamp = utc(event["timestamp_utc"])
        if previous is not None and stamp < previous:
            raise ValueError("decoded process events are not monotonic")
        previous = stamp
        if type(event["pid"]) is not int or event["pid"] < 1 \
                or type(event["parent_pid"]) is not int or event["parent_pid"] < 0 \
                or not isinstance(event["process_name"], str) or not event["process_name"]:
            raise ValueError(f"decoded process_events[{index}] has an invalid process identity")
        result.append(dict(event))
    return result


def _fixture_result(raw_path: Path) -> tuple[list[dict[str, Any]], dict[str, int], str]:
    value = read_json(raw_path)
    if value.get("format") != "VOX_FX_T18_RAW_ETL_FIXTURE_V1":
        raise ValueError("fixture raw ETL format is not supported")
    if value.get("provider") != PROVIDER:
        raise ValueError("fixture raw ETL provider is not the kernel process provider")
    events = _required_event_shape(value.get("process_events"))
    counters = value.get("counters")
    if (not isinstance(counters, dict) or type(counters.get("lost_events")) is not int
            or type(counters.get("buffer_loss_events")) is not int
            or counters["lost_events"] < 0 or counters["buffer_loss_events"] < 0):
        raise ValueError("fixture raw ETL loss counters are missing or negative")
    return events, {
        "lost_events": counters["lost_events"],
        "buffer_loss_events": counters["buffer_loss_events"],
        "events_received": len(events),
    }, "fixture-json-decoder-v1"


def _tool_version(executable: Path) -> str:
    if os.name != "nt":
        raise ValueError("the pinned tracerpt toolchain is Windows-only")
    # FileVersion is the provenance value recorded in the receipt.  The
    # command is intentionally fixed; no shell interpolation is used.
    command = ["powershell.exe", "-NoLogo", "-NoProfile", "-NonInteractive", "-Command",
               "(Get-Item -LiteralPath $env:FX_T18_TRACERPT).VersionInfo.FileVersion"]
    environment = os.environ.copy()
    environment["FX_T18_TRACERPT"] = str(executable)
    completed = subprocess.run(command, check=False, capture_output=True, text=True,
                               env=environment, timeout=30)
    if completed.returncode != 0:
        raise ValueError("could not read the pinned tracerpt file version")
    version = completed.stdout.strip()
    if not version:
        raise ValueError("tracerpt file version is empty")
    return version


def _data_value(event: ET.Element, names: set[str]) -> str | None:
    wanted = {name.casefold() for name in names}
    for node in event.iter():
        tag = node.tag.rsplit("}", 1)[-1].casefold()
        if tag != "data":
            continue
        name = str(node.attrib.get("Name", "")).casefold()
        if name in wanted:
            return node.text or ""
    return None


def _event_time(event: ET.Element) -> str:
    for node in event.iter():
        tag = node.tag.rsplit("}", 1)[-1].casefold()
        if tag == "timecreated" and node.attrib.get("SystemTime"):
            return _timestamp_text(node.attrib["SystemTime"], "tracerpt event timestamp")
    value = _data_value(event, {"Timestamp", "TimeCreated", "EventTime"})
    if value is None:
        raise ValueError("tracerpt event has no provider timestamp")
    return _timestamp_text(value, "tracerpt event timestamp")


def _parse_int(value: str | None, name: str) -> int:
    if value is None:
        raise ValueError(f"tracerpt event is missing {name}")
    try:
        return int(value, 0)
    except ValueError:
        try:
            return int(value)
        except ValueError as error:
            raise ValueError(f"tracerpt event {name} is not an integer") from error


def _read_summary_text(path: Path) -> str:
    raw = path.read_bytes()
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
        encodings = ("utf-16", "utf-16-le", "utf-16-be", "utf-8-sig")
    elif raw.startswith(b"\xef\xbb\xbf"):
        encodings = ("utf-8-sig", "utf-16", "utf-16-le", "utf-16-be")
    else:
        # A UTF-16 summary without a BOM is valid input on some Windows
        # writers. Prefer the endian whose odd/even bytes carry the NULs so a
        # permissive UTF-8 decoder cannot turn it into text with embedded NULs.
        probe = raw[:4096]
        little_nuls = sum(byte == 0 for byte in probe[1::2])
        big_nuls = sum(byte == 0 for byte in probe[::2])
        if little_nuls > max(2, len(probe) // 16):
            encodings = ("utf-16-le", "utf-8-sig", "utf-16", "utf-16-be")
        elif big_nuls > max(2, len(probe) // 16):
            encodings = ("utf-16-be", "utf-8-sig", "utf-16", "utf-16-le")
        else:
            encodings = ("utf-8-sig", "utf-16", "utf-16-le", "utf-16-be")
    for encoding in encodings:
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            continue
    return raw.decode()


def _summary_fields(summary: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for line in summary.splitlines():
        match = re.match(r"^\s*([^:=]{2,100})\s*[:=]\s*(.*?)\s*$", line)
        if not match:
            continue
        key = re.sub(r"[^a-z0-9]+", " ", match.group(1).casefold()).strip()
        value = match.group(2).strip().strip('"')
        if key and value:
            fields[key] = value
    return fields


def _summary_value(fields: dict[str, str], *names: str) -> str | None:
    for name in names:
        value = fields.get(name)
        if value:
            return value
    return None


def _summary_unstructured_value(summary: str, *labels: str) -> str | None:
    patterns = [re.compile(r"^\s*" + re.escape(label) + r"\s*:?\s*(.*?)\s*$", re.IGNORECASE)
                for label in labels]
    for line in summary.splitlines():
        for pattern in patterns:
            match = pattern.match(line)
            if match and match.group(1):
                return match.group(1).strip().strip('"')
    return None


def _summary_timestamp(value: str, name: str) -> str:
    text = value.strip()
    # Prefer an explicit ISO offset when tracerpt or a wrapper supplies one.
    try:
        return utc_text(text)
    except ValueError:
        pass
    explicit_utc = bool(re.search(r"\s+(?:UTC|GMT|Z)$", text, flags=re.IGNORECASE))
    if explicit_utc:
        text = re.sub(r"\s+(?:UTC|GMT|Z)$", "", text, flags=re.IGNORECASE).strip()
    text = re.sub(r"\s+", " ", text).strip()
    if "T" in text or "t" in text:
        text = re.sub(r"[Tt]", " ", text, count=1)
    # Windows text timestamps may carry seven fractional-second digits,
    # although datetime.strptime accepts at most six. Keep the seventh digit
    # separately so the canonical receipt does not silently invent precision.
    fraction = ""
    fraction_match = re.search(r"\.(\d+)(?=\s*(?:AM|PM)?$)", text, flags=re.IGNORECASE)
    if fraction_match:
        fraction = fraction_match.group(1)
        if len(fraction) > 7:
            raise ValueError(f"tracerpt summary {name} has more than seven fractional-second digits")
        normalized_fraction = (fraction[:6] + "000000")[:6]
        text = text[:fraction_match.start()] + "." + normalized_fraction + text[fraction_match.end():]
    parsed: datetime | None = None
    for pattern in (
        "%m/%d/%Y %H:%M:%S.%f", "%m/%d/%Y %H:%M:%S",
        "%m/%d/%Y %I:%M:%S.%f %p", "%m/%d/%Y %I:%M:%S %p",
        "%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%d %H:%M:%S",
        "%Y-%m-%d %I:%M:%S.%f %p", "%Y-%m-%d %I:%M:%S %p",
    ):
        try:
            parsed = datetime.strptime(text, pattern)
            break
        except ValueError:
            continue
    if parsed is None:
        raise ValueError(f"tracerpt summary {name} is not a supported timestamp: {value!r}")
    fraction_text = (fraction + "0000000")[:7]
    if explicit_utc:
        parsed_utc = parsed.replace(tzinfo=timezone.utc)
    else:
        # tracerpt commonly emits the Windows host's local wall clock without
        # an offset. Resolve that timestamp with the host timezone database;
        # the timezone is not a caller-provided coverage claim.
        local_epoch = time.mktime(parsed.timetuple()) + parsed.microsecond / 1_000_000
        parsed_utc = datetime.fromtimestamp(local_epoch, timezone.utc)
    return parsed_utc.strftime("%Y-%m-%dT%H:%M:%S") + "." + fraction_text + "Z"


def _parse_tracerpt_summary(summary: str) -> dict[str, Any]:
    fields = _summary_fields(summary)
    lost_value = _summary_value(fields, "events lost", "number of events lost", "number events lost")
    # Windows logger text has appeared as both `Buffers Lost` and
    # `Log Buffers Lost`; prefer the exact aggregate label when it is present,
    # then accept the file-buffer label used by the same retained trace query.
    buffer_value = _summary_value(
        fields, "buffers lost", "number of buffers lost", "number buffers lost",
        "log buffers lost", "number of log buffers lost",
    )
    if lost_value is None or buffer_value is None:
        lost_match = re.search(
            r"(?im)^\s*(?:(?:total|number\s+of)\s+)?events?\s+lost\s*(?::|=)?\s*([0-9][0-9,]*)\b", summary)
        buffer_match = re.search(
            r"(?im)^\s*(?:(?:total|number\s+of)\s+)?(?:log\s+|real\s+time\s+)?buffers?\s+lost\s*(?::|=)?\s*([0-9][0-9,]*)\b", summary)
        if lost_value is None and lost_match is not None:
            lost_value = lost_match.group(1)
        if buffer_value is None and buffer_match is not None:
            buffer_value = buffer_match.group(1)
    if lost_value is None or buffer_value is None:
        raise ValueError("tracerpt -summary Events Lost/Buffers Lost counters are absent")
    start_value = _summary_value(fields, "log start time", "trace start time", "start time") \
        or _summary_unstructured_value(summary, "Log Start Time", "Trace Start Time", "Start Time")
    end_value = _summary_value(fields, "log end time", "trace end time", "end time") \
        or _summary_unstructured_value(summary, "Log End Time", "Trace End Time", "End Time")
    session_value = _summary_value(fields, "logger name", "session name", "trace name") \
        or _summary_unstructured_value(summary, "Logger Name", "Session Name", "Trace Name")
    start = None
    end = None
    if start_value:
        try:
            start = _summary_timestamp(start_value, "Log Start Time")
        except ValueError:
            start = None
    if end_value:
        try:
            end = _summary_timestamp(end_value, "Log End Time")
        except ValueError:
            end = None
    if start is not None and end is not None and utc(end) <= utc(start):
        raise ValueError("tracerpt summary coverage interval is not positive")
    if session_value is not None and session_value.startswith("<"):
        session_value = None
    lost_events = int(str(lost_value).replace(",", ""))
    buffer_loss_events = int(str(buffer_value).replace(",", ""))
    if lost_events < 0 or buffer_loss_events < 0:
        raise ValueError("tracerpt summary loss counters cannot be negative")
    return {
        "lost_events": lost_events,
        "buffer_loss_events": buffer_loss_events,
        "coverage_start_utc": start,
        "coverage_end_utc": end,
        "session_name": session_value,
        "summary_source": SUMMARY_SOURCE,
        "coverage_source": COVERAGE_SOURCE,
        "session_name_source": SESSION_SOURCE,
    }


def _normalized_process_name(value: str) -> str:
    text = value.strip()
    if not text:
        return text
    # Kernel-Process ImageName is commonly a device-qualified path while WMI
    # and the owner policy use the executable basename.
    return re.split(r"[\\/]+", text)[-1] or text


def _parse_xml(decoded: Path, summary: str) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    root = ET.parse(decoded).getroot()
    events: list[dict[str, Any]] = []
    starts: dict[tuple[int, str], dict[str, Any]] = {}
    for event in root.iter():
        if event.tag.rsplit("}", 1)[-1].casefold() != "event":
            continue
        provider = None
        provider_guid = None
        event_id = None
        for node in event.iter():
            tag = node.tag.rsplit("}", 1)[-1].casefold()
            if tag == "provider":
                provider = node.attrib.get("Name")
                provider_guid = node.attrib.get("Guid")
            elif tag == "eventid":
                event_id = (node.text or "").strip()
        if (str(provider or "").casefold() not in PROCESS_PROVIDER_NAMES
                and str(provider_guid or "").strip("{}").casefold() != PROVIDER_GUID):
            continue
        try:
            kind = PROCESS_EVENT_IDS.get(int(event_id or "-1"))
        except ValueError:
            kind = None
        if kind is None:
            continue
        # Kernel ProcessStart/Stop carries PID, parent PID, image and creation
        # time. Path/hash/service/ancestry are populated by the owner-side
        # identity join and are required by the evaluator before correlation.
        pid = _parse_int(_data_value(event, {"ProcessId", "ProcessID", "NewProcessId"}), "ProcessId")
        parent_value = _data_value(event, {"ParentId", "ParentProcessId", "ParentPID"})
        process_name = _data_value(event, {"ImageName", "ProcessName", "ImageFileName"})
        start_time = _data_value(event, {"CreateTime", "CreationTime", "ProcessCreateTime"})
        if not start_time:
            raise ValueError("tracerpt process event lacks CreateTime")
        start_time_utc = _timestamp_text(start_time, "tracerpt process creation time")
        prior = starts.get((pid, start_time_utc))
        if kind == "start":
            if not process_name or not parent_value:
                raise ValueError("tracerpt process-start event lacks ImageName or ParentProcessID")
            parent_pid = _parse_int(parent_value, "ParentId")
            normalized_name = _normalized_process_name(process_name)
            if not normalized_name:
                raise ValueError("tracerpt process-start event has an empty ImageName")
        else:
            # Kernel-Process stop records commonly contain only ProcessID,
            # CreateTime, ExitTime and ExitCode. Reconcile their identity from
            # the matching start event instead of inventing a parent or name.
            # If a build repeats those fields, require agreement before using
            # the start record as the canonical lifecycle identity.
            if prior is None and (not process_name or not parent_value):
                raise ValueError("tracerpt process-stop event has no matching start identity")
            normalized_name = _normalized_process_name(process_name) if process_name else ""
            parent_pid = _parse_int(parent_value, "ParentId") if parent_value else None
            if prior is not None:
                if normalized_name and normalized_name.casefold() != str(prior["process_name"]).casefold():
                    raise ValueError("tracerpt process-stop ImageName disagrees with its start event")
                if parent_pid is not None and parent_pid != prior["parent_pid"]:
                    raise ValueError("tracerpt process-stop ParentProcessID disagrees with its start event")
                normalized_name = prior["process_name"]
                parent_pid = prior["parent_pid"]
            if not normalized_name or parent_pid is None:
                raise ValueError("tracerpt process-stop event has incomplete process identity")
        record = {
            "event": kind, "timestamp_utc": _event_time(event), "pid": pid,
            "process_name": normalized_name, "parent_pid": parent_pid,
            "start_time_utc": start_time_utc,
        }
        if kind == "start":
            starts[(pid, start_time_utc)] = record
        events.append(record)
    events = _required_event_shape(events)
    metadata = _parse_tracerpt_summary(summary)
    if events and metadata.get("coverage_start_utc") is not None and metadata.get("coverage_end_utc") is not None:
        first = utc(events[0]["timestamp_utc"])
        last = utc(events[-1]["timestamp_utc"])
        if first < utc(metadata["coverage_start_utc"]) or last > utc(metadata["coverage_end_utc"]):
            raise ValueError("tracerpt events fall outside the independently derived summary coverage")
    metadata["events_received"] = len(events)
    return events, metadata


def _collection_value(value: dict[str, Any], *names: str) -> Any:
    for name in names:
        if value.get(name) not in (None, ""):
            return value[name]
    return None


def _receipt_uint(fields: dict[str, str], name: str, path: Path) -> int:
    raw = fields.get(name)
    if raw is None or re.fullmatch(r"[0-9]+", raw) is None:
        raise ValueError(f"{path}: logman control receipt {name} is not a non-negative integer")
    return int(raw)


def _receipt_bool(fields: dict[str, str], name: str, path: Path) -> bool:
    raw = fields.get(name, "").casefold()
    if raw not in {"true", "false"}:
        raise ValueError(f"{path}: logman control receipt {name} is not true/false")
    return raw == "true"


def _receipt_raw_value(body: str, label: str, path: Path) -> str:
    pattern = re.compile(r"^\s*" + re.escape(label) + r"\s*[:=]\s*(.*?)\s*$", re.IGNORECASE)
    values = [match.group(1).strip().strip('"') for line in body.splitlines()
              if (match := pattern.match(line)) and match.group(1).strip()]
    if len(values) != 1:
        raise ValueError(f"{path}: logman control receipt must contain exactly one raw {label} value")
    return values[0]


def _receipt_raw_values(body: str, label: str, path: Path) -> list[str]:
    pattern = re.compile(r"^\s*" + re.escape(label) + r"\s*[:=]\s*(.*?)\s*$", re.IGNORECASE)
    values = [match.group(1).strip().strip('"') for line in body.splitlines()
              if (match := pattern.match(line)) and match.group(1).strip()]
    if not values:
        raise ValueError(f"{path}: logman control receipt has no raw {label} values")
    return values


def _retained_etl_files(raw_path: Path) -> list[Path]:
    """Enumerate the dedicated evidence directory's ETL files independently.

    The owner runbook reserves a clean directory for one direct session.  The
    parser therefore inventories every retained ``.etl`` in that directory,
    plus any raw-path sidecar segment (for example ``capture.etl.001``),
    instead of accepting a collector-provided count as proof.
    """
    raw_resolved = raw_path.resolve()
    raw_name = raw_path.name.casefold()
    files: list[Path] = []
    for candidate in raw_path.parent.iterdir():
        if not candidate.is_file():
            continue
        name = candidate.name.casefold()
        if candidate.resolve() == raw_resolved or candidate.suffix.casefold() == ".etl" \
                or name.startswith(raw_name + "."):
            files.append(candidate)
    return sorted(files, key=lambda item: item.name.casefold())


def _receipt_time_from_body(body: str, label: str, path: Path) -> str:
    try:
        return _summary_timestamp(_receipt_raw_value(body, label, path), f"{path} {label}")
    except ValueError as error:
        raise ValueError(f"{path}: raw {label} timestamp is not parseable") from error


def _receipt_raw_uint(body: str, label: str, expected: int, path: Path) -> None:
    raw = _receipt_raw_value(body, label, path)
    if re.fullmatch(r"[0-9]+", raw) is None or int(raw) != expected:
        raise ValueError(f"{path}: raw {label} does not reconcile to the retained receipt field")


def _receipt_raw_bool(body: str, label: str, expected: bool, path: Path) -> None:
    raw = _receipt_raw_value(body, label, path).casefold()
    if raw not in {"true", "false"} or (raw == "true") != expected:
        raise ValueError(f"{path}: raw {label} does not reconcile to the retained receipt field")


def _receipt_timestamp(fields: dict[str, str], body: str, operation: str, path: Path) -> str:
    sources = {
        "start": ("logman_query_output", "Start Time"),
        "logman_stop_output": ("logman_stop_output", "Stop Time"),
        "tracerpt_summary_header": ("tracerpt_summary_header", "Log End Time"),
    }
    source = fields.get("control_time_source")
    if operation == "start":
        expected_source, label = sources["start"]
    else:
        if source not in sources:
            raise ValueError(f"{path}: stop receipt has no trusted control-time source")
        expected_source, label = sources[source]
    if source != expected_source:
        raise ValueError(f"{path}: logman control receipt control-time source is invalid")
    raw_time = _receipt_time_from_body(body, label, path)
    declared_time = _timestamp_text(fields.get("control_time_utc", ""), f"{path} control time")
    if declared_time != raw_time:
        raise ValueError(f"{path}: control time does not reconcile to retained command output")
    return raw_time


def _control_receipt_projection(path: Path, fields: dict[str, str], operation: str,
                                control_time_utc: str) -> dict[str, Any]:
    result: dict[str, Any] = {
        "operation": operation,
        "path": str(path.resolve()),
        "sha256": sha256(path),
        "session_name": fields["session_name"],
        "control_time_utc": control_time_utc,
        "control_time_source": fields["control_time_source"],
        "trace_mode": fields["trace_mode"].casefold(),
        "provider_guid": _normalize_guid(fields["provider_guid"]),
        "keywords_hex": fields["keywords_hex"].casefold(),
        "level": int(fields["level"], 0),
        "max_file_mb": int(fields["max_file_mb"], 10),
    }
    if "cleanup_mode" in fields:
        result["cleanup_mode"] = fields["cleanup_mode"]
    if "delete_applicable" in fields:
        result["delete_applicable"] = fields["delete_applicable"].casefold() == "true"
    if "file_inventory_source" in fields:
        result["file_inventory_source"] = fields["file_inventory_source"]
    if "file_policy" in fields:
        result["file_policy"] = fields["file_policy"]
    for name in (
        "free_bytes_before_start", "minimum_free_bytes", "free_headroom_bytes",
        "initial_file_size", "bytes_written", "rotation_count", "etl_file_count",
    ):
        if name in fields:
            result[name] = int(fields[name], 10)
    if "truncation_detected" in fields:
        result["truncation_detected"] = fields["truncation_detected"].casefold() == "true"
    return result


def _read_control_receipt(path: Path, operation: str, session_name: str | None,
                          raw_path: Path, max_file_mb: int) -> dict[str, Any]:
    text = _read_summary_text(path)
    marker = "--- RAW LOGMAN OUTPUT ---"
    if marker not in text:
        raise ValueError(f"{path}: logman control receipt has no raw-output separator")
    header, body = text.split(marker, 1)
    fields: dict[str, str] = {}
    for line in header.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        fields[key.strip()] = value.strip()
    if fields.get("format") != COLLECTION_RECEIPT_FORMAT or fields.get("operation") != operation:
        raise ValueError(f"{path}: logman control receipt format/operation is invalid")
    if session_name is not None and fields.get("session_name") != session_name:
        raise ValueError(f"{path}: logman control receipt session does not match collection metadata")
    receipt_session = fields.get("session_name", "")
    if not receipt_session or receipt_session.startswith("<"):
        raise ValueError(f"{path}: logman control receipt session is missing")
    control_time_utc = _receipt_timestamp(fields, body, operation, path)
    for key in ("trace_mode", "provider_guid", "keywords_hex", "level", "etl_path", "max_file_mb"):
        if not fields.get(key):
            raise ValueError(f"{path}: logman control receipt is missing {key}")
    declared_etl = fields["etl_path"].replace("\\", "/").casefold()
    if declared_etl.rsplit("/", 1)[-1] != raw_path.name.casefold():
        raise ValueError(f"{path}: logman control receipt ETL filename does not match the retained ETL")
    if fields["trace_mode"].casefold() != COLLECTION_TRACE_MODE:
        raise ValueError(f"{path}: circular or non-binary ETW log mode is not accepted")
    if _normalize_guid(fields["provider_guid"]) != _normalize_guid(COLLECTION_PROVIDER_GUID):
        raise ValueError(f"{path}: logman control receipt provider GUID is not the kernel-process provider")
    if fields["keywords_hex"].casefold() != COLLECTION_KEYWORDS_HEX:
        raise ValueError(f"{path}: logman control receipt keyword mask is not 0x10")
    try:
        if int(fields["level"], 0) != COLLECTION_LEVEL:
            raise ValueError
        expected_max = int(fields["max_file_mb"], 10)
    except ValueError as error:
        raise ValueError(f"{path}: logman control receipt level or max file size is invalid") from error
    if (expected_max != max_file_mb or expected_max != COLLECTION_MAX_FILE_MB
            or expected_max <= 0):
        raise ValueError(f"{path}: logman control receipt max file size does not match the pinned policy")
    lower_header = header.casefold()
    lower_body = body.casefold()
    all_text = (header + "\n" + body).casefold()
    cleanup_mode = fields.get("cleanup_mode")
    if cleanup_mode is not None and cleanup_mode not in {
            COLLECTION_CLEANUP_DIRECT, COLLECTION_CLEANUP_PERSISTED}:
        raise ValueError(f"{path}: logman control receipt cleanup mode is unsupported")
    if receipt_session.casefold() not in all_text or raw_path.name.casefold() not in all_text:
        raise ValueError(f"{path}: logman control receipt is not bound to the session and ETL filename")
    if operation == "start":
        for name in ("free_bytes_before_start", "minimum_free_bytes", "free_headroom_bytes",
                     "initial_file_size", "rotation_count"):
            _receipt_uint(fields, name, path)
        if _receipt_bool(fields, "truncation_detected", path):
            raise ValueError(f"{path}: start receipt reports truncation")
        free_bytes = int(fields["free_bytes_before_start"])
        minimum_free = int(fields["minimum_free_bytes"])
        headroom = int(fields["free_headroom_bytes"])
        _receipt_raw_uint(body, "Free Bytes Before Start", free_bytes, path)
        _receipt_raw_uint(body, "Minimum Free Bytes", minimum_free, path)
        _receipt_raw_uint(body, "Free Headroom Bytes", headroom, path)
        _receipt_raw_uint(body, "Initial File Size", int(fields["initial_file_size"]), path)
        _receipt_raw_uint(body, "Rotation Count", int(fields["rotation_count"]), path)
        _receipt_raw_bool(body, "Truncation Detected", False, path)
        if (int(fields["initial_file_size"]) != 0 or int(fields["rotation_count"]) != 0
                or headroom <= 0
                or minimum_free != expected_max * 1024 * 1024 + headroom
                or free_bytes < minimum_free):
            raise ValueError(f"{path}: start receipt does not prove sufficient non-circular capacity")
        try:
            create_exit = _receipt_uint(fields, "create_exit_code", path)
            query_exit = _receipt_uint(fields, "query_exit_code", path)
        except ValueError as error:
            raise ValueError(f"{path}: start logman exit codes are invalid") from error
        if create_exit != 0 or query_exit != 0:
            raise ValueError(f"{path}: logman create/query did not complete successfully")
        create_command = lower_header
        if "[create]" not in lower_body or "[query]" not in lower_body:
            raise ValueError(f"{path}: start receipt does not retain separate create/query output")
        if not re.search(r"command_create\s*=.*\blogman(?:\.exe)?\s+create\s+trace\b", create_command):
            raise ValueError(f"{path}: start receipt does not retain the logman create command")
        if re.search(r"bincirc", create_command) or not re.search(r"(?:^|\s)-f\s+['\"]?bin(?:['\"]?)(?:\s|$)", create_command):
            raise ValueError(f"{path}: circular ETW logging mode is present or binary mode is absent")
        if not re.search(r"(?:^|\s)-max\s+['\"]?" + str(expected_max) + r"(?:['\"]?)(?:\s|$)", create_command):
            raise ValueError(f"{path}: start receipt does not retain the configured max file size")
        if not re.search(r"(?:^|\s)-p\s+['\"]?" + re.escape(COLLECTION_PROVIDER_GUID) + r"(?:['\"]?)(?:\s|$)", create_command, re.IGNORECASE):
            raise ValueError(f"{path}: start receipt does not retain the exact provider GUID argument")
        if COLLECTION_KEYWORDS_HEX not in create_command or re.search(r"0x10\s+5(?:\s|$)", create_command) is None:
            raise ValueError(f"{path}: start receipt does not retain the provider keyword/level")
        if not re.search(r"command_query\s*=.*\blogman(?:\.exe)?\s+query\b", create_command):
            raise ValueError(f"{path}: start receipt does not retain the logman query command")
        ets_arg = r"(?:^|\s)-ets(?:\s|$)"
        if cleanup_mode == COLLECTION_CLEANUP_DIRECT:
            if (not re.search(r"command_create\s*=.*" + ets_arg, create_command)
                    or not re.search(r"command_query\s*=.*" + ets_arg, create_command)):
                raise ValueError(f"{path}: direct transient create/query commands must use -ets")
        elif cleanup_mode == COLLECTION_CLEANUP_PERSISTED:
            if re.search(r"command_(?:create|query)\s*=.*" + ets_arg, create_command):
                raise ValueError(f"{path}: persisted DCS create/query commands must not use -ets")
        if not re.search(r"(?im)^\s*status\s*:\s*running\b", body):
            raise ValueError(f"{path}: start logman query did not prove a running session")
        if _normalize_guid(COLLECTION_PROVIDER_GUID) not in _normalize_guid(body):
            raise ValueError(f"{path}: start logman query did not prove the provider GUID")
        if "microsoft-windows-kernel-process" not in lower_body:
            raise ValueError(f"{path}: start logman query did not prove the provider name")
        if COLLECTION_KEYWORDS_HEX not in lower_body or re.search(r"(?im)^\s*(?:level|verbosity)\s*[:=]\s*5\b", body) is None:
            raise ValueError(f"{path}: start logman query did not prove keyword 0x10 and level 5")
        mode_lines = [line.casefold() for line in body.splitlines() if re.search(r"log\s+mode", line, re.IGNORECASE)]
        if not mode_lines or any("circular" in line for line in mode_lines) \
                or not any(re.search(r"\b(?:sequential|file|normal)\b", line) for line in mode_lines):
            raise ValueError(f"{path}: start logman query did not prove non-circular log mode")
        size_match = re.search(r"(?im)^\s*maximum\s+file\s+size\s*:\s*([0-9][0-9,]*)\b", body)
        if size_match is None or int(size_match.group(1).replace(",", "")) != expected_max:
            raise ValueError(f"{path}: start logman query did not prove the configured max file size")
        result = _control_receipt_projection(path, fields, operation, control_time_utc)
        result.update({"exit_code": create_exit, "query_exit_code": query_exit})
        return result
    for name in ("bytes_written", "rotation_count", "etl_file_count"):
        _receipt_uint(fields, name, path)
    if _receipt_bool(fields, "truncation_detected", path):
        raise ValueError(f"{path}: stop receipt reports ETL truncation")
    cleanup_mode = fields.get("cleanup_mode")
    if cleanup_mode not in {COLLECTION_CLEANUP_DIRECT, COLLECTION_CLEANUP_PERSISTED}:
        raise ValueError(f"{path}: stop receipt cleanup mode is unsupported")
    if (fields.get("file_policy") != COLLECTION_FILE_POLICY
            or fields.get("file_inventory_source") != COLLECTION_INVENTORY_SOURCE):
        raise ValueError(f"{path}: stop receipt does not identify the independent single-file ETL inventory policy")
    delete_applicable_raw = fields.get("delete_applicable", "").casefold()
    if delete_applicable_raw not in {"true", "false"}:
        raise ValueError(f"{path}: stop receipt delete applicability is invalid")
    delete_applicable = delete_applicable_raw == "true"
    if ((cleanup_mode == COLLECTION_CLEANUP_DIRECT and delete_applicable)
            or (cleanup_mode == COLLECTION_CLEANUP_PERSISTED and not delete_applicable)):
        raise ValueError(f"{path}: stop receipt cleanup mode/delete applicability disagree")
    stop_exit = _receipt_uint(fields, "stop_exit_code", path)
    post_query_exit = _receipt_uint(fields, "post_stop_query_exit_code", path)
    probe_exit = _receipt_uint(fields, "tracerpt_probe_exit_code", path)
    if stop_exit != 0 or post_query_exit == 0 or probe_exit != 0:
        raise ValueError(f"{path}: logman stop/post-stop-query or tracerpt probe did not prove cleanup")
    delete_exit: int | None = None
    if delete_applicable:
        delete_exit = _receipt_uint(fields, "delete_exit_code", path)
        if delete_exit != 0:
            raise ValueError(f"{path}: applicable logman delete did not complete successfully")
    elif fields.get("delete_exit_code", "").casefold() != "not_applicable":
        raise ValueError(f"{path}: direct transient session must mark delete as not applicable")
    actual_bytes = raw_path.stat().st_size if raw_path.is_file() else -1
    bytes_written = int(fields["bytes_written"])
    rotation_count = int(fields["rotation_count"])
    etl_file_count = int(fields["etl_file_count"])
    _receipt_raw_uint(body, "Bytes Written", bytes_written, path)
    _receipt_raw_uint(body, "Rotation Count", rotation_count, path)
    _receipt_raw_uint(body, "ETL File Count", etl_file_count, path)
    _receipt_raw_bool(body, "Truncation Detected", False, path)
    retained_files = _retained_etl_files(raw_path)
    retained_names = sorted(item.name.casefold() for item in retained_files)
    raw_names = sorted(item.casefold() for item in _receipt_raw_values(body, "ETL File", path))
    if (actual_bytes <= 0 or bytes_written != actual_bytes
            or bytes_written >= expected_max * 1024 * 1024
            or etl_file_count != len(retained_files)
            or raw_names != retained_names
            or rotation_count != max(0, len(retained_files) - 1)):
        raise ValueError(f"{path}: stop receipt bytes/rotation/file-inventory/truncation evidence does not reconcile to the ETL directory")
    if etl_file_count != 1 or rotation_count != 0:
        raise ValueError(f"{path}: independent ETL inventory violates the {COLLECTION_FILE_POLICY} policy")
    stop_command = lower_header
    required_markers = ["[stop]", "[post-stop-query]"]
    if delete_applicable:
        required_markers.append("[delete]")
    if any(marker not in lower_body for marker in required_markers):
        raise ValueError(f"{path}: stop receipt does not retain separate cleanup output")
    if not delete_applicable and ("[delete]" in lower_body or "command_delete=" in stop_command):
        raise ValueError(f"{path}: direct transient receipt contains an inapplicable delete operation")
    if not re.search(r"command_stop\s*=.*\blogman(?:\.exe)?\s+stop\b", stop_command):
        raise ValueError(f"{path}: stop receipt does not retain the logman stop command")
    if not re.search(r"command_post_stop_query\s*=.*\blogman(?:\.exe)?\s+query\b", stop_command):
        raise ValueError(f"{path}: stop receipt does not retain the post-stop query command")
    if delete_applicable and not re.search(r"command_delete\s*=.*\blogman(?:\.exe)?\s+delete\b", stop_command):
        raise ValueError(f"{path}: stop receipt does not retain the applicable logman delete command")
    ets_arg = r"(?:^|\s)-ets(?:\s|$)"
    if cleanup_mode == COLLECTION_CLEANUP_DIRECT:
        if (not re.search(r"command_stop\s*=.*" + ets_arg, stop_command)
                or not re.search(r"command_post_stop_query\s*=.*" + ets_arg, stop_command)):
            raise ValueError(f"{path}: direct transient cleanup commands must use -ets")
    elif re.search(r"command_(?:stop|post_stop_query|delete)\s*=.*" + ets_arg, stop_command):
        raise ValueError(f"{path}: persisted DCS cleanup commands must not use -ets")
    if fields["control_time_source"] == "tracerpt_summary_header" and not re.search(
            r"command_tracerpt_probe\s*=.*\btracerpt(?:\.exe)?\b", stop_command):
        raise ValueError(f"{path}: stop receipt does not retain the tracerpt header probe command")
    if "completed successfully" not in lower_body:
        raise ValueError(f"{path}: stop receipt does not retain successful logman stop output")
    if delete_applicable and re.search(r"\[delete\][\s\S]*?completed successfully", lower_body) is None:
        raise ValueError(f"{path}: stop receipt does not retain successful logman delete output")
    if re.search(r"(?:not found|does not exist|cannot find|no such)", lower_body) is None:
        raise ValueError(f"{path}: post-stop query does not prove the session was disabled")
    result = _control_receipt_projection(path, fields, operation, control_time_utc)
    result.update({
        "exit_code": stop_exit,
        "post_stop_query_exit_code": post_query_exit,
        "delete_exit_code": delete_exit,
        "tracerpt_probe_exit_code": probe_exit,
        "bytes_written": bytes_written,
        "rotation_count": rotation_count,
        "etl_file_count": etl_file_count,
        "file_inventory_source": fields["file_inventory_source"],
        "truncation_detected": False,
    })
    return result


def _normalize_guid(value: Any) -> str:
    return str(value).strip().strip("{}").casefold()


def _collection_int(value: dict[str, Any], name: str) -> int:
    raw = value.get(name)
    if type(raw) is int:
        return raw
    if isinstance(raw, str) and re.fullmatch(r"[0-9]+", raw):
        return int(raw)
    raise ValueError(f"ETW collection metadata {name} is not a non-negative integer")


def _read_collection_metadata(path: Path, raw_path: Path,
                              summary_metadata: dict[str, Any]) -> dict[str, Any]:
    value = read_json(path)
    if value.get("format") != COLLECTION_METADATA_FORMAT:
        raise ValueError("ETW collection metadata format is not supported")
    if value.get("provider") != PROVIDER:
        raise ValueError("ETW collection metadata provider is not the kernel process provider")
    if _normalize_guid(value.get("provider_guid")) != _normalize_guid(COLLECTION_PROVIDER_GUID):
        raise ValueError("ETW collection metadata provider GUID is not the kernel-process provider")
    if str(value.get("keywords_hex", "")).casefold() != COLLECTION_KEYWORDS_HEX \
            or type(value.get("level")) is not int or value.get("level") != COLLECTION_LEVEL:
        raise ValueError("ETW collection metadata keyword mask/level is not the pinned process selection")
    trace_mode = str(value.get("trace_mode", "")).casefold()
    if trace_mode != COLLECTION_TRACE_MODE:
        raise ValueError("ETW collection metadata is circular or not in bounded binary mode")
    max_file_mb = _collection_int(value, "max_file_mb")
    free_headroom_bytes = _collection_int(value, "free_headroom_bytes")
    minimum_free_bytes = _collection_int(value, "minimum_free_bytes")
    free_bytes = _collection_int(value, "free_bytes_before_start")
    if (max_file_mb != COLLECTION_MAX_FILE_MB
            or free_headroom_bytes != COLLECTION_FREE_HEADROOM_BYTES
            or minimum_free_bytes != max_file_mb * 1024 * 1024 + free_headroom_bytes
            or free_bytes < minimum_free_bytes):
        raise ValueError("ETW collection metadata does not prove sufficient non-circular file capacity")
    if value.get("capacity_policy") != COLLECTION_CAPACITY_POLICY:
        raise ValueError("ETW collection metadata capacity policy is not pinned")
    if value.get("file_policy") != COLLECTION_FILE_POLICY \
            or value.get("file_inventory_source") != COLLECTION_INVENTORY_SOURCE:
        raise ValueError("ETW collection metadata does not identify the independent single-file inventory policy")
    cleanup_mode = value.get("cleanup_mode")
    if cleanup_mode not in {COLLECTION_CLEANUP_DIRECT, COLLECTION_CLEANUP_PERSISTED}:
        raise ValueError("ETW collection metadata cleanup mode is unsupported")
    delete_applicable = value.get("delete_applicable")
    if type(delete_applicable) is not bool:
        raise ValueError("ETW collection metadata delete applicability is not boolean")
    delete_exit_value = value.get("delete_exit_code")
    if cleanup_mode == COLLECTION_CLEANUP_DIRECT:
        if delete_applicable or "delete_exit_code" not in value or delete_exit_value is not None:
            raise ValueError("direct transient collection metadata must explicitly mark delete as not applicable")
    elif (not delete_applicable or type(delete_exit_value) is not int
          or delete_exit_value != 0):
        raise ValueError("persisted DCS collection metadata must claim a successful delete")
    bytes_written = _collection_int(value, "bytes_written")
    rotation_count = _collection_int(value, "rotation_count")
    etl_file_count = _collection_int(value, "etl_file_count")
    retained_files = _retained_etl_files(raw_path)
    if (bytes_written <= 0 or bytes_written >= max_file_mb * 1024 * 1024
            or etl_file_count != len(retained_files)
            or rotation_count != max(0, len(retained_files) - 1)
            or value.get("truncation_detected") is not False
            or raw_path.stat().st_size != bytes_written):
        raise ValueError("ETW collection metadata does not reconcile to the independent ETL file inventory")
    if etl_file_count != 1 or rotation_count != 0:
        raise ValueError(f"ETW collection metadata violates the {COLLECTION_FILE_POLICY} policy")
    declared_session = _collection_value(value, "session_name", "logger_name")
    declared_start = _collection_value(value, "coverage_start_utc", "collection_start_utc", "start_utc")
    declared_end = _collection_value(value, "coverage_end_utc", "collection_end_utc", "stop_utc")
    receipt_specs = (
        ("start_record_path", "start_record_sha256", "start"),
        ("stop_record_path", "stop_record_sha256", "stop"),
    )
    control_receipts: list[dict[str, Any]] = []
    for path_field, hash_field, operation in receipt_specs:
        receipt_value = value.get(path_field)
        receipt_hash = value.get(hash_field)
        if not isinstance(receipt_value, str) or not receipt_value \
                or not isinstance(receipt_hash, str) or not SHA256_RE.fullmatch(receipt_hash):
            raise ValueError(f"ETW collection metadata {path_field}/{hash_field} is missing")
        receipt_path = Path(receipt_value)
        if not receipt_path.is_absolute():
            receipt_path = (path.parent / receipt_path).resolve()
        if receipt_path == path.resolve() or not receipt_path.is_file() \
                or sha256(receipt_path).casefold() != receipt_hash.casefold():
            raise ValueError(f"ETW collection metadata {operation} control receipt is missing or hash-mismatched")
        control_receipts.append(_read_control_receipt(
            receipt_path, operation, str(declared_session) if declared_session else None,
            raw_path, max_file_mb
        ))
    if len({item["path"] for item in control_receipts}) != len(control_receipts):
        raise ValueError("ETW collection metadata start/stop control receipts are not distinct")
    start_receipt, stop_receipt = control_receipts
    receipt_session = start_receipt["session_name"]
    if stop_receipt["session_name"] != receipt_session:
        raise ValueError("ETW collection start/stop receipts name different sessions")
    if declared_session is not None and str(declared_session) != receipt_session:
        raise ValueError("ETW collection metadata session is not bound to control receipts")
    session_value = receipt_session
    receipt_start = start_receipt["control_time_utc"]
    receipt_end = stop_receipt["control_time_utc"]
    summary_start = summary_metadata.get("coverage_start_utc")
    summary_end = summary_metadata.get("coverage_end_utc")
    for summary_value, receipt_value, label in ((summary_start, receipt_start, "start"),
                                                 (summary_end, receipt_end, "end")):
        if summary_value is not None and summary_value != receipt_value:
            raise ValueError(f"ETW collection {label} interval does not reconcile to the trusted control receipt")
    start = summary_start or receipt_start
    end = summary_end or receipt_end
    if declared_start is not None and _timestamp_text(declared_start, "collection start timestamp") != start:
        raise ValueError("ETW collection metadata start timestamp is not independently derived")
    if declared_end is not None and _timestamp_text(declared_end, "collection stop timestamp") != end:
        raise ValueError("ETW collection metadata stop timestamp is not independently derived")
    if utc(end) <= utc(start):
        raise ValueError("ETW collection metadata interval is not positive")
    for field, expected in (("start_observed_utc", start), ("stop_observed_utc", end)):
        if value.get(field) is not None and _timestamp_text(value[field], f"collection {field}") != expected:
            raise ValueError("ETW collection metadata interval is not bound to observed logman control times")
    declared_etl = _collection_value(value, "etl_path", "collector_artifact_path")
    if declared_etl is not None:
        declared_text = str(declared_etl).replace("\\", "/").casefold()
        if declared_text.rsplit("/", 1)[-1] != raw_path.name.casefold():
            raise ValueError("ETW collection metadata does not bind the retained ETL filename")
    cleanup_mode = stop_receipt.get("cleanup_mode")
    if cleanup_mode not in {COLLECTION_CLEANUP_DIRECT, COLLECTION_CLEANUP_PERSISTED}:
        raise ValueError("ETW collection stop cleanup mode is missing")
    if (start_receipt.get("cleanup_mode") != cleanup_mode
            or stop_receipt.get("file_policy") != COLLECTION_FILE_POLICY
            or stop_receipt.get("file_inventory_source") != COLLECTION_INVENTORY_SOURCE
            or value.get("cleanup_mode") != cleanup_mode
            or value.get("delete_applicable") is not stop_receipt.get("delete_applicable")
            or value.get("delete_exit_code") != stop_receipt.get("delete_exit_code")):
        raise ValueError("ETW collection metadata cleanup/inventory outcome is not bound to the control receipts")
    if (start_receipt["trace_mode"] != trace_mode
            or stop_receipt["trace_mode"] != trace_mode
            or start_receipt["max_file_mb"] != max_file_mb
            or stop_receipt["max_file_mb"] != max_file_mb
            or start_receipt.get("free_bytes_before_start") != free_bytes
            or start_receipt.get("minimum_free_bytes") != minimum_free_bytes
            or start_receipt.get("free_headroom_bytes") != free_headroom_bytes
            or stop_receipt.get("bytes_written") != bytes_written
            or stop_receipt.get("rotation_count") != rotation_count
            or stop_receipt.get("etl_file_count") != etl_file_count
            or stop_receipt.get("truncation_detected") is not False):
        raise ValueError("ETW collection metadata is not bound to control capacity/mode evidence")
    for field, expected in (("coverage_start_utc", start), ("coverage_end_utc", end),
                            ("session_name", session_value)):
        summary_value = summary_metadata.get(field)
        if summary_value is not None and summary_value != expected:
            raise ValueError(f"ETW collection metadata {field} does not reconcile to tracerpt summary")
    coverage_source = (SUMMARY_AND_CONTROL_COVERAGE_SOURCE
                       if summary_start is not None and summary_end is not None
                       else CONTROL_COVERAGE_SOURCE)
    session_source = (SUMMARY_AND_CONTROL_SESSION_SOURCE
                      if summary_metadata.get("session_name") is not None
                      else CONTROL_SESSION_SOURCE)
    return {
        "coverage_start_utc": start,
        "coverage_end_utc": end,
        "session_name": session_value,
        "collection_metadata_source": COLLECTION_METADATA_SOURCE,
        "collection_control_receipt_source": COLLECTION_CONTROL_RECEIPT_SOURCE,
        "collection_control_receipts": control_receipts,
        "coverage_source": coverage_source,
        "session_name_source": session_source,
        "trace_mode": trace_mode,
        "max_file_mb": max_file_mb,
        "free_headroom_bytes": free_headroom_bytes,
        "minimum_free_bytes": minimum_free_bytes,
        "free_bytes_before_start": free_bytes,
        "bytes_written": bytes_written,
        "rotation_count": rotation_count,
        "etl_file_count": etl_file_count,
        "file_inventory_source": COLLECTION_INVENTORY_SOURCE,
        "file_policy": COLLECTION_FILE_POLICY,
        "truncation_detected": False,
        "cleanup_mode": cleanup_mode,
        "delete_applicable": stop_receipt.get("delete_applicable"),
        "delete_exit_code": stop_receipt.get("delete_exit_code"),
    }


def _run_tracerpt(raw_path: Path, tool_path: Path,
                  retained_summary_path: Path | None = None,
                  collection_metadata_path: Path | None = None) -> tuple[list[dict[str, Any]], dict[str, Any], str, list[str]]:
    if not tool_path.is_file():
        raise ValueError(f"pinned tracerpt executable is missing: {tool_path}")
    version = _tool_version(tool_path)
    if version != PINNED_TRACERPT_VERSION:
        raise ValueError(f"tracerpt version {version!r} is not the pinned {PINNED_TRACERPT_VERSION!r}")
    with tempfile.TemporaryDirectory(prefix="fx-t18-tracerpt-") as raw_tmp:
        output = Path(raw_tmp) / "decoded.xml"
        summary = (retained_summary_path.resolve() if retained_summary_path is not None
                   else Path(raw_tmp) / "tracerpt-summary.txt")
        summary.parent.mkdir(parents=True, exist_ok=True)
        command = [str(tool_path.resolve()), str(raw_path.resolve()), "-o", str(output),
                   "-of", "XML", "-lr", "-summary", str(summary), "-y"]
        completed = subprocess.run(command, check=False, capture_output=True, text=True, timeout=180)
        if completed.returncode != 0 or not output.is_file() or not summary.is_file():
            raise ValueError("pinned tracerpt failed to decode the raw ETL and write its summary")
        # stdout/stderr are diagnostics only. Loss and trace metadata must come
        # from the explicitly requested -summary artifact and, when the
        # versioned tracerpt summary omits session/stop metadata, the retained
        # owner logman start/stop record.
        events, counters = _parse_xml(output, _read_summary_text(summary))
        if collection_metadata_path is not None:
            collection = _read_collection_metadata(collection_metadata_path.resolve(), raw_path, counters)
            counters.update(collection)
        for field in ("coverage_start_utc", "coverage_end_utc", "session_name"):
            if not counters.get(field):
                raise ValueError(f"tracerpt summary/collection metadata lacks {field}")
        if events:
            first = utc(events[0]["timestamp_utc"])
            last = utc(events[-1]["timestamp_utc"])
            if first < utc(counters["coverage_start_utc"]) or last > utc(counters["coverage_end_utc"]):
                raise ValueError("tracerpt events fall outside the retained collection interval")
        counters["summary_path"] = str(summary)
        counters["summary_sha256"] = sha256(summary)
        counters["command_argv"] = command
    return events, counters, version, command


def make_receipt(raw_path: Path, coverage_start: str | None = None,
                 coverage_end: str | None = None, session_name: str | None = None,
                 fixture: bool = False, tracerpt_path: Path | None = None,
                 identity_join_path: Path | None = None,
                 summary_path: Path | None = None,
                 collection_metadata_path: Path | None = None) -> dict[str, Any]:
    if not raw_path.is_file():
        raise ValueError(f"raw ETL is missing: {raw_path}")
    join_path: Path | None = None
    if fixture:
        raw_events, counters, tool_version = _fixture_result(raw_path)
        events = raw_events
        tool_name = "fixture-json-decoder"
        command_argv = ["parse-fx-t18-etl.py", "--fixture-raw", str(raw_path.resolve()),
                        "--output", "<receipt.json>"]
        start = utc_text(coverage_start or "2026-01-01T00:00:00Z")
        end = utc_text(coverage_end or "2026-01-01T00:01:00Z")
        derived_session = session_name or "FX-T18-fixture-session"
        counters.update({
            "coverage_start_utc": start, "coverage_end_utc": end,
            "session_name": derived_session,
            "summary_source": "fixture-json-declared-counters",
            "collection_metadata_source": "fixture-declared-metadata",
            "collection_control_receipt_source": "fixture-declared-metadata",
            "collection_control_receipts": [],
            "coverage_source": "fixture-declared-metadata",
            "session_name_source": "fixture-declared-metadata",
        })
    else:
        if collection_metadata_path is None:
            raise ValueError("production ETW parsing requires retained collection start/stop metadata")
        tool = tracerpt_path or Path(os.environ.get("FX_T18_TRACERPT", "tracerpt.exe"))
        raw_events, counters, tool_version, command_argv = _run_tracerpt(
            raw_path, tool, summary_path, collection_metadata_path)
        tool_name = command_argv[0]
        if identity_join_path is None:
            raise ValueError("production ETW parsing requires a retained identity join artifact")
        join_path = identity_join_path.resolve()
        if not join_path.is_file():
            raise ValueError(f"ETW identity join artifact is missing: {join_path}")
        events = _apply_identity_join(raw_events, _read_identity_join(join_path))
    start = counters["coverage_start_utc"]
    end = counters["coverage_end_utc"]
    derived_session = counters["session_name"]
    if utc(end) <= utc(start):
        raise ValueError("ETL coverage interval is not positive")
    raw_hash = sha256(raw_path)
    full_digest = event_digest(events)
    provenance: dict[str, Any] = {
        "parser_name": "vox-fx-t18-raw-etl-verifier", "parser_version": PARSER_VERSION,
        "toolchain_id": TOOLCHAIN_ID, "tool": tool_name, "tool_version": tool_version,
        "command_line": json.dumps(command_argv, ensure_ascii=True),
        "command_argv": command_argv, "command_template": TRACERPT_COMMAND,
        "event_source": EVENT_SOURCE, "counter_source": LOSS_SOURCE,
        "summary_source": counters["summary_source"],
        "collection_metadata_source": counters["collection_metadata_source"],
        "collection_control_receipt_source": counters["collection_control_receipt_source"],
        "collection_control_receipts": counters.get("collection_control_receipts", []),
        "coverage_source": counters["coverage_source"],
        "session_name_source": counters["session_name_source"],
        "derived_coverage_start_utc": start, "derived_coverage_end_utc": end,
        "derived_session_name": derived_session,
        "raw_etl_sha256": raw_hash,
        "decoded_core_event_digest": core_event_digest(raw_events),
        "decoded_event_digest": full_digest,
        "provenance": "owner-independent replay of retained raw ETL bytes",
        "replay": "python parse-fx-t18-etl.py --verify-receipt --etl <raw-etl> --receipt <parse.json>",
        "fixture_mode": fixture,
    }
    if not fixture:
        provenance.update({
            "summary_path": counters["summary_path"],
            "summary_sha256": counters["summary_sha256"],
            "collection_metadata_path": str(collection_metadata_path.resolve()),
            "collection_metadata_sha256": sha256(collection_metadata_path.resolve()),
        })
    if join_path is not None:
        provenance.update({
            "identity_join_format": "VOX_FX_T18_ETW_IDENTITY_JOIN_V1",
            "identity_join_path": str(join_path),
            "identity_join_sha256": sha256(join_path),
        })
    return {
        "format": FORMAT, "provider": PROVIDER, "session_name": derived_session,
        "coverage_start_utc": start, "coverage_end_utc": end,
        "events_received": counters["events_received"],
        "lost_events": counters["lost_events"], "buffer_loss_events": counters["buffer_loss_events"],
        "collector_artifact_sha256": raw_hash, "collector_artifact_path": str(raw_path.resolve()),
        "process_events": events, "process_event_digest": full_digest,
        "parser_provenance": provenance,
    }

def verify_receipt(raw_path: Path, receipt: dict[str, Any], fixture: bool = False) -> None:
    if receipt.get("format") != FORMAT or receipt.get("provider") != PROVIDER:
        raise ValueError("ETW parse receipt format/provider is not supported")
    provenance = receipt.get("parser_provenance")
    if not isinstance(provenance, dict):
        raise ValueError("ETW parse receipt lacks parser provenance")
    required = (
        "parser_name", "parser_version", "toolchain_id", "tool_version", "command_line",
        "command_template", "event_source", "counter_source", "summary_source",
        "collection_metadata_source", "collection_control_receipt_source", "coverage_source",
        "session_name_source", "derived_coverage_start_utc",
        "derived_coverage_end_utc", "derived_session_name", "raw_etl_sha256",
        "decoded_core_event_digest", "decoded_event_digest", "provenance", "replay",
    )
    if any(not isinstance(provenance.get(field), str) or not provenance[field] for field in required):
        raise ValueError("ETW parse receipt parser provenance is incomplete")
    if provenance.get("parser_name") != "vox-fx-t18-raw-etl-verifier" \
            or provenance.get("parser_version") != PARSER_VERSION \
            or provenance.get("toolchain_id") != TOOLCHAIN_ID \
            or provenance.get("event_source") != EVENT_SOURCE \
            or provenance.get("counter_source") != LOSS_SOURCE \
            or provenance.get("summary_source") != ("fixture-json-declared-counters" if fixture else SUMMARY_SOURCE) \
            or provenance.get("collection_metadata_source") != ("fixture-declared-metadata" if fixture else COLLECTION_METADATA_SOURCE) \
            or provenance.get("collection_control_receipt_source") != ("fixture-declared-metadata" if fixture else COLLECTION_CONTROL_RECEIPT_SOURCE) \
            or (not fixture and provenance.get("coverage_source") not in {
                COVERAGE_SOURCE, SUMMARY_AND_CONTROL_COVERAGE_SOURCE, CONTROL_COVERAGE_SOURCE
            }) \
            or (not fixture and provenance.get("session_name_source") not in {
                SESSION_SOURCE, SUMMARY_AND_CONTROL_SESSION_SOURCE, CONTROL_SESSION_SOURCE
            }) \
            or (fixture and (provenance.get("coverage_source") != "fixture-declared-metadata"
                             or provenance.get("session_name_source") != "fixture-declared-metadata")) \
            or provenance.get("command_template") != TRACERPT_COMMAND \
            or not isinstance(provenance.get("command_argv"), list) \
            or any(not isinstance(item, str) or not item for item in provenance["command_argv"]):
        raise ValueError("ETW parse receipt is not from the pinned independent parser")
    control_receipts = provenance.get("collection_control_receipts")
    if not isinstance(control_receipts, list) or (not fixture and len(control_receipts) != 2):
        raise ValueError("ETW parse receipt lacks the two hashed logman control receipts")
    try:
        if json.loads(provenance["command_line"]) != provenance["command_argv"]:
            raise ValueError("command line and argument vector differ")
    except (TypeError, json.JSONDecodeError) as error:
        raise ValueError("ETW parse receipt command line is not a canonical argument vector") from error
    if not fixture:
        argv = provenance["command_argv"]
        if "-summary" not in argv or "-lr" not in argv or "--identity-join" in argv:
            raise ValueError("tracerpt command argv does not request a separate summary artifact")
        summary_value = provenance.get("summary_path")
        summary_hash = provenance.get("summary_sha256")
        if (not isinstance(summary_value, str) or not summary_value
                or not isinstance(summary_hash, str) or not SHA256_RE.fullmatch(summary_hash)):
            raise ValueError("ETW parse receipt lacks its retained tracerpt summary path/hash")
        summary_path = Path(summary_value)
        if not summary_path.is_file() or sha256(summary_path).casefold() != summary_hash.casefold():
            raise ValueError("retained tracerpt summary hash does not reconcile")
        retained_summary = _parse_tracerpt_summary(_read_summary_text(summary_path))
        collection_value = provenance.get("collection_metadata_path")
        collection_hash = provenance.get("collection_metadata_sha256")
        if (not isinstance(collection_value, str) or not collection_value
                or not isinstance(collection_hash, str) or not SHA256_RE.fullmatch(collection_hash)):
            raise ValueError("ETW parse receipt lacks retained collection start/stop metadata provenance")
        collection_path = Path(collection_value)
        if not collection_path.is_file() or sha256(collection_path).casefold() != collection_hash.casefold():
            raise ValueError("retained collection metadata hash does not reconcile")
        collection = _read_collection_metadata(collection_path, raw_path, retained_summary)
        expected_metadata = {
            "lost_events": receipt.get("lost_events"),
            "buffer_loss_events": receipt.get("buffer_loss_events"),
            "coverage_start_utc": receipt.get("coverage_start_utc"),
            "coverage_end_utc": receipt.get("coverage_end_utc"),
            "session_name": receipt.get("session_name"),
        }
        actual_metadata = {
            "lost_events": retained_summary["lost_events"],
            "buffer_loss_events": retained_summary["buffer_loss_events"],
            "coverage_start_utc": collection["coverage_start_utc"],
            "coverage_end_utc": collection["coverage_end_utc"],
            "session_name": collection["session_name"],
        }
        if actual_metadata != expected_metadata:
            raise ValueError("retained summary/collection metadata does not reconcile to the parse receipt")
        if collection.get("collection_control_receipts") != control_receipts:
            raise ValueError("retained logman control receipts do not reconcile to parser provenance")
    actual_hash = sha256(raw_path)
    if actual_hash.casefold() != str(receipt.get("collector_artifact_sha256", "")).casefold() \
            or actual_hash.casefold() != str(provenance.get("raw_etl_sha256", "")).casefold():
        raise ValueError("ETW parse receipt raw ETL hash does not reconcile")
    if (receipt.get("coverage_start_utc") != provenance.get("derived_coverage_start_utc")
            or receipt.get("coverage_end_utc") != provenance.get("derived_coverage_end_utc")
            or receipt.get("session_name") != provenance.get("derived_session_name")):
        raise ValueError("ETW parse receipt coverage/session metadata is not independently derived")
    events = _required_event_shape(receipt.get("process_events"))
    if receipt.get("process_event_digest") != event_digest(events) \
            or provenance.get("decoded_event_digest") != event_digest(events):
        raise ValueError("ETW parse receipt decoded event digest does not reconcile")
    if receipt.get("events_received") != len(events):
        raise ValueError("ETW parse receipt received count does not reconcile")
    if receipt.get("lost_events") != 0 or receipt.get("buffer_loss_events") != 0:
        raise ValueError("ETW parse receipt reports loss")
    if fixture:
        replay_events, replay_counters, _ = _fixture_result(raw_path)
        replay_enriched = replay_events
        replay_counters.update({
            "coverage_start_utc": receipt.get("coverage_start_utc"),
            "coverage_end_utc": receipt.get("coverage_end_utc"),
            "session_name": receipt.get("session_name"),
        })
    else:
        tool = Path(provenance.get("tool", "tracerpt.exe"))
        collection_path = Path(str(provenance.get("collection_metadata_path", "")))
        replay_events, replay_counters, _, _ = _run_tracerpt(
            raw_path, tool, collection_metadata_path=collection_path)
        join_path_value = provenance.get("identity_join_path")
        join_hash = provenance.get("identity_join_sha256")
        if (provenance.get("identity_join_format") != "VOX_FX_T18_ETW_IDENTITY_JOIN_V1"
                or not isinstance(join_path_value, str) or not isinstance(join_hash, str)
                or not SHA256_RE.fullmatch(join_hash)):
            raise ValueError("production ETW parse receipt lacks its identity join provenance")
        join_path = Path(join_path_value)
        if not join_path.is_file() or sha256(join_path).casefold() != join_hash.casefold():
            raise ValueError("ETW identity join artifact hash does not reconcile")
        replay_enriched = _apply_identity_join(replay_events, _read_identity_join(join_path))
    expected_counters = {
        "lost_events": receipt.get("lost_events"),
        "buffer_loss_events": receipt.get("buffer_loss_events"),
        "events_received": receipt.get("events_received"),
        "coverage_start_utc": receipt.get("coverage_start_utc"),
        "coverage_end_utc": receipt.get("coverage_end_utc"),
        "session_name": receipt.get("session_name"),
    }
    replay_projection = {field: replay_counters.get(field) for field in expected_counters}
    replay_receipts = replay_counters.get("collection_control_receipts", [])
    if (core_event_digest(replay_events) != str(provenance.get("decoded_core_event_digest"))
            or event_digest(replay_enriched) != event_digest(events)
            or replay_projection != expected_counters
            or (not fixture and replay_receipts != control_receipts)):
        raise ValueError("independent raw ETL replay does not match the parse receipt")

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--etl", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--coverage-start", help="fixture-only declared metadata; production derives this from tracerpt -summary")
    parser.add_argument("--coverage-end", help="fixture-only declared metadata; production derives this from tracerpt -summary")
    parser.add_argument("--session-name", help="fixture-only declared metadata; production derives this from tracerpt -summary")
    parser.add_argument("--tracerpt", type=Path)
    parser.add_argument("--summary-output", type=Path,
                        help="retained tracerpt -summary artifact; production defaults beside --output")
    parser.add_argument("--collection-metadata", type=Path,
                        help="owner-retained logman start/stop metadata required for production")
    parser.add_argument("--identity-join", type=Path,
                        help="owner-retained ETW identity join JSON required for production parsing")
    parser.add_argument("--fixture-raw", action="store_true")
    parser.add_argument("--verify-receipt", type=Path)
    args = parser.parse_args()
    try:
        if args.verify_receipt:
            verify_receipt(args.etl, read_json(args.verify_receipt), fixture=args.fixture_raw)
            print("FX-T18 raw ETL replay: PASS")
        else:
            if args.output is None:
                parser.error("--output is required when creating a parse receipt")
            summary_path = args.summary_output
            if not args.fixture_raw and summary_path is None:
                summary_path = args.output.with_name(args.output.stem + ".tracerpt-summary.txt")
            receipt = make_receipt(args.etl, args.coverage_start, args.coverage_end,
                                   args.session_name, fixture=args.fixture_raw,
                                   tracerpt_path=args.tracerpt,
                                   identity_join_path=args.identity_join,
                                   summary_path=summary_path,
                                   collection_metadata_path=args.collection_metadata)
            args.output.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            print(f"FX-T18 raw ETL parse receipt: {'FIXTURE_BLOCKED' if args.fixture_raw else 'PASS'}")
    except (OSError, ValueError, ET.ParseError) as error:
        parser.exit(2, f"FX-T18 raw ETL parser: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
