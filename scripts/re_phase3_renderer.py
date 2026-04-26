"""
re_phase3_renderer.py
Phase 3 Step 2 — Renderer (R_*/D3D8) deep-dive.

Anchors from Phase 2:
  R_Init_or_caller  @ 0x17320  (string "Initializing Renderer" xref @ 0x17323)
  GLimp_Init area   @ 0x9838D  (string xref, enclosing fn unclear — scan outward)
  VV_ident_fn       @ 0x9D9C7  (gamma/HDR path)
  R_RenderScene     @ 0x80E90
  CreateDevice_call @ 0xA4245  (GLW_Init / D3D device creation)

Also:
  - AvSetDisplayMode callers (kernel thunk)
  - AvSendTVEncoderOption callers
  - D3D CreateTexture / UpdateTexture patterns
  - Texture cache R_FindImageFile area

Output: scripts/output/phase3_renderer.txt
"""

import sys, struct, os, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
from capstone import *
from capstone.x86 import *

XBE_EP_RETAIL = 0xA8FC57AB
XBE_KT_RETAIL = 0x5B6D40B6

def load_xbe(path):
    with open(path,'rb') as f: raw=f.read()
    assert raw[:4]==b'XBEH'
    base=struct.unpack_from('<I',raw,0x104)[0]
    ep_enc=struct.unpack_from('<I',raw,0x128)[0]
    sec_count=struct.unpack_from('<I',raw,0x11C)[0]
    sec_hdr=struct.unpack_from('<I',raw,0x120)[0]
    ep=(ep_enc^XBE_EP_RETAIL)&0xFFFFFFFF
    sections=[]
    for i in range(sec_count):
        o=(sec_hdr-base)+i*0x38
        va=struct.unpack_from('<I',raw,o+4)[0]
        vsz=struct.unpack_from('<I',raw,o+8)[0]
        roff=struct.unpack_from('<I',raw,o+0xC)[0]
        rsz=struct.unpack_from('<I',raw,o+0x10)[0]
        nva=struct.unpack_from('<I',raw,o+0x14)[0]
        noff=nva-base
        name=b''
        if 0<=noff<len(raw):
            end=raw.find(b'\x00',noff,noff+32)
            name=raw[noff:end if end!=-1 else noff+16]
        sections.append({'name':name.decode('ascii','replace').strip(),
                         'va':va,'vsz':vsz,'roff':roff,'rsz':rsz})

    def va2off(va):
        for s in sections:
            if s['va']<=va<s['va']+s['vsz']:
                return s['roff']+(va-s['va'])
        return None

    def sec_of(va):
        for s in sections:
            if s['va']<=va<s['va']+s['vsz']:
                return s['name']
        return '???'

    def read_str(va, maxlen=128):
        off=va2off(va)
        if off is None: return ''
        end=raw.find(b'\x00',off,off+maxlen)
        b=raw[off:end if end!=-1 else off+maxlen]
        try:
            s=b.decode('latin-1','replace')
            if all(0x20<=ord(c)<0x7F or c in '\r\n\t' for c in s):
                return s
        except: pass
        return ''

    return raw,base,sections,va2off,sec_of,ep,read_str

def build_thunk_map(raw, sections, base):
    kt_enc=struct.unpack_from('<I',raw,0x158)[0]
    kt_va=(kt_enc^XBE_KT_RETAIL)&0xFFFFFFFF
    va2off_s = lambda va: next(
        (s['roff']+(va-s['va']) for s in sections if s['va']<=va<s['va']+s['vsz']), None)
    kt_off=va2off_s(kt_va)
    if kt_off is None: return {}
    NAMES={
        1:'AvGetSavedDataAddress',2:'AvSendTVEncoderOption',3:'AvSetDisplayMode',
        4:'AvSetSavedDataAddress',
        15:'ExAllocatePool',16:'ExAllocatePoolWithTag',18:'ExFreePool',
        98:'KeBugCheck',99:'KeBugCheckEx',
        127:'KeQueryPerformanceCounter',
        164:'MmAllocateContiguousMemory',165:'MmAllocateContiguousMemoryEx',
        170:'MmFreeContiguousMemory',172:'MmGetPhysicalAddress',176:'MmMapIoSpace',
        183:'NtAllocateVirtualMemory',186:'NtClose',
        187:'NtCreateDirectoryObject',189:'NtCreateFile',194:'NtDeleteFile',
        218:'NtReadFile',234:'NtWriteFile',
        252:'PsCreateSystemThread',253:'PsCreateSystemThreadEx',
        325:'XeLoadSection',326:'XeUnloadSection',
        289:'RtlInitializeCriticalSection',275:'RtlEnterCriticalSection',
        292:'RtlLeaveCriticalSection',296:'RtlMoveMemory',318:'RtlZeroMemory',
        50:'HalReturnToFirmware',
    }
    result={}
    i=0
    while kt_off+i*4+4<=len(raw):
        val=struct.unpack_from('<I',raw,kt_off+i*4)[0]
        if val==0: break
        if not(val&0x80000000): break
        ord_=val&0x7FFFFFFF
        slot_va=kt_va+i*4
        result[slot_va]=NAMES.get(ord_,f'ord_{ord_}')
        i+=1
    return result

def find_callers_of(raw, text_sec, thunk_va):
    pattern = b'\xFF\x15' + struct.pack('<I', thunk_va)
    chunk = raw[text_sec['roff'] : text_sec['roff'] + text_sec['rsz']]
    results = []
    pos = 0
    while True:
        idx = chunk.find(pattern, pos)
        if idx == -1: break
        results.append(text_sec['va'] + idx)
        pos = idx + 1
    return results

def find_enclosing_fn(raw, va2off, text_sec, ref_va, lookback=0x800):
    off = va2off(ref_va)
    if off is None: return None
    start = max(off - lookback, text_sec['roff'])
    chunk = raw[start : off + 1]
    for i in range(len(chunk)-3, 0, -1):
        if chunk[i]==0x55 and chunk[i+1]==0x8B and chunk[i+2]==0xEC:
            return text_sec['va'] + (start - text_sec['roff']) + i
        if chunk[i]==0x83 and chunk[i+1]==0xEC:
            return text_sec['va'] + (start - text_sec['roff']) + i
    return None

def disasm_fn(raw, va2off, sec_of, start_va, max_insns=300, known=None):
    off=va2off(start_va)
    if off is None: return []
    md=Cs(CS_ARCH_X86,CS_MODE_32)
    md.detail=True
    chunk=raw[off:off+max_insns*15]
    result=[]
    for ins in md.disasm(chunk,start_va):
        result.append(ins)
        if ins.id in (X86_INS_RET,X86_INS_RETF) and len(result)>2:
            break
        if len(result)>=max_insns:
            break
    return result

def fmt(ins, sec_of, read_str, known=None):
    tag=''
    if ins.id==X86_INS_CALL and ins.operands:
        op=ins.operands[0]
        if op.type==X86_OP_IMM:
            tgt=op.imm&0xFFFFFFFF
            nm=(known or {}).get(tgt,'')
            tag=f'  -> {sec_of(tgt)} 0x{tgt:08X}'+(f' [{nm}]' if nm else '')
        elif op.type==X86_OP_MEM:
            disp=op.mem.disp&0xFFFFFFFF if op.mem.base==0 else None
            if disp:
                nm=(known or {}).get(disp,'?')
                tag=f'  -> [0x{disp:08X}] {nm}'
    if ins.id==X86_INS_PUSH and ins.operands and ins.operands[0].type==X86_OP_IMM:
        imm=ins.operands[0].imm&0xFFFFFFFF
        s=read_str(imm)
        if s and len(s)>3:
            tag=f'  ; "{s[:72]}"'
    return f'  0x{ins.address:08X}:  {ins.mnemonic:<8} {ins.op_str}{tag}'

def search_strings(raw, va2off, sections, patterns):
    """Search for byte patterns in .rdata and .text, return VA list."""
    results = {}
    for s in sections:
        if s['name'] not in ('.text', '.rdata', 'D3D', 'DSOUND'): continue
        chunk = raw[s['roff']:s['roff']+s['rsz']]
        for pat in patterns:
            enc = pat.encode('latin-1')
            pos = 0
            while True:
                idx = chunk.find(enc, pos)
                if idx == -1: break
                va = s['va'] + idx
                results.setdefault(pat, []).append(va)
                pos = idx + 1
    return results

def main():
    xbe_path=sys.argv[1] if len(sys.argv)>1 else r'Star Wars Jedi Academy game\default.xbe'
    script_dir=os.path.dirname(os.path.abspath(__file__))
    out_dir=os.path.join(script_dir,'output')
    os.makedirs(out_dir,exist_ok=True)
    out_path=os.path.join(out_dir,'phase3_renderer.txt')

    raw,base,sections,va2off,sec_of,ep,read_str=load_xbe(xbe_path)
    text_sec=next(s for s in sections if s['name']=='.text')
    thunks=build_thunk_map(raw,sections,base)

    ANCHORS = {
        0x00017320: 'R_Init_area',
        0x00080E90: 'R_RenderScene',
        0x0009838D: 'GLimp_Init_str_xref',
        0x0009D9C7: 'VV_ident_fn',
        0x000A4245: 'GLW_Init_CreateDevice',
    }
    known=dict(ANCHORS)
    known.update(thunks)

    lines=[]
    def H(t):
        lines.append('')
        lines.append('='*72)
        lines.append(t)
        lines.append('='*72)

    def dis(va, label, n=250):
        H(f'{label}  @ 0x{va:08X}')
        insns=disasm_fn(raw,va2off,sec_of,va,n,known)
        if not insns:
            lines.append('  [not found]')
            return insns
        for ins in insns:
            lines.append(fmt(ins,sec_of,read_str,known))
        lines.append(f'  [{len(insns)} insns]')
        return insns

    # ── 1. R_Init area (called from Com_Init @ 0x237F6) ─────────────────
    # 0x17320 is Com_Init's call; real R_Init is inside that function
    H('=== R_Init area (Com_Init calls 0x17320) ===')
    insns = dis(0x17320, 'R_Init_context', n=200)
    # Find all CALLs within it and follow important ones
    callees = set()
    for ins in insns:
        if ins.id == X86_INS_CALL and ins.operands and ins.operands[0].type == X86_OP_IMM:
            tgt = ins.operands[0].imm & 0xFFFFFFFF
            if sec_of(tgt) == '.text':
                callees.add(tgt)
    H(f'Callees of R_Init_context ({len(callees)} unique):')
    for c in sorted(callees):
        lines.append(f'  0x{c:08X}  {known.get(c,"")}')

    # ── 2. Each callee of R_Init_context ─────────────────────────────────
    for c in sorted(callees):
        dis(c, f'R_Init_callee', n=150)

    # ── 3. GLimp_Init area (xref @ 0x9838D) ──────────────────────────────
    # String xref is inside the function — scan back to find fn start
    fn = find_enclosing_fn(raw, va2off, text_sec, 0x9838D, lookback=0x1000)
    if fn:
        dis(fn, f'GLimp_Init (enclosing fn @ 0x{fn:08X})', n=300)
    else:
        # Disassemble from the xref point going back manually
        # Try a few candidate starts
        for candidate in [0x98310, 0x98340, 0x98350, 0x98360, 0x98370, 0x9830B]:
            dis(candidate, f'GLimp_Init_candidate_{candidate:08X}', n=80)

    # ── 4. VV_ident / gamma / HDR path ────────────────────────────────────
    dis(0x9D9C7, 'VV_ident_fn', n=200)

    # ── 5. GLW_Init / CreateDevice area ───────────────────────────────────
    dis(0xA4245, 'GLW_Init_CreateDevice', n=350)

    # ── 6. R_RenderScene ──────────────────────────────────────────────────
    dis(0x80E90, 'R_RenderScene', n=250)

    # ── 7. AvSetDisplayMode callers ───────────────────────────────────────
    av_slot = next((va for va,n in thunks.items() if n=='AvSetDisplayMode'), None)
    if av_slot:
        H(f'AvSetDisplayMode callers (thunk @ 0x{av_slot:08X})')
        callers = find_callers_of(raw, text_sec, av_slot)
        lines.append(f'  Total call sites: {len(callers)}')
        for cva in callers[:10]:
            fn2 = find_enclosing_fn(raw, va2off, text_sec, cva)
            lines.append(f'  call @ 0x{cva:08X}  fn ~ 0x{fn2:08X}' if fn2 else f'  call @ 0x{cva:08X}  fn ~ ???')
            if fn2:
                for ins in disasm_fn(raw,va2off,sec_of,fn2,200,known):
                    lines.append(fmt(ins,sec_of,read_str,known))

    # ── 8. AvSendTVEncoderOption callers ──────────────────────────────────
    avtv_slot = next((va for va,n in thunks.items() if n=='AvSendTVEncoderOption'), None)
    if avtv_slot:
        H(f'AvSendTVEncoderOption callers (thunk @ 0x{avtv_slot:08X})')
        callers = find_callers_of(raw, text_sec, avtv_slot)
        lines.append(f'  Total call sites: {len(callers)}')
        for cva in callers[:8]:
            fn2 = find_enclosing_fn(raw, va2off, text_sec, cva)
            lines.append(f'  call @ 0x{cva:08X}  fn ~ 0x{fn2:08X}' if fn2 else f'  call @ 0x{cva:08X}  fn ~ ???')

    # ── 9. MmAllocateContiguousMemory callers (D3D contiguous RAM) ────────
    mmal_slot = next((va for va,n in thunks.items() if n=='MmAllocateContiguousMemory'), None)
    if mmal_slot:
        H(f'MmAllocateContiguousMemory callers (thunk @ 0x{mmal_slot:08X})')
        callers = find_callers_of(raw, text_sec, mmal_slot)
        lines.append(f'  Total call sites: {len(callers)}')
        for cva in callers[:10]:
            fn2 = find_enclosing_fn(raw, va2off, text_sec, cva)
            lines.append(f'  call @ 0x{cva:08X}  fn ~ 0x{fn2:08X}' if fn2 else f'  call @ 0x{cva:08X}  fn ~ ???')
            if fn2:
                for ins in disasm_fn(raw,va2off,sec_of,fn2,120,known):
                    lines.append(fmt(ins,sec_of,read_str,known))

    # ── 10. D3D section: find D3D function table via indirect calls ────────
    # Scan .text for call dword ptr [reg+offset] patterns near known D3D VAs
    H('=== D3D vtable indirect call pattern scan (call [reg+N]) ===')
    d3d_sec = next((s for s in sections if s['name']=='D3D'), None)
    if d3d_sec:
        lines.append(f'D3D section: VA=0x{d3d_sec["va"]:08X} size=0x{d3d_sec["vsz"]:08X}')
    # Look for "CreateDevice" string in D3D section strings
    string_hits = search_strings(raw, va2off, sections,
        ['D3D_CreateDevice', 'CreateVertexBuffer', 'SetTexture', 'DrawIndexedVertices',
         'Initializing OpenGL', 'Initializing Renderer', 'Direct3D', 'XGraphics'])
    for pat, vas in string_hits.items():
        for va in vas[:3]:
            lines.append(f'  string "{pat}" @ 0x{va:08X}')

    with open(out_path,'w',encoding='utf-8') as f:
        f.write('\n'.join(lines)+'\n')
    print(f'Phase 3 Renderer written: {out_path}')

if __name__=='__main__':
    main()
