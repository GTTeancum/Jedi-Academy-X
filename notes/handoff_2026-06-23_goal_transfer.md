# Star Trek Elite Force X - Goal Transfer Handoff

Date: 2026-06-23
Repo: `C:\Programming\GitHub\Star-Trek-Elite-Force-X`

## Immediate Reason For Handoff

The current Codex goal record is tool-blocked, not code-blocked.

Current goal:

`Rework actual game intro, main menu, and early load-screen flow so Elite Force systems drive behavior and Jedi Academy code is only platform/toolchain scaffolding; replace JA menu/load behavior where it conflicts with EF.`

`get_goal` reports:

- `status`: `blocked`
- `threadId`: `019eb6b3-7e45-7d02-8825-38f4cc88fc0f`
- `createdAt`: `1782244367`
- `updatedAt`: `1782246222`

The exposed goal tools in this chat only allow:

- read current goal
- create a new goal
- mark an existing goal `complete`
- mark an existing goal `blocked`

There is no exposed unblock/resume/reset operation. Creating a new goal in this same chat fails because the blocked goal still counts as unfinished. Do not falsely mark it complete just to clear the state. Start a fresh chat and create the same goal there.

## Critical Project Rule

Elite Force behavior wins wherever it conflicts with inherited Jedi Academy code. JA is scaffolding for Xbox/toolchain/platform glue only. Do not keep JA menu/load/gameplay behavior behind if/else switches when original EF behavior exists; replace the conflicting JA path.

Do not patch symptoms or hardcode smoke-test outcomes. Fix the underlying system.

## User Constraints To Preserve

- Do not touch `C:\Programming\GitHub\Star-Trek-Elite-Force-X\build\release\BaseEF\default.cfg` unless explicitly asked. The user hand-edits this file.
- Do not commit or push unless explicitly asked.
- Release build only.
- New runtime paths should be logged before asking the user to test.
- Debug log is the practical runtime diagnostic path.
- Keep GLM/Ghoul2 dormant unless there is a hard compatibility wall.
- Avoid leaving huge ISO/temp/build artifacts around. Clean Codex-created junk when safe.

## Current Engineering Blocker

The active UI tree is still mostly Jedi Academy parser-menu architecture, with EF-looking wrapper screens layered on top. Original EF's qmenu system is present in `SP-Mod-Source-Code-master\ui`, but it is not compiled into active `code\ui`.

This is the core blocker for the current goal because the actual EF intro/main-menu/pause/load flow cannot be correct while actions route through JA's `Menus_ActivateByName` parser menus.

Evidence:

- Active `code\ui` contains JA parser menu files:
  - `code\ui\ui_main.cpp`
  - `code\ui\ui_shared.cpp`
  - `code\ui\ui_shared.h`
- Active EF-looking wrapper files:
  - `code\ui\ui_ef_frontend.cpp` (currently untracked)
  - `code\ui\ui_ef_pause.cpp` (currently untracked)
- Original EF qmenu source of truth exists only under:
  - `SP-Mod-Source-Code-master\ui\ui_atoms.cpp`
  - `SP-Mod-Source-Code-master\ui\ui_qmenu.cpp`
  - `SP-Mod-Source-Code-master\ui\ui_menu.cpp`
  - `SP-Mod-Source-Code-master\ui\ui_game.cpp`
  - `SP-Mod-Source-Code-master\ui\ui_controls2.cpp`
  - `SP-Mod-Source-Code-master\ui\ui_crew.cpp`
  - `SP-Mod-Source-Code-master\ui\ui_credits.cpp`
  - `SP-Mod-Source-Code-master\ui\ui_mods.cpp`
  - `SP-Mod-Source-Code-master\ui\ui_preferences.cpp`
  - `SP-Mod-Source-Code-master\ui\ui_sound.cpp`
  - `SP-Mod-Source-Code-master\ui\ui_video.cpp`
  - `SP-Mod-Source-Code-master\ui\ui_turbolift.cpp`

Important API mismatch:

- Active `code\ui\ui_public.h` is `UI_API_VERSION 3` and includes Xbox/JA bridge fields.
- Original `SP-Mod-Source-Code-master\ui\ui_public.h` is `UI_API_VERSION 2`.
- Do not blindly overwrite active `ui_public.h`. Port EF qmenu/screens behind the active API bridge or carefully reconcile the API.

## Current UI/Menu State

`code\x_exe\x_exe.vcproj` currently compiles these UI files:

- `..\ui\ui_atoms.cpp`
- `..\ui\ui_connect.cpp`
- `..\ui\ui_debug.cpp`
- `..\ui\ui_ef_frontend.cpp`
- `..\ui\ui_ef_pause.cpp`
- `..\ui\ui_main.cpp`
- `..\ui\ui_saber.cpp`
- `..\ui\ui_shared.cpp`
- `..\ui\ui_splash.cpp`
- `..\ui\ui_syscalls.cpp`

It does not compile the original EF qmenu files.

Known remaining JA parser-menu leaks from quick grep:

- `code\ui\ui_atoms.cpp`
  - `Menus_ActivateByName("missionfailed_menu")`
  - `Menus_ActivateByName(menuID)`
  - `Menus_ActivateByName(menuname)`
  - `Menus_ActivateByName("ingameMissionSelect1/2/3")`
  - `Menus_ActivateByName("ingameloadMenu")`
  - `Menus_ActivateByName("ingamesaveMenu")`
- `code\ui\ui_ef_pause.cpp`
  - `Menus_CloseAll()`
  - `Menus_ActivateByName(menuName)`
  - `Menus_ActivateByName("mainhud")`
- `code\ui\ui_main.cpp`
  - many `Menus_CloseAll` / `Menus_ActivateByName` paths, including pause, datapad, setup, mission failed, save/load, popup, and parser menu infrastructure

## Changes Made Immediately Before Handoff

The goal-state blocker was investigated. The restart did not clear it.

Small code cleanup performed:

- `code\ui\ui_ef_frontend.cpp`
  - Removed one direct `Menus_CloseAll()` dependency from `UI_EFMainMenu_Open`.
  - The EF frontend now avoids closing JA parser menus as part of opening.
  - It still uses `ui_ef_*` command names for buttons.

Previously started but not completed:

- `code\ui\ui_atoms.cpp`
  - `ui_ef_newgame`, `ui_ef_loadgame`, `ui_ef_configure`, `ui_ef_crew`, `ui_ef_credits`, `ui_ef_quit`, `ui_ef_tour`, and `ui_ef_mods` are recognized and currently suppressed with a log saying original EF qmenu screens are required.
  - This prevents accidental fallback to JA parser menus, but it is not a final implementation.
- `code\ui\ui_splash.cpp`
  - `SP_DisplayLogos()` was changed under `STEFX_ELITE_FORCE_SP` to play `eflogo` then `intro` instead of JA `logos`.
- `code\client\BinkVideo.cpp`
  - `loadScreenOnStop` recognizes `eflogo` as the EF logo movie.

Build status:

- No build was run after the most recent `ui_ef_frontend.cpp` cleanup.
- Earlier before that edit, `build\release\default.xbe` had been produced fresh, but do not treat it as verified for this goal.

## Recommended New-Chat Goal

Set the same goal fresh:

`Rework actual game intro, main menu, and early load-screen flow so Elite Force systems drive behavior and Jedi Academy code is only platform/toolchain scaffolding; replace JA menu/load behavior where it conflicts with EF.`

## Recommended Implementation Plan

1. Add/port EF qmenu core into active `code\ui` without clobbering the active Xbox UI API.
   - Start from `SP-Mod-Source-Code-master\ui\ui_local.h` qmenu type definitions.
   - Start from `SP-Mod-Source-Code-master\ui\ui_atoms.cpp` for `UI_PushMenu`, `UI_PopMenu`, `UI_KeyEvent`, `UI_Refresh` qmenu behavior.
   - Start from `SP-Mod-Source-Code-master\ui\ui_qmenu.cpp` for `Menu_AddItem`, `Menu_Draw`, `Menu_DefaultKey`, etc.
   - Prefer a new EF-owned bridge module if direct replacement risks breaking active API v3.

2. Route active `_UI_Refresh` and `_UI_KeyEvent` through EF qmenu when an EF qmenu is active.
   - Keep Xbox/API bridge intact.
   - Do not make JA parser menus the fallback for EF screens.

3. Port the actual EF main menu/LCARS flow.
   - Source: `SP-Mod-Source-Code-master\ui\ui_menu.cpp`
   - Key functions:
     - `UI_MainMenu`
     - `UI_LCARSIn_Menu`
     - `MainMenu_Init`
     - `MainMenu_Cache`
     - `M_MainMenu_Graphics`
     - `M_Main_Key`
   - Current wrapper `UI_EFMainMenu_Open` is provisional scaffolding. Replace or demote it once the real EF menu stack owns the screen.

4. Wire EF frontend actions to original EF screens.
   - `ui_ef_newgame` -> `UI_NewGameMenu`
   - `ui_ef_loadgame` -> `UI_LoadGameMenu(qtrue)` or original EF equivalent
   - `ui_ef_configure` -> `UI_SetupWeaponsMenu` / EF setup flow
   - `ui_ef_crew` -> `UI_CrewMenu`
   - `ui_ef_credits` -> `UI_CreditsMenu`
   - `ui_ef_quit` -> original EF quit menu
   - `ui_ef_tour` -> `UI_TourGameMenu`
   - `ui_ef_mods` -> `UI_ModsMenu`

5. Replace pause-menu save/load/configure/quit parser fallbacks.
   - Source: `SP-Mod-Source-Code-master\ui\ui_menu.cpp` and `ui_game.cpp`.
   - Do not keep JA `ingamesaveMenu`, `ingameloadMenu`, `ingameSetupMenu`, `quitMenu` parser routes as the final path.

6. After EF intro/main-menu ownership is correct, move the load-screen trigger earlier.
   - Current load-screen system partially works but appears late.
   - Do not solve this with a placeholder; use the real EF/Xbox load-screen path.

7. Build in narrow increments.
   - After qmenu core compiles.
   - After main menu compiles.
   - After action screens compile.
   - After load-screen timing changes.

## Useful Search Commands

```powershell
rg -n "Menus_ActivateByName|Menus_CloseAll|UI_EFMainMenu_Open|UI_EFPauseMenu_Open|UI_MainMenu\(|UI_InGameMenu\(|UI_PushMenu|menuframework_s|Menu_DefaultKey" code\ui SP-Mod-Source-Code-master\ui -S
```

```powershell
rg -n "UI_API_VERSION|typedef struct uiimport_s|typedef struct uiexport_s|UI_Init|UI_Refresh|UI_KeyEvent" code\ui SP-Mod-Source-Code-master\ui -S
```

```powershell
Select-String -Path code\x_exe\x_exe.vcproj -Pattern "ui_.*\.cpp" -Context 1,1
```

## Build/Smoke Notes

Primary build script:

```powershell
.\scripts\build_xbox.ps1 -Target sp -Config Release
```

Capture/smoke scripts have been adjusted toward renderer/log PNG capture rather than desktop capture:

- `scripts\capture_cxbx_sp.py`
- `scripts\smoke_cxbx_sp.ps1`

Respect the user's PC use. If screenshots are required, capture from emulator/XBE renderer where possible rather than the whole desktop.

## Dirty Tree Warning

The worktree is very dirty and includes user/runtime-generated changes. Do not reset or wholesale clean.

Especially do not overwrite:

- `build\release\BaseEF\default.cfg`

Untracked logs and pycache exist. Clean only Codex-created junk when safe and intentional.

## Current Highest-Risk Area

The highest-risk area is trying to wholesale copy `SP-Mod-Source-Code-master\ui` over `code\ui`. That will collide with active API version 3, Xbox save/input hooks, and existing bridge code. The safer path is a controlled EF qmenu integration under the current API, followed by removal of JA parser-menu routes where EF now owns behavior.

