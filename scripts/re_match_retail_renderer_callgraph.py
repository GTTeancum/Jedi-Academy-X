#!/usr/bin/env python3
"""Correlate changed retail renderer functions with a symbolized local build.

This is deliberately a candidate generator. Exact byte matches and pointer-table
anchors seed the graph; inferred matches are written with their evidence so they
can be reviewed before becoming authoritative function seeds.
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
    r"(?P<symbol>\S+)\s+(?P<address>[0-9A-Fa-f]{8})\s+f\s+(?P<object>\S+)\s*$"
)
RENDERER_OBJECT_RE = re.compile(
    r"^(?:matcomp|tr_.*|win_qgl_dx8|win_glimp_console|win_highdynamicrange|win_lighteffects|xbox_texture_man)\.obj$",
    re.IGNORECASE,
)
DECORATED_NAME_RE = re.compile(r"^\?(?P<name>[^@]+)@@")


@dataclass
class Function:
    address: int
    end: int
    symbol: str
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
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--xbe-analysis", type=Path, required=True)
    parser.add_argument("--retail-functions", type=Path, required=True)
    parser.add_argument("--exact", type=Path, required=True)
    parser.add_argument("--qgl-map", type=Path, required=True)
    parser.add_argument("--refexport-map", type=Path, required=True)
    parser.add_argument("--seed-functions", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--retail-min", type=lambda value: int(value, 0), default=0x70000)
    parser.add_argument("--retail-max", type=lambda value: int(value, 0), default=0xB6300)
    parser.add_argument("--iterations", type=int, default=8)
    return parser.parse_args()


def simple_name(symbol: str) -> str:
    match = DECORATED_NAME_RE.match(symbol)
    if match:
        return match.group("name")
    return symbol.lstrip("_")


def parse_local_map(path: Path) -> tuple[dict[int, Function], dict[str, list[int]]]:
    aliases: dict[int, list[tuple[str, str]]] = defaultdict(list)
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = MAP_FUNCTION_RE.match(line)
        if not match or not RENDERER_OBJECT_RE.match(match.group("object")):
            continue
        address = int(match.group("address"), 16)
        aliases[address].append((match.group("symbol"), match.group("object")))

    addresses = sorted(aliases)
    functions: dict[int, Function] = {}
    names: dict[str, list[int]] = defaultdict(list)
    for index, address in enumerate(addresses):
        symbol, object_name = aliases[address][0]
        end = addresses[index + 1] if index + 1 < len(addresses) else address
        functions[address] = Function(address, end, symbol, object_name)
        for alias, _ in aliases[address]:
            names[simple_name(alias)].append(address)
    return functions, names


def xbe_bytes(path: Path, analysis: dict, address: int, size: int) -> bytes:
    for section in analysis["sections"]:
        start = int(section["virtual_addr"], 16)
        raw_size = int(section["raw_size"])
        if start <= address < start + raw_size:
            raw = int(section["raw_addr"], 16) + address - start
            with path.open("rb") as stream:
                stream.seek(raw)
                return stream.read(min(size, start + raw_size - address))
    return b""


def disassemble_local(pe: pefile.PE, functions: dict[int, Function]) -> None:
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
    for function in functions.values():
        for target in function.calls:
            functions[target].callers.add(function.address)


def load_retail(
    path: Path, xbe: Path, analysis: dict, minimum: int, maximum: int
) -> dict[int, Function]:
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = False
    raw_functions = json.loads(path.read_text(encoding="utf-8"))
    starts = {
        int(item["start"], 16)
        for item in raw_functions
        if minimum <= int(item["start"], 16) < maximum
    }
    result: dict[int, Function] = {}
    for item in raw_functions:
        address = int(item["start"], 16)
        if address not in starts:
            continue
        end = int(item["end"], 16)
        data = xbe_bytes(xbe, analysis, address, int(item["size"])).rstrip(b"\x90\xCC")
        mnemonics = tuple(instruction.mnemonic for instruction in decoder.disasm(data, address))
        calls = {int(target, 16) for target in item.get("calls_to", []) if int(target, 16) in starts}
        result[address] = Function(address, end, item.get("name", ""), "retail", mnemonics, calls)
    for function in result.values():
        for target in function.calls:
            result[target].callers.add(function.address)
    return result


def add_anchor(
    local_to_retail: dict[int, int],
    retail_to_local: dict[int, int],
    local: int,
    retail: int,
) -> bool:
    if local in local_to_retail and local_to_retail[local] != retail:
        return False
    if retail in retail_to_local and retail_to_local[retail] != local:
        return False
    local_to_retail[local] = retail
    retail_to_local[retail] = local
    return True


def seed_anchors(
    args: argparse.Namespace, local_names: dict[str, list[int]]
) -> tuple[dict[int, int], dict[int, int], dict[tuple[int, int], str]]:
    local_to_retail: dict[int, int] = {}
    retail_to_local: dict[int, int] = {}
    methods: dict[tuple[int, int], str] = {}

    for row in csv.DictReader(args.exact.open(newline="", encoding="utf-8")):
        local = int(row["local_address"], 16)
        retail = int(row["retail_start"], 16)
        if add_anchor(local_to_retail, retail_to_local, local, retail):
            methods[(local, retail)] = "normalized_exact"

    named_rows: list[tuple[str, int, str]] = []
    for row in csv.DictReader(args.qgl_map.open(newline="", encoding="utf-8")):
        if row["stub"].lower() == "true":
            continue
        named_rows.append((row["dll"], int(row["retail_target"], 16), "qgl_pointer"))
    for row in csv.DictReader(args.refexport_map.open(newline="", encoding="utf-8")):
        named_rows.append(
            (row["function_name"], int(row["retail_target"], 16), "refexport_pointer")
        )
    for name, retail, method in named_rows:
        matches = sorted(set(local_names.get(name, [])))
        if len(matches) != 1:
            continue
        local = matches[0]
        if add_anchor(local_to_retail, retail_to_local, local, retail):
            methods[(local, retail)] = method
    if args.seed_functions:
        for seed in json.loads(args.seed_functions.read_text(encoding="utf-8")):
            method = str(seed.get("detection_method", ""))
            if method == "unique_source_string":
                continue
            matches = sorted(set(local_names.get(str(seed["name"]), [])))
            if len(matches) != 1:
                continue
            local = matches[0]
            retail = int(str(seed["start"]), 16)
            if add_anchor(local_to_retail, retail_to_local, local, retail):
                methods[(local, retail)] = method
    return local_to_retail, retail_to_local, methods


def sequence_similarity(left: tuple[str, ...], right: tuple[str, ...]) -> float:
    if not left or not right:
        return 0.0
    left_counts = Counter(left)
    right_counts = Counter(right)
    overlap = sum((left_counts & right_counts).values())
    return 2.0 * overlap / (len(left) + len(right))


def size_similarity(left: Function, right: Function) -> float:
    maximum = max(left.size, right.size, 1)
    return math.exp(-2.5 * abs(left.size - right.size) / maximum)


def graph_evidence(
    local: Function,
    retail: Function,
    local_to_retail: dict[int, int],
) -> tuple[int, int, int, int]:
    mapped_callees = {local_to_retail[target] for target in local.calls if target in local_to_retail}
    mapped_callers = {local_to_retail[target] for target in local.callers if target in local_to_retail}
    callee_hits = len(mapped_callees & retail.calls)
    caller_hits = len(mapped_callers & retail.callers)
    callee_misses = len(mapped_callees - retail.calls)
    caller_misses = len(mapped_callers - retail.callers)
    return callee_hits, caller_hits, callee_misses, caller_misses


def candidate_score(
    local: Function,
    retail: Function,
    local_to_retail: dict[int, int],
) -> tuple[float, dict[str, float | int]]:
    callee_hits, caller_hits, callee_misses, caller_misses = graph_evidence(
        local, retail, local_to_retail
    )
    shape = sequence_similarity(local.mnemonics, retail.mnemonics)
    size = size_similarity(local, retail)
    call_count = math.exp(-0.5 * abs(len(local.calls) - len(retail.calls)))
    score = (
        4.0 * callee_hits
        + 3.0 * caller_hits
        - 3.0 * callee_misses
        - 2.0 * caller_misses
        + 1.7 * shape
        + 0.8 * size
        + 0.5 * call_count
    )
    evidence: dict[str, float | int] = {
        "callee_hits": callee_hits,
        "caller_hits": caller_hits,
        "callee_misses": callee_misses,
        "caller_misses": caller_misses,
        "mnemonic_similarity": round(shape, 4),
        "size_similarity": round(size, 4),
        "call_count_similarity": round(call_count, 4),
    }
    return score, evidence


def main() -> int:
    args = parse_args()
    local_functions, local_names = parse_local_map(args.map)
    pe = pefile.PE(str(args.exe), fast_load=True)
    disassemble_local(pe, local_functions)
    analysis = json.loads(args.xbe_analysis.read_text(encoding="utf-8"))
    retail_functions = load_retail(
        args.retail_functions,
        args.xbe,
        analysis,
        args.retail_min,
        args.retail_max,
    )
    local_to_retail, retail_to_local, methods = seed_anchors(args, local_names)

    accepted: list[dict[str, object]] = []
    for iteration in range(1, args.iterations + 1):
        proposals: list[tuple[float, float, int, int, dict[str, float | int]]] = []
        for local_address, local in local_functions.items():
            if local_address in local_to_retail or len(local.mnemonics) < 3:
                continue
            ranked: list[tuple[float, int, dict[str, float | int]]] = []
            for retail_address, retail in retail_functions.items():
                if retail_address in retail_to_local or len(retail.mnemonics) < 3:
                    continue
                score, evidence = candidate_score(local, retail, local_to_retail)
                if int(evidence["callee_hits"]) + int(evidence["caller_hits"]) == 0:
                    continue
                ranked.append((score, retail_address, evidence))
            if not ranked:
                continue
            ranked.sort(reverse=True, key=lambda item: item[0])
            best_score, retail_address, evidence = ranked[0]
            runner_up = ranked[1][0] if len(ranked) > 1 else -999.0
            margin = best_score - runner_up
            hits = int(evidence["callee_hits"]) + int(evidence["caller_hits"])
            misses = int(evidence["callee_misses"]) + int(evidence["caller_misses"])
            shape = float(evidence["mnemonic_similarity"])
            if best_score < 6.0 or margin < 1.75 or misses > hits or (hits == 1 and shape < 0.72):
                continue
            proposals.append((best_score, margin, local_address, retail_address, evidence))

        by_retail: dict[int, list[tuple[float, float, int, int, dict[str, float | int]]]] = defaultdict(list)
        for proposal in proposals:
            by_retail[proposal[3]].append(proposal)
        iteration_accepts = 0
        for proposal in sorted(proposals, reverse=True):
            score, margin, local_address, retail_address, evidence = proposal
            if len(by_retail[retail_address]) != 1:
                continue
            if not add_anchor(local_to_retail, retail_to_local, local_address, retail_address):
                continue
            methods[(local_address, retail_address)] = "callgraph_inferred"
            accepted.append(
                {
                    "iteration": iteration,
                    "retail_start": f"0x{retail_address:08X}",
                    "local_address": f"0x{local_address:08X}",
                    "symbol": local_functions[local_address].symbol,
                    "object": local_functions[local_address].object_name,
                    "score": round(score, 4),
                    "runner_up_margin": round(margin, 4),
                    **evidence,
                }
            )
            iteration_accepts += 1
        if not iteration_accepts:
            break

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "iteration",
        "retail_start",
        "local_address",
        "symbol",
        "object",
        "score",
        "runner_up_margin",
        "callee_hits",
        "caller_hits",
        "callee_misses",
        "caller_misses",
        "mnemonic_similarity",
        "size_similarity",
        "call_count_similarity",
    ]
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(accepted)

    method_counts = Counter(methods.values())
    print(
        f"local={len(local_functions)} retail={len(retail_functions)} "
        f"initial_anchors={len(local_to_retail) - len(accepted)} inferred={len(accepted)} "
        f"methods={dict(sorted(method_counts.items()))} output={args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
