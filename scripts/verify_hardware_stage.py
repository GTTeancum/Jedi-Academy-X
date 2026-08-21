#!/usr/bin/env python3
"""Preflight-check the PK3-only retail Xbox hardware transfer stage."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


DEFAULT_STAGE = Path("build/hardware/StarTrekEliteForceX-Beta-20260801")
DEFAULT_CFG = Path(r"C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\BaseEF\default.cfg")
DEFAULT_CFG_SHA256 = "3C5B05EBF8B732E1D1065A2337BE63E421560E42C2B4E9CFB28A843F9AB38493"
RUNTIME_MARKER_NAMES = {
    "ef_sp_log.txt",
    "ef_mp_log.txt",
    "ef_runtime_commands.txt",
    "ef_runtime_commands.done",
    "ef_sp_level.txt",
    "ef_sp_commands.txt",
}
OBSERVATION_FILE = "HARDWARE_OBSERVATION.json"
REPORT_TYPE = "stefx-hardware-stage-preflight"
REPORT_SCHEMA_VERSION = 12
OBSERVATION_SCHEMA_VERSION = 3
BUILD_SCRIPT_CONTRACT_VERIFIER = Path("scripts/verify_build_xbox_contracts.py")
BUILD_SCRIPT_UNDER_TEST = Path("scripts/build_xbox.ps1")
PACKAGE_FRESHNESS_INPUTS = (
    Path("scripts/build_xbox.ps1"),
    Path("scripts/build_xbox_patch_pk3.py"),
    Path("scripts/check_mp_holomatch_ui.py"),
)
RUNTIME_SOURCE_EXTENSIONS = {
    ".asm",
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hpp",
    ".inl",
    ".rc",
    ".vcproj",
    ".vsh",
    ".psh",
}
SOURCE_FRESHNESS_SAMPLE_LIMIT = 24
RUNTIME_BUILD_ID_MARKER = b"STEFX_RUNTIME_BUILD_ID "
OBSERVATION_REQUIRED_FIELDS = {
    "sp": (
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
    ),
    "coop": (
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
        "splitScreenOk",
        "p2HudOk",
        "p2ControlsOk",
        "noUnrecoveredStall",
        "notes",
    ),
    "mp": (
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
        "botsOrCombatOk",
        "noUnrecoveredStall",
        "notes",
    ),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def resolve_repo_path(repo_root: Path, path: Path) -> Path:
    return path if path.is_absolute() else repo_root / path


def read_manifest(stage: Path) -> dict[str, object]:
    path = stage / "HARDWARE_PATCH_MANIFEST.json"
    if not path.is_file():
        raise FileNotFoundError(f"missing hardware patch manifest: {path}")
    return json.loads(path.read_text(encoding="utf-8-sig"))


def parse_manifest_utc(value: object) -> datetime | None:
    if not isinstance(value, str) or not value:
        return None
    try:
        normalized = value[:-1] + "+00:00" if value.endswith("Z") else value
        parsed = datetime.fromisoformat(normalized)
    except ValueError:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)


def manifest_path(stage: Path) -> Path:
    return stage / "HARDWARE_PATCH_MANIFEST.json"


def single_byte_media_patch(raw_path: Path, staged_path: Path, offset: int) -> bool:
    raw = raw_path.read_bytes()
    staged = staged_path.read_bytes()
    if len(raw) != len(staged) or offset < 0 or offset >= len(raw):
        return False
    differences = [index for index, (left, right) in enumerate(zip(raw, staged)) if left != right]
    return differences == [offset] and raw[offset] == 0x7D and staged[offset] == 0xEB


def runtime_build_id(path: Path) -> str | None:
    data = path.read_bytes()
    offset = data.find(RUNTIME_BUILD_ID_MARKER)
    if offset < 0:
        return None
    end = offset
    max_end = min(len(data), offset + 256)
    while end < max_end and data[end] not in (0, 10, 13):
        end += 1
    if end <= offset:
        return None
    return data[offset:end].decode("ascii", errors="ignore")


def expected_runtime_build_id_fragments(rel: str, frame_diagnostics: object) -> list[str]:
    flavor = "frame-diagnostics" if frame_diagnostics is True else "production"
    basename = Path(rel).name.lower()
    if basename == "default.xbe":
        return ["personality=default", f"flavor={flavor}", "log=ef_sp_log.txt"]
    if basename == "efmp.xbe":
        return ["personality=efmp", f"flavor={flavor}", "log=ef_mp_log.txt"]
    return [f"flavor={flavor}"]


def runtime_build_id_identity_failures(
    rel: str,
    build_id: str | None,
    frame_diagnostics: object,
    label: str,
) -> list[str]:
    if not build_id:
        return []
    failures: list[str] = []
    for fragment in expected_runtime_build_id_fragments(rel, frame_diagnostics):
        if fragment not in build_id:
            failures.append(
                f"{rel}: {label} runtimeBuildId has wrong identity; "
                f"missing {fragment!r}"
            )
    return failures


def holomatch_checker_command(repo_root: Path, stage: Path, direct_map: str) -> list[str]:
    checker = repo_root / "scripts" / "check_mp_holomatch_ui.py"
    return [
        sys.executable,
        str(checker),
        "--repo-root",
        str(repo_root),
        "--pk3",
        str(stage / "BaseEF" / "xbox1.pk3"),
        "--stage-baseef",
        str(stage / "BaseEF"),
        "--xbe",
        str(stage / "efmp.xbe"),
        "--direct-map",
        direct_map,
        "--code-only",
    ]


def run_holomatch_checker(repo_root: Path, stage: Path, direct_map: str) -> tuple[bool, str, object | None]:
    command = holomatch_checker_command(repo_root, stage, direct_map)
    result = subprocess.run(command, cwd=repo_root, text=True, capture_output=True)
    output = (result.stdout + result.stderr).strip()
    parsed: object | None = None
    try:
        parsed = json.loads(result.stdout) if result.stdout.strip() else None
    except json.JSONDecodeError:
        parsed = None
    return result.returncode == 0, output, parsed


def package_checker_evidence(parsed: object | None) -> dict[str, object]:
    if not isinstance(parsed, dict):
        return {"parsed": False}
    evidence: dict[str, object] = {"parsed": True}
    stage = parsed.get("stage")
    if isinstance(stage, dict):
        evidence["stagePolicy"] = {
            "stageOriginalImages": stage.get("stageOriginalImages"),
            "stageOriginalImagesAllowed": stage.get("stageOriginalImagesAllowed"),
            "stageLooseMpMapOverrides": stage.get("stageLooseMpMapOverrides"),
            "stageLooseMpMapOverridesAllowed": stage.get("stageLooseMpMapOverridesAllowed"),
            "stageUiScripts": stage.get("stageUiScripts"),
        }
    projects = parsed.get("projects")
    if isinstance(projects, dict):
        evidence["architecture"] = {
            "architecture": projects.get("architecture"),
            "codempDependency": projects.get("codempDependency"),
            "holomatchLaunchMap": projects.get("holomatchLaunchMap"),
            "modeHandoff": projects.get("modeHandoff"),
            "uiOwner": projects.get("uiOwner"),
        }
        layout = projects.get("frontendMapLabelLayout")
        if isinstance(layout, dict):
            evidence["frontendMapLabelLayout"] = layout
    package = parsed.get("package")
    if isinstance(package, dict):
        evidence["package"] = {
            "ddsEntries": package.get("ddsEntries"),
            "ddsFormats": package.get("ddsFormats"),
            "legacyUiEntries": package.get("legacyUiEntries"),
            "optimizedMultiplayerMapCount": package.get("optimizedMultiplayerMapCount"),
            "originalImageEntries": package.get("originalImageEntries"),
            "shaderScriptCount": package.get("shaderScriptCount"),
            "uiScriptCount": package.get("uiScriptCount"),
        }
    return evidence


def package_checker_evidence_failures(evidence: dict[str, object]) -> list[str]:
    failures: list[str] = []
    if evidence.get("parsed") is not True:
        return ["Holomatch package/architecture checker did not return structured JSON evidence"]

    stage_policy = evidence.get("stagePolicy")
    if not isinstance(stage_policy, dict):
        failures.append("Holomatch package/architecture checker did not report stage policy")
    else:
        if stage_policy.get("stageOriginalImagesAllowed") is not False:
            failures.append(
                "Holomatch package/architecture checker allows loose original-image stage fallbacks"
            )
        if stage_policy.get("stageLooseMpMapOverridesAllowed") is not False:
            failures.append(
                "Holomatch package/architecture checker allows loose map-override stage fallbacks"
            )

    architecture = evidence.get("architecture")
    if not isinstance(architecture, dict):
        failures.append("Holomatch package/architecture checker did not report SP-hosted architecture")
    else:
        if architecture.get("architecture") != "sp-hosted-code-only":
            failures.append(
                "Holomatch package/architecture checker did not prove SP-hosted code-only architecture"
            )
        if architecture.get("codempDependency") is not False:
            failures.append("Holomatch package/architecture checker reports a codemp dependency")

    layout = evidence.get("frontendMapLabelLayout")
    if not isinstance(layout, dict) or layout.get("checked") is not True:
        failures.append("Holomatch package/architecture checker did not report frontend map-label layout")

    return failures


def observation_schema_failures(observation: dict[str, object]) -> list[str]:
    failures: list[str] = []
    if observation.get("observationSchemaVersion") != OBSERVATION_SCHEMA_VERSION:
        failures.append(
            "observationSchemaVersion "
            f"{observation.get('observationSchemaVersion')!r} "
            f"does not match required {OBSERVATION_SCHEMA_VERSION}"
        )
    for mode, fields in OBSERVATION_REQUIRED_FIELDS.items():
        record = observation.get(mode)
        if not isinstance(record, dict):
            failures.append(f"{mode}: observation record missing")
            continue
        for field in fields:
            if field not in record:
                failures.append(f"{mode}: missing observation field {field}")
        if "mapsTested" in record and not isinstance(record["mapsTested"], list):
            failures.append(f"{mode}: mapsTested must be a list")
        if "evidenceFiles" in record and not isinstance(record["evidenceFiles"], list):
            failures.append(f"{mode}: evidenceFiles must be a list")
    return failures


def runtime_source_freshness(repo_root: Path, generated_utc: datetime | None) -> dict[str, object]:
    result: dict[str, object] = {
        "checked": False,
        "status": "unknown",
        "generatedUtc": generated_utc.isoformat().replace("+00:00", "Z")
        if generated_utc is not None
        else None,
        "newerRuntimeSourceCount": 0,
        "newerRuntimeSources": [],
    }
    source_root = repo_root / "code"
    if generated_utc is None:
        result["status"] = "missing-stage-time"
        return result
    if not source_root.is_dir():
        result["status"] = "source-root-missing"
        return result

    newer_sources: list[dict[str, object]] = []
    newer_count = 0
    for path in sorted(source_root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in RUNTIME_SOURCE_EXTENSIONS:
            continue
        modified_utc = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
        if modified_utc <= generated_utc:
            continue
        newer_count += 1
        if len(newer_sources) < SOURCE_FRESHNESS_SAMPLE_LIMIT:
            newer_sources.append(
                {
                    "path": path.relative_to(repo_root).as_posix(),
                    "modifiedUtc": modified_utc.isoformat().replace("+00:00", "Z"),
                }
            )

    result["checked"] = True
    result["status"] = "stale-runtime-source" if newer_count else "pass"
    result["newerRuntimeSourceCount"] = newer_count
    result["newerRuntimeSources"] = newer_sources
    result["sampleLimit"] = SOURCE_FRESHNESS_SAMPLE_LIMIT
    return result


def release_artifact_freshness(repo_root: Path, release_root: Path) -> dict[str, object]:
    artifact_paths = [release_root / "default.xbe", release_root / "efmp.xbe"]
    artifact_records: list[dict[str, object]] = []
    artifact_times: list[datetime] = []
    for path in artifact_paths:
        record: dict[str, object] = {
            "path": path.relative_to(repo_root).as_posix()
            if path.is_relative_to(repo_root)
            else str(path),
            "present": path.is_file(),
        }
        if path.is_file():
            modified_utc = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
            record["modifiedUtc"] = modified_utc.isoformat().replace("+00:00", "Z")
            artifact_times.append(modified_utc)
        artifact_records.append(record)

    result: dict[str, object] = {
        "checked": False,
        "status": "unknown",
        "releaseArtifacts": artifact_records,
        "oldestReleaseArtifactUtc": None,
        "newerRuntimeSourceCount": 0,
        "newerRuntimeSources": [],
    }
    source_root = repo_root / "code"
    if not source_root.is_dir():
        result["status"] = "source-root-missing"
        return result
    if len(artifact_times) != len(artifact_paths):
        result["status"] = "release-artifact-missing"
        return result

    oldest_artifact_utc = min(artifact_times)
    result["oldestReleaseArtifactUtc"] = oldest_artifact_utc.isoformat().replace("+00:00", "Z")
    newer_sources: list[dict[str, object]] = []
    newer_count = 0
    for path in sorted(source_root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in RUNTIME_SOURCE_EXTENSIONS:
            continue
        modified_utc = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
        if modified_utc <= oldest_artifact_utc:
            continue
        newer_count += 1
        if len(newer_sources) < SOURCE_FRESHNESS_SAMPLE_LIMIT:
            newer_sources.append(
                {
                    "path": path.relative_to(repo_root).as_posix(),
                    "modifiedUtc": modified_utc.isoformat().replace("+00:00", "Z"),
                }
            )

    result["checked"] = True
    result["status"] = "stale-release-artifacts" if newer_count else "pass"
    result["newerRuntimeSourceCount"] = newer_count
    result["newerRuntimeSources"] = newer_sources
    result["sampleLimit"] = SOURCE_FRESHNESS_SAMPLE_LIMIT
    return result


def package_freshness_inputs(repo_root: Path) -> list[Path]:
    inputs: list[Path] = []
    source_root = repo_root / "code"
    if source_root.is_dir():
        for path in sorted(source_root.rglob("*")):
            if path.is_file() and path.suffix.lower() in RUNTIME_SOURCE_EXTENSIONS:
                inputs.append(path)
    for rel_path in PACKAGE_FRESHNESS_INPUTS:
        path = resolve_repo_path(repo_root, rel_path)
        if path.is_file():
            inputs.append(path)
    return inputs


def release_package_freshness(repo_root: Path, release_root: Path) -> dict[str, object]:
    package_paths = [release_root / "BaseEF" / "xbox0.pk3", release_root / "BaseEF" / "xbox1.pk3"]
    package_records: list[dict[str, object]] = []
    package_times: list[datetime] = []
    for path in package_paths:
        record: dict[str, object] = {
            "path": path.relative_to(repo_root).as_posix()
            if path.is_relative_to(repo_root)
            else str(path),
            "present": path.is_file(),
        }
        if path.is_file():
            modified_utc = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
            record["modifiedUtc"] = modified_utc.isoformat().replace("+00:00", "Z")
            package_times.append(modified_utc)
        package_records.append(record)

    result: dict[str, object] = {
        "checked": False,
        "status": "unknown",
        "releasePackages": package_records,
        "oldestReleasePackageUtc": None,
        "newerPackageInputCount": 0,
        "newerPackageInputs": [],
    }
    if len(package_times) != len(package_paths):
        result["status"] = "release-package-missing"
        return result

    oldest_package_utc = min(package_times)
    result["oldestReleasePackageUtc"] = oldest_package_utc.isoformat().replace("+00:00", "Z")
    newer_inputs: list[dict[str, object]] = []
    newer_count = 0
    for path in package_freshness_inputs(repo_root):
        modified_utc = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
        if modified_utc <= oldest_package_utc:
            continue
        newer_count += 1
        if len(newer_inputs) < SOURCE_FRESHNESS_SAMPLE_LIMIT:
            newer_inputs.append(
                {
                    "path": path.relative_to(repo_root).as_posix()
                    if path.is_relative_to(repo_root)
                    else str(path),
                    "modifiedUtc": modified_utc.isoformat().replace("+00:00", "Z"),
                }
            )

    result["checked"] = True
    result["status"] = "stale-release-packages" if newer_count else "pass"
    result["newerPackageInputCount"] = newer_count
    result["newerPackageInputs"] = newer_inputs
    result["sampleLimit"] = SOURCE_FRESHNESS_SAMPLE_LIMIT
    return result


def expected_manifest_file_record(repo_root: Path, rel_path: Path) -> dict[str, object]:
    path = resolve_repo_path(repo_root, rel_path)
    record: dict[str, object] = {
        "path": str(path),
        "present": path.is_file(),
    }
    if path.is_file():
        record["bytes"] = path.stat().st_size
        record["sha256"] = sha256(path)
    return record


def build_script_contract_status(repo_root: Path, manifest: dict[str, object]) -> dict[str, object]:
    expected_records = {
        "verifier": expected_manifest_file_record(repo_root, BUILD_SCRIPT_CONTRACT_VERIFIER),
        "buildScript": expected_manifest_file_record(repo_root, BUILD_SCRIPT_UNDER_TEST),
    }
    result: dict[str, object] = {
        "checked": True,
        "status": "unknown",
        "expected": expected_records,
        "record": manifest.get("buildScriptContract"),
        "failures": [],
    }
    failures: list[str] = []
    record = manifest.get("buildScriptContract")
    if not isinstance(record, dict):
        failures.append("manifest is missing buildScriptContract")
        result["status"] = "fail"
        result["failures"] = failures
        return result

    if record.get("status") != "pass":
        failures.append(f"buildScriptContract status is not pass: {record.get('status')!r}")
    if record.get("exitCode") != 0:
        failures.append(f"buildScriptContract exitCode is not 0: {record.get('exitCode')!r}")

    for key, expected in expected_records.items():
        actual = record.get(key)
        if not isinstance(actual, dict):
            failures.append(f"buildScriptContract {key} record missing")
            continue
        if expected.get("present") is not True:
            failures.append(f"current {key} file missing: {expected.get('path')}")
            continue
        actual_path = actual.get("path")
        expected_path = expected.get("path")
        if isinstance(actual_path, str) and isinstance(expected_path, str):
            if actual_path.casefold() != expected_path.casefold():
                failures.append(
                    f"buildScriptContract {key} path {actual_path!r} != {expected_path!r}"
                )
        else:
            failures.append(f"buildScriptContract {key} path missing")
        for field in ("bytes", "sha256"):
            if actual.get(field) != expected.get(field):
                failures.append(
                    f"buildScriptContract {key} {field} {actual.get(field)!r} != {expected.get(field)!r}"
                )

    result["status"] = "pass" if not failures else "fail"
    result["failures"] = failures
    return result


def verifier_provenance() -> dict[str, object]:
    script_path = Path(__file__).resolve()
    return {
        "script": str(script_path),
        "scriptSha256": sha256(script_path),
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


def verify_stage(
    repo_root: Path,
    stage: Path,
    direct_map: str,
    default_cfg: Path | None,
    run_package_check: bool,
    allow_runtime_markers: bool = False,
) -> dict[str, object]:
    manifest_file = manifest_path(stage)
    manifest = read_manifest(stage)
    files = manifest.get("files")
    if not isinstance(files, dict):
        raise ValueError("hardware patch manifest has no files object")

    failures: list[str] = []
    warnings: list[str] = []
    verified_files: dict[str, object] = {}
    release_root = repo_root / "build" / "release"

    if manifest.get("frameDiagnostics") is not False:
        failures.append("stage is not a production hotlog-off build (frameDiagnostics must be false)")
    manifest_version = manifest.get("version")
    manifest_generated_utc = parse_manifest_utc(manifest.get("generatedUtc"))
    build_contract = build_script_contract_status(repo_root, manifest)
    if build_contract.get("status") != "pass":
        failures.extend(
            f"buildScriptContract: {failure}"
            for failure in build_contract.get("failures", [])
            if isinstance(failure, str)
        )
    source_freshness = runtime_source_freshness(repo_root, manifest_generated_utc)
    release_freshness = release_artifact_freshness(repo_root, release_root)
    package_freshness = release_package_freshness(repo_root, release_root)
    if source_freshness.get("status") == "stale-runtime-source":
        failures.append(
            "runtime source is newer than the staged hardware manifest; rebuild and restage before transfer"
        )
    elif source_freshness.get("status") != "pass":
        warnings.append(
            "runtime source freshness could not be fully checked: "
            f"{source_freshness.get('status')}"
        )
    if release_freshness.get("status") == "stale-release-artifacts":
        failures.append(
            "runtime source is newer than build/release XBE artifacts; rebuild before staging"
        )
    elif release_freshness.get("status") != "pass":
        warnings.append(
            "release artifact freshness could not be fully checked: "
            f"{release_freshness.get('status')}"
        )
    if package_freshness.get("status") == "stale-release-packages":
        failures.append(
            "runtime or package source is newer than build/release PK3 artifacts; rebuild before staging"
        )
    elif package_freshness.get("status") != "pass":
        warnings.append(
            "release package freshness could not be fully checked: "
            f"{package_freshness.get('status')}"
        )

    for rel, info in files.items():
        if not isinstance(info, dict):
            failures.append(f"{rel}: manifest record is not an object")
            continue
        staged_path = stage / rel.replace("/", "\\")
        source_path = release_root / rel.replace("/", "\\")
        if not staged_path.is_file():
            failures.append(f"{rel}: staged file missing")
            continue
        if not source_path.is_file():
            failures.append(f"{rel}: release source file missing")
            continue

        staged_size = staged_path.stat().st_size
        staged_hash = sha256(staged_path)
        source_hash = sha256(source_path)
        expected_size = int(info.get("bytes", -1))
        expected_hash = str(info.get("sha256", "")).upper()
        expected_source_hash = str(info.get("sourceSha256", "")).upper()

        if staged_size != expected_size:
            failures.append(f"{rel}: staged byte count {staged_size} != manifest {expected_size}")
        if staged_hash != expected_hash:
            failures.append(f"{rel}: staged SHA256 {staged_hash} != manifest {expected_hash}")
        if source_hash != expected_source_hash:
            failures.append(f"{rel}: release SHA256 {source_hash} != manifest source {expected_source_hash}")

        record: dict[str, object] = {
            "stagedSha256": staged_hash,
            "sourceSha256": source_hash,
            "bytes": staged_size,
        }
        if rel.lower().endswith(".xbe"):
            offset = int(info.get("mediaEnablePatchOffset", -1))
            media_patch_ok = single_byte_media_patch(source_path, staged_path, offset)
            manifest_runtime_build_id = info.get("runtimeBuildId")
            source_runtime_build_id = runtime_build_id(source_path)
            staged_runtime_build_id = runtime_build_id(staged_path)
            record["mediaEnablePatchOffset"] = offset
            record["mediaEnablePatchOk"] = media_patch_ok
            record["runtimeBuildId"] = staged_runtime_build_id
            record["sourceRuntimeBuildId"] = source_runtime_build_id
            if not media_patch_ok:
                failures.append(f"{rel}: staged XBE does not contain exactly the expected media patch")
            if not isinstance(manifest_runtime_build_id, str) or not manifest_runtime_build_id:
                failures.append(f"{rel}: manifest is missing runtimeBuildId")
            elif source_runtime_build_id != manifest_runtime_build_id:
                failures.append(f"{rel}: release XBE runtimeBuildId does not match manifest")
            elif staged_runtime_build_id != manifest_runtime_build_id:
                failures.append(f"{rel}: staged XBE runtimeBuildId does not match manifest")
            failures.extend(
                runtime_build_id_identity_failures(
                    rel,
                    source_runtime_build_id,
                    manifest.get("frameDiagnostics"),
                    "release XBE",
                )
            )
            failures.extend(
                runtime_build_id_identity_failures(
                    rel,
                    staged_runtime_build_id,
                    manifest.get("frameDiagnostics"),
                    "staged XBE",
                )
            )
            failures.extend(
                runtime_build_id_identity_failures(
                    rel,
                    manifest_runtime_build_id
                    if isinstance(manifest_runtime_build_id, str)
                    else None,
                    manifest.get("frameDiagnostics"),
                    "manifest",
                )
            )
        elif staged_hash != source_hash:
            failures.append(f"{rel}: staged non-XBE differs from release source")

        verified_files[rel] = record

    stale_runtime = sorted(path.name for path in stage.iterdir() if path.name in RUNTIME_MARKER_NAMES)
    if stale_runtime and not allow_runtime_markers:
        failures.append("stage contains stale runtime marker/log file(s): " + ", ".join(stale_runtime))
    if (stage / "HARDWARE_STAGE_MANIFEST.json").exists():
        failures.append("stage still contains superseded HARDWARE_STAGE_MANIFEST.json")

    baseef = stage / "BaseEF"
    extra_dirs = sorted(path.name for path in baseef.iterdir() if path.is_dir() and path.name != "soundbank")
    if extra_dirs:
        failures.append("stage BaseEF contains loose asset directorie(s): " + ", ".join(extra_dirs))
    if list(baseef.rglob("*.tga")) or list(baseef.rglob("*.jpg")) or list(baseef.rglob("*.png")):
        failures.append("stage BaseEF contains loose original image fallback(s)")

    observation_result: dict[str, object] = {"checked": False}
    observation_path = stage / OBSERVATION_FILE
    if observation_path.is_file():
        try:
            observation = json.loads(observation_path.read_text(encoding="utf-8-sig"))
            observation_version = observation.get("manifestVersion")
            matches_manifest = observation_version == manifest_version
            schema_failures = observation_schema_failures(observation)
            observation_result = {
                "checked": True,
                "path": str(observation_path),
                "bytes": observation_path.stat().st_size,
                "sha256": sha256(observation_path),
                "manifestVersion": observation_version,
                "observationSchemaVersion": observation.get("observationSchemaVersion"),
                "matchesManifest": matches_manifest,
                "schemaOk": not schema_failures,
                "schemaFailures": schema_failures,
            }
            if not matches_manifest:
                failures.append(
                    f"{OBSERVATION_FILE}: manifestVersion {observation_version!r} "
                    f"does not match stage {manifest_version!r}"
                )
            failures.extend(f"{OBSERVATION_FILE}: {failure}" for failure in schema_failures)
        except (OSError, json.JSONDecodeError) as exc:
            observation_result = {
                "checked": True,
                "path": str(observation_path),
                "bytes": observation_path.stat().st_size if observation_path.is_file() else None,
                "sha256": sha256(observation_path) if observation_path.is_file() else None,
                "error": str(exc),
            }
            failures.append(f"{OBSERVATION_FILE}: cannot parse observation template")
    elif manifest.get("frameDiagnostics") is False:
        warnings.append(f"{OBSERVATION_FILE} not found; create it before final hardware evidence capture")

    default_cfg_result: dict[str, object] = {"checked": False}
    if default_cfg is not None:
        if default_cfg.is_file():
            cfg_hash = sha256(default_cfg)
            default_cfg_result = {
                "checked": True,
                "path": str(default_cfg),
                "sha256": cfg_hash,
                "matchesCanonical": cfg_hash == DEFAULT_CFG_SHA256,
            }
            if cfg_hash != DEFAULT_CFG_SHA256:
                failures.append("canonical default.cfg hash mismatch")
        else:
            warnings.append(f"canonical default.cfg not found: {default_cfg}")

    package_check_result: dict[str, object] = {"checked": False}
    if run_package_check:
        ok, output, parsed_output = run_holomatch_checker(repo_root, stage, direct_map)
        package_evidence = package_checker_evidence(parsed_output)
        package_policy_failures = package_checker_evidence_failures(package_evidence)
        package_check_result = {
            "checked": True,
            "ok": ok,
            "evidence": package_evidence,
            "outputTail": output[-4000:],
        }
        if not ok:
            failures.append("Holomatch package/architecture checker failed")
        failures.extend(package_policy_failures)

    return {
        "stage": str(stage),
        "manifestVersion": manifest.get("version"),
        "manifest": {
            "path": str(manifest_file),
            "bytes": manifest_file.stat().st_size,
            "sha256": sha256(manifest_file),
        },
        "status": "pass" if not failures else "fail",
        "files": verified_files,
        "runtimeMarkers": stale_runtime,
        "sourceFreshness": source_freshness,
        "releaseArtifactFreshness": release_freshness,
        "releasePackageFreshness": package_freshness,
        "buildScriptContract": build_contract,
        "observation": observation_result,
        "defaultCfg": default_cfg_result,
        "packageCheck": package_check_result,
        "warnings": warnings,
        "failures": failures,
    }


def print_summary(result: dict[str, object]) -> None:
    print("STEFX hardware stage preflight")
    print(f"Stage: {result['stage']}")
    print(f"Manifest: {result.get('manifestVersion')}")
    manifest = result.get("manifest")
    if isinstance(manifest, dict):
        print(
            f"Manifest file: bytes={manifest.get('bytes')} "
            f"sha256={manifest.get('sha256')} path={manifest.get('path')}"
        )
    print(f"Overall: {result['status']}")
    freshness = result.get("sourceFreshness")
    if isinstance(freshness, dict) and freshness.get("checked"):
        print(
            "manifest source freshness: "
            f"{freshness.get('status')} newer={freshness.get('newerRuntimeSourceCount')}"
        )
        for record in freshness.get("newerRuntimeSources", [])[:5]:
            if isinstance(record, dict):
                print(
                    "  newer source: "
                    f"{record.get('path')} modified={record.get('modifiedUtc')}"
                )
    release_freshness = result.get("releaseArtifactFreshness")
    if isinstance(release_freshness, dict) and release_freshness.get("checked"):
        print(
            "release artifact freshness: "
            f"{release_freshness.get('status')} newer={release_freshness.get('newerRuntimeSourceCount')}"
        )
        for record in release_freshness.get("newerRuntimeSources", [])[:5]:
            if isinstance(record, dict):
                print(
                    "  newer than release: "
                    f"{record.get('path')} modified={record.get('modifiedUtc')}"
                )
    print()
    for rel, info in result["files"].items():
        print(f"{rel}:")
        print(f"  bytes={info['bytes']} staged={info['stagedSha256']}")
        if rel.lower().endswith(".xbe"):
            print(
                "  media patch: "
                f"offset={info.get('mediaEnablePatchOffset')} ok={info.get('mediaEnablePatchOk')}"
            )
    observation = result.get("observation")
    if isinstance(observation, dict) and observation.get("checked"):
        print()
        print(
            f"{OBSERVATION_FILE} version ok={observation.get('matchesManifest')} "
            f"schema ok={observation.get('schemaOk')} "
            f"bytes={observation.get('bytes')} "
            f"sha256={observation.get('sha256')} "
            f"path={observation.get('path')}"
        )
        if observation.get("error"):
            print(f"{OBSERVATION_FILE} error={observation.get('error')}")
    cfg = result.get("defaultCfg")
    if isinstance(cfg, dict) and cfg.get("checked"):
        print()
        print(f"default.cfg canonical hash ok={cfg.get('matchesCanonical')} path={cfg.get('path')}")
    package_check = result.get("packageCheck")
    if isinstance(package_check, dict) and package_check.get("checked"):
        print(f"Holomatch package checker ok={package_check.get('ok')}")
    for warning in result["warnings"]:
        print(f"warning: {warning}")
    for failure in result["failures"]:
        print(f"failure: {failure}")


def write_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def make_observation_template(manifest_version: str) -> dict[str, object]:
    return {
        "manifestVersion": manifest_version,
        "observationSchemaVersion": OBSERVATION_SCHEMA_VERSION,
        "notes": "self-test",
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


def make_self_test_stage(root: Path) -> Path:
    release = root / "build" / "release"
    stage = root / "build" / "hardware" / "stage"
    runtime_ids = {
        "default.xbe": "STEFX_RUNTIME_BUILD_ID personality=default flavor=production date=Aug 19 2026 time=10:00:00 log=ef_sp_log.txt",
        "efmp.xbe": "STEFX_RUNTIME_BUILD_ID personality=efmp flavor=production date=Aug 19 2026 time=10:01:00 log=ef_mp_log.txt",
    }
    raw_default = bytearray(b"default-self-test-xbe\0" + runtime_ids["default.xbe"].encode("ascii") + b"\0")
    raw_efmp = bytearray(b"efmp-self-test-xbe\0" + runtime_ids["efmp.xbe"].encode("ascii") + b"\0")
    raw_default[3] = 0x7D
    raw_efmp[4] = 0x7D
    staged_default = bytearray(raw_default)
    staged_efmp = bytearray(raw_efmp)
    staged_default[3] = 0xEB
    staged_efmp[4] = 0xEB
    xbox0 = b"xbox0-pk3-self-test"
    xbox1 = b"xbox1-pk3-self-test"

    payloads = {
        "default.xbe": (bytes(raw_default), bytes(staged_default), 3),
        "efmp.xbe": (bytes(raw_efmp), bytes(staged_efmp), 4),
        "BaseEF/xbox0.pk3": (xbox0, xbox0, None),
        "BaseEF/xbox1.pk3": (xbox1, xbox1, None),
    }
    files: dict[str, object] = {}
    release_timestamp = datetime(2026, 8, 19, 0, 1, tzinfo=timezone.utc).timestamp()
    for rel, (source_data, staged_data, patch_offset) in payloads.items():
        release_path = release / rel.replace("/", "\\")
        write_bytes(release_path, source_data)
        os.utime(release_path, (release_timestamp, release_timestamp))
        write_bytes(stage / rel.replace("/", "\\"), staged_data)
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
    os.utime(build_script, (release_timestamp, release_timestamp))
    os.utime(contract_verifier, (release_timestamp, release_timestamp))
    package_timestamp = datetime(2026, 8, 19, 0, 2, tzinfo=timezone.utc).timestamp()
    for rel in ("BaseEF/xbox0.pk3", "BaseEF/xbox1.pk3"):
        package_path = release / rel.replace("/", "\\")
        os.utime(package_path, (package_timestamp, package_timestamp))
    build_contract = {
        "status": "pass",
        "verifier": expected_manifest_file_record(root, BUILD_SCRIPT_CONTRACT_VERIFIER),
        "buildScript": expected_manifest_file_record(root, BUILD_SCRIPT_UNDER_TEST),
        "exitCode": 0,
        "stdout": "build_xbox contract verification passed",
        "stderr": "",
    }
    manifest = {
        "name": "self-test",
        "version": "self-test",
        "generatedUtc": "2026-08-19T00:00:00Z",
        "frameDiagnostics": False,
        "buildScriptContract": build_contract,
        "files": files,
    }
    (stage / "HARDWARE_PATCH_MANIFEST.json").write_text(
        "\ufeff" + json.dumps(manifest, indent=2), encoding="utf-8"
    )
    (stage / OBSERVATION_FILE).write_text(
        json.dumps(make_observation_template("self-test"), indent=2), encoding="utf-8"
    )
    return stage


def assert_self_test_failure(result: dict[str, object], expected_fragment: str) -> bool:
    return result["status"] == "fail" and any(
        expected_fragment in failure for failure in result["failures"]
    )


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="stefx_stage_test_") as tmp_name:
        root = Path(tmp_name)
        stage = make_self_test_stage(root)

        pass_result = verify_stage(
            repo_root=root,
            stage=stage,
            direct_map="hm_borg1",
            default_cfg=None,
            run_package_check=False,
        )
        if pass_result["status"] != "pass":
            print("self-test failed: clean synthetic stage did not pass", file=sys.stderr)
            print(json.dumps(pass_result, indent=2), file=sys.stderr)
            return 1
        contract_record = pass_result.get("buildScriptContract")
        if not isinstance(contract_record, dict) or contract_record.get("status") != "pass":
            print("self-test failed: clean synthetic stage did not report passing build contract", file=sys.stderr)
            print(json.dumps(contract_record, indent=2), file=sys.stderr)
            return 1
        package_freshness = pass_result.get("releasePackageFreshness")
        if not isinstance(package_freshness, dict) or package_freshness.get("status") != "pass":
            print("self-test failed: clean synthetic stage did not report fresh release packages", file=sys.stderr)
            print(json.dumps(package_freshness, indent=2), file=sys.stderr)
            return 1
        manifest_path_for_contract = manifest_path(stage)
        manifest_for_contract = json.loads(manifest_path_for_contract.read_text(encoding="utf-8-sig"))
        manifest_for_contract.pop("buildScriptContract", None)
        manifest_path_for_contract.write_text(
            "\ufeff" + json.dumps(manifest_for_contract, indent=2), encoding="utf-8"
        )
        missing_contract_result = verify_stage(
            repo_root=root,
            stage=stage,
            direct_map="hm_borg1",
            default_cfg=None,
            run_package_check=False,
        )
        if not assert_self_test_failure(missing_contract_result, "buildScriptContract"):
            print("self-test failed: missing build script contract was not rejected", file=sys.stderr)
            print(json.dumps(missing_contract_result, indent=2), file=sys.stderr)
            return 1
        stage = make_self_test_stage(root)
        (root / "scripts" / "build_xbox.ps1").write_text(
            "# changed after staging\n", encoding="utf-8"
        )
        stale_contract_result = verify_stage(
            repo_root=root,
            stage=stage,
            direct_map="hm_borg1",
            default_cfg=None,
            run_package_check=False,
        )
        if not assert_self_test_failure(stale_contract_result, "buildScriptContract"):
            print("self-test failed: stale build script contract was not rejected", file=sys.stderr)
            print(json.dumps(stale_contract_result, indent=2), file=sys.stderr)
            return 1
        stage = make_self_test_stage(root)
        stale_package_path = root / "build" / "release" / "BaseEF" / "xbox0.pk3"
        stale_package_time = datetime(2026, 8, 19, 0, 0, tzinfo=timezone.utc).timestamp()
        os.utime(stale_package_path, (stale_package_time, stale_package_time))
        stale_package_result = verify_stage(
            repo_root=root,
            stage=stage,
            direct_map="hm_borg1",
            default_cfg=None,
            run_package_check=False,
        )
        if not assert_self_test_failure(stale_package_result, "build/release PK3 artifacts"):
            print("self-test failed: stale release package was not rejected", file=sys.stderr)
            print(json.dumps(stale_package_result, indent=2), file=sys.stderr)
            return 1
        stage = make_self_test_stage(root)
        manifest_path_for_identity = manifest_path(stage)
        manifest_for_identity = json.loads(manifest_path_for_identity.read_text(encoding="utf-8-sig"))
        wrong_default_id = (
            "STEFX_RUNTIME_BUILD_ID personality=efmp flavor=production "
            "date=Aug 19 2026 time=10:02:00 log=ef_mp_log.txt"
        )
        manifest_for_identity["files"]["default.xbe"]["runtimeBuildId"] = wrong_default_id
        (root / "build" / "release" / "default.xbe").write_bytes(
            bytearray(b"abc}def\0" + wrong_default_id.encode("ascii") + b"\0")
        )
        (stage / "default.xbe").write_bytes(
            bytearray(b"abc\xebdef\0" + wrong_default_id.encode("ascii") + b"\0")
        )
        manifest_for_identity["files"]["default.xbe"]["sourceSha256"] = sha256(
            root / "build" / "release" / "default.xbe"
        )
        manifest_for_identity["files"]["default.xbe"]["sha256"] = sha256(stage / "default.xbe")
        manifest_for_identity["files"]["default.xbe"]["bytes"] = (stage / "default.xbe").stat().st_size
        manifest_path_for_identity.write_text(
            "\ufeff" + json.dumps(manifest_for_identity, indent=2), encoding="utf-8"
        )
        wrong_identity_result = verify_stage(
            repo_root=root,
            stage=stage,
            direct_map="hm_borg1",
            default_cfg=None,
            run_package_check=False,
        )
        if not assert_self_test_failure(wrong_identity_result, "wrong identity"):
            print("self-test failed: wrong default.xbe runtime identity was not rejected", file=sys.stderr)
            print(json.dumps(wrong_identity_result, indent=2), file=sys.stderr)
            return 1
        stage = make_self_test_stage(root)
        checker_command = holomatch_checker_command(root, stage, "hm_borg1")
        if "--allow-stage-original-images" in checker_command or "--allow-stage-map-overrides" in checker_command:
            print(
                "self-test failed: hardware preflight checker command allows loose stage assets",
                file=sys.stderr,
            )
            print(json.dumps(checker_command, indent=2), file=sys.stderr)
            return 1
        checker_evidence = package_checker_evidence(
            {
                "stage": {
                    "stageOriginalImages": 0,
                    "stageOriginalImagesAllowed": False,
                    "stageLooseMpMapOverrides": 0,
                    "stageLooseMpMapOverridesAllowed": False,
                    "stageUiScripts": 0,
                },
                "projects": {
                    "architecture": "sp-hosted-code-only",
                    "codempDependency": False,
                    "holomatchLaunchMap": "hm_borg1",
                    "modeHandoff": "default.xbe <-> efmp.xbe",
                    "uiOwner": "code/ui SP framework",
                    "frontendMapLabelLayout": {"checked": True, "xGaps": [198.0, 198.0, 198.0]},
                },
                "package": {
                    "ddsEntries": 1,
                    "ddsFormats": {"DXT1": 1},
                    "legacyUiEntries": 0,
                    "optimizedMultiplayerMapCount": 1,
                    "originalImageEntries": 0,
                    "shaderScriptCount": 1,
                    "uiScriptCount": 0,
                },
            }
        )
        if package_checker_evidence_failures(checker_evidence):
            print("self-test failed: valid package checker evidence was rejected", file=sys.stderr)
            print(json.dumps(checker_evidence, indent=2), file=sys.stderr)
            return 1
        bad_checker_evidence = dict(checker_evidence)
        bad_checker_evidence["stagePolicy"] = dict(checker_evidence["stagePolicy"])
        bad_checker_evidence["stagePolicy"]["stageOriginalImagesAllowed"] = True
        if not any(
            "loose original-image" in failure
            for failure in package_checker_evidence_failures(bad_checker_evidence)
        ):
            print("self-test failed: loose original-image package policy was not rejected", file=sys.stderr)
            print(json.dumps(bad_checker_evidence, indent=2), file=sys.stderr)
            return 1
        if not any(
            "structured JSON" in failure for failure in package_checker_evidence_failures({"parsed": False})
        ):
            print("self-test failed: unstructured package checker evidence was not rejected", file=sys.stderr)
            return 1
        manifest_result = pass_result.get("manifest")
        manifest_file = stage / "HARDWARE_PATCH_MANIFEST.json"
        if (
            not isinstance(manifest_result, dict)
            or manifest_result.get("path") != str(manifest_file)
            or manifest_result.get("bytes") != manifest_file.stat().st_size
            or manifest_result.get("sha256") != sha256(manifest_file)
        ):
            print("self-test failed: manifest provenance was not reported", file=sys.stderr)
            print(json.dumps(pass_result, indent=2), file=sys.stderr)
            return 1
        observation_provenance = pass_result.get("observation")
        observation_file = stage / OBSERVATION_FILE
        if (
            not isinstance(observation_provenance, dict)
            or observation_provenance.get("path") != str(observation_file)
            or observation_provenance.get("bytes") != observation_file.stat().st_size
            or observation_provenance.get("sha256") != sha256(observation_file)
        ):
            print("self-test failed: observation provenance was not reported", file=sys.stderr)
            print(json.dumps(pass_result, indent=2), file=sys.stderr)
            return 1
        report_path = stage / "stage_preflight_report.json"
        written_report = write_report_file(report_path, pass_result)
        if written_report != str(report_path.resolve()):
            print("self-test failed: stage report path was not returned", file=sys.stderr)
            return 1
        report = json.loads(report_path.read_text(encoding="utf-8"))
        provenance = report.get("verifierProvenance")
        if (
            report.get("reportType") != REPORT_TYPE
            or report.get("reportSchemaVersion") != REPORT_SCHEMA_VERSION
            or report.get("reportPath") != str(report_path.resolve())
            or "generatedAtUtc" not in report
            or not isinstance(provenance, dict)
            or provenance.get("scriptSha256") != sha256(Path(__file__).resolve())
            or "--self-test" not in provenance.get("argv", [])
        ):
            print("self-test failed: stage report metadata was not preserved", file=sys.stderr)
            print(json.dumps(report, indent=2), file=sys.stderr)
            return 1

        (stage / "ef_sp_log.txt").write_text("stale", encoding="utf-8")
        stale_result = verify_stage(root, stage, "hm_borg1", None, False)
        if not assert_self_test_failure(stale_result, "stale runtime marker"):
            print("self-test failed: stale runtime log was not rejected", file=sys.stderr)
            print(json.dumps(stale_result, indent=2), file=sys.stderr)
            return 1
        (stage / "ef_sp_log.txt").unlink()

        default_xbe = stage / "default.xbe"
        data = bytearray(default_xbe.read_bytes())
        data[5] ^= 0x01
        default_xbe.write_bytes(data)
        patch_result = verify_stage(root, stage, "hm_borg1", None, False)
        if not assert_self_test_failure(patch_result, "expected media patch"):
            print("self-test failed: bad media patch was not rejected", file=sys.stderr)
            print(json.dumps(patch_result, indent=2), file=sys.stderr)
            return 1

        stage = make_self_test_stage(root)
        xbox1 = stage / "BaseEF" / "xbox1.pk3"
        xbox1.write_bytes(xbox1.read_bytes() + b"changed")
        hash_result = verify_stage(root, stage, "hm_borg1", None, False)
        if not assert_self_test_failure(hash_result, "staged SHA256"):
            print("self-test failed: staged PK3 hash mismatch was not rejected", file=sys.stderr)
            print(json.dumps(hash_result, indent=2), file=sys.stderr)
            return 1

        stage = make_self_test_stage(root)
        observation = json.loads((stage / OBSERVATION_FILE).read_text(encoding="utf-8"))
        observation["manifestVersion"] = "older-stage"
        (stage / OBSERVATION_FILE).write_text(json.dumps(observation, indent=2), encoding="utf-8")
        observation_result = verify_stage(root, stage, "hm_borg1", None, False)
        if not assert_self_test_failure(observation_result, "manifestVersion"):
            print("self-test failed: stale observation manifest was not rejected", file=sys.stderr)
            print(json.dumps(observation_result, indent=2), file=sys.stderr)
            return 1

        stage = make_self_test_stage(root)
        observation = json.loads((stage / OBSERVATION_FILE).read_text(encoding="utf-8"))
        del observation["sp"]["mapsTested"]
        (stage / OBSERVATION_FILE).write_text(json.dumps(observation, indent=2), encoding="utf-8")
        schema_result = verify_stage(root, stage, "hm_borg1", None, False)
        if not assert_self_test_failure(schema_result, "missing observation field mapsTested"):
            print("self-test failed: stale observation schema was not rejected", file=sys.stderr)
            print(json.dumps(schema_result, indent=2), file=sys.stderr)
            return 1

        stage = make_self_test_stage(root)
        (stage / "HARDWARE_STAGE_MANIFEST.json").write_text("stale", encoding="utf-8")
        (stage / "BaseEF" / "loose").mkdir()
        (stage / "BaseEF" / "loose" / "bad.tga").write_bytes(b"tga")
        loose_result = verify_stage(root, stage, "hm_borg1", None, False)
        expected = (
            "superseded HARDWARE_STAGE_MANIFEST",
            "loose asset directorie",
            "loose original image",
        )
        if loose_result["status"] != "fail" or not all(
            any(fragment in failure for failure in loose_result["failures"])
            for fragment in expected
        ):
            print("self-test failed: loose/stale stage artifacts were not rejected", file=sys.stderr)
            print(json.dumps(loose_result, indent=2), file=sys.stderr)
            return 1

    with tempfile.TemporaryDirectory(prefix="stefx_stage_stale_source_test_") as tmp_name:
        root = Path(tmp_name)
        stage = make_self_test_stage(root)
        stale_source = root / "code" / "ui" / "ui_ef_frontend.cpp"
        stale_source.parent.mkdir(parents=True, exist_ok=True)
        stale_source.write_text("// newer than staged manifest\n", encoding="utf-8")
        newer_timestamp = datetime(2026, 8, 20, tzinfo=timezone.utc).timestamp()
        os.utime(stale_source, (newer_timestamp, newer_timestamp))
        source_result = verify_stage(root, stage, "hm_borg1", None, False)
        source_freshness = source_result.get("sourceFreshness")
        release_freshness = source_result.get("releaseArtifactFreshness")
        if (
            not assert_self_test_failure(source_result, "runtime source is newer")
            or not isinstance(source_freshness, dict)
            or source_freshness.get("status") != "stale-runtime-source"
            or source_freshness.get("newerRuntimeSourceCount") != 1
            or not isinstance(release_freshness, dict)
            or release_freshness.get("status") != "stale-release-artifacts"
            or release_freshness.get("newerRuntimeSourceCount") != 1
        ):
            print("self-test failed: newer runtime source was not rejected", file=sys.stderr)
            print(json.dumps(source_result, indent=2), file=sys.stderr)
            return 1

    print("verify_hardware_stage self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Preflight-check the current hardware transfer stage.")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--stage", type=Path, default=DEFAULT_STAGE)
    parser.add_argument("--direct-map", default="hm_borg1")
    parser.add_argument("--default-cfg", type=Path, default=DEFAULT_CFG)
    parser.add_argument("--skip-default-cfg", action="store_true")
    parser.add_argument("--skip-package-check", action="store_true")
    parser.add_argument("--report-out", type=Path, help="Write the JSON preflight result to this file.")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()

    repo_root = args.repo_root.resolve()
    stage = args.stage if args.stage.is_absolute() else repo_root / args.stage
    default_cfg = None if args.skip_default_cfg else args.default_cfg
    result = verify_stage(
        repo_root=repo_root,
        stage=stage,
        direct_map=args.direct_map,
        default_cfg=default_cfg,
        run_package_check=not args.skip_package_check,
    )
    report_path = args.report_out
    if report_path is not None and not report_path.is_absolute():
        report_path = repo_root / report_path
    written_report = write_report_file(report_path, result)
    if written_report:
        result["reportPath"] = written_report
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print_summary(result)
        if written_report:
            print(f"Report written: {written_report}")
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
