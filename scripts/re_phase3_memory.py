"""
re_phase3_memory.py
Phase 3 Step 5 — Memory (Hunk_*/Z_*) deep-dive.

Anchors:
  Z_Init  @ 0x49945  (already in phase3_sys — re-examine callee chain)

Primary goals:
  1. Map the full Hunk allocator layout (Hunk_Alloc, Hunk_AllocateTempMemory,
     Hunk_ClearToMark, Hunk_SetMark, Hunk_MemoryRemaining).
  2. Confirm physical memory layout:
     - Z_Init calls D3D_AllocContiguousMemory(0x1000000) @ 0x23F620 — confirm.
     - Is Hunk backed by the same contiguous block or separate?
  3. MmAllocateContiguousMemory callers — who allocates what?
  4. ExAllocatePool callers — Z_Malloc / small alloc paths.
  5. Hunk low-water / high-water marks.

Output: scripts/output/phase3_memory.txt
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
    sec_count=struct.unpack_from('<I',raw,0x11C)[0]
    sec_hdr=struct.unpack_from('<I',raw,0x120)[0]
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

    return raw,base,sections,va2off,sec_of,read_str

def build_thunk_map(raw, sections, base):
    kt_enc=struct.unpack_from('<I',raw,0x158)[0]
    kt_va=(kt_enc^XBE_KT_RETAIL)&0xFFFFFFFF
    va2off_s = lambda va: next(
        (s['roff']+(va-s['va']) for s in sections if s['va']<=va<s['va']+s['vsz']), None)
    kt_off=va2off_s(kt_va)
    if kt_off is None: return {}
    NAMES={
        1:'AvGetSavedDataAddress',2:'AvSendTVEncoderOption',3:'AvSetDisplayMode',
        15:'ExAllocatePool',16:'ExAllocatePoolWithTag',18:'ExFreePool',
        98:'KeBugCheck',99:'KeBugCheckEx',
        127:'KeQueryPerformanceCounter',
        164:'MmAllocateContiguousMemory',165:'MmAllocateContiguousMemoryEx',
        166:'MmAllocateSystemMemory',168:'MmCreateKernelStack',
        170:'MmFreeContiguousMemory',171:'MmFreeSystemMemory',
        172:'MmGetPhysicalAddress',176:'MmMapIoSpace',
        183:'NtAllocateVirtualMemory',186:'NtClose',
        189:'NtCreateFile',198:'NtFreeVirtualMemory',
        203:'NtProtectVirtualMemory',216:'NtQueryVirtualMemory',
        252:'PsCreateSystemThread',253:'PsCreateSystemThreadEx',
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

def scan_str(raw, sections, needle, sec_names=None):
    enc = needle.encode('latin-1')
    results=[]
    for s in sections:
        if sec_names and s['name'] not in sec_names: continue
        chunk = raw[s['roff']:s['roff']+s['rsz']]
        pos=0
        while True:
            idx=chunk.find(enc,pos)
            if idx==-1: break
            results.append((s['name'], s['va']+idx))
            pos=idx+1
    return results

def find_xrefs_to_va(raw, text_sec, target_va):
    """Find PUSH imm or MOV ...,imm targeting target_va in .text."""
    pattern = b'\x68' + struct.pack('<I', target_va)
    chunk = raw[text_sec['roff']:text_sec['roff']+text_sec['rsz']]
    results=[]
    pos=0
    while True:
        idx=chunk.find(pattern,pos)
        if idx==-1: break
        results.append(text_sec['va']+idx)
        pos=idx+1
    return sorted(set(results))

def main():
    xbe_path=sys.argv[1] if len(sys.argv)>1 else r'Star Wars Jedi Academy game\default.xbe'
    script_dir=os.path.dirname(os.path.abspath(__file__))
    out_dir=os.path.join(script_dir,'output')
    os.makedirs(out_dir,exist_ok=True)
    out_path=os.path.join(out_dir,'phase3_memory.txt')

    raw,base,sections,va2off,sec_of,read_str=load_xbe(xbe_path)
    text_sec=next(s for s in sections if s['name']=='.text')
    thunks=build_thunk_map(raw,sections,base)

    ANCHORS = {
        0x00049945: 'Z_Init',
        0x0004A060: 'Hunk_init?',   # called from Com_Init with push 1 right before Z_Init area
        0x00023F620: 'Z_Init_D3D_alloc_site?',
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
            return []
        for ins in insns:
            lines.append(fmt(ins,sec_of,read_str,known))
        lines.append(f'  [{len(insns)} insns]')
        return insns

    # ── 1. Z_Init and its callees ─────────────────────────────────────────
    insns = dis(0x49945, 'Z_Init', n=200)
    z_callees = set()
    for ins in insns:
        if ins.id==X86_INS_CALL and ins.operands and ins.operands[0].type==X86_OP_IMM:
            tgt=ins.operands[0].imm&0xFFFFFFFF
            if sec_of(tgt)=='.text':
                z_callees.add(tgt)
    H(f'Z_Init callees ({len(z_callees)}):')
    for c in sorted(z_callees):
        lines.append(f'  0x{c:08X}  {known.get(c,"")}')
    for c in sorted(z_callees):
        dis(c, f'Z_Init_callee_{c:08X}', n=120)

    # ── 2. String search for Hunk_* ───────────────────────────────────────
    H('=== Hunk_* string search ===')
    hunk_anchors = {}
    for needle in ['Hunk_Alloc', 'Hunk_AllocateTempMemory', 'Hunk_ClearToMark',
                   'Hunk_SetMark', 'Hunk_MemoryRemaining', 'Hunk_FreeTempMemory',
                   'Hunk_Clear', '----- Hunk_', 'Z_Malloc', 'Z_Free', 'Z_TagMalloc',
                   'Hunk_SmallLog2MallocSize', 'hunk', 'Zone memory']:
        hits = scan_str(raw, sections, needle)
        for (sname, va) in hits[:3]:
            lines.append(f'  "{needle}" in {sname} @ 0x{va:08X}')
            # xrefs
            xrefs = find_xrefs_to_va(raw, text_sec, va)
            for xva in xrefs[:3]:
                fn = find_enclosing_fn(raw, va2off, text_sec, xva)
                fn_str = f'0x{fn:08X}' if fn else '???'
                lines.append(f'    xref @ 0x{xva:08X}  fn ~ {fn_str}')
                if fn and fn not in hunk_anchors:
                    hunk_anchors[fn] = needle.strip('-').strip(':').replace(' ','_')

    # ── 3. Disassemble Hunk fn anchors ────────────────────────────────────
    H(f'=== Hunk fn anchors discovered: {len(hunk_anchors)} ===')
    for fn_va, label in sorted(hunk_anchors.items()):
        known[fn_va] = label
        lines.append(f'  0x{fn_va:08X}  {label}')
    for fn_va, label in sorted(hunk_anchors.items()):
        dis(fn_va, label, n=150)

    # ── 4. MmAllocateContiguousMemory callers (physical RAM) ──────────────
    mmal_slot = next((va for va,n in thunks.items() if n=='MmAllocateContiguousMemory'), None)
    if mmal_slot:
        H(f'MmAllocateContiguousMemory callers (thunk @ 0x{mmal_slot:08X})')
        callers = find_callers_of(raw, text_sec, mmal_slot)
        lines.append(f'  Total: {len(callers)}')
        seen_fns = set()
        for cva in callers:
            fn2 = find_enclosing_fn(raw, va2off, text_sec, cva)
            fn_str = f'0x{fn2:08X}' if fn2 else '???'
            lines.append(f'  call @ 0x{cva:08X}  fn ~ {fn_str}')
            if fn2 and fn2 not in seen_fns:
                seen_fns.add(fn2)
                for ins in disasm_fn(raw,va2off,sec_of,fn2,150,known):
                    lines.append(fmt(ins,sec_of,read_str,known))

    # ── 5. MmAllocateContiguousMemoryEx callers ───────────────────────────
    mmalx_slot = next((va for va,n in thunks.items() if n=='MmAllocateContiguousMemoryEx'), None)
    if mmalx_slot:
        H(f'MmAllocateContiguousMemoryEx callers (thunk @ 0x{mmalx_slot:08X})')
        callers = find_callers_of(raw, text_sec, mmalx_slot)
        lines.append(f'  Total: {len(callers)}')
        seen_fns = set()
        for cva in callers:
            fn2 = find_enclosing_fn(raw, va2off, text_sec, cva)
            fn_str = f'0x{fn2:08X}' if fn2 else '???'
            lines.append(f'  call @ 0x{cva:08X}  fn ~ {fn_str}')
            if fn2 and fn2 not in seen_fns:
                seen_fns.add(fn2)
                for ins in disasm_fn(raw,va2off,sec_of,fn2,150,known):
                    lines.append(fmt(ins,sec_of,read_str,known))

    # ── 6. ExAllocatePool callers (Z_Malloc / small allocs) ───────────────
    eap_slot = next((va for va,n in thunks.items() if n=='ExAllocatePool'), None)
    if eap_slot:
        H(f'ExAllocatePool callers (thunk @ 0x{eap_slot:08X})')
        callers = find_callers_of(raw, text_sec, eap_slot)
        lines.append(f'  Total: {len(callers)}')
        seen_fns = set()
        for cva in callers[:20]:
            fn2 = find_enclosing_fn(raw, va2off, text_sec, cva)
            fn_str = f'0x{fn2:08X}' if fn2 else '???'
            lines.append(f'  call @ 0x{cva:08X}  fn ~ {fn_str}')
            if fn2 and fn2 not in seen_fns:
                seen_fns.add(fn2)

    # ── 7. ExFreePool callers ─────────────────────────────────────────────
    efp_slot = next((va for va,n in thunks.items() if n=='ExFreePool'), None)
    if efp_slot:
        H(f'ExFreePool callers (thunk @ 0x{efp_slot:08X})')
        callers = find_callers_of(raw, text_sec, efp_slot)
        lines.append(f'  Total: {len(callers)}')
        seen_fns = set()
        for cva in callers[:20]:
            fn2 = find_enclosing_fn(raw, va2off, text_sec, cva)
            fn_str = f'0x{fn2:08X}' if fn2 else '???'
            lines.append(f'  call @ 0x{cva:08X}  fn ~ {fn_str}')
            if fn2 and fn2 not in seen_fns:
                seen_fns.add(fn2)

    # ── 8. 0x1000000 (16MB) literal — physical mem alloc size ─────────────
    H('=== 0x1000000 (16MB) constant in .text ===')
    chunk = raw[text_sec['roff']:text_sec['roff']+text_sec['rsz']]
    needle16m = struct.pack('<I', 0x1000000)
    pos=0
    while True:
        idx=chunk.find(needle16m,pos)
        if idx==-1: break
        va_here = text_sec['va']+idx
        fn2 = find_enclosing_fn(raw, va2off, text_sec, va_here)
        lines.append(f'  0x1000000 @ 0x{va_here:08X}  fn~0x{fn2:08X}' if fn2 else f'  0x1000000 @ 0x{va_here:08X}')
        pos=idx+1

    # Also 0x2000000 (32MB)
    needle32m = struct.pack('<I', 0x2000000)
    pos=0
    while True:
        idx=chunk.find(needle32m,pos)
        if idx==-1: break
        va_here = text_sec['va']+idx
        fn2 = find_enclosing_fn(raw, va2off, text_sec, va_here)
        lines.append(f'  0x2000000 @ 0x{va_here:08X}  fn~0x{fn2:08X}' if fn2 else f'  0x2000000 @ 0x{va_here:08X}')
        pos=idx+1

    # ── 9. Hunk_Alloc detail (from fn anchors, if found) ──────────────────
    # Also try the fn @ 0x4A060 which was called from Com_Init just before Z_Init
    dis(0x4A060, 'Com_Init_sub_before_Z_Init', n=150)

    with open(out_path,'w',encoding='utf-8') as f:
        f.write('\n'.join(lines)+'\n')
    print(f'Phase 3 Memory written: {out_path}')

if __name__=='__main__':
    main()
