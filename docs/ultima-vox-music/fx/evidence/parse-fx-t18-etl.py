#!/usr/bin/env python3
"""Decode and verify the raw FX-T18 process ETL outside the observer.

The production path is deliberately a thin, reproducible wrapper around the
pinned Microsoft Windows SDK ``tracerpt.exe`` tool.  Capture never creates a
parse receipt and an owner-signed JSON claim is not a parser.  The wrapper
records the raw ETL hash, the exact tracerpt path/version/command, the event
and loss-counter sources, and digests of both the raw ETW core and the enriched
identity-joined event stream. Production parsing requires a retained
`VOX_FX_T18_ETW_IDENTITY_JOIN_V1` artifact because historical image hashes and
verified service ancestry are not guaranteed to be fields in the raw event.
A replay reruns tracerpt against the retained bytes, reapplies that join and
compares the complete decoded result before the evaluator accepts the receipt.

The ``--fixture-raw`` path is only for offline conformance tests.  It uses a
JSON representation of the tracerpt event/counter result, is marked
qualification-prohibited, and exercises the same raw-byte hash and replay
checks.  It is not a substitute for a Windows ETL.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from typing import Any

FORMAT = "VOX_FX_T18_ETW_PARSE_RECEIPT_V1"
PROVIDER = "Microsoft-Windows-Kernel-Process/ETW"
PROCESS_EVENT_IDS = {1: "start", 2: "stop"}
TOOLCHAIN_ID = "microsoft-windows-sdk-tracerpt-10.0.26100.1"
PINNED_TRACERPT_VERSION = "10.0.26100.1"
TRACERPT_COMMAND = "tracerpt.exe <raw-etl> -o <decoded-xml> -of XML -lr -y"
EVENT_SOURCE = "Microsoft-Windows-Kernel-Process event IDs 1(ProcessStart) and 2(ProcessStop)"
LOSS_SOURCE = "tracerpt -lr summary: Events Lost and Buffers Lost"
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
    parsed = utc(value)
    return parsed.isoformat(timespec="microseconds").replace("+00:00", "Z")


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
                or not event["service_identity_sha256"]
                or any(not SHA256_RE.fullmatch(str(item)) for item in event["service_identity_sha256"])
                or event["service_ancestry_verified"] is not True
                or event["lineage_verification"] not in {"runner_service_ancestry_v1", "signed_job_lineage_v1"}
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
            return utc_text(node.attrib["SystemTime"])
    value = _data_value(event, {"Timestamp", "TimeCreated", "EventTime"})
    if value is None:
        raise ValueError("tracerpt event has no provider timestamp")
    return utc_text(value)


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


def _parse_xml(decoded: Path, summary: str) -> tuple[list[dict[str, Any]], dict[str, int]]:
    root = ET.parse(decoded).getroot()
    events: list[dict[str, Any]] = []
    for event in root.iter():
        if event.tag.rsplit("}", 1)[-1].casefold() != "event":
            continue
        provider = None
        event_id = None
        for node in event.iter():
            tag = node.tag.rsplit("}", 1)[-1].casefold()
            if tag == "provider":
                provider = node.attrib.get("Name")
            elif tag == "eventid":
                event_id = (node.text or "").strip()
        if provider not in {"Microsoft-Windows-Kernel-Process", "Microsoft-Windows-Kernel-Process/ETW"}:
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
        parent_pid = _parse_int(_data_value(event, {"ParentId", "ParentProcessId", "ParentPID"}), "ParentId")
        process_name = _data_value(event, {"ImageName", "ProcessName", "ImageFileName"})
        start_time = _data_value(event, {"CreateTime", "CreationTime", "ProcessCreateTime"})
        if not process_name or not start_time:
            raise ValueError("tracerpt process event lacks ImageName or CreateTime")
        events.append({
            "event": kind, "timestamp_utc": _event_time(event), "pid": pid,
            "process_name": process_name, "parent_pid": parent_pid,
            "start_time_utc": utc_text(start_time),
        })
    events = _required_event_shape(events)
    lost_match = None
    buffer_match = None
    for line in summary.splitlines():
        lowered = line.casefold()
        event_match = re.search(r"events?\s+lost\s*[:=]\s*(\d+)", lowered)
        if event_match:
            lost_match = event_match
        event_buffer_match = re.search(r"buffers?\s+lost\s*[:=]\s*(\d+)", lowered)
        if event_buffer_match:
            buffer_match = event_buffer_match
    if lost_match is None or buffer_match is None:
        raise ValueError("tracerpt Events Lost/Buffers Lost counter summary is absent")
    return events, {
        "lost_events": int(lost_match.group(1)),
        "buffer_loss_events": int(buffer_match.group(1)),
        "events_received": len(events),
    }


def _run_tracerpt(raw_path: Path, tool_path: Path) -> tuple[list[dict[str, Any]], dict[str, int], str]:
    if not tool_path.is_file():
        raise ValueError(f"pinned tracerpt executable is missing: {tool_path}")
    version = _tool_version(tool_path)
    if version != PINNED_TRACERPT_VERSION:
        raise ValueError(f"tracerpt version {version!r} is not the pinned {PINNED_TRACERPT_VERSION!r}")
    with tempfile.TemporaryDirectory(prefix="fx-t18-tracerpt-") as raw_tmp:
        output = Path(raw_tmp) / "decoded.xml"
        command = [str(tool_path), str(raw_path), "-o", str(output), "-of", "XML", "-lr", "-y"]
        completed = subprocess.run(command, check=False, capture_output=True, text=True, timeout=180)
        if completed.returncode != 0 or not output.is_file():
            raise ValueError("pinned tracerpt failed to decode the raw ETL")
        events, counters = _parse_xml(output, completed.stdout + "\n" + completed.stderr)
    return events, counters, version


def make_receipt(raw_path: Path, coverage_start: str, coverage_end: str,
                 session_name: str, fixture: bool = False,
                 tracerpt_path: Path | None = None,
                 identity_join_path: Path | None = None) -> dict[str, Any]:
    if not raw_path.is_file():
        raise ValueError(f"raw ETL is missing: {raw_path}")
    join_path: Path | None = None
    if fixture:
        raw_events, counters, tool_version = _fixture_result(raw_path)
        events = raw_events
        tool_name = "fixture-json-decoder"
        command_argv = ["parse-fx-t18-etl.py", "--fixture-raw", str(raw_path.resolve()),
                        "--output", "<receipt.json>"]
    else:
        tool = tracerpt_path or Path(os.environ.get("FX_T18_TRACERPT", "tracerpt.exe"))
        raw_events, counters, tool_version = _run_tracerpt(raw_path, tool)
        if identity_join_path is None:
            raise ValueError("production ETW parsing requires a retained identity join artifact")
        join_path = identity_join_path.resolve()
        if not join_path.is_file():
            raise ValueError(f"ETW identity join artifact is missing: {join_path}")
        events = _apply_identity_join(raw_events, _read_identity_join(join_path))
        tool_name = str(tool)
        command_argv = [str(tool), str(raw_path.resolve()), "-o", "<decoded-xml>",
                        "-of", "XML", "-lr", "-y", "--identity-join", str(join_path)]
    start = utc_text(coverage_start)
    end = utc_text(coverage_end)
    if utc(end) <= utc(start):
        raise ValueError("ETL coverage interval is not positive")
    raw_hash = sha256(raw_path)
    full_digest = event_digest(events)
    provenance: dict[str, Any] = {
        "parser_name": "vox-fx-t18-raw-etl-verifier", "parser_version": "1.0.0",
        "toolchain_id": TOOLCHAIN_ID, "tool": tool_name, "tool_version": tool_version,
        "command_line": json.dumps(command_argv, ensure_ascii=True),
        "command_argv": command_argv, "command_template": TRACERPT_COMMAND,
        "event_source": EVENT_SOURCE, "counter_source": LOSS_SOURCE,
        "raw_etl_sha256": raw_hash,
        "decoded_core_event_digest": core_event_digest(raw_events),
        "decoded_event_digest": full_digest,
        "provenance": "owner-independent replay of retained raw ETL bytes",
        "replay": "python parse-fx-t18-etl.py --verify-receipt --etl <raw-etl> --receipt <parse.json>",
        "fixture_mode": fixture,
    }
    if join_path is not None:
        provenance.update({
            "identity_join_format": "VOX_FX_T18_ETW_IDENTITY_JOIN_V1",
            "identity_join_path": str(join_path),
            "identity_join_sha256": sha256(join_path),
        })
    return {
        "format": FORMAT, "provider": PROVIDER, "session_name": session_name,
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
    required = ("parser_name", "parser_version", "toolchain_id", "tool_version", "command_line",
                "command_template", "event_source", "counter_source", "raw_etl_sha256", "decoded_core_event_digest",
                "decoded_event_digest", "provenance", "replay")
    if any(not isinstance(provenance.get(field), str) or not provenance[field] for field in required):
        raise ValueError("ETW parse receipt parser provenance is incomplete")
    if provenance.get("parser_name") != "vox-fx-t18-raw-etl-verifier" \
            or provenance.get("parser_version") != "1.0.0" \
            or provenance.get("toolchain_id") != TOOLCHAIN_ID \
            or provenance.get("event_source") != EVENT_SOURCE \
            or provenance.get("counter_source") != LOSS_SOURCE \
            or provenance.get("command_template") != TRACERPT_COMMAND \
            or not isinstance(provenance.get("command_argv"), list) \
            or any(not isinstance(item, str) or not item for item in provenance["command_argv"]):
        raise ValueError("ETW parse receipt is not from the pinned independent parser")
    try:
        if json.loads(provenance["command_line"]) != provenance["command_argv"]:
            raise ValueError("command line and argument vector differ")
    except (TypeError, json.JSONDecodeError) as error:
        raise ValueError("ETW parse receipt command line is not a canonical argument vector") from error
    actual_hash = sha256(raw_path)
    if actual_hash.casefold() != str(receipt.get("collector_artifact_sha256", "")).casefold() \
            or actual_hash.casefold() != str(provenance.get("raw_etl_sha256", "")).casefold():
        raise ValueError("ETW parse receipt raw ETL hash does not reconcile")
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
    else:
        tool = Path(provenance.get("tool", "tracerpt.exe"))
        replay_events, replay_counters, _ = _run_tracerpt(raw_path, tool)
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
    }
    if (core_event_digest(replay_events) != str(provenance.get("decoded_core_event_digest"))
            or event_digest(replay_enriched) != event_digest(events)
            or replay_counters != expected_counters):
        raise ValueError("independent raw ETL replay does not match the parse receipt")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--etl", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--coverage-start", default="2026-01-01T00:00:00Z")
    parser.add_argument("--coverage-end", default="2026-01-01T00:01:00Z")
    parser.add_argument("--session-name", default="FX-T18-KernelProcess")
    parser.add_argument("--tracerpt", type=Path)
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
            receipt = make_receipt(args.etl, args.coverage_start, args.coverage_end,
                                   args.session_name, fixture=args.fixture_raw,
                                   tracerpt_path=args.tracerpt,
                                   identity_join_path=args.identity_join)
            args.output.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            print(f"FX-T18 raw ETL parse receipt: {'FIXTURE_BLOCKED' if args.fixture_raw else 'PASS'}")
    except (OSError, ValueError, ET.ParseError) as error:
        parser.exit(2, f"FX-T18 raw ETL parser: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
