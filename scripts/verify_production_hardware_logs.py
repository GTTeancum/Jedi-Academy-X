#!/usr/bin/env python3
"""Verify production retail-Xbox SP/Holomatch logs for a staged hardware run."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import re
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable

import verify_holomatch_split_log as hm_split
from verify_hardware_stage import DEFAULT_CFG, verify_stage


HEARTBEAT_MARKER = "FRAME_HEARTBEAT"
PROFILE_MARKERS = ("STEFX_HW_FRAME_PROFILE:", "STEFX_HW_RENDER_SAMPLE:")
PAIR_RE = re.compile(r"([A-Za-z][A-Za-z0-9]*)=([^\s]+)")
FPS_RE = re.compile(r"^(\d+)\.(\d+)$")
QUOTED_MAP_RE = re.compile(r"\b(?:map|name)='([^']+)'")
FATAL_RE = re.compile(
    r"ERR_FATAL|FATAL|Unhandled exception|KeBugCheck|Out of memory|"
    r"Z_Malloc[^\r\n]*(?:failed|failure)|allocation failed|"
    r"texture allocation failure|D3DERR_OUTOFVIDEOMEMORY|OOM",
    re.IGNORECASE,
)

HIGH_FREQUENCY_MP_PREFIXES = (
    "STEFX_HM_BOTCMD:",
    "STEFX_HM_BOTLIB:",
    "STEFX_HM_SOUND_BRIDGE:",
    "STEFX_HM_EVENT_SERVER:",
    "STEFX_HM_EVENT_ROUTE:",
    "STEFX_HM_AMMO:",
    "STEFX_WEAPON:",
    "STEFX_VIS:",
)

MODE_FILES = {
    "sp": "ef_sp_log.txt",
    "mp": "ef_mp_log.txt",
}
EXPECTED_RETURNED_RUNTIME_MARKERS = frozenset(MODE_FILES.values())
OBSERVATION_FILE = "HARDWARE_OBSERVATION.json"
REPORT_TYPE = "stefx-production-hardware-log-verification"
REPORT_SCHEMA_VERSION = 19
OBSERVATION_SCHEMA_VERSION = 3
OBSERVATION_MODES = ("sp", "coop", "mp")
DEFAULT_MIN_EVIDENCE_BYTES = 1024
DEFAULT_MIN_IMAGE_WIDTH = 320
DEFAULT_MIN_IMAGE_HEIGHT = 240
IMAGE_VISUAL_TYPES = frozenset({"png", "jpeg", "bmp", "gif", "webp", "tiff"})
JPEG_SOF_MARKERS = frozenset(
    {
        0xC0,
        0xC1,
        0xC2,
        0xC3,
        0xC5,
        0xC6,
        0xC7,
        0xC9,
        0xCA,
        0xCB,
        0xCD,
        0xCE,
        0xCF,
    }
)
VISUAL_EVIDENCE_EXTENSIONS = frozenset(
    {
        ".png",
        ".jpg",
        ".jpeg",
        ".bmp",
        ".gif",
        ".webp",
        ".tif",
        ".tiff",
        ".mp4",
        ".mov",
        ".avi",
        ".mkv",
        ".webm",
    }
)
COMMON_OBSERVATION_FIELDS = (
    "mapsTested",
    "evidenceFiles",
    "durationSeconds",
    "visibleFpsMin",
    "visibleFpsMax",
    "memoryFreeMinimum",
    "memoryLargestFreeMinimum",
    "memoryUsedDelta",
    "loadingOk",
    "hudOk",
    "worldLightingOk",
    "controlsOk",
    "gameplayOk",
    "noUnrecoveredStall",
    "notes",
)
COMMON_OBSERVATION_BOOLEANS = (
    "loadingOk",
    "hudOk",
    "worldLightingOk",
    "controlsOk",
    "gameplayOk",
    "noUnrecoveredStall",
)
MODE_OBSERVATION_BOOLEANS = {
    "sp": COMMON_OBSERVATION_BOOLEANS,
    "coop": COMMON_OBSERVATION_BOOLEANS + ("splitScreenOk", "p2HudOk", "p2ControlsOk"),
    "mp": COMMON_OBSERVATION_BOOLEANS + ("botsOrCombatOk",),
}
MODE_OBSERVATION_FIELDS = {
    "sp": COMMON_OBSERVATION_FIELDS,
    "coop": COMMON_OBSERVATION_FIELDS + ("splitScreenOk", "p2HudOk", "p2ControlsOk"),
    "mp": COMMON_OBSERVATION_FIELDS + ("botsOrCombatOk",),
}


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def file_modified_utc(path: Path) -> str:
    modified = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
    return modified.isoformat().replace("+00:00", "Z")


def normalize_map_name(value: object) -> str:
    if not isinstance(value, str):
        return ""
    text = value.strip().replace("\\", "/")
    if text.lower().startswith("maps/"):
        text = text[5:]
    if text.lower().endswith(".bsp"):
        text = text[:-4]
    return text.strip().lower()


def unique_ordered(values: Iterable[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        if value and value not in seen:
            seen.add(value)
            result.append(value)
    return result


def extract_map_names(lines: Iterable[str], mode: str) -> dict[str, object]:
    raw_maps: list[str] = []
    for line in lines:
        if (
            "SV_SpawnServer" not in line
            and "CM_LoadMap" not in line
            and "direct Holomatch map is running" not in line
        ):
            continue
        for match in QUOTED_MAP_RE.finditer(line):
            raw_maps.append(match.group(1))
    normalized = unique_ordered(normalize_map_name(map_name) for map_name in raw_maps)
    return {
        "raw": unique_ordered(raw_maps),
        "normalized": normalized,
        "primary": normalized[-1] if normalized else None,
    }


def parse_scalar(value: str) -> int | float | str | list[int]:
    value = value.rstrip(",;")
    fps_match = FPS_RE.match(value)
    if fps_match:
        fraction = fps_match.group(2)
        return int(fps_match.group(1)) + int(fraction) / (10 ** len(fraction))
    if "/" in value:
        fields = value.split("/")
        if all(field.lstrip("-").isdigit() for field in fields):
            return [int(field) for field in fields]
    try:
        return int(value, 0)
    except ValueError:
        return value


def parse_heartbeats(lines: Iterable[str]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for line_number, line in enumerate(lines, 1):
        if HEARTBEAT_MARKER not in line:
            continue
        record: dict[str, object] = {"line": line_number, "raw": line.rstrip("\r\n")}
        payload = line.split(HEARTBEAT_MARKER, 1)[1]
        for key, value in PAIR_RE.findall(payload):
            record[key] = parse_scalar(value)
        records.append(record)
    return records


def numeric(records: list[dict[str, object]], key: str) -> list[float]:
    values: list[float] = []
    for record in records:
        value = record.get(key)
        if isinstance(value, (int, float)):
            values.append(float(value))
    return values


def series_summary(values: list[float]) -> dict[str, float | int]:
    if not values:
        return {"samples": 0}
    return {
        "samples": len(values),
        "min": min(values),
        "mean": statistics.fmean(values),
        "median": statistics.median(values),
        "max": max(values),
    }


def frame_progress(records: list[dict[str, object]]) -> dict[str, object]:
    frames = [int(value) for value in numeric(records, "completedFrame")]
    realtime = [int(value) for value in numeric(records, "realtime")]
    result: dict[str, object] = {"samples": len(records)}
    if frames:
        result.update(
            {
                "firstFrame": frames[0],
                "lastFrame": frames[-1],
                "frameDelta": frames[-1] - frames[0],
                "framesAdvanced": frames[-1] > frames[0],
                "nonAdvancingIntervals": sum(
                    1 for previous, current in zip(frames, frames[1:]) if current <= previous
                ),
            }
        )
    if realtime:
        result.update(
            {
                "firstRealtime": realtime[0],
                "lastRealtime": realtime[-1],
                "elapsedSeconds": max(0.0, (realtime[-1] - realtime[0]) / 1000.0),
                "realtimeAdvanced": realtime[-1] > realtime[0],
            }
        )
    return result


def memory_summary(records: list[dict[str, object]]) -> dict[str, object]:
    samples = [record["mem"] for record in records if isinstance(record.get("mem"), list)]
    samples = [sample for sample in samples if len(sample) >= 3]
    if not samples:
        return {"samples": 0}
    used = [int(sample[0]) for sample in samples]
    free = [int(sample[1]) for sample in samples]
    largest = [int(sample[2]) for sample in samples]
    return {
        "samples": len(samples),
        "usedFirst": used[0],
        "usedLast": used[-1],
        "usedDelta": used[-1] - used[0],
        "usedPeak": max(used),
        "freeMinimum": min(free),
        "largestFreeMinimum": min(largest),
    }


def renderer_path_summary(records: list[dict[str, object]]) -> dict[str, object]:
    paths = [int(value) for value in numeric(records, "path")]
    if not paths:
        return {"samples": 0}
    return {
        "samples": len(paths),
        "values": sorted(set(paths)),
        "allRetailPushPath": all(value == 1 for value in paths),
    }


def heartbeat_field_summary(records: list[dict[str, object]], fields: Iterable[str]) -> dict[str, object]:
    summary: dict[str, object] = {"samples": len(records), "fields": {}}
    field_summary: dict[str, object] = {}
    for field in fields:
        missing_lines = [
            int(record["line"])
            for record in records
            if field not in record and isinstance(record.get("line"), int)
        ]
        field_summary[field] = {
            "present": len(records) - len(missing_lines),
            "missing": len(missing_lines),
            "missingLines": missing_lines[:8],
        }
    summary["fields"] = field_summary
    return summary


def find_matching_lines(lines: list[str], pattern: re.Pattern[str], limit: int = 8) -> list[str]:
    matches: list[str] = []
    for line in lines:
        if pattern.search(line):
            matches.append(line.rstrip("\r\n"))
            if len(matches) >= limit:
                break
    return matches


def count_prefixes(lines: list[str], prefixes: tuple[str, ...]) -> dict[str, int]:
    counts = {prefix: 0 for prefix in prefixes}
    for line in lines:
        for prefix in prefixes:
            if line.startswith(prefix):
                counts[prefix] += 1
    return {prefix: count for prefix, count in counts.items() if count}


def load_manifest(stage_dir: Path) -> dict[str, object] | None:
    for name in ("HARDWARE_PATCH_MANIFEST.json", "HARDWARE_STAGE_MANIFEST.json"):
        path = stage_dir / name
        if path.is_file():
            return json.loads(path.read_text(encoding="utf-8-sig"))
    return None


def manifest_file_path(stage_dir: Path) -> Path | None:
    for name in ("HARDWARE_PATCH_MANIFEST.json", "HARDWARE_STAGE_MANIFEST.json"):
        path = stage_dir / name
        if path.is_file():
            return path
    return None


def manifest_file_record(stage_dir: Path) -> dict[str, object]:
    path = manifest_file_path(stage_dir)
    if path is None:
        return {"present": False, "path": str(stage_dir / "HARDWARE_PATCH_MANIFEST.json")}
    return {
        "present": True,
        "path": str(path),
        "bytes": path.stat().st_size,
        "modifiedUtc": file_modified_utc(path),
        "sha256": file_sha256(path),
    }


def manifest_runtime_build_ids(manifest: dict[str, object] | None) -> dict[str, str]:
    if not isinstance(manifest, dict):
        return {}
    files = manifest.get("files")
    if not isinstance(files, dict):
        return {}
    mapping = {
        "sp": "default.xbe",
        "mp": "efmp.xbe",
    }
    result: dict[str, str] = {}
    for mode, rel in mapping.items():
        record = files.get(rel)
        if not isinstance(record, dict):
            continue
        runtime_build_id = record.get("runtimeBuildId")
        if isinstance(runtime_build_id, str) and runtime_build_id:
            result[mode] = runtime_build_id
    return result


def expected_runtime_build_id_fragments(rel: str, frame_diagnostics: object) -> list[str]:
    flavor = "frame-diagnostics" if frame_diagnostics is True else "production"
    basename = Path(rel).name.lower()
    if basename == "default.xbe":
        return ["personality=default", f"flavor={flavor}", "log=ef_sp_log.txt"]
    if basename == "efmp.xbe":
        return ["personality=efmp", f"flavor={flavor}", "log=ef_mp_log.txt"]
    return [f"flavor={flavor}"]


def manifest_runtime_build_id_failures(
    manifest: dict[str, object] | None,
    expected_runtime_build_ids: dict[str, str],
) -> list[str]:
    failures: list[str] = []
    if not isinstance(manifest, dict):
        return ["stage manifest is missing; cannot bind logs to runtime XBE build IDs"]
    if not isinstance(manifest.get("files"), dict):
        return ["stage manifest files object is missing; cannot bind logs to runtime XBE build IDs"]
    required = {
        "sp": "default.xbe",
        "mp": "efmp.xbe",
    }
    for mode, rel in required.items():
        if mode not in expected_runtime_build_ids:
            failures.append(f"{rel}: manifest runtimeBuildId is missing; cannot bind {mode} log")
            continue
        runtime_build_id = expected_runtime_build_ids[mode]
        for fragment in expected_runtime_build_id_fragments(rel, manifest.get("frameDiagnostics")):
            if fragment not in runtime_build_id:
                failures.append(
                    f"{rel}: manifest runtimeBuildId has wrong identity; missing {fragment!r}"
                )
    return failures


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


def resolve_optional_path(base: Path, path: Path | None) -> Path | None:
    if path is None:
        return None
    return path if path.is_absolute() else base / path


def resolve_evidence_path(base: Path, value: object) -> Path | None:
    if not isinstance(value, str) or not value.strip():
        return None
    path = Path(value)
    return path if path.is_absolute() else base / path


def jpeg_dimensions(path: Path) -> tuple[int, int] | None:
    with path.open("rb") as handle:
        if handle.read(2) != b"\xff\xd8":
            return None

        while True:
            marker_prefix = handle.read(1)
            while marker_prefix and marker_prefix != b"\xff":
                marker_prefix = handle.read(1)
            if not marker_prefix:
                return None

            marker_data = handle.read(1)
            while marker_data == b"\xff":
                marker_data = handle.read(1)
            if not marker_data:
                return None

            marker = marker_data[0]
            if marker == 0xD9 or marker == 0xDA:
                return None
            if marker == 0x01 or 0xD0 <= marker <= 0xD7:
                continue

            length_data = handle.read(2)
            if len(length_data) != 2:
                return None
            segment_length = int.from_bytes(length_data, "big")
            if segment_length < 2:
                return None

            payload_length = segment_length - 2
            if marker in JPEG_SOF_MARKERS:
                segment = handle.read(min(payload_length, 5))
                if len(segment) != 5:
                    return None
                height = int.from_bytes(segment[1:3], "big")
                width = int.from_bytes(segment[3:5], "big")
                if width <= 0 or height <= 0:
                    return None
                return (width, height)

            handle.seek(payload_length, 1)


def visual_evidence_metadata(path: Path) -> dict[str, object] | None:
    suffix = path.suffix.lower()
    with path.open("rb") as handle:
        header = handle.read(64)
    if suffix == ".png" and header.startswith(b"\x89PNG\r\n\x1a\n"):
        if len(header) < 24:
            return None
        return {
            "visualType": "png",
            "width": int.from_bytes(header[16:20], "big"),
            "height": int.from_bytes(header[20:24], "big"),
        }
    if suffix in {".jpg", ".jpeg"} and header.startswith(b"\xff\xd8"):
        dimensions = jpeg_dimensions(path)
        if dimensions is None:
            return {"visualType": "jpeg"}
        return {
            "visualType": "jpeg",
            "width": dimensions[0],
            "height": dimensions[1],
        }
    if suffix == ".bmp" and header.startswith(b"BM"):
        if len(header) < 26:
            return None
        return {
            "visualType": "bmp",
            "width": abs(int.from_bytes(header[18:22], "little", signed=True)),
            "height": abs(int.from_bytes(header[22:26], "little", signed=True)),
        }
    if suffix == ".gif" and (header.startswith(b"GIF87a") or header.startswith(b"GIF89a")):
        if len(header) < 10:
            return None
        return {
            "visualType": "gif",
            "width": int.from_bytes(header[6:8], "little"),
            "height": int.from_bytes(header[8:10], "little"),
        }
    if suffix == ".webp" and header.startswith(b"RIFF") and header[8:12] == b"WEBP":
        return {"visualType": "webp"}
    if suffix in {".tif", ".tiff"} and (
        header.startswith(b"II*\x00") or header.startswith(b"MM\x00*")
    ):
        return {"visualType": "tiff"}
    if suffix in {".mp4", ".mov"} and len(header) >= 12 and header[4:8] == b"ftyp":
        return {"visualType": "isobmff"}
    if suffix == ".avi" and header.startswith(b"RIFF") and header[8:12] == b"AVI ":
        return {"visualType": "avi"}
    if suffix in {".mkv", ".webm"} and header.startswith(b"\x1a\x45\xdf\xa3"):
        return {"visualType": "ebml"}
    return None


def verifier_provenance() -> dict[str, object]:
    script_path = Path(__file__).resolve()
    return {
        "script": str(script_path),
        "scriptSha256": file_sha256(script_path),
        "python": sys.executable,
        "cwd": str(Path.cwd()),
        "argv": sys.argv[1:],
    }


def write_report_file(path: Path | None, output: dict[str, object]) -> str | None:
    if path is None:
        return None
    report_path = path.resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_output = dict(output)
    report_output.setdefault("reportType", REPORT_TYPE)
    report_output.setdefault("reportSchemaVersion", REPORT_SCHEMA_VERSION)
    report_output.setdefault("reportPath", str(report_path))
    report_output.setdefault("verifierProvenance", verifier_provenance())
    report_output.setdefault(
        "generatedAtUtc",
        datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
    )
    report_path.write_text(json.dumps(report_output, indent=2) + "\n", encoding="utf-8")
    return str(report_path)


def verify_stage_integrity(
    repo_root: Path,
    stage: Path,
    direct_map: str,
    default_cfg: Path | None,
    run_package_check: bool,
) -> dict[str, object]:
    try:
        return verify_stage(
            repo_root=repo_root,
            stage=stage,
            direct_map=direct_map,
            default_cfg=default_cfg,
            run_package_check=run_package_check,
            allow_runtime_markers=True,
        )
    except Exception as exc:  # pragma: no cover - defensive summary path
        return {
            "stage": str(stage),
            "status": "fail",
            "warnings": [],
            "failures": [f"stage integrity check failed: {exc}"],
        }


def collect_stage_integrity(
    repo_root: Path,
    stage: Path,
    direct_map: str,
    default_cfg: Path | None,
    run_package_check: bool,
    skip_stage_integrity: bool,
) -> tuple[dict[str, object], list[str]]:
    if skip_stage_integrity:
        stage_integrity: dict[str, object] = {"checked": False, "status": "skipped"}
    else:
        stage_integrity = verify_stage_integrity(
            repo_root=repo_root,
            stage=stage,
            direct_map=direct_map,
            default_cfg=default_cfg,
            run_package_check=run_package_check,
        )
    return stage_integrity, returned_runtime_marker_failures(stage_integrity)


def returned_runtime_marker_failures(stage_integrity: dict[str, object]) -> list[str]:
    markers = stage_integrity.get("runtimeMarkers", [])
    if not isinstance(markers, list):
        return []
    unexpected = sorted(
        marker for marker in markers if marker not in EXPECTED_RETURNED_RUNTIME_MARKERS
    )
    if not unexpected:
        return []
    return ["unexpected runtime marker file(s) in returned stage: " + ", ".join(unexpected)]


def observation_template(manifest_version: object = None) -> dict[str, object]:
    return {
        "manifestVersion": manifest_version or "",
        "observationSchemaVersion": OBSERVATION_SCHEMA_VERSION,
        "notes": "Fill this after the retail Xbox run. Use numbers, true/false, and concise notes.",
        "sp": {
            "mapsTested": [],
            "evidenceFiles": [],
            "durationSeconds": 0,
            "visibleFpsMin": 0,
            "visibleFpsMax": 0,
            "memoryFreeMinimum": 0,
            "memoryLargestFreeMinimum": 0,
            "memoryUsedDelta": 0,
            "loadingOk": False,
            "hudOk": False,
            "worldLightingOk": False,
            "controlsOk": False,
            "gameplayOk": False,
            "noUnrecoveredStall": False,
            "notes": "",
        },
        "coop": {
            "mapsTested": [],
            "evidenceFiles": [],
            "durationSeconds": 0,
            "visibleFpsMin": 0,
            "visibleFpsMax": 0,
            "memoryFreeMinimum": 0,
            "memoryLargestFreeMinimum": 0,
            "memoryUsedDelta": 0,
            "loadingOk": False,
            "hudOk": False,
            "worldLightingOk": False,
            "controlsOk": False,
            "gameplayOk": False,
            "splitScreenOk": False,
            "p2HudOk": False,
            "p2ControlsOk": False,
            "noUnrecoveredStall": False,
            "notes": "",
        },
        "mp": {
            "mapsTested": [],
            "evidenceFiles": [],
            "durationSeconds": 0,
            "visibleFpsMin": 0,
            "visibleFpsMax": 0,
            "memoryFreeMinimum": 0,
            "memoryLargestFreeMinimum": 0,
            "memoryUsedDelta": 0,
            "loadingOk": False,
            "hudOk": False,
            "worldLightingOk": False,
            "controlsOk": False,
            "gameplayOk": False,
            "botsOrCombatOk": False,
            "noUnrecoveredStall": False,
            "notes": "",
        },
    }


def write_observation_template(path: Path, manifest_version: object, force: bool) -> None:
    if path.exists() and not force:
        raise FileExistsError(f"observation template already exists: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(observation_template(manifest_version), indent=2) + "\n", encoding="utf-8")


def positive_number(value: object) -> bool:
    return isinstance(value, (int, float)) and value > 0


def verify_observation(
    path: Path,
    require: bool,
    min_duration_seconds: float,
    expected_manifest_version: object = None,
    expected_log_maps: dict[str, list[str]] | None = None,
    required_maps: dict[str, list[str]] | None = None,
    min_visible_fps: dict[str, float] | None = None,
    min_observed_largest_free: dict[str, int] | None = None,
    max_observed_used_delta: dict[str, int] | None = None,
    require_evidence_files: bool = False,
    min_evidence_bytes: int = DEFAULT_MIN_EVIDENCE_BYTES,
    min_image_width: int = DEFAULT_MIN_IMAGE_WIDTH,
    min_image_height: int = DEFAULT_MIN_IMAGE_HEIGHT,
) -> dict[str, object]:
    if not path.is_file():
        return {
            "checked": False,
            "path": str(path),
            "status": "missing" if require else "not_checked",
            "failures": [f"required observation file missing: {path}"] if require else [],
        }

    data = json.loads(path.read_text(encoding="utf-8-sig"))
    file_bytes = path.stat().st_size
    file_hash = file_sha256(path)
    failures: list[str] = []
    summary: dict[str, object] = {}
    evidence_files: dict[str, list[dict[str, object]]] = {}
    observed_manifest_version = data.get("manifestVersion")
    observed_schema_version = data.get("observationSchemaVersion")
    if observed_schema_version != OBSERVATION_SCHEMA_VERSION:
        failures.append(
            "observationSchemaVersion "
            f"{observed_schema_version!r} does not match required {OBSERVATION_SCHEMA_VERSION}"
        )
    if expected_manifest_version and observed_manifest_version != expected_manifest_version:
        failures.append(
            "manifestVersion mismatch: "
            f"observation={observed_manifest_version!r} stage={expected_manifest_version!r}"
        )
    for mode in OBSERVATION_MODES:
        record = data.get(mode)
        if not isinstance(record, dict):
            failures.append(f"{mode}: observation record missing")
            continue
        for field in MODE_OBSERVATION_FIELDS[mode]:
            if field not in record:
                failures.append(f"{mode}: missing observation field {field}")
        mode_summary = {
            "mapsTested": record.get("mapsTested"),
            "evidenceFiles": record.get("evidenceFiles"),
            "durationSeconds": record.get("durationSeconds"),
            "visibleFpsMin": record.get("visibleFpsMin"),
            "visibleFpsMax": record.get("visibleFpsMax"),
            "memoryFreeMinimum": record.get("memoryFreeMinimum"),
            "memoryLargestFreeMinimum": record.get("memoryLargestFreeMinimum"),
            "memoryUsedDelta": record.get("memoryUsedDelta"),
        }
        summary[mode] = mode_summary

        evidence_values = record.get("evidenceFiles", [])
        if isinstance(evidence_values, str):
            evidence_values = [evidence_values]
        if not isinstance(evidence_values, list):
            failures.append(f"{mode}: evidenceFiles must be a list")
            evidence_values = []
        if require_evidence_files and not evidence_values:
            failures.append(f"{mode}: evidenceFiles must include at least one file")
        mode_evidence: list[dict[str, object]] = []
        for value in evidence_values:
            evidence_path = resolve_evidence_path(path.parent, value)
            if evidence_path is None:
                failures.append(f"{mode}: evidenceFiles contains an invalid path")
                continue
            record_path = str(evidence_path)
            if not evidence_path.is_file():
                failures.append(f"{mode}: evidence file missing: {record_path}")
                mode_evidence.append({"path": record_path, "status": "missing"})
                continue
            evidence_bytes = evidence_path.stat().st_size
            evidence_status = "present"
            if evidence_path.suffix.lower() not in VISUAL_EVIDENCE_EXTENSIONS:
                failures.append(
                    f"{mode}: evidence file extension is not visual: {record_path}"
                )
                evidence_status = "unsupported-extension"
            if evidence_bytes <= 0:
                failures.append(f"{mode}: evidence file is empty: {record_path}")
                evidence_status = "empty"
            visual_metadata: dict[str, object] | None = None
            if evidence_status == "present":
                visual_metadata = visual_evidence_metadata(evidence_path)
                if visual_metadata is None:
                    failures.append(
                        f"{mode}: evidence file signature is not recognized as visual: {record_path}"
                    )
                    evidence_status = "bad-signature"
                elif evidence_bytes < min_evidence_bytes:
                    failures.append(
                        f"{mode}: evidence file is smaller than {min_evidence_bytes} bytes: "
                        f"{record_path}"
                    )
                    evidence_status = "too-small"
                elif visual_metadata.get("visualType") in IMAGE_VISUAL_TYPES:
                    width = visual_metadata.get("width")
                    height = visual_metadata.get("height")
                    if not isinstance(width, int) or not isinstance(height, int):
                        failures.append(
                            f"{mode}: evidence image dimensions are unavailable: {record_path}"
                        )
                        evidence_status = "unknown-dimensions"
                    elif width < min_image_width or height < min_image_height:
                        failures.append(
                            f"{mode}: evidence image dimensions {width}x{height} are below "
                            f"required {min_image_width}x{min_image_height}: {record_path}"
                        )
                        evidence_status = "too-small-dimensions"
            visual_type = None
            visual_width = None
            visual_height = None
            if isinstance(visual_metadata, dict):
                visual_type = visual_metadata.get("visualType")
                visual_width = visual_metadata.get("width")
                visual_height = visual_metadata.get("height")
            mode_evidence.append(
                {
                    "path": record_path,
                    "status": evidence_status,
                    "bytes": evidence_bytes,
                    "modifiedUtc": file_modified_utc(evidence_path),
                    "sha256": file_sha256(evidence_path),
                    "visualType": visual_type,
                    "width": visual_width,
                    "height": visual_height,
                }
            )
        evidence_files[mode] = mode_evidence

        maps_tested = record.get("mapsTested")
        if isinstance(maps_tested, str):
            maps_tested = [maps_tested]
        if not isinstance(maps_tested, list):
            failures.append(f"{mode}: mapsTested must be a non-empty list")
            normalized_maps: list[str] = []
        else:
            normalized_maps = unique_ordered(normalize_map_name(map_name) for map_name in maps_tested)
            if not normalized_maps:
                failures.append(f"{mode}: mapsTested must include at least one map")
        if expected_log_maps and normalized_maps:
            logged_maps = expected_log_maps.get(mode, [])
            if logged_maps and not set(normalized_maps).intersection(logged_maps):
                failures.append(
                    f"{mode}: mapsTested {normalized_maps} does not include logged map(s) {logged_maps}"
                )
        if required_maps:
            mode_required_maps = required_maps.get(mode, [])
            missing_maps = sorted(set(mode_required_maps) - set(normalized_maps))
            if missing_maps:
                failures.append(
                    f"{mode}: mapsTested is missing required map(s) {missing_maps}"
                )

        duration = record.get("durationSeconds")
        if not positive_number(duration) or float(duration) < min_duration_seconds:
            failures.append(
                f"{mode}: durationSeconds must be at least {min_duration_seconds:.0f}"
            )
        fps_min = record.get("visibleFpsMin")
        fps_max = record.get("visibleFpsMax")
        if not positive_number(fps_min):
            failures.append(f"{mode}: visibleFpsMin must be positive")
        if not positive_number(fps_max):
            failures.append(f"{mode}: visibleFpsMax must be positive")
        if positive_number(fps_min) and positive_number(fps_max) and float(fps_max) < float(fps_min):
            failures.append(f"{mode}: visibleFpsMax is below visibleFpsMin")
        if min_visible_fps and positive_number(fps_min):
            mode_min_fps = min_visible_fps.get(mode)
            if mode_min_fps is not None and float(fps_min) < mode_min_fps:
                failures.append(
                    f"{mode}: visibleFpsMin {float(fps_min):.1f} is below required {mode_min_fps:.1f}"
                )
        memory_largest_free = record.get("memoryLargestFreeMinimum")
        memory_used_delta = record.get("memoryUsedDelta")
        if min_observed_largest_free:
            mode_min_largest = min_observed_largest_free.get(mode)
            if mode_min_largest is not None and (
                not isinstance(memory_largest_free, (int, float))
                or float(memory_largest_free) < mode_min_largest
            ):
                failures.append(
                    f"{mode}: memoryLargestFreeMinimum {memory_largest_free} "
                    f"is below required {mode_min_largest}"
                )
        if max_observed_used_delta:
            mode_max_delta = max_observed_used_delta.get(mode)
            if mode_max_delta is not None and (
                not isinstance(memory_used_delta, (int, float))
                or float(memory_used_delta) > mode_max_delta
            ):
                failures.append(
                    f"{mode}: memoryUsedDelta {memory_used_delta} "
                    f"is above allowed {mode_max_delta}"
                )

        for field in MODE_OBSERVATION_BOOLEANS[mode]:
            if record.get(field) is not True:
                failures.append(f"{mode}: {field} must be true")

    return {
        "checked": True,
        "path": str(path),
        "bytes": file_bytes,
        "modifiedUtc": file_modified_utc(path),
        "sha256": file_hash,
        "status": "pass" if not failures else "fail",
        "manifestVersion": observed_manifest_version,
        "observationSchemaVersion": observed_schema_version,
        "summary": summary,
        "evidenceFiles": evidence_files,
        "failures": failures,
    }


def analyze_log(
    path: Path,
    mode: str,
    min_elapsed_seconds: float,
    expected_runtime_build_id: str | None = None,
) -> dict[str, object]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    lines = text.splitlines()
    heartbeats = parse_heartbeats(lines)
    maps = extract_map_names(lines, mode)
    fps = numeric(heartbeats, "fps")
    progress = frame_progress(heartbeats)
    memory = memory_summary(heartbeats)
    renderer_path = renderer_path_summary(heartbeats)
    heartbeat_fields = heartbeat_field_summary(
        heartbeats,
        ("completedFrame", "realtime", "path", "mem"),
    )
    fatal_hits = find_matching_lines(lines, FATAL_RE)
    profile_hits = [marker for marker in PROFILE_MARKERS if marker in text]
    high_frequency = count_prefixes(lines, HIGH_FREQUENCY_MP_PREFIXES) if mode == "mp" else {}
    runtime_build_id = {
        "expected": expected_runtime_build_id,
        "present": bool(expected_runtime_build_id and expected_runtime_build_id in text),
    }

    required_markers = []
    if mode == "sp":
        required_markers = [
            ("SP active gameplay", "JA: cls.state = CA_ACTIVE - GAME IS RUNNING" in text),
            ("SP first render complete", "STEFX_HW_CHECKPOINT: first frame render complete" in text),
        ]
    else:
        required_markers = [
            ("Holomatch runtime log", "Elite Force Holomatch Xbox log started" in text),
            ("Holomatch map running", "direct Holomatch map is running" in text),
            ("Holomatch local client active", "direct Holomatch local client is active" in text),
        ]

    failures: list[str] = []
    warnings: list[str] = []
    if fatal_hits:
        failures.append("fatal/OOM marker present")
    if profile_hits:
        failures.append("frame-profile marker present in production log")
    if len(heartbeats) < 2:
        failures.append(f"fewer than 2 FRAME_HEARTBEAT records ({len(heartbeats)})")
    if not maps.get("normalized"):
        failures.append("map marker unavailable")
    field_details = heartbeat_fields.get("fields")
    if isinstance(field_details, dict):
        for field, detail in field_details.items():
            if isinstance(detail, dict) and detail.get("missing"):
                failures.append(
                    f"FRAME_HEARTBEAT field {field} missing from "
                    f"{detail.get('missing')} record(s)"
                )
    if progress.get("framesAdvanced") is not True:
        failures.append("completedFrame did not advance")
    if progress.get("realtimeAdvanced") is not True:
        failures.append("realtime did not advance")
    elapsed = progress.get("elapsedSeconds")
    if not isinstance(elapsed, (int, float)):
        failures.append("heartbeat elapsed time unavailable")
    elif elapsed < min_elapsed_seconds:
        failures.append(
            f"heartbeat elapsed time {elapsed:.1f}s is below required {min_elapsed_seconds:.1f}s"
        )
    if high_frequency:
        failures.append("high-frequency Holomatch diagnostics present in hotlog-off build")
    if expected_runtime_build_id and runtime_build_id["present"] is not True:
        failures.append("runtime build id marker missing or does not match staged manifest")
    missing_required = [name for name, present in required_markers if not present]
    if missing_required:
        failures.append("missing mode marker(s): " + ", ".join(missing_required))
    if memory.get("largestFreeMinimum") == 0:
        failures.append("largest free memory reached zero")
    if memory.get("samples") != len(heartbeats):
        failures.append("memory sample missing from FRAME_HEARTBEAT record(s)")
    if renderer_path.get("samples") == 0:
        failures.append("renderer path unavailable in heartbeat records")
    elif renderer_path.get("samples") != len(heartbeats):
        failures.append("renderer path missing from FRAME_HEARTBEAT record(s)")
    elif renderer_path.get("allRetailPushPath") is not True:
        failures.append(f"non-retail renderer path value(s): {renderer_path.get('values')}")

    return {
        "mode": mode,
        "log": str(path),
        "bytes": path.stat().st_size,
        "modifiedUtc": file_modified_utc(path),
        "sha256": file_sha256(path),
        "heartbeatSamples": len(heartbeats),
        "progress": progress,
        "maps": maps,
        "fps": series_summary(fps),
        "heartbeatFields": heartbeat_fields,
        "memory": memory,
        "rendererPath": renderer_path,
        "fatalHits": fatal_hits,
        "profileMarkers": profile_hits,
        "highFrequencyDiagnostics": high_frequency,
        "runtimeBuildId": runtime_build_id,
        "requiredMarkers": {name: present for name, present in required_markers},
        "warnings": warnings,
        "failures": failures,
        "status": "pass" if not failures else "fail",
    }


def required_map_failures(
    results: dict[str, dict[str, object]],
    required_maps: dict[str, list[str]],
) -> list[str]:
    failures: list[str] = []
    for mode, maps in required_maps.items():
        if not maps:
            continue
        if mode not in results:
            continue
        result = results.get(mode, {})
        map_summary = result.get("maps")
        logged_maps = []
        if isinstance(map_summary, dict):
            logged_maps = map_summary.get("normalized", [])
        if not isinstance(logged_maps, list):
            logged_maps = []
        missing_maps = sorted(set(maps) - set(logged_maps))
        if missing_maps:
            failures.append(
                f"{mode}: returned log is missing required map marker(s) {missing_maps}"
            )
    return failures


def memory_threshold_failures(
    results: dict[str, dict[str, object]],
    min_free: dict[str, int],
    min_largest_free: dict[str, int],
    max_used_delta: dict[str, int],
) -> list[str]:
    failures: list[str] = []
    for mode, result in results.items():
        memory = result.get("memory")
        if not isinstance(memory, dict) or not memory.get("samples"):
            if min_free.get(mode) is not None or min_largest_free.get(mode) is not None:
                failures.append(f"{mode}: memory samples unavailable for threshold check")
            continue
        free_minimum = memory.get("freeMinimum")
        largest_free_minimum = memory.get("largestFreeMinimum")
        used_delta = memory.get("usedDelta")
        if min_free.get(mode) is not None and (
            not isinstance(free_minimum, (int, float)) or free_minimum < min_free[mode]
        ):
            failures.append(
                f"{mode}: freeMinimum {free_minimum} is below required {min_free[mode]}"
            )
        if min_largest_free.get(mode) is not None and (
            not isinstance(largest_free_minimum, (int, float))
            or largest_free_minimum < min_largest_free[mode]
        ):
            failures.append(
                f"{mode}: largestFreeMinimum {largest_free_minimum} "
                f"is below required {min_largest_free[mode]}"
            )
        if max_used_delta.get(mode) is not None and (
            not isinstance(used_delta, (int, float)) or used_delta > max_used_delta[mode]
        ):
            failures.append(
                f"{mode}: usedDelta {used_delta} is above allowed {max_used_delta[mode]}"
            )
    return failures


def verify_holomatch_split_log(path: Path, args: argparse.Namespace) -> dict[str, object]:
    require_audio_backend = getattr(args, "hm_split_require_audio_backend", False)
    require_audio_listeners = getattr(args, "hm_split_require_audio_listeners", False)
    min_audio_starts = getattr(args, "hm_split_min_audio_starts", None)
    min_audio_voice_starts = getattr(args, "hm_split_min_audio_voice_starts", None)
    require_audio_lip_active = getattr(args, "hm_split_require_audio_lip_active", False)
    criteria = {
        "players": 4,
        "minBots": args.hm_split_min_bots,
        "minElapsedSeconds": args.hm_split_min_elapsed_seconds,
        "minHeartbeatFps": args.hm_split_min_heartbeat_fps,
        "minLargestFree": args.hm_split_min_largest_free,
        "maxUsedDelta": args.hm_split_max_used_delta,
        "requireAttack": True,
        "requireUniqueControls": True,
        "requireControlMovement": True,
        "minControlMovementDistance": 8.0,
        "requireLocalNonBot": True,
        "requirePositiveSnapshotAdds": True,
        "requireP1VirtualControls": True,
        "requireLaunch": True,
        "requireFpFilterSlots": [1, 2, 3],
        "requireHudSlots": True,
        "requireHudQuadrants": True,
        "requireHudStatusSlots": True,
        "requireHudStatusQuadrants": True,
        "requireHudDividers": True,
        "requireExternalClientMap": True,
        "minUniqueOrigins": 4,
        "minUniqueRefdefOrigins": 3,
        "minUniqueRenderViews": 4,
        "requireQuadrantViewports": True,
        "minLocalP1Distance": 32.0,
        "requireAudioBackend": require_audio_backend,
        "requireAudioListeners": require_audio_listeners,
        "minAudioStarts": min_audio_starts,
        "minAudioVoiceStarts": min_audio_voice_starts,
        "requireAudioLipActive": require_audio_lip_active,
    }
    if not path.is_file():
        return {
            "status": "missing",
            "path": str(path),
            "criteria": criteria,
            "failures": [f"Holomatch split log missing: {path}"],
        }

    proof = hm_split.parse_lines(path.read_text(errors="replace").splitlines())
    split_args = argparse.Namespace(
        players=4,
        min_bots=args.hm_split_min_bots,
        min_client_state=4,
        min_hud_remaps=1,
        require_hud_slots=True,
        require_hud_quadrants=True,
        require_hud_status_slots=True,
        require_hud_status_quadrants=True,
        require_hud_dividers=True,
        min_unique_origins=4,
        min_unique_refdef_origins=3,
        min_unique_render_views=4,
        require_quadrant_viewports=True,
        require_external_client_map=True,
        origin_tolerance=8.0,
        min_local_p1_distance=32.0,
        require_attack=True,
        require_unique_controls=True,
        require_control_movement=True,
        min_control_movement_distance=8.0,
        require_local_non_bot=True,
        require_positive_snapshot_adds=True,
        require_p1_virtual_controls=True,
        require_launch=True,
        min_heartbeat_samples=2,
        min_elapsed_seconds=args.hm_split_min_elapsed_seconds,
        min_heartbeat_fps=args.hm_split_min_heartbeat_fps,
        min_free=None,
        min_largest_free=args.hm_split_min_largest_free,
        max_used_delta=args.hm_split_max_used_delta,
        require_frame_progress=True,
        require_retail_path=True,
        require_audio_backend=require_audio_backend,
        require_audio_listeners=require_audio_listeners,
        min_audio_compiled_listeners=4,
        min_audio_active_listeners=4,
        required_audio_listener_mask=0x0E,
        min_audio_starts=min_audio_starts,
        min_audio_voice_starts=min_audio_voice_starts,
        require_audio_lip_active=require_audio_lip_active,
        require_fp_filter_slot=[1, 2, 3],
        require_self_filter_slot=[],
    )
    failures = hm_split.verify(proof, split_args)
    fps = hm_split.numeric(proof.heartbeats, "fps")
    elapsed = hm_split.heartbeat_elapsed_seconds(proof.heartbeats)
    frame_delta = hm_split.heartbeat_frame_delta(proof.heartbeats)
    memory = hm_split.heartbeat_memory_summary(proof.heartbeats)
    return {
        "status": "pass" if not failures else "fail",
        "path": str(path),
        "criteria": criteria,
        "failures": failures,
        "summary": {
            "armedPlayers": proof.max_armed_players,
            "launchRecords": proof.launch_records,
            "stateSlots": sorted(proof.states),
            "stateP1Distances": {str(slot): state.p1_dist for slot, state in sorted(proof.states.items())},
            "stateLocalFlags": {str(slot): state.local for slot, state in sorted(proof.states.items())},
            "stateBotFlags": {str(slot): state.bot for slot, state in sorted(proof.states.items())},
            "stateMovement": {str(slot): distance for slot, distance in sorted(hm_split.slot_movement_distances(proof.state_history).items())},
            "maxBots": proof.max_bots,
            "cmdClients": sorted(proof.cmd_clients),
            "attackClients": sorted(proof.cmd_attack_clients),
            "cmdMoveProfiles": {str(client): sorted(values) for client, values in sorted(proof.cmd_moves.items())},
            "cmdAngleProfiles": {str(client): sorted(values) for client, values in sorted(proof.cmd_angles.items())},
            "refdefSlots": sorted(proof.refdef_slots),
            "refdefOrigins": {str(slot): origin for slot, origin in sorted(proof.refdef_origins.items())},
            "snapshotSlots": sorted(proof.snapshot_slots),
            "positiveSnapshotSlots": sorted(proof.snapshot_positive_slots),
            "renderSlots": sorted(proof.render_slots),
            "renderExternalSlots": sorted(proof.render_external_slots),
            "renderExternalClients": proof.render_external_clients,
            "renderRects": {str(slot): rect for slot, rect in sorted(proof.render_rects.items())},
            "renderViews": {str(slot): view for slot, view in sorted(proof.render_views.items())},
            "renderDoneSlots": sorted(proof.render_done_slots),
            "renderDoneExternalSlots": sorted(proof.render_done_external_slots),
            "renderDoneExternalClients": proof.render_done_external_clients,
            "renderDonePositiveSlots": sorted(proof.render_done_positive_slots),
            "renderDoneViews": {str(slot): view for slot, view in sorted(proof.render_done_views.items())},
            "firstPersonFilterSlots": sorted(proof.fp_filter_slots),
            "selfModelFilterSlots": sorted(proof.self_filter_slots),
            "selfModelFilterRefNumbers": {
                str(slot): sorted(values) for slot, values in sorted(proof.self_filter_ref_numbers.items())
            },
            "selfModelFilterParts": {
                str(slot): sorted(values) for slot, values in sorted(proof.self_filter_model_parts.items())
            },
            "hudSlots": sorted(proof.hud_slots),
            "hudRects": {str(slot): rect for slot, rect in sorted(proof.hud_rects.items())},
            "hudStatusSlots": sorted(proof.hud_status_slots),
            "hudStatusValidSlots": sorted(proof.hud_status_valid_slots),
            "hudStatusRects": {str(slot): rect for slot, rect in sorted(proof.hud_status_rects.items())},
            "hudStatusValues": {str(slot): values for slot, values in sorted(proof.hud_status_values.items())},
            "hudDividers": proof.hud_divider_records,
            "hudRemaps": proof.hud_remaps,
            "heartbeatSamples": len(proof.heartbeats),
            "heartbeatElapsedSeconds": elapsed,
            "heartbeatFrameDelta": frame_delta,
            "heartbeatFpsMin": min(fps) if fps else None,
            "heartbeatFpsMax": max(fps) if fps else None,
            "memory": memory,
            "audio": hm_split.audio_summary(proof),
        },
    }


def print_mode_summary(result: dict[str, object]) -> None:
    print(f"{result['mode'].upper()} log: {result['log']}")
    print(f"  status: {result['status']}")
    print(f"  bytes: {result['bytes']} sha256={result.get('sha256')}")
    print(f"  heartbeats: {result['heartbeatSamples']}")
    progress = result["progress"]
    if isinstance(progress, dict):
        elapsed = progress.get("elapsedSeconds")
        if isinstance(elapsed, (int, float)):
            print(
                "  progress: "
                f"frameDelta={progress.get('frameDelta', 'n/a')} "
                f"elapsed={elapsed:.1f}s"
            )
        else:
            print("  progress: n/a")
    fps = result["fps"]
    maps = result.get("maps")
    if isinstance(maps, dict) and maps.get("normalized"):
        print("  maps: " + ", ".join(str(map_name) for map_name in maps["normalized"]))
    if isinstance(fps, dict) and fps.get("samples"):
        print(
            "  heartbeat FPS: "
            f"min={float(fps['min']):.1f} "
            f"median={float(fps['median']):.1f} "
            f"max={float(fps['max']):.1f}"
        )
    else:
        print("  heartbeat FPS: unavailable")
    memory = result["memory"]
    if isinstance(memory, dict) and memory.get("samples"):
        print(
            "  memory: "
            f"usedDelta={memory['usedDelta']} "
            f"usedPeak={memory['usedPeak']} "
            f"largestFreeMin={memory['largestFreeMinimum']}"
        )
    renderer_path = result["rendererPath"]
    if isinstance(renderer_path, dict) and renderer_path.get("samples"):
        print(f"  renderer path values: {renderer_path['values']}")
    for warning in result["warnings"]:
        print(f"  warning: {warning}")
    for failure in result["failures"]:
        print(f"  failure: {failure}")
    high_frequency = result.get("highFrequencyDiagnostics")
    if isinstance(high_frequency, dict) and high_frequency:
        details = ", ".join(f"{key}={value}" for key, value in sorted(high_frequency.items()))
        print(f"  high-frequency diagnostics: {details}")


def heartbeat(frame: int, realtime: int, fps: str = "72.5", path: int = 1) -> str:
    return (
        "JA: FRAME_HEARTBEAT "
        f"completedFrame={frame} realtime={realtime} serverTime={frame * 50} "
        f"fps={fps} path={path} mem=1000/2000/1500/0"
    )


def run_self_test() -> int:
    screenshot_png = (
        b"\x89PNG\r\n\x1a\n"
        b"\x00\x00\x00\rIHDR"
        + (640).to_bytes(4, "big")
        + (480).to_bytes(4, "big")
        + b"\x08\x02\x00\x00\x00"
        b"\x90wS\xde"
        b"\x00\x00\x00\x00IEND\xaeB`\x82"
        + (b"\x00" * DEFAULT_MIN_EVIDENCE_BYTES)
    )
    tiny_png = (
        b"\x89PNG\r\n\x1a\n"
        b"\x00\x00\x00\rIHDR"
        + (1).to_bytes(4, "big")
        + (1).to_bytes(4, "big")
        + b"\x08\x02\x00\x00\x00"
        b"\x90wS\xde"
        b"\x00\x00\x00\x00IEND\xaeB`\x82"
        + (b"\x00" * DEFAULT_MIN_EVIDENCE_BYTES)
    )
    screenshot_jpeg = (
        b"\xff\xd8"
        b"\xff\xe0"
        + (16).to_bytes(2, "big")
        + b"JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
        b"\xff\xc0"
        + (17).to_bytes(2, "big")
        + b"\x08"
        + (480).to_bytes(2, "big")
        + (640).to_bytes(2, "big")
        + b"\x03\x01\x11\x00\x02\x11\x00\x03\x11\x00"
        b"\xff\xda"
        + (12).to_bytes(2, "big")
        + b"\x03\x01\x00\x02\x11\x03\x11\x00?\x00"
        + (b"\x00" * DEFAULT_MIN_EVIDENCE_BYTES)
    )
    with tempfile.TemporaryDirectory(prefix="stefx_hwlog_test_") as tmp_name:
        root = Path(tmp_name)
        stage = root / "stage"
        release = root / "build" / "release"
        stage.mkdir()
        runtime_ids = {
            "default.xbe": "STEFX_RUNTIME_BUILD_ID personality=default flavor=production date=Aug 19 2026 time=10:00:00 log=ef_sp_log.txt",
            "efmp.xbe": "STEFX_RUNTIME_BUILD_ID personality=efmp flavor=production date=Aug 19 2026 time=10:01:00 log=ef_mp_log.txt",
        }
        payloads = {
            "default.xbe": (
                bytearray(b"default-self-test-xbe\0" + runtime_ids["default.xbe"].encode("ascii") + b"\0"),
                3,
            ),
            "efmp.xbe": (
                bytearray(b"efmp-self-test-xbe\0" + runtime_ids["efmp.xbe"].encode("ascii") + b"\0"),
                4,
            ),
            "BaseEF/xbox0.pk3": (bytearray(b"xbox0-pk3-self-test"), None),
            "BaseEF/xbox1.pk3": (bytearray(b"xbox1-pk3-self-test"), None),
        }
        files: dict[str, object] = {}
        for rel, (source_data, patch_offset) in payloads.items():
            staged_data = bytearray(source_data)
            if patch_offset is not None:
                source_data[patch_offset] = 0x7D
                staged_data[patch_offset] = 0xEB
            source_path = release / rel.replace("/", "\\")
            staged_path = stage / rel.replace("/", "\\")
            source_path.parent.mkdir(parents=True, exist_ok=True)
            staged_path.parent.mkdir(parents=True, exist_ok=True)
            source_path.write_bytes(source_data)
            staged_path.write_bytes(staged_data)
            record: dict[str, object] = {
                "bytes": len(staged_data),
                "sha256": hashlib.sha256(staged_data).hexdigest().upper(),
                "sourceSha256": hashlib.sha256(source_data).hexdigest().upper(),
            }
            if patch_offset is not None:
                record["mediaEnablePatchOffset"] = patch_offset
                record["runtimeBuildId"] = runtime_ids[rel]
            files[rel] = record
        (stage / "BaseEF" / "soundbank").mkdir(parents=True, exist_ok=True)
        scripts_dir = root / "scripts"
        scripts_dir.mkdir(parents=True, exist_ok=True)
        build_script = scripts_dir / "build_xbox.ps1"
        contract_verifier = scripts_dir / "verify_build_xbox_contracts.py"
        build_script.write_text(
            'Invoke-BuildGraphForTarget -BuildTarget "sp" -Projects $spProjects\n'
            'Invoke-BuildGraphForTarget -BuildTarget "spmp" -Projects $spProjects\n',
            encoding="utf-8",
        )
        contract_verifier.write_text("# self-test verifier\n", encoding="utf-8")
        package_timestamp = datetime(2026, 8, 19, 0, 2, tzinfo=timezone.utc).timestamp()
        script_timestamp = datetime(2026, 8, 19, 0, 1, tzinfo=timezone.utc).timestamp()
        for path in (build_script, contract_verifier):
            path.touch()
            import os
            os.utime(path, (script_timestamp, script_timestamp))
        for rel in ("BaseEF/xbox0.pk3", "BaseEF/xbox1.pk3"):
            package_path = release / rel.replace("/", "\\")
            os.utime(package_path, (package_timestamp, package_timestamp))
        build_contract = {
            "status": "pass",
            "verifier": {
                "path": str(contract_verifier),
                "present": True,
                "bytes": contract_verifier.stat().st_size,
                "sha256": file_sha256(contract_verifier),
            },
            "buildScript": {
                "path": str(build_script),
                "present": True,
                "bytes": build_script.stat().st_size,
                "sha256": file_sha256(build_script),
            },
            "exitCode": 0,
            "stdout": "build_xbox contract verification passed",
            "stderr": "",
        }
        (stage / "HARDWARE_PATCH_MANIFEST.json").write_text(
            "\ufeff"
            + json.dumps(
                {
                    "version": "self-test",
                    "frameDiagnostics": False,
                    "buildScriptContract": build_contract,
                    "files": files,
                }
            ),
            encoding="utf-8",
        )

        sp_pass = stage / "ef_sp_log.txt"
        sp_pass.write_text(
            "\n".join(
                [
                    runtime_ids["default.xbe"],
                    "JA: SV_SpawnServer entered map='borg1' reload=0 dissolve=1",
                    "JA: cls.state = CA_ACTIVE - GAME IS RUNNING",
                    "STEFX_HW_CHECKPOINT: first frame render complete",
                    heartbeat(100, 10000),
                    heartbeat(200, 70000),
                    heartbeat(300, 130000),
                ]
            ),
            encoding="utf-8",
        )
        mp_pass = stage / "ef_mp_log.txt"
        mp_pass.write_text(
            "\n".join(
                [
                    runtime_ids["efmp.xbe"],
                    "JA: SV_SpawnServer entered map='hm_borg1' reload=0 dissolve=1",
                    "=== Elite Force Holomatch Xbox log started ===",
                    "STEFX_HM: direct Holomatch map is running map='hm_borg1'",
                    "STEFX_HM: direct Holomatch local client is active",
                    "STEFX_HM_SPLIT_LAUNCH: source=xbe map='hm_borg1' split=1 players=4 mode='holomatch' localPlayers=4 virtual=1 virtualP1=1",
                    "STEFX_HM_SPLIT_STATE: slot=0 players=4 bots=3 state=4 local=0 bot=0 svFlags=0x0 health=100 weapon=1 area=1 cluster=10 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=0 origin=(0,0,0) view=(0,0,0) time=1000 sample=1 interval=500",
                    "STEFX_HM_SPLIT_STATE: slot=1 players=4 bots=3 state=4 local=1 bot=0 svFlags=0x0 health=100 weapon=1 area=2 cluster=20 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=100 origin=(100,0,0) view=(0,20,0) time=1000 sample=1 interval=500",
                    "STEFX_HM_SPLIT_STATE: slot=2 players=4 bots=3 state=4 local=1 bot=0 svFlags=0x0 health=100 weapon=1 area=3 cluster=30 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=100 origin=(0,100,0) view=(0,40,0) time=1000 sample=1 interval=500",
                    "STEFX_HM_SPLIT_STATE: slot=3 players=4 bots=3 state=4 local=1 bot=0 svFlags=0x0 health=100 weapon=1 area=4 cluster=40 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=100 origin=(0,0,100) view=(0,60,0) time=1000 sample=1 interval=500",
                    "STEFX_HM_SPLIT_CMD: client=0 time=1000 move=(90,-36,0) buttons=0x1 weapon=1 angles=(0,0,0)",
                    "STEFX_HM_SPLIT_CMD: client=1 time=1000 move=(90,44,0) buttons=0x1 weapon=1 angles=(0,1,0)",
                    "STEFX_HM_SPLIT_CMD: client=2 time=1000 move=(90,-54,0) buttons=0x1 weapon=1 angles=(0,2,0)",
                    "STEFX_HM_SPLIT_CMD: client=3 time=1000 move=(90,28,0) buttons=0x1 weapon=1 angles=(0,3,0)",
                    "STEFX_HM_SPLIT_STATE: slot=0 players=4 bots=3 state=4 local=0 bot=0 svFlags=0x0 health=100 weapon=1 area=1 cluster=10 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=0 origin=(24,-10,0) view=(0,12,0) time=2200 sample=2 interval=500",
                    "STEFX_HM_SPLIT_STATE: slot=1 players=4 bots=3 state=4 local=1 bot=0 svFlags=0x0 health=100 weapon=1 area=2 cluster=20 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=118 origin=(122,20,0) view=(0,32,0) time=2200 sample=2 interval=500",
                    "STEFX_HM_SPLIT_STATE: slot=2 players=4 bots=3 state=4 local=1 bot=0 svFlags=0x0 health=100 weapon=1 area=3 cluster=30 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=118 origin=(20,122,0) view=(0,52,0) time=2200 sample=2 interval=500",
                    "STEFX_HM_SPLIT_STATE: slot=3 players=4 bots=3 state=4 local=1 bot=0 svFlags=0x0 health=100 weapon=1 area=4 cluster=40 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=118 origin=(22,20,100) view=(0,72,0) time=2200 sample=2 interval=500",
                    "STEFX_HM_SPLIT_REFDEF: slot=1 client=1 time=1000 origin=(100,0,48) angles=(0,20,0)",
                    "STEFX_HM_SPLIT_REFDEF: slot=2 client=2 time=1000 origin=(0,100,48) angles=(0,40,0)",
                    "STEFX_HM_SPLIT_REFDEF: slot=3 client=3 time=1000 origin=(0,0,148) angles=(0,60,0)",
                    "STEFX_HM_SPLIT_SNAPSHOT: slot=1 entsBefore=20 entsAfter=24 added=4 areaBytes=1 view=(100,0,48) state=4 local=1",
                    "STEFX_HM_SPLIT_SNAPSHOT: slot=2 entsBefore=24 entsAfter=28 added=4 areaBytes=1 view=(0,100,48) state=4 local=1",
                    "STEFX_HM_SPLIT_SNAPSHOT: slot=3 entsBefore=28 entsAfter=32 added=4 areaBytes=1 view=(0,0,148) state=4 local=1",
                    "STEFX_HM_SPLIT_RENDER: armed players=4 source=0,0 640x480 gl=0,0 640x480 fov=90/73",
                    "STEFX_HM_SPLIT_RENDER: slot=0 external=0 externalClient=0 drawBase=0 ref=0,0 320x240 gl=0,240 320x240 fov=90/53 view=(0,0,48) pvs=(0,0,48)",
                    "STEFX_HM_SPLIT_RENDER_DONE: slot=0 external=0 externalClient=0 drawDelta=10 drawAfter=10 cluster=10 cluster2=-1 view=(0,0,48)",
                    "STEFX_HM_SPLIT_RENDER: slot=1 external=1 externalClient=1 drawBase=10 ref=320,0 320x240 gl=320,240 320x240 fov=90/53 view=(100,0,48) pvs=(100,0,48)",
                    "STEFX_HM_SPLIT_RENDER_DONE: slot=1 external=1 externalClient=1 drawDelta=11 drawAfter=21 cluster=20 cluster2=-1 view=(100,0,48)",
                    "STEFX_HM_SPLIT_RENDER: slot=2 external=1 externalClient=2 drawBase=21 ref=0,240 320x240 gl=0,0 320x240 fov=90/53 view=(0,100,48) pvs=(0,100,48)",
                    "STEFX_HM_SPLIT_RENDER_DONE: slot=2 external=1 externalClient=2 drawDelta=12 drawAfter=33 cluster=30 cluster2=-1 view=(0,100,48)",
                    "STEFX_HM_SPLIT_RENDER: slot=3 external=1 externalClient=3 drawBase=33 ref=320,240 320x240 gl=320,0 320x240 fov=90/53 view=(0,0,148) pvs=(0,0,148)",
                    "STEFX_HM_SPLIT_RENDER_DONE: slot=3 external=1 externalClient=3 drawDelta=13 drawAfter=46 cluster=40 cluster2=-1 view=(0,0,148)",
                    "STEFX_HM_SPLIT_FP_FILTER: slot=1 entity=2 renderfx=0x4 hModel=12",
                    "STEFX_HM_SPLIT_FP_FILTER: slot=2 entity=2 renderfx=0x4 hModel=12",
                    "STEFX_HM_SPLIT_FP_FILTER: slot=3 entity=2 renderfx=0x4 hModel=12",
                    "STEFX_HM_SPLIT_SELF_FILTER: slot=1 entity=12 refNumber=1 renderfx=0x0 hModel=40 modelPart=lower origin=(100,0,0) view=(100,0,48) xyDist=0 zDelta=48",
                    "STEFX_HM_SPLIT_SELF_FILTER: slot=2 entity=13 refNumber=2 renderfx=0x0 hModel=41 modelPart=upper origin=(0,100,0) view=(0,100,48) xyDist=0 zDelta=48",
                    "STEFX_HM_SPLIT_SELF_FILTER: slot=3 entity=14 refNumber=3 renderfx=0x0 hModel=42 modelPart=head origin=(0,0,100) view=(0,0,148) xyDist=0 zDelta=48",
                    "STEFX_HM_SPLIT_HUD: slot=0 players=4 shared=1 shader=1 src=(0,0 640x480) dst=(0,0 320x240)",
                    "STEFX_HM_SPLIT_HUD: slot=1 players=4 shared=1 shader=1 src=(0,0 640x480) dst=(320,0 320x240)",
                    "STEFX_HM_SPLIT_HUD: slot=2 players=4 shared=1 shader=1 src=(0,0 640x480) dst=(0,240 320x240)",
                    "STEFX_HM_SPLIT_HUD: slot=3 players=4 shared=1 shader=1 src=(0,0 640x480) dst=(320,240 320x240)",
                    "STEFX_HM_SPLIT_HUD_STATUS: slot=0 players=4 valid=1 health=100 weapon=1 score=0 dst=(4,4 152x22)",
                    "STEFX_HM_SPLIT_HUD_STATUS: slot=1 players=4 valid=1 health=100 weapon=1 score=0 dst=(324,4 152x22)",
                    "STEFX_HM_SPLIT_HUD_STATUS: slot=2 players=4 valid=1 health=100 weapon=1 score=0 dst=(4,244 152x22)",
                    "STEFX_HM_SPLIT_HUD_STATUS: slot=3 players=4 valid=1 health=100 weapon=1 score=0 dst=(324,244 152x22)",
                    "STEFX_HM_SPLIT_HUD_DIVIDER: players=4 vertical=(320,0 0x480) horizontal=(0,240 640x0)",
                    heartbeat(500, 20000, "88.0"),
                    heartbeat(600, 80000, "87.5"),
                    heartbeat(700, 140000, "86.9"),
                ]
            ),
            encoding="utf-8",
        )

        manifest = load_manifest(stage)
        if not manifest or manifest.get("version") != "self-test":
            print("self-test failed: manifest with BOM was not parsed", file=sys.stderr)
            return 1
        manifest_path = stage / "HARDWARE_PATCH_MANIFEST.json"
        manifest_record = manifest_file_record(stage)
        if (
            manifest_record.get("path") != str(manifest_path)
            or manifest_record.get("bytes") != manifest_path.stat().st_size
            or manifest_record.get("modifiedUtc") != file_modified_utc(manifest_path)
            or manifest_record.get("sha256") != file_sha256(manifest_path)
        ):
            print("self-test failed: manifest file provenance was not reported", file=sys.stderr)
            print(json.dumps(manifest_record, indent=2), file=sys.stderr)
            return 1
        expected_ids = manifest_runtime_build_ids(manifest)
        if manifest_runtime_build_id_failures(manifest, expected_ids):
            print("self-test failed: complete manifest runtime build IDs were rejected", file=sys.stderr)
            print(json.dumps(expected_ids, indent=2), file=sys.stderr)
            return 1
        missing_runtime_id_manifest = json.loads(json.dumps(manifest))
        missing_runtime_id_manifest["files"]["efmp.xbe"].pop("runtimeBuildId", None)
        missing_id_failures = manifest_runtime_build_id_failures(
            missing_runtime_id_manifest,
            manifest_runtime_build_ids(missing_runtime_id_manifest),
        )
        if (
            not missing_id_failures
            or not any("efmp.xbe" in failure for failure in missing_id_failures)
        ):
            print("self-test failed: missing efmp.xbe runtimeBuildId was not rejected", file=sys.stderr)
            print(json.dumps(missing_id_failures, indent=2), file=sys.stderr)
            return 1
        wrong_runtime_id_manifest = json.loads(json.dumps(manifest))
        wrong_runtime_id_manifest["files"]["default.xbe"]["runtimeBuildId"] = runtime_ids["efmp.xbe"]
        wrong_id_failures = manifest_runtime_build_id_failures(
            wrong_runtime_id_manifest,
            manifest_runtime_build_ids(wrong_runtime_id_manifest),
        )
        if (
            not wrong_id_failures
            or not any("default.xbe" in failure and "wrong identity" in failure for failure in wrong_id_failures)
        ):
            print("self-test failed: wrong default.xbe runtimeBuildId identity was not rejected", file=sys.stderr)
            print(json.dumps(wrong_id_failures, indent=2), file=sys.stderr)
            return 1

        sp_result = analyze_log(sp_pass, "sp", 60.0)
        mp_result = analyze_log(mp_pass, "mp", 60.0)
        if sp_result["status"] != "pass" or mp_result["status"] != "pass":
            print("self-test failed: clean production logs did not pass", file=sys.stderr)
            print(json.dumps({"sp": sp_result, "mp": mp_result}, indent=2), file=sys.stderr)
            return 1
        split_result = verify_holomatch_split_log(
            mp_pass,
            argparse.Namespace(
                hm_split_min_bots=3,
                hm_split_min_elapsed_seconds=90.0,
                hm_split_min_heartbeat_fps=15.0,
                hm_split_min_largest_free=1000,
                hm_split_max_used_delta=0,
            ),
        )
        if split_result["status"] != "pass":
            print("self-test failed: clean Holomatch split log did not pass", file=sys.stderr)
            print(json.dumps(split_result, indent=2), file=sys.stderr)
            return 1
        if (
            sp_result.get("sha256") != file_sha256(sp_pass)
            or sp_result.get("modifiedUtc") != file_modified_utc(sp_pass)
            or mp_result.get("sha256") != file_sha256(mp_pass)
            or mp_result.get("modifiedUtc") != file_modified_utc(mp_pass)
        ):
            print("self-test failed: log file provenance was not reported", file=sys.stderr)
            print(json.dumps({"sp": sp_result, "mp": mp_result}, indent=2), file=sys.stderr)
            return 1
        runtime_id_result = analyze_log(
            sp_pass,
            "sp",
            60.0,
            runtime_ids["default.xbe"],
        )
        wrong_runtime_id_result = analyze_log(
            sp_pass,
            "sp",
            60.0,
            runtime_ids["efmp.xbe"],
        )
        if (
            runtime_id_result["status"] != "pass"
            or runtime_id_result.get("runtimeBuildId", {}).get("present") is not True
        ):
            print("self-test failed: matching runtime build id did not pass", file=sys.stderr)
            print(json.dumps(runtime_id_result, indent=2), file=sys.stderr)
            return 1
        if (
            wrong_runtime_id_result["status"] != "fail"
            or not any("runtime build id marker" in failure for failure in wrong_runtime_id_result["failures"])
        ):
            print("self-test failed: stale/mismatched runtime build id did not fail", file=sys.stderr)
            print(json.dumps(wrong_runtime_id_result, indent=2), file=sys.stderr)
            return 1

        stage_result = verify_stage_integrity(
            repo_root=root,
            stage=stage,
            direct_map="hm_borg1",
            default_cfg=None,
            run_package_check=False,
        )
        if stage_result["status"] != "pass" or sorted(stage_result.get("runtimeMarkers", [])) != [
            "ef_mp_log.txt",
            "ef_sp_log.txt",
        ]:
            print("self-test failed: returned-log stage integrity did not pass", file=sys.stderr)
            print(json.dumps(stage_result, indent=2), file=sys.stderr)
            return 1

        (stage / "ef_runtime_commands.txt").write_text("stale", encoding="utf-8")
        unexpected_marker_result = verify_stage_integrity(
            repo_root=root,
            stage=stage,
            direct_map="hm_borg1",
            default_cfg=None,
            run_package_check=False,
        )
        marker_failures = returned_runtime_marker_failures(unexpected_marker_result)
        if not marker_failures or "ef_runtime_commands.txt" not in marker_failures[0]:
            print("self-test failed: unexpected returned runtime marker was not rejected", file=sys.stderr)
            print(json.dumps(unexpected_marker_result, indent=2), file=sys.stderr)
            return 1
        (stage / "ef_runtime_commands.txt").unlink()

        mp_fail = root / "ef_mp_fail.txt"
        mp_fail.write_text(
            "\n".join(
                [
                    "JA: SV_SpawnServer entered map='hm_borg1' reload=0 dissolve=1",
                    "=== Elite Force Holomatch Xbox log started ===",
                    "STEFX_HM: direct Holomatch map is running map='hm_borg1'",
                    "STEFX_HM: direct Holomatch local client is active",
                    heartbeat(10, 1000),
                    "STEFX_HM_BOTCMD: client=1 time=1000 move=(1,0,0)",
                    "STEFX_HW_FRAME_PROFILE: frame=10 total=200",
                    "ERR_FATAL: synthetic failure",
                    heartbeat(20, 65000),
                ]
            ),
            encoding="utf-8",
        )
        fail_result = analyze_log(mp_fail, "mp", 10.0)
        expected_failures = {
            "fatal/OOM marker present",
            "frame-profile marker present in production log",
            "high-frequency Holomatch diagnostics present in hotlog-off build",
        }
        if fail_result["status"] != "fail" or not expected_failures.issubset(
            set(fail_result["failures"])
        ):
            print("self-test failed: noisy diagnostic log did not fail correctly", file=sys.stderr)
            print(json.dumps(fail_result, indent=2), file=sys.stderr)
            return 1

        mp_missing_marker = root / "ef_mp_missing_marker.txt"
        mp_missing_marker.write_text(
            "\n".join(
                [
                    "=== Elite Force Holomatch Xbox log started ===",
                    heartbeat(10, 1000),
                    heartbeat(20, 65000),
                ]
            ),
            encoding="utf-8",
        )
        missing_marker_result = analyze_log(mp_missing_marker, "mp", 10.0)
        if (
            missing_marker_result["status"] != "fail"
            or not any(
                failure.startswith("missing mode marker(s):")
                for failure in missing_marker_result["failures"]
            )
        ):
            print("self-test failed: missing mode markers did not fail", file=sys.stderr)
            print(json.dumps(missing_marker_result, indent=2), file=sys.stderr)
            return 1

        sp_wrong_path = root / "ef_sp_wrong_path.txt"
        sp_wrong_path.write_text(
            "\n".join(
                [
                    "JA: SV_SpawnServer entered map='borg1' reload=0 dissolve=1",
                    "JA: cls.state = CA_ACTIVE - GAME IS RUNNING",
                    "STEFX_HW_CHECKPOINT: first frame render complete",
                    heartbeat(10, 1000, path=1),
                    heartbeat(20, 65000, path=2),
                ]
            ),
            encoding="utf-8",
        )
        wrong_path_result = analyze_log(sp_wrong_path, "sp", 10.0)
        if (
            wrong_path_result["status"] != "fail"
            or not any(
                failure.startswith("non-retail renderer path value")
                for failure in wrong_path_result["failures"]
            )
        ):
            print("self-test failed: non-retail renderer path did not fail", file=sys.stderr)
            print(json.dumps(wrong_path_result, indent=2), file=sys.stderr)
            return 1

        sp_partial_heartbeat = root / "ef_sp_partial_heartbeat.txt"
        sp_partial_heartbeat.write_text(
            "\n".join(
                [
                    "JA: SV_SpawnServer entered map='borg1' reload=0 dissolve=1",
                    "JA: cls.state = CA_ACTIVE - GAME IS RUNNING",
                    "STEFX_HW_CHECKPOINT: first frame render complete",
                    "JA: FRAME_HEARTBEAT completedFrame=10 realtime=1000 fps=72.5 mem=1000/2000/1500/0",
                    "JA: FRAME_HEARTBEAT completedFrame=20 realtime=65000 fps=72.5 path=1",
                ]
            ),
            encoding="utf-8",
        )
        partial_heartbeat_result = analyze_log(sp_partial_heartbeat, "sp", 10.0)
        partial_failures = partial_heartbeat_result["failures"]
        if (
            partial_heartbeat_result["status"] != "fail"
            or not any("field path missing" in failure for failure in partial_failures)
            or not any("field mem missing" in failure for failure in partial_failures)
            or not any("memory sample missing" in failure for failure in partial_failures)
            or not any("renderer path missing" in failure for failure in partial_failures)
        ):
            print("self-test failed: partial heartbeat telemetry did not fail", file=sys.stderr)
            print(json.dumps(partial_heartbeat_result, indent=2), file=sys.stderr)
            return 1

        observation_path = stage / OBSERVATION_FILE
        write_observation_template(observation_path, "self-test", force=False)
        observation = json.loads(observation_path.read_text(encoding="utf-8"))
        for mode in OBSERVATION_MODES:
            observation[mode]["mapsTested"] = [
                "hm_borg1" if mode == "mp" else "borg1"
            ]
            observation[mode]["durationSeconds"] = 95
            observation[mode]["visibleFpsMin"] = 60
            observation[mode]["visibleFpsMax"] = 90
            observation[mode]["memoryFreeMinimum"] = 4096
            observation[mode]["memoryLargestFreeMinimum"] = 2048
            observation[mode]["memoryUsedDelta"] = 0
            for field in MODE_OBSERVATION_BOOLEANS[mode]:
                observation[mode][field] = True
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        obs_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
            min_observed_largest_free={"coop": 1024},
            max_observed_used_delta={"coop": 0},
        )
        if obs_result["status"] != "pass":
            print("self-test failed: clean observation file did not pass", file=sys.stderr)
            print(json.dumps(obs_result, indent=2), file=sys.stderr)
            return 1
        coop_summary = obs_result.get("summary", {}).get("coop")
        if (
            not isinstance(coop_summary, dict)
            or coop_summary.get("memoryLargestFreeMinimum") != 2048
            or coop_summary.get("memoryUsedDelta") != 0
        ):
            print("self-test failed: observation memory summary was not reported", file=sys.stderr)
            print(json.dumps(obs_result, indent=2), file=sys.stderr)
            return 1
        if (
            obs_result.get("sha256") != file_sha256(observation_path)
            or obs_result.get("modifiedUtc") != file_modified_utc(observation_path)
        ):
            print("self-test failed: observation file provenance was not reported", file=sys.stderr)
            print(json.dumps(obs_result, indent=2), file=sys.stderr)
            return 1

        sp_evidence = stage / "sp_photo.png"
        coop_evidence = stage / "coop_photo.png"
        mp_evidence = stage / "mp_photo.png"
        sp_evidence.write_bytes(screenshot_png)
        coop_evidence.write_bytes(screenshot_png)
        mp_evidence.write_bytes(screenshot_png)
        observation["sp"]["evidenceFiles"] = [sp_evidence.name]
        observation["coop"]["evidenceFiles"] = [coop_evidence.name]
        observation["mp"]["evidenceFiles"] = [mp_evidence.name]
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        evidence_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
            require_evidence_files=True,
        )
        if (
            evidence_result["status"] != "pass"
            or evidence_result["evidenceFiles"]["sp"][0].get("sha256") != file_sha256(sp_evidence)
            or evidence_result["evidenceFiles"]["sp"][0].get("modifiedUtc")
            != file_modified_utc(sp_evidence)
            or evidence_result["evidenceFiles"]["sp"][0].get("visualType") != "png"
            or evidence_result["evidenceFiles"]["sp"][0].get("width") != 640
            or evidence_result["evidenceFiles"]["sp"][0].get("height") != 480
        ):
            print("self-test failed: evidence files were not verified", file=sys.stderr)
            print(json.dumps(evidence_result, indent=2), file=sys.stderr)
            return 1

        jpeg_evidence = stage / "sp_photo.jpg"
        jpeg_evidence.write_bytes(screenshot_jpeg)
        observation["sp"]["evidenceFiles"] = [jpeg_evidence.name]
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        jpeg_evidence_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
            require_evidence_files=True,
        )
        if (
            jpeg_evidence_result["status"] != "pass"
            or jpeg_evidence_result["evidenceFiles"]["sp"][0].get("visualType") != "jpeg"
            or jpeg_evidence_result["evidenceFiles"]["sp"][0].get("width") != 640
            or jpeg_evidence_result["evidenceFiles"]["sp"][0].get("height") != 480
        ):
            print("self-test failed: JPEG evidence dimensions were not verified", file=sys.stderr)
            print(json.dumps(jpeg_evidence_result, indent=2), file=sys.stderr)
            return 1
        observation["sp"]["evidenceFiles"] = [sp_evidence.name]

        small_png_evidence = stage / "small_photo.png"
        small_png_evidence.write_bytes(tiny_png[:64])
        observation["coop"]["evidenceFiles"] = [small_png_evidence.name]
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        small_png_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
            require_evidence_files=True,
        )
        if (
            small_png_result["status"] != "fail"
            or small_png_result["evidenceFiles"]["coop"][0].get("status") != "too-small"
            or not any(
                "coop: evidence file is smaller than" in failure
                for failure in small_png_result["failures"]
            )
        ):
            print("self-test failed: too-small visual evidence file was not rejected", file=sys.stderr)
            print(json.dumps(small_png_result, indent=2), file=sys.stderr)
            return 1
        observation["coop"]["evidenceFiles"] = [coop_evidence.name]

        tiny_dimension_evidence = stage / "tiny_dimension_photo.png"
        tiny_dimension_evidence.write_bytes(tiny_png)
        observation["coop"]["evidenceFiles"] = [tiny_dimension_evidence.name]
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        tiny_dimension_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
            require_evidence_files=True,
        )
        if (
            tiny_dimension_result["status"] != "fail"
            or tiny_dimension_result["evidenceFiles"]["coop"][0].get("status")
            != "too-small-dimensions"
            or not any(
                "coop: evidence image dimensions" in failure
                for failure in tiny_dimension_result["failures"]
            )
        ):
            print("self-test failed: tiny-dimension visual evidence file was not rejected", file=sys.stderr)
            print(json.dumps(tiny_dimension_result, indent=2), file=sys.stderr)
            return 1
        observation["coop"]["evidenceFiles"] = [coop_evidence.name]

        bad_png_evidence = stage / "renamed_text.png"
        bad_png_evidence.write_text("not really a png", encoding="utf-8")
        observation["coop"]["evidenceFiles"] = [bad_png_evidence.name]
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        bad_png_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
            require_evidence_files=True,
        )
        if (
            bad_png_result["status"] != "fail"
            or bad_png_result["evidenceFiles"]["coop"][0].get("status") != "bad-signature"
            or not any(
                "coop: evidence file signature is not recognized as visual" in failure
                for failure in bad_png_result["failures"]
            )
        ):
            print("self-test failed: bad visual evidence signature was not rejected", file=sys.stderr)
            print(json.dumps(bad_png_result, indent=2), file=sys.stderr)
            return 1
        observation["coop"]["evidenceFiles"] = [coop_evidence.name]

        text_evidence = stage / "not_visual.txt"
        text_evidence.write_text("not visual proof", encoding="utf-8")
        observation["coop"]["evidenceFiles"] = [text_evidence.name]
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        text_evidence_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
            require_evidence_files=True,
        )
        if (
            text_evidence_result["status"] != "fail"
            or text_evidence_result["evidenceFiles"]["coop"][0].get("status")
            != "unsupported-extension"
            or not any(
                "coop: evidence file extension is not visual" in failure
                for failure in text_evidence_result["failures"]
            )
        ):
            print("self-test failed: non-visual evidence file was not rejected", file=sys.stderr)
            print(json.dumps(text_evidence_result, indent=2), file=sys.stderr)
            return 1
        observation["coop"]["evidenceFiles"] = [coop_evidence.name]

        empty_evidence = stage / "empty_photo.png"
        empty_evidence.write_bytes(b"")
        observation["coop"]["evidenceFiles"] = [empty_evidence.name]
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        empty_evidence_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
            require_evidence_files=True,
        )
        if (
            empty_evidence_result["status"] != "fail"
            or empty_evidence_result["evidenceFiles"]["coop"][0].get("status") != "empty"
            or not any(
                "coop: evidence file is empty" in failure
                for failure in empty_evidence_result["failures"]
            )
        ):
            print("self-test failed: empty evidence file was not rejected", file=sys.stderr)
            print(json.dumps(empty_evidence_result, indent=2), file=sys.stderr)
            return 1
        observation["coop"]["evidenceFiles"] = [coop_evidence.name]

        observation["mp"]["evidenceFiles"] = ["missing_photo.txt"]
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        missing_evidence_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
            require_evidence_files=True,
        )
        if missing_evidence_result["status"] != "fail" or not any(
            "mp: evidence file missing" in failure
            for failure in missing_evidence_result["failures"]
        ):
            print("self-test failed: missing evidence file was not rejected", file=sys.stderr)
            print(json.dumps(missing_evidence_result, indent=2), file=sys.stderr)
            return 1
        observation["mp"]["evidenceFiles"] = [mp_evidence.name]

        observation["manifestVersion"] = "older-stage"
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        stale_obs_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
        )
        if stale_obs_result["status"] != "fail" or not any(
            "manifestVersion mismatch" in failure for failure in stale_obs_result["failures"]
        ):
            print("self-test failed: stale observation version did not fail", file=sys.stderr)
            print(json.dumps(stale_obs_result, indent=2), file=sys.stderr)
            return 1
        observation["manifestVersion"] = "self-test"

        observation["sp"]["mapsTested"] = ["dn3"]
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        map_obs_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
        )
        if map_obs_result["status"] != "fail" or not any(
            "sp: mapsTested" in failure for failure in map_obs_result["failures"]
        ):
            print("self-test failed: observation/log map mismatch did not fail", file=sys.stderr)
            print(json.dumps(map_obs_result, indent=2), file=sys.stderr)
            return 1
        observation["sp"]["mapsTested"] = ["borg1"]

        low_fps_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 75.0, "coop": 50.0, "mp": 50.0},
        )
        if low_fps_result["status"] != "fail" or not any(
            "sp: visibleFpsMin" in failure for failure in low_fps_result["failures"]
        ):
            print("self-test failed: visible FPS floor was not enforced", file=sys.stderr)
            print(json.dumps(low_fps_result, indent=2), file=sys.stderr)
            return 1

        del observation["coop"]["memoryFreeMinimum"]
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        missing_field_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
        )
        if missing_field_result["status"] != "fail" or not any(
            "coop: missing observation field memoryFreeMinimum" in failure
            for failure in missing_field_result["failures"]
        ):
            print("self-test failed: missing observation schema field was not rejected", file=sys.stderr)
            print(json.dumps(missing_field_result, indent=2), file=sys.stderr)
            return 1
        observation["coop"]["memoryFreeMinimum"] = 4096

        observation["sp"]["mapsTested"] = ["borg1"]
        observation["coop"]["memoryLargestFreeMinimum"] = 512
        observation["coop"]["memoryUsedDelta"] = 8
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        observed_memory_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
            min_observed_largest_free={"coop": 1024},
            max_observed_used_delta={"coop": 0},
        )
        if (
            observed_memory_result["status"] != "fail"
            or not any(
                "coop: memoryLargestFreeMinimum" in failure
                for failure in observed_memory_result["failures"]
            )
            or not any(
                "coop: memoryUsedDelta" in failure
                for failure in observed_memory_result["failures"]
            )
        ):
            print("self-test failed: observed co-op memory thresholds were not enforced", file=sys.stderr)
            print(json.dumps(observed_memory_result, indent=2), file=sys.stderr)
            return 1
        observation["coop"]["memoryLargestFreeMinimum"] = 2048
        observation["coop"]["memoryUsedDelta"] = 0

        required_map_result = required_map_failures(
            {"sp": sp_result, "mp": mp_result},
            {"sp": ["borg6"], "mp": ["hm_borg1"]},
        )
        if not required_map_result or "borg6" not in required_map_result[0]:
            print("self-test failed: missing required log map did not fail", file=sys.stderr)
            print(json.dumps(required_map_result, indent=2), file=sys.stderr)
            return 1

        memory_pass = memory_threshold_failures(
            {"sp": sp_result, "mp": mp_result},
            min_free={"sp": 1500, "mp": 1500},
            min_largest_free={"sp": 1000, "mp": 1000},
            max_used_delta={"sp": 0, "mp": 0},
        )
        if memory_pass:
            print("self-test failed: clean memory thresholds did not pass", file=sys.stderr)
            print(json.dumps(memory_pass, indent=2), file=sys.stderr)
            return 1

        memory_fail = memory_threshold_failures(
            {"sp": sp_result, "mp": mp_result},
            min_free={"sp": 2500},
            min_largest_free={"mp": 2000},
            max_used_delta={},
        )
        if len(memory_fail) != 2 or not any("sp: freeMinimum" in item for item in memory_fail):
            print("self-test failed: memory thresholds were not enforced", file=sys.stderr)
            print(json.dumps(memory_fail, indent=2), file=sys.stderr)
            return 1

        observation["mp"]["botsOrCombatOk"] = False
        observation_path.write_text(json.dumps(observation, indent=2), encoding="utf-8")
        bad_obs_result = verify_observation(
            observation_path,
            require=True,
            min_duration_seconds=90,
            expected_manifest_version="self-test",
            expected_log_maps={"sp": ["borg1"], "mp": ["hm_borg1"]},
            required_maps={"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]},
            min_visible_fps={"sp": 50.0, "coop": 50.0, "mp": 50.0},
        )
        if bad_obs_result["status"] != "fail" or not any(
            "mp: botsOrCombatOk" in failure for failure in bad_obs_result["failures"]
        ):
            print("self-test failed: incomplete observation file did not fail", file=sys.stderr)
            print(json.dumps(bad_obs_result, indent=2), file=sys.stderr)
            return 1

        report_path = stage / "qualification_report.json"
        written_report = write_report_file(
            report_path,
            {
                "stage": str(stage),
                "manifestVersion": "self-test",
                "status": "pass",
                "logs": {"sp": sp_result, "mp": mp_result},
                "observation": obs_result,
            },
        )
        if written_report != str(report_path.resolve()):
            print("self-test failed: report path was not returned", file=sys.stderr)
            return 1
        report = json.loads(report_path.read_text(encoding="utf-8"))
        if (
            report.get("status") != "pass"
            or report.get("reportType") != REPORT_TYPE
            or report.get("reportSchemaVersion") != REPORT_SCHEMA_VERSION
            or report.get("reportPath") != str(report_path.resolve())
            or "generatedAtUtc" not in report
        ):
            print("self-test failed: report file did not contain expected payload", file=sys.stderr)
            print(json.dumps(report, indent=2), file=sys.stderr)
            return 1
        provenance = report.get("verifierProvenance")
        if (
            not isinstance(provenance, dict)
            or provenance.get("scriptSha256") != file_sha256(Path(__file__).resolve())
            or "--self-test" not in provenance.get("argv", [])
        ):
            print("self-test failed: report verifier provenance was not preserved", file=sys.stderr)
            print(json.dumps(report, indent=2), file=sys.stderr)
            return 1

        missing_report_path = stage / "missing_qualification_report.json"
        missing_result = subprocess.run(
            [
                sys.executable,
                str(Path(__file__).resolve()),
                "--repo-root",
                str(root),
                "--stage",
                str(stage),
                "--sp-log",
                str(root / "missing_sp_log.txt"),
                "--mp-log",
                str(root / "missing_mp_log.txt"),
                "--skip-default-cfg",
                "--skip-package-check",
                "--require-observation",
                "--require-evidence-files",
                "--min-evidence-bytes",
                "256",
                "--min-image-width",
                "160",
                "--min-image-height",
                "120",
                "--required-sp-map",
                "borg1",
                "--required-coop-map",
                "borg1",
                "--required-mp-map",
                "hm_borg1",
                "--min-sp-visible-fps",
                "50",
                "--min-coop-visible-fps",
                "50",
                "--min-mp-visible-fps",
                "50",
                "--min-sp-largest-free",
                "1000",
                "--max-mp-used-delta",
                "0",
                "--min-coop-observed-largest-free",
                "1000",
                "--max-coop-observed-used-delta",
                "0",
                "--report-out",
                str(missing_report_path),
                "--json",
            ],
            cwd=root,
            text=True,
            capture_output=True,
        )
        if missing_result.returncode != 2:
            print("self-test failed: missing-log verifier did not return code 2", file=sys.stderr)
            print(missing_result.stdout, file=sys.stderr)
            print(missing_result.stderr, file=sys.stderr)
            return 1
        try:
            missing_output = json.loads(missing_result.stdout)
            missing_report = json.loads(missing_report_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            print(f"self-test failed: missing-log report was not valid JSON: {exc}", file=sys.stderr)
            print(missing_result.stdout, file=sys.stderr)
            print(missing_result.stderr, file=sys.stderr)
            return 1
        if missing_output.get("status") != "missing" or missing_report.get("status") != "missing":
            print("self-test failed: missing-log status was not reported", file=sys.stderr)
            print(json.dumps({"stdout": missing_output, "report": missing_report}, indent=2), file=sys.stderr)
            return 1
        if (
            missing_report.get("reportType") != REPORT_TYPE
            or missing_report.get("reportSchemaVersion") != REPORT_SCHEMA_VERSION
            or missing_report.get("reportPath") != str(missing_report_path.resolve())
            or "generatedAtUtc" not in missing_report
        ):
            print("self-test failed: missing-log report schema metadata was not preserved", file=sys.stderr)
            print(json.dumps(missing_report, indent=2), file=sys.stderr)
            return 1
        missing_provenance = missing_report.get("verifierProvenance")
        missing_argv = missing_provenance.get("argv", []) if isinstance(missing_provenance, dict) else []
        if (
            not isinstance(missing_provenance, dict)
            or missing_provenance.get("scriptSha256") != file_sha256(Path(__file__).resolve())
            or "--required-sp-map" not in missing_argv
            or "--report-out" not in missing_argv
        ):
            print("self-test failed: missing-log report verifier provenance was not preserved", file=sys.stderr)
            print(json.dumps(missing_report, indent=2), file=sys.stderr)
            return 1
        criteria = missing_report.get("qualificationCriteria")
        if (
            not isinstance(criteria, dict)
            or criteria.get("requireObservation") is not True
            or criteria.get("requireEvidenceFiles") is not True
            or criteria.get("minEvidenceBytes") != 256
            or criteria.get("minImageWidth") != 160
            or criteria.get("minImageHeight") != 120
            or criteria.get("requiredMaps")
            != {"sp": ["borg1"], "coop": ["borg1"], "mp": ["hm_borg1"]}
            or criteria.get("minVisibleFps") != {"sp": 50.0, "coop": 50.0, "mp": 50.0}
            or criteria.get("memoryThresholds", {}).get("minLargestFree") != {"sp": 1000}
            or criteria.get("memoryThresholds", {}).get("maxUsedDelta") != {"mp": 0}
            or criteria.get("observedMemoryThresholds", {}).get("minLargestFree")
            != {"coop": 1000}
            or criteria.get("observedMemoryThresholds", {}).get("maxUsedDelta")
            != {"coop": 0}
        ):
            print("self-test failed: missing-log qualification criteria were not preserved", file=sys.stderr)
            print(json.dumps(missing_report, indent=2), file=sys.stderr)
            return 1
        if missing_report.get("stageIntegrity", {}).get("status") != "pass":
            print("self-test failed: missing-log report did not include passing stage integrity", file=sys.stderr)
            print(json.dumps(missing_report, indent=2), file=sys.stderr)
            return 1
        if missing_report.get("runtimeMarkerFailures"):
            print("self-test failed: expected runtime logs were treated as marker failures", file=sys.stderr)
            print(json.dumps(missing_report, indent=2), file=sys.stderr)
            return 1
        missing_manifest = missing_report.get("manifest")
        if (
            not isinstance(missing_manifest, dict)
            or missing_manifest.get("path") != str(manifest_path)
            or missing_manifest.get("bytes") != manifest_path.stat().st_size
            or missing_manifest.get("modifiedUtc") != file_modified_utc(manifest_path)
            or missing_manifest.get("sha256") != file_sha256(manifest_path)
        ):
            print("self-test failed: missing-log report did not include manifest provenance", file=sys.stderr)
            print(json.dumps(missing_report, indent=2), file=sys.stderr)
            return 1
        missing_observation = missing_report.get("observation")
        if (
            not isinstance(missing_observation, dict)
            or missing_observation.get("checked") is not True
            or missing_observation.get("sha256") != file_sha256(observation_path)
            or missing_observation.get("modifiedUtc") != file_modified_utc(observation_path)
            or missing_observation.get("status") != "fail"
            or not any(
                "mp: botsOrCombatOk" in failure
                for failure in missing_observation.get("failures", [])
            )
        ):
            print("self-test failed: missing-log report did not verify observation readiness", file=sys.stderr)
            print(json.dumps(missing_report, indent=2), file=sys.stderr)
            return 1

    print("verify_production_hardware_logs self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Verify production hardware logs copied back from the retail Xbox. "
            "This checks log liveness and obvious failure markers only; visible FPS "
            "and visual correctness still require observation or photos."
        )
    )
    parser.add_argument(
        "--stage",
        type=Path,
        default=Path("build/hardware/StarTrekEliteForceX-Beta-20260801"),
        help="Hardware stage directory containing ef_sp_log.txt and ef_mp_log.txt.",
    )
    parser.add_argument("--sp-log", type=Path, help="Override SP log path.")
    parser.add_argument("--mp-log", type=Path, help="Override Holomatch log path.")
    parser.add_argument(
        "--min-elapsed-seconds",
        type=float,
        default=60.0,
        help="Minimum elapsed heartbeat time expected in each log.",
    )
    parser.add_argument(
        "--observation",
        type=Path,
        help=f"Hardware observation JSON path. Defaults to stage/{OBSERVATION_FILE}.",
    )
    parser.add_argument(
        "--require-observation",
        action="store_true",
        help="Require filled manual FPS/visual/control observations for the final verdict.",
    )
    parser.add_argument(
        "--require-evidence-files",
        action="store_true",
        help="Require each observation mode to list at least one existing visual evidence file.",
    )
    parser.add_argument(
        "--min-evidence-bytes",
        type=int,
        default=DEFAULT_MIN_EVIDENCE_BYTES,
        help="Minimum byte size for each listed visual evidence file.",
    )
    parser.add_argument(
        "--min-image-width",
        type=int,
        default=DEFAULT_MIN_IMAGE_WIDTH,
        help="Minimum pixel width for each listed still-image evidence file.",
    )
    parser.add_argument(
        "--min-image-height",
        type=int,
        default=DEFAULT_MIN_IMAGE_HEIGHT,
        help="Minimum pixel height for each listed still-image evidence file.",
    )
    parser.add_argument(
        "--min-observation-seconds",
        type=float,
        default=90.0,
        help="Minimum manually observed runtime expected in each mode.",
    )
    parser.add_argument(
        "--required-sp-map",
        action="append",
        default=[],
        help="SP map name that must appear in both the returned SP log and observation mapsTested.",
    )
    parser.add_argument(
        "--required-mp-map",
        action="append",
        default=[],
        help="Holomatch map name that must appear in both the returned MP log and observation mapsTested.",
    )
    parser.add_argument(
        "--required-coop-map",
        action="append",
        default=[],
        help="Co-op map name that must appear in observation mapsTested.",
    )
    parser.add_argument(
        "--min-sp-visible-fps",
        type=float,
        help="Minimum acceptable visible FPS floor recorded in the SP observation.",
    )
    parser.add_argument(
        "--min-mp-visible-fps",
        type=float,
        help="Minimum acceptable visible FPS floor recorded in the Holomatch observation.",
    )
    parser.add_argument(
        "--min-coop-visible-fps",
        type=float,
        help="Minimum acceptable visible FPS floor recorded in the co-op observation.",
    )
    parser.add_argument(
        "--min-sp-free",
        type=int,
        help="Minimum acceptable SP free memory sample value from FRAME_HEARTBEAT mem=.",
    )
    parser.add_argument(
        "--min-mp-free",
        type=int,
        help="Minimum acceptable Holomatch free memory sample value from FRAME_HEARTBEAT mem=.",
    )
    parser.add_argument(
        "--min-sp-largest-free",
        type=int,
        help="Minimum acceptable SP largest-free memory sample value from FRAME_HEARTBEAT mem=.",
    )
    parser.add_argument(
        "--min-mp-largest-free",
        type=int,
        help="Minimum acceptable Holomatch largest-free memory sample value from FRAME_HEARTBEAT mem=.",
    )
    parser.add_argument(
        "--max-sp-used-delta",
        type=int,
        help="Maximum allowed SP used-memory growth between first and last heartbeat.",
    )
    parser.add_argument(
        "--max-mp-used-delta",
        type=int,
        help="Maximum allowed Holomatch used-memory growth between first and last heartbeat.",
    )
    parser.add_argument(
        "--require-hm-split-log",
        action="store_true",
        help="Require returned ef_mp_log.txt to pass the focused 4P Holomatch split-screen proof verifier.",
    )
    parser.add_argument("--hm-split-min-bots", type=int, default=3)
    parser.add_argument("--hm-split-min-elapsed-seconds", type=float, default=90.0)
    parser.add_argument("--hm-split-min-heartbeat-fps", type=float, default=15.0)
    parser.add_argument("--hm-split-min-largest-free", type=int, default=1048576)
    parser.add_argument("--hm-split-max-used-delta", type=int, default=0)
    parser.add_argument(
        "--hm-split-require-audio-backend",
        action="store_true",
        help="Require Holomatch split proof to include Xbox audio backend state 4 telemetry.",
    )
    parser.add_argument(
        "--hm-split-require-audio-listeners",
        action="store_true",
        help="Require Holomatch split proof to include four active audio listeners and P2-P4 listener updates.",
    )
    parser.add_argument("--hm-split-min-audio-starts", type=int)
    parser.add_argument("--hm-split-min-audio-voice-starts", type=int)
    parser.add_argument("--hm-split-require-audio-lip-active", action="store_true")
    parser.add_argument(
        "--min-coop-observed-largest-free",
        type=int,
        help=(
            "Minimum acceptable co-op memoryLargestFreeMinimum value recorded "
            "in the hardware observation."
        ),
    )
    parser.add_argument(
        "--max-coop-observed-used-delta",
        type=int,
        help=(
            "Maximum acceptable co-op memoryUsedDelta value recorded in the "
            "hardware observation."
        ),
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root for stage artifact integrity checks.",
    )
    parser.add_argument(
        "--direct-map",
        default="hm_borg1",
        help="Expected Holomatch direct-map package marker for stage integrity checks.",
    )
    parser.add_argument(
        "--default-cfg",
        type=Path,
        default=DEFAULT_CFG,
        help="Canonical controller config path for stage integrity checks.",
    )
    parser.add_argument(
        "--skip-default-cfg",
        action="store_true",
        help="Do not check the canonical default.cfg hash during stage integrity checks.",
    )
    parser.add_argument(
        "--skip-package-check",
        action="store_true",
        help="Do not run the Holomatch package checker during stage integrity checks.",
    )
    parser.add_argument(
        "--skip-stage-integrity",
        action="store_true",
        help="Only verify returned logs and observations; do not re-check staged artifacts.",
    )
    parser.add_argument(
        "--write-observation-template",
        action="store_true",
        help="Write an observation JSON template and exit.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Allow --write-observation-template to overwrite an existing file.",
    )
    parser.add_argument(
        "--report-out",
        type=Path,
        help="Write the JSON verifier result to this file for qualification records.",
    )
    parser.add_argument("--json", action="store_true", help="Emit JSON only.")
    parser.add_argument("--self-test", action="store_true", help="Run built-in verifier tests.")
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()

    repo_root = args.repo_root.resolve()
    stage = args.stage if args.stage.is_absolute() else repo_root / args.stage
    manifest = load_manifest(stage) if stage.exists() else None
    manifest_record = manifest_file_record(stage)
    observation_path = resolve_optional_path(repo_root, args.observation) or stage / OBSERVATION_FILE
    report_path = resolve_optional_path(repo_root, args.report_out)

    if args.write_observation_template:
        try:
            write_observation_template(
                observation_path,
                manifest.get("version") if isinstance(manifest, dict) else "",
                force=args.force,
            )
        except FileExistsError as exc:
            output = {"status": "exists", "error": str(exc)}
            written_report = write_report_file(report_path, output)
            if written_report:
                output["reportPath"] = written_report
            if args.json:
                print(json.dumps(output, indent=2))
            else:
                print(str(exc))
                print("Use --force to overwrite it.")
                if written_report:
                    print(f"Report written: {written_report}")
            return 1
        output = {"status": "written", "path": str(observation_path)}
        written_report = write_report_file(report_path, output)
        if written_report:
            output["reportPath"] = written_report
        if args.json:
            print(json.dumps(output, indent=2))
        else:
            print(f"Observation template written: {observation_path}")
            if written_report:
                print(f"Report written: {written_report}")
        return 0

    if isinstance(manifest, dict) and manifest.get("frameDiagnostics") is True:
        message = (
            "Stage manifest is a frame-diagnostic build; production log verification "
            "is not applicable."
        )
        output = {
            "stage": str(stage),
            "manifest": manifest_record,
            "manifestVersion": manifest.get("version"),
            "manifestFrameDiagnostics": manifest.get("frameDiagnostics"),
            "status": "not-applicable",
            "reason": message,
        }
        written_report = write_report_file(report_path, output)
        if written_report:
            output["reportPath"] = written_report
        if args.json:
            print(json.dumps(output, indent=2))
        else:
            print(message)
            print("Use the frame-phase photo/overlay procedure documented in TRANSFER_README.txt.")
            if written_report:
                print(f"Report written: {written_report}")
        return 2

    logs = {
        "sp": resolve_optional_path(repo_root, args.sp_log) or stage / MODE_FILES["sp"],
        "mp": resolve_optional_path(repo_root, args.mp_log) or stage / MODE_FILES["mp"],
    }
    stage_integrity, runtime_marker_failures = collect_stage_integrity(
        repo_root=repo_root,
        stage=stage,
        direct_map=args.direct_map,
        default_cfg=None if args.skip_default_cfg else args.default_cfg,
        run_package_check=not args.skip_package_check,
        skip_stage_integrity=args.skip_stage_integrity,
    )
    expected_runtime_build_ids = manifest_runtime_build_ids(manifest if isinstance(manifest, dict) else None)
    runtime_build_id_failures = manifest_runtime_build_id_failures(
        manifest if isinstance(manifest, dict) else None,
        expected_runtime_build_ids,
    )
    required_maps = {
        "sp": unique_ordered(normalize_map_name(map_name) for map_name in args.required_sp_map),
        "coop": unique_ordered(
            normalize_map_name(map_name) for map_name in args.required_coop_map
        ),
        "mp": unique_ordered(normalize_map_name(map_name) for map_name in args.required_mp_map),
    }
    min_visible_fps = {
        mode: value
        for mode, value in {
            "sp": args.min_sp_visible_fps,
            "coop": args.min_coop_visible_fps,
            "mp": args.min_mp_visible_fps,
        }.items()
        if value is not None
    }
    min_free = {
        mode: value
        for mode, value in {
            "sp": args.min_sp_free,
            "mp": args.min_mp_free,
        }.items()
        if value is not None
    }
    min_largest_free = {
        mode: value
        for mode, value in {
            "sp": args.min_sp_largest_free,
            "mp": args.min_mp_largest_free,
        }.items()
        if value is not None
    }
    max_used_delta = {
        mode: value
        for mode, value in {
            "sp": args.max_sp_used_delta,
            "mp": args.max_mp_used_delta,
        }.items()
        if value is not None
    }
    min_observed_largest_free = {
        mode: value
        for mode, value in {
            "coop": args.min_coop_observed_largest_free,
        }.items()
        if value is not None
    }
    max_observed_used_delta = {
        mode: value
        for mode, value in {
            "coop": args.max_coop_observed_used_delta,
        }.items()
        if value is not None
    }
    qualification_criteria = {
        "minElapsedSeconds": args.min_elapsed_seconds,
        "requireObservation": args.require_observation,
        "requireEvidenceFiles": args.require_evidence_files,
        "minEvidenceBytes": args.min_evidence_bytes,
        "minImageWidth": args.min_image_width,
        "minImageHeight": args.min_image_height,
        "minObservationSeconds": args.min_observation_seconds,
        "requiredMaps": required_maps,
        "minVisibleFps": min_visible_fps,
        "memoryThresholds": {
            "minFree": min_free,
            "minLargestFree": min_largest_free,
            "maxUsedDelta": max_used_delta,
        },
        "observedMemoryThresholds": {
            "minLargestFree": min_observed_largest_free,
            "maxUsedDelta": max_observed_used_delta,
        },
        "expectedRuntimeBuildIds": expected_runtime_build_ids,
        "holomatchSplitLog": {
            "required": args.require_hm_split_log,
            "minBots": args.hm_split_min_bots,
            "minElapsedSeconds": args.hm_split_min_elapsed_seconds,
            "minHeartbeatFps": args.hm_split_min_heartbeat_fps,
            "minLargestFree": args.hm_split_min_largest_free,
            "maxUsedDelta": args.hm_split_max_used_delta,
        },
    }

    missing = [f"{mode}:{path}" for mode, path in logs.items() if not path.is_file()]
    if missing:
        observation = verify_observation(
            observation_path,
            require=args.require_observation,
            min_duration_seconds=args.min_observation_seconds,
            expected_manifest_version=manifest.get("version") if isinstance(manifest, dict) else None,
            required_maps=required_maps,
            min_visible_fps=min_visible_fps,
            min_observed_largest_free=min_observed_largest_free,
            max_observed_used_delta=max_observed_used_delta,
            require_evidence_files=args.require_evidence_files,
            min_evidence_bytes=args.min_evidence_bytes,
            min_image_width=args.min_image_width,
            min_image_height=args.min_image_height,
        )
        output = {
            "stage": str(stage),
            "manifest": manifest_record,
            "manifestVersion": manifest.get("version") if isinstance(manifest, dict) else None,
            "manifestFrameDiagnostics": manifest.get("frameDiagnostics")
            if isinstance(manifest, dict)
            else None,
            "status": "missing",
            "missing": missing,
            "logs": {mode: str(path) for mode, path in logs.items()},
            "qualificationCriteria": qualification_criteria,
            "stageIntegrity": stage_integrity,
            "runtimeMarkerFailures": runtime_marker_failures,
            "runtimeBuildIdFailures": runtime_build_id_failures,
            "observation": observation,
        }
        written_report = write_report_file(report_path, output)
        if written_report:
            output["reportPath"] = written_report
        if args.json:
            print(json.dumps(output, indent=2))
        else:
            print("Missing required hardware log(s):")
            for item in missing:
                print(f"  {item}")
            print(f"Stage integrity: {stage_integrity.get('status')}")
            for failure in stage_integrity.get("failures", []):
                print(f"  stage failure: {failure}")
            runtime_markers = stage_integrity.get("runtimeMarkers")
            if runtime_markers:
                print("  returned runtime files present: " + ", ".join(runtime_markers))
            for failure in runtime_marker_failures:
                print(f"  stage failure: {failure}")
            for failure in runtime_build_id_failures:
                print(f"  runtime build id failure: {failure}")
            print(f"Observation: {observation['status']} path={observation['path']}")
            if observation.get("sha256"):
                print(f"  observation bytes={observation.get('bytes')} sha256={observation.get('sha256')}")
            for failure in observation.get("failures", []):
                print(f"  observation failure: {failure}")
            print("Copy ef_sp_log.txt and ef_mp_log.txt back into the stage folder, or use overrides.")
            if written_report:
                print(f"Report written: {written_report}")
        return 2

    results = {
        mode: analyze_log(
            path,
            mode,
            args.min_elapsed_seconds,
            expected_runtime_build_ids.get(mode),
        )
        for mode, path in logs.items()
    }
    required_log_map_failures = required_map_failures(results, required_maps)
    memory_failures = memory_threshold_failures(
        results,
        min_free=min_free,
        min_largest_free=min_largest_free,
        max_used_delta=max_used_delta,
    )
    holomatch_split_log = (
        verify_holomatch_split_log(logs["mp"], args)
        if args.require_hm_split_log
        else {"status": "not-required", "criteria": qualification_criteria["holomatchSplitLog"]}
    )
    observation = verify_observation(
        observation_path,
        require=args.require_observation,
        min_duration_seconds=args.min_observation_seconds,
        expected_manifest_version=manifest.get("version") if isinstance(manifest, dict) else None,
        expected_log_maps={
            mode: result.get("maps", {}).get("normalized", [])
            for mode, result in results.items()
            if isinstance(result.get("maps"), dict)
        },
        required_maps=required_maps,
        min_visible_fps=min_visible_fps,
        min_observed_largest_free=min_observed_largest_free,
        max_observed_used_delta=max_observed_used_delta,
        require_evidence_files=args.require_evidence_files,
        min_evidence_bytes=args.min_evidence_bytes,
        min_image_width=args.min_image_width,
        min_image_height=args.min_image_height,
    )
    status = "pass" if all(result["status"] == "pass" for result in results.values()) else "fail"
    if stage_integrity.get("status") == "fail":
        status = "fail"
    if runtime_marker_failures:
        status = "fail"
    if runtime_build_id_failures:
        status = "fail"
    if required_log_map_failures:
        status = "fail"
    if memory_failures:
        status = "fail"
    if args.require_hm_split_log and holomatch_split_log.get("status") != "pass":
        status = "fail"
    if observation["status"] == "fail" or (
        args.require_observation and observation["status"] != "pass"
    ):
        status = "fail"
    output = {
        "stage": str(stage),
        "manifest": manifest_record,
        "manifestVersion": manifest.get("version") if isinstance(manifest, dict) else None,
        "manifestFrameDiagnostics": manifest.get("frameDiagnostics")
        if isinstance(manifest, dict)
        else None,
        "status": status,
        "qualificationCriteria": qualification_criteria,
        "stageIntegrity": stage_integrity,
        "runtimeMarkerFailures": runtime_marker_failures,
        "runtimeBuildIdFailures": runtime_build_id_failures,
        "requiredMaps": required_maps,
        "minVisibleFps": min_visible_fps,
        "requiredLogMapFailures": required_log_map_failures,
        "memoryThresholds": {
            "minFree": min_free,
            "minLargestFree": min_largest_free,
            "maxUsedDelta": max_used_delta,
        },
        "observedMemoryThresholds": {
            "minLargestFree": min_observed_largest_free,
            "maxUsedDelta": max_observed_used_delta,
        },
        "memoryFailures": memory_failures,
        "holomatchSplitLog": holomatch_split_log,
        "logs": results,
        "observation": observation,
        "visibleFpsRequired": observation["status"] != "pass",
        "visualInspectionRequired": observation["status"] != "pass",
    }
    written_report = write_report_file(report_path, output)
    if written_report:
        output["reportPath"] = written_report

    if args.json:
        print(json.dumps(output, indent=2))
    else:
        print("STEFX production hardware log verification")
        if output["manifestVersion"]:
            print(f"Manifest: {output['manifestVersion']}")
        print(f"Overall: {status}")
        if stage_integrity.get("checked", True):
            print(f"Stage integrity: {stage_integrity.get('status')}")
            for failure in stage_integrity.get("failures", []):
                print(f"  stage failure: {failure}")
            runtime_markers = stage_integrity.get("runtimeMarkers")
            if runtime_markers:
                print("  returned runtime files present: " + ", ".join(runtime_markers))
            for failure in runtime_marker_failures:
                print(f"  stage failure: {failure}")
            for failure in runtime_build_id_failures:
                print(f"  runtime build id failure: {failure}")
        for failure in required_log_map_failures:
            print(f"  required-map failure: {failure}")
        for failure in memory_failures:
            print(f"  memory failure: {failure}")
        if args.require_hm_split_log:
            split_summary = holomatch_split_log.get("summary")
            print(f"Holomatch 4P split log: {holomatch_split_log.get('status')}")
            if isinstance(split_summary, dict):
                print(
                    "  split summary: "
                    f"states={split_summary.get('stateSlots')} "
                    f"cmds={split_summary.get('cmdClients')} "
                    f"bots={split_summary.get('maxBots')} "
                    f"fpsMin={split_summary.get('heartbeatFpsMin')} "
                    f"largestFreeMin="
                    f"{(split_summary.get('memory') or {}).get('largestFreeMinimum') if isinstance(split_summary.get('memory'), dict) else None}"
                )
            for failure in holomatch_split_log.get("failures", []):
                print(f"  split failure: {failure}")
        print()
        for mode in ("sp", "mp"):
            print_mode_summary(results[mode])
            print()
        print(f"Observation: {observation['status']} path={observation['path']}")
        if observation.get("sha256"):
            print(f"  observation bytes={observation.get('bytes')} sha256={observation.get('sha256')}")
        evidence_files = observation.get("evidenceFiles")
        if isinstance(evidence_files, dict):
            for mode, records in evidence_files.items():
                if not records:
                    continue
                for record in records:
                    print(
                        f"  {mode} evidence {record.get('status')}: "
                        f"{record.get('path')} bytes={record.get('bytes')} "
                        f"sha256={record.get('sha256')}"
                    )
        for failure in observation.get("failures", []):
            print(f"  observation failure: {failure}")
        if observation["status"] != "pass":
            print("Note: final qualification still needs filled visible FPS/control/visual observations.")
        if written_report:
            print(f"Report written: {written_report}")

    return 0 if status == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
