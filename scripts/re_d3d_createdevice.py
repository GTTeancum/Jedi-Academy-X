"""
re_d3d_createdevice.py
Extract exact D3DPRESENT_PARAMETERS from the original Xbox JA XBE.

Strategy:
  1. Load all sections, build VA→file offset map
  2. Scan every CALL in .text that lands in the D3D section VA range
  3. For each such call, examine the 30 preceding instructions
  4. Score each call by how many classic CreateDevice push-values appear:
       0 = D3DADAPTER_DEFAULT, 1 = D3DDEVTYPE_HAL, 0x40/0x80/0x44 = BehaviorFlags
  5. Identify the highest-scoring hit as CreateDevice
  6. Walk backwards from that call collecting MOV [reg+disp], imm writes
     to reconstruct the D3DPRESENT_PARAMETERS struct
  7. Also scan deeper: if CreateDevice is called via a wrapper, trace one level in

Bonus: also find Direct3D_SetPushBufferSize call (two large pow2 immediates).

Usage:
  python re_d3d_createdevice.py "path\\to\\default.xbe"
"""

import sys, struct, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
from capstone import *
from capstone.x86 import *

XBE_EP_RETAIL_KEY = 0xA8FC57AB

# Xbox D3DFMT values from d3d8types.h XDK 5849
D3DFMT = {
    0x00: 'D3DFMT_UNKNOWN(0)',
    0x01: 'D3DFMT_A8R8G8B8',        # tiled
    0x02: 'D3DFMT_X8R8G8B8',        # tiled
    0x03: 'D3DFMT_R5G6B5',          # tiled
    0x04: 'D3DFMT_R5G6B5(4)',
    0x05: 'D3DFMT_X8R8G8B8(5)',
    0x06: 'D3DFMT_A8R8G8B8(6)',
    0x0B: 'D3DFMT_A8',
    0x10: 'D3DFMT_A1R5G5B5',
    0x11: 'D3DFMT_X1R5G5B5',
    0x12: 'D3DFMT_A4R4G4B4',
    0x17: 'D3DFMT_LIN_A8R8G8B8',
    0x18: 'D3DFMT_LIN_X8R8G8B8',
    0x1B: 'D3DFMT_LIN_X8R8G8B8(1B)',
    0x1C: 'D3DFMT_LIN_A8R8G8B8(1C)',
    0x2E: 'D3DFMT_LIN_D24S8',
    0x30: 'D3DFMT_LIN_F16',
    0x3E: 'D3DFMT_LIN_D16',
    0x70: 'D3DFMT_D24S8',           # tiled depth
    0x6E: 'D3DFMT_D16',             # tiled depth
    0x75: 'D3DFMT_D24X8',           # tiled depth
    # common PC values that might appear
    0x15: 'D3DFMT_R8G8B8',
    0x16: 'D3DFMT_A8R8G8B8(PC)',
    0x19: 'D3DFMT_X8R8G8B8(PC)',
    0x23: 'D3DFMT_D32',
    0x4F: 'D3DFMT_D16_LOCKABLE',
    0x55: 'D3DFMT_D15S1',
    0x56: 'D3DFMT_D24S8(PC)',
    0x58: 'D3DFMT_D24X8(PC)',
    0x59: 'D3DFMT_D24X4S4',
}
D3DSWAP   = {0:'DISCARD',1:'FLIP',2:'COPY',3:'COPY_VSYNC'}
D3DMS     = {0:'NONE',1:'2X_LINEAR',2:'2X_SS_H',3:'2X_SS_V',4:'4X'}
D3DBH_MAP = {0x04:'PUREDEVICE', 0x40:'HARDWARE_VP', 0x80:'SOFTWARE_VP',
             0x200:'MIXED_VP', 0x44:'PURE+HW_VP'}

PP_FIELDS = {
    0x00: 'BackBufferWidth',
    0x04: 'BackBufferHeight',
    0x08: 'BackBufferFormat',
    0x0C: 'BackBufferCount',
    0x10: 'MultiSampleType',
    0x14: 'SwapEffect',
    0x18: 'hDeviceWindow',
    0x1C: 'Windowed',
    0x20: 'EnableAutoDepthStencil',
    0x24: 'AutoDepthStencilFormat',
    0x28: 'Flags',
    0x2C: 'FullScreen_RefreshRateInHz',
    0x30: 'FullScreen_PresentationInterval',
}

def fmt_field(name, val):
    if 'Format' in name:
        return D3DFMT.get(val, f'0x{val:02X}')
    if name == 'SwapEffect':
        return D3DSWAP.get(val, f'0x{val:X}')
    if name == 'MultiSampleType':
        return D3DMS.get(val, f'0x{val:X}')
    if name in ('Windowed','EnableAutoDepthStencil'):
        return 'TRUE' if val else 'FALSE'
    if name == 'FullScreen_PresentationInterval':
        if val == 0:    return 'D3DPRESENT_INTERVAL_DEFAULT(0)'
        if val == 1:    return 'D3DPRESENT_INTERVAL_ONE(1)'
        if val == 2:    return 'D3DPRESENT_INTERVAL_TWO(2)'
        if val == 0x80000000: return 'D3DPRESENT_INTERVAL_IMMEDIATE'
        return f'0x{val:08X}'
    return f'{val}  (0x{val:08X})'

# ── XBE loader ────────────────────────────────────────────────────────────────
def load_xbe(path):
    with open(path, 'rb') as f:
        raw = f.read()
    assert struct.unpack_from('<I', raw, 0)[0] == 0x48454258, "Not an XBE"
    base       = struct.unpack_from('<I', raw, 0x104)[0]
    ep_enc     = struct.unpack_from('<I', raw, 0x128)[0]
    sec_count  = struct.unpack_from('<I', raw, 0x11C)[0]
    sec_hdr_va = struct.unpack_from('<I', raw, 0x120)[0]
    ep = (ep_enc ^ XBE_EP_RETAIL_KEY) & 0xFFFFFFFF

    sections = []
    hdr_off  = sec_hdr_va - base
    for i in range(sec_count):
        o   = hdr_off + i * 0x38
        va  = struct.unpack_from('<I', raw, o + 0x04)[0]
        vsz = struct.unpack_from('<I', raw, o + 0x08)[0]
        ro  = struct.unpack_from('<I', raw, o + 0x0C)[0]
        rsz = struct.unpack_from('<I', raw, o + 0x10)[0]
        nva = struct.unpack_from('<I', raw, o + 0x14)[0]
        nof = nva - base
        name = b''
        if 0 <= nof < len(raw):
            end  = raw.find(b'\x00', nof, nof+32)
            name = raw[nof : end if end!=-1 else nof+16]
        sections.append({'name': name.decode('ascii','replace').strip(),
                         'va':va,'vsz':vsz,'roff':ro,'rsz':rsz})

    def va2file(va):
        for s in sections:
            if s['va'] <= va < s['va'] + s['vsz']:
                return s['roff'] + (va - s['va'])
        return None

    return raw, base, sections, va2file, ep

# ── Disassembler helpers ───────────────────────────────────────────────────────
def disasm(raw, va2file, start_va, n=300):
    off = va2file(start_va)
    if off is None: return []
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    chunk = raw[off: off + n*15]
    return list(md.disasm(chunk, start_va))[:n]

def print_insns(insns, mark=None):
    for ins in insns:
        tag = '>>>' if mark and ins.address == mark else '   '
        print(f'  {tag} 0x{ins.address:08X}:  {ins.mnemonic:<8s} {ins.op_str}')

# ── Scan .text for calls into a VA range ─────────────────────────────────────
def scan_calls_into_range(raw, text_sec, va_lo, va_hi):
    """Return list of (call_va, target_va) for direct CALLs landing in [va_lo, va_hi)."""
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    chunk = raw[text_sec['roff']: text_sec['roff'] + text_sec['rsz']]
    hits = []
    for ins in md.disasm(chunk, text_sec['va']):
        if ins.id == X86_INS_CALL and ins.operands:
            op = ins.operands[0]
            if op.type == X86_OP_IMM:
                tgt = op.imm & 0xFFFFFFFF
                if va_lo <= tgt < va_hi:
                    hits.append((ins.address, tgt))
    return hits

# ── Score a call site as CreateDevice ────────────────────────────────────────
def score_createdevice(context_insns):
    """
    Score how likely this call is CreateDevice.
    Push values that appear in a true CreateDevice call:
      0   = D3DADAPTER_DEFAULT (Adapter)
      1   = D3DDEVTYPE_HAL (DeviceType)
      0   = hFocusWindow NULL
      0x40 or 0x80 = BehaviorFlags
    Returns (score, pushes_list)
    """
    pushes = []
    for ins in context_insns:
        if ins.id == X86_INS_PUSH:
            for op in ins.operands:
                if op.type == X86_OP_IMM:
                    pushes.append(op.imm & 0xFFFFFFFF)
    score = 0
    if 0 in pushes: score += 1
    if 1 in pushes: score += 2   # HAL device type is strong signal
    vals = set(pushes)
    for bh in (0x40, 0x80, 0x04, 0x44, 0x200):
        if bh in vals: score += 3
    # 6 pushes = strong match for 6-arg CreateDevice
    if len(pushes) >= 5: score += 2
    return score, pushes

# ── Extract D3DPRESENT_PARAMETERS from MOV writes before call ─────────────────
def extract_pp(context_insns):
    """
    Collect all MOV [reg+disp], imm in the context window.
    Returns dict: disp -> (insn_va, imm_val)
    We try to figure out the base offset by looking for a LEA that loads
    the address of the struct (e.g.  lea eax, [ebp-0x28]).
    """
    # First pass: collect all [reg+disp] = imm
    raw_writes = {}   # (reg_id, disp) -> (va, val)
    for ins in context_insns:
        if ins.id != X86_INS_MOV: continue
        ops = ins.operands
        if len(ops) != 2: continue
        dst, src = ops[0], ops[1]
        if dst.type != X86_OP_MEM: continue
        if src.type != X86_OP_IMM: continue
        raw_writes[(dst.mem.base, dst.mem.disp)] = (ins.address, src.imm & 0xFFFFFFFF)

    return raw_writes

# ── Walk back from a call to find the D3DPRESENT_PARAMETERS pointer push ──────
def find_pp_base_offset(context_insns):
    """
    Look for LEA rx, [ebp-N] or LEA rx, [esp+N] right before a PUSH rx,
    which would be the D3DPRESENT_PARAMETERS* argument.
    Returns (reg, base_offset_sign, offset) or None.
    """
    for i, ins in enumerate(context_insns):
        if ins.id != X86_INS_LEA: continue
        ops = ins.operands
        if len(ops) != 2: continue
        dst, src = ops[0], ops[1]
        if src.type != X86_OP_MEM: continue
        # Look for the LEA result being pushed
        reg = dst.reg
        for j in range(i+1, min(i+6, len(context_insns))):
            nxt = context_insns[j]
            if nxt.id == X86_INS_PUSH and nxt.operands:
                if nxt.operands[0].type == X86_OP_REG and nxt.operands[0].reg == reg:
                    return (src.mem.base, src.mem.disp, ins.address)
    return None

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    path = sys.argv[1] if len(sys.argv)>1 else r'Star Wars Jedi Academy game\default.xbe'
    print(f"\n{'='*72}")
    print(f"  JA Xbox — D3D CreateDevice Extractor")
    print(f"{'='*72}\n")

    raw, base, sections, va2file, ep = load_xbe(path)
    print(f"Base      : 0x{base:08X}")
    print(f"Entry pt  : 0x{ep:08X}")
    print(f"Sections  : {len(sections)}")
    for s in sections:
        print(f"  {s['name']:16s}  VA=0x{s['va']:08X}  size=0x{s['vsz']:X}")

    # Identify .text and D3D sections
    text_sec = next((s for s in sections if s['name'] == '.text'), None)
    d3d_sec  = next((s for s in sections if 'D3D' in s['name'] and 'X' not in s['name']), None)
    if not text_sec:
        print("ERROR: no .text section found"); return
    if not d3d_sec:
        # fall back: pick section with 'D3D' substring
        d3d_secs = [s for s in sections if 'D3D' in s['name']]
        print(f"D3D sections: {[s['name'] for s in d3d_secs]}")
        d3d_sec = d3d_secs[0] if d3d_secs else None

    if d3d_sec:
        d3d_lo = d3d_sec['va']
        d3d_hi = d3d_sec['va'] + d3d_sec['vsz']
        print(f"\nD3D section : {d3d_sec['name']}  0x{d3d_lo:08X}–0x{d3d_hi:08X}")
    else:
        print("WARNING: no D3D section; will scan all inter-section calls")
        d3d_lo, d3d_hi = 0, 0xFFFFFFFF

    # ── Step 1: Scan all direct calls from .text into D3D section ────────────
    print(f"\n{'─'*72}")
    print(f"Scanning .text for direct CALLs into D3D section...")
    print(f"{'─'*72}")
    d3d_calls = scan_calls_into_range(raw, text_sec, d3d_lo, d3d_hi)
    print(f"Found {len(d3d_calls)} call sites")

    # ── Step 2: Score each call site ─────────────────────────────────────────
    scored = []
    for call_va, tgt_va in d3d_calls:
        ctx = disasm(raw, va2file, call_va - 0x200, n=80)
        # Keep only instructions at or before the call
        ctx = [i for i in ctx if i.address <= call_va]
        ctx_window = ctx[-30:]   # last 30 before call
        sc, pushes = score_createdevice(ctx_window)
        scored.append((sc, call_va, tgt_va, ctx_window, ctx))

    scored.sort(key=lambda x: -x[0])

    print(f"\nTop 10 scored call sites (score, call_va, target_va, pushes):")
    for sc, cva, tva, ctx_w, ctx in scored[:10]:
        bh_names = [D3DBH_MAP.get(p,'') for p in [p for _,p_,p in [(0,0,0)] if False] ]
        pushes_list = []
        for ins in ctx_w:
            if ins.id == X86_INS_PUSH:
                for op in ins.operands:
                    if op.type == X86_OP_IMM:
                        pushes_list.append(op.imm & 0xFFFFFFFF)
        print(f"  score={sc:3d}  call=0x{cva:08X}  target=0x{tva:08X}  "
              f"pushes={[hex(p) for p in pushes_list]}")

    # ── Step 3: Analyze top candidate ────────────────────────────────────────
    if not scored:
        print("No calls found into D3D section!"); return

    best = scored[0]
    sc, call_va, tgt_va, ctx_window, ctx_full = best

    print(f"\n{'─'*72}")
    print(f"BEST CANDIDATE: CreateDevice call @ 0x{call_va:08X}  →  0x{tgt_va:08X}  (score={sc})")
    print(f"{'─'*72}")

    # Print wider context
    print(f"\nContext (40 insns before call + call):")
    wider = disasm(raw, va2file, call_va - 0x300, n=100)
    wider = [i for i in wider if call_va - 0x200 <= i.address <= call_va]
    print_insns(wider, mark=call_va)

    # Extract D3DPRESENT_PARAMETERS writes
    print(f"\n{'─'*72}")
    print("D3DPRESENT_PARAMETERS reconstruction:")
    print(f"{'─'*72}")

    raw_writes = extract_pp(wider)
    if raw_writes:
        print(f"  All MOV [reg+disp], imm writes in context:")
        for (reg, disp), (va_ins, val) in sorted(raw_writes.items(), key=lambda x: x[0][1]):
            reg_name = {X86_REG_ESP:'esp', X86_REG_EBP:'ebp', X86_REG_EAX:'eax',
                        X86_REG_ECX:'ecx', X86_REG_EDX:'edx', X86_REG_EBX:'ebx',
                        X86_REG_ESI:'esi', X86_REG_EDI:'edi'}.get(reg, f'r{reg}')
            fname = PP_FIELDS.get(disp, f'+0x{disp:02X}')
            decoded = fmt_field(fname, val) if disp in PP_FIELDS else f'0x{val:08X}'
            print(f"    0x{va_ins:08X}: [{reg_name}+0x{disp:02X}] = {decoded:32s}  (raw=0x{val:08X})")
    else:
        print("  No MOV [reg+disp], imm writes found in context window.")
        print("  Struct may be built further back or passed by value from another function.")

    # ── Step 4: Also try to find push-buffer sizes near entry point ──────────
    print(f"\n{'─'*72}")
    print(f"Searching for Direct3D_SetPushBufferSize near entry point 0x{ep:08X}...")
    print(f"{'─'*72}")
    ep_insns = disasm(raw, va2file, ep, n=200)
    pb_hits = []
    for i in range(2, len(ep_insns)):
        ins = ep_insns[i]
        if ins.id != X86_INS_CALL: continue
        p2, p1 = ep_insns[i-2], ep_insns[i-1]
        if p2.id == X86_INS_PUSH and p1.id == X86_INS_PUSH:
            ops2 = [o.imm & 0xFFFFFFFF for o in p2.operands if o.type==X86_OP_IMM]
            ops1 = [o.imm & 0xFFFFFFFF for o in p1.operands if o.type==X86_OP_IMM]
            if ops2 and ops1:
                s1, s2 = ops2[0], ops1[0]
                if 0x8000 <= s1 <= 0x10000000 and 0x8000 <= s2 <= 0x10000000:
                    pb_hits.append((ins, s1, s2))
    if pb_hits:
        ins, s1, s2 = pb_hits[0]
        print(f"  SetPushBufferSize @ 0x{ins.address:08X}")
        print(f"    Arg1 (CommandBuffer): 0x{s1:08X} = {s1//1024} KB")
        print(f"    Arg2 (FixupBuffer)  : 0x{s2:08X} = {s2//1024} KB")
    else:
        # Check level-1 callees
        direct_calls = [ins for ins in ep_insns if ins.id==X86_INS_CALL
                        and ins.operands and ins.operands[0].type==X86_OP_IMM]
        for ci in direct_calls[:8]:
            sub = disasm(raw, va2file, ci.operands[0].imm, n=200)
            for i in range(2, len(sub)):
                ins = sub[i]
                if ins.id != X86_INS_CALL: continue
                p2, p1 = sub[i-2], sub[i-1]
                if p2.id == X86_INS_PUSH and p1.id == X86_INS_PUSH:
                    ops2 = [o.imm&0xFFFFFFFF for o in p2.operands if o.type==X86_OP_IMM]
                    ops1 = [o.imm&0xFFFFFFFF for o in p1.operands if o.type==X86_OP_IMM]
                    if ops2 and ops1:
                        s1, s2 = ops2[0], ops1[0]
                        if 0x8000 <= s1 <= 0x10000000 and 0x8000 <= s2 <= 0x10000000:
                            print(f"  SetPushBufferSize @ 0x{ins.address:08X}  "
                                  f"(in sub @ 0x{ci.operands[0].imm:08X})")
                            print(f"    Arg1: 0x{s1:08X} = {s1//1024} KB")
                            print(f"    Arg2: 0x{s2:08X} = {s2//1024} KB")
                            pb_hits.append((ins, s1, s2))
    if not pb_hits:
        print("  Not found in entry or level-1 functions.")

    # ── Step 5: Also scan for D3DFMT patterns directly in .text section ──────
    print(f"\n{'─'*72}")
    print("Scanning .text for known D3DFMT values in MOV imm writes (struct init):")
    print(f"{'─'*72}")
    fmt_target_vals = set(D3DFMT.keys()) - {0}
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    chunk = raw[text_sec['roff']: text_sec['roff'] + text_sec['rsz']]
    fmt_hits = []
    for ins in md.disasm(chunk, text_sec['va']):
        if ins.id == X86_INS_MOV and len(ins.operands)==2:
            dst, src = ins.operands
            if dst.type == X86_OP_MEM and src.type == X86_OP_IMM:
                val = src.imm & 0xFFFFFFFF
                if val in fmt_target_vals and val <= 0x80:
                    fmt_hits.append((ins.address, dst.mem.disp, val, D3DFMT[val]))
    print(f"  Found {len(fmt_hits)} D3DFMT writes")
    for va_ins, disp, val, name in fmt_hits[:20]:
        print(f"  0x{va_ins:08X}: [reg+0x{disp:02X}] = 0x{val:02X} ({name})")

    # ── Step 6: Print all top-5 candidates with full context ─────────────────
    print(f"\n{'─'*72}")
    print("Top 5 CreateDevice candidates — full context:")
    print(f"{'─'*72}")
    for rank, (sc, call_va, tgt_va, ctx_w, ctx_f) in enumerate(scored[:5]):
        print(f"\n  ── Rank {rank+1}: score={sc}  call=0x{call_va:08X}  target=0x{tgt_va:08X} ──")
        wider2 = disasm(raw, va2file, call_va - 0x200, n=80)
        wider2 = [i for i in wider2 if call_va - 0x150 <= i.address <= call_va]
        print_insns(wider2, mark=call_va)

    print(f"\n{'='*72}")
    print("Done.")

if __name__ == '__main__':
    main()
