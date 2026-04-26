"""
re_phase3_sound.py
Phase 3 Step 3 — Sound (S_*/DirectSound) deep-dive.

Anchors from Phase 2:
  S_DirectSound3D_init  @ 0x33885
  S_StartSound          @ 0x32565
  S_HashName            @ 0x30500
  S_LoadSoundBank       @ 0x48015

Also searches for:
  - DirectSoundCreate / IDirectSound8 vtable call patterns
  - S_Init caller chain
  - Streaming / mix thread (PsCreateSystemThreadEx callers)
  - Sound cvar registration

Output: scripts/output/phase3_sound.txt
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
        98:'KeBugCheck',
        164:'MmAllocateContiguousMemory',165:'MmAllocateContiguousMemoryEx',
        170:'MmFreeContiguousMemory',172:'MmGetPhysicalAddress',176:'MmMapIoSpace',
        183:'NtAllocateVirtualMemory',186:'NtClose',
        189:'NtCreateFile',218:'NtReadFile',234:'NtWriteFile',
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

def scan_strings_in_section(raw, s, needle):
    enc = needle.encode('latin-1')
    chunk = raw[s['roff']:s['roff']+s['rsz']]
    results=[]
    pos=0
    while True:
        idx=chunk.find(enc,pos)
        if idx==-1: break
        results.append(s['va']+idx)
        pos=idx+1
    return results

def main():
    xbe_path=sys.argv[1] if len(sys.argv)>1 else r'Star Wars Jedi Academy game\default.xbe'
    script_dir=os.path.dirname(os.path.abspath(__file__))
    out_dir=os.path.join(script_dir,'output')
    os.makedirs(out_dir,exist_ok=True)
    out_path=os.path.join(out_dir,'phase3_sound.txt')

    raw,base,sections,va2off,sec_of,read_str=load_xbe(xbe_path)
    text_sec=next(s for s in sections if s['name']=='.text')
    thunks=build_thunk_map(raw,sections,base)

    ANCHORS = {
        0x00033885: 'S_DirectSound3D_init',
        0x00032565: 'S_StartSound',
        0x00030500: 'S_HashName',
        0x00048015: 'S_LoadSoundBank',
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

    # ── 1. S_DirectSound3D_init — primary DS setup ───────────────────────
    insns = dis(0x33885, 'S_DirectSound3D_init', n=300)
    # Collect all callees
    ds_callees = set()
    for ins in insns:
        if ins.id==X86_INS_CALL and ins.operands and ins.operands[0].type==X86_OP_IMM:
            tgt=ins.operands[0].imm&0xFFFFFFFF
            if sec_of(tgt)=='.text':
                ds_callees.add(tgt)
    H(f'S_DS3D_init callees ({len(ds_callees)}):')
    for c in sorted(ds_callees):
        lines.append(f'  0x{c:08X}  {known.get(c,"")}')

    # Disassemble each callee
    for c in sorted(ds_callees):
        dis(c, f'S_DS3D_init_callee_{c:08X}', n=120)

    # ── 2. S_StartSound ───────────────────────────────────────────────────
    dis(0x32565, 'S_StartSound', n=200)

    # ── 3. S_HashName ─────────────────────────────────────────────────────
    dis(0x30500, 'S_HashName', n=100)

    # ── 4. S_LoadSoundBank ────────────────────────────────────────────────
    insns = dis(0x48015, 'S_LoadSoundBank', n=250)
    sb_callees = set()
    for ins in insns:
        if ins.id==X86_INS_CALL and ins.operands and ins.operands[0].type==X86_OP_IMM:
            tgt=ins.operands[0].imm&0xFFFFFFFF
            if sec_of(tgt)=='.text':
                sb_callees.add(tgt)
    H(f'S_LoadSoundBank callees ({len(sb_callees)}):')
    for c in sorted(sb_callees):
        lines.append(f'  0x{c:08X}  {known.get(c,"")}')

    # ── 5. S_Init caller chain
    # S_Init should be called from CL_Init area. From phase2: CL_Init string @ 0x2CEBF4,
    # xref @ 0x18041.  Scan for the S_Init string region.
    H('=== S_Init string search ===')
    for s in sections:
        if s['name'] not in ('.text',): continue
        for needle in ['S_Init', 'S_Shutdown', 'sound/']:
            hits = scan_strings_in_section(raw, s, needle)
            for va in hits[:5]:
                lines.append(f'  "{needle}" in {s["name"]} @ 0x{va:08X}')

    # Search rdata for sound strings
    rdata_sec = next((s for s in sections if s['name']=='.rdata'), None)
    if rdata_sec:
        for needle in ['----- S_Init', 'S_Init:', 'S_Shutdown', 'DSP', 'DirectSound',
                       'sound/music', 'sound/weapons', 'soundaliaseslist']:
            hits = scan_strings_in_section(raw, rdata_sec, needle)
            for va in hits[:5]:
                lines.append(f'  rdata "{needle}" @ 0x{va:08X}')

    # Also scan DSOUND section
    dsound_sec = next((s for s in sections if s['name']=='DSOUND'), None)
    if dsound_sec:
        H(f'DSOUND section: VA=0x{dsound_sec["va"]:08X} vsz=0x{dsound_sec["vsz"]:08X} roff=0x{dsound_sec["roff"]:08X}')
        # Disassemble first 80 instructions
        dis(dsound_sec['va'], 'DSOUND_section_start', n=80)

    # ── 6. PsCreateSystemThreadEx callers (sound mix/stream thread) ────────
    pst_slot = next((va for va,n in thunks.items() if n=='PsCreateSystemThreadEx'), None)
    if pst_slot:
        H(f'PsCreateSystemThreadEx callers (thunk @ 0x{pst_slot:08X})')
        callers = find_callers_of(raw, text_sec, pst_slot)
        lines.append(f'  Total: {len(callers)}')
        for cva in callers[:8]:
            fn2 = find_enclosing_fn(raw, va2off, text_sec, cva)
            lines.append(f'  call @ 0x{cva:08X}  fn ~ 0x{fn2:08X}' if fn2 else f'  call @ 0x{cva:08X}  fn ~ ???')
            if fn2:
                for ins in disasm_fn(raw,va2off,sec_of,fn2,150,known):
                    lines.append(fmt(ins,sec_of,read_str,known))

    # ── 7. RtlInitializeCriticalSection callers in sound range ────────────
    rtlcs_slot = next((va for va,n in thunks.items() if n=='RtlInitializeCriticalSection'), None)
    if rtlcs_slot:
        H(f'RtlInitializeCriticalSection callers (thunk @ 0x{rtlcs_slot:08X})')
        callers = find_callers_of(raw, text_sec, rtlcs_slot)
        lines.append(f'  Total: {len(callers)}')
        # Filter to sound range 0x30000-0x50000
        sound_callers = [c for c in callers if 0x30000 <= c <= 0x50000]
        lines.append(f'  In sound range 0x30000-0x50000: {len(sound_callers)}')
        for cva in sound_callers[:8]:
            fn2 = find_enclosing_fn(raw, va2off, text_sec, cva)
            lines.append(f'  call @ 0x{cva:08X}  fn ~ 0x{fn2:08X}' if fn2 else f'  call @ 0x{cva:08X}  fn ~ ???')

    with open(out_path,'w',encoding='utf-8') as f:
        f.write('\n'.join(lines)+'\n')
    print(f'Phase 3 Sound written: {out_path}')

if __name__=='__main__':
    main()
