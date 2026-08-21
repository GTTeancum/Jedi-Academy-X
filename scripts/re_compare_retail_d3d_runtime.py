#!/usr/bin/env python3
"""Compare named D3D runtime functions between two Xbox executables.

The Cxbx symbol cache supplies function starts. This tool removes relocated
absolute addresses while retaining control flow, constants, and object-field
offsets so XDK/QFE implementation differences remain visible.
"""

from __future__ import annotations

import argparse
import configparser
import difflib
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path

from capstone import CS_ARCH_X86, CS_GRP_JUMP, CS_GRP_RET, CS_MODE_32, Cs
from capstone.x86 import X86_INS_JMP, X86_OP_IMM, X86_OP_MEM, X86_OP_REG


RETAIL_EP_KEY = 0xA8FC57AB
DEBUG_EP_KEY = 0x94859D4B
RUNTIME_PREFIXES = (
    "D3D",
    "CDevice",
    "CMiniport",
    "CBaseTexture",
    "CPushBuffer",
    "CResource",
    "CSurface",
    "CTexture",
    "CVertexBuffer",
    "CVolumeTexture",
    "XG",
)
HOT_WORDS = (
    "BeginPush",
    "EndPush",
    "MakeSpace",
    "KickOff",
    "SetState",
    "SetTexture",
    "SetStream",
    "Draw",
    "Swap",
    "Present",
    "BlockUntil",
    "Wait",
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
        section_headers_va = struct.unpack_from("<I", self.data, 0x120)[0]
        section_headers_offset = section_headers_va - self.base
        self.sections: list[Section] = []
        for index in range(section_count):
            offset = section_headers_offset + index * 0x38
            flags = struct.unpack_from("<I", self.data, offset)[0]
            virtual_address, virtual_size, raw_address, raw_size = struct.unpack_from(
                "<IIII", self.data, offset + 4
            )
            self.sections.append(
                Section(flags, virtual_address, virtual_size, raw_address, raw_size)
            )

    def va_to_offset(self, address: int) -> int:
        for section in self.sections:
            if section.virtual_address <= address < (
                section.virtual_address + section.virtual_size
            ):
                return section.raw_address + address - section.virtual_address
        raise ValueError(f"VA 0x{address:08X} is outside {self.path}")

    def contains_va(self, address: int) -> bool:
        return any(
            section.virtual_address
            <= address
            < section.virtual_address + section.virtual_size
            for section in self.sections
        )

    def contains_executable_va(self, address: int) -> bool:
        return any(
            section.flags & 0x4
            and section.virtual_address
            <= address
            < section.virtual_address + section.virtual_size
            for section in self.sections
        )

    def executable_section_end(self, address: int) -> int:
        for section in self.sections:
            if (
                section.flags & 0x4
                and section.virtual_address
                <= address
                < section.virtual_address + section.virtual_size
            ):
                return section.virtual_address + section.virtual_size
        raise ValueError(f"VA 0x{address:08X} is outside executable sections in {self.path}")

    def read(self, address: int, size: int) -> bytes:
        offset = self.va_to_offset(address)
        return self.data[offset : offset + size]


def normalize_symbol_name(name: str) -> str:
    """Convert XDK linker decorations to the Cxbx symbol-cache spelling."""
    if name.startswith("?"):
        match = re.match(r"\?([^@]+)@([^@]+)@D3D@@", name)
        if match:
            function, scope = match.groups()
            if scope == "CDevice":
                function = {
                    "LazySetStateUP": "SetStateUP",
                    "LazySetStateVB": "SetStateVB",
                }.get(function, function)
            return f"{scope}_{function}"
        match = re.match(r"\?([^@]+)@D3D@@", name)
        if match:
            return f"D3D_{match.group(1)}"
        return name

    name = name.lstrip("_")
    return re.sub(r"@\d+$", "", name)


def load_symbol_cache(path: Path) -> dict[str, int]:
    parser = configparser.ConfigParser()
    parser.optionxform = str
    parser.read(path)
    if "Symbols" not in parser:
        raise ValueError(f"symbol cache has no [Symbols] section: {path}")
    return {
        normalize_symbol_name(name): int(value, 0)
        for name, value in parser["Symbols"].items()
    }


def load_linker_map(path: Path, xbe: Xbe) -> dict[str, int]:
    symbols: dict[str, int] = {}
    pattern = re.compile(
        r"^\s+([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})\s+"
        r"(\S+)\s+([0-9A-Fa-f]{8})(?:\s|$)"
    )
    for line in path.read_text(encoding="latin-1").splitlines():
        match = pattern.match(line)
        if not match:
            continue
        section_text, offset_text, raw_name, _address_text = match.groups()
        section_index = int(section_text, 16) - 1
        if not 0 <= section_index < len(xbe.sections):
            continue
        name = normalize_symbol_name(raw_name)
        address = (
            xbe.sections[section_index].virtual_address + int(offset_text, 16)
        )
        symbols.setdefault(name, address)
    if not symbols:
        raise ValueError(f"linker map has no public symbols: {path}")
    return symbols


def load_symbols(path: Path, xbe: Xbe | None = None) -> dict[str, int]:
    if path.suffix.lower() == ".map":
        if xbe is None:
            raise ValueError("an XBE is required when loading linker-map symbols")
        return load_linker_map(path, xbe)
    return load_symbol_cache(path)


def function_end(address: int, symbols: dict[str, int], cap: int) -> int:
    later = sorted({value for value in symbols.values() if value > address})
    return min(address + cap, later[0] if later else address + cap)


def normalize_immediate(
    value: int,
    function_start: int,
    function_end_address: int,
    address_to_names: dict[int, list[str]],
) -> str:
    value &= 0xFFFFFFFF
    if value < 0x10000:
        return f"0x{value:X}"
    if function_start <= value < function_end_address:
        return f"local+0x{value - function_start:X}"
    names = address_to_names.get(value)
    if names:
        return "sym:" + sorted(names)[0]
    return "abs"


def normalize_function(
    xbe: Xbe,
    symbols: dict[str, int],
    name: str,
    cap: int,
) -> tuple[list[str], list[str], int]:
    start = symbols[name]
    end = min(
        function_end(start, symbols, cap),
        xbe.executable_section_end(start),
    )
    address_to_names: dict[int, list[str]] = {}
    for symbol_name, address in symbols.items():
        address_to_names.setdefault(address, []).append(symbol_name)

    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    decoded: dict[int, tuple[str, str, int]] = {}
    pending = [start]
    while pending:
        address = pending.pop()
        while start <= address < end and address not in decoded:
            instruction_list = list(
                decoder.disasm(xbe.read(address, min(16, end - address)), address, 1)
            )
            if not instruction_list:
                break
            instruction = instruction_list[0]
            instruction_end = instruction.address + instruction.size
            if instruction_end > end:
                break

            operands: list[str] = []
            shape_operands: list[str] = []
            for operand in instruction.operands:
                if operand.type == X86_OP_REG:
                    register = instruction.reg_name(operand.reg)
                    operands.append(register)
                    shape_operands.append(register)
                elif operand.type == X86_OP_IMM:
                    immediate = normalize_immediate(
                        operand.imm, start, end, address_to_names
                    )
                    operands.append(immediate)
                    shape_operands.append(
                        "address" if (operand.imm & 0xFFFFFFFF) >= 0x10000 else immediate
                    )
                elif operand.type == X86_OP_MEM:
                    memory = operand.mem
                    base = instruction.reg_name(memory.base) if memory.base else ""
                    index = instruction.reg_name(memory.index) if memory.index else ""
                    if (
                        not base
                        and not index
                        and (memory.disp & 0xFFFFFFFF) >= 0x10000
                    ):
                        displacement = "abs"
                    else:
                        displacement = f"{memory.disp:+#x}"
                    operands.append(
                        f"mem({base},{index},{memory.scale},{displacement})"
                    )
                    shape_displacement = (
                        displacement if base in ("esp", "ebp") else "field"
                    )
                    shape_operands.append(
                        f"mem({base},{index},{memory.scale},{shape_displacement})"
                    )
                else:
                    operands.append(f"op{operand.type}")
                    shape_operands.append(f"op{operand.type}")
            decoded[instruction.address] = (
                instruction.mnemonic + " " + ",".join(operands),
                instruction.mnemonic + " " + ",".join(shape_operands),
                instruction.size,
            )

            is_jump = instruction.group(CS_GRP_JUMP)
            direct_target = None
            if is_jump and instruction.operands and instruction.operands[0].type == X86_OP_IMM:
                direct_target = instruction.operands[0].imm & 0xFFFFFFFF
                if (
                    start <= direct_target < end
                    and xbe.contains_executable_va(direct_target)
                    and direct_target not in decoded
                ):
                    pending.append(direct_target)
            if instruction.group(CS_GRP_RET) or instruction.mnemonic in ("int3", "hlt"):
                break
            if instruction.id == X86_INS_JMP or (is_jump and direct_target is None):
                break
            address = instruction_end

    tokens = [decoded[address][0] for address in sorted(decoded)]
    shape_tokens = [decoded[address][1] for address in sorted(decoded)]
    size = max(
        (address + decoded[address][2] - start for address in decoded),
        default=0,
    )
    return tokens, shape_tokens, size


def hot_score(name: str) -> int:
    score = 0
    for index, word in enumerate(HOT_WORDS):
        if word.lower() in name.lower():
            score += 100 - index
    if name.startswith("D3DDevice_"):
        score += 25
    if name.startswith("CDevice_"):
        score += 20
    return score


def compare(args: argparse.Namespace) -> dict[str, object]:
    left_xbe = Xbe(args.left_xbe)
    right_xbe = Xbe(args.right_xbe)
    left_symbols = load_symbols(args.left_symbols, left_xbe)
    right_symbols = load_symbols(args.right_symbols, right_xbe)
    common = sorted(
        name
        for name in set(left_symbols) & set(right_symbols)
        if name.startswith(RUNTIME_PREFIXES)
        and left_xbe.contains_executable_va(left_symbols[name])
        and right_xbe.contains_executable_va(right_symbols[name])
    )

    rows: list[dict[str, object]] = []
    for name in common:
        left_tokens, left_shape, left_size = normalize_function(
            left_xbe, left_symbols, name, args.function_cap
        )
        right_tokens, right_shape, right_size = normalize_function(
            right_xbe, right_symbols, name, args.function_cap
        )
        ratio = difflib.SequenceMatcher(
            None, left_tokens, right_tokens, autojunk=False
        ).ratio()
        shape_ratio = difflib.SequenceMatcher(
            None, left_shape, right_shape, autojunk=False
        ).ratio()
        rows.append(
            {
                "name": name,
                "left_address": left_symbols[name],
                "right_address": right_symbols[name],
                "left_size": left_size,
                "right_size": right_size,
                "left_instructions": len(left_tokens),
                "right_instructions": len(right_tokens),
                "similarity": round(ratio, 6),
                "shape_similarity": round(shape_ratio, 6),
                "exact": left_tokens == right_tokens,
                "shape_exact": left_shape == right_shape,
                "hot_score": hot_score(name),
            }
        )

    changed = [row for row in rows if not row["exact"]]
    changed.sort(
        key=lambda row: (
            -int(row["hot_score"]),
            bool(row["shape_exact"]),
            float(row["shape_similarity"]),
            float(row["similarity"]),
            -max(int(row["left_size"]), int(row["right_size"])),
            str(row["name"]),
        )
    )
    return {
        "left_xbe": str(args.left_xbe),
        "right_xbe": str(args.right_xbe),
        "left_symbols": str(args.left_symbols),
        "right_symbols": str(args.right_symbols),
        "common_runtime_symbols": len(common),
        "normalized_exact": len(rows) - len(changed),
        "changed": len(changed),
        "rows": rows,
        "ranked_changes": changed,
    }


def dump_named_function(args: argparse.Namespace, name: str) -> dict[str, object]:
    left_xbe = Xbe(args.left_xbe)
    right_xbe = Xbe(args.right_xbe)
    left_symbols = load_symbols(args.left_symbols, left_xbe)
    right_symbols = load_symbols(args.right_symbols, right_xbe)
    if name not in left_symbols:
        raise ValueError(f"{name} is absent from {args.left_symbols}")
    if name not in right_symbols:
        raise ValueError(f"{name} is absent from {args.right_symbols}")

    left_tokens, left_shape, left_size = normalize_function(
        left_xbe, left_symbols, name, args.function_cap
    )
    right_tokens, right_shape, right_size = normalize_function(
        right_xbe, right_symbols, name, args.function_cap
    )
    detail_diff = list(
        difflib.unified_diff(
            left_tokens,
            right_tokens,
            fromfile=f"left:{name}",
            tofile=f"right:{name}",
            lineterm="",
        )
    )
    shape_diff = list(
        difflib.unified_diff(
            left_shape,
            right_shape,
            fromfile=f"left-shape:{name}",
            tofile=f"right-shape:{name}",
            lineterm="",
        )
    )
    return {
        "name": name,
        "left_address": left_symbols[name],
        "right_address": right_symbols[name],
        "left_size": left_size,
        "right_size": right_size,
        "left_tokens": left_tokens,
        "right_tokens": right_tokens,
        "detail_diff": detail_diff,
        "shape_diff": shape_diff,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--left-xbe", type=Path, required=True)
    parser.add_argument("--left-symbols", type=Path, required=True)
    parser.add_argument("--right-xbe", type=Path, required=True)
    parser.add_argument("--right-symbols", type=Path, required=True)
    parser.add_argument("--function-cap", type=lambda value: int(value, 0), default=0x4000)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--top", type=int, default=50)
    parser.add_argument("--dump-name")
    args = parser.parse_args()

    if args.dump_name:
        result = dump_named_function(args, args.dump_name)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="ascii")
        print(
            f"{result['name']}: {result['left_size']}/{result['right_size']} bytes, "
            f"{len(result['left_tokens'])}/{len(result['right_tokens'])} instructions"
        )
        for line in result["detail_diff"]:
            print(line)
        return

    result = compare(args)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="ascii")

    print(
        "common={common_runtime_symbols} exact={normalized_exact} changed={changed}".format(
            **result
        )
    )
    print("score similarity shape sizes name")
    for row in result["ranked_changes"][: args.top]:
        print(
            f"{row['hot_score']:5d} {row['similarity']:.3f} "
            f"{row['shape_similarity']:.3f} "
            f"{row['left_size']:5d}/{row['right_size']:5d} {row['name']}"
        )


if __name__ == "__main__":
    main()
