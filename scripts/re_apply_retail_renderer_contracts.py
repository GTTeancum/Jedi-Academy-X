#!/usr/bin/env python3
"""Promote manually proven retail renderer contracts into a seed ledger."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--contracts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def address(entry: dict[str, object]) -> int:
    return int(str(entry["start"]), 16)


def main() -> int:
    args = parse_args()
    seeds = json.loads(args.base.read_text(encoding="utf-8"))
    contracts = json.loads(args.contracts.read_text(encoding="utf-8"))

    by_address = {address(seed): seed for seed in seeds}
    promoted = 0
    confirmed = 0
    for contract in contracts:
        start = address(contract)
        name = str(contract["name"])
        existing = by_address.get(start)
        if existing is not None:
            if existing["name"] != name:
                raise SystemExit(
                    f"conflict at 0x{start:08X}: {existing['name']} != {name}"
                )
            confirmed += 1
            continue

        seed = {
            "start": f"0x{start:08X}",
            "name": name,
            "confidence": float(contract["confidence"]),
            "detection_method": "manual_machine_contract",
        }
        seeds.append(seed)
        by_address[start] = seed
        promoted += 1

    seeds.sort(key=address)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(seeds, indent=4) + "\n", encoding="utf-8")
    print(
        f"base={len(seeds) - promoted} contracts={len(contracts)} "
        f"promoted={promoted} confirmed={confirmed} total={len(seeds)} "
        f"output={args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
