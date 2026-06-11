# SP Hardware Boot RE Questions - 2026-05-28

Goal: make the rebuilt SP XBE fail loudly and late on retail Xbox hardware, using the retail XBE as the source of truth when emulator behavior and hardware behavior diverge.

## XBE Header And Startup

- Does the rebuilt XBE preserve the retail certificate/header behaviors that retail hardware enforces before `WinMainCRTStartup`?
- Are our patched library version entries acceptable to retail hardware, or only sufficient for CXBX-R HLE?
- Does the retail CRT startup do anything before `main` that our ASM trampoline bypasses or changes?
- Is our 0x40000 stack size harmless compared with the retail 0x20000 stack commit?
- Are the init flags still `MOUNT_UTILITY_DRIVE` and `LIMIT_64MB`, and does that guarantee `Z:` availability on retail?

## Crash Diagnostics

- Does `precrt_ok` always hit `E:\ja_sp_log.txt` before CRT/static constructor work begins?
- Does every later log write flush to disk so the final line survives a hard crash or dashboard return?
- If the raw `E:` NT path fails, which fallback path is most likely to work on a modded retail Xbox?
- Do emulator-only `D:\ja_sp_*` probes fail fast and harmlessly on retail's read-only game volume?
- Should verbose logging remain opt-in only on hardware to avoid I/O stalls?

## Device Init

- Does `XInitDevices` run at the same point and only once before any controller polling?
- Do our D3D present parameters need to default to the retail XBE values on hardware?
- The current fakegl path uses CXBX-friendly values: `X8R8G8B8`, `IMMEDIATE`, and `PUREDEVICE`. Retail used `A8R8G8B8`, `DEFAULT`, no pure-device bit, and `Flags = 0x10` or `0x50`.
- Is omitting `Direct3D_SetPushBufferSize` safe on retail, or was that only an emulator workaround?
- Does the retail XBE use any HDTV/dashboard bit that we need to preserve when selecting flags?

## Filesystem And Scratch Space

- Do all `Z:` scratch files create, overwrite, and close cleanly on retail after `MOUNT_UTILITY_DRIVE`?
- Does the Bink copy thread fail gracefully if a video source or `Z:` destination is missing?
- Do checkpoint/savegame writes touch only known-good paths and close/flush before level transitions?
- Are GOB/filecode warnings non-fatal in every startup path that matters on retail?

## Media And Cutscenes

- Does the stubbed Bink/RAD path leave enough behavior for hardware boot and menu-to-gameplay traversal?
- Are all video copy operations bounded so a failed FMV cannot block the main thread forever?
- Does the post-Yavin1 FMV transition return control to the next level without relying on emulator timing?

## Input, Sound, And Main Loop

- Does controller detection match retail behavior closely enough to avoid a no-input boot?
- Are DirectSound init and sound bank loading still in the same order as retail?
- Did removing frame sleeps introduce any retail-only starvation risk for XAPI, sound, or input?
- Should active-frame yielding stay emulator-optimized, or should hardware use a more retail-like idle/yield policy?

## Test Protocol

- First hardware build should keep default, non-verbose logging.
- Clear or archive `E:\ja_sp_log.txt`, boot from a clean title folder, and collect the last line if it fails.
- If boot reaches menus, test start, FMVs through `ja01_e.bi`, Yavin1, following FMV, then Yavin1b gameplay.
- If no log is produced, focus on pre-CRT/header/entrypoint questions first.
- If the log stops at D3D creation, test a retail-present-parameter build next.

## Findings From This Pass

- The SP build script had drifted to `mainCRTStartup`, which bypassed the pre-CRT log trampoline. Restored `WinMainCRTStartup` as the SP entry point.
- Restoring the trampoline exposed a hot-spin bug after `_mainCRTStartup` returned. The startup thread now sleeps after writing `post_crt` instead of burning CPU beside the game thread.
- `NtFlushBuffersFile` is now linked and used for raw NT log handles. Pre-CRT and post-CRT probes explicitly flush before close.
- Normal log writes flush unless the line is a `FRAME_HEARTBEAT`; heartbeat flushing was too expensive under CXBX-R and is not a useful crash-point line.
- Current generated XBE audit passes: entry decodes to `_WinMainCRTStartup`, import table is only `xboxkrnl.exe`, raw/fallback log paths are embedded, and the expected Plan-B library table is present.
- CXBX-R yavin1b smoke after the trampoline/sleep/flush changes passes for 60 active seconds, with gameplay FPS back in the high 30s.
