# Jedi Academy Xbox MP Pause Status - 2026-05-25

## Mandate At Pause

MP work is paused for now. Do not keep tuning MP unless explicitly resumed. The current focus when MP resumes should remain stability and performance without visual downgrades.

Do not push. Do not commit unless explicitly requested.

## Current Deploy

Current MP XBE was rebuilt and deployed:

- Source XBE: `C:\Programming\GitHub\Jedi-Academy-X\codemp\x_exe\Release\jamp.xbe`
- Deployed XBE: `C:\Games\Emulators\CXBX\Jedi Academy rebuild\jamp.xbe`
- Size: `5652480`
- Timestamp: `2026-05-25 08:31:01`
- Build log: `C:\Programming\GitHub\Jedi-Academy-X\scripts\output\mp_build_deploy_current_20260525_082927.log`

## Dedicated Emulator Rules

Use only the dedicated project2 Cxbx instance for automation:

- Emulator dir: `C:\Programming\GitHub\Jedi-Academy-X\CXBXR`
- Loader: `C:\Programming\GitHub\Jedi-Academy-X\CXBXR\cxbxr-ldr-project2.exe`
- Process names allowed to kill: `cxbx-project2.exe`, `cxbxr-ldr-project2.exe`
- Game dir: `C:\Games\Emulators\CXBX\Jedi Academy rebuild`
- Launch: `cxbxr-ldr-project2.exe /load "C:\Games\Emulators\CXBX\Jedi Academy rebuild\jamp.xbe"`

Never change emulator settings. If an emulator setting appears relevant, stop and ask.

## Current Known State

What works:

- MP builds and produces `jamp.xbe`.
- MP boots through menus.
- Direct map loading reaches active gameplay.
- Rendering now fills the viewable area rather than the lower-left corner.
- The major HOM regression in MP was fixed before this pause.
- The current deployed build keeps the safer backbuffer-copy path and direct texcoord optimization.

User-observed unresolved issue:

- The deployed MP build still locks up after running for a while.

Other unresolved MP issues already observed:

- FPS is still too low for target quality. User saw low 20s and sometimes teens in gameplay.
- Saber throw / saber trail effects can draw as repeated default-looking rectangles.
- Some force/saber effects need a pass once base stability/perf is better.
- Character LOD appears to switch visibly.

## Active MP Code Changes To Keep

### `codemp\renderer\tr_shade.cpp`

Direct texcoord shortcut is enabled:

- `JAMP_XBOX_DIRECT_TEXCOORD_SHORTCUTS 1`
- `JAMP_XBOX_DIRECT_COLOR_SHORTCUTS 0`

Reason:

- The 8-minute project2 smoke test survived.
- It reduced copied/generated texcoord work by allowing simple texture coordinate arrays to be submitted directly.
- Direct color remains disabled because earlier color-path work was associated with visual flicker/regression risk.

Smoke result:

- `scripts\output\mp_project2_direct_texcoord_only_20260525_064438.summary.txt`
- `active=True`
- `heartbeats=442`
- `metrics=50`
- `fatalInGameLog=0`
- `receivedExceptionStdout=0`
- `surfaceWarnCount=0`
- Late profile examples were around `issue=21-31`, `backend=21-31`.

### `codemp\renderer\tr_image_xbox.cpp`

`tr.screenImage` is created as `GL_LIN_RGBA8`.

Reason:

- The Xbox backbuffer capture path now copies pixels directly into the destination texture surface.
- Linear format avoids the earlier surface-as-texture/pushbuffer hazard.

### `codemp\win32\win_qgl_dx8.cpp`

Backbuffer capture path uses `CopyRects` into the destination texture surface instead of binding the backbuffer as a texture.

Reason:

- Binding the backbuffer as a texture caused Cxbx-R pushbuffer/surface faults.
- The safer path produced a stable 8-minute smoke in combination with direct texcoords.

`_texImageRGBA` supports `GL_LIN_RGBA8`.

## Unsafe / Reverted Things

Do not re-enable broad D3D state caching casually:

- `JAMP_XBOX_D3D_STATE_CACHE` is currently `0`.

Do not re-enable texture-stage state caching casually:

- `JAMP_XBOX_TEXTURE_STAGE_STATE_CACHE` is currently `0`.

Reason:

- The narrow texture-stage cache looked promising in metrics, but it failed quickly in Cxbx-R stdout with an access violation.
- Test summary: `scripts\output\mp_project2_texstage_only_cache_20260525_070831.summary.txt`
- Result: `receivedExceptionStdout=2`
- Game log still heartbeated briefly, so relying only on `ja_mp_log.txt` would have missed the emulator-side failure.

Do not retry the old push-indexed path blindly:

- `JAMP_USE_DRAWINDEXED_UP` should remain `1`.
- Earlier experiments away from this path were not stable enough.

## Logs And Diagnostics

Runtime log:

- `C:\Games\Emulators\CXBX\Jedi Academy rebuild\ja_mp_log.txt`

Phase log:

- `C:\Games\Emulators\CXBX\Jedi Academy rebuild\ja_mp_phase.txt`

For Cxbx-side exceptions, check redirected stdout/stderr in:

- `C:\Programming\GitHub\Jedi-Academy-X\scripts\output\mp_project2_*.stdout.txt`
- `C:\Programming\GitHub\Jedi-Academy-X\scripts\output\mp_project2_*.stderr.txt`

Important lesson:

- Some failures appear only in Cxbx-R console/stdout, not in the game log.

## Suggested Resume Plan

1. Start from the current deployed source state.
2. Reproduce the user-observed timed lockup with project2 stdout/stderr capture.
3. If it locks without game-log fatal lines, treat Cxbx stdout/stderr as primary evidence.
4. Keep visual fidelity intact while looking for performance gains.
5. Prefer measured hot-path reductions over renderer-state guesses.
6. Do not touch emulator settings.

## Current Working Tree Note

Relevant MP source files currently modified:

- `codemp\renderer\tr_image_xbox.cpp`
- `codemp\renderer\tr_shade.cpp`
- `codemp\win32\win_qgl_dx8.cpp`

The broader tree contains many unrelated generated/build artifacts. Do not clean or revert unrelated files without explicit instruction.
