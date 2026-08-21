# Claude Handoff: Native D3D8 Renderer Optimization

Date: 2026-08-05

Repo:

`C:\Programming\GitHub\Star-Trek-Elite-Force-X`

## Objective

Optimize the new native Xbox D3D8 renderer while preserving one shared engine
implementation for SP, two-player co-op, and Holomatch.

The immediate priority is not small presentation cleanup. Get the renderer
complete, stable, and fast:

1. Restore lightmapping.
2. Restore required multipass shader behavior.
3. Improve sustained gameplay FPS without reducing fidelity.
4. Preserve SP/co-op/Holomatch behavior and map loading.

The user has explicitly deferred the vertically flipped 2D HUD and other small
finishing work until the renderer is running well. Do not claim the HUD fixed
without screenshot proof.

## Proven Native Checkpoint

Git checkpoint:

`6b6db31 Checkpoint native D3D8 gameplay`

Checkpoint XBE:

- Path: `build\release\default.xbe`
- Size: 4,403,200 bytes
- SHA256:
  `1102582A5884BE63FD982750783D130B77F98B8EA8DA4E4E93836F9E16EA2F78`
- Texture pool: 15 MiB

Proof already observed by the user:

- XEMU reached live `borg1` gameplay.
- Controls worked.
- World geometry and ordinary textures were coherent.
- XEMU's external counter showed 21 FPS.
- Engine telemetry showed approximately 23 FPS in the same run.
- The same native renderer path now loads on retail Xbox hardware.
- Hardware remains slow, but this establishes that the renderer is not an
  emulator-only accident.

Known visual limitations at this checkpoint:

- Lightmapping is absent.
- Some shaders are incomplete or incorrect.
- The fullscreen 2D HUD is vertically flipped.

Do not move backward to the old renderer merely because it renders more
effects. The purpose of this pass is to make the native D3D8 path complete.

## Current Uncommitted State

The working tree contains a telemetry-only change on top of `6b6db31`.
It does not intentionally change rendering behavior.

Modified source:

- `code\client\cl_main.cpp`
- `code\win32\openjkdf2\fakeglx.cpp`
- `code\win32\xb_log.cpp`

The telemetry counts:

- indexed draws requesting two or more texture stages
- successful mult-texture draws
- stage-1-ready submissions
- stage-1 mismatches
- indexed D3D draw failures
- stage-1 state applications and failures

The ten-second `FRAME_HEARTBEAT` adds:

`mt=attempts/draws/rREADY/mMISMATCH/dfDRAWFAIL s1=applies/failures`

Current telemetry XBE:

- Path: `build\release\default.xbe`
- Size: 4,407,296 bytes
- SHA256:
  `A5EC5461ECACBA14B71A614FB2E78FE7273700A1C6D0EE3551719A1E6B1D7272`

The telemetry XBE builds successfully and has been repacked into the one
retained XEMU ISO, but it has not yet been run. The latest existing
`build\release\ef_sp_log.txt` predates this probe and therefore does not contain
the `mt=` or `s1=` fields.

Unrelated working-tree noise:

- `code\renderer\tr_backend.cpp`
- `code\renderer\tr_model.cpp`

Those two files are reported modified because of line-ending normalization;
their filtered Git content matches the index. Do not rewrite them merely to
clean status.

Also leave these generated tracked files alone:

- `scripts\output\phase1_inventory.txt`
- `scripts\output\phase1_strings.txt`

## Toolchain

Use the clean older XDK:

`C:\XDK_5558\XDK`

Do not switch to the modified 5849 installation.

The current native build intentionally links the full XDK 5558 `d3d8.lib`,
not the previous instrumented `d3d8i.lib`.

Build command:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build_xbox.ps1 `
  -Target sp -ReuseObjects -SkipAssets
```

Holomatch/shared-engine build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\build_xbox.ps1 `
  -Target spmp
```

Repack the one retained XEMU ISO:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run_sp_xemu_smoke.ps1 `
  -Map borg1 -Repack -RepackOnly `
  -DefaultXbe .\build\release\default.xbe `
  -Name native_d3d8_probe
```

Run `scripts\cleanup_generated.ps1` before and after emulator runs. Do not
create per-run ISO copies or retained staging trees.

## Active Renderer Path

Start with these files:

- `code\win32\openjkdf2\fakeglx.cpp`
  - FakeGL-to-D3D8 state translation and indexed D3D submission.
  - `TextureState::SetTextureStageState` applies D3D texture stages.
  - `DrawIndexedPrimitiveUPXbox` is the active indexed draw boundary.
- `code\win32\openjkdf2\fakeglx_jka_compat.cpp`
  - Packs renderer arrays into fixed-function D3D8 vertices.
  - `JkaTryDrawElementsUP` derives the FVF texture count from enabled client
    texture-coordinate arrays.
- `code\renderer\tr_shade.cpp`
  - `DrawMultitextured` binds base texture on stage 0 and lightmap on stage 1.
  - Stage iteration enters `DrawMultitextured` when
    `pStage->bundle[1].image != 0`.
- `code\renderer\tr_bsp_xbox.cpp`
  - `R_LoadRawLightmaps` and `R_LoadXboxOptimizedLightmaps`.
- `code\qcommon\cm_load_xbox.cpp`
  - Raw Xbox BSP loading and lightmap selection.
- `code\win32\win_qgl_dx8.cpp`
  - Native renderer lifecycle and lower-level Xbox D3D submission helpers.

The current raw map path has already proved that Xbox BSPs load and contain
lightmap indices. The missing-lightmap question is now at submission/state
translation, not basic asset discovery.

## Most Useful Existing Performance Sample

The latest pre-probe heartbeat from a stable `borg1` run reported:

```text
fps=21.8
prim=5572
verts=797721
state=3619
ring=5572/12750K/w57/f0
path=2
be=83
```

Interpretation:

- About 5,572 primitive submissions per ten-second reporting window.
- About 12.75 MiB of ring-upload payload in that window.
- 57 ring wraps and zero ring fallbacks.
- Native draw path 2 was active.
- Backend time remains substantial.

The XEMU external counter separately showed 21 FPS. Retail hardware loads this
path but does not yet have a trustworthy measured FPS result for the native
checkpoint.

## Previously Measured and Rejected Candidates

Do not repeat these without new evidence:

- Fence-based persistent indexed vertex/index buffers: failed qualification.
- Fence-free persistent combined streams: approximately 17.0 FPS.
- Fence-free persistent vertex-only stream: approximately 14.7 FPS.
- Whole-program optimization/LTCG: approximately 18 FPS, no gain.
- `d3d8i.lib` as a shipping choice: diagnostic overhead reduced FPS.
- Minimal logger: approximately 17.6 FPS, no gain.
- Removing redundant frame-start depth/stencil clear: approximately 17.8 FPS.
- Immediate presentation interval: approximately 17.4 FPS.
- FVF/vertex-shader state cache: approximately 17.86 FPS, no gain.
- Recurring filesystem-poll cleanup was correct hygiene but did not explain
  the retail slowdown.

Detailed measurements:

`notes\non_ja_performance_candidates_2026-07-31.md`

Useful accepted cleanup/cache commits before the native checkpoint:

- `762f1a1 Cache redundant Xbox texture binds`
- `9f0105f Gate dormant Xbox surface trace classification`
- `b58046a Cache redundant Xbox render state uploads`
- `78d0deb Remove dormant Xbox renderer probe overhead`

## Authoritative Local References

The user specifically requested retail Xbox references beyond Jedi Academy:

- `Z:\Programming\Mercenaries source code\Final_Editor_And_Projects_Folders`
- `Z:\Programming\RM4+JadeSrc`
- `Z:\Programming\UC2004`
- `Z:\Programming\xbox\private\test`
- `C:\XDK_5558\XDK`

Use the real source and XDK samples for D3D8 state, texture-stage, pushbuffer,
and presentation behavior. Do not treat inherited JA Xbox code as the only
authority.

## Architecture Rules

- Common renderer, sound, controls, collision, filesystem, and UI framework
  live under `code\` and are shared.
- SP/co-op game and cgame remain their original libraries.
- Holomatch game, cgame, and bot libraries remain separate under
  `code\holomatch\official`.
- `codemp\` is deprecated and must not become a build, link, include, or
  runtime dependency.
- `default.xbe` is SP/co-op.
- `efmp.xbe` is Holomatch for now.
- Campaign/co-op loading presentation is separate from Holomatch loading
  presentation.
- Runtime textures are DDS-only. No fallback to original JPG/TGA/PNG files.
- There is no Ghoul2 in Elite Force. Do not import Ghoul2 assumptions into EF
  renderer work.

## Optimization Discipline

1. Change one candidate at a time.
2. Build and run between candidates.
3. Record sustained gameplay FPS, not startup or loading FPS.
4. Preserve screenshots for every visual claim.
5. Check SP, split-screen co-op, and Holomatch before accepting shared-engine
   changes.
6. If a candidate regresses rendering, stability, or FPS, return to the last
   accepted commit before trying another.
7. Keep the PC responsive and run builds/tests sequentially.
8. Never commit or push without explicit user instruction.

## Recommended First Move

Run the current telemetry build once on `borg1` and inspect the first two
heartbeats containing `mt=` and `s1=`.

- `mt attempts > 0`, `draws == attempts`, `ready == attempts`, zero mismatch,
  zero draw failures, and zero stage-1 apply failures means D3D receives the
  two-stage path; investigate texture content, combine state, and UVs.
- Zero mult-texture attempts means the shader stage never reaches the packed
  draw path; trace `DrawMultitextured` and `bundle[1].image`.
- Attempts with mismatches means FakeGL client texture arrays and D3D texture
  state are out of sync.
- Apply failures or draw failures are direct D3D8 errors and should be fixed
  before any performance tuning.

After lightmaps and required shader passes are restored, profile sustained
backend cost again. Do not optimize a visibly incomplete frame and call the
result a win.
