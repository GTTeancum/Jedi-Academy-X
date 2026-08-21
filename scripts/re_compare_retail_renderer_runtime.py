#!/usr/bin/env python3
"""Compare the current renderer with named functions in the retail JA MP XBE.

The local linker map and the retail RE ledger provide exact function ranges.
Absolute addresses are normalized, while constants, stack offsets, object-field
offsets, calls, and control-flow shape remain visible.
"""

from __future__ import annotations

import argparse
import csv
import difflib
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path

from capstone import CS_ARCH_X86, CS_GRP_JUMP, CS_GRP_RET, CS_MODE_32, Cs
from capstone.x86 import X86_INS_JMP, X86_OP_IMM, X86_OP_MEM, X86_OP_REG


MAP_FUNCTION_RE = re.compile(
    r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+"
    r"(?P<symbol>\S+)\s+(?P<address>[0-9A-Fa-f]{8})\s+f\s+(?P<object>\S+)\s*$"
)
RENDERER_OBJECT_RE = re.compile(
    r"^(?:matcomp|tr_.*|win_qgl_dx8|win_glimp_console|win_highdynamicrange|"
    r"win_lighteffects|xbox_texture_man)\.obj$",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class Section:
    flags: int
    virtual_address: int
    virtual_size: int
    raw_address: int
    raw_size: int


class Xbe:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        if self.data[:4] != b"XBEH":
            raise ValueError(f"not an XBE: {path}")
        self.base = struct.unpack_from("<I", self.data, 0x104)[0]
        section_count = struct.unpack_from("<I", self.data, 0x11C)[0]
        headers_va = struct.unpack_from("<I", self.data, 0x120)[0]
        headers_offset = headers_va - self.base
        self.sections: list[Section] = []
        for index in range(section_count):
            offset = headers_offset + index * 0x38
            flags = struct.unpack_from("<I", self.data, offset)[0]
            va, size, raw, raw_size = struct.unpack_from("<IIII", self.data, offset + 4)
            self.sections.append(Section(flags, va, size, raw, raw_size))

    def read(self, address: int, size: int) -> bytes:
        for section in self.sections:
            if section.virtual_address <= address < section.virtual_address + section.virtual_size:
                offset = section.raw_address + address - section.virtual_address
                return self.data[offset : offset + size]
        raise ValueError(f"VA 0x{address:08X} is outside {self.path}")

    def executable(self, address: int) -> bool:
        return any(
            section.flags & 0x4
            and section.virtual_address <= address < section.virtual_address + section.virtual_size
            for section in self.sections
        )


@dataclass(frozen=True)
class Function:
    name: str
    start: int
    end: int
    object_name: str = ""
    priority: float = 0.0


def canonical_symbol(symbol: str) -> str:
    if symbol.startswith("?"):
        return symbol[1:].split("@@", 1)[0]
    if symbol.startswith("_") and not symbol.startswith("__"):
        return symbol[1:]
    return symbol


def load_current_map(
    path: Path, image_shift: int, object_re: re.Pattern[str]
) -> dict[str, Function]:
    raw: list[tuple[str, int, str]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = MAP_FUNCTION_RE.match(line)
        if match:
            raw.append((match.group("symbol"), int(match.group("address"), 16), match.group("object")))
    raw.sort(key=lambda item: item[1])
    result: dict[str, Function] = {}
    for index, (symbol, address, object_name) in enumerate(raw):
        if not object_re.match(object_name) or index + 1 >= len(raw):
            continue
        name = canonical_symbol(symbol)
        function = Function(name, address - image_shift, raw[index + 1][1] - image_shift, object_name)
        previous = result.get(name)
        if previous is None or function.end - function.start > previous.end - previous.start:
            result[name] = function
    return result


def load_retail_ledger(path: Path) -> dict[str, Function]:
    result: dict[str, Function] = {}
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            if row.get("named") != "yes" or not row.get("name"):
                continue
            function = Function(
                row["name"],
                int(row["retail_start"], 16),
                int(row["retail_end"], 16),
                row.get("object_context", ""),
                float(row.get("priority_score") or 0.0),
            )
            result[function.name] = function
    return result


def normalize_immediate(value: int, function: Function, names: dict[int, str]) -> str:
    value &= 0xFFFFFFFF
    if value < 0x10000:
        return f"0x{value:X}"
    if function.start <= value < function.end:
        return f"local+0x{value - function.start:X}"
    return "sym:" + names[value] if value in names else "abs"


def normalize_function(
    xbe: Xbe, function: Function, names: dict[int, str]
) -> tuple[list[str], list[str], list[str], int]:
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    decoded: dict[int, tuple[str, str, str, int]] = {}
    pending = [function.start]
    while pending:
        address = pending.pop()
        while function.start <= address < function.end and address not in decoded:
            instructions = list(decoder.disasm(xbe.read(address, 16), address, 1))
            if not instructions:
                break
            instruction = instructions[0]
            instruction_end = instruction.address + instruction.size
            if instruction_end > function.end:
                break
            operands: list[str] = []
            shape_operands: list[str] = []
            structure_operands: list[str] = []
            for operand in instruction.operands:
                if operand.type == X86_OP_REG:
                    register = instruction.reg_name(operand.reg)
                    operands.append(register)
                    shape_operands.append(register)
                    structure_operands.append("reg")
                elif operand.type == X86_OP_IMM:
                    immediate = normalize_immediate(operand.imm, function, names)
                    operands.append(immediate)
                    shape_operands.append(
                        "address" if (operand.imm & 0xFFFFFFFF) >= 0x10000 else immediate
                    )
                    if function.start <= (operand.imm & 0xFFFFFFFF) < function.end:
                        structure_operands.append("local")
                    elif (operand.imm & 0xFFFFFFFF) in names:
                        structure_operands.append("symbol")
                    else:
                        structure_operands.append("imm")
                elif operand.type == X86_OP_MEM:
                    memory = operand.mem
                    base = instruction.reg_name(memory.base) if memory.base else ""
                    index = instruction.reg_name(memory.index) if memory.index else ""
                    if not base and not index and (memory.disp & 0xFFFFFFFF) >= 0x10000:
                        displacement = "abs"
                    else:
                        displacement = f"{memory.disp:+#x}"
                    operands.append(f"mem({base},{index},{memory.scale},{displacement})")
                    shape_displacement = displacement if base in ("esp", "ebp") else "field"
                    shape_operands.append(f"mem({base},{index},{memory.scale},{shape_displacement})")
                    structure_operands.append("mem")
                else:
                    operands.append(f"op{operand.type}")
                    shape_operands.append(f"op{operand.type}")
                    structure_operands.append(f"op{operand.type}")
            decoded[instruction.address] = (
                instruction.mnemonic + " " + ",".join(operands),
                instruction.mnemonic + " " + ",".join(shape_operands),
                instruction.mnemonic + " " + ",".join(structure_operands),
                instruction.size,
            )
            is_jump = instruction.group(CS_GRP_JUMP)
            direct_target = None
            if is_jump and instruction.operands and instruction.operands[0].type == X86_OP_IMM:
                direct_target = instruction.operands[0].imm & 0xFFFFFFFF
                if function.start <= direct_target < function.end and xbe.executable(direct_target):
                    pending.append(direct_target)
            if instruction.group(CS_GRP_RET) or instruction.mnemonic in ("int3", "hlt"):
                break
            if instruction.id == X86_INS_JMP or (is_jump and direct_target is None):
                break
            address = instruction_end
    ordered = sorted(decoded)
    tokens = [decoded[address][0] for address in ordered]
    shapes = [decoded[address][1] for address in ordered]
    structures = [decoded[address][2] for address in ordered]
    byte_count = max((address + decoded[address][3] - function.start for address in ordered), default=0)
    return tokens, shapes, structures, byte_count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--retail-xbe", type=Path, required=True)
    parser.add_argument("--retail-ledger", type=Path, required=True)
    parser.add_argument("--current-xbe", type=Path, required=True)
    parser.add_argument("--current-map", type=Path, required=True)
    parser.add_argument("--image-shift", type=lambda value: int(value, 0), default=0x3F0000)
    parser.add_argument(
        "--object-regex",
        default=RENDERER_OBJECT_RE.pattern,
        help="regular expression selecting current-map object files",
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    retail_xbe = Xbe(args.retail_xbe)
    current_xbe = Xbe(args.current_xbe)
    retail = load_retail_ledger(args.retail_ledger)
    current = load_current_map(
        args.current_map, args.image_shift, re.compile(args.object_regex, re.IGNORECASE)
    )
    retail_names = {function.start: name for name, function in retail.items()}
    current_names = {function.start: name for name, function in current.items()}
    rows: list[dict[str, object]] = []
    for name in sorted(set(retail) & set(current)):
        retail_tokens, retail_shapes, retail_structures, retail_bytes = normalize_function(
            retail_xbe, retail[name], retail_names
        )
        current_tokens, current_shapes, current_structures, current_bytes = normalize_function(
            current_xbe, current[name], current_names
        )
        detail_ratio = difflib.SequenceMatcher(None, retail_tokens, current_tokens, autojunk=False).ratio()
        shape_ratio = difflib.SequenceMatcher(None, retail_shapes, current_shapes, autojunk=False).ratio()
        structure_ratio = difflib.SequenceMatcher(
            None, retail_structures, current_structures, autojunk=False
        ).ratio()
        rows.append(
            {
                "name": name,
                "object": current[name].object_name,
                "retail_start": f"0x{retail[name].start:08X}",
                "current_start": f"0x{current[name].start:08X}",
                "retail_bytes": retail_bytes,
                "current_bytes": current_bytes,
                "retail_instructions": len(retail_tokens),
                "current_instructions": len(current_tokens),
                "detail_ratio": round(detail_ratio, 6),
                "shape_ratio": round(shape_ratio, 6),
                "structure_ratio": round(structure_ratio, 6),
                "priority": retail[name].priority,
                "difference_score": round((1.0 - shape_ratio) * 1000.0 + retail[name].priority, 3),
                "retail_only": [line for line in difflib.ndiff(retail_tokens, current_tokens) if line.startswith("- ")][:16],
                "current_only": [line for line in difflib.ndiff(retail_tokens, current_tokens) if line.startswith("+ ")][:16],
            }
        )
    rows.sort(key=lambda row: (-float(row["difference_score"]), str(row["name"])))
    payload = {
        "summary": {
            "retail_named": len(retail),
            "current_named": len(current),
            "common": len(rows),
            "shape_exact": sum(float(row["shape_ratio"]) == 1.0 for row in rows),
            "structure_exact": sum(float(row["structure_ratio"]) == 1.0 for row in rows),
            "detail_exact": sum(float(row["detail_ratio"]) == 1.0 for row in rows),
        },
        "functions": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(json.dumps(payload["summary"], sort_keys=True))
    for row in rows[:30]:
        print(
            f"{row['name']:<36} structure={row['structure_ratio']:.3f} "
            f"shape={row['shape_ratio']:.3f} detail={row['detail_ratio']:.3f} "
            f"insn={row['retail_instructions']}/{row['current_instructions']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
