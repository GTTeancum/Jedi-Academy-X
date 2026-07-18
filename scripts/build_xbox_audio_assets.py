#!/usr/bin/env python3
"""Create Xbox-native ADPCM WAV alternates for MP3 music and sound assets."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import os
import struct
from pathlib import Path


def find_tool(explicit: Path | None, names: tuple[str, ...]) -> Path | None:
    if explicit and explicit.exists():
        return explicit
    for name in names:
        found = shutil.which(name)
        if found:
            return Path(found)
    return None


def collect_mp3s(
    base_dir: Path,
    include_music: bool,
    include_sound: bool,
    all_sound: bool,
    sound_filters: tuple[str, ...],
) -> list[tuple[Path, str]]:
    roots: list[tuple[Path, str]] = []
    if include_music:
        roots.append((base_dir / "music", "music"))
    if include_sound:
        roots.append((base_dir / "sound", "sound"))

    files: list[tuple[Path, str]] = []
    for root, kind in roots:
        if not root.exists():
            continue
        for path in sorted(root.rglob("*.mp3")):
            if path.is_file():
                rel = path.relative_to(base_dir).as_posix().lower()
                if kind == "sound" and not all_sound and not any(token in rel for token in sound_filters):
                    continue
                files.append((path, kind))
    return files


def needs_update(source: Path, output: Path) -> bool:
    if not output.exists():
        return True
    return output.stat().st_mtime < source.stat().st_mtime


def read_wave_format_tag(path: Path) -> int | None:
    try:
        with path.open("rb") as wave:
            header = wave.read(12)
            if len(header) < 12 or header[:4] != b"RIFF" or header[8:12] != b"WAVE":
                return None

            remaining = path.stat().st_size - 12
            while remaining >= 8:
                chunk_header = wave.read(8)
                if len(chunk_header) != 8:
                    return None
                chunk_size = struct.unpack_from("<I", chunk_header, 4)[0]
                padded_size = chunk_size + (chunk_size & 1)
                remaining -= 8
                if padded_size > remaining:
                    return None
                if chunk_header[:4] == b"fmt " and chunk_size >= 16:
                    format_data = wave.read(16)
                    return struct.unpack_from("<H", format_data)[0]
                wave.seek(padded_size, 1)
                remaining -= padded_size
    except OSError:
        return None
    return None


def run_checked(args: list[str], label: str) -> str:
    result = subprocess.run(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"{label} failed with exit {result.returncode}:\n{result.stdout}")
    return result.stdout


def convert_one(
    source: Path,
    kind: str,
    base_dir: Path,
    temp_dir: Path,
    ffmpeg: Path,
    encoder: Path,
    force: bool,
) -> dict[str, object]:
    rel = source.relative_to(base_dir).as_posix().lower()
    output = source.with_suffix(".wav")
    if not force and output.exists():
        output_tag = read_wave_format_tag(output)
        if output_tag != 0x0069 or not needs_update(source, output):
            return {
                "path": rel,
                "output": output.relative_to(base_dir).as_posix().lower(),
                "status": "preserved" if output_tag != 0x0069 else "current",
                "sourceBytes": source.stat().st_size,
                "bytes": output.stat().st_size,
            }

    temp_pcm = temp_dir / (source.stem + ".pcm.wav")
    temp_adpcm = temp_dir / (source.stem + ".xadpcm.wav")
    ffmpeg_args = [
        str(ffmpeg),
        "-y",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(source),
    ]
    rel = source.relative_to(base_dir).as_posix().lower()
    if rel.startswith("sound/voice/"):
        ffmpeg_args.extend(["-ac", "1"])
    elif kind == "music":
        ffmpeg_args.extend(["-ac", "2"])
    ffmpeg_args.extend(["-acodec", "pcm_s16le", str(temp_pcm)])
    run_checked(ffmpeg_args, f"ffmpeg decode {source}")

    run_checked(
        [str(encoder), str(temp_pcm), str(temp_adpcm), "/C", "/Ob"],
        f"xbadpcmencode {source}",
    )
    if read_wave_format_tag(temp_adpcm) != 0x0069:
        raise RuntimeError(f"xbadpcmencode did not produce Xbox ADPCM WAV: {source}")
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(temp_adpcm, output)

    return {
        "path": rel,
        "output": output.relative_to(base_dir).as_posix().lower(),
        "status": "converted",
        "sourceBytes": source.stat().st_size,
        "bytes": output.stat().st_size,
    }


def build_audio_assets(
    base_dir: Path,
    ffmpeg: Path,
    encoder: Path,
    include_music: bool,
    include_sound: bool,
    all_sound: bool,
    sound_filters: tuple[str, ...],
    force: bool,
) -> dict[str, object]:
    mp3s = collect_mp3s(base_dir, include_music, include_sound, all_sound, sound_filters)
    records: list[dict[str, object]] = []
    converted = 0
    current = 0
    preserved = 0
    source_bytes = 0
    output_bytes = 0

    temp_root = base_dir.parent / "tmp" / "audio"
    temp_root.mkdir(parents=True, exist_ok=True)
    for stale in temp_root.glob("stefx_xaudio_*"):
        try:
            if stale.is_dir():
                shutil.rmtree(stale, ignore_errors=True)
            else:
                stale.unlink()
        except OSError:
            pass

    temp_dir = temp_root / ("stefx_xaudio_%d" % os.getpid())
    shutil.rmtree(temp_dir, ignore_errors=True)
    temp_dir.mkdir(parents=True, exist_ok=True)
    temp_name = str(temp_dir)
    try:
        for source, kind in mp3s:
            record = convert_one(source, kind, base_dir, temp_dir, ffmpeg, encoder, force)
            records.append(record)
            source_bytes += int(record["sourceBytes"])
            output_bytes += int(record["bytes"])
            if record["status"] == "converted":
                converted += 1
            elif record["status"] == "preserved":
                preserved += 1
            else:
                current += 1
    finally:
        shutil.rmtree(temp_name, ignore_errors=True)
    manifest = {
        "format": "stefx-xbox-audio-assets-v1",
        "encoding": "xbox-adpcm-wav",
        "source": "mp3",
        "allSound": all_sound,
        "soundFilters": list(sound_filters),
        "records": len(records),
        "converted": converted,
        "current": current,
        "preserved": preserved,
        "sourceBytes": source_bytes,
        "bytes": output_bytes,
        "ffmpeg": str(ffmpeg),
        "encoder": str(encoder),
        "assets": records,
    }

    out_dir = base_dir / "soundbank"
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = out_dir / "xbox_audio_assets_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="ascii")
    manifest["manifest"] = str(manifest_path)
    return manifest


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert EF MP3 music/voice to Xbox ADPCM WAV")
    parser.add_argument("--base-dir", type=Path, default=Path("build/release/BaseEF"))
    parser.add_argument("--ffmpeg", type=Path, default=None)
    parser.add_argument("--encoder", type=Path, default=Path(r"C:\XDK\xbox\bin\xbadpcmencode.exe"))
    parser.add_argument("--skip-music", action="store_true")
    parser.add_argument("--skip-sound", action="store_true")
    parser.add_argument("--all-sound", action="store_true")
    parser.add_argument("--all-voice", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument(
        "--sound-filter",
        action="append",
        default=["/borg1/"],
        help="Lowercase substring filter for sound paths when --all-sound is not set.",
    )
    parser.add_argument("--voice-filter", action="append", default=[], help=argparse.SUPPRESS)
    parser.add_argument("--force", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    ffmpeg = find_tool(args.ffmpeg, ("ffmpeg.exe", "ffmpeg"))
    encoder = find_tool(args.encoder, ("xbadpcmencode.exe",))
    if not ffmpeg:
        raise SystemExit("ffmpeg was not found")
    if not encoder:
        raise SystemExit("xbadpcmencode.exe was not found")

    manifest = build_audio_assets(
        args.base_dir.resolve(),
        ffmpeg.resolve(),
        encoder.resolve(),
        not args.skip_music,
        not args.skip_sound,
        args.all_sound or args.all_voice,
        tuple(item.lower().replace("\\", "/") for item in (args.sound_filter + args.voice_filter)),
        args.force,
    )
    print(json.dumps(manifest, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())



