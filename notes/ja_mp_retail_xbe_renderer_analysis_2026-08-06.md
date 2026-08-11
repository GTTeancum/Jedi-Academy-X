# Retail Jedi Academy MP XBE Renderer Analysis

Date: 2026-08-06

## Scope

This is a static-analysis reference for the retail Xbox Jedi Academy
multiplayer executable. It does not make `codemp/` an active Elite Force build
dependency. The purpose is to identify shipping Xbox renderer behavior that can
be tested in the shared Elite Force renderer.

## Inputs

- Retail XBE:
  `C:\Programming\GitHub\Jedi-Academy-X\Star Wars Jedi Academy game\jamp.xbe`
- Retail XBE SHA256:
  `74003C42786C021438C1F27833839F599DCE23E22F086E971C84038C504E7FB3`
- Retail build path:
  `c:\dev\ja\codemp\x_exe\finalbuild\jamp-final.exe`
- Retail build date: 2003-10-18
- Retail XDK: 5558
- Entry point: `0x000DEEFE`
- Image size: 9,131,040 bytes
- Text section: `0x00011000`, 2,523,724 bytes
- Kernel imports: 145

The mod-tools XBE differs from the retail XBE by one byte at file offset
`0xD0655` (`0x7D` to `0xEB`), consistent with a media-check bypass. Analysis
used the unmodified retail XBE.

## Tool

- Repository: `https://github.com/sp00nznet/xboxrecomp`
- Analysis branch: `ja-mp-analysis-pr5`
- Revision: `23aaddf990fdd869c31d569c45500443364849a0`
- Integrated PR:
  `https://github.com/sp00nznet/xboxrecomp/pull/5`

PR 5 was necessary for useful output. Relative to the base branch, it corrected
3,514 function boundaries in the seeded JA MP analysis, including the core
indexed draw function. Its function-end and carry-flag regression scripts both
pass locally.

The corrected text analysis found:

- 762,840 instructions
- 11,384 functions
- 175,366 cross-references
- 10,368 strings
- 87.8 percent reachable text

The full executable pass also identified the linked XDK code sections:

- D3D: 273 functions
- D3DX: 411 functions
- XGRPH: 47 functions
- DSOUND: 391 functions
- XNET: 368 functions
- XONLINE: 458 functions
- XPP: 210 functions

## Shipping Draw Path

The corrected retail function at `0x000B3640-0x000B3DC4` is 1,924 bytes and
448 instructions. It maps to the Xbox `dllDrawElements` implementation in:

`C:\Programming\GitHub\Jedi-Academy-X\clean-mp-original-build\codemp\win32\win_qgl_dx8.cpp`

The retail XBE and source agree on the following behavior:

1. The renderer reserves one D3D push buffer for a complete indexed draw.
2. Position, optional normal, color, and up to two texture-coordinate streams
   are copied directly into that push buffer.
3. It writes the NV2A stream-format method `0x1760` and stream-pointer methods
   `0x1720`, `0x1728`, `0x172C`, `0x1744`, and `0x1748`.
4. It submits triangle-list indices in batches capped at `0x3FE` (1022)
   indices through method `0x1800`, then terminates the packet.
5. The core indexed path does not create or lock a dynamic vertex/index ring
   and does not use `DrawIndexedPrimitiveUP`.
6. Runtime conversion from triangle lists to strips is disabled in the retail
   renderer.

The corrected lift is retained at:

`build/analysis/ja-mp-xbe-recomp/retail_draw_0x000B3640_lift.txt`

The lift lacks recovered ABI metadata and therefore guesses the function
signature incorrectly. It is valid as instruction/data-flow evidence, not as
drop-in recovered C source.

## Elite Force Comparison

The shared Elite Force production renderer now follows the retail JA Xbox
submission path rather than selecting among experimental backends:

- Indexed drawing uses the retail state-update order, stream packing, device
  push pointer, packet reservation, method sequence, and `PushIndices` batches.
- Non-indexed arrays and immediate-mode UI/effects write directly into
  `BeginPush`/`EndPush` packets as retail does.
- The old UP and dynamic VB/IB ring helpers remain compiled for diagnostics but
  are unreachable from production. `r_nativeDrawPath` is read-only and cannot
  reactivate an archived path-2 setting.
- Production drawing does not call `KickPushBuffer` or `BlockUntilIdle`.
- Device startup now uses the Xbox static `IDirect3D8::CreateDevice`, default
  push-buffer sizing, explicit retail presentation surfaces, and no allocation
  of the dead startup vertex ring.
- Vsync transitions use the retail `PersistDisplay` plus `Reset` lifecycle.
- The production QGL draw entry points now are the retail packet functions
  themselves. The non-retail outer wrapper, per-draw volatile telemetry writes,
  per-draw stage-1 readiness lookup, and dirty-texture diagnostic counters are
  absent from the shipping path. Renderer initialization records `path=1` once
  so hardware logs retain architecture identity without taxing every draw.
- The always-active non-retail scene profiler is also absent. Scene generation,
  world, polygons, projection, entities, sorting, lightmap passes, and completed
  surfaces no longer perform repeated timer calls or volatile diagnostic writes.
  Canonical renderer timing and the independent engine heartbeat remain intact.

The XDK 5558 hot-path SP proof on `borg6` completed at a 90.0 guest-FPS average
with an 85.8 minimum, versus 80.9 for the preceding device candidate. The
matching Holomatch proof on `hm_borg1` completed at an 84.5 guest-FPS average
with an 83.5 minimum, versus 83.1. Both 60-second runs passed visual checks and
remained alive at the timed finish. These are correctness and stability results
under XEMU, not retail-hardware performance proof.

The second hot-path proof remained visually correct and stable. SP `borg6`
averaged 90.3 guest FPS, effectively unchanged from 90.0. Holomatch `hm_borg1`
held 90.9 guest FPS for every harness sample, up from 84.5; its captured in-game
counter showed approximately 103-104 FPS. The candidate is staged as
`Beta-20260806-retail-ja-hotpath2`.

The third hot-path pass removes the retail-absent surface/stage trace selector,
draw-context bookkeeping, fallback-image hashing, fallback-stage counters, and
their calls from indexed submission, stage iteration, and end-surface paths.
It deliberately preserves Elite Force beam/HUD handling, scripted-panel state,
and all functional draw calls. SP `borg6` passed visually at 90.1 guest FPS,
effectively flat against 90.3. The first Holomatch sample averaged 71.5 while
facing a materially heavier corridor than the prior proof; a fixed-coordinate
and fixed-yaw `hm_borg1` control then passed visually at 90.9 guest FPS (90.7
minimum), matching the previous capped result. The one-folder hardware
candidate is now `Beta-20260806-retail-ja-hotpath3`.

## Ranked Follow-Up

1. Measure staged candidate `Beta-20260806-retail-ja-hotpath3` on a retail Xbox.
   The log must show path 1, advancing heartbeats, no ring/fallback/OOM events,
   and sustained SP and Holomatch gameplay FPS.
2. Audit texture residency separately. Retail JA splits a 10 MiB static world
   pool from a 4 MiB swappable model-skin pool; the current Elite Force port
   uses one 15 MiB static pool. Adapt this only with allocator and map-reload
   evidence because the game data and model lifetime differ.
3. Verify the remaining active state, texture upload, and presentation calls
   against both retail source and XBE. Ignore diagnostics and unreachable
   fallback code when measuring behavioral parity.
4. Separately compare the retail Xbox-specialized BSP, curve, and DDS texture
   loaders. They pack map data, precompute tangent data, retain DDS mip chains,
   and avoid unnecessary filesystem probes, but must be adapted to Elite Force
   formats rather than copied blindly.

The current retail timing also assigns roughly 106-160 ms per rendered frame
to server simulation and 89-112 ms to the client, versus 36-51 ms to the D3D
backend. The renderer is a real cost, but this evidence does not support
attributing the entire 3-4 FPS result to draw submission alone.

## Whole-Module Parity Ledger - 2026-08-11

The complete renderer-tree diff is retained at
`scripts/output/retail_renderer_numstat_20260811.txt`, and the function-context
low-level D3D diff is retained at
`scripts/output/retail_win_qgl_function_diff_20260811.txt`. Raw line counts are
not a completion percentage because Elite Force and Jedi Academy use different
game formats and frontend behavior.

Ranked active divergences:

1. Special Elite Force HUD/beam paths perform direct D3D overlay resets. These
   are absent from retail JA but protect EF-specific effects and are called
   less broadly than `RB_StretchPic`; isolate each call site before changing it.
2. Retail separates a 10 MiB static texture pool from a 4 MiB swappable skin
   pool; Elite Force currently uses one 15 MiB static pool. This affects
   residency and map/model churn more than ordinary draw submission. It needs
   allocator and reload evidence, not a blind copy.
3. Retail Xbox BSP, curve, and DDS loaders are substantially specialized.
   Their useful packing and mip-residency behavior must be adapted to EF BSP
   and texture formats. They primarily affect load time, memory, and later
   cache behavior rather than the immediate per-draw CPU path.

Rejected parity candidates:

1. `RB_StretchPic` performs `RB_XboxForce2DOverlayState` for every picture,
   issuing seven direct D3D render-state writes that retail does not issue.
   Removing that per-picture reset while retaining the `RB_SetGL2D` reset was
   iteration 6. A deterministic open-room `hm_dn1` comparison used position
   `1216 -392 664`, yaw `180`, with movement and attack disabled. Iteration 4
   captured 88.1, 86.7, and 89.5 FPS (88.1 average); iteration 6 captured 54.7
   and 83.0 FPS (68.9 average). Heartbeat average also fell from 89.2 to 86.0.
   The candidate was rejected and the per-picture reset restored.
2. `GL_State` forcibly invalidates depth, blend, depth-write, depth-test, and
   alpha-test state on every call. Retail uses the cache normally. Iteration 5
   restored retail caching, remained visually correct, but was slower in both
   SP and matched Holomatch XEMU controls, so it was reverted.
   Since those forced invalidations compensate for direct D3D state writes
   outside `glState.glStateBits`, iterations 5 and 6 were also tested as one
   coupled change. In the same deterministic room view, the pair remained
   visually correct but captured 69.7, 72.0, and 72.2 FPS (71.3 average), with
   a 76.0 heartbeat average. That is below iteration 4's 88.1 captured and 89.2
   heartbeat averages. The pair was rejected and reverted together. Report:
   `scripts/output/retail-ja-hotpath56-mp-dn1-spawn8-long_hm_dn1_20260811_143736.report.txt`.
   A repacked rollback proof recovered to an 87.0 heartbeat average (82.4
   minimum, 90.9 maximum) with clean world, lightmap, weapon, and HUD captures:
   `scripts/output/retail-ja-hotpath4-restored-mp-dn1-spawn8-long_hm_dn1_20260811_144228.report.txt`.
3. `DrawMultitextured` applies the same direct HUD/beam D3D state reset both
   before texture setup and immediately before drawing. Iteration 7 removed
   only the second reset while preserving the first and all EF overlay setup.
   The deterministic `hm_dn1` room view averaged 81.1 FPS (82.2, 82.2, 75.7,
   84.1), below iteration 4's 88.1 FPS. The duplicate reset was restored.
4. A complete texture-stage binding/state cache with ownership invalidation
   was tested as iteration 8. Its refined deterministic run averaged 80.6 FPS
   and visibly hitched, below iteration 4, so all cache changes were reverted.
5. Iteration 9 adapted retail's 10 MiB static and 4 MiB player-skin pool split
   while deliberately omitting retail's scratch-disk skin swapping. In matched
   screenshot-free 90-second `hm_dn1` runs, the split candidate averaged 84.7
   FPS (80.7 minimum) and the restored 15 MiB single-pool control averaged 86.2
   FPS (76.0 minimum). The 1.7% regression was rejected. A preceding visual
   run's late dip tracked native screenshot captures; the same candidate ran
   continuously when captures were disabled.

Retained follow-up:

1. Iteration 10 removes renderer success diagnostics from shipping frame paths.
   This includes unbounded command-dispatch and frame-end logging plus bounded
   scene, world, model, shader-stage, sky, UI, and animation success traces.
   Failure logs and crash breadcrumbs remain. One no-capture run reproduced an
   intermittent long stall and averaged 61.7 FPS. The unchanged repeat settled
   after its first sample and sustained approximately 90-91 FPS, while a
   separate visual run averaged 89.7 FPS and captured a correct open-room frame
   at 94.9 in-game FPS. The bundle is retained for its steady-state result, but
   the startup/probe hitch is tracked separately rather than credited to image
   capture or hidden by the average.

Benchmark controls must be embedded by repacking the ISO whenever the XBE or
control files change. `SmokeInputStart 0` does not disable input; it leaves the
harness active indefinitely and its zero-vector fallback walks the camera
forward. The deterministic comparison instead sets input and attack windows to
`999999`, then applies `setviewpos 1216 -392 664 180` after map startup.

Already matched or removed from production: indexed push submission,
non-indexed/immediate push submission, push packet sizing/method sequence,
device creation, presentation reset, dead VB/IB ring dispatch, per-draw
telemetry wrappers, and always-active renderer profiling.
