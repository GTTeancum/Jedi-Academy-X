#!/usr/bin/env python3
"""Materialize official PK3 assets needed by the Xbox Holomatch packer."""

from __future__ import annotations

import argparse
import json
import re
import zipfile
from pathlib import Path, PurePosixPath


IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".tga"}
MP_MAP_PREFIXES = ("ctf_", "hm_", "dm_", "team_")
PAK_NAME = re.compile(r"^pak(\d+)\.pk3$", re.IGNORECASE)


def archive_sort_key(path: Path) -> tuple[int, str]:
    match = PAK_NAME.match(path.name)
    return (int(match.group(1)) if match else -1, path.name.lower())


def safe_relative_path(name: str) -> Path | None:
    normalized = name.replace("\\", "/").lstrip("/")
    pure = PurePosixPath(normalized)
    if not normalized or pure.is_absolute() or ".." in pure.parts:
        return None
    return Path(*pure.parts)


def should_extract(name: str) -> bool:
    normalized = name.replace("\\", "/").lower()
    pure = PurePosixPath(normalized)
    suffix = pure.suffix.lower()

    if suffix in IMAGE_EXTS or suffix == ".shader":
        return True
    if normalized.startswith("scripts/") and suffix == ".arena":
        return True
    if len(pure.parts) == 2 and pure.parts[0] == "maps":
        return pure.stem.startswith(MP_MAP_PREFIXES) and suffix in {".bsp", ".aas"}
    return False


def extract_overlay(base_dir: Path, archive_dir: Path) -> dict[str, object]:
    archives = sorted(
        (
            path
            for path in archive_dir.iterdir()
            if path.is_file() and PAK_NAME.match(path.name)
        ),
        key=archive_sort_key,
    )
    if not archives:
        raise FileNotFoundError(f"no retail PAK archives found in {archive_dir}")
    extracted: dict[str, str] = {}
    archive_counts: dict[str, int] = {}

    for archive in archives:
        count = 0
        with zipfile.ZipFile(archive, "r") as source:
            for entry in source.infolist():
                if entry.is_dir() or not should_extract(entry.filename):
                    continue
                relative = safe_relative_path(entry.filename)
                if relative is None:
                    raise ValueError(f"unsafe PK3 entry in {archive}: {entry.filename}")
                destination = base_dir / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(source.read(entry))
                normalized = relative.as_posix().lower()
                extracted[normalized] = archive.name
                count += 1
        archive_counts[archive.name] = count

    manifest = {
        "archiveSource": str(archive_dir),
        "archives": [archive.name for archive in archives],
        "archiveCounts": archive_counts,
        "extractedFiles": len(extracted),
        "multiplayerMaps": sorted(
            PurePosixPath(path).stem
            for path in extracted
            if path.startswith("maps/") and path.endswith(".bsp")
        ),
    }
    (base_dir / "xbox_mp_source_overlay.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="ascii"
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-dir", required=True, type=Path)
    parser.add_argument("--archive-dir", type=Path)
    args = parser.parse_args()
    base_dir = args.base_dir.resolve()
    if not base_dir.is_dir():
        raise SystemExit(f"BaseEF directory not found: {base_dir}")
    archive_dir = (args.archive_dir or base_dir).resolve()
    if not archive_dir.is_dir():
        raise SystemExit(f"Retail PK3 archive directory not found: {archive_dir}")

    manifest = extract_overlay(base_dir, archive_dir)
    print(
        "Materialized official MP PK3 source overlay: "
        f"{manifest['extractedFiles']} files, "
        f"{len(manifest['multiplayerMaps'])} archive maps"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
