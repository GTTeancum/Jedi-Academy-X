#!/usr/bin/env python3
"""Create Xbox-native WAV alternates for MP3 music and sound assets."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import os
import struct
from pathlib import Path
from zipfile import BadZipFile, ZipFile


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


def materialize_archived_mp3s(
    base_dir: Path,
    include_music: bool,
    include_sound: bool,
    all_sound: bool,
    sound_filters: tuple[str, ...],
) -> list[str]:
    """Expose archive-only MP3s so the Xbox worker and soundbank can use WAV siblings.

    Runtime soundbank lookup and the QAL music worker cannot safely stream an MP3
    from inside a PK3.  Higher-numbered retail PAKs win, while an existing loose
    file keeps normal filesystem override precedence.
    """
    materialized: list[str] = []
    archives = sorted(
        (path for path in base_dir.glob("*.pk3") if path.stem.lower().startswith("pak")),
        key=lambda path: path.name.lower(),
        reverse=True,
    )
    claimed: set[str] = set()

    for archive in archives:
        try:
            with ZipFile(archive) as package:
                for info in package.infolist():
                    rel = info.filename.replace("\\", "/").lower().lstrip("/")
                    if not rel.endswith(".mp3") or rel in claimed:
                        continue
                    if ".." in Path(rel).parts:
                        continue

                    selected = (
                        (include_music and rel.startswith("music/"))
                        or (
                            include_sound
                            and rel.startswith("sound/")
                            and (all_sound or any(token in rel for token in sound_filters))
                        )
                    )
                    if not selected:
                        continue

                    claimed.add(rel)
                    destination = base_dir / Path(rel)
                    if destination.exists():
                        continue
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    with package.open(info) as source, destination.open("wb") as output:
                        shutil.copyfileobj(source, output)
                    materialized.append(rel)
        except (BadZipFile, OSError) as exc:
            raise RuntimeError(f"could not read archived audio from {archive}: {exc}") from exc

    return materialized


def needs_update(source: Path, output: Path) -> bool:
    if not output.exists():
        return True
    return output.stat().st_mtime < source.stat().st_mtime


def files_identical(left: Path, right: Path) -> bool:
    if left.stat().st_size != right.stat().st_size:
        return False
    with left.open("rb") as left_file, right.open("rb") as right_file:
        while True:
            left_chunk = left_file.read(1024 * 1024)
            right_chunk = right_file.read(1024 * 1024)
            if left_chunk != right_chunk:
                return False
            if not left_chunk:
                return True


def read_wave_format(path: Path) -> tuple[int, int] | None:
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
                    format_tag = struct.unpack_from("<H", format_data)[0]
                    sample_rate = struct.unpack_from("<I", format_data, 4)[0]
                    return format_tag, sample_rate
                wave.seek(padded_size, 1)
                remaining -= padded_size
    except OSError:
        return None
    return None


def read_wave_format_tag(path: Path) -> int | None:
    wave_format = read_wave_format(path)
    return wave_format[0] if wave_format is not None else None


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
    stock_wav_dirs: tuple[Path, ...],
) -> dict[str, object]:
    rel = source.relative_to(base_dir).as_posix().lower()
    output = source.with_suffix(".wav")
    output_rel = output.relative_to(base_dir)
    stock_wave = next(
        (
            root / output_rel
            for root in stock_wav_dirs
            if (root / output_rel).is_file()
        ),
        None,
    )

    stock_format = read_wave_format(stock_wave) if stock_wave is not None else None
    if not force and output.exists():
        output_format = read_wave_format(output)
        output_tag = output_format[0] if output_format is not None else None
        if output_tag == 0x0069 and (
            (stock_wave is None)
            or (
                stock_format is not None
                and stock_format[0] == 0x0069
                and files_identical(output, stock_wave)
            )
        ):
            return {
                "path": rel,
                "output": output.relative_to(base_dir).as_posix().lower(),
                "status": "current",
                "encoding": "xbox-adpcm",
                "sourceKind": "stock-xbadpcm-wav" if stock_wave is not None else "mp3",
                "sourceBytes": (stock_wave or source).stat().st_size,
                "bytes": output.stat().st_size,
            }

    temp_pcm = temp_dir / (source.stem + ".pcm.wav")
    temp_adpcm = temp_dir / (source.stem + ".xadpcm.wav")
    output.parent.mkdir(parents=True, exist_ok=True)

    if stock_wave is not None and stock_format is not None and stock_format[0] == 0x0069:
        shutil.copy2(stock_wave, output)
        source_kind = "stock-xbadpcm-wav"
    else:
        if stock_wave is not None and stock_format is not None and stock_format[0] == 0x0001:
            run_checked(
                [
                    str(ffmpeg),
                    "-y",
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-i",
                    str(stock_wave),
                    "-acodec",
                    "pcm_s16le",
                    str(temp_pcm),
                ],
                f"ffmpeg normalize stock PCM {stock_wave}",
            )
            source_kind = "stock-pcm-wav"
        else:
            ffmpeg_args = [
                str(ffmpeg),
                "-y",
                "-hide_banner",
                "-loglevel",
                "error",
                "-i",
                str(source),
            ]
            if kind == "music":
                ffmpeg_args.extend(["-ac", "2"])
            ffmpeg_args.extend(["-acodec", "pcm_s16le", str(temp_pcm)])
            run_checked(ffmpeg_args, f"ffmpeg decode {source}")
            source_kind = "mp3"

        run_checked(
            [str(encoder), str(temp_pcm), str(temp_adpcm), "/C", "/Ob"],
            f"xbadpcmencode {stock_wave or source}",
        )
        if read_wave_format_tag(temp_adpcm) != 0x0069:
            raise RuntimeError(f"xbadpcmencode did not produce Xbox ADPCM WAV: {stock_wave or source}")
        shutil.copy2(temp_adpcm, output)

    return {
        "path": rel,
        "output": output.relative_to(base_dir).as_posix().lower(),
        "status": "converted",
        "encoding": "xbox-adpcm",
        "sourceKind": source_kind,
        "sourceBytes": (stock_wave or source).stat().st_size,
        "bytes": output.stat().st_size,
    }


def convert_pcm_wave_in_place(
    source: Path,
    base_dir: Path,
    temp_dir: Path,
    ffmpeg: Path,
    encoder: Path,
    index: int,
) -> dict[str, object]:
    rel = source.relative_to(base_dir).as_posix().lower()
    source_bytes = source.stat().st_size
    temp_pcm = temp_dir / f"wavonly_{index:05d}.pcm.wav"
    temp_adpcm = temp_dir / f"wavonly_{index:05d}.xadpcm.wav"

    run_checked(
        [
            str(ffmpeg),
            "-y",
            "-hide_banner",
            "-loglevel",
            "error",
            "-i",
            str(source),
            "-acodec",
            "pcm_s16le",
            str(temp_pcm),
        ],
        f"ffmpeg normalize WAV {source}",
    )
    run_checked(
        [str(encoder), str(temp_pcm), str(temp_adpcm), "/C", "/Ob"],
        f"xbadpcmencode {source}",
    )
    if read_wave_format_tag(temp_adpcm) != 0x0069:
        raise RuntimeError(f"xbadpcmencode did not produce Xbox ADPCM WAV: {source}")
    shutil.copy2(temp_adpcm, source)

    return {
        "path": rel,
        "output": rel,
        "status": "converted",
        "encoding": "xbox-adpcm",
        "sourceKind": "wav-only-pcm",
        "sourceBytes": source_bytes,
        "bytes": source.stat().st_size,
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
    stock_wav_dirs: tuple[Path, ...],
) -> dict[str, object]:
    materialized = materialize_archived_mp3s(
        base_dir,
        include_music,
        include_sound,
        all_sound,
        sound_filters,
    )
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
            record = convert_one(
                source,
                kind,
                base_dir,
                temp_dir,
                ffmpeg,
                encoder,
                force,
                stock_wav_dirs,
            )
            records.append(record)
            source_bytes += int(record["sourceBytes"])
            output_bytes += int(record["bytes"])
            if record["status"] == "converted":
                converted += 1
            elif record["status"] == "preserved":
                preserved += 1
            else:
                current += 1

        # The retail data also contains WAV-only effects.  Convert those loose
        # files in place so every runtime path—not only the packed bank—uses
        # Xbox ADPCM.
        remaining_pcm_wavs = sorted(
            path
            for path in (base_dir / "sound").rglob("*.wav")
            if path.is_file() and read_wave_format_tag(path) != 0x0069
        )
        for index, source in enumerate(remaining_pcm_wavs):
            record = convert_pcm_wave_in_place(
                source,
                base_dir,
                temp_dir,
                ffmpeg,
                encoder,
                index,
            )
            records.append(record)
            source_bytes += int(record["sourceBytes"])
            output_bytes += int(record["bytes"])
            converted += 1
    finally:
        shutil.rmtree(temp_name, ignore_errors=True)
    manifest = {
        "format": "stefx-xbox-audio-assets-v1",
        "encoding": "xbox-adpcm-wav",
        "source": "stock-wav-preferred-then-mp3",
        "allSound": all_sound,
        "soundFilters": list(sound_filters),
        "stockWavDirs": [str(path) for path in stock_wav_dirs],
        "archivedMp3SourcesMaterialized": len(materialized),
        "archivedMp3Paths": materialized,
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
    parser.add_argument(
        "--stock-wav-dir",
        action="append",
        type=Path,
        default=[],
        help="Prefer matching stock WAV files from this BaseEF tree over MP3 transcoding.",
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
        not args.skip_sound,
        args.all_sound or args.all_voice,
        tuple(item.lower().replace("\\", "/") for item in (args.sound_filter + args.voice_filter)),
        args.force,
        tuple(path.resolve() for path in args.stock_wav_dir if path.is_dir()),
    )
    print(json.dumps(manifest, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())



