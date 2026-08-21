#!/usr/bin/env python3
"""Align divergent retail renderer intervals against the FinalBuild donor.

Known monotonic anchors are hard boundaries.  Dynamic programming aligns only
the functions between two anchors from the same object and emits evidence for
review; it never mutates the authoritative seed ledger.
"""

from __future__ import annotations

import argparse
import csv
import difflib
import json
import math
import re
from collections import Counter, defaultdict
from dataclasses import dataclass, field
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
    r"^(?:matcomp|tr_.*|win_qgl_dx8|win_glimp_console|"
    r"win_highdynamicrange|win_lighteffects|xbox_texture_man)\.obj$",
    re.IGNORECASE,
)
DECORATED_NAME_RE = re.compile(r"^\?(?P<name>[^@]+)@@")
CTOR_DTOR_RE = re.compile(r"^\?\?(?P<kind>[01])(?P<class>[^@]+)@@")


@dataclass
class Function:
    address: int
    end: int
    symbol: str
    name: str
    object_name: str
    mnemonics: tuple[str, ...] = ()
    instruction_tokens: tuple[str, ...] = ()
    calls: set[int] = field(default_factory=set)
    callers: set[int] = field(default_factory=set)

    @property
    def size(self) -> int:
        return max(0, self.end - self.address)


@dataclass(frozen=True)
class Anchor:
    retail: int
    donor: int
    name: str
    object_name: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--xbe-analysis", type=Path, required=True)
    parser.add_argument("--retail-functions", type=Path, required=True)
    parser.add_argument("--anchors", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--retail-min", type=lambda value: int(value, 0), default=0x71700)
    parser.add_argument("--retail-max", type=lambda value: int(value, 0), default=0xB6300)
    parser.add_argument("--gap-penalty", type=float, default=-1.0)
    parser.add_argument("--include-transitions", action="store_true")
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


def parse_donor_map(path: Path) -> dict[int, Function]:
    aliases: dict[int, list[tuple[str, str]]] = defaultdict(list)
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = MAP_FUNCTION_RE.match(line)
        if not match or not RENDERER_OBJECT_RE.match(match.group("object")):
            continue
        if match.group("symbol").startswith("$L"):
            continue
        aliases[int(match.group("address"), 16)].append(
            (match.group("symbol"), match.group("object"))
        )

    addresses = sorted(aliases)
    functions: dict[int, Function] = {}
    for index, address in enumerate(addresses):
        symbol, object_name = aliases[address][0]
        end = addresses[index + 1] if index + 1 < len(addresses) else address
        functions[address] = Function(
            address, end, symbol, simple_name(symbol), object_name
        )
    return functions


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
        function.instruction_tokens = tuple(
            instruction_token(instruction) for instruction in instructions
        )
        for instruction in instructions:
            if not instruction.group(capstone.CS_GRP_CALL) or not instruction.operands:
                continue
            operand = instruction.operands[0]
            if operand.type == X86_OP_IMM and int(operand.imm) in starts:
                function.calls.add(int(operand.imm))
    for function in functions.values():
        for target in function.calls:
            functions[target].callers.add(function.address)


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


def load_retail(
    path: Path, xbe: Path, analysis: dict, minimum: int, maximum: int
) -> dict[int, Function]:
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    starts: set[int] = set()
    raw = json.loads(path.read_text(encoding="utf-8"))
    for item in raw:
        address = int(item["start"], 16)
        if minimum <= address < maximum:
            starts.add(address)

    result: dict[int, Function] = {}
    for item in raw:
        address = int(item["start"], 16)
        if address not in starts:
            continue
        end = int(item["end"], 16)
        data = xbe_bytes(xbe, analysis, address, int(item["size"])).rstrip(b"\x90\xCC")
        instructions = list(decoder.disasm(data, address))
        mnemonics = tuple(instruction.mnemonic for instruction in instructions)
        tokens = tuple(instruction_token(instruction) for instruction in instructions)
        calls = {
            int(target, 16)
            for target in item.get("calls_to", [])
            if int(target, 16) in starts
        }
        result[address] = Function(
            address, end, str(item.get("name", "")), str(item.get("name", "")),
            "retail", mnemonics, tokens, calls
        )
    for function in result.values():
        for target in function.calls:
            result[target].callers.add(function.address)
    return result


def load_anchors(path: Path) -> list[Anchor]:
    anchors: list[Anchor] = []
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            if row.get("monotonic_chain", "").lower() != "true":
                continue
            anchors.append(
                Anchor(
                    int(row["retail_start"], 16),
                    int(row["donor_address"], 16),
                    row["name"],
                    row["object"],
                )
            )
    return sorted(anchors, key=lambda anchor: anchor.retail)


def normalized_immediate(value: int) -> str:
    return str(value) if -0x1000 <= value <= 0x1000 else "abs"


def instruction_token(instruction: capstone.CsInsn) -> str:
    operands: list[str] = []
    is_control_target = instruction.group(capstone.CS_GRP_CALL) or instruction.group(
        capstone.CS_GRP_JUMP
    )
    for operand in instruction.operands:
        if operand.type == X86_OP_REG:
            operands.append(f"r:{instruction.reg_name(operand.reg)}")
        elif operand.type == X86_OP_IMM:
            operands.append(
                "i:target" if is_control_target else f"i:{normalized_immediate(int(operand.imm))}"
            )
        elif operand.type == X86_OP_MEM:
            base = instruction.reg_name(operand.mem.base) if operand.mem.base else ""
            index = instruction.reg_name(operand.mem.index) if operand.mem.index else ""
            displacement = normalized_immediate(int(operand.mem.disp))
            operands.append(
                f"m:{base}:{index}:{int(operand.mem.scale)}:{displacement}"
            )
        else:
            operands.append(f"t:{operand.type}")
    return instruction.mnemonic + "|" + ",".join(operands)


def sequence_similarity(left: Function, right: Function) -> float:
    if not left.instruction_tokens or not right.instruction_tokens:
        return 0.0
    return difflib.SequenceMatcher(
        None, left.instruction_tokens, right.instruction_tokens, autojunk=False
    ).ratio()


def exp_similarity(left: int, right: int, scale: float) -> float:
    maximum = max(left, right, 1)
    return math.exp(-scale * abs(left - right) / maximum)


def pair_evidence(
    donor: Function,
    retail: Function,
    donor_to_retail: dict[int, int],
) -> dict[str, float | int]:
    shape = sequence_similarity(donor, retail)
    size = exp_similarity(donor.size, retail.size, 2.5)
    call_count = math.exp(-0.5 * abs(len(donor.calls) - len(retail.calls)))
    mapped_callees = {
        donor_to_retail[target] for target in donor.calls if target in donor_to_retail
    }
    mapped_callers = {
        donor_to_retail[target] for target in donor.callers if target in donor_to_retail
    }
    callee_hits = len(mapped_callees & retail.calls)
    caller_hits = len(mapped_callers & retail.callers)
    callee_misses = len(mapped_callees - retail.calls)
    caller_misses = len(mapped_callers - retail.callers)
    score = (
        3.5 * shape
        + 1.3 * size
        + 0.5 * call_count
        + 3.0 * callee_hits
        + 2.5 * caller_hits
        - 2.0 * callee_misses
        - 1.5 * caller_misses
        - 2.6
    )
    return {
        "score": score,
        "mnemonic_similarity": shape,
        "size_similarity": size,
        "call_count_similarity": call_count,
        "callee_hits": callee_hits,
        "caller_hits": caller_hits,
        "callee_misses": callee_misses,
        "caller_misses": caller_misses,
    }


def align_interval(
    donors: list[Function],
    retails: list[Function],
    donor_to_retail: dict[int, int],
    gap_penalty: float,
) -> list[tuple[Function, Function, dict[str, float | int]]]:
    rows = len(donors) + 1
    columns = len(retails) + 1
    scores = [[-math.inf] * columns for _ in range(rows)]
    steps = [[""] * columns for _ in range(rows)]
    scores[0][0] = 0.0
    for left in range(1, rows):
        scores[left][0] = scores[left - 1][0] + gap_penalty
        steps[left][0] = "D"
    for right in range(1, columns):
        scores[0][right] = scores[0][right - 1] + gap_penalty
        steps[0][right] = "R"

    evidence: dict[tuple[int, int], dict[str, float | int]] = {}
    for left in range(1, rows):
        for right in range(1, columns):
            pair = pair_evidence(donors[left - 1], retails[right - 1], donor_to_retail)
            evidence[(left - 1, right - 1)] = pair
            choices = [
                (scores[left - 1][right - 1] + float(pair["score"]), "M"),
                (scores[left - 1][right] + gap_penalty, "D"),
                (scores[left][right - 1] + gap_penalty, "R"),
            ]
            scores[left][right], steps[left][right] = max(choices, key=lambda item: item[0])

    aligned: list[tuple[Function, Function, dict[str, float | int]]] = []
    left = len(donors)
    right = len(retails)
    while left or right:
        step = steps[left][right]
        if step == "M":
            aligned.append((donors[left - 1], retails[right - 1], evidence[(left - 1, right - 1)]))
            left -= 1
            right -= 1
        elif step == "D":
            left -= 1
        elif step == "R":
            right -= 1
        else:
            raise RuntimeError(f"alignment traceback failed at {left},{right}")
    return list(reversed(aligned))


def confidence_tier(
    evidence: dict[str, float | int],
    donor_margin: float,
    retail_margin: float,
    reciprocal_best: bool,
) -> str:
    hits = int(evidence["callee_hits"]) + int(evidence["caller_hits"])
    misses = int(evidence["callee_misses"]) + int(evidence["caller_misses"])
    shape = float(evidence["mnemonic_similarity"])
    size = float(evidence["size_similarity"])
    if (
        reciprocal_best
        and misses == 0
        and shape >= 0.72
        and size >= 0.35
        and ((donor_margin >= 0.5 and retail_margin >= 0.5) or hits >= 2)
    ):
        return "strong_review_candidate"
    if reciprocal_best and misses <= hits and shape >= 0.55 and size >= 0.2:
        return "moderate_navigation_candidate"
    return "weak_navigation_only"


def main() -> int:
    args = parse_args()
    donor = parse_donor_map(args.map)
    disassemble_donor(pefile.PE(str(args.exe), fast_load=True), donor)
    analysis = json.loads(args.xbe_analysis.read_text(encoding="utf-8"))
    retail = load_retail(
        args.retail_functions, args.xbe, analysis, args.retail_min, args.retail_max
    )
    anchors = load_anchors(args.anchors)
    donor_to_retail = {anchor.donor: anchor.retail for anchor in anchors}
    donor_addresses = sorted(donor)
    retail_addresses = sorted(retail)
    donor_indexes = {address: index for index, address in enumerate(donor_addresses)}
    retail_indexes = {address: index for index, address in enumerate(retail_addresses)}

    output_rows: list[dict[str, object]] = []
    interval_count = 0
    for left, right in zip(anchors, anchors[1:]):
        if left.object_name != right.object_name and not args.include_transitions:
            continue
        if left.donor not in donor_indexes or right.donor not in donor_indexes:
            continue
        if left.retail not in retail_indexes or right.retail not in retail_indexes:
            continue
        donor_gap = [
            donor[address]
            for address in donor_addresses[
                donor_indexes[left.donor] + 1 : donor_indexes[right.donor]
            ]
        ]
        retail_gap = [
            retail[address]
            for address in retail_addresses[
                retail_indexes[left.retail] + 1 : retail_indexes[right.retail]
            ]
        ]
        if not donor_gap or not retail_gap or len(donor_gap) == len(retail_gap):
            continue
        interval_count += 1
        object_context = (
            left.object_name
            if left.object_name == right.object_name
            else f"{left.object_name} -> {right.object_name}"
        )
        pairs = align_interval(donor_gap, retail_gap, donor_to_retail, args.gap_penalty)
        pair_matrix = {
            (local.address, target.address): pair_evidence(local, target, donor_to_retail)
            for local in donor_gap
            for target in retail_gap
        }
        for local, target, evidence in pairs:
            donor_scores = sorted(
                (float(pair_matrix[(local.address, item.address)]["score"]) for item in retail_gap),
                reverse=True,
            )
            retail_scores = sorted(
                (float(pair_matrix[(item.address, target.address)]["score"]) for item in donor_gap),
                reverse=True,
            )
            donor_best = max(retail_gap, key=lambda item: float(pair_matrix[(local.address, item.address)]["score"]))
            retail_best = max(donor_gap, key=lambda item: float(pair_matrix[(item.address, target.address)]["score"]))
            donor_margin = donor_scores[0] - donor_scores[1] if len(donor_scores) > 1 else 999.0
            retail_margin = retail_scores[0] - retail_scores[1] if len(retail_scores) > 1 else 999.0
            reciprocal = donor_best.address == target.address and retail_best.address == local.address
            tier = confidence_tier(evidence, donor_margin, retail_margin, reciprocal)
            output_rows.append(
                {
                    "retail_start": f"0x{target.address:08X}",
                    "retail_size": target.size,
                    "candidate_name": local.name,
                    "donor_symbol": local.symbol,
                    "donor_address": f"0x{local.address:08X}",
                    "donor_size": local.size,
                    "object": object_context,
                    "left_anchor": left.name,
                    "right_anchor": right.name,
                    "donor_gap_count": len(donor_gap),
                    "retail_gap_count": len(retail_gap),
                    "pair_score": round(float(evidence["score"]), 4),
                    "mnemonic_similarity": round(float(evidence["mnemonic_similarity"]), 4),
                    "size_similarity": round(float(evidence["size_similarity"]), 4),
                    "call_count_similarity": round(float(evidence["call_count_similarity"]), 4),
                    "callee_hits": evidence["callee_hits"],
                    "caller_hits": evidence["caller_hits"],
                    "callee_misses": evidence["callee_misses"],
                    "caller_misses": evidence["caller_misses"],
                    "donor_margin": round(donor_margin, 4),
                    "retail_margin": round(retail_margin, 4),
                    "reciprocal_best": reciprocal,
                    "confidence_tier": tier,
                }
            )

    output_rows.sort(key=lambda row: int(str(row["retail_start"]), 16))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if output_rows:
        with args.output.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(output_rows[0]))
            writer.writeheader()
            writer.writerows(output_rows)
    else:
        args.output.write_text("", encoding="utf-8")

    tiers = Counter(str(row["confidence_tier"]) for row in output_rows)
    summary = {
        "divergent_intervals_aligned": interval_count,
        "aligned_pairs": len(output_rows),
        "confidence_tiers": dict(tiers),
        "output": str(args.output),
    }
    args.summary.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
