#!/usr/bin/env python
"""
Compare two XBE files field-by-field per the XBEIMAGE_HEADER struct in
xbox/private/inc/xbeimage.h.  Used to find structural differences between
shipped retail JKA XBE (works) and our build's XBE (CreateDevice hangs).

Output: side-by-side report of every header field.  Highlights deltas.
"""
import struct
import sys
from pathlib import Path

# XOR keys for entry-point and kernel-thunk encryption.
# Per xbox/private/sdktools/imagebld.new/imagebld.cpp:1450-1453, the keys are
# derived at sign time from the public key data.  For test-signed XBEs (which
# both builds are), the well-known fixed test keys apply:
EP_KEY_TEST  = 0x94859D4B  # Entry point XOR key
KT_KEY_TEST  = 0xEFB1F152  # Kernel thunk XOR key
# Retail (commercially signed) would be different; we're test-signed both ways.

# XBE header field map (offset → (name, format, size))
# Per xbox/private/inc/xbeimage.h _XBEIMAGE_HEADER struct
HEADER_FIELDS = [
    (0x000, "Signature",                  "4s",  4),
    (0x004, "EncryptedDigest",            "256s", 256),  # Skip in compare
    (0x104, "BaseAddress",                "<I",  4),
    (0x108, "SizeOfHeaders",              "<I",  4),
    (0x10C, "SizeOfImage",                "<I",  4),
    (0x110, "SizeOfImageHeader",          "<I",  4),
    (0x114, "TimeDateStamp",              "<I",  4),
    (0x118, "Certificate (VA)",           "<I",  4),
    (0x11C, "NumberOfSections",           "<I",  4),
    (0x120, "SectionHeaders (VA)",        "<I",  4),
    (0x124, "InitFlags",                  "<I",  4),
    (0x128, "AddressOfEntryPoint (XOR)",  "<I",  4),
    (0x12C, "TlsDirectory (VA)",          "<I",  4),
    (0x130, "SizeOfStackCommit",          "<I",  4),
    (0x134, "SizeOfHeapReserve",          "<I",  4),
    (0x138, "SizeOfHeapCommit",           "<I",  4),
    (0x13C, "NtBaseOfDll (PEBaseAddress)","<I",  4),
    (0x140, "NtSizeOfImage",              "<I",  4),
    (0x144, "NtCheckSum",                 "<I",  4),
    (0x148, "NtTimeDateStamp",            "<I",  4),
    (0x14C, "DebugPathName (VA)",         "<I",  4),
    (0x150, "DebugFileName (VA)",         "<I",  4),
    (0x154, "DebugUnicodeFileName (VA)",  "<I",  4),
    (0x158, "XboxKernelThunkData (XOR)",  "<I",  4),
    (0x15C, "ImportDirectory (VA)",       "<I",  4),
    (0x160, "NumberOfLibraryVersions",    "<I",  4),
    (0x164, "LibraryVersions (VA)",       "<I",  4),
    (0x168, "XboxKernelLibVer (VA)",      "<I",  4),
    (0x16C, "XapiLibVer (VA)",            "<I",  4),
    (0x170, "MicrosoftLogo (VA)",         "<I",  4),
    (0x174, "SizeOfMicrosoftLogo",        "<I",  4),
]

# XBE InitFlags bit definitions (per xbeimage.h)
INIT_FLAGS = [
    (0x00000001, "MOUNT_UTILITY_DRIVE"),
    (0x00000002, "FORMAT_UTILITY_DRIVE"),
    (0x00000004, "LIMIT_64MB"),
    (0x00000008, "DONT_SETUP_HARDDISK"),
]


def parse_header(path):
    data = Path(path).read_bytes()
    if data[:4] != b"XBEH":
        raise ValueError(f"{path}: not an XBE (magic={data[:4]!r})")
    header = {}
    for offset, name, fmt, size in HEADER_FIELDS:
        if name == "EncryptedDigest":
            continue  # Skip; differs by signing
        val = struct.unpack_from(fmt, data, offset)[0]
        header[name] = val
    return data, header


def decode_init_flags(flags):
    set_bits = []
    for bit, name in INIT_FLAGS:
        if flags & bit:
            set_bits.append(name)
    return ", ".join(set_bits) if set_bits else "(none)"


def va_to_off(data, va, base):
    """Convert virtual address to file offset using section table."""
    if va == 0:
        return None
    # Sections are at SectionHeaders(VA), each 0x38 bytes
    num_sec = struct.unpack_from("<I", data, 0x11C)[0]
    sec_va  = struct.unpack_from("<I", data, 0x120)[0]
    sec_off = sec_va - base
    for i in range(num_sec):
        s = sec_off + i * 0x38
        if s + 0x38 > len(data):
            break
        flags    = struct.unpack_from("<I", data, s + 0x00)[0]
        sec_va_i = struct.unpack_from("<I", data, s + 0x04)[0]
        sec_sz   = struct.unpack_from("<I", data, s + 0x08)[0]
        raw_off  = struct.unpack_from("<I", data, s + 0x0C)[0]
        raw_sz   = struct.unpack_from("<I", data, s + 0x10)[0]
        if sec_va_i <= va < sec_va_i + max(sec_sz, raw_sz):
            return raw_off + (va - sec_va_i)
    return None


def read_lib_versions(data, header):
    base = header["BaseAddress"]
    libs_va = header["LibraryVersions (VA)"]
    libs_off = libs_va - base
    n = header["NumberOfLibraryVersions"]
    out = []
    for i in range(n):
        e = libs_off + i * 16
        name  = data[e:e+8].rstrip(b"\x00").decode("ascii", errors="replace")
        major = struct.unpack_from("<H", data, e + 8)[0]
        minor = struct.unpack_from("<H", data, e + 10)[0]
        build = struct.unpack_from("<H", data, e + 12)[0]
        flags = struct.unpack_from("<H", data, e + 14)[0]
        qfe   = flags & 0x1FFF
        appr  = (flags >> 13) & 0x3
        dbg   = (flags >> 15) & 0x1
        out.append((name, major, minor, build, qfe, appr, dbg))
    return out


def read_section_table(data, header):
    base = header["BaseAddress"]
    n = header["NumberOfSections"]
    sec_va = header["SectionHeaders (VA)"]
    sec_off = sec_va - base
    sections = []
    for i in range(n):
        s = sec_off + i * 0x38
        flags    = struct.unpack_from("<I", data, s + 0x00)[0]
        va       = struct.unpack_from("<I", data, s + 0x04)[0]
        vsize    = struct.unpack_from("<I", data, s + 0x08)[0]
        raw_off  = struct.unpack_from("<I", data, s + 0x0C)[0]
        raw_sz   = struct.unpack_from("<I", data, s + 0x10)[0]
        name_va  = struct.unpack_from("<I", data, s + 0x14)[0]
        name_off = name_va - base
        name = ""
        if 0 <= name_off < len(data):
            end = data.find(b"\x00", name_off)
            if end > name_off:
                name = data[name_off:end].decode("ascii", errors="replace")
        sections.append((name, flags, va, vsize, raw_off, raw_sz))
    return sections


def section_flags_str(flags):
    """Decode XBE section flags per xbeimage.h"""
    bits = []
    if flags & 0x00000001: bits.append("WRITABLE")
    if flags & 0x00000002: bits.append("PRELOAD")
    if flags & 0x00000004: bits.append("EXECUTABLE")
    if flags & 0x00000008: bits.append("INSERTED_FILE")
    if flags & 0x00000010: bits.append("HEAD_PAGE_RO")
    if flags & 0x00000020: bits.append("TAIL_PAGE_RO")
    return "|".join(bits) if bits else "(none)"


def read_kernel_thunks(data, header, label):
    """Read kernel thunk table (array of ordinals, terminated by 0).
    The XboxKernelThunkData VA is XOR'd with KT_KEY_TEST."""
    raw = header["XboxKernelThunkData (XOR)"]
    decoded = raw ^ KT_KEY_TEST
    base = header["BaseAddress"]
    if not (base <= decoded < base + header["SizeOfImage"]):
        # Try other key just in case
        return None, None
    off = decoded - base
    ordinals = []
    while off + 4 <= len(data):
        v = struct.unpack_from("<I", data, off)[0]
        if v == 0:
            break
        # Ordinal imports have high bit set: 0x80000000 | ordinal
        ordinals.append(v & 0x7FFFFFFF)
        off += 4
    return decoded, ordinals


def main():
    a_path = "Star Wars Jedi Academy game/default.xbe"
    b_path = "code/x_exe/Release/default.xbe"

    a_data, a_hdr = parse_header(a_path)
    b_data, b_hdr = parse_header(b_path)

    print("=" * 96)
    print(f"{'FIELD':<32}  {'SHIPPED (works)':<22}  {'OURS (hangs)':<22}  DELTA")
    print("=" * 96)

    diffs = []
    for offset, name, fmt, size in HEADER_FIELDS:
        if name in ("Signature", "EncryptedDigest"):
            continue
        a = a_hdr[name]
        b = b_hdr[name]
        delta = "DIFFER" if a != b else ""
        if delta:
            diffs.append(name)
        if isinstance(a, int):
            print(f"{name:<32}  0x{a:08X} ({a:>10d})  0x{b:08X} ({b:>10d})  {delta}")
        else:
            print(f"{name:<32}  {a!r:<22}  {b!r:<22}  {delta}")

    print()
    print(f"InitFlags decoded:")
    print(f"  Shipped: {decode_init_flags(a_hdr['InitFlags'])}")
    print(f"  Ours:    {decode_init_flags(b_hdr['InitFlags'])}")

    print()
    print("=" * 96)
    print("LIBRARY VERSION TABLE")
    print("=" * 96)
    a_libs = read_lib_versions(a_data, a_hdr)
    b_libs = read_lib_versions(b_data, b_hdr)
    print(f"\nShipped ({len(a_libs)} entries):")
    for name, maj, mnr, bld, qfe, appr, dbg in a_libs:
        appr_str = ["unapproved", "conditional", "approved", "?"][appr]
        print(f"  {name:<10} v{maj}.{mnr} build={bld:<5} qfe={qfe} approved={appr_str} debug={dbg}")
    print(f"\nOurs ({len(b_libs)} entries):")
    for name, maj, mnr, bld, qfe, appr, dbg in b_libs:
        appr_str = ["unapproved", "conditional", "approved", "?"][appr]
        print(f"  {name:<10} v{maj}.{mnr} build={bld:<5} qfe={qfe} approved={appr_str} debug={dbg}")

    a_names = {l[0] for l in a_libs}
    b_names = {l[0] for l in b_libs}
    print(f"\nIn shipped but not ours:  {sorted(a_names - b_names) or '(none)'}")
    print(f"In ours but not shipped:  {sorted(b_names - a_names) or '(none)'}")

    print()
    print("=" * 96)
    print("SECTION TABLE")
    print("=" * 96)
    a_secs = read_section_table(a_data, a_hdr)
    b_secs = read_section_table(b_data, b_hdr)
    print(f"\nShipped ({len(a_secs)} sections):")
    for name, flags, va, vsize, raw_off, raw_sz in a_secs:
        print(f"  {name:<12} VA=0x{va:08X} VSize=0x{vsize:08X} RawSz=0x{raw_sz:08X} Flags={section_flags_str(flags)}")
    print(f"\nOurs ({len(b_secs)} sections):")
    for name, flags, va, vsize, raw_off, raw_sz in b_secs:
        print(f"  {name:<12} VA=0x{va:08X} VSize=0x{vsize:08X} RawSz=0x{raw_sz:08X} Flags={section_flags_str(flags)}")
    a_sec_names = {s[0] for s in a_secs}
    b_sec_names = {s[0] for s in b_secs}
    print(f"\nIn shipped but not ours:  {sorted(a_sec_names - b_sec_names) or '(none)'}")
    print(f"In ours but not shipped:  {sorted(b_sec_names - a_sec_names) or '(none)'}")

    print()
    print("=" * 96)
    print("KERNEL THUNK TABLE")
    print("=" * 96)
    a_thunk_va, a_ordinals = read_kernel_thunks(a_data, a_hdr, "shipped")
    b_thunk_va, b_ordinals = read_kernel_thunks(b_data, b_hdr, "ours")
    a_va_str = f"0x{a_thunk_va:08X}" if a_thunk_va else "(unreadable)"
    b_va_str = f"0x{b_thunk_va:08X}" if b_thunk_va else "(unreadable)"
    a_count_str = str(len(a_ordinals)) if a_ordinals is not None else "(unreadable)"
    b_count_str = str(len(b_ordinals)) if b_ordinals is not None else "(unreadable)"
    print(f"\nShipped: thunk VA={a_va_str}  count={a_count_str}")
    print(f"Ours:    thunk VA={b_va_str}  count={b_count_str}")
    if a_ordinals and b_ordinals:
        print(f"\nFirst 30 ordinals (out of {len(a_ordinals)}/{len(b_ordinals)}):")
        for i in range(min(30, max(len(a_ordinals), len(b_ordinals)))):
            a_ord = f"{a_ordinals[i]:>4}" if i < len(a_ordinals) else "  --"
            b_ord = f"{b_ordinals[i]:>4}" if i < len(b_ordinals) else "  --"
            mark = " " if a_ord == b_ord else "*"
            print(f"  [{i:>3}] shipped={a_ord}  ours={b_ord}  {mark}")
        a_set = set(a_ordinals)
        b_set = set(b_ordinals)
        print(f"\nOrdinals in shipped but not ours: {sorted(a_set - b_set)}")
        print(f"Ordinals in ours but not shipped: {sorted(b_set - a_set)}")

    print()
    print("=" * 96)
    print("SUMMARY OF HEADER DELTAS")
    print("=" * 96)
    for d in diffs:
        print(f"  - {d}")


if __name__ == "__main__":
    main()
