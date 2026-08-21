#!/usr/bin/env python3
"""Compare current renderer COFF functions with the clean retail donor objects.

The linked-XBE comparison is useful for address-level investigation, but COMDAT
folding and section reordering can make a function appear to extend into the
next body.  This tool asks the XDK linker to disassemble each object directly,
where every function retains an unambiguous section boundary.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


REPORT_TYPE = "stefx-retail-renderer-object-compare"
REPORT_SCHEMA_VERSION = 2
FUNCTION_RE = re.compile(r"^(\?[^ ]+|_[^ ]+|[A-Za-z][^ ]*)\s+\(.*\):$")
INSTRUCTION_RE = re.compile(r"^\s+[0-9A-Fa-f]{8}:\s+(.*\S)\s*$")


OBJECT_PAIRS = {
    "matcomp.obj": "matcomp.obj",
    "tr_animation.obj": "tr_animation.obj",
    "tr_backend_retail.obj": "tr_backend.obj",
    "tr_bsp_xbox.obj": "tr_bsp_xbox.obj",
    "tr_cmds_retail.obj": "tr_cmds.obj",
    "tr_curve.obj": "tr_curve_xbox.obj",
    "tr_font.obj": "tr_font.obj",
    "tr_ghoul2.obj": "tr_ghoul2.obj",
    "tr_image.obj": "tr_image_xbox.obj",
    "tr_init.obj": "tr_init.obj",
    "tr_light_retail.obj": "tr_light.obj",
    "tr_main_retail.obj": "tr_main.obj",
    "tr_marks.obj": "tr_marks.obj",
    "tr_mesh.obj": "tr_mesh.obj",
    "tr_model.obj": "tr_model.obj",
    "tr_noise.obj": "tr_noise.obj",
    "tr_quicksprite.obj": "tr_quicksprite.obj",
    "tr_scene_retail.obj": "tr_scene.obj",
    "tr_shade_calc_retail.obj": "tr_shade_calc.obj",
    "tr_shade_retail.obj": "tr_shade.obj",
    "tr_shader_retail.obj": "tr_shader.obj",
    "tr_shadows.obj": "tr_shadows.obj",
    "tr_sky_retail.obj": "tr_sky.obj",
    "tr_surface_retail.obj": "tr_surface.obj",
    "tr_surfacesprites.obj": "tr_surfacesprites.obj",
    "tr_world_retail.obj": "tr_world.obj",
    "tr_worldeffects.obj": "tr_worldeffects.obj",
    "win_glimp_console.obj": "win_glimp_console.obj",
    "win_highdynamicrange.obj": "win_highdynamicrange.obj",
    "win_lighteffects.obj": "win_lighteffects.obj",
    "win_qgl_dx8.obj": "win_qgl_dx8.obj",
}


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def path_record(path: Path) -> dict[str, object]:
    resolved = path.resolve()
    record: dict[str, object] = {
        "path": str(resolved),
        "present": resolved.exists(),
    }
    if resolved.exists():
        stat = resolved.stat()
        record["modifiedUtc"] = datetime.fromtimestamp(
            stat.st_mtime, timezone.utc
        ).isoformat().replace("+00:00", "Z")
        if resolved.is_file():
            record["type"] = "file"
            record["bytes"] = stat.st_size
            record["sha256"] = file_sha256(resolved)
        elif resolved.is_dir():
            record["type"] = "directory"
        else:
            record["type"] = "other"
    return record


def report_provenance() -> dict[str, object]:
    script_path = Path(__file__).resolve()
    return {
        "script": str(script_path),
        "scriptSha256": file_sha256(script_path),
        "argv": sys.argv[1:],
    }


def disassemble(link: Path, obj: Path) -> dict[str, list[str]]:
    result = subprocess.run(
        [str(link), "/dump", "/disasm:nobytes", str(obj)],
        check=True,
        capture_output=True,
        text=True,
        errors="replace",
    )

    functions: dict[str, list[str]] = {}
    current: list[str] | None = None
    for line in result.stdout.splitlines():
        function_match = FUNCTION_RE.match(line)
        if function_match:
            current = functions.setdefault(function_match.group(1), [])
            continue

        instruction_match = INSTRUCTION_RE.match(line)
        if current is not None and instruction_match:
            current.append(" ".join(instruction_match.group(1).split()))

    return functions


def find_objects(root: Path) -> dict[str, Path]:
    found: dict[str, Path] = {}
    for path in root.rglob("*.obj"):
        found.setdefault(path.name.lower(), path)
    return found


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--current-root", type=Path, required=True)
    parser.add_argument("--donor-root", type=Path, required=True)
    parser.add_argument("--link", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--include-names", action="store_true")
    parser.add_argument(
        "--pair",
        action="append",
        metavar="CURRENT=DONOR",
        help="compare an explicit object pair; repeat to replace the renderer defaults",
    )
    args = parser.parse_args()

    object_pairs = OBJECT_PAIRS
    if args.pair:
        object_pairs = {}
        for value in args.pair:
            current_name, separator, donor_name = value.partition("=")
            if not separator or not current_name or not donor_name:
                parser.error(f"invalid --pair value: {value!r}")
            object_pairs[current_name] = donor_name

    current_objects = find_objects(args.current_root)
    donor_objects = find_objects(args.donor_root)
    rows = []

    for current_name, donor_name in object_pairs.items():
        current_path = current_objects.get(current_name.lower())
        donor_path = donor_objects.get(donor_name.lower())
        if current_path is None or donor_path is None:
            rows.append(
                {
                    "current_object": current_name,
                    "donor_object": donor_name,
                    "currentFile": path_record(current_path) if current_path else None,
                    "donorFile": path_record(donor_path) if donor_path else None,
                    "error": "missing object",
                }
            )
            continue

        current_functions = disassemble(args.link, current_path)
        donor_functions = disassemble(args.link, donor_path)
        common = sorted(set(current_functions) & set(donor_functions))
        exact = [
            name
            for name in common
            if current_functions[name] == donor_functions[name]
        ]
        same_length = [
            name
            for name in common
            if len(current_functions[name]) == len(donor_functions[name])
        ]

        row = {
            "current_object": current_name,
            "donor_object": donor_name,
            "currentFile": path_record(current_path),
            "donorFile": path_record(donor_path),
            "current_functions": len(current_functions),
            "donor_functions": len(donor_functions),
            "common_functions": len(common),
            "same_instruction_count": len(same_length),
            "exact_instruction_text": len(exact),
        }
        if args.include_names:
            row["exact_names"] = exact
            row["same_length_nonexact_names"] = sorted(set(same_length) - set(exact))
            row["different_length_names"] = sorted(set(common) - set(same_length))
            row["function_instruction_counts"] = {
                name: {
                    "current": len(current_functions[name]),
                    "donor": len(donor_functions[name]),
                }
                for name in common
            }
        rows.append(row)

    compared = [row for row in rows if "error" not in row]
    totals = {
        "objects": len(compared),
        "common_functions": sum(row["common_functions"] for row in compared),
        "same_instruction_count": sum(
            row["same_instruction_count"] for row in compared
        ),
        "exact_instruction_text": sum(
            row["exact_instruction_text"] for row in compared
        ),
    }
    payload = {
        "reportType": REPORT_TYPE,
        "reportSchemaVersion": REPORT_SCHEMA_VERSION,
        "generatedAtUtc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "verifierProvenance": report_provenance(),
        "inputs": {
            "currentRoot": path_record(args.current_root),
            "donorRoot": path_record(args.donor_root),
            "link": path_record(args.link),
            "includeNames": bool(args.include_names),
            "objectPairs": dict(object_pairs),
        },
        "summary": totals,
        "objects": rows,
    }
    rendered = json.dumps(payload, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
