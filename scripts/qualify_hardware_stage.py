#!/usr/bin/env python3
"""Run the current hardware-stage and production-log qualification checks."""

from __future__ import annotations

import argparse
import ast
from datetime import datetime, timezone
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


REPORT_TYPE = "stefx-hardware-qualification-audit"
REPORT_SCHEMA_VERSION = 63
EXPECTED_STAGE_REPORT_TYPE = "stefx-hardware-stage-preflight"
EXPECTED_STAGE_REPORT_SCHEMA_VERSION = 12
EXPECTED_PRODUCTION_REPORT_TYPE = "stefx-production-hardware-log-verification"
EXPECTED_PRODUCTION_REPORT_SCHEMA_VERSION = 19
EXPECTED_XEMU_REFRESH_REPORT_TYPE = "stefx-xemu-qualification-proof-refresh"
EXPECTED_XEMU_REFRESH_REPORT_SCHEMA_VERSION = 3
EXPECTED_OBJECT_COMPARE_REPORT_TYPE = "stefx-retail-renderer-object-compare"
EXPECTED_OBJECT_COMPARE_REPORT_SCHEMA_VERSION = 2
DEFAULT_MIN_EVIDENCE_BYTES = 1024
DEFAULT_MIN_IMAGE_WIDTH = 320
DEFAULT_MIN_IMAGE_HEIGHT = 240
DEFAULT_STAGE = Path("build/hardware/StarTrekEliteForceX-Beta-20260801")
DEFAULT_MIN_ELAPSED_SECONDS = 90.0
DEFAULT_MIN_OBSERVATION_SECONDS = 90.0
OBSERVATION_MODES = ("sp", "coop", "mp")
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
XEMU_FRESHNESS_SAMPLE_LIMIT = 12
OBJECT_COMPARE_VALIDATION_SAMPLE_LIMIT = 8
RUNTIME_BUILD_ID_MARKER = b"STEFX_RUNTIME_BUILD_ID "
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
XEMU_PROOF_HARNESS_FILES = (
    Path("scripts/build_xbox.ps1"),
    Path("scripts/build_xbox_patch_pk3.py"),
    Path("scripts/check_mp_holomatch_ui.py"),
    Path("scripts/ja_xemu_smoke.py"),
    Path("scripts/run_sp_xemu_smoke.ps1"),
    Path("scripts/run_mp_xemu_smoke.ps1"),
    Path("scripts/refresh_xemu_qualification_proof.ps1"),
    Path("scripts/stage_hardware_pk3_test.ps1"),
    Path("scripts/verify_build_xbox_contracts.py"),
)
XEMU_RELEASE_ARTIFACTS = (
    Path("build/release/default.xbe"),
    Path("build/release/efmp.xbe"),
    Path("build/release/BaseEF/xbox0.pk3"),
    Path("build/release/BaseEF/xbox1.pk3"),
)
BUILD_SCRIPT_CONTRACT_VERIFIER = Path("scripts/verify_build_xbox_contracts.py")
BUILD_SCRIPT_UNDER_TEST = Path("scripts/build_xbox.ps1")
EXPECTED_XEMU_REFRESH_CRITERIA = {
    "pollXBlogStartDelay": 15,
    "pollXBlogInterval": 5,
}
RETAIL_RENDERER_CONTRACT_MIN = 0x70000
RETAIL_RENDERER_CONTRACT_MAX = 0xB6300
RETAIL_RENDERER_SOURCE_FILES = (
    Path("code/renderer/retail_xbox/tr_backend_retail.cpp"),
    Path("code/renderer/retail_xbox/tr_cmds_retail.cpp"),
    Path("code/renderer/retail_xbox/tr_light_retail.cpp"),
    Path("code/renderer/retail_xbox/tr_main_retail.cpp"),
    Path("code/renderer/retail_xbox/tr_scene_retail.cpp"),
    Path("code/renderer/retail_xbox/tr_shade_retail.cpp"),
    Path("code/renderer/retail_xbox/tr_shade_calc_retail.cpp"),
    Path("code/renderer/retail_xbox/tr_shader_retail.cpp"),
    Path("code/renderer/retail_xbox/tr_sky_retail.cpp"),
    Path("code/renderer/retail_xbox/tr_surface_retail.cpp"),
    Path("code/renderer/retail_xbox/tr_world_retail.cpp"),
)
RETAIL_RENDERER_REPLACED_LEGACY_SOURCES = (
    Path("code/renderer/tr_backend.cpp"),
    Path("code/renderer/tr_cmds.cpp"),
    Path("code/renderer/tr_light.cpp"),
    Path("code/renderer/tr_main.cpp"),
    Path("code/renderer/tr_scene.cpp"),
    Path("code/renderer/tr_shade.cpp"),
    Path("code/renderer/tr_shade_calc.cpp"),
    Path("code/renderer/tr_shader.cpp"),
    Path("code/renderer/tr_sky.cpp"),
    Path("code/renderer/tr_world.cpp"),
)
RETAIL_OBJECT_COMPARE_FRESHNESS_INPUTS = (
    Path("code/renderer/retail_xbox/retail_renderer_contract.h"),
    *RETAIL_RENDERER_SOURCE_FILES,
    Path("code/x_exe/x_exe.vcproj"),
    Path("scripts/build_xbox.ps1"),
    Path("scripts/compare_retail_renderer_objects.py"),
)
RETAIL_RENDERER_REQUIRED_DEFINES = (
    "FINAL_BUILD",
    "_FINAL",
    "STEFX_ELITE_FORCE_SP",
    "STEFX_RETAIL_RENDERER_ACTIVE",
    "STEFX_RETAIL_SURFACE_ACTIVE",
)
CONTRACT_ARTIFACTS = {
    "retailRendererContracts": Path("notes/ja_mp_retail_renderer_contracts.json"),
    "spObjectCompare": Path("build/analysis/retail-v76-current-sp-object-compare.json"),
    "hmObjectCompare": Path("build/analysis/retail-v76-current-hm-object-compare.json"),
    "abiEnumsState": Path("build/analysis/retail-v45-renderer-abi-enums-state.json"),
    "runtimeStructure": Path("build/analysis/retail-v45-runtime-compare-structure.json"),
}
XEMU_EVIDENCE_RUNS = {
    "spBorg2": {
        "mode": "sp",
        "map": "borg2",
        "report": Path("scripts/output/shared-hotlog-off-sp_borg2_20260819_074748.report.txt"),
        "contact": Path("scripts/output/shared-hotlog-off-sp_borg2_20260819_074748_contact.png"),
        "finalRegisters": Path(
            "scripts/output/shared-hotlog-off-sp_borg2_20260819_074748_final_registers.txt"
        ),
        "minOkShots": 4,
        "minUniqueOkShotByteSizes": 3,
        "minDurationSeconds": 50.0,
        "requireAllocatorStable": False,
    },
    "coopBorg1SplitScreen": {
        "mode": "coop",
        "map": "borg1",
        "report": Path(
            "scripts/output/shared-v77-coop-splitscreen_normal_20260819_162359.report.txt"
        ),
        "contact": Path(
            "scripts/output/shared-v77-coop-splitscreen_normal_20260819_162359_contact.png"
        ),
        "finalRegisters": Path(
            "scripts/output/shared-v77-coop-splitscreen_normal_20260819_162359_final_registers.txt"
        ),
        "minOkShots": 8,
        "minUniqueOkShotByteSizes": 6,
        "minDurationSeconds": 90.0,
        "requireAllocatorStable": False,
        "requireP2Refdef": True,
        "minP2RefdefSamples": 8,
        "requireDualPlayerModelState": True,
        "minDualPlayerModelSamples": 8,
    },
    "hmBorg1Soak": {
        "mode": "mp",
        "map": "hm_borg1",
        "report": Path("scripts/output/hm-v77-botcombat-soak_hm_borg1_20260819_140954.report.txt"),
        "contact": Path("scripts/output/hm-v77-botcombat-soak_hm_borg1_20260819_140954_contact.png"),
        "finalRegisters": Path(
            "scripts/output/hm-v77-botcombat-soak_hm_borg1_20260819_140954_final_registers.txt"
        ),
        "minOkShots": 10,
        "minUniqueOkShotByteSizes": 6,
        "minDurationSeconds": 90.0,
        "requireAllocatorStable": True,
    },
}
SHOT_RE = re.compile(r"^shot=\d+\s+t=([0-9.]+)\s+ok=(True|False)\s+bytes=(\d+)")
KEY_VALUE_PATH_RE = re.compile(r"^(contact|final_registers)=(.+)$")
PROOF_CONTEXT_RE = re.compile(r"^proof_context\s+(.+)$")
RUNTIME_XBE_IDENTITY_RE = re.compile(
    r"^runtime_xbe_identity\s+path=(?P<path>.+?)\s+present=True\s+"
    r"bytes=(?P<bytes>\d+)\s+sha256=(?P<sha256>[0-9A-Fa-f]+)\s+"
    r"runtimeBuildId=(?P<runtimeBuildId>.+)$"
)
XLOGTEX_RE = re.compile(
    r"^xblogtex\s+t=([0-9.]+).*?\bused=(\d+).*?\btotalFree=(\d+).*?\blargestFree=(\d+)"
)
P2_REFDEF_RE = re.compile(r"\bp2dbg=ref=1\b")
DUAL_PLAYER_MODEL_RE = re.compile(r"\bmodel=193/193\b")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


def resolve_path(repo_root: Path, path: Path) -> Path:
    return path if path.is_absolute() else repo_root / path


def read_json(path: Path) -> dict[str, object] | None:
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return None


def read_json_any(path: Path) -> object | None:
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return None


def now_utc() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def parse_utc_datetime(value: object) -> datetime | None:
    if not isinstance(value, str) or not value:
        return None
    try:
        normalized = value[:-1] + "+00:00" if value.endswith("Z") else value
        parsed = datetime.fromisoformat(normalized)
    except ValueError:
        return None
    if parsed.tzinfo is None:
        return parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)


def run_command(command: list[str], cwd: Path) -> dict[str, object]:
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
    parsed_stdout = None
    try:
        parsed_stdout = json.loads(result.stdout) if result.stdout.strip() else None
    except json.JSONDecodeError:
        parsed_stdout = None
    return {
        "command": command,
        "returnCode": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
        "json": parsed_stdout,
    }


def write_report(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def audit_provenance() -> dict[str, object]:
    script_path = Path(__file__).resolve()
    return {
        "script": str(script_path),
        "scriptSha256": file_sha256(script_path),
        "python": sys.executable,
        "cwd": str(Path.cwd()),
        "argv": sys.argv[1:],
    }


def script_literal_constant(path: Path, name: str) -> object:
    try:
        module = ast.parse(path.read_text(encoding="utf-8"))
    except (OSError, SyntaxError):
        return None
    for node in module.body:
        if not isinstance(node, ast.Assign):
            continue
        if not any(isinstance(target, ast.Name) and target.id == name for target in node.targets):
            continue
        try:
            return ast.literal_eval(node.value)
        except (ValueError, SyntaxError):
            return None
    return None


def report_contract_failures(
    stage_result: dict[str, object] | None,
    qualification_result: dict[str, object] | None,
    stage: Path | None = None,
    stage_report: Path | None = None,
    qualification_report: Path | None = None,
    stage_script: Path | None = None,
    qualification_script: Path | None = None,
    stage_argv: list[str] | None = None,
    qualification_argv: list[str] | None = None,
) -> list[str]:
    expected = {
        "stagePreflight": (
            stage_result,
            EXPECTED_STAGE_REPORT_TYPE,
            EXPECTED_STAGE_REPORT_SCHEMA_VERSION,
            stage_report,
            stage_script,
            stage_argv,
        ),
        "productionQualification": (
            qualification_result,
            EXPECTED_PRODUCTION_REPORT_TYPE,
            EXPECTED_PRODUCTION_REPORT_SCHEMA_VERSION,
            qualification_report,
            qualification_script,
            qualification_argv,
        ),
    }
    failures: list[str] = []
    for label, (
        report,
        report_type,
        schema_version,
        expected_path,
        expected_script,
        expected_argv,
    ) in expected.items():
        if not isinstance(report, dict):
            failures.append(f"{label}: report missing or invalid JSON")
            continue
        if report.get("reportType") != report_type:
            failures.append(
                f"{label}: reportType {report.get('reportType')!r} != {report_type!r}"
            )
        if report.get("reportSchemaVersion") != schema_version:
            failures.append(
                f"{label}: reportSchemaVersion {report.get('reportSchemaVersion')!r} "
                f"!= {schema_version}"
            )
        if expected_script is not None:
            script_report_type = script_literal_constant(expected_script, "REPORT_TYPE")
            script_schema_version = script_literal_constant(expected_script, "REPORT_SCHEMA_VERSION")
            if script_report_type != report_type:
                failures.append(
                    f"{label}: audit expected reportType {report_type!r} "
                    f"does not match verifier source {script_report_type!r}"
                )
            if script_schema_version != schema_version:
                failures.append(
                    f"{label}: audit expected reportSchemaVersion {schema_version!r} "
                    f"does not match verifier source {script_schema_version!r}"
                )
        provenance = report.get("verifierProvenance")
        if not isinstance(provenance, dict) or not provenance.get("scriptSha256"):
            failures.append(f"{label}: verifierProvenance.scriptSha256 missing")
        elif expected_script is not None:
            expected_script_path = expected_script.resolve()
            expected_script_sha = file_sha256(expected_script_path)
            if provenance.get("script") != str(expected_script_path):
                failures.append(
                    f"{label}: verifierProvenance.script {provenance.get('script')!r} "
                    f"!= {str(expected_script_path)!r}"
                )
            if provenance.get("scriptSha256") != expected_script_sha:
                failures.append(
                    f"{label}: verifierProvenance.scriptSha256 "
                    f"{provenance.get('scriptSha256')!r} != {expected_script_sha!r}"
                )
        if isinstance(provenance, dict) and expected_argv is not None:
            actual_argv = provenance.get("argv")
            if actual_argv != expected_argv:
                failures.append(f"{label}: verifierProvenance.argv does not match command")
        if expected_path is not None:
            actual_report_path = report.get("reportPath")
            expected_report_path = str(expected_path.resolve())
            if actual_report_path != expected_report_path:
                failures.append(
                    f"{label}: reportPath {actual_report_path!r} != {expected_report_path!r}"
                )
        if stage is not None:
            expected_stage = str(stage.resolve())
            actual_stage = report.get("stage")
            if isinstance(actual_stage, str) and actual_stage.casefold() == expected_stage.casefold():
                pass
            else:
                failures.append(f"{label}: stage {actual_stage!r} != {expected_stage!r}")
    if isinstance(stage_result, dict) and isinstance(qualification_result, dict):
        stage_manifest = stage_result.get("manifest")
        qualification_manifest = qualification_result.get("manifest")
        if not isinstance(stage_manifest, dict):
            failures.append("stagePreflight: manifest provenance missing")
        if not isinstance(qualification_manifest, dict):
            failures.append("productionQualification: manifest provenance missing")
        if isinstance(stage_manifest, dict) and isinstance(qualification_manifest, dict):
            for key in ("path", "bytes", "sha256"):
                stage_value = stage_manifest.get(key)
                qualification_value = qualification_manifest.get(key)
                if key == "path" and isinstance(stage_value, str) and isinstance(qualification_value, str):
                    if stage_value.casefold() == qualification_value.casefold():
                        continue
                if stage_value != qualification_value:
                    failures.append(
                        "stagePreflight/productionQualification manifest "
                        f"{key} mismatch: {stage_value!r} != {qualification_value!r}"
                    )
    return failures


def determine_overall_status(stage_status: object, qualification_status: object) -> str:
    if stage_status == "fail" or qualification_status == "fail":
        return "fail"
    if qualification_status == "missing":
        return "missing-runtime-proof"
    if stage_status == "pass" and qualification_status == "pass":
        return "pass"
    return "incomplete"


def contract_artifact_record(path: Path) -> dict[str, object]:
    record: dict[str, object] = {
        "path": str(path),
        "present": path.is_file(),
    }
    if path.is_file():
        record["bytes"] = path.stat().st_size
        record["sha256"] = file_sha256(path)
        data = read_json_any(path)
        if isinstance(data, dict):
            for key in ("reportType", "reportSchemaVersion", "generatedAtUtc"):
                if key in data:
                    record[key] = data[key]
            if isinstance(data.get("summary"), dict):
                record["summary"] = data["summary"]
            for key in ("field_summary", "enum_summary", "state_summary"):
                if isinstance(data.get(key), dict):
                    record[key] = data[key]
        elif isinstance(data, list):
            record["records"] = len(data)
            record["minConfidence"] = min(
                (
                    float(item.get("confidence", 0.0))
                    for item in data
                    if isinstance(item, dict) and isinstance(item.get("confidence"), (int, float))
                ),
                default=None,
            )
    return record


def parse_hex_int(value: object) -> int | None:
    if not isinstance(value, str) or not value:
        return None
    try:
        return int(value, 16)
    except ValueError:
        return None


def retail_contract_ledger_validation(data: object) -> dict[str, object]:
    failures: list[str] = []
    if not isinstance(data, list):
        return {
            "status": "fail",
            "records": 0,
            "retailEvidenceRecords": 0,
            "failures": ["retail renderer contract ledger is not a JSON list"],
        }

    starts: set[int] = set()
    names: set[str] = set()
    retail_evidence_records = 0
    min_confidence: float | None = None
    for index, item in enumerate(data):
        prefix = f"record {index}"
        if not isinstance(item, dict):
            failures.append(f"{prefix}: not an object")
            continue

        name = item.get("name")
        object_name = item.get("object")
        start = parse_hex_int(item.get("start"))
        end = parse_hex_int(item.get("end"))
        confidence = item.get("confidence")
        evidence = item.get("evidence")
        contract = item.get("contract")

        if not isinstance(name, str) or not name:
            failures.append(f"{prefix}: name missing")
        elif name in names:
            failures.append(f"{prefix}: duplicate name {name!r}")
        else:
            names.add(name)
        if not isinstance(object_name, str) or not object_name.endswith(".obj"):
            failures.append(f"{prefix}: object missing or not an .obj name")
        if start is None or end is None:
            failures.append(f"{prefix}: start/end address missing or invalid")
        else:
            if start in starts:
                failures.append(f"{prefix}: duplicate start address 0x{start:08X}")
            starts.add(start)
            if not (RETAIL_RENDERER_CONTRACT_MIN <= start < end <= RETAIL_RENDERER_CONTRACT_MAX):
                failures.append(
                    f"{prefix}: address range 0x{start:08X}-0x{end:08X} "
                    "is outside the retail renderer contract range"
                )
        if not isinstance(confidence, (int, float)):
            failures.append(f"{prefix}: confidence missing or non-numeric")
        else:
            confidence_value = float(confidence)
            min_confidence = confidence_value if min_confidence is None else min(min_confidence, confidence_value)
            if confidence_value < 0.9:
                failures.append(f"{prefix}: confidence {confidence_value:.2f} < 0.90")
        if not isinstance(evidence, list) or not evidence or not all(isinstance(entry, str) and entry for entry in evidence):
            failures.append(f"{prefix}: evidence must be a non-empty string list")
        else:
            evidence_text = " ".join(evidence).casefold()
            if any(token in evidence_text for token in ("retail", "shipping", "jamp")):
                retail_evidence_records += 1
            else:
                failures.append(f"{prefix}: evidence does not cite retail/shipping jamp authority")
        if not isinstance(contract, list) or not contract or not all(isinstance(entry, str) and entry for entry in contract):
            failures.append(f"{prefix}: contract must be a non-empty string list")

    if retail_evidence_records != len(data):
        failures.append(
            "retail renderer contract ledger does not cite retail/shipping evidence for every record"
        )

    return {
        "status": "pass" if not failures else "fail",
        "records": len(data),
        "uniqueStarts": len(starts),
        "uniqueNames": len(names),
        "minConfidence": min_confidence,
        "retailEvidenceRecords": retail_evidence_records,
        "failures": failures,
    }


def object_compare_report_validation(data: object, repo_root: Path) -> dict[str, object]:
    failures: list[str] = []
    if not isinstance(data, dict):
        return {"status": "fail", "failures": ["object compare report is not a JSON object"]}

    if data.get("reportType") != EXPECTED_OBJECT_COMPARE_REPORT_TYPE:
        failures.append(
            "reportType "
            f"{data.get('reportType')!r} != {EXPECTED_OBJECT_COMPARE_REPORT_TYPE!r}"
        )
    if data.get("reportSchemaVersion") != EXPECTED_OBJECT_COMPARE_REPORT_SCHEMA_VERSION:
        failures.append(
            "reportSchemaVersion "
            f"{data.get('reportSchemaVersion')!r} != {EXPECTED_OBJECT_COMPARE_REPORT_SCHEMA_VERSION}"
        )
    if parse_utc_datetime(data.get("generatedAtUtc")) is None:
        failures.append("generatedAtUtc missing or not UTC-parseable")

    compare_script = resolve_path(repo_root, Path("scripts/compare_retail_renderer_objects.py"))
    provenance = data.get("verifierProvenance")
    if not isinstance(provenance, dict):
        failures.append("verifierProvenance missing")
    else:
        if provenance.get("scriptSha256") != file_sha256(compare_script):
            failures.append("verifierProvenance.scriptSha256 does not match current compare script")
        if not isinstance(provenance.get("argv"), list):
            failures.append("verifierProvenance.argv missing")

    inputs = data.get("inputs")
    if not isinstance(inputs, dict):
        failures.append("inputs missing")
    else:
        object_pairs = inputs.get("objectPairs")
        if not isinstance(object_pairs, dict) or len(object_pairs) < 31:
            failures.append("inputs.objectPairs missing or has fewer than 31 entries")
        for key in ("currentRoot", "donorRoot", "link"):
            input_record = inputs.get(key)
            if not isinstance(input_record, dict) or not input_record.get("path"):
                failures.append(f"inputs.{key} path record missing")
            elif key == "link" and not input_record.get("sha256"):
                failures.append("inputs.link sha256 missing")

    rows = data.get("objects")
    if not isinstance(rows, list):
        failures.append("objects missing")
    else:
        compared_rows = 0
        row_failures: list[str] = []
        row_failure_count = 0

        def add_row_failure(message: str) -> None:
            nonlocal row_failure_count
            row_failure_count += 1
            if len(row_failures) < OBJECT_COMPARE_VALIDATION_SAMPLE_LIMIT:
                row_failures.append(message)

        for index, row in enumerate(rows):
            if not isinstance(row, dict):
                add_row_failure(f"objects[{index}] is not an object")
                continue
            if row.get("error"):
                continue
            compared_rows += 1
            for key in ("currentFile", "donorFile"):
                file_record = row.get(key)
                if not isinstance(file_record, dict):
                    add_row_failure(f"objects[{index}].{key} missing")
                    continue
                if not file_record.get("path"):
                    add_row_failure(f"objects[{index}].{key}.path missing")
                if not file_record.get("sha256"):
                    add_row_failure(f"objects[{index}].{key}.sha256 missing")
                if not file_record.get("modifiedUtc"):
                    add_row_failure(f"objects[{index}].{key}.modifiedUtc missing")
        if compared_rows < 31:
            failures.append("fewer than 31 compared object rows carry file provenance")
        failures.extend(row_failures)
        if row_failure_count > len(row_failures):
            failures.append(
                f"{row_failure_count - len(row_failures)} additional object row provenance failures omitted"
            )

    return {
        "status": "pass" if not failures else "fail",
        "failures": failures,
    }


def vcproj_relative_path(rel_path: Path) -> str:
    parts = list(rel_path.parts)
    if parts and parts[0].casefold() == "code":
        parts = parts[1:]
    return "..\\" + "\\".join(parts)


def validate_retail_renderer_source_contract(repo_root: Path) -> dict[str, object]:
    failures: list[str] = []
    files: dict[str, dict[str, object]] = {}

    contract_header = repo_root / "code" / "renderer" / "retail_xbox" / "retail_renderer_contract.h"
    header_record = proof_file_record(contract_header)
    if not contract_header.is_file():
        failures.append("retail renderer contract header missing")
    else:
        header_text = contract_header.read_text(encoding="utf-8-sig", errors="ignore")
        for marker in ("STEFX_RETAIL_RENDERER_ACTIVE", "STEFX_RETAIL_SCOPE", "STEFX_RETAIL_NAMESPACE_BEGIN"):
            if marker not in header_text:
                failures.append(f"retail renderer contract header missing {marker}")
    files["code/renderer/retail_xbox/retail_renderer_contract.h"] = header_record

    for rel_path in RETAIL_RENDERER_SOURCE_FILES:
        path = repo_root / rel_path
        rel_key = rel_path.as_posix()
        record = proof_file_record(path)
        files[rel_key] = record
        if not path.is_file():
            failures.append(f"{rel_key}: retail source missing")
            continue
        text = path.read_text(encoding="utf-8-sig", errors="ignore")
        if '#include "retail_renderer_contract.h"' not in text:
            failures.append(f"{rel_key}: missing retail_renderer_contract.h include")
        if "STEFX_RETAIL_NAMESPACE_BEGIN" not in text or "STEFX_RETAIL_NAMESPACE_END" not in text:
            failures.append(f"{rel_key}: missing retail namespace wrapper")

    project_path = repo_root / "code" / "x_exe" / "x_exe.vcproj"
    project_record = proof_file_record(project_path)
    if not project_path.is_file():
        failures.append("code/x_exe/x_exe.vcproj missing")
    else:
        project_text = project_path.read_text(encoding="utf-8-sig", errors="ignore")
        project_text_casefold = project_text.casefold()
        for rel_path in RETAIL_RENDERER_SOURCE_FILES:
            expected = f'RelativePath="{vcproj_relative_path(rel_path)}"'.casefold()
            if expected not in project_text_casefold:
                failures.append(f"code/x_exe/x_exe.vcproj missing {vcproj_relative_path(rel_path)}")

    build_script_path = repo_root / BUILD_SCRIPT_UNDER_TEST
    build_script_record = proof_file_record(build_script_path)
    if not build_script_path.is_file():
        failures.append("scripts/build_xbox.ps1 missing")
    else:
        build_text = build_script_path.read_text(encoding="utf-8-sig", errors="ignore")
        for define in RETAIL_RENDERER_REQUIRED_DEFINES:
            if define not in build_text:
                failures.append(f"scripts/build_xbox.ps1 missing retail renderer define {define}")
        for rel_path in RETAIL_RENDERER_REPLACED_LEGACY_SOURCES:
            replacement = vcproj_relative_path(rel_path)
            if replacement not in build_text:
                failures.append(f"scripts/build_xbox.ps1 missing legacy replacement {replacement}")
        if "retailRendererReplacements -icontains $source.RelativePath" not in build_text:
            failures.append("scripts/build_xbox.ps1 does not filter legacy renderer replacement sources")

    return {
        "status": "pass" if not failures else "fail",
        "sourceCount": len(RETAIL_RENDERER_SOURCE_FILES),
        "files": files,
        "project": project_record,
        "buildScript": build_script_record,
        "requiredDefines": list(RETAIL_RENDERER_REQUIRED_DEFINES),
        "replacedLegacySources": [path.as_posix() for path in RETAIL_RENDERER_REPLACED_LEGACY_SOURCES],
        "failures": failures,
    }


def proof_file_record(path: Path) -> dict[str, object]:
    record: dict[str, object] = {"path": str(path), "present": path.is_file()}
    if path.is_file():
        record["bytes"] = path.stat().st_size
        record["sha256"] = file_sha256(path)
    return record


def modified_utc_record(path: Path) -> dict[str, object]:
    record: dict[str, object] = {"path": str(path), "present": path.is_file()}
    if path.is_file():
        modified_utc = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
        record["modifiedUtc"] = modified_utc.isoformat().replace("+00:00", "Z")
    return record


def contract_artifact_freshness(
    repo_root: Path,
    artifact_path: Path,
    input_paths: tuple[Path, ...],
    sample_limit: int = 12,
) -> dict[str, object]:
    artifact = resolve_path(repo_root, artifact_path)
    record: dict[str, object] = {
        "checked": True,
        "artifact": modified_utc_record(artifact),
        "newerInputCount": 0,
        "newerInputs": [],
        "missingInputCount": 0,
        "missingInputs": [],
    }
    if not artifact.is_file():
        record["status"] = "artifact-missing"
        return record

    artifact_mtime = artifact.stat().st_mtime
    newer_inputs: list[dict[str, object]] = []
    missing_inputs: list[str] = []
    for rel_path in input_paths:
        input_path = resolve_path(repo_root, rel_path)
        if not input_path.is_file():
            missing_inputs.append(rel_path.as_posix())
            continue
        if input_path.stat().st_mtime > artifact_mtime:
            newer_inputs.append(modified_utc_record(input_path))

    record["newerInputCount"] = len(newer_inputs)
    record["newerInputs"] = newer_inputs[:sample_limit]
    record["missingInputCount"] = len(missing_inputs)
    record["missingInputs"] = missing_inputs[:sample_limit]
    record["status"] = "stale-contract-artifact" if newer_inputs or missing_inputs else "pass"
    return record


def build_script_contract_evidence(repo_root: Path) -> dict[str, object]:
    verifier_path = resolve_path(repo_root, BUILD_SCRIPT_CONTRACT_VERIFIER)
    build_script_path = resolve_path(repo_root, BUILD_SCRIPT_UNDER_TEST)
    evidence: dict[str, object] = {
        "status": "missing",
        "verifier": proof_file_record(verifier_path),
        "buildScript": proof_file_record(build_script_path),
        "argv": [
            str(verifier_path),
            "--build-script",
            str(build_script_path),
        ],
        "exitCode": None,
        "stdout": "",
        "stderr": "",
        "failures": [],
    }
    failures: list[str] = []
    if not verifier_path.is_file():
        failures.append("build_xbox contract verifier missing")
    if not build_script_path.is_file():
        failures.append("build_xbox.ps1 missing")
    if failures:
        evidence["status"] = "fail"
        evidence["failures"] = failures
        return evidence

    command = [sys.executable, str(verifier_path), "--build-script", str(build_script_path)]
    result = run_command(command, repo_root)
    evidence["exitCode"] = result["returnCode"]
    evidence["stdout"] = result["stdout"]
    evidence["stderr"] = result["stderr"]
    if result["returnCode"] != 0:
        failures.append("build_xbox target contract verification failed")
        if result["stderr"]:
            failures.append(str(result["stderr"]).strip())
    evidence["status"] = "pass" if not failures else "fail"
    evidence["failures"] = failures
    return evidence


def png_visual_metadata(path: Path) -> dict[str, object] | None:
    if not path.is_file():
        return None
    header = path.read_bytes()[:32]
    if (
        len(header) < 24
        or header[:8] != PNG_SIGNATURE
        or header[12:16] != b"IHDR"
    ):
        return None
    return {
        "visualType": "png",
        "width": int.from_bytes(header[16:20], "big"),
        "height": int.from_bytes(header[20:24], "big"),
    }


def xemu_contact_sheet_failures(
    path: Path,
    min_bytes: int = DEFAULT_MIN_EVIDENCE_BYTES,
    min_width: int = DEFAULT_MIN_IMAGE_WIDTH,
    min_height: int = DEFAULT_MIN_IMAGE_HEIGHT,
) -> list[str]:
    if not path.is_file():
        return ["contact sheet missing"]
    size = path.stat().st_size
    failures: list[str] = []
    if size < min_bytes:
        failures.append(f"contact sheet below {min_bytes} bytes")
    metadata = png_visual_metadata(path)
    if metadata is None:
        failures.append("contact sheet is not a PNG visual proof")
        return failures
    width = int(metadata["width"])
    height = int(metadata["height"])
    if width < min_width or height < min_height:
        failures.append(
            f"contact sheet dimensions {width}x{height} below required {min_width}x{min_height}"
        )
    return failures


def self_test_png_bytes(width: int = 320, height: int = 240) -> bytes:
    png_header = (
        PNG_SIGNATURE
        + (13).to_bytes(4, "big")
        + b"IHDR"
        + width.to_bytes(4, "big")
        + height.to_bytes(4, "big")
        + b"\x08\x02\x00\x00\x00"
        + b"\x00\x00\x00\x00"
    )
    return png_header + (b"\0" * (DEFAULT_MIN_EVIDENCE_BYTES - len(png_header)))


def runtime_build_id(path: Path) -> str | None:
    if not path.is_file():
        return None
    data = path.read_bytes()
    start = data.find(RUNTIME_BUILD_ID_MARKER)
    if start < 0:
        return None
    end = start
    limit = min(len(data), start + 256)
    while end < limit and data[end] not in (0, 10, 13):
        end += 1
    return data[start:end].decode("ascii", errors="replace")


def expected_runtime_build_id_fragments(rel_path: str) -> list[str]:
    basename = Path(rel_path).name.lower()
    if basename == "default.xbe":
        return ["personality=default", "log=ef_sp_log.txt"]
    if basename == "efmp.xbe":
        return ["personality=efmp", "log=ef_mp_log.txt"]
    return []


def runtime_build_id_identity_failures(rel_path: str, build_id: object, label: str) -> list[str]:
    if not isinstance(build_id, str) or not build_id:
        return []
    failures: list[str] = []
    for fragment in expected_runtime_build_id_fragments(rel_path):
        if fragment not in build_id:
            failures.append(f"{label} {rel_path} runtimeBuildId has wrong identity; missing {fragment!r}")
    return failures


def proof_context_float(proof_context: dict[str, object], key: str) -> float | None:
    value = proof_context.get(key)
    if not isinstance(value, str) or not value:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def xemu_proof_context_failures(
    proof_context: dict[str, object],
    config: dict[str, object],
) -> list[str]:
    failures: list[str] = []
    if proof_context.get("mode") != config["mode"]:
        failures.append(
            "XEMU report proof_context mode "
            f"{proof_context.get('mode')!r} != {config['mode']!r}"
        )
    if proof_context.get("map") != config["map"]:
        failures.append(
            "XEMU report proof_context map "
            f"{proof_context.get('map')!r} != {config['map']!r}"
        )
    duration = proof_context_float(proof_context, "duration")
    if duration is None:
        failures.append("XEMU report proof_context duration missing or invalid")
    elif duration < float(config["minDurationSeconds"]):
        failures.append(
            "XEMU report proof_context duration "
            f"{duration:.1f}s < required {float(config['minDurationSeconds']):.1f}s"
        )
    expected_duration = config.get("expectedProofDuration")
    if isinstance(expected_duration, (int, float)) and duration is not None:
        if abs(duration - float(expected_duration)) > 0.001:
            failures.append(
                "XEMU report proof_context duration "
                f"{duration:.1f}s != refresh manifest {float(expected_duration):.1f}s"
            )
    interval = proof_context_float(proof_context, "interval")
    if interval is None:
        failures.append("XEMU report proof_context interval missing or invalid")
    elif interval <= 0.0:
        failures.append("XEMU report proof_context interval must be positive")
    expected_interval = config.get("expectedProofInterval")
    if isinstance(expected_interval, (int, float)) and interval is not None:
        if abs(interval - float(expected_interval)) > 0.001:
            failures.append(
                "XEMU report proof_context interval "
                f"{interval:.1f}s != refresh manifest {float(expected_interval):.1f}s"
            )
    if proof_context.get("nativeScreenshots") != "True":
        failures.append("XEMU report proof_context nativeScreenshots is not True")
    return failures


def release_runtime_identity(repo_root: Path, rel_path: str) -> dict[str, object]:
    path = repo_root / "build" / "release" / rel_path
    record: dict[str, object] = {
        "path": str(path),
        "present": path.is_file(),
        "runtimeBuildId": None,
        "sha256": None,
    }
    if path.is_file():
        record["bytes"] = path.stat().st_size
        record["sha256"] = file_sha256(path)
        record["runtimeBuildId"] = runtime_build_id(path)
    return record


def collect_contract_evidence(repo_root: Path) -> dict[str, object]:
    artifacts = {
        name: contract_artifact_record(resolve_path(repo_root, rel_path))
        for name, rel_path in CONTRACT_ARTIFACTS.items()
    }
    build_contract = build_script_contract_evidence(repo_root)
    retail_source_contract = validate_retail_renderer_source_contract(repo_root)
    failures: list[str] = []

    contracts = artifacts["retailRendererContracts"]
    contracts_data = read_json_any(resolve_path(repo_root, CONTRACT_ARTIFACTS["retailRendererContracts"]))
    if not contracts.get("present"):
        failures.append("retail renderer contract ledger missing")
    elif int(contracts.get("records", 0)) < 10:
        failures.append("retail renderer contract ledger has fewer than 10 records")
    else:
        ledger_validation = retail_contract_ledger_validation(contracts_data)
        contracts["ledgerValidation"] = ledger_validation
        if ledger_validation.get("status") != "pass":
            failures.append("retail renderer contract ledger structure invalid")
            validation_failures = ledger_validation.get("failures")
            if isinstance(validation_failures, list):
                failures.extend(
                    f"retailRendererContracts: {failure}" for failure in validation_failures
                )

    for name in ("spObjectCompare", "hmObjectCompare"):
        record = artifacts[name]
        summary = record.get("summary")
        if not record.get("present"):
            failures.append(f"{name}: object-compare report missing")
            continue
        freshness = contract_artifact_freshness(
            repo_root,
            CONTRACT_ARTIFACTS[name],
            RETAIL_OBJECT_COMPARE_FRESHNESS_INPUTS,
        )
        record["freshness"] = freshness
        if freshness.get("status") == "stale-contract-artifact":
            failures.append(f"{name}: object-compare report predates retail renderer source or comparison inputs")
        elif freshness.get("status") not in ("pass", "artifact-missing"):
            failures.append(f"{name}: object-compare freshness status is {freshness.get('status')}")
        if not isinstance(summary, dict):
            failures.append(f"{name}: object-compare summary missing")
            continue
        report_validation = object_compare_report_validation(
            read_json_any(resolve_path(repo_root, CONTRACT_ARTIFACTS[name])),
            repo_root,
        )
        record["reportValidation"] = report_validation
        if report_validation.get("status") != "pass":
            failures.append(f"{name}: object-compare report provenance invalid")
            validation_failures = report_validation.get("failures")
            if isinstance(validation_failures, list):
                failures.extend(f"{name}: {failure}" for failure in validation_failures)
        if int(summary.get("objects", 0)) < 31:
            failures.append(f"{name}: fewer than 31 renderer objects compared")
        if int(summary.get("common_functions", 0)) < 2000:
            failures.append(f"{name}: fewer than 2000 common functions compared")
        if int(summary.get("exact_instruction_text", 0)) < 1400:
            failures.append(f"{name}: fewer than 1400 instruction-exact functions")

    abi = artifacts["abiEnumsState"]
    if not abi.get("present"):
        failures.append("abiEnumsState: ABI/enum/state report missing")
    else:
        field_summary = abi.get("field_summary")
        enum_summary = abi.get("enum_summary")
        state_summary = abi.get("state_summary")
        if not isinstance(field_summary, dict) or field_summary.get("different") != 0:
            failures.append("abiEnumsState: hot renderer field offsets are not all exact")
        if not isinstance(enum_summary, dict) or enum_summary.get("unexpected_differences") != 0:
            failures.append("abiEnumsState: unexpected enum differences present")
        if not isinstance(state_summary, dict) or state_summary.get("different") != 0:
            failures.append("abiEnumsState: GL state mask differences present")

    runtime = artifacts["runtimeStructure"]
    runtime_summary = runtime.get("summary")
    if not runtime.get("present"):
        failures.append("runtimeStructure: runtime structure comparison missing")
    elif not isinstance(runtime_summary, dict):
        failures.append("runtimeStructure: summary missing")
    else:
        if int(runtime_summary.get("common", 0)) < 500:
            failures.append("runtimeStructure: fewer than 500 common named functions")
        if int(runtime_summary.get("shape_exact", 0)) < 250:
            failures.append("runtimeStructure: fewer than 250 shape-exact functions")

    if build_contract.get("status") != "pass":
        failures.append("buildScriptContract: build_xbox target contract verification failed")
        contract_failures = build_contract.get("failures")
        if isinstance(contract_failures, list):
            failures.extend(f"buildScriptContract: {failure}" for failure in contract_failures)
    if retail_source_contract.get("status") != "pass":
        failures.append("retailSourceContract: active retail renderer source contract failed")
        source_failures = retail_source_contract.get("failures")
        if isinstance(source_failures, list):
            failures.extend(f"retailSourceContract: {failure}" for failure in source_failures)

    return {
        "status": "pass" if not failures else "fail",
        "artifacts": artifacts,
        "buildScriptContract": build_contract,
        "retailSourceContract": retail_source_contract,
        "failures": failures,
    }


def parse_xemu_report(path: Path) -> dict[str, object]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    shots: list[dict[str, object]] = []
    allocator_samples: list[dict[str, object]] = []
    linked_files: dict[str, str] = {}
    runtime_xbe_identities: list[dict[str, object]] = []
    proof_context: dict[str, str] | None = None
    p2_refdef_samples = 0
    dual_player_model_samples = 0
    xemu_version = None
    xemu_commit = None
    for line in text.splitlines():
        shot = SHOT_RE.match(line)
        if shot:
            shots.append(
                {
                    "t": float(shot.group(1)),
                    "ok": shot.group(2) == "True",
                    "bytes": int(shot.group(3)),
                }
            )
            continue
        path_match = KEY_VALUE_PATH_RE.match(line)
        if path_match:
            linked_files[path_match.group(1)] = path_match.group(2).strip()
            continue
        context = PROOF_CONTEXT_RE.match(line)
        if context:
            proof_context = {}
            for token in context.group(1).split():
                if "=" not in token:
                    continue
                key, value = token.split("=", 1)
                proof_context[key] = value
            continue
        identity = RUNTIME_XBE_IDENTITY_RE.match(line)
        if identity:
            runtime_xbe_identities.append(
                {
                    "path": identity.group("path"),
                    "basename": Path(identity.group("path")).name.lower(),
                    "bytes": int(identity.group("bytes")),
                    "sha256": identity.group("sha256").upper(),
                    "runtimeBuildId": identity.group("runtimeBuildId"),
                }
            )
            continue
        tex = XLOGTEX_RE.match(line)
        if tex:
            allocator_samples.append(
                {
                    "t": float(tex.group(1)),
                    "used": int(tex.group(2)),
                    "totalFree": int(tex.group(3)),
                    "largestFree": int(tex.group(4)),
                }
            )
            continue
        if line.startswith("xemu_version:"):
            xemu_version = line.split(":", 1)[1].strip()
        elif line.startswith("xemu_commit:"):
            xemu_commit = line.split(":", 1)[1].strip()
        if P2_REFDEF_RE.search(line):
            p2_refdef_samples += 1
        if DUAL_PLAYER_MODEL_RE.search(line):
            dual_player_model_samples += 1
    ok_shots = [shot for shot in shots if shot.get("ok") and int(shot.get("bytes", 0)) > 0]
    unique_ok_shot_byte_sizes = sorted({int(shot["bytes"]) for shot in ok_shots})
    duration = max((float(shot["t"]) for shot in shots), default=0.0)
    allocator_stable = False
    if allocator_samples:
        used_values = {sample["used"] for sample in allocator_samples}
        total_free_values = {sample["totalFree"] for sample in allocator_samples}
        largest_free_values = {sample["largestFree"] for sample in allocator_samples}
        allocator_stable = (
            len(used_values) == 1
            and len(total_free_values) == 1
            and len(largest_free_values) == 1
        )
    return {
        "aliveAtEnd": "alive_at_end" in text,
        "shotCount": len(shots),
        "okShotCount": len(ok_shots),
        "uniqueOkShotByteSizes": unique_ok_shot_byte_sizes,
        "uniqueOkShotByteSizeCount": len(unique_ok_shot_byte_sizes),
        "durationSeconds": duration,
        "linkedFiles": linked_files,
        "proofContext": proof_context,
        "runtimeXbeIdentities": runtime_xbe_identities,
        "allocatorSamples": len(allocator_samples),
        "allocatorStable": allocator_stable,
        "p2RefdefSamples": p2_refdef_samples,
        "dualPlayerModelSamples": dual_player_model_samples,
        "xemuVersion": xemu_version,
        "xemuCommit": xemu_commit,
    }


def xemu_proof_freshness(repo_root: Path, proof_path: Path) -> dict[str, object]:
    result: dict[str, object] = {
        "checked": False,
        "status": "proof-missing",
        "proofModifiedUtc": None,
        "oldestReleaseArtifactUtc": None,
        "newerRuntimeSourceCount": 0,
        "newerRuntimeSources": [],
        "newerReleaseArtifacts": [],
        "runtimeSourcesNewerThanReleaseCount": 0,
        "runtimeSourcesNewerThanRelease": [],
        "newerProofHarnessFiles": [],
        "sampleLimit": XEMU_FRESHNESS_SAMPLE_LIMIT,
    }
    if not proof_path.is_file():
        return result

    proof_mtime = datetime.fromtimestamp(proof_path.stat().st_mtime, timezone.utc)
    result["checked"] = True
    result["proofModifiedUtc"] = proof_mtime.isoformat().replace("+00:00", "Z")

    source_root = repo_root / "code"
    newer_sources: list[dict[str, object]] = []
    newer_count = 0
    if source_root.is_dir():
        for path in sorted(source_root.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in RUNTIME_SOURCE_EXTENSIONS:
                continue
            modified_utc = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
            if modified_utc <= proof_mtime:
                continue
            newer_count += 1
            if len(newer_sources) < XEMU_FRESHNESS_SAMPLE_LIMIT:
                newer_sources.append(
                    {
                        "path": path.relative_to(repo_root).as_posix(),
                        "modifiedUtc": modified_utc.isoformat().replace("+00:00", "Z"),
                    }
                )

    release_artifacts: list[dict[str, object]] = []
    release_artifact_times: list[datetime] = []
    for rel_path in XEMU_RELEASE_ARTIFACTS:
        path = repo_root / rel_path
        if not path.is_file():
            continue
        modified_utc = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
        release_artifact_times.append(modified_utc)
        if modified_utc <= proof_mtime:
            continue
        release_artifacts.append(
            {
                "path": rel_path.as_posix(),
                "modifiedUtc": modified_utc.isoformat().replace("+00:00", "Z"),
            }
        )

    sources_newer_than_release: list[dict[str, object]] = []
    sources_newer_than_release_count = 0
    if source_root.is_dir() and release_artifact_times:
        oldest_release_utc = min(release_artifact_times)
        result["oldestReleaseArtifactUtc"] = oldest_release_utc.isoformat().replace("+00:00", "Z")
        for path in sorted(source_root.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in RUNTIME_SOURCE_EXTENSIONS:
                continue
            modified_utc = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
            if modified_utc <= oldest_release_utc:
                continue
            sources_newer_than_release_count += 1
            if len(sources_newer_than_release) < XEMU_FRESHNESS_SAMPLE_LIMIT:
                sources_newer_than_release.append(
                    {
                        "path": path.relative_to(repo_root).as_posix(),
                        "modifiedUtc": modified_utc.isoformat().replace("+00:00", "Z"),
                    }
                )

    harness_files: list[dict[str, object]] = []
    for rel_path in XEMU_PROOF_HARNESS_FILES:
        path = repo_root / rel_path
        if not path.is_file():
            continue
        modified_utc = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
        if modified_utc <= proof_mtime:
            continue
        harness_files.append(
            {
                "path": rel_path.as_posix(),
                "modifiedUtc": modified_utc.isoformat().replace("+00:00", "Z"),
            }
        )

    result["newerRuntimeSourceCount"] = newer_count
    result["newerRuntimeSources"] = newer_sources
    result["newerReleaseArtifacts"] = release_artifacts
    result["runtimeSourcesNewerThanReleaseCount"] = sources_newer_than_release_count
    result["runtimeSourcesNewerThanRelease"] = sources_newer_than_release
    result["newerProofHarnessFiles"] = harness_files
    result["status"] = (
        "stale-xemu-proof"
        if newer_count or release_artifacts or sources_newer_than_release_count or harness_files
        else "pass"
    )
    return result


def xemu_report_sibling_paths(report: Path) -> tuple[Path, Path]:
    report_text = str(report)
    if report_text.endswith(".report.txt"):
        prefix = report_text[: -len(".report.txt")]
    else:
        prefix = str(report.with_suffix(""))
    return Path(prefix + "_contact.png"), Path(prefix + "_final_registers.txt")


def comparable_path_text(repo_root: Path, path: Path | str) -> str:
    candidate = Path(path)
    if not candidate.is_absolute():
        candidate = repo_root / candidate
    return str(candidate.resolve(strict=False)).casefold()


def path_is_inside_repo(repo_root: Path, path: Path | str) -> bool:
    root = repo_root.resolve(strict=False)
    candidate = Path(path)
    if not candidate.is_absolute():
        candidate = root / candidate
    try:
        candidate.resolve(strict=False).relative_to(root)
    except ValueError:
        return False
    return True


def xemu_linked_artifact_failures(
    repo_root: Path,
    parsed: dict[str, object],
    contact: Path,
    final_registers: Path,
) -> list[str]:
    linked_files = parsed.get("linkedFiles")
    if not isinstance(linked_files, dict):
        linked_files = {}
    expected = {
        "contact": contact,
        "final_registers": final_registers,
    }
    failures: list[str] = []
    for key, expected_path in expected.items():
        linked_path = linked_files.get(key)
        if not isinstance(linked_path, str) or not linked_path:
            failures.append(f"XEMU report missing {key} artifact link")
            continue
        if comparable_path_text(repo_root, linked_path) != comparable_path_text(repo_root, expected_path):
            failures.append(f"XEMU report {key} artifact link does not match audited file")
    return failures


def xemu_evidence_runs_with_overrides(
    sp_report: Path | None = None,
    coop_report: Path | None = None,
    mp_report: Path | None = None,
    expected_contexts: dict[str, dict[str, object]] | None = None,
) -> dict[str, dict[str, object]]:
    runs = {name: dict(config) for name, config in XEMU_EVIDENCE_RUNS.items()}
    overrides = {
        "spBorg2": sp_report,
        "coopBorg1SplitScreen": coop_report,
        "hmBorg1Soak": mp_report,
    }
    for name, report in overrides.items():
        if report is None:
            continue
        contact, final_registers = xemu_report_sibling_paths(report)
        runs[name]["report"] = report
        runs[name]["contact"] = contact
        runs[name]["finalRegisters"] = final_registers
    if expected_contexts:
        for name, expected_context in expected_contexts.items():
            if name not in runs or not isinstance(expected_context, dict):
                continue
            duration = expected_context.get("duration")
            interval = expected_context.get("interval")
            if isinstance(duration, (int, float)):
                runs[name]["expectedProofDuration"] = float(duration)
            if isinstance(interval, (int, float)):
                runs[name]["expectedProofInterval"] = float(interval)
    return runs


def xemu_refresh_report_record(
    repo_root: Path,
    path: Path | None,
    stage: Path | None = None,
) -> dict[str, object]:
    if path is None:
        return {"present": False, "used": False}
    report_path = resolve_path(repo_root, path)
    record = proof_file_record(report_path)
    record["used"] = True
    data = read_json(report_path)
    failures: list[str] = []
    derived_reports: dict[str, str] = {}
    derived_report_files: dict[str, dict[str, object]] = {}
    expected_contexts: dict[str, dict[str, object]] = {}
    generated_at: datetime | None = None
    if not isinstance(data, dict):
        failures.append("XEMU proof refresh report missing or invalid JSON")
    else:
        record["reportType"] = data.get("reportType")
        record["reportSchemaVersion"] = data.get("reportSchemaVersion")
        if data.get("reportType") != EXPECTED_XEMU_REFRESH_REPORT_TYPE:
            failures.append(
                "XEMU proof refresh reportType "
                f"{data.get('reportType')!r} != {EXPECTED_XEMU_REFRESH_REPORT_TYPE!r}"
            )
        if data.get("reportSchemaVersion") != EXPECTED_XEMU_REFRESH_REPORT_SCHEMA_VERSION:
            failures.append(
                "XEMU proof refresh reportSchemaVersion "
                f"{data.get('reportSchemaVersion')!r} "
                f"!= {EXPECTED_XEMU_REFRESH_REPORT_SCHEMA_VERSION}"
            )
        generated_at = parse_utc_datetime(data.get("generatedAtUtc"))
        if generated_at is None:
            failures.append("XEMU proof refresh generatedAtUtc missing or invalid")
        else:
            record["generatedAtUtc"] = generated_at.isoformat().replace("+00:00", "Z")
        manifest_repo_root = data.get("repoRoot")
        if not isinstance(manifest_repo_root, str) or not manifest_repo_root:
            failures.append("XEMU proof refresh repoRoot missing")
        elif comparable_path_text(repo_root, manifest_repo_root) != comparable_path_text(repo_root, repo_root):
            failures.append(
                "XEMU proof refresh repoRoot "
                f"{manifest_repo_root!r} != audited repo root {str(repo_root)!r}"
            )
        manifest_stage = data.get("stage")
        if stage is not None:
            if not isinstance(manifest_stage, str) or not manifest_stage:
                failures.append("XEMU proof refresh stage missing")
            elif comparable_path_text(repo_root, manifest_stage) != comparable_path_text(repo_root, stage):
                failures.append(
                    "XEMU proof refresh stage "
                    f"{manifest_stage!r} != audited stage {str(stage)!r}"
                )
        release_preflight = data.get("releasePreflight")
        if not isinstance(release_preflight, dict):
            failures.append("XEMU proof refresh releasePreflight missing")
        else:
            record["releasePreflight"] = release_preflight
            if release_preflight.get("label") != "Release freshness preflight":
                failures.append("XEMU proof refresh releasePreflight label mismatch")
            if release_preflight.get("exitCode") != 0:
                failures.append(
                    "XEMU proof refresh releasePreflight exitCode "
                    f"{release_preflight.get('exitCode')!r} != 0"
                )
            command = release_preflight.get("command")
            if not isinstance(command, list) or "-CheckFreshnessOnly" not in command:
                failures.append("XEMU proof refresh releasePreflight command missing -CheckFreshnessOnly")
            output = release_preflight.get("output")
            output_lines = [str(line) for line in output] if isinstance(output, list) else []
            output_text = "\n".join(output_lines)
            for marker in (
                "build_xbox.ps1 target contract ok",
                "build\\release XBE freshness ok",
                "build\\release PK3 freshness ok",
                "build\\release XBE runtime build ids ok",
            ):
                if marker not in output_text:
                    failures.append(
                        f"XEMU proof refresh releasePreflight output missing {marker!r}"
                    )
            for rel_path, fragments in {
                "build\\release\\default.xbe": (
                    "STEFX_RUNTIME_BUILD_ID",
                    "personality=default",
                    "log=ef_sp_log.txt",
                ),
                "build\\release\\efmp.xbe": (
                    "STEFX_RUNTIME_BUILD_ID",
                    "personality=efmp",
                    "log=ef_mp_log.txt",
                ),
            }.items():
                if not any(rel_path in line and all(fragment in line for fragment in fragments) for line in output_lines):
                    failures.append(
                        "XEMU proof refresh releasePreflight output missing "
                        f"runtime identity for {rel_path}"
                    )
        criteria = data.get("criteria")
        criteria_interval: float | None = None
        if not isinstance(criteria, dict):
            failures.append("XEMU proof refresh criteria missing")
        else:
            for key in ("interval", "firstShotDelay"):
                value = criteria.get(key)
                if not isinstance(value, (int, float)):
                    failures.append(f"XEMU proof refresh criteria {key} missing")
                elif float(value) < 0:
                    failures.append(f"XEMU proof refresh criteria {key} must be non-negative")
            for key, expected_value in EXPECTED_XEMU_REFRESH_CRITERIA.items():
                value = criteria.get(key)
                if value != expected_value:
                    failures.append(
                        f"XEMU proof refresh criteria {key} {value!r} != {expected_value!r}"
                    )
            interval_value = criteria.get("interval")
            if isinstance(interval_value, (int, float)):
                criteria_interval = float(interval_value)
        reports = data.get("reports")
        if not isinstance(reports, dict):
            failures.append("XEMU proof refresh report has no reports object")
        else:
            for name, config in XEMU_EVIDENCE_RUNS.items():
                entry = reports.get(name)
                if not isinstance(entry, dict):
                    failures.append(f"XEMU proof refresh report missing {name}")
                    continue
                if entry.get("mode") != config["mode"]:
                    failures.append(
                        f"XEMU proof refresh {name} mode {entry.get('mode')!r} "
                        f"!= {config['mode']!r}"
                    )
                if entry.get("map") != config["map"]:
                    failures.append(
                        f"XEMU proof refresh {name} map {entry.get('map')!r} "
                        f"!= {config['map']!r}"
                    )
                duration = entry.get("duration")
                if not isinstance(duration, (int, float)):
                    failures.append(f"XEMU proof refresh {name} duration missing")
                elif float(duration) < float(config["minDurationSeconds"]):
                    failures.append(
                        f"XEMU proof refresh {name} duration {float(duration):.1f}s "
                        f"< required {float(config['minDurationSeconds']):.1f}s"
                    )
                entry_path = entry.get("path")
                if not isinstance(entry_path, str) or not entry_path:
                    failures.append(f"XEMU proof refresh {name} path missing")
                    continue
                resolved_entry_path = resolve_path(repo_root, Path(entry_path))
                if not path_is_inside_repo(repo_root, resolved_entry_path):
                    failures.append(f"XEMU proof refresh {name} path is outside audited repo")
                    continue
                derived_reports[name] = str(resolved_entry_path)
                derived_file_record = proof_file_record(resolved_entry_path)
                if resolved_entry_path.is_file():
                    modified_utc = datetime.fromtimestamp(
                        resolved_entry_path.stat().st_mtime, timezone.utc
                    )
                    derived_file_record["modifiedUtc"] = modified_utc.isoformat().replace(
                        "+00:00", "Z"
                    )
                    if generated_at is not None and modified_utc > generated_at:
                        failures.append(
                            f"XEMU proof refresh {name} report file is newer than generatedAtUtc"
                        )
                derived_report_files[name] = derived_file_record
                if isinstance(duration, (int, float)) and criteria_interval is not None:
                    expected_contexts[name] = {
                        "duration": float(duration),
                        "interval": criteria_interval,
                    }
    record["status"] = "pass" if not failures else "fail"
    record["derivedReports"] = derived_reports
    record["derivedReportFiles"] = derived_report_files
    record["expectedProofContexts"] = expected_contexts
    record["failures"] = failures
    return record


def latest_xemu_refresh_report(repo_root: Path) -> Path | None:
    output_dir = repo_root / "scripts" / "output"
    if not output_dir.is_dir():
        return None
    candidates = [
        path
        for path in output_dir.glob("xemu_qualification_proof_refresh_*.json")
        if path.is_file()
    ]
    if not candidates:
        return None
    return max(candidates, key=lambda path: (path.stat().st_mtime_ns, path.name))


def select_xemu_refresh_report(
    repo_root: Path,
    explicit_refresh_report: Path | None,
    explicit_sp_report: Path | None,
    explicit_coop_report: Path | None,
    explicit_mp_report: Path | None,
) -> tuple[Path | None, str]:
    if explicit_refresh_report is not None:
        return explicit_refresh_report, "explicit"
    if any(report is not None for report in (explicit_sp_report, explicit_coop_report, explicit_mp_report)):
        return None, "disabled-by-explicit-proof-reports"
    selected = latest_xemu_refresh_report(repo_root)
    if selected is not None:
        return selected, "auto"
    return None, "none"


def annotate_xemu_refresh_selection(
    repo_root: Path,
    record: dict[str, object],
    selection_mode: str,
    explicit_sp_report: Path | None,
    explicit_coop_report: Path | None,
    explicit_mp_report: Path | None,
) -> dict[str, object]:
    record["selectionMode"] = selection_mode
    record["autoSelected"] = selection_mode == "auto"
    explicit_proof_overrides = {
        "spBorg2": str(resolve_path(repo_root, explicit_sp_report))
        if explicit_sp_report is not None
        else None,
        "coopBorg1SplitScreen": str(resolve_path(repo_root, explicit_coop_report))
        if explicit_coop_report is not None
        else None,
        "hmBorg1Soak": str(resolve_path(repo_root, explicit_mp_report))
        if explicit_mp_report is not None
        else None,
    }
    explicit_proof_overrides = {
        name: path for name, path in explicit_proof_overrides.items() if path is not None
    }
    if explicit_proof_overrides:
        record["explicitProofReportOverrides"] = explicit_proof_overrides
        record["autoSelectionSuppressed"] = (
            selection_mode == "disabled-by-explicit-proof-reports"
        )
    return record


def collect_xemu_evidence(
    repo_root: Path,
    evidence_runs: dict[str, dict[str, object]] | None = None,
) -> dict[str, object]:
    runs: dict[str, object] = {}
    failures: list[str] = []
    configured_runs = evidence_runs if evidence_runs is not None else XEMU_EVIDENCE_RUNS
    proof_harness_files = {
        rel_path.as_posix(): proof_file_record(repo_root / rel_path)
        for rel_path in XEMU_PROOF_HARNESS_FILES
    }
    for rel_path, record in proof_harness_files.items():
        if not record.get("present"):
            failures.append(f"proof harness file missing: {rel_path}")
    release_identities = {
        "default.xbe": release_runtime_identity(repo_root, "default.xbe"),
        "efmp.xbe": release_runtime_identity(repo_root, "efmp.xbe"),
    }
    for name, config in configured_runs.items():
        report = resolve_path(repo_root, config["report"])
        contact = resolve_path(repo_root, config["contact"])
        final_registers = resolve_path(repo_root, config["finalRegisters"])
        files = {
            "report": proof_file_record(report),
            "contact": proof_file_record(contact),
            "finalRegisters": proof_file_record(final_registers),
        }
        contact_metadata = png_visual_metadata(contact)
        if contact_metadata is not None:
            files["contact"].update(contact_metadata)
        freshness = xemu_proof_freshness(repo_root, report)
        run_failures: list[str] = []
        parsed: dict[str, object] = {}
        if not report.is_file():
            run_failures.append("report missing")
        else:
            parsed = parse_xemu_report(report)
            run_failures.extend(
                xemu_linked_artifact_failures(
                    repo_root,
                    parsed,
                    contact,
                    final_registers,
                )
            )
            proof_context = parsed.get("proofContext")
            if not isinstance(proof_context, dict):
                run_failures.append("XEMU report missing proof_context")
            else:
                run_failures.extend(xemu_proof_context_failures(proof_context, config))
            expected_xbe = "efmp.xbe" if config["mode"] == "mp" else "default.xbe"
            expected_identity = release_identities[expected_xbe]
            expected_build_id = expected_identity.get("runtimeBuildId")
            expected_sha256 = expected_identity.get("sha256")
            expected_path = expected_identity.get("path")
            expected_bytes = expected_identity.get("bytes")
            report_identities = [
                identity
                for identity in parsed.get("runtimeXbeIdentities", [])
                if isinstance(identity, dict) and identity.get("basename") == expected_xbe
            ]
            if not expected_identity.get("present"):
                run_failures.append(f"current release {expected_xbe} is missing")
            elif not expected_build_id:
                run_failures.append(f"current release {expected_xbe} is missing runtime build id")
            else:
                run_failures.extend(
                    runtime_build_id_identity_failures(
                        expected_xbe,
                        expected_build_id,
                        "current release",
                    )
                )
            if expected_identity.get("present") and expected_build_id and not report_identities:
                run_failures.append(f"XEMU report missing runtime XBE identity for {expected_xbe}")
            elif expected_identity.get("present") and expected_build_id and report_identities:
                report_identity = report_identities[-1]
                run_failures.extend(
                    runtime_build_id_identity_failures(
                        expected_xbe,
                        report_identity.get("runtimeBuildId"),
                        "XEMU report",
                    )
                )
                if report_identity.get("runtimeBuildId") != expected_build_id:
                    run_failures.append(
                        f"XEMU report runtime build id for {expected_xbe} does not match current release"
                    )
                if isinstance(expected_path, str) and comparable_path_text(
                    repo_root, report_identity.get("path", "")
                ) != comparable_path_text(repo_root, expected_path):
                    run_failures.append(
                        f"XEMU report path for {expected_xbe} does not match current release"
                    )
                if isinstance(expected_bytes, int) and report_identity.get("bytes") != expected_bytes:
                    run_failures.append(
                        f"XEMU report byte count for {expected_xbe} does not match current release"
                    )
                if expected_sha256 and report_identity.get("sha256") != expected_sha256:
                    run_failures.append(
                        f"XEMU report SHA256 for {expected_xbe} does not match current release"
                    )
            if not parsed.get("aliveAtEnd"):
                run_failures.append("alive_at_end marker missing")
            if int(parsed.get("okShotCount", 0)) < int(config["minOkShots"]):
                run_failures.append(
                    f"fewer than {config['minOkShots']} successful screenshots"
                )
            if int(parsed.get("uniqueOkShotByteSizeCount", 0)) < int(
                config["minUniqueOkShotByteSizes"]
            ):
                run_failures.append(
                    "successful screenshots do not show enough byte-size diversity "
                    f"({parsed.get('uniqueOkShotByteSizeCount', 0)} < "
                    f"{config['minUniqueOkShotByteSizes']})"
                )
            if float(parsed.get("durationSeconds", 0.0)) < float(config["minDurationSeconds"]):
                run_failures.append(
                    f"duration below {float(config['minDurationSeconds']):.1f}s"
                )
            if config.get("requireAllocatorStable") and not parsed.get("allocatorStable"):
                run_failures.append("texture allocator samples were not stable")
            if config.get("requireP2Refdef") and int(parsed.get("p2RefdefSamples", 0)) < int(
                config.get("minP2RefdefSamples", 1)
            ):
                run_failures.append(
                    "P2 refdef telemetry did not reach the required sample count "
                    f"({parsed.get('p2RefdefSamples', 0)} < "
                    f"{config.get('minP2RefdefSamples', 1)})"
                )
            if config.get("requireDualPlayerModelState") and int(
                parsed.get("dualPlayerModelSamples", 0)
            ) < int(config.get("minDualPlayerModelSamples", 1)):
                run_failures.append(
                    "dual-player model telemetry did not reach the required sample count "
                    f"({parsed.get('dualPlayerModelSamples', 0)} < "
                    f"{config.get('minDualPlayerModelSamples', 1)})"
                )
            if freshness.get("status") == "stale-xemu-proof":
                if freshness.get("newerRuntimeSourceCount"):
                    run_failures.append("XEMU proof report predates runtime source artifacts")
                if freshness.get("newerReleaseArtifacts"):
                    run_failures.append("XEMU proof report predates release XBE artifacts")
                if freshness.get("runtimeSourcesNewerThanReleaseCount"):
                    run_failures.append("current release XBEs predate runtime source artifacts")
                if freshness.get("newerProofHarnessFiles"):
                    run_failures.append("XEMU proof report predates proof harness artifacts")
                run_failures.append(
                    "XEMU proof predates runtime source, release XBE, or proof harness artifacts"
                )
            elif freshness.get("status") not in ("pass", "proof-missing"):
                run_failures.append(f"XEMU proof freshness status is {freshness.get('status')}")
        run_failures.extend(xemu_contact_sheet_failures(contact))
        if not final_registers.is_file() or final_registers.stat().st_size <= 0:
            run_failures.append("final register dump missing or empty")

        status = "pass" if not run_failures else "fail"
        runs[name] = {
            "status": status,
            "mode": config["mode"],
            "map": config["map"],
            "criteria": {
                "minOkShots": config["minOkShots"],
                "minUniqueOkShotByteSizes": config["minUniqueOkShotByteSizes"],
                "minDurationSeconds": config["minDurationSeconds"],
                "requireAllocatorStable": config["requireAllocatorStable"],
                "requireP2Refdef": bool(config.get("requireP2Refdef", False)),
                "minP2RefdefSamples": int(config.get("minP2RefdefSamples", 0)),
                "requireDualPlayerModelState": bool(
                    config.get("requireDualPlayerModelState", False)
                ),
                "minDualPlayerModelSamples": int(config.get("minDualPlayerModelSamples", 0)),
            },
            "files": files,
            "freshness": freshness,
            "parsed": parsed,
            "failures": run_failures,
        }
        failures.extend(f"{name}: {failure}" for failure in run_failures)

    return {
        "status": "pass" if not failures else "fail",
        "proofHarnessFiles": proof_harness_files,
        "releaseRuntimeIdentities": release_identities,
        "runs": runs,
        "failures": failures,
    }


def checklist_item(
    item_id: str,
    label: str,
    status: str,
    evidence: object = None,
    details: object = None,
) -> dict[str, object]:
    item: dict[str, object] = {
        "id": item_id,
        "label": label,
        "status": status,
    }
    if evidence is not None:
        item["evidence"] = evidence
    if details is not None:
        item["details"] = details
    return item


def missing_log_modes(qualification_result: dict[str, object] | None) -> set[str]:
    if not isinstance(qualification_result, dict):
        return {"sp", "mp"}
    missing = qualification_result.get("missing", [])
    if not isinstance(missing, list):
        return set()
    modes: set[str] = set()
    for value in missing:
        if isinstance(value, str) and ":" in value:
            mode = value.split(":", 1)[0]
            if mode in {"sp", "mp"}:
                modes.add(mode)
    return modes


def mode_log_item_status(qualification_result: dict[str, object] | None, mode: str) -> str:
    if not isinstance(qualification_result, dict):
        return "missing"
    if mode in missing_log_modes(qualification_result):
        return "missing"
    logs = qualification_result.get("logs")
    if not isinstance(logs, dict):
        return "missing"
    result = logs.get(mode)
    if not isinstance(result, dict):
        return "missing"
    return "pass" if result.get("status") == "pass" else "fail"


def runtime_build_identity_status(
    stage_result: dict[str, object] | None,
    qualification_result: dict[str, object] | None,
) -> tuple[str, list[object]]:
    details: list[object] = []
    logs_missing = bool(missing_log_modes(qualification_result))
    required_xbes = ("default.xbe", "efmp.xbe")

    if not isinstance(stage_result, dict):
        return "missing", ["stage preflight report missing"]

    files = stage_result.get("files")
    if not isinstance(files, dict):
        return "missing", ["stage preflight did not report staged XBE identities"]

    for rel in required_xbes:
        record = files.get(rel)
        if not isinstance(record, dict):
            details.append(f"{rel}: stage preflight did not report XBE identity")
            continue
        staged_build_id = record.get("runtimeBuildId")
        source_build_id = record.get("sourceRuntimeBuildId")
        if not isinstance(source_build_id, str) or not source_build_id:
            details.append(f"{rel}: release XBE runtimeBuildId missing")
        if not isinstance(staged_build_id, str) or not staged_build_id:
            details.append(f"{rel}: staged XBE runtimeBuildId missing")
        if (
            isinstance(source_build_id, str)
            and source_build_id
            and isinstance(staged_build_id, str)
            and staged_build_id
            and source_build_id != staged_build_id
        ):
            details.append(f"{rel}: release/staged runtimeBuildId mismatch")

    if isinstance(qualification_result, dict):
        runtime_build_id_failures = qualification_result.get("runtimeBuildIdFailures")
        if isinstance(runtime_build_id_failures, list):
            details.extend(runtime_build_id_failures)
        logs = qualification_result.get("logs")
        if isinstance(logs, dict):
            for mode in ("sp", "mp"):
                result = logs.get(mode)
                if not isinstance(result, dict):
                    continue
                runtime_build_id = result.get("runtimeBuildId")
                if not isinstance(runtime_build_id, dict):
                    details.append(f"{mode}: returned log runtimeBuildId summary missing")
                elif runtime_build_id.get("present") is not True:
                    details.append(f"{mode}: returned log runtimeBuildId did not match manifest")

    if details:
        return "fail", details
    if logs_missing:
        return "missing", ["returned SP/Holomatch logs are required to prove runtime build identity"]
    return "pass", []


def build_acceptance_checklist(
    stage_result: dict[str, object] | None,
    qualification_result: dict[str, object] | None,
    contract_evidence: dict[str, object] | None = None,
    xemu_evidence: dict[str, object] | None = None,
) -> list[dict[str, object]]:
    items: list[dict[str, object]] = []

    stage_status = stage_result.get("status") if isinstance(stage_result, dict) else None
    items.append(
        checklist_item(
            "stage-transfer-integrity",
            "Staged XBE/PK3 transfer integrity and package architecture",
            "pass" if stage_status == "pass" else "fail" if stage_status == "fail" else "missing",
            evidence=stage_result.get("stage") if isinstance(stage_result, dict) else None,
            details=stage_result.get("failures") if isinstance(stage_result, dict) else None,
        )
    )

    runtime_identity_status, runtime_identity_details = runtime_build_identity_status(
        stage_result,
        qualification_result,
    )
    items.append(
        checklist_item(
            "runtime-build-identity-binding",
            "Staged XBE runtime build IDs bind release artifacts and returned logs to the tested binaries",
            runtime_identity_status,
            details=runtime_identity_details,
        )
    )

    for mode, label in (("sp", "SP/co-op retail runtime log"), ("mp", "Holomatch retail runtime log")):
        status = mode_log_item_status(qualification_result, mode)
        log_details = None
        evidence = None
        if isinstance(qualification_result, dict):
            logs = qualification_result.get("logs")
            if isinstance(logs, dict):
                log_record = logs.get(mode)
                if isinstance(log_record, dict):
                    evidence = log_record.get("log")
                    log_details = log_record.get("failures")
                elif isinstance(log_record, str):
                    evidence = log_record
        items.append(checklist_item(f"{mode}-runtime-log", label, status, evidence, log_details))

    logs_missing = bool(missing_log_modes(qualification_result))
    required_map_failures: object = None
    memory_failures: object = None
    observation_memory_failures: list[str] = []
    renderer_path_failures: list[str] = []
    if isinstance(qualification_result, dict):
        required_map_failures = qualification_result.get("requiredLogMapFailures")
        memory_failures = qualification_result.get("memoryFailures")
        observation_record = qualification_result.get("observation")
        if isinstance(observation_record, dict):
            observation_failures = observation_record.get("failures")
            if isinstance(observation_failures, list):
                observation_memory_failures = [
                    failure
                    for failure in observation_failures
                    if isinstance(failure, str) and "memory" in failure
                ]
        logs = qualification_result.get("logs")
        if isinstance(logs, dict):
            for mode in ("sp", "mp"):
                result = logs.get(mode)
                if not isinstance(result, dict):
                    continue
                renderer_path = result.get("rendererPath")
                if not isinstance(renderer_path, dict) or renderer_path.get("allRetailPushPath") is not True:
                    renderer_path_failures.append(mode)

    required_map_status = "missing" if logs_missing else "pass"
    if required_map_failures:
        required_map_status = "fail"
    items.append(
        checklist_item(
            "required-map-coverage",
            "Required representative SP, co-op, and Holomatch maps appear in returned logs and observation",
            required_map_status,
            details=required_map_failures,
        )
    )

    renderer_path_status = "missing" if logs_missing else "pass"
    if renderer_path_failures:
        renderer_path_status = "fail"
    items.append(
        checklist_item(
            "retail-renderer-path",
            "Returned heartbeats prove the shared native retail D3D8 push path",
            renderer_path_status,
            details=renderer_path_failures,
        )
    )

    memory_status = "missing" if logs_missing else "pass"
    combined_memory_failures: list[object] = []
    if isinstance(memory_failures, list):
        combined_memory_failures.extend(memory_failures)
    elif memory_failures:
        combined_memory_failures.append(memory_failures)
    combined_memory_failures.extend(observation_memory_failures)
    if combined_memory_failures:
        memory_status = "fail"
    items.append(
        checklist_item(
            "memory-thresholds",
            "Returned heartbeats and co-op observation satisfy configured memory stability thresholds",
            memory_status,
            details=combined_memory_failures,
        )
    )

    observation = qualification_result.get("observation") if isinstance(qualification_result, dict) else None
    observation_status = "missing"
    observation_details = None
    observation_evidence = None
    if isinstance(observation, dict):
        raw_status = observation.get("status")
        if raw_status == "pass":
            observation_status = "pass"
        elif raw_status == "fail":
            observation_status = "fail"
        else:
            observation_status = "missing"
        observation_details = observation.get("failures")
        observation_evidence = observation.get("path")
    items.append(
        checklist_item(
            "visible-fps-visual-gameplay-observation",
            "Filled hardware observation proves visible FPS, loading, HUD, lighting, controls, gameplay, stalls, and bot/combat checks",
            observation_status,
            observation_evidence,
            observation_details,
        )
    )

    evidence_status = "missing"
    evidence_details: list[object] = []
    if isinstance(observation, dict):
        evidence_files = observation.get("evidenceFiles")
        if isinstance(evidence_files, dict):
            evidence_status = "pass"
            for mode in OBSERVATION_MODES:
                mode_records = evidence_files.get(mode, [])
                if not mode_records:
                    evidence_status = "missing"
                    evidence_details.append({mode: "no evidence files listed"})
                    continue
                if not isinstance(mode_records, list):
                    evidence_status = "fail"
                    evidence_details.append({mode: "invalid evidence record"})
                    continue
                missing_or_invalid = [
                    record
                    for record in mode_records
                    if not isinstance(record, dict) or record.get("status") != "present"
                ]
                if missing_or_invalid:
                    evidence_status = "fail"
                    evidence_details.extend(missing_or_invalid)
        elif observation_status == "fail":
            evidence_status = "fail"
            evidence_details = observation.get("failures", [])
    items.append(
        checklist_item(
            "visual-evidence-files",
            "Observation evidence files exist and have SHA256 provenance",
            evidence_status,
            details=evidence_details,
        )
    )

    coop_status = "missing"
    coop_details = None
    if isinstance(observation, dict):
        failures = observation.get("failures", [])
        if not isinstance(failures, list):
            failures = []
        coop_failures = [
            failure for failure in failures if isinstance(failure, str) and failure.startswith("coop:")
        ]
        summary = observation.get("summary")
        coop_summary = summary.get("coop") if isinstance(summary, dict) else None
        if observation.get("status") == "pass":
            coop_status = "pass"
        elif coop_failures:
            coop_status = "fail"
            coop_details = coop_failures
        elif not isinstance(coop_summary, dict):
            coop_status = "missing"
        else:
            coop_status = "missing"
    items.append(
        checklist_item(
            "coop-splitscreen-observation",
            "Filled co-op observation proves split-screen, P2 HUD, P2 controls, visible FPS, and gameplay",
            coop_status,
            details=coop_details,
        )
    )

    contract_status = "missing"
    contract_details = None
    if isinstance(contract_evidence, dict):
        raw_status = contract_evidence.get("status")
        contract_status = "pass" if raw_status == "pass" else "fail"
        contract_details = contract_evidence.get("failures")
    items.append(
        checklist_item(
            "retail-renderer-contract-provenance",
            "Saved comparison artifacts tie the shared renderer back to the shipping retail jamp.xbe contract",
            contract_status,
            details=contract_details,
        )
    )

    xemu_status = "missing"
    xemu_details = None
    if isinstance(xemu_evidence, dict):
        raw_status = xemu_evidence.get("status")
        xemu_status = "pass" if raw_status == "pass" else "fail"
        xemu_details = xemu_evidence.get("failures")
    items.append(
        checklist_item(
            "xemu-emulator-proof-provenance",
            "Saved XEMU SP, co-op split-screen, and Holomatch proof artifacts are present and meet conservative liveness evidence checks",
            xemu_status,
            details=xemu_details,
        )
    )

    qualification_status = (
        qualification_result.get("status") if isinstance(qualification_result, dict) else None
    )
    final_status = determine_overall_status(stage_status, qualification_status)
    if contract_status == "fail" or xemu_status == "fail":
        final_status = "fail"
    items.append(
        checklist_item(
            "production-qualification-verdict",
            "Both retail XBEs satisfy the configured production hardware qualification gate",
            "pass" if final_status == "pass" else "fail" if final_status == "fail" else "missing",
            details={"overall": final_status},
        )
    )

    return items


def open_acceptance_items(checklist: list[dict[str, object]]) -> list[dict[str, object]]:
    return [
        {
            "id": str(item.get("id", "")),
            "status": str(item.get("status", "")),
            "label": str(item.get("label", "")),
        }
        for item in checklist
        if item.get("status") in {"missing", "fail"}
    ]


def run_self_test() -> int:
    expected = {
        ("pass", "pass"): "pass",
        ("pass", "missing"): "missing-runtime-proof",
        ("fail", "missing"): "fail",
        ("fail", "pass"): "fail",
        ("pass", "fail"): "fail",
        ("pass", None): "incomplete",
        (None, "pass"): "incomplete",
    }
    for inputs, output in expected.items():
        result = determine_overall_status(*inputs)
        if result != output:
            print(
                "self-test failed: "
                f"determine_overall_status{inputs!r} returned {result!r}, expected {output!r}",
                file=sys.stderr,
            )
            return 1
    checklist = build_acceptance_checklist(
        {"stage": "stage", "status": "pass", "failures": []},
        {
            "status": "missing",
            "missing": ["sp:stage/ef_sp_log.txt", "mp:stage/ef_mp_log.txt"],
            "logs": {
                "sp": "stage/ef_sp_log.txt",
                "mp": "stage/ef_mp_log.txt",
            },
            "observation": {
                "status": "fail",
                "path": "stage/HARDWARE_OBSERVATION.json",
                "failures": ["sp: mapsTested must include at least one map"],
            },
        },
        {"status": "pass", "failures": []},
        {"status": "pass", "failures": []},
    )
    statuses = {item["id"]: item["status"] for item in checklist}
    labels = {item["id"]: item["label"] for item in checklist}
    if (
        statuses.get("stage-transfer-integrity") != "pass"
        or statuses.get("sp-runtime-log") != "missing"
        or statuses.get("mp-runtime-log") != "missing"
        or statuses.get("visible-fps-visual-gameplay-observation") != "fail"
        or statuses.get("production-qualification-verdict") != "missing"
        or "co-op" not in str(labels.get("required-map-coverage", ""))
    ):
        print("self-test failed: acceptance checklist did not summarize missing proof", file=sys.stderr)
        print(json.dumps(checklist, indent=2), file=sys.stderr)
        return 1
    open_items = open_acceptance_items(checklist)
    open_ids = [item["id"] for item in open_items]
    if (
        "sp-runtime-log" not in open_ids
        or "visible-fps-visual-gameplay-observation" not in open_ids
        or not all({"id", "status", "label"} <= set(item) for item in open_items)
    ):
        print("self-test failed: open acceptance item summary is incomplete", file=sys.stderr)
        print(json.dumps(open_items, indent=2), file=sys.stderr)
        return 1
    identity_stage = {
        "status": "pass",
        "files": {
            "default.xbe": {
                "runtimeBuildId": "STEFX_RUNTIME_BUILD_ID personality=default",
                "sourceRuntimeBuildId": "STEFX_RUNTIME_BUILD_ID personality=default",
            },
            "efmp.xbe": {
                "runtimeBuildId": "STEFX_RUNTIME_BUILD_ID personality=efmp",
                "sourceRuntimeBuildId": "STEFX_RUNTIME_BUILD_ID personality=efmp",
            },
        },
    }
    identity_missing_status, identity_missing_details = runtime_build_identity_status(
        identity_stage,
        {
            "status": "missing",
            "missing": ["sp:stage/ef_sp_log.txt", "mp:stage/ef_mp_log.txt"],
            "logs": {
                "sp": "stage/ef_sp_log.txt",
                "mp": "stage/ef_mp_log.txt",
            },
        },
    )
    if identity_missing_status != "missing" or not identity_missing_details:
        print("self-test failed: missing returned logs did not leave identity binding missing", file=sys.stderr)
        print(json.dumps(identity_missing_details, indent=2), file=sys.stderr)
        return 1
    identity_pass_status, identity_pass_details = runtime_build_identity_status(
        identity_stage,
        {
            "status": "pass",
            "logs": {
                "sp": {
                    "runtimeBuildId": {
                        "expected": "STEFX_RUNTIME_BUILD_ID personality=default",
                        "present": True,
                    },
                },
                "mp": {
                    "runtimeBuildId": {
                        "expected": "STEFX_RUNTIME_BUILD_ID personality=efmp",
                        "present": True,
                    },
                },
            },
        },
    )
    if identity_pass_status != "pass" or identity_pass_details:
        print("self-test failed: matching runtime identity binding did not pass", file=sys.stderr)
        print(json.dumps(identity_pass_details, indent=2), file=sys.stderr)
        return 1
    identity_fail_stage = json.loads(json.dumps(identity_stage))
    identity_fail_stage["files"]["efmp.xbe"]["runtimeBuildId"] = None
    identity_fail_status, identity_fail_details = runtime_build_identity_status(
        identity_fail_stage,
        {
            "status": "pass",
            "runtimeBuildIdFailures": [
                "efmp.xbe: manifest runtimeBuildId is missing; cannot bind mp log"
            ],
            "logs": {
                "sp": {
                    "runtimeBuildId": {
                        "expected": "STEFX_RUNTIME_BUILD_ID personality=default",
                        "present": True,
                    },
                },
                "mp": {
                    "runtimeBuildId": {
                        "expected": None,
                        "present": False,
                    },
                },
            },
        },
    )
    if (
        identity_fail_status != "fail"
        or not any("efmp.xbe" in str(detail) for detail in identity_fail_details)
    ):
        print("self-test failed: missing staged efmp runtime identity did not fail", file=sys.stderr)
        print(json.dumps(identity_fail_details, indent=2), file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        expected_stage = root / "stage"
        stage_report = root / "stage_report.json"
        qualification_report = root / "qualification_report.json"
        stage_script = root / "verify_hardware_stage.py"
        qualification_script = root / "verify_production_hardware_logs.py"
        stage_script.write_text(
            "REPORT_TYPE = %r\nREPORT_SCHEMA_VERSION = %d\n"
            % (EXPECTED_STAGE_REPORT_TYPE, EXPECTED_STAGE_REPORT_SCHEMA_VERSION),
            encoding="utf-8",
        )
        qualification_script.write_text(
            "REPORT_TYPE = %r\nREPORT_SCHEMA_VERSION = %d\n"
            % (EXPECTED_PRODUCTION_REPORT_TYPE, EXPECTED_PRODUCTION_REPORT_SCHEMA_VERSION),
            encoding="utf-8",
        )
        shared_manifest = {
            "path": str((root / "HARDWARE_PATCH_MANIFEST.json").resolve()),
            "bytes": 1234,
            "sha256": "ABCDEF",
        }
        stage_argv = ["--stage", str(expected_stage.resolve()), "--json"]
        qualification_argv = ["--stage", str(expected_stage.resolve()), "--require-observation"]
        good_subreport_failures = report_contract_failures(
            {
                "reportType": EXPECTED_STAGE_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_STAGE_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(stage_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(stage_script.resolve()),
                    "scriptSha256": file_sha256(stage_script),
                    "argv": list(stage_argv),
                },
            },
            {
                "reportType": EXPECTED_PRODUCTION_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_PRODUCTION_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(qualification_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(qualification_script.resolve()),
                    "scriptSha256": file_sha256(qualification_script),
                    "argv": list(qualification_argv),
                },
            },
            expected_stage,
            stage_report,
            qualification_report,
            stage_script,
            qualification_script,
            stage_argv,
            qualification_argv,
        )
        if good_subreport_failures:
            print("self-test failed: valid subreport contracts were rejected", file=sys.stderr)
            print(json.dumps(good_subreport_failures, indent=2), file=sys.stderr)
            return 1
        stale_subreport_failures = report_contract_failures(
            {
                "reportType": EXPECTED_STAGE_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_STAGE_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(stage_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(stage_script.resolve()),
                    "scriptSha256": file_sha256(stage_script),
                    "argv": list(stage_argv),
                },
            },
            {
                "reportType": EXPECTED_PRODUCTION_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_PRODUCTION_REPORT_SCHEMA_VERSION - 1,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(qualification_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(qualification_script.resolve()),
                    "scriptSha256": file_sha256(qualification_script),
                    "argv": list(qualification_argv),
                },
            },
            expected_stage,
            stage_report,
            qualification_report,
            stage_script,
            qualification_script,
            stage_argv,
            qualification_argv,
        )
        if not any("productionQualification: reportSchemaVersion" in failure for failure in stale_subreport_failures):
            print("self-test failed: stale production subreport schema was not rejected", file=sys.stderr)
            print(json.dumps(stale_subreport_failures, indent=2), file=sys.stderr)
            return 1
        wrong_path_subreport_failures = report_contract_failures(
            {
                "reportType": EXPECTED_STAGE_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_STAGE_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str((root / "different_stage_report.json").resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(stage_script.resolve()),
                    "scriptSha256": file_sha256(stage_script),
                    "argv": list(stage_argv),
                },
            },
            {
                "reportType": EXPECTED_PRODUCTION_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_PRODUCTION_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(qualification_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(qualification_script.resolve()),
                    "scriptSha256": file_sha256(qualification_script),
                    "argv": list(qualification_argv),
                },
            },
            expected_stage,
            stage_report,
            qualification_report,
            stage_script,
            qualification_script,
            stage_argv,
            qualification_argv,
        )
        if not any("stagePreflight: reportPath" in failure for failure in wrong_path_subreport_failures):
            print("self-test failed: mismatched subreport path was not rejected", file=sys.stderr)
            print(json.dumps(wrong_path_subreport_failures, indent=2), file=sys.stderr)
            return 1
        wrong_script_subreport_failures = report_contract_failures(
            {
                "reportType": EXPECTED_STAGE_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_STAGE_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(stage_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(stage_script.resolve()),
                    "scriptSha256": "BAD",
                    "argv": list(stage_argv),
                },
            },
            {
                "reportType": EXPECTED_PRODUCTION_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_PRODUCTION_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(qualification_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(qualification_script.resolve()),
                    "scriptSha256": file_sha256(qualification_script),
                    "argv": list(qualification_argv),
                },
            },
            expected_stage,
            stage_report,
            qualification_report,
            stage_script,
            qualification_script,
            stage_argv,
            qualification_argv,
        )
        if not any("stagePreflight: verifierProvenance.scriptSha256" in failure for failure in wrong_script_subreport_failures):
            print("self-test failed: mismatched verifier script hash was not rejected", file=sys.stderr)
            print(json.dumps(wrong_script_subreport_failures, indent=2), file=sys.stderr)
            return 1
        qualification_script.write_text(
            "REPORT_TYPE = %r\nREPORT_SCHEMA_VERSION = %d\n"
            % (EXPECTED_PRODUCTION_REPORT_TYPE, EXPECTED_PRODUCTION_REPORT_SCHEMA_VERSION + 1),
            encoding="utf-8",
        )
        drift_subreport_failures = report_contract_failures(
            {
                "reportType": EXPECTED_STAGE_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_STAGE_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(stage_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(stage_script.resolve()),
                    "scriptSha256": file_sha256(stage_script),
                    "argv": list(stage_argv),
                },
            },
            {
                "reportType": EXPECTED_PRODUCTION_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_PRODUCTION_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(qualification_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(qualification_script.resolve()),
                    "scriptSha256": file_sha256(qualification_script),
                    "argv": list(qualification_argv),
                },
            },
            expected_stage,
            stage_report,
            qualification_report,
            stage_script,
            qualification_script,
            stage_argv,
            qualification_argv,
        )
        if not any("does not match verifier source" in failure for failure in drift_subreport_failures):
            print("self-test failed: verifier source schema drift was not rejected", file=sys.stderr)
            print(json.dumps(drift_subreport_failures, indent=2), file=sys.stderr)
            return 1
        mismatched_manifest = dict(shared_manifest)
        mismatched_manifest["sha256"] = "123456"
        manifest_mismatch_failures = report_contract_failures(
            {
                "reportType": EXPECTED_STAGE_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_STAGE_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(stage_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(stage_script.resolve()),
                    "scriptSha256": file_sha256(stage_script),
                    "argv": list(stage_argv),
                },
            },
            {
                "reportType": EXPECTED_PRODUCTION_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_PRODUCTION_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(qualification_report.resolve()),
                "manifest": mismatched_manifest,
                "verifierProvenance": {
                    "script": str(qualification_script.resolve()),
                    "scriptSha256": file_sha256(qualification_script),
                    "argv": list(qualification_argv),
                },
            },
            expected_stage,
            stage_report,
            qualification_report,
            stage_script,
            qualification_script,
            stage_argv,
            qualification_argv,
        )
        if not any("manifest sha256 mismatch" in failure for failure in manifest_mismatch_failures):
            print("self-test failed: child report manifest mismatch was not rejected", file=sys.stderr)
            print(json.dumps(manifest_mismatch_failures, indent=2), file=sys.stderr)
            return 1
        stage_mismatch_failures = report_contract_failures(
            {
                "reportType": EXPECTED_STAGE_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_STAGE_REPORT_SCHEMA_VERSION,
                "stage": str((root / "other_stage").resolve()),
                "reportPath": str(stage_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(stage_script.resolve()),
                    "scriptSha256": file_sha256(stage_script),
                    "argv": list(stage_argv),
                },
            },
            {
                "reportType": EXPECTED_PRODUCTION_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_PRODUCTION_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(qualification_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(qualification_script.resolve()),
                    "scriptSha256": file_sha256(qualification_script),
                    "argv": list(qualification_argv),
                },
            },
            expected_stage,
            stage_report,
            qualification_report,
            stage_script,
            qualification_script,
            stage_argv,
            qualification_argv,
        )
        if not any("stagePreflight: stage" in failure for failure in stage_mismatch_failures):
            print("self-test failed: child report stage mismatch was not rejected", file=sys.stderr)
            print(json.dumps(stage_mismatch_failures, indent=2), file=sys.stderr)
            return 1
        argv_mismatch_failures = report_contract_failures(
            {
                "reportType": EXPECTED_STAGE_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_STAGE_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(stage_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(stage_script.resolve()),
                    "scriptSha256": file_sha256(stage_script),
                    "argv": ["--different"],
                },
            },
            {
                "reportType": EXPECTED_PRODUCTION_REPORT_TYPE,
                "reportSchemaVersion": EXPECTED_PRODUCTION_REPORT_SCHEMA_VERSION,
                "stage": str(expected_stage.resolve()),
                "reportPath": str(qualification_report.resolve()),
                "manifest": dict(shared_manifest),
                "verifierProvenance": {
                    "script": str(qualification_script.resolve()),
                    "scriptSha256": file_sha256(qualification_script),
                    "argv": list(qualification_argv),
                },
            },
            expected_stage,
            stage_report,
            qualification_report,
            stage_script,
            qualification_script,
            stage_argv,
            qualification_argv,
        )
        if not any("stagePreflight: verifierProvenance.argv" in failure for failure in argv_mismatch_failures):
            print("self-test failed: child report argv mismatch was not rejected", file=sys.stderr)
            print(json.dumps(argv_mismatch_failures, indent=2), file=sys.stderr)
            return 1
        report_record = proof_file_record(stage_script)
        if (
            report_record.get("path") != str(stage_script)
            or report_record.get("bytes") != stage_script.stat().st_size
            or report_record.get("sha256") != file_sha256(stage_script)
        ):
            print("self-test failed: proof file record did not include bytes/SHA provenance", file=sys.stderr)
            print(json.dumps(report_record, indent=2), file=sys.stderr)
            return 1
    bad_contract_checklist = build_acceptance_checklist(
        {"stage": "stage", "status": "pass", "failures": []},
        {"status": "pass", "logs": {}, "observation": {"status": "pass", "evidenceFiles": {}}},
        {"status": "fail", "failures": ["missing contract report"]},
        {"status": "pass", "failures": []},
    )
    bad_contract_statuses = {item["id"]: item["status"] for item in bad_contract_checklist}
    if (
        bad_contract_statuses.get("retail-renderer-contract-provenance") != "fail"
        or bad_contract_statuses.get("production-qualification-verdict") != "fail"
    ):
        print("self-test failed: contract evidence failure did not fail final verdict", file=sys.stderr)
        print(json.dumps(bad_contract_checklist, indent=2), file=sys.stderr)
        return 1
    valid_ledger_validation = retail_contract_ledger_validation(
        [
            {
                "start": "0x000735D0",
                "end": "0x0007370D",
                "name": "RB_ExecuteRenderCommands",
                "object": "tr_backend.obj",
                "confidence": 1.0,
                "evidence": ["Called by the shipping retail jamp.xbe render-command issue path."],
                "contract": ["Consume the back-end command stream until RC_END_OF_LIST."],
            }
        ]
    )
    if (
        valid_ledger_validation.get("status") != "pass"
        or valid_ledger_validation.get("retailEvidenceRecords") != 1
    ):
        print("self-test failed: valid retail renderer contract ledger was rejected", file=sys.stderr)
        print(json.dumps(valid_ledger_validation, indent=2), file=sys.stderr)
        return 1
    invalid_ledger_validation = retail_contract_ledger_validation(
        [
            {
                "start": "0x000735D0",
                "end": "0x0007370D",
                "name": "RB_ExecuteRenderCommands",
                "object": "tr_backend.obj",
                "confidence": 1.0,
                "evidence": ["Only cites a generic source-level guess."],
                "contract": ["Consume the back-end command stream until RC_END_OF_LIST."],
            }
        ]
    )
    if (
        invalid_ledger_validation.get("status") != "fail"
        or not any("retail/shipping" in failure for failure in invalid_ledger_validation.get("failures", []))
    ):
        print("self-test failed: retail renderer contract ledger without shipping evidence was not rejected", file=sys.stderr)
        print(json.dumps(invalid_ledger_validation, indent=2), file=sys.stderr)
        return 1
    compare_script = resolve_path(repo_root_from_script(), Path("scripts/compare_retail_renderer_objects.py"))
    compare_rows = [
        {
            "current_object": f"current_{index}.obj",
            "donor_object": f"donor_{index}.obj",
            "currentFile": {
                "path": f"C:\\repo\\current_{index}.obj",
                "sha256": "A" * 64,
                "modifiedUtc": "2026-08-19T10:00:00Z",
            },
            "donorFile": {
                "path": f"C:\\repo\\donor_{index}.obj",
                "sha256": "B" * 64,
                "modifiedUtc": "2026-08-19T10:00:00Z",
            },
        }
        for index in range(31)
    ]
    valid_compare_report = {
        "reportType": EXPECTED_OBJECT_COMPARE_REPORT_TYPE,
        "reportSchemaVersion": EXPECTED_OBJECT_COMPARE_REPORT_SCHEMA_VERSION,
        "generatedAtUtc": "2026-08-19T12:00:00Z",
        "verifierProvenance": {
            "scriptSha256": file_sha256(compare_script),
            "argv": ["--current-root", "current", "--donor-root", "donor"],
        },
        "inputs": {
            "currentRoot": {"path": "C:\\repo\\current", "present": True},
            "donorRoot": {"path": "C:\\repo\\donor", "present": True},
            "link": {"path": "C:\\xdk\\link.exe", "sha256": "C" * 64},
            "objectPairs": {f"current_{index}.obj": f"donor_{index}.obj" for index in range(31)},
        },
        "summary": {"objects": 31, "common_functions": 2000, "exact_instruction_text": 1400},
        "objects": compare_rows,
    }
    valid_compare_validation = object_compare_report_validation(valid_compare_report, repo_root_from_script())
    if valid_compare_validation.get("status") != "pass":
        print("self-test failed: valid object-compare report provenance was rejected", file=sys.stderr)
        print(json.dumps(valid_compare_validation, indent=2), file=sys.stderr)
        return 1
    invalid_compare_report = dict(valid_compare_report)
    invalid_compare_report.pop("verifierProvenance")
    invalid_compare_validation = object_compare_report_validation(invalid_compare_report, repo_root_from_script())
    if (
        invalid_compare_validation.get("status") != "fail"
        or not any("verifierProvenance" in failure for failure in invalid_compare_validation.get("failures", []))
    ):
        print("self-test failed: object-compare report without provenance was not rejected", file=sys.stderr)
        print(json.dumps(invalid_compare_validation, indent=2), file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="stefx_contract_freshness_") as tmp_name:
        temp_root = Path(tmp_name)
        artifact = temp_root / "build" / "analysis" / "object-compare.json"
        source = temp_root / "code" / "renderer" / "retail_xbox" / "tr_backend_retail.cpp"
        artifact.parent.mkdir(parents=True, exist_ok=True)
        source.parent.mkdir(parents=True, exist_ok=True)
        artifact.write_text("{}", encoding="utf-8")
        source.write_text("// source\n", encoding="utf-8")
        old_time = datetime(2026, 8, 19, 10, 0, tzinfo=timezone.utc).timestamp()
        new_time = datetime(2026, 8, 19, 11, 0, tzinfo=timezone.utc).timestamp()
        os.utime(source, (old_time, old_time))
        os.utime(artifact, (new_time, new_time))
        fresh_contract = contract_artifact_freshness(
            temp_root,
            Path("build/analysis/object-compare.json"),
            (Path("code/renderer/retail_xbox/tr_backend_retail.cpp"),),
        )
        if fresh_contract.get("status") != "pass":
            print("self-test failed: fresh contract artifact was marked stale", file=sys.stderr)
            print(json.dumps(fresh_contract, indent=2), file=sys.stderr)
            return 1
        os.utime(source, (new_time + 60, new_time + 60))
        stale_contract = contract_artifact_freshness(
            temp_root,
            Path("build/analysis/object-compare.json"),
            (Path("code/renderer/retail_xbox/tr_backend_retail.cpp"),),
        )
        if (
            stale_contract.get("status") != "stale-contract-artifact"
            or stale_contract.get("newerInputCount") != 1
        ):
            print("self-test failed: stale contract artifact was not detected", file=sys.stderr)
            print(json.dumps(stale_contract, indent=2), file=sys.stderr)
            return 1
    current_retail_source_contract = validate_retail_renderer_source_contract(repo_root_from_script())
    if current_retail_source_contract.get("status") != "pass":
        print("self-test failed: current retail renderer source contract did not pass", file=sys.stderr)
        print(json.dumps(current_retail_source_contract, indent=2), file=sys.stderr)
        return 1
    current_build_contract = build_script_contract_evidence(repo_root_from_script())
    if current_build_contract.get("status") != "pass":
        print("self-test failed: current build_xbox contract evidence did not pass", file=sys.stderr)
        print(json.dumps(current_build_contract, indent=2), file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="stefx_bad_retail_source_contract_") as tmp_name:
        temp_root = Path(tmp_name)
        for rel_path in RETAIL_RENDERER_SOURCE_FILES:
            path = temp_root / rel_path
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                '#include "retail_renderer_contract.h"\n'
                "STEFX_RETAIL_NAMESPACE_BEGIN\n"
                "STEFX_RETAIL_NAMESPACE_END\n",
                encoding="utf-8",
            )
        header = temp_root / "code" / "renderer" / "retail_xbox" / "retail_renderer_contract.h"
        header.write_text(
            "STEFX_RETAIL_RENDERER_ACTIVE\nSTEFX_RETAIL_SCOPE\nSTEFX_RETAIL_NAMESPACE_BEGIN\n",
            encoding="utf-8",
        )
        project = temp_root / "code" / "x_exe" / "x_exe.vcproj"
        project.parent.mkdir(parents=True, exist_ok=True)
        project.write_text(
            "\n".join(
                f'<File RelativePath="{vcproj_relative_path(rel_path)}" />'
                for rel_path in RETAIL_RENDERER_SOURCE_FILES
            ),
            encoding="utf-8",
        )
        build_script = temp_root / BUILD_SCRIPT_UNDER_TEST
        build_script.parent.mkdir(parents=True, exist_ok=True)
        build_script.write_text(
            "\n".join(RETAIL_RENDERER_REQUIRED_DEFINES)
            + "\n"
            + "\n".join(vcproj_relative_path(rel_path) for rel_path in RETAIL_RENDERER_REPLACED_LEGACY_SOURCES)
            + "\nretailRendererReplacements -icontains $source.RelativePath\n",
            encoding="utf-8",
        )
        good_source_contract = validate_retail_renderer_source_contract(temp_root)
        if good_source_contract.get("status") != "pass":
            print("self-test failed: valid synthetic retail renderer source contract was rejected", file=sys.stderr)
            print(json.dumps(good_source_contract, indent=2), file=sys.stderr)
            return 1
        first_source = temp_root / RETAIL_RENDERER_SOURCE_FILES[0]
        first_source.write_text(
            "STEFX_RETAIL_NAMESPACE_BEGIN\nSTEFX_RETAIL_NAMESPACE_END\n",
            encoding="utf-8",
        )
        bad_source_contract = validate_retail_renderer_source_contract(temp_root)
        if (
            bad_source_contract.get("status") != "fail"
            or not any("retail_renderer_contract.h include" in failure for failure in bad_source_contract.get("failures", []))
        ):
            print("self-test failed: retail renderer source without contract include was not rejected", file=sys.stderr)
            print(json.dumps(bad_source_contract, indent=2), file=sys.stderr)
            return 1
    with tempfile.TemporaryDirectory(prefix="stefx_bad_build_contract_") as tmp_name:
        temp_root = Path(tmp_name)
        scripts_dir = temp_root / "scripts"
        scripts_dir.mkdir(parents=True, exist_ok=True)
        source_verifier = resolve_path(repo_root_from_script(), BUILD_SCRIPT_CONTRACT_VERIFIER)
        (scripts_dir / "verify_build_xbox_contracts.py").write_text(
            source_verifier.read_text(encoding="utf-8-sig"), encoding="utf-8"
        )
        (scripts_dir / "build_xbox.ps1").write_text(
            """
function Invoke-BuildGraphForTarget {
    $previousBuildTarget = $script:StefxBuildTarget
    $script:StefxBuildTarget = $BuildTarget
    try {
        Invoke-BuildGraph -Projects $Projects
    } finally {
        $script:StefxBuildTarget = $previousBuildTarget
    }
}
switch ($Target) {
    "sp" {
        Invoke-BuildGraphForTarget -BuildTarget "sp" -Projects $spProjects
        Assert-ActiveReleaseXbes @((Join-Path $repoReleaseDir "default.xbe"))
    }
    "mp" {
        Invoke-BuildGraphForTarget -BuildTarget "mp" -Projects $mpProjects
    }
    "spmp" {
        Invoke-BuildGraph -Projects $spProjects
        Assert-ActiveReleaseXbes @(
            (Join-Path $repoReleaseDir "default.xbe"),
            (Join-Path $repoReleaseDir "efmp.xbe")
        )
    }
    "all" {
        Invoke-BuildGraphForTarget -BuildTarget "sp" -Projects $spProjects
        Invoke-BuildGraphForTarget -BuildTarget "mp" -Projects $mpProjects
    }
}
""",
            encoding="utf-8",
        )
        bad_build_contract = build_script_contract_evidence(temp_root)
        if (
            bad_build_contract.get("status") != "fail"
            or not any("target contract" in str(failure) for failure in bad_build_contract.get("failures", []))
        ):
            print("self-test failed: bad spmp build contract evidence was not rejected", file=sys.stderr)
            print(json.dumps(bad_build_contract, indent=2), file=sys.stderr)
            return 1
    bad_xemu_checklist = build_acceptance_checklist(
        {"stage": "stage", "status": "pass", "failures": []},
        {"status": "pass", "logs": {}, "observation": {"status": "pass", "evidenceFiles": {}}},
        {"status": "pass", "failures": []},
        {"status": "fail", "failures": ["missing XEMU report"]},
    )
    bad_xemu_statuses = {item["id"]: item["status"] for item in bad_xemu_checklist}
    if (
        bad_xemu_statuses.get("xemu-emulator-proof-provenance") != "fail"
        or bad_xemu_statuses.get("production-qualification-verdict") != "fail"
    ):
        print("self-test failed: XEMU evidence failure did not fail final verdict", file=sys.stderr)
        print(json.dumps(bad_xemu_checklist, indent=2), file=sys.stderr)
        return 1
    if "coopBorg1SplitScreen" not in XEMU_EVIDENCE_RUNS:
        print("self-test failed: XEMU evidence config is missing co-op split-screen proof", file=sys.stderr)
        return 1
    coop_config = XEMU_EVIDENCE_RUNS["coopBorg1SplitScreen"]
    if (
        coop_config.get("mode") != "coop"
        or coop_config.get("map") != "borg1"
        or int(coop_config.get("minOkShots", 0)) < 8
        or float(coop_config.get("minDurationSeconds", 0.0)) < 90.0
        or int(coop_config.get("minUniqueOkShotByteSizes", 0)) < 6
        or coop_config.get("requireP2Refdef") is not True
        or int(coop_config.get("minP2RefdefSamples", 0)) < 8
        or coop_config.get("requireDualPlayerModelState") is not True
        or int(coop_config.get("minDualPlayerModelSamples", 0)) < 8
    ):
        print("self-test failed: co-op XEMU evidence criteria are too weak", file=sys.stderr)
        print(json.dumps(coop_config, indent=2, default=str), file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_report = Path(temp_dir) / "xemu.report.txt"
        temp_report.write_text(
            "\n".join(
                [
                    "proof_context mode=coop map=borg1 name=selftest duration=100 interval=10 display=xemu nativeScreenshots=True",
                    "shot=00 t=10.0 ok=True bytes=123 detail=xemu native screenshot",
                    "shot=01 t=20.0 ok=True bytes=123 detail=xemu native screenshot",
                    "shot=02 t=30.0 ok=True bytes=456 detail=xemu native screenshot",
                    "shot=03 t=40.0 ok=False bytes=0 detail=miss",
                    "xblogtex t=20.0 base=0x0 used=1024 capacity=2048 high=0 blocks=0 totalFree=1024 largestFree=1024 complete=1",
                    "xblogtex t=30.0 base=0x0 used=1024 capacity=2048 high=0 blocks=0 totalFree=1024 largestFree=1024 complete=1",
                    "xblog t=30.0 p2dbg=ref=1 model=193/193 rf=0x0100000d/0x0200000d",
                    "runtime_xbe_identity path=C:\\repo\\build\\release\\default.xbe present=True bytes=12345 sha256=ABCDEF runtimeBuildId=STEFX_RUNTIME_BUILD_ID personality=default flavor=production date=Aug 19 2026 time=12:00:00 log=ef_sp_log.txt",
                    "alive_at_end pid=1",
                    "xemu_version: 0.test",
                    "xemu_commit: abc",
                ]
            )
            + "\n",
            encoding="utf-8",
        )
        parsed = parse_xemu_report(temp_report)
        if (
            parsed.get("okShotCount") != 3
            or parsed.get("uniqueOkShotByteSizeCount") != 2
            or parsed.get("allocatorStable") is not True
            or parsed.get("durationSeconds") != 40.0
            or parsed.get("p2RefdefSamples") != 1
            or parsed.get("dualPlayerModelSamples") != 1
            or parsed.get("proofContext") != {
                "mode": "coop",
                "map": "borg1",
                "name": "selftest",
                "duration": "100",
                "interval": "10",
                "display": "xemu",
                "nativeScreenshots": "True",
            }
            or not parsed.get("runtimeXbeIdentities")
            or parsed["runtimeXbeIdentities"][0].get("basename") != "default.xbe"
        ):
            print("self-test failed: XEMU report parser did not summarize screenshot diversity", file=sys.stderr)
            print(json.dumps(parsed, indent=2), file=sys.stderr)
            return 1
    with tempfile.TemporaryDirectory() as temp_dir:
        xbe = Path(temp_dir) / "default.xbe"
        expected_id = (
            "STEFX_RUNTIME_BUILD_ID personality=default flavor=production "
            "date=Aug 19 2026 time=12:00:00 log=ef_sp_log.txt"
        )
        xbe.write_bytes(b"prefix\0" + expected_id.encode("ascii") + b"\0suffix")
        if runtime_build_id(xbe) != expected_id:
            print("self-test failed: runtime build id extractor did not find marker", file=sys.stderr)
            return 1
    override_runs = xemu_evidence_runs_with_overrides(
        sp_report=Path("scripts/output/new-sp.report.txt"),
        coop_report=Path("scripts/output/new-coop.report.txt"),
        mp_report=Path("scripts/output/new-mp.report.txt"),
    )
    if (
        override_runs["spBorg2"]["contact"] != Path("scripts/output/new-sp_contact.png")
        or override_runs["coopBorg1SplitScreen"]["finalRegisters"]
        != Path("scripts/output/new-coop_final_registers.txt")
        or override_runs["hmBorg1Soak"]["report"] != Path("scripts/output/new-mp.report.txt")
    ):
        print("self-test failed: XEMU report override paths were not derived correctly", file=sys.stderr)
        print(json.dumps(override_runs, indent=2, default=str), file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        refresh_report = root / "scripts" / "output" / "refresh.json"
        refresh_report.parent.mkdir(parents=True)
        refresh_payload = {
            "reportType": EXPECTED_XEMU_REFRESH_REPORT_TYPE,
            "reportSchemaVersion": EXPECTED_XEMU_REFRESH_REPORT_SCHEMA_VERSION,
            "generatedAtUtc": "2026-08-19T12:00:00Z",
            "repoRoot": str(root),
            "stage": str(root / "stage"),
            "releasePreflight": {
                "label": "Release freshness preflight",
                "command": [
                    "-File",
                    str(root / "scripts" / "stage_hardware_pk3_test.ps1"),
                    "-OutputDir",
                    str(root / "stage"),
                    "-CheckFreshnessOnly",
                ],
                "exitCode": 0,
                "output": [
                    "build_xbox.ps1 target contract ok",
                "build\\release XBE freshness ok",
                "build\\release PK3 freshness ok",
                "build\\release XBE runtime build ids ok",
                "  build\\release\\default.xbe: STEFX_RUNTIME_BUILD_ID personality=default flavor=production log=ef_sp_log.txt",
                "  build\\release\\efmp.xbe: STEFX_RUNTIME_BUILD_ID personality=efmp flavor=production log=ef_mp_log.txt",
            ],
        },
            "criteria": {
                "interval": 10,
                "firstShotDelay": 18,
                "pollXBlogStartDelay": 15,
                "pollXBlogInterval": 5,
            },
            "reports": {
                "spBorg2": {
                    "mode": "sp",
                    "map": "borg2",
                    "duration": 70,
                    "path": "scripts/output/refresh-sp.report.txt",
                },
                "coopBorg1SplitScreen": {
                    "mode": "coop",
                    "map": "borg1",
                    "duration": 270,
                    "path": "scripts/output/refresh-coop.report.txt",
                },
                "hmBorg1Soak": {
                    "mode": "mp",
                    "map": "hm_borg1",
                    "duration": 150,
                    "path": "scripts/output/refresh-mp.report.txt",
                },
            },
        }
        refresh_report.write_text(json.dumps(refresh_payload), encoding="utf-8")
        refresh_record = xemu_refresh_report_record(root, refresh_report, root / "stage")
        if (
            refresh_record.get("status") != "pass"
            or refresh_record.get("reportType") != EXPECTED_XEMU_REFRESH_REPORT_TYPE
            or refresh_record.get("reportSchemaVersion")
            != EXPECTED_XEMU_REFRESH_REPORT_SCHEMA_VERSION
            or refresh_record.get("generatedAtUtc") != "2026-08-19T12:00:00Z"
            or refresh_record.get("derivedReports", {}).get("spBorg2")
            != str(root / "scripts" / "output" / "refresh-sp.report.txt")
            or refresh_record.get("expectedProofContexts", {}).get("coopBorg1SplitScreen", {}).get("duration")
            != 270.0
            or refresh_record.get("expectedProofContexts", {}).get("coopBorg1SplitScreen", {}).get("interval")
            != 10.0
        ):
            print("self-test failed: XEMU refresh report was not parsed", file=sys.stderr)
            print(json.dumps(refresh_record, indent=2), file=sys.stderr)
            return 1
        refresh_payload_without_preflight = json.loads(json.dumps(refresh_payload))
        refresh_payload_without_preflight.pop("releasePreflight", None)
        refresh_report.write_text(json.dumps(refresh_payload_without_preflight), encoding="utf-8")
        missing_preflight_record = xemu_refresh_report_record(root, refresh_report, root / "stage")
        if (
            missing_preflight_record.get("status") != "fail"
            or not any("releasePreflight" in failure for failure in missing_preflight_record.get("failures", []))
        ):
            print("self-test failed: missing XEMU refresh releasePreflight was not rejected", file=sys.stderr)
            print(json.dumps(missing_preflight_record, indent=2), file=sys.stderr)
            return 1
        refresh_payload_without_pk3_marker = json.loads(json.dumps(refresh_payload))
        refresh_payload_without_pk3_marker["releasePreflight"]["output"].remove(
            "build\\release PK3 freshness ok"
        )
        refresh_report.write_text(json.dumps(refresh_payload_without_pk3_marker), encoding="utf-8")
        missing_pk3_preflight_record = xemu_refresh_report_record(root, refresh_report, root / "stage")
        if (
            missing_pk3_preflight_record.get("status") != "fail"
            or not any("PK3 freshness" in failure for failure in missing_pk3_preflight_record.get("failures", []))
        ):
            print("self-test failed: missing XEMU refresh PK3 freshness marker was not rejected", file=sys.stderr)
            print(json.dumps(missing_pk3_preflight_record, indent=2), file=sys.stderr)
            return 1
        refresh_payload_without_default_identity = json.loads(json.dumps(refresh_payload))
        refresh_payload_without_default_identity["releasePreflight"]["output"].remove(
            "  build\\release\\default.xbe: STEFX_RUNTIME_BUILD_ID personality=default flavor=production log=ef_sp_log.txt"
        )
        refresh_report.write_text(json.dumps(refresh_payload_without_default_identity), encoding="utf-8")
        missing_default_identity_record = xemu_refresh_report_record(root, refresh_report, root / "stage")
        if (
            missing_default_identity_record.get("status") != "fail"
            or not any("default.xbe" in failure and "runtime identity" in failure for failure in missing_default_identity_record.get("failures", []))
        ):
            print("self-test failed: missing XEMU refresh default.xbe runtime identity was not rejected", file=sys.stderr)
            print(json.dumps(missing_default_identity_record, indent=2), file=sys.stderr)
            return 1
        refresh_payload["reports"]["hmBorg1Soak"]["map"] = "wrong_map"
        refresh_report.write_text(json.dumps(refresh_payload), encoding="utf-8")
        bad_refresh_record = xemu_refresh_report_record(root, refresh_report, root / "stage")
        if (
            bad_refresh_record.get("status") != "fail"
            or not any("hmBorg1Soak map" in failure for failure in bad_refresh_record.get("failures", []))
        ):
            print("self-test failed: bad XEMU refresh report map was not rejected", file=sys.stderr)
            print(json.dumps(bad_refresh_record, indent=2), file=sys.stderr)
            return 1
        refresh_payload["reports"]["hmBorg1Soak"]["map"] = "hm_borg1"
        refresh_payload["reports"]["coopBorg1SplitScreen"]["duration"] = 30
        refresh_report.write_text(json.dumps(refresh_payload), encoding="utf-8")
        short_refresh_record = xemu_refresh_report_record(root, refresh_report, root / "stage")
        if (
            short_refresh_record.get("status") != "fail"
            or not any("coopBorg1SplitScreen duration" in failure for failure in short_refresh_record.get("failures", []))
        ):
            print("self-test failed: short XEMU refresh proof duration was not rejected", file=sys.stderr)
            print(json.dumps(short_refresh_record, indent=2), file=sys.stderr)
            return 1
        refresh_payload["reports"]["coopBorg1SplitScreen"]["duration"] = 270
        refresh_payload["reports"]["spBorg2"]["path"] = str(Path(temp_dir).parent / "outside.report.txt")
        refresh_report.write_text(json.dumps(refresh_payload), encoding="utf-8")
        outside_refresh_record = xemu_refresh_report_record(root, refresh_report, root / "stage")
        if (
            outside_refresh_record.get("status") != "fail"
            or not any("spBorg2 path is outside audited repo" in failure for failure in outside_refresh_record.get("failures", []))
        ):
            print("self-test failed: outside XEMU refresh proof path was not rejected", file=sys.stderr)
            print(json.dumps(outside_refresh_record, indent=2), file=sys.stderr)
            return 1
        refresh_payload["reports"]["spBorg2"]["path"] = "scripts/output/refresh-sp.report.txt"
        refresh_payload["repoRoot"] = str(Path(temp_dir).parent)
        refresh_report.write_text(json.dumps(refresh_payload), encoding="utf-8")
        wrong_root_refresh_record = xemu_refresh_report_record(root, refresh_report, root / "stage")
        if (
            wrong_root_refresh_record.get("status") != "fail"
            or not any("repoRoot" in failure for failure in wrong_root_refresh_record.get("failures", []))
        ):
            print("self-test failed: wrong XEMU refresh repoRoot was not rejected", file=sys.stderr)
            print(json.dumps(wrong_root_refresh_record, indent=2), file=sys.stderr)
            return 1
        refresh_payload["repoRoot"] = str(root)
        refresh_payload["stage"] = str(root / "other-stage")
        refresh_report.write_text(json.dumps(refresh_payload), encoding="utf-8")
        wrong_stage_refresh_record = xemu_refresh_report_record(root, refresh_report, root / "stage")
        if (
            wrong_stage_refresh_record.get("status") != "fail"
            or not any("stage" in failure for failure in wrong_stage_refresh_record.get("failures", []))
        ):
            print("self-test failed: wrong XEMU refresh stage was not rejected", file=sys.stderr)
            print(json.dumps(wrong_stage_refresh_record, indent=2), file=sys.stderr)
            return 1
        refresh_payload["stage"] = str(root / "stage")
        refresh_payload["criteria"]["pollXBlogInterval"] = 99
        refresh_report.write_text(json.dumps(refresh_payload), encoding="utf-8")
        bad_criteria_refresh_record = xemu_refresh_report_record(root, refresh_report, root / "stage")
        if (
            bad_criteria_refresh_record.get("status") != "fail"
            or not any("pollXBlogInterval" in failure for failure in bad_criteria_refresh_record.get("failures", []))
        ):
            print("self-test failed: bad XEMU refresh criteria were not rejected", file=sys.stderr)
            print(json.dumps(bad_criteria_refresh_record, indent=2), file=sys.stderr)
            return 1
        refresh_payload["criteria"]["pollXBlogInterval"] = 5
        refresh_payload["generatedAtUtc"] = "not-a-date"
        refresh_report.write_text(json.dumps(refresh_payload), encoding="utf-8")
        bad_generated_at_record = xemu_refresh_report_record(root, refresh_report, root / "stage")
        if (
            bad_generated_at_record.get("status") != "fail"
            or not any("generatedAtUtc" in failure for failure in bad_generated_at_record.get("failures", []))
        ):
            print("self-test failed: bad XEMU refresh generatedAtUtc was not rejected", file=sys.stderr)
            print(json.dumps(bad_generated_at_record, indent=2), file=sys.stderr)
            return 1
        refresh_payload["generatedAtUtc"] = "2026-08-19T12:00:00Z"
        future_report = root / "scripts" / "output" / "refresh-sp.report.txt"
        future_report.write_text("future report\n", encoding="utf-8")
        future_time = datetime(2026, 8, 19, 12, 1, tzinfo=timezone.utc).timestamp()
        os.utime(future_report, (future_time, future_time))
        refresh_report.write_text(json.dumps(refresh_payload), encoding="utf-8")
        future_report_record = xemu_refresh_report_record(root, refresh_report, root / "stage")
        if (
            future_report_record.get("status") != "fail"
            or not any("spBorg2 report file is newer than generatedAtUtc" in failure for failure in future_report_record.get("failures", []))
        ):
            print("self-test failed: future XEMU refresh report file was not rejected", file=sys.stderr)
            print(json.dumps(future_report_record, indent=2), file=sys.stderr)
            return 1
        older_refresh = root / "scripts" / "output" / "xemu_qualification_proof_refresh_20260819_115900.json"
        newer_refresh = root / "scripts" / "output" / "xemu_qualification_proof_refresh_20260819_120000.json"
        older_refresh.write_text(json.dumps(refresh_payload), encoding="utf-8")
        newer_refresh.write_text(json.dumps(refresh_payload), encoding="utf-8")
        old_manifest_time = datetime(2026, 8, 19, 11, 59, tzinfo=timezone.utc).timestamp()
        new_manifest_time = datetime(2026, 8, 19, 12, 0, tzinfo=timezone.utc).timestamp()
        os.utime(older_refresh, (old_manifest_time, old_manifest_time))
        os.utime(newer_refresh, (new_manifest_time, new_manifest_time))
        selected_refresh = latest_xemu_refresh_report(root)
        if selected_refresh != newer_refresh:
            print("self-test failed: latest XEMU refresh manifest was not selected", file=sys.stderr)
            print(f"selected={selected_refresh} expected={newer_refresh}", file=sys.stderr)
            return 1
        selected_refresh, selection_mode = select_xemu_refresh_report(root, None, None, None, None)
        if selected_refresh != newer_refresh or selection_mode != "auto":
            print("self-test failed: XEMU refresh manifest auto-selection did not choose latest", file=sys.stderr)
            print(f"selected={selected_refresh} mode={selection_mode}", file=sys.stderr)
            return 1
        selected_refresh, selection_mode = select_xemu_refresh_report(
            root, older_refresh, None, None, None
        )
        if selected_refresh != older_refresh or selection_mode != "explicit":
            print("self-test failed: explicit XEMU refresh manifest did not take precedence", file=sys.stderr)
            print(f"selected={selected_refresh} mode={selection_mode}", file=sys.stderr)
            return 1
        selected_refresh, selection_mode = select_xemu_refresh_report(
            root, None, root / "scripts" / "output" / "manual-sp.report.txt", None, None
        )
        if selected_refresh is not None or selection_mode != "disabled-by-explicit-proof-reports":
            print("self-test failed: explicit XEMU report override did not disable refresh auto-selection", file=sys.stderr)
            print(f"selected={selected_refresh} mode={selection_mode}", file=sys.stderr)
            return 1
        for stale_refresh in (older_refresh, newer_refresh):
            stale_refresh.unlink()
        selected_refresh, selection_mode = select_xemu_refresh_report(root, None, None, None, None)
        if selected_refresh is not None or selection_mode != "none":
            print("self-test failed: missing XEMU refresh manifest did not report none selection", file=sys.stderr)
            print(f"selected={selected_refresh} mode={selection_mode}", file=sys.stderr)
            return 1
        annotated = annotate_xemu_refresh_selection(
            root,
            {"present": False, "used": False},
            "disabled-by-explicit-proof-reports",
            root / "scripts" / "output" / "manual-sp.report.txt",
            None,
            root / "scripts" / "output" / "manual-mp.report.txt",
        )
        expected_overrides = {
            "spBorg2": str(root / "scripts" / "output" / "manual-sp.report.txt"),
            "hmBorg1Soak": str(root / "scripts" / "output" / "manual-mp.report.txt"),
        }
        if (
            annotated.get("selectionMode") != "disabled-by-explicit-proof-reports"
            or annotated.get("autoSelected") is not False
            or annotated.get("autoSelectionSuppressed") is not True
            or annotated.get("explicitProofReportOverrides") != expected_overrides
        ):
            print("self-test failed: XEMU refresh selection provenance was not annotated", file=sys.stderr)
            print(json.dumps(annotated, indent=2), file=sys.stderr)
            return 1
        auto_annotated = annotate_xemu_refresh_selection(
            root,
            {"present": True, "used": True},
            "auto",
            None,
            None,
            None,
        )
        if (
            auto_annotated.get("selectionMode") != "auto"
            or auto_annotated.get("autoSelected") is not True
            or "explicitProofReportOverrides" in auto_annotated
            or "autoSelectionSuppressed" in auto_annotated
        ):
            print("self-test failed: XEMU refresh auto-selection provenance was not annotated", file=sys.stderr)
            print(json.dumps(auto_annotated, indent=2), file=sys.stderr)
            return 1
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        report = root / "scripts" / "output" / "fresh.report.txt"
        source = root / "code" / "renderer" / "tr_backend.cpp"
        release = root / "build" / "release" / "default.xbe"
        mp_release = root / "build" / "release" / "efmp.xbe"
        xbox0_pk3 = root / "build" / "release" / "BaseEF" / "xbox0.pk3"
        xbox1_pk3 = root / "build" / "release" / "BaseEF" / "xbox1.pk3"
        harness = root / "scripts" / "ja_xemu_smoke.py"
        report.parent.mkdir(parents=True)
        source.parent.mkdir(parents=True)
        release.parent.mkdir(parents=True)
        xbox0_pk3.parent.mkdir(parents=True)
        report.write_text("alive_at_end\n", encoding="utf-8")
        source.write_text("// source\n", encoding="utf-8")
        release.write_bytes(b"xbe")
        mp_release.write_bytes(b"mp-xbe")
        xbox0_pk3.write_bytes(b"xbox0")
        xbox1_pk3.write_bytes(b"xbox1")
        harness.write_text("# harness\n", encoding="utf-8")
        old_time = datetime(2026, 8, 19, 10, 0, tzinfo=timezone.utc).timestamp()
        new_time = datetime(2026, 8, 19, 11, 0, tzinfo=timezone.utc).timestamp()
        for path in (source, release, mp_release, xbox0_pk3, xbox1_pk3, harness):
            path.touch()
            path_mtime = old_time
            os.utime(path, (path_mtime, path_mtime))
        os.utime(report, (new_time, new_time))
        fresh = xemu_proof_freshness(root, report)
        if fresh.get("status") != "pass":
            print("self-test failed: fresh XEMU proof was marked stale", file=sys.stderr)
            print(json.dumps(fresh, indent=2), file=sys.stderr)
            return 1
        os.utime(source, (new_time + 60, new_time + 60))
        stale = xemu_proof_freshness(root, report)
        if (
            stale.get("status") != "stale-xemu-proof"
            or stale.get("newerRuntimeSourceCount") != 1
            or not stale.get("newerRuntimeSources")
        ):
            print("self-test failed: stale XEMU proof was not detected", file=sys.stderr)
            print(json.dumps(stale, indent=2), file=sys.stderr)
            return 1
        os.utime(source, (new_time + 60, new_time + 60))
        os.utime(report, (new_time + 120, new_time + 120))
        stale_release = xemu_proof_freshness(root, report)
        if (
            stale_release.get("status") != "stale-xemu-proof"
            or stale_release.get("runtimeSourcesNewerThanReleaseCount") != 1
            or not stale_release.get("runtimeSourcesNewerThanRelease")
        ):
            print("self-test failed: stale release artifact behind runtime source was not detected", file=sys.stderr)
            print(json.dumps(stale_release, indent=2), file=sys.stderr)
            return 1
        os.utime(source, (old_time, old_time))
        os.utime(report, (new_time, new_time))
        os.utime(xbox1_pk3, (new_time + 60, new_time + 60))
        stale_package = xemu_proof_freshness(root, report)
        if (
            stale_package.get("status") != "stale-xemu-proof"
            or not any(
                record.get("path") == "build/release/BaseEF/xbox1.pk3"
                for record in stale_package.get("newerReleaseArtifacts", [])
                if isinstance(record, dict)
            )
        ):
            print("self-test failed: newer release PK3 was not detected as stale XEMU proof", file=sys.stderr)
            print(json.dumps(stale_package, indent=2), file=sys.stderr)
            return 1
        os.utime(xbox1_pk3, (old_time, old_time))
        os.utime(source, (old_time, old_time))
        os.utime(report, (new_time, new_time))
        os.utime(harness, (new_time + 60, new_time + 60))
        stale_harness = xemu_proof_freshness(root, report)
        if (
            stale_harness.get("status") != "stale-xemu-proof"
            or not stale_harness.get("newerProofHarnessFiles")
        ):
            print("self-test failed: newer XEMU proof harness was not detected", file=sys.stderr)
            print(json.dumps(stale_harness, indent=2), file=sys.stderr)
            return 1
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        output = root / "scripts" / "output"
        release_dir = root / "build" / "release"
        output.mkdir(parents=True)
        release_dir.mkdir(parents=True)
        default_id = (
            "STEFX_RUNTIME_BUILD_ID personality=default flavor=production "
            "date=Aug 19 2026 time=12:00:00 log=ef_sp_log.txt"
        )
        efmp_id = (
            "STEFX_RUNTIME_BUILD_ID personality=efmp flavor=production "
            "date=Aug 19 2026 time=12:01:00 log=ef_mp_log.txt"
        )
        default_xbe = release_dir / "default.xbe"
        efmp_xbe = release_dir / "efmp.xbe"
        xbox0_pk3 = release_dir / "BaseEF" / "xbox0.pk3"
        xbox1_pk3 = release_dir / "BaseEF" / "xbox1.pk3"
        xbox0_pk3.parent.mkdir(parents=True, exist_ok=True)
        default_xbe.write_bytes(b"default\0" + default_id.encode("ascii") + b"\0")
        efmp_xbe.write_bytes(b"efmp\0" + efmp_id.encode("ascii") + b"\0")
        xbox0_pk3.write_bytes(b"xbox0")
        xbox1_pk3.write_bytes(b"xbox1")
        harness_paths = []
        for rel_path in XEMU_PROOF_HARNESS_FILES:
            harness_path = root / rel_path
            harness_path.parent.mkdir(parents=True, exist_ok=True)
            harness_path.write_text("# harness\n", encoding="utf-8")
            harness_paths.append(harness_path)
        report = output / "identity.report.txt"
        contact = output / "identity_contact.png"
        registers = output / "identity_final_registers.txt"
        contact.write_bytes(self_test_png_bytes())
        registers.write_text("regs\n", encoding="utf-8")
        def write_identity_report(
            build_id: str,
            contact_path: Path = contact,
            registers_path: Path = registers,
            proof_mode: str = "sp",
            proof_map: str = "borg2",
            proof_duration: int = 10,
            proof_interval: int = 1,
            native_screenshots: str = "True",
            runtime_xbe_path: Path | None = None,
            runtime_xbe_bytes: int | None = None,
        ) -> None:
            identity_path = runtime_xbe_path if runtime_xbe_path is not None else default_xbe
            identity_bytes = runtime_xbe_bytes if runtime_xbe_bytes is not None else default_xbe.stat().st_size
            report.write_text(
                "\n".join(
                    [
                        "proof_context mode=%s map=%s name=identity duration=%u interval=%u display=xemu nativeScreenshots=%s"
                        % (proof_mode, proof_map, proof_duration, proof_interval, native_screenshots),
                        "runtime_xbe_identity path=%s present=True bytes=%u sha256=%s runtimeBuildId=%s"
                        % (identity_path, identity_bytes, file_sha256(default_xbe), build_id),
                        "shot=00 t=2.0 ok=True bytes=100 detail=xemu native screenshot",
                        "alive_at_end pid=1",
                        "final_registers=%s" % registers_path,
                        "contact=%s" % contact_path,
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
        write_identity_report(default_id)
        old_time = datetime(2026, 8, 19, 10, 0, tzinfo=timezone.utc).timestamp()
        new_time = datetime(2026, 8, 19, 11, 0, tzinfo=timezone.utc).timestamp()
        for path in (default_xbe, efmp_xbe, xbox0_pk3, xbox1_pk3, *harness_paths):
            os.utime(path, (old_time, old_time))
        for path in (report, contact, registers):
            os.utime(path, (new_time, new_time))

        original_runs = dict(XEMU_EVIDENCE_RUNS)
        try:
            XEMU_EVIDENCE_RUNS.clear()
            XEMU_EVIDENCE_RUNS["identitySp"] = {
                "mode": "sp",
                "map": "borg2",
                "report": Path("scripts/output/identity.report.txt"),
                "contact": Path("scripts/output/identity_contact.png"),
                "finalRegisters": Path("scripts/output/identity_final_registers.txt"),
                "minOkShots": 1,
                "minUniqueOkShotByteSizes": 1,
                "minDurationSeconds": 1.0,
                "expectedProofDuration": 10.0,
                "expectedProofInterval": 1.0,
                "requireAllocatorStable": False,
            }
            pass_result = collect_xemu_evidence(root)
            if pass_result.get("status") != "pass":
                print("self-test failed: matching XEMU runtime identity was rejected", file=sys.stderr)
                print(json.dumps(pass_result, indent=2), file=sys.stderr)
                return 1
            harness_records = pass_result.get("proofHarnessFiles")
            if (
                not isinstance(harness_records, dict)
                or len(harness_records) != len(XEMU_PROOF_HARNESS_FILES)
                or any(
                    not isinstance(harness_records.get(rel_path.as_posix()), dict)
                    or not harness_records[rel_path.as_posix()].get("sha256")
                    for rel_path in XEMU_PROOF_HARNESS_FILES
                )
            ):
                print("self-test failed: XEMU proof harness SHA provenance was not reported", file=sys.stderr)
                print(json.dumps(pass_result, indent=2), file=sys.stderr)
                return 1
            write_identity_report(default_id, proof_map="wrong_map")
            os.utime(report, (new_time, new_time))
            wrong_context_result = collect_xemu_evidence(root)
            if (
                wrong_context_result.get("status") != "fail"
                or not any(
                    "proof_context map" in failure
                    for failure in wrong_context_result.get("failures", [])
                )
            ):
                print("self-test failed: mismatched XEMU proof context was not rejected", file=sys.stderr)
                print(json.dumps(wrong_context_result, indent=2), file=sys.stderr)
                return 1
            write_identity_report(default_id, proof_duration=9)
            os.utime(report, (new_time, new_time))
            mismatched_duration_result = collect_xemu_evidence(root)
            if (
                mismatched_duration_result.get("status") != "fail"
                or not any(
                    "duration 9.0s != refresh manifest" in failure
                    for failure in mismatched_duration_result.get("failures", [])
                )
            ):
                print("self-test failed: mismatched XEMU proof_context duration was not rejected", file=sys.stderr)
                print(json.dumps(mismatched_duration_result, indent=2), file=sys.stderr)
                return 1
            write_identity_report(default_id, proof_interval=2)
            os.utime(report, (new_time, new_time))
            mismatched_interval_result = collect_xemu_evidence(root)
            if (
                mismatched_interval_result.get("status") != "fail"
                or not any(
                    "interval 2.0s != refresh manifest" in failure
                    for failure in mismatched_interval_result.get("failures", [])
                )
            ):
                print("self-test failed: mismatched XEMU proof_context interval was not rejected", file=sys.stderr)
                print(json.dumps(mismatched_interval_result, indent=2), file=sys.stderr)
                return 1
            write_identity_report(default_id, proof_duration=0)
            os.utime(report, (new_time, new_time))
            short_context_result = collect_xemu_evidence(root)
            if (
                short_context_result.get("status") != "fail"
                or not any(
                    "proof_context duration" in failure
                    for failure in short_context_result.get("failures", [])
                )
            ):
                print("self-test failed: short XEMU proof_context duration was not rejected", file=sys.stderr)
                print(json.dumps(short_context_result, indent=2), file=sys.stderr)
                return 1
            write_identity_report(default_id, proof_interval=0)
            os.utime(report, (new_time, new_time))
            bad_interval_result = collect_xemu_evidence(root)
            if (
                bad_interval_result.get("status") != "fail"
                or not any(
                    "proof_context interval" in failure
                    for failure in bad_interval_result.get("failures", [])
                )
            ):
                print("self-test failed: bad XEMU proof_context interval was not rejected", file=sys.stderr)
                print(json.dumps(bad_interval_result, indent=2), file=sys.stderr)
                return 1
            write_identity_report(default_id, native_screenshots="False")
            os.utime(report, (new_time, new_time))
            non_native_result = collect_xemu_evidence(root)
            if (
                non_native_result.get("status") != "fail"
                or not any(
                    "nativeScreenshots is not True" in failure
                    for failure in non_native_result.get("failures", [])
                )
            ):
                print("self-test failed: non-native XEMU screenshot proof_context was not rejected", file=sys.stderr)
                print(json.dumps(non_native_result, indent=2), file=sys.stderr)
                return 1
            write_identity_report(default_id)
            os.utime(report, (new_time, new_time))
            wrong_path = root / "elsewhere" / "default.xbe"
            wrong_path.parent.mkdir(parents=True)
            wrong_path.write_bytes(default_xbe.read_bytes())
            write_identity_report(default_id, runtime_xbe_path=wrong_path)
            os.utime(report, (new_time, new_time))
            wrong_path_result = collect_xemu_evidence(root)
            if (
                wrong_path_result.get("status") != "fail"
                or not any(
                    "XEMU report path for default.xbe does not match current release" in failure
                    for failure in wrong_path_result.get("failures", [])
                )
            ):
                print("self-test failed: mismatched XEMU runtime identity path was not rejected", file=sys.stderr)
                print(json.dumps(wrong_path_result, indent=2), file=sys.stderr)
                return 1
            write_identity_report(default_id, runtime_xbe_bytes=default_xbe.stat().st_size + 1)
            os.utime(report, (new_time, new_time))
            wrong_bytes_result = collect_xemu_evidence(root)
            if (
                wrong_bytes_result.get("status") != "fail"
                or not any(
                    "XEMU report byte count for default.xbe does not match current release" in failure
                    for failure in wrong_bytes_result.get("failures", [])
                )
            ):
                print("self-test failed: mismatched XEMU runtime identity byte count was not rejected", file=sys.stderr)
                print(json.dumps(wrong_bytes_result, indent=2), file=sys.stderr)
                return 1
            write_identity_report(default_id)
            os.utime(report, (new_time, new_time))
            write_identity_report(default_id + "-mismatch")
            fail_result = collect_xemu_evidence(root)
            if (
                fail_result.get("status") != "fail"
                or not any("runtime build id" in failure for failure in fail_result.get("failures", []))
            ):
                print("self-test failed: mismatched XEMU runtime identity was not rejected", file=sys.stderr)
                print(json.dumps(fail_result, indent=2), file=sys.stderr)
                return 1
            wrong_default_id = (
                "STEFX_RUNTIME_BUILD_ID personality=efmp flavor=production "
                "date=Aug 19 2026 time=12:02:00 log=ef_mp_log.txt"
            )
            default_xbe.write_bytes(b"default\0" + wrong_default_id.encode("ascii") + b"\0")
            os.utime(default_xbe, (old_time, old_time))
            write_identity_report(wrong_default_id)
            os.utime(report, (new_time, new_time))
            wrong_identity_result = collect_xemu_evidence(root)
            if (
                wrong_identity_result.get("status") != "fail"
                or not any(
                    "current release default.xbe runtimeBuildId has wrong identity" in failure
                    for failure in wrong_identity_result.get("failures", [])
                )
            ):
                print("self-test failed: wrong current XBE runtime identity was not rejected", file=sys.stderr)
                print(json.dumps(wrong_identity_result, indent=2), file=sys.stderr)
                return 1
            default_xbe.write_bytes(b"default\0" + default_id.encode("ascii") + b"\0")
            os.utime(default_xbe, (old_time, old_time))
            write_identity_report(default_id)
            contact.write_text("not visual proof\n", encoding="utf-8")
            os.utime(report, (new_time, new_time))
            os.utime(contact, (new_time, new_time))
            bad_contact_result = collect_xemu_evidence(root)
            if (
                bad_contact_result.get("status") != "fail"
                or not any(
                    "contact sheet is not a PNG visual proof" in failure
                    for failure in bad_contact_result.get("failures", [])
                )
            ):
                print("self-test failed: bad XEMU contact sheet was not rejected", file=sys.stderr)
                print(json.dumps(bad_contact_result, indent=2), file=sys.stderr)
                return 1
            contact.write_bytes(self_test_png_bytes())
            wrong_contact = output / "wrong_contact.png"
            wrong_contact.write_bytes(self_test_png_bytes())
            write_identity_report(default_id, contact_path=wrong_contact)
            for path in (report, contact, wrong_contact):
                os.utime(path, (new_time, new_time))
            bad_link_result = collect_xemu_evidence(root)
            if (
                bad_link_result.get("status") != "fail"
                or not any(
                    "XEMU report contact artifact link does not match audited file" in failure
                    for failure in bad_link_result.get("failures", [])
                )
            ):
                print("self-test failed: mismatched XEMU contact link was not rejected", file=sys.stderr)
                print(json.dumps(bad_link_result, indent=2), file=sys.stderr)
                return 1
            wrong_registers = output / "wrong_final_registers.txt"
            wrong_registers.write_text("wrong regs\n", encoding="utf-8")
            write_identity_report(default_id, registers_path=wrong_registers)
            for path in (report, registers, wrong_registers):
                os.utime(path, (new_time, new_time))
            bad_register_link_result = collect_xemu_evidence(root)
            if (
                bad_register_link_result.get("status") != "fail"
                or not any(
                    "XEMU report final_registers artifact link does not match audited file" in failure
                    for failure in bad_register_link_result.get("failures", [])
                )
            ):
                print(
                    "self-test failed: mismatched XEMU final-register link was not rejected",
                    file=sys.stderr,
                )
                print(json.dumps(bad_register_link_result, indent=2), file=sys.stderr)
                return 1
        finally:
            XEMU_EVIDENCE_RUNS.clear()
            XEMU_EVIDENCE_RUNS.update(original_runs)
    parser = build_parser()
    defaults = parser.parse_args([])
    provenance = audit_provenance()
    if (
        provenance.get("script") != str(Path(__file__).resolve())
        or provenance.get("scriptSha256") != file_sha256(Path(__file__).resolve())
        or "--self-test" not in provenance.get("argv", [])
    ):
        print("self-test failed: audit provenance was not reported", file=sys.stderr)
        print(json.dumps(provenance, indent=2), file=sys.stderr)
        return 1
    if (
        defaults.min_elapsed_seconds != DEFAULT_MIN_ELAPSED_SECONDS
        or defaults.min_observation_seconds != DEFAULT_MIN_OBSERVATION_SECONDS
        or defaults.min_coop_observed_largest_free != 1048576
        or defaults.max_coop_observed_used_delta != 0
        or defaults.min_evidence_bytes != DEFAULT_MIN_EVIDENCE_BYTES
        or defaults.min_image_width != DEFAULT_MIN_IMAGE_WIDTH
        or defaults.min_image_height != DEFAULT_MIN_IMAGE_HEIGHT
    ):
        print("self-test failed: default qualification gates drifted", file=sys.stderr)
        print(
            json.dumps(
                {
                    "minElapsedSeconds": defaults.min_elapsed_seconds,
                    "minObservationSeconds": defaults.min_observation_seconds,
                    "minCoopObservedLargestFree": defaults.min_coop_observed_largest_free,
                    "maxCoopObservedUsedDelta": defaults.max_coop_observed_used_delta,
                    "minEvidenceBytes": defaults.min_evidence_bytes,
                    "minImageWidth": defaults.min_image_width,
                    "minImageHeight": defaults.min_image_height,
                },
                indent=2,
            ),
            file=sys.stderr,
        )
        return 1
    print("qualify_hardware_stage self-test passed")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run stage preflight and production hardware evidence checks as one audit."
    )
    parser.add_argument("--repo-root", type=Path, default=repo_root_from_script())
    parser.add_argument("--stage", type=Path, default=DEFAULT_STAGE)
    parser.add_argument("--direct-map", default="hm_borg1")
    parser.add_argument("--stage-report", type=Path, help="Stage preflight report path.")
    parser.add_argument("--qualification-report", type=Path, help="Production qualification report path.")
    parser.add_argument("--audit-report", type=Path, help="Combined audit report path.")
    parser.add_argument("--skip-default-cfg", action="store_true")
    parser.add_argument("--skip-package-check", action="store_true")
    parser.add_argument("--min-elapsed-seconds", type=float, default=DEFAULT_MIN_ELAPSED_SECONDS)
    parser.add_argument(
        "--min-observation-seconds",
        type=float,
        default=DEFAULT_MIN_OBSERVATION_SECONDS,
    )
    parser.add_argument("--required-sp-map", action="append", default=["borg2"])
    parser.add_argument("--required-coop-map", action="append", default=["borg1"])
    parser.add_argument("--required-mp-map", action="append", default=["hm_borg1"])
    parser.add_argument("--min-sp-visible-fps", type=float, default=30.0)
    parser.add_argument("--min-coop-visible-fps", type=float, default=30.0)
    parser.add_argument("--min-mp-visible-fps", type=float, default=30.0)
    parser.add_argument("--min-sp-largest-free", type=int, default=1048576)
    parser.add_argument("--min-mp-largest-free", type=int, default=1048576)
    parser.add_argument("--max-sp-used-delta", type=int, default=0)
    parser.add_argument("--max-mp-used-delta", type=int, default=0)
    parser.add_argument("--require-hm-split-log", action="store_true")
    parser.add_argument("--hm-split-min-bots", type=int, default=3)
    parser.add_argument("--hm-split-min-elapsed-seconds", type=float, default=90.0)
    parser.add_argument("--hm-split-min-heartbeat-fps", type=float, default=15.0)
    parser.add_argument("--hm-split-min-largest-free", type=int, default=1048576)
    parser.add_argument("--hm-split-max-used-delta", type=int, default=0)
    parser.add_argument("--min-coop-observed-largest-free", type=int, default=1048576)
    parser.add_argument("--max-coop-observed-used-delta", type=int, default=0)
    parser.add_argument("--min-evidence-bytes", type=int, default=DEFAULT_MIN_EVIDENCE_BYTES)
    parser.add_argument("--min-image-width", type=int, default=DEFAULT_MIN_IMAGE_WIDTH)
    parser.add_argument("--min-image-height", type=int, default=DEFAULT_MIN_IMAGE_HEIGHT)
    parser.add_argument("--xemu-sp-report", type=Path, help="Override saved XEMU SP borg2 proof report.")
    parser.add_argument(
        "--xemu-coop-report",
        type=Path,
        help="Override saved XEMU co-op borg1 split-screen proof report.",
    )
    parser.add_argument("--xemu-mp-report", type=Path, help="Override saved XEMU Holomatch hm_borg1 proof report.")
    parser.add_argument(
        "--xemu-refresh-report",
        type=Path,
        help=(
            "Use a refresh_xemu_qualification_proof.ps1 JSON manifest for XEMU proof report paths. "
            "If omitted and no --xemu-*-report overrides are supplied, the newest "
            "scripts/output/xemu_qualification_proof_refresh_*.json is used when present."
        ),
    )
    parser.add_argument("--no-require-observation", action="store_true")
    parser.add_argument("--no-require-evidence-files", action="store_true")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.self_test:
        return run_self_test()

    repo_root = args.repo_root.resolve()
    stage = resolve_path(repo_root, args.stage).resolve()
    stage_report = resolve_path(
        repo_root, args.stage_report or stage / "hardware_stage_preflight_report.json"
    ).resolve()
    qualification_report = resolve_path(
        repo_root, args.qualification_report or stage / "hardware_qualification_report.json"
    ).resolve()
    audit_report = resolve_path(
        repo_root, args.audit_report or stage / "hardware_qualification_audit.json"
    ).resolve()

    stage_command = [
        sys.executable,
        str(repo_root / "scripts" / "verify_hardware_stage.py"),
        "--repo-root",
        str(repo_root),
        "--stage",
        str(stage),
        "--direct-map",
        args.direct_map,
        "--report-out",
        str(stage_report),
        "--json",
    ]
    if args.skip_default_cfg:
        stage_command.append("--skip-default-cfg")
    if args.skip_package_check:
        stage_command.append("--skip-package-check")

    qualification_command = [
        sys.executable,
        str(repo_root / "scripts" / "verify_production_hardware_logs.py"),
        "--repo-root",
        str(repo_root),
        "--stage",
        str(stage),
        "--direct-map",
        args.direct_map,
        "--min-elapsed-seconds",
        str(args.min_elapsed_seconds),
        "--min-observation-seconds",
        str(args.min_observation_seconds),
        "--min-sp-visible-fps",
        str(args.min_sp_visible_fps),
        "--min-coop-visible-fps",
        str(args.min_coop_visible_fps),
        "--min-mp-visible-fps",
        str(args.min_mp_visible_fps),
        "--min-sp-largest-free",
        str(args.min_sp_largest_free),
        "--min-mp-largest-free",
        str(args.min_mp_largest_free),
        "--max-sp-used-delta",
        str(args.max_sp_used_delta),
        "--max-mp-used-delta",
        str(args.max_mp_used_delta),
        "--min-coop-observed-largest-free",
        str(args.min_coop_observed_largest_free),
        "--max-coop-observed-used-delta",
        str(args.max_coop_observed_used_delta),
        "--min-evidence-bytes",
        str(args.min_evidence_bytes),
        "--min-image-width",
        str(args.min_image_width),
        "--min-image-height",
        str(args.min_image_height),
        "--report-out",
        str(qualification_report),
        "--json",
    ]
    if not args.no_require_observation:
        qualification_command.append("--require-observation")
    if not args.no_require_evidence_files:
        qualification_command.append("--require-evidence-files")
    if args.skip_default_cfg:
        qualification_command.append("--skip-default-cfg")
    if args.skip_package_check:
        qualification_command.append("--skip-package-check")
    if args.require_hm_split_log:
        qualification_command.append("--require-hm-split-log")
        qualification_command.extend(["--hm-split-min-bots", str(args.hm_split_min_bots)])
        qualification_command.extend(["--hm-split-min-elapsed-seconds", str(args.hm_split_min_elapsed_seconds)])
        qualification_command.extend(["--hm-split-min-heartbeat-fps", str(args.hm_split_min_heartbeat_fps)])
        qualification_command.extend(["--hm-split-min-largest-free", str(args.hm_split_min_largest_free)])
        qualification_command.extend(["--hm-split-max-used-delta", str(args.hm_split_max_used_delta)])
    for map_name in args.required_sp_map:
        qualification_command.extend(["--required-sp-map", map_name])
    for map_name in args.required_coop_map:
        qualification_command.extend(["--required-coop-map", map_name])
    for map_name in args.required_mp_map:
        qualification_command.extend(["--required-mp-map", map_name])

    stage_run = run_command(stage_command, repo_root)
    qualification_run = run_command(qualification_command, repo_root)
    stage_result = read_json(stage_report)
    qualification_result = read_json(qualification_report)
    contract_evidence = collect_contract_evidence(repo_root)
    xemu_refresh_report_path, xemu_refresh_report_selection_mode = select_xemu_refresh_report(
        repo_root,
        args.xemu_refresh_report,
        args.xemu_sp_report,
        args.xemu_coop_report,
        args.xemu_mp_report,
    )
    xemu_refresh_report = xemu_refresh_report_record(repo_root, xemu_refresh_report_path, stage)
    annotate_xemu_refresh_selection(
        repo_root,
        xemu_refresh_report,
        xemu_refresh_report_selection_mode,
        args.xemu_sp_report,
        args.xemu_coop_report,
        args.xemu_mp_report,
    )
    refresh_reports = xemu_refresh_report.get("derivedReports")
    if not isinstance(refresh_reports, dict):
        refresh_reports = {}
    refresh_sp_report = (
        Path(refresh_reports["spBorg2"]) if isinstance(refresh_reports.get("spBorg2"), str) else None
    )
    refresh_coop_report = (
        Path(refresh_reports["coopBorg1SplitScreen"])
        if isinstance(refresh_reports.get("coopBorg1SplitScreen"), str)
        else None
    )
    refresh_mp_report = (
        Path(refresh_reports["hmBorg1Soak"])
        if isinstance(refresh_reports.get("hmBorg1Soak"), str)
        else None
    )
    refresh_expected_contexts = xemu_refresh_report.get("expectedProofContexts")
    if not isinstance(refresh_expected_contexts, dict):
        refresh_expected_contexts = {}
    active_expected_contexts: dict[str, dict[str, object]] = {}
    for name, explicit_report in (
        ("spBorg2", args.xemu_sp_report),
        ("coopBorg1SplitScreen", args.xemu_coop_report),
        ("hmBorg1Soak", args.xemu_mp_report),
    ):
        expected_context = refresh_expected_contexts.get(name)
        if explicit_report is None and isinstance(expected_context, dict):
            active_expected_contexts[name] = expected_context
    xemu_runs = xemu_evidence_runs_with_overrides(
        args.xemu_sp_report or refresh_sp_report,
        args.xemu_coop_report or refresh_coop_report,
        args.xemu_mp_report or refresh_mp_report,
        active_expected_contexts,
    )
    xemu_evidence = collect_xemu_evidence(repo_root, xemu_runs)
    refresh_failures = xemu_refresh_report.get("failures")
    if isinstance(refresh_failures, list) and refresh_failures:
        xemu_evidence["status"] = "fail"
        failures = xemu_evidence.get("failures")
        if not isinstance(failures, list):
            failures = []
        failures.extend(str(failure) for failure in refresh_failures)
        xemu_evidence["failures"] = failures
    subreport_contract_failures = report_contract_failures(
        stage_result,
        qualification_result,
        stage,
        stage_report,
        qualification_report,
        repo_root / "scripts" / "verify_hardware_stage.py",
        repo_root / "scripts" / "verify_production_hardware_logs.py",
        stage_command[2:],
        qualification_command[2:],
    )
    stage_status = stage_result.get("status") if isinstance(stage_result, dict) else None
    qualification_status = (
        qualification_result.get("status") if isinstance(qualification_result, dict) else None
    )
    overall = determine_overall_status(stage_status, qualification_status)
    if (
        contract_evidence.get("status") == "fail"
        or xemu_evidence.get("status") == "fail"
        or subreport_contract_failures
    ):
        overall = "fail"
    acceptance_checklist = build_acceptance_checklist(
        stage_result,
        qualification_result,
        contract_evidence,
        xemu_evidence,
    )
    acceptance_checklist.append(
        checklist_item(
            "qualification-report-provenance",
            "Stage preflight and production qualification reports have the expected type, schema, and verifier provenance",
            "pass" if not subreport_contract_failures else "fail",
            details=subreport_contract_failures,
        )
    )
    open_items = open_acceptance_items(acceptance_checklist)

    payload: dict[str, object] = {
        "reportType": REPORT_TYPE,
        "reportSchemaVersion": REPORT_SCHEMA_VERSION,
        "generatedAtUtc": now_utc(),
        "auditProvenance": audit_provenance(),
        "repoRoot": str(repo_root),
        "stage": str(stage),
        "status": overall,
        "overallStatus": overall,
        "openAcceptanceItems": open_items,
        "reports": {
            "stagePreflight": str(stage_report),
            "productionQualification": str(qualification_report),
        },
        "subreportFiles": {
            "stagePreflight": proof_file_record(stage_report),
            "productionQualification": proof_file_record(qualification_report),
        },
        "stagePreflight": {
            "returnCode": stage_run["returnCode"],
            "status": stage_status,
            "report": stage_result,
        },
        "productionQualification": {
            "returnCode": qualification_run["returnCode"],
            "status": qualification_status,
            "report": qualification_result,
        },
        "subreportContractFailures": subreport_contract_failures,
        "retailContractEvidence": contract_evidence,
        "xemuProofRefreshReport": xemu_refresh_report,
        "xemuEvidence": xemu_evidence,
        "acceptanceChecklist": acceptance_checklist,
        "commands": {
            "stagePreflight": stage_command,
            "productionQualification": qualification_command,
        },
    }
    write_report(audit_report, payload)

    if args.json:
        print(json.dumps(payload, indent=2))
    else:
        print("STEFX hardware qualification audit")
        print(f"Stage: {stage}")
        print(f"Overall: {overall}")
        print(f"Stage preflight: {stage_status} report={stage_report}")
        print(f"Production qualification: {qualification_status} report={qualification_report}")
        if open_items:
            print(
                "Open acceptance item(s): "
                + ", ".join(str(item["id"]) for item in open_items)
            )
        print(f"Audit report: {audit_report}")
    return 0 if overall == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
