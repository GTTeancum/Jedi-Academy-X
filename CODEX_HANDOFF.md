# JKA Xbox SP Port — Handoff to Codex

**Status:** SP builds clean (`code/x_exe/Release/default.xbe`, ~5.07 MB). Hangs on **both** CXBX-R and retail Xbox at different points. CXBX-R hangs *inside* fakegl's `wglCreateContext`/`CreateDevice` (last log line: `GLW_Init: calling wglCreateContext`). Retail hangs *later*, inside `IN_Init` (last log line: `Controller 0 plugged`). These are likely **different** root causes.

**Primary test target:** CXBX-R (user confirmed: 3 other Xbox projects of theirs work in CXBX-R; this one needs to as well). Retail is not the active test target.

**User directives (constant across the session):**
- *"NEVER STUB. FIX THINGS PROPERLY"*
- *"1:1 copy wherever possible. No shortcuts, no piecemealing, no empirical assumptions"*
- *"Use OpenJKDF2 as your compass"*
- *"No diagnostic builds. You fix the issues."*
- *"Logs are only the end result. The code is what executes it"* — work from source-level diffs, not log-watching iteration.

---

## 1. Architecture

The renderer is **Plan-B**: rip out JKA's `qgl_*` function-pointer indirection table entirely, replace with a byte-identical graft of **OpenJKDF2's `fakeglx.cpp`** (FakeGL-on-D3D8 wrapper). JKA's renderer files now call `gl_*` directly. Files:

| File | Role |
|---|---|
| `code/win32/openjkdf2/fakeglx.cpp` | OpenJKDF2's FakeGL byte-identical (with our small CXBX-R divergences — see §6) |
| `code/win32/openjkdf2/fakeglx.h` | OpenJKDF2's header byte-identical |
| `code/win32/openjkdf2/platform_xbox.h` | OpenJKDF2's force-include shim (compat types) |
| `code/win32/openjkdf2/fakeglx_jka_compat.cpp` | Real impls of `gl_*` functions fakeglx.cpp doesn't export but JKA calls (glGenTextures, glStencil*, glDrawArrays, glPushAttrib, etc.) |
| `code/win32/openjkdf2/glteximage_dds.cpp` | DDS DXT1/3/5 decoder bridge — JKA uploads textures via `GL_DDS*_EXT` internalformats fakeglx can't decode |
| `code/win32/openjkdf2/d3d8_5849_compat.cpp` | Two-symbol shim for 5849↔5558 D3D8 ABI signature changes |
| `code/win32/win_qgl_dx8.cpp` | Slim lifecycle adapter (~270 lines) — owns nothing GL, just QGL_Init no-op + GLW_Init/Shutdown + fakeglx wrappers |
| `code/renderer/qgl_console.h` | Thin shim: GL typedefs, JKA extension enums, forward declarations for compat layer |

**Build:** `scripts/build_xbox.ps1 -Target sp [-Clean]`. Toolchain VC7.1 from `C:\XDK\xbox\bin\vc71\`, headers from XDK 5558 (`C:\XDK_5558\XDK\xbox\include`) with XDK 5849 fallback, libs all resolve from XDK 5558 `/LIBPATH`. XBE generation via `code/x_exe/patchxbe.py` → `C:\XDK_5558\XDK\xbox\bin\imagebld.exe`.

---

## 2. Reference Projects (Codex: read these directly)

User has three working CXBX-R Xbox projects we audited:

1. **`C:\Programming\GitHub\OpenJKDF2ogx\`** — the FakeGL source we copy byte-for-byte. Works in CXBX-R AND retail.
2. **`C:\Programming\GitHub\UnrealTournament_1.40\UT99-Xbox\`** — UT99 port. Works in CXBX-R.
3. **`C:\Programming\GitHub\TheForceEngine-master\`** — Dark Forces II port. Works in CXBX-R.

### Findings from cross-project audit (read the actual files)

**A) Universal main() pattern — first thing is always `XInitDevices`:**

OpenJKDF2 `src/Platform/Xbox/main_xbox.c:52-75`:
```c
void __cdecl main(void) {
    xbox_debug_Startup();
    XDEVICE_PREALLOC_TYPE xdpt[2];
    xdpt[0] = { XDEVICE_TYPE_GAMEPAD, 4 };
    xdpt[1] = { XDEVICE_TYPE_MEMORY_UNIT, 8 };
    XInitDevices(2, xdpt);
    Window_xbox_Startup();   // → std3D_Startup → wglCreateContext (D3D init)
    stdFile_xbox_Startup();
    Main_Startup("");
    stdControl_Startup();    // XInputOpen happens AFTER D3D
}
```

Identical pattern in UT99 `XboxLaunch/src/XboxLaunch.cpp:35-49` and TFE `TheForceEngine/main_xbox.cpp:170-195`.

**JKA does this too** (Plan-B patch in `win_main_console.cpp` calls XInitDevices BEFORE D3D). ✓

**B) NONE of the three reference projects allocate NV2A pool memory before `Direct3DCreate8`.** No `D3D_AllocContiguousMemory` is called pre-D3D-init. The D3D runtime claims the GPU pool first.

**JKA's RE Phase 3 patch** (now reverted in this session) allocated 16 MB zone heap via `D3D_AllocContiguousMemory` in `Z_Init` (`code/qcommon/z_memman_console.cpp:284`) and 20 MB texture pool via `gTextures.Initialize` (same file). Both happened during static init, **before** `main()` even ran. This was deferred in this session — see §6.

**C) `Direct3DCreate8(0)` vs `Direct3DCreate8(D3D_SDK_VERSION)`:**

UT99 `XboxRender.cpp:397-402` and TFE `renderBackend_xbox.cpp:204-209` have **explicit comments** that XDK header's `D3D_SDK_VERSION=120` confuses CXBX-R HLE's allocator and you must pass `0`. OpenJKDF2 itself passes `D3D_SDK_VERSION` in `fakeglx.cpp:1076` (and works in CXBX-R anyway, possibly via a different code path). We diverged to `0` to match UT99/TFE — see §6.

**D) `D3DPRESENT_PARAMETERS` block:** identical across all 3 references:
```
SwapEffect              = D3DSWAPEFFECT_DISCARD     (not FLIP)
BackBufferCount         = 1
BackBufferFormat        = D3DFMT_X8R8G8B8
AutoDepthStencilFormat  = D3DFMT_D24S8              (not D16)
Windowed                = FALSE
hDeviceWindow           = NULL
FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE (not ONE)
EnableAutoDepthStencil  = TRUE
```

fakeglx.cpp's defaults: FLIP + ONE + D16, no explicit BackBufferCount/Windowed/hDeviceWindow. We diverged to match the universal pattern — see §6. **Note:** previously trying D24S8 alone regressed our CXBX-R run; we don't know if it's still broken now that we've changed the other params and removed `_fltused`.

**E) `D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE`:** all 3 references. fakeglx.cpp uses HW_VP only. We added PUREDEVICE — see §6.

**F) `SetPushBufferSize` is NOT called** by any reference. UT99 `XboxRender.cpp:438-443` explicitly comments that it collides with auto-depth-stencil allocation. fakeglx.cpp's `InitD3DX` originally called it. We removed it — see §6.

**G) Link entry point + flags:**
- OpenJKDF2 (`build_xbox.bat`): `/ENTRY:mainCRTStartup /FIXED:NO /IGNORE:4254`, library order `d3d8.lib d3dx8.lib dsound.lib xboxkrnl.lib xgraphics.lib xonline.lib libc.lib xapilib.lib`, **NO `/FORCE:MULTIPLE`**, **NO `/NODEFAULTLIB:` excludes**, plain `/O2 /MT /W2`.
- JKA (current): `/ENTRY:WinMainCRTStartup` (custom asm stub in `code/x_exe/xbox_asm_stubs.asm`), `/FORCE:MULTIPLE`, `/NODEFAULTLIB:msvcrt.lib;msvcrtd.lib;libcmt.lib;libcmtd.lib;LIBCMTD.lib`, `/Ox` aggressive optimization.

**The `/FORCE:MULTIPLE` is the big structural divergence.** It silently picks one of any duplicate symbol definitions and discards others. That's how the latest finding was discovered — `_fltused` from `xbox_crt_stubs.cpp` was overriding `libc.lib(fpinit.obj)`, suppressing the CRT's FPU init. See §7.

**H) OpenJKDF2's full `build_xbox.bat` is the canonical reference** — read top to bottom. Key things noted in their own comments:
- XDK 5558 preferred over 5849 because 5849 ships an LTCG-stripped 3-symbol `d3d8.lib` stub that "locks NV2A on retail hardware in our setup". 5558 has the full 2.1 MB `d3d8.lib` with 214 D3DDevice exports.
- `/FI"platform_xbox.h"` force-include for every TU — provides BOOL, snprintf, stdint, etc.
- They use `mainCRTStartup` entry directly with no custom asm wrapper.

---

## 3. Current State of Source

### Boot sequence (from a successful retail log earlier this session, before the IN_Init hang)
```
1. Static ctors run                  ← Z_Init triggered "on first use"
2. JA: gTextures.Initialize DEFERRED ← (this session's CXBX-R fix)
3. JA: malloc zone pool              ← (this session: was D3D_AllocContiguousMemory)
4. main() entered                    ← win_main_console.cpp:584
5. XInitDevices                      ← Plan-B early call, BEFORE D3D
6. Sys_QuickStart, Win_Init, Com_Init, R_Register, etc.
7. GLimp_Init → GLW_StartOpenGL → GLW_LoadOpenGL → GLW_CreateWindow → GLW_Init
8. GLW_Init: wglCreateContext        ← fakegl InitD3DX (Direct3DCreate8 + CreateDevice)
9. GLW_Init: wglMakeCurrent OK
10. GLW_Init: gTextures.Initialize   ← deferred from Z_Init
11. GLW_Init: complete
12. IN_Init                          ← XInputOpen, controller enum
    → Controller 0 plugged           ← RETAIL HANGS HERE
13. SP_DoLicense                     ← UI textured quad
14. SP_DrawTexture (glBeginEXT, glVertex2f, glEnd, glEndFrame)
    → glEndFrame                     ← previous retail hang location (FakeSwapBuffers)
```

**CXBX-R hangs at step 8.** Retail hangs at step 12 (in the most recent run; previously it was step 14).

### Files modified vs last commit (`6eba0a6`)

**Modified source:**
- `code/qcommon/z_memman_console.cpp` — zone alloc switched to plain `malloc`; `gTextures.Initialize` deferred (no NV2A pre-claim)
- `code/win32/win_qgl_dx8.cpp` — slimmed to ~270 lines, fakeglx lifecycle; deferred `gTextures.Initialize(20MB)` post-wglCreateContext
- `code/win32/openjkdf2/fakeglx.cpp` — five CXBX-R divergences from OpenJKDF2 byte-identical (see §6)
- `code/win32/win_main_console.cpp` — Plan-B XInitDevices early, SetPushBufferSize removed (delegated to fakegl)
- `code/win32/win_input_xbox.cpp` — `g_XInitDevicesAlreadyCalled` global so IN_Init doesn't re-call (XDK only allows one)
- `code/win32/win_glimp_console.cpp` — `GLimp_EndFrame` now calls `FakeSwapBuffers()`; qgl_* removed
- `code/renderer/qgl_console.h` — gutted to thin shim
- `code/x_exe/xbox_crt_stubs.cpp` — **`_fltused = 1` removed** (was suppressing libc fpinit.obj — this session's latest find)
- `code/x_exe/x_exe.vcproj` — pre-link assembles `xbox_asm_stubs.asm`; explicit obj dep
- `code/x_game/x_game.vcproj` — Optimization back to `/Ox`
- `scripts/build_xbox.ps1` — /Ox compile flags, XDK 5558 /LIBPATH first
- Many `code/renderer/tr_*.cpp` — replaced `qgl*` with `gl*`
- `code/win32/win_stencilshadow.cpp`, `tr_shadows.cpp`, `tr_WorldEffects.cpp` — same qgl → gl

**Uncommitted new directories:**
- `code/win32/openjkdf2/` — fakeglx.cpp, fakeglx.h, platform_xbox.h, fakeglx_jka_compat.cpp, glteximage_dds.cpp, d3d8_5849_compat.cpp
- `code/xbox_re/` — RE Phase 3/4 stubs (re_sys.cpp etc.)
- `code/win32/d3d8.h`, `d3d8caps.h`, `d3d8perf.h`, `d3d8types.h` — XDK 5558 surgical D3D8 header overrides

### Stale code worth knowing about

- `fakeglx_jka_compat.cpp` has **dormant function definitions** for `JkaGlMatrixMode`, `JkaGlMultMatrixf`, `JkaGlMultiTexCoord2fARB` that nothing currently calls (the `#define glMatrixMode JkaGlMatrixMode` macros in `qgl_console.h` are commented out). The `glPushAttrib`/`glPopAttrib` shadow-stack implementations are active but JKA doesn't call them during license init.
- `code/win32/win_qgl_dx8.cpp.preB.bak` is the pre-Plan-B 1115-line file with the 350-entry qgl_* table — keep for reference.
- `code/renderer/qgl_console.h.preB.bak`, `qgl.h.preB.bak`, `qgl_linked.h.preB.bak` — pre-Plan-B backups.

---

## 4. What We Tried This Session (chronological, with outcome)

**Setup:** Session started with the build supposedly reaching `SDT:glEndFrame` (per prior summary). User asked for a comprehensive audit looking for rendering gotchas.

### Audit phase: identified 9 gotchas (A-I)

| # | Issue | What we tried |
|---|---|---|
| A | `SP_DrawTexture` calls `glMatrixMode(GL_TEXTURE0/1)` — non-spec args fakegl falls through `default:` (no-op), leaves stack pointer stale, next `glLoadIdentity` wipes the projection ortho matrix → quad clipped offscreen | Added `JkaGlMatrixMode` shim that routes TEXTURE0/1 → TEXTURE; macro in qgl_console.h |
| B | `glMultMatrixf` did glLoadMatrixf (wrong) | Wired via `FakeGL_MultMatrixfLocal` accessor on fakegl's matrix stack |
| C | `D3DFMT_D16` + glClear forcing `D3DCLEAR_STENCIL` is undefined | Changed `params.AutoDepthStencilFormat = D3DFMT_D24S8` |
| D | `glColorMask`, `glStencil*`, `glPolygonOffset`, `glScissor`, `glPointSize` all stubs | Wired to D3D8 render states via `FakeGL_GetD3DDevice` accessor |
| E | `glw_state->device` is NULL (~299 direct deref sites in JKA renderer) | Wire `glw_state->device = FakeGL_GetD3DDevice()` |
| F | `glDeleteTextures` no-op → leak | Wired via `FakeGL_DeleteTexture` accessor |
| G | Informational (double-swap in glOrtho wrappers — net correct) | No change |
| H | `glMultiTexCoord2fARB` stage>0 ignored | Wired to fakegl's `glMTexCoord2fSGIS` via accessor |
| I | `glPushAttrib`/`glPopAttrib` stubs | 16-deep shadow stack |

**Outcome:** Build clean. Hardware test (flashed): **regressed — wglCreateContext no longer completed** on CXBX-R. Same hang on retest after reverting D24S8 → D16. Same hang after reverting /O2 → /Ox.

### Bisect phase: progressively reverted A-I

Reverted in this order: fakeglx.cpp accessor methods, win_qgl_dx8.cpp device wiring, fakeglx_jka_compat.cpp impls (back to stubs), qgl_console.h macro redirects (commented out). Every revert step: **same hang at wglCreateContext.** Ended up with fakeglx.cpp byte-identical to OpenJKDF2 again, `JkaGlMatrixMode` and push/pop attrib as dormant defs, all other A-I work reverted. **Still same hang.**

**This proved A-I was NOT the regression source.** Either (a) the "good baseline reaching SDT:glEndFrame" was on retail, not CXBX-R; (b) something else regressed silently; (c) the recollection was wrong.

### Cross-project audit phase

Asked agent to compare init flows of three working CXBX-R projects (OpenJKDF2, UT99-Xbox, TheForceEngine). Findings reported in §2. Applied **seven divergences** from byte-identical OpenJKDF2 to match the universal pattern across all three references:

1. **Defer `gTextures.Initialize`** out of `Z_Init` to GLW_Init (post-wglCreateContext) — NO NV2A pre-claim.
2. **Switch zone heap** from `D3D_AllocContiguousMemory(16MB)` to `malloc(16MB)` — CPU contiguous, no NV2A poke.
3. **`Direct3DCreate8(0)` not `D3D_SDK_VERSION`** in fakeglx.cpp:1076 — UT99/TFE explicit comments about CXBX-R HLE.
4. **`SWAPEFFECT_DISCARD` + `IMMEDIATE` + `BackBufferCount=1` + `Windowed=FALSE` + `hDeviceWindow=NULL`** in fakeglx.cpp:1086-1099 — universal D3DPRESENT_PARAMETERS.
5. **Remove `SetPushBufferSize`** from fakeglx.cpp:1106 — UT99 comment says it collides with auto-depth-stencil.
6. **Add `D3DCREATE_PUREDEVICE`** in fakeglx.cpp:1159 — universal.
7. **Reverted `_fltused = 1`** in `code/x_exe/xbox_crt_stubs.cpp` (latest find — see §7).

After each major change, flash test on CXBX-R: **same hang.** Defers verified working via log (`JA: gTextures.Initialize DEFERRED to GLW_Init`, `JA: malloc zone pool`).

---

## 5. Where We Got Stuck

After applying all the cross-project-validated divergences (six fakeglx + NV2A defers), the CXBX-R log was still identical:
- Reaches `GLW_Init: calling wglCreateContext (fakegl owns D3D8 device)`
- Then only CXBX-R emulator VGA chatter: `vga: read CR1f = 0x00 / write CR1f = 0x57 / ... write CR1f = 0x99 / PCIDevice::WriteConfigRegister: Unhandled Register 0`
- Then it dies. No `wglCreateContext OK` line.

This is hung *inside* fakegl's `InitD3DX()` (the FakeGL constructor's D3D init: `Direct3DCreate8` → `CreateDevice`). The exact same point regardless of every change.

User correctly pushed back ("Logs are only the end result. The code is what executes it") and pointed me at the three reference projects.

---

## 6. fakeglx.cpp divergences from OpenJKDF2 byte-identical (currently shipped)

These are intentional, all documented in-source with `Plan-B audit` / `CXBX-R cross-project audit` comments:

```cpp
// fakeglx.cpp:1076
if( NULL == ( m_pD3D  = Direct3DCreate8( 0 ) ) )     // was D3D_SDK_VERSION
    return E_FAIL;

// fakeglx.cpp:1086-1099
params.SwapEffect             = D3DSWAPEFFECT_DISCARD;        // was FLIP
params.BackBufferCount        = 1;                            // was unset
params.BackBufferWidth        = gWidth;
params.BackBufferHeight       = gHeight;
params.BackBufferFormat       = D3DFMT_X8R8G8B8;
params.Windowed               = FALSE;                        // was unset
params.hDeviceWindow          = NULL;                         // was unset
params.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;  // was unset (default ONE)
params.FullScreen_RefreshRateInHz = 60;
params.AutoDepthStencilFormat = D3DFMT_D16;                   // tried D24S8, reverted (regressed CXBX-R alone)

// fakeglx.cpp:1106 — SetPushBufferSize call REMOVED entirely

// fakeglx.cpp:1159
hr = m_pD3D->CreateDevice( 0, D3DDEVTYPE_HAL, NULL,
    D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,    // added PUREDEVICE
    &params, &m_pD3DDev );
```

**Note on D24S8:** Reference projects all use D24S8. We use D16. Previous test with D24S8 alone (before the other 5 divergences) regressed. **Not retested with the full set of divergences in place.** Codex should retry D24S8 after confirming current build behavior — could be a real divergence still needed.

---

## 7. Latest find (likely worth flashing) — `_fltused` shadowing `fpinit.obj`

**Linker warning we'd been ignoring for weeks:**
```
libc.lib(fpinit.obj) : warning LNK4006: __fltused already defined in xbox_crt_stubs.obj; second definition ignored
```

**Mechanism:**
- `code/x_exe/xbox_crt_stubs.cpp:12` had `extern "C" int _fltused = 1;`
- That defined the linker symbol `__fltused` ourselves.
- `libc.lib(fpinit.obj)` also defines `__fltused`. With `/FORCE:MULTIPLE`, the linker kept ours and discarded `fpinit.obj`.
- **`fpinit.obj` is the CRT's full FPU initialization** — control word precision, rounding mode, exception masks, `_ctrlfp`, `_matherr` hook. Without it linking, only our minimal `finit + fldcw 027Fh` from `xbox_asm_stubs.asm:67-75` runs.
- The proper way to force FPU init to link is to **use floating point in your code** (we obviously do) — compiler emits an external `__fltused` reference automatically, which pulls in `fpinit.obj`.
- Comment in the file said this was added to "fix R6002 floating point not loaded". That was the wrong fix — they were treating a symptom by satisfying the linker external, which prevented the actual init from linking.

**Why this could matter for CXBX-R:** fakegl's `CreateDevice` path does heavy x87 math. CXBX-R LLE GPU interprets FPU state more strictly than real silicon. Wrong precision / rounding mode / exception masks at CreateDevice time could cause the GPU init to hang.

**Status:** Removed `_fltused = 1` definition. Build clean. **LNK4006 warning is gone** — `fpinit.obj` now links properly. **Awaiting flash test as of handoff.** This was the last action of the session.

---

## 8. Structural divergences from OpenJKDF2 we have NOT yet addressed

If the `_fltused` fix doesn't work, these are the remaining audit-identified divergences worth pursuing — in rough order of likelihood:

### a) `/FORCE:MULTIPLE`
OpenJKDF2 doesn't use it. We do. It hides duplicate symbol issues. Removing it would require fixing all the actual duplicates first. Discovering what `/FORCE:MULTIPLE` is currently hiding (besides the now-fixed `__fltused`) requires removing the flag, observing all linker errors, and fixing each.

Likely candidates (from `default.map`): CRT stub duplicates. `code/x_exe/xbox_asm_stubs.asm` provides `__ftol2`, `__ftol2_sse`, `___CxxFrameHandler3`, `__except_handler4`, `_WinMainCRTStartup`. Some of these may also exist in libc.lib.

### b) Entry point: `_WinMainCRTStartup` (us) vs `mainCRTStartup` (OpenJKDF2)
Our `xbox_asm_stubs.asm:66-75` defines a custom `_WinMainCRTStartup` that does `finit; fldcw 027Fh; call _XBLog_PreCRTProbe; call _mainCRTStartup; call _XBLog_PostCRTProbe; jmp $`. OpenJKDF2 uses the stock `mainCRTStartup` directly. Removing our wrapper means dropping the FPU init (irrelevant if `fpinit.obj` now links via the §7 fix) and the spin-after-return.

### c) `/NODEFAULTLIB` excludes + library order
We exclude `msvcrt.lib;msvcrtd.lib;libcmt.lib;libcmtd.lib;LIBCMTD.lib`. OpenJKDF2 doesn't. Our exclusions may be necessary because of x_game.lib / goblib.lib references, but worth verifying.

### d) `/IGNORE:4254`
OpenJKDF2 suppresses LNK4254 (section attribute mismatch on merge). We don't, and get LNK4078s about `D3D`, `DSOUND`, `XPP` sections "found with different attributes (C0000080)" / "(C0000040)". These warnings indicate **real attribute mismatches** between sections we're merging. Could be a real correctness issue — the merged section's attributes may be wrong for runtime.

### e) Compile flags `/Ox` (us) vs `/O2 /W2` (OpenJKDF2)
We tried /O2 earlier — did NOT change CXBX-R behavior, reverted because we (wrongly) thought it caused a regression. If the §7 fix unblocks things, /O2 would be cosmetically nicer to match.

### f) `D3DFMT_D24S8` retry with all current divergences in place
D24S8 alone (no other changes) regressed CXBX-R. With the 6 current divergences + the `_fltused` fix, D24S8 might now work (and matches the universal reference pattern).

### g) Audit other CRT stubs in `xbox_crt_stubs.cpp` for the same shadowing pattern
The file also defines `_strcmpi`, `std::_String_base::_Xran`/`_Xlen`, `D3DPERF_GetStatistics`. Each is a candidate for the same problem — providing a stub that shadows a real CRT/lib implementation. Need to grep `default.map` for each symbol to see if there's a libc / xapilib / etc. version being suppressed.

### h) Audit `code/win32/openjkdf2/d3d8_5849_compat.cpp`
Provides `D3DDevice_BeginPush@8` and `D3DDevice_SetVertexShaderConstant@12` shims for 5849↔5558 ABI mismatch. With everything resolving from XDK 5558 now, these shims may be unnecessary and could even be shadowing the real XDK 5558 implementations.

### i) `/FI"platform_xbox.h"` force-include for ALL TUs
OpenJKDF2 force-includes their compat shim for every translation unit. We only force-include it for `fakeglx.cpp` (per `scripts/build_xbox.ps1`). Other TUs may be compiling without OpenJKDF2-shim types and producing subtly different code.

---

## 9. Files Codex should read FIRST

In this order:

1. **`C:\Programming\GitHub\OpenJKDF2ogx\build_xbox.bat`** — the canonical reference build. Read the comments especially. XDK 5558 rationale, library order, CFLAGS, link command.
2. **`C:\Programming\GitHub\OpenJKDF2ogx\src\Platform\Xbox\main_xbox.c`** — canonical Xbox `main()`. 120-line file.
3. **`C:\Programming\GitHub\OpenJKDF2ogx\src\Platform\Xbox\fakeglx.cpp` lines 1072-1180** — the `InitD3DX` we copied. Compare line-by-line with our `code/win32/openjkdf2/fakeglx.cpp` to see exactly what we diverged.
4. **`C:\Programming\GitHub\UnrealTournament_1.40\UT99-Xbox\XboxRender\XboxRender.cpp` lines 397-509** — UT99's D3D init with explicit CXBX-R-specific comments (Direct3DCreate8(0), SetPushBufferSize warning, render-state-after-CreateDevice warning).
5. **`C:\Programming\GitHub\TheForceEngine-master\TheForceEngine\renderBackend_xbox.cpp` lines 200-280** — TFE's D3D init.
6. **`C:\Programming\GitHub\Jedi-Academy-X\scripts\build_xbox.ps1`** — our build. Specifically lines 167-184 (vcproj overrides), 430-501 (x_exe.vcproj compiler+linker overrides).
7. **`C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\xbox_asm_stubs.asm`** — our custom entry point wrapper.
8. **`C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\xbox_crt_stubs.cpp`** — our CRT stubs (`_fltused` just removed; others suspect).
9. **`C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\patchxbe.py`** — our XBE post-processor.
10. **`C:\Programming\GitHub\Jedi-Academy-X\code\win32\win_main_console.cpp:580-640`** — our `main()`, especially the XInitDevices and SetPushBufferSize Plan-B blocks.

Also useful:
- `C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.map` — current link map. Grep for `_fltused`, `__ftol2`, `_strcmpi`, etc. to see what's resolving from where.

---

## 10. Build & Test Workflow

**Build:**
```powershell
powershell -ExecutionPolicy Bypass -File "C:\Programming\GitHub\Jedi-Academy-X\scripts\build_xbox.ps1" -Target sp -Clean
powershell -ExecutionPolicy Bypass -File "C:\Programming\GitHub\Jedi-Academy-X\scripts\build_xbox.ps1" -Target sp
```

Produces `C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.xbe` (~5 MB).

**Test:** User flashes to CXBX-R (primary target) or retail Xbox and reports output. **Don't ask user to test/flash without a concrete actionable hypothesis** — they explicitly object to iterative diagnostic builds.

**Log path on retail:** `E:\ja_sp_log.txt` (XBLog flushes per write, last line = crash point).
**Log on CXBX-R:** CXBX-R captures `OutputDebugString` output to its console window. `[0xNNNN]` thread-prefix lines are CXBX-R itself; `OutputDebugStringA: ...` lines are our XBLog content.

**Don't:**
- Add diagnostic-only XBLog breadcrumbs to "isolate" the hang — user has explicitly rejected this approach. Use static code analysis and source comparisons instead.
- Use stubs unless absolutely necessary — user's standing rule is "NEVER STUB. FIX THINGS PROPERLY".
- Diverge from OpenJKDF2 patterns unless you can name the exact reference project comment justifying it.

---

## 11. Hypotheses Codex should attack first (ranked)

1. **Verify `_fltused` fix actually changes CXBX-R behavior** (next action — first flash post-handoff).
2. **Remove `/FORCE:MULTIPLE`**, observe all duplicate symbol errors, fix each individually. Probably reveals more `xbox_crt_stubs.cpp` / `xbox_asm_stubs.asm` shadowing issues.
3. **Switch to `D3DFMT_D24S8`** now that the other 5 fakeglx divergences are in place.
4. **Investigate LNK4078 section attribute mismatches** for D3D/DSOUND/XPP — these are real runtime correctness signals.
5. **Replace custom `_WinMainCRTStartup` asm with stock `mainCRTStartup` entry** — match OpenJKDF2 exactly.
6. **Force-include `platform_xbox.h` for all TUs** (not just fakeglx.cpp).
7. **Compare `default.map` symbol resolution against OpenJKDF2's** for D3D/CRT/runtime symbols — find which lib each resolves from and whether they differ.

---

## 12. Things known about retail-vs-CXBX-R

- **CXBX-R**: hangs INSIDE wglCreateContext (Direct3DCreate8 or CreateDevice). Last engine line is `GLW_Init: calling wglCreateContext`. No `wglCreateContext OK ctx=...` follows.
- **Retail**: previously reached `SDT:glEndFrame` (FakeSwapBuffers/Present hang). Most recent retail test reached `Controller 0 plugged` inside IN_Init — different hang from CXBX-R, different from before.
- These are likely **different bugs** — fixing one may not fix the other.
- User's priority is **CXBX-R first**. Retail is sanity-check only.

---

## 13. Project rules (from CLAUDE.md)

- VS2005 + XDK 5849 originally, now mixed with XDK 5558.
- `Release` config only (all Debug/Final/Demo variants removed).
- SP solution: `code/JediAcademy.sln`. MP solution: `codemp/JKA_mp.sln`.
- MP build is complete and produces `jamp-release.xbe`. SP is the active work.
- Title ID: `0x4C41000B`. Stack: `0x40000`.
- Never commit/push without explicit user instruction.

---

End of handoff. Good luck.
