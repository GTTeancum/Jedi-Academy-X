#!/usr/bin/env python3
"""Validate the Elite Force Xbox packed-BSP converter across canonical maps."""

from __future__ import annotations

import argparse
import importlib.util
import struct
import tempfile
from pathlib import Path
from zipfile import ZipFile


FIXED_RECORD_SIZES = {
    "brushes": 7,
    "brushsides": 6,
    "faces": 29,
    "flares": 22,
    "indexes": 2,
    "leafbrushes": 4,
    "leafs": 23,
    "leafsurfaces": 4,
    "lightarray": 2,
    "misc": 4,
    "models": 36,
    "nodes": 20,
    "patches": 33,
    "planes": 16,
    "shaders": 72,
    "trisurfs": 19,
    "verts": 68,
}
SURFACE_LUMPS = ("faces", "patches", "trisurfs", "flares")
EF_LUMP_SURFACES = 13
EF_LUMP_LIGHTGRID = 15
EF_LUMP_SHADERS = 1
EF_LUMP_BRUSHES = 8
EF_LUMP_BRUSHSIDES = 9


def load_patch_builder(repo_root: Path):
    path = repo_root / "scripts" / "build_xbox_patch_pk3.py"
    spec = importlib.util.spec_from_file_location("build_xbox_patch_pk3", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def find_archive(repo_root: Path, base_ef: Path, pak_name: str) -> Path:
    candidates = (
        base_ef / pak_name,
        repo_root / "third_party_private" / "elite-force-runtime" / "BaseEF" / pak_name,
        Path(r"C:\Games\Emulators\stefx_iso_seed_complete\BaseEF") / pak_name,
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"Could not locate canonical {pak_name}")


def extract_multiplayer_maps(
    repo_root: Path, base_ef: Path, destination: Path
) -> list[Path]:
    for pak_name in ("PAK0.PK3", "PAK3.PK3"):
        pak_path = find_archive(repo_root, base_ef, pak_name)
        with ZipFile(pak_path) as archive:
            for name in archive.namelist():
                basename = Path(name).name.lower()
                if not name.lower().startswith("maps/") or not basename.endswith(".bsp"):
                    continue
                if not basename.startswith(("hm_", "ctf_", "dm_", "team_")):
                    continue
                output = destination / basename
                if not output.exists():
                    output.write_bytes(archive.read(name))
    return sorted(destination.glob("*.bsp"))


def validate_map(builder, repo_root: Path, bsp_path: Path) -> dict[str, object]:
    lumps, report = builder.packed_bsp_lumps(repo_root, bsp_path)
    for name, width in FIXED_RECORD_SIZES.items():
        if len(lumps[name]) % width:
            raise RuntimeError(
                f"{bsp_path.name}: {name} has {len(lumps[name])} bytes, not a multiple of {width}"
            )

    raw = bsp_path.read_bytes()
    raw_lumps = builder.parse_bsp_lumps(raw, bsp_path)
    raw_surface_count = raw_lumps[EF_LUMP_SURFACES][1] // 104
    misc_surface_count = struct.unpack("<I", lumps["misc"])[0]
    packed_surface_count = sum(
        len(lumps[name]) // FIXED_RECORD_SIZES[name] for name in SURFACE_LUMPS
    )
    if raw_surface_count != misc_surface_count or raw_surface_count != packed_surface_count:
        raise RuntimeError(
            f"{bsp_path.name}: surface count raw={raw_surface_count}, "
            f"misc={misc_surface_count}, packed={packed_surface_count}"
        )

    raw_grid_count = raw_lumps[EF_LUMP_LIGHTGRID][1] // 8
    grid = lumps["lightgrid"]
    grid_headers_size = raw_grid_count * 7
    if len(grid) < grid_headers_size:
        raise RuntimeError(f"{bsp_path.name}: packed light-grid headers are truncated")
    for index in range(raw_grid_count):
        flags = grid[index * 7]
        data_offset = struct.unpack_from("<I", grid, index * 7 + 3)[0]
        data_size = 1 + (6 if flags & 1 else 0)
        if data_offset < grid_headers_size or data_offset + data_size > len(grid):
            raise RuntimeError(
                f"{bsp_path.name}: light-grid {index} points outside its packed data"
            )
    if len(lumps["lightarray"]) != raw_grid_count * 2:
        raise RuntimeError(f"{bsp_path.name}: light-array count does not match light-grid")
    return report


def source_shader_ranges(builder, bsp_path: Path) -> tuple[int, int, int, int]:
    raw = bsp_path.read_bytes()
    lumps = builder.parse_bsp_lumps(raw, bsp_path)
    shader_count = lumps[EF_LUMP_SHADERS][1] // 72

    brush_offset, brush_length = lumps[EF_LUMP_BRUSHES]
    brush_max = max(
        (struct.unpack_from("<iii", raw, brush_offset + index * 12)[2]
         for index in range(brush_length // 12)),
        default=-1,
    )
    side_offset, side_length = lumps[EF_LUMP_BRUSHSIDES]
    side_max = max(
        (struct.unpack_from("<ii", raw, side_offset + index * 8)[1]
         for index in range(side_length // 8)),
        default=-1,
    )
    surface_offset, surface_length = lumps[EF_LUMP_SURFACES]
    surface_max = max(
        (struct.unpack_from("<i", raw, surface_offset + index * 104)[0]
         for index in range(surface_length // 104)),
        default=-1,
    )
    return shader_count, brush_max, side_max, surface_max


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    base_ef = repo_root / "build" / "release" / "BaseEF"
    campaign_maps = sorted((base_ef / "maps").glob("*.bsp"))
    builder = load_patch_builder(repo_root)

    totals = {"SP": 0, "MP": 0}
    largest = (0, "", "")
    with tempfile.TemporaryDirectory(prefix="stefx-packed-bsp-audit-") as temp_name:
        multiplayer_dir = Path(temp_name) / "multiplayer"
        multiplayer_dir.mkdir()
        multiplayer_maps = extract_multiplayer_maps(repo_root, base_ef, multiplayer_dir)
        print(
            f"Packed BSP audit: SP={len(campaign_maps)} MP={len(multiplayer_maps)} "
            f"total={len(campaign_maps) + len(multiplayer_maps)}",
            flush=True,
        )
        shader_ranges = []
        for kind, paths in (("SP", campaign_maps), ("MP", multiplayer_maps)):
            for path in paths:
                values = source_shader_ranges(builder, path)
                shader_ranges.append((max(values), kind, path.name, values))
        shader_ranges.sort(reverse=True)
        print(
            "Source shader ranges: "
            + ", ".join(
                f"{kind}:{name}=count{values[0]}/brush{values[1]}/side{values[2]}/surface{values[3]}"
                for _, kind, name, values in shader_ranges[:12]
            ),
            flush=True,
        )
        for kind, paths in (("SP", campaign_maps), ("MP", multiplayer_maps)):
            for index, path in enumerate(paths, 1):
                print(f"AUDIT {kind} {index:02d}/{len(paths):02d} {path.stem}", flush=True)
                report = validate_map(builder, repo_root, path)
                packed_bytes = int(report["packedLumpBytes"])
                largest_bytes = int(report["largestPackedLumpBytes"])
                totals[kind] += packed_bytes
                if largest_bytes > largest[0]:
                    largest = (
                        largest_bytes,
                        path.name,
                        str(report["largestPackedLump"]),
                    )
                print(
                    f"PASS {kind} {index:02d}/{len(paths):02d} {path.stem}: "
                    f"raw={path.stat().st_size} packed={packed_bytes} "
                    f"largest={report['largestPackedLump']}:{largest_bytes}",
                    flush=True,
                )

    print(
        f"Packed BSP audit passed: maps={len(campaign_maps) + len(multiplayer_maps)} "
        f"SP-bytes={totals['SP']} MP-bytes={totals['MP']} "
        f"largest={largest[1]}:{largest[2]}:{largest[0]}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
