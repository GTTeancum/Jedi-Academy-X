#!/usr/bin/env python
"""Audit the SP executable's startup and logging shape.

This is a local RE helper so future edits can prove they preserved:
- _WinMainCRTStartup as the real PE entrypoint
- xapi/libc startup before main()
- a minimal import table
- the expected XBLog functions and log path string
"""

from __future__ import print_function

import os
import re
import struct
import sys

try:
    import pefile
    import capstone
except ImportError as exc:
    print("Missing dependency: %s" % exc)
    sys.exit(1)


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DEFAULT_EXE = os.path.join(REPO_ROOT, "code", "x_exe", "Release", "default.exe")
DEFAULT_MAP = os.path.join(REPO_ROOT, "code", "x_exe", "Release", "default.map")
DEFAULT_XBE = os.path.join(REPO_ROOT, "code", "x_exe", "Release", "default.xbe")


def load_map_symbols(map_path):
    symbols = {}
    pattern = re.compile(r"^\s+[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+(\S+)\s+([0-9A-Fa-f]{8})\s")
    with open(map_path, "r") as handle:
        for line in handle:
            match = pattern.match(line)
            if not match:
                continue
            symbol_name = match.group(1)
            symbol_va = int(match.group(2), 16)
            symbols[symbol_name] = symbol_va
    return symbols


def disassemble(pe, start_va, count):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    image = pe.get_memory_mapped_image()
    image_base = pe.OPTIONAL_HEADER.ImageBase
    start_off = start_va - image_base
    lines = []
    for index, insn in enumerate(md.disasm(image[start_off:start_off + 0x180], start_va)):
        lines.append("%08X: %-8s %s" % (insn.address, insn.mnemonic, insn.op_str))
        if index + 1 >= count:
            break
    return lines


def read_c_string(pe, va):
    image = pe.get_memory_mapped_image()
    image_base = pe.OPTIONAL_HEADER.ImageBase
    off = va - image_base
    raw = image[off:off + 256].split(b"\0", 1)[0]
    return raw.decode("ascii", "replace")


def main():
    exe_path = DEFAULT_EXE if len(sys.argv) < 2 else sys.argv[1]
    map_path = DEFAULT_MAP if len(sys.argv) < 3 else sys.argv[2]
    xbe_path = DEFAULT_XBE if len(sys.argv) < 4 else sys.argv[3]

    pe = pefile.PE(exe_path)
    symbols = load_map_symbols(map_path)

    entry_va = pe.OPTIONAL_HEADER.ImageBase + pe.OPTIONAL_HEADER.AddressOfEntryPoint
    imports = [entry.dll.decode("ascii", "replace") for entry in pe.DIRECTORY_ENTRY_IMPORT]

    print("Executable: %s" % exe_path)
    print("Map file:   %s" % map_path)
    print("XBE file:   %s" % xbe_path)
    print("Entry VA:   0x%08X" % entry_va)
    print("Imports:    %s" % ", ".join(imports))
    print("")

    interesting = [
        "_WinMainCRTStartup",
        "_mainCRTStartup",
        "_mainXapiStartup@4",
        "_main",
        "_XBLog_Init",
        "_XBLog_Write",
        "_NtCreateFile@36",
        "_NtWriteFile@32",
        "_NtClose@4",
        "_CreateFileA@28",
        "_WriteFile@20",
        "_OutputDebugStringA@4",
    ]

    for symbol in interesting:
        value = symbols.get(symbol)
        print("%-24s %s" % (symbol, "0x%08X" % value if value else "MISSING"))

    print("")

    failures = []
    if imports != ["xboxkrnl.exe"]:
        failures.append("expected imports to be only xboxkrnl.exe, got %r" % imports)

    expected_entry = symbols.get("_WinMainCRTStartup")
    if expected_entry != entry_va:
        failures.append(
            "expected PE entrypoint 0x%08X to match _WinMainCRTStartup 0x%08X"
            % (entry_va, expected_entry or 0)
        )

    # Verify the NT device path strings are embedded in the binary.
    # g_logPath is now NULL at init time (set dynamically), so we check
    # for the literal strings in the image data instead.
    image = pe.get_memory_mapped_image()
    nt_path_probe = b"\\Device\\Harddisk0\\Partition1\\ja_sp_log.txt"
    ca_path_probe = b"E:\\ja_sp_log.txt"
    if nt_path_probe in image:
        print("NT device path found in binary: OK")
    else:
        failures.append("NT device path %r not found in binary" % nt_path_probe.decode())
    if ca_path_probe in image:
        print("CreateFileA fallback path found in binary: OK")
    else:
        failures.append("CreateFileA fallback path %r not found in binary" % ca_path_probe.decode())
    print("")

    with open(xbe_path, "rb") as handle:
        xbe = handle.read()
    xbe_base = struct.unpack_from("<I", xbe, 0x104)[0]
    xbe_encoded_entry = struct.unpack_from("<I", xbe, 0x128)[0]
    xbe_entry = xbe_encoded_entry ^ 0x94859D4B
    xbe_lib_count = struct.unpack_from("<I", xbe, 0x160)[0]
    xbe_lib_va = struct.unpack_from("<I", xbe, 0x164)[0]
    xbe_lib_off = xbe_lib_va - xbe_base
    xbe_libs = []
    for index in range(xbe_lib_count):
        off = xbe_lib_off + index * 16
        name = xbe[off:off + 8].rstrip(b"\0").decode("ascii", "replace")
        xbe_libs.append(name)

    print("Decoded XBE entry: 0x%08X" % xbe_entry)
    print("XBE libraries:     %s" % ", ".join(xbe_libs))
    print("")

    expected_xbe_entry = 0x00010000 + pe.OPTIONAL_HEADER.AddressOfEntryPoint
    if xbe_entry != expected_xbe_entry:
        failures.append(
            "expected decoded XBE entry 0x%08X, got 0x%08X"
            % (expected_xbe_entry, xbe_entry)
        )

    expected_libs = ["XAPILIB", "LIBC", "D3D8I", "DSOUND", "XBOXKRNL", "XONLINE", "D3D8", "XGRAPHC"]
    if xbe_libs != expected_libs:
        failures.append("expected XBE libraries %r, got %r" % (expected_libs, xbe_libs))

    for label in ("_WinMainCRTStartup", "_mainCRTStartup", "_mainXapiStartup@4", "_main", "_XBLog_Init", "_XBLog_Write"):
        va = symbols.get(label)
        if not va:
            continue
        print("%s:" % label)
        for line in disassemble(pe, va, 18):
            print("  " + line)
        print("")

    if failures:
        print("AUDIT FAILED")
        for failure in failures:
            print("  - %s" % failure)
        sys.exit(2)

    print("AUDIT OK")


if __name__ == "__main__":
    main()
