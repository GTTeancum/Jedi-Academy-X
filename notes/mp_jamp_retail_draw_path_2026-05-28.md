# MP JAMP Retail Draw Path RE - 2026-05-28

Source XBE:
- `C:\Programming\GitHub\Jedi-Academy-X\Star Wars Jedi Academy game\jamp.xbe`

Goal:
- Keep MP rendering as close to retail `jamp.xbe` as possible before hardware testing.

Findings:
- Retail `GLW_Init` present-parameter constants match the XDK 5558 headers used by the current MP build:
  - `D3DMULTISAMPLE_NONE == 0x11`
  - `D3DSWAPEFFECT_DISCARD == 1`
  - `D3DPRESENTFLAG_WIDESCREEN == 0x10`
  - `D3DPRESENTFLAG_PROGRESSIVE == 0x40`
- The current MP `GLW_Init` already matches the retail CreateDevice block in the important fields:
  - 640-wide widescreen branch.
  - 480p sets 640x480 and flags `0x50`.
  - `D3DFMT_A8R8G8B8`, one backbuffer, linear D24S8 depth/stencil, hardware vertex processing.
- Retail draw contexts around `0x000B054B`, `0x000B0574`, `0x000B09A9`, `0x000B0A3B`, `0x000B0A68`, and `0x000B0E81` show the Xbox renderer building vertex/index payloads into the D3D pushbuffer.
  - It copies `vec4_t` position/normal-style blocks from static tessellation buffers.
  - It emits pushbuffer method words such as `0x4172c`, `0x41730`, and terminates with the pushbuffer submit path.
  - This matches the original source-side `BeginPush` / direct packet / `EndPush` path, not the added `DrawIndexedVerticesUP` helper.

Code change:
- `codemp/win32/win_qgl_dx8.cpp`
  - `JAMP_USE_DRAWINDEXED_UP` changed from `1` to `0`.
  - This restores `dllDrawElements` fallthrough to the retail-shaped pushbuffer path.
- `codemp/win32/win_main_console.cpp`
  - `JAMP_CXBX_SMOKE_STARTUP_COMMAND` changed back to an empty default string.
  - `XBL_Tick()` is called once from the main loop after `Com_Frame()`, matching the retail loop shape found near `0x00051B50`.
  - Always-on phase breadcrumbs now bracket the main-loop XBL tick.
- `codemp/qcommon/common.cpp`
  - Removed the extra `Com_Frame()`-local guarded XBL tick to avoid double-ticking XBL every rendered frame.
- `codemp/client/snd_dma_console.cpp`, `codemp/client/cl_main.cpp`, `codemp/cgame/cg_main.c`
  - `JAMP_CXBX_SMOKE_SKIP_SOUND` default changed from `1` to `0`.
- `codemp/server/sv_init.cpp`
  - `JAMP_CXBX_SMOKE_SKIP_VOICE` default changed from `1` to `0`.
- `codemp/xbox/XBLive.cpp`
  - `JAMP_CXBX_SMOKE_SKIP_XBL_TICK` default changed from `1` to `0`.
- `codemp/win32/xb_log.cpp`
  - Raw NT fallback logging now calls `NtFlushBuffersFile()` after each write.
- `codemp/x_exe/xbox_asm_stubs.asm`
  - The post-CRT fallback loop now sleeps for 1000 ms instead of hot-spinning if CRT startup returns.

Verification:
- MP Release build completed and generated:
  - `codemp\x_exe\Release\jamp.exe`
  - `codemp\x_exe\Release\jamp.xbe`
- Latest hardware-oriented build after the main-loop/XBL cleanup:
  - `codemp\x_exe\Release\jamp.exe` size `5,832,704`, timestamp `2026-05-28 12:05:32`
  - `codemp\x_exe\Release\jamp.xbe` size `5,652,480`, timestamp `2026-05-28 12:05:33`
- Map spot checks from `codemp\x_exe\Release\jamp.map`:
  - `?XBL_Tick@@YAXXZ` at `0x004F6180`
  - `_WinMainCRTStartup` at `0x004FB9B2`
  - `_Sleep@4` linked from `xapilib:synch.obj`
  - `_NtFlushBuffersFile@8` linked from `xboxkrnl:xboxkrnl.exe`
  - `?dllDrawElements@@YAXIHIPBX@Z` at `0x004C7CE0`
- CXBX-R Project2 smoke was not successful with the retail-shaped pushbuffer path:
  - Log reached `SV_SpawnServer begin mp/ffa5`.
  - Last game log line was viewport setup during the first `SCR_UpdateScreen` / `EndFrame`.
  - Emulator crashed outside Xbox code at `cxbxr-emu-project2.dll+0x221742`, AV reading `0x1C362000`.

Interpretation:
- The old `DrawIndexedVerticesUP` path appears to be an emulator compatibility/workaround path, not a retail-accurate path.
- For hardware-first work, keeping the pushbuffer path enabled is the more 1:1 choice.
- For CXBX-R-only smoke, the UP path may still be needed as a separate emulator diagnostic build option.
