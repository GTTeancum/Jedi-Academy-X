# Claude Handoff: Retail Xbox Logging Failure

You are taking over a deep-dive on a retail OG Xbox logging failure in this repo:

Repo:
C:\Programming\GitHub\Jedi-Academy-X

Goal:
Figure out, with evidence, why SP logging is not persisting to disk on retail hardware. Do not do shallow iteration. I want root-cause analysis and the most reliable fix path.

Context:
- This is an Xbox port of Jedi Academy.
- Retail Xbox is the only real test target.
- CXBX-R is only marginally useful. It shows OutputDebugString-style breadcrumbs, but hardware file logging is what matters.
- Logging is the current blocker. We cannot proceed meaningfully until it writes a real log file on hardware.
- The PM/user is frustrated with blind iteration. Please do deep RE and compare against known-working ports before proposing fixes.

Known-working comparison projects:
- C:\Programming\GitHub\OpenJKDF2ogx
- C:\Programming\GitHub\UnrealTournament_1.40

Those projects do successfully create logs on Xbox hardware.

What the user has observed on hardware:
- Multiple historical attempts either created no log at all or created 0-byte marker/log files.
- Files like `ja_crt_entered.txt` / `ja_crt_before_log.txt` have appeared as 0 bytes in some earlier iterations.
- Current main log has also appeared as 0 bytes in previous tests.
- User checked next to the XBE and also in TDATA/UDATA for TitleID `0x4C41000B`; nothing useful there.
- User has said memory-card/savegame paths are not acceptable for this workflow.
- User does not want to dig into save folders every time.
- User wants the log in a practical location like game dir / root-style access, same spirit as UT99/OpenJKDF2.

What CXBX-R showed previously:
- The app reaches `main()`.
- `XBLog_Init()` runs.
- It reported `E:\ja_sp_log.txt`.
- It printed many boot breadcrumbs after that.
- So the in-memory/debug-print side was alive in emulator.
- But CXBX-R is not trustworthy enough to validate the real file-write path.

Current code status as of right now:
- Current SP logger file is:
  C:\Programming\GitHub\Jedi-Academy-X\code\win32\xb_log.cpp
- Current main entry usage:
  C:\Programming\GitHub\Jedi-Academy-X\code\win32\win_main_console.cpp
- Current asm startup stub:
  C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\xbox_asm_stubs.asm
- Current CRT stub file:
  C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\xbox_crt_stubs.cpp
- Current build script:
  C:\Programming\GitHub\Jedi-Academy-X\scripts\build_xbox.ps1
- Current audit script:
  C:\Programming\GitHub\Jedi-Academy-X\scripts\audit_sp_startup.py

Important current facts already verified:
1. The current built SP artifacts are:
   - C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.exe
   - C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.xbe
   - C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\Release\default.map

2. Current hashes:
   - default.exe: EEE793E7351DF3E3C46356E1A96752342738032AD56E73A32C40150180DBB80F
   - default.xbe: 93177B501F717AFF4ED6A5BA11E8493D4CBF3205C2957B3C83A58B0ECC1E154C

3. Current startup audit passes:
   - PE entrypoint matches `_WinMainCRTStartup`
   - `_WinMainCRTStartup` calls `_mainCRTStartup`
   - `_main` calls `_XBLog_Init`
   - imports are only `xboxkrnl.exe`
   - XBE libraries are:
     `XAPILIB, LIBC, D3D8I, DSOUND, XBOXKRNL, XONLINE, D3D8, XGRAPHC`

4. Current audit output showed:
   - `_WinMainCRTStartup` at `0x00446493`
   - `_mainCRTStartup` at `0x004C6BCF`
   - `_main` at `0x0043DE30`
   - `_XBLog_Init` at `0x004C0BD0`
   - `_XBLog_Write` at `0x004C0C50`
   - `_CreateFileA@28` at `0x004C2755`
   - `_WriteFile@20` at `0x004C4048`

5. The current `g_logPath` embedded in the binary is:
   - `D:\ja_sp_log.txt`

6. Current logger path fallback list in `xb_log.cpp` is:
   - `D:\ja_sp_log.txt`
   - `E:\ja_sp_log.txt`
   - `F:\ja_sp_log.txt`
   - `ja_sp_log.txt`

7. Current `xb_log.cpp` behavior:
   - `CreateFileA(...)`
   - `CREATE_ALWAYS`
   - `FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH`
   - persistent handle
   - `WriteFile(...)`
   - `FlushFileBuffers(...)` after each write
   - `OutputDebugStringA(...)` echo as well

8. Current `main` path does call `XBLog_Init()` very early in startup.

9. The old pre-CRT marker approach has already been removed/cleaned up. This is now a simpler startup path that defers logging until after CRT/Xapi init.

10. There is a local audit helper:
    - `C:\Programming\GitHub\Jedi-Academy-X\scripts\audit_sp_startup.py`
    It disassembles the built EXE/XBE and verifies the startup/logging shape.

One nuance in the build script:
- In `scripts/build_xbox.ps1`, the SP link block uses:
  `xapilib.lib;libc.lib;d3d8i.lib;xgraphics.lib;dsound.lib;dmusic.lib;xboxkrnl.lib;x_game.lib;goblib.lib;xonline.lib`
- A separate MP block still mentions `xbdm.lib`; do not confuse that with SP.

What I want from you:
1. Deeply compare JA's file logging path against the working implementations in:
   - OpenJKDF2ogx
   - UnrealTournament_1.40
2. Determine exactly why JA can:
   - reach `XBLog_Init`
   - compile `CreateFileA/WriteFile/FlushFileBuffers`
   - and still fail to produce a non-empty persistent file on retail hardware.
3. Be explicit about Xbox filesystem semantics here:
   - what paths are actually writable for a retail title launched this way
   - whether `D:\`, `E:\`, `F:\`, or relative paths are meaningful/writable from this runtime context
   - whether `CreateFileA` can "succeed" in a misleading way here
   - whether writes are buffered/virtualized/discarded due to mount context or title launch mode
4. If the correct answer is "JA should log exactly the way UT99 does," identify the exact code pattern we should port, not a paraphrase.
5. If there is an XDK/XAPI requirement we're missing, cite it.
6. Please prefer archived XDK docs / primary sources if you use web research.

Constraints:
- Do not suggest more blind test iterations unless they are the final confirmation of a well-supported fix.
- Do not suggest savegame-folder logging as the normal workflow.
- Do not spend time on CXBX-R rendering/debugger issues.
- Logging is the blocker, not renderer work right now.

Helpful local files to inspect first:
- C:\Programming\GitHub\Jedi-Academy-X\code\win32\xb_log.cpp
- C:\Programming\GitHub\Jedi-Academy-X\code\win32\win_main_console.cpp
- C:\Programming\GitHub\Jedi-Academy-X\code\x_exe\xbox_asm_stubs.asm
- C:\Programming\GitHub\Jedi-Academy-X\scripts\audit_sp_startup.py
- C:\Programming\GitHub\OpenJKDF2ogx\...
- C:\Programming\GitHub\UnrealTournament_1.40\...

Please return:
- root cause
- confidence level
- exact code/files to change
- why the previous attempts produced 0-byte files or no files
- the shortest reliable fix path
