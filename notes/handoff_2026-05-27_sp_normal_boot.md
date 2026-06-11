# Jedi Academy Xbox Handoff - 2026-05-27

## Current User Intent

This chat is bloated; continue in a new window from this handoff.

Immediate focus is SP normal boot:

- Normal boot through UI to first gameplay must work.
- User initially wanted external key automation, but external `SendKeys` did not reliably drive Cxbx/Xbox input.
- User explicitly changed direction: it is now OK to automate accept/start/menu progression in game code, "gingerly."
- Do not add broad gameplay hacks or visual artifacts. Gate any automation narrowly, preferably behind a marker file, and log it.
- Do not push. Do not commit unless explicitly requested.

Latest small to-do addition:

- Added an uncommitted roadmap item in `AGENTS.md`: outside-file support. Files outside GOBs using the same folder structure should be read and should supersede matching GOB-contained files.
- User asked to only log this; no runtime action should be taken for it now.

## Repository / Paths

- Repo: `C:\Programming\GitHub\Jedi-Academy-X`
- SP source: `code\`
- MP source: `codemp\`
- Dedicated Cxbx instance: `C:\Programming\GitHub\Jedi-Academy-X\CXBXR`
- Game dir: `C:\Games\Emulators\CXBX\Jedi Academy rebuild`
- Junction sometimes used: `C:\Games\Emulators\CXBX\JARebuild`
- Dedicated loader: `C:\Programming\GitHub\Jedi-Academy-X\CXBXR\cxbxr-ldr-project2.exe`
- Dedicated process names only: `cxbx-project2.exe`, `cxbxr-ldr-project2.exe`
- SP XBE source: `C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.xbe`
- SP XBE deploy target: `C:\Games\Emulators\CXBX\Jedi Academy rebuild\default.xbe`
- SP log: `C:\Games\Emulators\CXBX\Jedi Academy rebuild\ja_sp_log.txt`
- Cxbx logs: `C:\Programming\GitHub\Jedi-Academy-X\CXBXR\CxbxDebug.txt`, `KrnlDebug.txt`
- Direct-map control file: `C:\Games\Emulators\CXBX\Jedi Academy rebuild\ja_sp_level.txt`
  - Empty = normal boot.
  - `yavin1` = direct-load yavin intro level.

## Emulator Rules

- Use only the dedicated project2 Cxbx instance for automation.
- Kill only `cxbx-project2.exe` and `cxbxr-ldr-project2.exe`.
- Never kill the user's other Cxbx instances.
- If another Cxbx instance is active or the run stalls, exit/wait about 3 minutes and retry.
- Do not change emulator settings. If emulator settings seem relevant, ask first.
- I restored `CXBXR\settings.ini` back to video/gui-only after a failed temporary keyboard-profile experiment.

## Recent Commits

Latest commits:

- `5df363d` - `Checkpoint JA Xbox renderer and runtime progress`
- `050913c` - `Checkpoint Xbox renderer progress`
- `093ce65` - `Allow Xbox UI screen updates before map load`
- `c0b2a9a` - `Initialize Xbox UI before menu activation`
- `7c97b5d` - `Restore optional Xbox UI boot`

Do not assume the worktree is clean. It contains many generated build/log artifacts and untracked diagnostic outputs. Be careful with staging.

## Current SP State

What works:

- Direct `yavin1` in-engine intro cutscene now works.
- User confirmed yavin1 is fixed.
- Sound works in the direct yavin path.
- GLM characters now appear in the yavin intro.
- The previous yavin intro model/depth issue was fixed without breaking the cutscene.

Important current renderer/runtime fixes to preserve:

- `code\renderer\tr_shade.cpp` has the yavin intro model depth fix: `RB_XboxAdjustYavinIntroModelState()` logs/keeps depth via `XBOX_YAVIN_INTRO_MODEL_DEPTH_KEEP`.
- Existing door/HOM fixes from earlier SP work should not be disturbed unless there is strong evidence they are involved.
- The SP UI is wired back in. 2D UI orientation was fixed earlier.

Current problem:

- Normal boot through menus to first gameplay is not yet working autonomously.
- External keyboard injection reached UI/attract but did not reliably drive the menu.
- Last normal-boot external-input test reached UI init, looped, then played `attract.bik`; it did not reach active gameplay.
- User now permits in-code automation of menu progression.

Recent normal-boot log clues:

- `_UI_Init done`
- `CL_InitUI returned`
- UI refresh loops
- `CIN_PlayCinematic arg='attract'`
- `BinkVideo::Start ... d:\base\video\attract.bik ... exit playing`
- Summary from the failed external-input smoke was roughly:
  - `menuSeen=True`
  - `loadingSeen=True`
  - `activeOrHeartbeatSeen=False`
  - `fatalInLog=False`

## Recommended Next Step

Implement a narrow SP Xbox auto-accept/auto-start facility in code.

Preferred shape:

- Xbox-only code.
- Gated by a marker file, for example `D:\ja_sp_autosmoke.txt`, so it is not active for normal play or hardware tests unless deliberately enabled.
- For smoke runs, create the marker file before launch and remove it after.
- Press/dispatch the same key path the UI already expects rather than hardcoding menu state if possible.
- Log every automated press with state/keycatcher info, but throttle it.
- Stop pressing once the client reaches loading/active state or after a bounded maximum press count.

Files to inspect first:

- `code\client\cl_keys.cpp`
  - `CL_KeyEvent(...)` handles UI key dispatch and cinematic skipping.
- `code\client\keys.h`
  - Confirm internal constants for enter/A/mouse/joy keys.
- `code\win32\win_wndproc.cpp`
  - Shows how keyboard virtual keys map to internal key constants.
- `code\ui\ui_shared.cpp`, `code\ui\ui_main.cpp`, `code\ui\ui_atoms.cpp`
  - Confirm which keys activate UI menu items.
- `code\client\cl_main.cpp`
  - Likely place for a small `CL_XboxAutoSmokeTick()` call during normal UI/cinematic frames.

Possible implementation idea:

- Add `CL_XboxAutoSmokeEnabled()` with a one-time marker-file check.
- Add `CL_XboxAutoSmokeTick()` in `CL_Frame` after UI/cinematic state is current.
- Every 1500-2000 ms, call `CL_KeyEvent(key, qtrue, cls.realtime)` and `CL_KeyEvent(key, qfalse, cls.realtime + 1)` for the correct accept key(s).
- Consider sending both an Enter-style key and a controller/joy accept key only if UI inspection shows both are valid and harmless.
- Do not spam frame-by-frame input.
- Do not keep pressing during actual gameplay.

## SP Build / Deploy / Smoke Skeleton

Build:

```powershell
cd C:\Programming\GitHub\Jedi-Academy-X
powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target sp
```

Deploy:

```powershell
Copy-Item "C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.xbe" "C:\Games\Emulators\CXBX\Jedi Academy rebuild\default.xbe" -Force
```

Normal boot setup:

```powershell
Set-Content "C:\Games\Emulators\CXBX\Jedi Academy rebuild\ja_sp_level.txt" ""
```

Dedicated process helpers:

```powershell
function Get-CxbxProject2Processes {
  Get-CimInstance Win32_Process |
    Where-Object { $_.Name -in @('cxbx-project2.exe','cxbxr-ldr-project2.exe') }
}
function Stop-CxbxProject2Processes {
  Get-CxbxProject2Processes | ForEach-Object {
    try { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue } catch {}
  }
}
```

Launch:

```powershell
$cxbx = 'C:\Programming\GitHub\Jedi-Academy-X\CXBXR'
$game = 'C:\Games\Emulators\CXBX\Jedi Academy rebuild'
Stop-CxbxProject2Processes
Start-Sleep -Seconds 2
Remove-Item "$game\ja_sp_log.txt" -Force -ErrorAction SilentlyContinue
$p = Start-Process -FilePath "$cxbx\cxbxr-ldr-project2.exe" -ArgumentList '/load "C:\Games\Emulators\CXBX\Jedi Academy rebuild\default.xbe"' -WorkingDirectory $cxbx -PassThru -WindowStyle Hidden
```

Use a bounded watchdog. For normal boot through first gameplay, 8 minutes is reasonable. Always stop project2 processes after the run.

## MP State

MP is paused unless explicitly resumed.

Current MP status is documented in:

- `notes\mp_pause_status_2026-05-25.md`

High-level MP state:

- MP builds and boots.
- Direct map loading reaches active gameplay.
- Rendering fills the viewable area.
- Major MP HOM regression was fixed.
- Current deployed MP still locks up after running for a while.
- FPS is still below target.
- Saber/force effects need a later pass.

Do not resume MP unless asked.

## To-Do List

Immediate:

1. Implement gated in-code SP normal-boot automation.
2. Build/deploy SP.
3. Smoke normal boot from empty `ja_sp_level.txt` through first gameplay.
4. Preserve yavin1 direct-load fix and sound/GLM behavior.
5. Keep logs focused and bounded.

Deferred:

- Outside-file support: read loose files outside GOBs using the same folder structure; loose files supersede GOB entries.
- MP stability/performance/effects.
- SP polish items previously observed: shadows, remaining missing textures/materials, Bink/video path as needed.

