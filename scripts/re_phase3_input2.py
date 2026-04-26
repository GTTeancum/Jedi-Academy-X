"""
re_phase3_input2.py
Phase 3 Step 4b — Input anchoring via "Controller" .rdata strings.

The initial input script found "Controller" strings at:
  .rdata @ 0x2D312A
  .rdata @ 0x2D3139
  .rdata @ 0x2D3155

These are likely cvar names (in_controller, in_controllerScheme, etc.).
Find their xrefs and anchor IN_Init, IN_Frame.

Also: scan for XGetDeviceChanges call pattern — on Xbox XDK 5558, XInput
functions are in the XAPI runtime. Look for the call to an address in the
XAPI/XPP/XONLINE sections from .text (indirect call via thunk or direct call).

Output: scripts/output/phase3_input2.txt
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

def find_enclosing_fn(raw, va2off, text_sec, ref_va, lookback=0x1000):
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

def disasm_fn(raw, va2off, sec_of, start_va, max_insns=250, known=None):
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

def main():
    xbe_path=sys.argv[1] if len(sys.argv)>1 else r'Star Wars Jedi Academy game\default.xbe'
    script_dir=os.path.dirname(os.path.abspath(__file__))
    out_dir=os.path.join(script_dir,'output')
    os.makedirs(out_dir,exist_ok=True)
    out_path=os.path.join(out_dir,'phase3_input2.txt')

    raw,base,sections,va2off,sec_of,read_str=load_xbe(xbe_path)
    text_sec=next(s for s in sections if s['name']=='.text')
    rdata_sec=next((s for s in sections if s['name']=='.rdata'),None)
    known={}

    lines=[]
    def H(t):
        lines.append('')
        lines.append('='*72)
        lines.append(t)
        lines.append('='*72)

    def dis(va, label, n=200):
        H(f'{label}  @ 0x{va:08X}')
        insns=disasm_fn(raw,va2off,sec_of,va,n,known)
        if not insns:
            lines.append('  [not found]')
            return []
        for ins in insns:
            lines.append(fmt(ins,sec_of,read_str,known))
        lines.append(f'  [{len(insns)} insns]')
        return insns

    # ── 1. Dump strings around .rdata 0x2D312A-0x2D3200 ──────────────────
    H('=== .rdata input strings @ 0x2D3120-0x2D3200 ===')
    for va in range(0x2D3120, 0x2D3200, 1):
        off = va2off(va)
        if off is None: continue
        if raw[off] in range(0x20, 0x7F) or raw[off] == 0:
            s = read_str(va, 48)
            if len(s) >= 3:
                lines.append(f'  0x{va:08X}: "{s}"')
                va += len(s)  # skip ahead (approximate)

    # ── 2. Find all PUSH imm32 and MOV reg,imm32 xrefs to these strings ──
    H('=== PUSH imm32 xrefs to 0x2D312A, 0x2D3139, 0x2D3155 ===')
    anchors_found = {}
    for target_va in [0x2D312A, 0x2D3139, 0x2D3155, 0x2D3164, 0x2D3179]:
        s = read_str(target_va, 48)
        lines.append(f'  String @ 0x{target_va:08X}: "{s}"')
        # Search for PUSH imm32 == target_va
        pat = b'\x68' + struct.pack('<I', target_va)
        chunk = raw[text_sec['roff']:text_sec['roff']+text_sec['rsz']]
        pos=0
        while True:
            idx=chunk.find(pat,pos)
            if idx==-1: break
            xva = text_sec['va']+idx
            fn = find_enclosing_fn(raw, va2off, text_sec, xva)
            fn_str = f'0x{fn:08X}' if fn else '???'
            lines.append(f'    push @ 0x{xva:08X}  fn ~ {fn_str}')
            if fn and fn not in anchors_found:
                anchors_found[fn] = f'IN_fn_{fn:08X}'
            pos=idx+1
        # Also MOV reg/mem, imm32
        pat2 = struct.pack('<I', target_va)
        pos=0
        hits=[]
        while True:
            idx=chunk.find(pat2,pos)
            if idx==-1: break
            # Must be prefixed by a MOV opcode
            if idx >= 1:
                prev = chunk[idx-1]
                if prev in (0x68,):  # already covered above
                    pos=idx+4; continue
                # Check for C7 (MOV [mem], imm32), B8-BF (MOV reg,imm32)
                if prev in (0xBF, 0xBE, 0xBD, 0xBC, 0xBB, 0xBA, 0xB9, 0xB8):
                    xva = text_sec['va']+idx-1
                    fn = find_enclosing_fn(raw, va2off, text_sec, xva)
                    fn_str = f'0x{fn:08X}' if fn else '???'
                    if (xva, fn) not in hits:
                        hits.append((xva,fn))
                        lines.append(f'    mov @ 0x{xva:08X}  fn ~ {fn_str}')
                        if fn and fn not in anchors_found:
                            anchors_found[fn] = f'IN_fn_{fn:08X}'
            pos=idx+4

    # ── 3. Disassemble all discovered input fn anchors ────────────────────
    H(f'=== Input fn anchors discovered: {len(anchors_found)} ===')
    for fn_va, label in sorted(anchors_found.items()):
        lines.append(f'  0x{fn_va:08X}  {label}')
        s=read_str(fn_va,8)  # see if the fn starts with a known string push
        # Check if any "IN_" string near this fn
    for fn_va, label in sorted(anchors_found.items()):
        dis(fn_va, label, n=200)

    # ── 4. Scan .text for calls to XONLINE / XPP (indirect calls to those VA ranges)
    H('=== .text calls into XONLINE/XPP sections ===')
    xon_sec = next((s for s in sections if s['name']=='XONLINE'), None)
    xpp_sec = next((s for s in sections if s['name']=='XPP'), None)
    text_chunk = raw[text_sec['roff']:text_sec['roff']+text_sec['rsz']]
    # Direct call: E8 xx xx xx xx  where target falls in XONLINE/XPP VA range
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    call_into_xon = []
    call_into_xpp = []
    insn_buf = text_chunk[:min(len(text_chunk), 200000)]
    for ins in md.disasm(insn_buf, text_sec['va']):
        if ins.id == X86_INS_CALL and ins.operands:
            op=ins.operands[0]
            if op.type==X86_OP_IMM:
                tgt=op.imm&0xFFFFFFFF
                if xon_sec and xon_sec['va']<=tgt<xon_sec['va']+xon_sec['vsz']:
                    call_into_xon.append((ins.address, tgt))
                if xpp_sec and xpp_sec['va']<=tgt<xpp_sec['va']+xpp_sec['vsz']:
                    call_into_xpp.append((ins.address, tgt))
    lines.append(f'  Calls from .text into XONLINE: {len(call_into_xon)}')
    for site, tgt in call_into_xon[:15]:
        fn = find_enclosing_fn(raw, va2off, text_sec, site)
        lines.append(f'    call @ 0x{site:08X} -> XONLINE 0x{tgt:08X}  fn~0x{fn:08X}' if fn else f'    call @ 0x{site:08X} -> XONLINE 0x{tgt:08X}')
    lines.append(f'  Calls from .text into XPP: {len(call_into_xpp)}')
    for site, tgt in call_into_xpp[:15]:
        fn = find_enclosing_fn(raw, va2off, text_sec, site)
        lines.append(f'    call @ 0x{site:08X} -> XPP 0x{tgt:08X}  fn~0x{fn:08X}' if fn else f'    call @ 0x{site:08X} -> XPP 0x{tgt:08X}')

    # ── 5. Disassemble XPP section start properly ─────────────────────────
    # XPP on Xbox contains XInput (XGetDeviceChanges etc.) code
    if xpp_sec:
        H(f'XPP section full scan (VA=0x{xpp_sec["va"]:08X} vsz=0x{xpp_sec["vsz"]:08X})')
        lines.append(f'  roff=0x{xpp_sec["roff"]:08X}')
        # Find first non-zero byte in XPP
        xpp_chunk = raw[xpp_sec['roff']:xpp_sec['roff']+xpp_sec['rsz']]
        first_nonzero = 0
        for i,b in enumerate(xpp_chunk):
            if b != 0:
                first_nonzero = i
                break
        lines.append(f'  First non-zero byte at offset 0x{first_nonzero:X} (VA 0x{xpp_sec["va"]+first_nonzero:08X})')
        # Disassemble from there
        start_va = xpp_sec['va'] + first_nonzero
        H(f'XPP code @ 0x{start_va:08X}')
        insns=disasm_fn(raw,va2off,sec_of,start_va,120,known)
        for ins in insns:
            lines.append(fmt(ins,sec_of,read_str,known))

    with open(out_path,'w',encoding='utf-8') as f:
        f.write('\n'.join(lines)+'\n')
    print(f'Phase 3 Input2 written: {out_path}')

if __name__=='__main__':
    main()
