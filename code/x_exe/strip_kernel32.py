"""
strip_kernel32.py - Remove KERNEL32.DLL from a PE import table.
Usage: python strip_kernel32.py <pe_file>
Required because imagebld.exe rejects Win32 DLL imports.
"""
import struct, sys

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

def strip_kernel32(filename):
    with open(filename, 'rb') as f:
        data = bytearray(f.read())

    # PE header offset
    pe_off = struct.unpack_from('<I', data, 0x3C)[0]
    assert data[pe_off:pe_off+4] == b'PE\x00\x00', "Not a PE file"

    opt_off = pe_off + 24
    magic = struct.unpack_from('<H', data, opt_off)[0]
    assert magic == 0x10B, "Not PE32"

    # Import directory is data directory entry 1 (offset 96 into optional header + 8 per entry)
    import_dd = opt_off + 96 + 8
    import_rva  = struct.unpack_from('<I', data, import_dd)[0]
    import_size = struct.unpack_from('<I', data, import_dd + 4)[0]

    if import_rva == 0:
        print("No import directory found")
        return

    imp_off = rva_to_offset(data, import_rva, pe_off)
    if imp_off is None:
        print("Could not map import RVA to file offset")
        return

    # Walk IMAGE_IMPORT_DESCRIPTORs (20 bytes each)
    DESC = 20
    descs = []
    pos = imp_off
    while True:
        orig, ts, fwd, name_rva, first = struct.unpack_from('<5I', data, pos)
        if orig == 0 and name_rva == 0 and first == 0:
            descs.append(None)  # terminator
            break
        descs.append((pos, orig, ts, fwd, name_rva, first))
        pos += DESC

    # Find KERNEL32.DLL
    remove_idx = None
    for idx, d in enumerate(descs):
        if d is None:
            break
        _, _, _, _, name_rva, _ = d
        noff = rva_to_offset(data, name_rva, pe_off)
        if noff is None:
            continue
        end = data.index(0, noff)
        dll_name = bytes(data[noff:end]).upper()
        if dll_name == b'KERNEL32.DLL':
            remove_idx = idx
            print(f"Found KERNEL32.DLL at import entry {idx}, removing...")
            break

    if remove_idx is None:
        print("KERNEL32.DLL not found in import table - nothing to do")
        return

    # Compact: shift all entries after remove_idx down by DESC bytes (overwrite it)
    # descs[-1] is the terminator (None), everything before it is real
    term_pos = imp_off + len(descs) * DESC  # position just after terminator

    src_start = imp_off + (remove_idx + 1) * DESC  # first byte to copy
    dst_start = imp_off + remove_idx * DESC          # destination

    # Number of bytes to move: from src_start to end of terminator (inclusive)
    move_len = term_pos - src_start + DESC  # +DESC to include terminator itself... 
    # Actually terminator is at imp_off + (len(descs)-1)*DESC
    term_file_pos = imp_off + (len(descs) - 1) * DESC
    move_len = (term_file_pos + DESC) - src_start

    data[dst_start:dst_start + move_len] = data[src_start:src_start + move_len]
    # Zero out the now-duplicate bytes at the end
    dup_start = dst_start + move_len
    data[dup_start:dup_start + DESC] = b'\x00' * DESC

    with open(filename, 'wb') as f:
        f.write(data)
    print(f"Done. {filename} patched: KERNEL32.DLL removed from import table.")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: python {sys.argv[0]} <pe_file>")
        sys.exit(1)
    strip_kernel32(sys.argv[1])
