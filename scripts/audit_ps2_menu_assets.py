#!/usr/bin/env python3
"""Audit extracted PS2/Xbox menu assets against their source references."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from PIL import Image, ImageChops

import extract_ps2_menu_assets


FORBIDDEN_ASSETS = (
    Path("base/menu/ps2/controller/body.tga"),
    Path("base/menu/ps2/controller/layout.tga"),
    Path("base/menu/ps2/screens/newgame_base.tga"),
    Path("base/menu/ps2/screens/loadgame_base.tga"),
    Path("base/menu/ps2/screens/configure_base.tga"),
    Path("base/menu/ps2/screens/audio_base.tga"),
    Path("base/menu/ps2/screens/video_base.tga"),
    Path("base/menu/ps2/screens/controller_base.tga"),
)

REQUIRED_XBOX_GLYPHS = (
    "xbox_a.tga",
    "xbox_b.tga",
    "xbox_x.tga",
    "xbox_y.tga",
    "xbox_white.tga",
    "xbox_black.tga",
    "xbox_lt.tga",
    "xbox_rt.tga",
    "xbox_back.tga",
    "xbox_start.tga",
    "xbox_lstick.tga",
    "xbox_rstick.tga",
    "xbox_dpad_up.tga",
    "xbox_dpad_down.tga",
    "xbox_dpad_left.tga",
    "xbox_dpad_right.tga",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def max_delta(a: Image.Image, b: Image.Image) -> int:
    diff = ImageChops.difference(a.convert("RGBA"), b.convert("RGBA"))
    extrema = diff.getextrema()
    return max(channel[1] for channel in extrema)


def crop_reference(ref_name: str, box: tuple[int, int, int, int]) -> Image.Image:
    source = extract_ps2_menu_assets.viewport_image(extract_ps2_menu_assets.REFS[ref_name])
    x, y, w, h = box
    return source.crop((x, y, x + w, y + h)).convert("RGBA")


def apply_expected_masks(rel: str, image: Image.Image) -> Image.Image:
    image = image.copy()
    for mask in extract_ps2_menu_assets.MASKS.get(rel, ()):
        mx, my, mw, mh = mask
        image.paste((0, 0, 0, 255), (mx, my, mx + mw, my + mh))
    return image


def expected_texture_size(width: int, height: int) -> tuple[int, int]:
    return (
        extract_ps2_menu_assets.next_power_of_two(width),
        extract_ps2_menu_assets.next_power_of_two(height),
    )


def edge_padding_delta(actual: Image.Image, width: int, height: int) -> int:
    if actual.width == width and actual.height == height:
        return 0

    expected = Image.new("RGBA", actual.size)
    visible = actual.crop((0, 0, width, height))
    expected.paste(visible, (0, 0))

    if width < actual.width:
        edge = visible.crop((width - 1, 0, width, height))
        for x in range(width, actual.width):
            expected.paste(edge, (x, 0))

    if height < actual.height:
        edge = expected.crop((0, height - 1, actual.width, height))
        for y in range(height, actual.height):
            expected.paste(edge, (0, y))

    return max_delta(expected, actual)


def main() -> int:
    failures: list[str] = []
    report: dict[str, object] = {"assets": [], "forbidden": [], "xboxGlyphs": []}

    for ref_name, entries in extract_ps2_menu_assets.CROPS.items():
        for _name, rel, box in entries:
            path = extract_ps2_menu_assets.output_path(rel)
            expected = apply_expected_masks(rel, crop_reference(ref_name, box))
            if not path.exists():
                failures.append(f"missing {path}")
                continue
            actual = Image.open(path).convert("RGBA")
            expected_size = expected_texture_size(box[2], box[3])
            visible = actual.crop((0, 0, box[2], box[3])) if actual.size == expected_size else actual
            delta = max_delta(expected, visible) if visible.size == expected.size else 255
            padding_delta = edge_padding_delta(actual, box[2], box[3]) if actual.size == expected_size else 255
            item = {
                "path": str(path),
                "reference": ref_name,
                "box": box,
                "size": actual.size,
                "visibleSize": expected.size,
                "expectedSize": expected_size,
                "sha256": sha256(path),
                "maxDelta": delta,
                "paddingMaxDelta": padding_delta,
            }
            report["assets"].append(item)  # type: ignore[index]
            if actual.size != expected_size or delta != 0 or padding_delta != 0:
                failures.append(
                    f"mismatch {path} size={actual.size} expected={expected_size} "
                    f"visible={expected.size} maxDelta={delta} paddingMaxDelta={padding_delta}"
                )

    for path in FORBIDDEN_ASSETS:
        exists = path.exists()
        report["forbidden"].append({"path": str(path), "exists": exists})  # type: ignore[index]
        if exists:
            failures.append(f"forbidden asset still exists {path}")

    for name in REQUIRED_XBOX_GLYPHS:
        path = Path("base/menu/common") / name
        exists = path.exists()
        report["xboxGlyphs"].append(  # type: ignore[index]
            {"path": str(path), "exists": exists, "sha256": sha256(path) if exists else None}
        )
        if not exists:
            failures.append(f"missing Xbox glyph {path}")

    out = Path("scripts/output/ps2_menu_asset_audit.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(f"audited {len(report['assets'])} PS2 element assets")
    print(f"checked {len(FORBIDDEN_ASSETS)} forbidden assets")
    print(f"checked {len(REQUIRED_XBOX_GLYPHS)} Xbox glyphs")
    print(f"wrote {out}")
    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        return 1
    print("AUDIT OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
