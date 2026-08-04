# Elite Force X Game To-Do

Current qualification snapshot: `HOLOMATCH_QUALIFICATION.md`.

## Remaining Items

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
- `Beta-20260802-retail-file-probe-fix` was rejected as the primary
  performance fix after retail gameplay remained at approximately 2 FPS. Its
  removal of recurring diagnostic filesystem probes remains as runtime
  cleanup.
- The next retail A/B candidate is
  `Beta-20260802-retail-inline-push`, staged in the existing one-folder
  hardware directory at
  `build/hardware/StarTrekEliteForceX-Beta-20260801`. It replaces the shared
  renderer's non-indexed `DrawPrimitiveUP` calls with the XDK 5558 canonical
  `BeginPush`/`D3DPUSH_INLINE_ARRAY` path. Indexed submissions and packaged
  assets are unchanged. First confirm that rendering remains complete, then
  record FPS in campaign gameplay and Holomatch before accepting or rejecting
  it.
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
