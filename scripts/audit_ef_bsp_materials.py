#!/usr/bin/env python3
"""Audit Elite Force BSP materials against shader scripts and image assets.

This is intentionally outside the game runtime: it reads Q3/EF BSP shader
lumps, resolves each material through the loaded .shader scripts, and checks
image references using the Xbox renderer's lookup order.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import zipfile
from dataclasses import dataclass, field
from pathlib import Path


IMAGE_EXTS = (".dds", ".jpg", ".jpeg", ".tga")
BSP_LUMP_COUNT = 17
BSP_SHADER_LUMP = 1
BSP_SURFACE_LUMP = 13
BSP_SHADER_SIZE = 72
BSP_SURFACE_SIZE = 104


@dataclass
class ShaderStage:
    line: int
    maps: list[str] = field(default_factory=list)
    blend: str | None = None
    alpha: str | None = None
    depthwrite: bool = False
    depthfunc: str | None = None
    tcmods: list[str] = field(default_factory=list)


@dataclass
class ShaderDef:
    name: str
    source: str
    line: int
    surfaceparms: list[str] = field(default_factory=list)
    stages: list[ShaderStage] = field(default_factory=list)


@dataclass
class BspMaterial:
    index: int
    name: str
    surface_flags: int
    content_flags: int
    surface_count: int = 0


def normalized(path: str) -> str:
    return path.replace("\\", "/").lower()


def strip_image_ext(path: str) -> str:
    path = normalized(path)
    for ext in IMAGE_EXTS:
        if path.endswith(ext):
            return path[: -len(ext)]
    return path


def add_asset(assets: dict[str, list[tuple[str, int]]], rel: str, source: str, size: int) -> None:
    assets.setdefault(normalized(rel), []).append((source, size))


def index_assets(base_dir: Path) -> tuple[dict[str, list[tuple[str, int]]], set[str]]:
    assets: dict[str, list[tuple[str, int]]] = {}
    patch_pk3_assets: set[str] = set()

    for path in base_dir.rglob("*"):
        if path.is_file():
            add_asset(assets, path.relative_to(base_dir).as_posix(), "loose", path.stat().st_size)

    for pk3 in sorted(base_dir.glob("*.pk3")):
        with zipfile.ZipFile(pk3) as archive:
            for entry in archive.infolist():
                if entry.is_dir():
                    continue
                rel = normalized(entry.filename)
                add_asset(assets, rel, pk3.name, entry.file_size)
                if pk3.name.lower() == "xbox0.pk3":
                    patch_pk3_assets.add(rel)

    return assets, patch_pk3_assets


def resolve_image(
    token: str,
    assets: dict[str, list[tuple[str, int]]],
    patch_pk3_assets: set[str],
) -> dict[str, object]:
    token_norm = normalized(token)
    if token_norm.startswith("$") or token_norm.startswith("*"):
        return {"token": token, "status": "builtin", "path": token_norm, "sources": []}

    base_name = strip_image_ext(token_norm)

    # Match the Xbox EF renderer: explicit DDS is exact, otherwise use a DDS
    # only when it is present in the patch PK3, then JPG, then TGA.
    if token_norm.endswith(".dds"):
        dds = base_name + ".dds"
        return {
            "token": token,
            "status": "found" if dds in assets else "missing",
            "path": dds,
            "sources": assets.get(dds, []),
        }

    dds = base_name + ".dds"
    if dds in patch_pk3_assets:
        return {"token": token, "status": "found_patch_dds", "path": dds, "sources": assets[dds]}

    for ext in (".jpg", ".tga"):
        candidate = base_name + ext
        if candidate in assets:
            return {"token": token, "status": "found", "path": candidate, "sources": assets[candidate]}

    return {"token": token, "status": "missing", "path": base_name + "(.jpg/.tga)", "sources": []}


COMMENT_LINE = re.compile(r"//.*")
COMMENT_BLOCK = re.compile(r"/\*.*?\*/", re.S)


def strip_comments(text: str) -> str:
    text = COMMENT_BLOCK.sub("", text)
    return "\n".join(COMMENT_LINE.sub("", line) for line in text.splitlines())


def parse_shader_text(text: str, source: str) -> dict[str, list[ShaderDef]]:
    defs: dict[str, list[ShaderDef]] = {}
    lines = strip_comments(text).splitlines()
    i = 0

    while i < len(lines):
        raw = lines[i].strip()
        i += 1
        if not raw or raw in ("{", "}"):
            continue

        name = raw.split()[0]
        j = i
        while j < len(lines) and not lines[j].strip():
            j += 1
        if j >= len(lines) or lines[j].strip() != "{":
            continue

        start_line = j + 1
        i = j + 1
        depth = 1
        body: list[tuple[int, str]] = []
        while i < len(lines) and depth > 0:
            text_line = lines[i].strip()
            if text_line == "{":
                depth += 1
            elif text_line == "}":
                depth -= 1
                if depth == 0:
                    i += 1
                    break
            body.append((i + 1, text_line))
            i += 1

        shader = ShaderDef(name=name, source=source, line=start_line)
        current_stage: ShaderStage | None = None
        for line_no, line in body:
            if not line:
                continue
            tokens = line.split()
            key = tokens[0].lower()

            if line == "{":
                current_stage = ShaderStage(line=line_no)
                continue
            if line == "}":
                if current_stage:
                    shader.stages.append(current_stage)
                    current_stage = None
                continue

            if current_stage is None:
                if key == "surfaceparm" and len(tokens) > 1:
                    shader.surfaceparms.append(tokens[1].lower())
                continue

            if key in ("map", "clampmap") and len(tokens) > 1:
                current_stage.maps.append(tokens[1])
            elif key == "animmap" and len(tokens) > 2:
                current_stage.maps.extend(tokens[2:])
            elif key == "blendfunc":
                current_stage.blend = " ".join(tokens[1:])
            elif key == "alphafunc":
                current_stage.alpha = " ".join(tokens[1:])
            elif key == "depthwrite":
                current_stage.depthwrite = True
            elif key == "depthfunc":
                current_stage.depthfunc = " ".join(tokens[1:])
            elif key == "tcmod":
                current_stage.tcmods.append(" ".join(tokens[1:]))

        defs.setdefault(normalized(name), []).append(shader)

    return defs


def load_shader_defs(base_dir: Path) -> dict[str, list[ShaderDef]]:
    defs: dict[str, list[ShaderDef]] = {}

    def merge(parsed: dict[str, list[ShaderDef]]) -> None:
        for name, items in parsed.items():
            defs.setdefault(name, []).extend(items)

    for path in sorted((base_dir / "scripts").glob("*.shader")):
        merge(parse_shader_text(path.read_text(errors="ignore"), path.relative_to(base_dir).as_posix()))

    for pk3 in sorted(base_dir.glob("*.pk3")):
        with zipfile.ZipFile(pk3) as archive:
            for entry in archive.infolist():
                if normalized(entry.filename).startswith("scripts/") and normalized(entry.filename).endswith(".shader"):
                    text = archive.read(entry).decode("latin1", errors="ignore")
                    merge(parse_shader_text(text, f"{pk3.name}:{entry.filename}"))

    return defs


def parse_bsp_materials(path: Path) -> list[BspMaterial]:
    data = path.read_bytes()
    if data[:4] != b"IBSP":
        raise ValueError(f"{path} is not an IBSP file")

    lumps = [struct.unpack_from("<ii", data, 8 + i * 8) for i in range(BSP_LUMP_COUNT)]
    shader_offset, shader_len = lumps[BSP_SHADER_LUMP]
    materials: list[BspMaterial] = []

    for index in range(shader_len // BSP_SHADER_SIZE):
        record = data[shader_offset + index * BSP_SHADER_SIZE : shader_offset + (index + 1) * BSP_SHADER_SIZE]
        name = record[:64].split(b"\0", 1)[0].decode("latin1")
        surface_flags, content_flags = struct.unpack_from("<ii", record, 64)
        materials.append(BspMaterial(index, name, surface_flags, content_flags))

    surface_offset, surface_len = lumps[BSP_SURFACE_LUMP]
    if surface_len % BSP_SURFACE_SIZE == 0:
        for index in range(surface_len // BSP_SURFACE_SIZE):
            shader_num = struct.unpack_from("<i", data, surface_offset + index * BSP_SURFACE_SIZE)[0]
            if 0 <= shader_num < len(materials):
                materials[shader_num].surface_count += 1

    return materials


def classify_material(name: str, shader: ShaderDef | None) -> str:
    name_norm = normalized(name)
    if name_norm == "textures/common/black":
        return "structural-black"
    if shader is None:
        return "implicit-or-unresolved"

    blends = [(stage.blend or "").lower() for stage in shader.stages]
    has_additive = any(blend == "gl_one gl_one" for blend in blends)
    has_trans = "trans" in shader.surfaceparms or any(stage.alpha for stage in shader.stages)

    if "flare" in name_norm or "forcefield" in shader.surfaceparms or has_additive:
        return "effect/additive-or-flare+trans" if has_trans else "effect/additive-or-flare"
    if has_trans:
        return "shader+trans"
    return "shader"


def audit_material(
    material: BspMaterial,
    shader_defs: dict[str, list[ShaderDef]],
    assets: dict[str, list[tuple[str, int]]],
    patch_pk3_assets: set[str],
) -> dict[str, object]:
    name_norm = normalized(material.name)
    defs = shader_defs.get(name_norm, [])
    shader = defs[-1] if defs else None

    if shader:
        maps = [map_token for stage in shader.stages for map_token in stage.maps]
        resolved = [resolve_image(token, assets, patch_pk3_assets) for token in maps]
        definition = f"{shader.source}:{shader.line}"
        stages = [
            {
                "line": stage.line,
                "maps": stage.maps,
                "blend": stage.blend,
                "alpha": stage.alpha,
                "depthwrite": stage.depthwrite,
                "depthfunc": stage.depthfunc,
                "tcmods": stage.tcmods,
            }
            for stage in shader.stages
        ]
    else:
        maps = [material.name]
        resolved = [resolve_image(material.name, assets, patch_pk3_assets)]
        definition = None
        stages = [
            {
                "line": None,
                "maps": maps,
                "blend": None,
                "alpha": None,
                "depthwrite": False,
                "depthfunc": None,
                "tcmods": [],
            }
        ]

    missing = [item for item in resolved if item["status"] == "missing"]
    return {
        "index": material.index,
        "name": material.name,
        "surfaceCount": material.surface_count,
        "surfaceFlags": material.surface_flags,
        "contentFlags": material.content_flags,
        "definition": definition,
        "definitionCount": len(defs),
        "kind": classify_material(material.name, shader),
        "maps": maps,
        "stages": stages,
        "resolved": resolved,
        "missing": missing,
    }


def run_audit(base_dir: Path, maps: list[str]) -> dict[str, object]:
    assets, patch_pk3_assets = index_assets(base_dir)
    shader_defs = load_shader_defs(base_dir)

    result: dict[str, object] = {"baseDir": str(base_dir), "maps": {}}
    maps_out: dict[str, object] = result["maps"]  # type: ignore[assignment]
    for map_name in maps:
        bsp = base_dir / "maps" / f"{map_name}.bsp"
        rows = [
            audit_material(material, shader_defs, assets, patch_pk3_assets)
            for material in parse_bsp_materials(bsp)
        ]
        used_rows = [row for row in rows if int(row["surfaceCount"]) > 0]
        missing_rows = [row for row in used_rows if row["missing"]]
        unresolved_rows = [row for row in used_rows if row["definition"] is None and row["missing"]]
        fx_rows = [
            row
            for row in used_rows
            if str(row["kind"]).startswith("effect/") or row["kind"] == "structural-black"
        ]
        maps_out[map_name] = {
            "bsp": str(bsp),
            "materialCount": len(rows),
            "usedMaterialCount": len(used_rows),
            "usedMissingAssetCount": len(missing_rows),
            "usedUnresolvedMaterialCount": len(unresolved_rows),
            "expectedEffectOrBlackCount": len(fx_rows),
            "materials": rows,
        }
    return result


def print_summary(result: dict[str, object]) -> None:
    maps = result["maps"]
    assert isinstance(maps, dict)
    for map_name, map_result in maps.items():
        assert isinstance(map_result, dict)
        print(
            f"{map_name}: materials={map_result['materialCount']} "
            f"used={map_result['usedMaterialCount']} "
            f"used_missing_assets={map_result['usedMissingAssetCount']} "
            f"used_unresolved_materials={map_result['usedUnresolvedMaterialCount']} "
            f"expected_fx_or_black={map_result['expectedEffectOrBlackCount']}"
        )
        materials = map_result["materials"]
        assert isinstance(materials, list)
        for row in materials:
            assert isinstance(row, dict)
            if not row["surfaceCount"]:
                continue
            if not row["missing"] and not str(row["kind"]).startswith("effect/") and row["kind"] != "structural-black":
                continue
            print(
                f"  idx={row['index']:>3} surf={row['surfaceCount']:>4} "
                f"{row['name']} kind={row['kind']} def={row['definition']} "
                f"missing={','.join(item['path'] for item in row['missing'])}"
            )


def _sources_to_text(sources: object) -> str:
    if not sources:
        return ""
    items = []
    assert isinstance(sources, list)
    for source, size in sources:
        items.append(f"{source}({size})")
    return ", ".join(items)


def _resolved_by_token(row: dict[str, object]) -> dict[str, dict[str, object]]:
    resolved = row["resolved"]
    assert isinstance(resolved, list)
    out: dict[str, dict[str, object]] = {}
    for item in resolved:
        assert isinstance(item, dict)
        out[str(item["token"])] = item
    return out


def write_material_report(result: dict[str, object], path: Path, include_unused: bool = False) -> None:
    lines: list[str] = []
    maps = result["maps"]
    assert isinstance(maps, dict)

    lines.append(f"BaseEF: {result['baseDir']}")
    lines.append("")

    for map_name, map_result in maps.items():
        assert isinstance(map_result, dict)
        lines.append(
            f"[{map_name}] materials={map_result['materialCount']} "
            f"used={map_result['usedMaterialCount']} "
            f"missing={map_result['usedMissingAssetCount']} "
            f"unresolved={map_result['usedUnresolvedMaterialCount']}"
        )
        materials = map_result["materials"]
        assert isinstance(materials, list)

        for row_obj in materials:
            assert isinstance(row_obj, dict)
            if not include_unused and not int(row_obj["surfaceCount"]):
                continue

            lines.append(
                f"  idx={int(row_obj['index']):03d} surfaces={int(row_obj['surfaceCount']):04d} "
                f"flags=0x{int(row_obj['surfaceFlags']):x}/0x{int(row_obj['contentFlags']):x} "
                f"kind={row_obj['kind']} name={row_obj['name']}"
            )
            lines.append(
                f"    shader={row_obj['definition'] if row_obj['definition'] else '<implicit image lookup>'} "
                f"defs={row_obj['definitionCount']}"
            )

            resolved_by_token = _resolved_by_token(row_obj)
            stages = row_obj["stages"]
            assert isinstance(stages, list)
            for stage_index, stage_obj in enumerate(stages):
                assert isinstance(stage_obj, dict)
                blend = stage_obj["blend"] if stage_obj["blend"] else "-"
                alpha = stage_obj["alpha"] if stage_obj["alpha"] else "-"
                depthfunc = stage_obj["depthfunc"] if stage_obj["depthfunc"] else "-"
                tcmods = stage_obj["tcmods"]
                assert isinstance(tcmods, list)
                lines.append(
                    f"    stage={stage_index} line={stage_obj['line']} blend={blend} "
                    f"alpha={alpha} depthwrite={int(bool(stage_obj['depthwrite']))} "
                    f"depthfunc={depthfunc} tcmods={' | '.join(str(x) for x in tcmods) if tcmods else '-'}"
                )
                maps_obj = stage_obj["maps"]
                assert isinstance(maps_obj, list)
                if not maps_obj:
                    lines.append("      map=<none>")
                for token in maps_obj:
                    token_text = str(token)
                    resolved = resolved_by_token.get(token_text)
                    if resolved:
                        lines.append(
                            f"      map={token_text} -> {resolved['status']} {resolved['path']} "
                            f"{_sources_to_text(resolved['sources'])}"
                        )
                    else:
                        lines.append(f"      map={token_text} -> <not resolved>")

        lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-dir", type=Path, default=Path("build/release/BaseEF"))
    parser.add_argument("--maps", default="borg1,borg2,borg3,borg4,borg5,borg6")
    parser.add_argument("--json", type=Path, default=None, help="Optional output JSON path")
    parser.add_argument("--report", type=Path, default=None, help="Optional text material trace report path")
    parser.add_argument("--include-unused", action="store_true", help="Include unused BSP material slots in report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    maps = [entry.strip() for entry in args.maps.split(",") if entry.strip()]
    result = run_audit(args.base_dir, maps)
    print_summary(result)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(f"wrote {args.json}")
    if args.report:
        write_material_report(result, args.report, args.include_unused)
        print(f"wrote {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
