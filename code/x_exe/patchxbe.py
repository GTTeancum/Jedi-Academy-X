#!/usr/bin/env python
# patchxbe.py - Jedi Academy Xbox SP build post-processor
# Usage: patchxbe.py <exe_path> <xbe_out_path>

import sys, os, struct, subprocess

exe_path = sys.argv[1]
xbe_path = sys.argv[2]

# ── Step 1: Strip KERNEL32.DLL from PE import table ──────────────────────

def rva_to_offset(data, rva, pe_offset):
    num_sections = struct.unpack_from('<H', data, pe_offset + 6)[0]
    opt_size = struct.unpack_from('<H', data, pe_offset + 20)[0]
    sec_offset = pe_offset + 24 + opt_size
    for i in range(num_sections):
        s = sec_offset + i * 40
        vaddr    = struct.unpack_from('<I', data, s + 12)[0]
        vsize    = struct.unpack_from('<I', data, s + 16)[0]
        raw_off  = struct.unpack_from('<I', data, s + 20)[0]
        raw_size = struct.unpack_from('<I', data, s + 16)[0]
        span = max(vsize, raw_size)
        if span and vaddr <= rva < vaddr + span:
            return raw_off + (rva - vaddr)
    return None

with open(exe_path, 'rb') as f:
    data = bytearray(f.read())

pe_off = struct.unpack_from('<I', data, 0x3C)[0]
assert data[pe_off:pe_off+4] == b'PE\x00\x00', "Not a PE file"

opt_off = pe_off + 24
import_dd  = opt_off + 96 + 8
import_rva = struct.unpack_from('<I', data, import_dd)[0]

if import_rva:
    imp_off = rva_to_offset(data, import_rva, pe_off)
    if imp_off:
        DESC = 20
        descs = []
        pos = imp_off
        while True:
            fields = struct.unpack_from('<5I', data, pos)
            if fields[0] == 0 and fields[3] == 0 and fields[4] == 0:
                descs.append(None)
                break
            descs.append((pos,) + fields)
            pos += DESC
        for idx, d in enumerate(descs):
            if d is None: break
            noff = rva_to_offset(data, d[4], pe_off)
            if noff:
                end = data.index(0, noff)
                if bytes(data[noff:end]).upper() == b'KERNEL32.DLL':
                    print("Removing KERNEL32.DLL from import table")
                    term = imp_off + (len(descs) - 1) * DESC
                    src  = imp_off + (idx + 1) * DESC
                    dst  = imp_off + idx * DESC
                    mlen = (term + DESC) - src
                    data[dst:dst+mlen] = data[src:src+mlen]
                    data[dst+mlen:dst+mlen+DESC] = b'\x00' * DESC
                    break

# ── Step 1b: Remove empty PE sections (e.g. .rsrc with no data) ─────────
# imagebld chokes on sections with zero raw size and zero virtual size

opt_size = struct.unpack_from('<H', data, pe_off + 20)[0]
sec_table = pe_off + 24 + opt_size
num_sec = struct.unpack_from('<H', data, pe_off + 6)[0]
removed = 0
i = 0
while i < num_sec - removed:
    s = sec_table + i * 40
    vsize = struct.unpack_from('<I', data, s + 8)[0]
    raw_size = struct.unpack_from('<I', data, s + 16)[0]
    raw_off = struct.unpack_from('<I', data, s + 20)[0]
    sname = data[s:s+8].rstrip(b'\x00').decode('ascii', errors='replace')
    if vsize == 0 and raw_size == 0:
        print("Removing empty section: %s" % sname)
        # Shift subsequent section headers up
        remaining = (num_sec - removed - i - 1) * 40
        if remaining > 0:
            data[s:s+remaining] = data[s+40:s+40+remaining]
        # Zero out the last header slot
        data[s+remaining:s+remaining+40] = b'\x00' * 40
        removed += 1
    else:
        i += 1
if removed:
    struct.pack_into('<H', data, pe_off + 6, num_sec - removed)
    print("Section count: %d -> %d" % (num_sec, num_sec - removed))

# ── Step 2: Patch PE subsystem to Xbox (14) ──────────────────────────────

sub_off = pe_off + 24 + 68
old_sub = struct.unpack_from('<H', data, sub_off)[0]
struct.pack_into('<H', data, sub_off, 14)
print("Subsystem: %d -> 14 (Xbox)" % old_sub)

temp_exe = exe_path + '.xbox.tmp'
with open(temp_exe, 'wb') as f:
    f.write(data)

# ── Step 3: Run imagebld ─────────────────────────────────────────────────

# Plan-B (OpenJKDF2 1:1): use XDK 5558's imagebld since the EXE is now
# linked against XDK 5558's d3d8/d3dx8/libc.  XDK 5849's imagebld appears
# to silently strip .text section content from XDK 5558-linked objs
# (XBE drops from 5MB to 1.2MB; runtime log shows our code missing).
# OpenJKDF2's build_xbox.bat uses %XDK_ROOT%\bin\imagebld.exe where
# XDK_ROOT=C:\XDK_5558\XDK\xbox — matching that exactly.
imagebld = r'C:\XDK_5558\XDK\xbox\bin\imagebld.exe'
map_path  = exe_path.replace('.exe', '.map')

script_dir = os.path.dirname(os.path.abspath(__file__))
title_img = os.path.join(script_dir, 'TitleImage.bmp')
save_img  = os.path.join(script_dir, 'SaveImage.bmp')

cmd = [
    imagebld,
    '/IN:'   + temp_exe,
    '/OUT:'  + xbe_path,
    '/TESTNAME:Jedi Knight: Jedi Academy',
    '/TESTID:0x4C41000B',
    '/TESTLANKEY:4C41000B4C41000B4C41000B4C41000B',
    '/STACK:0x40000',
    '/TESTMEDIATYPES:0xFFFFFFFF',
    '/NOLIBWARN',
]
if os.path.exists(title_img):
    cmd.append('/TITLEIMAGE:' + title_img)
    print("Title image: " + title_img)
if os.path.exists(map_path):
    cmd.append('/MAP:' + map_path)

print("Running imagebld...")
result = subprocess.call(cmd)
os.remove(temp_exe)

if result != 0:
    print("imagebld failed with code %d" % result)
    sys.exit(result)

print("XBE created: " + xbe_path)

# ── Step 4: Inject missing D3D8 + XGRAPHC library version entries ─────────

print("Checking library version entries for CXBX-Reloaded HLE...")

with open(xbe_path, 'rb') as f:
    xbe = bytearray(f.read())

# Match the shipped title's process flags.  The retail XBE uses
# MOUNT_UTILITY_DRIVE | LIMIT_64MB; keeping the 64MB limit matters when
# reproducing texture/heap pressure on retail hardware and Cxbx.
old_init_flags = struct.unpack_from('<I', xbe, 0x124)[0]
new_init_flags = old_init_flags | 0x00000004
if new_init_flags != old_init_flags:
    struct.pack_into('<I', xbe, 0x124, new_init_flags)
    print("  InitFlags: 0x%08X -> 0x%08X" % (old_init_flags, new_init_flags))

# Match the retail process stack reservation.  Retail SP commits 0x20000;
# imagebld's output from this Win32 toolchain has been larger, which steals
# memory from the same constrained 64MB address space as texture heaps.
old_stack_commit = struct.unpack_from('<I', xbe, 0x130)[0]
retail_stack_commit = 0x00020000
if old_stack_commit != retail_stack_commit:
    struct.pack_into('<I', xbe, 0x130, retail_stack_commit)
    print("  StackCommit: 0x%08X -> 0x%08X" % (old_stack_commit, retail_stack_commit))

base_addr  = struct.unpack_from('<I', xbe, 0x104)[0]
lib_count  = struct.unpack_from('<I', xbe, 0x160)[0]
lib_va     = struct.unpack_from('<I', xbe, 0x164)[0]
lib_offset = lib_va - base_addr

existing_names = set()
existing_build = 5849
for i in range(lib_count):
    off  = lib_offset + i * 16
    name = xbe[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
    existing_names.add(name)
    existing_build = struct.unpack_from('<H', xbe, off + 12)[0]
    if name == 'D3D8':
        old_flags = struct.unpack_from('<H', xbe, off + 14)[0]
        new_flags = (old_flags & 0xE000) | 4
        if new_flags != old_flags:
            struct.pack_into('<H', xbe, off + 14, new_flags)
            print("  D3D8 qfe: %d -> 4" % (old_flags & 0x1FFF))

print("  Existing: %s" % ', '.join(sorted(existing_names)))

def make_lib_entry(name, build, flags=0x4001):
    return name.encode('ascii')[:8].ljust(8, b'\x00') + struct.pack('<HHHH', 1, 0, build, flags)

to_add = []
if 'D3D8' not in existing_names:
    to_add.append(make_lib_entry('D3D8', existing_build))
    print("  Adding D3D8")
if 'XGRAPHC' not in existing_names:
    to_add.append(make_lib_entry('XGRAPHC', existing_build))
    print("  Adding XGRAPHC")

if not to_add:
    print("  D3D8 and XGRAPHC already present - no injection needed")
    with open(xbe_path, 'wb') as f:
        f.write(xbe)
else:
    new_entries = bytearray()
    for i in range(lib_count):
        new_entries += xbe[lib_offset + i*16 : lib_offset + i*16 + 16]
    for e in to_add:
        new_entries += e
    new_count = lib_count + len(to_add)

    while len(xbe) % 4 != 0:
        xbe.append(0)
    new_table_off = len(xbe)
    new_table_va  = new_table_off + base_addr
    xbe += new_entries

    struct.pack_into('<I', xbe, 0x160, new_count)
    struct.pack_into('<I', xbe, 0x164, new_table_va)

    for i in range(new_count):
        off  = new_table_off + i * 16
        name = xbe[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
        va   = new_table_va + i * 16
        if name.startswith('XAPILIB'):
            struct.pack_into('<I', xbe, 0x16C, va)
        elif name == 'XBOXKRNL':
            struct.pack_into('<I', xbe, 0x168, va)

    with open(xbe_path, 'wb') as f:
        f.write(xbe)
    print("  Library table updated (%d -> %d entries)" % (lib_count, new_count))

print("Done. %s ready for CXBX-Reloaded." % os.path.basename(xbe_path))
