"""
re_phase3_input.py
Phase 3 Step 4 — Input (IN_*/XInput) deep-dive.

No direct anchors from Phase 2 — must locate via string search.
Known id Tech 3 Xbox input strings:
  "IN_Init", "XInputGetState", "IN_Frame", "joy", "joystick",
  "in_joystick", "in_xbox"

Strategy:
  1. Search .rdata for IN_Init / XInput strings -> xrefs in .text -> fn anchors
  2. Disassemble all XInputGetCapabilities / XInputGetState / XInputSetState
     callers (these are NOT NT kernel thunks on Xbox — they're in XAPI / XONLINE
     sections or .text inline).
  3. Disassemble IN_Init, IN_Frame, IN_Shutdown areas.
  4. Map button/axis translation.

Output: scripts/output/phase3_input.txt
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

def find_xrefs_to(raw, text_sec, target_va):
    """Find all PUSH imm32 == target_va in .text (likely string address push)."""
    pattern = b'\x68' + struct.pack('<I', target_va)
    chunk = raw[text_sec['roff']:text_sec['roff']+text_sec['rsz']]
    results=[]
    pos=0
    while True:
        idx=chunk.find(pattern,pos)
        if idx==-1: break
        results.append(text_sec['va']+idx)
        pos=idx+1
    # Also MOV ... imm32
    pattern2 = struct.pack('<I', target_va)
    pos=0
    while True:
        idx=chunk.find(pattern2,pos)
        if idx==-1: break
        results.append(text_sec['va']+idx)
        pos=idx+len(pattern2)
    return sorted(set(results))

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

def main():
    xbe_path=sys.argv[1] if len(sys.argv)>1 else r'Star Wars Jedi Academy game\default.xbe'
    script_dir=os.path.dirname(os.path.abspath(__file__))
    out_dir=os.path.join(script_dir,'output')
    os.makedirs(out_dir,exist_ok=True)
    out_path=os.path.join(out_dir,'phase3_input.txt')

    raw,base,sections,va2off,sec_of,read_str=load_xbe(xbe_path)
    text_sec=next(s for s in sections if s['name']=='.text')
    thunks=build_thunk_map(raw,sections,base)
    known=dict(thunks)

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

    # ── 1. String hunt for IN_* / XInput patterns ─────────────────────────
    H('=== Input string search ===')
    needles = [
        'IN_Init', 'IN_Shutdown', 'IN_Frame', 'IN_ClearStates',
        'XInputGetState', 'XInputGetCapabilities', 'XInputSetState',
        'in_joystick', 'in_xbox', 'joy_threshold', 'joypad',
        'Gamepad', 'gamepad', 'controller', 'Controller',
        '----- IN_Init', 'IN_Init:',
    ]
    anchors_found = {}
    for needle in needles:
        hits = scan_str(raw, sections, needle)
        for (sname, va) in hits[:4]:
            lines.append(f'  "{needle}" in {sname} @ 0x{va:08X}')
            # Find xrefs in .text
            xrefs = find_xrefs_to(raw, text_sec, va)
            for xva in xrefs[:4]:
                fn = find_enclosing_fn(raw, va2off, text_sec, xva)
                fn_str = f'0x{fn:08X}' if fn else '???'
                lines.append(f'    xref @ 0x{xva:08X}  fn ~ {fn_str}')
                if fn and fn not in anchors_found:
                    anchors_found[fn] = f'IN_{needle.strip("-").strip(":").split("_")[1] if "_" in needle else needle}'

    # ── 2. XInput section (XONLINE has XInput in XDK 5558) ───────────────
    H('=== XONLINE section (may contain XInput) ===')
    xon_sec = next((s for s in sections if s['name']=='XONLINE'), None)
    if xon_sec:
        lines.append(f'  XONLINE: VA=0x{xon_sec["va"]:08X} vsz=0x{xon_sec["vsz"]:08X} roff=0x{xon_sec["roff"]:08X}')
        dis(xon_sec['va'], 'XONLINE_start', n=60)
    xpp_sec = next((s for s in sections if s['name']=='XPP'), None)
    if xpp_sec:
        lines.append(f'  XPP: VA=0x{xpp_sec["va"]:08X} vsz=0x{xpp_sec["vsz"]:08X}')
        dis(xpp_sec['va'], 'XPP_start', n=80)

    # ── 3. Disassemble discovered IN_* fn anchors ─────────────────────────
    H(f'=== Discovered input fn anchors: {len(anchors_found)} ===')
    for fn_va, label in sorted(anchors_found.items()):
        known[fn_va] = label
        lines.append(f'  0x{fn_va:08X}  {label}')

    for fn_va, label in sorted(anchors_found.items()):
        dis(fn_va, label, n=200)

    # ── 4. Scan .text for XInputGetState-style call signatures ────────────
    # XInputGetState on Xbox is typically in XAPI.LIB, linked into .text.
    # Pattern: look for the XInput hub function — reads memory at a MMIO addr
    # for controller slot 0.  Also look for 0x8000_0000 (A button mask).
    H('=== XInput button mask pattern scan ===')
    # Search for common Xbox button constants in .text disassembly
    # A=0x1000, B=0x2000, X=0x4000, Y=0x8000 packed into WORD
    # Left trigger / right trigger are bytes
    button_patterns = [
        (b'\x81\xe1\x00\x10\x00\x00', 'test_A_button_0x1000'),
        (b'\x81\xe1\x00\x20\x00\x00', 'test_B_button_0x2000'),
        (b'\x81\xe1\x00\x40\x00\x00', 'test_X_button_0x4000'),
        (b'\x81\xe1\x00\x80\x00\x00', 'test_Y_button_0x8000'),
        (b'\xf7\xc1\x00\x10\x00\x00', 'test_A_f7c1'),
        (b'\xf7\xc1\x00\x20\x00\x00', 'test_B_f7c1'),
    ]
    chunk = raw[text_sec['roff']:text_sec['roff']+text_sec['rsz']]
    for pat, name in button_patterns:
        pos=0
        while True:
            idx=chunk.find(pat,pos)
            if idx==-1: break
            va = text_sec['va']+idx
            fn = find_enclosing_fn(raw, va2off, text_sec, va)
            lines.append(f'  {name} @ 0x{va:08X}  fn~0x{fn:08X}' if fn else f'  {name} @ 0x{va:08X}')
            pos=idx+1

    # ── 5. Look for XGetDeviceChanges / XGetDeviceInformation patterns ────
    H('=== XInput/gamepad device strings ===')
    for needle in ['XGetDevice', 'XINPUT', 'XInput', 'XAutoPowerOn', 'XDEVICE']:
        hits = scan_str(raw, sections, needle)
        for (sname, va) in hits[:4]:
            lines.append(f'  "{needle}" in {sname} @ 0x{va:08X}')
            s = read_str(va)
            if s: lines.append(f'    -> "{s[:80]}"')

    # ── 6. IN_Frame candidate — called from main loop ─────────────────────
    # Look for very short functions (10-40 insns) that call XInputGetState
    # near the game loop area (around 0x15000-0x20000)
    H('=== Potential IN_Frame candidates (short fns 0x10000-0x20000) ===')
    # Scan for call [mem32] patterns in that range which might be XInput vtable
    chunk_range = raw[text_sec['roff']:text_sec['roff']+text_sec['rsz']]
    for offset in range(0, min(0x20000, len(chunk_range)), 1):
        va_here = text_sec['va'] + offset
        if 0x10000 <= va_here <= 0x20000:
            b = chunk_range[offset:offset+3]
            # ff 15 XX XX XX XX = call [mem32]
            if len(b)>=2 and b[0]==0xFF and b[1]==0x15:
                if offset+6 <= len(chunk_range):
                    tgt = struct.unpack_from('<I', chunk_range, offset+2)[0]
                    nm = thunks.get(tgt,'')
                    if nm:
                        fn = find_enclosing_fn(raw, va2off, text_sec, va_here)
                        lines.append(f'  thunk_call {nm} @ 0x{va_here:08X}  fn~0x{fn:08X}' if fn else f'  thunk_call {nm} @ 0x{va_here:08X}')

    with open(out_path,'w',encoding='utf-8') as f:
        f.write('\n'.join(lines)+'\n')
    print(f'Phase 3 Input written: {out_path}')

if __name__=='__main__':
    main()
