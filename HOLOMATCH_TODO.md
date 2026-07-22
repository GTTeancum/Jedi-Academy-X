# Holomatch Vertical Slice To-Do

Current proof snapshot: see `HOLOMATCH_QUALIFICATION.md`.

## Remaining Items

None for the current drivable Holomatch vertical slice. User signoff is complete as of 2026-07-22.

## Confirmed Good

- Loading screens: user signed off.
   - MP loading screen now uses the map shot as the full backdrop, with right-justified localized map title and mode metadata over it.
   - Requirements now locked:
     - Map shot is the backdrop, with no picture-in-picture inset.
     - Full SP LCARS loading wheel cluster is used at the SP left-side position.
     - Map title comes from localized arena metadata (`longname_<ui_language>`, falling back to `longname`), not the raw map filename.
     - Bot count is not shown.
     - Map title and mode are right-justified.
     - Wheel quarter highlights use measured source-art geometry, and the SP numeric wheel labels have a bitmap-font fallback when the original tiny font handle is unavailable.
   - Runtime loading-screen lookups are DDS-only; missing map previews are logged instead of falling back to old texture formats.
   - `xbox1.pk3` is DDS-only for Holomatch textures: current strict stage check reports 1253 DDS entries, zero original image entries, and only BGRA32/DXT1 formats.
   - Initial and subsequent map-load proof captures are clean:
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-hm-loadscreen-dds-20260722-0128\proofs\20260722-012633-loadscreen-first.png`
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-hm-loadscreen-dds-20260722-0128\proofs\20260722-012818-loadscreen-second-ctf-voy1-immediate.png`
   - Fresh remaining-map loading-screen captures from the 4-map qualification run:
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-093150\proofs\20260722-093217-hm_for2-loading.png`
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-093150\proofs\20260722-093410-hm_raven-loading.png`
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-093150\proofs\20260722-093602-hm_temple-loading.png`
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-093150\proofs\20260722-093749-hm_voy3-loading.png`
   - Pre-inset-removal burst proof captured 15 loading-screen PNGs in one run, three requested per map transition: `build/proofs/holomatch-map-sweep-20260722-095029`.
     - Maps: `hm_borg1`, `hm_for2`, `hm_raven`, `hm_temple`, `hm_voy3`.
     - Runtime proof: 5/5 maps passed after each loading sequence with target BSP loaded, bots active, no fatal markers, no memory issue, and zero low-FPS samples.
     - Representative inspected loading frames:
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-095029\proofs\20260722-095041-hm_borg1-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-095029\proofs\20260722-095148-hm_for2-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-095029\proofs\20260722-095239-hm_raven-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-095029\proofs\20260722-095337-hm_temple-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-095029\proofs\20260722-095433-hm_voy3-loading-02.png`
     - Note: `hm_voy3-loading-01` caught a very early low-color transition frame; `hm_voy3-loading-02` and `hm_voy3-loading-03` captured the intended loading art.
   - Current source removes the duplicate smaller mapshot inset; next visual pass should re-capture at least five loading sequences with the mapshot used only as the backdrop.
   - Fresh post-inset-removal / SP-wheel proof captured 15 loading-screen PNGs in one run, three requested per map transition: `build/proofs/holomatch-map-sweep-20260722-113915`.
     - Maps: `hm_borg1`, `hm_for2`, `hm_raven`, `hm_temple`, `hm_voy3`.
     - Runtime proof: 5/5 maps passed after each loading sequence with target BSP loaded, bots active, no fatal markers, no memory issue, and zero low-FPS samples.
     - Representative inspected loading frames:
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-113915\proofs\20260722-113928-hm_borg1-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-113915\proofs\20260722-114032-hm_for2-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-113915\proofs\20260722-114124-hm_raven-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-113915\proofs\20260722-114223-hm_temple-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-113915\proofs\20260722-114333-hm_voy3-loading-01.png`
     - Visual check: no picture-in-picture inset; the mapshot is the backdrop.
     - Superseded by later clarified requirements for full SP wheel at the left-side SP position and localized/right-justified metadata; needs fresh proof.
   - Fresh localized/right-justified proof captured 15 loading-screen PNGs in one run: `build/proofs/holomatch-map-sweep-20260722-122228`.
     - Maps: `hm_borg1`, `hm_for2`, `hm_raven`, `hm_temple`, `hm_voy3`.
     - Runtime proof: 5/5 maps passed with target BSP loaded, bots active, no fatal markers, no memory issue, and no frame-rate issue.
   - Fresh wheel-geometry proof captured loading-screen PNGs in one run: `build/proofs/holomatch-map-sweep-20260722-124226`.
     - Representative inspected frame: `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-124226\proofs\20260722-124248-hm_borg1-loading-01.png`.
     - Visual check: localized map title, no bot count, right-justified map/mode, full SP wheel left-side position, computed quarter alignment, and numeric labels visible.
     - Runtime note: 5/5 maps loaded target BSPs with bots active and no fatal/OOM markers; the smoke harness marked 3 maps failed due to active-match counter thresholds during capture-heavy proof, so use the prior 122228 run for stability signoff and this run for wheel visual proof.
   - Read-only black-screen assessment:
     - `CL_MapLoading` draws one transitional connect frame, then `CL_FlushMemory` shuts down cgame, UI, and the renderer.
     - `UpdateLoadingAnimation` is disabled for the EF renderer-owned load screen path, so the server does not present the real loading screen during `CM_LoadMap` / `RE_LoadWorldMap`.
     - The MP loading screen starts drawing when normal `SCR_UpdateScreen` resumes after the server spawn returns, causing the observed black gap between maps.
   - Current build changes `UpdateLoadingAnimation` from a disabled stub into a guarded renderer-backed load-screen pulse.
     - The pulse draws the shared SP/MP loading screen and submits a frame at existing server load milestones.
     - It skips safely until `CL_StartHunkUsers` has restored the renderer after `CL_FlushMemory`.
     - Build passed and staged as `efmp.xbe`.
   - Fresh post-pulse / latched-gametype proof captured 15 loading-screen PNGs in one continuous run: `build/proofs/holomatch-map-sweep-20260722-133113`.
     - Maps: `hm_borg1`, `hm_for2`, `hm_raven`.
     - Runtime proof: 3/3 maps passed with target BSP loaded, bots active, no fatal markers, no memory issue, and no frame-rate issue.
     - Representative inspected loading frames:
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-133113\proofs\20260722-133129-hm_borg1-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-133113\proofs\20260722-133220-hm_for2-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-133113\proofs\20260722-133315-hm_raven-loading-01.png`
     - Log proof: first load pulses now use target map metadata and target latched mode, e.g. `hm_borg1` -> `Assimilation` / Free For All instead of inheriting stale Capture The Flag.
     - Visual check: backdrop-only mapshot, no bot count, right-justified localized map/mode, full SP wheel cluster at the left-side SP position, and visible wheel numerals.
   - Final five-map loading-screen proof captured ten loading-screen PNGs in one continuous run: `build/proofs/holomatch-map-sweep-20260722-140018`.
     - Maps: `hm_borg1`, `hm_for2`, `hm_raven`, `hm_dn1`, `ctf_dn1`.
     - Runtime proof: all five maps loaded with target BSPs, bots active, no fatal markers, no memory issue, and no capture issue.
     - Frame-rate proof: `hm_borg1` 63.4 FPS average / 63.4 minimum; `hm_for2` 61.7 / 57.4; `hm_raven` 64.2 / 62.0; `hm_dn1` 63.5 / 60.5; `ctf_dn1` 62.6 / 56.0.
     - Representative loading captures:
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-140018\proofs\20260722-140024-hm_borg1-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-140018\proofs\20260722-140055-hm_for2-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-140018\proofs\20260722-140126-hm_raven-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-140018\proofs\20260722-140154-hm_dn1-loading-01.png`
       - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-140018\proofs\20260722-140212-ctf_dn1-loading-01.png`
     - Harness note: the process returned nonzero only because the short loading-screen hold did not satisfy older combat counter thresholds; the loading/runtime checks passed.

- Broader map stability / all-map sweep completion.
   - Raw per-surface EF BSP render/collision loading is now in the SP-hosted MP path, avoiding the old full converted-vertex temp buffer that failed on `ctf_breach`.
   - Continuous 18-map sweep passed 18/18 in one 1808-second CXBX-CodexCapture session: `build/proofs/holomatch-map-sweep-20260722-055208`.
     - Includes warmed `hm_borg3`, `ctf_kln2`, and `ctf_breach`.
     - `ctf_breach` passed at 64.2 FPS average / 61.0 minimum, memory 6636 -> 6592 KB, bots active, no fatal/OOM markers.
   - Follow-up remainder sweep passed 11/15 maps in one 1442-second session: `build/proofs/holomatch-map-sweep-20260722-082952`.
     - Passed: `ctf_for1`, `ctf_neptune`, `ctf_oldwest`, `ctf_reservoir`, `ctf_singularity`, `ctf_dn1`, `ctf_spyglass2`, `ctf_stasis`, `hm_altar`, `hm_blastradius`, `hm_borgattack`.
   - Final remaining-map sweep passed 4/4 maps in one 453-second session: `build/proofs/holomatch-map-sweep-20260722-093150`.
     - Passed: `hm_for2`, `hm_raven`, `hm_temple`, `hm_voy3`.
     - All four maps reported `engineTransitionStarted=true`, `targetBspLoaded=true`, bots active, no fatal markers, no memory issue, no capture issue, and zero low-FPS samples.
     - `hm_for2` re-test passed cleanly at 66.8 FPS average / 59.5 minimum, memory 10548 -> 10356 KB, bots active, target BSP loaded.
   - Current queued test build:
     - `build/release/efmp.xbe` and `C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\efmp.xbe`
     - `efmp.xbe` SHA256: `3A5C0408F1B1D7B58234082BC2FEDBAF78BD897617D4B9F9D39D7734B4EFE96B`
     - `BaseEF\xbox1.pk3` SHA256: `2E6E87F9C77E551195314BFA609DC6B180167AE3BE6B9778DD7B6451FF128DC6`
     - Direct boot default is restored to `hm_borg1` after the loading-screen proof work.
     - Runtime map-switch commands now execute as one normal command-buffer batch instead of line-by-line immediate execution.
       - Staged `efmp.xbe` contains `STEFX_HM_SWEEP: command batch execute begin`, `STEFX_HM_SWEEP: command batch execute done`, and rotating runtime command slot paths.
     - Smoke harness now requires the target BSP-load breadcrumb before treating a runtime map as playable.
     - Loading-screen proof capture waits for engine map-transition start (`SV_Map_` / `SV_SpawnServer` / raw BSP probe), so it will not capture the previous map if the command file is consumed but the engine map command does not run.
     - Per-map smoke results now record `engineTransitionStarted` and `targetBspLoaded` so partial transitions are visible in CSV/JSON without manual log parsing.
     - Staged code-only verifier passed against `C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\efmp.xbe`: `directBoot=hm_borg1`, `codempDependency=false`, 1021 `code/` sources checked, 33 optimized MP maps present, 33 AAS checksum patches present, 0 staged original images, 0 staged UI scripts.
     - Fresh direct-boot proof after restoring the default map passed 1/1 in `build/proofs/holomatch-map-sweep-20260722-134807`.
       - Boot log queues `map hm_borg1` before first frame and loads `maps/hm_borg1.bsp`.
       - Runtime proof: 82 heartbeats, 65.0 FPS average / 60.0 minimum, memory 12008 -> 11796 KB, bots active, no fatal marker, no frame-rate issue, no memory issue.
       - Gameplay counters: 34 ammo pickups, 190 weapon-fire events, 46 damage events, 28 bot-to-player damage events, 18 phaser client-target events.
       - Visual proof: `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-134807\proofs\20260722-134920-hm_borg1.png`.

- Controls: user signed off.
  - Weapon cycling must remain on the same path as single player.
  - In MP, the single-player objectives/datapad command is remapped in code to official Holomatch `+info` / `-info`, so whatever button the user maps to objectives brings up the scoreboard.
- Phaser beam: user signed off after the shared snapshot/renderer build.
- Sound: user signed off.
  - Official Holomatch sound channels are translated into SP sound channels before playback.
  - Repeated/echoing footsteps are resolved without muting footsteps.
- Bot visibility on `hm_borg1`: user confirmed good.
- Ammo and weapon pickup behavior on `hm_borg1`: user confirmed good.
- Player-to-bot damage and kill/combat feel: user signed off.
- Movement/collision on `hm_dn1`: user confirmed pass.
- Movement/collision teleporter/stair follow-up: user signed off.
- HUD/UI: user signed off after the SP-hosted HUD pass.

## Current Ground Rules

- Build Holomatch from `code/` only. `codemp/` must remain unnecessary for build, link, include, and runtime.
  - `spmp` builds now fail immediately if any resolved compile source or link object comes from `codemp\`.
- Keep `default.xbe` as SP/co-op. Development Holomatch output is `efmp.xbe`.
  - SP/co-op preservation proof: repo and staged `default.xbe` both remain timestamped `7/17/2026 11:09:56 PM`, length `4919296`, SHA256 `0116A113C44154B2CDAACB98AC6881C0E52DA2765AE390FB0479F471268E4C88`.
- Tests should boot straight to a map until menus are intentionally revisited.
- Controls must match single-player 1:1. Do not add Holomatch-only convenience bindings.
- Do not clobber split-screen paths; split-screen is future Holomatch work.
- Use CXBX-R, not XEMU, for this phase.
- For visual proof, use `C:\Games\Emulators\CXBX-CodexCapture\Start-CodexCaptureSession.ps1` and `Request-CodexScreenshot.ps1`.
  - Do not use desktop/window capture as proof.
  - Inspect the timestamped PNG from `captures\<session>\proofs\...` before claiming visual proof.

## Future Work

- Four-player split-screen Holomatch.
  - Do this after the current vertical-slice list is finished.
  - Preserve existing SP/co-op split-screen code paths while bringing Holomatch up to the same control and viewport standards.
