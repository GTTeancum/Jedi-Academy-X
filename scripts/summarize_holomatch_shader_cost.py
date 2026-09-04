#!/usr/bin/env python3
"""Summarize bounded split-screen shader-cost records from an XEMU report."""

from __future__ import annotations

import argparse
import re
from collections import defaultdict
from pathlib import Path


COST_RE = re.compile(
    r"STEFX_HW_SHADER_COST: sample=(?P<sample>\d+) rank=(?P<rank>\d+) "
    r"shader=(?P<shader>\d+) name='(?P<name>[^']*)' cycles=(?P<cycles>\d+) "
    r"batches=(?P<batches>\d+) passes=(?P<passes>\d+) indexes=(?P<indexes>\d+) "
    r"blend=(?P<blend>\d+) alphaTest=(?P<alpha>\d+) twoSided=(?P<two_sided>\d+) "
    r"world=(?P<world>\d+) entity=(?P<entity>\d+)"
)
TOTAL_RE = re.compile(
    r"STEFX_HW_SHADER_COST_TOTAL: sample=(?P<sample>\d+) shaders=(?P<shaders>\d+) "
    r"cycles=(?P<cycles>\d+) batches=(?P<batches>\d+) passes=(?P<passes>\d+) "
    r"indexes=(?P<indexes>\d+)"
)


def parse_report(text: str) -> tuple[dict[tuple[int, int], dict[str, object]], dict[int, dict[str, int]]]:
    costs: dict[tuple[int, int], dict[str, object]] = {}
    totals: dict[int, dict[str, int]] = {}
    for line in text.splitlines():
        match = COST_RE.search(line)
        if match:
            fields: dict[str, object] = {
                key: (value if key == "name" else int(value))
                for key, value in match.groupdict().items()
            }
            costs[(int(fields["sample"]), int(fields["rank"]))] = fields
            continue
        match = TOTAL_RE.search(line)
        if match:
            fields = {key: int(value) for key, value in match.groupdict().items()}
            totals[fields["sample"]] = fields
    return costs, totals


def summarize(costs: dict[tuple[int, int], dict[str, object]], totals: dict[int, dict[str, int]]) -> str:
    aggregate: dict[tuple[int, str], dict[str, float]] = defaultdict(lambda: defaultdict(float))
    for fields in costs.values():
        key = (int(fields["shader"]), str(fields["name"]))
        row = aggregate[key]
        row["samples"] += 1
        row["rank"] += int(fields["rank"])
        for metric in (
            "cycles", "batches", "passes", "indexes", "blend", "alpha",
            "two_sided", "world", "entity",
        ):
            row[metric] += int(fields[metric])

    total_cycles = sum(row["cycles"] for row in totals.values())
    lines = [
        f"samples={len(totals)} top_records={len(costs)} tracked_cycles={total_cycles}",
        "share  avgRank samples cycles     batches passes blend alpha twoSide world entity shader name",
    ]
    ordered = sorted(aggregate.items(), key=lambda item: item[1]["cycles"], reverse=True)
    for (shader, name), row in ordered:
        share = (100.0 * row["cycles"] / total_cycles) if total_cycles else 0.0
        avg_rank = row["rank"] / row["samples"]
        lines.append(
            f"{share:5.1f}% {avg_rank:7.2f} {int(row['samples']):7d} "
            f"{int(row['cycles']):10d} {int(row['batches']):7d} {int(row['passes']):6d} "
            f"{int(row['blend']):5d} {int(row['alpha']):5d} {int(row['two_sided']):7d} "
            f"{int(row['world']):5d} {int(row['entity']):6d} {shader:6d} {name}"
        )
    return "\n".join(lines)


def self_test() -> None:
    sample = """
STEFX_HW_SHADER_COST: sample=7 rank=1 shader=42 name='gfx/effects/test' cycles=300 batches=4 passes=8 indexes=120 blend=8 alphaTest=0 twoSided=8 world=0 entity=4
STEFX_HW_SHADER_COST: sample=7 rank=2 shader=9 name='textures/test/wall' cycles=100 batches=2 passes=2 indexes=60 blend=0 alphaTest=0 twoSided=0 world=2 entity=0
STEFX_HW_SHADER_COST_TOTAL: sample=7 shaders=100 cycles=500 batches=10 passes=14 indexes=240
STEFX_HW_SHADER_COST: sample=8 rank=1 shader=42 name='gfx/effects/test' cycles=200 batches=3 passes=6 indexes=90 blend=6 alphaTest=0 twoSided=6 world=0 entity=3
STEFX_HW_SHADER_COST_TOTAL: sample=8 shaders=100 cycles=400 batches=8 passes=12 indexes=180
"""
    costs, totals = parse_report(sample)
    output = summarize(costs, totals)
    assert len(costs) == 3
    assert len(totals) == 2
    assert "55.6%" in output
    assert "gfx/effects/test" in output
    print("summarize_holomatch_shader_cost self-test passed")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.report is None:
        parser.error("report is required unless --self-test is used")
    costs, totals = parse_report(args.report.read_text(encoding="utf-8", errors="replace"))
    if not costs or not totals:
        raise SystemExit("no STEFX_HW_SHADER_COST records found")
    print(summarize(costs, totals))


if __name__ == "__main__":
    main()
