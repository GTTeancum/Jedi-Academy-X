# Elite Force X Game To-Do

Current qualification snapshot: `HOLOMATCH_QUALIFICATION.md`.

## Remaining Items

- Retail performance needs one confirmation run with
  `Beta-20260802-renderfix2`, staged in the existing one-folder hardware
  directory at `build/hardware/StarTrekEliteForceX-Beta-20260801`. Phase
  profiling found the shared front-end regression: dormant junk-sky, Borg,
  and scavenger diagnostics scanned every BSP leaf and performed many shader
  name comparisons for every submitted world surface even when verbose
  runtime logging was disabled. Commit `78d0deb` disables those probes in
  normal Release builds without changing visibility or rendering decisions.
  On the identical XEMU `borg3` test, average SP performance rose from 22.4 to
  47.1 FPS; `R_MarkLeaves` fell from 7-11 ms to 0 ms and world submission from
  6-10 ms to 1-2 ms. The rebuilt shared renderer also passed `hm_borg1` in
  `efmp.xbe` at 66.7 FPS average, 64.4 FPS minimum, with no sub-30 samples.
  A separate `borg1` crawl/map run advanced normally after a screenshot-free
  repeat and settled around 40-54 FPS; its transient model streaming occurs
  in `CG_AddPacketEntities`, not the shared renderer. Proof reports are
  `scripts/output/stefx-render-diag-gate-sp_borg3_20260802_022207.report.txt`,
  `scripts/output/stefx-render-diag-gate-mp_hm_borg1_20260802_023636.report.txt`,
  and `scripts/output/stefx-borg1-cgphase_borg1_20260802_025540.report.txt`.
  Hardware must confirm that this removes the previously observed roughly
  1 FPS behavior in both personalities.
- Hardware beta boot retest is required after the `hwfix10` client-model
  registration boundary is repaired. Retail
  testing first exposed controller/UI startup-order faults; the repaired path
  now reaches deferred `borg1` startup, completes the client map-loading
  handoff, completes cold-server client and snapshot allocation, and creates
  every initial configstring. Cold boot skips the inherited previous-level
  destructor, cvar compaction, and diagnostic `memmap.txt` disk walk; genuine
  later map transitions retain their cleanup. The client subsystem restart,
  renderer initialization, core shader registration, vertical-slice precache,
  and sound registration now all complete on retail hardware. The current
  boundary was narrowed to the SP raw render-surface conversion inside
  `CM_LoadMap("maps/borg1.bsp")`; PK3 access, the complete 7.3 MB BSP read,
  BSP validation, lightmaps, and shaders all passed. The SP bulk converter
  expanded all 70,154 draw vertices into a 4.77 MB temporary allocation.
  `hwfix8` moved SP onto the same bounded-memory raw renderer and collision
  loader as Holomatch, using per-surface scratch sized to the map's 99-vertex
  maximum. That path passed all scratch allocation and patch construction on
  retail with roughly 19.9 MB free, then stopped while registering shaders in
  the face-sizing pass. `hwfix9` proved all 183 unique world shader/lightmap
  pairs register successfully on retail hardware. Offline validation also
  proved all 111,927 planar index references in `borg1` are in range. The stop
  remained inside the 12,906-face sizing pass, which redundantly queried cached
  shader metrics for every face and did not update the loading screen.
  `hwfix10` cached those metrics once per shader/lightmap pair and added bounded
  loading updates plus progress markers to both the face-sizing and face-build
  passes. Retail then completed the entire raw renderer, collision, server, and
  cgame world-load sequence. The resulting black screen was the error fallback
  after the first client-model precache reported `DEFAULT_MODELS failed to
  register`. Base game assets remain owned by `pak[x].pk3`; `xbox[x].pk3`
  remains an Xbox override package and must not duplicate the base game. The
  `hwfix11-modeltrace` preserved both patch PK3s byte-for-byte and added one
  bounded trace across client render-info parsing, model and skin syscalls,
  renderer cache/file/allocation/loading, skin registration, and
  `animation.cfg` parsing. Its retail log stopped silently after planar face
  10,752 of 12,906. `hwfix12-facebounds` resolved each canonical raw
  shader/lightmap pair once, reused that shader during construction, and
  bounds-checked every face write; retail then completed all 12,906 faces,
  collision, server initialization, and cgame world loading. The resulting
  black screen is the error fallback after `DEFAULT_MODELS failed to register`.
  That run also proved the defensive face reservation used only 1,735,111 of
  3,607,827 bytes. `hwfix13-modelcritical` retains shader reuse and bounds
  checks, restores exact face sizing to return roughly 1.87 MB to model
  registration, and makes the bounded model/skin/filesystem trace unfiltered
  on retail. Base game assets remain in retail `pak[x].pk3`; the two
  `xbox[x].pk3` patch files stay byte-for-byte unchanged during this code-only
  iteration. The next retail run proved the model loader itself was healthy:
  the `DEFAULT_MODELS` failure was caused by a missing base `pak0.pk3`. After
  restoring that retail archive, the game reached `CA_ACTIVE` and continued
  loading deferred character models, but appeared frozen on the loading screen.
  `hwfix14-loadstall` removes the temporary per-model synchronous log flood,
  stops treating the expected process-heap-to-zone allocation path as a
  failure, and adds explicitly flushed checkpoints around the first active
  gameplay frame and the last observed Borg head-skin parser boundary. Retail
  then reached `CA_ACTIVE`, but the forced log ended at `first active frame
  begin`, proving that the loading screen was waiting on the first
  `CG_DrawActiveFrame` rather than a map-load failure. The first active frame
  was attempting to register every visible deferred character model
  synchronously. `hwfix15-firstframe-stream` limits deferred visible-character
  registration to one character per rendered frame and adds forced checkpoints
  after snapshots, prediction, view setup, packet-entity submission, and final
  rendering. This should let the first frame present while remaining character
  models stream over subsequent frames, and will identify the exact stage if
  retail hardware still stops. With the direct-map marker removed, retail
  hardware now reaches the normal main menu and the Holomatch menu handoff
  successfully launches a live Holomatch map. Holomatch is functional but
  remains in the low single-digit FPS range on the tested retail setup. A
  separate direct-map SP attempt completed world-map registration but stopped
  before `CA_ACTIVE`; `hwfix16-media-checkpoints` adds bounded, force-flushed
  checkpoints around the intervening loading-screen refresh, shader, model,
  item, inline-model, client, entity, HUD, and speaker-media phases.
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
- Do not repeat the rejected direct pushbuffer or hot-counter-removal
  experiments without new evidence.
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
