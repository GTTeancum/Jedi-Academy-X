#!/usr/bin/env python3
"""Promote exceptionally strong retail FinalBuild call-graph matches.

This is intentionally stricter than the candidate generator.  A promotion must
have multiple already-mapped graph edges, no graph contradictions, strong
instruction-shape and size agreement, and a clear runner-up margin.  Existing
weak source-string placeholders may be upgraded; stronger seeds are preserved.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


DECORATED_NAME_RE = re.compile(r"^\?(?P<name>[^@]+)@@")
CTOR_DTOR_RE = re.compile(r"^\?\?(?P<kind>[01])(?P<class>[^@]+)@@")
WEAK_METHODS = {"unique_source_string"}


def simple_name(symbol: str) -> str:
    constructor = CTOR_DTOR_RE.match(symbol)
    if constructor:
        class_name = constructor.group("class")
        return class_name if constructor.group("kind") == "0" else f"~{class_name}"
    decorated = DECORATED_NAME_RE.match(symbol)
    if decorated:
        return decorated.group("name")
    return symbol.lstrip("_")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidates", type=Path, required=True)
    parser.add_argument("--seeds", type=Path, required=True)
    parser.add_argument("--output-seeds", type=Path, required=True)
    parser.add_argument("--output-report", type=Path, required=True)
    parser.add_argument("--min-hits", type=int, default=2)
    parser.add_argument("--min-shape", type=float, default=0.72)
    parser.add_argument("--min-size", type=float, default=0.35)
    parser.add_argument("--min-margin", type=float, default=5.0)
    parser.add_argument("--confidence", type=float, default=0.997)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    seeds = json.loads(args.seeds.read_text(encoding="utf-8"))
    seeds_by_address = {int(item["start"], 16): item for item in seeds}
    report: list[dict[str, object]] = []

    with args.candidates.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            hits = int(row["callee_hits"]) + int(row["caller_hits"])
            misses = int(row["callee_misses"]) + int(row["caller_misses"])
            shape = float(row["mnemonic_similarity"])
            size = float(row["size_similarity"])
            margin = float(row["runner_up_margin"])
            if (
                hits < args.min_hits
                or misses != 0
                or shape < args.min_shape
                or size < args.min_size
                or margin < args.min_margin
            ):
                continue

            address = int(row["retail_start"], 16)
            name = simple_name(row["symbol"])
            existing = seeds_by_address.get(address)
            action = "added"
            if existing:
                if existing.get("detection_method") not in WEAK_METHODS:
                    continue
                action = "upgraded_weak_seed"
                existing.update(
                    {
                        "name": name,
                        "confidence": args.confidence,
                        "detection_method": "retail_finalbuild_callgraph_shape_reviewed",
                    }
                )
            else:
                existing = {
                    "start": f"0x{address:08X}",
                    "name": name,
                    "confidence": args.confidence,
                    "detection_method": "retail_finalbuild_callgraph_shape_reviewed",
                }
                seeds.append(existing)
                seeds_by_address[address] = existing

            report.append(
                {
                    "retail_start": f"0x{address:08X}",
                    "name": name,
                    "object": row["object"],
                    "action": action,
                    "graph_hits": hits,
                    "graph_misses": misses,
                    "mnemonic_similarity": shape,
                    "size_similarity": size,
                    "runner_up_margin": margin,
                    "evidence": "strict reviewed FinalBuild graph and machine shape",
                }
            )

    seeds.sort(key=lambda item: int(item["start"], 16))
    args.output_seeds.parent.mkdir(parents=True, exist_ok=True)
    args.output_seeds.write_text(json.dumps(seeds, indent=4) + "\n", encoding="utf-8")
    args.output_report.parent.mkdir(parents=True, exist_ok=True)
    with args.output_report.open("w", newline="", encoding="utf-8") as stream:
        fieldnames = [
            "retail_start",
            "name",
            "object",
            "action",
            "graph_hits",
            "graph_misses",
            "mnemonic_similarity",
            "size_similarity",
            "runner_up_margin",
            "evidence",
        ]
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(report)
    print(f"promoted={len(report)} output={args.output_seeds}")
    for row in report:
        print(f"{row['retail_start']} {row['name']} ({row['action']})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
