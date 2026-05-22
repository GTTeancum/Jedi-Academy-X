"""
re_d3d_init.py
Targeted reverse engineering of D3D8 device initialization from the
original shipped Xbox Jedi Academy XBE.

Extracts:
  - D3DPRESENT_PARAMETERS struct passed to CreateDevice
  - Direct3D_SetPushBufferSize arguments
  - Back-buffer/depth-stencil formats
  - CreateDevice flags and adapter type

Usage:
  python re_d3d_init.py "path\to\default.xbe"
"""

import sys
import struct
import os
import io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
from capstone import *
from capstone.x86 import *

# ---------------------------------------------------------------------------
# XBE parser (minimal — just what we need)
# ---------------------------------------------------------------------------

XBE_MAGIC = 0x48454258  # 'XBEH'

def parse_xbe(data):
    magic = struct.unpack_from('<I', data, 0)[0]
    if magic != XBE_MAGIC:
        raise ValueError(f"Not a valid XBE (magic=0x{magic:08X})")

    base_addr      = struct.unpack_from('<I', data, 0x104)[0]
    headers_size   = struct.unpack_from('<I', data, 0x108)[0]
    sec_count      = struct.unpack_from('<I', data, 0x11C)[0]  # number of sections
    sec_headers_va = struct.unpack_from('<I', data, 0x120)[0]  # VA of section headers array

    print(f"XBE base address : 0x{base_addr:08X}")
    print(f"Header size      : 0x{headers_size:X}")
    print(f"Section count    : {sec_count}")

    # Build VA→file-offset map from sections
    sections = []
    sec_hdr_off = sec_headers_va - base_addr
    for i in range(sec_count):
        off = sec_hdr_off + i * 0x38
        flags      = struct.unpack_from('<II', data, off)[1]  # [0]=flags lo, [1]=flags hi?
        # Section header layout (XBE spec):
        # +0x00 flags
        # +0x04 virtual addr
        # +0x08 virtual size
        # +0x0C raw addr
        # +0x10 raw size
        # +0x14 name addr (VA)
        flags_lo   = struct.unpack_from('<I', data, off + 0x00)[0]
        virt_addr  = struct.unpack_from('<I', data, off + 0x04)[0]
        virt_size  = struct.unpack_from('<I', data, off + 0x08)[0]
        raw_addr   = struct.unpack_from('<I', data, off + 0x0C)[0]
        raw_size   = struct.unpack_from('<I', data, off + 0x10)[0]
        name_va    = struct.unpack_from('<I', data, off + 0x14)[0]

        # Read section name
        name_off = name_va - base_addr
        name = b''
        if 0 <= name_off < len(data):
            end = data.index(b'\x00', name_off) if b'\x00' in data[name_off:name_off+64] else name_off+16
            name = data[name_off:end]

        sections.append({
            'name':      name.decode('ascii', errors='replace'),
            'va':        virt_addr,
            'vsize':     virt_size,
            'raw_off':   raw_addr,
            'raw_size':  raw_size,
        })
        print(f"  Section '{name.decode('ascii','replace'):16s}': VA=0x{virt_addr:08X}  raw=0x{raw_addr:08X}  size=0x{raw_size:X}")

    return base_addr, sections

def va_to_file(va, base_addr, sections):
    for s in sections:
        if s['va'] <= va < s['va'] + s['vsize']:
            return s['raw_off'] + (va - s['va'])
    return None

def read_va(data, va, size, base_addr, sections):
    off = va_to_file(va, base_addr, sections)
    if off is None:
        return None
    return data[off:off+size]

# ---------------------------------------------------------------------------
# Find D3D-related imports (kernel thunk table)
# ---------------------------------------------------------------------------

# The XBE kernel thunk table stores ordinal-encoded import addresses.
# D3D8 functions are NOT in the thunk table — they come from d3d8.lib which
# provides stub functions. We need to find the actual CreateDevice call by
# searching for the D3DPRESENT_PARAMETERS push sequence in the code.

# D3DPRESENT_PARAMETERS layout (Xbox D3D8, 0x28 bytes):
#   +0x00 BackBufferWidth        UINT
#   +0x04 BackBufferHeight       UINT
#   +0x08 BackBufferFormat       D3DFORMAT
#   +0x0C BackBufferCount        UINT
#   +0x10 MultiSampleType        D3DMULTISAMPLE_TYPE
#   +0x14 SwapEffect             D3DSWAPEFFECT
#   +0x18 hDeviceWindow          HWND  (NULL on Xbox)
#   +0x1C Windowed               BOOL  (FALSE on Xbox)
#   +0x20 EnableAutoDepthStencil BOOL
#   +0x24 AutoDepthStencilFormat D3DFORMAT
# Note: Xbox D3D8 present params differ from PC — no FullScreen_RefreshRateInHz etc.

# Known Xbox D3D8 format values (from d3d8types.h / XDK)
D3DFMT = {
    0x00000000: 'D3DFMT_UNKNOWN',
    0x00000006: 'D3DFMT_A8R8G8B8',
    0x00000005: 'D3DFMT_X8R8G8B8',
    0x00000004: 'D3DFMT_R5G6B5',
    0x00000013: 'D3DFMT_X1R5G5B5',
    0x00000010: 'D3DFMT_A1R5G5B5',
    0x00000002: 'D3DFMT_A4R4G4B4',
    0x00000070: 'D3DFMT_D24S8',
    0x0000006E: 'D3DFMT_D16',
    0x00000075: 'D3DFMT_D24X8',
    # Xbox linear variants
    0x00000011: 'D3DFMT_LIN_A1R5G5B5',
    0x0000001D: 'D3DFMT_LIN_A4R4G4B4',
    0x0000001C: 'D3DFMT_LIN_A8R8G8B8',
    0x0000001E: 'D3DFMT_LIN_B8',
    0x00000017: 'D3DFMT_LIN_G8B8',
    0x0000001F: 'D3DFMT_LIN_R4G4B4A4',
    0x00000015: 'D3DFMT_LIN_R5G5B5A1',
    0x00000016: 'D3DFMT_LIN_R5G6B5',
    0x00000020: 'D3DFMT_LIN_R6G5B5',
    0x00000018: 'D3DFMT_LIN_R8B8',
    0x00000019: 'D3DFMT_LIN_R8G8B8A8',
    0x0000001A: 'D3DFMT_LIN_X1R5G5B5',
    0x0000001B: 'D3DFMT_LIN_X8R8G8B8',
}

D3DSWAPEFFECT = {0: 'DISCARD', 1: 'FLIP', 2: 'COPY', 3: 'COPY_VSYNC'}
D3DMULTISAMPLE = {0: 'NONE', 1: '2_SAMPLES_MULTISAMPLE_LINEAR',
                  2: '2_SAMPLES_SUPERSAMPLE_HORIZONTAL_LINEAR',
                  3: '2_SAMPLES_SUPERSAMPLE_VERTICAL_LINEAR',
                  4: '4_SAMPLES_MULTISAMPLE_LINEAR'}

CREATE_FLAGS = {
    0x00000004: 'D3DCREATE_PUREDEVICE',
    0x00000040: 'D3DCREATE_HARDWARE_VERTEXPROCESSING',
    0x00000080: 'D3DCREATE_SOFTWARE_VERTEXPROCESSING',
    0x00000200: 'D3DCREATE_MIXED_VERTEXPROCESSING',
}

# ---------------------------------------------------------------------------
# Disassemble and find CreateDevice call site
# ---------------------------------------------------------------------------

def find_code_section(sections):
    """Return the largest executable section (the main code section)."""
    # On Xbox XBEs there's typically one big code section (often unnamed or 'XPP')
    best = max(sections, key=lambda s: s['raw_size'])
    return best

def disasm_range(data, file_off, size, load_va):
    """Disassemble `size` bytes at file_off, returning list of (va, mnemonic, op_str, bytes)."""
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    insns = []
    chunk = data[file_off:file_off+size]
    for insn in md.disasm(chunk, load_va):
        insns.append(insn)
    return insns

def fmt_name(val):
    return D3DFMT.get(val, f'0x{val:08X}')

def decode_present_params(data, pp_va, base_addr, sections):
    """Read and decode a D3DPRESENT_PARAMETERS struct at the given VA."""
    raw = read_va(data, pp_va, 0x28, base_addr, sections)
    if raw is None or len(raw) < 0x28:
        return None
    fields = struct.unpack_from('<IIIIIIIIII', raw)
    return {
        'BackBufferWidth':        fields[0],
        'BackBufferHeight':       fields[1],
        'BackBufferFormat':       fmt_name(fields[2]),
        'BackBufferCount':        fields[3],
        'MultiSampleType':        D3DMULTISAMPLE.get(fields[4], f'0x{fields[4]:X}'),
        'SwapEffect':             D3DSWAPEFFECT.get(fields[5], f'0x{fields[5]:X}'),
        'hDeviceWindow':          f'0x{fields[6]:08X}',
        'Windowed':               bool(fields[7]),
        'EnableAutoDepthStencil': bool(fields[8]),
        'AutoDepthStencilFormat': fmt_name(fields[9]),
    }

def scan_for_createdevice(data, base_addr, sections):
    """
    Scan all code sections for the IDirect3D8::CreateDevice call pattern.
    On Xbox, CreateDevice is a method call (call through vtable or direct).
    We look for sequences that push D3DPRESENT_PARAMETERS addresses and
    D3DCREATE_* flags.
    Strategy: find all 'call' instructions near a 'lea' or 'push' of a
    D3DPRESENT_PARAMETERS-sized struct.
    """
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True

    results = []

    for sec in sections:
        if sec['raw_size'] < 0x100:
            continue
        raw_off = sec['raw_off']
        va_base = sec['va']
        chunk = data[raw_off:raw_off + sec['raw_size']]

        insns = list(md.disasm(chunk, va_base))

        for i, insn in enumerate(insns):
            # Look for: call to something where a few instructions before
            # we see a push of create flags (0x40 = HARDWARE_VP) or a lea/push
            # of a struct address.
            if insn.id not in (X86_INS_CALL,):
                continue

            # Grab window of 30 instructions before this call
            window = insns[max(0, i-30):i+1]

            # Collect all immediate pushes in the window
            pushes = []
            leas   = []
            for w in window:
                if w.id == X86_INS_PUSH:
                    for op in w.operands:
                        if op.type == X86_OP_IMM:
                            pushes.append((w.address, op.imm))
                if w.id == X86_INS_LEA:
                    for op in w.operands:
                        if op.type == X86_OP_MEM and op.mem.disp != 0:
                            leas.append((w.address, op.mem.disp))

            # Heuristic: CreateDevice takes 7 args on Xbox:
            #   Adapter(0), DeviceType(1=HAL), hFocusWindow(0),
            #   BehaviorFlags, pPresentationParameters, ppReturnedDevice
            # We expect to see push 0x1 (D3DDEVTYPE_HAL) and
            # push 0x40/0x04 (behavior flags) in the window.

            has_hal    = any(v == 1 for _, v in pushes)  # D3DDEVTYPE_HAL
            has_hwvp   = any(v in (0x40, 0x04, 0x44, 0x400040) for _, v in pushes)
            has_zero   = any(v == 0 for _, v in pushes)  # adapter 0

            if has_hal and (has_hwvp or has_zero):
                call_target = None
                if insn.operands:
                    op = insn.operands[0]
                    if op.type == X86_OP_IMM:
                        call_target = op.imm

                results.append({
                    'call_va':   insn.address,
                    'target_va': call_target,
                    'pushes':    pushes,
                    'leas':      leas,
                    'window':    window,
                })

    return results

def scan_for_setpushbuffer(data, base_addr, sections):
    """
    Find Direct3D_SetPushBufferSize calls.
    This function takes (CommandBufferSize, FixupBufferSize).
    Look for two consecutive pushes of large power-of-2 values before a call.
    """
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    results = []

    for sec in sections:
        if sec['raw_size'] < 0x100:
            continue
        chunk = data[sec['raw_off']:sec['raw_off'] + sec['raw_size']]
        insns = list(md.disasm(chunk, sec['va']))

        for i, insn in enumerate(insns):
            if insn.id != X86_INS_CALL:
                continue
            window = insns[max(0, i-10):i+1]
            pushes = []
            for w in window:
                if w.id == X86_INS_PUSH:
                    for op in w.operands:
                        if op.type == X86_OP_IMM and op.imm >= 0x10000:
                            pushes.append(op.imm)
            # SetPushBufferSize takes two large sizes
            if len(pushes) >= 2:
                results.append({
                    'call_va': insn.address,
                    'sizes':   pushes[-2:],
                    'window':  window,
                })
    return results

def scan_memory_alloc(data, base_addr, sections):
    """
    Find XPhysicalAlloc / D3D_AllocContiguousMemory calls.
    These take a Size, Alignment, Protect, and PageAttributes argument.
    Look for pushes of 0x404 (PAGE_READWRITE | PAGE_WRITECOMBINE) near calls.
    """
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    results = []

    for sec in sections:
        if sec['raw_size'] < 0x100:
            continue
        chunk = data[sec['raw_off']:sec['raw_off'] + sec['raw_size']]
        insns = list(md.disasm(chunk, sec['va']))

        for i, insn in enumerate(insns):
            if insn.id != X86_INS_CALL:
                continue
            window = insns[max(0, i-15):i+1]
            pushes = [(w.address, op.imm)
                      for w in window if w.id == X86_INS_PUSH
                      for op in w.operands if op.type == X86_OP_IMM]
            # XPhysicalAlloc signature marker: push of 0x404 (writecombine) or 0x4 (PAGE_READWRITE)
            has_wc = any(v in (0x404, 0x4) for _, v in pushes)
            if has_wc and len(pushes) >= 3:
                results.append({
                    'call_va': insn.address,
                    'pushes':  pushes,
                    'window':  window,
                })
    return results

# ---------------------------------------------------------------------------
# Print helpers
# ---------------------------------------------------------------------------

def print_insns(insns, highlight_va=None):
    for ins in insns:
        marker = '>>>' if ins.address == highlight_va else '   '
        print(f"  {marker} 0x{ins.address:08X}:  {ins.mnemonic:8s} {ins.op_str}")

def decode_create_flags(val):
    flags = []
    for mask, name in CREATE_FLAGS.items():
        if val & mask:
            flags.append(name)
    return ' | '.join(flags) if flags else f'0x{val:X}'

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) < 2:
        print("Usage: re_d3d_init.py <path_to_default.xbe>")
        sys.exit(1)

    path = sys.argv[1]
    print(f"\n{'='*70}")
    print(f"  Jedi Academy Xbox XBE — D3D Init Analysis")
    print(f"  {path}")
    print(f"{'='*70}\n")

    with open(path, 'rb') as f:
        data = f.read()
    print(f"XBE size: {len(data):,} bytes ({len(data)/1024/1024:.2f} MB)\n")

    base_addr, sections = parse_xbe(data)
    print()

    # ---- SetPushBufferSize ----
    print(f"{'─'*60}")
    print("SCANNING: Direct3D_SetPushBufferSize calls")
    print(f"{'─'*60}")
    pb_results = scan_for_setpushbuffer(data, base_addr, sections)
    seen_sizes = set()
    for r in pb_results:
        key = tuple(r['sizes'])
        if key in seen_sizes:
            continue
        seen_sizes.add(key)
        sizes = r['sizes']
        print(f"\n  Call @ 0x{r['call_va']:08X}")
        print(f"  Arg0 (CommandBufferSize) : 0x{sizes[-1]:08X}  ({sizes[-1]:,} bytes = {sizes[-1]//1024} KB)")
        print(f"  Arg1 (FixupBufferSize)   : 0x{sizes[-2]:08X}  ({sizes[-2]:,} bytes = {sizes[-2]//1024} KB)")
        print(f"  Instructions:")
        print_insns(r['window'], r['call_va'])

    # ---- CreateDevice ----
    print(f"\n{'─'*60}")
    print("SCANNING: IDirect3D8::CreateDevice call sites")
    print(f"{'─'*60}")
    cd_results = scan_for_createdevice(data, base_addr, sections)
    print(f"  Found {len(cd_results)} candidate call site(s)\n")

    for idx, r in enumerate(cd_results[:5]):  # cap at 5
        print(f"\n  [Candidate {idx+1}] Call @ 0x{r['call_va']:08X}")
        if r['target_va']:
            print(f"  Target: 0x{r['target_va']:08X}")

        # Decode push values
        pushes = r['pushes']
        print(f"  Push immediates: {[hex(v) for _, v in pushes]}")

        # Try to find behavior flags
        for va, v in pushes:
            name = decode_create_flags(v)
            if 'D3DCREATE' in name:
                print(f"  BehaviorFlags: {name}")

        # Try to find a D3DPRESENT_PARAMETERS by looking for lea of a nearby address
        for lea_va, disp in r['leas']:
            pp = decode_present_params(data, disp, base_addr, sections)
            if pp and pp['BackBufferWidth'] in (640, 720, 848, 1280):
                print(f"\n  D3DPRESENT_PARAMETERS @ 0x{disp:08X}:")
                for k, v in pp.items():
                    print(f"    {k:30s} = {v}")
                break

        print(f"  Disassembly:")
        print_insns(r['window'], r['call_va'])

    # ---- Memory allocs ----
    print(f"\n{'─'*60}")
    print("SCANNING: Contiguous memory allocations (XPhysicalAlloc / D3D)")
    print(f"{'─'*60}")
    mem_results = scan_memory_alloc(data, base_addr, sections)
    seen_allocs = set()
    for r in mem_results[:8]:
        sizes = tuple(v for _, v in r['pushes'] if v >= 0x10000)
        if sizes in seen_allocs:
            continue
        seen_allocs.add(sizes)
        print(f"\n  Call @ 0x{r['call_va']:08X}")
        print(f"  Push args: {[hex(v) for _, v in r['pushes']]}")
        print_insns(r['window'], r['call_va'])

    # ---- Raw data search: scan for D3DPRESENT_PARAMETERS by known patterns ----
    print(f"\n{'─'*60}")
    print("SCANNING: D3DPRESENT_PARAMETERS structs in data sections")
    print("  (looking for 640x480 or 720x480 backbuffer + known D3DFMT values)")
    print(f"{'─'*60}")

    known_formats = set(D3DFMT.keys())
    for sec in sections:
        if sec['raw_size'] < 0x28:
            continue
        chunk = data[sec['raw_off']:sec['raw_off'] + sec['raw_size']]
        for off in range(0, len(chunk) - 0x28, 4):
            fields = struct.unpack_from('<IIIIIIIIII', chunk, off)
            w, h, fmt, cnt, ms, swap, hwnd, windowed, autos, afmt = fields
            if w in (320, 640, 720, 848) and h in (240, 480) and fmt in known_formats and cnt in (1, 2):
                va = sec['va'] + off
                print(f"\n  D3DPRESENT_PARAMETERS @ VA 0x{va:08X} (raw+0x{off:X}) in '{sec['name']}':")
                print(f"    BackBufferWidth        = {w}")
                print(f"    BackBufferHeight       = {h}")
                print(f"    BackBufferFormat       = {fmt_name(fmt)}")
                print(f"    BackBufferCount        = {cnt}")
                print(f"    MultiSampleType        = {D3DMULTISAMPLE.get(ms, hex(ms))}")
                print(f"    SwapEffect             = {D3DSWAPEFFECT.get(swap, hex(swap))}")
                print(f"    hDeviceWindow          = 0x{hwnd:08X}")
                print(f"    Windowed               = {bool(windowed)}")
                print(f"    EnableAutoDepthStencil = {bool(autos)}")
                print(f"    AutoDepthStencilFormat = {fmt_name(afmt)}")

    print(f"\n{'='*70}")
    print("Analysis complete.")
    print(f"{'='*70}\n")

if __name__ == '__main__':
    main()
