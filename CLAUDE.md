# Jedi Academy Xbox Build - Project Notes

## Development Workflow
- **Programmer:** Claude (AI) — all code changes are made by Claude
- **Compile & Test:** User compiles and runs when Claude asks
- **Never commit or push** without explicit user instruction

## Testing Environment
- **Hardware:** Retail Xbox only (no dev kit, no debugger attach)
- **Target build:** Release only — all other configurations (Debug, FinalBuild, DemoDebug, DemoRelease, DemoFinal, SHDebug, etc.) are removed from all .vcproj and .sln files
- **Diagnostic tool:** Debug log output — the SP codebase has logging strings that write to a log file; this is the **only** practical way to diagnose runtime issues
- All new code paths must be instrumented with log output before asking the user to test
- Do not assume a crash cause — log before and after suspect calls so the log tells us where execution stopped

## Repository Structure
- **SP (Single Player):** `code/` — `JediAcademy.sln`
- **MP (Multiplayer):** `codemp/` — `JKA_mp.sln`
- **XDK:** `C:\XDK\` (manually extracted from XDKSetup5849.exe via 7-Zip)

## Toolchain
- **Compiler:** VS2005 (Microsoft Visual Studio 8)
- **XDK Version:** 5849
- **Platform:** Win32 (renamed from Xenon/Xbox in all vcproj/sln files)
- **MASM:** `C:\Program Files (x86)\Microsoft Visual Studio 8\VC\bin\ml.exe`
- **imagebld:** `C:\XDK\xbox\bin\imagebld.exe`

---

## SP Build Status — COMPLETE ✅

Release config builds and produces XBE:
- `Release` → `default.exe` + `default.xbe`

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
- `libc.lib` from `C:\XDK\lib\`
- `C:\XDK\lib` in `AdditionalLibraryDirectories`
- `IgnoreDefaultLibraryNames`: msvcrt/libcmt variants
- `/FORCE:MULTIPLE`
- `BufferSecurityCheck="FALSE"`
- `d3d8i.lib` (NOT `d3d8.lib` — retail d3d8.lib uses kernel-only inline symbols unlinkable with Win32 toolchain)
- Stub obj `.\Release\exe\xbox_asm_stubs.obj` listed explicitly in AdditionalDependencies
- `EntryPointSymbol="_WinMainCRTStartup"`

### SP XBE Generation (post-build, baked into x_exe.vcproj)
Calls `patchxbe.py $(ProjectDir) .\Release\default.exe .\Release\default.xbe`

**`patchxbe.py`** does four things:
1. Strips `KERNEL32.DLL` from the PE import table (imagebld rejects Win32 DLL imports)
2. Patches PE subsystem field to Xbox (14)
3. Runs `C:\XDK\xbox\bin\imagebld.exe` with test signing flags
4. Injects D3D8 and XGRAPHC library version entries into the XBE

Pre-link event assembles stubs automatically — Rebuild works without manual intervention.

### XBE Metadata
- **Title ID:** `0x4C41000B`
- **LAN Key:** `4C41000B4C41000B4C41000B4C41000B`
- **Title Name:** Jedi Knight: Jedi Academy
- **Stack Size:** `0x40000`

---

## MP Build Status — COMPLETE ✅

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

## Testing Notes
- Retail Xbox is the only test target
- Log file at `E:\ja_sp_log.txt` (SP) and `E:\ja_mp_log.txt` (MP) — XBLog flushes every write, last line = crash point

## JO MP Source Reference
- JO MP source is at `D:\Programming\GitHub\jedioutcast-master\CODE-mp\`
- Used for porting Holocron/Jedi Master gametypes to JA MP

---

## Key Technical Notes

### d3d8.lib vs d3d8i.lib
Retail `d3d8.lib` implements D3D methods as inline functions that directly manipulate Xbox hardware registers (`_D3D__RenderState`, `_D3D__DirtyFlags`). These are only exported from the Xbox kernel binary at runtime — not from `xboxkrnl.lib`. The Win32 linker cannot resolve them. Always use `d3d8i.lib` (instrumented) instead.

### XDK Tools Location
All XDK tools are at `C:\XDK\xbox\bin\` — including `imagebld.exe`, `xsasm.exe`, `xbcp.exe`, etc.

### patchxbe.py Location
`code/x_exe/patchxbe.py` — SP XBE post-processor. When wiring MP, copy to `codemp/x_exe/patchxbe.py`.

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
