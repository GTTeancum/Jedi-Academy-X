# Xbox Build Guide

This repository is the Jedi Academy Xbox engine baseline now being used as the starting point for `Star-Trek-Elite-Force-X`.

Use this file as the first build reference in a fresh Codex session. Some older handoff files and `AGENTS.md` still contain historical VS2005/XDK notes; the scripted build below is the current practical path.

## Quick Commands

Run from the repository root in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target mp
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target all
```

Optional clean rebuild:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp -Clean
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target mp -Clean
```

Recommended log capture:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp *> scripts\output\build_sp_latest.log
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target mp *> scripts\output\build_mp_latest.log
```

## Outputs

Single player:

- EXE: `code\x_exe\Release\default.exe`
- XBE: `code\x_exe\Release\default.xbe`
- MAP: `code\x_exe\Release\default.map`

Multiplayer:

- EXE: `codemp\x_exe\Release\jamp.exe`
- XBE: `codemp\x_exe\Release\jamp.xbe`
- MAP: `codemp\x_exe\Release\jamp.map`

## Required Local Toolchain

The script expects these paths on this machine:

- XDK 5849 install/extract: `C:\XDK`
- XDK 5558 install/extract: `C:\XDK_5558\XDK`
- XDK 5558 source-repo libs, if present: `C:\Programming\GitHub\xbox\public\xdk\lib`
- XDK 5558 source-repo SDK libs, if present: `C:\Programming\GitHub\xbox\public\sdk\lib\i386`
- VS2005 MASM: `C:\Program Files (x86)\Microsoft Visual Studio 8\VC\bin\ml.exe`
- Python on PATH as `python`

The build script uses the XDK vc71 compiler/linker from:

```text
C:\XDK\xbox\bin\vc71
```

It assembles MASM stubs with VS2005 `ml.exe`, then post-processes the linked Win32 EXE into an XBE.

## Include/Lib Layout

Current build strategy:

- Headers primarily come from `C:\XDK\xbox\include` and `C:\XDK\include`.
- The repo has surgical D3D8 header overrides under `code\win32` so the renderer sees the right Xbox D3D8 surface without switching every header to 5558.
- Libraries prefer `C:\XDK_5558\XDK\xbox\lib`, then fall back through the 5558 source repo libs and XDK 5849 libs.

This was done because the shipped-style renderer path is much happier with XDK 5558 retail `d3d8.lib`/`d3dx8.lib`/`libc.lib`, while the broader JKA source still expects many 5849 headers.

## XBE Post-Processing

Both SP and MP use `patchxbe.py` after linking.

SP post-processor:

```text
code\x_exe\patchxbe.py
```

MP post-processor:

```text
codemp\x_exe\patchxbe.py
```

Both scripts:

- strip `KERNEL32.DLL` from the PE import table
- remove empty PE sections that confuse `imagebld`
- patch the PE subsystem to Xbox
- run `C:\XDK_5558\XDK\xbox\bin\imagebld.exe`

SP also mutates the produced XBE to match the retail-style process flags/stack and D3D library metadata.

MP defaults to a clean `imagebld` XBE. For a CXBX-R-specific MP artifact, set this env var before building:

```powershell
$env:JAMP_PATCHXBE_MUTATE_HEADERS = '1'
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target mp
Remove-Item Env:\JAMP_PATCHXBE_MUTATE_HEADERS
```

That CXBX-R variant intentionally fails `xbecert` because it mutates XBE header/library metadata. Use the clean default for hardware/Xemu-style validation unless there is a specific CXBX-R reason.

## Xemu Smoke Tests

SP smoke harness:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_sp_xemu_smoke.ps1 -Build -Repack -Map yavin1b -Duration 90
```

Useful SP variants:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_sp_xemu_smoke.ps1 -Build -Repack -Maps yavin1b,hoth2,taspir1 -Duration 90
powershell -ExecutionPolicy Bypass -File scripts\run_sp_xemu_smoke.ps1 -Repack -Map hoth2 -VideoDebug
powershell -ExecutionPolicy Bypass -File scripts\run_sp_xemu_smoke.ps1 -Repack -Map yavin1b -Headless -NoScreenshots
```

MP smoke harness:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_mp_xemu_smoke.ps1 -Repack -Duration 90
```

Xemu ISO staging expects:

- `build\xemu\sp_direct_stage`
- `build\xemu\mp_direct_stage`
- `C:\nxdk\tools\extract-xiso\build\extract-xiso.exe`

The smoke scripts repack with `extract-xiso` and run `scripts\ja_xemu_smoke.py`.

## CXBX-R Deployment Notes

For manual CXBX-R testing, copy the desired XBE into:

```text
C:\Games\Emulators\CXBX\Jedi Academy rebuild\
```

Common copies:

```powershell
Copy-Item code\x_exe\Release\default.xbe 'C:\Games\Emulators\CXBX\Jedi Academy rebuild\default.xbe' -Force
Copy-Item codemp\x_exe\Release\jamp.xbe 'C:\Games\Emulators\CXBX\Jedi Academy rebuild\jamp.xbe' -Force
```

Known CXBX-R caveat: MP may need the `JAMP_PATCHXBE_MUTATE_HEADERS=1` build to exercise CXBX-R HLE behavior. Hardware/Xemu should prefer the clean MP default.

## Runtime Logs

Retail/emulated Xbox log paths:

- SP: `E:\ja_sp_log.txt`
- MP: `E:\ja_mp_log.txt`

In CXBX-R, those normally appear under:

```text
C:\Games\Emulators\CXBX\EmuDisk\Partition1\ja_sp_log.txt
C:\Games\Emulators\CXBX\EmuDisk\Partition1\ja_mp_log.txt
```

The logging system flushes frequently. For crashes, the last line is usually the most important breadcrumb.

## Current Baseline Caveats

- SP is the stronger baseline. It has booted and run through Xemu direct-map testing, with lightmaps, sky/fog/weather, and cinematics receiving the most attention.
- MP builds, but has had menu/control/runtime instability. Treat MP as secondary unless you are explicitly working on multiplayer.
- The JA working tree may contain many ignored/untracked generated logs, ISO stages, object files, emulator outputs, and old experiments. Do not commit those unless explicitly asked.
- Prefer `scripts\build_xbox.ps1` over opening the old Visual Studio solutions for routine builds.
- Do not change the XDK/include/lib ordering casually. The current ordering is the result of renderer and `imagebld` compatibility work.

## First Steps For Elite Force Port Work

In `C:\Programming\GitHub\Star-Trek-Elite-Force-X`, this same build guide should exist at repo root. The Elite Force released SP code currently sits beside the engine baseline in:

```text
SP-Mod-Source-Code-master
```

Recommended first workflow in a new Codex thread:

1. Read this file.
2. Build SP with `scripts\build_xbox.ps1 -Target sp`.
3. Confirm `code\x_exe\Release\default.xbe` is produced.
4. Use the SP code path as the engine baseline for integrating the Elite Force SP DLL/source.
5. Keep JA-specific gameplay changes separate from reusable engine/runtime changes.
