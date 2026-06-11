# Star Trek Elite Force X - Xbox Build Guide

This repository currently starts from the stabilized Jedi Academy Xbox SP engine baseline. Treat the inherited Jedi Academy code as the engine scaffold, not as the final product identity.

The released Elite Force SP source drop is in:

```text
SP-Mod-Source-Code-master
```

Use this file as the first build reference in a fresh Codex session. The goal is to keep the inherited Xbox build pipeline working while replacing/adapting gameplay, assets, and game-side code toward Star Trek: Voyager - Elite Force.

## Current Reality

- The build system, project layout, and many output names are still inherited from Jedi Academy.
- The useful baseline is the single-player engine path under `code\`.
- Multiplayer under `codemp\` exists because it came with the baseline, but it is not the primary Elite Force port path.
- The SP build produces `default.exe`, `default.xbe`, and `default.map` under root `build\release`.
- Do not rename title metadata casually; first keep the baseline build reproducible.

## Quick Build Commands

Run from `C:\Programming\GitHub\Star-Trek-Elite-Force-X` in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp
```

Optional MP build, mostly for inherited-engine comparison:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target mp
```

Build both inherited targets:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target all
```

Optional clean rebuild:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp -Clean
```

Recommended log capture:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp *> scripts\output\build_sp_latest.log
```

## Primary Output

Single-player engine output:

- EXE: `build\release\default.exe`
- XBE: `build\release\default.xbe`
- MAP: `build\release\default.map`

These names are inherited. For the first Elite Force integration phase, build success means `build\release\default.xbe` is produced.

Inherited multiplayer output, if needed:

- EXE: `codemp\x_exe\Release\jamp.exe`
- XBE: `codemp\x_exe\Release\jamp.xbe`
- MAP: `codemp\x_exe\Release\jamp.map`

## Required Local Toolchain

The scripted build expects these machine-local paths:

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

It assembles Xbox startup/CRT stubs with VS2005 `ml.exe`, compiles/links with XDK vc71 tools, then post-processes the linked Win32 EXE into an XBE.

## Include And Lib Ordering

Do not change this casually. The current ordering is part of the working inherited-engine baseline.

- Headers primarily come from `C:\XDK\xbox\include` and `C:\XDK\include`.
- D3D8 header overrides live under `code\win32` so the renderer gets the required Xbox D3D8 API surface without switching all headers to 5558.
- Libraries prefer `C:\XDK_5558\XDK\xbox\lib`, then fall back through the 5558 source repo libs and XDK 5849 libs.

The reason: the renderer path is more stable with XDK 5558 retail `d3d8.lib`, `d3dx8.lib`, and `libc.lib`, while the inherited source still expects many 5849 headers.

## XBE Post-Processing

SP uses:

```text
code\x_exe\patchxbe.py
```

That script:

- strips `KERNEL32.DLL` from the PE import table
- removes empty PE sections that confuse `imagebld`
- patches the PE subsystem to Xbox
- runs `C:\XDK_5558\XDK\xbox\bin\imagebld.exe`
- mutates the produced XBE to match retail-style process flags, stack commit, and D3D library metadata

Inherited MP uses:

```text
codemp\x_exe\patchxbe.py
```

MP defaults to a clean `imagebld` XBE. For a CXBX-R-specific inherited MP artifact:

```powershell
$env:JAMP_PATCHXBE_MUTATE_HEADERS = '1'
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target mp
Remove-Item Env:\JAMP_PATCHXBE_MUTATE_HEADERS
```

That MP CXBX-R variant intentionally fails `xbecert` because it mutates XBE header/library metadata. This is mainly historical context for inherited MP testing.

## Xemu Smoke Tests

SP smoke harness:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_sp_xemu_smoke.ps1 -Build -Repack -Map yavin1b -Duration 90
```

The map names are still inherited from the engine baseline until Elite Force content is wired in.

Useful inherited SP variants:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_sp_xemu_smoke.ps1 -Build -Repack -Maps yavin1b,hoth2,taspir1 -Duration 90
powershell -ExecutionPolicy Bypass -File scripts\run_sp_xemu_smoke.ps1 -Repack -Map yavin1b -Headless -NoScreenshots
```

Xemu ISO staging expects:

- `build\xemu\sp_direct_stage`
- `C:\nxdk\tools\extract-xiso\build\extract-xiso.exe`

The smoke scripts repack with `extract-xiso` and run `scripts\ja_xemu_smoke.py`. The helper script name is inherited.

## CXBX-R Deployment Notes

For manual CXBX-R testing, copy the SP XBE into the existing test folder:

```powershell
Copy-Item build\release\default.xbe 'C:\Games\Emulators\CXBX\Jedi Academy rebuild\default.xbe' -Force
```

That folder name is inherited from the baseline. It can be renamed later once the Elite Force content/runtime path is established.

## Runtime Logs

Retail/emulated Xbox log paths:

- SP: `E:\ef_sp_log.txt`
- MP: `E:\ja_mp_log.txt`

In CXBX-R, these normally appear under:

```text
C:\Games\Emulators\CXBX\EmuDisk\Partition1\ef_sp_log.txt
C:\Games\Emulators\CXBX\EmuDisk\Partition1\ja_mp_log.txt
```

The logging system flushes frequently. For crashes, the last line is usually the most important breadcrumb.

The SP log name is Elite Force-specific. Keep this path stable during boot and map-loading work so crash breadcrumbs remain easy to compare.

## Baseline Caveats

- Focus on the SP path. It is the strongest baseline and the intended carrier for Elite Force SP code.
- The inherited MP path builds but has had menu/control/runtime instability; avoid using it as the first port target.
- The working tree may contain generated logs, ISO stages, object files, emulator outputs, and old experiments. Do not commit generated debris unless explicitly asked.
- Prefer `scripts\build_xbox.ps1` over opening old Visual Studio solutions for routine builds.
- Keep JA-specific gameplay/content changes separate from reusable engine/runtime changes.

## Recommended First Workflow

1. Read this file.
2. Build SP:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp
```

3. Confirm `build\release\default.xbe` exists.
4. Inspect `SP-Mod-Source-Code-master` and plan how the Elite Force SP game code maps into the inherited `code\game`, `code\cgame`, and renderer/runtime boundaries.
5. Keep the inherited JA engine booting while replacing/adapting game-side systems incrementally.
