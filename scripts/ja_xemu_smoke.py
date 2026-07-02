#!/usr/bin/env python3
import argparse
import os
import socket
import subprocess
import sys
import time
import re
import json

from PIL import Image, ImageDraw


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


def ensure_xemu_copy():
    if not os.path.exists(XEMU_JA):
        import shutil
        shutil.copy2(XEMU_BASE, XEMU_JA)


def toml_set_single_quoted(toml, key, value, section="general"):
    escaped = value.replace("'", "''")
    replacement = "%s = '%s'" % (key, escaped)
    pattern = r"(?m)^%s = '.*'$" % re.escape(key)
    if re.search(pattern, toml):
        return re.sub(pattern, lambda _match: replacement, toml)

    section_pattern = r"(?m)^\[%s\]\s*$" % re.escape(section)
    match = re.search(section_pattern, toml)
    if match:
        insert_at = match.end()
        return toml[:insert_at] + "\n" + replacement + toml[insert_at:]
    return "[%s]\n%s\n\n%s" % (section, replacement, toml)


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

    with open(xemu_exe, "rb") as f:
        data = f.read()
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

    _name, text_va, text_raw, text_size = text_section
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
    if not psapi.EnumProcessModules(process_handle, ctypes.byref(modules),
                                    ctypes.sizeof(modules), ctypes.byref(needed)):
        return None
    return int(modules[0]) if modules[0] else None


def xemu_trigger_native_screenshot(pid, xemu_exe, screenshot_dir, timeout=3.0):
    if os.name != "nt":
        return False, "xemu native screenshots require Windows process memory access", None

    import ctypes

    os.makedirs(screenshot_dir, exist_ok=True)
    before = set(os.path.abspath(os.path.join(screenshot_dir, name))
                 for name in os.listdir(screenshot_dir)
                 if name.lower().endswith(".png"))

    pointer_rva = xemu_find_screenshot_flag_pointer_rva(xemu_exe)
    if pointer_rva is None:
        return False, "xemu native screenshot flag path not found in %s" % xemu_exe, None

    kernel32 = ctypes.windll.kernel32
    PROCESS_QUERY_INFORMATION = 0x0400
    PROCESS_VM_OPERATION = 0x0008
    PROCESS_VM_READ = 0x0010
    PROCESS_VM_WRITE = 0x0020
    process = kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                   PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid)
    if not process:
        return False, "xemu native screenshot OpenProcess failed pid=%d" % pid, None

    flag_addr = None
    try:
        module_base = xemu_process_module_base(process)
        if module_base is None:
            return False, "xemu native screenshot module base not found pid=%d" % pid, None

        pointer_addr = module_base + pointer_rva
        pointer_buf = (ctypes.c_ubyte * 8)()
        transferred = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(process, ctypes.c_void_p(pointer_addr),
                                          pointer_buf, 8, ctypes.byref(transferred)):
            return False, "xemu native screenshot flag pointer unreadable rva=0x%x" % pointer_rva, None

        flag_addr = int.from_bytes(bytes(pointer_buf), "little")
        one = (ctypes.c_ubyte * 1)(1)
        if not kernel32.WriteProcessMemory(process, ctypes.c_void_p(flag_addr),
                                           one, 1, ctypes.byref(transferred)):
            return False, "xemu native screenshot flag write failed addr=0x%x" % flag_addr, None
    finally:
        kernel32.CloseHandle(process)

    end = time.time() + timeout
    newest = None
    while time.time() < end:
        current = []
        for name in os.listdir(screenshot_dir):
            if not name.lower().endswith(".png"):
                continue
            path = os.path.abspath(os.path.join(screenshot_dir, name))
            if path not in before and os.path.exists(path):
                current.append(path)
        if current:
            newest = max(current, key=lambda path: os.path.getmtime(path))
            return True, "xemu native screenshot flag_rva=0x%x flag=0x%x dir=%s file=%s" % (
                pointer_rva, flag_addr or 0, screenshot_dir, newest), newest
        time.sleep(0.1)

    return False, "xemu native screenshot flag set but no PNG appeared rva=0x%x flag=0x%x dir=%s" % (
        pointer_rva, flag_addr or 0, screenshot_dir), newest


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


class QmpClient:
    def __init__(self, sock):
        self.sock = sock
        self.file = sock.makefile("rwb", buffering=0)
        self._read_obj()
        self.cmd("qmp_capabilities")

    def _read_obj(self):
        while True:
            line = self.file.readline()
            if not line:
                raise RuntimeError("qmp connection closed")
            line = line.strip()
            if not line:
                continue
            return json.loads(line.decode("utf-8", errors="replace"))

    def cmd(self, execute, arguments=None):
        payload = {"execute": execute}
        if arguments is not None:
            payload["arguments"] = arguments
        self.file.write((json.dumps(payload) + "\r\n").encode("utf-8"))
        while True:
            reply = self._read_obj()
            if "event" in reply:
                continue
            return reply

    def close(self):
        try:
            self.file.close()
        except Exception:
            pass
        try:
            self.sock.close()
        except Exception:
            pass


def qmp_connect(port, timeout=12.0):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            sock = socket.socket()
            sock.settimeout(3.0)
            sock.connect(("127.0.0.1", port))
            return QmpClient(sock)
        except Exception as exc:
            last = exc
            time.sleep(0.25)
    raise RuntimeError("qmp not ready: %s" % last)


def qmp_screendump(qmp, path):
    if qmp is None:
        return False, "qmp disabled"

    target = os.path.abspath(path).replace("\\", "/")
    try:
        if os.path.exists(path):
            os.remove(path)
    except Exception:
        pass

    try:
        reply = qmp.cmd("screendump", {"filename": target})
        if "error" in reply:
            hmp_reply = qmp.cmd("human-monitor-command", {
                "command-line": "screendump %s" % target,
            })
            if "error" in hmp_reply:
                return False, "qmp screendump error: %s; hmp-through-qmp error: %s" % (
                    reply["error"], hmp_reply["error"])
            reply = hmp_reply
        end = time.time() + 4.0
        while time.time() < end:
            if os.path.exists(path) and os.path.getsize(path) > 0:
                img = Image.open(path).convert("RGB")
                img.save(path)
                return True, "qmp screendump %dx%d" % (img.width, img.height)
            time.sleep(0.1)
        return False, "qmp screendump missing reply=%s" % reply
    except Exception as exc:
        return False, "qmp screendump failed: %s" % exc


def strip_ansi(text):
    return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)


def parse_register_snapshot(text):
    clean = strip_ansi(text)
    out = {}
    for key in ("EIP", "EAX", "EBX", "ECX", "EDX", "ESP", "CR2"):
        m = re.search(r"\b%s=([0-9a-fA-F]{8})" % key, clean)
        if m:
            out[key.lower()] = int(m.group(1), 16)
    hlt = re.search(r"\bHLT=([01])", clean)
    if hlt:
        out["hlt"] = int(hlt.group(1))
    return out


def parse_monitor_words(text, base_addr=None):
    text = strip_ansi(text)
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
        if len(words) >= 5 and words[0] == 0x53504A41 and words[4] == 0x48424653:
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
            if scan_words[i] == 0x53504A41 and scan_words[i + 4] == 0x48424653:
                found_addr = scan_start + i * 4 + 4
                found_delta = boot_va - found_addr
                log("xblog_probe scanned delta=0x%x addr=0x%08x" % (found_delta, found_addr))
                return found_addr

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


def monitor_read_phys_words(sock, phys_addr, count=16):
    reply = monitor_cmd(sock, "xp/%dwx 0x%08x" % (count, phys_addr), 0.2)
    return parse_monitor_words(reply, phys_addr)


def monitor_read_string(sock, addr, byte_count, phys_delta=None):
    if sock is None or addr is None or byte_count <= 0:
        return ""
    read_addr = addr
    cmd = "x"
    if phys_delta is not None:
        read_addr = addr - phys_delta
        cmd = "xp"
    word_count = (byte_count + 3) // 4
    reply = monitor_cmd(sock, "%s/%dwx 0x%08x" % (cmd, word_count, read_addr), 0.25)
    words = parse_monitor_words(reply, read_addr)
    raw = bytearray()
    for word in words:
        raw.extend((
            word & 0xff,
            (word >> 8) & 0xff,
            (word >> 16) & 0xff,
            (word >> 24) & 0xff,
        ))
    raw = raw[:byte_count]
    if b"\x00" in raw:
        raw = raw[:raw.index(0)]
    return raw.decode("ascii", errors="replace")


def monitor_pmemsave(sock, phys_addr, byte_count, path):
    monitor_path = os.path.abspath(path).replace("\\", "/")
    try:
        if os.path.exists(path):
            os.remove(path)
    except Exception:
        pass
    monitor_cmd(sock, "pmemsave 0x%08x 0x%x %s" % (phys_addr, byte_count, monitor_path), 4.0)
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
    if sock is None:
        return False, "monitor disabled"
    ppm_path = os.path.abspath(os.path.splitext(path)[0] + ".ppm")
    try:
        rel_ppm_path = os.path.relpath(ppm_path, os.getcwd()).replace("\\", "/")
    except ValueError:
        rel_ppm_path = None
    monitor_path = ppm_path.replace("\\", "/")
    replies = []
    try:
        if os.path.exists(ppm_path):
            os.remove(ppm_path)
        commands = []
        if rel_ppm_path and not rel_ppm_path.startswith(".."):
            commands.append("screendump %s" % rel_ppm_path)
            commands.append('screendump "%s"' % rel_ppm_path)
        commands.extend([
            "screendump %s" % monitor_path,
            'screendump "%s"' % monitor_path,
            "screendump %s" % ppm_path,
        ])
        for cmd in commands:
            reply = monitor_cmd(sock, cmd, 1.0)
            replies.append("%s => %s" % (
                cmd,
                strip_ansi(reply).strip().replace("\n", "\\n")[:160]))
            end = time.time() + 3.0
            while time.time() < end:
                if os.path.exists(ppm_path) and os.path.getsize(ppm_path) > 0:
                    break
                time.sleep(0.1)
            if os.path.exists(ppm_path) and os.path.getsize(ppm_path) > 0:
                break
        if not os.path.exists(ppm_path) or os.path.getsize(ppm_path) == 0:
            raise RuntimeError("screendump missing (%s)" % " | ".join(replies))
        img = Image.open(ppm_path).convert("RGB")
        img.save(path)
        try:
            os.remove(ppm_path)
        except Exception:
            pass
        return True, "monitor screendump %dx%d" % (img.width, img.height)
    except Exception as exc:
        fb_ok, fb_detail = monitor_framebuffer_dump(sock, path, phys_delta)
        if fb_ok:
            return True, "screendump failed: %s; %s" % (exc, fb_detail)
        return False, "screendump failed: %s; %s" % (exc, fb_detail)


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
    ):
        va, map_path = resolve_map_symbol(symbol)
        if va is None:
            log("xblog_auto_dump skipped symbol=%s reason=unresolved" % symbol)
            continue
        phys = va - phys_delta
        if phys <= 0:
            log("xblog_auto_dump skipped symbol=%s va=0x%08x delta=0x%x" % (symbol, va, phys_delta))
            continue
        spec = "0x%08x:0x%x:%s" % (phys, length, name)
        out.append(spec)
        log("xblog_auto_dump symbol=%s va=0x%08x phys=0x%08x len=0x%x map=%s" %
            (symbol, va, phys, length, map_path))
        if symbol in ("_g_SPXBLogMirror", "_g_SPXBLogLastLine"):
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
    parser.add_argument("--hdd", default=r"C:\Games\Emulators\Xemu\HDD\xbox_hdd.qcow2")
    parser.add_argument("--xemu-exe", default=XEMU_JA,
                        help="Xemu executable to launch. Defaults to the JA-isolated copy.")
    parser.add_argument("--config-path", default="",
                        help="Optional xemu.toml path to pass with -config_path and update for this run.")
    parser.add_argument("--explicit-xbox-machine", action="store_true",
                        help="Launch with explicit OG Xbox machine/BIOS/HDD/DVD args instead of relying on xemu.toml.")
    parser.add_argument("--shared-snapshot-hdd", action="store_true",
                        help="With --explicit-xbox-machine, open the HDD without an exclusive lock and write to a temporary snapshot.")
    parser.add_argument("--visible", action="store_true")
    parser.add_argument("--headless", action="store_true",
                        help="Run without a display window. Implies --display none and skips screenshots.")
    parser.add_argument("--no-screenshots", action="store_true",
                        help="Skip interval screenshots without changing the display backend.")
    parser.add_argument("--monitor-screenshots", action="store_true",
                        help="Capture screenshots from guest framebuffer memory even with headless displays.")
    parser.add_argument("--xemu-native-screenshots", action="store_true",
                        help="Capture screenshots through XEMU's built-in F12 screenshot action.")
    parser.add_argument("--xemu-screenshot-dir", default="",
                        help="Directory for XEMU native screenshots. Defaults to <run>_xemu_screenshots.")
    parser.add_argument("--screenshot-start-delay", type=float, default=0.0,
                        help="Delay the first interval screenshot by this many seconds.")
    parser.add_argument("--no-window-fallback", action="store_true",
                        help="Do not fall back to desktop/window screenshots when guest framebuffer capture fails.")
    parser.add_argument("--display", choices=["xemu", "sdl", "none", "egl-headless", "nographic"],
                        default="xemu",
                        help="Xemu/QEMU display backend. Use none or egl-headless for unattended runs.")
    parser.add_argument("--no-monitor", action="store_true")
    parser.add_argument("--qmp-port", type=int, default=0,
                        help="QMP port for direct Xemu screenshots. 0 auto-selects monitor port + 1000 when monitor screenshots are enabled.")
    parser.add_argument("--keys", default="",
                        help="Ignored. Use --monitor-keys so input goes through Xemu's monitor.")
    parser.add_argument("--monitor-keys", default="")
    parser.add_argument("--clicks", default="",
                        help="Ignored. Desktop/window clicking is disabled for Xemu smoke tests.")
    parser.add_argument("--video-debug", action="store_true",
                        help="Ignored. Do not automate Xemu UI clicks from this harness.")
    parser.add_argument("--video-debug-clicks", default="128x34,128x88",
                        help="Ignored; retained for compatibility with older command lines.")
    parser.add_argument("--smoke-keymap", action="store_true")
    parser.add_argument("--xemu-arg", action="append", default=[])
    parser.add_argument("--keep-net", action="store_true")
    parser.add_argument("--dump-mem", action="append", default=[],
                        help="Dump guest memory before closing monitor: addr:length[:name]")
    parser.add_argument("--dump-phys", action="append", default=[],
                        help="Dump guest physical memory before closing monitor: addr:length[:name]")
    parser.add_argument("--skip-final-dumps", action="store_true",
                        help="Skip final register and memory dumps so timing-only smokes can exit quickly.")
    parser.add_argument("--watch-cr2", default="",
                        help="Poll registers and dump memory when CR2 matches this value")
    parser.add_argument("--poll-xblog", action="store_true",
                        help="Poll SP/MP XBLog counters through the monitor during the run.")
    parser.add_argument("--poll-xblog-lite", action="store_true",
                        help="Poll only the compact SP/MP XBLog heartbeat words. Avoids expensive per-symbol telemetry reads.")
    parser.add_argument("--poll-xblog-kind", choices=["auto", "sp", "mp"], default="auto",
                        help="Prefer SP or MP XBLog symbols when polling. Auto preserves the historical probe order.")
    parser.add_argument("--poll-xblog-addr", default="",
                        help="Address of g_*XBBootPhase. Empty means resolve _g_SPXBBootPhase from the current map.")
    parser.add_argument("--poll-xblog-phys-delta", default="0x284000",
                        help="Auto-resolved XBLog VA minus physical monitor address. Use 0 to poll virtual x/ memory.")
    parser.add_argument("--sample-registers", action="store_true",
                        help="Log guest EIP/HLT samples from Xemu's monitor during the run.")
    parser.add_argument("--fail-on-frozen", action="store_true",
                        help="Return failure when register samples stay halted at one EIP and XBLog never advances.")
    parser.add_argument("--leave-running-on-frozen", action="store_true",
                        help="If --fail-on-frozen detects a frozen guest, leave XEMU running for inspection.")
    args = parser.parse_args()

    os.makedirs(OUT_DIR, exist_ok=True)
    ensure_xemu_copy()
    xemu_exe = os.path.abspath(args.xemu_exe)
    args.iso = os.path.abspath(args.iso)
    args.hdd = os.path.abspath(args.hdd)
    if not os.path.exists(args.iso):
        raise FileNotFoundError("Xemu ISO not found: %s" % args.iso)
    if not os.path.exists(args.hdd):
        raise FileNotFoundError("Xemu HDD image not found: %s" % args.hdd)

    stamp = time.strftime("%Y%m%d_%H%M%S")
    prefix = os.path.join(OUT_DIR, "%s_%s" % (args.name, stamp))
    raw_log = prefix + ".xemu.txt"
    report = prefix + ".report.txt"
    toml_backup = None
    config_path = os.path.abspath(args.config_path) if args.config_path else XEMU_TOML
    xemu_screenshot_dir = os.path.abspath(args.xemu_screenshot_dir) if args.xemu_screenshot_dir else os.path.abspath(prefix + "_xemu_screenshots")

    if os.path.exists(config_path):
        with open(config_path, "r", encoding="utf-8", errors="replace") as f:
            toml_backup = f.read()
        toml = re.sub(r"hdd_path = '.*'", lambda _m: "hdd_path = '%s'" % args.hdd, toml_backup)
        toml = re.sub(r"dvd_path = '.*'", lambda _m: "dvd_path = '%s'" % args.iso, toml)
        if args.xemu_native_screenshots:
            os.makedirs(xemu_screenshot_dir, exist_ok=True)
            toml = toml_set_single_quoted(toml, "screenshot_dir", xemu_screenshot_dir)
        if not args.keep_net:
            toml = re.sub(r"(?m)^enable = true$", "enable = false", toml, count=1)
        if args.smoke_keymap:
            toml = re.sub(r"(?ms)\n?\[input\.keyboard_controller_scancode_map\]\n.*?(?=\n\[|\Z)", "", toml)
            toml = toml.rstrip() + "\n\n" + SMOKE_KEYBOARD_MAP + "\n"
        with open(config_path, "w", encoding="utf-8", errors="replace") as f:
            f.write(toml)

    if args.explicit_xbox_machine:
        hdd_drive = "index=0,media=disk,file=%s,locked=on" % args.hdd
        if args.shared_snapshot_hdd:
            hdd_drive = "index=0,media=disk,file=%s,locked=off,snapshot=on" % args.hdd
        argv = [
            xemu_exe,
            "-machine", r"xbox,bootrom=C:\Games\Emulators\Xemu\MCPX\mcpx_1.0.bin,short-animation=on,kernel-irqchip=off,avpack=hdtv",
            "-device", r"smbus-storage,file=C:\Games\Emulators\Xemu\EEPROM\eeprom.bin",
            "-bios", r"C:\Games\Emulators\Xemu\BIOS\xbox-4627_debug.bin",
            "-m", "64",
            "-drive", hdd_drive,
            "-drive", "index=1,media=cdrom,file=%s" % args.iso,
        ]
    else:
        argv = [
            xemu_exe,
        ]
        if args.config_path:
            argv += ["-config_path", config_path]
        argv += ["-dvd_path", args.iso]
    if not args.no_monitor:
        argv += ["-monitor", "tcp:127.0.0.1:%d,server,nowait" % args.port]
    qmp_port = args.qmp_port
    if qmp_port == 0 and args.monitor_screenshots and not args.no_screenshots:
        qmp_port = args.port + 1000
    if qmp_port > 0:
        argv += ["-qmp", "tcp:127.0.0.1:%d,server,nowait" % qmp_port]
    if not args.visible:
        # Keep the xemu display backend, but start it minimized-ish via QEMU's
        # normal window. We still capture frames through the monitor.
        pass
    display_mode = "none" if args.headless else args.display
    freeze_detection_reliable = display_mode not in ("none", "egl-headless", "nographic")
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
    if args.fail_on_frozen and not freeze_detection_reliable:
        log("freeze_detector=disabled display=%s reason=headless-hlt-sampling-unreliable" % display_mode)
    logf = open(raw_log, "w", encoding="utf-8", errors="replace")
    startupinfo = None
    if os.name == "nt" and not args.visible:
        startupinfo = subprocess.STARTUPINFO()
        startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startupinfo.wShowWindow = 4  # SW_SHOWNOACTIVATE
    proc = subprocess.Popen(argv, cwd=os.path.dirname(xemu_exe) or XEMU_DIR,
                            stdout=logf, stderr=subprocess.STDOUT,
                            startupinfo=startupinfo)
    log("pid=%d" % proc.pid)
    if args.keys:
        log("keys_ignored=desktop_input_disabled use=--monitor-keys")
    monitor_key_events = parse_key_schedule(args.monitor_keys)
    if args.clicks:
        log("clicks_ignored=desktop_input_disabled")
    if args.video_debug:
        log("video_debug_ignored=desktop_ui_automation_disabled")
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
    if monitor_key_events and not args.poll_xblog_lite:
        next_xblog_poll = max(event[0] for event in monitor_key_events) + 1.0
    last_xblog_write_count = None
    xblog_progressed = False
    register_samples = []
    next_register_poll = 0.0
    xblog_offsets = resolve_symbol_offsets("_g_SPXBBootPhase", [
        "_g_SPXBPhaseLast",
        "_g_SPXBComSubphase",
        "_g_SPXBCbufExecCount",
        "_g_SPXBCmdExecCount",
        "_g_SPXBCmdPhase",
        "_g_SPXBCmdHash",
        "_g_SPXBCmdArgc",
        "_g_SPXBCmdLoopIndex",
        "_g_SPXBCmdLoopNameHash",
        "_g_SPXBCmdFunctionPtr",
        "_g_SPXBCmdArgv0First4",
        "_g_SPXBCmdNameFirst4",
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
        "_g_SPXBSurfaceTypeCounts",
        "_g_SPXBEntityTypeCounts",
        "_g_SPXBMapPhase",
        "_g_SPXBDirectMapState",
        "_g_SPXBDirectMapLastError",
        "_g_SPXBDirectMapPathIndex",
        "_g_SPXBDirectMapFirst4",
        "_g_SPXBSvMapState",
        "_g_SPXBClHunkState",
        "_g_SPXBComErrorCode",
        "_g_SPXBComErrorHash",
        "_g_SPXBComErrorFirst4",
        "_g_SPXBComErrorNext4",
        "_g_SPXBClHunkCaller",
        "_g_SPXBClHunkCallCount",
        "_g_SPXBCmLoadState",
        "_g_SPXBCmLoadLumpHash",
        "_g_SPXBCmLoadLumpLen",
        "_g_SPXBGamePhase",
        "_g_SPXBGentitiesPtr",
        "_g_SPXBClientsPtr",
        "_g_SPXBGentitySize",
        "_g_SPXBClientFieldBefore",
        "_g_SPXBClientFieldAfter",
        "_g_SPXBFramebufferData",
        "_g_SPXBFramebufferPitch",
        "_g_SPXBFramebufferWidth",
        "_g_SPXBFramebufferHeight",
        "_g_SPXBFramebufferFormat",
        "_g_SPXBFramebufferSize",
        "_g_SPXBKeyCatchers",
        "_g_SPXBKeyLastKey",
        "_g_SPXBKeyLastDown",
        "_g_SPXBKeyLastPhaseHash",
        "_g_SPXBKeyTraceCount",
        "_g_SPXBSmokeButtonCount",
        "_g_SPXBSmokeButtonPressCount",
        "_g_SPXBSmokeButtonReleaseCount",
        "_g_SPXBSmokeButtonUiStartMs",
        "_g_SPXBSmokeButtonLast",
        "_g_SPXBSoakCommandCount",
        "_g_SPXBSoakCommandExecuted",
        "_g_SPXBSoakCommandLastHash",
        "_g_SPXBSoakCommandLastAtMs",
        "_g_SPXBSoakCommandLastElapsed",
        "_g_SPXBSoakTraceCount",
        "_g_SPXBSoakTraceScopeHash",
        "_g_SPXBSoakTraceEventHash",
        "_g_SPXBSoakTraceNameHash",
        "_g_SPXBSoakTraceLastFreePhys",
        "_g_SPXBUISetActiveCount",
        "_g_SPXBUIActiveMenuHash",
        "_g_SPXBUIActiveResult",
        "_g_SPXBUIMainMenuCount",
        "_g_SPXBUIKeyEventCount",
        "_g_SPXBUIKeyLast",
        "_g_SPXBFrontEndPhase",
        "_g_SPXBFrontEndMenuHash",
        "_g_SPXBFrontEndItemHash",
        "_g_SPXBFrontEndScriptHash",
        "_g_SPXBFrontEndPopup",
        "_g_SPXBFrontEndResponse",
        "_g_SPXBFrontEndController",
        "_g_SPXBCinPhase",
        "_g_SPXBCinHandle",
        "_g_SPXBCinStatus",
        "_g_SPXBCinLoopCount",
        "_g_SPXBBinkPhase",
        "_g_SPXBBinkRunCount",
        "_g_SPXBBinkWaitLoops",
        "_g_SPXBBinkWaitBreaks",
        "_g_SPXBBinkFrameNum",
        "_g_SPXBBinkFrames",
        "_g_SPXBBinkOpenFlags",
        "_g_SPXBBinkWidth",
        "_g_SPXBBinkHeight",
        "_g_SPXBBinkAlpha",
        "_g_SPXBBinkCopySkipped",
        "_g_SPXBBinkStartResult",
        "_g_SPXBBinkStatus",
        "_g_SPXBBinkAllocSeq",
        "_g_SPXBBinkLastAllocSize",
        "_g_SPXBBinkLastAllocPtr",
        "_g_SPXBBinkFreeSeq",
        "_g_SPXBBinkLastFreePtr",
        "_g_SPXBBinkLastAvailPhys",
        "_g_SPXBBinkLastZoneAlloc",
        "_g_SPXBBinkLastZoneFree",
        "_g_SPXBBinkLastTempPool",
        "_g_SPXBBinkMemCode",
        "_g_SPXBBinkOutstandingCount",
        "_g_SPXBBinkOutstandingBytes",
        "_g_SPXBBinkPeakOutstandingBytes",
        "_g_SPXBRenderDrawSurfCount",
        "_g_SPXBRenderDrawSurfDelta",
        "_g_SPXBRenderLeafCount",
        "_g_SPXBRenderCullPatch",
        "_g_SPXBRenderCullMd3",
        "_g_SPXBRenderCullBox",
        "_g_SPXBRenderDlightSurfaces",
        "_g_SPXBRenderDlightCulled",
        "_g_SPXBSkyIterCalls",
        "_g_SPXBSkyPortalMainFallbacks",
        "_g_SPXBSkyClipCalls",
        "_g_SPXBSkyBoxDrawCalls",
        "_g_SPXBSkyBoxSidesDrawn",
        "_g_SPXBSkyNoOuterBox",
        "_g_SPXBSkyCloudBuilds",
        "_g_SPXBSkyGenericCalls",
        "_g_SPXBWorldSurfaceAddCalls",
        "_g_SPXBWorldSkySurfaceAdds",
        "_g_SPXBWorldPortalSurfaceAdds",
        "_g_SPXBDrawSurfTotalAdds",
        "_g_SPXBDrawSurfSkyAdds",
        "_g_SPXBDrawSurfPortalAdds",
        "_g_SPXBDrawSurfForceSightSkips",
        "_g_SPXBCGameRenderCalls",
        "_g_SPXBCGameDrawFrameReturns",
        "_g_SPXBLoadingInfoFrames",
        "_g_SPXBLoadingSnapshotsProcessed",
        "_g_SPXBLoadingStateInfoHandoffs",
        "_g_SPXBLoadingTransitionCommands",
        "_g_SPXBLoadingTransitionScreenUpdates",
        "_g_SPXBLoadingLastClientState",
        "_g_SPXBLoadingLastServerTime",
        "_g_SPXBRenderSceneCalls",
        "_g_SPXBRenderSceneNoWorld",
        "_g_SPXBRenderViewCalls",
        "_g_SPXBRenderViewWorldCalls",
        "_g_SPXBScreenDrawCalls",
        "_g_SPXBScreenForceDirectCalls",
        "_g_SPXBScreenFullscreenSkips",
        "_g_SPXBScreenCinematicDraws",
        "_g_SPXBScreenCGameCalls",
        "_g_SPXBScreenUIRefreshes",
        "_g_SPXBScreenDirectReturns",
        "_g_SPXBCLFrameEnterCalls",
        "_g_SPXBCLFrameDirectReturns",
        "_g_SPXBCLFrameBeforeScreen",
        "_g_SPXBCLFrameAfterScreen",
        "_g_SPXBCLFrameCompleted",
        "_g_SPXBRenderRegistrationState",
        "_g_SPXBRenderStretchPicCalls",
        "_g_SPXBRenderStretchPicCmdNull",
        "_g_SPXBRenderBeginFrameCalls",
        "_g_SPXBRenderBeginFrameUnregistered",
        "_g_SPXBRenderEndFrameCalls",
        "_g_SPXBRenderEndFrameUnregistered",
        "_g_SPXBRenderEndFrameCmdNull",
        "_g_SPXBRenderEndFrameBeginFail",
        "_g_SPXBRenderIssueCalls",
        "_g_SPXBRenderIssueCmdUsed",
        "_g_SPXBCompatBeginFrameCalls",
        "_g_SPXBCompatEndFrameCalls",
        "_g_SPXBFakeSwapBuffersCalls",
        "_g_SPXBDx8BeginFrameCalls",
        "_g_SPXBDx8EndFrameCalls",
        "_g_SPXBDx8PresentCalls",
        "_g_SPXBDx8PresentHr",
        "_g_SPXBDx8FramebufferUpdates",
        "_g_SPXBDx8FramebufferBackBufferFail",
    ])
    xblog_direct_counter_symbols = [
        "_g_SPXBComFrameCount",
        "_g_SPXBClFrameCount",
        "_g_SPXBClsFrameCount",
        "_g_SPXBHeartbeatCount",
        "_g_SPXBCmdLast",
        "_g_SPXBCmdTextLast",
        "_g_SPXBCmdFunctionNameLast",
        "_g_SPXBCinPhase",
        "_g_SPXBCinHandle",
        "_g_SPXBCinStatus",
        "_g_SPXBCinLoopCount",
        "_g_SPXBCinArgLast",
        "_g_SPXBBinkPhase",
        "_g_SPXBBinkRunCount",
        "_g_SPXBBinkWaitLoops",
        "_g_SPXBBinkWaitBreaks",
        "_g_SPXBBinkFrameNum",
        "_g_SPXBBinkFrames",
        "_g_SPXBBinkOpenFlags",
        "_g_SPXBBinkWidth",
        "_g_SPXBBinkHeight",
        "_g_SPXBBinkAlpha",
        "_g_SPXBBinkCopySkipped",
        "_g_SPXBBinkStartResult",
        "_g_SPXBBinkStatus",
        "_g_SPXBBinkAllocSeq",
        "_g_SPXBBinkLastAllocSize",
        "_g_SPXBBinkLastAllocPtr",
        "_g_SPXBBinkFreeSeq",
        "_g_SPXBBinkLastFreePtr",
        "_g_SPXBBinkLastAvailPhys",
        "_g_SPXBBinkLastZoneAlloc",
        "_g_SPXBBinkLastZoneFree",
        "_g_SPXBBinkLastTempPool",
        "_g_SPXBBinkMemCode",
        "_g_SPXBBinkOutstandingCount",
        "_g_SPXBBinkOutstandingBytes",
        "_g_SPXBBinkPeakOutstandingBytes",
        "_g_SPXBSkyIterCalls",
        "_g_SPXBSkyPortalMainFallbacks",
        "_g_SPXBSkyClipCalls",
        "_g_SPXBSkyBoxDrawCalls",
        "_g_SPXBSkyBoxSidesDrawn",
        "_g_SPXBSkyNoOuterBox",
        "_g_SPXBSkyCloudBuilds",
        "_g_SPXBSkyGenericCalls",
        "_g_SPXBWorldSurfaceAddCalls",
        "_g_SPXBWorldSkySurfaceAdds",
        "_g_SPXBWorldPortalSurfaceAdds",
        "_g_SPXBDrawSurfTotalAdds",
        "_g_SPXBDrawSurfSkyAdds",
        "_g_SPXBDrawSurfPortalAdds",
        "_g_SPXBDrawSurfForceSightSkips",
        "_g_SPXBCGameRenderCalls",
        "_g_SPXBCGameDrawFrameReturns",
        "_g_SPXBLoadingInfoFrames",
        "_g_SPXBLoadingSnapshotsProcessed",
        "_g_SPXBLoadingStateInfoHandoffs",
        "_g_SPXBLoadingTransitionCommands",
        "_g_SPXBLoadingTransitionScreenUpdates",
        "_g_SPXBLoadingLastClientState",
        "_g_SPXBLoadingLastServerTime",
        "_g_SPXBRenderSceneCalls",
        "_g_SPXBRenderSceneNoWorld",
        "_g_SPXBRenderViewCalls",
        "_g_SPXBRenderViewWorldCalls",
        "_g_SPXBScreenDrawCalls",
        "_g_SPXBScreenForceDirectCalls",
        "_g_SPXBScreenFullscreenSkips",
        "_g_SPXBScreenCinematicDraws",
        "_g_SPXBScreenCGameCalls",
        "_g_SPXBScreenUIRefreshes",
        "_g_SPXBScreenDirectReturns",
        "_g_SPXBCLFrameEnterCalls",
        "_g_SPXBCLFrameDirectReturns",
        "_g_SPXBCLFrameBeforeScreen",
        "_g_SPXBCLFrameAfterScreen",
        "_g_SPXBCLFrameCompleted",
        "_g_SPXBRenderRegistrationState",
        "_g_SPXBRenderStretchPicCalls",
        "_g_SPXBRenderStretchPicCmdNull",
        "_g_SPXBRenderBeginFrameCalls",
        "_g_SPXBRenderBeginFrameUnregistered",
        "_g_SPXBRenderEndFrameCalls",
        "_g_SPXBRenderEndFrameUnregistered",
        "_g_SPXBRenderEndFrameCmdNull",
        "_g_SPXBRenderEndFrameBeginFail",
        "_g_SPXBRenderIssueCalls",
        "_g_SPXBRenderIssueCmdUsed",
        "_g_SPXBCompatBeginFrameCalls",
        "_g_SPXBCompatEndFrameCalls",
        "_g_SPXBFakeSwapBuffersCalls",
        "_g_SPXBDx8BeginFrameCalls",
        "_g_SPXBDx8EndFrameCalls",
        "_g_SPXBDx8PresentCalls",
        "_g_SPXBDx8PresentHr",
        "_g_SPXBDx8FramebufferUpdates",
        "_g_SPXBDx8FramebufferBackBufferFail",
    ]
    xblog_direct_counter_addrs = {}
    for symbol in xblog_direct_counter_symbols:
        addr, _map_path = resolve_map_symbol(symbol)
        if addr is not None:
            xblog_direct_counter_addrs[symbol] = addr
    capture_enabled = (not args.no_screenshots) and (
        args.xemu_native_screenshots or
        args.monitor_screenshots or
        display_mode not in ("none", "egl-headless", "nographic"))

    sock = None
    qmp = None
    frozen_guest = False
    last_xblog_progress_elapsed = None
    loop_elapsed = 0.0
    try:
        if qmp_port > 0:
            try:
                qmp = qmp_connect(qmp_port)
                log("qmp=ready port=%d" % qmp_port)
            except Exception as exc:
                log("qmp=unavailable port=%d err=%s" % (qmp_port, exc))
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
        next_shot = max(0.0, float(args.screenshot_start_delay))
        shot = 0
        process_exited = False
        while time.time() - start < args.duration:
            elapsed = time.time() - start
            loop_elapsed = elapsed
            rc = proc.poll()
            if rc is not None:
                log("process_exit t=%.1f rc=%s" % (elapsed, rc))
                process_exited = True
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

            if sock is not None and args.sample_registers and elapsed >= next_register_poll:
                next_register_poll = elapsed + max(1.0, float(args.interval))
                try:
                    regs = monitor_cmd(sock, "info registers", 0.08)
                    sample = parse_register_snapshot(regs)
                    if sample:
                        sample["t"] = elapsed
                        register_samples.append(sample)
                        log("regs t=%.1f eip=0x%08x hlt=%s cr2=0x%08x" %
                            (elapsed,
                             sample.get("eip", 0),
                             sample.get("hlt", "?"),
                             sample.get("cr2", 0)))
                except OSError as exc:
                    log("regs t=%.1f unavailable=%s" % (elapsed, exc))

            for event in monitor_key_events:
                if not event[3] and elapsed >= event[0]:
                    if sock is None:
                        ok = False
                    else:
                        monitor_cmd(sock, "sendkey %s" % event[1], max(0.05, event[2]))
                        ok = True
                    event[3] = True
                    log("monitor_key t=%.1f key=%s hold=%.2f ok=%s" % (elapsed, event[1], event[2], ok))

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
                            if delta > 0:
                                xblog_progressed = True
                                last_xblog_progress_elapsed = elapsed
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

                    poll_words = max(64, max(xblog_offsets.values() or [0]) + 24)
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
                        if delta > 0:
                            xblog_progressed = True
                            last_xblog_progress_elapsed = elapsed
                        last_xblog_write_count = write_count
                        def lite_word_for(symbol):
                            idx = xblog_offsets.get(symbol)
                            if idx is None or idx >= len(words):
                                return 0
                            return words[idx]
                        if args.poll_xblog_lite:
                            log("xblog_lite t=%.1f boot=0x%08x mirror=%u writes=%u delta=%d hb=0x%08x count=%u frame=%u rt=%u st=%u fps=%.1f phase=0x%08x load=info:%u snap:%u handoff:%u trans:%u screen:%u state:%u st:%u ui_menu=0x%08x ui_result=0x%08x trace=count:%u scope:0x%08x event:0x%08x name:0x%08x free:%u" %
                                (elapsed, boot_phase, mirror_pos, write_count, delta,
                                 heartbeat_magic, heartbeat_count, heartbeat_frame,
                                 heartbeat_rt, heartbeat_st, heartbeat_fps10 / 10.0,
                                 lite_word_for("_g_SPXBPhaseLast"),
                                 lite_word_for("_g_SPXBLoadingInfoFrames"),
                                 lite_word_for("_g_SPXBLoadingSnapshotsProcessed"),
                                 lite_word_for("_g_SPXBLoadingStateInfoHandoffs"),
                                 lite_word_for("_g_SPXBLoadingTransitionCommands"),
                                 lite_word_for("_g_SPXBLoadingTransitionScreenUpdates"),
                                 lite_word_for("_g_SPXBLoadingLastClientState"),
                                 lite_word_for("_g_SPXBLoadingLastServerTime"),
                                 lite_word_for("_g_SPXBUIActiveMenuHash"),
                                 lite_word_for("_g_SPXBUIActiveResult"),
                                 lite_word_for("_g_SPXBSoakTraceCount"),
                                 lite_word_for("_g_SPXBSoakTraceScopeHash"),
                                 lite_word_for("_g_SPXBSoakTraceEventHash"),
                                 lite_word_for("_g_SPXBSoakTraceNameHash"),
                                 lite_word_for("_g_SPXBSoakTraceLastFreePhys")))
                            continue
                        def word_for(symbol):
                            idx = xblog_offsets.get(symbol)
                            if idx is None or idx >= len(words):
                                return 0
                            return words[idx]
                        xblog_phys_delta_current = None
                        if xblog_va_for_probe is not None and xblog_addr is not None:
                            xblog_phys_delta_current = xblog_va_for_probe - xblog_addr
                        def direct_word_for(symbol):
                            addr = xblog_direct_counter_addrs.get(symbol)
                            if addr is None or xblog_phys_delta_current is None:
                                return word_for(symbol)
                            value = monitor_read_u32(sock, addr, xblog_phys_delta_current)
                            return value if value is not None else 0
                        def direct_word_alt_for(symbol, adjust):
                            addr = xblog_direct_counter_addrs.get(symbol)
                            if addr is None or xblog_phys_delta_current is None:
                                return 0
                            words_alt = monitor_read_phys_words(sock, addr - xblog_phys_delta_current + adjust, 1)
                            return words_alt[0] if words_alt else 0
                        def direct_string_for(symbol, byte_count):
                            addr = xblog_direct_counter_addrs.get(symbol)
                            if addr is None:
                                return ""
                            return monitor_read_string(sock, addr, byte_count, xblog_phys_delta_current)
                        def array_words_for(symbol, count):
                            idx = xblog_offsets.get(symbol)
                            if idx is None or idx + count > len(words):
                                return []
                            return words[idx:idx + count]
                        phase_last = word_for("_g_SPXBPhaseLast")
                        com_subphase = word_for("_g_SPXBComSubphase")
                        cbuf_exec_count = word_for("_g_SPXBCbufExecCount")
                        cmd_exec_count = word_for("_g_SPXBCmdExecCount")
                        cmd_phase = word_for("_g_SPXBCmdPhase")
                        cmd_hash = word_for("_g_SPXBCmdHash")
                        cmd_argc = word_for("_g_SPXBCmdArgc")
                        cmd_loop_index = word_for("_g_SPXBCmdLoopIndex")
                        cmd_loop_hash = word_for("_g_SPXBCmdLoopNameHash")
                        cmd_function = word_for("_g_SPXBCmdFunctionPtr")
                        cmd_argv0 = word_for("_g_SPXBCmdArgv0First4")
                        cmd_name = word_for("_g_SPXBCmdNameFirst4")
                        cmd_last_text = direct_string_for("_g_SPXBCmdLast", 128)
                        cmd_full_text = direct_string_for("_g_SPXBCmdTextLast", 128)
                        cmd_func_name = direct_string_for("_g_SPXBCmdFunctionNameLast", 64)
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
                        surf_types = array_words_for("_g_SPXBSurfaceTypeCounts", 16)
                        ent_types = array_words_for("_g_SPXBEntityTypeCounts", 16)
                        surf_type_summary = ",".join("%d:%u" % (i, v) for i, v in enumerate(surf_types) if v)
                        ent_type_summary = ",".join("%d:%u" % (i, v) for i, v in enumerate(ent_types) if v)
                        map_phase = word_for("_g_SPXBMapPhase")
                        direct_map_state = word_for("_g_SPXBDirectMapState")
                        direct_map_error = word_for("_g_SPXBDirectMapLastError")
                        direct_map_path = word_for("_g_SPXBDirectMapPathIndex")
                        direct_map_first4 = word_for("_g_SPXBDirectMapFirst4")
                        sv_map_state = word_for("_g_SPXBSvMapState")
                        cl_hunk_state = word_for("_g_SPXBClHunkState")
                        com_error_code = word_for("_g_SPXBComErrorCode")
                        com_error_hash = word_for("_g_SPXBComErrorHash")
                        com_error_first4 = word_for("_g_SPXBComErrorFirst4")
                        com_error_next4 = word_for("_g_SPXBComErrorNext4")
                        cl_hunk_caller = word_for("_g_SPXBClHunkCaller")
                        cl_hunk_call_count = word_for("_g_SPXBClHunkCallCount")
                        cm_load_state = word_for("_g_SPXBCmLoadState")
                        cm_load_lump_hash = word_for("_g_SPXBCmLoadLumpHash")
                        cm_load_lump_len = word_for("_g_SPXBCmLoadLumpLen")
                        game_phase = word_for("_g_SPXBGamePhase")
                        gentities_ptr = word_for("_g_SPXBGentitiesPtr")
                        clients_ptr = word_for("_g_SPXBClientsPtr")
                        gentity_size = word_for("_g_SPXBGentitySize")
                        client_field_before = word_for("_g_SPXBClientFieldBefore")
                        client_field_after = word_for("_g_SPXBClientFieldAfter")
                        fb_data = word_for("_g_SPXBFramebufferData")
                        fb_pitch = word_for("_g_SPXBFramebufferPitch")
                        fb_width = word_for("_g_SPXBFramebufferWidth")
                        fb_height = word_for("_g_SPXBFramebufferHeight")
                        fb_format = word_for("_g_SPXBFramebufferFormat")
                        fb_size = word_for("_g_SPXBFramebufferSize")
                        fb_nz = 0
                        fb_rgb_nz = 0
                        fb_xor = 0
                        fb_first = 0
                        if fb_data and fb_pitch and fb_width and fb_height and fb_width <= 1920 and fb_height <= 1080:
                            fb_phys = (fb_data & 0x03ffffff)
                            sample_points = (
                                (fb_width // 4, fb_height // 4),
                                (fb_width // 2, fb_height // 4),
                                ((fb_width * 3) // 4, fb_height // 4),
                                (fb_width // 4, fb_height // 2),
                                (fb_width // 2, fb_height // 2),
                                ((fb_width * 3) // 4, fb_height // 2),
                                (fb_width // 4, (fb_height * 3) // 4),
                                (fb_width // 2, (fb_height * 3) // 4),
                                ((fb_width * 3) // 4, (fb_height * 3) // 4),
                            )
                            for sample_x, sample_y in sample_points:
                                sample_phys = fb_phys + sample_y * fb_pitch + sample_x * 4
                                try:
                                    fb_words = monitor_read_phys_words(sock, sample_phys, 4)
                                    for fb_word in fb_words:
                                        if fb_first == 0:
                                            fb_first = fb_word
                                        if fb_word:
                                            fb_nz += 1
                                        if (fb_word & 0x00ffffff):
                                            fb_rgb_nz += 1
                                        fb_xor ^= fb_word
                                except OSError:
                                    pass
                        key_catchers = word_for("_g_SPXBKeyCatchers")
                        key_last = word_for("_g_SPXBKeyLastKey")
                        key_down = word_for("_g_SPXBKeyLastDown")
                        key_phase = word_for("_g_SPXBKeyLastPhaseHash")
                        key_count = word_for("_g_SPXBKeyTraceCount")
                        smoke_count = word_for("_g_SPXBSmokeButtonCount")
                        smoke_press = word_for("_g_SPXBSmokeButtonPressCount")
                        smoke_release = word_for("_g_SPXBSmokeButtonReleaseCount")
                        smoke_ui = word_for("_g_SPXBSmokeButtonUiStartMs")
                        smoke_last = word_for("_g_SPXBSmokeButtonLast")
                        soak_count = word_for("_g_SPXBSoakCommandCount")
                        soak_exec = word_for("_g_SPXBSoakCommandExecuted")
                        soak_hash = word_for("_g_SPXBSoakCommandLastHash")
                        soak_at = word_for("_g_SPXBSoakCommandLastAtMs")
                        soak_elapsed = word_for("_g_SPXBSoakCommandLastElapsed")
                        soak_trace_count = word_for("_g_SPXBSoakTraceCount")
                        soak_trace_scope = word_for("_g_SPXBSoakTraceScopeHash")
                        soak_trace_event = word_for("_g_SPXBSoakTraceEventHash")
                        soak_trace_name = word_for("_g_SPXBSoakTraceNameHash")
                        soak_trace_free = word_for("_g_SPXBSoakTraceLastFreePhys")
                        ui_set = word_for("_g_SPXBUISetActiveCount")
                        ui_menu = word_for("_g_SPXBUIActiveMenuHash")
                        ui_result = word_for("_g_SPXBUIActiveResult")
                        ui_main = word_for("_g_SPXBUIMainMenuCount")
                        ui_key_count = word_for("_g_SPXBUIKeyEventCount")
                        ui_key_last = word_for("_g_SPXBUIKeyLast")
                        fe_phase = word_for("_g_SPXBFrontEndPhase")
                        fe_menu = word_for("_g_SPXBFrontEndMenuHash")
                        fe_item = word_for("_g_SPXBFrontEndItemHash")
                        fe_script = word_for("_g_SPXBFrontEndScriptHash")
                        fe_popup = word_for("_g_SPXBFrontEndPopup")
                        fe_response = word_for("_g_SPXBFrontEndResponse")
                        fe_controller = word_for("_g_SPXBFrontEndController")
                        cin_phase = direct_word_for("_g_SPXBCinPhase")
                        cin_handle = direct_word_for("_g_SPXBCinHandle")
                        cin_status = direct_word_for("_g_SPXBCinStatus")
                        cin_loop = direct_word_for("_g_SPXBCinLoopCount")
                        cin_arg = direct_string_for("_g_SPXBCinArgLast", 64)
                        bink_phase = direct_word_for("_g_SPXBBinkPhase")
                        bink_run = direct_word_for("_g_SPXBBinkRunCount")
                        bink_wait = direct_word_for("_g_SPXBBinkWaitLoops")
                        bink_wait_breaks = direct_word_for("_g_SPXBBinkWaitBreaks")
                        bink_frame = direct_word_for("_g_SPXBBinkFrameNum")
                        bink_frames = direct_word_for("_g_SPXBBinkFrames")
                        bink_flags = direct_word_for("_g_SPXBBinkOpenFlags")
                        bink_width = direct_word_for("_g_SPXBBinkWidth")
                        bink_height = direct_word_for("_g_SPXBBinkHeight")
                        bink_alpha = direct_word_for("_g_SPXBBinkAlpha")
                        bink_copy = direct_word_for("_g_SPXBBinkCopySkipped")
                        bink_start = direct_word_for("_g_SPXBBinkStartResult")
                        bink_status = direct_word_for("_g_SPXBBinkStatus")
                        bink_alloc_seq = direct_word_for("_g_SPXBBinkAllocSeq")
                        bink_alloc_size = direct_word_for("_g_SPXBBinkLastAllocSize")
                        bink_alloc_ptr = direct_word_for("_g_SPXBBinkLastAllocPtr")
                        bink_free_seq = direct_word_for("_g_SPXBBinkFreeSeq")
                        bink_free_ptr = direct_word_for("_g_SPXBBinkLastFreePtr")
                        bink_avail_phys = direct_word_for("_g_SPXBBinkLastAvailPhys")
                        bink_zone_alloc = direct_word_for("_g_SPXBBinkLastZoneAlloc")
                        bink_zone_free = direct_word_for("_g_SPXBBinkLastZoneFree")
                        bink_temp_pool = direct_word_for("_g_SPXBBinkLastTempPool")
                        bink_mem_code = direct_word_for("_g_SPXBBinkMemCode")
                        bink_outstanding_count = direct_word_for("_g_SPXBBinkOutstandingCount")
                        bink_outstanding_bytes = direct_word_for("_g_SPXBBinkOutstandingBytes")
                        bink_peak_outstanding_bytes = direct_word_for("_g_SPXBBinkPeakOutstandingBytes")
                        drawsurf_count = word_for("_g_SPXBRenderDrawSurfCount")
                        drawsurf_delta = word_for("_g_SPXBRenderDrawSurfDelta")
                        leaf_count = word_for("_g_SPXBRenderLeafCount")
                        cull_patch = word_for("_g_SPXBRenderCullPatch")
                        cull_md3 = word_for("_g_SPXBRenderCullMd3")
                        cull_box = word_for("_g_SPXBRenderCullBox")
                        dlight_surfs = word_for("_g_SPXBRenderDlightSurfaces")
                        dlight_culled = word_for("_g_SPXBRenderDlightCulled")
                        sky_iter = direct_word_for("_g_SPXBSkyIterCalls")
                        sky_portal = direct_word_for("_g_SPXBSkyPortalMainFallbacks")
                        sky_clip = direct_word_for("_g_SPXBSkyClipCalls")
                        sky_box = direct_word_for("_g_SPXBSkyBoxDrawCalls")
                        sky_sides = direct_word_for("_g_SPXBSkyBoxSidesDrawn")
                        sky_no_outer = direct_word_for("_g_SPXBSkyNoOuterBox")
                        sky_clouds = direct_word_for("_g_SPXBSkyCloudBuilds")
                        sky_generic = direct_word_for("_g_SPXBSkyGenericCalls")
                        world_adds = direct_word_for("_g_SPXBWorldSurfaceAddCalls")
                        world_sky = direct_word_for("_g_SPXBWorldSkySurfaceAdds")
                        world_portal = direct_word_for("_g_SPXBWorldPortalSurfaceAdds")
                        draw_total = direct_word_for("_g_SPXBDrawSurfTotalAdds")
                        draw_sky = direct_word_for("_g_SPXBDrawSurfSkyAdds")
                        draw_portal = direct_word_for("_g_SPXBDrawSurfPortalAdds")
                        draw_forcesight = direct_word_for("_g_SPXBDrawSurfForceSightSkips")
                        direct_com_frame = direct_word_for("_g_SPXBComFrameCount")
                        direct_cl_frame = direct_word_for("_g_SPXBClFrameCount")
                        direct_cls_frame = direct_word_for("_g_SPXBClsFrameCount")
                        direct_hb_count = direct_word_for("_g_SPXBHeartbeatCount")
                        cg_render = direct_word_for("_g_SPXBCGameRenderCalls")
                        cg_return = direct_word_for("_g_SPXBCGameDrawFrameReturns")
                        rscene = direct_word_for("_g_SPXBRenderSceneCalls")
                        rscene_noworld = direct_word_for("_g_SPXBRenderSceneNoWorld")
                        rview = direct_word_for("_g_SPXBRenderViewCalls")
                        rview_world = direct_word_for("_g_SPXBRenderViewWorldCalls")
                        screen_draw = direct_word_for("_g_SPXBScreenDrawCalls")
                        screen_force = direct_word_for("_g_SPXBScreenForceDirectCalls")
                        screen_skip = direct_word_for("_g_SPXBScreenFullscreenSkips")
                        screen_cin = direct_word_for("_g_SPXBScreenCinematicDraws")
                        screen_cgame = direct_word_for("_g_SPXBScreenCGameCalls")
                        screen_ui = direct_word_for("_g_SPXBScreenUIRefreshes")
                        screen_ret = direct_word_for("_g_SPXBScreenDirectReturns")
                        clf_enter = direct_word_for("_g_SPXBCLFrameEnterCalls")
                        clf_direct = direct_word_for("_g_SPXBCLFrameDirectReturns")
                        clf_before = direct_word_for("_g_SPXBCLFrameBeforeScreen")
                        clf_after = direct_word_for("_g_SPXBCLFrameAfterScreen")
                        clf_done = direct_word_for("_g_SPXBCLFrameCompleted")
                        render_registered = direct_word_for("_g_SPXBRenderRegistrationState")
                        render_stretch = direct_word_for("_g_SPXBRenderStretchPicCalls")
                        render_stretch_null = direct_word_for("_g_SPXBRenderStretchPicCmdNull")
                        render_begin = direct_word_for("_g_SPXBRenderBeginFrameCalls")
                        render_begin_unregistered = direct_word_for("_g_SPXBRenderBeginFrameUnregistered")
                        render_end = direct_word_for("_g_SPXBRenderEndFrameCalls")
                        render_end_unregistered = direct_word_for("_g_SPXBRenderEndFrameUnregistered")
                        render_end_cmd_null = direct_word_for("_g_SPXBRenderEndFrameCmdNull")
                        render_end_begin_fail = direct_word_for("_g_SPXBRenderEndFrameBeginFail")
                        render_issue = direct_word_for("_g_SPXBRenderIssueCalls")
                        render_issue_cmd_used = direct_word_for("_g_SPXBRenderIssueCmdUsed")
                        compat_begin = direct_word_for("_g_SPXBCompatBeginFrameCalls")
                        compat_end = direct_word_for("_g_SPXBCompatEndFrameCalls")
                        fake_swap = direct_word_for("_g_SPXBFakeSwapBuffersCalls")
                        dx8_begin = direct_word_for("_g_SPXBDx8BeginFrameCalls")
                        dx8_end = direct_word_for("_g_SPXBDx8EndFrameCalls")
                        dx8_present = direct_word_for("_g_SPXBDx8PresentCalls")
                        dx8_present_hr = direct_word_for("_g_SPXBDx8PresentHr")
                        dx8_fb_updates = direct_word_for("_g_SPXBDx8FramebufferUpdates")
                        dx8_fb_fail = direct_word_for("_g_SPXBDx8FramebufferBackBufferFail")
                        alt_clf_enter = direct_word_alt_for("_g_SPXBCLFrameEnterCalls", 0x1000)
                        alt_clf_before = direct_word_alt_for("_g_SPXBCLFrameBeforeScreen", 0x1000)
                        alt_screen_draw = direct_word_alt_for("_g_SPXBScreenDrawCalls", 0x1000)
                        alt_cg_render = direct_word_alt_for("_g_SPXBCGameRenderCalls", 0x1000)
                        alt_rview = direct_word_alt_for("_g_SPXBRenderViewCalls", 0x1000)
                        log("xblog t=%.1f boot=0x%08x mirror=%u writes=%u delta=%d hb=0x%08x count=%u frame=%u rt=%u st=%u fps=%.1f phase=0x%08x com=%u cbuf=%u cmd=%u/%u/hash:0x%08x/argc:%u/idx:%u/lhash:0x%08x/fn:0x%08x/argv0:0x%08x/name:0x%08x map=%u svmap=%u clhunk=%u caller=%u calls=%u err=%u/0x%08x/0x%08x/0x%08x dm=0x%x/err:%u/path:%u/first4:0x%08x game=%u ge=0x%08x gent=0x%08x ent=0x%08x gsz=%u nent=%u be=%u prim=%u verts=%u state=%u split=%u/%u/%u/%u final=%u flush=%u ds=%u/%u leaf=%u cull=patch:%06x md3:%06x box:%06x dl:%u/%u sky=iter:%u portal:%u clip:%u box:%u sides:%u noouter:%u clouds:%u gen:%u world=add:%u sky:%u portal:%u draw=add:%u sky:%u portal:%u fskip:%u dir=com:%u cl:%u cls:%u hb:%u pipe=cg:%u/%u rs:%u now:%u rv:%u world:%u screen=draw:%u force:%u skip:%u cin:%u cgame:%u ui:%u ret:%u clf=enter:%u direct:%u before:%u after:%u done:%u rend=reg:%u sp:%u/%u bf:%u/%u ef:%u/%u/%u/%u issue:%u/%u gl:%u/%u sw:%u dx8=%u/%u/%u/hr:0x%08x/fb:%u/%u alt=clf:%u before:%u screen:%u cg:%u rv:%u stype=%s etype=%s fb=data:0x%08x pitch:%u wh:%ux%u fmt:0x%08x size:0x%08x nz:%u rgbnz:%u xor:0x%08x first:0x%08x key=catch:0x%x last:%u down:%u ph:0x%x n:%u smoke=count:%u press:%u rel:%u ui:%u last:0x%x ui=set:%u menu:0x%x result:%u main:%u keyn:%u keylast:0x%x fe=phase:0x%08x menu:0x%08x item:0x%08x script:0x%08x popup:%u resp:%u ctrl:0x%08x" %
                            (elapsed, boot_phase, mirror_pos, write_count, delta,
                             heartbeat_magic, heartbeat_count, heartbeat_frame,
                             heartbeat_rt, heartbeat_st, heartbeat_fps10 / 10.0,
                             phase_last, com_subphase,
                             cbuf_exec_count, cmd_exec_count, cmd_phase,
                             cmd_hash, cmd_argc, cmd_loop_index, cmd_loop_hash,
                             cmd_function, cmd_argv0, cmd_name,
                             map_phase, sv_map_state, cl_hunk_state,
                             cl_hunk_caller, cl_hunk_call_count,
                             com_error_code, com_error_hash, com_error_first4, com_error_next4,
                             direct_map_state, direct_map_error,
                             direct_map_path, direct_map_first4,
                             game_phase, client_field_before, gentities_ptr,
                             clients_ptr, gentity_size, client_field_after,
                             render_backend, primitive_calls, primitive_verts,
                             state_flushes, split_shader, split_fog, split_dlight,
                             split_entity, split_final, split_flush,
                             drawsurf_count, drawsurf_delta, leaf_count,
                             cull_patch, cull_md3, cull_box,
                             dlight_surfs, dlight_culled,
                             sky_iter, sky_portal, sky_clip, sky_box, sky_sides,
                             sky_no_outer, sky_clouds, sky_generic,
                             world_adds, world_sky, world_portal,
                             draw_total, draw_sky, draw_portal, draw_forcesight,
                             direct_com_frame, direct_cl_frame,
                             direct_cls_frame, direct_hb_count,
                             cg_render, cg_return, rscene, rscene_noworld,
                             rview, rview_world,
                             screen_draw, screen_force, screen_skip, screen_cin,
                             screen_cgame, screen_ui, screen_ret,
                             clf_enter, clf_direct, clf_before, clf_after, clf_done,
                             render_registered, render_stretch, render_stretch_null,
                             render_begin, render_begin_unregistered,
                             render_end, render_end_unregistered, render_end_cmd_null,
                             render_end_begin_fail, render_issue, render_issue_cmd_used,
                             compat_begin, compat_end, fake_swap,
                             dx8_begin, dx8_end, dx8_present,
                             dx8_present_hr, dx8_fb_updates, dx8_fb_fail,
                             alt_clf_enter, alt_clf_before, alt_screen_draw,
                             alt_cg_render, alt_rview,
                             surf_type_summary or "-",
                             ent_type_summary or "-",
                             fb_data, fb_pitch, fb_width, fb_height, fb_format, fb_size,
                             fb_nz, fb_rgb_nz, fb_xor, fb_first,
                             key_catchers, key_last, key_down, key_phase, key_count,
                             smoke_count, smoke_press, smoke_release, smoke_ui, smoke_last,
                             ui_set, ui_menu, ui_result, ui_main, ui_key_count, ui_key_last,
                             fe_phase, fe_menu, fe_item, fe_script, fe_popup, fe_response, fe_controller))
                        log("cmd_text t=%.1f last=%r text=%r fn=%r" %
                            (elapsed, cmd_last_text, cmd_full_text, cmd_func_name))
                        log("cin t=%.1f phase=%u handle=%u status=%u loop=%u arg=%r" %
                            (elapsed, cin_phase, cin_handle, cin_status, cin_loop, cin_arg))
                        log("bink t=%.1f phase=%u run=%u wait=%u breaks=%u frame=%u/%u flags=0x%08x wh=%ux%u alpha=%u copy=%u start=%u status=%u" %
                            (elapsed, bink_phase, bink_run, bink_wait, bink_wait_breaks,
                             bink_frame, bink_frames, bink_flags, bink_width, bink_height,
                             bink_alpha, bink_copy, bink_start, bink_status))
                        log("bink_mem t=%.1f code=%u alloc_seq=%u alloc_size=%u alloc_ptr=0x%08x free_seq=%u free_ptr=0x%08x active=%u active_bytes=%u peak_bytes=%u phys=%u zone=%u/%u temp=%u" %
                            (elapsed, bink_mem_code, bink_alloc_seq, bink_alloc_size,
                             bink_alloc_ptr, bink_free_seq, bink_free_ptr,
                             bink_outstanding_count, bink_outstanding_bytes,
                             bink_peak_outstanding_bytes,
                             bink_avail_phys, bink_zone_alloc, bink_zone_free,
                             bink_temp_pool))
                        log("soak t=%.1f count=%u exec=%u last_hash=0x%08x at=%u elapsed=%u" %
                            (elapsed, soak_count, soak_exec, soak_hash, soak_at, soak_elapsed))
                        log("cm t=%.1f state=%u lump=0x%08x len=%u" %
                            (elapsed, cm_load_state, cm_load_lump_hash, cm_load_lump_len))
                        log("trace t=%.1f count=%u scope=0x%08x event=0x%08x name=0x%08x freephys=%u" %
                            (elapsed, soak_trace_count, soak_trace_scope, soak_trace_event,
                             soak_trace_name, soak_trace_free))
                    elif len(words) >= 3:
                        boot_phase = words[0]
                        mirror_pos = words[1]
                        write_count = words[2]
                        delta = 0 if last_xblog_write_count is None else write_count - last_xblog_write_count
                        if delta > 0:
                            xblog_progressed = True
                            last_xblog_progress_elapsed = elapsed
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
                    ok = False
                    detail_parts = []
                    if args.xemu_native_screenshots:
                        ok, native_detail, native_path = xemu_trigger_native_screenshot(
                            proc.pid, xemu_exe, xemu_screenshot_dir)
                        detail_parts.append(native_detail)
                        if ok and native_path:
                            png = native_path
                    elif args.monitor_screenshots:
                        ok, fb_detail = monitor_framebuffer_dump(sock, png, xblog_phys_delta)
                        detail_parts.append(fb_detail)
                    if (args.monitor_screenshots or args.xemu_native_screenshots) and args.no_window_fallback:
                        detail = "; ".join(detail_parts)
                        if ok:
                            shot_paths.append(png)
                        log("shot=%02d t=%.1f ok=%s bytes=%s detail=%s" %
                            (shot, elapsed, ok, os.path.getsize(png) if ok and os.path.exists(png) else 0, detail))
                        shot += 1
                        next_shot += args.interval
                        continue
                    if not ok and not args.xemu_native_screenshots:
                        ok, qmp_detail = qmp_screendump(qmp, png)
                        detail_parts.append(qmp_detail)
                    if not ok and not args.xemu_native_screenshots:
                        mon_ok, mon_detail = monitor_screendump(sock, png, xblog_phys_delta)
                        detail_parts.append(mon_detail)
                        ok = mon_ok
                    detail = "; ".join(detail_parts)
                    if ok:
                        shot_paths.append(png)
                    log("shot=%02d t=%.1f ok=%s bytes=%s detail=%s" %
                        (shot, elapsed, ok, os.path.getsize(png) if ok and os.path.exists(png) else 0, detail))
                else:
                    log("shot=%02d t=%.1f skipped display=%s" % (shot, elapsed, display_mode))
                shot += 1
                next_shot += args.interval

            time.sleep(0.25)

        if proc.poll() is None:
            log("alive_at_end pid=%d" % proc.pid)

        if sock is not None and not args.skip_final_dumps:
            dump_monitor_state(sock, prefix, "final", args.dump_mem, log)
            final_phys_specs = list(args.dump_phys)
            if args.poll_xblog and xblog_mode == "sp":
                xblog_phys_delta = None
                if xblog_va_for_probe is not None and xblog_addr is not None:
                    xblog_phys_delta = xblog_va_for_probe - xblog_addr
                elif not args.poll_xblog_addr:
                    xblog_phys_delta = int(args.poll_xblog_phys_delta, 0)
                final_phys_specs = append_xblog_auto_dumps(final_phys_specs, xblog_phys_delta, log)
            dump_physical_memory(sock, prefix, final_phys_specs, log)
        elif args.skip_final_dumps:
            log("final_dumps=skipped")

        frozen_guest = False
        if args.fail_on_frozen and freeze_detection_reliable and register_samples and not process_exited:
            tail = register_samples[-3:]
            eips = {sample.get("eip") for sample in tail if "eip" in sample}
            halted = len(tail) >= 3 and all(sample.get("hlt") == 1 for sample in tail if "hlt" in sample)
            recent_window = max(5.0, float(args.interval) * 2.0)
            recent_progress = (
                last_xblog_progress_elapsed is not None and
                loop_elapsed - last_xblog_progress_elapsed <= recent_window)
            if len(tail) >= 3 and len(eips) == 1 and halted and not recent_progress:
                frozen_guest = True
                only_eip = next(iter(eips)) or 0
                log("guest_frozen eip=0x%08x tail_samples=%u total_samples=%u xblog_progress=%s last_progress=%s recent_window=%.1f" %
                    (only_eip, len(tail), len(register_samples), xblog_progressed,
                     "%.1f" % last_xblog_progress_elapsed if last_xblog_progress_elapsed is not None else "none",
                     recent_window))
    finally:
        if qmp:
            try:
                qmp.close()
            except Exception:
                pass
        if sock:
            try:
                sock.close()
            except Exception:
                pass
        if proc.poll() is None and frozen_guest and args.leave_running_on_frozen:
            log("xemu_left_running_frozen pid=%d" % proc.pid)
        elif proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2.0)
                log("xemu_terminated rc=%s" % proc.returncode)
            except subprocess.TimeoutExpired:
                log("xemu_terminate_timeout pid=%d" % proc.pid)
                proc.kill()
                try:
                    proc.wait(timeout=2.0)
                    log("xemu_killed rc=%s" % proc.returncode)
                except subprocess.TimeoutExpired:
                    log("xemu_kill_timeout pid=%d" % proc.pid)
        logf.close()
        if toml_backup is not None:
            with open(config_path, "w", encoding="utf-8", errors="replace") as f:
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
    if frozen_guest:
        sys.exit(4)


if __name__ == "__main__":
    main()
