#!/usr/bin/env python3
"""Report shader-stage expansion for an Elite Force BSP without running the game."""

from __future__ import annotations

import argparse
import json
import re
import struct
from collections import defaultdict
from pathlib import Path
from zipfile import ZipFile


LUMP_SHADERS = 1
LUMP_SURFACES = 13
SHADER_RECORD_SIZE = 72
SURFACE_RECORD_SIZE = 104
TOKEN_RE = re.compile(r'"[^"\r\n]*"|[{}]|[^\s{}]+')


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\r\n]*", " ", text)


def parse_shader_file(text: str) -> dict[str, list[list[str]]]:
    tokens = TOKEN_RE.findall(strip_comments(text))
    shaders: dict[str, list[list[str]]] = {}
    index = 0
    while index + 1 < len(tokens):
        name = tokens[index].strip('"').lower()
        index += 1
        if tokens[index] != "{":
            index += 1
            continue
        index += 1
        depth = 1
        stages: list[list[str]] = []
        current: list[str] | None = None
        while index < len(tokens) and depth:
            token = tokens[index]
            index += 1
            if token == "{":
                depth += 1
                if depth == 2:
                    current = []
            elif token == "}":
                if depth == 2 and current is not None:
                    stages.append(current)
                    current = None
                depth -= 1
            elif depth == 2 and current is not None:
                current.append(token.strip('"').lower())
        shaders[name] = stages
    return shaders


def load_shader_definitions(pk3_paths: list[Path]) -> dict[str, list[list[str]]]:
    definitions: dict[str, list[list[str]]] = {}
    for pk3_path in pk3_paths:
        with ZipFile(pk3_path) as archive:
            for member in archive.namelist():
                if member.lower().startswith("scripts/") and member.lower().endswith(".shader"):
                    text = archive.read(member).decode("latin1", "replace")
                    definitions.update(parse_shader_file(text))
    return definitions


def read_map(pk3_paths: list[Path], map_name: str) -> bytes:
    member = f"maps/{map_name.removesuffix('.bsp')}.bsp".lower()
    found: bytes | None = None
    for pk3_path in pk3_paths:
        with ZipFile(pk3_path) as archive:
            names = {name.lower(): name for name in archive.namelist()}
            if member in names:
                found = archive.read(names[member])
    if found is None:
        raise FileNotFoundError(f"Could not find {member} in supplied PK3 files")
    return found


def bsp_lumps(data: bytes) -> list[tuple[int, int]]:
    ident, version = struct.unpack_from("<4sI", data, 0)
    if ident != b"IBSP" or version != 46:
        raise ValueError(f"Expected Elite Force IBSP 46, got {ident!r} version {version}")
    return [struct.unpack_from("<II", data, 8 + index * 8) for index in range(17)]


def stage_value(stage: list[str], keyword: str) -> str:
    try:
        index = stage.index(keyword)
    except ValueError:
        return ""
    return stage[index + 1] if index + 1 < len(stage) else ""


def blend_mode(stage: list[str]) -> tuple[str, str]:
    try:
        index = stage.index("blendfunc")
    except ValueError:
        return ("one", "zero")
    if index + 1 >= len(stage):
        return ("", "")
    first = stage[index + 1]
    aliases = {
        "add": ("one", "one"),
        "filter": ("dst_color", "zero"),
        "blend": ("src_alpha", "one_minus_src_alpha"),
    }
    if first in aliases:
        return aliases[first]
    second = stage[index + 2] if index + 2 < len(stage) else ""
    return (first.removeprefix("gl_"), second.removeprefix("gl_"))


def first_pair_collapses(stages: list[list[str]]) -> bool:
    if len(stages) < 2:
        return False
    supported = {
        (("one", "zero"), ("src_color", "zero")),
        (("one", "zero"), ("dst_color", "zero")),
        (("dst_color", "zero"), ("dst_color", "zero")),
        (("src_color", "zero"), ("dst_color", "zero")),
        (("dst_color", "zero"), ("src_color", "zero")),
        (("src_color", "zero"), ("src_color", "zero")),
        (("one", "zero"), ("one", "one")),
        (("one", "one"), ("one", "one")),
    }
    if (blend_mode(stages[0]), blend_mode(stages[1])) not in supported:
        return False
    rgb0 = stage_value(stages[0], "rgbgen") or "identity"
    rgb1 = stage_value(stages[1], "rgbgen") or "identity"
    alpha0 = stage_value(stages[0], "alphagen") or "identity"
    alpha1 = stage_value(stages[1], "alphagen") or "identity"
    return rgb0 == rgb1 and alpha0 == alpha1


def summarize(data: bytes, definitions: dict[str, list[list[str]]]) -> list[dict[str, object]]:
    lumps = bsp_lumps(data)
    shader_offset, shader_length = lumps[LUMP_SHADERS]
    shader_names = [
        data[offset : offset + 64].split(b"\0", 1)[0].decode("latin1").lower()
        for offset in range(shader_offset, shader_offset + shader_length, SHADER_RECORD_SIZE)
    ]
    aggregate: dict[int, list[int]] = defaultdict(lambda: [0, 0, 0, 0])
    surface_offset, surface_length = lumps[LUMP_SURFACES]
    for offset in range(surface_offset, surface_offset + surface_length, SURFACE_RECORD_SIZE):
        shader_index = struct.unpack_from("<i", data, offset)[0]
        vertices = struct.unpack_from("<i", data, offset + 16)[0]
        indexes = struct.unpack_from("<i", data, offset + 24)[0]
        lightmap = struct.unpack_from("<i", data, offset + 28)[0]
        row = aggregate[shader_index]
        row[0] += 1
        row[1] += vertices
        row[2] += indexes
        row[3] += int(lightmap >= 0)

    rows: list[dict[str, object]] = []
    for shader_index, (surfaces, vertices, indexes, lightmapped) in aggregate.items():
        name = shader_names[shader_index]
        stages = definitions.get(name, [])
        detail_stages = sum("detail" in stage for stage in stages)
        collapsed = first_pair_collapses(stages)
        if stages:
            submissions = len(stages) - int(collapsed)
        else:
            submissions = 1
        extra = max(0, submissions - 1)
        rows.append(
            {
                "shader": name,
                "surfaces": surfaces,
                "vertices": vertices,
                "indexes": indexes,
                "lightmappedSurfaces": lightmapped,
                "explicitStages": len(stages),
                "detailStages": detail_stages,
                "firstPairCollapses": collapsed,
                "estimatedSubmissionsPerBatch": submissions,
                "estimatedExtraIndexWork": indexes * extra,
                "stageMaps": [stage_value(stage, "map") for stage in stages],
                "stageBlends": ["/".join(blend_mode(stage)) for stage in stages],
            }
        )
    return sorted(rows, key=lambda row: (int(row["estimatedExtraIndexWork"]), int(row["indexes"])), reverse=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("map")
    parser.add_argument(
        "--base-ef",
        type=Path,
        default=Path(r"C:\Games\Emulators\stefx_iso_seed_complete\BaseEF"),
    )
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    pk3_paths = sorted(args.base_ef.glob("*.pk3"), key=lambda path: path.name.lower())
    if not pk3_paths:
        raise FileNotFoundError(f"No PK3 files found under {args.base_ef}")
    rows = summarize(read_map(pk3_paths, args.map.lower()), load_shader_definitions(pk3_paths))
    total_indexes = sum(int(row["indexes"]) for row in rows)
    extra_indexes = sum(int(row["estimatedExtraIndexWork"]) for row in rows)
    detail_indexes = sum(
        int(row["indexes"]) * int(row["detailStages"])
        for row in rows
    )
    result = {
        "map": args.map.removesuffix(".bsp"),
        "pk3Files": [path.name for path in pk3_paths],
        "shaderCount": len(rows),
        "totalSourceIndexes": total_indexes,
        "estimatedExtraIndexWork": extra_indexes,
        "estimatedIndexExpansion": (total_indexes + extra_indexes) / max(1, total_indexes),
        "detailStageIndexWork": detail_indexes,
        "shaders": rows,
    }
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(
        f"{result['map']}: shaders={len(rows)} sourceIndexes={total_indexes} "
        f"extraIndexes={extra_indexes} expansion={result['estimatedIndexExpansion']:.2f}x "
        f"detailIndexWork={detail_indexes}"
    )
    for row in rows[:20]:
        print(
            f"{row['estimatedExtraIndexWork']:7} extra  {row['indexes']:7} idx  "
            f"{row['surfaces']:4} surf  {row['explicitStages']} stages/"
            f"{row['detailStages']} detail  {row['shader']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
