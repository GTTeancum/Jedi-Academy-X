# Elite Force X Game To-Do

Current qualification snapshot: `HOLOMATCH_QUALIFICATION.md`.

## Remaining Items

- Active retail-renderer qualification candidate:
  `Beta-20260806-retail-ja-hotpath4`. All production geometry submission now
  follows the shipping Jedi Academy Xbox push-buffer flow. Indexed drawing
  uses the retail state-update order, stream layout, device push pointer,
  `vert_size + count/2 + 60` reservation, and canonical `PushIndices`
  batches. Non-indexed arrays and immediate-mode UI/effects now also write
  directly into retail-style `BeginPush`/`EndPush` packets instead of copying
  through the experimental dynamic ring. The VB/IB ring and UP helpers remain
  compiled for diagnostics but are unreachable from production; the archived
  `r_nativeDrawPath=2` setting can no longer select them. The shared change is
  built into both `default.xbe` and `efmp.xbe` with XDK 5558. Focused XEMU
  proof completed for both personalities:
  `scripts/output/retail-ja-fullpush-sp-proof_borg6_20260806_222827.report.txt`
  averaged 80.3 guest FPS over 60 seconds with `path=1`, 34,956 successful
  two-texture/lightmap draws, zero mismatch, and intact world/HUD captures;
  its final automated visual rejection was a false positive caused by a
  bright weapon impact, not fallback geometry.
  `scripts/output/retail-ja-fullpush-mp-proof_hm_borg1_20260806_223117.report.txt`
  passed at an 88.2 guest FPS average over 60 seconds with clean Holomatch
  world, HUD, weapon, and phaser captures.
  Device creation and presentation now also follow retail JA: Xbox static
  `IDirect3D8::CreateDevice`, default push-buffer sizing, no startup allocation
  of the dead vertex ring, explicit retail presentation surfaces, and
  `PersistDisplay`/`Reset` for vsync transitions. The focused device proof
  `scripts/output/retail-ja-device-sp-proof_borg6_20260806_224035.report.txt`
  passed at an 80.9 guest FPS average, and
  `scripts/output/retail-ja-device-mp-proof_hm_borg1_20260806_224331.report.txt`
  passed at an 83.1 guest FPS average; both have clean gameplay captures.
  The production draw entry points now match retail structure directly: the
  indexed and non-indexed retail packet functions are the QGL entry points,
  rather than sitting behind wrappers that wrote volatile diagnostics and
  performed a texture lookup on every draw. Stage-1 diagnostic counters were
  also removed from the dirty-texture update path; `path=1` is recorded once
  during renderer initialization. The focused hot-path proof
  `scripts/output/retail-ja-hotpath-sp-proof_borg6_20260806_230927.report.txt`
  passed visually and averaged 90.0 guest FPS over 60 seconds (85.8 minimum),
  versus 80.9 for the preceding device candidate. The matching Holomatch proof
  `scripts/output/retail-ja-hotpath-mp-proof_hm_borg1_20260806_231202.report.txt`
  passed visually and averaged 84.5 guest FPS (83.5 minimum), versus 83.1.
  The second retail hot-path pass removes the remaining always-active
  non-retail renderer profiler, repeated phase timer calls, lightmap-pass
  telemetry, and per-surface/batch diagnostic accounting. It does not change
  rendering, batching, shader, UV, or split-screen behavior. Its SP proof
  `scripts/output/retail-ja-hotpath2-sp-proof_borg6_20260806_233838.report.txt`
  passed visually at a 90.3 guest-FPS average (87.3 minimum), effectively flat
  against 90.0. Its Holomatch proof
  `scripts/output/retail-ja-hotpath2-mp-proof_hm_borg1_20260806_234117.report.txt`
  passed visually at a locked 90.9 guest FPS, up from 84.5; the captured
  in-game counter showed approximately 103-104 FPS. Independent frame,
  heartbeat, backend, and upload evidence remains available.
  The third retail hot-path pass removes the remaining per-surface and
  per-shader-stage trace machinery plus fallback-stage telemetry from
  `tr_shade.cpp`; rendering decisions, draw order, beam/HUD compatibility, and
  scripted-panel behavior are unchanged. SP `borg6` passed visually and
  averaged 90.1 guest FPS, flat against 90.3. A fixed-view Holomatch
  `hm_borg1` control passed visually and averaged 90.9 guest FPS (90.7
  minimum), matching the previous capped result. An earlier 71.5-FPS sample
  faced a substantially heavier corridor and is retained as workload evidence,
  not treated as a regression or an A/B comparison.
  The fourth retail hot-path pass removes eight per-frame volatile phase-marker
  writes around the shared begin/end-frame path. SP `borg6` passed visually at
  a 90.6 guest-FPS average; its three captured in-game samples were 96.1,
  100.1, and 97.1 FPS. A corrected fixed-view Holomatch A/B measured the rebuilt
  marker-present hotpath3 binary at approximately 111.25 FPS and hotpath4 at
  143.3 FPS, a provisional 28.8% XEMU improvement. Reports are
  `scripts/output/retail-ja-hotpath4-sp-proof_borg6_20260807_003833.report.txt`,
  `scripts/output/retail-ja-hotpath3-rebuilt-mp-locked_hm_borg1_20260807_010432.report.txt`,
  and
  `scripts/output/retail-ja-hotpath4-mp-locked_hm_borg1_20260807_011355.report.txt`.
  Retail hardware validation is still required.
  Iteration 5 restored retail `GL_State` caching by removing this project's
  forced Xbox state invalidation. It remained visually correct but regressed
  SP `borg6` captured samples from a 97.8 FPS average to 76.7 FPS. The matched
  Holomatch wall control fell from 143.3 to 108.4 FPS, approximately 24.3%.
  The wall view is not representative gameplay workload, but the identical A/B
  setup and independent SP regression are sufficient to reject this candidate.
  Iteration 5 was reverted; future FPS qualification must use a locked open
  scene containing world geometry, lightmaps, bots, effects, weapon, and HUD.
  This is correctness evidence, not retail-hardware performance proof.
  Iteration 6 removed the seven direct D3D state writes performed by every
  `RB_StretchPic`, while retaining the transition-time reset in `RB_SetGL2D`.
  It was tested in the same deterministic `hm_dn1` room view at
  `1216 -392 664`, yaw `180`, with movement and attack disabled. The accepted
  iteration-4 build produced captured in-game samples of 88.1, 86.7, and
  89.5 FPS (88.1 average); iteration 6 produced 54.7 and 83.0 FPS (68.9
  average), with the independent heartbeat also falling from 89.2 to 86.0.
  Iteration 6 was rejected and reverted. Repacked ISOs are mandatory whenever
  changing the benchmark controls or XBE; otherwise XEMU runs stale controls.
  Because the iteration-5 cache change and iteration-6 per-picture reset are
  mechanically coupled, they were also tested together rather than inferred
  from the isolated results. The combined candidate remained visually correct
  in the same deterministic room view, but captured 69.7, 72.0, and 72.2 FPS
  (71.3 average) and averaged 76.0 FPS by the independent heartbeat. Both are
  below iteration 4's 88.1 captured and 89.2 heartbeat averages. The combined
  candidate was rejected, both changes were reverted together, and both
  release XBEs were rebuilt from iteration 4. Evidence:
  `scripts/output/retail-ja-hotpath56-mp-dn1-spawn8-long_hm_dn1_20260811_143736.report.txt`.
  A fresh repacked rollback proof then recovered to an 87.0 heartbeat average
  (82.4 minimum, 90.9 maximum) with clean captures, confirming the accepted
  state was restored. Report:
  `scripts/output/retail-ja-hotpath4-restored-mp-dn1-spawn8-long_hm_dn1_20260811_144228.report.txt`.
  Iteration 7 removed only the second of two identical direct D3D HUD/beam
  state resets in `DrawMultitextured`; the first reset and all EF overlay
  preparation remained. The same deterministic room view produced 82.2, 82.2,
  75.7, and 84.1 FPS (81.1 average), below iteration 4's 88.1 average.
  Iteration 7 was rejected and reverted. Both release XBEs remain built from
  the accepted iteration-4 renderer state.
  Iteration 8 added complete texture-stage binding/state caches, including
  invalidation for direct D3D writers. It remained visually correct but the
  refined run averaged 80.6 FPS and showed user-visible hitches, below the
  accepted iteration-4 controls, so it was rejected and reverted.
  Iteration 9 adapted retail JA's separate 10 MiB world and 4 MiB player-skin
  texture pools without its scratch-disk swapping. A screenshot-free 90-second
  `hm_dn1` run averaged 84.7 FPS (80.7 minimum); the immediately rebuilt
  single-pool control averaged 86.2 FPS (76.0 minimum) under the identical
  fixed open-room workload. The split was 1.7% slower and was rejected. Native
  screenshot capture itself caused the apparent late-run stutters in the first
  iteration-9 proof; the no-capture run stayed continuous. Evidence:
  `scripts/output/retail-ja-hotpath9-split-texture-pools-noscreens_hm_dn1_20260811_164158.report.txt`
  and
  `scripts/output/retail-ja-hotpath4-ab-noscreens_hm_dn1_20260811_164824.report.txt`.
  Iteration 10 removes shipping success-path renderer diagnostics that flushed
  synchronously from command dispatch, frame end, scene traversal, surface
  submission, and shader-stage execution. Failure breadcrumbs, overflow
  reports, crash sentinels, and one-time split-screen diagnostics remain. The
  first no-capture run exposed a real intermittent stall and averaged 61.7 FPS,
  but an unchanged repeat recovered after its initial sample and then held
  approximately 90-91 FPS for the remaining 73 seconds. A separate visual
  proof averaged 89.7 FPS (85.0 minimum) and captured a clean open-room frame
  with the in-game counter at 94.9 FPS. Iteration 10 is retained as a
  steady-state win; startup/probe hitching remains open and is not being
  misclassified as screenshot or renderer throughput. Evidence:
  `scripts/output/retail-ja-hotpath10-shipping-logs-noscreens_hm_dn1_20260811_171118.report.txt`,
  `scripts/output/retail-ja-hotpath10-shipping-logs-noscreens-repeat2_hm_dn1_20260811_171709.report.txt`,
  and
  `scripts/output/retail-ja-hotpath10-shipping-logs-visual_hm_dn1_20260811_172000.report.txt`.
  Iteration 11 removes the remaining per-texture upload/load success records
  and periodic shader-find progress records. DDS selection, mip chains,
  uploads, shader parsing, fallbacks, and failure diagnostics are unchanged.
  Its screenshot-free run had one 44.5-FPS attachment sample, then locked at
  90.9 FPS for all remaining samples (86.3 overall). The visual run averaged
  90.0 FPS (88.7 minimum) and captured a correct open-room frame at 93.9
  in-game FPS. The candidate is retained. A separate roughly 270-write burst
  during raw map/shader setup remains visible and is the next bounded hitch
  target. Evidence:
  `scripts/output/retail-ja-hotpath11-asset-traces-noscreens_hm_dn1_20260811_173011.report.txt`
  and
  `scripts/output/retail-ja-hotpath11-asset-traces-visual_hm_dn1_20260811_173303.report.txt`.
  Iteration 12 removes the remaining raw BSP shader-resolve, shader-stage,
  per-surface, flare, face-progress, and shader-file success records. It keeps
  phase boundaries, memory totals, loading-animation updates, shader fallback
  failures, and sky telemetry. The screenshot-free fixed-room run held
  90.7-90.9 FPS for all ten samples (90.9 average), eliminating iteration
  11's slow first sample. The visual proof averaged 90.7 FPS and captured the
  correct world, lightmaps, weapon, and HUD at 94.0 in-game FPS. A later
  approximately 280-write runtime burst remains, but neither run showed a
  corresponding frame-rate dip; it is tracked separately rather than being
  attributed to BSP setup. Evidence:
  `scripts/output/retail-ja-hotpath12-bsp-precache-traces-noscreens_hm_dn1_20260811_174220.report.txt`
  and
  `scripts/output/retail-ja-hotpath12-bsp-precache-traces-visual_hm_dn1_20260811_174502.report.txt`.
  The one-folder PK3-only hardware stage is
  `build/hardware/StarTrekEliteForceX-Beta-20260801` (0.569 GiB). The next
  SP and Holomatch hardware logs must show `path=1`, advancing heartbeats,
  no ring calls/fallbacks/OOM, and sustained gameplay FPS before this item
  can pass.
- 2026-08-04 native D3D8 checkpoint: the direct Xbox D3D8 backend in
  `code/win32/win_qgl_dx8.cpp`, built end-to-end with the clean XDK 5558
  toolchain and a 15 MiB texture pool, reached `borg1` gameplay from the
  current XEMU ISO. The embedded XBE is byte-identical to
  `build/release/default.xbe` except for extract-xiso's one-byte media patch;
  the unpatched XBE SHA256 is
  `1102582A5884BE63FD982750783D130B77F98B8EA8DA4E4E93836F9E16EA2F78`.
  This is the first direct-D3D8 gameplay checkpoint. The interrupted harness
  did not retain screenshots or FPS samples, so visual correctness, sustained
  performance, allocator headroom, and SP/co-op/Holomatch regressions remain
  unqualified.
- Retail performance is the active blocker. Photographed hardware baselines
  from `Beta-20260802-hardware-fps` are approximately 0.5-1.1 FPS during the
  campaign title crawl, 2.3 FPS during loading, 3.0-3.7 FPS during scripted
  map scenes, and 2.0 FPS in first-person gameplay. The prior XEMU results are
  not a valid proxy for retail performance.
- The rejected path-2 retail gameplay measurement on 2026-08-06 was 3-4 FPS
  in both SP and Holomatch. The engine heartbeat reports approximately 5 FPS,
  with four 50 ms server simulation ticks consuming 106-160 ms per rendered
  frame, the client consuming 89-112 ms, and the D3D backend consuming
  36-51 ms. The native ring path is active with no logged fallback or ring
  stall. A previously observed momentary 20+ FPS reading is not a qualified
  baseline and must not be used as performance evidence. Path 2 is no longer
  reachable in the active production build.
- `Beta-20260806-scheduler1` is staged in the existing one-folder hardware
  directory. It changes only the shared local-server scheduler: one simulation
  tick is allowed per rendered frame and overdue backlog is discarded while
  preserving the sub-tick remainder. The next retail log must show
  `svtick=1`; retain the change only if both SP and Holomatch improve without
  gameplay instability.
- The first `Beta-20260806-scheduler1` retail run sustained approximately
  5.7-8.0 FPS with `svtick=1`, then stopped on a diagnosed filesystem-zone
  allocation failure rather than scheduler starvation. `music/borg1.mp3` is
  1,743,206 bytes; the MP3 metadata path requested 1,743,207 bytes from a
  fragmented 23 MiB game zone whose 3,091,305 free bytes had a largest
  contiguous block of only 1,679,059 bytes. The exact log signature was
  `EFALLOC_FATAL: request=1743207 tag=6`.
- `Beta-20260806-scheduler-music1` is now staged in the same hardware folder.
  It preserves the exact MP3 header and frame-walk decoder behavior but reads
  the temporary compressed file through the Xbox process heap instead of
  `TAG_FILESYS` zone memory. Accept it only when the retail log contains
  `metadata=process_heap`, continues heartbeats beyond the prior three-minute
  failure point, and contains neither `EFALLOC_FATAL` nor MP3 allocation/read
  warnings.
- `Beta-20260806-nosound1` was the prior diagnostic and has been superseded by
  the retail push-path candidate. Both Xbox personalities still stop at the
  shared sound initialization boundary before creating the audio device or
  allocating sound banks, channels, lip-sync tables, ambient state, streams,
  or MP3 metadata. The log must contain `Xbox audio disabled by build policy`.
  Use this build to determine sustained FPS and stability with the complete
  sound subsystem removed from the runtime workload; audio restoration and
  optimization remain required before release.
- `Beta-20260802-retail-file-probe-fix` was rejected as the primary
  performance fix after retail gameplay remained at approximately 2 FPS. Its
  removal of recurring diagnostic filesystem probes remains as runtime
  cleanup.
- The earlier `Beta-20260802-retail-inline-push` non-indexed experiment is no
  longer the active candidate. The current hardware stage uses the retail JA
  indexed push path described above; confirm rendering first, then measure
  campaign and Holomatch gameplay separately.
- The restored 682-asset Xbox UI package works on retail. The subsequent
  tutorial freeze was not a menu or package failure: the log completed the raw
  BSP read, render surfaces, collision, entities, and visibility, then stopped
  inside renderer world finalization. The Xbox raw-BSP fog loader was
  dereferencing a missing shader `fogParms` block; it now uses the canonical SP
  fallback values for malformed/missing fog definitions.
- The corrected shared-SP `default.xbe` reached active tutorial gameplay in
  XEMU for a full 60-second proof with advancing server/client heartbeats, no
  crash or stall, and 90.9 reported FPS for every active sample. Proof is in
  `scripts/output/sp-tutorial-fogfix-proof_tutorial_20260802_143557.report.txt`
  and
  `scripts/output/sp-tutorial-fogfix-proof_tutorial_20260802_143557_contact.png`.
  The hardware stage contains fresh SP and Holomatch XBEs with their required
  one-byte media enable patches. `xbox0.pk3` and `xbox1.pk3` remain
  byte-identical to the UI-confirmed package. Both XBEs temporarily draw the
  same one-second presented-frame FPS counter in the upper-left corner.
  The first campaign map now reaches gameplay on retail. Confirm the tutorial
  still advances from its loading screen, then record FPS during loading, the
  campaign crawl, scripted map scenes, SP gameplay, and Holomatch gameplay.
- A separate 75-second `borg1` run with the same corrected XBE reached the
  active server and continuously advanced the canonical campaign introduction.
  Five native captures cover its changing backdrops; active FPS averaged 56.9,
  bottomed at 45.9, and had no sub-30 sample. Proof is in
  `scripts/output/sp-borg1-fogfix-proof_borg1_20260802_145143.report.txt` and
  `scripts/output/sp-borg1-fogfix-proof_borg1_20260802_145143_contact.png`.
- Earlier XEMU proof of the campaign crawl and restored UI package is in
  `scripts/output/sp-borg1-uipack-loadfix_borg1_20260802_104227.report.txt` and
  `scripts/output/sp-borg1-uipack-loadfix_borg1_20260802_104227_contact.png`.
- Temporary FPS-overlay visual proof is in
  `scripts/output/sp-hardware-fps-overlay-proof_tutorial_20260802_180338.report.txt`
  and
  `scripts/output/sp-hardware-fps-overlay-proof_tutorial_20260802_180338_contact.png`.
- Post-beta: continue shared renderer, simulation, and presentation
  optimization toward stable 30 FPS in XEMU/LLE without reducing fidelity.
- Post-beta: implement four-player split-screen after the current two-player
  SP/co-op path remains stable.
- Post-beta: recreate the PS2 Holomatch setup, advanced-options, and player
  setup screens as functional pixel-perfect menus using the stored references.
- Before a public binary release, commit the qualified source snapshot and
  rebuild the package so `release_manifest.json` records
  `sourceTreeDirty: false`.

## Beta Candidate

Date: 2026-08-01

- Package: `build/beta/StarTrekEliteForceX-Beta-20260801`.
- XISO SHA256:
  `F434561D66B4687F2CF06DA36D5707DBC8BB7F6B1F3A25ED713A5B236BD54C83`.
- Entry point: `default.xbe`.
- SP/co-op personality: `default.xbe`.
- Holomatch personality: `efmp.xbe`.
- Shared runtime: `BaseEF`.
- `codemp/` build/runtime dependency: none.
- Diagnostic or smoke markers in the packaged XISO: zero.

The marker-free XISO passed a separate final frontend boot:

- Report:
  `scripts/output/stefx-beta-cleaniso-final_normal_20260801_114812.report.txt`.
- Visual proof:
  `scripts/output/stefx-beta-cleaniso-final_normal_20260801_114812_contact.png`.

## Confirmed Good

- Full release builds pass for both `default.xbe` and `efmp.xbe`.
- Campaign loading, cinematics, localized quoted level title, gameplay, and
  main-menu return pass.
- The six-item shared main menu contains New Game, Load Game, Cooperative,
  Holomatch, Configure, and Voyager Crew.
- Two-player co-op retains the full-screen campaign introduction before
  switching to two live viewports.
- P2 has an independent viewport, camera, origin, input, HUD, and weapon view.
- P1/P2 presentation and Borg materials are intact.
- Holomatch FFA and CTF pass with bots, controls, scoreboard, weapons, ammo,
  pickups, phaser beam, damage, HUD, loading screens, audio, stairs, and
  teleporters.
- `default.xbe` and `efmp.xbe` hand off in both directions.
- `xbox1.pk3` contains all 33 optimized MP maps and 1277 DDS textures:
  1149 DXT1 and 128 BGRA32.
- `xbox1.pk3` contains zero original JPG/TGA/PNG entries and zero legacy UI
  scripts.
- The runtime stage contains zero loose MP map overrides, zero loose original
  texture fallbacks for `xbox1.pk3`, and zero loose UI scripts.
- The shared soundbank contains 7971 records: 7947 Xbox ADPCM and 24 preserved
  PCM.
- User signoff is recorded for Holomatch HUD, loading screens, controls,
  phaser, footsteps, bot visibility/damage, pickups, movement, stairs, and
  teleporters.

## Final Soak

The final uninterrupted XEMU/LLE session passed:

1. SP loading and gameplay.
2. Return to the main menu.
3. Canonical campaign/co-op introduction.
4. Live two-player split-screen with independent P2 movement.
5. Return to the main menu.
6. Cross-XBE launch into Holomatch.
7. More than four minutes of stable Holomatch gameplay.

Proof:

- Report:
  `scripts/output/stefx-beta-final-minisoak2_borg2_20260801_101352.report.txt`.
- Visual proof:
  `scripts/output/stefx-beta-final-minisoak2_borg2_20260801_101352_contact.png`.
- XBE roundtrip:
  `scripts/output/stefx-beta-xbe-roundtrip6_normal_20260801_095353.report.txt`.

## Architecture Rules

- Common renderer, audio, input, collision, filesystem, UI framework, and
  platform code live under `code/` and are shared.
- Game-library personalities remain separate:
  - `default.xbe` links the original SP/co-op game and cgame from
    `SP-Mod-Source-Code-master`.
  - `efmp.xbe` links the official Holomatch game, cgame, and bot code from
    `code/holomatch/official`.
- Build Holomatch with `scripts/build_xbox.ps1 -Target spmp`.
- `codemp/` is historical/deprecated and must not enter active build, link,
  include, or runtime paths.
- Campaign/co-op loading presentation must remain independent from Holomatch
  loading presentation.
- Controls remain identical to SP. In Holomatch, the code-level objectives
  action maps to official `+info` / `-info` scoreboard behavior.
- Runtime textures are DDS-only. Do not add original-image fallbacks.
- Use XEMU/LLE for current qualification and XEMU-native screenshots for visual
  proof.
- Run `scripts/cleanup_generated.ps1` before and after emulator sessions.
- Report FPS on every emulator run, but treat raw FPS tuning as post-beta unless
  a functional stall or crash occurs.

## Performance Handoff

- Accepted source checkpoint:
  `528ca8d` (`Document Xbox D3D pushbuffer bottleneck`).
- Accepted state-cache proof:
  `scripts/output/stefx-state-cache-authoritative_borg1_20260730_074616.report.txt`.
- Retail JA MP XBE analysis now provides new direct-push evidence. Its shipping
  Xbox `dllDrawElements` at `0x000B3640` writes tessellator streams and indices
  into one D3D push buffer, batches at 1022 indices, and does not use a dynamic
  VB/IB ring for the core indexed path. The next isolated renderer A/B is the
  existing `r_nativeDrawPath 1` against current default path 2; do not combine
  it with another optimization or retain it without measured visual and FPS
  proof.
- Rejected direct-push or hot-counter-removal experiments must not be repeated
  beyond that controlled A/B without additional evidence.
- Retail XBE analysis and reproducibility details:
  `notes/ja_mp_retail_xbe_renderer_analysis_2026-08-06.md`.
- Continue one isolated optimization at a time and retain only candidates that
  improve measured gameplay without fidelity regressions.
- Detailed ranked non-JA candidates:
  `notes/non_ja_performance_candidates_2026-07-31.md`.
- XEMU/LLE performance is not comparable to the historical 50-65+ FPS
  CXBX-R/HLE results.

## Recovery North Star

- Commit `cfc5918b` (`Stabilize split-screen EF overlays and capture harness`,
  2026-07-14) remains the SP/co-op source-parity north star.
- Commit `90d64c89` (`Add SP-hosted Holomatch vertical slice`) was never a
  known-good SP baseline; its title must not be treated as proof.
- The current beta restores the intended architecture: shared common systems,
  separate game personalities, and both XBEs on one XISO.
