#!/usr/bin/env python3
import argparse
import glob
import re
import sys
from pathlib import Path

from PIL import Image, ImageStat


def parse_args():
    parser = argparse.ArgumentParser(description="Check captured Xbox visual smoke frames for obvious render failures.")
    parser.add_argument("images", nargs="+", help="Image paths or glob patterns.")
    parser.add_argument("--log", default="", help="Optional EF runtime log to scan for render fallback failures.")
    parser.add_argument("--max-black-pct", type=float, default=70.0)
    parser.add_argument("--min-mean", type=float, default=12.0)
    parser.add_argument("--min-unique", type=int, default=200)
    parser.add_argument("--client-top-skip", type=int, default=32, help="Rows to ignore for window chrome/title bars.")
    parser.add_argument("--grid-line-min-frac", type=float, default=0.45)
    parser.add_argument("--grid-line-min-count", type=int, default=3)
    parser.add_argument("--include-contact", action="store_true",
                        help="Include contact-sheet images in analysis instead of skipping *_contact.png.")
    return parser.parse_args()


def expand_images(patterns):
    paths = []
    for pattern in patterns:
        matches = glob.glob(pattern)
        if matches:
            paths.extend(matches)
        elif glob.has_magic(pattern):
            continue
        else:
            paths.append(pattern)
    unique = []
    seen = set()
    for item in paths:
        path = str(Path(item))
        if path not in seen:
            seen.add(path)
            unique.append(path)
    return unique


def count_near_white_lines(image, top_skip, min_frac):
    rgb = image.convert("RGB")
    width, height = rgb.size
    top_skip = max(0, min(top_skip, height - 1))
    pixels = rgb.load()

    row_hits = 0
    for y in range(top_skip, height):
        near_white = 0
        for x in range(width):
            r, g, b = pixels[x, y]
            if r > 215 and g > 215 and b > 215:
                near_white += 1
        if near_white >= width * min_frac:
            row_hits += 1

    col_hits = 0
    usable_height = height - top_skip
    for x in range(width):
        near_white = 0
        for y in range(top_skip, height):
            r, g, b = pixels[x, y]
            if r > 215 and g > 215 and b > 215:
                near_white += 1
        if near_white >= usable_height * min_frac:
            col_hits += 1

    return row_hits, col_hits


def analyze_image(path, args):
    image = Image.open(path).convert("RGB")
    width, height = image.size
    stat = ImageStat.Stat(image)
    colors = image.getcolors(maxcolors=width * height)
    unique = len(colors) if colors is not None else width * height
    total = width * height

    black = 0
    if colors is not None:
        for count, value in colors:
            if value == (0, 0, 0):
                black += count
    black_pct = 100.0 * black / float(total)
    mean = sum(stat.mean) / 3.0
    grid_rows, grid_cols = count_near_white_lines(image, args.client_top_skip, args.grid_line_min_frac)

    failures = []
    if black_pct > args.max_black_pct:
        failures.append(f"blackPct {black_pct:.2f} > {args.max_black_pct:.2f}")
    if mean < args.min_mean:
        failures.append(f"mean {mean:.2f} < {args.min_mean:.2f}")
    if unique < args.min_unique:
        failures.append(f"unique {unique} < {args.min_unique}")
    if grid_rows >= args.grid_line_min_count and grid_cols >= args.grid_line_min_count:
        failures.append(f"fallback-grid-like white lines rows={grid_rows} cols={grid_cols}")

    return {
        "path": path,
        "size": f"{width}x{height}",
        "mean": mean,
        "blackPct": black_pct,
        "unique": unique,
        "gridRows": grid_rows,
        "gridCols": grid_cols,
        "failures": failures,
    }


def scan_log(path):
    if not path:
        return []
    log_path = Path(path)
    if not log_path.exists():
        return [f"log missing: {log_path}"]
    text = log_path.read_text(errors="replace")
    patterns = [
        r"SHADER_MAP_FALLBACK",
        r"STEFX_TEX_FAIL",
        r"\bFATAL\b",
        r"Com_Error",
        r"ERR_DROP",
    ]
    failures = []
    for pattern in patterns:
        count = len(re.findall(pattern, text))
        if count:
            failures.append(f"{pattern} count={count}")
    fallback_hits = [int(match.group(1)) for match in re.finditer(r"\bfallback=([0-9]+)", text)]
    nonzero_fallbacks = [value for value in fallback_hits if value != 0]
    if nonzero_fallbacks:
        failures.append(f"renderer fallback count={len(nonzero_fallbacks)} max={max(nonzero_fallbacks)}")
    return failures


def main():
    args = parse_args()
    paths = expand_images(args.images)
    if not args.include_contact:
        paths = [path for path in paths if not Path(path).stem.endswith("_contact")]
    if not paths:
        print("status=FAIL")
        print("reason=no images")
        return 1

    all_failures = []
    for path in paths:
        result = analyze_image(path, args)
        print(
            "frame path={path} size={size} mean={mean:.2f} blackPct={blackPct:.2f} "
            "unique={unique} gridRows={gridRows} gridCols={gridCols}".format(**result)
        )
        for failure in result["failures"]:
            all_failures.append(f"{path}: {failure}")

    log_failures = scan_log(args.log)
    all_failures.extend(log_failures)

    if all_failures:
        print("status=FAIL")
        for failure in all_failures:
            print(f"failure={failure}")
        return 1

    print("status=PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
