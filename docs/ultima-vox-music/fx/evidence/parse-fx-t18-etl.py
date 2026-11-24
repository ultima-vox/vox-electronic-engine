#!/usr/bin/env python3
"""Decode and verify the raw FX-T18 process ETL outside the observer.

The production path is deliberately a thin, reproducible wrapper around the
pinned Microsoft Windows SDK ``tracerpt.exe`` tool.  Capture never creates a
parse receipt and an owner-signed JSON claim is not a parser.  The wrapper
records the raw ETL hash, the exact tracerpt path/version/command, the
retained ``tracerpt -summary`` artifact that supplies loss counters and
trace metadata, plus a retained collection start/stop metadata artifact when
tracerpt omits the session or stop boundary, and digests of both the raw ETW
core and the enriched identity-joined event stream. Production parsing requires
a retained
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
COLLECTION_METADATA_SOURCE = "owner-retained logman start/stop metadata"
COVERAGE_SOURCE = "tracerpt summary or retained collection start/stop metadata"
SESSION_SOURCE = "tracerpt summary or retained collection session metadata"
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


def _read_collection_metadata(path: Path, raw_path: Path,
                              summary_metadata: dict[str, Any]) -> dict[str, Any]:
    value = read_json(path)
    if value.get("format") != COLLECTION_METADATA_FORMAT:
        raise ValueError("ETW collection metadata format is not supported")
    if value.get("provider") != PROVIDER:
        raise ValueError("ETW collection metadata provider is not the kernel process provider")
    session_value = _collection_value(value, "session_name", "logger_name")
    start_value = _collection_value(value, "coverage_start_utc", "collection_start_utc", "start_utc")
    end_value = _collection_value(value, "coverage_end_utc", "collection_end_utc", "stop_utc")
    if not isinstance(session_value, str) or not session_value or session_value.startswith("<"):
        raise ValueError("ETW collection metadata session name is absent")
    if start_value is None or end_value is None:
        raise ValueError("ETW collection metadata start/stop interval is absent")
    start = _timestamp_text(start_value, "collection start timestamp")
    end = _timestamp_text(end_value, "collection stop timestamp")
    if utc(end) <= utc(start):
        raise ValueError("ETW collection metadata interval is not positive")
    declared_etl = _collection_value(value, "etl_path", "collector_artifact_path")
    if declared_etl is not None:
        declared_text = str(declared_etl).replace("\\", "/").casefold()
        if declared_text.rsplit("/", 1)[-1] != raw_path.name.casefold():
            raise ValueError("ETW collection metadata does not bind the retained ETL filename")
    for field, expected in (("coverage_start_utc", start), ("coverage_end_utc", end),
                            ("session_name", session_value)):
        summary_value = summary_metadata.get(field)
        if summary_value is not None and summary_value != expected:
            raise ValueError(f"ETW collection metadata {field} does not reconcile to tracerpt summary")
    return {
        "coverage_start_utc": start,
        "coverage_end_utc": end,
        "session_name": session_value,
        "collection_metadata_source": COLLECTION_METADATA_SOURCE,
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
        "parser_name": "vox-fx-t18-raw-etl-verifier", "parser_version": "1.1.0",
        "toolchain_id": TOOLCHAIN_ID, "tool": tool_name, "tool_version": tool_version,
        "command_line": json.dumps(command_argv, ensure_ascii=True),
        "command_argv": command_argv, "command_template": TRACERPT_COMMAND,
        "event_source": EVENT_SOURCE, "counter_source": LOSS_SOURCE,
        "summary_source": counters["summary_source"],
        "collection_metadata_source": counters["collection_metadata_source"],
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
        "collection_metadata_source", "coverage_source", "session_name_source", "derived_coverage_start_utc",
        "derived_coverage_end_utc", "derived_session_name", "raw_etl_sha256",
        "decoded_core_event_digest", "decoded_event_digest", "provenance", "replay",
    )
    if any(not isinstance(provenance.get(field), str) or not provenance[field] for field in required):
        raise ValueError("ETW parse receipt parser provenance is incomplete")
    if provenance.get("parser_name") != "vox-fx-t18-raw-etl-verifier" \
            or provenance.get("parser_version") != "1.1.0" \
            or provenance.get("toolchain_id") != TOOLCHAIN_ID \
            or provenance.get("event_source") != EVENT_SOURCE \
            or provenance.get("counter_source") != LOSS_SOURCE \
            or provenance.get("summary_source") != ("fixture-json-declared-counters" if fixture else SUMMARY_SOURCE) \
            or provenance.get("collection_metadata_source") != ("fixture-declared-metadata" if fixture else COLLECTION_METADATA_SOURCE) \
            or provenance.get("coverage_source") != ("fixture-declared-metadata" if fixture else COVERAGE_SOURCE) \
            or provenance.get("session_name_source") != ("fixture-declared-metadata" if fixture else SESSION_SOURCE) \
            or provenance.get("command_template") != TRACERPT_COMMAND \
            or not isinstance(provenance.get("command_argv"), list) \
            or any(not isinstance(item, str) or not item for item in provenance["command_argv"]):
        raise ValueError("ETW parse receipt is not from the pinned independent parser")
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
    if (core_event_digest(replay_events) != str(provenance.get("decoded_core_event_digest"))
            or event_digest(replay_enriched) != event_digest(events)
            or replay_projection != expected_counters):
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
