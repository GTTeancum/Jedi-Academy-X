"""
re_d3d_extract.py
Extract the exact D3DPRESENT_PARAMETERS from the CreateDevice call at 0xA44CB.
Also trace SetPushBufferSize and post-CreateDevice calls.
"""

import sys, struct, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
from capstone import *
from capstone.x86 import *

XBE_EP_RETAIL_KEY = 0xA8FC57AB

D3DFMT_XBOX = {
    0x01:'D3DFMT_A8R8G8B8(tiled)',0x02:'D3DFMT_X8R8G8B8(tiled)',
    0x03:'D3DFMT_R5G6B5(tiled)',0x05:'D3DFMT_X1R5G5B5(tiled)',
    0x06:'D3DFMT_A1R5G5B5(tiled)',
    0x17:'D3DFMT_LIN_A8R8G8B8',0x18:'D3DFMT_LIN_X8R8G8B8',
    0x19:'D3DFMT_LIN_R5G6B5',0x2E:'D3DFMT_LIN_D24S8',
    0x70:'D3DFMT_D24S8(tiled)',0x6E:'D3DFMT_D16(tiled)',
    0x3C:'D3DFMT_LIN_D16',0x3D:'D3DFMT_LIN_F16',
    0x2F:'D3DFMT_LIN_F24S8',
}
D3DSWAP   = {0:'DISCARD',1:'FLIP',2:'COPY',3:'COPY_VSYNC'}
D3DBH     = {
    0x01:'SOFTWARE_VP',0x04:'PUREDEVICE',
    0x40:'HARDWARE_VP',0x80:'SOFTWARE_VP',
    0x44:'PURE+HW_VP',0xC0:'HW_VP|SW_VP'
}
PP_FIELDS = {
    0x00:'BackBufferWidth',    0x04:'BackBufferHeight',
    0x08:'BackBufferFormat',   0x0C:'BackBufferCount',
    0x10:'MultiSampleType',    0x14:'SwapEffect',
    0x18:'hDeviceWindow',      0x1C:'Windowed',
    0x20:'EnableAutoDepthStencil',0x24:'AutoDepthStencilFormat',
    0x28:'Flags',              0x2C:'RefreshRateInHz',
    0x30:'PresentationInterval',
}

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

REG_NAME={X86_REG_EAX:'eax',X86_REG_EBX:'ebx',X86_REG_ECX:'ecx',
          X86_REG_EDX:'edx',X86_REG_ESP:'esp',X86_REG_EBP:'ebp',
          X86_REG_ESI:'esi',X86_REG_EDI:'edi'}

def disasm(raw, va2file, start_va, n=500):
    off=va2file(start_va)
    if off is None: return []
    md=Cs(CS_ARCH_X86,CS_MODE_32)
    md.detail=True
    return list(md.disasm(raw[off:off+n*15],start_va))[:n]

def print_insns(insns, mark=None, section_of=None):
    for ins in insns:
        tag='>>>' if mark and ins.address==mark else '   '
        notes=[]
        if ins.operands:
            ops=ins.operands
            if ins.id==X86_INS_CALL:
                if ops[0].type==X86_OP_IMM:
                    tgt=ops[0].imm&0xFFFFFFFF
                    if section_of: notes.append(f'→{section_of(tgt)}')
                elif ops[0].type==X86_OP_MEM:
                    notes.append('INDIRECT')
            if ins.id==X86_INS_MOV and len(ops)==2:
                d,s=ops
                if d.type==X86_OP_MEM and s.type==X86_OP_IMM:
                    v=s.imm&0xFFFFFFFF
                    disp=d.mem.disp
                    if v in (640,480,720,576,1280): notes.append(f'W/H={v}')
                    if v in D3DFMT_XBOX: notes.append(f'FMT={D3DFMT_XBOX[v]}')
                    if 0<=disp<=0x30 and disp in PP_FIELDS: notes.append(f'PP.{PP_FIELDS[disp]}')
        n2='  # '+','.join(notes) if notes else ''
        print(f'  {tag} 0x{ins.address:08X}:  {ins.mnemonic:<8s} {ins.op_str}{n2}')

def main():
    path=sys.argv[1] if len(sys.argv)>1 else r'Star Wars Jedi Academy game\default.xbe'
    raw,base,sections,va2file,section_of,ep=load_xbe(path)

    d3d_sec=next(s for s in sections if s['name']=='D3D')
    d3d_lo=d3d_sec['va']

    # ── 1. CreateDevice context ───────────────────────────────────────────────
    createdevice_va = 0xA44CB
    print(f"{'='*72}")
    print(f"CreateDevice call at 0x{createdevice_va:08X}")
    print(f"{'='*72}")

    # Disassemble from function start — need to find the function prologue
    # Search backwards for sub esp or push ebp
    ctx = disasm(raw, va2file, createdevice_va - 0x400, n=300)
    # Find the call
    create_idx = next((i for i,ins in enumerate(ctx) if ins.address==createdevice_va), None)
    if create_idx is None:
        print("ERROR: call not found in window")
        return

    print(f"\nFull context (400 bytes before + 200 after CreateDevice):")
    print_insns(ctx[:create_idx+20], mark=createdevice_va, section_of=section_of)

    # ── 2. Reconstruct D3DPRESENT_PARAMETERS ─────────────────────────────────
    print(f"\n{'='*72}")
    print("D3DPRESENT_PARAMETERS reconstruction:")
    print(f"{'='*72}")

    # The CreateDevice call at 0xA44CB has:
    #   push ebx          ← Adapter (last arg pushed = first arg)
    #   push ebp          ← DeviceType
    #   push ebx          ← hFocusWindow
    #   push 0x40         ← BehaviorFlags
    #   push eax          ← pPresentationParameters (lea eax, [esp+0x18])
    #   push edx          ← ppReturnedDeviceInterface (push happens first in asm)
    #
    # Since pPresentationParameters = lea eax, [esp+0x18] AFTER the first push (edx),
    # the D3DPP struct is at ESP_original + 0x14.
    #
    # Writes to [esp+X] before the pushes map to D3DPP as: D3DPP_offset = X - 0x14
    # So:
    #   [esp+0x14] = D3DPP+0x00 = BackBufferWidth
    #   [esp+0x18] = D3DPP+0x04 = BackBufferHeight
    #   [esp+0x1c] = D3DPP+0x08 = BackBufferFormat
    #   ...
    #   [esp+0x38] = D3DPP+0x24 = AutoDepthStencilFormat (= 0x2E seen above)
    #
    # We saw: [esp+0x38] = 0x2E. And [esp+0x28], [esp+0x2c], [esp+0x30], [esp+0x34] = various.
    # We need to find [esp+0x14..0x20] for Width, Height, Format, Count.

    # Collect all [esp+X]/[ebp+X] = imm writes in the window before createdevice
    PP_BASE_OFF = 0x14  # D3DPP starts at [esp+0x14] relative to ESP at time of writes
    esp_writes = {}  # offset_from_pp -> (va, val)
    for ins in ctx[:create_idx]:
        if ins.id != X86_INS_MOV: continue
        ops = ins.operands
        if len(ops) != 2: continue
        d,s = ops
        if d.type != X86_OP_MEM: continue
        if s.type != X86_OP_IMM: continue
        val = s.imm & 0xFFFFFFFF
        disp = d.mem.disp
        reg = d.mem.base
        if reg == X86_REG_ESP:
            pp_off = disp - PP_BASE_OFF
            if 0 <= pp_off <= 0x34:
                esp_writes[pp_off] = (ins.address, val)

    print("\nD3DPRESENT_PARAMETERS (from [esp+X] writes, PP base at [esp+0x14]):")
    print(f"  {'Field':32s} {'Value':30s} RawHex    Insn VA")
    print(f"  {'-'*80}")
    all_ok = True
    for off, fname in sorted(PP_FIELDS.items()):
        if off in esp_writes:
            va_ins, val = esp_writes[off]
            # Decode
            if 'Format' in fname:
                decoded = D3DFMT_XBOX.get(val, f'UNKNOWN(0x{val:02X})')
            elif fname == 'SwapEffect':
                decoded = D3DSWAP.get(val, f'0x{val:X}')
            elif fname in ('Windowed','EnableAutoDepthStencil'):
                decoded = 'TRUE' if val else 'FALSE'
            elif fname == 'PresentationInterval':
                decoded = {0:'DEFAULT',1:'ONE',2:'TWO',0x80000000:'IMMEDIATE'}.get(val,f'0x{val:08X}')
            elif fname == 'RefreshRateInHz':
                decoded = f'{val} Hz (0=default)'
            else:
                decoded = str(val)
            print(f"  {fname:32s} {decoded:30s} 0x{val:08X}  @ 0x{va_ins:08X}")
        else:
            print(f"  {fname:32s} {'(not found as imm write)':30s}")
            if off <= 0x0C:
                all_ok = False

    # ── 3. Find Width/Height — may be loaded from variables ────────────────
    print(f"\n{'='*72}")
    print("Width/Height source — look for MOV reg, imm or MOV [esp+14], reg before createdevice:")
    print(f"{'='*72}")

    # The Width/Height might be loaded from cvar/globals into a register first
    # and then moved to [esp+0x14]/[esp+0x18]
    # Look for [esp+0x14] writes and [esp+0x18] writes
    w_writes = [(ins.address, ins) for ins in ctx[:create_idx]
                if ins.id==X86_INS_MOV and ins.operands and len(ins.operands)==2
                and ins.operands[0].type==X86_OP_MEM
                and ins.operands[0].mem.base==X86_REG_ESP
                and ins.operands[0].mem.disp==0x14]  # BackBufferWidth
    h_writes = [(ins.address, ins) for ins in ctx[:create_idx]
                if ins.id==X86_INS_MOV and ins.operands and len(ins.operands)==2
                and ins.operands[0].type==X86_OP_MEM
                and ins.operands[0].mem.base==X86_REG_ESP
                and ins.operands[0].mem.disp==0x18]  # BackBufferHeight

    for va,ins in w_writes[-3:]:
        print(f"  Width write @ 0x{va:08X}: {ins.mnemonic} {ins.op_str}")
    for va,ins in h_writes[-3:]:
        print(f"  Height write @ 0x{va:08X}: {ins.mnemonic} {ins.op_str}")

    # Also look for [esp+0x1c] = BackBufferFormat
    f_writes = [(ins.address, ins) for ins in ctx[:create_idx]
                if ins.id==X86_INS_MOV and ins.operands and len(ins.operands)==2
                and ins.operands[0].type==X86_OP_MEM
                and ins.operands[0].mem.base==X86_REG_ESP
                and ins.operands[0].mem.disp==0x1c]  # BackBufferFormat
    for va,ins in f_writes[-3:]:
        print(f"  Format write @ 0x{va:08X}: {ins.mnemonic} {ins.op_str}")

    # Also look for [esp+0x20] = BackBufferCount
    c_writes = [(ins.address, ins) for ins in ctx[:create_idx]
                if ins.id==X86_INS_MOV and ins.operands and len(ins.operands)==2
                and ins.operands[0].type==X86_OP_MEM
                and ins.operands[0].mem.base==X86_REG_ESP
                and ins.operands[0].mem.disp==0x20]  # BackBufferCount
    for va,ins in c_writes[-3:]:
        print(f"  Count write @ 0x{va:08X}: {ins.mnemonic} {ins.op_str}")

    # ── 4. Look at ALL [esp+X] writes in the full window ─────────────────
    print(f"\n{'='*72}")
    print("ALL [esp+X]=imm writes in the 0x400-byte window before CreateDevice:")
    print(f"{'='*72}")
    for ins in ctx[:create_idx]:
        if ins.id != X86_INS_MOV: continue
        ops = ins.operands
        if len(ops) != 2: continue
        d,s = ops
        if d.type != X86_OP_MEM: continue
        reg = d.mem.base
        disp = d.mem.disp
        if reg == X86_REG_ESP:
            val_note = ''
            if s.type == X86_OP_IMM:
                val = s.imm & 0xFFFFFFFF
                pp_off = disp - PP_BASE_OFF
                pp_name = PP_FIELDS.get(pp_off, f'PP+0x{pp_off:02X}' if 0<=pp_off<=0x34 else '')
                if val in D3DFMT_XBOX: val_note = f' = {D3DFMT_XBOX[val]}'
                if val in (640,480,720): val_note = f' (W/H={val})'
                if pp_name: val_note += f'  → {pp_name}'
            print(f"  0x{ins.address:08X}: {ins.mnemonic} {ins.op_str}{val_note}")

    # ── 5. What calls D3D section after CreateDevice? ─────────────────────
    print(f"\n{'='*72}")
    print("D3D calls after CreateDevice (next 30 insns after call 0xA44CB):")
    print(f"{'='*72}")
    post_ctx = disasm(raw, va2file, createdevice_va, n=100)
    for ins in post_ctx[:60]:
        if ins.id == X86_INS_CALL and ins.operands and ins.operands[0].type==X86_OP_IMM:
            tgt = ins.operands[0].imm & 0xFFFFFFFF
            if d3d_lo <= tgt < d3d_lo + d3d_sec['vsz']:
                print(f"  0x{ins.address:08X}: call 0x{tgt:08X}  [D3D+0x{tgt-d3d_lo:X}]")

    # ── 6. BehaviorFlags detail ─────────────────────────────────────────────
    print(f"\n{'='*72}")
    print("BehaviorFlags pushed:")
    print(f"{'='*72}")
    for ins in ctx[max(0,create_idx-10):create_idx]:
        if ins.id == X86_INS_PUSH and ins.operands:
            op = ins.operands[0]
            if op.type == X86_OP_IMM:
                v = op.imm & 0xFFFFFFFF
                bh = D3DBH.get(v, f'0x{v:08X}')
                print(f"  0x{ins.address:08X}: push 0x{v:08X}  ({bh})")

    # ── 7. Disassemble D3D function 0x23F570 (CreateDevice) ───────────────
    print(f"\n{'='*72}")
    print("D3D section function 0x23F570 (CreateDevice entry):")
    print(f"{'='*72}")
    fn = disasm(raw, va2file, 0x23F570, n=30)
    print_insns(fn, section_of=section_of)

    # ── 8. Also look at 0x23D560 (called right after CreateDevice) ─────────
    print(f"\n{'='*72}")
    print("D3D section function 0x23D560 (called right after CreateDevice):")
    print(f"{'='*72}")
    fn2 = disasm(raw, va2file, 0x23D560, n=30)
    print_insns(fn2, section_of=section_of)

    print(f"\n{'='*72}")
    print("Done.")

if __name__ == '__main__':
    main()
