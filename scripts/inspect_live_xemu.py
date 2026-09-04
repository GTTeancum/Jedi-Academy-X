#!/usr/bin/env python3
"""Read STEFX diagnostics from an already-running XEMU process.

This deliberately uses read-only process access.  It does not send window,
keyboard, controller, monitor, or debugger commands to XEMU.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import re
import sys
from ctypes import wintypes

import ja_xemu_smoke


PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
MEM_COMMIT = 0x1000
PAGE_GUARD = 0x100
PAGE_NOACCESS = 0x01
READ_CHUNK = 8 * 1024 * 1024
BUILD_MARKER = b"STEFX_RUNTIME_BUILD_ID personality=default"


class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", wintypes.DWORD),
        ("PartitionId", wintypes.WORD),
        ("RegionSize", ctypes.c_size_t),
        ("State", wintypes.DWORD),
        ("Protect", wintypes.DWORD),
        ("Type", wintypes.DWORD),
    ]


def open_process(pid: int):
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.ReadProcessMemory.argtypes = [
        wintypes.HANDLE,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_size_t),
    ]
    kernel32.ReadProcessMemory.restype = wintypes.BOOL
    kernel32.VirtualQueryEx.argtypes = [
        wintypes.HANDLE,
        ctypes.c_void_p,
        ctypes.POINTER(MEMORY_BASIC_INFORMATION),
        ctypes.c_size_t,
    ]
    kernel32.VirtualQueryEx.restype = ctypes.c_size_t
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL

    handle = kernel32.OpenProcess(
        PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid
    )
    if not handle:
        raise OSError(ctypes.get_last_error(), f"OpenProcess({pid}) failed")
    return kernel32, handle


def read_bytes(kernel32, handle, address: int, size: int) -> bytes:
    if address < 0 or size <= 0:
        return b""
    buffer = ctypes.create_string_buffer(size)
    received = ctypes.c_size_t()
    ok = kernel32.ReadProcessMemory(
        handle,
        ctypes.c_void_p(address),
        buffer,
        size,
        ctypes.byref(received),
    )
    if not ok and received.value == 0:
        return b""
    return buffer.raw[: received.value]


def readable_regions(kernel32, handle):
    address = 0
    mbi = MEMORY_BASIC_INFORMATION()
    while kernel32.VirtualQueryEx(
        handle, ctypes.c_void_p(address), ctypes.byref(mbi), ctypes.sizeof(mbi)
    ):
        base = int(mbi.BaseAddress or 0)
        size = int(mbi.RegionSize)
        if (
            mbi.State == MEM_COMMIT
            and not (mbi.Protect & PAGE_GUARD)
            and not (mbi.Protect & PAGE_NOACCESS)
        ):
            yield base, size
        next_address = base + size
        if next_address <= address:
            break
        address = next_address


def find_all(kernel32, handle, needle: bytes) -> list[int]:
    hits: list[int] = []
    overlap = max(0, len(needle) - 1)
    for base, size in readable_regions(kernel32, handle):
        offset = 0
        carry = b""
        while offset < size:
            amount = min(READ_CHUNK, size - offset)
            data = read_bytes(kernel32, handle, base + offset, amount)
            if not data:
                break
            search = carry + data
            start = 0
            while True:
                index = search.find(needle, start)
                if index < 0:
                    break
                hits.append(base + offset - len(carry) + index)
                start = index + 1
            carry = search[-overlap:] if overlap else b""
            offset += len(data)
            if len(data) < amount:
                break
    return hits


def map_literal_runtime_va(map_path: str, token: str) -> int:
    with open(map_path, "r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if token not in line:
                continue
            match = re.search(r"\s([0-9a-fA-F]{8})\s+xb_log\.obj\s*$", line)
            if match:
                linked = int(match.group(1), 16)
                return ja_xemu_smoke.runtime_map_va(linked, map_path)
    raise RuntimeError(f"Could not resolve literal containing {token!r} in {map_path}")


def symbol_runtime_va(map_path: str, symbol: str) -> int | None:
    return ja_xemu_smoke.resolve_runtime_map_symbol_in(symbol, map_path)


def read_word(kernel32, handle, ram_base: int, runtime_va: int, delta: int):
    data = read_bytes(kernel32, handle, ram_base + runtime_va - delta, 4)
    return int.from_bytes(data, "little") if len(data) == 4 else None


def read_word_from_anchor(
    kernel32, handle, anchor_host: int, anchor_va: int, runtime_va: int
):
    data = read_bytes(kernel32, handle, anchor_host + runtime_va - anchor_va, 4)
    return int.from_bytes(data, "little") if len(data) == 4 else None


def circular_log(raw: bytes, position: int) -> str:
    if not raw:
        return ""
    if position < len(raw):
        ordered = raw[:position]
    else:
        start = position & (len(raw) - 1)
        ordered = raw[start:] + raw[:start]
    return ordered.replace(b"\0", b"").decode("latin-1", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--map", dest="map_path", required=True)
    parser.add_argument("--physical-delta", default="0x284000")
    parser.add_argument("--log-tail-lines", type=int, default=160)
    args = parser.parse_args()

    map_path = os.path.abspath(args.map_path)
    delta = int(args.physical_delta, 0)
    marker_va = map_literal_runtime_va(map_path, "STEFX_RUNTIME_BUILD_ID")
    marker_phys = marker_va - delta

    kernel32, handle = open_process(args.pid)
    try:
        marker_hits = find_all(kernel32, handle, BUILD_MARKER)
        symbols = [
            "_g_SPXBLogMagic",
            "_g_SPXBBootPhase",
            "_g_SPXBLogMirrorPos",
            "_g_SPXBHeartbeatMagic",
            "_g_SPXBHeartbeatCount",
            "_g_SPXBHeartbeatFrame",
            "_g_SPXBHeartbeatRealtime",
            "_g_SPXBHeartbeatServerTime",
            "_g_SPXBHeartbeatFps10",
            "_g_SPXBMainLoopCount",
            "_g_SPXBComFrameCount",
            "_g_SPXBSvFrameCount",
            "_g_SPXBClFrameCount",
            "_g_SPXBClsState",
            "_g_SPXBPhaseLast",
            "_g_SPXBClTailStage",
            "_g_SPXBComTailStage",
            "_g_SPXBComFrameDepth",
            "_g_SPXBComSubphase",
            "_g_SPXBMainTailStage",
            "_g_SPXBAudioUpdateStage",
            "_g_SPXBAudioUpdateSerial",
            "_g_SPXBAudioLoadStage",
            "_g_SPXBAudioLoadIndex",
            "_g_SPXBAudioLoadHandle",
            "_g_SPXBGameWeaponFireStage",
            "_g_SPXBGameWeaponFireEntity",
            "_g_SPXBGameWeaponFireWeaponAlt",
            "_g_SPXBCGameWeaponFireStage",
            "_g_SPXBCGameWeaponFireEntity",
            "_g_SPXBCGameWeaponFireWeaponAlt",
            "_g_SPXBRenderListStage",
            "_g_SPXBEndSurfaceStage",
            "_g_SPXBNativeSubmitStage",
            "_g_SPXBFileAllocStage",
            "_g_SPXBFileAllocPathHash",
            "_g_SPXBFileAllocPathPtr",
            "_g_SPXBFileAllocLength",
            "_g_SPXBFileAllocTag",
            "_g_SPXBFileAllocMutex",
            "_g_SPXBFileAllocWaitResult",
            "_g_SPXBFileAllocReleaseResult",
            "_g_SPXBFSWholeCloseStage",
            "_g_SPXBFSWholeCloseHandle",
            "_g_SPXBQALStreamStage",
            "_g_SPXBCGInitStage",
            "_g_SPXBCGInitMediaIndex",
            "_g_SPXBCGInitMediaHash",
            "_g_SPXBCGInitRegisterStage",
            "_g_SPXBLastWholeFilePathHash",
            "_g_SPXBLastWholeFileLength",
            "_g_SPXBLastWholeFileResult",
            "_g_SPXBLastWholeFileAlloc",
            "_g_SPXBLastWholeFileTag",
            "_g_SPXBLastWholeFileOwner",
            "_g_SPXBSoundLoadStage",
            "_g_SPXBSoundLoadHash",
            "_g_SPXBSoundLoadLength",
            "_g_SPXBSoundLoadResult",
            "_g_SPXBModelProbeStage",
            "_g_SPXBModelProbePathHash",
            "_g_SPXBModelProbeFileLen",
        ]
        resolved = {
            symbol: symbol_runtime_va(map_path, symbol) for symbol in symbols
        }
        mirror_va = symbol_runtime_va(map_path, "_g_SPXBLogMirror")
        mirror_pos_va = resolved["_g_SPXBLogMirrorPos"]
        boot_va = resolved["_g_SPXBBootPhase"]
        log_magic_va = resolved["_g_SPXBLogMagic"]
        heartbeat_magic_va = resolved["_g_SPXBHeartbeatMagic"]
        if (
            mirror_va is None
            or mirror_pos_va is None
            or boot_va is None
            or log_magic_va is None
            or heartbeat_magic_va is None
        ):
            raise RuntimeError("Required log symbols were not found in the linker map")

        # The initialized .rdata literal may also occur in the circular log or
        # an ISO cache, and XEMU does not guarantee equal host mapping deltas
        # for distinct guest sections.  Anchor directly on the adjacent live
        # .data magic words instead; all exported diagnostic words and the log
        # mirror share that XBE section.
        magic_hits = find_all(kernel32, handle, (0x53504546).to_bytes(4, "little"))
        candidates = []
        for hit in magic_hits:
            log_magic = int.from_bytes(read_bytes(kernel32, handle, hit, 4), "little")
            heartbeat_magic = read_word_from_anchor(
                kernel32, handle, hit, log_magic_va, heartbeat_magic_va
            )
            if log_magic != 0x53504546 or heartbeat_magic != 0x48424653:
                continue
            boot_phase = read_word_from_anchor(
                kernel32, handle, hit, log_magic_va, boot_va
            )
            mirror_pos = read_word_from_anchor(
                kernel32, handle, hit, log_magic_va, mirror_pos_va
            )
            mirror = read_bytes(
                kernel32, handle, hit + mirror_va - log_magic_va, 32768
            )
            log_text = circular_log(mirror, mirror_pos or 0)
            score = (
                20 * int(log_magic == 0x53504546)
                + 20 * int(heartbeat_magic == 0x48424653)
                + int("STEFX" in log_text)
                + int((boot_phase or 0) != 0x11110001)
            )
            candidates.append(
                {
                    "anchorHost": hit,
                    "logMagic": log_magic,
                    "heartbeatMagic": heartbeat_magic,
                    "bootPhase": boot_phase,
                    "mirrorPos": mirror_pos,
                    "score": score,
                    "log": log_text,
                }
            )
        if not candidates:
            raise RuntimeError("Live STEFX diagnostic magic was not found in XEMU")
        candidate = max(candidates, key=lambda item: item["score"])
        anchor_host = int(candidate["anchorHost"])

        values = {}
        for symbol, runtime_va in resolved.items():
            if runtime_va is not None:
                values[symbol] = read_word_from_anchor(
                    kernel32, handle, anchor_host, log_magic_va, runtime_va
                )

        log_lines = str(candidate["log"]).splitlines()
        result = {
            "pid": args.pid,
            "map": map_path,
            "physicalDelta": delta,
            "markerHits": len(marker_hits),
            "diagnosticMagicHits": len(candidates),
            "dataAnchorHost": f"0x{anchor_host:016X}",
            "symbols": values,
            "logTail": log_lines[-max(1, args.log_tail_lines) :],
        }
        print(json.dumps(result, indent=2))
        return 0
    finally:
        kernel32.CloseHandle(handle)


if __name__ == "__main__":
    sys.exit(main())
