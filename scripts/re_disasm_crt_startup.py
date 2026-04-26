"""
Disassemble _mainCRTStartup (XDK 5849 xapilib) to understand what it does
before and during C++ static ctor iteration.
"""
import struct, sys, re
try:
    import capstone
except ImportError:
    print("pip install capstone"); sys.exit(1)

EXE = r"C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.exe"
MAP = r"C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.map"
OUT = r"C:\Programming\GitHub\Jedi-Academy-X\scripts\output\phase3_crt_startup.txt"

IMAGE_BASE = 0x400000

with open(EXE, "rb") as f:
    raw = f.read()

def va_to_off(va):
    return va - IMAGE_BASE

def read_at(va, size):
    off = va_to_off(va)
    if off < 0 or off + size > len(raw):
        return b''
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
    sections = [
        (0x1000,   0x25E000, ".text"),
        (0x25F000, 0x1D000,  "D3D"),
        (0x27C000, 0x27000,  "DSOUND"),
        (0x2A3000, 0x3B000,  "XONLINE"),
        (0x2DE000, 0x16000,  "XNET"),
        (0x2F4000, 0x9000,   "XPP"),
        (0x302000, 0x3D000,  ".rdata"),
        (0x33F000, 0x93F15C, ".data"),
    ]
    for (base, size, name) in sections:
        if base <= rva < base + size:
            return name
    return "?"

def disasm_fn(start_va, max_insns=80, stop_on_ret=True):
    code = read_at(start_va, max_insns * 15)
    if not code:
        return []
    out = []
    for insn in md.disasm(code, start_va):
        out.append(insn)
        if stop_on_ret and insn.mnemonic in ('ret', 'retn'):
            break
        if len(out) >= max_insns:
            break
    return out

def format_insn(insn):
    if insn.mnemonic in ('call', 'jmp') and insn.operands:
        op = insn.operands[0]
        if op.type == capstone.x86.X86_OP_IMM:
            tgt = op.imm & 0xFFFFFFFF
            name = sym_va2name.get(tgt, "")
            sect = section_of(tgt)
            if name:
                suffix = f"  -> {name}  [{sect}]"
            else:
                suffix = f"  -> 0x{tgt:08X}  [{sect}]"
        else:
            suffix = ""
    else:
        suffix = ""
    return f"  0x{insn.address:08X}:  {insn.mnemonic:<10} {insn.op_str}{suffix}"

lines = []
def out(s=""):
    lines.append(s)

# Targets to disassemble
targets = {
    0x004c503f: "_mainCRTStartup",
    0x006CF063: "XoSetupLiveSignatureEntryPoints",
    0x004CBEA9: "___onexitinit",
    0x004CF29C: "___initstdio",
    0x004D37DB: "__ioinit",
}

for va, label in targets.items():
    out(f"\n{'='*80}")
    out(f"{label}  @ 0x{va:08X}")
    out(f"{'='*80}")
    insns = disasm_fn(va, max_insns=120)
    if not insns:
        out(f"  (no code)")
        continue
    for insn in insns:
        out(format_insn(insn))

txt = '\n'.join(lines)
with open(OUT, 'w', errors='replace') as f:
    f.write(txt)
print(txt)
print(f"\nWritten to {OUT}")
