#!/usr/bin/env python3
"""Build the simple Raven/VV Xbox WAV soundbank used by win_stream_dx8.cpp."""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import os
import zlib
from pathlib import Path


def normalized_qpath(path: Path) -> str:
    return path.as_posix().lower()


def sound_crc(qpath: str) -> int:
    os_path = ("d:\\BaseEF\\" + qpath.replace("/", "\\")).lower()
    return zlib.crc32(os_path.encode("ascii")) & 0xFFFFFFFF


def collect_wavs(base_dir: Path) -> list[Path]:
    sound_dir = base_dir / "sound"
    if not sound_dir.exists():
        return []
    return sorted(path for path in sound_dir.rglob("*.wav") if path.is_file())


def read_wave_info(path: Path) -> dict[str, int] | None:
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return None

    info: dict[str, int] = {}
    pos = 12
    while pos + 8 <= len(data):
        chunk = data[pos : pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        pos += 8
        if chunk == b"fmt " and size >= 16 and pos + 16 <= len(data):
            (
                info["formatTag"],
                info["channels"],
                info["samplesPerSec"],
                info["avgBytesPerSec"],
                info["blockAlign"],
                info["bitsPerSample"],
            ) = struct.unpack_from("<HHIIHH", data, pos)
        elif chunk == b"data":
            info["dataBytes"] = size
        pos += size + (size & 1)

    return info if "formatTag" in info else None


def read_wave_format_tag(path: Path) -> int | None:
    info = read_wave_info(path)
    return info["formatTag"] if info else None


def find_default_encoder() -> Path | None:
    for candidate in (
        Path(r"C:\XDK\xbox\bin\xbadpcmencode.exe"),
        Path(r"C:\XDK\bin\xbadpcmencode.exe"),
    ):
        if candidate.exists():
            return candidate
    found = shutil.which("xbadpcmencode.exe")
    return Path(found) if found else None


def encode_xbadpcm(source: Path, out_path: Path, encoder: Path) -> Path:
    out_path.parent.mkdir(parents=True, exist_ok=True)

    info = read_wave_info(source)
    if not info:
        shutil.copy2(source, out_path)
        return out_path
    if info["formatTag"] == 0x0069:
        shutil.copy2(source, out_path)
        return out_path
    if info["formatTag"] != 1 or info["bitsPerSample"] != 16:
        shutil.copy2(source, out_path)
        return out_path

    result = subprocess.run(
        [str(encoder), str(source), str(out_path), "/C", "/Ob"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"xbadpcmencode failed for {source} with exit {result.returncode}:\n{result.stdout}"
        )

    if not out_path.exists():
        shutil.copy2(source, out_path)
        return out_path
    if read_wave_format_tag(out_path) != 0x0069:
        raise RuntimeError(f"xbadpcmencode did not produce Xbox ADPCM WAV: {out_path}")
    return out_path


def build_soundbank(
    base_dir: Path,
    encoding: str,
    encoder: Path | None,
    preserve_pcm_prefixes: tuple[str, ...],
) -> dict[str, object]:
    out_dir = base_dir / "soundbank"
    out_dir.mkdir(parents=True, exist_ok=True)
    bank_path = out_dir / "sound.bnk"
    table_path = out_dir / "sound.tbl"
    manifest_path = out_dir / "soundbank_manifest.json"

    records: list[tuple[int, int, int, int, str]] = []
    offset = 0
    original_bytes = 0
    encoded_count = 0
    preserved_pcm_count = 0
    wavs = collect_wavs(base_dir)

    if encoding == "xbadpcm":
        if encoder is None:
            encoder = find_default_encoder()
        if encoder is None or not encoder.exists():
            raise RuntimeError("xbadpcm encoding requested, but xbadpcmencode.exe was not found")

    temp_root = base_dir.parent / "tmp" / "soundbank"
    temp_root.mkdir(parents=True, exist_ok=True)
    for stale in temp_root.glob("stefx_xbadpcm_*"):
        try:
            if stale.is_dir():
                shutil.rmtree(stale, ignore_errors=True)
            else:
                stale.unlink()
        except OSError:
            pass

    with bank_path.open("wb") as bank:
        encoded_root = temp_root / ("stefx_xbadpcm_%d" % os.getpid())
        shutil.rmtree(encoded_root, ignore_errors=True)
        encoded_root.mkdir(parents=True, exist_ok=True)
        temp_name = str(encoded_root)
        try:
            for source in wavs:
                qpath = normalized_qpath(source.relative_to(base_dir))
                original_bytes += source.stat().st_size
                bank_source = source
                preserve_pcm = any(qpath.startswith(prefix) for prefix in preserve_pcm_prefixes)
                if encoding == "xbadpcm" and not preserve_pcm:
                    bank_source = encode_xbadpcm(source, encoded_root / ("%06d_%s" % (len(records), source.name)), encoder)
                if read_wave_format_tag(bank_source) == 0x0069:
                    encoded_count += 1
                else:
                    preserved_pcm_count += 1

                data = bank_source.read_bytes()
                code = sound_crc(qpath)
                records.append((code, offset, len(data), 0, qpath))
                bank.write(data)
                offset += len(data)
        finally:
            shutil.rmtree(temp_name, ignore_errors=True)
    records.sort(key=lambda item: item[0])
    with table_path.open("wb") as table:
        for code, sound_offset, size, flags, _qpath in records:
            table.write(struct.pack("<IIIb", code, sound_offset, size, flags))

    manifest = {
        "format": "stefx-wav-bank-v2",
        "encoding": encoding,
        "bank": "soundbank/sound.bnk",
        "table": "soundbank/sound.tbl",
        "records": len(records),
        "sourceBytes": original_bytes,
        "bytes": bank_path.stat().st_size if bank_path.exists() else 0,
        "encodedRecords": encoded_count,
        "preservedPcmRecords": preserved_pcm_count,
        "preservedPcmPrefixes": list(preserve_pcm_prefixes),
        "encoder": str(encoder) if encoder else None,
        "sounds": [
            {
                "path": qpath,
                "crc": f"0x{code:08x}",
                "offset": sound_offset,
                "size": size,
                "flags": flags,
            }
            for code, sound_offset, size, flags, qpath in records
        ],
    }
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="ascii")

    return {
        "records": len(records),
        "encoding": encoding,
        "sourceBytes": original_bytes,
        "bytes": manifest["bytes"],
        "encodedRecords": encoded_count,
        "preservedPcmRecords": preserved_pcm_count,
        "bank": str(bank_path),
        "table": str(table_path),
        "manifest": str(manifest_path),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build BaseEF/soundbank/sound.bnk and sound.tbl")
    parser.add_argument("--base-dir", type=Path, default=Path("build/release/BaseEF"))
    parser.add_argument("--encoding", choices=("pcm", "xbadpcm"), default="xbadpcm")
    parser.add_argument("--encoder", type=Path, default=None)
    parser.add_argument(
        "--preserve-pcm-prefix",
        action="append",
        default=[],
        help="Lowercase sound qpath prefix whose PCM WAVs must not be ADPCM encoded.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    print(
        json.dumps(
            build_soundbank(
                args.base_dir.resolve(),
                args.encoding,
                args.encoder,
                tuple(item.lower().replace("\\", "/") for item in args.preserve_pcm_prefix),
            ),
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())





