"""
re_d3d_deep.py
Deep trace of the D3D init chain: 0x11450–0x11900 + follow 0x29B050.
Also traces the entry-point call chain to find who calls the init wrapper.
"""

import sys, struct, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
from capstone import *
from capstone.x86 import *

XBE_EP_RETAIL_KEY = 0xA8FC57AB

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
    def section_of(va):
        for s in sections:
            if s['va']<=va<s['va']+s['vsz']:
                return s['name']
        return '???'
    return raw,base,sections,va2file,section_of,ep

def disasm_range(raw, va2file, start_va, n=300):
    off = va2file(start_va)
    if off is None: return []
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    chunk = raw[off:off+n*15]
    return list(md.disasm(chunk, start_va))[:n]

REG_NAME = {
    X86_REG_EAX:'eax',X86_REG_EBX:'ebx',X86_REG_ECX:'ecx',X86_REG_EDX:'edx',
    X86_REG_ESP:'esp',X86_REG_EBP:'ebp',X86_REG_ESI:'esi',X86_REG_EDI:'edi',
}

D3DFMT = {
    0x01:'D3DFMT_A8R8G8B8(tiled)',0x02:'D3DFMT_X8R8G8B8(tiled)',
    0x17:'D3DFMT_LIN_A8R8G8B8',0x18:'D3DFMT_LIN_X8R8G8B8',
    0x2E:'D3DFMT_LIN_D24S8',0x70:'D3DFMT_D24S8(tiled)',
    0x6E:'D3DFMT_D16(tiled)',0x3C:'D3DFMT_LIN_D16',
}
PP_LAYOUT = {
    0x00:'BackBufferWidth', 0x04:'BackBufferHeight',
    0x08:'BackBufferFormat', 0x0C:'BackBufferCount',
    0x10:'MultiSampleType', 0x14:'SwapEffect',
    0x18:'hDeviceWindow',   0x1C:'Windowed',
    0x20:'EnableAutoDepthStencil', 0x24:'AutoDepthStencilFormat',
    0x28:'Flags', 0x2C:'RefreshRate', 0x30:'PresentInterval',
}

def print_fn(insns, section_of, mark=None):
    for ins in insns:
        tag = '>>>' if mark and ins.address==mark else '   '
        notes = []
        if ins.operands:
            ops = ins.operands
            if ins.id == X86_INS_CALL:
                if ops[0].type == X86_OP_IMM:
                    tgt = ops[0].imm & 0xFFFFFFFF
                    notes.append(f'→ {section_of(tgt)}')
                elif ops[0].type == X86_OP_MEM:
                    notes.append('INDIRECT/vtable')
            if ins.id == X86_INS_MOV and len(ops)==2:
                dst,src = ops
                if dst.type==X86_OP_MEM and src.type==X86_OP_IMM:
                    val = src.imm & 0xFFFFFFFF
                    disp = dst.mem.disp
                    if val in (640,480,720): notes.append(f'W/H={val}')
                    if val in D3DFMT:       notes.append(f'FMT={D3DFMT[val]}')
                    if 0<=disp<=0x30 and disp in PP_LAYOUT:
                        notes.append(f'PP.{PP_LAYOUT[disp]}={val}')
        note_str = '  # ' + ', '.join(notes) if notes else ''
        print(f'  {tag} 0x{ins.address:08X}:  {ins.mnemonic:<8s} {ins.op_str}{note_str}')

def main():
    path = sys.argv[1] if len(sys.argv)>1 else r'Star Wars Jedi Academy game\default.xbe'
    raw,base,sections,va2file,section_of,ep = load_xbe(path)

    d3d_sec  = next(s for s in sections if s['name']=='D3D')
    d3d_lo   = d3d_sec['va']
    d3d_hi   = d3d_sec['va'] + d3d_sec['vsz']

    # ── 1. Entry point call chain ──────────────────────────────────────────
    print(f"{'='*72}")
    print(f"Entry point chain from 0x{ep:08X}:")
    print(f"{'='*72}")
    ep_insns = disasm_range(raw, va2file, ep, n=80)
    print_fn(ep_insns, section_of)

    # Follow first few direct calls to find the D3D init
    for ins in ep_insns:
        if ins.id == X86_INS_CALL and ins.operands and ins.operands[0].type==X86_OP_IMM:
            tgt = ins.operands[0].imm & 0xFFFFFFFF
            if section_of(tgt) == '.text':
                sub = disasm_range(raw, va2file, tgt, n=80)
                for s in sub:
                    if s.id == X86_INS_CALL and s.operands and s.operands[0].type==X86_OP_IMM:
                        t2 = s.operands[0].imm & 0xFFFFFFFF
                        if 0x11400 <= t2 <= 0x11600:
                            print(f"\n  → found path: EP→0x{tgt:08X}→0x{s.address:08X}→0x{t2:08X}")

    # ── 2. Full disassembly of 0x11450–0x11800 ────────────────────────────
    print(f"\n{'='*72}")
    print(f"D3D init function 0x11450–0x11800:")
    print(f"{'='*72}")
    fn_insns = disasm_range(raw, va2file, 0x11450, n=500)
    print_fn(fn_insns, section_of)

    # ── 3. Disassemble 0x29B050 — suspected CreateDevice wrapper ─────────
    print(f"\n{'='*72}")
    print(f"Function at 0x29B050 (called with push 0x100000 + edi):")
    print(f"{'='*72}")
    fn2 = disasm_range(raw, va2file, 0x29B050, n=300)
    print_fn(fn2, section_of)

    # ── 4. Disassemble 0x29C260 — called with two function pointers ───────
    print(f"\n{'='*72}")
    print(f"Function at 0x29C260 (called with fn ptrs 0x113C0, 0x11400):")
    print(f"{'='*72}")
    fn3 = disasm_range(raw, va2file, 0x29C260, n=200)
    print_fn(fn3, section_of)

    # ── 5. Disassemble around 0x113C0 and 0x11400 (the function-ptr args) ─
    for va,label in [(0x113C0, 'fn-ptr arg 0x113C0'), (0x11400, 'fn-ptr arg 0x11400')]:
        print(f"\n{'='*72}")
        print(f"Function at 0x{va:08X} ({label}):")
        print(f"{'='*72}")
        fn = disasm_range(raw, va2file, va, n=80)
        print_fn(fn, section_of)

    # ── 6. Scan for BIG push-then-call patterns (SetPushBufferSize) ───────
    print(f"\n{'='*72}")
    print("All 'push large_imm / ... / call' patterns (SetPushBufferSize candidates):")
    print(f"{'='*72}")
    all_text = disasm_range(raw, va2file, 0x11000, n=2000)
    for i in range(1, len(all_text)):
        ins = all_text[i]
        if ins.id != X86_INS_CALL: continue
        # look back for pushes of large power-of-2 values
        window = all_text[max(0,i-10):i]
        big_pushes = []
        for w in window:
            if w.id == X86_INS_PUSH and w.operands and w.operands[0].type==X86_OP_IMM:
                v = w.operands[0].imm & 0xFFFFFFFF
                if 0x10000 <= v <= 0x2000000 and (v & (v-1)) == 0:   # power of 2
                    big_pushes.append((w.address, v))
        if big_pushes:
            tgt = ins.operands[0].imm & 0xFFFFFFFF if ins.operands and ins.operands[0].type==X86_OP_IMM else 0
            print(f"  CALL @ 0x{ins.address:08X} → 0x{tgt:08X}  [{section_of(tgt)}]")
            for a,v in big_pushes:
                print(f"    push 0x{v:08X} ({v//1024} KB) @ 0x{a:08X}")

    print(f"\n{'='*72}")
    print("Done.")

if __name__ == '__main__':
    main()
