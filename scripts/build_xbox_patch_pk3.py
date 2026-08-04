#!/usr/bin/env python3
"""Build the Elite Force Xbox patch PK3.

The pack intentionally uses normal PK3/ZIP storage. Runtime code can choose
Xbox-specific alternates such as maps/xbox/<map>.bsp and <texture>.dds, but
those overrides are opt-in so unproven conversions cannot perturb gameplay.
"""

from __future__ import annotations

import argparse
import hashlib
import io
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
EF_LUMP_LIGHTMAPS = 14
EF_HEADER_LUMPS = 17
EF_SHADER_ENTRY_SIZE = 72
AAS_IDENT = b"EAAS"
AAS_HEADER_SIZE = 12 + 14 * 8
MAX_QPATH = 64
LIGHTMAP_SIZE = 128
LIGHTMAP_RGB_BYTES = LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3
LIGHTMAP_RGB565_DDS_BYTES = 128 + LIGHTMAP_SIZE * LIGHTMAP_SIZE * 2
DEFAULT_LIGHTMAP_BOOST = 2.5
MULTIPLAYER_MAP_PREFIXES = ("ctf_", "hm_", "dm_", "team_")

IMAGE_EXTS = (".jpg", ".jpeg", ".tga", ".png")
SKYBOX_SUFFIXES = ("rt", "lf", "bk", "ft", "up", "dn")
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
    "models/players2",
)
HUD_TEXTURE_SEEDS = (
    "gfx/2d",
    "gfx/hud",
    "gfx/interface",
)
HIGH_FIDELITY_TEXTURES = (
    # Borg wall panels use thin pipe/detail silhouettes against black backing.
    # A 128px cap makes these read as large black slabs, so keep their source
    # resolution while still packaging them as Xbox DDS overrides.
    "textures/borg/xpanelb",
    # dn1 uses textures/common/junk_sky, whose skyParms reference env/junk.
    # These faces are very dark/detail-heavy; the normal 128px cap loses the
    # junk/star signal and makes the openings look like flat black voids.
    "env/junk_rt",
    "env/junk_lf",
    "env/junk_bk",
    "env/junk_ft",
    "env/junk_up",
    "env/junk_dn",
)
RGB565_TEXTURES = (
    # DXT1 block compression crushes the thin dark/bright traces in these
    # structural wall panels. RGB565 is still an Xbox-native DDS path and keeps
    # the source signal intact enough for the lightmap multiply pass.
    "textures/borg/xpanelb",
    "env/junk_rt",
    "env/junk_lf",
    "env/junk_bk",
    "env/junk_ft",
    "env/junk_up",
    "env/junk_dn",
)
ALWAYS_TEXTURES = (
    # These atlases are registered directly by the shared SP frontend/pause
    # code, so shader-script discovery cannot find them. They require alpha
    # fidelity and are therefore emitted through the gfx/ BGRA32 DDS path.
    "gfx/2d/chars_big",
    "gfx/2d/chars_medium",
    "gfx/2d/chars_tiny",
)
FULLSCREEN_TEXTURE_SEEDS = (
    "textures/common/70yearjourney",
    "textures/common/enemyspace",
    "textures/common/sevenspace",
    "textures/common/tuvokhazard",
)
HOLOMATCH_LOADSCREEN_OVERRIDES = (
    # Loading-screen assets are already shipped by the SP Xbox package.
    # Rebuilding them into xbox1.pk3 shadows the proven SP copies and has
    # produced corrupted loading-wheel draws in Holomatch.
    "menu/common/corner_lr_8_16.dds",
)
HOLOMATCH_SHARED_SP_DDS = (
    # The SP interface HUD owns Holomatch HUD drawing, so xbox1.pk3 carries the
    # same proven DDS assets from xbox0.pk3. This keeps Holomatch DDS-only even
    # after loose original image fallbacks are removed from the staged tree.
    "gfx/interface/ammobar.dds",
    "gfx/interface/ammolowercap1.dds",
    "gfx/interface/ammolowercap2.dds",
    "gfx/interface/ammouppercap1.dds",
    "gfx/interface/ammouppercap2.dds",
    "gfx/interface/armorcap1.dds",
    "gfx/interface/armorcap2.dds",
    "gfx/interface/healthcap1.dds",
    "gfx/interface/healthcap2.dds",
)
SEEDED_SHARED_SP_DDS = HOLOMATCH_SHARED_SP_DDS + HOLOMATCH_LOADSCREEN_OVERRIDES
SEEDED_UI_DDS_PREFIXES = (
    "gfx/",
    "icons/",
    "menu/",
    "sprites/",
)
ORIGINAL_FORMAT_TEXTURES = (
    # These are script/cgame-owned intro assets. Until their Xbox-native
    # conversion has visual proof, do not let xbox0.pk3 override the original
    # JPG/TGA path that Elite Force scripts already drive correctly.
    "gfx/2d/charsgrid_med",
    # Dark/detail-heavy sky backing loses too much signal in the current DXT1
    # conversion, so keep the stock JPG until the Xbox-native path is proven.
    "textures/borg/borgsky",
    # These Borg materials rely on the stock EF shader scripts and authored
    # alpha/additive masks. DDS overrides have produced black panel/field
    # artifacts in CXBX-R/XEMU, so keep the original assets as the authority.
    "textures/borg/bars",
    "textures/borg/bars2",
    "textures/borg/basic1",
    "textures/borg/bigborg",
    "textures/borg/forceborder",
    "textures/borg/forceborder2",
    "textures/borg/forceborder3",
    "textures/borg/green1",
    "textures/borg/green1_dos",
    "textures/borg/oddlight1",
    "textures/borg/oddlight1dam",
    "textures/borg/oddlight1mult",
    "textures/borg/static",
    "textures/borg/static2",
    "textures/borg/static_yellow",
    "textures/common/70yearjourney",
    "textures/common/enemyspace",
    "textures/common/sevenspace",
    "textures/common/tuvokhazard",
)

XBOX_PATCH_SHADER_TEXT = """\
// Xbox-specific shader fixes for stock Elite Force assets.
//
// Keep borg1 structural panels script-neutral: textures/borg/xpanelb is an
// implicit BSP material in the stock map, so texture fidelity is handled by the
// DDS asset format rather than by overriding the shader script.
"""

XBOX_PATCH_SHADER_PATH = "scripts/xbox_borg_fix.shader"

REFERENCE_RE = re.compile(r"\b(qer_editorimage|map|clampmap|animmap|skyparms)\s+(.+)", re.IGNORECASE)
FIXED_ZIP_TIME = (2026, 1, 1, 0, 0, 0)
XBOX_EFFECTS_IMAGE_REL = "sound/dsstdfx.bin"
XBOX_EFFECTS_IMAGE_CANDIDATES = (
    Path(r"C:\XDK\source\dsound\dsp\dsstdfx.bin"),
    Path(r"C:\XDK\Samples\Xbox\Sound\BackgroundMusic\Media\dsstdfx.bin"),
)
HOLOMATCH_BOTFILE_ALIASES = {
    "botfiles/bots/long_i.c": "botfiles/bots/biessman_i.c",
}


def normalized_rel(path: str) -> str:
    return path.replace("\\", "/").strip().lower()


def _rol32(value: int, bits: int) -> int:
    value &= 0xFFFFFFFF
    return ((value << bits) | (value >> (32 - bits))) & 0xFFFFFFFF


def md4_digest(data: bytes) -> bytes:
    mask = 0xFFFFFFFF
    a = 0x67452301
    b = 0xEFCDAB89
    c = 0x98BADCFE
    d = 0x10325476
    bit_length = (len(data) * 8) & 0xFFFFFFFFFFFFFFFF
    padded = data + b"\x80"
    padded += b"\x00" * ((56 - len(padded) % 64) % 64)
    padded += struct.pack("<Q", bit_length)

    def f(x: int, y: int, z: int) -> int:
        return ((x & y) | (~x & z)) & mask

    def g(x: int, y: int, z: int) -> int:
        return ((x & y) | (x & z) | (y & z)) & mask

    def h(x: int, y: int, z: int) -> int:
        return (x ^ y ^ z) & mask

    for offset in range(0, len(padded), 64):
        x = list(struct.unpack_from("<16I", padded, offset))
        aa, bb, cc, dd = a, b, c, d

        shifts = (3, 7, 11, 19)
        order = range(16)
        for i, k in enumerate(order):
            s = shifts[i & 3]
            if i & 3 == 0:
                a = _rol32((a + f(b, c, d) + x[k]) & mask, s)
            elif i & 3 == 1:
                d = _rol32((d + f(a, b, c) + x[k]) & mask, s)
            elif i & 3 == 2:
                c = _rol32((c + f(d, a, b) + x[k]) & mask, s)
            else:
                b = _rol32((b + f(c, d, a) + x[k]) & mask, s)

        shifts = (3, 5, 9, 13)
        order = (0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15)
        for i, k in enumerate(order):
            s = shifts[i & 3]
            if i & 3 == 0:
                a = _rol32((a + g(b, c, d) + x[k] + 0x5A827999) & mask, s)
            elif i & 3 == 1:
                d = _rol32((d + g(a, b, c) + x[k] + 0x5A827999) & mask, s)
            elif i & 3 == 2:
                c = _rol32((c + g(d, a, b) + x[k] + 0x5A827999) & mask, s)
            else:
                b = _rol32((b + g(c, d, a) + x[k] + 0x5A827999) & mask, s)

        shifts = (3, 9, 11, 15)
        order = (0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15)
        for i, k in enumerate(order):
            s = shifts[i & 3]
            if i & 3 == 0:
                a = _rol32((a + h(b, c, d) + x[k] + 0x6ED9EBA1) & mask, s)
            elif i & 3 == 1:
                d = _rol32((d + h(a, b, c) + x[k] + 0x6ED9EBA1) & mask, s)
            elif i & 3 == 2:
                c = _rol32((c + h(d, a, b) + x[k] + 0x6ED9EBA1) & mask, s)
            else:
                b = _rol32((b + h(c, d, a) + x[k] + 0x6ED9EBA1) & mask, s)

        a = (a + aa) & mask
        b = (b + bb) & mask
        c = (c + cc) & mask
        d = (d + dd) & mask

    return struct.pack("<4I", a, b, c, d)


def com_block_checksum(data: bytes) -> int:
    digest = struct.unpack("<4I", md4_digest(data))
    return (digest[0] ^ digest[1] ^ digest[2] ^ digest[3]) & 0xFFFFFFFF


def patch_aas_checksum(data: bytes, checksum: int) -> bytes:
    if len(data) < AAS_HEADER_SIZE:
        raise ValueError("AAS file is too small to patch")
    ident, version = struct.unpack_from("<4sI", data, 0)
    if ident != AAS_IDENT or version not in (4, 5):
        raise ValueError(f"not an Elite Force AAS file ident={ident!r} version={version}")
    output = bytearray(data)
    checksum_bytes = bytearray(struct.pack("<I", checksum & 0xFFFFFFFF))
    if version == 5:
        for index in range(len(checksum_bytes)):
            checksum_bytes[index] ^= (index * 119) & 0xFF
    output[8:12] = checksum_bytes
    return bytes(output)


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
    if keyword == "skyparms":
        tokens = [f"{tokens[0]}_{suffix}" for suffix in SKYBOX_SUFFIXES] if tokens else []
    elif keyword == "animmap":
        tokens = tokens[1:]
    else:
        tokens = tokens[:1]

    refs: set[str] = set()
    for token in tokens:
        token = normalized_rel(token)
        if not (token.startswith("textures/") or token.startswith("env/")):
            continue
        if token.startswith("textures/common/") and not is_always_texture(token):
            continue
        refs.add(token)
    return refs


def is_always_texture(candidate: str) -> bool:
    candidate = normalized_rel(candidate)
    path = Path(*candidate.split("/"))
    if path.suffix.lower() in IMAGE_EXTS or path.suffix.lower() == ".dds":
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


def should_use_rgb565_texture(candidate: str) -> bool:
    candidate = normalized_rel(candidate)
    path = Path(*candidate.split("/"))
    if path.suffix.lower() in IMAGE_EXTS or path.suffix.lower() == ".dds":
        candidate = normalized_rel(path.with_suffix("").as_posix())
    return candidate in RGB565_TEXTURES


def should_force_bgra32_texture(candidate: str) -> bool:
    candidate = normalized_rel(candidate)
    path = Path(*candidate.split("/"))
    if path.suffix.lower() in IMAGE_EXTS or path.suffix.lower() == ".dds":
        candidate = normalized_rel(path.with_suffix("").as_posix())
    return candidate.startswith("gfx/")


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
            or candidate.startswith("models/players2/")
            or candidate.startswith("gfx/")
            or candidate.startswith("env/")
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
    if is_always_texture(out_rel):
        # fonts.dat stores coordinates in the authored 256x256 atlases.
        # Resizing these like ordinary HUD art makes every glyph sample the
        # wrong rectangle even though the DDS itself loads successfully.
        return max(args.max_hud_texture_size, 256)
    if out_rel.startswith("env/"):
        return args.max_loadscreen_texture_size
    if out_rel.startswith("levelshots/"):
        return max(args.max_texture_size, 256)
    if Path(out_rel).with_suffix("").as_posix() in HIGH_FIDELITY_TEXTURES:
        return max(args.max_texture_size, 256)
    if out_rel.startswith("models/players/") or out_rel.startswith("models/players2/"):
        return args.max_player_texture_size
    if out_rel.startswith("gfx/"):
        return args.max_hud_texture_size
    if is_fullscreen_texture(out_rel):
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


def clamp_byte(value: float) -> int:
    if value <= 0:
        return 0
    if value >= 255:
        return 255
    return int(value + 0.5)


def encode_lightmap_rgb565_dds(raw_rgb: bytes, boost: float) -> bytes:
    if len(raw_rgb) != LIGHTMAP_RGB_BYTES:
        raise ValueError(f"lightmap record is {len(raw_rgb)} bytes, expected {LIGHTMAP_RGB_BYTES}")

    payload = bytearray(LIGHTMAP_SIZE * LIGHTMAP_SIZE * 2)
    out_pos = 0
    for in_pos in range(0, len(raw_rgb), 3):
        r = clamp_byte(raw_rgb[in_pos + 0] * boost)
        g = clamp_byte(raw_rgb[in_pos + 1] * boost)
        b = clamp_byte(raw_rgb[in_pos + 2] * boost)
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        payload[out_pos] = value & 0xFF
        payload[out_pos + 1] = (value >> 8) & 0xFF
        out_pos += 2

    return dds_header(
        LIGHTMAP_SIZE,
        LIGHTMAP_SIZE,
        LIGHTMAP_SIZE * 2,
        16,
        (0x0000F800, 0x000007E0, 0x0000001F, 0),
        False,
    ) + bytes(payload)


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


def build_dds(
    source: Path,
    max_size: int,
    force_bgra32: bool = False,
    force_rgb565: bool = False,
    alpha_format: str = "bgra32",
) -> tuple[bytes, dict[str, object]] | None:
    with Image.open(source) as opened:
        has_alpha = image_has_alpha(opened)
        image = resize_for_xbox(opened, max_size)
        if force_rgb565:
            payload = encode_rgb565(image)
            header = dds_header(
                image.width,
                image.height,
                image.width * 2,
                16,
                (0x0000F800, 0x000007E0, 0x0000001F, 0),
                False,
            )
            fmt = "rgb565"
        elif force_bgra32:
            payload = encode_bgra32(image)
            header = dds_bgra32_header(image.width, image.height, image.width * 4)
            fmt = "bgra32"
        elif has_alpha and alpha_format == "bgra32":
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


def build_bgra32_dds_from_bytes(source_name: str, source_bytes: bytes, max_size: int) -> tuple[bytes, dict[str, object]]:
    with Image.open(io.BytesIO(source_bytes)) as opened:
        image = resize_for_xbox(opened, max_size)
        payload = encode_bgra32(image)
        header = dds_bgra32_header(image.width, image.height, image.width * 4)
        info = {
            "source": source_name,
            "format": "bgra32",
            "sourceWidth": opened.width,
            "sourceHeight": opened.height,
            "width": image.width,
            "height": image.height,
            "bytes": len(header) + len(payload),
            "sha1": hashlib.sha1(header + payload).hexdigest(),
        }
        return header + payload, info


def build_seeded_ui_dds_from_bytes(
    source_name: str,
    source_bytes: bytes,
    max_size: int,
) -> tuple[bytes, dict[str, object]]:
    with Image.open(io.BytesIO(source_bytes)) as opened:
        has_alpha = image_has_alpha(opened)
        image = resize_for_xbox(opened, max_size)
        if has_alpha:
            payload = encode_bgra32(image)
            header = dds_bgra32_header(image.width, image.height, image.width * 4)
            fmt = "bgra32"
        else:
            payload = encode_dxt1(image)
            header = dds_dxt1_header(image.width, image.height, len(payload))
            fmt = "dxt1"
        info = {
            "source": source_name,
            "format": fmt,
            "sourceWidth": opened.width,
            "sourceHeight": opened.height,
            "width": image.width,
            "height": image.height,
            "bytes": len(header) + len(payload),
            "sha1": hashlib.sha1(header + payload).hexdigest(),
        }
        return header + payload, info


def add_holomatch_loadscreen_overrides(
    zip_out: zipfile.ZipFile,
    base_dir: Path,
    max_size: int,
    textures: list[dict[str, object]],
) -> None:
    source_pk3 = base_dir / "xbox0.pk3"
    if not source_pk3.is_file():
        raise FileNotFoundError(f"missing xbox0.pk3 for Holomatch loadscreen overrides: {source_pk3}")

    with zipfile.ZipFile(source_pk3, "r") as source_zip:
        entries = {name.lower(): name for name in source_zip.namelist()}
        for rel in HOLOMATCH_LOADSCREEN_OVERRIDES:
            source_name = entries.get(rel.lower())
            if not source_name:
                raise FileNotFoundError(f"missing Holomatch loadscreen source in xbox0.pk3: {rel}")
            data, info = build_bgra32_dds_from_bytes(
                f"xbox0.pk3:{source_name}",
                source_zip.read(source_name),
                max_size,
            )
            zip_write_bytes(zip_out, rel, data)
            info["path"] = rel
            info["source"] = f"xbox0.pk3:{source_name}"
            info["maxTextureSize"] = max_size
            textures.append(info)


def add_holomatch_shared_sp_dds(
    zip_out: zipfile.ZipFile,
    base_dir: Path,
    max_size: int,
    textures: list[dict[str, object]],
    support_files: list[str],
) -> None:
    source_pk3 = base_dir / "xbox0.pk3"
    if not source_pk3.is_file():
        raise FileNotFoundError(f"missing xbox0.pk3 for Holomatch shared SP DDS assets: {source_pk3}")

    with zipfile.ZipFile(source_pk3, "r") as source_zip:
        entries = {name.lower(): name for name in source_zip.namelist()}
        for rel in HOLOMATCH_SHARED_SP_DDS:
            source_name = entries.get(rel.lower())
            if not source_name:
                raise FileNotFoundError(f"missing shared SP DDS asset in xbox0.pk3: {rel}")
            data, info = build_bgra32_dds_from_bytes(
                f"xbox0.pk3:{source_name}",
                source_zip.read(source_name),
                max_size,
            )
            zip_write_bytes(zip_out, rel, data)
            info["path"] = rel
            info["source"] = f"xbox0.pk3:{source_name}"
            info["maxTextureSize"] = max_size
            textures.append(info)
            support_files.append(rel)


def add_seeded_shared_sp_dds(
    zip_out: zipfile.ZipFile,
    seed_pk3: Path,
    max_size: int,
    textures: list[dict[str, object]],
) -> None:
    if not seed_pk3.is_file():
        raise FileNotFoundError(f"missing shared SP DDS seed package: {seed_pk3}")

    written_names = {normalized_rel(name) for name in zip_out.namelist()}
    with zipfile.ZipFile(seed_pk3, "r") as source_zip:
        entries = {name.lower(): name for name in source_zip.namelist()}
        seeded_ui = sorted(
            rel
            for rel in entries
            if rel.endswith(".dds") and rel.startswith(SEEDED_UI_DDS_PREFIXES)
        )
        for rel in sorted(set(SEEDED_SHARED_SP_DDS).union(seeded_ui)):
            if normalized_rel(rel) in written_names:
                continue
            source_name = entries.get(rel.lower())
            if not source_name:
                raise FileNotFoundError(f"missing shared SP DDS asset in seed package: {rel}")
            data, info = build_seeded_ui_dds_from_bytes(
                f"{seed_pk3.name}:{source_name}",
                source_zip.read(source_name),
                max_size,
            )
            zip_write_bytes(zip_out, rel, data)
            written_names.add(normalized_rel(rel))
            info["path"] = rel
            info["source"] = f"{seed_pk3.name}:{source_name}"
            info["maxTextureSize"] = max_size
            textures.append(info)


def parse_bsp_lumps(data: bytes, bsp_path: Path) -> list[tuple[int, int]]:
    if len(data) < 8 + EF_HEADER_LUMPS * 8:
        raise ValueError(f"{bsp_path} is too small to be an EF BSP")

    ident, version = struct.unpack_from("<4sI", data, 0)
    if ident != EF_BSP_IDENT or version != EF_BSP_VERSION:
        raise ValueError(f"{bsp_path} is not an Elite Force IBSP v46 map")

    lumps: list[tuple[int, int]] = []
    for lump in range(EF_HEADER_LUMPS):
        ofs, length = struct.unpack_from("<II", data, 8 + lump * 8)
        if ofs < 0 or length < 0 or ofs > len(data) or length > len(data) - ofs:
            raise ValueError(f"{bsp_path} has invalid lump {lump} ofs={ofs} len={length}")
        lumps.append((ofs, length))
    return lumps


def campaign_bsp_paths(base_dir: Path) -> list[Path]:
    maps_dir = base_dir / "maps"
    paths: list[Path] = []
    for path in sorted(maps_dir.glob("*.bsp")):
        name = path.name.lower()
        if name.startswith(MULTIPLAYER_MAP_PREFIXES):
            continue
        paths.append(path)
    return paths


def multiplayer_bsp_paths(base_dir: Path) -> list[Path]:
    maps_dir = base_dir / "maps"
    return sorted(
        path
        for path in maps_dir.glob("*.bsp")
        if path.name.lower().startswith(MULTIPLAYER_MAP_PREFIXES)
    )


def selected_bsp_paths(base_dir: Path, mode: str, map_name: str) -> list[Path]:
    maps_dir = base_dir / "maps"
    if mode == "none":
        return []
    if mode == "campaign":
        return campaign_bsp_paths(base_dir)
    if mode == "multiplayer":
        return multiplayer_bsp_paths(base_dir)
    if mode == "all":
        return sorted(maps_dir.glob("*.bsp"))
    if mode == "map":
        path = maps_dir / f"{map_name}.bsp"
        if not path.exists():
            raise FileNotFoundError(path)
        return [path]
    raise ValueError(f"unknown BSP map mode {mode}")


def optimized_bsp_and_lightmaps(bsp_path: Path, boost: float) -> tuple[bytes, bytes, dict[str, object]]:
    data = bsp_path.read_bytes()
    lumps = parse_bsp_lumps(data, bsp_path)
    lightmap_ofs, lightmap_len = lumps[EF_LUMP_LIGHTMAPS]

    if lightmap_len <= 0:
        raise ValueError(f"{bsp_path} has no lightmap lump to optimize")
    if lightmap_len % LIGHTMAP_RGB_BYTES:
        raise ValueError(f"{bsp_path} lightmap lump is not 128x128 RGB aligned")

    lightmap_count = lightmap_len // LIGHTMAP_RGB_BYTES
    sidecar = bytearray(struct.pack("<I", LIGHTMAP_RGB565_DDS_BYTES))
    for index in range(lightmap_count):
        start = lightmap_ofs + index * LIGHTMAP_RGB_BYTES
        sidecar.extend(encode_lightmap_rgb565_dds(data[start : start + LIGHTMAP_RGB_BYTES], boost))

    new_lumps = [(0, 0)] * EF_HEADER_LUMPS
    output = bytearray(8 + EF_HEADER_LUMPS * 8)
    write_items = sorted(
        (ofs, lump, length)
        for lump, (ofs, length) in enumerate(lumps)
        if lump != EF_LUMP_LIGHTMAPS and length > 0
    )

    for ofs, lump, length in write_items:
        while len(output) & 3:
            output.append(0)
        new_ofs = len(output)
        output.extend(data[ofs : ofs + length])
        new_lumps[lump] = (new_ofs, length)

    new_lumps[EF_LUMP_LIGHTMAPS] = (8 + EF_HEADER_LUMPS * 8, 0)
    struct.pack_into("<4sI", output, 0, EF_BSP_IDENT, EF_BSP_VERSION)
    for lump, (ofs, length) in enumerate(new_lumps):
        struct.pack_into("<II", output, 8 + lump * 8, ofs, length)

    optimized = bytes(output)
    optimized_lumps = parse_bsp_lumps(optimized, bsp_path)

    preserved_hash_ok = True
    for lump in range(EF_HEADER_LUMPS):
        if lump == EF_LUMP_LIGHTMAPS:
            continue
        old_ofs, old_len = lumps[lump]
        new_ofs, new_len = optimized_lumps[lump]
        if old_len != new_len:
            preserved_hash_ok = False
            break
        if hashlib.sha1(data[old_ofs : old_ofs + old_len]).digest() != hashlib.sha1(optimized[new_ofs : new_ofs + new_len]).digest():
            preserved_hash_ok = False
            break

    sidecar_bytes = len(sidecar)
    stream_peak_before = len(data) + LIGHTMAP_SIZE * LIGHTMAP_SIZE * 4
    stream_peak_after = len(optimized) + LIGHTMAP_RGB565_DDS_BYTES + (LIGHTMAP_SIZE * LIGHTMAP_SIZE * 4)
    report = {
        "map": bsp_path.stem,
        "source": normalized_rel(bsp_path.as_posix()),
        "originalBspBytes": len(data),
        "optimizedBspBytes": len(optimized),
        "rawLightmapBytesRemoved": lightmap_len,
        "optimizedLightmapSidecarBytes": sidecar_bytes,
        "lightmapCount": lightmap_count,
        "lightmapRecordBytes": LIGHTMAP_RGB565_DDS_BYTES,
        "bspReadBufferBytesSaved": len(data) - len(optimized),
        "estimatedPeakLoadBytesBefore": stream_peak_before,
        "estimatedPeakLoadBytesAfter": stream_peak_after,
        "estimatedPeakLoadBytesSaved": stream_peak_before - stream_peak_after,
        "nonLightmapLumpsPreserved": preserved_hash_ok,
        "lightmapBoostBaked": boost,
    }
    if not preserved_hash_ok:
        raise ValueError(f"{bsp_path} optimized BSP changed a non-lightmap lump")
    return optimized, bytes(sidecar), report


def zip_write_bytes(zip_out: zipfile.ZipFile, rel: str, data: bytes) -> None:
    info = zipfile.ZipInfo(normalized_rel(rel), FIXED_ZIP_TIME)
    info.compress_type = zipfile.ZIP_STORED
    info.external_attr = 0o644 << 16
    zip_out.writestr(info, data)


def existing_dds_entries(out_path: Path) -> dict[str, bytes]:
    carried: dict[str, bytes] = {}
    if not out_path.is_file():
        return carried

    try:
        with zipfile.ZipFile(out_path, "r") as existing:
            for source_name in existing.namelist():
                rel = normalized_rel(source_name)
                if Path(rel).suffix.lower() != ".dds":
                    continue
                data = existing.read(source_name)
                if len(data) >= 128 and data[:4] == b"DDS ":
                    carried[rel] = data
    except (OSError, zipfile.BadZipFile):
        return {}

    return carried


def carried_dds_info(source_name: str, data: bytes) -> dict[str, object]:
    height = struct.unpack_from("<I", data, 12)[0]
    width = struct.unpack_from("<I", data, 16)[0]
    fourcc_code = data[84:88]
    bits_per_pixel = struct.unpack_from("<I", data, 88)[0]
    if fourcc_code == b"DXT1":
        fmt = "dxt1"
    elif fourcc_code == b"DXT5":
        fmt = "dxt5"
    elif bits_per_pixel == 32:
        fmt = "bgra32"
    elif bits_per_pixel == 16:
        fmt = "rgb565"
    else:
        fmt = "unknown"
    return {
        "source": source_name,
        "format": fmt,
        "sourceWidth": width,
        "sourceHeight": height,
        "width": width,
        "height": height,
        "bytes": len(data),
        "sha1": hashlib.sha1(data).hexdigest(),
        "carriedForward": True,
    }


def ui_script_files(base_dir: Path) -> list[Path]:
    ui_dir = base_dir / "ui"
    if not ui_dir.is_dir():
        return []

    deprecated_mp_menu_scripts = {"jahud.txt", "jampmenus.txt", "jampingame.txt", "testhud.menu"}
    scripts: list[Path] = []
    for path in ui_dir.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix.lower() not in {".txt", ".menu"}:
            continue
        if path.name.lower() == "vssver.scc":
            continue

        rel = normalized_rel(path.relative_to(ui_dir).as_posix()).lower()
        if rel in deprecated_mp_menu_scripts or rel.startswith("jamp/"):
            continue

        scripts.append(path)

    return sorted(scripts)


def holomatch_support_files(base_dir: Path, map_names: list[str]) -> list[Path]:
    direct_files = [
        *(f"maps/{map_name}.aas" for map_name in map_names),
        "scripts/bots.txt",
        "scripts/arenas.txt",
        "scripts/xpack.arena",
        "botfiles/bots/chars.h",
        "botfiles/bots/seven_c.c",
        "botfiles/bots/seven_i.c",
        "botfiles/bots/seven_t.c",
        "botfiles/bots/seven_w.c",
        "botfiles/bots/reaver_c.c",
        "botfiles/bots/reaver_i.c",
        "botfiles/bots/reaver_t.c",
        "botfiles/bots/reaver_w.c",
    ]
    support_dirs = (
        "botfiles",
        "models/players/munro",
        "models/players/seven",
        "models/players/reaver",
        "models/players2",
        "models/powerups/trek",
        "models/weapons2",
    )

    seen: set[str] = set()
    files: list[Path] = []

    def add_path(path: Path) -> None:
        if not path.is_file():
            return
        rel = normalized_rel(path.relative_to(base_dir).as_posix())
        if rel in seen:
            return
        seen.add(rel)
        files.append(path)

    for rel in direct_files:
        add_path(base_dir / Path(*rel.split("/")))

    for rel_dir in support_dirs:
        root = base_dir / Path(*rel_dir.split("/"))
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if path.name.lower() == "vssver.scc":
                continue
            if path.suffix.lower() in IMAGE_EXTS or path.suffix.lower() == ".dds":
                continue
            add_path(path)

    return sorted(files, key=lambda path: normalized_rel(path.relative_to(base_dir).as_posix()))


def shader_script_files(base_dir: Path) -> list[Path]:
    scripts_dir = base_dir / "scripts"
    if not scripts_dir.is_dir():
        return []

    return sorted(path for path in scripts_dir.glob("*.shader") if path.is_file())


def console_shader_list_bytes(shader_scripts: list[str]) -> bytes:
    names = sorted({rel.rsplit("/", 1)[-1] for rel in shader_scripts})
    return (("\r\n".join(names) + "\r\n") if names else "").encode("ascii")


def console_file_list_bytes(names: list[str]) -> bytes:
    unique = sorted({name for name in names if name})
    return (("\r\n".join(unique) + "\r\n") if unique else "").encode("ascii")


def resolve_xbox_effects_image(explicit_path: Path | None) -> Path:
    candidates = [explicit_path] if explicit_path else list(XBOX_EFFECTS_IMAGE_CANDIDATES)
    for candidate in candidates:
        if candidate and candidate.is_file():
            return candidate.resolve()

    checked = ", ".join(str(path) for path in candidates if path)
    raise FileNotFoundError(
        f"missing Xbox effects image for {XBOX_EFFECTS_IMAGE_REL}; checked: {checked}"
    )


def build_patch(args: argparse.Namespace) -> dict[str, object]:
    base_dir = args.base_dir.resolve()
    map_path = base_dir / "maps" / f"{args.map}.bsp"
    out_path = args.output.resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    carried_dds = existing_dds_entries(out_path)

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
    preserved_original_sources: list[dict[str, str]] = []
    preserved_original_written: set[str] = set()
    bsp_optimizations: list[dict[str, object]] = []
    bsp_outputs: list[tuple[str, str, bytes, str, bytes]] = []
    bsp_checksums: dict[str, int] = {}
    patched_aas_checksums: dict[str, int] = {}
    ui_scripts: list[str] = []
    support_files: list[str] = []
    effects_image_info: dict[str, object] | None = None
    shader_scripts: list[str] = []
    shader_script_set: set[str] = set()

    if args.include_bsp and args.bsp_mode != "none":
        raise ValueError("--include-bsp cannot be combined with --bsp-mode")

    if args.bsp_mode != "none":
        for bsp_path in selected_bsp_paths(base_dir, args.bsp_maps, args.map):
            optimized_bsp, optimized_lightmaps, report = optimized_bsp_and_lightmaps(
                bsp_path, args.lightmap_boost
            )
            map_name = bsp_path.stem.lower()
            bsp_out_rel = f"maps/xbox/{map_name}.bsp"
            lightmap_out_rel = f"maps/xbox/{map_name}.lmpdds"
            checksum = com_block_checksum(optimized_bsp)
            report["bspPath"] = bsp_out_rel
            report["lightmapPath"] = lightmap_out_rel
            report["xboxBspChecksum"] = checksum
            bsp_optimizations.append(report)
            bsp_outputs.append((bsp_out_rel, map_name, optimized_bsp, lightmap_out_rel, optimized_lightmaps))
            bsp_checksums[map_name] = checksum

    with zipfile.ZipFile(out_path, "w") as zip_out:
        patch_shader_rel = normalized_rel(XBOX_PATCH_SHADER_PATH)
        zip_write_bytes(zip_out, patch_shader_rel, XBOX_PATCH_SHADER_TEXT.encode("ascii"))
        shader_scripts.append(patch_shader_rel)
        shader_script_set.add(patch_shader_rel)

        for shader_path in shader_script_files(base_dir):
            shader_rel = normalized_rel(shader_path.relative_to(base_dir).as_posix())
            if shader_rel in shader_script_set:
                continue
            zip_write_bytes(zip_out, shader_rel, shader_path.read_bytes())
            shader_scripts.append(shader_rel)
            shader_script_set.add(shader_rel)

        shader_list_path = "scripts/_console_shader_list_"
        zip_write_bytes(zip_out, shader_list_path, console_shader_list_bytes(shader_scripts))

        if not args.no_ui_scripts:
            for ui_path in ui_script_files(base_dir):
                ui_rel = normalized_rel(ui_path.relative_to(base_dir).as_posix())
                zip_write_bytes(zip_out, ui_rel, ui_path.read_bytes())
                ui_scripts.append(ui_rel)

        if args.holomatch_support_assets:
            support_map_names = sorted(bsp_checksums) if bsp_checksums else [args.map.lower()]
            for support_path in holomatch_support_files(base_dir, support_map_names):
                support_rel = normalized_rel(support_path.relative_to(base_dir).as_posix())
                support_data = support_path.read_bytes()
                support_suffix = Path(support_rel).suffix.lower()
                support_map_name = Path(support_rel).stem.lower()
                if support_suffix == ".aas" and support_map_name in bsp_checksums:
                    checksum = bsp_checksums[support_map_name]
                    support_data = patch_aas_checksum(support_data, checksum)
                    patched_aas_checksums[support_rel] = checksum
                zip_write_bytes(zip_out, support_rel, support_data)
                support_files.append(support_rel)
            for alias_rel, source_rel in sorted(HOLOMATCH_BOTFILE_ALIASES.items()):
                if alias_rel in support_files:
                    continue
                source_path = base_dir / Path(*source_rel.split("/"))
                if not source_path.is_file():
                    raise FileNotFoundError(
                        f"missing official EF botfile alias source: {source_path}"
                    )
                zip_write_bytes(zip_out, alias_rel, source_path.read_bytes())
                support_files.append(alias_rel)
            arena_names = [
                name
                for name in ("arenas.txt", "xpack.arena")
                if (base_dir / "scripts" / name).is_file()
            ]
            holomatch_lists = {
                "scripts/_console_bot_list_": ["bots.txt"],
                "scripts/_console_arena_list_": arena_names,
            }
            for list_rel, names in holomatch_lists.items():
                zip_write_bytes(zip_out, list_rel, console_file_list_bytes(names))
                support_files.append(list_rel)

            if args.effects_image is not None:
                effects_image_path = resolve_xbox_effects_image(args.effects_image)
                effects_image_data = effects_image_path.read_bytes()
                zip_write_bytes(zip_out, XBOX_EFFECTS_IMAGE_REL, effects_image_data)
                support_files.append(XBOX_EFFECTS_IMAGE_REL)
                effects_image_info = {
                    "path": XBOX_EFFECTS_IMAGE_REL,
                    "source": str(effects_image_path),
                    "bytes": len(effects_image_data),
                    "sha1": hashlib.sha1(effects_image_data).hexdigest(),
                }
            add_holomatch_shared_sp_dds(
                zip_out,
                base_dir,
                args.max_hud_texture_size,
                textures,
                support_files,
            )
            add_holomatch_loadscreen_overrides(
                zip_out,
                base_dir,
                args.max_loadscreen_texture_size,
                textures,
            )

        bsp_rel: str | None = None
        if args.include_bsp:
            bsp_rel = f"maps/xbox/{args.map}.bsp"
            zip_write_bytes(zip_out, bsp_rel, map_path.read_bytes())

        if args.bsp_mode != "none":
            for bsp_out_rel, _map_name, optimized_bsp, lightmap_out_rel, optimized_lightmaps in bsp_outputs:
                zip_write_bytes(zip_out, bsp_out_rel, optimized_bsp)
                zip_write_bytes(zip_out, lightmap_out_rel, optimized_lightmaps)

        written_names = {normalized_rel(name) for name in zip_out.namelist()}
        for out_rel, source in sorted(resolved.items()):
            out_rel = normalized_rel(out_rel)
            if out_rel in written_names:
                continue
            if not args.dds_only and should_preserve_original_texture(out_rel):
                source_rel = normalized_rel(source.relative_to(base_dir).as_posix())
                if source_rel not in preserved_original_written:
                    zip_write_bytes(zip_out, source_rel, source.read_bytes())
                    preserved_original_written.add(source_rel)
                    written_names.add(source_rel)
                preserved_original.append(out_rel)
                preserved_original_sources.append(
                    {
                        "path": source_rel,
                        "source": source_rel,
                        "skippedOverride": out_rel,
                    }
                )
                continue
            max_size = texture_size_for_path(out_rel, args)
            built = build_dds(
                source,
                max_size,
                force_bgra32=should_force_bgra32_texture(out_rel),
                force_rgb565=should_use_rgb565_texture(out_rel),
                alpha_format=args.alpha_texture_format,
            )
            if built is None:
                skipped_alpha.append(out_rel)
                continue
            dds, info = built
            zip_write_bytes(zip_out, out_rel, dds)
            written_names.add(out_rel)
            info["path"] = out_rel
            info["source"] = normalized_rel(source.relative_to(base_dir).as_posix())
            info["maxTextureSize"] = max_size
            textures.append(info)

        if args.dds_seed is not None:
            add_seeded_shared_sp_dds(
                zip_out,
                args.dds_seed.resolve(),
                args.max_hud_texture_size,
                textures,
            )

        written_names = {normalized_rel(name) for name in zip_out.namelist()}
        for out_rel, data in sorted(carried_dds.items()):
            if out_rel in written_names:
                continue
            zip_write_bytes(zip_out, out_rel, data)
            info = carried_dds_info(f"{out_path.name}:{out_rel}", data)
            info["path"] = out_rel
            info["maxTextureSize"] = texture_size_for_path(out_rel, args)
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
            "ddsOnly": args.dds_only,
            "alphaTextureFormat": args.alpha_texture_format,
            "textureCount": len(textures),
            "textures": textures,
            "skippedTextureCandidates": skipped,
            "skippedAlphaTextures": skipped_alpha,
            "preservedOriginalTextures": preserved_original,
            "preservedOriginalTextureSources": preserved_original_sources,
            "patchShaders": [XBOX_PATCH_SHADER_PATH],
            "shaderScriptCount": len(shader_scripts),
            "shaderScripts": shader_scripts,
            "shaderListPath": shader_list_path,
            "shaderListCount": len(shader_scripts),
            "uiScriptCount": len(ui_scripts),
            "uiScripts": ui_scripts,
            "supportFileCount": len(support_files),
            "supportFiles": support_files,
            "effectsImage": effects_image_info,
            "patchedAasChecksums": patched_aas_checksums,
            "bspMode": args.bsp_mode,
            "bspMaps": args.bsp_maps,
            "bspOptimizationCount": len(bsp_optimizations),
            "bspOptimizations": bsp_optimizations,
            "bspOptimizationTotals": {
                "originalBspBytes": sum(int(item["originalBspBytes"]) for item in bsp_optimizations),
                "optimizedBspBytes": sum(int(item["optimizedBspBytes"]) for item in bsp_optimizations),
                "rawLightmapBytesRemoved": sum(int(item["rawLightmapBytesRemoved"]) for item in bsp_optimizations),
                "optimizedLightmapSidecarBytes": sum(int(item["optimizedLightmapSidecarBytes"]) for item in bsp_optimizations),
                "bspReadBufferBytesSaved": sum(int(item["bspReadBufferBytesSaved"]) for item in bsp_optimizations),
                "estimatedPeakLoadBytesSaved": sum(int(item["estimatedPeakLoadBytesSaved"]) for item in bsp_optimizations),
            },
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
        "ddsOnly": args.dds_only,
        "alphaTextureFormat": args.alpha_texture_format,
        "textures": len(textures),
        "skipped": len(skipped),
        "skippedAlpha": len(skipped_alpha),
        "preservedOriginal": len(preserved_original),
        "preservedOriginalSources": len(preserved_original_written),
        "uiScripts": len(ui_scripts),
        "supportFiles": len(support_files),
        "bspMode": args.bsp_mode,
        "bspMaps": args.bsp_maps,
        "bspOptimizations": len(bsp_optimizations),
        "bspOptimizationTotals": manifest["bspOptimizationTotals"],
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
    parser.add_argument(
        "--dds-seed",
        type=Path,
        help="Optional licensed DDS seed used for direct-registration SP HUD atlases.",
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
        "--no-ui-scripts",
        action="store_true",
        help="Do not pack UI .menu/.txt scripts into this patch PK3.",
    )
    parser.add_argument(
        "--holomatch-support-assets",
        action="store_true",
        help="Pack non-texture map/bot/model support files needed by the direct Holomatch smoke target.",
    )
    parser.add_argument(
        "--effects-image",
        type=Path,
        help=(
            "Optional Xbox DirectSound effects image to pack as sound/dsstdfx.bin. "
            "Holomatch omits it by default to match the SP dry-audio setup."
        ),
    )
    parser.add_argument(
        "--bsp-mode",
        choices=("none", "optimized-lightmaps"),
        default="none",
        help=(
            "Build Xbox BSP overrides that preserve all geometry/collision lumps "
            "and move raw RGB lightmaps to streamed RGB565 DDS sidecars."
        ),
    )
    parser.add_argument(
        "--bsp-maps",
        choices=("map", "campaign", "multiplayer", "all"),
        default="map",
        help="Which BSPs to optimize when --bsp-mode is active.",
    )
    parser.add_argument(
        "--lightmap-boost",
        type=float,
        default=DEFAULT_LIGHTMAP_BOOST,
        help="Brightness multiplier baked into optimized RGB565 lightmaps.",
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
    parser.add_argument(
        "--dds-only",
        action="store_true",
        help="Convert all selected source textures to DDS instead of preserving original JPG/TGA overrides.",
    )
    parser.add_argument(
        "--alpha-texture-format",
        choices=("dxt5", "bgra32"),
        default="bgra32",
        help="DDS format to use for selected source textures with alpha.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    summary = build_patch(parse_args(argv))
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
