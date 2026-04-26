"""
re_phase3_sys.py
Phase 3 Step 1 — Sys_*/FS platform layer deep-dive.
Targets (from Phase 2 anchor map):
  FS_Startup      0x25F90
  FS_basepath     0x45080
  Sys_QueEvent    0x44CB5
  Z_Init          0x49945
  S_DS3D_init     0x33885
  Com_Init        0x237A8

Also scans for:
  - NtCreateFile callers (Sys_FileOpen / FS_FOpenFile)
  - NtReadFile callers  (Sys_FileRead)
  - Drive symlink setup (XeDeviceMount / NtCreateSymbolicLinkObject pattern)
  - XeLoadSection callers

Output: scripts/output/phase3_sys.txt
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
        if off is None: return f'<VA 0x{va:08X} unmapped>'
        end=raw.find(b'\x00',off,off+maxlen)
        b=raw[off:end if end!=-1 else off+maxlen]
        return b.decode('latin-1','replace')

    return raw,base,sections,va2off,sec_of,ep,read_str

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
            tag=f'  → {sec_of(tgt)} 0x{tgt:08X}'+(f' [{nm}]' if nm else '')
        elif op.type==X86_OP_MEM:
            disp=op.mem.disp&0xFFFFFFFF if op.mem.base==0 else None
            if disp:
                nm=(known or {}).get(disp,'?')
                tag=f'  → [0x{disp:08X}] {nm}'
    # Annotate PUSH of .rdata strings
    if ins.id==X86_INS_PUSH and ins.operands and ins.operands[0].type==X86_OP_IMM:
        imm=ins.operands[0].imm&0xFFFFFFFF
        s=read_str(imm)
        if s and all(0x20<=ord(c)<0x7F for c in s[:4]):
            tag=f'  ; "{s[:64]}"' if s else ''
    return f'  0x{ins.address:08X}:  {ins.mnemonic:<8} {ins.op_str}{tag}'

def find_callers_of(raw, text_sec, thunk_va):
    """Find all call dword ptr [thunk_va] in .text, return call site VAs."""
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

def find_enclosing_fn(raw, va2off, text_sec, ref_va, lookback=0x600):
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

def build_thunk_map(raw, sections, base):
    kt_enc=struct.unpack_from('<I',raw,0x158)[0]
    kt_va=(kt_enc^XBE_KT_RETAIL)&0xFFFFFFFF
    va2off_simple = lambda va: next(
        (s['roff']+(va-s['va']) for s in sections if s['va']<=va<s['va']+s['vsz']), None)
    kt_off=va2off_simple(kt_va)
    if kt_off is None: return {}
    NAMES={
        187:'NtCreateDirectoryObject',188:'NtCreateEvent',189:'NtCreateFile',
        190:'NtCreateIoCompletion',191:'NtCreateMutant',192:'NtCreateSemaphore',
        193:'NtCreateTimer',194:'NtDeleteFile',196:'NtDuplicateObject',
        197:'NtFlushBuffersFile',198:'NtFreeVirtualMemory',199:'NtFsControlFile',
        200:'NtOpenDirectoryObject',201:'NtOpenFile',202:'NtOpenSymbolicLinkObject',
        203:'NtProtectVirtualMemory',204:'NtPulseEvent',206:'NtQueryDirectoryFile',
        207:'NtQueryDirectoryObject',208:'NtQueryEvent',209:'NtQueryFullAttributesFile',
        210:'NtQueryInformationFile',211:'NtQueryIoCompletion',212:'NtQueryMutant',
        213:'NtQuerySemaphore',214:'NtQuerySymbolicLinkObject',215:'NtQueryTimer',
        216:'NtQueryVirtualMemory',217:'NtQueryVolumeInformationFile',218:'NtReadFile',
        219:'NtReadFileScatter',220:'NtReleaseMutant',221:'NtReleaseSemaphore',
        222:'NtRemoveIoCompletion',223:'NtResumeThread',224:'NtSetEvent',
        225:'NtSetInformationFile',226:'NtSetIoCompletion',227:'NtSetSystemTime',
        228:'NtSetTimerEx',229:'NtSignalAndWaitForSingleObjectEx',230:'NtSuspendThread',
        232:'NtWaitForSingleObjectEx',233:'NtWaitForMultipleObjectsEx',234:'NtWriteFile',
        235:'NtWriteFileGather',236:'NtYieldExecution',
        183:'NtAllocateVirtualMemory',186:'NtClose',
        15:'ExAllocatePool',16:'ExAllocatePoolWithTag',18:'ExFreePool',
        98:'KeBugCheck',99:'KeBugCheckEx',
        127:'KeQueryPerformanceCounter',128:'KeQueryPerformanceFrequency',
        129:'KeQuerySystemTime',155:'KeTickCount',
        164:'MmAllocateContiguousMemory',165:'MmAllocateContiguousMemoryEx',
        166:'MmAllocateSystemMemory',168:'MmCreateKernelStack',170:'MmFreeContiguousMemory',
        171:'MmFreeSystemMemory',172:'MmGetPhysicalAddress',176:'MmMapIoSpace',
        1:'AvGetSavedDataAddress',2:'AvSendTVEncoderOption',3:'AvSetDisplayMode',
        4:'AvSetSavedDataAddress',
        252:'PsCreateSystemThread',253:'PsCreateSystemThreadEx',
        256:'PsTerminateSystemThread',
        325:'XeLoadSection',326:'XeUnloadSection',324:'XeImageFileName',
        258:'RtlAnsiStringToUnicodeString',259:'RtlAppendStringToString',
        268:'RtlCompareString',270:'RtlCopyString',287:'RtlInitAnsiString',
        288:'RtlInitUnicodeString',289:'RtlInitializeCriticalSection',
        275:'RtlEnterCriticalSection',292:'RtlLeaveCriticalSection',
        296:'RtlMoveMemory',318:'RtlZeroMemory',
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

# ── Phase 3 anchors from Phase 2 ──────────────────────────────────────────
ANCHORS = {
    0x000237A8: 'Com_Init',
    0x00025F90: 'FS_Startup',
    0x0002C4B0: 'Sys_Init_or_early_init',    # called early in Com_Init
    0x00021F20: 'Sys_Milliseconds_or_time',  # second call in Com_Init
    0x00049940: 'Com_Init_sub1',
    0x00042960: 'Com_Init_sub2',
    0x00017320: 'CL_Init_sub',
    0x00045080: 'FS_basepath_init_detail',
    0x00044CB5: 'Sys_QueEvent',
    0x00049945: 'Z_Init',
    0x00032565: 'S_StartSound',
    0x00033885: 'S_DirectSound3D_init',
    0x00030500: 'S_HashName',
    0x0003CBC0: 'SV_Init',
    0x00080E90: 'R_RenderScene',
    0x0009D9C7: 'VV_ident_fn',
    0x000A4245: 'GLW_Init',
    0x00048015: 'S_LoadSoundBank',
    0x00012E90: 'Com_Init_late1',
    0x00075E30: 'Com_Init_late2',
}

def main():
    xbe_path=sys.argv[1] if len(sys.argv)>1 else r'Star Wars Jedi Academy game\default.xbe'
    script_dir=os.path.dirname(os.path.abspath(__file__))
    out_dir=os.path.join(script_dir,'output')
    os.makedirs(out_dir,exist_ok=True)
    out_path=os.path.join(out_dir,'phase3_sys.txt')

    raw,base,sections,va2off,sec_of,ep,read_str=load_xbe(xbe_path)
    text_sec=next(s for s in sections if s['name']=='.text')
    thunks=build_thunk_map(raw,sections,base)
    # Reverse: thunk_va → name
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

    # ── 1. FS_Startup ────────────────────────────────────────────────────
    dis(0x25F90, 'FS_Startup')

    # ── 2. FS_basepath init (inner detail) ───────────────────────────────
    dis(0x45080, 'FS_basepath_init')

    # ── 3. Z_Init ────────────────────────────────────────────────────────
    dis(0x49945, 'Z_Init')

    # ── 4. Sys_QueEvent ──────────────────────────────────────────────────
    dis(0x44CB5, 'Sys_QueEvent')

    # ── 5. Com_Init first calls (early platform init) ────────────────────
    # From Com_Init (0x237A8): first few calls are platform setup
    # 0x22B60, 0x2C4B0, 0x21F20 etc.
    for va,lbl in [(0x22B60,'Com_Init_arg_handler'),
                   (0x2C4B0,'Com_Init_call2'),
                   (0x21F20,'Com_Init_call3_time?'),
                   (0x49940,'Com_Init_sub1'),
                   (0x42960,'Com_Init_sub2'),
                   (0x17320,'CL_Init_area'),
                   (0x750A0,'Com_Init_call7'),
                   (0x98310,'Com_Init_call8'),
                   (0xBF960,'Com_Init_call9'),
                   ]:
        dis(va, lbl, n=80)

    # ── 6. NtCreateFile callers (Sys_FileOpen / FS_FOpenFile area) ───────
    ntcf_slot = next((va for va,n in thunks.items() if n=='NtCreateFile'), None)
    if ntcf_slot:
        H(f'NtCreateFile callers (thunk slot 0x{ntcf_slot:08X})')
        callers=find_callers_of(raw,text_sec,ntcf_slot)
        lines.append(f'  Total: {len(callers)} call sites')
        for cva in callers[:20]:
            fn=find_enclosing_fn(raw,va2off,text_sec,cva)
            fn_str=f'0x{fn:08X}' if fn else '???'
            lines.append(f'  call @ 0x{cva:08X}  fn ≈ {fn_str}')

    # ── 7. NtReadFile callers ─────────────────────────────────────────────
    ntrf_slot=next((va for va,n in thunks.items() if n=='NtReadFile'),None)
    if ntrf_slot:
        H(f'NtReadFile callers (thunk slot 0x{ntrf_slot:08X})')
        callers=find_callers_of(raw,text_sec,ntrf_slot)
        lines.append(f'  Total: {len(callers)} call sites')
        for cva in callers[:15]:
            fn=find_enclosing_fn(raw,va2off,text_sec,cva)
            fn_str=f'0x{fn:08X}' if fn else '???'
            lines.append(f'  call @ 0x{cva:08X}  fn ≈ {fn_str}')

    # ── 8. XeLoadSection callers ──────────────────────────────────────────
    xels_slot=next((va for va,n in thunks.items() if n=='XeLoadSection'),None)
    if xels_slot:
        H(f'XeLoadSection callers (thunk slot 0x{xels_slot:08X})')
        callers=find_callers_of(raw,text_sec,xels_slot)
        lines.append(f'  Total: {len(callers)} call sites')
        for cva in callers[:10]:
            fn=find_enclosing_fn(raw,va2off,text_sec,cva)
            fn_str=f'0x{fn:08X}' if fn else '???'
            lines.append(f'  call @ 0x{cva:08X}  fn ≈ {fn_str}')
            if fn:
                insns=disasm_fn(raw,va2off,sec_of,fn,120,known)
                for ins in insns:
                    lines.append(fmt(ins,sec_of,read_str,known))

    # ── 9. PsCreateSystemThreadEx callers ─────────────────────────────────
    pst_slot=next((va for va,n in thunks.items() if n=='PsCreateSystemThreadEx'),None)
    if pst_slot:
        H(f'PsCreateSystemThreadEx callers (thunk slot 0x{pst_slot:08X})')
        callers=find_callers_of(raw,text_sec,pst_slot)
        lines.append(f'  Total: {len(callers)} call sites')
        for cva in callers[:8]:
            fn=find_enclosing_fn(raw,va2off,text_sec,cva)
            fn_str=f'0x{fn:08X}' if fn else '???'
            lines.append(f'  call @ 0x{cva:08X}  fn ≈ {fn_str}')
            if fn:
                insns=disasm_fn(raw,va2off,sec_of,fn,100,known)
                for ins in insns:
                    lines.append(fmt(ins,sec_of,read_str,known))

    # ── 10. GLW_Init / CreateDevice area ─────────────────────────────────
    dis(0xA4245, 'GLW_Init_CreateDevice_area', n=300)

    # ── 11. SV_Init ──────────────────────────────────────────────────────
    dis(0x3CBC0, 'SV_Init', n=150)

    # ── 12. S_DirectSound3D_init ─────────────────────────────────────────
    dis(0x33885, 'S_DirectSound3D_init', n=150)

    with open(out_path,'w',encoding='utf-8') as f:
        f.write('\n'.join(lines)+'\n')
    print(f'Phase 3 Sys written: {out_path}')

if __name__=='__main__':
    main()
