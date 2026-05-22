"""
Deep retail XBE / asset analysis for the JA Xbox port.

This is intentionally evidence-oriented: it writes compact reports that can be
compared against source code decisions without needing the Ghidra UI.
"""

import collections
import os
import struct
import sys
from pathlib import Path

from capstone import *
from capstone.x86 import *

XBE_EP_RETAIL = 0xA8FC57AB
XBE_KT_RETAIL = 0x5B6D40B6

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "scripts" / "output" / "deep_retail"
RETAIL_ROOT = REPO / "Star Wars Jedi Academy game"
RETAIL_XBE = RETAIL_ROOT / "default.xbe"

D3D_FORMATS_XBOX = {
    0xFFFFFFFF: "D3DFMT_UNKNOWN",
    0x00000006: "D3DFMT_A8R8G8B8",
    0x00000007: "D3DFMT_X8R8G8B8",
    0x00000005: "D3DFMT_R5G6B5",
    0x00000003: "D3DFMT_X1R5G5B5",
    0x00000002: "D3DFMT_A1R5G5B5",
    0x00000004: "D3DFMT_A4R4G4B4",
    0x00000019: "D3DFMT_A8",
    0x0000000B: "D3DFMT_P8",
    0x00000000: "D3DFMT_L8",
    0x0000001A: "D3DFMT_A8L8",
    0x00000001: "D3DFMT_AL8",
    0x0000002A: "D3DFMT_D24S8",
    0x0000002C: "D3DFMT_D16",
    0x0000000C: "D3DFMT_DXT1",
    0x0000000E: "D3DFMT_DXT2/DXT3",
    0x0000000F: "D3DFMT_DXT4/DXT5",
    0x00000010: "D3DFMT_LIN_A1R5G5B5",
    0x0000001D: "D3DFMT_LIN_A4R4G4B4",
    0x00000012: "D3DFMT_LIN_A8R8G8B8",
    0x0000001E: "D3DFMT_LIN_X8R8G8B8",
    0x00000011: "D3DFMT_LIN_R5G6B5",
    0x00000013: "D3DFMT_LIN_L8",
    0x0000002E: "D3DFMT_LIN_D24S8",
}

GL_FORMATS = {
    0x83F0: "GL_COMPRESSED_RGB_S3TC_DXT1_EXT",
    0x83F1: "GL_COMPRESSED_RGBA_S3TC_DXT1_EXT",
    0x83F2: "GL_COMPRESSED_RGBA_S3TC_DXT3_EXT",
    0x83F3: "GL_COMPRESSED_RGBA_S3TC_DXT5_EXT",
    0x9995: "GL_DDS1_EXT",
    0x9996: "GL_DDS5_EXT",
    0x9997: "GL_DDS_RGB16_EXT",
    0x9998: "GL_DDS_RGBA32_EXT",
}


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def cstr(data, off, limit=512):
    if off is None or off < 0 or off >= len(data):
        return ""
    end = data.find(b"\x00", off, min(len(data), off + limit))
    if end < 0:
        end = min(len(data), off + limit)
    return data[off:end].decode("ascii", "replace")


class Xbe:
    def __init__(self, path):
        self.path = Path(path)
        self.raw = self.path.read_bytes()
        if u32(self.raw, 0) != 0x48454258:
            raise ValueError(f"Not an XBE: {path}")
        self.base = u32(self.raw, 0x104)
        self.entry = u32(self.raw, 0x128) ^ XBE_EP_RETAIL
        self.kernel_thunk = u32(self.raw, 0x158) ^ XBE_KT_RETAIL
        self.section_count = u32(self.raw, 0x11C)
        self.section_headers_va = u32(self.raw, 0x120)
        self.sections = []
        hdr_off = self.section_headers_va - self.base
        for i in range(self.section_count):
            off = hdr_off + i * 0x38
            flags = u32(self.raw, off)
            va = u32(self.raw, off + 0x04)
            vsz = u32(self.raw, off + 0x08)
            roff = u32(self.raw, off + 0x0C)
            rsz = u32(self.raw, off + 0x10)
            name_va = u32(self.raw, off + 0x14)
            name = cstr(self.raw, name_va - self.base, 64)
            self.sections.append({
                "name": name,
                "flags": flags,
                "va": va,
                "vsz": vsz,
                "roff": roff,
                "rsz": rsz,
            })

    def section(self, name):
        return next(s for s in self.sections if s["name"] == name)

    def va_to_off(self, va):
        for s in self.sections:
            if s["va"] <= va < s["va"] + max(s["vsz"], s["rsz"]):
                off = s["roff"] + (va - s["va"])
                if 0 <= off < len(self.raw):
                    return off
        return None

    def bytes_at(self, va, size):
        off = self.va_to_off(va)
        if off is None:
            return b""
        return self.raw[off:off + size]

    def read_cstr_va(self, va):
        return cstr(self.raw, self.va_to_off(va))

    def section_bytes(self, name):
        s = self.section(name)
        return s, self.raw[s["roff"]:s["roff"] + s["rsz"]]


def disasm(xbe, start, size):
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    return list(md.disasm(xbe.bytes_at(start, size), start))


def find_ascii_strings(xbe, min_len=4):
    out = []
    printable = set(range(0x20, 0x7F)) | {9, 10, 13}
    for s in xbe.sections:
        data = xbe.raw[s["roff"]:s["roff"] + s["rsz"]]
        i = 0
        while i < len(data):
            if data[i] not in printable:
                i += 1
                continue
            j = i
            while j < len(data) and data[j] in printable:
                j += 1
            if j - i >= min_len:
                va = s["va"] + i
                text = data[i:j].decode("ascii", "replace")
                out.append((va, s["name"], text))
            i = j + 1
    return out


def find_push_xrefs(xbe, target_va):
    text = xbe.section(".text")
    data = xbe.raw[text["roff"]:text["roff"] + text["rsz"]]
    needle = b"\x68" + struct.pack("<I", target_va)
    results = []
    pos = data.find(needle)
    while pos >= 0:
        results.append(text["va"] + pos)
        pos = data.find(needle, pos + 1)
    return results


def find_imm32_xrefs(xbe, target_va):
    text = xbe.section(".text")
    data = xbe.raw[text["roff"]:text["roff"] + text["rsz"]]
    needle = struct.pack("<I", target_va)
    results = []
    pos = data.find(needle)
    while pos >= 0:
        results.append(text["va"] + pos)
        pos = data.find(needle, pos + 1)
    return results


def find_bytes_va(xbe, blob):
    hits = []
    for s in xbe.sections:
        data = xbe.raw[s["roff"]:s["roff"] + s["rsz"]]
        pos = data.find(blob)
        while pos >= 0:
            hits.append(s["va"] + pos)
            pos = data.find(blob, pos + 1)
    return hits


def function_bounds_by_padding(xbe):
    text = xbe.section(".text")
    data = xbe.raw[text["roff"]:text["roff"] + text["rsz"]]
    starts = {text["va"]}
    # MSVC release code in this XBE is frequently int3 padded.
    for i in range(1, len(data) - 16):
        if data[i - 1] == 0xCC and data[i] != 0xCC:
            # Accept likely prologue or short leaf function starts.
            if data[i] in (0x55, 0x56, 0x57, 0x53, 0x83, 0x81, 0x8B, 0xA1, 0xB8, 0xE9, 0xC3):
                starts.add(text["va"] + i)
    starts = sorted(starts)
    bounds = []
    end = text["va"] + text["rsz"]
    for i, st in enumerate(starts):
        en = starts[i + 1] if i + 1 < len(starts) else end
        if en > st:
            bounds.append((st, en))
    return bounds


def enclosing_function(bounds, va):
    for st, en in bounds:
        if st <= va < en:
            return st, en
    return None, None


def analyze_cvars(xbe, bounds, strings):
    wanted = [
        "r_allowExtensions", "r_ext_compress_textures", "r_ext_compress_lightmaps",
        "r_ext_preferred_tc_method", "r_picmip", "r_colorMipLevels",
        "r_detailtextures", "r_texturebits", "r_texturebitslm", "r_simpleMipMaps",
        "r_textureMode", "r_modelpoolmegs", "r_dynamiclight", "r_vertexLight",
    ]
    by_text = {t: va for va, _sec, t in strings if t in wanted}
    lines = []
    lines.append("CVAR REGISTRATION EVIDENCE")
    lines.append("=" * 80)
    cvar_get = 0x24CC0
    for name in wanted:
        va = by_text.get(name)
        if not va:
            lines.append(f"{name}: not found")
            continue
        xrefs = find_push_xrefs(xbe, va)
        lines.append(f"\n{name} @ 0x{va:08X}, push-xrefs={len(xrefs)}")
        for xr in xrefs[:8]:
            st, en = enclosing_function(bounds, xr)
            insns = disasm(xbe, max(st or xr - 64, xr - 64), (en - st) if st else 160)
            # Locate the push and next Cvar_Get call; collect prior pushes.
            idx = next((i for i, ins in enumerate(insns) if ins.address == xr), None)
            default = flags = None
            call_addr = None
            if idx is not None:
                call_i = None
                for j in range(idx, min(len(insns), idx + 12)):
                    ins = insns[j]
                    if ins.id == X86_INS_CALL and ins.operands and ins.operands[0].type == X86_OP_IMM:
                        if (ins.operands[0].imm & 0xFFFFFFFF) == cvar_get:
                            call_i = j
                            call_addr = ins.address
                            break
                if call_i is not None:
                    pushes = []
                    for k in range(call_i - 1, max(-1, call_i - 12), -1):
                        ins = insns[k]
                        if ins.id == X86_INS_PUSH and ins.operands and ins.operands[0].type == X86_OP_IMM:
                            pushes.append((ins.address, ins.operands[0].imm & 0xFFFFFFFF))
                            if len(pushes) == 3:
                                break
                    if len(pushes) == 3:
                        # Walking backwards from call: name, default, flags.
                        name_va = pushes[0][1]
                        default_va = pushes[1][1]
                        flags = pushes[2][1]
                        if name_va == va:
                            default = xbe.read_cstr_va(default_va)
            lines.append(f"  xref 0x{xr:08X} fn=0x{(st or 0):08X}-0x{(en or 0):08X} default={default!r} flags={flags} call={call_addr}")
    return "\n".join(lines)


def analyze_texture_functions(xbe, bounds, strings):
    anchors = [
        "CreateTexture", "GL_Bind: NULL image", "WARNING: reused image",
        "total images", "total texture mem", "total texels",
        "DDS1", "DDS5", "DDS16", "DDS32", "DXT1", "DXT5",
        "Vicarious Visions", "Optimized DX8/OpenGL Layer", "r_imagelist",
        "texturemode:", "picmip:", "compressed textures:",
    ]
    by_text = {}
    for va, sec, text in strings:
        for anchor in anchors:
            if anchor in text:
                by_text.setdefault(anchor, []).append((va, sec, text))
    d3d = xbe.section("D3D")
    d3d_lo, d3d_hi = d3d["va"], d3d["va"] + d3d["vsz"]
    lines = ["TEXTURE / FAKEGL ANCHORS", "=" * 80]
    seen_funcs = set()
    for anchor in anchors:
        vals = by_text.get(anchor, [])
        lines.append(f"\n{anchor!r}: {len(vals)} string(s)")
        for sva, sec, text in vals[:16]:
            xrefs = find_imm32_xrefs(xbe, sva)
            lines.append(f"  string 0x{sva:08X} {sec}, imm-xrefs={len(xrefs)} text={text!r}")
            for xr in xrefs[:16]:
                st, en = enclosing_function(bounds, xr)
                lines.append(f"    xref 0x{xr:08X} fn=0x{(st or 0):08X}-0x{(en or 0):08X}")
                if st:
                    seen_funcs.add((st, en))

    lines.append("\nFUNCTION SUMMARIES FOR TEXTURE ANCHORS")
    lines.append("=" * 80)
    for st, en in sorted(seen_funcs):
        size = min(en - st, 0x700)
        insns = disasm(xbe, st, size)
        calls = []
        imms = []
        for ins in insns:
            if ins.id == X86_INS_CALL and ins.operands and ins.operands[0].type == X86_OP_IMM:
                tgt = ins.operands[0].imm & 0xFFFFFFFF
                where = "D3D" if d3d_lo <= tgt < d3d_hi else ".text"
                calls.append((ins.address, tgt, where))
            for op in ins.operands:
                if op.type == X86_OP_IMM:
                    val = op.imm & 0xFFFFFFFF
                    if val in D3D_FORMATS_XBOX or val in GL_FORMATS or val in (0x9995, 0x9996, 0x9997, 0x9998):
                        imms.append((ins.address, val))
        lines.append(f"\nfn 0x{st:08X}-0x{en:08X} size=0x{en-st:X}")
        if calls:
            lines.append("  calls:")
            for addr, tgt, where in calls[:32]:
                lines.append(f"    0x{addr:08X} -> 0x{tgt:08X} [{where}]")
        if imms:
            lines.append("  interesting immediates:")
            for addr, val in imms[:32]:
                lines.append(f"    0x{addr:08X}: 0x{val:08X} {D3D_FORMATS_XBOX.get(val, GL_FORMATS.get(val, ''))}")
    return "\n".join(lines)


def analyze_retail_texture_upload_paths(xbe, bounds):
    d3d = xbe.section("D3D")
    xgrph = xbe.section("XGRPH")
    d3d_lo, d3d_hi = d3d["va"], d3d["va"] + d3d["vsz"]
    xg_lo, xg_hi = xgrph["va"], xgrph["va"] + xgrph["vsz"]
    lines = ["RETAIL TEXTURE UPLOAD / CONVERSION PATHS", "=" * 80]

    for sva in find_bytes_va(xbe, b"CreateTexture"):
        lines.append(f"CreateTexture bytes at 0x{sva:08X}")
        for xr in find_push_xrefs(xbe, sva) + find_imm32_xrefs(xbe, sva):
            st, en = enclosing_function(bounds, xr)
            lines.append(f"  xref 0x{xr:08X} fn=0x{(st or 0):08X}-0x{(en or 0):08X}")

    ranges = [
        (0x000A1EB0, 0x000A2517, "DX8/OpenGL texture render/update helper"),
        (0x000A2700, 0x000A27E0, "TexSubImage / surface load helper"),
        (0x00072770, 0x00072A00, "R_ImageList_f / texture memory accounting"),
        (0x00072A00, 0x000734F0, "R_FindImageFile/R_CreateImage cluster"),
    ]
    for st, en, label in ranges:
        lines.append(f"\n{label}: 0x{st:08X}-0x{en:08X}")
        insns = disasm(xbe, st, en - st)
        for ins in insns:
            note = ""
            if ins.id == X86_INS_CALL and ins.operands and ins.operands[0].type == X86_OP_IMM:
                tgt = ins.operands[0].imm & 0xFFFFFFFF
                if d3d_lo <= tgt < d3d_hi:
                    note = f" -> D3D+0x{tgt-d3d_lo:X}"
                elif xg_lo <= tgt < xg_hi:
                    note = f" -> XGRPH+0x{tgt-xg_lo:X}"
            imms = []
            for op in ins.operands:
                if op.type == X86_OP_IMM:
                    val = op.imm & 0xFFFFFFFF
                    if val in D3D_FORMATS_XBOX:
                        imms.append(D3D_FORMATS_XBOX[val])
                    elif val in GL_FORMATS:
                        imms.append(GL_FORMATS[val])
            if note or imms or ins.address in (0xA220A, 0xA2227, 0xA237B, 0xA23DE):
                extra = ""
                if imms:
                    extra = " ; " + ", ".join(imms)
                lines.append(f"  0x{ins.address:08X}: {ins.mnemonic:<8} {ins.op_str:<44}{note}{extra}")

    lines.append("\nINTERPRETATION")
    lines.append("- Retail keeps GL_DDS1/GL_DDS5 images as compressed formats for image accounting.")
    lines.append("- The 0xA1EB0 helper has a D3DFMT_DXT1 branch and calls XGCompressRect into compressed storage.")
    lines.append("- Therefore an Xbox-compatible path should upload DDS block payloads to D3D DXT textures instead of expanding all DDS assets to RGBA.")
    return "\n".join(lines)


def dds_mip_bytes(fmt, width, height, mipcount):
    total = 0
    w, h = width, height
    levels = max(1, mipcount)
    for _ in range(levels):
        if fmt == "DXT1":
            total += ((w + 3) // 4) * ((h + 3) // 4) * 8
        elif fmt in ("DXT3", "DXT5"):
            total += ((w + 3) // 4) * ((h + 3) // 4) * 16
        elif fmt == "RGB16":
            total += w * h * 2
        elif fmt == "RGBA32":
            total += w * h * 4
        else:
            total += w * h * 4
        w = max(1, w // 2)
        h = max(1, h // 2)
    return total


def parse_dds(path):
    data = path.read_bytes()
    if len(data) < 128 or data[:4] != b"DDS ":
        return None
    h = data[4:128]
    height = struct.unpack_from("<I", h, 8)[0]
    width = struct.unpack_from("<I", h, 12)[0]
    mipcount = struct.unpack_from("<I", h, 24)[0]
    pf_flags = struct.unpack_from("<I", h, 76)[0]
    fourcc = struct.unpack_from("<I", h, 80)[0]
    rgb_bits = struct.unpack_from("<I", h, 84)[0]
    if pf_flags & 0x4:
        fmt = struct.pack("<I", fourcc).decode("ascii", "replace")
    elif rgb_bits == 16:
        fmt = "RGB16"
    elif rgb_bits == 32:
        fmt = "RGBA32"
    else:
        fmt = f"RAW{rgb_bits}"
    return {
        "path": path,
        "width": width,
        "height": height,
        "mipcount": mipcount,
        "format": fmt,
        "file_bytes": len(data),
        "payload_bytes": max(0, len(data) - 128),
        "expected_bytes": dds_mip_bytes(fmt, width, height, mipcount),
    }


def analyze_assets():
    rows = []
    for p in (RETAIL_ROOT / "base").rglob("*.dds"):
        info = parse_dds(p)
        if info:
            rows.append(info)
    by_fmt = collections.defaultdict(list)
    for r in rows:
        by_fmt[r["format"]].append(r)
    lines = ["DDS ASSET STATS", "=" * 80]
    lines.append(f"total DDS files: {len(rows)}")
    total_payload = sum(r["payload_bytes"] for r in rows)
    total_rgba32_base = sum(r["width"] * r["height"] * 4 for r in rows)
    total_rgba32_mips = sum(dds_mip_bytes("RGBA32", r["width"], r["height"], r["mipcount"]) for r in rows)
    lines.append(f"payload bytes on disk: {total_payload:,} ({total_payload/1048576:.2f} MiB)")
    lines.append(f"if expanded to RGBA32 base levels: {total_rgba32_base:,} ({total_rgba32_base/1048576:.2f} MiB)")
    lines.append(f"if expanded to RGBA32 full mip chains: {total_rgba32_mips:,} ({total_rgba32_mips/1048576:.2f} MiB)")
    lines.append("")
    for fmt, group in sorted(by_fmt.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        payload = sum(r["payload_bytes"] for r in group)
        max_dim = max(max(r["width"], r["height"]) for r in group)
        avg_mips = sum(r["mipcount"] for r in group) / len(group)
        lines.append(f"{fmt:<8} count={len(group):5} payload={payload/1048576:8.2f} MiB maxDim={max_dim:4} avgMips={avg_mips:.2f}")
    lines.append("\nLargest DDS payloads:")
    for r in sorted(rows, key=lambda r: r["payload_bytes"], reverse=True)[:80]:
        rel = r["path"].relative_to(RETAIL_ROOT)
        lines.append(f"  {r['payload_bytes']:9} {r['format']:<6} {r['width']:4}x{r['height']:<4} mips={r['mipcount']:<2} {rel}")
    return "\n".join(lines)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    xbe = Xbe(RETAIL_XBE)
    strings = find_ascii_strings(xbe)
    bounds = function_bounds_by_padding(xbe)

    (OUT / "00_sections.txt").write_text(
        "\n".join(
            [
                f"path={xbe.path}",
                f"base=0x{xbe.base:08X}",
                f"entry=0x{xbe.entry:08X}",
                f"kernel_thunk=0x{xbe.kernel_thunk:08X}",
                f"sections={len(xbe.sections)}",
                "",
            ] + [
                f"{s['name']:<12} va=0x{s['va']:08X} vsz=0x{s['vsz']:08X} roff=0x{s['roff']:08X} rsz=0x{s['rsz']:08X} flags=0x{s['flags']:08X}"
                for s in xbe.sections
            ]
        ),
        encoding="utf-8",
    )
    (OUT / "01_cvars.txt").write_text(analyze_cvars(xbe, bounds, strings), encoding="utf-8")
    (OUT / "02_texture_anchors.txt").write_text(analyze_texture_functions(xbe, bounds, strings), encoding="utf-8")
    (OUT / "03_dds_assets.txt").write_text(analyze_assets(), encoding="utf-8")
    (OUT / "04_texture_paths.txt").write_text(analyze_retail_texture_upload_paths(xbe, bounds), encoding="utf-8")
    print(f"Wrote deep retail reports to {OUT}")


if __name__ == "__main__":
    main()
