#!/usr/bin/env python3
"""Align retail renderer functions to the clean JA renderer object layout.

The linker preserves object and (usually) function order, which makes order a
useful navigation aid after exact-byte and call-graph matching reach a fixed
point.  Order alone is not authoritative, so this script emits candidates and
segment diagnostics; it never edits the function seed ledger.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


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


@dataclass(frozen=True)
class DonorFunction:
    address: int
    symbol: str
    name: str
    object_name: str


@dataclass(frozen=True)
class RetailFunction:
    address: int
    end: int
    size: int
    calls: tuple[int, ...]


@dataclass(frozen=True)
class Anchor:
    donor_index: int
    retail_index: int
    donor: DonorFunction
    retail: RetailFunction
    method: str
    confidence: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--retail-functions", type=Path, required=True)
    parser.add_argument("--seeds", type=Path, required=True)
    parser.add_argument("--output-prefix", type=Path, required=True)
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


def parse_donor_map(path: Path) -> tuple[list[DonorFunction], list[str]]:
    aliases: dict[int, list[tuple[str, str]]] = defaultdict(list)
    object_first_address: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = MAP_FUNCTION_RE.match(line)
        if not match:
            continue
        object_name = match.group("object")
        if not RENDERER_OBJECT_RE.match(object_name):
            continue
        # VC71 emits compiler-local branch labels as `f i` map entries.  They
        # are interior basic-block labels, not callable function boundaries.
        if match.group("symbol").startswith("$L"):
            continue
        address = int(match.group("address"), 16)
        aliases[address].append((match.group("symbol"), object_name))
        object_first_address.setdefault(object_name, address)

    functions: list[DonorFunction] = []
    for address in sorted(aliases):
        symbol, object_name = aliases[address][0]
        functions.append(DonorFunction(address, symbol, simple_name(symbol), object_name))
    object_order = [
        name for name, _ in sorted(object_first_address.items(), key=lambda item: item[1])
    ]
    return functions, object_order


def parse_retail_functions(
    path: Path, minimum: int, maximum: int
) -> list[RetailFunction]:
    functions: list[RetailFunction] = []
    for item in json.loads(path.read_text(encoding="utf-8")):
        address = int(item["start"], 16)
        if not minimum <= address < maximum:
            continue
        end = int(item["end"], 16)
        calls = tuple(sorted(int(target, 16) for target in item.get("calls_to", [])))
        functions.append(RetailFunction(address, end, int(item["size"]), calls))
    return sorted(functions, key=lambda function: function.address)


def load_seeds(path: Path) -> dict[int, dict[str, object]]:
    return {
        int(str(item["start"]), 16): item
        for item in json.loads(path.read_text(encoding="utf-8"))
        if item.get("detection_method") != "unique_source_string"
    }


def resolve_anchors(
    donor: list[DonorFunction],
    retail: list[RetailFunction],
    seeds: dict[int, dict[str, object]],
) -> list[Anchor]:
    donor_by_name: dict[str, list[int]] = defaultdict(list)
    retail_by_address = {function.address: index for index, function in enumerate(retail)}
    for index, function in enumerate(donor):
        donor_by_name[function.name].append(index)

    anchors: list[Anchor] = []
    for address, seed in seeds.items():
        retail_index = retail_by_address.get(address)
        if retail_index is None:
            continue
        candidates = donor_by_name.get(str(seed["name"]), [])
        if len(candidates) != 1:
            continue
        donor_index = candidates[0]
        anchors.append(
            Anchor(
                donor_index,
                retail_index,
                donor[donor_index],
                retail[retail_index],
                str(seed.get("detection_method", "")),
                float(seed.get("confidence", 0.0)),
            )
        )
    return sorted(anchors, key=lambda anchor: anchor.retail_index)


def longest_monotonic_anchors(anchors: list[Anchor]) -> tuple[list[Anchor], list[Anchor]]:
    """Return a longest donor-index-increasing anchor chain and its rejects."""
    if not anchors:
        return [], []
    lengths = [1] * len(anchors)
    previous = [-1] * len(anchors)
    for right in range(len(anchors)):
        for left in range(right):
            if anchors[left].donor_index >= anchors[right].donor_index:
                continue
            if lengths[left] + 1 > lengths[right]:
                lengths[right] = lengths[left] + 1
                previous[right] = left
    cursor = max(range(len(anchors)), key=lengths.__getitem__)
    selected_indexes: set[int] = set()
    while cursor >= 0:
        selected_indexes.add(cursor)
        cursor = previous[cursor]
    selected = [anchor for index, anchor in enumerate(anchors) if index in selected_indexes]
    rejected = [anchor for index, anchor in enumerate(anchors) if index not in selected_indexes]
    return selected, rejected


def object_anchor_groups(
    anchors: list[Anchor], object_order: list[str]
) -> dict[str, list[Anchor]]:
    groups: dict[str, list[Anchor]] = defaultdict(list)
    for anchor in anchors:
        groups[anchor.donor.object_name].append(anchor)
    return {
        object_name: sorted(groups[object_name], key=lambda anchor: anchor.retail_index)
        for object_name in object_order
        if groups[object_name]
    }


def write_objects(
    path: Path,
    groups: dict[str, list[Anchor]],
    donor: list[DonorFunction],
    retail: list[RetailFunction],
) -> None:
    donor_by_object: dict[str, list[DonorFunction]] = defaultdict(list)
    for function in donor:
        donor_by_object[function.object_name].append(function)
    ordered_objects = list(groups)
    rows: list[dict[str, object]] = []
    for object_index, object_name in enumerate(ordered_objects):
        anchors = groups[object_name]
        previous_last = (
            groups[ordered_objects[object_index - 1]][-1].retail_index
            if object_index > 0
            else 0
        )
        next_first = (
            groups[ordered_objects[object_index + 1]][0].retail_index
            if object_index + 1 < len(ordered_objects)
            else len(retail)
        )
        rows.append(
            {
                "object": object_name,
                "donor_function_count": len(donor_by_object[object_name]),
                "anchor_count": len(anchors),
                "first_anchor": f"0x{anchors[0].retail.address:08X}",
                "first_anchor_name": anchors[0].donor.name,
                "last_anchor": f"0x{anchors[-1].retail.address:08X}",
                "last_anchor_name": anchors[-1].donor.name,
                "retail_functions_between_neighbor_anchors": max(0, next_first - previous_last - 1),
                "lower_boundary_after": (
                    f"0x{retail[previous_last].address:08X}" if previous_last < len(retail) else ""
                ),
                "upper_boundary_before": (
                    f"0x{retail[next_first].address:08X}" if next_first < len(retail) else ""
                ),
                "boundary_status": "inferred_from_neighbor_object_anchors",
            }
        )
    write_csv(path, rows)


def align_segments(
    anchors: list[Anchor], donor: list[DonorFunction], retail: list[RetailFunction]
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    segment_rows: list[dict[str, object]] = []
    candidate_rows: list[dict[str, object]] = []
    for left, right in zip(anchors, anchors[1:]):
        if left.donor.object_name != right.donor.object_name:
            continue
        donor_gap = donor[left.donor_index + 1 : right.donor_index]
        retail_gap = retail[left.retail_index + 1 : right.retail_index]
        equal_count = len(donor_gap) == len(retail_gap)
        segment_rows.append(
            {
                "object": left.donor.object_name,
                "left_retail": f"0x{left.retail.address:08X}",
                "left_name": left.donor.name,
                "right_retail": f"0x{right.retail.address:08X}",
                "right_name": right.donor.name,
                "donor_gap_count": len(donor_gap),
                "retail_gap_count": len(retail_gap),
                "count_delta": len(retail_gap) - len(donor_gap),
                "order_alignment_status": "equal_count_candidate" if equal_count else "divergent",
            }
        )
        if not equal_count:
            continue
        for donor_function, retail_function in zip(donor_gap, retail_gap):
            candidate_rows.append(
                {
                    "retail_start": f"0x{retail_function.address:08X}",
                    "retail_end": f"0x{retail_function.end:08X}",
                    "retail_size": retail_function.size,
                    "candidate_name": donor_function.name,
                    "donor_symbol": donor_function.symbol,
                    "donor_address": f"0x{donor_function.address:08X}",
                    "object": donor_function.object_name,
                    "left_anchor": left.donor.name,
                    "right_anchor": right.donor.name,
                    "evidence": "equal_count_between_monotonic_anchors",
                    "confidence_tier": "navigation_only_unverified",
                }
            )
    return segment_rows, candidate_rows


def write_anchor_ledger(path: Path, anchors: list[Anchor], selected: set[tuple[int, int]]) -> None:
    rows: list[dict[str, object]] = []
    for anchor in anchors:
        rows.append(
            {
                "retail_start": f"0x{anchor.retail.address:08X}",
                "name": anchor.donor.name,
                "object": anchor.donor.object_name,
                "donor_address": f"0x{anchor.donor.address:08X}",
                "method": anchor.method,
                "confidence": anchor.confidence,
                "monotonic_chain": (anchor.donor_index, anchor.retail_index) in selected,
            }
        )
    write_csv(path, rows)


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    args = parse_args()
    donor, object_order = parse_donor_map(args.map)
    retail = parse_retail_functions(args.retail_functions, args.retail_min, args.retail_max)
    seeds = load_seeds(args.seeds)
    anchors = resolve_anchors(donor, retail, seeds)
    monotonic, rejected = longest_monotonic_anchors(anchors)
    groups = object_anchor_groups(monotonic, object_order)

    prefix = args.output_prefix
    selected = {(anchor.donor_index, anchor.retail_index) for anchor in monotonic}
    write_anchor_ledger(prefix.with_name(prefix.name + "-anchors.csv"), anchors, selected)
    write_objects(prefix.with_name(prefix.name + "-objects.csv"), groups, donor, retail)
    segments, candidates = align_segments(monotonic, donor, retail)
    write_csv(prefix.with_name(prefix.name + "-segments.csv"), segments)
    write_csv(prefix.with_name(prefix.name + "-candidates.csv"), candidates)

    summary = {
        "donor_functions": len(donor),
        "retail_functions": len(retail),
        "resolved_anchors": len(anchors),
        "monotonic_anchors": len(monotonic),
        "rejected_nonmonotonic_anchors": len(rejected),
        "objects_with_anchors": len(groups),
        "segments": len(segments),
        "equal_count_segments": sum(
            row["order_alignment_status"] == "equal_count_candidate" for row in segments
        ),
        "navigation_candidates": len(candidates),
        "rejected": [
            {
                "retail_start": f"0x{anchor.retail.address:08X}",
                "name": anchor.donor.name,
                "object": anchor.donor.object_name,
            }
            for anchor in rejected
        ],
    }
    summary_path = prefix.with_name(prefix.name + "-summary.json")
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
