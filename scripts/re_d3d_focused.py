"""
re_d3d_focused.py
Focused analysis: entry point → D3D setup in original Xbox JA XBE.

Approach:
  1. Decode the XBE entry point (retail-key XOR decrypt)
  2. Disassemble the startup function to find Direct3D_SetPushBufferSize
     and the CreateDevice vtable call
  3. Trace the D3DPRESENT_PARAMETERS as it's built on the stack

Usage: python re_d3d_focused.py "path\to\default.xbe"
"""

import sys, struct, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
from capstone import *
from capstone.x86 import *

# XBE entry-point key (retail XBEs XOR the EP with this)
XBE_EP_RETAIL_KEY = 0xA8FC57AB
XBE_EP_DEBUG_KEY  = 0x94859D4B

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
    0x0000001C: 'D3DFMT_LIN_A8R8G8B8',
    0x0000001B: 'D3DFMT_LIN_X8R8G8B8',
    0x00000016: 'D3DFMT_LIN_R5G6B5',
    0x00000011: 'D3DFMT_LIN_A1R5G5B5',
    0x0000001A: 'D3DFMT_LIN_X1R5G5B5',
}
D3DSWAPEFFECT  = {0:'DISCARD',1:'FLIP',2:'COPY',3:'COPY_VSYNC'}
D3DMULTISAMPLE = {0:'NONE',1:'2x_MS_LINEAR',2:'2x_SS_H',3:'2x_SS_V',4:'4x_MS'}
D3DBEHAVIOR    = {0x04:'PUREDEVICE', 0x40:'HARDWARE_VP', 0x80:'SOFTWARE_VP', 0x200:'MIXED_VP'}

def fmt_name(v):
    return D3DFMT.get(v, f'0x{v:08X}')

# ---------------------------------------------------------------------------
# XBE load
# ---------------------------------------------------------------------------
def load_xbe(path):
    with open(path, 'rb') as f:
        data = f.read()
    assert struct.unpack_from('<I', data, 0)[0] == 0x48454258, "Not an XBE"

    base        = struct.unpack_from('<I', data, 0x104)[0]
    ep_enc      = struct.unpack_from('<I', data, 0x128)[0]
    sec_count   = struct.unpack_from('<I', data, 0x11C)[0]
    sec_hdr_va  = struct.unpack_from('<I', data, 0x120)[0]

    # Decrypt entry point (try retail key first, then debug)
    ep_retail = ep_enc ^ XBE_EP_RETAIL_KEY
    ep_debug  = ep_enc ^ XBE_EP_DEBUG_KEY

    # Parse sections
    sections = []
    hdr_off = sec_hdr_va - base
    for i in range(sec_count):
        off = hdr_off + i * 0x38
        va   = struct.unpack_from('<I', data, off + 0x04)[0]
        vsz  = struct.unpack_from('<I', data, off + 0x08)[0]
        roff = struct.unpack_from('<I', data, off + 0x0C)[0]
        rsz  = struct.unpack_from('<I', data, off + 0x10)[0]
        nva  = struct.unpack_from('<I', data, off + 0x14)[0]
        noff = nva - base
        name = b''
        if 0 <= noff < len(data):
            end  = data.find(b'\x00', noff, noff+32)
            name = data[noff:end if end!=-1 else noff+16]
        sections.append({'name': name.decode('ascii','replace').strip(),
                         'va': va, 'vsz': vsz, 'roff': roff, 'rsz': rsz})

    def va2file(va):
        for s in sections:
            if s['va'] <= va < s['va'] + s['vsz']:
                return s['roff'] + (va - s['va'])
        return None

    def read_at(va, n):
        f = va2file(va)
        return data[f:f+n] if f is not None else None

    return data, base, sections, va2file, read_at, ep_retail, ep_debug

# ---------------------------------------------------------------------------
# Disassemble a function — follow up to `max_insns` instructions
# ---------------------------------------------------------------------------
def disasm_at(data, va2file, start_va, max_insns=300):
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    off = va2file(start_va)
    if off is None:
        return []
    chunk = data[off: off + max_insns * 15]
    return list(md.disasm(chunk, start_va))[:max_insns]

def print_insns(insns, highlight=None, limit=None):
    for ins in (insns[:limit] if limit else insns):
        m = '>>>' if highlight and ins.address == highlight else '   '
        print(f"  {m} 0x{ins.address:08X}:  {ins.mnemonic:8s} {ins.op_str}")

# ---------------------------------------------------------------------------
# Find Direct3D_SetPushBufferSize
# Signature: two DWORD args pushed as immediates before a call.
# The first call in main() that takes two large power-of-2 values
# is almost certainly SetPushBufferSize.
# ---------------------------------------------------------------------------
def find_pushbuffer_call(insns):
    """
    Scan instruction list for: push <size1> / push <size2> / call <fn>
    where sizes are >= 0x8000 (32 KB) and power-of-two-ish.
    Returns (call_insn, size1, size2) or None.
    """
    for i in range(2, len(insns)):
        ins = insns[i]
        if ins.id != X86_INS_CALL:
            continue
        prev2 = insns[i-2]
        prev1 = insns[i-1]
        if prev2.id == X86_INS_PUSH and prev1.id == X86_INS_PUSH:
            ops2 = [o.imm for o in prev2.operands if o.type == X86_OP_IMM]
            ops1 = [o.imm for o in prev1.operands if o.type == X86_OP_IMM]
            if ops2 and ops1:
                s1, s2 = ops2[0] & 0xFFFFFFFF, ops1[0] & 0xFFFFFFFF
                if 0x8000 <= s1 <= 0x4000000 and 0x8000 <= s2 <= 0x4000000:
                    return ins, s1, s2
    return None

# ---------------------------------------------------------------------------
# Find CreateDevice vtable call
# Xbox D3D8 CreateDevice is a COM-style vtable call:
#   mov  eax, [IDirect3D8_ptr]
#   call dword ptr [eax + N]     ; vtable slot for CreateDevice
# OR a direct call to IDirect3D8::CreateDevice if inlined.
#
# The key signature is: args pushed include 0x1 (D3DDEVTYPE_HAL)
# and a BehaviorFlags value (0x40 = HARDWARE_VP).
# Immediately before the call, lea/mov loads the D3DPRESENT_PARAMETERS.
# ---------------------------------------------------------------------------
def find_createdevice(insns):
    """
    Look for CreateDevice call pattern. Returns index into insns list.
    Heuristic: find a CALL preceded by push 0x1 (HAL) within 20 insns.
    """
    for i, ins in enumerate(insns):
        if ins.id not in (X86_INS_CALL, X86_INS_CALL):
            continue
        window = insns[max(0,i-25):i]
        pushes = []
        for w in window:
            if w.id == X86_INS_PUSH:
                for op in w.operands:
                    if op.type == X86_OP_IMM:
                        pushes.append(op.imm & 0xFFFFFFFF)
        # Must push HAL (1) and adapter (0)
        if 1 in pushes and 0 in pushes:
            # Also look for behavior flags
            behavior = [v for v in pushes if v in (0x40,0x80,0x04,0x44,0xC0)]
            if behavior or len(pushes) >= 4:
                return i
    return -1

# ---------------------------------------------------------------------------
# Simulate stack to track D3DPRESENT_PARAMETERS construction
# ---------------------------------------------------------------------------
def extract_present_params_from_stack_build(insns, call_idx, data, va2file):
    """
    Walk backwards from the CreateDevice call and collect MOV/LEA instructions
    that write into [esp+N] or [ebp-N] to reconstruct D3DPRESENT_PARAMETERS.
    The struct layout (Xbox, 0x28 bytes):
      +0x00 BackBufferWidth
      +0x04 BackBufferHeight
      +0x08 BackBufferFormat
      +0x0C BackBufferCount
      +0x10 MultiSampleType
      +0x14 SwapEffect
      +0x18 hDeviceWindow
      +0x1C Windowed
      +0x20 EnableAutoDepthStencil
      +0x24 AutoDepthStencilFormat
    """
    pp = {}
    field_names = {
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
    }

    # Look for: mov dword ptr [esp+N], imm  or  mov dword ptr [ebp+N], imm
    # within the 80 instructions before the call
    window = insns[max(0, call_idx-80):call_idx]
    for ins in reversed(window):
        if ins.id not in (X86_INS_MOV,):
            continue
        ops = ins.operands
        if len(ops) < 2:
            continue
        dst, src = ops[0], ops[1]
        # Destination must be memory [reg+disp]
        if dst.type != X86_OP_MEM:
            continue
        # Source should be immediate
        if src.type == X86_OP_IMM:
            val = src.imm & 0xFFFFFFFF
            disp = dst.mem.disp
            # We care about offsets 0..0x24 from the struct base
            # The struct is usually at [esp+K] or [ebp-K]; we look for
            # small positive displacements from esp or negative from ebp.
            # Just collect all [reg+disp] = imm mappings where |disp|<=0x100
            if dst.mem.base != 0 and abs(disp) <= 0x100:
                pp[disp] = (ins.address, val)

    return pp, field_names, window

# ---------------------------------------------------------------------------
# Directly scan .text for MOV instructions that initialize the struct
# More reliable: look for known field values near CreateDevice
# ---------------------------------------------------------------------------
def scan_createdevice_area(data, va2file, va_start, scan_bytes=0x400):
    """Disassemble a wider area around suspected CreateDevice and dump everything."""
    off = va2file(va_start)
    if off is None:
        return []
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    return list(md.disasm(data[off:off+scan_bytes], va_start))

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    path = sys.argv[1] if len(sys.argv) > 1 else r"Star Wars Jedi Academy game\default.xbe"
    print(f"\n{'='*68}")
    print(f"  Jedi Academy Xbox — Focused D3D Init RE")
    print(f"{'='*68}\n")

    data, base, sections, va2file, read_at, ep_retail, ep_debug = load_xbe(path)

    print(f"Base address : 0x{base:08X}")
    print(f"Entry point  : retail=0x{ep_retail:08X}  debug=0x{ep_debug:08X}")
    print(f"Sections     : {len(sections)}")

    # Pick the entry point (retail for a shipped disc)
    ep = ep_retail
    print(f"\nUsing retail entry point: 0x{ep:08X}")

    # Disassemble from entry point
    print(f"\n{'─'*68}")
    print(f"Entry point disassembly (first 60 insns from 0x{ep:08X}):")
    print(f"{'─'*68}")
    ep_insns = disasm_at(data, va2file, ep, max_insns=60)
    print_insns(ep_insns)

    # Find SetPushBufferSize
    print(f"\n{'─'*68}")
    print("Direct3D_SetPushBufferSize:")
    print(f"{'─'*68}")
    pb = find_pushbuffer_call(ep_insns)
    if pb:
        call_ins, s1, s2 = pb
        print(f"  Call @ 0x{call_ins.address:08X}")
        print(f"  CommandBufferSize : 0x{s1:08X}  ({s1:,} = {s1//1024} KB)")
        print(f"  FixupBufferSize   : 0x{s2:08X}  ({s2:,} = {s2//1024} KB)")
    else:
        print("  Not found in first 60 instructions.")
        print("  (May be called from a sub-function — tracing first CALL)")

    # Find first CALL in entry point and follow it (often WinMain or init fn)
    first_calls = [ins for ins in ep_insns if ins.id == X86_INS_CALL
                   and ins.operands and ins.operands[0].type == X86_OP_IMM]

    print(f"\n{'─'*68}")
    print(f"Following first-level CALLs from entry point:")
    print(f"{'─'*68}")

    # Try the first few calls — one of them will be the D3D init function
    all_insns = list(ep_insns)
    for depth1_call in first_calls[:6]:
        target = depth1_call.operands[0].imm
        sub_insns = disasm_at(data, va2file, target, max_insns=200)
        if not sub_insns:
            continue

        # Does this sub contain SetPushBufferSize?
        pb2 = find_pushbuffer_call(sub_insns)
        cd_idx = find_createdevice(sub_insns)

        if pb2 or cd_idx >= 0:
            print(f"\n  *** D3D INIT FUNCTION @ 0x{target:08X} "
                  f"(called from 0x{depth1_call.address:08X}) ***")
            if pb2:
                call_ins, s1, s2 = pb2
                print(f"\n  Direct3D_SetPushBufferSize:")
                print(f"    CommandBufferSize : 0x{s1:08X}  ({s1//1024} KB)")
                print(f"    FixupBufferSize   : 0x{s2:08X}  ({s2//1024} KB)")

            if cd_idx >= 0:
                print(f"\n  CreateDevice call @ 0x{sub_insns[cd_idx].address:08X}")
                print(f"  Context (surrounding instructions):")
                print_insns(sub_insns, highlight=sub_insns[cd_idx].address,
                            limit=None)

                # Reconstruct D3DPRESENT_PARAMETERS
                pp_raw, field_names, window = extract_present_params_from_stack_build(
                    sub_insns, cd_idx, data, va2file)

                if pp_raw:
                    print(f"\n  D3DPRESENT_PARAMETERS (reconstructed from stack writes):")
                    for disp in sorted(pp_raw.keys()):
                        va_ins, val = pp_raw[disp]
                        fname = field_names.get(disp, f'+0x{disp:02X}')
                        # Decode value
                        if 'Format' in fname:
                            decoded = fmt_name(val)
                        elif fname == 'SwapEffect':
                            decoded = D3DSWAPEFFECT.get(val, hex(val))
                        elif fname == 'MultiSampleType':
                            decoded = D3DMULTISAMPLE.get(val, hex(val))
                        elif fname in ('Windowed','EnableAutoDepthStencil'):
                            decoded = str(bool(val))
                        else:
                            decoded = str(val)
                        print(f"    {fname:32s} = {decoded}  (@ insn 0x{va_ins:08X}, raw=0x{val:08X})")

            # Dump full function
            print(f"\n  Full function @ 0x{target:08X}:")
            print_insns(sub_insns)

    # Also scan the .text section for D3DPRESENT_PARAMETERS-like stack builds
    # by looking for 'mov dword ptr [esp+X], 0x280' or 'mov dword ptr [esp+X], 0x1E0' (640x480)
    print(f"\n{'─'*68}")
    print("Scanning .text for 640x480 / 720x480 backbuffer setup (mov imm to mem):")
    print(f"{'─'*68}")
    text_sec = next((s for s in sections if s['name'] == '.text'), sections[0])
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    chunk = data[text_sec['roff']:text_sec['roff'] + text_sec['rsz']]
    hits = []
    for ins in md.disasm(chunk, text_sec['va']):
        if ins.id == X86_INS_MOV and ins.operands:
            ops = ins.operands
            if (len(ops) == 2 and ops[0].type == X86_OP_MEM
                    and ops[1].type == X86_OP_IMM):
                val = ops[1].imm & 0xFFFFFFFF
                if val in (640, 480, 720, 0x280, 0x1E0, 0x2D0):
                    hits.append(ins)

    print(f"  Found {len(hits)} width/height assignments\n")
    # Group by proximity — hits within 200 bytes of each other are likely same function
    clusters = []
    for h in hits:
        placed = False
        for c in clusters:
            if abs(h.address - c[-1].address) < 300:
                c.append(h)
                placed = True
                break
        if not placed:
            clusters.append([h])

    for c in clusters[:5]:
        print(f"  Cluster @ ~0x{c[0].address:08X}:")
        # Disassemble 0x80 bytes around first hit to see context
        ctx = disasm_at(data, va2file, c[0].address - 0x40, max_insns=60)
        print_insns(ctx)
        print()

    print(f"\n{'='*68}")
    print("Done.")

if __name__ == '__main__':
    main()
