#!/usr/bin/env python3
import argparse
import ctypes
import os
import socket
import subprocess
import time
import re

from PIL import Image, ImageDraw


XEMU_DIR = r"C:\Games\Emulators\Xemu"
XEMU_BASE = os.path.join(XEMU_DIR, "xemu.exe")
XEMU_JA = os.path.join(XEMU_DIR, "xemu_ja.exe")
OUT_DIR = os.path.join("scripts", "output")
XEMU_TOML = os.path.join(XEMU_DIR, "xemu.toml")
MAP_PATH_OVERRIDE = ""


def cap_windows_process_affinity(pid, core_count=6):
    if os.name != "nt":
        return None

    available = max(1, os.cpu_count() or 1)
    selected = min(max(1, core_count), available)
    logical_ids = list(range(0, available, 2))[:selected]
    if len(logical_ids) < selected:
        logical_ids = list(range(selected))
    affinity_mask = sum(1 << logical_id for logical_id in logical_ids)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = [
        ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.SetProcessAffinityMask.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t]
    kernel32.SetProcessAffinityMask.restype = ctypes.c_int
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel32.CloseHandle.restype = ctypes.c_int

    process_set_information = 0x0200
    handle = kernel32.OpenProcess(process_set_information, False, pid)
    if not handle:
        raise OSError(ctypes.get_last_error(), "OpenProcess failed")
    try:
        if not kernel32.SetProcessAffinityMask(handle, affinity_mask):
            raise OSError(
                ctypes.get_last_error(), "SetProcessAffinityMask failed")
    finally:
        kernel32.CloseHandle(handle)
    return affinity_mask

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


def resolve_map_symbol(symbol):
    map_paths = []
    if MAP_PATH_OVERRIDE:
        map_paths.append(MAP_PATH_OVERRIDE)
    map_paths += [
        os.path.join("build", "release", "default.map"),
        os.path.join("build", "release", "ja-release.map"),
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


def resolve_map_symbol_in(symbol, path):
    pattern = re.compile(r"\b%s\b\s+([0-9a-fA-F]{8})\b" % re.escape(symbol))
    if not os.path.exists(path):
        return None
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                match = pattern.search(line)
                if match:
                    return int(match.group(1), 16)
    except OSError:
        pass
    return None


def resolve_literal_map_symbol_in(symbol, path):
    """Resolve decorated C++ symbols whose leading '?' has no word boundary."""
    pattern = re.compile(
        r"(?:^|\s)%s\s+([0-9a-fA-F]{8})(?:\s|$)" % re.escape(symbol))
    if not os.path.exists(path):
        return None
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                match = pattern.search(line)
                if match:
                    return int(match.group(1), 16)
    except OSError:
        pass
    return None


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


def resolve_symbol_offsets_in(base_symbol, symbols, path):
    base = resolve_map_symbol_in(base_symbol, path)
    offsets = {}
    if base is None:
        return offsets
    for name in symbols:
        addr = resolve_map_symbol_in(name, path)
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
    return []


def ensure_xemu_copy():
    if not os.path.exists(XEMU_JA):
        import shutil
        shutil.copy2(XEMU_BASE, XEMU_JA)


_XEMU_SCREENSHOT_FLAG_RVA_CACHE = {}


def pe_sections(data):
    pe_offset = int.from_bytes(data[0x3c:0x40], "little")
    section_count = int.from_bytes(data[pe_offset + 6:pe_offset + 8], "little")
    optional_size = int.from_bytes(data[pe_offset + 20:pe_offset + 22], "little")
    optional_offset = pe_offset + 24
    magic = int.from_bytes(data[optional_offset:optional_offset + 2], "little")
    if magic == 0x20b:
        image_base = int.from_bytes(data[optional_offset + 24:optional_offset + 32], "little")
    else:
        image_base = int.from_bytes(data[optional_offset + 28:optional_offset + 32], "little")
    section_offset = optional_offset + optional_size
    sections = []
    for index in range(section_count):
        offset = section_offset + index * 40
        name = data[offset:offset + 8].split(b"\0", 1)[0].decode("ascii", "replace")
        virtual_size = int.from_bytes(data[offset + 8:offset + 12], "little")
        virtual_address = int.from_bytes(data[offset + 12:offset + 16], "little")
        raw_size = int.from_bytes(data[offset + 16:offset + 20], "little")
        raw_offset = int.from_bytes(data[offset + 20:offset + 24], "little")
        sections.append((name, virtual_address, raw_offset, max(virtual_size, raw_size)))
    return image_base, sections


def pe_file_offset_to_va(offset, image_base, sections):
    for _name, virtual_address, raw_offset, size in sections:
        if raw_offset and raw_offset <= offset < raw_offset + size:
            return image_base + virtual_address + (offset - raw_offset)
    return None


def pe_va_to_file_offset(va, image_base, sections):
    rva = va - image_base
    for _name, virtual_address, raw_offset, size in sections:
        if virtual_address <= rva < virtual_address + size:
            return raw_offset + (rva - virtual_address)
    return None


def pe_find_rip_xrefs(data, image_base, sections, target_va):
    text_section = None
    for section in sections:
        if section[0] == ".text":
            text_section = section
            break
    if text_section is None:
        return []

    _name, text_va, text_raw, text_size = text_section
    text = data[text_raw:text_raw + text_size]
    hits = []
    import struct
    for index in range(0, len(text) - 4):
        disp = struct.unpack_from("<i", text, index)[0]
        va = image_base + text_va + index + 4 + disp
        if va == target_va:
            hits.append(image_base + text_va + index)
    return hits


def xemu_find_screenshot_flag_pointer_rva(xemu_exe):
    cached = _XEMU_SCREENSHOT_FLAG_RVA_CACHE.get(xemu_exe)
    if cached is not None:
        return cached

    with open(xemu_exe, "rb") as handle:
        data = handle.read()
    image_base, sections = pe_sections(data)

    screenshot_offsets = []
    start = 0
    while True:
        offset = data.find(b"Screenshot\0", start)
        if offset < 0:
            break
        screenshot_offsets.append(offset)
        start = offset + 1

    f12_offsets = []
    start = 0
    while True:
        offset = data.find(b"F12\0", start)
        if offset < 0:
            break
        f12_offsets.append(offset)
        start = offset + 1

    screenshot_xrefs = []
    for offset in screenshot_offsets:
        va = pe_file_offset_to_va(offset, image_base, sections)
        if va is not None:
            screenshot_xrefs.extend(pe_find_rip_xrefs(data, image_base, sections, va))

    f12_xrefs = []
    for offset in f12_offsets:
        va = pe_file_offset_to_va(offset, image_base, sections)
        if va is not None:
            f12_xrefs.extend(pe_find_rip_xrefs(data, image_base, sections, va))

    anchors = []
    for screenshot_xref in screenshot_xrefs:
        for f12_xref in f12_xrefs:
            if abs(screenshot_xref - f12_xref) < 64:
                anchors.append(min(screenshot_xref, f12_xref) - 3)

    text_section = None
    for section in sections:
        if section[0] == ".text":
            text_section = section
            break
    if text_section is None:
        return None

    _name, _text_va, text_raw, text_size = text_section
    text_end = text_raw + text_size
    import struct
    for anchor in anchors:
        anchor_offset = pe_va_to_file_offset(anchor, image_base, sections)
        if anchor_offset is None:
            continue
        search_start = max(text_raw, anchor_offset - 128)
        search_end = min(text_end, anchor_offset + 0x1200)
        for offset in range(search_start, search_end - 12):
            if (data[offset:offset + 3] == b"\x48\x8b\x05" and
                    data[offset + 7:offset + 10] == b"\xc6\x00\x01" and
                    data[offset + 10] == 0xe9):
                pattern_va = pe_file_offset_to_va(offset, image_base, sections)
                disp = struct.unpack_from("<i", data, offset + 3)[0]
                pointer_va = pattern_va + 7 + disp
                pointer_rva = pointer_va - image_base
                _XEMU_SCREENSHOT_FLAG_RVA_CACHE[xemu_exe] = pointer_rva
                return pointer_rva
    return None


def xemu_process_module_base(process_handle):
    import ctypes

    psapi = ctypes.windll.psapi
    modules = (ctypes.c_void_p * 1024)()
    needed = ctypes.c_ulong()
    if not psapi.EnumProcessModules(
            process_handle, ctypes.byref(modules), ctypes.sizeof(modules),
            ctypes.byref(needed)):
        return None
    return int(modules[0]) if modules[0] else None


def xemu_trigger_native_screenshot(pid, xemu_exe, screenshot_dir, timeout=3.0):
    if os.name != "nt":
        return False, "xemu native screenshots require Windows", None

    import ctypes

    os.makedirs(screenshot_dir, exist_ok=True)
    before = {
        os.path.abspath(os.path.join(screenshot_dir, name))
        for name in os.listdir(screenshot_dir)
        if name.lower().endswith(".png")
    }

    pointer_rva = xemu_find_screenshot_flag_pointer_rva(xemu_exe)
    if pointer_rva is None:
        return False, "xemu native screenshot flag path not found", None

    kernel32 = ctypes.windll.kernel32
    access = 0x0400 | 0x0008 | 0x0010 | 0x0020
    process = kernel32.OpenProcess(access, False, pid)
    if not process:
        return False, "xemu native screenshot OpenProcess failed pid=%d" % pid, None

    flag_addr = None
    try:
        module_base = xemu_process_module_base(process)
        if module_base is None:
            return False, "xemu native screenshot module base not found", None

        pointer_addr = module_base + pointer_rva
        pointer_buf = (ctypes.c_ubyte * 8)()
        transferred = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(
                process, ctypes.c_void_p(pointer_addr), pointer_buf, 8,
                ctypes.byref(transferred)):
            return False, "xemu native screenshot flag pointer unreadable", None

        flag_addr = int.from_bytes(bytes(pointer_buf), "little")
        one = (ctypes.c_ubyte * 1)(1)
        if not kernel32.WriteProcessMemory(
                process, ctypes.c_void_p(flag_addr), one, 1,
                ctypes.byref(transferred)):
            return False, "xemu native screenshot flag write failed", None
    finally:
        kernel32.CloseHandle(process)

    end = time.time() + timeout
    while time.time() < end:
        current = []
        for name in os.listdir(screenshot_dir):
            if not name.lower().endswith(".png"):
                continue
            path = os.path.abspath(os.path.join(screenshot_dir, name))
            if path not in before and os.path.exists(path):
                current.append(path)
        if current:
            newest = max(current, key=os.path.getmtime)
            return True, (
                "xemu native screenshot flag_rva=0x%x flag=0x%x file=%s" %
                (pointer_rva, flag_addr or 0, newest)
            ), newest
        time.sleep(0.1)

    return False, "xemu native screenshot flag set but no PNG appeared", None


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
    for delta in (
        preferred_delta,
        0x2a3000,
        0x2a4000,
        0x287000,
        0x286000,
        0x285000,
        0x284000,
        0x283000,
        0x282000,
        0x281000,
        0x280000,
        0x264000,
        0x244000,
    ):
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
        if len(words) >= 5 and words[0] == 0x53504546 and words[4] == 0x48424653:
            log("xblog_probe matched delta=0x%x addr=0x%08x" % (delta, addr))
            return addr
        log("xblog_probe miss delta=0x%x addr=0x%08x words=%s" %
            (delta, addr, ",".join("0x%08x" % w for w in words[:5])))

        scan_start = max(0, (addr - 0x1000) & ~3)
        try:
            scan_reply = monitor_cmd(sock, "xp/2048wx 0x%08x" % scan_start, 0.8)
        except OSError:
            continue
        scan_words = parse_monitor_words(scan_reply)
        for i in range(0, max(0, len(scan_words) - 5)):
            if scan_words[i] == 0x53504546 and scan_words[i + 4] == 0x48424653:
                found_addr = scan_start + i * 4 + 4
                found_delta = boot_va - found_addr
                log("xblog_probe scanned delta=0x%x addr=0x%08x" % (found_delta, found_addr))
                return found_addr

    # XEMU's physical placement can move by more than the narrow page probes
    # above. Scan only the small title-data window for the paired telemetry
    # sentinels instead of guessing another fixed delta.
    broad_start = 0x00500000
    broad_size = 0x00200000
    broad_path = os.path.abspath(
        os.path.join(OUT_DIR, "_xblog_probe_%d.bin" % os.getpid()))
    try:
        if monitor_pmemsave(sock, broad_start, broad_size, broad_path):
            with open(broad_path, "rb") as broad_file:
                broad_data = broad_file.read()
            heartbeat = b"SFBH"
            precrt = b"FEPS"
            search_at = 0
            while True:
                heartbeat_at = broad_data.find(heartbeat, search_at)
                if heartbeat_at < 0:
                    break
                if (heartbeat_at >= 16 and
                        broad_data[heartbeat_at - 16:heartbeat_at - 12] == precrt):
                    found_addr = broad_start + heartbeat_at - 12
                    found_delta = boot_va - found_addr
                    log("xblog_probe broad delta=0x%x addr=0x%08x" %
                        (found_delta, found_addr))
                    return found_addr
                search_at = heartbeat_at + 4
    except OSError as exc:
        log("xblog_probe broad unavailable=%s" % exc)
    finally:
        try:
            os.remove(broad_path)
        except OSError:
            pass
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


def monitor_texture_allocator(sock, physical_addr):
    max_free_blocks = 4096
    free_blocks_offset = 16
    free_block_count_offset = free_blocks_offset + max_free_blocks * 8

    header_reply = monitor_cmd(
        sock, "xp/4wx 0x%08x" % physical_addr, 0.12)
    header = parse_monitor_words(header_reply, physical_addr)
    if len(header) < 4:
        return None

    base, used, capacity, high_water = header[:4]
    count_addr = physical_addr + free_block_count_offset
    count_reply = monitor_cmd(
        sock, "xp/1wx 0x%08x" % count_addr, 0.12)
    count_words = parse_monitor_words(count_reply, count_addr)
    if not count_words:
        return None
    block_count = count_words[0]

    total_free = 0
    largest_free = 0
    complete = True
    if block_count > max_free_blocks:
        complete = False
    elif block_count:
        blocks_addr = physical_addr + free_blocks_offset
        blocks_reply = monitor_cmd(
            sock, "xp/%dwx 0x%08x" % (block_count * 2, blocks_addr), 0.16)
        blocks = parse_monitor_words(blocks_reply, blocks_addr)
        if len(blocks) < block_count * 2:
            complete = False
        else:
            sizes = blocks[1:block_count * 2:2]
            total_free = sum(sizes)
            largest_free = max(sizes)

    return {
        "base": base,
        "used": used,
        "capacity": capacity,
        "high_water": high_water,
        "block_count": block_count,
        "total_free": total_free,
        "largest_free": largest_free,
        "complete": complete,
    }


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

    candidate_deltas = []
    if phys_delta is not None:
        candidate_deltas.append(phys_delta)
    candidate_deltas.extend([None, 0x284000, 0x2a4000, 0x2a3000, 0x280000, 0x2a5000, 0x2a6000])

    seen_deltas = set()
    data = pitch = width = height = 0
    used_delta = None
    probe_details = []
    for candidate_delta in candidate_deltas:
        key = -1 if candidate_delta is None else candidate_delta
        if key in seen_deltas:
            continue
        seen_deltas.add(key)

        probe_data = monitor_read_u32(sock, addrs["_g_SPXBFramebufferData"], candidate_delta) or 0
        probe_pitch = monitor_read_u32(sock, addrs["_g_SPXBFramebufferPitch"], candidate_delta) or 0
        probe_width = monitor_read_u32(sock, addrs["_g_SPXBFramebufferWidth"], candidate_delta) or 0
        probe_height = monitor_read_u32(sock, addrs["_g_SPXBFramebufferHeight"], candidate_delta) or 0
        probe_details.append("%s:data=0x%08x pitch=%u size=%ux%u" % (
            "virt" if candidate_delta is None else "0x%x" % candidate_delta,
            probe_data, probe_pitch, probe_width, probe_height))

        if (probe_data != 0 and probe_pitch != 0 and probe_width != 0 and probe_height != 0 and
            probe_width <= 1920 and probe_height <= 1080 and probe_pitch >= probe_width * 4):
            data = probe_data
            pitch = probe_pitch
            width = probe_width
            height = probe_height
            used_delta = candidate_delta
            break

    if data == 0 or pitch == 0 or width == 0 or height == 0:
        return False, "framebuffer telemetry empty probes=%s" % "; ".join(probe_details[:8])
    if width > 1920 or height > 1080 or pitch < width * 4:
        return False, "framebuffer telemetry invalid data=0x%08x pitch=%u size=%ux%u probes=%s" % (
            data, pitch, width, height, "; ".join(probe_details[:8]))

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
        return True, "guest framebuffer %ux%u pitch=%u data=0x%08x delta=%s" % (
            width, height, pitch, data, "virt" if used_delta is None else "0x%x" % used_delta)
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
            log("final_dump_phys_reply=present bytes=%u" % len(reply))


def dump_virtual_memory_binary(sock, prefix, specs, log):
    if sock is None:
        return
    for spec in specs:
        parts = spec.split(":", 2)
        if len(parts) < 2:
            log("final_dump_bin_mem_invalid=%s" % spec)
            continue
        try:
            addr = int(parts[0], 0)
            length = int(parts[1], 0)
        except ValueError:
            log("final_dump_bin_mem_invalid=%s" % spec)
            continue
        name = parts[2] if len(parts) > 2 and parts[2] else "mem_%08x" % addr
        safe_name = re.sub(r"[^A-Za-z0-9_.-]", "_", name)
        dump_path = os.path.abspath("%s_final_%s.bin" % (prefix, safe_name))
        monitor_path = dump_path.replace("\\", "/")
        try:
            reply = monitor_cmd(sock, 'memsave 0x%08x 0x%x "%s"' % (addr, length, monitor_path), 4.0)
        except OSError as exc:
            log("final_dump_bin_mem_unavailable=%s err=%s" % (spec, exc))
            continue
        log("final_dump_bin_mem=%s" % dump_path)
        if reply.strip():
            log("final_dump_bin_mem_reply=present bytes=%u" % len(reply))


def append_xblog_auto_dumps(specs, phys_delta, log):
    if phys_delta is None:
        log("xblog_auto_dump skipped reason=no-phys-delta")
        return specs

    out = list(specs)
    for symbol, length, name in (
        ("_g_SPXBLogMirror", 0x8000, "sp_log_mirror_auto"),
        ("_g_SPXBLogLastLine", 0x200, "sp_log_lastline_auto"),
        ("_g_SPXBSVProbeMagic", 0x40, "stefx_sv_probe_auto"),
    ):
        va, map_path = resolve_map_symbol(symbol)
        if va is None:
            log("xblog_auto_dump skipped symbol=%s reason=unresolved" % symbol)
            continue
        phys = va - phys_delta
        if phys <= 0:
            log("xblog_auto_dump skipped symbol=%s va=0x%08x delta=0x%x" % (symbol, va, phys_delta))
            continue
        out.append("0x%08x:0x%x:%s" % (phys, length, name))
        log("xblog_auto_dump symbol=%s va=0x%08x phys=0x%08x len=0x%x map=%s" %
            (symbol, va, phys, length, map_path))
        bss_phys = phys + 0x1000
        bss_name = "%s_bss" % name
        out.append("0x%08x:0x%x:%s" % (bss_phys, length, bss_name))
        log("xblog_auto_dump_bss symbol=%s va=0x%08x phys=0x%08x len=0x%x" %
            (symbol, va, bss_phys, length))
    return out


def append_xblog_auto_virtual_dumps(specs, log):
    out = list(specs)
    for symbol, length, name in (
        ("_g_SPXBLogMirror", 0x8000, "sp_log_mirror_auto_vmem"),
        ("_g_SPXBLogLastLine", 0x200, "sp_log_lastline_auto_vmem"),
        ("_g_SPXBSVProbeMagic", 0x40, "stefx_sv_probe_auto_vmem"),
    ):
        va, map_path = resolve_map_symbol(symbol)
        if va is None:
            log("xblog_auto_vmem skipped symbol=%s reason=unresolved" % symbol)
            continue
        out.append("0x%08x:0x%x:%s" % (va, length, name))
        log("xblog_auto_vmem symbol=%s va=0x%08x len=0x%x map=%s" %
            (symbol, va, length, map_path))
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", required=True)
    parser.add_argument("--name", default="ja_xemu")
    parser.add_argument("--port", type=int, default=4460)
    parser.add_argument("--duration", type=int, default=60)
    parser.add_argument("--interval", type=int, default=5)
    parser.add_argument("--first-shot-delay", type=float, default=0.0,
                        help="Seconds to wait before the first framebuffer capture.")
    parser.add_argument("--hdd", default=r"C:\Games\Emulators\Xemu\HDD\xbox_hdd.qcow2")
    parser.add_argument("--xemu-exe", default=XEMU_JA,
                        help="Xemu executable to launch. Defaults to the JA-isolated copy.")
    parser.add_argument("--config-path", default="",
                        help="Optional xemu.toml path to update and pass to Xemu with -config_path.")
    parser.add_argument("--explicit-xbox-machine", action="store_true",
                        help="Launch with explicit OG Xbox machine/BIOS/HDD/DVD args instead of relying on xemu.toml.")
    parser.add_argument("--visible", action="store_true")
    parser.add_argument("--headless", action="store_true",
                        help="Run without a display window. Implies --display none and skips screenshots.")
    parser.add_argument("--no-screenshots", action="store_true",
                        help="Skip interval screenshots without changing the display backend.")
    parser.add_argument("--xemu-native-screenshots", action="store_true",
                        help="Capture through XEMU's built-in F12 screenshot action.")
    parser.add_argument("--xemu-screenshot-dir", default="",
                        help="Directory watched for XEMU native screenshot PNGs.")
    parser.add_argument("--display", choices=["xemu", "sdl", "none", "egl-headless", "nographic"],
                        default="xemu",
                        help="Xemu/QEMU display backend. Use none or egl-headless for unattended runs.")
    parser.add_argument("--no-monitor", action="store_true")
    parser.add_argument("--monitor-keys", default="")
    parser.add_argument("--smoke-keymap", action="store_true")
    parser.add_argument("--xemu-arg", action="append", default=[])
    parser.add_argument("--keep-net", action="store_true")
    parser.add_argument("--dump-mem", action="append", default=[],
                        help="Dump guest memory before closing monitor: addr:length[:name]")
    parser.add_argument("--dump-bin-mem", action="append", default=[],
                        help="Dump guest virtual memory bytes with monitor memsave: addr:length[:name]")
    parser.add_argument("--dump-phys", action="append", default=[],
                        help="Dump guest physical memory before closing monitor: addr:length[:name]")
    parser.add_argument("--xblog-auto-dumps", action="store_true",
                        help="Add XBLog mirror/last-line physical and virtual dumps from resolved symbols.")
    parser.add_argument("--watch-cr2", default="",
                        help="Poll registers and dump memory when CR2 matches this value")
    parser.add_argument("--poll-xblog", action="store_true",
                        help="Poll SP/MP XBLog counters through the monitor during the run.")
    parser.add_argument("--poll-xblog-perf-only", action="store_true",
                        help="Poll only the compact heartbeat/performance range.")
    parser.add_argument("--poll-xblog-start-delay", type=float, default=0.0,
                        help="Delay the first XBLog poll to avoid disturbing timed gameplay.")
    parser.add_argument("--poll-xblog-kind", choices=["auto", "sp", "mp"], default="auto",
                        help="Prefer SP or MP XBLog symbols when polling. Auto preserves the historical probe order.")
    parser.add_argument("--map-file", default="",
                        help="Prefer symbols from this linker map for telemetry and memory dumps.")
    parser.add_argument("--poll-xblog-addr", default="",
                        help="Address of g_*XBBootPhase. Empty means resolve _g_SPXBBootPhase from the current map.")
    parser.add_argument("--poll-xblog-phys-addr", default="",
                        help="Exact physical address of g_*XBBootPhase; bypasses automatic physical probing.")
    parser.add_argument("--poll-xblog-phys-delta", default="0x284000",
                        help="Auto-resolved XBLog VA minus physical monitor address. Use 0 to poll virtual x/ memory.")
    args = parser.parse_args()

    global MAP_PATH_OVERRIDE
    if args.map_file:
        MAP_PATH_OVERRIDE = os.path.abspath(args.map_file)
        if not os.path.isfile(MAP_PATH_OVERRIDE):
            parser.error("Map file not found: %s" % MAP_PATH_OVERRIDE)

    os.makedirs(OUT_DIR, exist_ok=True)
    args.iso = os.path.abspath(args.iso)
    args.hdd = os.path.abspath(args.hdd)
    if not os.path.isfile(args.iso):
        parser.error("ISO not found: %s" % args.iso)
    if not os.path.isfile(args.hdd):
        parser.error("HDD image not found: %s" % args.hdd)
    xemu_exe = os.path.abspath(args.xemu_exe)
    if os.path.normcase(xemu_exe) == os.path.normcase(os.path.abspath(XEMU_JA)):
        ensure_xemu_copy()
    config_path = os.path.abspath(args.config_path) if args.config_path else XEMU_TOML

    stamp = time.strftime("%Y%m%d_%H%M%S")
    prefix = os.path.join(OUT_DIR, "%s_%s" % (args.name, stamp))
    raw_log = prefix + ".xemu.txt"
    report = prefix + ".report.txt"
    if args.xemu_screenshot_dir:
        xemu_screenshot_dir = os.path.abspath(args.xemu_screenshot_dir)
    elif config_path:
        xemu_screenshot_dir = os.path.join(os.path.dirname(config_path), "screenshots")
    else:
        xemu_screenshot_dir = os.path.abspath(prefix + "_xemu_screenshots")
    toml_backup = None

    if os.path.exists(config_path):
        with open(config_path, "r", encoding="utf-8", errors="replace") as f:
            toml_backup = f.read()
        toml = re.sub(r"hdd_path = '.*'", lambda _m: "hdd_path = '%s'" % args.hdd, toml_backup)
        toml = re.sub(r"dvd_path = '.*'", lambda _m: "dvd_path = '%s'" % args.iso, toml)
        if not args.keep_net:
            toml = re.sub(r"(?m)^enable = true$", "enable = false", toml, count=1)
        if args.smoke_keymap:
            toml = re.sub(r"(?ms)\n?\[input\.keyboard_controller_scancode_map\]\n.*?(?=\n\[|\Z)", "", toml)
            toml = toml.rstrip() + "\n\n" + SMOKE_KEYBOARD_MAP + "\n"
        with open(config_path, "w", encoding="utf-8", errors="replace") as f:
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
    if args.config_path:
        argv += ["-config_path", config_path]
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
        print(msg, flush=True)
        lines.append(msg)

    xblog_auto_phys_pending = False
    if args.xblog_auto_dumps:
        phys_delta_for_auto = None
        try:
            phys_delta_for_auto = int(args.poll_xblog_phys_delta, 0)
        except ValueError:
            phys_delta_for_auto = None
        if args.poll_xblog:
            xblog_auto_phys_pending = True
            log("xblog_auto_dump deferred reason=awaiting-probed-phys-delta")
        else:
            args.dump_phys = append_xblog_auto_dumps(args.dump_phys, phys_delta_for_auto, log)
        args.dump_bin_mem = append_xblog_auto_virtual_dumps(args.dump_bin_mem, log)

    log("launch: %s" % " ".join(argv))
    logf = open(raw_log, "w", encoding="utf-8", errors="replace")
    xemu_cwd = os.path.dirname(xemu_exe) or XEMU_DIR
    popen_options = {}
    if os.name == "nt":
        popen_options["creationflags"] = getattr(
            subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0x00004000)
        startupinfo = subprocess.STARTUPINFO()
        startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startupinfo.wShowWindow = 7  # SW_SHOWMINNOACTIVE
        popen_options["startupinfo"] = startupinfo
    proc = subprocess.Popen(
        argv,
        cwd=xemu_cwd,
        stdout=logf,
        stderr=subprocess.STDOUT,
        **popen_options)
    log("pid=%d" % proc.pid)
    try:
        affinity_mask = cap_windows_process_affinity(proc.pid)
        if affinity_mask is not None:
            log("process_affinity_mask=0x%X" % affinity_mask)
    except OSError as exc:
        log("process_affinity_warning=%s" % exc)
    monitor_key_events = parse_key_schedule(args.monitor_keys)
    shot_paths = []
    active_fps_samples = []
    gameplay_fps_samples = []
    personality_active_fps_samples = {}
    personality_gameplay_fps_samples = {}
    last_fps_heartbeat_count = None
    watch_cr2 = int(args.watch_cr2, 0) if args.watch_cr2 else None
    watch_done = False
    next_watch = 0.0
    xblog_addr = None
    xblog_mode = ""
    xblog_mp_pos_addr = None
    xblog_mp_write_addr = None
    xblog_va_for_probe = None
    xblog_map = None
    if args.poll_xblog:
        if args.poll_xblog_phys_addr:
            xblog_addr = int(args.poll_xblog_phys_addr, 0)
            xblog_va_for_probe, xblog_map = resolve_map_symbol("_g_SPXBBootPhase")
            xblog_cmd = "xp"
            xblog_mode = "sp"
            log("xblog_symbol=_g_SPXBBootPhase va=0x%08x poll=xp addr=0x%08x map=%s forced=1" %
                (xblog_va_for_probe or 0, xblog_addr, xblog_map))
        elif args.poll_xblog_addr:
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
    next_xblog_poll = max(0.0, args.poll_xblog_start_delay)
    next_xblog_probe = 0.0
    last_xblog_write_count = None
    texture_allocator_symbol = "?gTextures@@3VStaticTextureAllocator@@A"
    texture_allocator_map = (
        xblog_map if xblog_map else os.path.join(
            "build", "release", "default.map"))
    texture_allocator_va = resolve_literal_map_symbol_in(
        texture_allocator_symbol, texture_allocator_map)
    next_texture_allocator_poll = 0.0
    if texture_allocator_va is not None:
        log("texture_allocator_symbol=%s va=0x%08x map=%s" %
            (texture_allocator_symbol, texture_allocator_va,
             texture_allocator_map))
    else:
        log("texture_allocator_symbol=%s unresolved map=%s" %
            (texture_allocator_symbol, texture_allocator_map))
    xblog_offsets = resolve_symbol_offsets("_g_SPXBBootPhase", [
        "_g_SPXBUIStateMagic",
        "_g_SPXBUIStarted",
        "_g_SPXBUIKeyCatcher",
        "_g_SPXBUIPauseActive",
        "_g_SPXBUIQmenuActive",
        "_g_SPXBUIRefreshCount",
        "_g_SPXBUIPauseOpenCount",
        "_g_SPXBUIPauseDrawCount",
        "_g_SPXBUIFontMagic",
        "_g_SPXBMiniSoakMagic",
        "_g_SPXBMiniSoakStage",
        "_g_SPXBMiniSoakTransitions",
        "_g_SPXBMiniSoakActiveMsec",
        "_g_SPXBMiniSoakFlags",
        "_g_SPXBMainLoopCount",
        "_g_SPXBComFrameCount",
        "_g_SPXBSvFrameCount",
        "_g_SPXBClFrameCount",
        "_g_SPXBClsState",
        "_g_SPXBClServerTime",
        "_g_SPXBClsFrameCount",
        "_g_SPXBPhaseLast",
        "_g_SPXBComSubphase",
        "_g_SPXBComSpinCount",
        "_g_SPXBComMsec",
        "_g_SPXBComFrameTime",
        "_g_SPXBComLastTime",
        "_g_SPXBCbufExecCount",
        "_g_SPXBCmdExecCount",
        "_g_SPXBCmdPhase",
        "_g_SPXBCmdHash",
        "_g_SPXBCmdArgc",
        "_g_SPXBMapPhase",
        "_g_SPXBMapHash",
        "_g_SPXBGamePhase",
        "_g_SPXBGameEntityCount",
        "_g_SPXBPerfFrameMsec",
        "_g_SPXBPerfServerMsec",
        "_g_SPXBPerfClientMsec",
        "_g_SPXBPerfGameMsec",
        "_g_SPXBPerfFrontendMsec",
        "_g_SPXBPerfBackendMsec",
        "_g_SPXBPerfAudioMsec",
        "_g_SPXBPerfServerTicks",
        "_g_SPXBPerfServerLastGameMsec",
        "_g_SPXBPerfServerMaxGameMsec",
        "_g_SPXBPerfGamePreMsec",
        "_g_SPXBPerfGameEntitiesMsec",
        "_g_SPXBPerfGamePostMsec",
        "_g_SPXBPerfGameEntitiesVisited",
        "_g_SPXBPerfGameMissiles",
        "_g_SPXBPerfGameItems",
        "_g_SPXBPerfGameMovers",
        "_g_SPXBPerfGameClients",
        "_g_SPXBPerfGameThinkDue",
        "_g_SPXBPerfGameScripted",
        "_g_SPXBPerfGameOther",
        "_g_SPXBPerfScreenDrawMsec",
        "_g_SPXBPerfEndFrameMsec",
        "_g_SPXBPerfRenderTotalMsec",
        "_g_SPXBPerfRenderSetupMsec",
        "_g_SPXBPerfRenderMarkLeavesMsec",
        "_g_SPXBPerfRenderWorldMsec",
        "_g_SPXBPerfRenderPolysMsec",
        "_g_SPXBPerfRenderProjectionMsec",
        "_g_SPXBPerfRenderEntitiesMsec",
        "_g_SPXBPerfRenderSortMsec",
        "_g_SPXBPerfRenderDebugMsec",
        "_g_SPXBPerfRenderViews",
        "_g_SPXBPerfRenderPortals",
        "_g_SPXBPerfRenderDrawSurfs",
        "_g_SPXBPerfRenderRefEntities",
        "_g_SPXBPerfRenderLeafs",
        "_g_SPXBCameraActive",
        "_g_SPXBBorgPluggedCount",
        "_g_SPXBBorgPluggedEnt",
        "_g_SPXBBorgPluggedSpawnflags",
        "_g_SPXBBorgPluggedAnim",
        "_g_SPXBBorgPluggedLegsModel",
        "_g_SPXBBorgPluggedTorsoModel",
        "_g_SPXBBorgPluggedHeadModel",
        "_g_SPXBBorgPluggedLegsSkin",
        "_g_SPXBBorgPluggedTorsoSkin",
        "_g_SPXBBorgPluggedHeadSkin",
        "_g_SPXBBorgPluggedLegsNameHash",
        "_g_SPXBBorgPluggedTorsoNameHash",
        "_g_SPXBBorgPluggedHeadNameHash",
        "_g_SPXBBorgActiveCount",
        "_g_SPXBBorgActiveEnt",
        "_g_SPXBBorgActiveSpawnflags",
        "_g_SPXBBorgActiveAnim",
        "_g_SPXBBorgActiveLegsModel",
        "_g_SPXBBorgActiveTorsoModel",
        "_g_SPXBBorgActiveHeadModel",
        "_g_SPXBBorgActiveLegsSkin",
        "_g_SPXBBorgActiveTorsoSkin",
        "_g_SPXBBorgActiveHeadSkin",
        "_g_SPXBBorgActiveLegsNameHash",
        "_g_SPXBBorgActiveTorsoNameHash",
        "_g_SPXBBorgActiveHeadNameHash",
        "_g_SPXBRenderBackendMsec",
        "_g_SPXBFakeGLPrimitiveCalls",
        "_g_SPXBFakeGLPrimitiveVerts",
        "_g_SPXBFakeGLStateFlushes",
        "_g_SPXBNativeDrawMode",
        "_g_SPXBNativeDrawCount",
        "_g_SPXBNativeDrawSourceVertices",
        "_g_SPXBNativeDrawMaxIndex",
        "_g_SPXBNativeDrawStride",
        "_g_SPXBNativeDrawIndicesPtr",
        "_g_SPXBNativeDrawVerticesPtr",
        "_g_SPXBNativeDrawPath",
        "_g_SPXBNativeDrawShader",
        "_g_SPXBNativeDrawVertexOffset",
        "_g_SPXBNativeDrawIndexOffset",
        "_g_SPXBNativeDrawVertexBytes",
        "_g_SPXBNativeDrawIndexBytes",
        "_g_SPXBNativeDrawLockFlags",
        "_g_SPXBNativeDrawMinIndex",
        "_g_SPXBRenderSplitShader",
        "_g_SPXBRenderSplitFog",
        "_g_SPXBRenderSplitDlight",
        "_g_SPXBRenderSplitEntity",
        "_g_SPXBRenderSplitFinal",
        "_g_SPXBRenderSplitFlush",
        "_g_SPXBSplitSlotActive",
        "_g_SPXBSplitSlot0DrawDelta",
        "_g_SPXBSplitSlot1DrawDelta",
        "_g_SPXBSplitSlot0WorldDelta",
        "_g_SPXBSplitSlot1WorldDelta",
        "_g_SPXBSplitSlot0Cluster",
        "_g_SPXBSplitSlot1Cluster",
        "_g_SPXBSplitSlot1WorldRetryDelta",
        "_g_SPXBSplitSlot1WorldFallback",
        "_g_SPXBSplitSlot0MarkedLeaves",
        "_g_SPXBSplitSlot1MarkedLeaves",
        "_g_SPXBSplitSlot0PvsRejected",
        "_g_SPXBSplitSlot1PvsRejected",
        "_g_SPXBSplitSlot0AreaRejected",
        "_g_SPXBSplitSlot1AreaRejected",
        "_g_SPXBSplitSlot0RootVis",
        "_g_SPXBSplitSlot1RootVis",
        "_g_SPXBSplitSlot0WorldAttempts",
        "_g_SPXBSplitSlot1WorldAttempts",
        "_g_SPXBSplitSlot0WorldCulled",
        "_g_SPXBSplitSlot1WorldCulled",
        "_g_SPXBSplitSlot0WorldAlready",
        "_g_SPXBSplitSlot1WorldAlready",
        "_g_SPXBSplitSlot0WorldAdded",
        "_g_SPXBSplitSlot1WorldAdded",
        "_g_SPXBSplitP2Ent",
        "_g_SPXBSplitP2TraceFrac1000",
        "_g_SPXBSplitP2ViewX",
        "_g_SPXBSplitP2ViewY",
        "_g_SPXBSplitP2ViewZ",
        "_g_SPXBSplitP2PsX",
        "_g_SPXBSplitP2PsY",
        "_g_SPXBSplitP2PsZ",
        "_g_SPXBSplitP2CurX",
        "_g_SPXBSplitP2CurY",
        "_g_SPXBSplitP2CurZ",
        "_g_SPXBSplitP2AnglesPitch",
        "_g_SPXBSplitP2AnglesYaw",
        "_g_SPXBSplitP2RefdefValid",
        "_g_SPXBSplitP2SceneConsidered",
        "_g_SPXBSplitP2SceneAdded",
        "_g_SPXBSplitP2SceneSelfAdded",
        "_g_SPXBSplitP2ModelEnter",
        "_g_SPXBSplitP2ModelReturn",
        "_g_SPXBSplitP2ModelInfoValid",
        "_g_SPXBSplitP2ModelSubmitted",
        "_g_SPXBSplitP2ModelLegs",
        "_g_SPXBSplitP2ModelTorso",
        "_g_SPXBSplitP2ModelHead",
        "_g_SPXBSplitP2ModelRenderfx",
        "_g_SPXBSplitP2RendererRefs",
        "_g_SPXBSplitP2RendererLastModel",
        "_g_SPXBSplitP2RendererLastRenderfx",
        "_g_SPXBSplitP2RendererLastZ",
        "_g_SPXBViewWeaponP1Adds",
        "_g_SPXBViewWeaponP2Adds",
        "_g_SPXBViewWeaponP1Skips",
        "_g_SPXBViewWeaponP2Skips",
        "_g_SPXBViewWeaponP1Model",
        "_g_SPXBViewWeaponP2Model",
        "_g_SPXBViewWeaponP1Renderfx",
        "_g_SPXBViewWeaponP2Renderfx",
        "_g_SPXBViewWeaponP1RendererAdds",
        "_g_SPXBViewWeaponP2RendererAdds",
        "_g_SPXBViewWeaponP1RendererFiltered",
        "_g_SPXBViewWeaponP2RendererFiltered",
        "_g_SPXBViewWeaponP1LastSkip",
        "_g_SPXBViewWeaponP2LastSkip",
        "_g_SPXBWeaponRegWeapon",
        "_g_SPXBWeaponRegPathHash",
        "_g_SPXBWeaponRegViewModel",
        "_g_SPXBWeaponRegWorldModel",
        "_g_SPXBWeaponRegHandsModel",
        "_g_SPXBWeaponRegFailCode",
        "_g_SPXBWeaponModelTraceStage",
        "_g_SPXBWeaponModelTracePathHash",
        "_g_SPXBWeaponModelTraceDiskLen",
        "_g_SPXBWeaponModelTraceDiskSuccess",
        "_g_SPXBWeaponModelTraceIdent",
        "_g_SPXBWeaponModelTraceVersion",
        "_g_SPXBWeaponModelTraceSize",
        "_g_SPXBWeaponModelTraceLoaded",
        "_g_SPXBWeaponModelTraceHandle",
        "_g_SPXBWeaponModelTraceFailCode",
        "_g_SPXBWeaponLoadStage",
        "_g_SPXBWeaponLoadReadLen",
        "_g_SPXBWeaponLoadTypeWeapon",
        "_g_SPXBWeaponLoadModelWeapon",
        "_g_SPXBWeaponLoadModelHash",
        "_g_SPXBWeaponLoadSlot4Hash",
        "_g_SPXBWeaponLoadSlot4Ammo",
        "_g_SPXBWeaponLoadSlot4First4",
        "_g_SPXBWeaponRegFirst4",
        "_g_SPXBWeaponRegClassHash",
        "_g_SPXBSplitCameraMode",
        "_g_SPXBSplitP1TraceFrac1000",
        "_g_SPXBSplitP1LocalX1000",
        "_g_SPXBSplitP1LocalY1000",
        "_g_SPXBSplitP1LocalZ1000",
        "_g_SPXBSplitP2LocalX1000",
        "_g_SPXBSplitP2LocalY1000",
        "_g_SPXBSplitP2LocalZ1000",
        "_g_SPXBSplitLocalDiffX1000",
        "_g_SPXBSplitLocalDiffY1000",
        "_g_SPXBSplitLocalDiffZ1000",
        "_g_SPXBDirectMapStatus",
        "_g_SPXBDirectMapHash",
        "_g_SPXBDirectMapQueuedCount",
        "_g_SPXBSkyTraceMagic",
        "_g_SPXBSkyOuterPresentMask",
        "_g_SPXBSkyOuterFallbackMask",
        "_g_SPXBSkyOuterTexMask",
        "_g_SPXBSkyOuterDrawMask",
        "_g_SPXBSkyLastPasses",
        "_g_SPXBSkyLastSort",
        "_g_SPXBSkyResolveMagic",
        "_g_SPXBSkyResolveCount",
        "_g_SPXBSkyResolveShaderNum",
        "_g_SPXBSkyResolveMapHash",
        "_g_SPXBSkyResolveResolvedHash",
        "_g_SPXBSkyResolveSurfaceFlags",
        "_g_SPXBSkyResolveDefault",
        "_g_SPXBSkyResolveExplicit",
        "_g_SPXBSkyResolveHasSky",
        "_g_SPXBSkyResolvePasses",
        "_g_SPXBSkyResolveSortX1000",
        "_g_SPXBSkyResolveLightmap0",
        "_g_SPXBShaderScanMagic",
        "_g_SPXBShaderScanScriptsFound",
        "_g_SPXBShaderScanShadersFound",
        "_g_SPXBShaderScanLoaded",
        "_g_SPXBShaderScanBytes",
        "_g_SPXBShaderScanEntries",
        "_g_SPXBShaderScanSkyLightSeen",
        "_g_SPXBShaderScanJunkSkySeen",
        "_g_SPXBShaderScanManifestActive",
        "_g_SPXBShaderScanManifestReadLen",
        "_g_SPXBShaderScanManifestCount",
        "_g_SPXBShaderScanRawBytes",
        "_g_SPXBShaderScanVoyagerListed",
        "_g_SPXBShaderScanVoyagerReadLen",
        "_g_SPXBShaderScanVoyagerSkyToken",
        "_g_SPXBShaderScanCommonReadLen",
        "_g_SPXBShaderLookupMagic",
        "_g_SPXBShaderLookupCount",
        "_g_SPXBShaderLookupHash",
        "_g_SPXBShaderLookupIndexedFound",
        "_g_SPXBShaderLookupLinearFound",
        "_g_SPXBShaderLookupEntries",
        "_g_SPXBHelmetP1Submitted",
        "_g_SPXBHelmetP2Submitted",
        "_g_SPXBHelmetP1Attached",
        "_g_SPXBHelmetP2Attached",
        "_g_SPXBHelmetP1Model",
        "_g_SPXBHelmetP2Model",
        "_g_SPXBHelmetP1Renderfx",
        "_g_SPXBHelmetP2Renderfx",
        "_g_SPXBHelmetRendererRefs",
        "_g_SPXBHelmetRendererSurfaces",
        "_g_SPXBHelmetRendererFiltered",
        "_g_SPXBHelmetRendererLastModel",
        "_g_SPXBHelmetRendererLastRenderfx",
        "_g_SPXBHelmetRendererLastEnt",
        "_g_SPXBHelmetRendererLastFilter",
        "_g_SPXBHelmetRendererLastSurfaceModel",
        "_g_SPXBHelmetGameP1Ensure",
        "_g_SPXBHelmetGameP2Ensure",
        "_g_SPXBHelmetGameP1Slot",
        "_g_SPXBHelmetGameP2Slot",
        "_g_SPXBHelmetCgameP1Slot0",
        "_g_SPXBHelmetCgameP1Slot1",
        "_g_SPXBHelmetCgameP2Slot0",
        "_g_SPXBHelmetCgameP2Slot1",
        "_g_SPXBHelmetBoltOnLoadLen",
        "_g_SPXBHelmetBoltOnCount",
        "_g_SPXBHelmetBoltOnHelmetIndex",
        "_g_SPXBHelmetAddAttempts",
        "_g_SPXBHelmetAddKnownIndex",
        "_g_SPXBHelmetAddFailCode",
        "_g_SPXBFallbackTraceMagic",
        "_g_SPXBFallbackStageCount",
        "_g_SPXBFallbackLastShaderHash",
        "_g_SPXBFallbackLastImageHash",
        "_g_SPXBFallbackLastStage",
        "_g_SPXBFallbackLastPasses",
        "_g_SPXBFallbackLastFlags",
        "_g_SPXBFallbackLastTexnum",
        "_g_SPXBFallbackLastLightmap",
        "_g_SPXBFallbackLastStateBits",
        "_g_SPXBFallbackLastIndexes",
        "_g_SPXBFallbackLastX1000",
        "_g_SPXBFallbackLastY1000",
        "_g_SPXBFallbackLastZ1000",
        "_g_SPXBSVProbeMagic",
        "_g_SPXBSVProbePhase",
        "_g_SPXBSVProbeSubphase",
        "_g_SPXBSVProbeA",
        "_g_SPXBSVProbeB",
        "_g_SPXBSVProbeC",
        "_g_SPXBSVProbeD",
        "_g_SPXBLoadingTitleMagic",
        "_g_SPXBLoadingTitleStatus",
        "_g_SPXBLoadingTitleShader",
        "_g_SPXBLoadingTitleDraws",
        "_g_SPXBLoadingTitleLastChar",
        "_g_SPXBLoadingTitleMapHash",
        "_g_SPXBLoadingTitleTextHash",
    ])
    xblog_symbol_names = list(xblog_offsets.keys())
    xblog_font_symbols = [
        "_g_SPXBUIFontLoadAttempts",
        "_g_SPXBUIFontFileLen",
        "_g_SPXBUIFontLoaded",
        "_g_SPXBUIFontTinyShader",
        "_g_SPXBUIFontMediumShader",
        "_g_SPXBUIFontBigShader",
        "_g_SPXBUIFontDrawCalls",
        "_g_SPXBUIFontDrawRejectMask",
        "_g_SPXBUIFontLastChar",
        "_g_SPXBUIFontLastShader",
        "_g_SPXBUIFontLastSX",
        "_g_SPXBUIFontLastSY",
        "_g_SPXBUIFontLastSW",
    ]
    xblog_font_base_va, xblog_font_map = resolve_map_symbol(xblog_font_symbols[0])
    xblog_font_offsets = resolve_symbol_offsets(xblog_font_symbols[0], xblog_font_symbols)
    xblog_title_symbols = [
        "_g_SPXBLoadingTitleMagic",
        "_g_SPXBLoadingTitleStatus",
        "_g_SPXBLoadingTitleShader",
        "_g_SPXBLoadingTitleDraws",
        "_g_SPXBLoadingTitleLastChar",
        "_g_SPXBLoadingTitleMapHash",
        "_g_SPXBLoadingTitleTextHash",
    ]
    xblog_title_base_va, _xblog_title_map = resolve_map_symbol(
        xblog_title_symbols[0])
    xblog_title_offsets = resolve_symbol_offsets(
        xblog_title_symbols[0], xblog_title_symbols)
    xblog_current_personality = "efmp" if (
        xblog_map and os.path.basename(xblog_map).lower() == "efmp.map") else "default"
    next_personality_probe = 0.0
    if xblog_font_base_va is not None:
        log("xblog_font_symbol=%s va=0x%08x map=%s" %
            (xblog_font_symbols[0], xblog_font_base_va, xblog_font_map))
    mini_soak_probes = []
    for personality, map_path in (
            ("default", os.path.join("build", "release", "default.map")),
            ("efmp", os.path.join("build", "release", "efmp.map"))):
        mini_va = resolve_map_symbol_in("_g_SPXBMiniSoakMagic", map_path)
        if mini_va is not None:
            mini_soak_probes.append((personality, mini_va, map_path))
            log("xblog_soak_probe personality=%s va=0x%08x map=%s" %
                (personality, mini_va, map_path))
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
                    if xblog_auto_phys_pending:
                        probed_delta = xblog_va_for_probe - xblog_addr
                        args.dump_phys = append_xblog_auto_dumps(args.dump_phys, probed_delta, log)
                        xblog_auto_phys_pending = False
                else:
                    # Normal frontend boot can map the title after the monitor
                    # connection is ready. Retry from the main loop instead of
                    # polling the preferred address forever.
                    xblog_addr = None
                    next_xblog_probe = max(1.0, float(args.interval))
        else:
            log("monitor=disabled")
        start = time.time()
        next_shot = max(0.0, float(args.first_shot_delay))
        shot = 0
        while time.time() - start < args.duration:
            elapsed = time.time() - start
            rc = proc.poll()
            if rc is not None:
                log("process_exit t=%.1f rc=%s" % (elapsed, rc))
                break

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

            if (sock is not None and args.poll_xblog and
                    xblog_va_for_probe is not None and xblog_addr is None and
                    elapsed >= next_xblog_probe):
                next_xblog_probe = elapsed + max(1.0, float(args.interval))
                probed_addr = probe_xblog_physical_addr(
                    sock, xblog_va_for_probe,
                    int(args.poll_xblog_phys_delta, 0), log)
                if probed_addr is not None:
                    xblog_addr = probed_addr
                    next_xblog_poll = max(elapsed, args.poll_xblog_start_delay)
                    if xblog_auto_phys_pending:
                        probed_delta = xblog_va_for_probe - xblog_addr
                        args.dump_phys = append_xblog_auto_dumps(
                            args.dump_phys, probed_delta, log)
                        xblog_auto_phys_pending = False

            if sock is not None and xblog_addr is not None and elapsed >= next_xblog_poll:
                next_xblog_poll = elapsed + 1.0
                try:
                    runtime_delta = int(args.poll_xblog_phys_delta, 0)
                    if xblog_va_for_probe is not None and xblog_addr is not None:
                        runtime_delta = xblog_va_for_probe - xblog_addr
                    header_reply = monitor_cmd(
                        sock, "xp/9wx 0x%08x" % xblog_addr, 0.3)
                    header_words = parse_monitor_words(header_reply, xblog_addr)
                    if ((len(header_words) < 4 or
                         header_words[3] != 0x48424653) and
                            elapsed >= next_personality_probe):
                        next_personality_probe = elapsed + max(
                            2.0, float(args.interval))
                        default_map = os.path.join(
                            "build", "release", "default.map")
                        efmp_map = os.path.join(
                            "build", "release", "efmp.map")
                        personality_candidates = [
                            ("default",
                             resolve_map_symbol_in(
                                 "_g_SPXBBootPhase", default_map),
                             default_map),
                            ("efmp",
                             resolve_map_symbol_in(
                                 "_g_SPXBBootPhase", efmp_map),
                             efmp_map),
                        ]
                        if xblog_current_personality == "default":
                            personality_candidates.reverse()
                        for personality, boot_va, personality_map in personality_candidates:
                            if boot_va is None:
                                continue
                            probed_addr = probe_xblog_physical_addr(
                                sock, boot_va, runtime_delta, log)
                            if probed_addr is None:
                                continue
                            xblog_addr = probed_addr
                            xblog_va_for_probe = boot_va
                            xblog_offsets = resolve_symbol_offsets_in(
                                "_g_SPXBBootPhase", xblog_symbol_names,
                                personality_map)
                            xblog_font_base_va = resolve_map_symbol_in(
                                xblog_font_symbols[0], personality_map)
                            xblog_font_offsets = resolve_symbol_offsets_in(
                                xblog_font_symbols[0], xblog_font_symbols,
                                personality_map)
                            xblog_title_base_va = resolve_map_symbol_in(
                                xblog_title_symbols[0], personality_map)
                            xblog_title_offsets = resolve_symbol_offsets_in(
                                xblog_title_symbols[0], xblog_title_symbols,
                                personality_map)
                            texture_allocator_map = personality_map
                            texture_allocator_va = resolve_literal_map_symbol_in(
                                texture_allocator_symbol,
                                texture_allocator_map)
                            xblog_current_personality = personality
                            last_fps_heartbeat_count = None
                            runtime_delta = xblog_va_for_probe - xblog_addr
                            log("xblog_personality_switch t=%.1f personality=%s "
                                "va=0x%08x phys=0x%08x delta=0x%x map=%s" %
                                (elapsed, personality, boot_va, xblog_addr,
                                 runtime_delta, personality_map))
                            break
                    if not args.poll_xblog_perf_only:
                        for personality, mini_va, _mini_map in mini_soak_probes:
                            mini_addr = mini_va - runtime_delta
                            mini_reply = monitor_cmd(
                                sock, "xp/5wx 0x%08x" % mini_addr, 0.25)
                            mini_words = parse_monitor_words(mini_reply, mini_addr)
                            if len(mini_words) >= 5 and mini_words[0] == 0x4D534F4B:
                                log("xblogsoak-live t=%.1f personality=%s stage=%u transitions=%u activeMsec=%u flags=0x%08x" %
                                    (elapsed, personality, mini_words[1], mini_words[2],
                                     mini_words[3], mini_words[4]))
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

                    if args.poll_xblog_perf_only:
                        perf_poll_symbols = [
                            "_g_SPXBClsState",
                            "_g_SPXBPerfFrameMsec",
                            "_g_SPXBPerfServerMsec",
                            "_g_SPXBPerfClientMsec",
                            "_g_SPXBPerfGameMsec",
                            "_g_SPXBPerfFrontendMsec",
                            "_g_SPXBPerfBackendMsec",
                            "_g_SPXBPerfAudioMsec",
                            "_g_SPXBCameraActive",
                            "_g_SPXBPerfServerTicks",
                            "_g_SPXBPerfServerLastGameMsec",
                            "_g_SPXBPerfServerMaxGameMsec",
                            "_g_SPXBPerfGamePreMsec",
                            "_g_SPXBPerfGameEntitiesMsec",
                            "_g_SPXBPerfGamePostMsec",
                            "_g_SPXBPerfGameEntitiesVisited",
                            "_g_SPXBPerfGameMissiles",
                            "_g_SPXBPerfGameItems",
                            "_g_SPXBPerfGameMovers",
                            "_g_SPXBPerfGameClients",
                            "_g_SPXBPerfGameThinkDue",
                            "_g_SPXBPerfGameScripted",
                            "_g_SPXBPerfGameOther",
                            "_g_SPXBPerfScreenDrawMsec",
                            "_g_SPXBPerfEndFrameMsec",
                            "_g_SPXBPerfRenderTotalMsec",
                            "_g_SPXBPerfRenderSetupMsec",
                            "_g_SPXBPerfRenderMarkLeavesMsec",
                            "_g_SPXBPerfRenderWorldMsec",
                            "_g_SPXBPerfRenderPolysMsec",
                            "_g_SPXBPerfRenderProjectionMsec",
                            "_g_SPXBPerfRenderEntitiesMsec",
                            "_g_SPXBPerfRenderSortMsec",
                            "_g_SPXBPerfRenderDebugMsec",
                            "_g_SPXBPerfRenderViews",
                            "_g_SPXBPerfRenderPortals",
                            "_g_SPXBPerfRenderDrawSurfs",
                            "_g_SPXBPerfRenderRefEntities",
                            "_g_SPXBPerfRenderLeafs",
                        ]
                        perf_poll_offsets = [
                            xblog_offsets[name] for name in perf_poll_symbols
                            if name in xblog_offsets
                        ]
                        poll_words = max(64, max(perf_poll_offsets or [0]) + 2)
                    else:
                        poll_words = max(64, max(xblog_offsets.values() or [0]) + 8)
                    reply = monitor_cmd(sock, "%s/%dwx 0x%08x" % (xblog_cmd, poll_words, xblog_addr), 0.4)
                    words = parse_monitor_words(reply, xblog_addr)
                    font_words = []
                    title_words = []
                    if (not args.poll_xblog_perf_only and
                            xblog_font_base_va is not None and
                            xblog_va_for_probe is not None):
                        runtime_delta = xblog_va_for_probe - xblog_addr
                        xblog_font_addr = xblog_font_base_va - runtime_delta
                        font_poll_words = max(16, max(xblog_font_offsets.values() or [0]) + 2)
                        font_reply = monitor_cmd(
                            sock,
                            "xp/%dwx 0x%08x" % (font_poll_words, xblog_font_addr),
                            0.4)
                        font_words = parse_monitor_words(font_reply, xblog_font_addr)
                    if (not args.poll_xblog_perf_only and
                            xblog_title_base_va is not None and
                            xblog_va_for_probe is not None):
                        runtime_delta = xblog_va_for_probe - xblog_addr
                        xblog_title_addr = xblog_title_base_va - runtime_delta
                        title_poll_words = max(
                            7, max(xblog_title_offsets.values() or [0]) + 1)
                        title_reply = monitor_cmd(
                            sock,
                            "xp/%dwx 0x%08x" % (
                                title_poll_words, xblog_title_addr),
                            0.4)
                        title_words = parse_monitor_words(
                            title_reply, xblog_title_addr)
                    if (texture_allocator_va is not None and
                            xblog_va_for_probe is not None and
                            elapsed >= next_texture_allocator_poll):
                        next_texture_allocator_poll = elapsed + max(
                            5.0, float(args.interval))
                        runtime_delta = xblog_va_for_probe - xblog_addr
                        texture_allocator_addr = (
                            texture_allocator_va - runtime_delta)
                        texture_allocator = monitor_texture_allocator(
                            sock, texture_allocator_addr)
                        if texture_allocator is None:
                            log("xblogtex t=%.1f unavailable va=0x%08x phys=0x%08x" %
                                (elapsed, texture_allocator_va,
                                 texture_allocator_addr))
                        else:
                            log("xblogtex t=%.1f base=0x%08x used=%u capacity=%u "
                                "high=%u blocks=%u totalFree=%u largestFree=%u "
                                "complete=%u" %
                                (elapsed, texture_allocator["base"],
                                 texture_allocator["used"],
                                 texture_allocator["capacity"],
                                 texture_allocator["high_water"],
                                 texture_allocator["block_count"],
                                 texture_allocator["total_free"],
                                 texture_allocator["largest_free"],
                                 1 if texture_allocator["complete"] else 0))
                    if len(words) >= 9:
                        telemetry_words = words
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
                            if idx is None or idx >= len(telemetry_words):
                                return 0
                            return telemetry_words[idx]
                        def signed32(value):
                            return value - 0x100000000 if value & 0x80000000 else value
                        def font_word_for(symbol):
                            idx = xblog_font_offsets.get(symbol)
                            if idx is None or idx >= len(font_words):
                                return 0
                            return font_words[idx]
                        def title_word_for(symbol):
                            idx = xblog_title_offsets.get(symbol)
                            if idx is None or idx >= len(title_words):
                                return 0
                            return title_words[idx]
                        render_backend = word_for("_g_SPXBRenderBackendMsec")
                        primitive_calls = word_for("_g_SPXBFakeGLPrimitiveCalls")
                        primitive_verts = word_for("_g_SPXBFakeGLPrimitiveVerts")
                        state_flushes = word_for("_g_SPXBFakeGLStateFlushes")
                        native_draw_mode = word_for("_g_SPXBNativeDrawMode")
                        native_draw_count = word_for("_g_SPXBNativeDrawCount")
                        native_draw_source_vertices = word_for(
                            "_g_SPXBNativeDrawSourceVertices")
                        native_draw_max_index = word_for(
                            "_g_SPXBNativeDrawMaxIndex")
                        native_draw_stride = word_for("_g_SPXBNativeDrawStride")
                        native_draw_indices = word_for(
                            "_g_SPXBNativeDrawIndicesPtr")
                        native_draw_vertices = word_for(
                            "_g_SPXBNativeDrawVerticesPtr")
                        native_draw_path = word_for("_g_SPXBNativeDrawPath")
                        native_draw_shader = word_for("_g_SPXBNativeDrawShader")
                        native_draw_vertex_offset = word_for(
                            "_g_SPXBNativeDrawVertexOffset")
                        native_draw_index_offset = word_for(
                            "_g_SPXBNativeDrawIndexOffset")
                        native_draw_vertex_bytes = word_for(
                            "_g_SPXBNativeDrawVertexBytes")
                        native_draw_index_bytes = word_for(
                            "_g_SPXBNativeDrawIndexBytes")
                        native_draw_lock_flags = word_for(
                            "_g_SPXBNativeDrawLockFlags")
                        native_draw_min_index = word_for(
                            "_g_SPXBNativeDrawMinIndex")
                        main_loop_count = word_for("_g_SPXBMainLoopCount")
                        com_frame_count = word_for("_g_SPXBComFrameCount")
                        sv_frame_count = word_for("_g_SPXBSvFrameCount")
                        cl_frame_count = word_for("_g_SPXBClFrameCount")
                        cls_state = word_for("_g_SPXBClsState")
                        cl_server_time = word_for("_g_SPXBClServerTime")
                        cls_frame_count = word_for("_g_SPXBClsFrameCount")
                        phase_last = word_for("_g_SPXBPhaseLast")
                        com_subphase = word_for("_g_SPXBComSubphase")
                        com_spin_count = word_for("_g_SPXBComSpinCount")
                        com_msec = word_for("_g_SPXBComMsec")
                        com_frame_time = word_for("_g_SPXBComFrameTime")
                        com_last_time = word_for("_g_SPXBComLastTime")
                        cbuf_exec_count = word_for("_g_SPXBCbufExecCount")
                        cmd_exec_count = word_for("_g_SPXBCmdExecCount")
                        cmd_phase = word_for("_g_SPXBCmdPhase")
                        cmd_hash = word_for("_g_SPXBCmdHash")
                        cmd_argc = word_for("_g_SPXBCmdArgc")
                        map_phase = word_for("_g_SPXBMapPhase")
                        map_hash = word_for("_g_SPXBMapHash")
                        game_phase = word_for("_g_SPXBGamePhase")
                        game_entity_count = word_for("_g_SPXBGameEntityCount")
                        perf_frame = word_for("_g_SPXBPerfFrameMsec")
                        perf_server = word_for("_g_SPXBPerfServerMsec")
                        perf_client = word_for("_g_SPXBPerfClientMsec")
                        perf_game = word_for("_g_SPXBPerfGameMsec")
                        perf_frontend = word_for("_g_SPXBPerfFrontendMsec")
                        perf_backend = word_for("_g_SPXBPerfBackendMsec")
                        perf_audio = word_for("_g_SPXBPerfAudioMsec")
                        perf_server_ticks = word_for("_g_SPXBPerfServerTicks")
                        perf_server_last_game = word_for("_g_SPXBPerfServerLastGameMsec")
                        perf_server_max_game = word_for("_g_SPXBPerfServerMaxGameMsec")
                        perf_game_pre = word_for("_g_SPXBPerfGamePreMsec")
                        perf_game_entities = word_for("_g_SPXBPerfGameEntitiesMsec")
                        perf_game_post = word_for("_g_SPXBPerfGamePostMsec")
                        perf_game_entities_visited = word_for("_g_SPXBPerfGameEntitiesVisited")
                        perf_game_missiles = word_for("_g_SPXBPerfGameMissiles")
                        perf_game_items = word_for("_g_SPXBPerfGameItems")
                        perf_game_movers = word_for("_g_SPXBPerfGameMovers")
                        perf_game_clients = word_for("_g_SPXBPerfGameClients")
                        perf_game_think_due = word_for("_g_SPXBPerfGameThinkDue")
                        perf_game_scripted = word_for("_g_SPXBPerfGameScripted")
                        perf_game_other = word_for("_g_SPXBPerfGameOther")
                        perf_screen_draw = word_for("_g_SPXBPerfScreenDrawMsec")
                        perf_end_frame = word_for("_g_SPXBPerfEndFrameMsec")
                        perf_render_total = word_for("_g_SPXBPerfRenderTotalMsec")
                        perf_render_setup = word_for("_g_SPXBPerfRenderSetupMsec")
                        perf_render_mark = word_for("_g_SPXBPerfRenderMarkLeavesMsec")
                        perf_render_world = word_for("_g_SPXBPerfRenderWorldMsec")
                        perf_render_polys = word_for("_g_SPXBPerfRenderPolysMsec")
                        perf_render_projection = word_for("_g_SPXBPerfRenderProjectionMsec")
                        perf_render_entities = word_for("_g_SPXBPerfRenderEntitiesMsec")
                        perf_render_sort = word_for("_g_SPXBPerfRenderSortMsec")
                        perf_render_debug = word_for("_g_SPXBPerfRenderDebugMsec")
                        perf_render_views = word_for("_g_SPXBPerfRenderViews")
                        perf_render_portals = word_for("_g_SPXBPerfRenderPortals")
                        perf_render_draw_surfs = word_for("_g_SPXBPerfRenderDrawSurfs")
                        perf_render_ref_entities = word_for("_g_SPXBPerfRenderRefEntities")
                        perf_render_leafs = word_for("_g_SPXBPerfRenderLeafs")
                        camera_active = word_for("_g_SPXBCameraActive")
                        borg_plugged_count = word_for("_g_SPXBBorgPluggedCount")
                        borg_plugged_ent = word_for("_g_SPXBBorgPluggedEnt")
                        borg_plugged_spawnflags = word_for("_g_SPXBBorgPluggedSpawnflags")
                        borg_plugged_anim = word_for("_g_SPXBBorgPluggedAnim")
                        borg_plugged_legs_model = word_for("_g_SPXBBorgPluggedLegsModel")
                        borg_plugged_torso_model = word_for("_g_SPXBBorgPluggedTorsoModel")
                        borg_plugged_head_model = word_for("_g_SPXBBorgPluggedHeadModel")
                        borg_plugged_legs_skin = word_for("_g_SPXBBorgPluggedLegsSkin")
                        borg_plugged_torso_skin = word_for("_g_SPXBBorgPluggedTorsoSkin")
                        borg_plugged_head_skin = word_for("_g_SPXBBorgPluggedHeadSkin")
                        borg_plugged_legs_hash = word_for("_g_SPXBBorgPluggedLegsNameHash")
                        borg_plugged_torso_hash = word_for("_g_SPXBBorgPluggedTorsoNameHash")
                        borg_plugged_head_hash = word_for("_g_SPXBBorgPluggedHeadNameHash")
                        borg_active_count = word_for("_g_SPXBBorgActiveCount")
                        borg_active_ent = word_for("_g_SPXBBorgActiveEnt")
                        borg_active_spawnflags = word_for("_g_SPXBBorgActiveSpawnflags")
                        borg_active_anim = word_for("_g_SPXBBorgActiveAnim")
                        borg_active_legs_model = word_for("_g_SPXBBorgActiveLegsModel")
                        borg_active_torso_model = word_for("_g_SPXBBorgActiveTorsoModel")
                        borg_active_head_model = word_for("_g_SPXBBorgActiveHeadModel")
                        borg_active_legs_skin = word_for("_g_SPXBBorgActiveLegsSkin")
                        borg_active_torso_skin = word_for("_g_SPXBBorgActiveTorsoSkin")
                        borg_active_head_skin = word_for("_g_SPXBBorgActiveHeadSkin")
                        borg_active_legs_hash = word_for("_g_SPXBBorgActiveLegsNameHash")
                        borg_active_torso_hash = word_for("_g_SPXBBorgActiveTorsoNameHash")
                        borg_active_head_hash = word_for("_g_SPXBBorgActiveHeadNameHash")
                        split_shader = word_for("_g_SPXBRenderSplitShader")
                        split_fog = word_for("_g_SPXBRenderSplitFog")
                        split_dlight = word_for("_g_SPXBRenderSplitDlight")
                        split_entity = word_for("_g_SPXBRenderSplitEntity")
                        split_final = word_for("_g_SPXBRenderSplitFinal")
                        split_flush = word_for("_g_SPXBRenderSplitFlush")
                        split_slot_active = word_for("_g_SPXBSplitSlotActive")
                        split_slot0_draw = word_for("_g_SPXBSplitSlot0DrawDelta")
                        split_slot1_draw = word_for("_g_SPXBSplitSlot1DrawDelta")
                        split_slot0_world = word_for("_g_SPXBSplitSlot0WorldDelta")
                        split_slot1_world = word_for("_g_SPXBSplitSlot1WorldDelta")
                        split_slot0_cluster = word_for("_g_SPXBSplitSlot0Cluster")
                        split_slot1_cluster = word_for("_g_SPXBSplitSlot1Cluster")
                        split_slot1_retry = word_for("_g_SPXBSplitSlot1WorldRetryDelta")
                        split_slot1_fallback = word_for("_g_SPXBSplitSlot1WorldFallback")
                        split_slot0_marked = word_for("_g_SPXBSplitSlot0MarkedLeaves")
                        split_slot1_marked = word_for("_g_SPXBSplitSlot1MarkedLeaves")
                        split_slot0_pvsrej = word_for("_g_SPXBSplitSlot0PvsRejected")
                        split_slot1_pvsrej = word_for("_g_SPXBSplitSlot1PvsRejected")
                        split_slot0_arearej = word_for("_g_SPXBSplitSlot0AreaRejected")
                        split_slot1_arearej = word_for("_g_SPXBSplitSlot1AreaRejected")
                        split_slot0_rootvis = word_for("_g_SPXBSplitSlot0RootVis")
                        split_slot1_rootvis = word_for("_g_SPXBSplitSlot1RootVis")
                        split_slot0_attempt = word_for("_g_SPXBSplitSlot0WorldAttempts")
                        split_slot1_attempt = word_for("_g_SPXBSplitSlot1WorldAttempts")
                        split_slot0_culled = word_for("_g_SPXBSplitSlot0WorldCulled")
                        split_slot1_culled = word_for("_g_SPXBSplitSlot1WorldCulled")
                        split_slot0_already = word_for("_g_SPXBSplitSlot0WorldAlready")
                        split_slot1_already = word_for("_g_SPXBSplitSlot1WorldAlready")
                        split_slot0_added = word_for("_g_SPXBSplitSlot0WorldAdded")
                        split_slot1_added = word_for("_g_SPXBSplitSlot1WorldAdded")
                        split_p2_ent = word_for("_g_SPXBSplitP2Ent")
                        split_p2_trace = word_for("_g_SPXBSplitP2TraceFrac1000")
                        split_p2_view_x = word_for("_g_SPXBSplitP2ViewX")
                        split_p2_view_y = word_for("_g_SPXBSplitP2ViewY")
                        split_p2_view_z = word_for("_g_SPXBSplitP2ViewZ")
                        split_p2_ps_x = word_for("_g_SPXBSplitP2PsX")
                        split_p2_ps_y = word_for("_g_SPXBSplitP2PsY")
                        split_p2_ps_z = word_for("_g_SPXBSplitP2PsZ")
                        split_p2_cur_x = word_for("_g_SPXBSplitP2CurX")
                        split_p2_cur_y = word_for("_g_SPXBSplitP2CurY")
                        split_p2_cur_z = word_for("_g_SPXBSplitP2CurZ")
                        split_p2_pitch = word_for("_g_SPXBSplitP2AnglesPitch")
                        split_p2_yaw = word_for("_g_SPXBSplitP2AnglesYaw")
                        split_p2_refdef = word_for("_g_SPXBSplitP2RefdefValid")
                        split_p2_scene_considered = word_for("_g_SPXBSplitP2SceneConsidered")
                        split_p2_scene_added = word_for("_g_SPXBSplitP2SceneAdded")
                        split_p2_scene_self = word_for("_g_SPXBSplitP2SceneSelfAdded")
                        split_p2_model_enter = word_for("_g_SPXBSplitP2ModelEnter")
                        split_p2_model_return = word_for("_g_SPXBSplitP2ModelReturn")
                        split_p2_model_info = word_for("_g_SPXBSplitP2ModelInfoValid")
                        split_p2_model_submitted = word_for("_g_SPXBSplitP2ModelSubmitted")
                        split_p2_model_legs = word_for("_g_SPXBSplitP2ModelLegs")
                        split_p2_model_torso = word_for("_g_SPXBSplitP2ModelTorso")
                        split_p2_model_head = word_for("_g_SPXBSplitP2ModelHead")
                        split_p2_model_renderfx = word_for("_g_SPXBSplitP2ModelRenderfx")
                        split_p2_renderer_refs = word_for("_g_SPXBSplitP2RendererRefs")
                        split_p2_renderer_model = word_for("_g_SPXBSplitP2RendererLastModel")
                        split_p2_renderer_renderfx = word_for("_g_SPXBSplitP2RendererLastRenderfx")
                        split_p2_renderer_z = word_for("_g_SPXBSplitP2RendererLastZ")
                        vw_p1_adds = word_for("_g_SPXBViewWeaponP1Adds")
                        vw_p2_adds = word_for("_g_SPXBViewWeaponP2Adds")
                        vw_p1_skips = word_for("_g_SPXBViewWeaponP1Skips")
                        vw_p2_skips = word_for("_g_SPXBViewWeaponP2Skips")
                        vw_p1_model = word_for("_g_SPXBViewWeaponP1Model")
                        vw_p2_model = word_for("_g_SPXBViewWeaponP2Model")
                        vw_p1_rf = word_for("_g_SPXBViewWeaponP1Renderfx")
                        vw_p2_rf = word_for("_g_SPXBViewWeaponP2Renderfx")
                        vw_p1_render_adds = word_for("_g_SPXBViewWeaponP1RendererAdds")
                        vw_p2_render_adds = word_for("_g_SPXBViewWeaponP2RendererAdds")
                        vw_p1_render_filtered = word_for("_g_SPXBViewWeaponP1RendererFiltered")
                        vw_p2_render_filtered = word_for("_g_SPXBViewWeaponP2RendererFiltered")
                        vw_p1_last_skip = word_for("_g_SPXBViewWeaponP1LastSkip")
                        vw_p2_last_skip = word_for("_g_SPXBViewWeaponP2LastSkip")
                        weapon_reg_weapon = word_for("_g_SPXBWeaponRegWeapon")
                        weapon_reg_hash = word_for("_g_SPXBWeaponRegPathHash")
                        weapon_reg_view = word_for("_g_SPXBWeaponRegViewModel")
                        weapon_reg_world = word_for("_g_SPXBWeaponRegWorldModel")
                        weapon_reg_hands = word_for("_g_SPXBWeaponRegHandsModel")
                        weapon_reg_fail = word_for("_g_SPXBWeaponRegFailCode")
                        weapon_model_stage = word_for("_g_SPXBWeaponModelTraceStage")
                        weapon_model_hash = word_for("_g_SPXBWeaponModelTracePathHash")
                        weapon_model_disk_len = word_for("_g_SPXBWeaponModelTraceDiskLen")
                        weapon_model_disk_success = word_for("_g_SPXBWeaponModelTraceDiskSuccess")
                        weapon_model_ident = word_for("_g_SPXBWeaponModelTraceIdent")
                        weapon_model_version = word_for("_g_SPXBWeaponModelTraceVersion")
                        weapon_model_size = word_for("_g_SPXBWeaponModelTraceSize")
                        weapon_model_loaded = word_for("_g_SPXBWeaponModelTraceLoaded")
                        weapon_model_handle = word_for("_g_SPXBWeaponModelTraceHandle")
                        weapon_model_fail = word_for("_g_SPXBWeaponModelTraceFailCode")
                        weapon_load_stage = word_for("_g_SPXBWeaponLoadStage")
                        weapon_load_read_len = word_for("_g_SPXBWeaponLoadReadLen")
                        weapon_load_type_weapon = word_for("_g_SPXBWeaponLoadTypeWeapon")
                        weapon_load_model_weapon = word_for("_g_SPXBWeaponLoadModelWeapon")
                        weapon_load_model_hash = word_for("_g_SPXBWeaponLoadModelHash")
                        weapon_load_slot4_hash = word_for("_g_SPXBWeaponLoadSlot4Hash")
                        weapon_load_slot4_ammo = word_for("_g_SPXBWeaponLoadSlot4Ammo")
                        weapon_load_slot4_first4 = word_for("_g_SPXBWeaponLoadSlot4First4")
                        weapon_reg_first4 = word_for("_g_SPXBWeaponRegFirst4")
                        weapon_reg_class_hash = word_for("_g_SPXBWeaponRegClassHash")
                        split_camera_mode = word_for("_g_SPXBSplitCameraMode")
                        split_p1_trace = word_for("_g_SPXBSplitP1TraceFrac1000")
                        split_p1_local_x = word_for("_g_SPXBSplitP1LocalX1000")
                        split_p1_local_y = word_for("_g_SPXBSplitP1LocalY1000")
                        split_p1_local_z = word_for("_g_SPXBSplitP1LocalZ1000")
                        split_p2_local_x = word_for("_g_SPXBSplitP2LocalX1000")
                        split_p2_local_y = word_for("_g_SPXBSplitP2LocalY1000")
                        split_p2_local_z = word_for("_g_SPXBSplitP2LocalZ1000")
                        split_local_diff_x = word_for("_g_SPXBSplitLocalDiffX1000")
                        split_local_diff_y = word_for("_g_SPXBSplitLocalDiffY1000")
                        split_local_diff_z = word_for("_g_SPXBSplitLocalDiffZ1000")
                        direct_status = word_for("_g_SPXBDirectMapStatus")
                        direct_hash = word_for("_g_SPXBDirectMapHash")
                        direct_queued = word_for("_g_SPXBDirectMapQueuedCount")
                        sky_magic = word_for("_g_SPXBSkyTraceMagic")
                        sky_present = word_for("_g_SPXBSkyOuterPresentMask")
                        sky_fallback = word_for("_g_SPXBSkyOuterFallbackMask")
                        sky_tex = word_for("_g_SPXBSkyOuterTexMask")
                        sky_draw = word_for("_g_SPXBSkyOuterDrawMask")
                        sky_passes = word_for("_g_SPXBSkyLastPasses")
                        sky_sort = word_for("_g_SPXBSkyLastSort")
                        skyres_magic = word_for("_g_SPXBSkyResolveMagic")
                        skyres_count = word_for("_g_SPXBSkyResolveCount")
                        skyres_shader_num = word_for("_g_SPXBSkyResolveShaderNum")
                        skyres_map_hash = word_for("_g_SPXBSkyResolveMapHash")
                        skyres_resolved_hash = word_for("_g_SPXBSkyResolveResolvedHash")
                        skyres_surface_flags = word_for("_g_SPXBSkyResolveSurfaceFlags")
                        skyres_default = word_for("_g_SPXBSkyResolveDefault")
                        skyres_explicit = word_for("_g_SPXBSkyResolveExplicit")
                        skyres_has_sky = word_for("_g_SPXBSkyResolveHasSky")
                        skyres_passes = word_for("_g_SPXBSkyResolvePasses")
                        skyres_sort_x1000 = word_for("_g_SPXBSkyResolveSortX1000")
                        skyres_lightmap0 = word_for("_g_SPXBSkyResolveLightmap0")
                        shader_scan_magic = word_for("_g_SPXBShaderScanMagic")
                        shader_scan_scripts = word_for("_g_SPXBShaderScanScriptsFound")
                        shader_scan_shaders = word_for("_g_SPXBShaderScanShadersFound")
                        shader_scan_loaded = word_for("_g_SPXBShaderScanLoaded")
                        shader_scan_bytes = word_for("_g_SPXBShaderScanBytes")
                        shader_scan_entries = word_for("_g_SPXBShaderScanEntries")
                        shader_scan_sky_light = word_for("_g_SPXBShaderScanSkyLightSeen")
                        shader_scan_junk_sky = word_for("_g_SPXBShaderScanJunkSkySeen")
                        shader_scan_manifest_active = word_for("_g_SPXBShaderScanManifestActive")
                        shader_scan_manifest_read_len = word_for("_g_SPXBShaderScanManifestReadLen")
                        shader_scan_manifest_count = word_for("_g_SPXBShaderScanManifestCount")
                        shader_scan_raw_bytes = word_for("_g_SPXBShaderScanRawBytes")
                        shader_scan_voyager_listed = word_for("_g_SPXBShaderScanVoyagerListed")
                        shader_scan_voyager_read_len = word_for("_g_SPXBShaderScanVoyagerReadLen")
                        shader_scan_voyager_sky_token = word_for("_g_SPXBShaderScanVoyagerSkyToken")
                        shader_scan_common_read_len = word_for("_g_SPXBShaderScanCommonReadLen")
                        shader_lookup_magic = word_for("_g_SPXBShaderLookupMagic")
                        shader_lookup_count = word_for("_g_SPXBShaderLookupCount")
                        shader_lookup_hash = word_for("_g_SPXBShaderLookupHash")
                        shader_lookup_indexed = word_for("_g_SPXBShaderLookupIndexedFound")
                        shader_lookup_linear = word_for("_g_SPXBShaderLookupLinearFound")
                        shader_lookup_entries = word_for("_g_SPXBShaderLookupEntries")
                        helmet_p1_submitted = word_for("_g_SPXBHelmetP1Submitted")
                        helmet_p2_submitted = word_for("_g_SPXBHelmetP2Submitted")
                        helmet_p1_attached = word_for("_g_SPXBHelmetP1Attached")
                        helmet_p2_attached = word_for("_g_SPXBHelmetP2Attached")
                        helmet_p1_model = word_for("_g_SPXBHelmetP1Model")
                        helmet_p2_model = word_for("_g_SPXBHelmetP2Model")
                        helmet_p1_rf = word_for("_g_SPXBHelmetP1Renderfx")
                        helmet_p2_rf = word_for("_g_SPXBHelmetP2Renderfx")
                        helmet_renderer_refs = word_for("_g_SPXBHelmetRendererRefs")
                        helmet_renderer_surfaces = word_for("_g_SPXBHelmetRendererSurfaces")
                        helmet_renderer_filtered = word_for("_g_SPXBHelmetRendererFiltered")
                        helmet_renderer_last_model = word_for("_g_SPXBHelmetRendererLastModel")
                        helmet_renderer_last_rf = word_for("_g_SPXBHelmetRendererLastRenderfx")
                        helmet_renderer_last_ent = word_for("_g_SPXBHelmetRendererLastEnt")
                        helmet_renderer_last_filter = word_for("_g_SPXBHelmetRendererLastFilter")
                        helmet_renderer_last_surface_model = word_for("_g_SPXBHelmetRendererLastSurfaceModel")
                        helmet_game_p1_ensure = word_for("_g_SPXBHelmetGameP1Ensure")
                        helmet_game_p2_ensure = word_for("_g_SPXBHelmetGameP2Ensure")
                        helmet_game_p1_slot = word_for("_g_SPXBHelmetGameP1Slot")
                        helmet_game_p2_slot = word_for("_g_SPXBHelmetGameP2Slot")
                        helmet_cgame_p1_slot0 = word_for("_g_SPXBHelmetCgameP1Slot0")
                        helmet_cgame_p1_slot1 = word_for("_g_SPXBHelmetCgameP1Slot1")
                        helmet_cgame_p2_slot0 = word_for("_g_SPXBHelmetCgameP2Slot0")
                        helmet_cgame_p2_slot1 = word_for("_g_SPXBHelmetCgameP2Slot1")
                        helmet_bolton_load_len = word_for("_g_SPXBHelmetBoltOnLoadLen")
                        helmet_bolton_count = word_for("_g_SPXBHelmetBoltOnCount")
                        helmet_bolton_index = word_for("_g_SPXBHelmetBoltOnHelmetIndex")
                        helmet_add_attempts = word_for("_g_SPXBHelmetAddAttempts")
                        helmet_add_known_index = word_for("_g_SPXBHelmetAddKnownIndex")
                        helmet_add_fail = word_for("_g_SPXBHelmetAddFailCode")
                        fb_magic = word_for("_g_SPXBFallbackTraceMagic")
                        fb_count = word_for("_g_SPXBFallbackStageCount")
                        fb_shader = word_for("_g_SPXBFallbackLastShaderHash")
                        fb_image = word_for("_g_SPXBFallbackLastImageHash")
                        fb_stage = word_for("_g_SPXBFallbackLastStage")
                        fb_passes = word_for("_g_SPXBFallbackLastPasses")
                        fb_flags = word_for("_g_SPXBFallbackLastFlags")
                        fb_texnum = word_for("_g_SPXBFallbackLastTexnum")
                        fb_lightmap = word_for("_g_SPXBFallbackLastLightmap")
                        fb_state = word_for("_g_SPXBFallbackLastStateBits")
                        fb_indexes = word_for("_g_SPXBFallbackLastIndexes")
                        fb_x = word_for("_g_SPXBFallbackLastX1000")
                        fb_y = word_for("_g_SPXBFallbackLastY1000")
                        fb_z = word_for("_g_SPXBFallbackLastZ1000")
                        sv_probe_magic = word_for("_g_SPXBSVProbeMagic")
                        sv_probe_phase = word_for("_g_SPXBSVProbePhase")
                        sv_probe_subphase = word_for("_g_SPXBSVProbeSubphase")
                        sv_probe_a = word_for("_g_SPXBSVProbeA")
                        sv_probe_b = word_for("_g_SPXBSVProbeB")
                        sv_probe_c = word_for("_g_SPXBSVProbeC")
                        sv_probe_d = word_for("_g_SPXBSVProbeD")
                        loading_title_magic = title_word_for("_g_SPXBLoadingTitleMagic")
                        loading_title_status = title_word_for("_g_SPXBLoadingTitleStatus")
                        loading_title_shader = title_word_for("_g_SPXBLoadingTitleShader")
                        loading_title_draws = title_word_for("_g_SPXBLoadingTitleDraws")
                        loading_title_last_char = title_word_for("_g_SPXBLoadingTitleLastChar")
                        loading_title_map_hash = title_word_for("_g_SPXBLoadingTitleMapHash")
                        loading_title_text_hash = title_word_for("_g_SPXBLoadingTitleTextHash")
                        ui_magic = word_for("_g_SPXBUIStateMagic")
                        ui_started = word_for("_g_SPXBUIStarted")
                        ui_catcher = word_for("_g_SPXBUIKeyCatcher")
                        ui_pause = word_for("_g_SPXBUIPauseActive")
                        ui_qmenu = word_for("_g_SPXBUIQmenuActive")
                        ui_refresh = word_for("_g_SPXBUIRefreshCount")
                        ui_pause_open = word_for("_g_SPXBUIPauseOpenCount")
                        ui_pause_draw = word_for("_g_SPXBUIPauseDrawCount")
                        ui_font_magic = word_for("_g_SPXBUIFontMagic")
                        ui_font_loads = font_word_for("_g_SPXBUIFontLoadAttempts")
                        ui_font_len = font_word_for("_g_SPXBUIFontFileLen")
                        ui_font_loaded = font_word_for("_g_SPXBUIFontLoaded")
                        ui_font_tiny = font_word_for("_g_SPXBUIFontTinyShader")
                        ui_font_medium = font_word_for("_g_SPXBUIFontMediumShader")
                        ui_font_big = font_word_for("_g_SPXBUIFontBigShader")
                        ui_font_draws = font_word_for("_g_SPXBUIFontDrawCalls")
                        ui_font_reject = font_word_for("_g_SPXBUIFontDrawRejectMask")
                        ui_font_char = font_word_for("_g_SPXBUIFontLastChar")
                        ui_font_shader = font_word_for("_g_SPXBUIFontLastShader")
                        ui_font_sx = font_word_for("_g_SPXBUIFontLastSX")
                        ui_font_sy = font_word_for("_g_SPXBUIFontLastSY")
                        ui_font_sw = font_word_for("_g_SPXBUIFontLastSW")
                        mini_soak_magic = word_for("_g_SPXBMiniSoakMagic")
                        mini_soak_stage = word_for("_g_SPXBMiniSoakStage")
                        mini_soak_transitions = word_for("_g_SPXBMiniSoakTransitions")
                        mini_soak_active_msec = word_for("_g_SPXBMiniSoakActiveMsec")
                        mini_soak_flags = word_for("_g_SPXBMiniSoakFlags")
                        if (heartbeat_count != last_fps_heartbeat_count and
                                heartbeat_fps10 > 0):
                            last_fps_heartbeat_count = heartbeat_count
                            fps_sample = heartbeat_fps10 / 10.0
                            if cls_state == 7:
                                active_fps_samples.append(fps_sample)
                                personality_active_fps_samples.setdefault(
                                    xblog_current_personality, []).append(fps_sample)
                                if camera_active == 0:
                                    gameplay_fps_samples.append(fps_sample)
                                    personality_gameplay_fps_samples.setdefault(
                                        xblog_current_personality, []).append(fps_sample)
                        detail_log = (log if xblog_current_personality == "default"
                                      else lambda _message: None)
                        if xblog_current_personality == "efmp":
                            log("xblog t=%.1f personality=efmp boot=0x%08x "
                                "mirror=%u writes=%u delta=%d hb=0x%08x "
                                "count=%u frame=%u rt=%u st=%u fps=%.1f cls=%u" %
                                (elapsed, boot_phase, mirror_pos, write_count,
                                 delta, heartbeat_magic, heartbeat_count,
                                 heartbeat_frame, heartbeat_rt, heartbeat_st,
                                 heartbeat_fps10 / 10.0, cls_state))
                        detail_log("xblog t=%.1f boot=0x%08x mirror=%u writes=%u delta=%d hb=0x%08x count=%u frame=%u rt=%u st=%u fps=%.1f main=%u com=%u sv=%u cl=%u cls=%u clst=%u clsfr=%u phase=0x%08x sub=%u spin=%u msec=%u ctime=%u ltime=%u cbuf=%u cmd=%u cmdp=%u cmdh=0x%08x argc=%u mapp=%u maph=0x%08x gamep=%u ents=%u be=%u prim=%u verts=%u state=%u split=%u/%u/%u/%u final=%u flush=%u splitSlot=%u draw=%u/%u world=%u/%u retry=%u fallback=%u cluster=%d/%d mark=%d/%d pvsrej=%u/%u arearej=%u/%u root=%d/%d surf=%u/%u/%u/%u/%u/%u/%u/%u p2=%u trace=%u view=%d/%d/%d ps=%d/%d/%d cur=%d/%d/%d ang=%d/%d cam=%u p1trace=%u p1loc=%d/%d/%d p2loc=%d/%d/%d diff=%d/%d/%d p2dbg=ref=%u scene=%u/%u/%u model=%u/%u/%u/%u h=%u/%u/%u rf=0x%08x renderer=%u/%u/0x%08x/%d vw=%u/%u/%u/%u model=%u/%u rf=0x%08x/0x%08x rend=%u/%u filt=%u/%u skip=%u/%u wreg=%u/0x%08x/0x%08x/0x%08x/%u/%u/%u/%u wload=%u/%u/%u/%u/0x%08x/0x%08x/%u/0x%08x wm=%u/0x%08x/%u/%u/0x%08x/%u/%u/%u/%u/%u sky=0x%08x/%u/%u/%u/%u/%u/%u direct=%u/0x%08x/%u svp=0x%08x/0x%08x/%u/%u/%u/%u/%u" %
                            (elapsed, boot_phase, mirror_pos, write_count, delta,
                             heartbeat_magic, heartbeat_count, heartbeat_frame,
                             heartbeat_rt, heartbeat_st, heartbeat_fps10 / 10.0,
                             main_loop_count, com_frame_count, sv_frame_count,
                             cl_frame_count, cls_state, cl_server_time, cls_frame_count,
                             phase_last, com_subphase, com_spin_count, com_msec,
                             com_frame_time, com_last_time, cbuf_exec_count,
                             cmd_exec_count, cmd_phase, cmd_hash, cmd_argc,
                             map_phase, map_hash, game_phase, game_entity_count,
                             render_backend, primitive_calls, primitive_verts,
                             state_flushes, split_shader, split_fog, split_dlight,
                             split_entity, split_final, split_flush,
                             split_slot_active, split_slot0_draw, split_slot1_draw,
                             split_slot0_world, split_slot1_world,
                             split_slot1_retry, split_slot1_fallback,
                             signed32(split_slot0_cluster), signed32(split_slot1_cluster),
                             signed32(split_slot0_marked), signed32(split_slot1_marked),
                             split_slot0_pvsrej, split_slot1_pvsrej,
                             split_slot0_arearej, split_slot1_arearej,
                             signed32(split_slot0_rootvis), signed32(split_slot1_rootvis),
                             split_slot0_attempt, split_slot1_attempt,
                             split_slot0_culled, split_slot1_culled,
                             split_slot0_already, split_slot1_already,
                             split_slot0_added, split_slot1_added,
                             split_p2_ent, split_p2_trace,
                              signed32(split_p2_view_x), signed32(split_p2_view_y), signed32(split_p2_view_z),
                              signed32(split_p2_ps_x), signed32(split_p2_ps_y), signed32(split_p2_ps_z),
                              signed32(split_p2_cur_x), signed32(split_p2_cur_y), signed32(split_p2_cur_z),
                              signed32(split_p2_pitch), signed32(split_p2_yaw),
                              split_camera_mode, split_p1_trace,
                              signed32(split_p1_local_x), signed32(split_p1_local_y), signed32(split_p1_local_z),
                              signed32(split_p2_local_x), signed32(split_p2_local_y), signed32(split_p2_local_z),
                              signed32(split_local_diff_x), signed32(split_local_diff_y), signed32(split_local_diff_z),
                              split_p2_refdef,
                              split_p2_scene_considered, split_p2_scene_added, split_p2_scene_self,
                              split_p2_model_enter, split_p2_model_return, split_p2_model_info,
                              split_p2_model_submitted,
                              split_p2_model_legs, split_p2_model_torso, split_p2_model_head,
                              split_p2_model_renderfx,
                              split_p2_renderer_refs, split_p2_renderer_model,
                              split_p2_renderer_renderfx, signed32(split_p2_renderer_z),
                              vw_p1_adds, vw_p2_adds, vw_p1_skips, vw_p2_skips,
                              vw_p1_model, vw_p2_model,
                              vw_p1_rf, vw_p2_rf,
                               vw_p1_render_adds, vw_p2_render_adds,
                               vw_p1_render_filtered, vw_p2_render_filtered,
                               vw_p1_last_skip, vw_p2_last_skip,
                               weapon_reg_weapon, weapon_reg_hash,
                               weapon_reg_first4, weapon_reg_class_hash,
                               weapon_reg_view, weapon_reg_world, weapon_reg_hands,
                               weapon_reg_fail, weapon_load_stage, weapon_load_read_len,
                               weapon_load_type_weapon, weapon_load_model_weapon,
                               weapon_load_model_hash, weapon_load_slot4_hash,
                               weapon_load_slot4_ammo, weapon_load_slot4_first4,
                               weapon_model_stage, weapon_model_hash,
                               weapon_model_disk_len, weapon_model_disk_success,
                               weapon_model_ident, weapon_model_version, weapon_model_size,
                               weapon_model_loaded, weapon_model_handle, weapon_model_fail,
                               sky_magic, sky_present, sky_fallback, sky_tex,
                               sky_draw, sky_passes, sky_sort,
                             direct_status, direct_hash, direct_queued,
                             sv_probe_magic, sv_probe_phase, sv_probe_subphase,
                             sv_probe_a, sv_probe_b, sv_probe_c, sv_probe_d))
                        detail_log("xblogui t=%.1f magic=0x%08x started=%u catcher=0x%08x pause=%u qmenu=%u refresh=%u open=%u draw=%u" %
                            (elapsed, ui_magic, ui_started, ui_catcher, ui_pause,
                             ui_qmenu, ui_refresh, ui_pause_open, ui_pause_draw))
                        detail_log("xblogperf t=%.1f frame=%u server=%u client=%u game=%u frontend=%u backend=%u audio=%u camera=%u ticks=%u lastGame=%u maxGame=%u screen=%u/%u gamePhases=%u/%u/%u entities=%u categories=missile:%u item:%u mover:%u client:%u think:%u script:%u other:%u" %
                            (elapsed, perf_frame, perf_server, perf_client,
                             perf_game, perf_frontend, perf_backend, perf_audio,
                             camera_active, perf_server_ticks,
                             perf_server_last_game, perf_server_max_game,
                             perf_screen_draw, perf_end_frame,
                             perf_game_pre, perf_game_entities, perf_game_post,
                             perf_game_entities_visited, perf_game_missiles,
                             perf_game_items, perf_game_movers, perf_game_clients,
                             perf_game_think_due, perf_game_scripted,
                             perf_game_other))
                        detail_log("xblogrender t=%.1f total=%u setup=%u mark=%u world=%u polys=%u projection=%u entities=%u sort=%u debug=%u views=%u portals=%u drawSurfs=%u refEntities=%u leafs=%u" %
                            (elapsed, perf_render_total, perf_render_setup,
                             perf_render_mark, perf_render_world,
                             perf_render_polys, perf_render_projection,
                             perf_render_entities, perf_render_sort,
                             perf_render_debug, perf_render_views,
                             perf_render_portals, perf_render_draw_surfs,
                             perf_render_ref_entities, perf_render_leafs))
                        detail_log("xblognative t=%.1f path=%u mode=%u count=%u sourceVerts=%u indexRange=%u..%u stride=%u shader=0x%08x vbOff=%u ibOff=%u vbBytes=%u ibBytes=%u locks=0x%08x indices=0x%08x vertices=0x%08x" %
                            (elapsed, native_draw_path, native_draw_mode,
                             native_draw_count, native_draw_source_vertices,
                             native_draw_min_index, native_draw_max_index,
                             native_draw_stride, native_draw_shader,
                             native_draw_vertex_offset,
                             native_draw_index_offset,
                             native_draw_vertex_bytes,
                             native_draw_index_bytes,
                             native_draw_lock_flags,
                             native_draw_indices, native_draw_vertices))
                        detail_log("xblogborg t=%.1f plugged=count=%u ent=%u flags=0x%08x anim=%u/%u models=%u/%u/%u skins=%u/%u/%u names=%08x/%08x/%08x active=count=%u ent=%u flags=0x%08x anim=%u/%u models=%u/%u/%u skins=%u/%u/%u names=%08x/%08x/%08x" %
                            (elapsed,
                             borg_plugged_count, borg_plugged_ent,
                             borg_plugged_spawnflags,
                             borg_plugged_anim & 0xffff,
                             (borg_plugged_anim >> 16) & 0xffff,
                             borg_plugged_legs_model, borg_plugged_torso_model,
                             borg_plugged_head_model, borg_plugged_legs_skin,
                             borg_plugged_torso_skin, borg_plugged_head_skin,
                             borg_plugged_legs_hash, borg_plugged_torso_hash,
                             borg_plugged_head_hash,
                             borg_active_count, borg_active_ent,
                             borg_active_spawnflags,
                             borg_active_anim & 0xffff,
                             (borg_active_anim >> 16) & 0xffff,
                             borg_active_legs_model, borg_active_torso_model,
                             borg_active_head_model, borg_active_legs_skin,
                             borg_active_torso_skin, borg_active_head_skin,
                             borg_active_legs_hash, borg_active_torso_hash,
                             borg_active_head_hash))
                        detail_log("xblogfont t=%.1f magic=0x%08x loads=%u len=%u loaded=%u shaders=%u/%u/%u draws=%u reject=0x%08x last=%u/%u/%u/%u/%u" %
                            (elapsed, ui_font_magic, ui_font_loads, ui_font_len,
                             ui_font_loaded, ui_font_tiny, ui_font_medium,
                             ui_font_big, ui_font_draws, ui_font_reject,
                             ui_font_char, ui_font_shader, ui_font_sx,
                             ui_font_sy, ui_font_sw))
                        detail_log("xblogsoak t=%.1f magic=0x%08x stage=%u transitions=%u activeMsec=%u flags=0x%08x" %
                            (elapsed, mini_soak_magic, mini_soak_stage,
                             mini_soak_transitions, mini_soak_active_msec,
                             mini_soak_flags))
                        detail_log("xblogloadtitle t=%.1f magic=0x%08x status=0x%08x shader=%u draws=%u last=%u map=0x%08x text=0x%08x" %
                            (elapsed, loading_title_magic, loading_title_status,
                             loading_title_shader, loading_title_draws,
                             loading_title_last_char, loading_title_map_hash,
                             loading_title_text_hash))
                        if (helmet_game_p1_ensure or helmet_game_p2_ensure or
                                helmet_cgame_p1_slot0 or helmet_cgame_p1_slot1 or
                                helmet_cgame_p2_slot0 or helmet_cgame_p2_slot1 or
                                helmet_p1_submitted or helmet_p2_submitted or
                                helmet_renderer_refs or helmet_renderer_surfaces or
                                helmet_renderer_filtered or helmet_bolton_load_len or
                                helmet_bolton_count or helmet_add_attempts):
                            detail_log("xbloghelmet t=%.1f load=%u/%u/%u add=%u/%u/%u game=%u/%u/%u/%u cgame=%u/%u/%u/%u p1=%u/%u/%u/0x%08x p2=%u/%u/%u/0x%08x rend=%u/%u/%u/%u/0x%08x/%u/%u/%u" %
                                (elapsed,
                                 helmet_bolton_load_len, helmet_bolton_count,
                                 helmet_bolton_index, helmet_add_attempts,
                                 helmet_add_known_index, helmet_add_fail,
                                 helmet_game_p1_ensure, helmet_game_p2_ensure,
                                 helmet_game_p1_slot, helmet_game_p2_slot,
                                 helmet_cgame_p1_slot0, helmet_cgame_p1_slot1,
                                 helmet_cgame_p2_slot0, helmet_cgame_p2_slot1,
                                 helmet_p1_submitted, helmet_p1_attached,
                                 helmet_p1_model, helmet_p1_rf,
                                 helmet_p2_submitted, helmet_p2_attached,
                                 helmet_p2_model, helmet_p2_rf,
                                 helmet_renderer_refs, helmet_renderer_surfaces,
                                 helmet_renderer_filtered, helmet_renderer_last_model,
                                 helmet_renderer_last_rf, helmet_renderer_last_ent,
                                 helmet_renderer_last_filter,
                                 helmet_renderer_last_surface_model))
                        if fb_count or fb_shader or fb_image:
                            detail_log("xblogfb t=%.1f magic=0x%08x count=%u shader=0x%08x image=0x%08x stage=%u passes=%u flags=0x%08x tex=%u lm=%u state=0x%08x idx=%u xyz=%d/%d/%d" %
                                (elapsed, fb_magic, fb_count, fb_shader, fb_image,
                                 fb_stage, fb_passes, fb_flags, fb_texnum,
                                 fb_lightmap, fb_state, fb_indexes,
                                 signed32(fb_x), signed32(fb_y), signed32(fb_z)))
                        if skyres_count or skyres_map_hash or skyres_resolved_hash:
                            detail_log("xblogskyres t=%.1f magic=0x%08x count=%u shaderNum=%u map=0x%08x resolved=0x%08x surf=0x%08x default=%u explicit=%u hasSky=%u passes=%u sort1000=%d lm0=%d" %
                                (elapsed, skyres_magic, skyres_count, skyres_shader_num,
                                 skyres_map_hash, skyres_resolved_hash, skyres_surface_flags,
                                 skyres_default, skyres_explicit, skyres_has_sky,
                                 skyres_passes, signed32(skyres_sort_x1000),
                                 signed32(skyres_lightmap0)))
                        if shader_scan_magic:
                            detail_log("xblogshader t=%.1f scan=0x%08x scripts=%u shaders=%u loaded=%u bytes=%u raw=%u entries=%u skyLight=%u junkSky=%u manifest=%u/%u/%u voyager=%u/%u/%u common=%u lookup=0x%08x count=%u hash=0x%08x indexed=%u linear=%u lookupEntries=%u" %
                                (elapsed, shader_scan_magic, shader_scan_scripts,
                                 shader_scan_shaders, shader_scan_loaded, shader_scan_bytes,
                                 shader_scan_raw_bytes, shader_scan_entries,
                                 shader_scan_sky_light, shader_scan_junk_sky,
                                 shader_scan_manifest_active,
                                 shader_scan_manifest_read_len,
                                 shader_scan_manifest_count,
                                 shader_scan_voyager_listed,
                                 shader_scan_voyager_read_len,
                                 shader_scan_voyager_sky_token,
                                 shader_scan_common_read_len,
                                 shader_lookup_magic, shader_lookup_count, shader_lookup_hash,
                                 shader_lookup_indexed, shader_lookup_linear, shader_lookup_entries))
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
                if capture_enabled:
                    time.sleep(0.1)
                    png = os.path.abspath("%s_%02d.png" % (prefix, shot))
                    xblog_phys_delta = None
                    if xblog_va_for_probe is not None and xblog_addr is not None:
                        xblog_phys_delta = xblog_va_for_probe - xblog_addr
                    if args.xemu_native_screenshots:
                        guest_frozen = False
                        if sock is not None:
                            monitor_cmd(sock, "stop", 0.2)
                            guest_frozen = True
                        try:
                            ok, detail, native_path = xemu_trigger_native_screenshot(
                                proc.pid, xemu_exe, xemu_screenshot_dir)
                        finally:
                            if guest_frozen:
                                monitor_cmd(sock, "cont", 0.2)
                        if ok and native_path:
                            png = native_path
                    else:
                        ok, detail = monitor_screendump(sock, png, xblog_phys_delta)
                    if ok:
                        shot_paths.append(png)
                    log("shot=%02d t=%.1f ok=%s bytes=%s detail=%s" %
                        (shot, elapsed, ok, os.path.getsize(png) if ok and os.path.exists(png) else 0, detail))
                else:
                    log("shot=%02d t=%.1f skipped display=%s" % (shot, elapsed, display_mode))
                shot += 1
                next_shot += args.interval

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
            # Freeze the guest before collecting final diagnostics. Large memory
            # dumps can take several seconds, otherwise ring buffers and related
            # counters continue changing underneath the capture.
            monitor_cmd(sock, "stop", 0.2)
            log("emulation_stopped_for_final_diagnostics")
            dump_monitor_state(sock, prefix, "final", args.dump_mem, log)
            dump_virtual_memory_binary(sock, prefix, args.dump_bin_mem, log)
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
            with open(config_path, "w", encoding="utf-8", errors="replace") as f:
                f.write(toml_backup)

    contact = prefix + "_contact.png"
    if write_contact_sheet(shot_paths, contact):
        log("contact=%s" % os.path.abspath(contact))

    def log_fps_summary(label, samples):
        if not samples:
            log("fps_summary bucket=%s samples=0" % label)
            return
        ordered = sorted(samples)
        p10_index = max(0, int((len(ordered) - 1) * 0.10))
        below_30 = sum(1 for sample in samples if sample < 30.0)
        log("fps_summary bucket=%s samples=%u avg=%.1f min=%.1f p10=%.1f max=%.1f below30=%u below30_pct=%.1f" %
            (label, len(samples), sum(samples) / len(samples), ordered[0],
             ordered[p10_index], ordered[-1], below_30,
             (below_30 * 100.0) / len(samples)))

    log_fps_summary("active", active_fps_samples)
    log_fps_summary("gameplay", gameplay_fps_samples)
    for personality in sorted(personality_active_fps_samples):
        log_fps_summary(
            "%s_active" % personality,
            personality_active_fps_samples[personality])
    for personality in sorted(personality_gameplay_fps_samples):
        log_fps_summary(
            "%s_gameplay" % personality,
            personality_gameplay_fps_samples[personality])

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
