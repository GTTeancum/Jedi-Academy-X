#!/usr/bin/env python3
"""Render the current PS2-style EF frontend layout from shipped assets.

This is an offline developer aid, not runtime proof. It mirrors the measured
constants in code/ui/ui_ef_frontend.cpp closely enough to compare geometry,
colors, and selected assets against the PS2 reference before asking for a fresh
Xbox/CXBX capture.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from PIL import Image

VIEW_W = 1920
VIEW_H = 899
X_SCALE_FROM_640 = VIEW_W / 640.0
Y_SCALE_FROM_480 = VIEW_H / 480.0

FONT_TINY = 0
FONT_MEDIUM = 1
FONT_BIG = 2
FONT_HEIGHT = {FONT_TINY: 27, FONT_MEDIUM: 27, FONT_BIG: 36}
FONT_GAP = {FONT_TINY: 1, FONT_MEDIUM: 3, FONT_BIG: 3}
FONT_SPACE = {FONT_TINY: 4, FONT_MEDIUM: 8, FONT_BIG: 12}

PS2_BUTTON_PURPLE = (105, 83, 145, 255)
PS2_BUTTON_SELECTED = (155, 135, 189, 255)
PS2_STRIP_PURPLE = (121, 69, 111, 255)
PS2_TOP_PURPLE = (73, 53, 83, 255)
PS2_LIGHT_BROWN = (171, 103, 59, 255)
PS2_DARK_BROWN = (97, 49, 17, 255)
PS2_GOLD = (213, 176, 44, 255)
PS2_MAP_GOLD = (193, 193, 46, 255)
PS2_SELECTED_TEXT = (219, 217, 223, 255)
BLACK = (0, 0, 0, 255)
WHITE = (255, 255, 255, 255)
DARK_GREY = (64, 64, 64, 255)


TOP_PURPLE_BANDS = (
    (181, 23, 192, 46), (181, 69, 191, 39), (185, 108, 187, 1), (186, 109, 186, 2), (187, 111, 185, 7),
    (187, 118, 186, 2), (187, 120, 188, 1), (187, 121, 189, 4), (188, 125, 188, 1), (190, 126, 186, 1),
    (190, 127, 187, 1), (191, 128, 187, 1), (191, 129, 189, 5), (192, 134, 188, 1), (194, 135, 187, 2),
    (195, 137, 189, 2), (195, 139, 190, 2), (195, 141, 191, 1), (196, 142, 192, 1), (198, 143, 191, 1),
    (199, 144, 190, 1), (199, 145, 191, 1), (199, 146, 195, 1), (200, 147, 196, 1), (201, 148, 195, 1),
    (202, 149, 195, 1), (203, 150, 196, 1), (203, 151, 202, 1), (204, 152, 201, 2), (205, 154, 425, 1),
    (206, 155, 424, 1), (207, 156, 423, 1), (208, 157, 422, 1), (209, 158, 421, 2), (212, 160, 418, 1),
    (213, 161, 417, 1), (214, 162, 416, 1), (215, 163, 415, 1), (216, 164, 414, 1), (218, 165, 412, 1),
    (219, 166, 411, 1), (220, 167, 410, 2), (224, 169, 406, 1), (226, 170, 404, 1), (227, 171, 403, 2),
    (232, 173, 398, 1), (235, 174, 395, 1), (236, 175, 394, 2), (238, 177, 392, 1), (250, 178, 380, 1),
    (254, 179, 376, 1), (255, 180, 375, 1), (256, 181, 374, 3),
)

DARK_BROWN_BANDS = (
    (256, 196, 374, 2), (255, 198, 375, 1), (254, 199, 376, 1), (252, 200, 378, 1), (237, 201, 393, 1),
    (236, 202, 394, 2), (235, 204, 395, 1), (232, 205, 398, 1), (228, 206, 402, 1), (227, 207, 403, 2),
    (225, 209, 405, 1), (220, 210, 410, 2), (219, 212, 411, 2), (216, 214, 414, 1), (215, 215, 415, 2),
    (214, 217, 416, 1), (212, 218, 418, 1), (210, 219, 420, 1), (209, 220, 421, 1), (208, 221, 422, 2),
    (207, 223, 423, 1), (206, 224, 424, 1), (204, 225, 426, 1), (204, 226, 204, 1), (203, 227, 202, 1),
    (203, 228, 195, 1), (202, 229, 195, 1), (202, 230, 194, 1), (200, 231, 196, 1), (199, 232, 195, 1),
    (199, 233, 190, 2), (198, 235, 190, 1), (196, 236, 192, 1), (195, 237, 191, 1), (195, 238, 190, 2),
    (195, 240, 189, 2), (194, 242, 187, 1), (194, 243, 186, 1), (192, 244, 188, 1), (191, 245, 189, 5),
    (191, 250, 186, 2), (190, 252, 186, 1), (188, 253, 188, 1), (187, 254, 189, 3), (187, 257, 188, 2),
    (187, 259, 186, 1), (187, 260, 185, 9), (186, 269, 186, 1), (185, 270, 187, 1), (181, 271, 191, 40),
    (181, 311, 192, 23),
)

BUTTONS = (
    (491, 253, "NEW GAME"),
    (491, 356, "LOAD GAME"),
    (491, 459, "HOLOMATCH"),
    (491, 562, "CONFIGURE"),
    (491, 665, "VOYAGER CREW"),
    (491, 768, "CREDITS"),
)


LEFT_LCARS_TOP_RECT = (181, 23, 449, 311)
TOP_BARS_RECT = (641, 154, 1083, 73)
TITLE_RECT = (435, 66, 765, 56)
SELECT_PROMPT_RECT = (1384, 73, 265, 48)
METER_RECTS = ((280, 300, 78, 28), (280, 350, 78, 28), (263, 735, 96, 31))
BUTTON_LABEL_RECTS = (
    ((515, 276, 242, 45), (515, 276, 242, 45)),
    ((515, 380, 254, 43), (515, 380, 254, 43)),
    ((515, 483, 264, 44), (515, 483, 264, 44)),
    ((515, 586, 230, 43), (515, 586, 230, 43)),
    ((516, 689, 340, 43), (516, 689, 340, 43)),
    ((515, 792, 175, 43), (515, 792, 175, 43)),
)

QUADRANT_LABELS = (
    (1082, 306, "Dominion"),
    (997, 371, "Bajoran"),
    (997, 417, "Wormhole"),
    (1522, 315, "Voyager"),
    (1375, 372, "Borg"),
    (1375, 418, "Space"),
    (970, 519, "Gamma"),
    (970, 569, "Alpha"),
    (1575, 518, "Delta"),
    (1592, 567, "Beta"),
    (1010, 661, "Ferengi Alliance"),
    (970, 715, "Cardassia"),
    (970, 770, "Federation"),
    (1534, 648, "Romulan"),
    (1534, 700, "Empire"),
    (1534, 741, "Klingon"),
    (1534, 793, "Empire"),
)

FONT_TRIPLE_RE = re.compile(r"\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*\}")


def load_font_maps(path: Path) -> list[list[tuple[int, int, int]]]:
    triples = [(int(a), int(b), int(c)) for a, b, c in FONT_TRIPLE_RE.findall(path.read_text(errors="ignore"))]
    if len(triples) < 768:
        raise ValueError(f"expected at least 768 font triples in {path}, got {len(triples)}")
    return [triples[0:256], triples[256:512], triples[512:768]]


class Renderer:
    def __init__(self, base_dir: Path):
        self.base = base_dir
        self.image = Image.new("RGBA", (VIEW_W, VIEW_H), (0, 0, 0, 255))
        self.font_maps = load_font_maps(base_dir / "ext_data" / "fonts.dat")
        self.font_atlas = {
            FONT_TINY: Image.open(base_dir / "gfx" / "2d" / "chars_tiny.tga").convert("RGBA"),
            FONT_MEDIUM: Image.open(base_dir / "gfx" / "2d" / "chars_medium.tga").convert("RGBA"),
            FONT_BIG: Image.open(base_dir / "gfx" / "2d" / "chars_big.tga").convert("RGBA"),
        }
        self.assets = {
            "button_left": Image.open(base_dir / "menu" / "common" / "barbuttonleft.tga").convert("RGBA"),
            "button_right": Image.open(base_dir / "menu" / "new" / "bar1.tga").convert("RGBA"),
            "quadrants": Image.open(base_dir / "menu" / "special" / "ps2_map_panel.tga").convert("RGBA"),
            "corner_love": Image.open(base_dir / "menu" / "common" / "corner_love.tga").convert("RGBA"),
            "corner_love_2": Image.open(base_dir / "menu" / "common" / "corner_love_2.tga").convert("RGBA"),
            "panel_corner": Image.open(base_dir / "menu" / "lcarscontrols" / "round11.tga").convert("RGBA"),
            "select": Image.open(base_dir / "menu" / "common" / "ps2_select_x.tga").convert("RGBA"),
            "left_lcars_top": Image.open(base_dir / "menu" / "ps2" / "main" / "left_lcars_top.tga").convert("RGBA"),
            "top_bars": Image.open(base_dir / "menu" / "ps2" / "main" / "top_bars.tga").convert("RGBA"),
            "title": Image.open(base_dir / "menu" / "ps2" / "main" / "title_main.tga").convert("RGBA"),
            "select_prompt": Image.open(base_dir / "menu" / "ps2" / "main" / "select_prompt.tga").convert("RGBA"),
            "meter_0": Image.open(base_dir / "menu" / "ps2" / "main" / "meter_0.tga").convert("RGBA"),
            "meter_1": Image.open(base_dir / "menu" / "ps2" / "main" / "meter_1.tga").convert("RGBA"),
            "meter_2": Image.open(base_dir / "menu" / "ps2" / "main" / "meter_2.tga").convert("RGBA"),
            "button_left_cap": Image.open(base_dir / "menu" / "ps2" / "main" / "button_left_cap.tga").convert("RGBA"),
            "button_right_normal": Image.open(base_dir / "menu" / "ps2" / "main" / "button_right_normal.tga").convert("RGBA"),
            "button_right_selected": Image.open(base_dir / "menu" / "ps2" / "main" / "button_right_selected.tga").convert("RGBA"),
            "button_0_normal": Image.open(base_dir / "menu" / "ps2" / "main" / "button_0_normal.tga").convert("RGBA"),
            "button_0_selected": Image.open(base_dir / "menu" / "ps2" / "main" / "button_0_selected.tga").convert("RGBA"),
            "button_1_normal": Image.open(base_dir / "menu" / "ps2" / "main" / "button_1_normal.tga").convert("RGBA"),
            "button_1_selected": Image.open(base_dir / "menu" / "ps2" / "main" / "button_1_selected.tga").convert("RGBA"),
            "button_2_normal": Image.open(base_dir / "menu" / "ps2" / "main" / "button_2_normal.tga").convert("RGBA"),
            "button_2_selected": Image.open(base_dir / "menu" / "ps2" / "main" / "button_2_selected.tga").convert("RGBA"),
            "button_3_normal": Image.open(base_dir / "menu" / "ps2" / "main" / "button_3_normal.tga").convert("RGBA"),
            "button_3_selected": Image.open(base_dir / "menu" / "ps2" / "main" / "button_3_selected.tga").convert("RGBA"),
            "button_4_normal": Image.open(base_dir / "menu" / "ps2" / "main" / "button_4_normal.tga").convert("RGBA"),
            "button_4_selected": Image.open(base_dir / "menu" / "ps2" / "main" / "button_4_selected.tga").convert("RGBA"),
            "button_5_normal": Image.open(base_dir / "menu" / "ps2" / "main" / "button_5_normal.tga").convert("RGBA"),
            "button_5_selected": Image.open(base_dir / "menu" / "ps2" / "main" / "button_5_selected.tga").convert("RGBA"),
        }

    def tint(self, img: Image.Image, color: tuple[int, int, int, int]) -> Image.Image:
        rgba = img.convert("RGBA")
        r, g, b, a = rgba.split()
        cr, cg, cb, ca = color
        r = r.point(lambda value: value * cr // 255)
        g = g.point(lambda value: value * cg // 255)
        b = b.point(lambda value: value * cb // 255)
        if ca != 255:
            a = a.point(lambda value: value * ca // 255)
        return Image.merge("RGBA", (r, g, b, a))

    def draw_rect(self, x: float, y: float, w: float, h: float, color: tuple[int, int, int, int]) -> None:
        box = (round(x), round(y), round(x + w), round(y + h))
        if box[2] <= box[0] or box[3] <= box[1]:
            return
        layer = Image.new("RGBA", (box[2] - box[0], box[3] - box[1]), color)
        self.image.alpha_composite(layer, (box[0], box[1]))


    def draw_bands(self, bands: tuple[tuple[int, int, int, int], ...], color: tuple[int, int, int, int]) -> None:
        for x, y, w, h in bands:
            self.draw_rect(x, y, w, h, color)

    def draw_asset(
        self,
        name: str,
        x: float,
        y: float,
        w: float,
        h: float,
        color: tuple[int, int, int, int] = WHITE,
        flip_x: bool = False,
        flip_y: bool = False,
    ) -> None:
        img = self.assets[name]
        if flip_x:
            img = img.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        if flip_y:
            img = img.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        target_w = max(1, round(abs(w)))
        target_h = max(1, round(abs(h)))
        resized = img.resize((target_w, target_h), Image.Resampling.BILINEAR)
        if color != WHITE:
            resized = self.tint(resized, color)
        self.image.alpha_composite(resized, (round(x), round(y)))

    def text_width(self, text: str, font: int, scale: float) -> float:
        width = 0.0
        gap = FONT_GAP[font]
        for ch in text:
            code = ord(ch) & 0xFF
            sw = FONT_SPACE[font] if ch == " " else self.font_maps[font][code][2]
            if sw != -1:
                width += (sw + gap) * scale
        return width - gap * scale if width > 0.0 else 0.0

    def draw_text(
        self,
        x640: float,
        y480: float,
        text: str,
        font: int,
        color: tuple[int, int, int, int],
        x_scale: float = 1.0,
        y_scale: float | None = None,
        align: str = "left",
    ) -> None:
        if y_scale is None:
            y_scale = x_scale
        # 1920x899 PS2 viewport for comparison.
        x = x640 * X_SCALE_FROM_640
        y = y480 * Y_SCALE_FROM_480
        if align == "center":
            x -= self.text_width(text, font, x_scale) * X_SCALE_FROM_640 * 0.5
        elif align == "right":
            x -= self.text_width(text, font, x_scale) * X_SCALE_FROM_640

        gap = FONT_GAP[font]
        atlas = self.font_atlas[font]
        raw_h = FONT_HEIGHT[font]
        cursor_x = x
        for ch in text:
            code = ord(ch) & 0xFF
            sx, sy, sw = self.font_maps[font][code]
            if ch == " ":
                sw = FONT_SPACE[font]
            if sw == -1:
                continue
            draw_w = sw * x_scale * X_SCALE_FROM_640
            draw_h = raw_h * y_scale * Y_SCALE_FROM_480
            if ch != " ":
                glyph = atlas.crop((sx, sy, sx + sw, sy + raw_h))
                glyph = glyph.resize((max(1, round(draw_w)), max(1, round(draw_h))), Image.Resampling.BILINEAR)
                glyph = self.tint(glyph, color)
                self.image.alpha_composite(glyph, (round(cursor_x), round(y)))
            cursor_x += (sw + gap) * x_scale * X_SCALE_FROM_640

    def draw_ps2_panel_bracket(self, x: float, y: float, w: float, right: bool, lower: bool) -> None:
        bracket_h = 240.0
        cap_h = 60.0
        stem_start = 31.0
        stem_w = 24.0
        stem_x = x + w - stem_w if right else x
        if lower:
            self.draw_asset("panel_corner", x, y + bracket_h - cap_h, w, cap_h, PS2_STRIP_PURPLE, flip_x=right, flip_y=True)
            self.draw_rect(stem_x, y, stem_w, bracket_h - stem_start, PS2_STRIP_PURPLE)
        else:
            self.draw_asset("panel_corner", x, y, w, cap_h, PS2_STRIP_PURPLE, flip_x=right)
            self.draw_rect(stem_x, y + stem_start, stem_w, bracket_h - stem_start, PS2_STRIP_PURPLE)

    def draw(self) -> Image.Image:
        self.draw_rect(0, 0, VIEW_W, VIEW_H, BLACK)
        self.draw_asset("left_lcars_top", *LEFT_LCARS_TOP_RECT)
        self.draw_asset("top_bars", *TOP_BARS_RECT)
        self.draw_rect(181, 341, 192, 393, PS2_LIGHT_BROWN)
        self.draw_rect(181, 742, 192, 138, PS2_DARK_BROWN)

        self.draw_asset("title", *TITLE_RECT)
        self.draw_asset("select_prompt", *SELECT_PROMPT_RECT)

        self.draw_asset("meter_0", *METER_RECTS[0])
        self.draw_asset("meter_1", *METER_RECTS[1])
        self.draw_asset("meter_2", *METER_RECTS[2])

        self.draw_asset("quadrants", 918, 253, 803, 607)

        for index, (x, y, label) in enumerate(BUTTONS):
            fill = PS2_BUTTON_SELECTED if index == 0 else PS2_BUTTON_PURPLE
            variant = "selected" if index == 0 else "normal"
            self.draw_asset("button_left_cap", 401, y, 70, 86)
            self.draw_rect(x, y, 367, 86, fill)
            self.draw_asset(f"button_right_{variant}", 858, y, 27, 86)
            self.draw_asset(f"button_{index}_{variant}", *BUTTON_LABEL_RECTS[index][1 if index == 0 else 0])

        return self.image


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-dir", type=Path, default=Path("build/release/BaseEF"))
    parser.add_argument("--output", type=Path, default=Path("scripts/output/ps2_frontend_mock.png"))
    args = parser.parse_args()

    image = Renderer(args.base_dir).draw()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.output)
    print(f"wrote {args.output} {image.size[0]}x{image.size[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
