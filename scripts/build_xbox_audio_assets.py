#!/usr/bin/env python3
"""Create Xbox-native ADPCM WAV alternates for MP3 music and voice assets."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import tempfile
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
    include_voice: bool,
    all_voice: bool,
    voice_filters: tuple[str, ...],
) -> list[tuple[Path, str]]:
    roots: list[tuple[Path, str]] = []
    if include_music:
        roots.append((base_dir / "music", "music"))
    if include_voice:
        roots.append((base_dir / "sound" / "voice", "voice"))

    files: list[tuple[Path, str]] = []
    for root, kind in roots:
        if not root.exists():
            continue
        for path in sorted(root.rglob("*.mp3")):
            if path.is_file():
                rel = path.relative_to(base_dir).as_posix().lower()
                if kind == "voice" and not all_voice and not any(token in rel for token in voice_filters):
                    continue
                files.append((path, kind))
    return files


def needs_update(source: Path, output: Path) -> bool:
    if not output.exists():
        return True
    return output.stat().st_mtime < source.stat().st_mtime


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
    if not force and not needs_update(source, output):
        return {
            "path": rel,
            "output": output.relative_to(base_dir).as_posix().lower(),
            "status": "current",
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
    if kind == "voice":
        ffmpeg_args.extend(["-ac", "1"])
    elif kind == "music":
        ffmpeg_args.extend(["-ac", "2"])
    ffmpeg_args.extend(["-acodec", "pcm_s16le", str(temp_pcm)])
    run_checked(ffmpeg_args, f"ffmpeg decode {source}")

    run_checked(
        [str(encoder), str(temp_pcm), str(temp_adpcm), "/C", "/Ob"],
        f"xbadpcmencode {source}",
    )
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
    include_voice: bool,
    all_voice: bool,
    voice_filters: tuple[str, ...],
    force: bool,
) -> dict[str, object]:
    mp3s = collect_mp3s(base_dir, include_music, include_voice, all_voice, voice_filters)
    records: list[dict[str, object]] = []
    converted = 0
    current = 0
    source_bytes = 0
    output_bytes = 0

    with tempfile.TemporaryDirectory(prefix="stefx_xaudio_") as temp_name:
        temp_dir = Path(temp_name)
        for source, kind in mp3s:
            record = convert_one(source, kind, base_dir, temp_dir, ffmpeg, encoder, force)
            records.append(record)
            source_bytes += int(record["sourceBytes"])
            output_bytes += int(record["bytes"])
            if record["status"] == "converted":
                converted += 1
            else:
                current += 1

    manifest = {
        "format": "stefx-xbox-audio-assets-v1",
        "encoding": "xbox-adpcm-wav",
        "source": "mp3",
        "allVoice": all_voice,
        "voiceFilters": list(voice_filters),
        "records": len(records),
        "converted": converted,
        "current": current,
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
    parser.add_argument("--skip-voice", action="store_true")
    parser.add_argument("--all-voice", action="store_true")
    parser.add_argument(
        "--voice-filter",
        action="append",
        default=["/borg1/"],
        help="Lowercase substring filter for voice paths when --all-voice is not set.",
    )
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
        not args.skip_voice,
        args.all_voice,
        tuple(item.lower().replace("\\", "/") for item in args.voice_filter),
        args.force,
    )
    print(json.dumps(manifest, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
