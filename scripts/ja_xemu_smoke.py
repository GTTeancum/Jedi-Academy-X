#!/usr/bin/env python3
import argparse
import os
import socket
import subprocess
import time
import re
import ctypes
from ctypes import wintypes

from PIL import Image, ImageDraw, ImageGrab


XEMU_DIR = r"C:\Games\Emulators\Xemu"
XEMU_BASE = os.path.join(XEMU_DIR, "xemu.exe")
XEMU_JA = os.path.join(XEMU_DIR, "xemu_ja.exe")
OUT_DIR = os.path.join("scripts", "output")
XEMU_TOML = os.path.join(XEMU_DIR, "xemu.toml")

SMOKE_KEYBOARD_MAP = """
[input.keyboard_controller_scancode_map]
a = 4
b = 5
x = 27
y = 28
white = 30
black = 31
ltrigger = 26
rtrigger = 22
dpad_left = 22
dpad_right = 9
dpad_up = 8
dpad_down = 7
lstick_left = 22
lstick_right = 9
lstick_up = 8
lstick_down = 7
lstick_btn = 29
rstick_left = 13
rstick_right = 15
rstick_up = 12
rstick_down = 14
rstick_btn = 16
start = 40
back = 42
guide = 10
""".strip()


user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
MAPVK_VK_TO_VSC = 0
KEYEVENTF_EXTENDEDKEY = 0x0001
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_SCANCODE = 0x0008
SWP_NOSIZE = 0x0001
SWP_NOMOVE = 0x0002
HWND_TOP = 0
HWND_TOPMOST = -1
HWND_NOTOPMOST = -2
PW_RENDERFULLCONTENT = 0x00000002


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


class RGBQUAD(ctypes.Structure):
    _fields_ = [
        ("rgbBlue", ctypes.c_byte),
        ("rgbGreen", ctypes.c_byte),
        ("rgbRed", ctypes.c_byte),
        ("rgbReserved", ctypes.c_byte),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [
        ("bmiHeader", BITMAPINFOHEADER),
        ("bmiColors", RGBQUAD * 1),
    ]


def find_window_for_pid(pid):
    result = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def enum_proc(hwnd, _lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        proc_id = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(proc_id))
        if proc_id.value == pid:
            length = user32.GetWindowTextLengthW(hwnd)
            title = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, title, length + 1)
            result.append((hwnd, title.value))
            return False
        return True

    user32.EnumWindows(enum_proc, 0)
    return result[0] if result else (None, "")


def capture_window(pid, path):
    hwnd, title = find_window_for_pid(pid)
    if not hwnd:
        return False, "no window"
    rect = wintypes.RECT()
    if not user32.GetWindowRect(hwnd, ctypes.byref(rect)):
        return False, "GetWindowRect failed"
    width = rect.right - rect.left
    height = rect.bottom - rect.top
    if width <= 0 or height <= 0:
        return False, "empty window rect"
    try:
        user32.ShowWindow(hwnd, 5)
        user32.SetForegroundWindow(hwnd)
    except Exception:
        pass
    time.sleep(0.25)
    try:
        # PrintWindow often returns a stale OpenGL front buffer for Xemu.  A
        # real desktop crop is more truthful once the window has focus.
        img = ImageGrab.grab((rect.left, rect.top, rect.right, rect.bottom))
        img.save(path)
        return True, "%s %dx%d foreground-grab" % (title or "<untitled>", width, height)
    except Exception as exc:
        printed, print_detail = print_window(hwnd, width, height, path)
        if printed:
            return True, "%s %dx%d hwnd-print; grab=%s" % (title or "<untitled>", width, height, exc)
        return False, "grab=%s; print=%s" % (exc, print_detail)
    finally:
        try:
            user32.SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE)
        except Exception:
            pass


def print_window(hwnd, width, height, path):
    hdc_window = user32.GetWindowDC(hwnd)
    if not hdc_window:
        return False, "GetWindowDC failed"
    hdc_mem = gdi32.CreateCompatibleDC(hdc_window)
    hbmp = gdi32.CreateCompatibleBitmap(hdc_window, width, height)
    if not hdc_mem or not hbmp:
        if hbmp:
            gdi32.DeleteObject(hbmp)
        if hdc_mem:
            gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(hwnd, hdc_window)
        return False, "CreateCompatibleBitmap failed"

    old = gdi32.SelectObject(hdc_mem, hbmp)
    ok = user32.PrintWindow(hwnd, hdc_mem, PW_RENDERFULLCONTENT)
    if not ok:
        ok = user32.PrintWindow(hwnd, hdc_mem, 0)

    bmp_info = BITMAPINFO()
    bmp_info.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmp_info.bmiHeader.biWidth = width
    bmp_info.bmiHeader.biHeight = -height
    bmp_info.bmiHeader.biPlanes = 1
    bmp_info.bmiHeader.biBitCount = 32
    bmp_info.bmiHeader.biCompression = 0
    buf_len = width * height * 4
    buf = ctypes.create_string_buffer(buf_len)
    got = 0
    if ok:
        got = gdi32.GetDIBits(hdc_mem, hbmp, 0, height, buf, ctypes.byref(bmp_info), 0)

    gdi32.SelectObject(hdc_mem, old)
    gdi32.DeleteObject(hbmp)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(hwnd, hdc_window)

    if not ok or got == 0:
        return False, "PrintWindow failed"

    img = Image.frombuffer("RGB", (width, height), buf.raw, "raw", "BGRX", 0, 1)
    extrema = img.getextrema()
    if all(lo == hi == 0 for lo, hi in extrema):
        return False, "PrintWindow black"
    img.save(path)
    return True, "PrintWindow"


def click_window_relative(pid, rel_x, rel_y):
    hwnd, _title = find_window_for_pid(pid)
    if not hwnd:
        return False
    rect = wintypes.RECT()
    if not user32.GetWindowRect(hwnd, ctypes.byref(rect)):
        return False
    x = rect.left + rel_x
    y = rect.top + rel_y
    try:
        user32.ShowWindow(hwnd, 5)
        user32.SetForegroundWindow(hwnd)
    except Exception:
        pass
    user32.SetCursorPos(x, y)
    user32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.03)
    user32.mouse_event(0x0004, 0, 0, 0, 0)
    return True


VK_CODES = {
    "enter": 0x0D,
    "return": 0x0D,
    "space": 0x20,
    "esc": 0x1B,
    "escape": 0x1B,
    "up": 0x26,
    "down": 0x28,
    "left": 0x25,
    "right": 0x27,
    "tab": 0x09,
    "backspace": 0x08,
    "1": 0x31,
    "2": 0x32,
    "3": 0x33,
    "4": 0x34,
    "5": 0x35,
    "a": 0x41,
    "b": 0x42,
    "c": 0x43,
    "d": 0x44,
    "e": 0x45,
    "f": 0x46,
    "i": 0x49,
    "j": 0x4A,
    "k": 0x4B,
    "l": 0x4C,
    "o": 0x4F,
    "w": 0x57,
    "x": 0x58,
    "y": 0x59,
    "s": 0x53,
}


def press_key(pid, name, hold=0.18):
    hwnd, _title = find_window_for_pid(pid)
    if hwnd:
        try:
            user32.ShowWindow(hwnd, 5)
            user32.SetForegroundWindow(hwnd)
        except Exception:
            pass
    vk = VK_CODES.get(name.lower())
    if vk is None:
        return False
    scan = user32.MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)
    flags = KEYEVENTF_SCANCODE
    if name.lower() in ("up", "down", "left", "right"):
        flags |= KEYEVENTF_EXTENDEDKEY
    user32.keybd_event(vk, scan, flags, 0)
    time.sleep(hold)
    user32.keybd_event(vk, scan, flags | KEYEVENTF_KEYUP, 0)
    return True


def parse_key_schedule(spec):
    events = []
    if not spec:
        return events
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        when, key = part.split(":", 1)
        hold = 0.18
        if "*" in key:
            key, hold_spec = key.split("*", 1)
            hold = float(hold_spec)
        events.append([float(when), key.strip(), hold, False])
    return sorted(events, key=lambda e: e[0])


def parse_click_schedule(spec):
    events = []
    if not spec:
        return events
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        when, xy = part.split(":", 1)
        x, y = xy.split("x", 1)
        events.append([float(when), int(x), int(y), False])
    return sorted(events, key=lambda e: e[0])


def parse_xy_pair(spec):
    x, y = spec.lower().split("x", 1)
    return int(x), int(y)


def resolve_map_symbol(symbol):
    map_paths = [
        os.path.join("code", "x_exe", "Release", "default.map"),
        os.path.join("code", "x_exe", "Release", "ja-release.map"),
        os.path.join("codemp", "x_exe", "Release", "default.map"),
        os.path.join("codemp", "x_exe", "Release", "jamp.map"),
        os.path.join("codemp", "x_exe", "Release", "jamp-release.map"),
    ]
    pattern = re.compile(r"\b%s\b\s+([0-9a-fA-F]{8})\b" % re.escape(symbol))
    for path in map_paths:
        if not os.path.exists(path):
            continue
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                for line in f:
                    match = pattern.search(line)
                    if match:
                        return int(match.group(1), 16), path
        except OSError:
            pass
    return None, None


def resolve_symbol_offsets(base_symbol, symbols):
    base, _base_path = resolve_map_symbol(base_symbol)
    offsets = {}
    if base is None:
        return offsets
    for name in symbols:
        addr, _path = resolve_map_symbol(name)
        if addr is not None and addr >= base:
            offsets[name] = (addr - base) // 4
    return offsets


def write_contact_sheet(paths, dest):
    images = []
    for path in paths:
        try:
            images.append((path, Image.open(path).convert("RGB")))
        except Exception:
            pass
    if not images:
        return False

    thumb_w = 420
    label_h = 24
    padding = 10
    cols = 2
    thumbs = []
    for path, img in images:
        scale = float(thumb_w) / float(img.width)
        thumb_h = max(1, int(img.height * scale))
        thumb = img.resize((thumb_w, thumb_h))
        thumbs.append((path, thumb))

    rows = (len(thumbs) + cols - 1) // cols
    cell_h = max(t.height for _p, t in thumbs) + label_h + padding
    sheet = Image.new("RGB", (cols * (thumb_w + padding) + padding, rows * cell_h + padding), "white")
    draw = ImageDraw.Draw(sheet)
    for idx, (path, thumb) in enumerate(thumbs):
        row = idx // cols
        col = idx % cols
        x = padding + col * (thumb_w + padding)
        y = padding + row * cell_h
        sheet.paste(thumb, (x, y + label_h))
        draw.text((x, y), os.path.basename(path), fill=(0, 0, 0))
    sheet.save(dest)
    return True


def close_windows_security_alerts():
    closed = []
    wm_close = 0x0010

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def enum_proc(hwnd, _lparam):
        if user32.IsWindowVisible(hwnd):
            length = user32.GetWindowTextLengthW(hwnd)
            if length:
                title = ctypes.create_unicode_buffer(length + 1)
                user32.GetWindowTextW(hwnd, title, length + 1)
                if "Windows Security" in title.value:
                    user32.PostMessageW(hwnd, wm_close, 0, 0)
                    closed.append(title.value)
        return True

    user32.EnumWindows(enum_proc, 0)
    return closed


def ensure_xemu_copy():
    if not os.path.exists(XEMU_JA):
        import shutil
        shutil.copy2(XEMU_BASE, XEMU_JA)


def monitor_connect(port, timeout=12.0):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            sock = socket.socket()
            sock.settimeout(2.0)
            sock.connect(("127.0.0.1", port))
            time.sleep(0.2)
            try:
                sock.recv(65536)
            except Exception:
                pass
            return sock
        except Exception as exc:
            last = exc
            time.sleep(0.25)
    raise RuntimeError("monitor not ready: %s" % last)


def monitor_cmd(sock, cmd, wait=0.2):
    sock.sendall((cmd + "\r\n").encode("ascii"))
    time.sleep(wait)
    data = b""
    sock.settimeout(1.0)
    try:
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
    except Exception:
        pass
    return data.decode("utf-8", errors="replace")


def parse_monitor_words(text, base_addr=None):
    text = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)
    words = []
    for line in text.splitlines():
        if ":" not in line:
            continue
        addr_text, values_text = line.split(":", 1)
        try:
            addr = int(addr_text.strip(), 16)
        except ValueError:
            continue
        values = []
        for token in re.findall(r"\b(?:0x)?[0-9a-fA-F]{8}\b", values_text):
            try:
                values.append(int(token, 16))
            except ValueError:
                pass
        if base_addr is None or addr == base_addr or values:
            words.extend(values)
    return words


def probe_xblog_physical_addr(sock, boot_va, preferred_delta, log):
    candidates = []
    for delta in (preferred_delta, 0x2a4000, 0x284000):
        if delta and delta not in candidates:
            candidates.append(delta)

    for delta in candidates:
        addr = boot_va - delta
        if addr < 4:
            continue
        try:
            reply = monitor_cmd(sock, "xp/8wx 0x%08x" % (addr - 4), 0.3)
        except OSError:
            continue
        words = parse_monitor_words(reply)
        if len(words) >= 5 and words[0] == 0x53504A41 and words[4] == 0x48424653:
            log("xblog_probe matched delta=0x%x addr=0x%08x" % (delta, addr))
            return addr
        log("xblog_probe miss delta=0x%x addr=0x%08x words=%s" %
            (delta, addr, ",".join("0x%08x" % w for w in words[:5])))
    return None


def monitor_read_u32(sock, addr, phys_delta=None):
    cmd = "x"
    read_addr = addr
    if phys_delta is not None:
        cmd = "xp"
        read_addr = addr - phys_delta
    reply = monitor_cmd(sock, "%s/1wx 0x%08x" % (cmd, read_addr), 0.2)
    words = parse_monitor_words(reply, read_addr)
    return words[0] if words else None


def monitor_pmemsave(sock, phys_addr, byte_count, path):
    monitor_path = os.path.abspath(path).replace("\\", "/")
    try:
        if os.path.exists(path):
            os.remove(path)
    except Exception:
        pass
    monitor_cmd(sock, "pmemsave 0x%08x 0x%x %s" % (phys_addr, byte_count, monitor_path), 1.0)
    return os.path.exists(path) and os.path.getsize(path) >= byte_count


def monitor_framebuffer_dump(sock, path, phys_delta=None):
    if sock is None:
        return False, "monitor disabled"

    required = [
        "_g_SPXBFramebufferData",
        "_g_SPXBFramebufferPitch",
        "_g_SPXBFramebufferWidth",
        "_g_SPXBFramebufferHeight",
    ]
    addrs = {}
    for name in required:
        resolved = resolve_map_symbol(name)
        if not resolved:
            return False, "framebuffer symbol missing: %s" % name
        addrs[name] = resolved[0]

    data = monitor_read_u32(sock, addrs["_g_SPXBFramebufferData"], phys_delta) or 0
    pitch = monitor_read_u32(sock, addrs["_g_SPXBFramebufferPitch"], phys_delta) or 0
    width = monitor_read_u32(sock, addrs["_g_SPXBFramebufferWidth"], phys_delta) or 0
    height = monitor_read_u32(sock, addrs["_g_SPXBFramebufferHeight"], phys_delta) or 0
    if data == 0 or pitch == 0 or width == 0 or height == 0:
        return False, "framebuffer telemetry empty data=0x%08x pitch=%u size=%ux%u" % (data, pitch, width, height)
    if width > 1920 or height > 1080 or pitch < width * 4:
        return False, "framebuffer telemetry invalid data=0x%08x pitch=%u size=%ux%u" % (data, pitch, width, height)

    bin_path = os.path.abspath(os.path.splitext(path)[0] + ".fb.bin")
    byte_count = pitch * height
    phys_addr = data & 0x03ffffff
    if not monitor_pmemsave(sock, phys_addr, byte_count, bin_path):
        return False, "pmemsave missing data=0x%08x phys=0x%08x bytes=%u" % (data, phys_addr, byte_count)

    try:
        raw = open(bin_path, "rb").read()
        img = Image.new("RGB", (width, height))
        pixels = bytearray()
        for y in range(height):
            base = y * pitch
            for x in range(width):
                p = base + x * 4
                if p + 2 < len(raw):
                    pixels += bytes((raw[p + 2], raw[p + 1], raw[p]))
                else:
                    pixels += b"\x00\x00\x00"
        img.frombytes(bytes(pixels))
        img.save(path)
        try:
            os.remove(bin_path)
        except Exception:
            pass
        return True, "guest framebuffer %ux%u pitch=%u data=0x%08x" % (width, height, pitch, data)
    except Exception as exc:
        return False, "framebuffer convert failed: %s" % exc


def monitor_screendump(sock, path, phys_delta=None):
    return monitor_framebuffer_dump(sock, path, phys_delta)

    # Kept for reference: this Xemu build exposes a QEMU monitor without the
    # screendump command, so screenshots must come from guest framebuffer memory.
    if sock is None:
        return False, "monitor disabled"
    ppm_path = os.path.abspath(os.path.splitext(path)[0] + ".ppm")
    monitor_path = ppm_path.replace("\\", "/")
    try:
        if os.path.exists(ppm_path):
            os.remove(ppm_path)
        monitor_cmd(sock, 'screendump "%s"' % monitor_path, 1.0)
        if not os.path.exists(ppm_path) or os.path.getsize(ppm_path) == 0:
            return False, "screendump missing"
        img = Image.open(ppm_path).convert("RGB")
        img.save(path)
        try:
            os.remove(ppm_path)
        except Exception:
            pass
        return True, "monitor screendump %dx%d" % (img.width, img.height)
    except Exception as exc:
        return False, "screendump failed: %s" % exc


def dump_monitor_state(sock, prefix, tag, dump_specs, log):
    try:
        regs = monitor_cmd(sock, "info registers", 0.1)
    except OSError as exc:
        log("%s_registers_unavailable=%s" % (tag, exc))
        return ""
    regs_path = os.path.abspath("%s_%s_registers.txt" % (prefix, tag))
    with open(regs_path, "w", encoding="utf-8", errors="replace") as f:
        f.write(regs)
    log("%s_registers=%s" % (tag, regs_path))

    for spec in dump_specs:
        parts = spec.split(":", 2)
        if len(parts) < 2:
            log("%s_dump_mem_invalid=%s" % (tag, spec))
            continue
        try:
            addr = int(parts[0], 0)
            length = int(parts[1], 0)
        except ValueError:
            log("%s_dump_mem_invalid=%s" % (tag, spec))
            continue
        name = parts[2] if len(parts) > 2 and parts[2] else "mem_%08x" % addr
        words = (length + 3) // 4
        try:
            dump = monitor_cmd(sock, "x/%dwx 0x%08x" % (words, addr), 2.0)
        except OSError as exc:
            log("%s_dump_mem_unavailable=%s err=%s" % (tag, spec, exc))
            continue
        safe_name = re.sub(r"[^A-Za-z0-9_.-]", "_", name)
        dump_path = os.path.abspath("%s_%s_%s.txt" % (prefix, tag, safe_name))
        with open(dump_path, "w", encoding="utf-8", errors="replace") as f:
            f.write(dump)
        log("%s_dump_mem=%s" % (tag, dump_path))

    return regs


def dump_physical_memory(sock, prefix, specs, log):
    if sock is None:
        return
    for spec in specs:
        parts = spec.split(":", 2)
        if len(parts) < 2:
            log("final_dump_phys_invalid=%s" % spec)
            continue
        try:
            addr = int(parts[0], 0)
            length = int(parts[1], 0)
        except ValueError:
            log("final_dump_phys_invalid=%s" % spec)
            continue
        name = parts[2] if len(parts) > 2 and parts[2] else "phys_%08x" % addr
        safe_name = re.sub(r"[^A-Za-z0-9_.-]", "_", name)
        dump_path = os.path.abspath("%s_final_%s.bin" % (prefix, safe_name))
        monitor_path = dump_path.replace("\\", "/")
        try:
            reply = monitor_cmd(sock, 'pmemsave 0x%08x 0x%x "%s"' % (addr, length, monitor_path), 4.0)
        except OSError as exc:
            log("final_dump_phys_unavailable=%s err=%s" % (spec, exc))
            continue
        log("final_dump_phys=%s" % dump_path)
        if reply.strip():
            log("final_dump_phys_reply=%s" % reply.strip().replace("\n", "\\n"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", required=True)
    parser.add_argument("--name", default="ja_xemu")
    parser.add_argument("--port", type=int, default=4460)
    parser.add_argument("--duration", type=int, default=60)
    parser.add_argument("--interval", type=int, default=5)
    parser.add_argument("--hdd", default=r"C:\Games\Emulators\Xemu\HDD\jediacademy_hdd.qcow2")
    parser.add_argument("--xemu-exe", default=XEMU_JA,
                        help="Xemu executable to launch. Defaults to the JA-isolated copy.")
    parser.add_argument("--explicit-xbox-machine", action="store_true",
                        help="Launch with explicit OG Xbox machine/BIOS/HDD/DVD args instead of relying on xemu.toml.")
    parser.add_argument("--visible", action="store_true")
    parser.add_argument("--headless", action="store_true",
                        help="Run without a display window. Implies --display none and skips screenshots.")
    parser.add_argument("--no-screenshots", action="store_true",
                        help="Skip interval screenshots without changing the display backend.")
    parser.add_argument("--display", choices=["xemu", "sdl", "none", "egl-headless", "nographic"],
                        default="xemu",
                        help="Xemu/QEMU display backend. Use none or egl-headless for unattended runs.")
    parser.add_argument("--no-monitor", action="store_true")
    parser.add_argument("--keys", default="")
    parser.add_argument("--monitor-keys", default="")
    parser.add_argument("--clicks", default="")
    parser.add_argument("--video-debug", action="store_true",
                        help="For visible Xemu runs, click Debug > Video to open Xemu's FPS/MSPF overlay.")
    parser.add_argument("--video-debug-clicks", default="128x34,128x88",
                        help="Window-relative menu/item clicks for --video-debug: debugMenuXxY,videoItemXxY.")
    parser.add_argument("--smoke-keymap", action="store_true")
    parser.add_argument("--xemu-arg", action="append", default=[])
    parser.add_argument("--keep-net", action="store_true")
    parser.add_argument("--dump-mem", action="append", default=[],
                        help="Dump guest memory before closing monitor: addr:length[:name]")
    parser.add_argument("--dump-phys", action="append", default=[],
                        help="Dump guest physical memory before closing monitor: addr:length[:name]")
    parser.add_argument("--watch-cr2", default="",
                        help="Poll registers and dump memory when CR2 matches this value")
    parser.add_argument("--poll-xblog", action="store_true",
                        help="Poll SP/MP XBLog counters through the monitor during the run.")
    parser.add_argument("--poll-xblog-kind", choices=["auto", "sp", "mp"], default="auto",
                        help="Prefer SP or MP XBLog symbols when polling. Auto preserves the historical probe order.")
    parser.add_argument("--poll-xblog-addr", default="",
                        help="Address of g_*XBBootPhase. Empty means resolve _g_SPXBBootPhase from the current map.")
    parser.add_argument("--poll-xblog-phys-delta", default="0x284000",
                        help="Auto-resolved XBLog VA minus physical monitor address. Use 0 to poll virtual x/ memory.")
    args = parser.parse_args()

    os.makedirs(OUT_DIR, exist_ok=True)
    ensure_xemu_copy()
    xemu_exe = os.path.abspath(args.xemu_exe)

    stamp = time.strftime("%Y%m%d_%H%M%S")
    prefix = os.path.join(OUT_DIR, "%s_%s" % (args.name, stamp))
    raw_log = prefix + ".xemu.txt"
    report = prefix + ".report.txt"
    toml_backup = None

    if os.path.exists(XEMU_TOML):
        with open(XEMU_TOML, "r", encoding="utf-8", errors="replace") as f:
            toml_backup = f.read()
        toml = re.sub(r"hdd_path = '.*'", lambda _m: "hdd_path = '%s'" % args.hdd, toml_backup)
        toml = re.sub(r"dvd_path = '.*'", lambda _m: "dvd_path = '%s'" % args.iso, toml)
        if not args.keep_net:
            toml = re.sub(r"(?m)^enable = true$", "enable = false", toml, count=1)
        if args.smoke_keymap:
            toml = re.sub(r"(?ms)\n?\[input\.keyboard_controller_scancode_map\]\n.*?(?=\n\[|\Z)", "", toml)
            toml = toml.rstrip() + "\n\n" + SMOKE_KEYBOARD_MAP + "\n"
        with open(XEMU_TOML, "w", encoding="utf-8", errors="replace") as f:
            f.write(toml)

    if args.explicit_xbox_machine:
        argv = [
            xemu_exe,
            "-machine", r"xbox,bootrom=C:\Games\Emulators\Xemu\MCPX\mcpx_1.0.bin,short-animation=on,kernel-irqchip=off,avpack=hdtv",
            "-device", r"smbus-storage,file=C:\Games\Emulators\Xemu\EEPROM\eeprom.bin",
            "-bios", r"C:\Games\Emulators\Xemu\BIOS\xbox-4627_debug.bin",
            "-m", "64",
            "-drive", "index=0,media=disk,file=%s,locked=on" % args.hdd,
            "-drive", "index=1,media=cdrom,file=%s" % args.iso,
        ]
    else:
        argv = [
            xemu_exe,
            "-dvd_path", args.iso,
        ]
    if not args.no_monitor:
        argv += ["-monitor", "tcp:127.0.0.1:%d,server,nowait" % args.port]
    if not args.visible:
        # Keep the xemu display backend, but start it minimized-ish via QEMU's
        # normal window. We still capture frames through the monitor.
        pass
    display_mode = "none" if args.headless else args.display
    if display_mode != "xemu":
        if display_mode == "nographic":
            argv += ["-nographic"]
        else:
            argv += ["-display", display_mode]
    argv += args.xemu_arg

    lines = []
    def log(msg):
        print(msg)
        lines.append(msg)

    log("launch: %s" % " ".join(argv))
    logf = open(raw_log, "w", encoding="utf-8", errors="replace")
    proc = subprocess.Popen(argv, cwd=XEMU_DIR, stdout=logf, stderr=subprocess.STDOUT)
    log("pid=%d" % proc.pid)
    key_events = parse_key_schedule(args.keys)
    monitor_key_events = parse_key_schedule(args.monitor_keys)
    click_events = parse_click_schedule(args.clicks)
    shot_paths = []
    watch_cr2 = int(args.watch_cr2, 0) if args.watch_cr2 else None
    watch_done = False
    next_watch = 0.0
    xblog_addr = None
    xblog_mode = ""
    xblog_mp_pos_addr = None
    xblog_mp_write_addr = None
    xblog_va_for_probe = None
    if args.poll_xblog:
        if args.poll_xblog_addr:
            xblog_addr = int(args.poll_xblog_addr, 0)
            xblog_cmd = "x"
            xblog_mode = "sp"
        else:
            prefer_mp = args.poll_xblog_kind == "mp"
            prefer_sp = args.poll_xblog_kind == "sp"
            if prefer_mp:
                xblog_addr = None
            else:
                xblog_addr, xblog_map = resolve_map_symbol("_g_SPXBBootPhase")
            if prefer_sp and xblog_addr is None:
                xblog_addr = 0x00d90528
                xblog_cmd = "x"
                xblog_mode = "sp"
                log("xblog_symbol=_g_SPXBBootPhase unresolved fallback=x/0x%08x" % xblog_addr)
            elif not prefer_mp and xblog_addr is not None:
                phys_delta = int(args.poll_xblog_phys_delta, 0)
                xblog_cmd = "xp" if phys_delta else "x"
                xblog_va = xblog_addr
                xblog_va_for_probe = xblog_va if phys_delta else None
                xblog_addr = xblog_addr - phys_delta
                xblog_mode = "sp"
                log("xblog_symbol=_g_SPXBBootPhase va=0x%08x poll=%s addr=0x%08x map=%s" %
                    (xblog_va, xblog_cmd, xblog_addr, xblog_map))
            else:
                mp_pos, mp_pos_map = resolve_map_symbol("_g_XBLogMirrorPos")
                mp_write, _mp_write_map = resolve_map_symbol("_g_XBLogWriteCount")
                if mp_pos is not None and mp_write is not None:
                    phys_delta = int(args.poll_xblog_phys_delta, 0)
                    xblog_cmd = "xp" if phys_delta else "x"
                    xblog_mode = "mp"
                    xblog_addr = mp_pos - phys_delta
                    xblog_mp_pos_addr = mp_pos - phys_delta
                    xblog_mp_write_addr = mp_write - phys_delta
                    log("xblog_symbol=_g_XBLogMirrorPos va=0x%08x poll=%s addr=0x%08x map=%s" %
                        (mp_pos, xblog_cmd, xblog_mp_pos_addr, mp_pos_map))
                elif not prefer_mp:
                    xblog_addr = 0x00d90528
                    xblog_cmd = "x"
                    xblog_mode = "sp"
                    log("xblog_symbol=_g_SPXBBootPhase unresolved fallback=x/0x%08x" % xblog_addr)
                else:
                    log("xblog_symbol=_g_XBLogMirrorPos unresolved")
    next_xblog_poll = 0.0
    last_xblog_write_count = None
    xblog_offsets = resolve_symbol_offsets("_g_SPXBBootPhase", [
        "_g_SPXBRenderBackendMsec",
        "_g_SPXBFakeGLPrimitiveCalls",
        "_g_SPXBFakeGLPrimitiveVerts",
        "_g_SPXBFakeGLStateFlushes",
        "_g_SPXBRenderSplitShader",
        "_g_SPXBRenderSplitFog",
        "_g_SPXBRenderSplitDlight",
        "_g_SPXBRenderSplitEntity",
        "_g_SPXBRenderSplitFinal",
        "_g_SPXBRenderSplitFlush",
    ])
    video_debug_done = False
    try:
        video_debug_menu_click, video_debug_item_click = [
            parse_xy_pair(part.strip()) for part in args.video_debug_clicks.split(",", 1)
        ]
    except Exception:
        video_debug_menu_click = (128, 34)
        video_debug_item_click = (128, 88)
    capture_enabled = (not args.no_screenshots) and display_mode not in ("none", "egl-headless", "nographic")

    sock = None
    try:
        if not args.no_monitor:
            sock = monitor_connect(args.port)
            log("monitor=ready port=%d" % args.port)
            if args.poll_xblog and xblog_va_for_probe is not None:
                probed_addr = probe_xblog_physical_addr(
                    sock, xblog_va_for_probe, int(args.poll_xblog_phys_delta, 0), log)
                if probed_addr is not None:
                    xblog_addr = probed_addr
        else:
            log("monitor=disabled")
        start = time.time()
        next_shot = 0.0
        shot = 0
        while time.time() - start < args.duration:
            elapsed = time.time() - start
            rc = proc.poll()
            if rc is not None:
                log("process_exit t=%.1f rc=%s" % (elapsed, rc))
                break

            if args.video_debug and capture_enabled and not video_debug_done and elapsed >= 2.0:
                ok_menu = click_window_relative(proc.pid, video_debug_menu_click[0], video_debug_menu_click[1])
                time.sleep(0.15)
                ok_item = click_window_relative(proc.pid, video_debug_item_click[0], video_debug_item_click[1])
                log("video_debug t=%.1f menu=%s,%s ok=%s item=%s,%s ok=%s" %
                    (elapsed,
                     video_debug_menu_click[0], video_debug_menu_click[1], ok_menu,
                     video_debug_item_click[0], video_debug_item_click[1], ok_item))
                video_debug_done = True

            if sock is not None and watch_cr2 is not None and not watch_done and elapsed >= next_watch:
                next_watch = elapsed + 0.08
                regs = monitor_cmd(sock, "info registers", 0.05)
                m = re.search(r"CR2=([0-9a-fA-F]{8})", regs)
                if m and int(m.group(1), 16) == watch_cr2:
                    log("watch_cr2_hit t=%.2f cr2=0x%08x" % (elapsed, watch_cr2))
                    watch_regs_path = os.path.abspath("%s_watch_cr2_registers.txt" % prefix)
                    with open(watch_regs_path, "w", encoding="utf-8", errors="replace") as f:
                        f.write(regs)
                    log("watch_cr2_registers=%s" % watch_regs_path)
                    dump_monitor_state(sock, prefix, "watch_cr2", args.dump_mem, log)
                    watch_done = True

            if sock is not None and xblog_addr is not None and elapsed >= next_xblog_poll:
                next_xblog_poll = elapsed + max(0.5, float(args.interval))
                try:
                    if xblog_mode == "mp":
                        reply = monitor_cmd(sock, "%s/1wx 0x%08x" % (xblog_cmd, xblog_mp_pos_addr), 0.4)
                        words = parse_monitor_words(reply, xblog_mp_pos_addr)
                        reply_write = monitor_cmd(sock, "%s/1wx 0x%08x" % (xblog_cmd, xblog_mp_write_addr), 0.4)
                        write_words = parse_monitor_words(reply_write, xblog_mp_write_addr)
                        if words and write_words:
                            mirror_pos = words[0]
                            write_count = write_words[0]
                            delta = 0 if last_xblog_write_count is None else write_count - last_xblog_write_count
                            last_xblog_write_count = write_count
                            log("xblog t=%.1f mp mirror=%u writes=%u delta=%d" %
                                (elapsed, mirror_pos, write_count, delta))
                        else:
                            debug_path = os.path.abspath("%s_xblog_%04d.txt" % (prefix, int(elapsed * 10)))
                            with open(debug_path, "w", encoding="utf-8", errors="replace") as f:
                                f.write(reply)
                                f.write("\n--- write ---\n")
                                f.write(reply_write)
                            log("xblog t=%.1f mp unreadable raw=%s" % (elapsed, debug_path))
                        continue

                    poll_words = max(64, max(xblog_offsets.values() or [0]) + 8)
                    reply = monitor_cmd(sock, "%s/%dwx 0x%08x" % (xblog_cmd, poll_words, xblog_addr), 0.4)
                    words = parse_monitor_words(reply, xblog_addr)
                    if len(words) >= 9:
                        boot_phase = words[0]
                        mirror_pos = words[1]
                        write_count = words[2]
                        heartbeat_magic = words[3]
                        heartbeat_count = words[4]
                        heartbeat_frame = words[5]
                        heartbeat_rt = words[6]
                        heartbeat_st = words[7]
                        heartbeat_fps10 = words[8]
                        delta = 0 if last_xblog_write_count is None else write_count - last_xblog_write_count
                        last_xblog_write_count = write_count
                        def word_for(symbol):
                            idx = xblog_offsets.get(symbol)
                            if idx is None or idx >= len(words):
                                return 0
                            return words[idx]
                        render_backend = word_for("_g_SPXBRenderBackendMsec")
                        primitive_calls = word_for("_g_SPXBFakeGLPrimitiveCalls")
                        primitive_verts = word_for("_g_SPXBFakeGLPrimitiveVerts")
                        state_flushes = word_for("_g_SPXBFakeGLStateFlushes")
                        split_shader = word_for("_g_SPXBRenderSplitShader")
                        split_fog = word_for("_g_SPXBRenderSplitFog")
                        split_dlight = word_for("_g_SPXBRenderSplitDlight")
                        split_entity = word_for("_g_SPXBRenderSplitEntity")
                        split_final = word_for("_g_SPXBRenderSplitFinal")
                        split_flush = word_for("_g_SPXBRenderSplitFlush")
                        log("xblog t=%.1f boot=0x%08x mirror=%u writes=%u delta=%d hb=0x%08x count=%u frame=%u rt=%u st=%u fps=%.1f be=%u prim=%u verts=%u state=%u split=%u/%u/%u/%u final=%u flush=%u" %
                            (elapsed, boot_phase, mirror_pos, write_count, delta,
                             heartbeat_magic, heartbeat_count, heartbeat_frame,
                             heartbeat_rt, heartbeat_st, heartbeat_fps10 / 10.0,
                             render_backend, primitive_calls, primitive_verts,
                             state_flushes, split_shader, split_fog, split_dlight,
                             split_entity, split_final, split_flush))
                    elif len(words) >= 3:
                        boot_phase = words[0]
                        mirror_pos = words[1]
                        write_count = words[2]
                        delta = 0 if last_xblog_write_count is None else write_count - last_xblog_write_count
                        last_xblog_write_count = write_count
                        log("xblog t=%.1f boot=0x%08x mirror=%u writes=%u delta=%d heartbeat=unreadable" %
                            (elapsed, boot_phase, mirror_pos, write_count, delta))
                    else:
                        debug_path = os.path.abspath("%s_xblog_%04d.txt" % (prefix, int(elapsed * 10)))
                        with open(debug_path, "w", encoding="utf-8", errors="replace") as f:
                            f.write(reply)
                        log("xblog t=%.1f unreadable=%s raw=%s" %
                            (elapsed, reply.strip().replace("\n", "\\n")[:240], debug_path))
                except OSError as exc:
                    log("xblog t=%.1f unavailable=%s" % (elapsed, exc))
                    xblog_addr = None

            if elapsed >= next_shot:
                close_windows_security_alerts()
                if display_mode not in ("none", "egl-headless", "nographic") and elapsed < 12.0:
                    click_window_relative(proc.pid, 747, 678)
                if capture_enabled:
                    time.sleep(0.1)
                    png = os.path.abspath("%s_%02d.png" % (prefix, shot))
                    xblog_phys_delta = None
                    if xblog_va_for_probe is not None and xblog_addr is not None:
                        xblog_phys_delta = xblog_va_for_probe - xblog_addr
                    ok, detail = monitor_screendump(sock, png, xblog_phys_delta)
                    if not ok and display_mode == "xemu":
                        win_ok, win_detail = capture_window(proc.pid, png)
                        if win_ok:
                            ok = True
                            detail = "%s; fallback=%s" % (detail, win_detail)
                    if ok:
                        shot_paths.append(png)
                    log("shot=%02d t=%.1f ok=%s bytes=%s detail=%s" %
                        (shot, elapsed, ok, os.path.getsize(png) if ok and os.path.exists(png) else 0, detail))
                else:
                    log("shot=%02d t=%.1f skipped display=%s" % (shot, elapsed, display_mode))
                shot += 1
                next_shot += args.interval

            for event in click_events:
                if not event[3] and elapsed >= event[0]:
                    ok = click_window_relative(proc.pid, event[1], event[2])
                    event[3] = True
                    log("click t=%.1f x=%d y=%d ok=%s" % (elapsed, event[1], event[2], ok))

            for event in key_events:
                if not event[3] and elapsed >= event[0]:
                    ok = press_key(proc.pid, event[1], event[2])
                    event[3] = True
                    log("key t=%.1f key=%s hold=%.2f ok=%s" % (elapsed, event[1], event[2], ok))

            for event in monitor_key_events:
                if not event[3] and elapsed >= event[0]:
                    if sock is None:
                        ok = False
                    else:
                        monitor_cmd(sock, "sendkey %s" % event[1], max(0.05, event[2]))
                        ok = True
                    event[3] = True
                    log("monitor_key t=%.1f key=%s hold=%.2f ok=%s" % (elapsed, event[1], event[2], ok))

            time.sleep(0.25)

        if proc.poll() is None:
            log("alive_at_end pid=%d" % proc.pid)

        if sock is not None:
            dump_monitor_state(sock, prefix, "final", args.dump_mem, log)
            dump_physical_memory(sock, prefix, args.dump_phys, log)
    finally:
        if sock:
            try:
                sock.close()
            except Exception:
                pass
        if proc.poll() is None:
            proc.terminate()
            time.sleep(1.0)
            if proc.poll() is None:
                proc.kill()
        logf.close()
        if toml_backup is not None:
            with open(XEMU_TOML, "w", encoding="utf-8", errors="replace") as f:
                f.write(toml_backup)

    contact = prefix + "_contact.png"
    if write_contact_sheet(shot_paths, contact):
        log("contact=%s" % os.path.abspath(contact))

    try:
        with open(raw_log, encoding="utf-8", errors="replace") as f:
            tail = f.readlines()[-80:]
        log("--- xemu tail ---")
        for line in tail:
            log(line.rstrip())
    except Exception as exc:
        log("raw_log_read_failed=%s" % exc)

    with open(report, "w", encoding="utf-8", errors="replace") as f:
        f.write("\n".join(lines) + "\n")
    print("report=%s" % report)


if __name__ == "__main__":
    main()
