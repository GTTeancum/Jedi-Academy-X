#!/usr/bin/env python3
"""Extract PS2 menu element textures from supplied PCSX2 captures."""

from __future__ import annotations

from pathlib import Path

from PIL import Image


VIEWPORT = (0, 111, 1920, 899)
OUT = Path("base/menu/ps2")
MENU_OUT = Path("base/menu")
STALE_OUTPUTS = (
    OUT / "controller/layout.tga",
)

REFS = {
    "main": Path(r"C:\Users\smmel\AppData\Local\Temp\codex-clipboard-10425340-5746-487c-aa23-ae1133e8b973.png"),
    "newgame": Path(r"C:\Users\smmel\AppData\Local\Temp\codex-clipboard-607b50d4-7fac-4417-b2b4-1cd743528cdf.png"),
    "loadgame": Path(r"C:\Users\smmel\AppData\Local\Temp\codex-clipboard-c09d52b5-3ff2-417d-9727-0ded3d05704e.png"),
    "configure": Path(r"C:\Users\smmel\AppData\Local\Temp\codex-clipboard-afd22a2e-a938-4b51-9b54-8954153a6aef.png"),
    "audio": Path(r"C:\Users\smmel\AppData\Local\Temp\codex-clipboard-50de5063-11bc-42b3-9d2f-ec9af18ebf2e.png"),
    "video": Path(r"C:\Users\smmel\AppData\Local\Temp\codex-clipboard-4e4cd9ea-8737-4228-b91d-80142eef9382.png"),
    "controller": Path(r"C:\Users\smmel\AppData\Local\Temp\codex-clipboard-322742b9-90a8-4a5f-870b-3c13b7b5aa77.png"),
}

CROPS = {
    "main": [
        ("left_lcars_top", "main/left_lcars_top.tga", (181, 23, 449, 311)),
        ("top_bars", "main/top_bars.tga", (641, 154, 1083, 73)),
        ("title_main", "main/title_main.tga", (435, 66, 765, 56)),
        ("select_prompt", "main/select_prompt.tga", (1384, 73, 265, 48)),
        ("meter_0", "main/meter_0.tga", (280, 300, 78, 28)),
        ("meter_1", "main/meter_1.tga", (280, 350, 78, 28)),
        ("meter_2", "main/meter_2.tga", (263, 735, 96, 31)),
        ("button_left_cap", "main/button_left_cap.tga", (401, 253, 70, 86)),
        ("button_right_selected", "main/button_right_selected.tga", (858, 253, 27, 86)),
        ("button_right_normal", "main/button_right_normal.tga", (858, 356, 27, 86)),
        ("button_0_selected", "main/button_0_selected.tga", (515, 276, 242, 45)),
        ("button_1_normal", "main/button_1_normal.tga", (515, 380, 254, 43)),
        ("button_2_normal", "main/button_2_normal.tga", (515, 483, 264, 44)),
        ("button_3_normal", "main/button_3_normal.tga", (515, 586, 230, 43)),
        ("button_4_normal", "main/button_4_normal.tga", (516, 689, 340, 43)),
        ("button_5_normal", "main/button_5_normal.tga", (515, 792, 175, 43)),
        ("map_panel", "special/ps2_map_panel.tga", (918, 253, 803, 607)),
        ("prompt_select_mid", "prompts/select_mid.tga", (1460, 68, 250, 50)),
    ],
    "newgame": [
        ("title", "newgame/title.tga", (430, 52, 800, 86)),
        ("left_panel", "newgame/left_panel.tga", (181, 341, 192, 539)),
        ("difficulty_header", "newgame/difficulty_header.tga", (415, 238, 513, 83)),
        ("easy", "newgame/easy.tga", (415, 332, 513, 66)),
        ("normal", "newgame/normal.tga", (415, 406, 513, 66)),
        ("challenging", "newgame/challenging.tga", (415, 481, 513, 66)),
        ("difficult", "newgame/difficult.tga", (415, 556, 513, 66)),
        ("gender_header", "newgame/gender_header.tga", (415, 631, 513, 85)),
        ("female", "newgame/female.tga", (415, 725, 513, 66)),
        ("male", "newgame/male.tga", (415, 800, 513, 66)),
        ("warp_core", "newgame/warp_core.tga", (954, 237, 168, 639)),
        ("tutorial", "newgame/tutorial.tga", (1197, 313, 520, 94)),
        ("engage_block", "newgame/engage_block.tga", (1115, 552, 606, 271)),
        ("prompt_select_high", "prompts/select_high.tga", (1460, 40, 250, 50)),
        ("prompt_back_high", "prompts/back_high.tga", (1460, 96, 250, 50)),
    ],
    "loadgame": [
        ("left_frame", "loadgame/left_frame.tga", (198, 50, 78, 845)),
        ("right_frame", "loadgame/right_frame.tga", (1644, 84, 77, 811)),
        ("bottom_left", "loadgame/bottom_left.tga", (276, 882, 834, 34)),
        ("bottom_right", "loadgame/bottom_right.tga", (1117, 882, 527, 34)),
        ("prompt_select", "prompts/load_select.tga", (765, 586, 230, 68)),
        ("prompt_back", "prompts/load_back.tga", (1070, 586, 220, 68)),
    ],
    "configure": [
        ("utility_left", "common/utility_left.tga", (236, 47, 314, 811)),
        ("utility_top", "common/utility_top.tga", (495, 189, 1052, 33)),
        ("utility_top_right", "common/utility_top_right.tga", (1551, 189, 144, 42)),
        ("utility_right", "common/utility_right.tga", (1566, 236, 128, 538)),
        ("utility_bottom_left", "common/utility_bottom_left.tga", (558, 792, 794, 66)),
        ("utility_bottom_right", "common/utility_bottom_right.tga", (1359, 792, 335, 66)),
        ("title", "configure/title.tga", (530, 78, 760, 82)),
        ("audio", "configure/audio.tga", (723, 294, 619, 104)),
        ("video", "configure/video.tga", (723, 425, 619, 104)),
        ("controller", "configure/controller.tga", (723, 556, 619, 104)),
    ],
    "audio": [
        ("title", "audio/title.tga", (530, 78, 620, 82)),
        ("effects", "audio/effects.tga", (553, 281, 960, 77)),
        ("music", "audio/music.tga", (553, 378, 937, 77)),
        ("voice", "audio/voice.tga", (553, 476, 960, 76)),
        ("sound", "audio/sound.tga", (553, 573, 700, 120)),
        ("prompt_accept_high", "prompts/accept_high.tga", (1460, 40, 250, 50)),
        ("prompt_cancel_high", "prompts/cancel_high.tga", (1460, 96, 250, 50)),
    ],
    "video": [
        ("title", "video/title.tga", (530, 78, 1030, 82)),
        ("corner_tl", "video/corner_tl.tga", (173, 9, 300, 225)),
        ("corner_tr", "video/corner_tr.tga", (1447, 9, 300, 225)),
        ("corner_bl", "video/corner_bl.tga", (173, 665, 300, 225)),
        ("corner_br", "video/corner_br.tga", (1447, 665, 300, 225)),
        ("instructions", "video/instructions.tga", (560, 285, 815, 220)),
        ("prompt_switch", "prompts/switch_corners.tga", (850, 610, 430, 70)),
        ("prompt_default", "prompts/default.tga", (645, 690, 240, 70)),
        ("prompt_accept", "prompts/accept.tga", (975, 690, 250, 70)),
        ("prompt_cancel", "prompts/cancel.tga", (1300, 690, 250, 70)),
    ],
    "controller": [
        ("title", "controller/title.tga", (280, 50, 960, 95)),
        ("frame_top_left", "controller/frame_top_left.tga", (198, 154, 1236, 34)),
        ("frame_top_right", "controller/frame_top_right.tga", (1442, 154, 282, 170)),
        ("frame_left_upper", "controller/frame_left_upper.tga", (198, 188, 119, 298)),
        ("frame_left_middle", "controller/frame_left_middle.tga", (198, 491, 119, 356)),
        ("frame_left_lower", "controller/frame_left_lower.tga", (198, 854, 119, 45)),
        ("frame_right_upper", "controller/frame_right_upper.tga", (1605, 265, 119, 170)),
        ("frame_right_lower", "controller/frame_right_lower.tga", (1605, 441, 119, 458)),
        ("frame_bottom_left", "controller/frame_bottom_left.tga", (317, 847, 849, 52)),
        ("frame_bottom_right", "controller/frame_bottom_right.tga", (1173, 847, 551, 52)),
        ("left_callouts", "controller/left_callouts.tga", (338, 194, 300, 653)),
        ("center_pad", "controller/center_pad.tga", (705, 240, 530, 300)),
        ("analog_left", "controller/analog_left.tga", (650, 585, 320, 262)),
        ("analog_right", "controller/analog_right.tga", (996, 585, 320, 262)),
        ("right_callouts", "controller/right_callouts.tga", (1337, 194, 270, 653)),
    ],
}

PAD_EXISTING = (
    OUT / "main/button_0_normal.tga",
    OUT / "main/button_1_selected.tga",
    OUT / "main/button_2_selected.tga",
    OUT / "main/button_3_selected.tga",
    OUT / "main/button_4_selected.tga",
    OUT / "main/button_5_selected.tga",
)

MASKS = {
    "main/left_lcars_top.tga": (
        (245, 25, 204, 130),
        (220, 230, 229, 81),
    ),
}


def viewport_image(path: Path) -> Image.Image:
    if not path.exists():
        raise FileNotFoundError(path)
    image = Image.open(path).convert("RGBA")
    x, y, w, h = VIEWPORT
    return image.crop((x, y, x + w, y + h))


def next_power_of_two(value: int) -> int:
    out = 1
    while out < value:
        out <<= 1
    return out


def pad_to_power_of_two(crop: Image.Image) -> Image.Image:
    pot_w = next_power_of_two(crop.width)
    pot_h = next_power_of_two(crop.height)
    if crop.width == pot_w and crop.height == pot_h:
        return crop

    padded = Image.new("RGBA", (pot_w, pot_h))
    padded.paste(crop, (0, 0))

    if crop.width < pot_w:
        edge = crop.crop((crop.width - 1, 0, crop.width, crop.height))
        for x in range(crop.width, pot_w):
            padded.paste(edge, (x, 0))

    if crop.height < pot_h:
        edge = padded.crop((0, crop.height - 1, pot_w, crop.height))
        for y in range(crop.height, pot_h):
            padded.paste(edge, (0, y))

    return padded


def output_path(rel: str) -> Path:
    if rel.startswith("special/"):
        return MENU_OUT / rel
    return OUT / rel


def save_crop(source: Image.Image, rel: str, box: tuple[int, int, int, int]) -> None:
    x, y, w, h = box
    crop = source.crop((x, y, x + w, y + h))
    for mask in MASKS.get(rel, ()):
        mx, my, mw, mh = mask
        crop.paste((0, 0, 0, 255), (mx, my, mx + mw, my + mh))
    crop = pad_to_power_of_two(crop)
    dest = output_path(rel)
    dest.parent.mkdir(parents=True, exist_ok=True)
    crop.save(dest)
    print(f"{dest} source={w}x{h} texture={crop.width}x{crop.height}")


def main() -> int:
    for stale in STALE_OUTPUTS:
        stale.unlink(missing_ok=True)
    for ref_name, entries in CROPS.items():
        source = viewport_image(REFS[ref_name])
        for _name, rel, box in entries:
            save_crop(source, rel, box)
    for path in PAD_EXISTING:
        if path.exists():
            image = Image.open(path).convert("RGBA")
            padded = pad_to_power_of_two(image)
            padded.save(path)
            print(f"{path} preserved={image.width}x{image.height} texture={padded.width}x{padded.height}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
