#!/usr/bin/env python3
"""Print one bounded x86 function from an XBE analysis or mapped PE donor."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

import capstone
import pefile


MAP_FUNCTION_RE = re.compile(
    r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+"
    r"(?P<symbol>\S+)\s+(?P<address>[0-9A-Fa-f]{8})\s+f\s+(?P<object>\S+)\s*$"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--start", type=lambda value: int(value, 0))
    parser.add_argument("--end", type=lambda value: int(value, 0))
    parser.add_argument("--functions", type=Path)
    parser.add_argument("--xbe-analysis", type=Path)
    parser.add_argument("--map", type=Path)
    parser.add_argument("--name")
    return parser.parse_args()


def mapped_function(path: Path, name: str) -> tuple[int, int, str]:
    functions: list[tuple[int, str]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = MAP_FUNCTION_RE.match(line)
        if match:
            functions.append((int(match.group("address"), 16), match.group("symbol")))
    functions.sort()
    matches = [index for index, (_, symbol) in enumerate(functions) if name in symbol]
    if len(matches) != 1:
        raise SystemExit(f"expected one map match for {name!r}, found {len(matches)}")
    index = matches[0]
    start, symbol = functions[index]
    if index + 1 >= len(functions):
        raise SystemExit(f"map has no following boundary for {symbol}")
    return start, functions[index + 1][0], symbol


def retail_function(path: Path, start: int) -> tuple[int, int, str]:
    functions = json.loads(path.read_text(encoding="utf-8"))
    for function in functions:
        if int(function["start"], 16) == start:
            return start, int(function["end"], 16), function.get("name", "")
    raise SystemExit(f"retail function 0x{start:08X} was not found")


def xbe_bytes(binary: Path, analysis_path: Path, start: int, end: int) -> bytes:
    analysis = json.loads(analysis_path.read_text(encoding="utf-8"))
    for section in analysis["sections"]:
        section_start = int(section["virtual_addr"], 16)
        raw_size = int(section["raw_size"])
        if section_start <= start and end <= section_start + raw_size:
            raw_start = int(section["raw_addr"], 16) + start - section_start
            with binary.open("rb") as stream:
                stream.seek(raw_start)
                return stream.read(end - start)
    raise SystemExit(f"range 0x{start:08X}-0x{end:08X} is not in XBE raw data")


def main() -> int:
    args = parse_args()
    symbol = ""
    if args.map and args.name:
        start, end, symbol = mapped_function(args.map, args.name)
    elif args.functions and args.start is not None:
        start, end, symbol = retail_function(args.functions, args.start)
    elif args.start is not None and args.end is not None:
        start, end = args.start, args.end
    else:
        raise SystemExit("use --map/--name, --functions/--start, or --start/--end")

    if args.xbe_analysis:
        data = xbe_bytes(args.binary, args.xbe_analysis, start, end)
    else:
        pe = pefile.PE(str(args.binary), fast_load=True)
        data = pe.get_data(start - int(pe.OPTIONAL_HEADER.ImageBase), end - start)

    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    print(f"{symbol or 'function'} 0x{start:08X}-0x{end:08X} ({end - start} bytes)")
    for instruction in decoder.disasm(data.rstrip(b"\x90\xCC"), start):
        print(
            f"0x{instruction.address:08X}  {instruction.bytes.hex():<20} "
            f"{instruction.mnemonic:<9} {instruction.op_str}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
