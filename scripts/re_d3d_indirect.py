"""
re_d3d_indirect.py
Find the D3D CreateDevice call in the renderer section of .text.
Since all direct calls into D3D are in the Bink wrapper, the renderer
must use indirect (vtable) calls.

Strategy:
  1. Scan .text (excluding first 0x50000 bytes = Bink/startup area) for
     indirect CALLs: call dword ptr [eax], call dword ptr [eax+N], etc.
  2. For each indirect call, check the preceding 60 insns for:
     - Writes to consecutive stack offsets (MOV [esp+N], imm) suggesting
       D3DPRESENT_PARAMETERS construction
     - The magic values: 640, 480, D3DFMT values
  3. Separately scan for: sub esp, N where N >= 0x20 (large stack frame)
     followed by a series of mov [esp+X], imm writes → likely PP construction
  4. Dump the D3DPRESENT_PARAMETERS area
"""

import sys, struct, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
from capstone import *
from capstone.x86 import *
from collections import defaultdict

XBE_EP_RETAIL_KEY = 0xA8FC57AB

# Xbox D3DFMT subset
D3DFMT_XBOX = {
    0x01:'D3DFMT_A8R8G8B8(tiled)',
    0x02:'D3DFMT_X8R8G8B8(tiled)',
    0x03:'D3DFMT_R5G6B5(tiled)',
    0x17:'D3DFMT_LIN_A8R8G8B8',
    0x18:'D3DFMT_LIN_X8R8G8B8',
    0x19:'D3DFMT_LIN_R5G6B5',
    0x2E:'D3DFMT_LIN_D24S8',
    0x70:'D3DFMT_D24S8(tiled)',
    0x6E:'D3DFMT_D16(tiled)',
    0x3C:'D3DFMT_LIN_D16',
    0x3D:'D3DFMT_LIN_F16',
    0x2F:'D3DFMT_LIN_F24S8',
}
PP_FIELDS = {
    0x00:'Width', 0x04:'Height', 0x08:'BackBufferFormat',
    0x0C:'BackBufferCount', 0x10:'MultiSampleType', 0x14:'SwapEffect',
    0x18:'hDeviceWindow', 0x1C:'Windowed', 0x20:'EnableAutoDepthStencil',
    0x24:'AutoDepthStencilFormat', 0x28:'Flags',
    0x2C:'RefreshRateInHz', 0x30:'PresentationInterval',
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

def main():
    path = sys.argv[1] if len(sys.argv)>1 else r'Star Wars Jedi Academy game\default.xbe'
    raw,base,sections,va2file,section_of,ep = load_xbe(path)

    text_sec = next(s for s in sections if s['name']=='.text')
    d3d_sec  = next(s for s in sections if s['name']=='D3D')
    d3d_lo   = d3d_sec['va']
    d3d_hi   = d3d_sec['va'] + d3d_sec['vsz']

    print(f"Base: 0x{base:08X}  EP: 0x{ep:08X}")
    print(f"D3D section: 0x{d3d_lo:08X}–0x{d3d_hi:08X}")
    print(f".text section: 0x{text_sec['va']:08X}–0x{text_sec['va']+text_sec['vsz']:08X}")

    # ── Strategy A: scan for D3DPRESENT_PARAMETERS construction ─────────────
    # Look for: SUB ESP, N (N >= 0x20 suggesting a frame)
    # followed within 60 insns by: MOV [ESP+0], imm, MOV [ESP+4], imm etc.
    # The key signature is: Width (640) at [base], Height (480) at [base+4],
    # or Format at [base+8].
    # We scan via raw binary for the target constants in context.

    print(f"\n{'='*72}")
    print("Strategy A: Scan .text for ESP-relative D3DPRESENT_PARAMETERS construction")
    print(f"{'='*72}")

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True

    # Skip first 0x50000 bytes (Bink + startup area ends around 0x60000)
    # Actually let's skip the first 0x40000 to be safe but not miss too much
    skip_start = 0x50000
    scan_va_start = text_sec['va'] + skip_start
    scan_raw_start = va2file(scan_va_start)
    if scan_raw_start is None:
        scan_va_start = text_sec['va']
        scan_raw_start = text_sec['roff']
    scan_size = text_sec['vsz'] - skip_start

    print(f"Scanning 0x{scan_va_start:08X} onward ({scan_size//1024} KB)")
    chunk = raw[scan_raw_start: scan_raw_start + scan_size]

    # Collect all instructions
    # This will be large; process in windows
    WINDOW = 5000   # insns per window
    total_hits = []

    # Collect all MOV [ESP+disp], imm where val is a D3DFMT or width/height
    print("Collecting marker writes (640, 480, D3DFMT values)...")
    markers = []   # (va, disp, val, note)
    fmt_vals = set(D3DFMT_XBOX.keys())

    for ins in md.disasm(chunk, scan_va_start):
        if ins.id != X86_INS_MOV: continue
        ops = ins.operands
        if len(ops) != 2: continue
        dst, src = ops
        if dst.type != X86_OP_MEM: continue
        if src.type != X86_OP_IMM: continue
        val = src.imm & 0xFFFFFFFF
        disp = dst.mem.disp
        base_reg = dst.mem.base
        if val in (640, 480, 320, 240, 720, 576):
            markers.append((ins.address, disp, val, f'W/H={val}'))
        elif val in fmt_vals:
            markers.append((ins.address, disp, val, f'FMT={D3DFMT_XBOX[val]}'))

    print(f"Found {len(markers)} marker writes")

    # Group markers that are close to each other (within 0x300 bytes)
    clusters = []
    for m in markers:
        placed = False
        for c in clusters:
            if abs(m[0] - c[-1][0]) < 0x400:
                c.append(m)
                placed = True
                break
        if not placed:
            clusters.append([m])

    # Filter clusters with both W/H and FMT writes — strong D3DPP signal
    strong_clusters = [c for c in clusters
                       if any('W/H' in m[3] for m in c) and any('FMT' in m[3] for m in c)]

    print(f"\nStrong clusters (W/H + FMT): {len(strong_clusters)}")
    for i, cluster in enumerate(strong_clusters[:10]):
        print(f"\n  Cluster {i+1}: {len(cluster)} markers near 0x{cluster[0][0]:08X}")
        for m in cluster:
            print(f"    0x{m[0]:08X}: [reg+0x{m[1]:02X}] = {m[2]}  ({m[3]})")

    # ── Strategy B: look for the D3DPRESENT_PARAMETERS based on Xbox layout ─
    # The Xbox D3DPRESENT_PARAMETERS has Width at +0, Height at +4 (unlike PC).
    # Scan for: MOV [reg+0], 0x280 (640) AND MOV [reg+4], 0x1E0 (480)
    # within 100 bytes of each other.

    print(f"\n{'='*72}")
    print("Strategy B: Find [reg+0]=640 AND [reg+4]=480 within 200 bytes")
    print(f"{'='*72}")

    # Collect all [reg+0]=640 and [reg+4]=480 writes
    writes_0_640 = []
    writes_4_480 = []
    all_esp_writes = []  # (va, disp, val) for [esp+disp]=val or [ebp+disp]=val

    for ins in md.disasm(chunk, scan_va_start):
        if ins.id != X86_INS_MOV: continue
        ops = ins.operands
        if len(ops) != 2: continue
        dst, src = ops
        if dst.type != X86_OP_MEM: continue
        if src.type != X86_OP_IMM: continue
        val = src.imm & 0xFFFFFFFF
        disp = dst.mem.disp
        reg  = dst.mem.base
        if val == 640 and disp == 0: writes_0_640.append(ins.address)
        if val == 480 and disp == 4: writes_4_480.append(ins.address)
        if reg in (X86_REG_ESP, X86_REG_EBP):
            all_esp_writes.append((ins.address, disp, val))

    print(f"  [reg+0]=640: {len(writes_0_640)} hits")
    print(f"  [reg+4]=480: {len(writes_4_480)} hits")

    for a0 in writes_0_640:
        for a4 in writes_4_480:
            if abs(a0 - a4) < 200:
                print(f"\n  MATCH! [r+0]=640 @ 0x{a0:08X}, [r+4]=480 @ 0x{a4:08X}")
                print(f"  (distance: {abs(a0-a4)} bytes)")

    # ── Strategy C: scan for MOV [esp+0], imm AND MOV [esp+4], imm ──────────
    print(f"\n{'='*72}")
    print("Strategy C: Find MOV [esp+0],N AND MOV [esp+4],N both within 200 bytes")
    print("  (D3DPRESENT_PARAMETERS on stack — Width then Height)")
    print(f"{'='*72}")

    esp_0_writes = [(a,v) for a,d,v in all_esp_writes if d == 0 and 320<=v<=1280]
    esp_4_writes = [(a,v) for a,d,v in all_esp_writes if d == 4 and 240<=v<=1024]

    for a0,w in esp_0_writes:
        for a4,h in esp_4_writes:
            if abs(a0-a4) < 300:
                print(f"  [esp+0]={w} @ 0x{a0:08X}, [esp+4]={h} @ 0x{a4:08X}")

    # ── Strategy D: find ALL [ebp+disp]/[esp+disp] writes of D3DFMT values ──
    print(f"\n{'='*72}")
    print("Strategy D: All D3DFMT value writes on stack (renderer section)")
    print(f"{'='*72}")

    fmt_writes = [(a,d,v) for a,d,v in all_esp_writes if v in fmt_vals]
    print(f"  Found {len(fmt_writes)} D3DFMT writes")
    for a,d,v in fmt_writes[:30]:
        sig = f'-0x{(-d)&0xFFFFFFFF:02X}' if d < 0 else f'+0x{d:02X}'
        print(f"  0x{a:08X}: [esp/ebp {sig}] = 0x{v:02X} ({D3DFMT_XBOX[v]})")

    # ── Strategy E: look for the D3D section address range being loaded ──────
    print(f"\n{'='*72}")
    print(f"Strategy E: Find PUSH or MOV with D3D section VAs (0x{d3d_lo:08X}–0x{d3d_hi:08X})")
    print(f"{'='*72}")

    d3d_refs = []
    for ins in md.disasm(chunk, scan_va_start):
        if ins.operands:
            for op in ins.operands:
                if op.type == X86_OP_IMM:
                    v = op.imm & 0xFFFFFFFF
                    if d3d_lo <= v < d3d_hi:
                        d3d_refs.append((ins.address, v, ins.mnemonic, ins.op_str))
    print(f"  Found {len(d3d_refs)} D3D VA references")
    for a,v,m,s in d3d_refs[:20]:
        print(f"  0x{a:08X}: {m} {s}  → D3D+0x{v-d3d_lo:X}")

    # ── Strategy F: look for indirect CALLs after D3DFMT writes ─────────────
    print(f"\n{'='*72}")
    print("Strategy F: Indirect CALLs within 200 bytes after D3DFMT writes")
    print(f"{'='*72}")

    # Collect all indirect calls in renderer section
    indirect_calls = []
    for ins in md.disasm(chunk, scan_va_start):
        if ins.id == X86_INS_CALL and ins.operands:
            op = ins.operands[0]
            if op.type == X86_OP_MEM:
                indirect_calls.append(ins.address)

    print(f"  {len(indirect_calls)} indirect calls in renderer section")
    # Find ones close after D3DFMT writes
    fmt_vas = [a for a,d,v in fmt_writes]
    for icall in indirect_calls:
        for fva in fmt_vas:
            if 0 < icall - fva < 300:
                print(f"  D3DFMT write @ 0x{fva:08X}, indirect call @ 0x{icall:08X}  (+0x{icall-fva:X})")

    # ── Strategy G: look for the string "Failed to create" in .data ─────────
    print(f"\n{'='*72}")
    print("Strategy G: Find 'Failed to create' string → trace xref back to CreateDevice")
    print(f"{'='*72}")

    # Search raw bytes for the string
    rdata_sec = next((s for s in sections if s['name'] in ('.rdata','.data')), None)
    needle = b'Failed to create'
    pos = 0
    found_strs = []
    while True:
        pos = raw.find(needle, pos)
        if pos == -1: break
        # Convert file offset back to VA
        for s in sections:
            if s['roff'] <= pos < s['roff'] + s['rsz']:
                va = s['va'] + (pos - s['roff'])
                end = raw.find(b'\x00', pos, pos+128)
                text = raw[pos:end].decode('ascii','replace')
                found_strs.append((va, text))
                break
        pos += 1

    print(f"  Found {len(found_strs)} 'Failed to create' strings:")
    for va,text in found_strs:
        print(f"  0x{va:08X}: '{text}'")

    if found_strs:
        # Search for these VA values in .text
        for str_va, text in found_strs:
            sv = struct.pack('<I', str_va)
            search_in = raw[text_sec['roff']: text_sec['roff'] + text_sec['rsz']]
            spos = 0
            while True:
                spos = search_in.find(sv, spos)
                if spos == -1: break
                ref_va = text_sec['va'] + spos
                print(f"\n  Xref to '{text}' at 0x{ref_va:08X}")
                # Disassemble around this reference
                ctx_start = ref_va - 0x80
                off = va2file(ctx_start)
                if off:
                    ctx_md = Cs(CS_ARCH_X86, CS_MODE_32)
                    ctx_md.detail = True
                    ctx_insns = list(ctx_md.disasm(raw[off:off+0x200], ctx_start))
                    for ci in ctx_insns:
                        note = ' <<<' if abs(ci.address - ref_va) < 8 else ''
                        print(f"    0x{ci.address:08X}: {ci.mnemonic:<8s} {ci.op_str}{note}")
                spos += 1

    print(f"\n{'='*72}")
    print("Done.")

if __name__ == '__main__':
    main()
