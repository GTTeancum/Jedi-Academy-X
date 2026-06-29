#!/usr/bin/env python3
"""Compare an Elite Force Xbox frontend capture against the PS2 menu reference.

The PS2 reference supplied for this work is a 1920x1080 PCSX2 screenshot whose
actual game viewport is 1920x899 at y=111. The Xbox renderer capture is normally
640x480. This tool crops the PS2 viewport, scales the candidate capture into that
same viewport space, and reports global plus measured-region pixel differences.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageChops, ImageStat


DEFAULT_REFERENCE_VIEWPORT = (0, 111, 1920, 899)
DEFAULT_CANDIDATE_VIEWPORT = None


@dataclass(frozen=True)
class Region:
    name: str
    box: tuple[int, int, int, int]
    required: bool = True


# Boxes are in PS2 viewport coordinates, not full screenshot coordinates.
REGIONS_BY_SCREEN: dict[str, tuple[Region, ...]] = {
    "main": (
        Region("top_lcars", (181, 23, 449, 311)),
        Region("top_bars", (641, 154, 1083, 73)),
        Region("title", (430, 55, 800, 84)),
        Region("select_prompt", (1388, 65, 320, 70), required=False),
        Region("left_meter", (181, 280, 192, 600)),
        Region("button_stack", (401, 253, 484, 601)),
        Region("selected_button", (401, 253, 484, 86)),
        Region("map_brackets", (918, 253, 803, 607)),
        Region("map_labels", (960, 290, 760, 570)),
        Region("galaxy_art", (970, 253, 700, 607), required=False),
    ),
    "newgame": (
        Region("top_lcars", (181, 23, 449, 311)),
        Region("top_bars", (641, 154, 1083, 73)),
        Region("title", (430, 52, 800, 86)),
        Region("left_panel", (181, 341, 192, 539)),
        Region("difficulty_stack", (415, 238, 513, 384)),
        Region("gender_stack", (415, 631, 513, 235)),
        Region("warp_core", (954, 237, 168, 639)),
        Region("tutorial", (1197, 313, 520, 94)),
        Region("engage_block", (1115, 552, 606, 271)),
        Region("prompts", (1380, 35, 340, 130), required=False),
    ),
    "loadgame": (
        Region("left_frame", (198, 50, 78, 845)),
        Region("right_frame", (1644, 84, 77, 811)),
        Region("bottom_frame", (276, 882, 1368, 34)),
        Region("dialog_box", (535, 206, 850, 487)),
        Region("dialog_text", (610, 300, 700, 270), required=False),
        Region("prompts", (680, 585, 620, 80), required=False),
    ),
    "configure": (
        Region("utility_left", (236, 47, 314, 811)),
        Region("utility_top", (495, 189, 1200, 43)),
        Region("utility_right", (1566, 236, 128, 538)),
        Region("utility_bottom", (558, 792, 1136, 66)),
        Region("title", (530, 78, 760, 82)),
        Region("rows", (723, 294, 619, 366)),
        Region("prompts", (1380, 35, 340, 130), required=False),
    ),
    "audio": (
        Region("utility_left", (236, 47, 314, 811)),
        Region("utility_top", (495, 189, 1200, 43)),
        Region("utility_right", (1566, 236, 128, 538)),
        Region("utility_bottom", (558, 792, 1136, 66)),
        Region("title", (530, 78, 620, 82)),
        Region("rows_and_sliders", (553, 281, 960, 412)),
        Region("prompts", (1380, 35, 340, 130), required=False),
    ),
    "video": (
        Region("video_corners", (160, 0, 1600, 899)),
        Region("utility_left", (236, 47, 314, 811)),
        Region("utility_top", (495, 189, 1200, 43)),
        Region("utility_right", (1566, 236, 128, 538)),
        Region("utility_bottom", (558, 792, 1136, 66)),
        Region("title", (530, 78, 1030, 82)),
        Region("instructions", (560, 285, 815, 220)),
        Region("prompts", (560, 585, 1000, 185), required=False),
    ),
    "controller": (
        Region("title", (280, 50, 960, 95)),
        Region("frame", (198, 154, 1526, 801)),
        Region("control_map", (320, 285, 1280, 620), required=False),
        Region("prompts", (1380, 35, 340, 130), required=False),
    ),
}


def parse_box(value: str) -> tuple[int, int, int, int]:
    parts = [int(part.strip()) for part in value.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("box must be x,y,w,h")
    x, y, w, h = parts
    if w <= 0 or h <= 0:
        raise argparse.ArgumentTypeError("box width and height must be positive")
    return x, y, w, h


def crop_box(image: Image.Image, box: tuple[int, int, int, int]) -> Image.Image:
    x, y, w, h = box
    return image.crop((x, y, x + w, y + h))


def to_rgb(path: Path) -> Image.Image:
    return Image.open(path).convert("RGB")


def stats_for(reference: Image.Image, candidate: Image.Image) -> dict[str, float | int]:
    diff = ImageChops.difference(reference, candidate)
    stat = ImageStat.Stat(diff)
    pixels = reference.width * reference.height
    mean_channels = stat.mean
    rms_channels = stat.rms
    extrema = stat.extrema
    mean_abs = sum(mean_channels) / 3.0
    rms = math.sqrt(sum(channel * channel for channel in rms_channels) / 3.0)
    max_delta = max(high for _low, high in extrema)

    exact = 0
    within_2 = 0
    within_8 = 0
    ref_pixels = reference.load()
    cand_pixels = candidate.load()
    for y in range(reference.height):
        for x in range(reference.width):
            pr = ref_pixels[x, y]
            pc = cand_pixels[x, y]
            delta = max(abs(pr[0] - pc[0]), abs(pr[1] - pc[1]), abs(pr[2] - pc[2]))
            if delta == 0:
                exact += 1
            if delta <= 2:
                within_2 += 1
            if delta <= 8:
                within_8 += 1

    return {
        "width": reference.width,
        "height": reference.height,
        "pixels": pixels,
        "meanAbs": mean_abs,
        "rms": rms,
        "maxDelta": max_delta,
        "exactPct": exact / pixels * 100.0,
        "within2Pct": within_2 / pixels * 100.0,
        "within8Pct": within_8 / pixels * 100.0,
    }


def regions_for_screen(screen: str) -> tuple[Region, ...]:
    try:
        return REGIONS_BY_SCREEN[screen]
    except KeyError as exc:
        raise SystemExit(f"unknown screen: {screen}") from exc


def iter_regions(screen: str, names: Iterable[str] | None) -> tuple[Region, ...]:
    regions = regions_for_screen(screen)
    if not names:
        return regions
    wanted = set(names)
    unknown = wanted.difference(region.name for region in regions)
    if unknown:
        raise SystemExit(f"unknown region(s): {', '.join(sorted(unknown))}")
    return tuple(region for region in regions if region.name in wanted)


def save_heatmap(reference: Image.Image, candidate: Image.Image, output: Path) -> None:
    diff = ImageChops.difference(reference, candidate).convert("RGB")
    # Boost subtle differences for visual inspection while preserving hot areas.
    diff = diff.point(lambda value: min(255, value * 4))
    output.parent.mkdir(parents=True, exist_ok=True)
    diff.save(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--screen", choices=sorted(REGIONS_BY_SCREEN), default="main", help="Menu screen region set")
    parser.add_argument("--reference", required=True, type=Path, help="PCSX2 reference screenshot")
    parser.add_argument("--candidate", required=True, type=Path, help="Xbox renderer/window capture")
    parser.add_argument(
        "--reference-viewport",
        type=parse_box,
        default=DEFAULT_REFERENCE_VIEWPORT,
        help="Reference viewport crop as x,y,w,h. Default: 0,111,1920,899",
    )
    parser.add_argument(
        "--candidate-viewport",
        type=parse_box,
        default=DEFAULT_CANDIDATE_VIEWPORT,
        help="Optional candidate crop as x,y,w,h before scaling. Default: full image",
    )
    parser.add_argument(
        "--region",
        action="append",
        help="Limit report to one or more named regions",
    )
    parser.add_argument("--heatmap", type=Path, help="Optional boosted diff heatmap output")
    parser.add_argument("--json", type=Path, help="Optional JSON report output")
    parser.add_argument("--max-mean", type=float, default=3.0, help="Required-region mean absolute limit")
    parser.add_argument("--max-rms", type=float, default=6.0, help="Required-region RMS limit")
    parser.add_argument("--min-within8", type=float, default=95.0, help="Required-region percent within delta 8")
    args = parser.parse_args()

    reference_full = to_rgb(args.reference)
    candidate_full = to_rgb(args.candidate)
    reference = crop_box(reference_full, args.reference_viewport)
    candidate_crop = crop_box(candidate_full, args.candidate_viewport) if args.candidate_viewport else candidate_full
    candidate = candidate_crop.resize(reference.size, Image.Resampling.BILINEAR)

    report: dict[str, object] = {
        "reference": str(args.reference),
        "candidate": str(args.candidate),
        "screen": args.screen,
        "referenceViewport": args.reference_viewport,
        "candidateViewport": args.candidate_viewport,
        "candidateSourceSize": candidate_crop.size,
        "comparisonSize": reference.size,
        "global": stats_for(reference, candidate),
        "regions": {},
    }

    failed: list[str] = []
    for region in iter_regions(args.screen, args.region):
        ref_region = crop_box(reference, region.box)
        cand_region = crop_box(candidate, region.box)
        region_stats = stats_for(ref_region, cand_region)
        region_stats["required"] = region.required
        region_stats["box"] = region.box
        report["regions"][region.name] = region_stats  # type: ignore[index]
        if region.required:
            if (
                region_stats["meanAbs"] > args.max_mean
                or region_stats["rms"] > args.max_rms
                or region_stats["within8Pct"] < args.min_within8
            ):
                failed.append(region.name)

    if args.heatmap:
        save_heatmap(reference, candidate, args.heatmap)
        report["heatmap"] = str(args.heatmap)

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    global_stats = report["global"]
    print(
        "global "
        f"meanAbs={global_stats['meanAbs']:.3f} "
        f"rms={global_stats['rms']:.3f} "
        f"within8={global_stats['within8Pct']:.2f}% "
        f"exact={global_stats['exactPct']:.2f}%"
    )
    for name, values in report["regions"].items():
        print(
            f"{name} "
            f"meanAbs={values['meanAbs']:.3f} "
            f"rms={values['rms']:.3f} "
            f"within8={values['within8Pct']:.2f}% "
            f"required={values['required']}"
        )

    if failed:
        print("FAILED regions=" + ",".join(failed))
        return 1

    print("PASS required regions within thresholds")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
