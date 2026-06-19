#!/usr/bin/env python3
"""Build the Elite Force Xbox patch PK3.

The pack intentionally uses normal PK3/ZIP storage. Runtime code can choose
Xbox-specific alternates such as maps/xbox/<map>.bsp and <texture>.dds, but
those overrides are opt-in so unproven conversions cannot perturb gameplay.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import warnings
import zipfile
from pathlib import Path

warnings.filterwarnings(
    "ignore",
    category=DeprecationWarning,
    message=r"Image\.Image\.getdata is deprecated.*",
)

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover - this is an operator error path
    raise SystemExit("Pillow is required to build XBOX0.PK3") from exc


EF_BSP_IDENT = b"IBSP"
EF_BSP_VERSION = 46
EF_LUMP_SHADERS = 1
EF_HEADER_LUMPS = 17
EF_SHADER_ENTRY_SIZE = 72
MAX_QPATH = 64

IMAGE_EXTS = (".jpg", ".jpeg", ".tga", ".png")
DIRECTORY_TEXTURE_SEEDS = (
    "textures/borg",
    "textures/detail",
)
PLAYER_TEXTURE_SEEDS = (
    "models/players/avatar",
    "models/players/biessman",
    "models/players/borgbig",
    "models/players/borgbig2",
    "models/players/borgbig3",
    "models/players/borgbig4",
    "models/players/borgthin",
    "models/players/borgthin2",
    "models/players/borgthin3",
    "models/players/borgthin4",
    "models/players/crewthin",
    "models/players/hazard",
    "models/players/hazardfemale",
    "models/players/munro",
    "models/players/tuvok",
    "models/players/tuvok_h",
)
HUD_TEXTURE_SEEDS = (
    "gfx/2d",
    "gfx/hud",
)
ALWAYS_TEXTURES = ()
FULLSCREEN_TEXTURE_SEEDS = (
    "textures/common/70yearjourney",
    "textures/common/enemyspace",
    "textures/common/sevenspace",
    "textures/common/tuvokhazard",
)
ORIGINAL_FORMAT_TEXTURES = (
    # These are script/cgame-owned intro assets. Until their Xbox-native
    # conversion has visual proof, do not let xbox0.pk3 override the original
    # JPG/TGA path that Elite Force scripts already drive correctly.
    "gfx/2d/chars_big",
    "gfx/2d/charsgrid_med",
    "gfx/2d/chars_medium",
    "gfx/2d/chars_tiny",
    # Dark/detail-heavy sky backing loses too much signal in the current DXT1
    # conversion, so keep the stock JPG until the Xbox-native path is proven.
    "textures/borg/borgsky",
    "textures/common/70yearjourney",
    "textures/common/enemyspace",
    "textures/common/sevenspace",
    "textures/common/tuvokhazard",
)

REFERENCE_RE = re.compile(r"\b(qer_editorimage|map|clampmap|animmap)\s+(.+)", re.IGNORECASE)
FIXED_ZIP_TIME = (2026, 1, 1, 0, 0, 0)


def normalized_rel(path: str) -> str:
    return path.replace("\\", "/").strip().lower()


def strip_line_comment(line: str) -> str:
    return line.split("//", 1)[0].strip()


def read_bsp_shader_names(bsp_path: Path) -> list[str]:
    data = bsp_path.read_bytes()
    if len(data) < 8 + EF_HEADER_LUMPS * 8:
        raise ValueError(f"{bsp_path} is too small to be an EF BSP")

    ident, version = struct.unpack_from("<4sI", data, 0)
    if ident != EF_BSP_IDENT or version != EF_BSP_VERSION:
        raise ValueError(f"{bsp_path} is not an Elite Force IBSP v46 map")

    lump_ofs = 8 + EF_LUMP_SHADERS * 8
    shader_ofs, shader_len = struct.unpack_from("<II", data, lump_ofs)
    if shader_ofs < 0 or shader_len < 0 or shader_ofs + shader_len > len(data):
        raise ValueError(f"{bsp_path} has an invalid shader lump")
    if shader_len % EF_SHADER_ENTRY_SIZE:
        raise ValueError(f"{bsp_path} shader lump is not dshader_t aligned")

    names: set[str] = set()
    for offset in range(shader_ofs, shader_ofs + shader_len, EF_SHADER_ENTRY_SIZE):
        raw = data[offset : offset + MAX_QPATH].split(b"\0", 1)[0]
        name = normalized_rel(raw.decode("latin1"))
        if name and name != "noshader":
            names.add(name)
    return sorted(names)


def parse_shader_references(base_dir: Path, shader_names: set[str]) -> set[str]:
    refs: set[str] = set()
    scripts_dir = base_dir / "scripts"
    if not scripts_dir.exists():
        return refs

    for shader_file in sorted(scripts_dir.glob("*.shader")):
        pending_header: str | None = None
        current_header: str | None = None
        active = False
        depth = 0

        for raw_line in shader_file.read_text(errors="ignore").splitlines():
            line = strip_line_comment(raw_line)
            if not line:
                continue

            if depth == 0:
                if "{" not in line:
                    pending_header = normalized_rel(line.split()[0])
                    continue

                before_brace = line.split("{", 1)[0].strip()
                current_header = normalized_rel(before_brace.split()[0]) if before_brace else pending_header
                active = bool(current_header and current_header in shader_names)
                pending_header = None

            if active:
                refs.update(extract_texture_references(line))

            depth += line.count("{")
            depth -= line.count("}")
            if depth <= 0:
                depth = 0
                current_header = None
                active = False

    return refs


def extract_texture_references(line: str) -> set[str]:
    match = REFERENCE_RE.search(line)
    if not match:
        return set()

    keyword = match.group(1).lower()
    tokens = [token.strip('"') for token in match.group(2).split()]
    if keyword == "animmap":
        tokens = tokens[1:]
    else:
        tokens = tokens[:1]

    refs: set[str] = set()
    for token in tokens:
        token = normalized_rel(token)
        if not token.startswith("textures/"):
            continue
        if token.startswith("textures/common/") and not is_always_texture(token):
            continue
        refs.add(token)
    return refs


def is_always_texture(candidate: str) -> bool:
    candidate = normalized_rel(candidate)
    path = Path(*candidate.split("/"))
    if path.suffix.lower() in IMAGE_EXTS:
        candidate = normalized_rel(path.with_suffix("").as_posix())
    return candidate in ALWAYS_TEXTURES


def is_fullscreen_texture(candidate: str) -> bool:
    candidate = normalized_rel(candidate)
    path = Path(*candidate.split("/"))
    if path.suffix.lower() in IMAGE_EXTS or path.suffix.lower() == ".dds":
        candidate = normalized_rel(path.with_suffix("").as_posix())
    return candidate in FULLSCREEN_TEXTURE_SEEDS


def should_preserve_original_texture(candidate: str) -> bool:
    candidate = normalized_rel(candidate)
    path = Path(*candidate.split("/"))
    if path.suffix.lower() in IMAGE_EXTS or path.suffix.lower() == ".dds":
        candidate = normalized_rel(path.with_suffix("").as_posix())
    return candidate in ORIGINAL_FORMAT_TEXTURES


def directory_texture_candidates(base_dir: Path, rel_dirs: tuple[str, ...]) -> set[str]:
    candidates: set[str] = set()
    for rel_dir in rel_dirs:
        root = base_dir / rel_dir
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix.lower() in IMAGE_EXTS:
                candidates.add(normalized_rel(path.relative_to(base_dir).as_posix()))
    return candidates


def all_image_candidates(base_dir: Path) -> set[str]:
    candidates: set[str] = set()
    for path in base_dir.rglob("*"):
        if path.is_file() and path.suffix.lower() in IMAGE_EXTS:
            rel = normalized_rel(path.relative_to(base_dir).as_posix())
            candidates.add(rel)
    return candidates


def resolve_texture_source(base_dir: Path, candidate: str, allow_all: bool = False) -> tuple[Path, str] | None:
    candidate = normalized_rel(candidate)
    if allow_all:
        allowed = True
    else:
        allowed = (
            candidate.startswith("textures/")
            or candidate.startswith("models/players/")
            or candidate.startswith("gfx/")
        )
    if not allowed:
        return None
    if not allow_all and candidate.startswith("textures/common/") and not is_always_texture(candidate):
        return None
    rel_path = Path(*candidate.split("/"))
    if rel_path.suffix.lower() in IMAGE_EXTS:
        exact = base_dir / rel_path
        if exact.exists():
            return exact, rel_path.with_suffix(".dds").as_posix().lower()
        rel_path = rel_path.with_suffix("")

    for ext in IMAGE_EXTS:
        source = base_dir / rel_path.with_suffix(ext)
        if source.exists():
            return source, rel_path.with_suffix(".dds").as_posix().lower()
    return None


def texture_size_for_path(out_rel: str, args: argparse.Namespace) -> int:
    out_rel = normalized_rel(out_rel)
    if out_rel.startswith("models/players/"):
        return args.max_player_texture_size
    if out_rel.startswith("gfx/"):
        return args.max_hud_texture_size
    if is_always_texture(out_rel) or is_fullscreen_texture(out_rel):
        return args.max_loadscreen_texture_size
    return args.max_texture_size


def next_power_of_two(value: int) -> int:
    out = 1
    while out < value:
        out <<= 1
    return out


def xbox_dimension(value: int, max_size: int) -> int:
    return max(1, min(max_size, next_power_of_two(value)))


def image_has_alpha(image: Image.Image) -> bool:
    if image.mode in ("RGBA", "LA"):
        alpha = image.getchannel("A")
        return alpha.getextrema()[0] < 250
    if image.mode == "P" and "transparency" in image.info:
        converted = image.convert("RGBA")
        alpha = converted.getchannel("A")
        return alpha.getextrema()[0] < 250
    return False


def resize_for_xbox(image: Image.Image, max_size: int) -> Image.Image:
    target = (
        xbox_dimension(image.width, max_size),
        xbox_dimension(image.height, max_size),
    )
    if target == image.size:
        return image
    resample = getattr(Image, "Resampling", Image).LANCZOS
    return image.resize(target, resample)


def dds_header(width: int, height: int, pitch: int, rgb_bits: int, masks: tuple[int, int, int, int], alpha: bool) -> bytes:
    DDSD_CAPS = 0x00000001
    DDSD_HEIGHT = 0x00000002
    DDSD_WIDTH = 0x00000004
    DDSD_PITCH = 0x00000008
    DDSD_PIXELFORMAT = 0x00001000
    DDPF_ALPHAPIXELS = 0x00000001
    DDPF_RGB = 0x00000040
    DDSCAPS_TEXTURE = 0x00001000

    pf_flags = DDPF_RGB | (DDPF_ALPHAPIXELS if alpha else 0)
    fields = [
        124,
        DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT,
        height,
        width,
        pitch,
        0,
        1,
        *([0] * 11),
        32,
        pf_flags,
        0,
        rgb_bits,
        masks[0],
        masks[1],
        masks[2],
        masks[3],
        DDSCAPS_TEXTURE,
        0,
        0,
        0,
        0,
    ]
    return b"DDS " + struct.pack("<31I", *fields)


def fourcc(value: bytes) -> int:
    return struct.unpack("<I", value)[0]


def dds_dxt1_header(width: int, height: int, linear_size: int) -> bytes:
    DDSD_CAPS = 0x00000001
    DDSD_HEIGHT = 0x00000002
    DDSD_WIDTH = 0x00000004
    DDSD_PIXELFORMAT = 0x00001000
    DDSD_LINEARSIZE = 0x00080000
    DDPF_FOURCC = 0x00000004
    DDSCAPS_TEXTURE = 0x00001000

    fields = [
        124,
        DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_LINEARSIZE,
        height,
        width,
        linear_size,
        0,
        1,
        *([0] * 11),
        32,
        DDPF_FOURCC,
        fourcc(b"DXT1"),
        0,
        0,
        0,
        0,
        0,
        DDSCAPS_TEXTURE,
        0,
        0,
        0,
        0,
    ]
    return b"DDS " + struct.pack("<31I", *fields)


def dds_dxt5_header(width: int, height: int, linear_size: int) -> bytes:
    DDSD_CAPS = 0x00000001
    DDSD_HEIGHT = 0x00000002
    DDSD_WIDTH = 0x00000004
    DDSD_PIXELFORMAT = 0x00001000
    DDSD_LINEARSIZE = 0x00080000
    DDPF_FOURCC = 0x00000004
    DDSCAPS_TEXTURE = 0x00001000

    fields = [
        124,
        DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_LINEARSIZE,
        height,
        width,
        linear_size,
        0,
        1,
        *([0] * 11),
        32,
        DDPF_FOURCC,
        fourcc(b"DXT5"),
        0,
        0,
        0,
        0,
        0,
        DDSCAPS_TEXTURE,
        0,
        0,
        0,
        0,
    ]
    return b"DDS " + struct.pack("<31I", *fields)


def dds_bgra32_header(width: int, height: int, pitch: int) -> bytes:
    DDSD_CAPS = 0x00000001
    DDSD_HEIGHT = 0x00000002
    DDSD_WIDTH = 0x00000004
    DDSD_PITCH = 0x00000008
    DDSD_PIXELFORMAT = 0x00001000
    DDPF_ALPHAPIXELS = 0x00000001
    DDPF_RGB = 0x00000040
    DDSCAPS_TEXTURE = 0x00001000

    fields = [
        124,
        DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT,
        height,
        width,
        pitch,
        0,
        1,
        *([0] * 11),
        32,
        DDPF_RGB | DDPF_ALPHAPIXELS,
        0,
        32,
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000,
        DDSCAPS_TEXTURE,
        0,
        0,
        0,
        0,
    ]
    return b"DDS " + struct.pack("<31I", *fields)


def rgb_to_565(color: tuple[int, int, int]) -> int:
    r, g, b = color
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def rgb_from_565(value: int) -> tuple[int, int, int]:
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def color_distance_sq(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    return (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])


def encode_dxt1(image: Image.Image) -> bytes:
    rgb = image.convert("RGB")
    pixels = list(rgb.getdata())
    payload = bytearray()

    for by in range(0, rgb.height, 4):
        for bx in range(0, rgb.width, 4):
            block: list[tuple[int, int, int]] = []
            for y in range(4):
                sy = min(by + y, rgb.height - 1)
                for x in range(4):
                    sx = min(bx + x, rgb.width - 1)
                    block.append(pixels[sy * rgb.width + sx])

            darkest = min(block, key=lambda c: c[0] * 3 + c[1] * 6 + c[2])
            brightest = max(block, key=lambda c: c[0] * 3 + c[1] * 6 + c[2])
            c0 = rgb_to_565(brightest)
            c1 = rgb_to_565(darkest)
            if c0 == c1:
                c0 = rgb_to_565(brightest)
                c1 = max(0, c0 - 1)
            elif c0 < c1:
                c0, c1 = c1, c0

            p0 = rgb_from_565(c0)
            p1 = rgb_from_565(c1)
            palette = (
                p0,
                p1,
                (
                    (2 * p0[0] + p1[0]) // 3,
                    (2 * p0[1] + p1[1]) // 3,
                    (2 * p0[2] + p1[2]) // 3,
                ),
                (
                    (p0[0] + 2 * p1[0]) // 3,
                    (p0[1] + 2 * p1[1]) // 3,
                    (p0[2] + 2 * p1[2]) // 3,
                ),
            )

            indices = 0
            for i, color in enumerate(block):
                best = min(range(4), key=lambda idx: color_distance_sq(color, palette[idx]))
                indices |= best << (2 * i)

            payload.extend(struct.pack("<HHI", c0, c1, indices))

    return bytes(payload)


def encode_dxt5(image: Image.Image) -> bytes:
    rgba = image.convert("RGBA")
    pixels = list(rgba.getdata())
    payload = bytearray()

    for by in range(0, rgba.height, 4):
        for bx in range(0, rgba.width, 4):
            block: list[tuple[int, int, int, int]] = []
            for y in range(4):
                sy = min(by + y, rgba.height - 1)
                for x in range(4):
                    sx = min(bx + x, rgba.width - 1)
                    block.append(pixels[sy * rgba.width + sx])

            alphas = [px[3] for px in block]
            a0 = max(alphas)
            a1 = min(alphas)
            alpha_palette = [
                a0,
                a1,
                (6 * a0 + 1 * a1) // 7,
                (5 * a0 + 2 * a1) // 7,
                (4 * a0 + 3 * a1) // 7,
                (3 * a0 + 4 * a1) // 7,
                (2 * a0 + 5 * a1) // 7,
                (1 * a0 + 6 * a1) // 7,
            ]
            alpha_indices = 0
            for i, alpha in enumerate(alphas):
                best = min(range(8), key=lambda idx: abs(alpha - alpha_palette[idx]))
                alpha_indices |= best << (3 * i)

            payload.extend(struct.pack("<BB", a0, a1))
            payload.extend(alpha_indices.to_bytes(6, "little"))

            rgb_block = [(r, g, b) for r, g, b, _a in block]
            darkest = min(rgb_block, key=lambda c: c[0] * 3 + c[1] * 6 + c[2])
            brightest = max(rgb_block, key=lambda c: c[0] * 3 + c[1] * 6 + c[2])
            c0 = rgb_to_565(brightest)
            c1 = rgb_to_565(darkest)
            if c0 == c1:
                c1 = max(0, c0 - 1)
            elif c0 < c1:
                c0, c1 = c1, c0

            p0 = rgb_from_565(c0)
            p1 = rgb_from_565(c1)
            palette = (
                p0,
                p1,
                (
                    (2 * p0[0] + p1[0]) // 3,
                    (2 * p0[1] + p1[1]) // 3,
                    (2 * p0[2] + p1[2]) // 3,
                ),
                (
                    (p0[0] + 2 * p1[0]) // 3,
                    (p0[1] + 2 * p1[1]) // 3,
                    (p0[2] + 2 * p1[2]) // 3,
                ),
            )

            color_indices = 0
            for i, color in enumerate(rgb_block):
                best = min(range(4), key=lambda idx: color_distance_sq(color, palette[idx]))
                color_indices |= best << (2 * i)

            payload.extend(struct.pack("<HHI", c0, c1, color_indices))

    return bytes(payload)


def encode_rgb565(image: Image.Image) -> bytes:
    image = image.convert("RGB")
    payload = bytearray(image.width * image.height * 2)
    pos = 0
    for r, g, b in image.getdata():
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        payload[pos] = value & 0xFF
        payload[pos + 1] = (value >> 8) & 0xFF
        pos += 2
    return bytes(payload)


def encode_bgra32(image: Image.Image) -> bytes:
    image = image.convert("RGBA")
    payload = bytearray(image.width * image.height * 4)
    pos = 0
    for r, g, b, a in image.getdata():
        payload[pos] = b
        payload[pos + 1] = g
        payload[pos + 2] = r
        payload[pos + 3] = a
        pos += 4
    return bytes(payload)


def build_dds(source: Path, max_size: int, force_bgra32: bool = False) -> tuple[bytes, dict[str, object]] | None:
    with Image.open(source) as opened:
        has_alpha = image_has_alpha(opened)
        image = resize_for_xbox(opened, max_size)
        if force_bgra32:
            payload = encode_bgra32(image)
            header = dds_bgra32_header(image.width, image.height, image.width * 4)
            fmt = "bgra32"
        elif has_alpha:
            payload = encode_dxt5(image)
            header = dds_dxt5_header(image.width, image.height, len(payload))
            fmt = "dxt5"
        else:
            payload = encode_dxt1(image)
            header = dds_dxt1_header(image.width, image.height, len(payload))
            fmt = "dxt1"

        info = {
            "source": source.as_posix(),
            "format": fmt,
            "sourceWidth": opened.width,
            "sourceHeight": opened.height,
            "width": image.width,
            "height": image.height,
            "bytes": len(header) + len(payload),
        }
        return header + payload, info


def zip_write_bytes(zip_out: zipfile.ZipFile, rel: str, data: bytes) -> None:
    info = zipfile.ZipInfo(normalized_rel(rel), FIXED_ZIP_TIME)
    info.compress_type = zipfile.ZIP_STORED
    info.external_attr = 0o644 << 16
    zip_out.writestr(info, data)


def build_patch(args: argparse.Namespace) -> dict[str, object]:
    base_dir = args.base_dir.resolve()
    map_path = base_dir / "maps" / f"{args.map}.bsp"
    out_path = args.output.resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)

    shader_names = set(read_bsp_shader_names(map_path))
    resolved: dict[str, Path] = {}
    skipped: list[str] = []
    if args.texture_mode != "none":
        candidates = set(shader_names)
        candidates.update(parse_shader_references(base_dir, shader_names))
        candidates.update(directory_texture_candidates(base_dir, DIRECTORY_TEXTURE_SEEDS))
        candidates.update(directory_texture_candidates(base_dir, PLAYER_TEXTURE_SEEDS))
        candidates.update(directory_texture_candidates(base_dir, HUD_TEXTURE_SEEDS))
        candidates.update(ALWAYS_TEXTURES)
        candidates.update(FULLSCREEN_TEXTURE_SEEDS)

        if args.texture_mode == "all":
            candidates.update(all_image_candidates(base_dir))

        for candidate in sorted(candidates):
            found = resolve_texture_source(base_dir, candidate, args.texture_mode == "all")
            if found:
                source, out_rel = found
                resolved[out_rel] = source
            elif candidate.startswith("textures/") and not candidate.startswith("textures/common/"):
                skipped.append(candidate)

    textures: list[dict[str, object]] = []
    skipped_alpha: list[str] = []
    preserved_original: list[str] = []
    with zipfile.ZipFile(out_path, "w") as zip_out:
        bsp_rel: str | None = None
        if args.include_bsp:
            bsp_rel = f"maps/xbox/{args.map}.bsp"
            zip_write_bytes(zip_out, bsp_rel, map_path.read_bytes())

        for out_rel, source in sorted(resolved.items()):
            if should_preserve_original_texture(out_rel):
                preserved_original.append(out_rel)
                continue
            max_size = texture_size_for_path(out_rel, args)
            built = build_dds(source, max_size, force_bgra32=is_fullscreen_texture(out_rel))
            if built is None:
                skipped_alpha.append(out_rel)
                continue
            dds, info = built
            zip_write_bytes(zip_out, out_rel, dds)
            info["path"] = out_rel
            info["source"] = normalized_rel(source.relative_to(base_dir).as_posix())
            info["maxTextureSize"] = max_size
            textures.append(info)

        manifest = {
            "name": "Star Trek: Elite Force Xbox patch pack",
            "map": args.map,
            "bsp": bsp_rel,
            "bspIncluded": bool(bsp_rel),
            "maxTextureSize": args.max_texture_size,
            "maxPlayerTextureSize": args.max_player_texture_size,
            "maxHudTextureSize": args.max_hud_texture_size,
            "maxLoadscreenTextureSize": args.max_loadscreen_texture_size,
            "textureMode": args.texture_mode,
            "textureCount": len(textures),
            "textures": textures,
            "skippedTextureCandidates": skipped,
            "skippedAlphaTextures": skipped_alpha,
            "preservedOriginalTextures": preserved_original,
        }
        zip_write_bytes(
            zip_out,
            "xbox_patch_manifest.json",
            json.dumps(manifest, indent=2, sort_keys=True).encode("ascii"),
        )

    return {
        "output": str(out_path),
        "bytes": out_path.stat().st_size,
        "map": args.map,
        "bspIncluded": args.include_bsp,
        "shaderNames": len(shader_names),
        "textureMode": args.texture_mode,
        "textures": len(textures),
        "skipped": len(skipped),
        "skippedAlpha": len(skipped_alpha),
        "preservedOriginal": len(preserved_original),
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build an Elite Force Xbox patch PK3")
    parser.add_argument("--base-dir", type=Path, default=Path("build/release/BaseEF"))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build/release/BaseEF/xbox0.pk3"),
        help="Patch PK3 output.",
    )
    parser.add_argument("--map", default="borg1")
    parser.add_argument("--max-texture-size", type=int, default=128)
    parser.add_argument("--max-player-texture-size", type=int, default=64)
    parser.add_argument("--max-hud-texture-size", type=int, default=128)
    parser.add_argument("--max-loadscreen-texture-size", type=int, default=512)
    parser.add_argument(
        "--include-bsp",
        action="store_true",
        help="Pack maps/xbox/<map>.bsp so the runtime BSP override becomes active.",
    )
    parser.add_argument(
        "--texture-mode",
        choices=("none", "borg1", "all"),
        default="none",
        help=(
            "DDS texture substitution mode. Disabled by default because the "
            "current mode only converts opaque textures to DXT1; alpha textures stay loose."
        ),
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    summary = build_patch(parse_args(argv))
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
