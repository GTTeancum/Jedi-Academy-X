#!/usr/bin/env python3
"""
re_full_render_disasm.py
Full disassembly of the retail Jedi Academy SP rendering pipeline (XDK 5558).

Strategy: walk the call-graph from known rendering anchors (main → Com_Init →
CL_InitRef → GLimp_Init → GLW_Init → CreateDevice; per-frame: R_RenderScene),
collecting every reachable function. Disassemble each function fully with
Capstone, resolve call targets, annotate strings/imports/known-symbols, and
emit a single comprehensive trace.

OUTPUT: scripts/output/full_render_disasm.txt — sectioned by function.

The output is structured so we can rewrite the rendering pipeline
instruction-for-instruction in C++ if needed.
"""
import sys, struct, os, io, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG

XBE_PATH = r"C:\Programming\GitHub\Jedi-Academy-X\Jedi Academy Xbox mod tools\Jedi Knight Jedi Academy\default.xbe"
OUT_PATH = r"C:\Programming\GitHub\Jedi-Academy-X\scripts\output\full_render_disasm.txt"

XBE_EP_RETAIL = 0xA8FC57AB
XBE_KT_RETAIL = 0x5B6D40B6

# Known rendering anchors from prior phase work
RENDER_ANCHORS = {
    0x17320:  "R_Init_context",
    0x98310:  "GLW_StartOpenGL",
    0x97DA5:  "GLimp_Init_fn",
    0xA4245:  "GLW_Init_CreateDevice",
    0x23F570: "Direct3D_CreateDevice",
    0x9D9C7:  "VV_ident_HDR",
    0x80E90:  "R_RenderScene",
    0x62100:  "Render_dispatch",
    0x44E50:  "Z_Malloc",
    0x49945:  "Com_InitZoneMemory",
    0x49B00:  "Z_Malloc_inner",
    0xC26A8:  "XGetVideoFlags",
    0x23D560: "Direct3D_GetDeviceCaps",
    0x23D420: "D3DXCreateMatrixStack",
    0x23F620: "D3D_AllocContiguousMemory",
}

# Known kernel ordinals → names (subset of xboxkrnl)
KERNEL_ORD = {
    0x18: "MmAllocateContiguousMemory",
    0x80: "ObReferenceObjectByHandle",
    0xBB: "RtlInitAnsiString",
    0xBE: "RtlInitUnicodeString",
    0xE2: "XInitDevices",
    0xEC: "XGetVideoFlags",
    0xF6: "XInputOpen",
    0xFA: "XGetDeviceChanges",
    0x115: "XInputGetState",
    0x121: "XGetVideoStandard",
    0x123: "XBoxHwInfo",
    0x126: "AvGetSavedDataAddress",
    0x12D: "ExAllocatePool",
    0x150: "KeWaitForSingleObject",
    0x14F: "KeQueryPerformanceCounter",
}

def load_xbe():
    with open(XBE_PATH,'rb') as f: raw=f.read()
    assert raw[:4]==b'XBEH'
    base = struct.unpack_from('<I', raw, 0x104)[0]
    ep_enc = struct.unpack_from('<I', raw, 0x128)[0]
    ep = ep_enc ^ XBE_EP_RETAIL
    sec_count = struct.unpack_from('<I', raw, 0x11C)[0]
    sec_hdr = struct.unpack_from('<I', raw, 0x120)[0]
    sections = []
    for i in range(sec_count):
        o = (sec_hdr - base) + i*0x38
        flags = struct.unpack_from('<I', raw, o+0)[0]
        va = struct.unpack_from('<I', raw, o+4)[0]
        vsz = struct.unpack_from('<I', raw, o+8)[0]
        roff = struct.unpack_from('<I', raw, o+0xC)[0]
        rsz = struct.unpack_from('<I', raw, o+0x10)[0]
        nva = struct.unpack_from('<I', raw, o+0x14)[0]
        noff = nva - base
        end = raw.find(b'\x00', noff, noff+32)
        name = raw[noff:end if end!=-1 else noff+16].decode('ascii','replace').strip()
        sections.append({'name':name,'flags':flags,'va':va,'vsz':vsz,'roff':roff,'rsz':rsz})
    return raw, base, ep, sections

def section_of(va, sections):
    for s in sections:
        if s['va']<=va<s['va']+s['vsz']:
            return s
    return None

def va_to_off(va, sections):
    s = section_of(va, sections)
    if s is None: return None
    return s['roff'] + (va - s['va'])

def read_str(raw, va, sections, maxlen=128):
    o = va_to_off(va, sections)
    if o is None or o<0 or o>=len(raw): return None
    end = raw.find(b'\x00', o, o+maxlen)
    if end == -1: end = o+maxlen
    b = raw[o:end]
    if not b: return None
    try:
        s = b.decode('latin-1','replace')
        printable = sum(1 for c in s if 0x20<=ord(c)<0x7F or c in '\r\n\t')
        if printable >= len(s)*0.9 and len(s)>=2:
            return s
    except: pass
    return None

def build_kernel_thunks(raw, sections):
    """Walk the kernel imports thunk table and return {VA: ordinal_name}."""
    kt_enc = struct.unpack_from('<I', raw, 0x158)[0]
    kt_va = kt_enc ^ XBE_KT_RETAIL
    off = va_to_off(kt_va, sections)
    thunks = {}
    if off is None: return thunks, kt_va
    i = 0
    while True:
        v = struct.unpack_from('<I', raw, off + i*4)[0]
        if v == 0: break
        if v & 0x80000000:
            ordinal = v & 0x7FFFFFFF
            name = KERNEL_ORD.get(ordinal, f"Kernel_ord_{ordinal:#x}")
            thunks[kt_va + i*4] = name
        i += 1
        if i > 1000: break
    return thunks, kt_va

def find_function_end(md, raw, off, va, sections, max_len=0x4000):
    """Heuristic: scan forward from va until we hit ret/jmp followed by alignment."""
    # Get section bounds
    s = section_of(va, sections)
    if s is None: return va
    sec_end = s['va'] + s['vsz']
    end_va = min(va + max_len, sec_end)
    end_off = va_to_off(end_va, sections)
    code = raw[off:end_off]
    last_ret = va
    for ins in md.disasm(code, va):
        if ins.mnemonic in ('ret','iret','retf'):
            last_ret = ins.address + ins.size
            # Look ahead for int3 alignment or another function start
            next_va = last_ret
            next_off = va_to_off(next_va, sections)
            if next_off is None or next_off+8>len(raw): break
            ahead = raw[next_off:next_off+16]
            # If padding (CC) present, function ended
            if ahead[:1] == b'\xCC':
                return last_ret
            # If next bytes look like a function prologue (push ebp; mov ebp,esp), end
            if ahead[:3] == b'\x55\x8B\xEC' or ahead[:1] == b'\x55':
                return last_ret
            # If next is sub esp (typical prolog)
            if ahead[:3] == b'\x83\xEC\x00' or ahead[:2] == b'\x83\xEC' or ahead[:3] == b'\x81\xEC\x00':
                return last_ret
        if ins.mnemonic in ('jmp',) and ins.op_str.startswith('0x') == False and '[' not in ins.op_str:
            # Unconditional jmp to a register/memory might be tail-call; treat as ret-ish
            pass
    return last_ret if last_ret > va else end_va

def call_targets(md, raw, va_start, va_end, sections):
    """Disassemble a range, return set of call target VAs in .text."""
    targets = set()
    off = va_to_off(va_start, sections)
    end_off = va_to_off(va_end, sections)
    if off is None or end_off is None: return targets
    code = raw[off:end_off]
    for ins in md.disasm(code, va_start):
        try:
            if ins.mnemonic == 'call' and len(ins.operands) == 1:
                op = ins.operands[0]
                if op.type == X86_OP_IMM:
                    t = op.imm & 0xFFFFFFFF
                    s = section_of(t, sections)
                    if s and s['name'] == '.text':
                        targets.add(t)
        except Exception:
            continue
    return targets

def annotate_operand(md, raw, ins, sections, kernel_thunks, func_names, str_cache):
    """Return annotation suffix for operand: -> name, ; "string", ; ord_xxx etc."""
    notes = []
    try:
        operands = ins.operands
    except Exception:
        return ''
    for op in operands:
        if op.type == X86_OP_IMM:
            v = op.imm & 0xFFFFFFFF
            # Function call target
            if ins.mnemonic == 'call':
                if v in func_names:
                    notes.append(f"-> {func_names[v]}")
                else:
                    s = section_of(v, sections)
                    if s:
                        notes.append(f"-> {s['name']} {v:#x}")
            else:
                # String?
                if v >= 0x10000 and v < 0x800000:
                    if v in str_cache:
                        s = str_cache[v]
                    else:
                        s = read_str(raw, v, sections, maxlen=64)
                        str_cache[v] = s
                    if s:
                        notes.append(f'; "{s[:48]}"')
        elif op.type == X86_OP_MEM:
            disp = op.mem.disp & 0xFFFFFFFF
            if op.mem.base == 0 and op.mem.index == 0 and disp:
                # Direct memory access — could be kernel thunk
                if disp in kernel_thunks:
                    notes.append(f"; -> {kernel_thunks[disp]}")
                # String pointer?
                elif disp >= 0x10000 and disp < 0x800000:
                    s = read_str(raw, disp, sections, maxlen=64)
                    if s:
                        notes.append(f'; -> "{s[:48]}"')
    return ' '.join(notes)

def disasm_function(md, raw, va, sections, kernel_thunks, func_names, str_cache, max_len=0x4000):
    """Disassemble a function; return (lines, end_va, call_targets_in_text)."""
    off = va_to_off(va, sections)
    if off is None:
        return [f"; ! could not resolve VA {va:#x} to file offset"], va, set()
    end_va = find_function_end(md, raw, off, va, sections, max_len)
    end_off = va_to_off(end_va, sections)
    code = raw[off:end_off]
    lines = []
    targets = set()
    for ins in md.disasm(code, va):
        bytehex = ' '.join(f'{b:02X}' for b in ins.bytes)
        if len(bytehex) > 32: bytehex = bytehex[:29] + '...'
        anno = annotate_operand(md, raw, ins, sections, kernel_thunks, func_names, str_cache)
        line = f"  {ins.address:08X}:  {bytehex:32s}  {ins.mnemonic:8s} {ins.op_str}"
        if anno:
            line += f"  {anno}"
        lines.append(line)
        try:
            if ins.mnemonic == 'call' and len(ins.operands)==1 and ins.operands[0].type == X86_OP_IMM:
                t = ins.operands[0].imm & 0xFFFFFFFF
                s = section_of(t, sections)
                if s and s['name']=='.text' and t not in func_names:
                    targets.add(t)
        except Exception:
            pass
    return lines, end_va, targets

def collect_strings(raw, sections, max_per_section=None):
    """Find all printable strings in .rdata for cross-reference."""
    result = {}
    for s in sections:
        if s['name'] not in ('.rdata', '.data'): continue
        data = raw[s['roff']:s['roff']+s['rsz']]
        i = 0
        while i < len(data):
            j = i
            while j < len(data) and (0x20 <= data[j] < 0x7F or data[j] in (0x09, 0x0A, 0x0D)):
                j += 1
            if j - i >= 4:
                try:
                    txt = data[i:j].decode('latin-1','replace')
                    va = s['va'] + i
                    result[va] = txt
                except: pass
            i = max(j+1, i+1)
    return result

def main():
    print(f"[+] Loading {XBE_PATH}", file=sys.stderr)
    raw, base, ep, sections = load_xbe()
    print(f"    base={base:#x} ep={ep:#x} sections={len(sections)}", file=sys.stderr)
    for s in sections:
        print(f"    {s['name']:12s} va={s['va']:#010x} vsz={s['vsz']:#010x} flags={s['flags']:#x}", file=sys.stderr)

    kernel_thunks, kt_va = build_kernel_thunks(raw, sections)
    print(f"[+] Kernel thunk table at {kt_va:#x} ({len(kernel_thunks)} thunks)", file=sys.stderr)

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    md.skipdata = True

    func_names = dict(RENDER_ANCHORS)

    # Collect strings for cross-reference
    print("[+] Collecting strings", file=sys.stderr)
    strings = collect_strings(raw, sections)
    print(f"    {len(strings)} strings found", file=sys.stderr)
    str_cache = {}

    # Walk call graph from rendering anchors
    print("[+] Walking call graph from anchors", file=sys.stderr)
    visited = set()
    queue = list(RENDER_ANCHORS.keys())
    func_disasm = {}  # va -> (name, lines, end_va)
    while queue:
        va = queue.pop()
        if va in visited: continue
        visited.add(va)
        s = section_of(va, sections)
        if s is None or s['name'] != '.text': continue
        name = func_names.get(va, f"sub_{va:08X}")
        lines, end_va, targets = disasm_function(md, raw, va, sections, kernel_thunks, func_names, str_cache)
        func_disasm[va] = (name, lines, end_va)
        # Limit recursion to keep output bounded; only follow deeply within rendering
        for t in targets:
            if t not in visited and len(visited) < 250:
                queue.append(t)

    print(f"[+] Disassembled {len(func_disasm)} functions", file=sys.stderr)

    # Also disassemble the D3D section monolithically (it contains d3d8.lib code)
    d3d_section = next((s for s in sections if s['name']=='D3D'), None)
    print("[+] Writing output", file=sys.stderr)
    with open(OUT_PATH,'w', encoding='utf-8') as f:
        f.write("="*80+"\n")
        f.write("FULL DISASSEMBLY: Jedi Academy SP retail rendering pipeline\n")
        f.write(f"Source: {XBE_PATH}\n")
        f.write(f"Image base: {base:#x}\n")
        f.write(f"Entry point: {ep:#x}\n")
        f.write(f"Kernel thunk table VA: {kt_va:#x}\n")
        f.write("="*80+"\n\n")

        f.write("SECTIONS\n"+"-"*80+"\n")
        for s in sections:
            f.write(f"  {s['name']:12s} VA={s['va']:#010x} VSize={s['vsz']:#010x} RawOff={s['roff']:#010x} Flags={s['flags']:#x}\n")
        f.write("\n")

        f.write("KNOWN ANCHORS (rendering pipeline entry points)\n"+"-"*80+"\n")
        for va, name in sorted(RENDER_ANCHORS.items()):
            f.write(f"  {va:#010x}  {name}\n")
        f.write("\n")

        f.write("KERNEL IMPORTS (resolved ordinals)\n"+"-"*80+"\n")
        for tva, name in sorted(kernel_thunks.items()):
            f.write(f"  {tva:#010x}  {name}\n")
        f.write("\n")

        f.write("="*80+"\n")
        f.write(f"FUNCTION DISASSEMBLY ({len(func_disasm)} functions)\n")
        f.write("="*80+"\n\n")

        for va in sorted(func_disasm.keys()):
            name, lines, end_va = func_disasm[va]
            sec = section_of(va, sections)
            f.write(f"\n;{'='*78}\n")
            f.write(f"; {name}  (VA {va:#010x} - {end_va:#010x}, {end_va-va} bytes, section {sec['name'] if sec else '?'})\n")
            f.write(f";{'='*78}\n")
            f.write(f"{name}:\n")
            for ln in lines:
                f.write(ln+'\n')

        # Append D3D section dump (raw bytes per VA — for spot-checking)
        if d3d_section:
            f.write("\n\n"+"="*80+"\n")
            f.write(f"D3D SECTION RAW BYTES (VA {d3d_section['va']:#x}, {d3d_section['vsz']} bytes)\n")
            f.write("="*80+"\n")
            data = raw[d3d_section['roff']:d3d_section['roff']+min(d3d_section['rsz'], 0x4000)]
            for off in range(0, len(data), 16):
                chunk = data[off:off+16]
                hexs = ' '.join(f'{b:02X}' for b in chunk)
                ascii_repr = ''.join(chr(b) if 0x20<=b<0x7F else '.' for b in chunk)
                f.write(f"  {d3d_section['va']+off:08X}: {hexs:<47s}  {ascii_repr}\n")
            if d3d_section['rsz'] > 0x4000:
                f.write(f"  ... (truncated; D3D section is {d3d_section['rsz']} bytes total)\n")

    print(f"[+] Wrote {OUT_PATH}", file=sys.stderr)

if __name__ == "__main__":
    main()
