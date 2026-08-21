# Jedi Academy Xbox Build - Project Notes

## Current Holomatch Override - 2026-07-22
- Active Holomatch development starts from the working Elite Force SP engine in `code\`.
- Build Holomatch with `scripts\build_xbox.ps1 -Target spmp`.
- The active Holomatch development artifact is `build\release\efmp.xbe`; current qualification packages it into the shared XEMU ISO and the single bounded hardware stage.
- Runtime data is staged under `BaseEF`; the active Holomatch package is `BaseEF\xbox1.pk3`.
- `codemp\` is historical/deprecated for this project phase and must not be a build, link, include, or runtime dependency for SP-hosted `efmp.xbe`.
- `default.xbe` remains the SP/co-op executable and must not be overwritten by Holomatch work.
- Current Holomatch tests boot straight to `hm_borg1` with bots; menus are future work.
- The authoritative current status files are `GAME_TODO.md` and `HOLOMATCH_QUALIFICATION.md`.
- Use XEMU/LLE for the current unified SP/co-op/Holomatch qualification. The
  unmodified retail Jedi Academy multiplayer XBE crashes CXBX-R, so CXBX-R is
  not a compatibility or regression authority for the JA-derived renderer and
  must not be used to qualify that path. Retail Xbox hardware is the final
  performance authority.

## Development Workflow
- **Programmer:** Codex (AI) — all code changes are made by Codex
- **Compile & Test:** User compiles and runs when Codex asks
- **Never commit or push** without explicit user instruction

## Testing Environment
- **Hardware:** Retail Xbox only (no dev kit, no debugger attach)
- **Emulator:** XEMU/LLE for active qualification. CXBX-R is historical-only for this JA-derived renderer and is not a regression authority.
- **Target build:** Release only — all other configurations (Debug, FinalBuild, DemoDebug, DemoRelease, DemoFinal, SHDebug, etc.) are removed from all .vcproj and .sln files
- **Diagnostic tool:** Debug log output — the SP codebase has logging strings that write to a log file; this is the **only** practical way to diagnose runtime issues
- All new code paths must be instrumented with log output before asking the user to test
- Do not assume a crash cause — log before and after suspect calls so the log tells us where execution stopped
- **Canonical controller config:** `C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\BaseEF\default.cfg` is read-only to Codex. It may be inspected and compared, but must never be edited, overwritten, generated, staged into, or used as a package output target.

## Repository Structure
- **SP (Single Player):** `code/` — `JediAcademy.sln`
- **Historical MP (deprecated):** `codemp/` — `JKA_mp.sln`
- **XDK:** clean, unmodified 5558 at `C:\XDK_5558\XDK\`; do not use the modified 5849 tree for active builds.

## Toolchain
- **Compiler:** VS2005 (Microsoft Visual Studio 8)
- **XDK Version:** 5558
- **Platform:** Win32 (renamed from Xenon/Xbox in all vcproj/sln files)
- **MASM:** `C:\Program Files (x86)\Microsoft Visual Studio 8\VC\bin\ml.exe`
- **imagebld:** `C:\XDK\xbox\bin\imagebld.exe`

---

## SP Build Status — COMPLETE ✅

Release config builds and produces XBE:
- `Release` → `ja-release.exe` + `ja-release.xbe`

### Key SP Fixes Applied
- `Xenon` → `Win32` platform rename in all vcproj/sln files
- D3D9 → D3D8 headers in `win32/glw_win_dx8.h`, `win_lighteffects.h`, `win_highdynamicrange.h`, `win_qgl_dx8.cpp`
- Bink/RAD stub headers: `client/bink.h`, `client/RAD.h`
- `BinkVideo.cpp/h`: `IDirect3DTexture9` → `IDirect3DTexture8`, `s32` → `S32`, OpenFlags stubbed
- wchar_t casts in `sv_savegame.cpp`, `xb_settings.cpp`, `ui_main.cpp`
- ~200 for-loop variable scope fixes across cgame, game, renderer, ghoul2, server files
- `G2_misc.cpp`: multiple hoisted `int i` fixes

### SP Runtime Fixes Applied
- `Sys_InitFileCodes`: changed from `Com_Error(ERR_DROP)` to warning on failure (filecode cache is non-critical)
- `Com_Error(ERR_DROP)`: guarded `CL_FlushMemory`/`CL_StartHunkUsers` with `com_cl_running` check (prevents crash during early-init errors)
- `win_qgl_dx8.cpp`: NULL device guards on `dllBeginFrame`, `dllMaterialfv`, QGL_Init `SetMaterial`, `GLW_Shutdown` `Release`
- `patchxbe.py`: added empty PE section removal (`.rsrc`) from MP version — prevents imagebld failures
- XBLog breadcrumbs: GOB init/open, Sys_StreamInit, TheGhoul2InfoArray, Com_Frame first 3 frames, granular CL_Disconnect steps

### SP Stub Files (`code/x_exe/`)
Two files must exist in `code/x_exe/`:

**`xbox_asm_stubs.asm`** — provides:
- `__ftol2_sse` — x87 truncation
- `__ftol2` — same
- `___CxxFrameHandler3` — returns ExceptionContinueSearch
- `__except_handler4` — buffer security stub
- `_WinMainCRTStartup` — Xbox startup entry point

**`xbox_crt_stubs.cpp`** — provides:
- `_strcmpi` → forwards to `_stricmp`

### SP Linker Settings (Release)
- Xbox system libraries come from the unmodified XDK 5558 tree at `C:\XDK_5558\XDK\xbox\lib`.
- Keep the active Xbox renderer link entirely on one XDK version; do not mix 5558 headers or objects with 5849 libraries.
- `IgnoreDefaultLibraryNames`: msvcrt/libcmt variants
- `/FORCE:MULTIPLE`
- `BufferSecurityCheck="FALSE"`
- `d3d8.lib` (retail runtime). `d3d8i.lib` is diagnostic-only and regressed the measured gameplay baseline.
- Stub obj `.\Release\exe\xbox_asm_stubs.obj` listed explicitly in AdditionalDependencies
- `EntryPointSymbol="_WinMainCRTStartup"`

### SP XBE Generation (post-build, baked into x_exe.vcproj)
Calls `patchxbe.py $(ProjectDir) .\Release\ja-release.exe .\Release\ja-release.xbe`

**`patchxbe.py`** does four things:
1. Strips `KERNEL32.DLL` from the PE import table (imagebld rejects Win32 DLL imports)
2. Patches PE subsystem field to Xbox (14)
3. Runs `C:\XDK\xbox\bin\imagebld.exe` with test signing flags
4. Injects D3D8 and XGRAPHC library version entries into the XBE for CXBX-Reloaded HLE

Pre-link event assembles stubs automatically — Rebuild works without manual intervention.

### XBE Metadata
- **Title ID:** `0x4C41000B`
- **LAN Key:** `4C41000B4C41000B4C41000B4C41000B`
- **Title Name:** Jedi Knight: Jedi Academy
- **Stack Size:** `0x40000`

---

## Historical MP Build Status — Deprecated

This section documents the old inherited MP tree only. It is not the active Holomatch build path, and `codemp\` must remain unnecessary for SP-hosted `efmp.xbe`.

### Projects in `codemp/JKA_mp.sln`
| Project | Status |
|---|---|
| goblib | ✅ 0 errors |
| x_botlib | ✅ 0 errors |
| x_ui | ✅ 0 errors |
| x_jk2game | ✅ 0 errors |
| x_jk2cgame | ✅ 0 errors |
| x_exe | ✅ Release builds (`jamp-release.exe` + `jamp-release.xbe`) |

### Key MP Fixes Applied (source files in `codemp/`)
- `Xbox` → `Win32` platform rename (36-254 replacements per file)
- `client/cl_data.h`: added return type to `operator=(const ClientManager&)`
- `renderer/modelmem.h`: hoisted `int i` before for loop
- `ui/ui_main.c`: `const baseClass` → `const int baseClass` (lines 1461, 10986); hoisted `int i` before post-loop assert
- `botlib/l_precomp.cpp`: `ctime((const long*)` → `ctime((const time_t*)`
- `qcommon/xb_settings.cpp`: `(LPCWSTR)` casts on `XCreateSaveGame`/`XDeleteSaveGame` calls; `(wchar_t*)` cast on `mbstowcs`
- `client/snd_dma_console.cpp`: hoisted `int i` before for loop
- `renderer/tr_font.cpp`: hoisted `iFontToFind` and `it` before their for loops
- `renderer/tr_shade.cpp`: hoisted `int i`
- `win32/win_highdynamicrange.cpp`: hoisted `int xx`
- `win32/win_lighteffects.cpp`: hoisted `int i`
- `win32/win_qgl_dx8.cpp`: fixed `for(int i=` scope
- `xbox/xblive.cpp`: hoisted `int i`
- `qcommon/huffman.cpp`: hoisted `int i`
- Various for-loop scope fixes across game/cgame files

### MP Stub Files (`codemp/x_exe/`)
Same `xbox_asm_stubs.asm` and `xbox_crt_stubs.cpp` as SP — copied from `code/x_exe/`.
The asm stub also includes `__except_handler4` (required by MP's `win_shared.cpp`).

### MP Runtime Fixes Applied
- Same `Sys_InitFileCodes` non-fatal fix as SP
- Same `Com_Error(ERR_DROP)` early-init guard as SP (`CL_FlushMemory` skipped when `com_cl_running` not set)
- Same D3D NULL device guards in `win_qgl_dx8.cpp` as SP (4 crash points)

### MP XBE — COMPLETE ✅
`patchxbe.py` in `codemp/x_exe/` handles KERNEL32 stripping, empty section removal (.rsrc), subsystem patch, and imagebld. Pre-link event assembles stubs. Linker uses `/FIXED:NO` to generate .reloc section (required by imagebld). `EmbedManifest="false"` set. `EntryPointSymbol="WinMainCRTStartup"` (no leading underscore — linker decorates it).

### Holocron FFA + Jedi Master Port (from JO MP)
Code complete — needs compile test. Changes:
- `q_shared.h`: uncommented `isJediMaster`, `holocronsCarried[]`, `holocronCantTouch`, `holocronCantTouchTime`, `holocronBits` in playerState_t; uncommented `isJediMaster` in both entityState_t variants
- `g_main.c`: removed "not supported" blocks, uncommented `g_MaxHolocronCarry` cvar
- `g_combat.c`: uncommented G_GetJediMaster, G_ThereIsAMaster, JM death/scoring, friendly fire prevention
- `g_client.c`: uncommented `isJediMaster = qtrue` on saber pickup, `= qfalse` on spawn
- `g_active.c`: uncommented G_UpdateJediMasterBroadcasts body
- `g_misc.c`: ported HolocronRespawn, HolocronPopOut, HolocronTouch, HolocronThink from JO; fixed SP_misc_holocron (removed assert(0), #ifndef _XBOX guards, uncommented isJediMaster)
- `w_force.c`: uncommented HolocronUpdate, holocron init, holocron force regen, JM force grants
- `bg_misc.c`: uncommented isJediMaster and holocronBits in BG_PlayerStateToEntityState
- `gameinfo.txt`: added "Holocron FFA" (1) and "Jedi Master" (2) to both gametype lists

### MP XBLog Integration
- `Com_Printf` restructured: XBLog_Write always runs even in Release (original was `#ifdef _DEBUG` guarded)
- `win_main_console.cpp`: full boot sequence breadcrumbs (JAMP: prefix)
- `common.cpp`: breadcrumbs throughout Com_Init

---

## Roadmap
1. ✅ SP — Release builds and produces XBE; XBLog wired to `E:\ja_log.txt` with breadcrumbs throughout boot
2. ✅ MP — Release builds and produces XBE; XBLog wired to `E:\ja_log.txt` with breadcrumbs (`JAMP:` prefix)
3. ✅ Holocron FFA + Jedi Master gametypes ported from JO to JA MP
4. 📋 Test SP on retail Xbox (check `E:\ja_log.txt` for boot progress)
5. 📋 Test MP on retail Xbox
5. 📋 Jedi Outcast single player build
6. 📋 Re-theme JA SP/MP UI to more closely match PC version's UI theme
7. 📋 Port JA's .skin segment selection to JO SP — cosmetic customization of Kyle using alternate .skin files in the same model folder (same GLM, falls back to base skin if segments missing)
8. 📋 Add outside-file support — files placed outside GOBs using the same folder structure should be read by the filesystem and should supersede matching GOB-contained files

## Testing Notes
- Use XEMU/LLE for current unified SP/co-op/Holomatch proof.
- Retail Xbox remains the final target.
- Current logs are `E:\ef_sp_log.txt` for SP and `D:\ef_mp_log.txt`, falling back to `E:\ef_mp_log.txt`, for SP-hosted Holomatch. XBLog flushes every write; last line = crash point.
- Run `scripts\cleanup_generated.ps1` before and after emulator runs and after every completed build/package/test cycle. `scripts\run_sp_xemu_smoke.ps1` does this automatically. Use `-Aggressive` after replacing a beta stage so obsolete package trees cannot accumulate.
- Keep one current XEMU ISO under `build\xemu`; per-run ISO copies and staging trees are temporary artifacts. Use `-KeepStage` only for a specific diagnostic that needs inspection afterward.
- Smoke output retention is bounded. Preserve a proof explicitly in project notes or a committed artifact before allowing newer runs to rotate it out.

## JO MP Source Reference
- JO MP source is at `D:\Programming\GitHub\jedioutcast-master\CODE-mp\`
- Used for porting Holocron/Jedi Master gametypes to JA MP

---

## Key Technical Notes

### d3d8.lib vs d3d8i.lib
The active build links XDK 5558's retail `d3d8.lib`. Use `d3d8i.lib` only for a bounded diagnostic that needs its counters, then restore `d3d8.lib`: the instrumented runtime reduced the measured gameplay baseline and is not a shipping candidate.

### XDK Tools Location
The active build tools are under `C:\XDK_5558\XDK\xbox\bin\`, including `imagebld.exe`, `xsasm.exe`, and `xbcp.exe`.

### patchxbe.py Location
`code/x_exe/patchxbe.py` — active SP and SP-hosted Holomatch XBE post-processor. Do not wire active Holomatch through `codemp/x_exe`.

### Symbol Naming in MASM
`.model flat` does NOT prepend underscores to PUBLIC names. Write the exact linker symbol name:
- C name `foo` → linker symbol `_foo` → MASM `PUBLIC _foo`
- C name `_foo` → linker symbol `__foo` → MASM `PUBLIC __foo`

### $(IntDir) in AdditionalDependencies
`$(IntDir)` does not expand in `AdditionalDependencies`. Use hardcoded relative paths like `.\\Debug\\exe\\xbox_asm_stubs.obj`.

### XDK Include Paths
Required in all MP vcprojs: `C:\XDK\xbox\include;C:\XDK\include`
Both `platform.h` and `xboxcommon.h` include `xtl.h` which lives in `C:\XDK\xbox\include\`.

### Solution Format
MP solution (`JKA_mp.sln`) is VS2003 Format Version 7.00. VS2005 loads it fine once all vcprojs use Win32 platform.
