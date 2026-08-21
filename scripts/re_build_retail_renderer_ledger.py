#!/usr/bin/env python3
"""Build an exhaustive, ranked ledger for the retail Xbox renderer.

The seed ledger contains authoritative names, while the disassembler function
table contains the complete recovered renderer range.  This script joins those
sources and uses monotonic donor anchors only to describe object context.  It
does not promote names or mutate the seed ledger.
"""

from __future__ import annotations

import argparse
import csv
import json
from bisect import bisect_left
from collections import Counter
from pathlib import Path


HOT_OBJECT_WEIGHTS = {
    "tr_backend.obj": 5,
    "tr_shade.obj": 5,
    "tr_shade_calc.obj": 5,
    "tr_surface.obj": 5,
    "tr_world.obj": 4,
    "tr_bsp_xbox.obj": 4,
    "tr_image.obj": 4,
    "win_qgl_dx8.obj": 5,
    "win_glimp_console.obj": 5,
    "win_highdynamicrange.obj": 5,
    "win_lighteffects.obj": 5,
    "xbox_texture_man.obj": 4,
    "tr_mesh.obj": 3,
    "tr_model.obj": 3,
    "tr_scene.obj": 3,
    "tr_main.obj": 3,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--retail-functions", type=Path, required=True)
    parser.add_argument("--seeds", type=Path, required=True)
    parser.add_argument("--anchors", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--targets", type=Path, required=True)
    parser.add_argument("--retail-min", type=lambda value: int(value, 0), default=0x70000)
    parser.add_argument("--retail-max", type=lambda value: int(value, 0), default=0xB6300)
    parser.add_argument("--target-count", type=int, default=80)
    return parser.parse_args()


def load_json(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def load_anchors(path: Path) -> list[dict[str, object]]:
    anchors: list[dict[str, object]] = []
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            if str(row.get("monotonic_chain", "")).lower() != "true":
                continue
            anchors.append(
                {
                    "address": int(row["retail_start"], 16),
                    "name": row["name"],
                    "object": row["object"],
                }
            )
    return sorted(anchors, key=lambda item: int(item["address"]))


def object_context(
    address: int,
    anchor_addresses: list[int],
    anchors: list[dict[str, object]],
) -> tuple[str, str, str, str]:
    index = bisect_left(anchor_addresses, address)
    if index < len(anchors) and int(anchors[index]["address"]) == address:
        anchor = anchors[index]
        return str(anchor["object"]), "anchor", str(anchor["name"]), str(anchor["name"])

    left = anchors[index - 1] if index else None
    right = anchors[index] if index < len(anchors) else None
    left_name = str(left["name"]) if left else ""
    right_name = str(right["name"]) if right else ""
    if left and right and left["object"] == right["object"]:
        return str(left["object"]), "between_same_object_anchors", left_name, right_name
    if left and right:
        return (
            f"{left['object']} -> {right['object']}",
            "object_transition_ambiguous",
            left_name,
            right_name,
        )
    if left:
        return str(left["object"]), "after_last_anchor", left_name, ""
    if right:
        return str(right["object"]), "before_first_anchor", "", right_name
    return "", "unbounded", "", ""


def priority_score(
    object_name: str,
    caller_count: int,
    callee_count: int,
    size: int,
    instruction_count: int,
    context_status: str,
) -> int:
    base_object = object_name.split(" -> ", 1)[0]
    object_weight = HOT_OBJECT_WEIGHTS.get(base_object, 1)
    score = object_weight * 20
    score += min(caller_count, 25) * 14
    score += min(callee_count, 20) * 3
    score += min(size // 128, 20) * 4
    score += min(instruction_count // 32, 20) * 3
    if context_status == "between_same_object_anchors":
        score += 12
    elif context_status == "object_transition_ambiguous":
        score -= 10
    return score


def write_csv(path: Path, rows: list[dict[str, object]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    seeds = {int(item["start"], 16): item for item in load_json(args.seeds)}
    anchors = load_anchors(args.anchors)
    anchor_addresses = [int(item["address"]) for item in anchors]
    raw_functions = load_json(args.retail_functions)

    rows: list[dict[str, object]] = []
    for function in raw_functions:
        address = int(function["start"], 16)
        if not args.retail_min <= address < args.retail_max:
            continue
        seed = seeds.get(address)
        object_name, context_status, left_anchor, right_anchor = object_context(
            address, anchor_addresses, anchors
        )
        called_by = function.get("called_by", [])
        calls_to = function.get("calls_to", [])
        size = int(function["size"])
        instruction_count = int(function["num_instructions"])
        score = priority_score(
            object_name,
            len(called_by),
            len(calls_to),
            size,
            instruction_count,
            context_status,
        )
        rows.append(
            {
                "retail_start": f"0x{address:08X}",
                "retail_end": function["end"],
                "size": size,
                "instruction_count": instruction_count,
                "caller_count": len(called_by),
                "callee_count": len(calls_to),
                "name": str(seed["name"]) if seed else "",
                "named": "yes" if seed else "no",
                "detection_method": str(seed.get("detection_method", "")) if seed else "",
                "confidence": seed.get("confidence", "") if seed else "",
                "object_context": object_name,
                "context_status": context_status,
                "left_anchor": left_anchor,
                "right_anchor": right_anchor,
                "priority_score": score if not seed else "",
            }
        )

    rows.sort(key=lambda row: int(str(row["retail_start"]), 16))
    fieldnames = list(rows[0]) if rows else []
    write_csv(args.output, rows, fieldnames)

    unnamed = [row for row in rows if row["named"] == "no"]
    targets = sorted(
        unnamed,
        key=lambda row: (
            int(row["priority_score"]),
            int(row["caller_count"]),
            int(row["size"]),
        ),
        reverse=True,
    )[: args.target_count]
    write_csv(args.targets, targets, fieldnames)

    total_bytes = sum(int(row["size"]) for row in rows)
    named_rows = [row for row in rows if row["named"] == "yes"]
    named_bytes = sum(int(row["size"]) for row in named_rows)
    exact_rows = [
        row
        for row in rows
        if row["detection_method"] == "retail_finalbuild_normalized_exact"
    ]
    summary = {
        "renderer_range": [f"0x{args.retail_min:08X}", f"0x{args.retail_max:08X}"],
        "function_count": len(rows),
        "named_function_count": len(named_rows),
        "unnamed_function_count": len(unnamed),
        "named_function_percent": round(100.0 * len(named_rows) / max(len(rows), 1), 2),
        "total_bytes": total_bytes,
        "named_bytes": named_bytes,
        "named_byte_percent": round(100.0 * named_bytes / max(total_bytes, 1), 2),
        "exact_function_count": len(exact_rows),
        "detection_methods": dict(Counter(str(row["detection_method"]) for row in named_rows)),
        "unnamed_by_object_context": dict(
            Counter(str(row["object_context"]) for row in unnamed)
        ),
        "top_target_count": len(targets),
    }
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    args.summary.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(
        f"functions={len(rows)} named={len(named_rows)} unnamed={len(unnamed)} "
        f"named_bytes={named_bytes}/{total_bytes} ({summary['named_byte_percent']}%)"
    )
    print(f"ledger={args.output}")
    print(f"targets={args.targets}")
    print(f"summary={args.summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
