#!/usr/bin/env python3
"""Resolve duplicate exact renderer signatures with monotonic object anchors.

The strict exact matcher intentionally rejects signatures that occur more than
once.  VC71 emits many identical STL and inline helper bodies, so this pass
uses already-proven retail/donor anchor pairs to constrain a duplicate to the
same object interval.  It promotes only a single surviving donor function or
multiple surviving functions that all share the same simplified name.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
from bisect import bisect_left
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

import capstone
import pefile
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG


MAP_FUNCTION_RE = re.compile(
    r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+"
    r"(?P<symbol>\S+)\s+(?P<address>[0-9A-Fa-f]{8})\s+f(?:\s+i)?\s+"
    r"(?P<object>\S+)\s*$"
)
RENDERER_OBJECT_RE = re.compile(
    r"^(?:matcomp|tr_.*|win_qgl_dx8|win_glimp_console|win_highdynamicrange|win_lighteffects|xbox_texture_man)\.obj$",
    re.IGNORECASE,
)
DECORATED_NAME_RE = re.compile(r"^\?(?P<name>[^@]+)@@")
CTOR_DTOR_RE = re.compile(r"^\?\?(?P<kind>[01])(?P<class>[^@]+)@@")
WEAK_METHODS = {"unique_source_string"}


@dataclass
class DonorFunction:
    address: int
    end: int
    symbol: str
    name: str
    object_name: str
    signature: str = ""
    instruction_count: int = 0


@dataclass
class Anchor:
    retail_address: int
    donor_address: int
    name: str
    object_name: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--donor-pe", type=Path, required=True)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--xbe-analysis", type=Path, required=True)
    parser.add_argument("--retail-functions", type=Path, required=True)
    parser.add_argument("--seeds", type=Path, required=True)
    parser.add_argument("--anchors", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--output-seeds", type=Path)
    parser.add_argument("--retail-min", type=lambda value: int(value, 0), default=0x70000)
    parser.add_argument("--retail-max", type=lambda value: int(value, 0), default=0xB6300)
    return parser.parse_args()


def simple_name(symbol: str) -> str:
    constructor = CTOR_DTOR_RE.match(symbol)
    if constructor:
        class_name = constructor.group("class")
        return class_name if constructor.group("kind") == "0" else f"~{class_name}"
    decorated = DECORATED_NAME_RE.match(symbol)
    if decorated:
        return decorated.group("name")
    return symbol.lstrip("_")


def parse_donor_map(path: Path) -> list[DonorFunction]:
    aliases: dict[int, list[tuple[str, str]]] = defaultdict(list)
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = MAP_FUNCTION_RE.match(line)
        if not match or not RENDERER_OBJECT_RE.match(match.group("object")):
            continue
        symbol = match.group("symbol")
        if symbol.startswith("$L"):
            continue
        aliases[int(match.group("address"), 16)].append((symbol, match.group("object")))

    addresses = sorted(aliases)
    functions: list[DonorFunction] = []
    for index, address in enumerate(addresses):
        symbol, object_name = aliases[address][0]
        end = addresses[index + 1] if index + 1 < len(addresses) else address
        functions.append(DonorFunction(address, end, symbol, simple_name(symbol), object_name))
    return functions


def load_anchors(path: Path) -> list[Anchor]:
    anchors: list[Anchor] = []
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            if str(row.get("monotonic_chain", "")).lower() != "true":
                continue
            anchors.append(
                Anchor(
                    int(row["retail_start"], 16),
                    int(row["donor_address"], 16),
                    row["name"],
                    row["object"],
                )
            )
    return sorted(anchors, key=lambda anchor: anchor.retail_address)


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
            displacement_text = (
                "address" if base == "abs" or not -0x1000 <= displacement <= 0x1000 else str(displacement)
            )
            operands.append(f"mem:{base}:{index}:{memory.scale}:{displacement_text}")
        else:
            operands.append(f"type:{operand.type}")
    return instruction.mnemonic + (" " + ",".join(operands) if operands else "")


def signature(disassembler: capstone.Cs, data: bytes, address: int) -> tuple[str, int]:
    data = data.rstrip(b"\x90\xCC")
    normalized = [normalize_instruction(item) for item in disassembler.disasm(data, address)]
    digest = hashlib.sha256("\n".join(normalized).encode("utf-8")).hexdigest()
    return digest, len(normalized)


def xbe_bytes(path: Path, analysis: dict[str, object], address: int, size: int) -> bytes:
    for section in analysis["sections"]:
        start = int(section["virtual_addr"], 16)
        raw_size = int(section["raw_size"])
        if start <= address < start + raw_size:
            raw = int(section["raw_addr"], 16) + address - start
            with path.open("rb") as stream:
                stream.seek(raw)
                return stream.read(min(size, start + raw_size - address))
    return b""


def anchor_interval(
    address: int, anchors: list[Anchor], anchor_addresses: list[int]
) -> tuple[Anchor, Anchor] | None:
    index = bisect_left(anchor_addresses, address)
    if index == 0 or index >= len(anchors):
        return None
    left = anchors[index - 1]
    right = anchors[index]
    if left.object_name != right.object_name:
        return None
    return left, right


def main() -> int:
    args = parse_args()
    disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    disassembler.detail = True
    donor_pe = pefile.PE(str(args.donor_pe), fast_load=True)
    image_base = int(donor_pe.OPTIONAL_HEADER.ImageBase)
    donor_functions = parse_donor_map(args.map)
    donor_by_signature: dict[str, list[DonorFunction]] = defaultdict(list)
    for function in donor_functions:
        if function.end <= function.address:
            continue
        data = donor_pe.get_data(function.address - image_base, function.end - function.address)
        function.signature, function.instruction_count = signature(
            disassembler, data, function.address
        )
        if function.instruction_count >= 3:
            donor_by_signature[function.signature].append(function)

    analysis = json.loads(args.xbe_analysis.read_text(encoding="utf-8"))
    raw_retail = json.loads(args.retail_functions.read_text(encoding="utf-8"))
    seeds = json.loads(args.seeds.read_text(encoding="utf-8"))
    seeds_by_address = {int(item["start"], 16): item for item in seeds}
    anchors = load_anchors(args.anchors)
    anchor_addresses = [anchor.retail_address for anchor in anchors]

    rows: list[dict[str, object]] = []
    for item in raw_retail:
        address = int(item["start"], 16)
        if not args.retail_min <= address < args.retail_max or address in seeds_by_address:
            continue
        data = xbe_bytes(args.xbe, analysis, address, int(item["size"]))
        digest, instruction_count = signature(disassembler, data, address)
        matches = donor_by_signature.get(digest, [])
        if len(matches) < 2 or instruction_count < 3:
            continue
        interval = anchor_interval(address, anchors, anchor_addresses)
        if not interval:
            continue
        left, right = interval
        constrained = [
            match
            for match in matches
            if match.object_name == left.object_name
            and left.donor_address < match.address < right.donor_address
        ]
        if not constrained:
            continue
        names = {match.name for match in constrained}
        if len(constrained) != 1 and len(names) != 1:
            continue
        chosen = constrained[0]
        resolution = "single_exact_duplicate_in_anchor_interval"
        if len(constrained) > 1:
            resolution = "same_name_exact_duplicates_in_anchor_interval"
        rows.append(
            {
                "retail_start": f"0x{address:08X}",
                "retail_end": item["end"],
                "name": chosen.name,
                "symbol": chosen.symbol,
                "object": chosen.object_name,
                "donor_address": f"0x{chosen.address:08X}",
                "global_duplicate_count": len(matches),
                "interval_duplicate_count": len(constrained),
                "instruction_count": instruction_count,
                "left_anchor": left.name,
                "right_anchor": right.name,
                "resolution": resolution,
            }
        )

    rows.sort(key=lambda row: int(row["retail_start"], 16))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "retail_start",
        "retail_end",
        "name",
        "symbol",
        "object",
        "donor_address",
        "global_duplicate_count",
        "interval_duplicate_count",
        "instruction_count",
        "left_anchor",
        "right_anchor",
        "resolution",
    ]
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    promoted = 0
    if args.output_seeds:
        for row in rows:
            address = int(row["retail_start"], 16)
            existing = seeds_by_address.get(address)
            if existing and existing.get("detection_method") not in WEAK_METHODS:
                continue
            value = {
                "start": f"0x{address:08X}",
                "name": row["name"],
                "confidence": 0.995,
                "detection_method": "retail_finalbuild_duplicate_exact_order_corroborated",
            }
            if existing:
                existing.update(value)
            else:
                seeds.append(value)
                seeds_by_address[address] = value
            promoted += 1
        seeds.sort(key=lambda item: int(item["start"], 16))
        args.output_seeds.write_text(json.dumps(seeds, indent=4) + "\n", encoding="utf-8")

    print(
        f"donor_functions={len(donor_functions)} duplicate_exact_resolutions={len(rows)} "
        f"promoted={promoted}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
