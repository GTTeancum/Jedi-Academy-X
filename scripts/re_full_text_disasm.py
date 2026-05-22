#!/usr/bin/env python3
"""
re_full_text_disasm.py
COMPLETE disassembly of every executable section in the retail Jedi Academy
SP XBE. Every byte of .text + D3D + D3DX + XGRPH is decoded, with function
boundaries marked, call targets resolved, and string operands annotated.

OUTPUT: scripts/output/full_text_disasm.txt — the gold standard for
rewriting the rendering pipeline byte-for-byte.
"""
import sys, struct, os, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

XBE_PATH = r"C:\Programming\GitHub\Jedi-Academy-X\Jedi Academy Xbox mod tools\Jedi Knight Jedi Academy\default.xbe"
OUT_DIR = r"C:\Programming\GitHub\Jedi-Academy-X\scripts\output"

XBE_EP_RETAIL = 0xA8FC57AB
XBE_KT_RETAIL = 0x5B6D40B6

# Known anchors with names — used to seed function-name resolution
NAMED_ANCHORS = {
    0x17320:  "R_Init_context",
    0x98310:  "GLW_StartOpenGL",
    0x97DA5:  "GLimp_Init_fn",
    0xA4245:  "GLW_Init_CreateDevice",
    0x9D9C7:  "VV_ident_HDR",
    0x80E90:  "R_RenderScene",
    0x62100:  "Render_dispatch",
    0x44E50:  "Z_Malloc",
    0x49945:  "Com_InitZoneMemory",
    0x49B00:  "Z_Malloc_inner",
    0xC26A8:  "XGetVideoFlags",
    0x33885:  "S_DirectSound3D_init",
    0x32565:  "S_StartSound",
    0x30500:  "S_HashName",
    0x48015:  "S_LoadSoundBank",
    0x44CB5:  "Sys_QueEvent",
}

# Sections to fully disassemble
DISASM_SECTIONS = ('.text', 'D3D', 'D3DX', 'XGRPH')

# Known kernel ordinals
KERNEL_ORD = {
    0x18: "MmAllocateContiguousMemory",
    0x57: "KeInitializeDpc",
    0x80: "ObReferenceObjectByHandle",
    0x82: "ObOpenObjectByName",
    0xBB: "RtlInitAnsiString",
    0xBE: "RtlInitUnicodeString",
    0xCA: "RtlEnterCriticalSection",
    0xCB: "RtlLeaveCriticalSection",
    0xE2: "XInitDevices",
    0xEC: "XGetVideoFlags",
    0xF6: "XInputOpen",
    0xFA: "XGetDeviceChanges",
    0x115: "XInputGetState",
    0x121: "XGetVideoStandard",
    0x123: "XBoxHwInfo",
    0x126: "AvGetSavedDataAddress",
    0x12D: "ExAllocatePool",
    0x14F: "KeQueryPerformanceCounter",
    0x150: "KeWaitForSingleObject",
    0x183: "NtAllocateVirtualMemory",
    0x183: "NtAllocateVirtualMemory",
    0x153: "KeConnectInterrupt",
    0x158: "HalRegisterShutdownNotification",
    0x15F: "HalGetInterruptVector",
    0x163: "KeSetEvent",
    0x101: "RtlGetLastError",
    0xC4: "PsCreateSystemThreadEx",
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
        if printable >= len(s)*0.9 and len(s)>=3:
            return s
    except: pass
    return None

def build_kernel_thunks(raw, sections):
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

def discover_function_starts(raw, section, md):
    """Find all function-prologue patterns in the section."""
    starts = set()
    data = raw[section['roff']:section['roff']+section['rsz']]
    base_va = section['va']
    # Common x86 function prologues:
    # 55 8B EC      push ebp; mov ebp,esp
    # 83 EC ??      sub esp,imm8
    # 81 EC ?? ?? ?? ?? sub esp,imm32
    # 53 56 57      push ebx; push esi; push edi  (saves)
    # 6A ??         push imm8 (start of func that takes args first)
    # E8/E9 follow CC pad (alignment after ret/jmp)
    i = 0
    while i < len(data):
        # int3 padding -> next non-int3 is a function start
        if data[i] == 0xCC:
            j = i
            while j < len(data) and data[j] == 0xCC:
                j += 1
            if j < len(data):
                starts.add(base_va + j)
            i = j + 1
            continue
        i += 1
    # Also: every byte that follows a ret + alignment in the disassembly walk
    return starts

def annotate_operand(raw, ins, sections, kernel_thunks, func_names, str_cache):
    notes = []
    try:
        operands = ins.operands
    except Exception:
        return ''
    for op in operands:
        if op.type == X86_OP_IMM:
            v = op.imm & 0xFFFFFFFF
            if ins.mnemonic == 'call':
                if v in func_names:
                    notes.append(f"-> {func_names[v]}")
                else:
                    s = section_of(v, sections)
                    if s:
                        notes.append(f"-> {s['name']} {v:#x}")
            else:
                if 0x10000 <= v < 0xC30000:
                    if v in str_cache:
                        sv = str_cache[v]
                    else:
                        sv = read_str(raw, v, sections, maxlen=80)
                        str_cache[v] = sv
                    if sv and len(sv) >= 3:
                        notes.append(f'; "{sv[:60]}"')
        elif op.type == X86_OP_MEM:
            disp = op.mem.disp & 0xFFFFFFFF
            if op.mem.base == 0 and op.mem.index == 0 and disp:
                if disp in kernel_thunks:
                    notes.append(f"; -> {kernel_thunks[disp]}")
                elif 0x10000 <= disp < 0xC30000:
                    if disp in str_cache:
                        sv = str_cache[disp]
                    else:
                        sv = read_str(raw, disp, sections, maxlen=80)
                        str_cache[disp] = sv
                    if sv and len(sv) >= 3:
                        notes.append(f'; -> "{sv[:60]}"')
    return ' '.join(notes)

def disasm_section(raw, section, md, kernel_thunks, func_names, str_cache, fout):
    """Disassemble entire section, marking function boundaries."""
    base = section['va']
    data = raw[section['roff']:section['roff']+section['rsz']]
    # Find function starts via int3 padding boundary
    starts = discover_function_starts(raw, section, md)
    starts.add(base)  # Section start is always a function start
    starts_sorted = sorted(s for s in starts if base <= s < base + len(data))

    fout.write(f"\n{'='*80}\n")
    fout.write(f"SECTION: {section['name']}\n")
    fout.write(f"  VA: {section['va']:#010x}-{section['va']+section['vsz']:#010x}\n")
    fout.write(f"  RawOff: {section['roff']:#010x}  RawSize: {section['rsz']:#x}\n")
    fout.write(f"  Function starts found: {len(starts_sorted)}\n")
    fout.write(f"{'='*80}\n\n")

    cur_func_va = None
    written_names = set()
    insn_count = 0

    for ins in md.disasm(data, base):
        # Mark function boundary
        if ins.address in starts:
            cur_func_va = ins.address
            name = func_names.get(cur_func_va, f"sub_{cur_func_va:08X}")
            if cur_func_va not in written_names:
                fout.write(f"\n; ----------------------------------------\n")
                fout.write(f"{name}:  ; VA {cur_func_va:#010x}\n")
                written_names.add(cur_func_va)

        bytehex = ' '.join(f'{b:02X}' for b in ins.bytes)
        if len(bytehex) > 32: bytehex = bytehex[:29] + '...'
        anno = annotate_operand(raw, ins, sections, kernel_thunks, func_names, str_cache)
        line = f"  {ins.address:08X}:  {bytehex:32s}  {ins.mnemonic:8s} {ins.op_str}"
        if anno:
            line += f"  {anno}"
        fout.write(line + '\n')

        # Track ret + int3 pad as next function boundary
        if ins.mnemonic in ('ret','retf','iret') and ins.size <= 3:
            next_va = ins.address + ins.size
            next_off = next_va - base
            if next_off < len(data) and data[next_off] == 0xCC:
                # int3 follows; next non-CC is a new function
                pass

        insn_count += 1
        if insn_count % 50000 == 0:
            print(f"  ... {insn_count} insns, at VA {ins.address:#x}", file=sys.stderr)

    fout.write(f"\n; ({insn_count} total instructions in {section['name']})\n")

if __name__ == "__main__":
    print(f"[+] Loading {XBE_PATH}", file=sys.stderr)
    raw, base, ep, sections = load_xbe()
    print(f"    base={base:#x} ep={ep:#x}", file=sys.stderr)

    kernel_thunks, kt_va = build_kernel_thunks(raw, sections)
    print(f"[+] {len(kernel_thunks)} kernel thunks at {kt_va:#x}", file=sys.stderr)

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    md.skipdata = True

    func_names = dict(NAMED_ANCHORS)
    str_cache = {}

    out_path = os.path.join(OUT_DIR, "full_text_disasm.txt")
    print(f"[+] Writing {out_path}", file=sys.stderr)
    with open(out_path, 'w', encoding='utf-8') as fout:
        fout.write("="*80 + "\n")
        fout.write("FULL DISASSEMBLY: Jedi Academy SP retail (XDK 5558)\n")
        fout.write(f"Source: {XBE_PATH}\n")
        fout.write(f"Image base: {base:#x}\n")
        fout.write(f"Entry point: {ep:#x}\n")
        fout.write("="*80 + "\n\n")

        fout.write("ALL SECTIONS\n" + "-"*80 + "\n")
        for s in sections:
            fout.write(f"  {s['name']:12s}  VA={s['va']:#010x}  Size={s['vsz']:#010x}  RawOff={s['roff']:#010x}  Flags={s['flags']:#x}\n")
        fout.write("\n")

        fout.write("KERNEL IMPORT THUNKS\n" + "-"*80 + "\n")
        for tva, name in sorted(kernel_thunks.items()):
            fout.write(f"  {tva:#010x}  {name}\n")
        fout.write("\n")

        fout.write("NAMED ANCHORS\n" + "-"*80 + "\n")
        for va, name in sorted(NAMED_ANCHORS.items()):
            fout.write(f"  {va:#010x}  {name}\n")
        fout.write("\n")

        for sname in DISASM_SECTIONS:
            section = next((s for s in sections if s['name']==sname), None)
            if section is None:
                print(f"[-] Section {sname} not found", file=sys.stderr)
                continue
            print(f"[+] Disassembling {sname} ({section['vsz']:#x} bytes)", file=sys.stderr)
            disasm_section(raw, section, md, kernel_thunks, func_names, str_cache, fout)

    print(f"[+] Done. {out_path}", file=sys.stderr)
    print(f"    File size: {os.path.getsize(out_path):,} bytes", file=sys.stderr)
