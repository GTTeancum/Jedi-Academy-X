"""
Reads the static ctor table (.CRT$XCU) from default.exe and resolves
each function pointer against default.map to identify all C++ global
constructors running before main().
"""

import struct, sys, os, re

EXE = r"C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.exe"
MAP = r"C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.map"

# ------------------------------------------------------------------
# Parse PE
# ------------------------------------------------------------------
with open(EXE, "rb") as f:
    raw = f.read()

e_lfanew = struct.unpack_from("<I", raw, 0x3C)[0]
pe_off   = e_lfanew
machine, num_sections, _, _, opt_size, _ = struct.unpack_from("<HHIIIH", raw, pe_off+4)
opt_off  = pe_off + 24
magic    = struct.unpack_from("<H", raw, opt_off)[0]  # 0x10b=PE32
image_base  = struct.unpack_from("<I", raw, opt_off+28)[0]
sect_off = opt_off + opt_size   # start of section headers

def parse_sections():
    sections = []
    for i in range(num_sections):
        o = sect_off + i*40
        name  = raw[o:o+8].rstrip(b'\x00').decode('latin-1','replace').replace('�','?')
        vsize = struct.unpack_from("<I", raw, o+8)[0]
        rva   = struct.unpack_from("<I", raw, o+12)[0]
        raw_size = struct.unpack_from("<I", raw, o+16)[0]
        raw_ptr  = struct.unpack_from("<I", raw, o+20)[0]
        sections.append((name, rva, vsize, raw_ptr, raw_size))
    return sections

def rva_to_offset(rva):
    for (name, sect_rva, vsize, raw_ptr, raw_size) in sections:
        if sect_rva <= rva < sect_rva + max(vsize, raw_size):
            return raw_ptr + (rva - sect_rva)
    return None

sections = parse_sections()

print("PE sections:")
for i,(name, rva, vsize, raw_ptr, raw_size) in enumerate(sections):
    print(f"  [{i+1:2d}] {name:20s} rva=0x{rva:08x} va=0x{image_base+rva:08x} rawoff=0x{raw_ptr:08x} size=0x{vsize:x}")

# ------------------------------------------------------------------
# Parse map file: build symbol table addr→name
# ------------------------------------------------------------------
# map format: "section_num:hex_offset   symbol_name   VA  flags  obj"
# We need to find each section's base VA from the PE

# map section numbers (1-based in MSVC) correspond to PE sections in order
# Section 1 = first PE section, etc.
# But CRT sections in MSVC maps are separate from PE sections…
# The map section index matches PE section order.

# Build section VA bases: map section N → VA = image_base + sections[N-1].rva
def map_addr_to_va(sect_num, offset):
    if sect_num < 1 or sect_num > len(sections):
        return None
    return image_base + sections[sect_num-1][1] + offset

# Parse all symbols from map
sym_va_to_name = {}
sym_pattern = re.compile(r'^\s+(\d{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8})')
with open(MAP, 'r', errors='replace') as f:
    for line in f:
        m = sym_pattern.match(line)
        if m:
            sect = int(m.group(1))
            off  = int(m.group(2), 16)
            name = m.group(3)
            va_str = int(m.group(4), 16)
            sym_va_to_name[va_str] = name

print(f"\nLoaded {len(sym_va_to_name)} symbols from map file")

# ------------------------------------------------------------------
# Find .CRT$XCU by scanning for it in map section layout
# ------------------------------------------------------------------
# From the map: section 9, offset 0x10, size 0xF8 → .CRT$XCU
# map section 9 = PE section 9 (0-indexed: index 8)
crt_sect_num = 9
crt_xcu_off  = 0x10
crt_xcu_size = 0xF8

if crt_sect_num > len(sections):
    print(f"ERROR: PE has only {len(sections)} sections, need section {crt_sect_num}")
    sys.exit(1)

pe_sect = sections[crt_sect_num - 1]
crt_xcu_rva = pe_sect[1] + crt_xcu_off
crt_xcu_file_off = rva_to_offset(crt_xcu_rva)

print(f"\n.CRT$XCU: section {crt_sect_num} '{pe_sect[0]}', rva=0x{crt_xcu_rva:08x}, file_off=0x{crt_xcu_file_off:08x}")

if crt_xcu_file_off is None:
    print("ERROR: could not map .CRT$XCU RVA to file offset")
    sys.exit(1)

# Read all ctor pointers
num_ctors = crt_xcu_size // 4
print(f"Reading {num_ctors} ctor pointers from file offset 0x{crt_xcu_file_off:x}:\n")

print(f"{'#':>3}  {'VA':>10}  Symbol")
print("-" * 80)
for i in range(num_ctors):
    ptr_off = crt_xcu_file_off + i * 4
    if ptr_off + 4 > len(raw):
        print(f"  {i+1:3d}: (out of file)")
        break
    fn_va = struct.unpack_from("<I", raw, ptr_off)[0]
    if fn_va == 0:
        print(f"  {i+1:3d}: 0x{fn_va:08x}  (null)")
        continue
    name = sym_va_to_name.get(fn_va, "<unknown>")
    print(f"  {i+1:3d}: 0x{fn_va:08x}  {name}")
