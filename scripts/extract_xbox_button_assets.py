#!/usr/bin/env python3
"""Extract transparent Xbox menu button glyphs from xbox_duke_buttons.png."""

from __future__ import annotations

from pathlib import Path

from PIL import Image


SOURCE = Path("xbox_duke_buttons.png")
OUT = Path("base/menu/common")

CROPS = {
    "xbox_a.tga": (45, 35, 145, 170),
    "xbox_b.tga": (235, 35, 145, 170),
    "xbox_x.tga": (425, 35, 145, 170),
    "xbox_y.tga": (615, 35, 145, 170),
    "xbox_white.tga": (800, 60, 145, 145),
    "xbox_black.tga": (970, 60, 145, 145),
    "xbox_lt.tga": (1030, 615, 120, 90),
    "xbox_rt.tga": (1180, 615, 120, 90),
    "xbox_back.tga": (1190, 325, 110, 70),
    "xbox_start.tga": (1170, 85, 130, 60),
    "xbox_lstick.tga": (430, 285, 140, 140),
    "xbox_rstick.tga": (620, 285, 140, 140),
    "xbox_dpad_up.tga": (35, 575, 170, 170),
    "xbox_dpad_down.tga": (415, 575, 170, 170),
    "xbox_dpad_left.tga": (605, 575, 170, 170),
    "xbox_dpad_right.tga": (795, 575, 170, 170),
}


def transparent_black(image: Image.Image) -> Image.Image:
    rgba = image.convert("RGBA")
    pixels = rgba.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            r, g, b, a = pixels[x, y]
            if r <= 2 and g <= 2 and b <= 2:
                pixels[x, y] = (0, 0, 0, 0)
            else:
                pixels[x, y] = (r, g, b, a)
    return rgba


def main() -> int:
    source = Image.open(SOURCE).convert("RGBA")
    OUT.mkdir(parents=True, exist_ok=True)
    for name, (x, y, w, h) in CROPS.items():
        crop = transparent_black(source.crop((x, y, x + w, y + h)))
        dest = OUT / name
        crop.save(dest)
        print(f"{dest} {w}x{h}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
