#!/usr/bin/env python3
"""
compare_init_calls.py
For CDevice::Init in both retail (XDK 5558) and our build (XDK 5849 d3d8ltcg),
extract the ordered sequence of called functions and other behaviors. Use this
to identify what our Init does that retail doesn't (or vice versa).
"""
import re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

RETAIL_FILE = r"C:\Programming\GitHub\Jedi-Academy-X\scripts\output\full_text_disasm.txt"
OURS_FILE   = r"C:\Users\smmel\AppData\Local\Temp\default_disasm_ascii.txt"

# Retail target VA -> reasonable name (where we can identify)
RETAIL_NAMES = {
    0x23F570: "Direct3D_CreateDevice",
    0x247CD0: "CDevice::Init",
    0x241B00: "CDevice::InitializePushBuffer",
    0x24350D: "CMiniport::InitHardware",
    0x2431FB: "CMiniport::CreateCtxDmaObject",
    0x242F83: "CMiniport::InitDMAChannel",
    0x242FE6: "CMiniport::CreateOneCtxDmaObject_helper",  # called many times
    0x2432CA: "CMiniport::CreateContextDma_variant",
    0x23F620: "D3D_AllocContiguousMemory",
    0xC2CCA:  "XMemAlloc(.text wrapper)",
}

def extract_calls(path, start_label, is_retail):
    """Return ordered list of (rel_offset, target_repr) for each call insn
    inside the function body."""
    with open(path,'r',encoding='utf-8',errors='replace') as f:
        data = f.read()
    # Find function start
    m = re.search(r'^\s*' + re.escape(start_label) + r'\s*:.*?$', data, re.MULTILINE)
    if not m:
        return None
    start = m.end()
    # Find end: int3 padding followed by next non-CC content (heuristic)
    body_text = data[start:start+200000]  # cap
    calls = []
    base_va = None
    int3_streak = 0
    for line in body_text.splitlines():
        line = line.rstrip()
        if not line: continue
        if line.startswith('; '):
            continue
        # Function or section header in retail file
        if re.match(r'^\s*[A-Za-z_$@?]\w[\w@?$.]*\s*:\s*(?:\s*;.*)?$', line) and 'sub_' in line:
            # next function — stop
            break
        # In ours file, dumpbin uses no leading space for function headers
        if not line.startswith(' ') and ':' in line and not re.match(r'^\s*[0-9A-Fa-f]+:', line):
            break
        m = re.match(r'^\s*([0-9A-Fa-f]+):\s+([0-9A-Fa-f ]+?)\s{2,}(\S+)(?:\s+(.*?))?\s*$', line)
        if not m: continue
        addr_hex, raw, mnem, ops = m.groups()
        addr = int(addr_hex, 16)
        if base_va is None: base_va = addr
        if mnem.lower() == 'int3':
            int3_streak += 1
            if int3_streak >= 8:
                break
            continue
        else:
            int3_streak = 0
        if mnem.lower() == 'call':
            ops_clean = (ops or '').strip()
            target = ops_clean
            # Resolve retail VA targets
            if is_retail:
                tm = re.match(r'0x([0-9A-Fa-f]+)', ops_clean)
                if tm:
                    tva = int(tm.group(1), 16)
                    name = RETAIL_NAMES.get(tva, '')
                    target = f"0x{tva:X}" + (f" {name}" if name else "")
                else:
                    target = ops_clean
            else:
                # In ours, ops_clean is the symbol name
                # Strip address prefix if present
                target = re.sub(r'^[0-9A-Fa-f]+\s+','', ops_clean)
            calls.append((addr - base_va, target))
    return calls, base_va

def main():
    print("Retail CDevice::Init (sub_00247CD0)")
    retail_calls, rb = extract_calls(RETAIL_FILE, "sub_00247CD0", is_retail=True)
    if retail_calls is None:
        print("  not found"); return
    print(f"  base VA: 0x{rb:X}, {len(retail_calls)} calls")
    print()
    print(f"  {'Off':>6}  Target")
    print(f"  {'-'*6}  {'-'*60}")
    for off, t in retail_calls:
        print(f"  +{off:04X}  {t}")
    print()

    print("Our CDevice::Init")
    our_calls, ob = extract_calls(OURS_FILE,
        "?Init@CDevice@D3D@@QAEJPAU_D3DPRESENT_PARAMETERS_@@@Z", is_retail=False)
    if our_calls is None:
        print("  not found"); return
    print(f"  base VA: 0x{ob:X}, {len(our_calls)} calls")
    print()
    print(f"  {'Off':>6}  Target")
    print(f"  {'-'*6}  {'-'*60}")
    for off, t in our_calls:
        print(f"  +{off:04X}  {t}")

if __name__ == "__main__":
    main()
