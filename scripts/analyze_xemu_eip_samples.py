#!/usr/bin/env python3
"""Resolve XEMU EIP samples against a Visual C++ linker map."""

from __future__ import annotations

import argparse
import bisect
import collections
import re
from dataclasses import dataclass
from pathlib import Path


SAMPLE_RE = re.compile(r"eipsample\s+t=(?P<time>[0-9.]+)\s+eip=0x(?P<eip>[0-9a-fA-F]+)")
SYMBOL_RE = re.compile(
    r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+"
    r"(?P<symbol>\S+)\s+(?P<address>[0-9A-Fa-f]{8})\s+f\s+(?P<object>\S+)\s*$"
)


@dataclass(frozen=True)
class Symbol:
    address: int
    name: str
    object_name: str


def parse_int(value: str) -> int:
    return int(value, 0)


def load_symbols(path: Path, image_shift: int) -> list[Symbol]:
    symbols: list[Symbol] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = SYMBOL_RE.match(line)
        if not match:
            continue
        address = int(match.group("address"), 16) - image_shift
        if address >= 0:
            symbols.append(Symbol(address, match.group("symbol"), match.group("object")))
    symbols.sort(key=lambda symbol: symbol.address)
    return symbols


def load_samples(path: Path, start_time: float, end_time: float | None) -> list[tuple[float, int]]:
    samples: list[tuple[float, int]] = []
    for match in SAMPLE_RE.finditer(path.read_text(encoding="utf-8", errors="replace")):
        sample_time = float(match.group("time"))
        if sample_time < start_time or (end_time is not None and sample_time > end_time):
            continue
        samples.append((sample_time, int(match.group("eip"), 16)))
    return samples


def resolve(symbols: list[Symbol], addresses: list[int], eip: int) -> Symbol | None:
    index = bisect.bisect_right(addresses, eip) - 1
    if index < 0:
        return None
    return symbols[index]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("map", type=Path)
    parser.add_argument("--image-shift", type=parse_int, default=0x3F0000)
    parser.add_argument("--start-time", type=float, default=0.0)
    parser.add_argument("--end-time", type=float)
    parser.add_argument("--limit", type=int, default=30)
    parser.add_argument("--object-limit", type=int, default=15)
    args = parser.parse_args()

    symbols = load_symbols(args.map, args.image_shift)
    samples = load_samples(args.report, args.start_time, args.end_time)
    if not symbols:
        parser.error(f"no function symbols found in {args.map}")
    if not samples:
        parser.error(f"no samples found in requested time window in {args.report}")

    addresses = [symbol.address for symbol in symbols]
    counts: collections.Counter[tuple[str, str, int]] = collections.Counter()
    category_counts: collections.Counter[str] = collections.Counter()
    object_counts: collections.Counter[str] = collections.Counter()
    for _, eip in samples:
        if eip >= 0x80000000:
            category_counts["kernel"] += 1
            object_counts["xboxkrnl"] += 1
            counts[("<kernel>", "xboxkrnl", eip)] += 1
            continue
        symbol = resolve(symbols, addresses, eip)
        if symbol is None:
            category_counts["unresolved"] += 1
            object_counts["unknown"] += 1
            counts[("<unresolved>", "unknown", eip)] += 1
            continue
        category_counts["title"] += 1
        object_counts[symbol.object_name] += 1
        counts[(symbol.name, symbol.object_name, symbol.address)] += 1

    total = len(samples)
    end_label = "end" if args.end_time is None else f"{args.end_time:.2f}s"
    print(f"report: {args.report}")
    print(f"map: {args.map}")
    print(f"window: {args.start_time:.2f}s..{end_label}")
    print(f"samples: {total}")
    for category in ("title", "kernel", "unresolved"):
        count = category_counts[category]
        print(f"{category}: {count} ({100.0 * count / total:.1f}%)")
    print()
    print(" count    pct  object")
    for object_name, count in object_counts.most_common(args.object_limit):
        print(f"{count:6d} {100.0 * count / total:6.1f}%  {object_name}")
    print()
    print(" count    pct  xbe_addr  object                         symbol")
    for (name, object_name, address), count in counts.most_common(args.limit):
        print(f"{count:6d} {100.0 * count / total:6.1f}%  {address:08x}  {object_name:<29.29} {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
