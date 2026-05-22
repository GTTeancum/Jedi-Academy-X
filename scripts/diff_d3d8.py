#!/usr/bin/env python3
"""
diff_d3d8.py
Side-by-side instruction-level diff of corresponding d3d8 functions in
retail (XDK 5558 d3d8.lib) vs our build (XDK 5849 d3d8ltcg.lib).

Strategy:
- Extract function body from both disassembly files (start at first match
  of the function header; stop at next int3-padded function start).
- Normalize: strip absolute addresses on operands & control flow targets,
  collapse to mnemonic + relative-offset operand pattern.
- Diff normalized streams; report aligned and unaligned regions.

Usage: python diff_d3d8.py
"""
import re, os, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

RETAIL = r"C:\Programming\GitHub\Jedi-Academy-X\scripts\output\full_text_disasm.txt"
OURS   = r"C:\Users\smmel\AppData\Local\Temp\default_disasm_ascii.txt"
OUT    = r"C:\Programming\GitHub\Jedi-Academy-X\scripts\output\d3d8_lib_diff.md"

# Function pairs to compare: (retail_label, ours_label, friendly_name)
PAIRS = [
    ("sub_0023F570",   "_Direct3D_CreateDevice@24",                                  "Direct3D_CreateDevice"),
    ("sub_00247CD0",   "?Init@CDevice@D3D@@QAEJPAU_D3DPRESENT_PARAMETERS_@@@Z",      "CDevice::Init"),
    ("sub_00241B00",   "?InitializePushBuffer@CDevice@D3D@@QAEJXZ",                  "CDevice::InitializePushBuffer"),
    ("sub_00243510",   "?InitHardware@CMiniport@D3D@@QAEHXZ",                        "CMiniport::InitHardware"),
    ("sub_0023F620",   "_D3D_AllocContiguousMemory@8",                               "D3D_AllocContiguousMemory"),
]

def extract_function_body(path, label):
    """Return list of (addr_hex, mnemonic, op_str, raw_bytes_str) for all lines
    between the function label and its terminator.
    The retail file uses lines like:
       sub_0023F570:  ; VA 0x0023f570
         0023F570:  A1 44 FC 24 00     mov  eax, dword ptr [...]
    The dumpbin (ours) file uses:
       _Direct3D_CreateDevice@24:
         006DACC0: A1 08 D8 6F 00     mov   eax, dword ptr [...]
    """
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()
    n = len(lines)
    i = 0
    # Find function start
    label_re = re.compile(r'^\s*' + re.escape(label) + r'\s*:')
    start = None
    for j, ln in enumerate(lines):
        if label_re.match(ln):
            start = j + 1
            break
    if start is None:
        return None

    body = []
    insn_re = re.compile(
        r'^\s*([0-9A-Fa-f]+):\s+'      # address
        r'((?:[0-9A-Fa-f]{2}\s*)+?)\s{2,}'  # raw bytes (2+ trailing spaces)
        r'(\S+)'                       # mnemonic
        r'(?:\s+(.*?))?\s*$'           # operands
    )
    # Some lines are continuation bytes from a multi-line instruction (no
    # address/mnemonic). Capture them and append to previous.
    cont_re = re.compile(r'^\s+([0-9A-Fa-f]{2}(?:\s[0-9A-Fa-f]{2})+)\s*$')
    pad_re  = re.compile(r'^\s*[0-9A-Fa-f]+:\s+(?:CC\s)+(?:CC)?\s*int3?\s*$')

    seen_int3_padding = False
    for k in range(start, min(start + 8000, n)):
        ln = lines[k].rstrip()
        if not ln: continue
        # Stop when we hit the NEXT function/section header
        if re.match(r'^[A-Za-z_$@?]', ln) and ':' in ln and '\t' not in ln:
            break
        if re.match(r'^\s*;\s*-{4,}', ln):
            continue
        m = insn_re.match(ln)
        if m:
            addr, raw, mnem, ops = m.groups()
            mnem_low = mnem.lower()
            # int3 padding marks end of function
            if mnem_low == 'int3' or mnem_low == 'int' or mnem_low.startswith('int3'):
                # accumulate up to 16 int3s as padding then stop
                seen_int3_padding = True
                # peek ahead: if many int3 in a row, function ended
                continue
            if seen_int3_padding:
                # We had int3 padding then real code — that means we already left
                # this function. Stop.
                break
            body.append((int(addr,16), mnem_low, (ops or '').strip(), raw.strip()))
            continue
        # Continuation bytes (multi-line operand) — append to last
        cm = cont_re.match(ln)
        if cm and body:
            body[-1] = (body[-1][0], body[-1][1], body[-1][2], body[-1][3] + ' ' + cm.group(1))
            continue
    return body

def normalize_op(s, base, len_total):
    """Normalize operand string to a comparable form."""
    if not s: return ''
    # Drop comments after ';'
    if ';' in s:
        s = s.split(';',1)[0]
    # Remove dword/word/byte size keywords
    s = re.sub(r'\b(dword|word|byte|qword)\s+ptr\s+', '', s, flags=re.I)
    # Strip dumpbin syntactic noise: 'ds:', 'offset', 'short', 'far'
    s = re.sub(r'\bds\s*:\s*', '', s, flags=re.I)
    s = re.sub(r'\boffset\s+', '', s, flags=re.I)
    s = re.sub(r'\bshort\s+', '', s, flags=re.I)
    s = re.sub(r'\bfar\s+', '', s, flags=re.I)
    s = re.sub(r'\bnear\s+', '', s, flags=re.I)
    s = re.sub(r'\bptr\b', '', s, flags=re.I)
    # Bare hex addresses (006DACD3 -> IMM)
    s = re.sub(r'\b[0-9A-Fa-f]{6,8}\b', 'IMM', s)
    # 0x-prefixed hex
    s = re.sub(r'\b0[xX][0-9A-Fa-f]+\b', 'IMM', s)
    # MASM-style hex (NNh)
    s = re.sub(r'\b[0-9A-Fa-f]+h\b', 'IMM', s)
    # Microsoft-mangled C++ symbol names ? prefix (any [A-Za-z@?_$.0-9] sequence)
    s = re.sub(r'\?\w[\w@?$.]*', 'IMM', s)
    # Underscore-prefixed C symbols (like _D3D__pDevice, _XMemAlloc@8)
    s = re.sub(r'_[A-Za-z]\w*(?:@\d+)?', 'IMM', s)
    # Decimal numbers (any 1+ digits not preceded by alphanumerics)
    s = re.sub(r'(?<![A-Za-z0-9_])\d+(?![A-Za-z0-9_])', 'IMM', s)
    # Remove whitespace AROUND punctuation (the two disasm formats differ here)
    s = re.sub(r'\s*([,\[\]+\-*])\s*', r'\1', s)
    # Collapse all remaining whitespace
    s = re.sub(r'\s+', ' ', s).strip()
    return s

def normalize_body(body):
    if body is None: return None
    out = []
    if not body: return out
    base = body[0][0]
    total = body[-1][0] - base
    for addr, mnem, ops, raw in body:
        rel = addr - base
        nops = normalize_op(ops, base, total)
        out.append((rel, mnem, nops))
    return out

def diff_normalized(retail_body, ours_body, name):
    lines = []
    lines.append(f"## {name}\n")
    if retail_body is None:
        lines.append(f"_retail body not found_\n"); return '\n'.join(lines)
    if ours_body is None:
        lines.append(f"_our body not found_\n"); return '\n'.join(lines)
    rn = normalize_body(retail_body)
    on = normalize_body(ours_body)
    lines.append(f"- Retail length: **{len(retail_body)}** instructions ({retail_body[-1][0]-retail_body[0][0]+ (1)} bytes span)")
    lines.append(f"- Ours length:   **{len(ours_body)}** instructions ({ours_body[-1][0]-ours_body[0][0]+ (1)} bytes span)")
    lines.append("")
    # Walk both pointer-style; report divergences
    i = j = 0
    aligned = 0
    diverged = 0
    examples = []
    while i < len(rn) and j < len(on):
        ri = rn[i]; oi = on[j]
        if ri[1] == oi[1] and ri[2] == oi[2]:
            aligned += 1
            i += 1; j += 1
            continue
        # Try to resync: look ahead a few instructions on either side
        resync = None
        for ahead_i in range(0, 6):
            for ahead_j in range(0, 6):
                if ahead_i + ahead_j == 0: continue
                if i+ahead_i < len(rn) and j+ahead_j < len(on):
                    if rn[i+ahead_i][1] == on[j+ahead_j][1] and rn[i+ahead_i][2] == on[j+ahead_j][2]:
                        resync = (ahead_i, ahead_j); break
            if resync: break
        # Record divergence
        diverged += max(resync[0] if resync else 1, resync[1] if resync else 1)
        if len(examples) < 25:
            window_r = rn[i:i + (resync[0] if resync else 1)]
            window_o = on[j:j + (resync[1] if resync else 1)]
            examples.append((ri[0], oi[0], window_r, window_o))
        if resync:
            i += resync[0]; j += resync[1]
        else:
            i += 1; j += 1
    # Trailing
    trail_r = len(rn) - i
    trail_o = len(on) - j
    lines.append(f"- Aligned matching instructions: **{aligned}**")
    lines.append(f"- Divergent regions: **{diverged}** instructions")
    lines.append(f"- Trailing length differences: retail+{trail_r}, ours+{trail_o}")
    lines.append("")
    if examples:
        lines.append("### Divergence examples\n")
        for k, (rrel, orel, wr, wo) in enumerate(examples, 1):
            lines.append(f"**Divergence #{k}**  (retail rel +{rrel:#x}, ours rel +{orel:#x})")
            lines.append("```")
            lines.append("RETAIL:")
            for r in wr:
                lines.append(f"  +{r[0]:04x}  {r[1]:8s} {r[2]}")
            lines.append("OURS:")
            for o in wo:
                lines.append(f"  +{o[0]:04x}  {o[1]:8s} {o[2]}")
            lines.append("```")
            lines.append("")
    return '\n'.join(lines)

if __name__ == "__main__":
    print("[+] Loading files", file=sys.stderr)
    md = ["# d3d8 lib instruction-level diff: retail vs our build", ""]
    md.append("Comparing retail XDK 5558 `d3d8.lib` (in-XBE D3D section) against our")
    md.append("statically-linked XDK 5849 `d3d8ltcg.lib` (in our default.exe).")
    md.append("")
    md.append("Operands are normalized: absolute addresses → `IMM`, symbol names → `SYM`.")
    md.append("This isolates structural divergences from address-only differences.")
    md.append("")
    for retail_lbl, ours_lbl, name in PAIRS:
        print(f"[+] {name}: {retail_lbl} vs {ours_lbl}", file=sys.stderr)
        rbody = extract_function_body(RETAIL, retail_lbl)
        obody = extract_function_body(OURS, ours_lbl)
        md.append(diff_normalized(rbody, obody, name))
        md.append("\n---\n")
    with open(OUT,'w',encoding='utf-8') as f:
        f.write('\n'.join(md))
    print(f"[+] Wrote {OUT}", file=sys.stderr)
