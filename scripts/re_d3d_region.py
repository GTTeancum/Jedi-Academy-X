"""
re_d3d_region.py
Disassemble the D3D init region of the original JA Xbox XBE.
The 0x11000-0x11400 area contains the D3D setup functions.
We need to find:
  1. The D3DPRESENT_PARAMETERS struct construction
  2. The CreateDevice call (possibly vtable/indirect)
  3. The SetPushBufferSize call

Usage: python re_d3d_region.py "path\\to\\default.xbe"
"""

import sys, struct, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
from capstone import *
from capstone.x86 import *

XBE_EP_RETAIL_KEY = 0xA8FC57AB

D3DFMT_XBOX = {
    # XDK 5849 d3d8types.h Xbox-specific format codes
    # From actual XDK header — tiled formats use small values
    # Linear formats use D3DFMT_LIN_* prefix
    0x00: 'D3DFMT_UNKNOWN',
    0x01: 'D3DFMT_A8R8G8B8',         # TILED
    0x02: 'D3DFMT_X8R8G8B8',         # TILED
    0x03: 'D3DFMT_R5G6B5',           # TILED
    0x04: 'D3DFMT_R5G5B5',           # TILED
    0x05: 'D3DFMT_X1R5G5B5',         # TILED
    0x06: 'D3DFMT_A1R5G5B5',         # TILED
    0x07: 'D3DFMT_A4R4G4B4',         # TILED
    0x0B: 'D3DFMT_P8',               # TILED
    0x11: 'D3DFMT_DXT1',
    0x12: 'D3DFMT_DXT2',
    0x14: 'D3DFMT_DXT4',
    0x17: 'D3DFMT_LIN_A8R8G8B8',
    0x18: 'D3DFMT_LIN_X8R8G8B8',
    0x19: 'D3DFMT_LIN_R5G6B5',
    0x1A: 'D3DFMT_LIN_R5G5B5',
    0x1B: 'D3DFMT_LIN_X1R5G5B5',
    0x1C: 'D3DFMT_LIN_A1R5G5B5',
    0x1D: 'D3DFMT_LIN_A4R4G4B4',
    0x1E: 'D3DFMT_LIN_A8',
    0x1F: 'D3DFMT_LIN_L8',
    0x2E: 'D3DFMT_LIN_D24S8',
    0x2F: 'D3DFMT_LIN_F24S8',
    0x3C: 'D3DFMT_LIN_D16',
    0x3D: 'D3DFMT_LIN_F16',
    # Tiled depth formats
    0x70: 'D3DFMT_D24S8',            # TILED
    0x6E: 'D3DFMT_D16',              # TILED
    0x75: 'D3DFMT_D24X8',            # TILED
}

D3DSWAP   = {0:'DISCARD',1:'FLIP',2:'COPY',3:'COPY_VSYNC'}
D3DMS     = {0:'NONE',1:'2X_LINEAR',2:'2X_SS_H',3:'2X_SS_V',4:'4X'}
D3DBH     = {0x04:'PUREDEVICE', 0x40:'HARDWARE_VP', 0x80:'SOFTWARE_VP',
             0x200:'MIXED_VP', 0x44:'PURE+HW_VP', 0xC0:'HW+SW_VP'}

PP_LAYOUT = [
    (0x00,'BackBufferWidth'),
    (0x04,'BackBufferHeight'),
    (0x08,'BackBufferFormat'),
    (0x0C,'BackBufferCount'),
    (0x10,'MultiSampleType'),
    (0x14,'SwapEffect'),
    (0x18,'hDeviceWindow'),
    (0x1C,'Windowed'),
    (0x20,'EnableAutoDepthStencil'),
    (0x24,'AutoDepthStencilFormat'),
    (0x28,'Flags'),
    (0x2C,'FullScreen_RefreshRateInHz'),
    (0x30,'FullScreen_PresentationInterval'),
]

def load_xbe(path):
    with open(path,'rb') as f: raw=f.read()
    assert struct.unpack_from('<I',raw,0)[0]==0x48454258
    base       = struct.unpack_from('<I',raw,0x104)[0]
    ep_enc     = struct.unpack_from('<I',raw,0x128)[0]
    sec_count  = struct.unpack_from('<I',raw,0x11C)[0]
    sec_hdr_va = struct.unpack_from('<I',raw,0x120)[0]
    ep = (ep_enc ^ XBE_EP_RETAIL_KEY) & 0xFFFFFFFF
    sections=[]
    hdr_off = sec_hdr_va - base
    for i in range(sec_count):
        o=hdr_off+i*0x38
        va=struct.unpack_from('<I',raw,o+0x04)[0]
        vsz=struct.unpack_from('<I',raw,o+0x08)[0]
        ro=struct.unpack_from('<I',raw,o+0x0C)[0]
        rsz=struct.unpack_from('<I',raw,o+0x10)[0]
        nva=struct.unpack_from('<I',raw,o+0x14)[0]
        nof=nva-base
        name=b''
        if 0<=nof<len(raw):
            end=raw.find(b'\x00',nof,nof+32)
            name=raw[nof:end if end!=-1 else nof+16]
        sections.append({'name':name.decode('ascii','replace').strip(),
                         'va':va,'vsz':vsz,'roff':ro,'rsz':rsz})
    def va2file(va):
        for s in sections:
            if s['va']<=va<s['va']+s['vsz']:
                return s['roff']+(va-s['va'])
        return None
    return raw,base,sections,va2file,ep

def disasm_range(raw, va2file, start_va, end_va):
    off = va2file(start_va)
    if off is None: return []
    size = end_va - start_va
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    return list(md.disasm(raw[off:off+size+15], start_va))

def decode_val(name, val):
    if 'Format' in name:
        return D3DFMT_XBOX.get(val, f'0x{val:02X}')
    if name == 'SwapEffect':     return D3DSWAP.get(val, f'0x{val:X}')
    if name == 'MultiSampleType': return D3DMS.get(val, f'0x{val:X}')
    if name in ('Windowed','EnableAutoDepthStencil'): return 'TRUE' if val else 'FALSE'
    if name == 'FullScreen_PresentationInterval':
        if val==0:           return 'D3DPRESENT_INTERVAL_DEFAULT'
        if val==1:           return 'D3DPRESENT_INTERVAL_ONE'
        if val==2:           return 'D3DPRESENT_INTERVAL_TWO'
        if val==0x80000000:  return 'D3DPRESENT_INTERVAL_IMMEDIATE'
        return f'0x{val:08X}'
    if name == 'FullScreen_RefreshRateInHz':
        return f'{val} Hz (0=default)'
    return f'{val}  (0x{val:08X})'

REG_NAME = {
    X86_REG_EAX:'eax', X86_REG_EBX:'ebx', X86_REG_ECX:'ecx', X86_REG_EDX:'edx',
    X86_REG_ESP:'esp', X86_REG_EBP:'ebp', X86_REG_ESI:'esi', X86_REG_EDI:'edi',
}

def main():
    path = sys.argv[1] if len(sys.argv)>1 else r'Star Wars Jedi Academy game\default.xbe'
    raw, base, sections, va2file, ep = load_xbe(path)

    text_sec = next(s for s in sections if s['name']=='.text')
    d3d_sec  = next(s for s in sections if s['name']=='D3D')
    d3d_lo   = d3d_sec['va']
    d3d_hi   = d3d_sec['va'] + d3d_sec['vsz']

    print(f"Entry point : 0x{ep:08X}")
    print(f"D3D section : 0x{d3d_lo:08X}–0x{d3d_hi:08X}")
    print()

    # Disassemble 0x11000–0x11500 — the D3D init cluster
    scan_start = 0x11000
    scan_end   = 0x11500
    insns = disasm_range(raw, va2file, scan_start, scan_end)

    print(f"{'='*72}")
    print(f"Full disassembly 0x{scan_start:08X}–0x{scan_end:08X}:")
    print(f"{'='*72}")
    for ins in insns:
        # Annotate interesting things
        note = ''
        if ins.operands:
            ops = ins.operands
            # Direct call to D3D section
            if ins.id == X86_INS_CALL and ops[0].type==X86_OP_IMM:
                tgt = ops[0].imm & 0xFFFFFFFF
                if d3d_lo <= tgt < d3d_hi:
                    note = f'  ← D3D call to 0x{tgt:08X}'
                elif text_sec['va'] <= tgt < text_sec['va']+text_sec['vsz']:
                    note = f'  ← text call'
            # Indirect call (vtable)
            if ins.id == X86_INS_CALL and ops[0].type==X86_OP_MEM:
                note = '  ← INDIRECT CALL (vtable?)'
            # MOV mem, imm
            if ins.id == X86_INS_MOV and len(ops)==2:
                dst, src = ops
                if dst.type==X86_OP_MEM and src.type==X86_OP_IMM:
                    val = src.imm & 0xFFFFFFFF
                    if val in (640, 480, 720, 0x280, 0x1E0):
                        note = f'  ← W/H={val}'
                    elif val in D3DFMT_XBOX:
                        note = f'  ← D3DFMT={D3DFMT_XBOX[val]}'
                    elif val in D3DSWAP.values():
                        pass
                    disp = dst.mem.disp
                    for off, fname in PP_LAYOUT:
                        if disp == off and val in D3DFMT_XBOX:
                            note = f'  ← PP.{fname}={D3DFMT_XBOX[val]}'
        print(f'  0x{ins.address:08X}:  {ins.mnemonic:<8s} {ins.op_str}{note}')

    # Now look for all CALLs in this region (direct + indirect)
    print(f"\n{'='*72}")
    print(f"All CALLs in 0x{scan_start:08X}–0x{scan_end:08X}:")
    print(f"{'='*72}")
    for ins in insns:
        if ins.id != X86_INS_CALL: continue
        if ins.operands and ins.operands[0].type == X86_OP_IMM:
            tgt = ins.operands[0].imm & 0xFFFFFFFF
            kind = 'D3D' if d3d_lo<=tgt<d3d_hi else 'text' if text_sec['va']<=tgt<text_sec['va']+text_sec['vsz'] else 'other'
            print(f'  0x{ins.address:08X}: call  0x{tgt:08X}  [{kind}]')
        elif ins.operands and ins.operands[0].type == X86_OP_MEM:
            op = ins.operands[0]
            reg = REG_NAME.get(op.mem.base,'?')
            disp = op.mem.disp
            print(f'  0x{ins.address:08X}: call  [{reg}+0x{disp:X}]  [INDIRECT/vtable]')
        else:
            print(f'  0x{ins.address:08X}: call  {ins.op_str}  [?]')

    # Also scan for MOV-writes that look like D3DPRESENT_PARAMETERS
    print(f"\n{'='*72}")
    print(f"D3DPRESENT_PARAMETERS candidates (all MOV [reg+disp], imm in region):")
    print(f"{'='*72}")
    pp_fields_off = {off for off,_ in PP_LAYOUT}
    # Collect by function (group by nearby addresses)
    pp_writes = []
    for ins in insns:
        if ins.id != X86_INS_MOV: continue
        ops = ins.operands
        if len(ops) != 2: continue
        dst, src = ops
        if dst.type != X86_OP_MEM: continue
        if src.type != X86_OP_IMM: continue
        val = src.imm & 0xFFFFFFFF
        disp = dst.mem.disp
        reg = REG_NAME.get(dst.mem.base, f'r{dst.mem.base}')
        pp_writes.append((ins.address, reg, disp, val))

    print(f"  Total MOV [reg+disp], imm writes: {len(pp_writes)}")
    for addr, reg, disp, val in pp_writes:
        fname = dict(PP_LAYOUT).get(disp, f'+0x{disp:02X}')
        decoded = decode_val(fname, val) if disp in dict(PP_LAYOUT) else f'0x{val:08X}'
        in_pp = '*** PP ***' if disp in pp_fields_off else ''
        print(f'  0x{addr:08X}: [{reg}+0x{disp:02X}] = {val:10} (0x{val:08X})  {decoded}  {in_pp}')

    # Try to reconstruct the D3DPRESENT_PARAMETERS struct
    # Look for the densest cluster of writes to offsets 0x00..0x30
    # within 0x200 bytes of each other
    print(f"\n{'='*72}")
    print(f"D3DPRESENT_PARAMETERS struct reconstruction:")
    print(f"{'='*72}")

    # Group writes that are within 0x200 bytes of the first write to offset 0x00
    # But since the struct is on the stack (ebp-relative), we need to know the base
    # Find all writes where the same register is used at multiple PP offsets
    from collections import defaultdict
    reg_disp_map = defaultdict(list)  # reg -> [(disp, addr, val)]
    for addr, reg, disp, val in pp_writes:
        reg_disp_map[reg].append((disp, addr, val))

    # For each register, check if we have writes at multiple PP field offsets
    for reg, writes in reg_disp_map.items():
        disps = [d for d,_,_ in writes]
        pp_hits = [d for d in disps if d in pp_fields_off]
        if len(pp_hits) >= 2:
            print(f"\n  Register '{reg}' has {len(pp_hits)} writes to PP field offsets:")
            base_off = min(pp_hits)
            # The struct pointer is [reg + base_off] → base_off = 0 for the struct
            for off_pp, fname in PP_LAYOUT:
                # Find writes to (off_pp + base_off) or (off_pp - base_off)
                candidates = [(a,v) for d,a,v in writes if d == off_pp or d == off_pp + base_off]
                if candidates:
                    addr_c, val_c = candidates[0]
                    print(f'    0x{addr_c:08X}: {fname:32s} = {decode_val(fname, val_c)}  (raw=0x{val_c:08X})')
                else:
                    print(f'    {"??":10s}  {fname:32s} = ???')

    print(f"\n{'='*72}")
    print("Done.")

if __name__ == '__main__':
    main()
