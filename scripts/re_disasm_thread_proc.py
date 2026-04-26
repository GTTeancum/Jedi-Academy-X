"""
Disassemble _mainCRTStartup's thread proc (0x4c4fcb) to see how it
iterates C++ ctors and calls main().
Also show what's in .rdata at 0x71c28c/0x71c290/0x71c29c (ctor table bounds).
"""
import struct, sys, re
try:
    import capstone
except ImportError:
    print("pip install capstone"); sys.exit(1)

EXE = r"C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.exe"
MAP = r"C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.map"
OUT = r"C:\Programming\GitHub\Jedi-Academy-X\scripts\output\phase3_thread_proc.txt"

IMAGE_BASE = 0x400000

with open(EXE, "rb") as f:
    raw = f.read()

def va_to_off(va):
    return va - IMAGE_BASE

def read_u32(va):
    off = va_to_off(va)
    if off < 0 or off + 4 > len(raw): return None
    return struct.unpack_from("<I", raw, off)[0]

def read_at(va, size):
    off = va_to_off(va)
    if off < 0 or off + size > len(raw): return b''
    return raw[off:off+size]

sym_va2name = {}
sym_pattern = re.compile(r'^\s+(\d{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8})')
with open(MAP, 'r', errors='replace') as f:
    for line in f:
        m = sym_pattern.match(line)
        if m:
            va = int(m.group(4), 16)
            sym_va2name[va] = m.group(3)

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

def section_of(va):
    rva = va - IMAGE_BASE
    sects = [
        (0x1000, 0x25E000, ".text"), (0x25F000, 0x1D000, "D3D"),
        (0x27C000, 0x27000, "DSOUND"), (0x2A3000, 0x3B000, "XONLINE"),
        (0x2DE000, 0x16000, "XNET"), (0x2F4000, 0x9000, "XPP"),
        (0x302000, 0x3D000, ".rdata"), (0x33F000, 0x93F15C, ".data"),
    ]
    for (b, s, n) in sects:
        if b <= rva < b + s: return n
    return "?"

def disasm_fn(start_va, max_insns=150, stop_on_ret=True):
    code = read_at(start_va, max_insns * 15)
    if not code: return []
    out = []
    for insn in md.disasm(code, start_va):
        out.append(insn)
        if stop_on_ret and insn.mnemonic in ('ret', 'retn'): break
        if len(out) >= max_insns: break
    return out

def fmt(insn):
    if insn.mnemonic in ('call', 'jmp') and insn.operands:
        op = insn.operands[0]
        if op.type == capstone.x86.X86_OP_IMM:
            tgt = op.imm & 0xFFFFFFFF
            name = sym_va2name.get(tgt, "")
            sect = section_of(tgt)
            suffix = f"  -> {name or f'0x{tgt:08X}'}  [{sect}]"
        else:
            suffix = ""
    else:
        suffix = ""
    return f"  0x{insn.address:08X}:  {insn.mnemonic:<10} {insn.op_str}{suffix}"

lines = []
def out(s=""): lines.append(s); print(s)

# Show ctor table bounds from .rdata
out("=== Ctor table bounds (referenced by _mainCRTStartup) ===")
for va, label in [(0x71c28c, "__xc_a (ctor begin)"),
                  (0x71c290, "__xc_z or mid"),
                  (0x71c29c, "__xc_z (ctor end)")]:
    val = read_u32(va)
    sym = sym_va2name.get(va, "?")
    if val is not None:
        sym2 = sym_va2name.get(val, "?")
        out(f"  [{va:08X}] {sym:40s} = 0x{val:08X}  ({sym2}  [{section_of(val)}])")
    else:
        out(f"  [{va:08X}] {sym:40s} = (unreadable)")

out()

# Also read .XAPI$LNA/.LNI/.LNZ area mentioned in map
for label, va in [("XAPI$LNA", 0x71c25c), ("XAPI$LNI", 0x71c260), ("XAPI$LNZ", 0x71c264)]:
    val = read_u32(va)
    if val is not None:
        sym2 = sym_va2name.get(val, "?")
        out(f"  [{va:08X}] {label:40s} = 0x{val:08X}  ({sym2}  [{section_of(val)}])")

out()
out("=== Thread proc @ 0x4c4fcb ===")
for insn in disasm_fn(0x4c4fcb, max_insns=150):
    out(fmt(insn))

# Follow any interesting callees
out()
out("=== _initterm (if present) ===")
initterm_va = None
for va, name in sym_va2name.items():
    if '__initterm' in name and 'initterm_e' not in name and 'INITTERM' not in name:
        initterm_va = va
        out(f"  Found: {name} @ 0x{va:08X}")
        break

if initterm_va:
    out()
    for insn in disasm_fn(initterm_va, max_insns=40):
        out(fmt(insn))

txt = '\n'.join(lines)
with open(OUT, 'w', errors='replace') as f:
    f.write(txt)
print(f"\nWritten to {OUT}")
