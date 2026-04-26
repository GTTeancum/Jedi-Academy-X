"""
Disassemble each static ctor thunk from .CRT$XCU in our rebuilt default.exe,
resolve call targets against default.map, and identify what each ctor does.
"""
import struct, sys, re
try:
    import capstone
except ImportError:
    print("pip install capstone"); sys.exit(1)

EXE = r"C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.exe"
MAP = r"C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.map"
OUT = r"C:\Programming\GitHub\Jedi-Academy-X\scripts\output\phase3_ctors.txt"

with open(EXE, "rb") as f:
    raw = f.read()

IMAGE_BASE = 0x400000

def va_to_off(va):
    """For this PE all rawoff == RVA (flat mapping), so offset = va - image_base"""
    rva = va - IMAGE_BASE
    return rva  # file offset == rva for this exe layout

def read_bytes(va, size):
    off = va_to_off(va)
    if off < 0 or off + size > len(raw):
        return b''
    return raw[off:off+size]

# Parse map for symbol lookup
sym_va2name = {}
sym_pattern = re.compile(r'^\s+(\d{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8})')
with open(MAP, 'r', errors='replace') as f:
    for line in f:
        m = sym_pattern.match(line)
        if m:
            va = int(m.group(4), 16)
            sym_va2name[va] = m.group(3)

# Static ctor VAs (from re_find_static_ctors.py output)
CTOR_VAS = [
    0x0065D990, 0x0065D9A0, 0x0065D9B0, 0x0065D9C0, 0x0065D9D0,
    0x0065DA00, 0x0065DA20, 0x0065DA40, 0x0065DA50, 0x0065DAB0,
    0x0065DAE0, 0x0065DAF0, 0x0065DB30, 0x0065DB50, 0x0065DB70,
    0x0065DB80, 0x0065DB90, 0x0065DBB0, 0x0065DBD0, 0x00683B72,
    0x00683B7D, 0x00687607, 0x00687612, 0x0065DC00, 0x0065DC10,
    0x0065DC30, 0x0065DC50, 0x0065DC70, 0x0065DC90, 0x0065DCB0,
    0x0065DCD0, 0x0065DD20, 0x0065DD30, 0x0065DD50, 0x0065DD60,
    0x0065DD70, 0x0065DDC0, 0x0065DDD0, 0x0065DDE0, 0x0065DE00,
    0x0065DE20, 0x0065DE30, 0x0065DE50, 0x0065DE70, 0x0065DEB0,
    0x0065DED0, 0x0065DEE0, 0x0065DF00, 0x0065DF10, 0x0065DF30,
    0x0065DF50, 0x0065DF70, 0x0065DF80, 0x0065DF90, 0x0065DFA0,
    0x0065DFB0, 0x0065DFC0, 0x0065DFD0, 0x0065E010, 0x0065E030,
    0x0065E050, 0x006FB122,
]

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
        (0x2FD000, 0x4030,   "D3D_URW"),
        (0x302000, 0x3D000,  ".rdata"),
        (0x33F000, 0x93F15C, ".data"),
    ]
    for (base, size, name) in sections:
        if base <= rva < base + size:
            return name
    return "?"

def disasm_fn(start_va, max_insns=30):
    """Disassemble up to max_insns at start_va, stop at ret."""
    code = read_bytes(start_va, max_insns * 10)
    if not code:
        return []
    out = []
    for insn in md.disasm(code, start_va):
        out.append(insn)
        if insn.mnemonic in ('ret', 'retn', 'jmp'):
            break
        if len(out) >= max_insns:
            break
    return out

def resolve_call(insn, sym_va2name):
    """Try to resolve a call/jmp target."""
    if insn.mnemonic in ('call', 'jmp') and insn.operands:
        op = insn.operands[0]
        if op.type == capstone.x86.X86_OP_IMM:
            tgt = op.imm & 0xFFFFFFFF
            name = sym_va2name.get(tgt, "")
            return tgt, name
    return None, None

lines = []
def out(s=""):
    lines.append(s)
    print(s)

out(f"Static ctor disassembly for {len(CTOR_VAS)} ctors")
out("=" * 80)

for idx, va in enumerate(CTOR_VAS):
    sect = section_of(va)
    sym = sym_va2name.get(va, "<?>")
    out(f"\n[{idx+1:2d}] 0x{va:08X}  {sym}  ({sect})")

    insns = disasm_fn(va, 40)
    if not insns:
        out(f"     (no code at VA)")
        continue

    for insn in insns:
        tgt, tgt_name = resolve_call(insn, sym_va2name)
        if tgt:
            sect2 = section_of(tgt)
            if tgt_name:
                suffix = f"  -> {tgt_name}  [{sect2}]"
            else:
                suffix = f"  -> 0x{tgt:08X}  [{sect2}]"
        else:
            suffix = ""
        out(f"  0x{insn.address:08X}:  {insn.mnemonic:<10} {insn.op_str}{suffix}")

out("\n" + "=" * 80)
out("Done.")

with open(OUT, 'w', errors='replace') as f:
    f.write('\n'.join(lines))
print(f"\nWritten to {OUT}")

