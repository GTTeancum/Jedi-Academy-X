#!/usr/bin/env python3
"""Intersect independent retail renderer candidate ledgers.

Only candidates that map the same retail address to the same undecorated name
are emitted.  Optional seed output appends only previously unseen addresses.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


DECORATED_NAME_RE = re.compile(r"^\?(?P<name>[^@]+)@@")
CTOR_DTOR_RE = re.compile(r"^\?\?(?P<kind>[01])(?P<class>[^@]+)@@")


def simple_name(symbol: str) -> str:
    ctor = CTOR_DTOR_RE.match(symbol)
    if ctor:
        class_name = ctor.group("class")
        return class_name if ctor.group("kind") == "0" else f"~{class_name}"
    decorated = DECORATED_NAME_RE.match(symbol)
    if decorated:
        return decorated.group("name")
    return symbol.lstrip("_")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--left", type=Path, required=True)
    parser.add_argument("--right", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seeds", type=Path)
    parser.add_argument("--output-seeds", type=Path)
    parser.add_argument("--method", default="cross_donor_callgraph_consensus")
    parser.add_argument("--confidence", type=float, default=0.995)
    return parser.parse_args()


def load(path: Path) -> dict[tuple[str, str], dict[str, str]]:
    result: dict[tuple[str, str], dict[str, str]] = {}
    for row in csv.DictReader(path.open(newline="", encoding="utf-8")):
        key = (row["retail_start"].upper(), simple_name(row["symbol"]))
        result[key] = row
    return result


def main() -> None:
    args = parse_args()
    left = load(args.left)
    right = load(args.right)
    common = sorted(set(left) & set(right), key=lambda key: int(key[0], 16))
    rows: list[dict[str, object]] = []
    for address_name in common:
        left_row = left[address_name]
        right_row = right[address_name]
        rows.append(
            {
                "retail_start": address_name[0],
                "name": address_name[1],
                "left_symbol": left_row["symbol"],
                "left_object": left_row["object"],
                "left_score": left_row["score"],
                "left_margin": left_row["runner_up_margin"],
                "right_symbol": right_row["symbol"],
                "right_object": right_row["object"],
                "right_score": right_row["score"],
                "right_margin": right_row["runner_up_margin"],
                "evidence": args.method,
            }
        )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        if rows:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)

    promoted = 0
    if args.output_seeds:
        if not args.seeds:
            raise SystemExit("--output-seeds requires --seeds")
        seeds = json.loads(args.seeds.read_text(encoding="utf-8"))
        addresses = {int(str(item["start"]), 16) for item in seeds}
        for row in rows:
            address = int(str(row["retail_start"]), 16)
            if address in addresses:
                continue
            seeds.append(
                {
                    "start": f"0x{address:08X}",
                    "name": row["name"],
                    "confidence": args.confidence,
                    "detection_method": args.method,
                }
            )
            addresses.add(address)
            promoted += 1
        seeds.sort(key=lambda item: int(str(item["start"]), 16))
        args.output_seeds.write_text(json.dumps(seeds, indent=4) + "\n", encoding="utf-8")
    print(f"agreements={len(rows)} promoted={promoted}")


if __name__ == "__main__":
    main()
