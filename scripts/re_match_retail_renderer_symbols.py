#!/usr/bin/env python3
"""Donate renderer symbols from a mapped local PE to a retail Xbox XBE."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
from collections import defaultdict
from pathlib import Path

import capstone
import pefile
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG


MAP_FUNCTION_RE = re.compile(
    r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+"
    r"(?P<symbol>\S+)\s+(?P<address>[0-9A-Fa-f]{8})\s+f\s+(?P<object>\S+)\s*$"
)
RENDERER_OBJECT_RE = re.compile(
    r"^(?:matcomp|tr_.*|win_qgl_dx8|win_glimp_console|win_highdynamicrange|win_lighteffects|xbox_texture_man)\.obj$",
    re.IGNORECASE,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--xbe-analysis", type=Path, required=True)
    parser.add_argument("--retail-functions", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--retail-min", type=lambda value: int(value, 0), default=0x70000)
    parser.add_argument("--retail-max", type=lambda value: int(value, 0), default=0xB6300)
    parser.add_argument(
        "--object-regex",
        default=RENDERER_OBJECT_RE.pattern,
        help="Regular expression selecting donor object files from the linker map",
    )
    return parser.parse_args()


def parse_map(path: Path, object_regex: re.Pattern[str]) -> list[dict[str, object]]:
    functions: list[dict[str, object]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = MAP_FUNCTION_RE.match(line)
        if not match or not object_regex.match(match.group("object")):
            continue
        functions.append(
            {
                "symbol": match.group("symbol"),
                "address": int(match.group("address"), 16),
                "object": match.group("object"),
            }
        )
    functions.sort(key=lambda item: int(item["address"]))
    for index, function in enumerate(functions):
        next_address = int(functions[index + 1]["address"]) if index + 1 < len(functions) else 0
        function["end"] = next_address
    return functions


def xbe_bytes(path: Path, analysis: dict, address: int, size: int) -> bytes:
    for section in analysis["sections"]:
        start = int(section["virtual_addr"], 16)
        raw_size = int(section["raw_size"])
        if start <= address < start + raw_size:
            available = start + raw_size - address
            raw = int(section["raw_addr"], 16) + address - start
            with path.open("rb") as stream:
                stream.seek(raw)
                return stream.read(min(size, available))
    return b""


def normalize_instruction(instruction: capstone.CsInsn) -> str:
    operands: list[str] = []
    for operand in instruction.operands:
        if operand.type == X86_OP_REG:
            operands.append(instruction.reg_name(operand.reg))
        elif operand.type == X86_OP_IMM:
            value = int(operand.imm)
            if instruction.group(capstone.CS_GRP_CALL) or instruction.group(capstone.CS_GRP_JUMP):
                operands.append("target")
            elif -0x10000 <= value <= 0x10000:
                operands.append(f"imm:{value}")
            else:
                operands.append("imm:address")
        elif operand.type == X86_OP_MEM:
            memory = operand.mem
            base = instruction.reg_name(memory.base) if memory.base else "abs"
            index = instruction.reg_name(memory.index) if memory.index else "none"
            displacement = int(memory.disp)
            if base == "abs" or not -0x1000 <= displacement <= 0x1000:
                displacement_text = "address"
            else:
                displacement_text = str(displacement)
            operands.append(f"mem:{base}:{index}:{memory.scale}:{displacement_text}")
        else:
            operands.append(f"type:{operand.type}")
    return instruction.mnemonic + (" " + ",".join(operands) if operands else "")


def signature(disassembler: capstone.Cs, data: bytes, address: int) -> tuple[str, int, str]:
    data = data.rstrip(b"\x90\xCC")
    normalized = [normalize_instruction(insn) for insn in disassembler.disasm(data, address)]
    payload = "\n".join(normalized).encode("utf-8")
    digest = hashlib.sha256(payload).hexdigest()
    mnemonic_digest = hashlib.sha256(
        "\n".join(item.split(" ", 1)[0] for item in normalized).encode("utf-8")
    ).hexdigest()
    return digest, len(normalized), mnemonic_digest


def main() -> int:
    args = parse_args()
    pe = pefile.PE(str(args.exe), fast_load=True)
    image_base = int(pe.OPTIONAL_HEADER.ImageBase)
    object_regex = re.compile(args.object_regex, re.IGNORECASE)
    local_functions = parse_map(args.map, object_regex)
    retail_analysis = json.loads(args.xbe_analysis.read_text(encoding="utf-8"))
    retail_functions = json.loads(args.retail_functions.read_text(encoding="utf-8"))
    disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    disassembler.detail = True

    local_by_signature: dict[str, list[dict[str, object]]] = defaultdict(list)
    for function in local_functions:
        start = int(function["address"])
        end = int(function["end"])
        if end <= start:
            continue
        rva = start - image_base
        data = pe.get_data(rva, end - start)
        digest, count, mnemonic_digest = signature(disassembler, data, start)
        function.update(
            {
                "signature": digest,
                "instruction_count": count,
                "mnemonic_signature": mnemonic_digest,
            }
        )
        if count >= 3:
            local_by_signature[digest].append(function)

    retail_by_signature: dict[str, list[dict[str, object]]] = defaultdict(list)
    for function in retail_functions:
        start = int(function["start"], 16)
        if not args.retail_min <= start < args.retail_max:
            continue
        size = int(function["size"])
        data = xbe_bytes(args.xbe, retail_analysis, start, size)
        digest, count, mnemonic_digest = signature(disassembler, data, start)
        function = dict(function)
        function.update(
            {
                "signature": digest,
                "decoded_instruction_count": count,
                "mnemonic_signature": mnemonic_digest,
            }
        )
        if count >= 3:
            retail_by_signature[digest].append(function)

    rows: list[dict[str, object]] = []
    for digest, local_matches in local_by_signature.items():
        retail_matches = retail_by_signature.get(digest, [])
        if len(local_matches) != 1 or len(retail_matches) != 1:
            continue
        local = local_matches[0]
        retail = retail_matches[0]
        rows.append(
            {
                "retail_start": retail["start"],
                "retail_end": retail["end"],
                "retail_size": retail["size"],
                "symbol": local["symbol"],
                "object": local["object"],
                "local_address": f"0x{int(local['address']):08X}",
                "instruction_count": local["instruction_count"],
                "match_method": "normalized_exact_unique",
            }
        )

    rows.sort(key=lambda item: int(str(item["retail_start"]), 16))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "retail_start",
        "retail_end",
        "retail_size",
        "symbol",
        "object",
        "local_address",
        "instruction_count",
        "match_method",
    ]
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(
        f"local_functions={len(local_functions)} retail_candidates={sum(len(v) for v in retail_by_signature.values())} "
        f"unique_matches={len(rows)} output={args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
