#!/usr/bin/env python3
"""Promote exact retail renderer matches into the authoritative seed ledger."""

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
    parser.add_argument("--seeds", type=Path, required=True)
    parser.add_argument("--exact", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--method", default="retail_finalbuild_normalized_exact")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    seeds = json.loads(args.seeds.read_text(encoding="utf-8"))
    by_address = {int(str(seed["start"]), 16): seed for seed in seeds}
    added = 0
    upgraded = 0

    with args.exact.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            address = int(row["retail_start"], 16)
            name = simple_name(row["symbol"])
            replacement = {
                "start": f"0x{address:08X}",
                "name": name,
                "confidence": 1,
                "detection_method": args.method,
            }
            if address in by_address:
                if by_address[address] != replacement:
                    by_address[address] = replacement
                    upgraded += 1
            else:
                by_address[address] = replacement
                added += 1

    output = [by_address[address] for address in sorted(by_address)]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=4) + "\n", encoding="utf-8")
    print(f"exact={added + upgraded} added={added} upgraded={upgraded} total={len(output)}")


if __name__ == "__main__":
    main()
