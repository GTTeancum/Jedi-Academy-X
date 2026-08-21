#!/usr/bin/env python3
"""Promote only strict, reviewable sequence-alignment renderer matches."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


MALFORMED_NAME_RE = re.compile(r"^(?:[RG]|\?[01][A-Za-z].*)$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seeds", type=Path, required=True)
    parser.add_argument("--alignment", type=Path, required=True)
    parser.add_argument("--output-seeds", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    return parser.parse_args()


def is_strict(row: dict[str, str]) -> bool:
    if row["reciprocal_best"].lower() != "true":
        return False
    if int(row["callee_misses"]) or int(row["caller_misses"]):
        return False
    shape = float(row["mnemonic_similarity"])
    size = float(row["size_similarity"])
    donor_margin = float(row["donor_margin"])
    retail_margin = float(row["retail_margin"])
    hits = int(row["callee_hits"]) + int(row["caller_hits"])
    machine_match = (
        shape >= 0.92
        and size >= 0.70
        and donor_margin >= 1.0
        and retail_margin >= 1.0
    )
    graph_match = (
        hits >= 2
        and shape >= 0.82
        and size >= 0.35
        and donor_margin >= 0.5
        and retail_margin >= 0.5
    )
    return machine_match or graph_match


def confidence(row: dict[str, str]) -> float:
    hits = int(row["callee_hits"]) + int(row["caller_hits"])
    shape = float(row["mnemonic_similarity"])
    size = float(row["size_similarity"])
    if hits >= 2 and shape >= 0.92 and size >= 0.7:
        return 0.995
    if hits >= 2:
        return 0.99
    return 0.985


def main() -> int:
    args = parse_args()
    seeds = json.loads(args.seeds.read_text(encoding="utf-8"))
    by_address = {int(item["start"], 16): item for item in seeds}
    report: list[dict[str, object]] = []

    with args.alignment.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            if not is_strict(row):
                continue
            address = int(row["retail_start"], 16)
            current = by_address.get(address)
            if current and current["name"] == row["candidate_name"]:
                continue
            action = "added"
            if current:
                current_name = str(current["name"])
                current_method = str(current.get("detection_method", ""))
                replaceable = (
                    current_method == "unique_source_string"
                    or MALFORMED_NAME_RE.match(current_name) is not None
                )
                if not replaceable:
                    report.append(
                        {
                            **row,
                            "action": "conflict_not_promoted",
                            "previous_name": current_name,
                            "previous_method": current_method,
                        }
                    )
                    continue
                action = "replaced_malformed_or_weak"

            seed = {
                "start": f"0x{address:08X}",
                "name": row["candidate_name"],
                "detection_method": "retail_finalbuild_sequence_machine_reviewed",
                "confidence": confidence(row),
                "evidence": {
                    "object": row["object"],
                    "donor_address": row["donor_address"],
                    "donor_symbol": row["donor_symbol"],
                    "left_anchor": row["left_anchor"],
                    "right_anchor": row["right_anchor"],
                    "mnemonic_sequence_similarity": float(row["mnemonic_similarity"]),
                    "size_similarity": float(row["size_similarity"]),
                    "callee_hits": int(row["callee_hits"]),
                    "caller_hits": int(row["caller_hits"]),
                    "callee_misses": int(row["callee_misses"]),
                    "caller_misses": int(row["caller_misses"]),
                    "donor_margin": float(row["donor_margin"]),
                    "retail_margin": float(row["retail_margin"]),
                },
            }
            by_address[address] = seed
            report.append(
                {
                    **row,
                    "action": action,
                    "previous_name": str(current["name"]) if current else "",
                    "previous_method": str(current.get("detection_method", "")) if current else "",
                }
            )

    output = sorted(by_address.values(), key=lambda item: int(item["start"], 16))
    args.output_seeds.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    if report:
        with args.report.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(report[0]))
            writer.writeheader()
            writer.writerows(report)
    else:
        args.report.write_text("", encoding="utf-8")

    promoted = sum(str(row["action"]).startswith(("added", "replaced")) for row in report)
    conflicts = sum(row["action"] == "conflict_not_promoted" for row in report)
    print(
        f"input_seeds={len(seeds)} output_seeds={len(output)} "
        f"promoted={promoted} conflicts={conflicts} report={args.report}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
