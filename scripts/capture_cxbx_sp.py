#!/usr/bin/env python3
"""Run the SP XBE in CXBX and request random post-load renderer captures.

This harness deliberately does not capture, move, focus, minimize, or inspect
desktop windows. Cxbx-Reloaded does not expose a native snapshot facility in
the build we have, so screenshots are requested through the running XBE's
instrumented backbuffer dump and logs are used as the authoritative smoke
evidence when no dump is produced.
"""

from __future__ import annotations

import argparse
import ctypes
import random
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path


def parse_args() -> argparse.Namespace:
    repo_default = Path(__file__).resolve().parents[1]
    return argparse.ArgumentParser(description=__doc__).parse_args(namespace=argparse.Namespace(
        repo=repo_default,
        cxbx_loader=Path(r"C:\Programming\GitHub\Jedi-Academy-X\CXBXR\cxbxr-ldr-project2.exe"),
        level="borg1",
        screenshot_count=3,
        random_window_seconds=24.0,
        active_grace_seconds=4.0,
        watchdog_seconds=300.0,
        heartbeat_loaded_server_time=72000,
        seed=None,
    ))


def build_parser() -> argparse.ArgumentParser:
    repo_default = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=repo_default)
    parser.add_argument(
        "--cxbx-loader",
        type=Path,
        default=Path(r"C:\Programming\GitHub\Jedi-Academy-X\CXBXR\cxbxr-ldr-project2.exe"),
    )
    parser.add_argument(
        "--cxbx-workdir",
        type=Path,
        default=None,
        help="Working directory/data root for CXBX. Defaults to the loader directory.",
    )
    parser.add_argument("--level", default="borg1")
    parser.add_argument("--screenshot-count", type=int, default=3)
    parser.add_argument("--random-window-seconds", type=float, default=24.0)
    parser.add_argument("--active-grace-seconds", type=float, default=4.0)
    parser.add_argument("--watchdog-seconds", type=float, default=300.0)
    parser.add_argument("--heartbeat-loaded-server-time", type=int, default=72000)
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--smoke-forward", type=int, default=127)
    parser.add_argument("--smoke-side", type=int, default=0)
    parser.add_argument("--smoke-yaw", type=int, default=0)
    parser.add_argument("--smoke-start", type=int, default=71000)
    parser.add_argument("--smoke-end", type=int, default=112000)
    parser.add_argument("--attack-start", type=int, default=76000)
    parser.add_argument("--attack-end", type=int, default=100000)
    parser.add_argument("--extra-command", default="")
    parser.add_argument("--active-command", default="cam_disable")
    parser.add_argument("--active-command-server-time", type=int, default=72000)
    parser.add_argument(
        "--scripted-intro",
        action="store_true",
        help="Only select the level and request captures; do not inject cam_disable or smoke gameplay cvars.",
    )
    parser.add_argument(
        "--normal-time",
        action="store_true",
        help="Do not force smoke fasttime/timescale; use this when measuring runtime FPS.",
    )
    parser.add_argument(
        "--allow-partial",
        action="store_true",
        help="Exit zero after boot/capture even if vertical-slice gameplay proof counters are missing.",
    )
    return parser


def read_text(path: Path) -> str:
    if not path.exists():
        return ""
    try:
        return path.read_text(errors="ignore")
    except OSError:
        return ""


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def runtime_roots(release: Path, cxbx_root: Path) -> list[Path]:
    roots = [release]
    emu_partition = cxbx_root / "EmuDisk" / "Partition1"
    for candidate in (emu_partition, cxbx_root):
        if candidate not in roots:
            roots.append(candidate)
    return roots


def first_existing_path(roots: list[Path], name: str, min_mtime: float | None = None) -> Path | None:
    for root in roots:
        candidate = root / name
        if candidate.exists():
            if min_mtime is None:
                return candidate
            try:
                if candidate.stat().st_mtime >= min_mtime:
                    return candidate
            except OSError:
                pass
    if min_mtime is not None:
        return None
    return roots[0] / name


def read_runtime_text(roots: list[Path], name: str, min_mtime: float | None = None) -> tuple[Path, str]:
    path = first_existing_path(roots, name, min_mtime)
    if path is None:
        return roots[0] / name, ""
    return path, read_text(path)


def remove_runtime_file(roots: list[Path], name: str) -> None:
    for root in roots:
        try:
            path = root / name
            if path.exists():
                path.unlink()
        except OSError:
            pass


def write_runtime_file(roots: list[Path], name: str, text: str) -> None:
    for root in roots:
        try:
            write_text(root / name, text)
        except OSError:
            pass


def setup_release_inputs(release: Path, cxbx_root: Path, args: argparse.Namespace) -> None:
    roots = runtime_roots(release, cxbx_root)
    cleanup_names = [
        "ef_sp_log.txt",
        "ef_sp_cxbx_present_throttle.txt",
        "ef_sp_screenshot_request.txt",
        "ef_sp_screenshot_pending.txt",
        "ef_sp_screenshot_log.txt",
        "ef_sp_commands.txt",
        "ef_sp_postmap_commands.txt",
        "ef_sp_smoke_harness.txt",
        "ef_sp_active_commands.txt",
        "ef_sp_active_command_time.txt",
    ]
    for name in cleanup_names:
        remove_runtime_file(roots, name)

    write_runtime_file(roots, "ef_sp_level.txt", args.level + "\n")
    write_runtime_file(roots, "ef_sp_smoke_harness.txt", "1\n")
    if args.scripted_intro:
        return

    smoke_cmd = (
        "cam_disable;"
        "set stefx_smoke_input 1;"
        "set stefx_smoke_aim 1;"
        "set stefx_smoke_wake_ai 1;"
        "set stefx_smoke_unlock_player 1;"
        "set stefx_smoke_ready_weapon 1;"
        "set stefx_smoke_stage_enemy 1;"
        f"set stefx_smoke_input_forward {args.smoke_forward};"
        f"set stefx_smoke_input_side {args.smoke_side};"
        f"set stefx_smoke_input_yaw {args.smoke_yaw};"
        f"set stefx_smoke_input_start {args.smoke_start};"
        f"set stefx_smoke_input_attack_start {args.attack_start};"
        f"set stefx_smoke_input_attack_end {args.attack_end};"
        f"set stefx_smoke_input_end {args.smoke_end}"
    )
    if args.extra_command:
        smoke_cmd = args.extra_command.rstrip(";") + ";" + smoke_cmd
    if args.normal_time:
        postmap_cmd = "set stefx_smoke_fasttime 0;set timescale 1;" + smoke_cmd
    else:
        postmap_cmd = (
            "set stefx_smoke_fasttime 1;"
            "set stefx_smoke_fasttime_msec 2000;"
            "set timescale 40;"
            + smoke_cmd
        )
    write_runtime_file(roots, "ef_sp_commands.txt", smoke_cmd + "\n")
    write_runtime_file(roots, "ef_sp_postmap_commands.txt", postmap_cmd + "\n")
    if args.active_command:
        write_runtime_file(roots, "ef_sp_active_commands.txt", args.active_command.rstrip(";") + "\n")
        write_runtime_file(roots, "ef_sp_active_command_time.txt", f"{args.active_command_server_time}\n")


def cleanup_smoke_inputs(release: Path, cxbx_root: Path) -> None:
    roots = runtime_roots(release, cxbx_root)
    cleanup_names = [
        "ef_sp_commands.txt",
        "ef_sp_postmap_commands.txt",
        "ef_sp_smoke_harness.txt",
        "ef_sp_active_commands.txt",
        "ef_sp_active_command_time.txt",
        "ef_sp_cxbx_present_throttle.txt",
        "ef_sp_screenshot_request.txt",
        "ef_sp_screenshot_pending.txt",
    ]
    for name in cleanup_names:
        remove_runtime_file(roots, name)


def request_renderer_capture(
    release: Path,
    cxbx_root: Path,
    output: Path,
    window_pid: int | None = None,
    timeout_seconds: float = 20.0,
) -> tuple[str, bool]:
    roots = runtime_roots(release, cxbx_root)
    request_paths = [root / "ef_sp_screenshot_request.txt" for root in roots]
    candidates = []
    for root in roots:
        candidates.append(root / "ef_sp_backbuffer.bmp")
        candidates.append(root / "ef_sp_xgshot.bmp")
    for candidate in candidates:
        try:
            candidate.unlink()
        except OSError:
            pass

    wrote_request = False
    for request in request_paths:
        try:
            write_text(request, "capture\n")
            wrote_request = True
        except OSError:
            pass
    if not wrote_request:
        return f"renderer capture request write failed paths={';'.join(str(p) for p in request_paths)}", False

    deadline = time.monotonic() + timeout_seconds
    found: Path | None = None
    while time.monotonic() < deadline:
        for candidate in candidates:
            try:
                if candidate.exists() and candidate.stat().st_size > 54:
                    found = candidate
                    break
            except OSError:
                pass
        if found:
            break
        time.sleep(0.25)

    if not found:
        if window_pid is not None:
            window_result, window_nonblank = capture_window_client_bmp(window_pid, output)
            if window_result:
                return window_result, window_nonblank
        _, log_text = read_runtime_text(roots, "ef_sp_log.txt")
        log_capture = extract_log_encoded_capture(log_text)
        if log_capture:
            src_width, src_height, src_rgb = log_capture
            output = output.with_suffix(".bmp")
            scaled_rgb = scale_rgb_nearest(src_width, src_height, src_rgb, 640, 480)
            write_bmp_rgb(output, 640, 480, scaled_rgb)
            for request in request_paths:
                try:
                    request.unlink()
                except OSError:
                    pass
            nonblank, signal = analyze_bmp_signal(output)
            return (
                f"{output} rendererLogCapture={src_width}x{src_height}->640x480 "
                f"bytes={output.stat().st_size} {signal}",
                nonblank,
            )
        for request in request_paths:
            try:
                request.unlink()
            except OSError:
                pass
        return f"renderer capture timeout requests={';'.join(str(p) for p in request_paths)}", False

    output = output.with_suffix(".bmp")
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(found, output)
    for request in request_paths:
        try:
            request.unlink()
        except OSError:
            pass
    nonblank, signal = analyze_bmp_signal(output)
    if not nonblank and window_pid is not None:
        window_result, window_nonblank = capture_window_client_bmp(window_pid, output)
        if window_result:
            return (
                f"{output} rendererCapture={found} bytes={found.stat().st_size} {signal}; "
                f"windowFallback={window_result}",
                window_nonblank,
            )
    return f"{output} rendererCapture={found} bytes={found.stat().st_size} {signal}", nonblank


HEARTBEAT_RE = re.compile(r"JA: FRAME_HEARTBEAT completedFrame=(\d+) realtime=(\d+) serverTime=(-?\d+).*?\bfps=([0-9.]+)")
LOG_SHOT_BEGIN_RE = re.compile(r"STEFX: renderer screenshot log begin .* out=(\d+)x(\d+)")
LOG_SHOT_CHUNK_RE = re.compile(
    r"STEFX: renderer screenshot log chunk row=(\d+) x=(\d+) pixels=(\d+) data=([0-9a-fA-F]+)"
)
LOG_SHOT_END_RE = re.compile(r"STEFX: renderer screenshot log end")


def write_bmp_rgb(path: Path, width: int, height: int, rgb: bytes) -> None:
    row_stride = (width * 3 + 3) & ~3
    image_bytes = row_stride * height
    header = bytearray(54)
    header[0:2] = b"BM"
    header[2:6] = (54 + image_bytes).to_bytes(4, "little")
    header[10:14] = (54).to_bytes(4, "little")
    header[14:18] = (40).to_bytes(4, "little")
    header[18:22] = width.to_bytes(4, "little")
    header[22:26] = height.to_bytes(4, "little")
    header[26:28] = (1).to_bytes(2, "little")
    header[28:30] = (24).to_bytes(2, "little")
    header[34:38] = image_bytes.to_bytes(4, "little")

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(header)
        pad = b"\0" * (row_stride - width * 3)
        for y in range(height - 1, -1, -1):
            row = bytearray()
            base = y * width * 3
            for x in range(width):
                r = rgb[base + x * 3 + 0]
                g = rgb[base + x * 3 + 1]
                b = rgb[base + x * 3 + 2]
                row.extend((b, g, r))
            f.write(row)
            f.write(pad)


def analyze_bmp_signal(path: Path) -> tuple[bool, str]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        return False, f"signal=unreadable error={exc}"

    if len(data) < 54 or data[0:2] != b"BM":
        return False, f"signal=invalidBmp bytes={len(data)}"

    pixel_offset = int.from_bytes(data[10:14], "little")
    dib_size = int.from_bytes(data[14:18], "little")
    if dib_size < 40 or len(data) < 14 + dib_size:
        return False, f"signal=unsupportedDib dib={dib_size} bytes={len(data)}"

    width = int.from_bytes(data[18:22], "little", signed=True)
    height_signed = int.from_bytes(data[22:26], "little", signed=True)
    planes = int.from_bytes(data[26:28], "little")
    bpp = int.from_bytes(data[28:30], "little")
    compression = int.from_bytes(data[30:34], "little")
    if width <= 0 or height_signed == 0 or planes != 1 or compression != 0 or bpp not in (24, 32):
        return False, (
            f"signal=unsupportedBmp width={width} height={height_signed} "
            f"planes={planes} bpp={bpp} compression={compression}"
        )

    height = abs(height_signed)
    row_stride = ((width * bpp + 31) // 32) * 4
    if pixel_offset + row_stride * height > len(data):
        return False, (
            f"signal=truncated width={width} height={height} bpp={bpp} "
            f"need={pixel_offset + row_stride * height} bytes={len(data)}"
        )

    step_x = max(1, width // 64)
    step_y = max(1, height // 48)
    samples = 0
    bright = 0
    luma_total = 0.0
    max_luma = 0.0
    bytes_per_pixel = bpp // 8
    top_down = height_signed < 0

    for y in range(0, height, step_y):
        stored_y = y if top_down else (height - 1 - y)
        row_base = pixel_offset + stored_y * row_stride
        for x in range(0, width, step_x):
            pix = row_base + x * bytes_per_pixel
            b = data[pix]
            g = data[pix + 1]
            r = data[pix + 2]
            luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
            samples += 1
            luma_total += luma
            if luma > max_luma:
                max_luma = luma
            if luma > 4.0:
                bright += 1

    avg_luma = luma_total / samples if samples else 0.0
    nonblank = bright >= 4 and max_luma > 8.0 and avg_luma > 1.0
    return nonblank, (
        f"signal={'nonblank' if nonblank else 'blank'} width={width} height={height} "
        f"bpp={bpp} avgLuma={avg_luma:.2f} maxLuma={max_luma:.2f} bright={bright}/{samples}"
    )


def capture_window_client_bmp(pid: int, output: Path) -> tuple[str, bool]:
    if sys.platform != "win32":
        return "", False

    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32

    class RECT(ctypes.Structure):
        _fields_ = [
            ("left", ctypes.c_long),
            ("top", ctypes.c_long),
            ("right", ctypes.c_long),
            ("bottom", ctypes.c_long),
        ]

    class POINT(ctypes.Structure):
        _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ("biSize", ctypes.c_uint32),
            ("biWidth", ctypes.c_long),
            ("biHeight", ctypes.c_long),
            ("biPlanes", ctypes.c_uint16),
            ("biBitCount", ctypes.c_uint16),
            ("biCompression", ctypes.c_uint32),
            ("biSizeImage", ctypes.c_uint32),
            ("biXPelsPerMeter", ctypes.c_long),
            ("biYPelsPerMeter", ctypes.c_long),
            ("biClrUsed", ctypes.c_uint32),
            ("biClrImportant", ctypes.c_uint32),
        ]

    class RGBQUAD(ctypes.Structure):
        _fields_ = [
            ("rgbBlue", ctypes.c_ubyte),
            ("rgbGreen", ctypes.c_ubyte),
            ("rgbRed", ctypes.c_ubyte),
            ("rgbReserved", ctypes.c_ubyte),
        ]

    class BITMAPINFO(ctypes.Structure):
        _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", RGBQUAD * 1)]

    hwnds: list[tuple[int, int, str]] = []
    EnumWindowsProc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    @EnumWindowsProc
    def enum_windows(hwnd: int, _lparam: int) -> bool:
        if not user32.IsWindowVisible(hwnd):
            return True

        process_id = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(process_id))
        if process_id.value != pid:
            return True

        rect = RECT()
        if not user32.GetClientRect(hwnd, ctypes.byref(rect)):
            return True
        width = rect.right - rect.left
        height = rect.bottom - rect.top
        if width <= 32 or height <= 32:
            return True

        title_buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, title_buf, len(title_buf))
        hwnds.append((width * height, hwnd, title_buf.value))
        return True

    user32.EnumWindows(enum_windows, 0)
    if not hwnds:
        return f"{output} windowFallback=none pid={pid}", False

    _area, hwnd, title = max(hwnds, key=lambda item: item[0])
    rect = RECT()
    if not user32.GetClientRect(hwnd, ctypes.byref(rect)):
        return f"{output} windowFallback=getClientRectFailed pid={pid}", False
    width = rect.right - rect.left
    height = rect.bottom - rect.top
    if width <= 0 or height <= 0:
        return f"{output} windowFallback=emptyClient pid={pid} hwnd=0x{hwnd:x}", False

    point = POINT(0, 0)
    if not user32.ClientToScreen(hwnd, ctypes.byref(point)):
        return f"{output} windowFallback=clientToScreenFailed pid={pid} hwnd=0x{hwnd:x}", False

    screen_dc = user32.GetDC(None)
    if not screen_dc:
        return f"{output} windowFallback=getDcFailed pid={pid} hwnd=0x{hwnd:x}", False

    mem_dc = gdi32.CreateCompatibleDC(screen_dc)
    bitmap = gdi32.CreateCompatibleBitmap(screen_dc, width, height)
    old_bitmap = None
    try:
        if not mem_dc or not bitmap:
            return f"{output} windowFallback=createBitmapFailed pid={pid} hwnd=0x{hwnd:x}", False
        old_bitmap = gdi32.SelectObject(mem_dc, bitmap)
        srccopy = 0x00CC0020
        captureblt = 0x40000000
        if not gdi32.BitBlt(mem_dc, 0, 0, width, height, screen_dc, point.x, point.y, srccopy | captureblt):
            return f"{output} windowFallback=bitBltFailed pid={pid} hwnd=0x{hwnd:x}", False

        row_stride = (width * 3 + 3) & ~3
        image_bytes = row_stride * height
        bmi = BITMAPINFO()
        bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        bmi.bmiHeader.biWidth = width
        bmi.bmiHeader.biHeight = height
        bmi.bmiHeader.biPlanes = 1
        bmi.bmiHeader.biBitCount = 24
        bmi.bmiHeader.biCompression = 0
        bmi.bmiHeader.biSizeImage = image_bytes
        pixels = ctypes.create_string_buffer(image_bytes)
        dib_rgb_colors = 0
        rows = gdi32.GetDIBits(mem_dc, bitmap, 0, height, pixels, ctypes.byref(bmi), dib_rgb_colors)
        if rows != height:
            return (
                f"{output} windowFallback=getDIBitsFailed rows={rows}/{height} "
                f"pid={pid} hwnd=0x{hwnd:x}"
            ), False

        output = output.with_suffix(".bmp")
        output.parent.mkdir(parents=True, exist_ok=True)
        header = bytearray(54)
        header[0:2] = b"BM"
        header[2:6] = (54 + image_bytes).to_bytes(4, "little")
        header[10:14] = (54).to_bytes(4, "little")
        header[14:18] = (40).to_bytes(4, "little")
        header[18:22] = width.to_bytes(4, "little", signed=True)
        header[22:26] = height.to_bytes(4, "little", signed=True)
        header[26:28] = (1).to_bytes(2, "little")
        header[28:30] = (24).to_bytes(2, "little")
        header[34:38] = image_bytes.to_bytes(4, "little")
        with output.open("wb") as f:
            f.write(header)
            f.write(pixels.raw)

        nonblank, signal = analyze_bmp_signal(output)
        return (
            f"{output} windowCapture=hwnd:0x{hwnd:x} pid={pid} title='{title}' "
            f"bytes={output.stat().st_size} {signal}",
            nonblank,
        )
    finally:
        if old_bitmap:
            gdi32.SelectObject(mem_dc, old_bitmap)
        if bitmap:
            gdi32.DeleteObject(bitmap)
        if mem_dc:
            gdi32.DeleteDC(mem_dc)
        user32.ReleaseDC(None, screen_dc)


def extract_log_encoded_capture(log_text: str) -> tuple[int, int, bytes] | None:
    begins = list(LOG_SHOT_BEGIN_RE.finditer(log_text))
    for begin in reversed(begins):
        end = LOG_SHOT_END_RE.search(log_text, begin.end())
        if not end:
            continue

        width = int(begin.group(1))
        height = int(begin.group(2))
        if width <= 0 or height <= 0 or width > 4096 or height > 4096:
            continue

        pixels = bytearray(width * height * 3)
        chunks = 0
        segment = log_text[begin.end():end.start()]
        for match in LOG_SHOT_CHUNK_RE.finditer(segment):
            row = int(match.group(1))
            x = int(match.group(2))
            count = int(match.group(3))
            data = match.group(4)
            if row < 0 or row >= height or x < 0 or x >= width:
                continue
            for n in range(count):
                offset = n * 6
                if offset + 6 > len(data) or x + n >= width:
                    break
                dst = (row * width + x + n) * 3
                pixels[dst + 0] = int(data[offset:offset + 2], 16)
                pixels[dst + 1] = int(data[offset + 2:offset + 4], 16)
                pixels[dst + 2] = int(data[offset + 4:offset + 6], 16)
            chunks += 1

        if chunks:
            return width, height, bytes(pixels)
    return None


def scale_rgb_nearest(width: int, height: int, rgb: bytes, out_width: int, out_height: int) -> bytes:
    out = bytearray(out_width * out_height * 3)
    for y in range(out_height):
        src_y = (y * height) // out_height
        for x in range(out_width):
            src_x = (x * width) // out_width
            src = (src_y * width + src_x) * 3
            dst = (y * out_width + x) * 3
            out[dst:dst + 3] = rgb[src:src + 3]
    return bytes(out)


def parse_latest_heartbeat(log_text: str) -> tuple[int, int, int] | None:
    latest = None
    for match in HEARTBEAT_RE.finditer(log_text):
        latest = (int(match.group(1)), int(match.group(2)), int(match.group(3)))
    return latest


def parse_heartbeat_fps(log_text: str) -> tuple[float, float, int]:
    values = [float(match.group(4)) for match in HEARTBEAT_RE.finditer(log_text)]
    if not values:
        return 0.0, 0.0, 0
    tail = values[-20:]
    return min(tail), sum(tail) / len(tail), len(values)


def config_has_xbox_binds(repo: Path, release: Path) -> bool:
    candidates = [
        release / "BaseEF" / "default.cfg",
        repo / "base" / "default.cfg",
    ]
    required_patterns = [
        r"seta\s+m_pitch\s+\"-0\.022\"",
        r"seta\s+sensitivity\s+\"2\"",
        r"seta\s+sensitivityY\s+\"2\"",
        r"seta\s+joy_deadzone\s+\"0\.18\"",
        r"bind\s+JOY12\s+\+attack",
        r"bind\s+JOY10\s+\+altattack",
        r"bind\s+JOY11\s+\+moveup",
        r"bind\s+JOY9\s+\+movedown",
        r"bind\s+JOY15\s+\+use",
        r"bind\s+JOY16\s+\"toggle cl_run\"",
        r"Back=JOY1",
        r"Right Trigger=JOY12",
        r"A=JOY15",
    ]
    for candidate in candidates:
        text = read_text(candidate)
        if text and all(re.search(pattern, text) for pattern in required_patterns):
            return True
    return False


def log_has_fatal(log_text: str) -> bool:
    fatal_needles = [
        "Z_Malloc(): Out of memory",
        "Com_Error",
        "Sys_Error",
        "ERR_FATAL",
        "fatal error",
    ]
    lower = log_text.lower()
    return any(needle.lower() in lower for needle in fatal_needles)


def count_matches(log_text: str, pattern: str) -> int:
    return len(re.findall(pattern, log_text))


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    repo = args.repo.resolve()
    cxbx_loader = args.cxbx_loader.resolve()
    release = repo / "build" / "release"
    cxbx_root = (args.cxbx_workdir.resolve() if args.cxbx_workdir else cxbx_loader.parent)
    roots = runtime_roots(release, cxbx_root)
    xbe = release / "default.xbe"
    log_path = first_existing_path(roots, "ef_sp_log.txt")
    output_dir = repo / "scripts" / "output"
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    summary_path = output_dir / f"cxbx_sp_renderer_{args.level}_{stamp}.summary.txt"
    stdout_path = output_dir / f"cxbx_sp_renderer_{args.level}_{stamp}.stdout.txt"
    stderr_path = output_dir / f"cxbx_sp_renderer_{args.level}_{stamp}.stderr.txt"

    if not cxbx_loader.exists():
        print(f"missing CXBX loader: {cxbx_loader}", file=sys.stderr)
        return 2
    if not xbe.exists():
        print(f"missing XBE: {xbe}", file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    due_offsets = sorted(
        args.active_grace_seconds + rng.random() * max(0.0, args.random_window_seconds)
        for _ in range(max(0, args.screenshot_count))
    )

    setup_release_inputs(release, cxbx_root, args)
    output_dir.mkdir(parents=True, exist_ok=True)
    launch_wall_time = time.time()

    stdout_file = stdout_path.open("w", encoding="utf-8", errors="ignore")
    stderr_file = stderr_path.open("w", encoding="utf-8", errors="ignore")
    proc = subprocess.Popen(
        [str(cxbx_loader), "/load", str(xbe)],
        cwd=str(cxbx_root),
        stdout=stdout_file,
        stderr=stderr_file,
        text=True,
    )

    start = time.monotonic()
    active_at: float | None = None
    captures: list[str] = []
    captured_count = 0
    nonblank_captured_count = 0
    blank_capture_count = 0
    last_heartbeat: tuple[int, int, int] | None = None
    fatal = False
    try:
        while True:
            elapsed = time.monotonic() - start
            if elapsed > args.watchdog_seconds:
                break

            log_path, log_text = read_runtime_text(roots, "ef_sp_log.txt", launch_wall_time)
            fatal = log_has_fatal(log_text)
            heartbeat = parse_latest_heartbeat(log_text)
            if heartbeat:
                last_heartbeat = heartbeat

            loaded = "STEFX: CG_AddViewWeapon added" in log_text
            if heartbeat and heartbeat[2] >= args.heartbeat_loaded_server_time:
                loaded = True

            if active_at is None and loaded:
                active_at = time.monotonic()

            if active_at is not None and captured_count < len(due_offsets):
                active_elapsed = time.monotonic() - active_at
                if active_elapsed >= due_offsets[captured_count]:
                    capture_path = output_dir / (
                        f"{args.level}_renderer_{stamp}_{captured_count + 1:02d}.bmp"
                    )
                    frame_text = ""
                    if last_heartbeat:
                        frame_text = (
                            f" frame={last_heartbeat[0]}"
                            f" serverTime={last_heartbeat[2]}"
                        )
                    capture_result, capture_nonblank = request_renderer_capture(
                        release,
                        cxbx_root,
                        capture_path,
                        window_pid=proc.pid,
                    )
                    if capture_nonblank:
                        nonblank_captured_count += 1
                    else:
                        blank_capture_count += 1
                    captures.append(
                        f"{capture_result} at active+{active_elapsed:.1f}s{frame_text}"
                    )
                    captured_count += 1

            if fatal:
                break
            if active_at is not None and captured_count >= len(due_offsets):
                if not due_offsets:
                    active_elapsed = time.monotonic() - active_at
                    if active_elapsed < args.active_grace_seconds:
                        time.sleep(0.5)
                        continue
                # Keep one small cushion after the last grab so the log catches up.
                time.sleep(1.0)
                break
            if proc.poll() is not None:
                break
            time.sleep(0.5)
    finally:
        try:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=3)
        except Exception:
            if proc.poll() is None:
                proc.kill()
        stdout_file.close()
        stderr_file.close()
        cleanup_smoke_inputs(release, cxbx_root)

    log_path, log_text = read_runtime_text(roots, "ef_sp_log.txt", launch_wall_time)
    fatal = fatal or log_has_fatal(log_text)
    last_heartbeat = parse_latest_heartbeat(log_text) or last_heartbeat
    fps_min, fps_avg, fps_samples = parse_heartbeat_fps(log_text)
    xbox_binds_ok = config_has_xbox_binds(repo, release)
    evidence = {
        "mapRawBspLoads": count_matches(log_text, r"EF: CM_LoadMap raw BSP 'maps/borg1\.bsp'"),
        "rawLightmapLoads": count_matches(log_text, r"EF: R_LoadRawLightmaps map='maps/borg1\.bsp'"),
        "rawLightmapStats": count_matches(log_text, r"EF: RAW_LIGHTMAP_STATS index="),
        "activeWorldMultitexture": count_matches(log_text, r"EF: ACTIVE_MTEXTURE shader='textures/"),
        "stage1LightmapApplies": count_matches(
            log_text,
            r"EF: TEX_STAGE_APPLY stage=1 texid=(?:1[1-9]|[2-9][0-9]+)\b",
        ),
        "textureRebinds": count_matches(log_text, r"STEFX: FORCE_TEXTURE_REBIND"),
        "introImageLoads": count_matches(
            log_text,
            r"STEFX: INTRO_IMAGE create done name='textures/common/(70yearjourney|enemyspace|sevenspace|tuvokhazard)",
        ),
        "introBackgroundDraws": count_matches(
            log_text,
            r"STEFX: INTRO_DRAW .*shader='textures/common/(70yearjourney|enemyspace|sevenspace|tuvokhazard)'",
        ),
        "introScrollCommands": count_matches(log_text, r"STEFX: Q3_ScrollText send id='@scrolling1'"),
        "introScrollServerCommands": count_matches(log_text, r"STEFX: EF servercmd st scrolltext key='@scrolling1'"),
        "introScrollSetup": count_matches(log_text, r"STEFX: CG_ScrollText original ready key='@scrolling1'"),
        "introScrollDraws": count_matches(log_text, r"STEFX: CG_DrawScrollText original active"),
        "introVoiceStarts": count_matches(log_text, r"STEFX: Q3_PlaySound .*Captainslog1"),
        "introVoiceLoads": count_matches(log_text, r"STEFX: S_EndLoadSound loaded .*captainslog1\.wav"),
        "hudDraw2D": count_matches(log_text, r"STEFX: HUD Draw2D proof"),
        "soundPlays": count_matches(log_text, r"STEFX: QAL play"),
        "soundLooseReads": count_matches(log_text, r"STEFX: loose sound read direct=1"),
        "soundLooseReadOk": count_matches(
            log_text,
            r"STEFX: loose sound read direct=1 .* bytes=(?!0\b)\d+ error=0",
        ),
        "soundAssetLoads": count_matches(
            log_text,
            r"STEFX: (S_EndLoadSound loaded|Xbox WAV music loaded)",
        ),
        "viewWeaponAdds": count_matches(log_text, r"STEFX: CG_AddViewWeapon added"),
        "borgModelLoads": count_matches(
            log_text,
            r"STEFX: CG_RegisterClientModelname .* MDR 'models/players/borg[^']*' -> [1-9]\d*",
        ),
        "borgFallbacks": count_matches(
            log_text,
            r"STEFX: CG_RegisterClientRenderInfo fallback requested head='borg",
        ),
        "smokeInputApplied": count_matches(log_text, r"STEFX: smoke input applied"),
        "smokeInputMoving": count_matches(
            log_text,
            r"STEFX: smoke input applied .*\bmove=(?!\(0,0,0\))\(-?\d+,-?\d+,-?\d+\)",
        ),
        "smokeInputAttacking": count_matches(log_text, r"STEFX: smoke input applied .*attack=1"),
        "smokeAimTargets": count_matches(log_text, r"STEFX: smoke aim target"),
        "smokeStageEnemy": count_matches(log_text, r"STEFX: smoke stage enemy target="),
        "smokeUnlocks": count_matches(log_text, r"STEFX: smoke unlock player control"),
        "smokeReadyWeapon": count_matches(log_text, r"STEFX: smoke ready weapon"),
        "smokeAiWake": count_matches(log_text, r"STEFX: smoke wake enemy"),
        "controllerButtons": count_matches(log_text, r"STEFX: controller button"),
        "controllerAxes": count_matches(log_text, r"STEFX: controller axes"),
        "inputGateCleared": count_matches(log_text, r"STEFX: direct-map input gate cleared"),
        "xboxBindsInstalled": count_matches(
            log_text,
            r"STEFX: ((installed|confirmed|replaced) Xbox bind|Xbox controls preserving default\.cfg)",
        ) + (1 if xbox_binds_ok else 0),
        "fpsSamples": fps_samples,
        "fpsMinTail": round(fps_min, 1),
        "fpsAvgTail": round(fps_avg, 1),
        "fpsAcceptable": 1 if fps_min >= 30.0 and fps_avg >= 45.0 else 0,
        "clientMoveResults": count_matches(log_text, r"STEFX: ClientThink PM state .* moved=1"),
        "playerAttackCmds": count_matches(log_text, r"STEFX: ClientThink player attack probe"),
        "playerPmoveFireEvents": count_matches(log_text, r"STEFX: PM_AddEvent fire"),
        "playerClientFireEvents": count_matches(log_text, r"STEFX: ClientEvents fire"),
        "playerCgameFireEvents": count_matches(log_text, r"STEFX: CG_FireWeapon ent=0"),
        "projectileSnapshotEvents": count_matches(log_text, r"STEFX: engine EF CL_GetSnapshot event .* eType=52 event=38"),
        "playerFireWeapon": count_matches(log_text, r"STEFX: FireWeapon enter ent=0"),
        "playerDamageHits": count_matches(log_text, r"STEFX: G_Damage player hit"),
        "npcDamageHits": count_matches(log_text, r"STEFX: G_Damage player hit target=\d+ class='NPC'"),
        "npcPainEvents": count_matches(log_text, r"STEFX: NPC_Pain"),
        "npcEnemyAcquired": count_matches(log_text, r"STEFX: NPC_SetEnemy"),
        "npcSpawns": count_matches(log_text, r"STEFX: NPC_Begin"),
        "cgameCharacters": count_matches(log_text, r"STEFX: CG_Player valid ent="),
        "characterAnimSurfaces": count_matches(log_text, r"STEFX: R_AddAnimSurfaces"),
        "characterAnimSurfaceVisible": count_matches(
            log_text,
            r"STEFX: R_AddAnimSurfaces visible",
        ),
        "cgameCharacterSubmits": count_matches(
            log_text,
            r"STEFX: CG_Player submitted (legs|torso|head) ent=",
        ),
        "cgamePlayerAdds": count_matches(
            log_text,
            r"STEFX: CG_AddCEntity player visible-candidate ent=",
        ),
        "characterAnimSurfaceCullOut": count_matches(
            log_text,
            r"STEFX: R_AddAnimSurfaces cull out",
        ),
        "mdrPlaceholderSkips": count_matches(log_text, r"EF: skipping MDR placeholder render"),
    }
    if args.scripted_intro:
        required_groups = {
            "map": ("mapRawBspLoads",),
            "intro_image_load": ("introImageLoads",),
            "intro_background_draw": ("introBackgroundDraws",),
            "intro_scroll_command": ("introScrollCommands", "introScrollServerCommands"),
            "intro_scroll_setup": ("introScrollSetup",),
            "intro_scroll_draw": ("introScrollDraws",),
            "intro_voice_start": ("introVoiceStarts",),
            "intro_voice_load": ("introVoiceLoads",),
        }
    else:
        required_groups = {
            "map": ("mapRawBspLoads",),
            "lighting": ("rawLightmapLoads", "rawLightmapStats"),
            "textured_world": ("activeWorldMultitexture",),
            "stage1_lightmap": ("stage1LightmapApplies",),
            "texture_rebind": ("textureRebinds",),
            "visible_weapon": ("viewWeaponAdds",),
            "hud": ("hudDraw2D",),
            "sound_read": ("soundLooseReadOk", "soundAssetLoads"),
            "sound_play": ("soundPlays",),
            "input_gate": ("inputGateCleared",),
            "xbox_binds": ("xboxBindsInstalled",),
            "smoke_input": ("smokeInputMoving",),
            "movement": ("clientMoveResults",),
            "attack_cmd": ("smokeInputAttacking", "playerAttackCmds", "playerPmoveFireEvents"),
            "server_fire": ("playerPmoveFireEvents", "playerClientFireEvents", "playerFireWeapon"),
            "client_fire": ("playerCgameFireEvents", "projectileSnapshotEvents"),
            "characters_present": ("npcSpawns", "cgameCharacters"),
            "characters_visible": ("characterAnimSurfaceVisible", "cgameCharacterSubmits", "cgamePlayerAdds"),
            "ai_present": ("npcEnemyAcquired", "npcPainEvents", "npcDamageHits"),
            "weapon_interaction": ("playerDamageHits", "npcPainEvents", "npcDamageHits"),
        }
        if args.normal_time:
            required_groups["fps"] = ("fpsAcceptable",)
    missing_requirements = [
        name
        for name, keys in required_groups.items()
        if not any(evidence.get(key, 0) > 0 for key in keys)
    ]
    if evidence.get("mdrPlaceholderSkips", 0) > 0:
        missing_requirements.append("mdr_placeholder_render")
    if evidence.get("borgFallbacks", 0) > 0 or evidence.get("borgModelLoads", 0) <= 0:
        missing_requirements.append("borg_identity")
    required_capture_count = len(due_offsets)
    if required_capture_count > 0 and captured_count < required_capture_count:
        missing_requirements.append("renderer_screenshot")
    if required_capture_count > 0 and nonblank_captured_count < required_capture_count:
        missing_requirements.append("renderer_nonblank_screenshot")
    vertical_slice_pass = active_at is not None and not fatal and not missing_requirements
    scripted_intro_pass = args.scripted_intro and vertical_slice_pass
    active_elapsed_text = "n/a"
    if active_at is not None:
        active_elapsed_text = f"{time.monotonic() - active_at:.1f}"

    summary_lines = [
        f"level={args.level}",
        f"active={active_at is not None}",
        f"fatal={fatal}",
        f"verticalSlicePass={vertical_slice_pass}",
        f"scriptedIntroPass={scripted_intro_pass}",
        f"missingRequirements={','.join(missing_requirements) if missing_requirements else 'none'}",
        "desktopCapture=process-window-client-fallback",
        "captureSource=xbe-renderer-request,window-client-fallback",
        f"activeElapsedSeconds={active_elapsed_text}",
        f"dueOffsets={','.join(f'{x:.3f}' for x in due_offsets)}",
        f"rendererCaptureCount={captured_count}",
        f"rendererNonblankCaptureCount={nonblank_captured_count}",
        f"rendererBlankCaptureCount={blank_capture_count}",
        f"lastHeartbeat={last_heartbeat}",
        f"log={log_path}",
        f"stdout={stdout_path}",
        f"stderr={stderr_path}",
        "evidence:",
        *[f"{key}={value}" for key, value in evidence.items()],
        "captures:",
    ]
    summary_lines.extend(captures if captures else ["none"])
    write_text(summary_path, "\n".join(summary_lines) + "\n")
    print("\n".join(summary_lines))
    print(f"summary={summary_path}")

    if fatal or active_at is None or captured_count < required_capture_count:
        return 1
    if missing_requirements and not args.allow_partial:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
