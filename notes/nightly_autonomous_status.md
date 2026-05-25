# Jedi Academy Xbox Overnight Autonomous Status

## Mandate
- Work autonomously throughout the night.
- At the end of each work cycle, update this document with the cycle summary, verification, and new issues to test.
- Re-read this document before starting the next cycle.
- Continue autonomously without asking for input unless human testing is required.
- Do not push. Commit only when explicitly requested.

## Current Priorities
1. Restore proper level lighting.
2. Keep mouth movement working.
3. Keep texture LOD at normal/high quality operation.
4. Preserve the solved door/HOM behavior and avoid changing it unless a new finding proves it is involved.

## Current Baseline
- Last source commit: `a1bc043` (`Stabilize Xbox SP renderer and diagnostics`).
- `academy1` smoke passed with heartbeat active.
- Mouth/lip movement works through the silent Xbox voice-volume path.
- Texture LOD was raised by forcing Xbox `r_picmip=0` and allowing DDS uploads up to 1024px before top-mip skipping.
- Lightmap image bookkeeping now marks `*maps/.../lightmapN` as lightmaps.
- Logs show active world surfaces submitting base texture plus lightmap.

## Open Issues For Investigation
- User reports no visible level lighting despite lightmaps being submitted.
- Need determine whether fakeGL/D3D texture stage 1 is actually applying lightmap modulation during active world draws.
- Need verify whether the LOD lift causes texture allocation failures or runtime instability on heavier levels.

## Work Cycle Log

### Cycle 0 - 2026-05-21 22:19
- Created this status document and recorded the overnight mandate.
- Created a thread heartbeat automation to resume this work every 30 minutes overnight.
- Next action: re-read this file, inspect lighting-stage logs/code, then run another focused build/smoke cycle if emulator is free.

### Cycle 1 - 2026-05-21 23:35
- Re-read this document and continued under the overnight autonomy mandate.
- Added focused fakeGL diagnostics around texture stage 1, lightmap entries, texture-stage state, and D3D HRESULTs.
- Built SP repeatedly and smoke-tested `academy1` with 5-minute watchdog / active-frame windows. Runs stayed stable with heartbeat frames, no `Received Exception`, no `FATAL`, and no texture allocation failure reports.
- Logs prove the renderer is submitting active world surfaces with base texture plus `*maps/academy1/lightmapN`, and fakeGL sees stage 1 enabled/dirty with valid lightmap texture entries.
- Logs also prove D3D accepts the stage-1 modulation state (`D3DTOP_MODULATE`) with `hr=0`.
- Remaining uncertainty: prove the actual `SetTexture(1, lightmap)` call and primitive submission ordering. The old `SetTexture stage=1` counter is inconclusive even though direct texture entries are valid.
- Next action: instrument the exact stage-1 `SetTexture` call and draw submission path, then run another smoke cycle.

### Cycle 2 - 2026-05-22 00:02
- Re-read this document and continued under the overnight autonomy mandate.
- Added direct fakeGL logging for `SetTexture(1, ...)` plus two-texture-coordinate draw samples.
- Built SP and smoke-tested `academy1` with a 5-minute watchdog and active heartbeat window. The run stayed stable: no `Received Exception`, no `FATAL`, no texture allocation failures, and heartbeat frames continued.
- Logs now prove stage 1 does receive concrete lightmap textures and D3D returns `hr=0` for those binds. Draw samples also show the submitted world vertex format carries two texture coordinate sets with nonzero base UVs and lightmap UVs.
- Lightmap load audit shows Xbox lightmaps are converted from RGB565 DDS payloads into RGBA upload data, and `*maps/academy1/lightmapN` entries are correctly marked as lightmaps.
- New working theory: the remaining visible lighting problem is less likely to be "stage 1 never binds" and more likely to be entity/model lighting, lightgrid data decode, shader color generation, or a subtler world-lightmap/content issue.
- Next action: instrument entity lightgrid sampling and diffuse-color generation, then run another smoke cycle.

### Cycle 3 - 2026-05-22 00:17
- Re-read this document and continued under the overnight autonomy mandate.
- Added lightgrid/entity-light/diffuse-color diagnostics and learned the XBE is using `VVLightMan`, not the generic `RB_CalcDiffuseColor` functions.
- Found a real lighting bug: `VVLightManager::RB_CalcDiffuseColor` and `RB_CalcDiffuseEntityColor` were relying on `GL_LIGHTING`, but fakeGL treats `GL_LIGHTING` as a no-op. The code then filled model vertices with constant white or constant entity color, discarding the computed lightgrid values.
- Changed `VVLightManager` to compute software diffuse lighting directly into Xbox/D3D packed vertex colors, using the existing lightgrid ambient, directed light, and local light direction.
- Built SP and smoke-tested `academy1` for 45 active seconds: pass, heartbeat active, no exception/fatal, no texture failures, mouth/lip events still present.
- Smoke-tested `taspir2` for 60 active seconds: pass, heartbeat active, no exception/fatal, no texture failures, mouth/lip events still present.
- Logs now show `VV_ENTITY_LIGHT`, `VV_DIFFUSE_COLOR`, and `VV_DIFFUSE_ENTITY_COLOR` firing with non-constant output colors, proving model/character lighting is no longer flat in the active render path.
- New issues to test: human visual check for character/model lighting on `academy1` and `taspir2`; confirm world/level lightmaps are visibly correct and not merely bound; check whether remaining "level lighting" complaint was model-only or still affects brush/world surfaces.
- Next action: quiet temporary fakeGL proof logs, keep the behavior fix, rebuild, and run clean smokes.

### Cycle 4 - 2026-05-22 00:19
- Re-read this document and continued under the overnight autonomy mandate.
- Throttled the one-off fakeGL stage/lightmap proof logs so future runs keep the important evidence without flooding the log or dragging performance as much.
- Rebuilt SP. Produced `code/x_exe/Release/default.xbe` timestamped `2026-05-22 00:13:24`.
- Clean-smoked `academy1` for 60 active seconds: pass, active, heartbeat count 62, no exception/fatal, no texture allocation failures.
- Clean-smoked `taspir2` for 60 active seconds: pass, active, heartbeat count 62, no exception/fatal, no texture allocation failures.
- Bounded proof counts after throttling: `VV_ENTITY_LIGHT=64`, `VV_DIFFUSE_COLOR=64`, `VV_DIFFUSE_ENTITY_COLOR=64`, `fakegl two-stage draw sample=8`, and `fakegl force stage1 dirty=8`.
- New issues to test: human visual check for improved model lighting; human visual check whether world surfaces still look unlit; if world lightmaps still look wrong, focus next on stage-1 blend math/lightmap color scale rather than model lighting.
- Next action: inspect world lightmap blend/state code and reduce remaining uncertainty without requiring a visible emulator test yet.

### Cycle 5 - 2026-05-22 00:50
- Re-read this document and continued under the overnight autonomy mandate.
- Found a separate texture-quality limiter: DDS/direct uploads now allow large textures, but the RGBA fallback path still clamped decoded textures to 128px. Raised that conservative fallback cap to 512px to better match the user's request for normal/high texture LOD while keeping a memory ceiling.
- Added bounded Xbox lightmap payload statistics in `R_LoadLightmaps` so the log can prove whether converted lightmaps contain real contrast.
- Rebuilt SP. Produced `code/x_exe/Release/default.xbe` timestamped `2026-05-22 00:34:40`. The build wrapper returned nonzero, but `imagebld` completed and the XBE timestamp/size updated.
- First `academy1` smoke stalled before active with no fatal/exception and no heartbeat, matching the known CXBX startup-stall pattern. Waited 3 minutes and retried per operating procedure.
- Retry-smoked `academy1` for 60 active seconds: pass, active, heartbeat count 62, no exception/fatal, no texture allocation failures. Lightmap stats show non-flat data, e.g. `academy1` min 0, max up to 249, average roughly 64-100 across sampled lightmaps.
- Smoke-tested `taspir2` for 90 active seconds: pass, active, heartbeat count 91, no exception/fatal, no texture allocation failures. Lightmap stats show non-flat data, e.g. `taspir2` max near 255, average roughly 51-91 across sampled lightmaps.
- New issues to test: human visual check whether the 512px RGBA fallback improves muddy non-DDS textures; human visual check whether world lighting still looks wrong despite non-flat lightmaps and proven stage-1 binds. If world lighting still appears wrong, next focus is blend/color-space/gamma/overbright behavior rather than missing lightmap content.
- Next action: inspect fakeGL/D3D blend/color stage semantics against expected OpenGL two-pass lightmap modulation and see whether stage color args or texture factor semantics differ on Xbox.

### Cycle 6 - 2026-05-22 01:00
- Re-read this document and continued under the overnight autonomy mandate.
- Ran the current `code/x_exe/Release/default.xbe` through a long `taspir2` stability smoke: `WatchdogSeconds=660`, `ActiveSeconds=480`.
- Result: pass. The game reached active play and stayed active for `480.1` seconds, with `heartbeatCount=472`, `lastHeartbeatFrame=12927`, `failureCount=0`, `fileFatalCount=0`, `consoleFatalCount=0`, and `fatalCount=0`.
- No texture allocation failures appeared with the 512px RGBA fallback cap and 1024px DDS direct-upload cap.
- Observed hidden-smoke heartbeat FPS later in the run around the mid/high 20s. This may reflect the hidden CXBX/logging/headless run rather than the user's visible 100+ FPS, but it is worth human visual confirmation after the LOD lift.
- New issues to test: human visual check for texture sharpness and lighting on the 12:34:40 AM XBE; note visible FPS compared to the previous build; confirm whether the world-level lighting complaint remains after the model-lighting fix.
- Next action: continue code audit of world-lighting semantics, especially whether the Xbox fakeGL stage state exactly matches the intended OpenGL lightmap modulation path.

### Cycle 7 - 2026-05-22 01:18
- Re-read this document and continued under the overnight autonomy mandate.
- Audited the SP `DrawMultitextured` path, fakeGL texture-stage state application, and the clean MP Xbox D3D `win_qgl_dx8.cpp` texture-stage setup.
- Confirmed the active world path is submitting base texture in stage 0 and a `*maps/.../lightmapN` texture in stage 1, with `GL_MODULATE` mapping to `D3DTOP_MODULATE`.
- Confirmed fakeGL's stage-1 D3D state uses `COLORARG1=D3DTA_TEXTURE`, `COLORARG2=D3DTA_CURRENT`, and `COLOROP=D3DTOP_MODULATE`, matching the expected base-texture-times-lightmap behavior.
- Confirmed the logs from the 8-minute `taspir2` run show real world surfaces using lightmaps, non-flat lightmap payloads, valid stage-1 UVs, and non-constant model/entity diffuse output from `VVLightMan`.
- No new source edit in this cycle. The evidence now points away from "missing lightmap bind/content" and toward visible output interpretation: gamma/brightness/overbright, lightmap color scale, or a human-visible question that the automated log cannot settle.
- New issues to test: human visual check whether world lightmaps are visibly modulating after the model-lighting fix; if still too flat/bright/dark, compare lightmap scale/gamma against retail or MP output rather than chasing bind failures.
- Next action: continue autonomous audit for low-risk renderer parity gaps, especially differences between fakeGL and MP's native Xbox D3D path that could affect lightmap brightness without breaking texture mapping.

### Cycle 8 - 2026-05-22 01:26
- Re-read this document and continued under the overnight autonomy mandate.
- Found and patched a low-risk renderer parity gap against MP's native Xbox D3D path: enabled fakeGL texture stages now explicitly set `D3DTSS_TEXTURETRANSFORMFLAGS` to `D3DTTFF_COUNT2`, and disabled stages explicitly reset it to `D3DTTFF_DISABLE`.
- Rebuilt SP. Produced `code/x_exe/Release/default.xbe` timestamped `2026-05-22 01:19:33`.
- Smoke-tested `academy1` for 60 active seconds: pass, active, heartbeat count 62, no exception/fatal, no texture allocation failures.
- Smoke-tested `taspir2` for 90 active seconds: pass, active, heartbeat count 90, no exception/fatal, no texture allocation failures.
- Logs prove D3D accepts the new stage-1 texture coordinate/transform state (`hr=0`) while active world surfaces still use base texture plus `*maps/.../lightmapN`.
- This patch may or may not visibly affect lighting; it is a correctness/parity fix, not a proven lighting cure.
- New issues to test: human visual check whether world lighting changed with the 01:19:33 XBE; if it did not, continue investigating lightmap scale/gamma/overbright or alternate stage math rather than texture-coordinate binding.
- Next action: re-read this document, then audit overbright/gamma/lightmap scaling paths and compare SP Xbox behavior to stock PC/MP expectations.

### Cycle 9 - 2026-05-22 01:40
- Re-read this document and continued under the overnight autonomy mandate.
- Audited the Xbox lightmap and gamma paths. SP Xbox and MP Xbox both load BSP lightmaps as DDS/RGB565 payloads rather than PC's expanded 24-bit lightmap path.
- Verified the actual `taspir2` lightmap data masks are standard RGB565 (`R=0xF800`, `G=0x07E0`, `B=0x001F`), so the current RGB565 decode/channel order is correct.
- Added bounded diagnostics for `R_SetColorMappings` and lightmap DDS masks.
- Rebuilt SP. Produced `code/x_exe/Release/default.xbe` timestamped `2026-05-22 01:36:46`.
- Smoke-tested `taspir2` for 60 active seconds: pass, active, heartbeat count 62, no exception/fatal, no texture allocation failures.
- Runtime diagnostics show `gammaSupport=1`, `fullscreen=0`, `overbright=0`, `identityLight=1`, `r_gamma=1`, `r_intensity=1`, and lightmap masks `0000f800,000007e0,0000001f`.
- Conclusion: the current world-lighting issue is not explained by missing lightmap content, bad lightmap texture binding, wrong RGB565 channel masks, or overbright/gamma crushing.
- New issues to test: human visual check whether the current XBE already looks properly lit after model-lighting and stage-state fixes; if still wrong, next likely suspects are D3D stage math for special shaders, blend/depth state interaction, or material-specific shader collapse rather than global lightmap load.
- Next action: re-read this document, then audit shader collapse/material cases that may bypass the ordinary base-texture-times-lightmap path.

### Cycle 10 - 2026-05-22 02:08
- Re-read this document and continued under the overnight autonomy mandate.
- Audited shader collapse/material cases against MP. The SP `r_vertexLight` collapse differs from MP, but shipped cfg/defaults keep `r_vertexLight=0`, so it is not the likely live cause unless a user cfg overrides it.
- Found a real SP renderer hazard: `tr_shade.cpp` sends specular/environment/bump stages to `glw_state->lightEffects`, but SP's `win_lighteffects.cpp` is currently a no-op stub. On Xbox this could silently skip those material passes.
- Added an Xbox fallback so those stages are logged and continue through the ordinary draw path instead of calling the empty light-effects backend.
- Rebuilt SP. Produced `code/x_exe/Release/default.xbe` timestamped `2026-05-22 02:02:50`.
- Smoke-tested `taspir2` for 120 active seconds: pass, active, heartbeat count 121, no exception/fatal, no texture allocation failures.
- The new `XBOX_LIGHTEFFECTS_FALLBACK` log did not fire during this `taspir2` slice, so early `taspir2` lighting is not explained by skipped specular/environment/bump stages.
- New issues to test: human visual check whether the 02:02:50 build changes any shiny/environment materials later in the game; continue searching for the current level-lighting issue in ordinary lightmap/material state rather than special light-effects paths.
- Next action: re-read this document, then compare ordinary world draw state with MP/fakeGL more deeply, especially color packing, blend state, and whether stage 0/1 alpha or depth state can neutralize lightmap modulation.

### Cycle 11 - 2026-05-22 02:29
- Re-read this document and continued under the overnight autonomy mandate.
- Added a bounded `XBOX_WORLD_STAGE` diagnostic to the ordinary multitexture draw path, then rebuilt SP twice to correct the first overly-strict "world only" filter.
- Latest XBE from this cycle: `code/x_exe/Release/default.xbe` timestamped `2026-05-22 02:22:44`.
- Smoke-tested `taspir2` for 60 active seconds: pass, active, heartbeat count 62, no exception/fatal, no texture allocation failures.
- New diagnostics prove active world surfaces are not being forced into vertex-light or fullbright mode: `r_vertexLight=0`, `r_lightmap=0`, `r_fullbright=0`, `rgbGen=2` (`CGEN_IDENTITY`), vertex color is white, bundle 0 is the base texture, and bundle 1 is a `*maps/taspir2/lightmapN` image.
- This rules out shader color generation/cvar collapse as the reason for flat world lighting. The active world path is the expected base texture multiplied by lightmap.
- Next suspects: lightmap texture upload/storage format, D3D texture-stage defaults not covered by current logging, or the user's remaining "lighting" report referring more to dynamic/entity lighting than static BSP lightmaps.
- New issues to test: human visual check whether the 02:22:44 build has any visible world-lighting change; if not, capture a comparison point with `r_lightmap 1` or an equivalent lightmap-only mode so we can see whether Cxbx displays the lightmap textures themselves correctly.
- Next action: re-read this document, then inspect the uncompressed DDS/RGB565 lightmap upload path and compare fakeGL's stage defaults against the MP native D3D path.

### Cycle 12 - 2026-05-22 03:03
- Re-read this document and continued under the overnight autonomy mandate.
- Found a renderer parity gap: MP's native Xbox D3D path implements `GL_FOG` and `glFog*` through D3D render states, while SP fakeGL was treating fog as inert and the JKA compatibility layer stubbed `glFogf`, `glFogfv`, and `glFogi`.
- Added narrow fakeGL fog support: `GL_FOG` now tracks enabled state, `glFogf/glFogfv/glFogi` update fog density/start/end/color/mode, and fakeGL applies `D3DRS_FOGENABLE`, `D3DRS_FOGTABLEMODE`, `D3DRS_FOGDENSITY`, `D3DRS_FOGSTART`, `D3DRS_FOGEND`, and `D3DRS_FOGCOLOR`. The compat layer now forwards JKA fog calls into fakeGL.
- Rebuilt SP. The build wrapper still returned nonzero, but `imagebld` completed and produced `code/x_exe/Release/default.xbe` timestamped `2026-05-22 02:56:49`.
- Smoke-tested `taspir2` for 60 active seconds: pass, active, heartbeat count 62, no exception/fatal, no texture allocation failures.
- Runtime log showed only the initial disabled fog apply (`enabled=0`); no `glFogf/glFogfv/glFogi` calls fired during this `taspir2` slice. This makes fog support a useful correctness/parity fix, but not yet evidence for the current level-lighting bug.
- Additional log review shows many suspect material passes with `fogPass=1` but `fogNum=0` on glow/environment shaders; actual `RB_FogPass` did not run in the smoke.
- New issues to test: if the user sees grey/doorway/fullscreen fog behavior on a map with real fog, this build should be visually compared; otherwise continue investigating shader/material pass state and lightmap-only debug modes.
- Next action: re-read this document, then add a controlled lightmap-only/runtime material diagnostic or inspect the shader pass state that can make lit world surfaces appear flatter than their bound lightmaps.

### Cycle 13 - 2026-05-22 03:23
- Re-read this document and continued under the overnight autonomy mandate.
- Added a startup-command probe path: `CL_Frame` now reads `D:\ja_sp_commands.txt` on first run and queues any nonblank commands before the automated `devmap`. `scripts\smoke_cxbx_sp.ps1` can now write that file via `-StartupCommand`.
- Rebuilt SP. Produced `code\x_exe\Release\default.xbe` timestamped `2026-05-22 03:09:10`.
- Smoke-tested `taspir2` with `-StartupCommand "r_lightmap 1"` for 45 active seconds: pass, active, heartbeat count 48, no exception/fatal, no texture allocation failures.
- Smoke-tested `academy1` with `-StartupCommand "r_lightmap 1"` for 45 active seconds: pass, active, heartbeat count 47, no exception/fatal, no texture allocation failures.
- Both logs prove the startup cvar took effect in the real world draw path: `XBOX_WORLD_STAGE` entries show `r_lightmap=1`, `r_vertexLight=0`, `r_fullbright=0`, base texture in bundle 0, lightmap in bundle 1, and stage 1 using `GL_REPLACE` as expected for lightmap-only mode.
- Mouth/lip movement stayed alive in both probes (`Xbox silent voice start` entries on each map).
- New issues to test: a human or screenshot-visible check should compare normal mode versus `r_lightmap 1`. If `r_lightmap 1` visibly shows only the lightmaps, then the lightmap textures and stage selection are working and the remaining complaint is likely brightness/material composition. If it does not, the next suspect is texture upload/storage or Cxbx D3D interpretation of the lightmap texture format.
- Next action: re-read this document, then inspect lightmap upload/storage format and fakeGL texture format conversion for anything that could make lightmaps valid in logs but visually wrong.

### Cycle 14 - 2026-05-22 03:43
- Re-read this document and continued under the overnight autonomy mandate.
- Compared SP fakeGL against MP's native Xbox `win_qgl_dx8.cpp` texture-stage update path.
- Found a real fakeGL parity miss: texture entries recorded `GL_TEXTURE_WRAP_S/T`, but `TextureState::SetTextureStageState` never applied `D3DTSS_ADDRESSU` or `D3DTSS_ADDRESSV`. MP applies both on every enabled stage.
- Added `GLToDXTextureAddress` and now apply `ADDRESSU/ADDRESSV` for enabled stages. Stage 1 logs confirm lightmap stages are now clamped (`GL_CLAMP` -> `D3DTADDRESS_CLAMP`) instead of relying on whatever D3D default was active.
- First build failed because this XDK GL header set lacks `GL_CLAMP_TO_EDGE`; fixed the mapper to guard that enum. Rebuilt SP successfully and produced `code\x_exe\Release\default.xbe` timestamped `2026-05-22 03:34:25`.
- Smoke-tested `taspir2` for 60 active seconds: pass, active, heartbeat count 62, no exception/fatal, no texture allocation failures, mouth/lip events still present.
- Smoke-tested `academy1` for 60 active seconds: pass, active, heartbeat count 62, no exception/fatal, no texture allocation failures, mouth/lip events still present.
- New issues to test: human visual check whether the 03:34:25 XBE changes lightmap seams, doorway edge artifacts, or any remaining grey/edge sampling behavior. This fix is more likely to affect seams/edge sampling than global lighting brightness.
- Next action: re-read this document, then continue comparing fakeGL to MP native state for other missing stage defaults that could affect lighting/material composition without touching doors/HOM.

### Cycle 15 - 2026-05-22 03:52
- Re-read this document and continued under the overnight autonomy mandate.
- Compared fakeGL's normal texture-stage update path against MP's native Xbox D3D path and the Xbox post-effect cleanup code.
- Found another plausible state-leak class: fakeGL was setting the main color/alpha/filter/wrap state, but did not reset Xbox-specific texture-stage state that other effects code can touch (`D3DTSS_COLORKEYOP`, `D3DTSS_COLORSIGN`, `D3DTSS_ALPHAKILL`) or LOD overrides (`D3DTSS_MAXMIPLEVEL`, `D3DTSS_MIPMAPLODBIAS`).
- Patched fakeGL so enabled and disabled stages explicitly reset those defaults during the normal stage update. This is a conservative D3D-state hygiene fix and does not touch doors, HOM, area visibility, or mover broadcasting.
- Rebuilt SP. The wrapper returned nonzero as before, but `imagebld` completed and produced `code\x_exe\Release\default.xbe` timestamped `2026-05-22 03:46:30`.
- Smoke-tested `taspir2` for 60 active seconds: pass, active, heartbeat count 61, no exception/fatal, no texture allocation failures, mouth/lip events still present.
- Smoke-tested `academy1` for 60 active seconds: pass, active, heartbeat count 61, no exception/fatal, no texture allocation failures, mouth/lip events still present.
- New issues to test: human visual check whether the 03:46:30 XBE changes grey flashing/doorway panels, world-lighting stability, or any residual material-state weirdness. If no visible change, the next likely line is shader-stage semantic parity (`GL_DECAL`, environment stages, and stage-0 `CURRENT` vs `DIFFUSE`) rather than lightmap loading/binding.
- Next action: re-read this document, then continue auditing shader-stage semantics and active material use before making any higher-risk parity changes.

### Cycle 16 - 2026-05-22 04:05
- Re-read this document and continued under the overnight autonomy mandate.
- Audited shader-stage semantics. `GL_DECAL` remains a known mismatch (`fakeGL` maps it to `D3DTOP_SELECTARG1`, MP maps it to `D3DTOP_DOTPRODUCT3` for bump mapping), but the current `academy1`/`taspir2` probes did not log any active `GL_DECAL` or light-effects fallback use, so that path was left untouched for now.
- Patched the live ordinary stage path to match MP's stage argument setup: fakeGL now uses `D3DTA_CURRENT` for `COLORARG2` and `ALPHAARG2` on all stages instead of special-casing stage 0 as `D3DTA_DIFFUSE`. D3D's stage-0 current input is the incoming vertex diffuse color, so this should preserve the model-lighting fix while matching MP's native Xbox renderer.
- Rebuilt SP. The wrapper returned nonzero as before, but `imagebld` completed and produced `code\x_exe\Release\default.xbe` timestamped `2026-05-22 03:58:26`.
- Smoke-tested `taspir2` for 60 active seconds: pass, active, heartbeat count 62, no exception/fatal, no texture allocation failures.
- Smoke-tested `academy1` for 60 active seconds: pass, active, heartbeat count 62, no exception/fatal, no texture allocation failures, mouth/lip events still present.
- Logs confirm the new stage state (`colorArg2=0x00000001`, `alphaArg2=0x00000001`) is applied during active rendering.
- New issues to test: human visual check whether the 03:58:26 XBE changes character/world shading relative to 03:46:30. If no visible change, defer `GL_DECAL` until a map/material proves it is active; continue looking at remaining lighting through visibility, dynamic lights, and lightgrid/world blend behavior.
- Next action: re-read this document, then audit dynamic-light and lightgrid/world interaction paths without touching doors/HOM.

### Cycle 17 - 2026-05-22 05:00
- Re-read this document and continued under the overnight autonomy mandate.
- Audited the SP dynamic-light path under `VV_LIGHTING`. Found a live renderer gap: `VVLightMan` receives dynamic lights and marks surfaces with `tess.dlightBits`, but the SP Xbox `LightEffects::RenderDynamicLights()` backend is stubbed and returns false, so projected/world dynamic-light passes were not being drawn.
- Added bounded diagnostics for submitted VV dynamic lights and for the render attempt. Logs show live dynamic lights on `taspir2` and the stubbed backend returning `rendered=0`.
- Added a narrow Xbox fallback: `tr.dlightImage` now exists on Xbox, `R_CreateDlightImage` generates the built-in 64x64 falloff image with the Xbox `R_CreateImage` signature, and `ProjectDlightTextureVV` projects `VVLightMan` lights directly when the light-effects backend does nothing.
- Rebuilt SP. Produced `code\x_exe\Release\default.xbe` timestamped `2026-05-22 04:53:18`, size `5181440`.
- Smoke-tested `taspir2` for 180 active seconds: pass, active, heartbeat count 178, no exception/fatal, no texture allocation failures, and mouth/lip events still present.
- Runtime counts from that smoke: `VV_ADD_DLIGHT=16`, `XBOX_RENDER_DLIGHT=3`, `XBOX_PROJECT_DLIGHT_FALLBACK=3`, confirming the new fallback is exercised in gameplay.
- New issues to test: human visual check whether projectile/explosion/dynamic light flashes now appear on `taspir2`; verify whether this changes perceived "proper lighting" in active gameplay. Static BSP lightmaps still need visual confirmation separately.
- Next action: re-read this document, run a second-map smoke for regression coverage, then continue inspecting static world-lighting parity if dynamic-light fallback stays stable.

### Cycle 18 - 2026-05-22 05:04
- Re-read this document and continued under the overnight autonomy mandate.
- Smoke-tested the same 04:53:18 dynamic-light fallback XBE on `academy1` for 90 active seconds: pass, active, heartbeat count 91, no exception/fatal, no texture allocation failures.
- Mouth/lip movement remained active on `academy1` (`Xbox silent voice start=15` in this run).
- `academy1` did not submit dynamic lights during this slice (`VV_ADD_DLIGHT=0`, `XBOX_RENDER_DLIGHT=0`, `XBOX_PROJECT_DLIGHT_FALLBACK=0`), so this was a stability/regression pass rather than a dynamic-light visual proof.
- Hidden-smoke heartbeat FPS on `academy1` stayed roughly 60-80 FPS late in the cutscene; `taspir2` remains the heavier dynamic-light regression point.
- New issues to test: visually compare `taspir2` projectile/explosion lighting on the 04:53:18 XBE; static BSP/world lighting still needs human visual confirmation because logs prove submission but cannot prove perceived brightness.
- Next action: re-read this document and continue static world-lighting parity review, focusing on differences that can affect perceived brightness without touching doors/HOM.

### Cycle 19 - 2026-05-22 05:15
- Re-read this document and continued under the overnight autonomy mandate.
- Audited static/world-lighting parity after the dynamic-light fallback. Compared SP Xbox lightmap loading, overbright/color mapping, and fakeGL stage math against MP/native Xbox renderer behavior.
- Did not change source in this cycle. The main tempting lead, `overbright=0` in CXBX, matches both SP and MP defaults (`r_overBrightBits` defaults to `0`) and should not be forced without visual proof.
- Smoke-tested the current 04:53:18 XBE on `taspir2` with startup command `r_lightmap 1` for 45 active seconds: pass, active, heartbeat count 47, no exception/fatal, no texture allocation failures.
- Logs again prove the runtime cvar path works and the world draw path enters lightmap-only mode: `Startup command=1`, `r_lightmap=1` in all 32 sampled `XBOX_WORLD_STAGE` entries, real `*maps/taspir2/lightmapN` textures bound on stage 1, `GL_REPLACE`/`D3DTOP_SELECTARG1` stage state accepted by D3D, and valid stage-1 UVs.
- The same run kept mouth/lip events alive (`Xbox silent voice start=9`).
- Noted existing `BMODEL_VERTEX_LIGHT_BLEND` breadcrumbs in the log. Those relate more to mover/breakable model lighting/shading polish than to the static BSP lightmap path, so they remain lower priority per user direction.
- New issues to test: human visual comparison of normal mode vs `r_lightmap 1` remains the key static-lighting question; if `r_lightmap 1` visibly shows contrast, static lightmaps are functionally present and remaining work is brightness/material perception. If it appears flat, the next target is texture upload/storage interpretation despite valid binds.
- Next action: re-read this document and continue reviewing material-stage cases that can alter perceived world brightness without changing doors/HOM or mover broadcasting.

## Human Test Queue
- Visual check whether the latest build changes texture sharpness.
- Visual check whether lighting is still flat after lightmap bookkeeping correction.
- Visual check whether the 01:19:33 texture-transform parity XBE changes world lighting.
- Visual check the 01:36:46 diagnostic XBE if convenient; behavior should match 01:19:33 except for additional log evidence.
- Visual check whether the 03:46:30 stage-reset XBE changes grey flashing/doorway panels or residual material-state artifacts.
- Visual check whether the 03:58:26 stage-arg parity XBE changes character/world shading relative to 03:46:30.
- Visual check whether the 04:53:18 dynamic-light fallback XBE shows projectile/explosion/dynamic light contribution on `taspir2`.

### Cycle 20 - 2026-05-22 07:53
- Re-read this document and continued under the overnight autonomy mandate.
- User reported that loading UI and HUD/UI textures are upside down, while lighting and mipmaps look better; some textures still fail to load/show white.
- Audited the two affected 2D paths separately. The legal/title splash uses `SP_DrawTexture` with direct immediate-mode GL texcoords; HUD/menu/font elements use renderer `RB_StretchPic` / rotate-pic 2D commands.
- Patched only Xbox 2D texture-coordinate handling: `RB_StretchPic`, `RB_RotatePic`, `RB_RotatePic2`, and the splash strip now invert V with `1.0f - t` on Xbox. World/BSP/model texture coordinates are not changed.
- Rebuilt SP successfully. Fresh XBE: `code\x_exe\Release\default.xbe`, timestamp `2026-05-22 07:52:30`, size `5181440`.
- Smoke test pending: a non-isolated/user CXBX process was active after build, so I did not launch a second emulator instance immediately to avoid a false stall/resource collision.
- New issues to test: loading/legal/title splash should be right-side-up; HUD/menu/font/rotated UI should be right-side-up; watch for intentionally mirrored UI arrows or sliders; continue tracking any missing/white textures separately from the UI flip.
- Next action: wait for CXBX contention to clear, then run a short isolated smoke pass and continue investigating missing textures.
- Copied the fresh XBE to `C:\Games\Emulators\CXBX\Jedi Academy rebuild\default.xbe` after build; any already-running emulator instance still has the old image in memory until restart.

### Cycle 21 - 2026-05-22 08:05
- Re-read this document and continued under the overnight autonomy mandate.
- Checked emulator/process state before running another test. The user's non-isolated `cxbx.exe` is still active from `C:\Games\Emulators\CXBX\cxbx.exe`, so I did not start the isolated CXBXR instance and risk a false stall.
- Confirmed the freshly built 07:52:30 XBE has been copied to the game directory as `C:\Games\Emulators\CXBX\Jedi Academy rebuild\default.xbe`; the already-running emulator must be restarted to pick it up.
- Re-audited the previous visible/test log for the user's "some textures not loading" report. In that log there were no texture allocation failures, no image lookup failures, no fallback/default stage images in active suspect logs, and no `fallback0=1` hits. That means at least the previous white panels were not obviously caused by ordinary `R_FindImageFile` failure or fakeGL texture allocation failure.
- Conclusion: the missing/white visual issue should be treated separately from the UI V-flip and probably needs a fresh log from the 07:52:30 XBE before changing code. The next useful evidence is whether the white surfaces persist after the 2D-only flip, and whether they log as sky/fog/special shader surfaces rather than missing image assets.
- New issues to test: restart CXBX-R so it loads the 07:52:30 XBE; verify splash/HUD orientation; if white surfaces remain, capture the map/context because logs currently do not show image-load failure for the prior run.
- Next action: when emulator contention clears, run an isolated short smoke and inspect fresh logs for UI flip side effects and shader/image fallback evidence.

### Cycle 22 - 2026-05-22 08:36
- Re-read this document and analyzed the user's fresh run from the 07:52:30 UI-flip XBE.
- Important finding: the fresh log still had `D:\ja_sp_commands.txt` present with `r_lightmap 1`, so the run was in lightmap-only debug mode. The log explicitly shows `CL_Frame firstRun: queue startup command 'r_lightmap 1'` and all sampled `XBOX_WORLD_STAGE` entries report `r_lightmap=1`.
- Cleared the stale command file from `C:\Games\Emulators\CXBX\Jedi Academy rebuild\ja_sp_commands.txt`. The level selector file remains set to `taspir2`.
- Texture evidence from the fresh run: no `could not find`, no texture allocation failures, no `CreateTexture failed`, and no `fallback0=1` hits. This argues against missing asset files or normal texture upload failure as the cause of the white spaces seen in that run.
- The remaining `fallback1=1` entries are from single-texture/blended shader stages whose second bundle is naturally null; their primary textures are real (`textures/taspir/door`, `textures/taspir/door_glow`, `textures/taspir/256_128rustv`, `textures/factory/env_pipe`) and are not default/fallback images.
- New issues to test: rerun the same 07:52:30 XBE after the command-file clear and verify normal textured mode (`r_lightmap=0`) before judging white spaces/missing textures. If white panels persist in normal mode, continue with shader/sky/fog/clear-path diagnostics rather than image-loading diagnostics.
- Next action: continue autonomous investigation after a normal-mode confirmation log; do not treat the stale `r_lightmap 1` run as proof of missing textures.

### Cycle 23 - 2026-05-22 08:43
- Re-read this document and checked whether an autonomous normal-mode smoke test was safe to run.
- Confirmed `ja_sp_commands.txt` is still absent, so the next run should not queue `r_lightmap 1`; `ja_sp_level.txt` remains `taspir2`.
- Confirmed the copied game XBE is still the 07:52:30 UI-flip build and the latest log is still the 08:27 stale-lightmap run.
- A non-isolated user CXBX process is currently active from `C:\Games\Emulators\CXBX\cxbx.exe`, so I did not launch the isolated CXBXR instance and risk the known shared-resource stall.
- No source changes in this cycle. The next useful evidence is a normal-mode log or visual pass from the already-copied 07:52:30 XBE.
- New issues to test: restart/run after the command-file clear and confirm the log says `r_lightmap=0`; if UI is fixed and white spaces remain, collect the normal-mode shader evidence.
- Next action: wait for CXBX contention to clear, then run isolated smoke; meanwhile continue source/log audit only.

### Cycle 24 - 2026-05-22 22:20
- Re-read this document and incorporated the user's new focus: concentrate on the first chronological Yavin sequence, especially the opening Yavin cutscene level, then the first 2-3 maps (`yavin1`, `yavin1b`, `yavin2`).
- Reduced broad renderer log volume so Yavin smoke logs remain useful: world-cull/add/mark-leaves, sky iteration, stage suspect/world-stage, lightgrid/VV entity light, backend draw-surf, and swap-buffer breadcrumbs now use much smaller budgets. This was diagnostic-only; no renderer behavior was intentionally changed.
- Rebuilt SP successfully. Fresh XBE: `code\x_exe\Release\default.xbe`, timestamp `2026-05-22 22:10:53`, size `5435392`.
- Ran hidden isolated CXBX-R smoke tests for 8 minutes each on `yavin1`, `yavin1b`, and `yavin2` using `ja_sp_level.txt` direct-map boot.
- Results: all three maps stayed alive until the watchdog, reached `CA_ACTIVE`, had active frame returns and hundreds of framerate heartbeats, and logged no fatal exception, no spawn error, no texture allocation failure, and no `EmuXB2PC_D3DFormat: Unknown Format (0x000000FF)` reproduction.
- Map details: `yavin1` had 335 heartbeats and opened Bink during the intro path; `yavin1b` had 448 heartbeats; `yavin2` had 446 heartbeats. Logs were roughly 400-429 KB after the log-budget cut.
- Current evidence says the first Yavin maps are runtime-stable in hidden smoke. Remaining Yavin work is visual correctness, especially the white/grey sky or portal-looking panels and any special shader behavior in the opening cutscene path.
- New issues to test: human visual pass on the 22:10:53 XBE through `yavin1` opening cutscene and transition into `yavin1b`; report whether white panels/grey portal surfaces still appear, and in which exact camera/location.
- Next action: keep investigation scoped to `yavin1`, `yavin1b`, and `yavin2`; add narrow sky/portal/special-surface diagnostics rather than broad renderer logging.

### Cycle 25 - 2026-05-23 00:35
- Re-read this document and stayed scoped to the first chronological Yavin maps: `yavin1`, `yavin1b`, and `yavin2`.
- Backed out the broad Xbox global-fog clear experiment from `RB_DrawBuffer`; hidden logs showed `XBOX_GLOBAL_FOG_CLEAR` never fired on the Yavin maps, so it was not useful evidence for the white/grey panel issue.
- Added one narrow visual diagnostic for the Yavin sequence only: on Xbox, `RB_StageIteratorGeneric` now skips `textures/common/gradient2` only when the active map is `yavin1`, `yavin1b`, or `yavin2`, and logs `XBOX_YAVIN_SKY_OVERLAY_SKIP`. This is intended to test whether the additive `gradient2` overlay is creating the white/grey Yavin panels.
- Reduced a few old door/bmodel breadcrumb budgets (`CG_MOVER`, `CG_MOVER_DOOR_NOCULL`, `R_BMODEL_FORCE_NOCULL`) so logs stop ballooning while preserving the actual door/HOM behavior.
- Rebuilt SP successfully. Fresh XBE: `code\x_exe\Release\default.xbe`, timestamp `2026-05-23 00:04:40`, size `5435392`.
- Ran hidden isolated CXBX-R smoke tests for 8 minutes each on `yavin1`, `yavin1b`, and `yavin2` using the 00:04:40 XBE.
- Results: all three maps stayed alive until the watchdog, reached `CA_ACTIVE`, logged active frame returns, produced hundreds of `FRAME_HEARTBEAT` entries, and had no fatal exception, no spawn error, and no `EmuXB2PC_D3DFormat: Unknown Format (0x000000FF)`.
- Map details: `yavin1` had 339 heartbeats and 12 overlay skips; `yavin1b` had 449 heartbeats and 12 overlay skips; `yavin2` had 446 heartbeats and 12 overlay skips. Saved logs: `scripts/output/smoke_yavin1_gradient2skip_quiet_20260523_001331.log`, `scripts/output/smoke_yavin1b_gradient2skip_quiet_20260523_002136.log`, and `scripts/output/smoke_yavin2_gradient2skip_quiet_20260523_002941.log`.
- Current evidence: the `gradient2` diagnostic is active and stable, but hidden smoke cannot confirm whether it fixes the visible white/grey panels. The remaining suspect if this does not visually help is `textures/skies/cloudlayer_yavin`, especially its additive first pass and `alphasquare` mask pass.
- New issues to test: visually run the 00:04:40 XBE on `yavin1` first; confirm whether the white/grey Yavin sky/portal panels are gone, reduced, or unchanged. If unchanged, next pass should target `textures/skies/cloudlayer_yavin` rather than missing texture loading.
- Next action: keep `ja_sp_level.txt` pointed at `yavin1` for the opening-cutscene-first workflow and wait for visual confirmation before making a second sky-overlay behavior change.

### Cycle 26 - 2026-05-23 01:45
- Re-read this document and kept the current Yavin visual diagnostic in place for later human testing.
- Shifted autonomous work to things hidden smoke can prove: normal UI boot, Bink/cinematic startup, long `yavin1` progression, and control coverage on `yavin1b`/`yavin2`.
- Smoke-tested the 00:04:40 XBE with empty `ja_sp_level.txt` for 5 minutes. Result: alive until watchdog, UI initialized, no fatal exception, and Bink opened `d:\base\video\attract.bik` twice with no Bink failure. This confirms normal UI/attract boot is stable with the current build.
- Smoke-tested direct `yavin1` for 12 minutes. Result: alive until watchdog, reached active gameplay, 563 frame heartbeats, no fatal exception, no spawn error, no texture allocation failure, and no `EmuXB2PC_D3DFormat: Unknown Format (0x000000FF)`. The run opened `ja01_e.bik` and `ja02.bik` successfully and continued through the early Yavin sequence.
- Smoke-tested direct `yavin1b` and `yavin2` for 5 minutes each. Results: both alive until watchdog, both reached active gameplay, both had hundreds of heartbeats, no fatal exception, no spawn error, and no unknown D3D format error.
- Noted one nonfatal cinematic breadcrumb for later: `jk0101_sw` is requested and not found before the successful `ja01_e` playback. It did not block the run, so it is lower priority than visible Yavin rendering.
- Reduced old high-frequency proof logs now that they have served their purpose: fakeGL begin-state, texture2D toggle, viewport-apply, and VV diffuse entity color samples now keep smaller budgets. This is diagnostic-only and should not change rendering behavior.
- New issues to test: visually run the copied XBE on `yavin1` and report whether the Yavin white/grey panels changed. If unchanged, next likely target remains `textures/skies/cloudlayer_yavin`, especially its additive/mask passes.
- Next action: rebuild after the log-budget cleanup, run short normal UI and Yavin smoke tests to confirm behavior stays stable and logs shrink, then leave `ja_sp_level.txt` on `yavin1` for user testing.

### Cycle 27 - 2026-05-23 02:25
- Re-read this document and completed the log-budget cleanup follow-up from Cycle 26.
- Rebuilt SP successfully after the diagnostic log-budget changes. Fresh XBE: `code\x_exe\Release\default.xbe`, timestamp `2026-05-23 01:42:12`, size `5435392`; it has also been copied to the game directory as `default.xbe`.
- Smoke-tested normal UI boot for 3 minutes with empty `ja_sp_level.txt`. Result: alive until watchdog, UI initialized, no fatal exception, no unknown D3D format error, and a smaller 113 KB log. This keeps normal boot/attract coverage green.
- The first immediate direct-map retry after UI boot produced the known early CXBX-R stall signature: tiny 8 KB log ending around `Com_Frame #0`, no fatal text, and no active gameplay. After the documented 3-minute cooldown, the same `yavin1` direct boot passed normally.
- Smoke-tested direct `yavin1` for 5 minutes after cooldown. Result: alive until watchdog, reached active gameplay, 171 heartbeats, 12 `XBOX_YAVIN_SKY_OVERLAY_SKIP` entries, no fatal exception, no spawn error, and no unknown D3D format error.
- Smoke-tested direct `yavin1b` and `yavin2` for 3 minutes each using the cooldown-aware cadence. Results: both alive until watchdog, both reached active gameplay, both had active frame returns and more than 160 heartbeats, both logged 12 overlay skips, and neither had fatal/format errors.
- Updated operational note: immediate back-to-back isolated CXBX-R launches can still stall before game work without producing a crash log. Treat tiny early logs ending before active state as emulator contention, wait 3 minutes, and retry before calling it a game regression.
- New issues to test: visually run the 01:42:12 XBE on `yavin1` and check whether the Yavin white/grey panels changed. If unchanged, next likely visual diagnostic should move from `textures/common/gradient2` to `textures/skies/cloudlayer_yavin`.
- Next action: keep `ja_sp_level.txt` on `yavin1` and avoid further visual-surface changes until the current Yavin diagnostic has a human visual result.

### Cycle 28 - 2026-05-23 02:42
- Re-read this document and worked on an autonomous testing problem rather than another visual renderer change.
- Improved the reusable CXBX-R smoke harness. `scripts/smoke_cxbx_sp_retry.ps1` now passes through `-StartupCommand` and treats `FAIL_NOT_ACTIVE` with a tiny early log and no fatal markers as retryable emulator startup contention. `scripts/smoke_cxbx_sp.ps1` now recognizes active Bink/cinematic tail output as progress, so intro movies do not get mislabeled as gameplay heartbeat stalls.
- Validation found an important nuance: `yavin1` can sit in a cinematic path after active state with no gameplay heartbeat yet. The harness should not call that a freeze just because `FRAME_HEARTBEAT` pauses; it should rely on cinematic progress, fatal markers, or the broader watchdog.
- Smoke-tested the patched harness on direct `yavin1b` with `-ActiveSeconds 20`. Result: pass, active, 21 heartbeats, 20.3 active seconds, no fatal exception, no unknown D3D format error. This proves the normal gameplay heartbeat path still works after the cinematic exception.
- Reset the game directory back to `ja_sp_level.txt = yavin1` and copied the latest 01:42:12 XBE to `default.xbe` for the next human visual test.
- New issues to test: visually run `yavin1` and report whether the current `textures/common/gradient2` skip changes the white/grey Yavin panels. The current harness work does not alter rendering.
- Next action: hold further Yavin visual changes until the current diagnostic has a human result; use the retry harness for future unattended smoke passes.

### Cycle 29 - 2026-05-23 03:02
- Re-read this document and chose an autonomous task that does not require human visual confirmation: make the smoke harness cover normal UI/attract boot explicitly.
- Added `-AllowNoActive` to `scripts/smoke_cxbx_sp.ps1` and pass-through support in `scripts/smoke_cxbx_sp_retry.ps1`. This lets normal UI boot pass when UI initialization is proven and no fatal markers appear, instead of incorrectly failing because no gameplay map reached `CA_ACTIVE`.
- Audited the current Yavin sky evidence without changing rendering. The active diagnostic still skips `textures/common/gradient2` on `yavin1`, `yavin1b`, and `yavin2`; if the human visual result is unchanged, the next likely target remains `textures/skies/cloudlayer_yavin` stage behavior rather than asset loading, because prior logs show `cloudlayer2`, `alphasquare`, and `gradient2` images all load without fallback.
- Reduced `CIN_RunCinematic enter` budget from 96 to 16 so UI/attract logs remain useful while still preserving start/stop/failure breadcrumbs. This is diagnostic-only and should not alter playback.
- Rebuilt SP successfully. Fresh XBE: `code\x_exe\Release\default.xbe`, timestamp `2026-05-23 02:54:49`, size `5435392`; it was copied by the smoke harness to the game directory.
- Smoke-tested normal UI/attract boot for 3 minutes with empty `ja_sp_level.txt` using `-AllowNoActive`. Result: pass, UI initialized, active false as expected, `attract.bik` opened and stopped cleanly, no fatal exception, and no unknown D3D format error. Summary/log prefix: `scripts/output/cxbx_sp_20260523_025553.*`.
- Smoke-tested direct `yavin2` for 60 active seconds on the same XBE. Result: pass, active, 61 heartbeats, 60.9 active seconds, no fatal exception, no texture allocation failures, and no unknown D3D format error. Summary/log prefix: `scripts/output/cxbx_sp_20260523_025934.*`.
- Reset the game directory back to `ja_sp_level.txt = yavin1` for the user's next visual pass.
- New issues to test: visually run the 02:54:49 XBE on `yavin1` and report whether the `textures/common/gradient2` skip changes the white/grey Yavin panels. UI/attract and `yavin2` hidden-smoke stability are green.
- Next action: hold further visual renderer changes until that Yavin result lands; hidden-smoke work can continue with harness coverage or non-visual regressions.
