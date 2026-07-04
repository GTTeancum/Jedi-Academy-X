#!/usr/bin/env python3
import argparse
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
    map_paths = [
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
    return []


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
    parser.add_argument("--hdd", default=r"C:\Games\Emulators\Xemu\HDD\jediacademy_hdd.qcow2")
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
    parser.add_argument("--poll-xblog-kind", choices=["auto", "sp", "mp"], default="auto",
                        help="Prefer SP or MP XBLog symbols when polling. Auto preserves the historical probe order.")
    parser.add_argument("--poll-xblog-addr", default="",
                        help="Address of g_*XBBootPhase. Empty means resolve _g_SPXBBootPhase from the current map.")
    parser.add_argument("--poll-xblog-phys-delta", default="0x284000",
                        help="Auto-resolved XBLog VA minus physical monitor address. Use 0 to poll virtual x/ memory.")
    args = parser.parse_args()

    os.makedirs(OUT_DIR, exist_ok=True)
    xemu_exe = os.path.abspath(args.xemu_exe)
    if os.path.normcase(xemu_exe) == os.path.normcase(os.path.abspath(XEMU_JA)):
        ensure_xemu_copy()
    config_path = os.path.abspath(args.config_path) if args.config_path else XEMU_TOML

    stamp = time.strftime("%Y%m%d_%H%M%S")
    prefix = os.path.join(OUT_DIR, "%s_%s" % (args.name, stamp))
    raw_log = prefix + ".xemu.txt"
    report = prefix + ".report.txt"
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
        print(msg)
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
    proc = subprocess.Popen(argv, cwd=xemu_cwd, stdout=logf, stderr=subprocess.STDOUT)
    log("pid=%d" % proc.pid)
    monitor_key_events = parse_key_schedule(args.monitor_keys)
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
        "_g_SPXBSVProbeMagic",
        "_g_SPXBSVProbePhase",
        "_g_SPXBSVProbeSubphase",
        "_g_SPXBSVProbeA",
        "_g_SPXBSVProbeB",
        "_g_SPXBSVProbeC",
        "_g_SPXBSVProbeD",
    ])
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
                        telemetry_words = words
                        if xblog_cmd == "xp" and xblog_va_for_probe is not None:
                            telemetry_addr = xblog_va_for_probe - 0x284000
                            try:
                                telemetry_reply = monitor_cmd(sock, "xp/%dwx 0x%08x" % (poll_words, telemetry_addr), 0.4)
                                telemetry_probe = parse_monitor_words(telemetry_reply, telemetry_addr)
                                if len(telemetry_probe) >= len(words):
                                    telemetry_words = telemetry_probe
                            except OSError:
                                pass
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
                        render_backend = word_for("_g_SPXBRenderBackendMsec")
                        primitive_calls = word_for("_g_SPXBFakeGLPrimitiveCalls")
                        primitive_verts = word_for("_g_SPXBFakeGLPrimitiveVerts")
                        state_flushes = word_for("_g_SPXBFakeGLStateFlushes")
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
                        sv_probe_magic = word_for("_g_SPXBSVProbeMagic")
                        sv_probe_phase = word_for("_g_SPXBSVProbePhase")
                        sv_probe_subphase = word_for("_g_SPXBSVProbeSubphase")
                        sv_probe_a = word_for("_g_SPXBSVProbeA")
                        sv_probe_b = word_for("_g_SPXBSVProbeB")
                        sv_probe_c = word_for("_g_SPXBSVProbeC")
                        sv_probe_d = word_for("_g_SPXBSVProbeD")
                        log("xblog t=%.1f boot=0x%08x mirror=%u writes=%u delta=%d hb=0x%08x count=%u frame=%u rt=%u st=%u fps=%.1f main=%u com=%u sv=%u cl=%u cls=%u clst=%u clsfr=%u phase=0x%08x sub=%u spin=%u msec=%u ctime=%u ltime=%u cbuf=%u cmd=%u cmdp=%u cmdh=0x%08x argc=%u mapp=%u maph=0x%08x gamep=%u ents=%u be=%u prim=%u verts=%u state=%u split=%u/%u/%u/%u final=%u flush=%u splitSlot=%u draw=%u/%u world=%u/%u retry=%u fallback=%u cluster=%d/%d mark=%d/%d pvsrej=%u/%u arearej=%u/%u root=%d/%d surf=%u/%u/%u/%u/%u/%u/%u/%u p2=%u trace=%u view=%d/%d/%d ps=%d/%d/%d cur=%d/%d/%d ang=%d/%d cam=%u p1trace=%u p1loc=%d/%d/%d p2loc=%d/%d/%d diff=%d/%d/%d p2dbg=ref=%u scene=%u/%u/%u model=%u/%u/%u/%u h=%u/%u/%u rf=0x%08x renderer=%u/%u/0x%08x/%d vw=%u/%u/%u/%u model=%u/%u rf=0x%08x/0x%08x rend=%u/%u filt=%u/%u skip=%u/%u wreg=%u/0x%08x/0x%08x/0x%08x/%u/%u/%u/%u wload=%u/%u/%u/%u/0x%08x/0x%08x/%u/0x%08x wm=%u/0x%08x/%u/%u/0x%08x/%u/%u/%u/%u/%u direct=%u/0x%08x/%u svp=0x%08x/0x%08x/%u/%u/%u/%u/%u" %
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
                               direct_status, direct_hash, direct_queued,
                              sv_probe_magic, sv_probe_phase, sv_probe_subphase,
                             sv_probe_a, sv_probe_b, sv_probe_c, sv_probe_d))
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
