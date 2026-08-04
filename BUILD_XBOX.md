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
- Holomatch multiplayer for this project is SP-hosted and builds entirely through the `code\` path as the `spmp` target.
- `codemp\` is retired for the active Holomatch vertical slice. It must not be a build, link, include, or runtime dependency for `efmp.xbe`.
- The SP/co-op build produces `default.exe`, `default.xbe`, and `default.map` under root `build\release`.
- Do not rename title metadata casually; first keep the baseline build reproducible.

## Quick Build Commands

Run from `C:\Programming\GitHub\Star-Trek-Elite-Force-X` in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp
```

Holomatch MP build:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target spmp
```

Build SP and Holomatch separately when you need both artifacts:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target spmp
```

Optional clean rebuild:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp -Clean
```

Recommended log capture:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp *> scripts\output\build_sp_latest.log
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target spmp *> scripts\output\build_spmp_latest.log
```

## Primary Output

Single-player engine output:

- EXE: `build\release\default.exe`
- XBE: `build\release\default.xbe`
- MAP: `build\release\default.map`

These names are inherited. SP/co-op build success means `build\release\default.xbe` is produced.

Holomatch multiplayer output:

- EXE: `build\release\efmp.exe`
- XBE: `build\release\efmp.xbe`
- MAP: `build\release\efmp.map`

The staged CXBX-R development copy is:

- XBE: `C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\efmp.xbe`
- Runtime package: `C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\BaseEF\xbox1.pk3`

`efmp.xbe` direct-boots `hm_borg1` for the current vertical slice. Holomatch menus are not part of the boot path yet.

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

The retired inherited MP tree still contains:

```text
codemp\x_exe\patchxbe.py
```

That path is historical context only for this project phase. Do not use it for active Holomatch qualification. The active Holomatch build is the SP-hosted `spmp` target from `code\`.

For a CXBX-R-specific inherited MP artifact, only when doing explicit archaeology:

```powershell
$env:JAMP_PATCHXBE_MUTATE_HEADERS = '1'
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target mp
Remove-Item Env:\JAMP_PATCHXBE_MUTATE_HEADERS
```

That MP CXBX-R variant intentionally fails `xbecert` because it mutates XBE header/library metadata. This is mainly historical context for inherited MP testing.

## Smoke Tests

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

Holomatch smoke testing for this phase is CXBX-R only, not XEMU. Use:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\smoke_cxbx_mp_all_maps.ps1 -MapNames hm_borg1 -HoldSeconds 90 -ScreenshotAtSeconds 45
```

Use `C:\Games\Emulators\CXBX-CodexCapture` for visual proof. The capture helper must own the emulator process; do not use desktop/window screenshots as qualification proof.

## CXBX-R Deployment Notes

For manual CXBX-R testing, copy the SP XBE into the existing test folder:

```powershell
Copy-Item build\release\default.xbe 'C:\Games\Emulators\CXBX\Jedi Academy rebuild\default.xbe' -Force
```

That folder name is inherited from the baseline. It can be renamed later once the Elite Force content/runtime path is established.

For Holomatch MP testing, keep the separate MP artifact name:

```powershell
Copy-Item build\release\efmp.xbe 'C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\efmp.xbe' -Force
```

Do not copy the Holomatch MP build over `default.xbe`; that file is reserved for the SP/co-op path.

## Runtime Logs

Retail/emulated Xbox log paths:

- SP: `E:\ef_sp_log.txt`
- SP-hosted Holomatch MP: `D:\ef_mp_log.txt`, falling back to `E:\ef_mp_log.txt`

In CXBX-R, these normally appear under:

```text
C:\Games\Emulators\CXBX\EmuDisk\Partition1\ef_sp_log.txt
C:\Games\Emulators\CXBX\EmuDisk\Partition1\ef_mp_log.txt
```

The logging system flushes frequently. For crashes, the last line is usually the most important breadcrumb.

The SP log name is Elite Force-specific. Keep this path stable during boot and map-loading work so crash breadcrumbs remain easy to compare.

## Baseline Caveats

- For SP/co-op work, focus on the `code\` path. It is the strongest baseline and the intended carrier for Elite Force SP code.
- For Holomatch work, use the `code\` path and the `spmp` build target. Keep its output isolated as `efmp.xbe`.
- `codemp\` is deprecated for active Holomatch work. The code-only verifier must continue to report `codempDependency=false`.
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

For current Holomatch continuation, also read:

- `GAME_TODO.md`
- `HOLOMATCH_QUALIFICATION.md`
