#!/usr/bin/env python3
"""Corroborate object-order renderer candidates with machine-code evidence.

This consumes candidates from re_align_retail_renderer_objects.py and scores
them against the clean JA donor using only already-established anchors,
instruction shape, size, and direct internal call relationships.  It does not
modify the authoritative seed file.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

import capstone
import pefile
from capstone.x86 import X86_OP_IMM


MAP_FUNCTION_RE = re.compile(
    r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+"
    r"(?P<symbol>\S+)\s+(?P<address>[0-9A-Fa-f]{8})\s+f(?:\s+i)?\s+"
    r"(?P<object>\S+)\s*$"
)
DECORATED_NAME_RE = re.compile(r"^\?(?P<name>[^@]+)@@")
CTOR_DTOR_RE = re.compile(r"^\?\?(?P<kind>[01])(?P<class>[^@]+)@@")


@dataclass
class Function:
    address: int
    end: int
    name: str
    object_name: str
    mnemonics: tuple[str, ...] = ()
    calls: set[int] = field(default_factory=set)
    callers: set[int] = field(default_factory=set)

    @property
    def size(self) -> int:
        return max(0, self.end - self.address)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--donor-pe", type=Path, required=True)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--xbe-analysis", type=Path, required=True)
    parser.add_argument("--retail-functions", type=Path, required=True)
    parser.add_argument("--seeds", type=Path, required=True)
    parser.add_argument("--candidates", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--output-seeds", type=Path)
    parser.add_argument("--retail-min", type=lambda value: int(value, 0), default=0x70000)
    parser.add_argument("--retail-max", type=lambda value: int(value, 0), default=0xB6300)
    return parser.parse_args()


def simple_name(symbol: str) -> str:
    ctor = CTOR_DTOR_RE.match(symbol)
    if ctor:
        class_name = ctor.group("class")
        return class_name if ctor.group("kind") == "0" else f"~{class_name}"
    decorated = DECORATED_NAME_RE.match(symbol)
    if decorated:
        return decorated.group("name")
    return symbol.lstrip("_")


def parse_map(path: Path) -> tuple[dict[int, Function], dict[str, list[int]]]:
    aliases: dict[int, list[tuple[str, str]]] = defaultdict(list)
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = MAP_FUNCTION_RE.match(line)
        if not match:
            continue
        address = int(match.group("address"), 16)
        aliases[address].append((match.group("symbol"), match.group("object")))
    addresses = sorted(aliases)
    functions: dict[int, Function] = {}
    names: dict[str, list[int]] = defaultdict(list)
    for index, address in enumerate(addresses):
        symbol, object_name = aliases[address][0]
        end = addresses[index + 1] if index + 1 < len(addresses) else address
        functions[address] = Function(address, end, simple_name(symbol), object_name)
        for alias, _ in aliases[address]:
            names[simple_name(alias)].append(address)
    return functions, names


def disassemble_donor(pe: pefile.PE, functions: dict[int, Function]) -> None:
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    image_base = int(pe.OPTIONAL_HEADER.ImageBase)
    starts = set(functions)
    for function in functions.values():
        if function.size <= 0:
            continue
        data = pe.get_data(function.address - image_base, function.size).rstrip(b"\x90\xCC")
        instructions = list(decoder.disasm(data, function.address))
        function.mnemonics = tuple(instruction.mnemonic for instruction in instructions)
        for instruction in instructions:
            if not instruction.group(capstone.CS_GRP_CALL) or not instruction.operands:
                continue
            operand = instruction.operands[0]
            if operand.type == X86_OP_IMM and int(operand.imm) in starts:
                function.calls.add(int(operand.imm))
    populate_callers(functions)


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


def load_retail(
    path: Path,
    xbe: Path,
    analysis: dict[str, object],
    minimum: int,
    maximum: int,
) -> dict[int, Function]:
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    starts = {
        int(item["start"], 16)
        for item in json.loads(path.read_text(encoding="utf-8"))
        if minimum <= int(item["start"], 16) < maximum
    }
    result: dict[int, Function] = {}
    for item in json.loads(path.read_text(encoding="utf-8")):
        address = int(item["start"], 16)
        if address not in starts:
            continue
        end = int(item["end"], 16)
        data = xbe_bytes(xbe, analysis, address, int(item["size"])).rstrip(b"\x90\xCC")
        mnemonics = tuple(instruction.mnemonic for instruction in decoder.disasm(data, address))
        calls = {int(target, 16) for target in item.get("calls_to", []) if int(target, 16) in starts}
        result[address] = Function(address, end, "", "retail", mnemonics, calls)
    populate_callers(result)
    return result


def populate_callers(functions: dict[int, Function]) -> None:
    for function in functions.values():
        for target in function.calls:
            if target in functions:
                functions[target].callers.add(function.address)


def load_anchor_map(
    path: Path, donor_names: dict[str, list[int]], retail: dict[int, Function]
) -> tuple[dict[int, int], dict[int, int]]:
    donor_to_retail: dict[int, int] = {}
    retail_to_donor: dict[int, int] = {}
    for item in json.loads(path.read_text(encoding="utf-8")):
        if item.get("detection_method") == "unique_source_string":
            continue
        retail_address = int(str(item["start"]), 16)
        if retail_address not in retail:
            continue
        candidates = sorted(set(donor_names.get(str(item["name"]), [])))
        if len(candidates) != 1:
            continue
        donor_address = candidates[0]
        if donor_address in donor_to_retail or retail_address in retail_to_donor:
            continue
        donor_to_retail[donor_address] = retail_address
        retail_to_donor[retail_address] = donor_address
    return donor_to_retail, retail_to_donor


def sequence_similarity(left: tuple[str, ...], right: tuple[str, ...]) -> float:
    if not left or not right:
        return 0.0
    overlap = sum((Counter(left) & Counter(right)).values())
    return 2.0 * overlap / (len(left) + len(right))


def size_similarity(left: Function, right: Function) -> float:
    maximum = max(left.size, right.size, 1)
    return math.exp(-2.5 * abs(left.size - right.size) / maximum)


def evidence_grade(
    hits: int, misses: int, mnemonic: float, size: float, call_delta: int
) -> str:
    if misses == 0 and hits >= 2:
        return "graph_corroborated"
    if misses == 0 and hits >= 1 and mnemonic >= 0.80 and size >= 0.55:
        return "graph_and_shape_corroborated"
    if misses == 0 and hits == 0 and mnemonic >= 0.90 and size >= 0.75 and call_delta == 0:
        return "strong_shape_only"
    if misses > hits and misses >= 2:
        return "graph_contradicted"
    return "unresolved"


def main() -> None:
    args = parse_args()
    donor, donor_names = parse_map(args.map)
    disassemble_donor(pefile.PE(str(args.donor_pe), fast_load=True), donor)
    analysis = json.loads(args.xbe_analysis.read_text(encoding="utf-8"))
    retail = load_retail(
        args.retail_functions, args.xbe, analysis, args.retail_min, args.retail_max
    )
    donor_to_retail, _ = load_anchor_map(args.seeds, donor_names, retail)

    rows: list[dict[str, object]] = []
    for candidate in csv.DictReader(args.candidates.open(newline="", encoding="utf-8")):
        donor_address = int(candidate["donor_address"], 16)
        retail_address = int(candidate["retail_start"], 16)
        donor_function = donor.get(donor_address)
        retail_function = retail.get(retail_address)
        if donor_function is None or retail_function is None:
            continue
        mapped_callees = {
            donor_to_retail[target]
            for target in donor_function.calls
            if target in donor_to_retail
        }
        mapped_callers = {
            donor_to_retail[target]
            for target in donor_function.callers
            if target in donor_to_retail
        }
        callee_hits = len(mapped_callees & retail_function.calls)
        caller_hits = len(mapped_callers & retail_function.callers)
        callee_misses = len(mapped_callees - retail_function.calls)
        caller_misses = len(mapped_callers - retail_function.callers)
        mnemonic = sequence_similarity(donor_function.mnemonics, retail_function.mnemonics)
        size = size_similarity(donor_function, retail_function)
        call_delta = abs(len(donor_function.calls) - len(retail_function.calls))
        hits = callee_hits + caller_hits
        misses = callee_misses + caller_misses
        rows.append(
            {
                **candidate,
                "donor_size": donor_function.size,
                "donor_internal_calls": len(donor_function.calls),
                "retail_internal_calls": len(retail_function.calls),
                "callee_hits": callee_hits,
                "caller_hits": caller_hits,
                "callee_misses": callee_misses,
                "caller_misses": caller_misses,
                "mnemonic_similarity": round(mnemonic, 4),
                "size_similarity": round(size, 4),
                "evidence_grade": evidence_grade(hits, misses, mnemonic, size, call_delta),
            }
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        if rows:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    counts = Counter(str(row["evidence_grade"]) for row in rows)
    promoted = 0
    upgraded = 0
    if args.output_seeds:
        seed_items = json.loads(args.seeds.read_text(encoding="utf-8"))
        seed_by_address = {
            int(str(item["start"]), 16): index
            for index, item in enumerate(seed_items)
        }
        for row in rows:
            if not str(row["evidence_grade"]).startswith("graph"):
                continue
            retail_address = int(str(row["retail_start"]), 16)
            replacement = {
                "start": f"0x{retail_address:08X}",
                "name": row["candidate_name"],
                "confidence": 0.997,
                "detection_method": "object_order_graph_corroborated",
            }
            existing_index = seed_by_address.get(retail_address)
            if existing_index is not None:
                if seed_items[existing_index].get("detection_method") == "unique_source_string":
                    seed_items[existing_index] = replacement
                    upgraded += 1
                continue
            seed_items.append(replacement)
            seed_by_address[retail_address] = len(seed_items) - 1
            promoted += 1
        seed_items.sort(key=lambda item: int(str(item["start"]), 16))
        args.output_seeds.parent.mkdir(parents=True, exist_ok=True)
        args.output_seeds.write_text(json.dumps(seed_items, indent=4) + "\n", encoding="utf-8")
    print(json.dumps({"candidates": len(rows), "grades": counts}, indent=2))
    if args.output_seeds:
        print(f"promoted_seeds={promoted} upgraded_weak_seeds={upgraded}")


if __name__ == "__main__":
    main()
