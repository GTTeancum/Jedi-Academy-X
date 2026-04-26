"""
re_phase2_anchors.py
Phase 2: Anchor id Tech 3 subsystems.
  1. Trace entry point call chain (CRT startup → main)
  2. Find subsystem init functions via string xrefs:
     - FS_Startup, Com_Init, CL_Init, SV_Init, R_Init, S_Init, IN_Init, Sys_*
  3. Trace each init function's call graph (one level deep)
  4. Identify Sys_* platform functions (file I/O, memory, time)
Output: scripts/output/phase2_anchors.txt
"""

import sys, struct, os, io, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
from capstone import *
from capstone.x86 import *

XBE_EP_RETAIL = 0xA8FC57AB
XBE_KT_RETAIL = 0x5B6D40B6

def load_xbe(path):
    with open(path, 'rb') as f:
        raw = f.read()
    assert raw[:4] == b'XBEH'
    base      = struct.unpack_from('<I', raw, 0x104)[0]
    ep_enc    = struct.unpack_from('<I', raw, 0x128)[0]
    sec_count = struct.unpack_from('<I', raw, 0x11C)[0]
    sec_hdr   = struct.unpack_from('<I', raw, 0x120)[0]
    ep = (ep_enc ^ XBE_EP_RETAIL) & 0xFFFFFFFF
    sections = []
    for i in range(sec_count):
        o = (sec_hdr - base) + i * 0x38
        va   = struct.unpack_from('<I', raw, o + 0x04)[0]
        vsz  = struct.unpack_from('<I', raw, o + 0x08)[0]
        roff = struct.unpack_from('<I', raw, o + 0x0C)[0]
        rsz  = struct.unpack_from('<I', raw, o + 0x10)[0]
        nva  = struct.unpack_from('<I', raw, o + 0x14)[0]
        noff = nva - base
        name = b''
        if 0 <= noff < len(raw):
            end = raw.find(b'\x00', noff, noff + 32)
            name = raw[noff : end if end != -1 else noff+16]
        sections.append({'name': name.decode('ascii','replace').strip(),
                         'va': va, 'vsz': vsz, 'roff': roff, 'rsz': rsz})

    def va2off(va):
        for s in sections:
            if s['va'] <= va < s['va'] + s['vsz']:
                return s['roff'] + (va - s['va'])
        return None

    def sec_of(va):
        for s in sections:
            if s['va'] <= va < s['va'] + s['vsz']:
                return s['name']
        return '???'

    # Build a flat searchable image (VA→raw) — map each section
    return raw, base, sections, va2off, sec_of, ep

def disasm(raw, va2off, start_va, max_insns=200):
    off = va2off(start_va)
    if off is None: return []
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    chunk = raw[off : off + max_insns * 15]
    insns = list(md.disasm(chunk, start_va))
    return insns[:max_insns]

def disasm_until_ret(raw, va2off, start_va, max_insns=500):
    """Disassemble until ret/retn or max_insns."""
    off = va2off(start_va)
    if off is None: return []
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    chunk = raw[off : off + max_insns * 15]
    result = []
    for ins in md.disasm(chunk, start_va):
        result.append(ins)
        if ins.id in (X86_INS_RET, X86_INS_RETF):
            break
        if len(result) >= max_insns:
            break
    return result

def get_direct_calls(insns):
    """Return list of (call_va, target_va) for direct calls."""
    calls = []
    for ins in insns:
        if ins.id == X86_INS_CALL and ins.operands and ins.operands[0].type == X86_OP_IMM:
            calls.append((ins.address, ins.operands[0].imm & 0xFFFFFFFF))
    return calls

def fmt_insn(ins, sec_of, known_funcs=None):
    tag = ''
    if ins.id == X86_INS_CALL and ins.operands and ins.operands[0].type == X86_OP_IMM:
        tgt = ins.operands[0].imm & 0xFFFFFFFF
        sec = sec_of(tgt)
        name = known_funcs.get(tgt, '') if known_funcs else ''
        tag = f'  → {sec} 0x{tgt:08X}' + (f' [{name}]' if name else '')
    elif ins.id == X86_INS_CALL and ins.operands and ins.operands[0].type == X86_OP_MEM:
        tag = '  → [indirect/vtable]'
    return f'  0x{ins.address:08X}:  {ins.mnemonic:<8} {ins.op_str}{tag}'

def scan_string_xrefs(raw, sections, va2off, needle_va):
    """Find all places in .text that reference needle_va as an immediate."""
    needle_bytes = struct.pack('<I', needle_va)
    text_sec = next((s for s in sections if s['name'] == '.text'), None)
    if not text_sec: return []
    base_off = text_sec['roff']
    results = []
    chunk = raw[base_off : base_off + text_sec['rsz']]
    pos = 0
    while True:
        idx = chunk.find(needle_bytes, pos)
        if idx == -1: break
        va = text_sec['va'] + idx
        results.append(va)
        pos = idx + 1
    return results

def find_enclosing_function(raw, va2off, text_sec, ref_va, lookback=0x400):
    """Walk backwards from ref_va to find a plausible function start (PUSH EBP / MOV EBP,ESP or SUB ESP,xx)."""
    off = va2off(ref_va)
    if off is None: return None
    # Simple heuristic: look for 55 8B EC (push ebp; mov ebp, esp) or
    # 53 (push ebx), 56 (push esi), 57 (push edi) prologue patterns
    start = max(off - lookback, text_sec['roff'])
    chunk = raw[start:off+1]
    # Scan backwards for common x86 prologues
    candidates = []
    for i in range(len(chunk)-3, 0, -1):
        # PUSH EBP; MOV EBP, ESP
        if chunk[i] == 0x55 and chunk[i+1] == 0x8B and chunk[i+2] == 0xEC:
            candidates.append(start - text_sec['roff'] + i + text_sec['va'])
            break
        # SUB ESP, imm8/32
        if i > 0 and chunk[i] == 0x83 and chunk[i+1] == 0xEC:
            candidates.append(start - text_sec['roff'] + i + text_sec['va'])
            break
    return candidates[0] if candidates else None

def find_string_va(raw, sections, needle):
    """Find the actual null-terminated string start containing needle in .rdata.
    Walks backwards from the substring match to the preceding null byte so the
    returned VA is what the compiler PUSH-es (the full string including any prefix)."""
    rdata = next((s for s in sections if s['name'] == '.rdata'), None)
    if not rdata: return None
    chunk = raw[rdata['roff'] : rdata['roff'] + rdata['rsz']]
    enc = needle.encode('ascii')
    idx = chunk.find(enc)
    if idx == -1: return None
    # Walk backwards to find the start of this null-terminated string
    start = idx
    while start > 0 and chunk[start - 1] != 0:
        start -= 1
    return rdata['va'] + start

# ── Known landmark strings → function names ────────────────────────────────
LANDMARK_STRINGS = {
    # Strings as they appear in .rdata (substring match, \n included in binary but we match the text)
    'FS_Startup':                        'FS_Startup',
    'Client Initialization':             'CL_Init',
    'CL_Shutdown':                       'CL_Shutdown',
    'Initializing Renderer':             'R_Init_or_caller',
    'Server Initialization':             'SV_Init',
    'Sys_QueEvent: overflow':            'Sys_QueEvent',
    'WARNING: List file %s not found':   'Sys_ListFiles_or_caller',
    'd:\\base\\':                        'FS_basepath_area',
    'Initialising zone memory':          'Z_Init',
    'Ghoul2 Model has no mdxa':          'G2_caller',
    'GLimp_Init() - Invalid GL Driver':  'GLimp_Init',
    'Error during initialization %s':    'Com_Init_or_caller',
    'S_HashName: empty name':            'S_HashName',
    'S_StartSound: bad entitynum %i':    'S_StartSound_or_caller',
    'DirectSound3D':                     'S_DirectSound3D_init',
    'Client Initialization Complete':    'CL_Init_complete',
    'Bad cgame system trap':             'CL_cgame_syscall',
    'Ghoul2 model was reloaded':         'G2_model_reload',
    'R_RenderScene: NULL worldmodel':    'R_RenderScene_or_caller',
    'RE_BeginFrame':                     'RE_BeginFrame_or_caller',
    'GLimp_Init':                        'GLimp_Init_str',
    'Vicarious Visions':                 'VV_ident',
    'Failed to create device':           'CreateDevice_caller',
    'Direct3D_SetPushBufferSize':        'PushBufferSize_caller',
    'xboxErrorResponse':                 'UI_Xbox_error',
    'Ran out of transform space':        'G2_transform_space',
    'Z:\\humanoidglaswap':               'Sys_humanoidGLA',
    'Z:\\cinematicglaswap':              'Sys_cinematicGLA',
    'd:\\base\\soundbank\\sound.tbl':    'S_LoadSoundBank',
}

def main():
    xbe_path = sys.argv[1] if len(sys.argv) > 1 else \
        r'Star Wars Jedi Academy game\default.xbe'

    script_dir = os.path.dirname(os.path.abspath(__file__))
    out_dir    = os.path.join(script_dir, 'output')
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, 'phase2_anchors.txt')

    raw, base, sections, va2off, sec_of, ep = load_xbe(xbe_path)
    text_sec = next(s for s in sections if s['name'] == '.text')

    lines = []
    known = {}   # va → name

    def H(t):
        lines.append('')
        lines.append('=' * 72)
        lines.append(t)
        lines.append('=' * 72)

    def disasm_fn(va, label, max_insns=150):
        H(f'{label}  @ 0x{va:08X}')
        insns = disasm_until_ret(raw, va2off, va, max_insns)
        if not insns:
            lines.append('  [no disassembly — VA not in any section]')
            return []
        for ins in insns:
            lines.append(fmt_insn(ins, sec_of, known))
        lines.append(f'  [{len(insns)} instructions]')
        return insns

    # ── 1. Entry point ─────────────────────────────────────────────────────
    H(f'ENTRY POINT @ 0x{ep:08X} ({sec_of(ep)})')
    ep_insns = disasm_until_ret(raw, va2off, ep, 300)
    for ins in ep_insns:
        lines.append(fmt_insn(ins, sec_of, known))
    direct_calls_ep = get_direct_calls(ep_insns)
    lines.append(f'  [Direct calls from entry point: {len(direct_calls_ep)}]')
    for cva, tva in direct_calls_ep:
        lines.append(f'    call @ 0x{cva:08X} → 0x{tva:08X} [{sec_of(tva)}]')

    # Follow each direct call from entry point one level (looking for main/init)
    for cva, tva in direct_calls_ep:
        if sec_of(tva) == '.text':
            sub = disasm_until_ret(raw, va2off, tva, 200)
            sub_calls = get_direct_calls(sub)
            H(f'  Callee @ 0x{tva:08X} (called from EP+0x{cva-ep:X})')
            for ins in sub:
                lines.append(fmt_insn(ins, sec_of, known))
            lines.append(f'  [Direct calls: {len(sub_calls)}]')
            for cc, ct in sub_calls:
                lines.append(f'    call @ 0x{cc:08X} → 0x{ct:08X} [{sec_of(ct)}]')

    # ── 2. Landmark string xrefs → function anchors ────────────────────────
    H('LANDMARK STRING XREFS → FUNCTION ANCHORS')
    anchors = {}
    for needle, fname in LANDMARK_STRINGS.items():
        sva = find_string_va(raw, sections, needle)
        if sva is None:
            lines.append(f'  STRING NOT FOUND: {needle!r}')
            continue
        # Find all .text references to this string VA
        xrefs = scan_string_xrefs(raw, sections, va2off, sva)
        lines.append(f'  {fname}:')
        lines.append(f'    string @ 0x{sva:08X}  xrefs: {len(xrefs)}')
        for xref in xrefs[:5]:
            fn_va = find_enclosing_function(raw, va2off, text_sec, xref)
            fn_str = f'0x{fn_va:08X}' if fn_va else '???'
            lines.append(f'    xref @ 0x{xref:08X}  enclosing fn ≈ {fn_str}')
            if fn_va and fn_va not in anchors:
                anchors[fn_va] = fname
                known[fn_va] = fname

    # ── 3. Disassemble each anchor function ────────────────────────────────
    H('ANCHOR FUNCTION DISASSEMBLY')
    seen_fns = set()
    for va, name in sorted(anchors.items()):
        if va in seen_fns: continue
        seen_fns.add(va)
        disasm_fn(va, name)

    # ── 4. Specific additional anchors ─────────────────────────────────────
    # Find main() by looking for 'Direct3D_SetPushBufferSize' pattern or
    # for the function that contains 'Error during initialization'
    H('Sys_* PLATFORM LAYER SCAN')
    # Find Sys_Milliseconds candidate: look for KeQueryPerformanceCounter usage
    # It will be a small function calling KeQueryPerformanceCounter (thunk in .rdata)
    # Scan .text for "call [addr in .rdata]" and correlate
    rdata_sec = next(s for s in sections if s['name'] == '.rdata')

    # More targeted: scan .text for call [mem] where mem is in .rdata
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    text_off  = text_sec['roff']
    text_va   = text_sec['va']
    text_size = text_sec['rsz']
    text_chunk = raw[text_off : text_off + min(text_size, 0x240000)]

    kt_va = (struct.unpack_from('<I', raw, 0x158)[0] ^ XBE_KT_RETAIL) & 0xFFFFFFFF
    kt_off = va2off(kt_va)

    # Build ordinal→VA map for thunk table entries
    thunk_to_name = {}
    KRNL_SELECT = {
        127: 'KeQueryPerformanceCounter',
        128: 'KeQueryPerformanceFrequency',
        155: 'KeTickCount',
        129: 'KeQuerySystemTime',
        189: 'NtCreateFile',
        218: 'NtReadFile',
        234: 'NtWriteFile',
        186: 'NtClose',
        198: 'NtFreeVirtualMemory',
        183: 'NtAllocateVirtualMemory',
        164: 'MmAllocateContiguousMemory',
        165: 'MmAllocateContiguousMemoryEx',
        170: 'MmFreeContiguousMemory',
        3:   'AvSetDisplayMode',
        2:   'AvSendTVEncoderOption',
        100: 'AvSetSavedDataAddress',
        1:   'AvGetSavedDataAddress',
        252: 'PsCreateSystemThread',
        253: 'PsCreateSystemThreadEx',
        325: 'XeLoadSection',
        326: 'XeUnloadSection',
    }

    if kt_off is not None:
        i = 0
        while kt_off + i*4 + 4 <= len(raw):
            val = struct.unpack_from('<I', raw, kt_off + i*4)[0]
            if val == 0: break
            if not (val & 0x80000000): break
            ord_ = val & 0x7FFFFFFF
            thunk_slot_va = kt_va + i * 4
            if ord_ in KRNL_SELECT:
                thunk_to_name[thunk_slot_va] = KRNL_SELECT[ord_]
            i += 1

    lines.append(f'  Thunk slot VAs for key kernel functions:')
    for tva_slot, tname in sorted(thunk_to_name.items(), key=lambda x: x[1]):
        lines.append(f'    [0x{tva_slot:08X}] = {tname}')

    # Find callers of KeQueryPerformanceCounter — likely Sys_Milliseconds
    perf_slots = {va: n for va, n in thunk_to_name.items()
                  if n == 'KeQueryPerformanceCounter'}
    if perf_slots:
        slot_va = next(iter(perf_slots))
        needle  = struct.pack('<I', slot_va)  # FF 15 <slot_va> = call [slot_va]
        call_pattern = b'\xFF\x15' + needle
        H(f'Callers of KeQueryPerformanceCounter (thunk slot 0x{slot_va:08X})')
        pos = 0
        count = 0
        while count < 20:
            idx = text_chunk.find(call_pattern, pos)
            if idx == -1: break
            call_va = text_va + idx
            fn_va   = find_enclosing_function(raw, va2off, text_sec, call_va)
            fn_str  = f'0x{fn_va:08X}' if fn_va else '???'
            lines.append(f'  call [KeQueryPerf] @ 0x{call_va:08X}  fn ≈ {fn_str}')
            if fn_va:
                insns = disasm_until_ret(raw, va2off, fn_va, 60)
                for ins in insns:
                    lines.append('  ' + fmt_insn(ins, sec_of, known))
            pos = idx + 1
            count += 1

    # Find callers of AvSetDisplayMode — likely GLimp or direct D3D init
    av_slots = {va: n for va, n in thunk_to_name.items()
                if n == 'AvSetDisplayMode'}
    if av_slots:
        slot_va = next(iter(av_slots))
        call_pattern = b'\xFF\x15' + struct.pack('<I', slot_va)
        H(f'Callers of AvSetDisplayMode (thunk slot 0x{slot_va:08X})')
        pos = 0
        count = 0
        while count < 5:
            idx = text_chunk.find(call_pattern, pos)
            if idx == -1: break
            call_va = text_va + idx
            fn_va   = find_enclosing_function(raw, va2off, text_sec, call_va)
            fn_str  = f'0x{fn_va:08X}' if fn_va else '???'
            lines.append(f'  call [AvSetDisplayMode] @ 0x{call_va:08X}  fn ≈ {fn_str}')
            if fn_va:
                insns = disasm_until_ret(raw, va2off, fn_va, 80)
                for ins in insns:
                    lines.append('  ' + fmt_insn(ins, sec_of, known))
            pos = idx + 1
            count += 1

    # Find callers of PsCreateSystemThread — threading setup
    ps_slots = {va: n for va, n in thunk_to_name.items()
                if n in ('PsCreateSystemThread', 'PsCreateSystemThreadEx')}
    if ps_slots:
        for slot_va, sname in ps_slots.items():
            call_pattern = b'\xFF\x15' + struct.pack('<I', slot_va)
            H(f'Callers of {sname} (thunk slot 0x{slot_va:08X})')
            pos = 0
            count = 0
            while count < 8:
                idx = text_chunk.find(call_pattern, pos)
                if idx == -1: break
                call_va = text_va + idx
                fn_va   = find_enclosing_function(raw, va2off, text_sec, call_va)
                lines.append(f'  call [{sname}] @ 0x{call_va:08X}  fn ≈ 0x{fn_va:08X}' if fn_va
                              else f'  call [{sname}] @ 0x{call_va:08X}  fn ≈ ???')
                pos = idx + 1
                count += 1

    # Write output
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print(f'Phase 2 anchors written: {out_path}')

if __name__ == '__main__':
    main()
