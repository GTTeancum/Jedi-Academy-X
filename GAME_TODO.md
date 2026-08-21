# Elite Force X Game To-Do

Current qualification snapshot: `HOLOMATCH_QUALIFICATION.md`.

## Remaining Items

- 2026-08-20 retail hardware note:
  cooperative split-screen was observed at roughly 15 FPS, and no SP/co-op log
  file was produced for upload. Treat this as an unresolved performance and
  proof-capture blocker, not as a qualified result.

- Cooperative menu flow remains incomplete:
  add character select, difficulty select, and separate new-game/load-game
  choices for co-op instead of dropping directly into a fixed setup.

- Cooperative HUD work remains open:
  one-player and two-player co-op need distinct HUD layouts/shapes, with the
  two-player layout sized and clipped for split-screen.

- Dialogue presentation remains incomplete:
  voice lines still do not play in cases where the talking-head face texture is
  supposed to animate.

- 2026-08-21 SP `borg1` right-facing black-surface concern is closed in
  XEMU/LLE. The first-control view at `(104, -936, 0)`, yaw 360, exposed BSP
  sky faces using `textures/common/sky`; the retail definition lived in
  `scripts/voyager.shader`, but that stock shader script was omitted from the
  shared Xbox patch package and console shader list. The canonical package
  builder now retains both patched `scripts/borg.shader` and stock
  `scripts/voyager.shader`. Final `xbox1.pk3` SHA256 is
  `A4A1D63520DCEDDFA2C5D3C8915CAEB791BE5804C9CCFCABBE8A6A947A77D40B`;
  it contains the retail `skyParms env/stars 512 -` contract, all six star-cube
  DDS faces, and the Holomatch support assets including `maps/hm_borg1.aas`.
  The focused `borg1` material audit reports zero used missing assets and zero
  used unresolved materials. A 180-second no-screenshot run remained alive at
  controlled shutdown and produced
  `scripts/output/sp-borg1-skyfix-live_borg1_20260821_111421.report.txt` plus
  the extracted
  `scripts/output/sp-borg1-skyfix-live_borg1_20260821_111421_ef_sp_log.txt`;
  the log identifies the current production `default.xbe`, loads
  `maps/borg1.bsp`, and contains no shader-fallback record. In the requested
  live rerun, the user inspected the surface 90 degrees right of first control
  and confirmed it fixed. This closes that exact SP visual defect only; it does
  not replace broader SP map coverage or Holomatch/hardware qualification.

- Holomatch four-player local split-screen is now active work:
  the Holomatch button now launches a local 4P FFA-with-bots match in
  `efmp.xbe`, with synthetic controls for P1-P4 during proof runs so all four
  viewports can be exercised before controller detection is added. P1 keeps the
  normal client snapshot path; P2-P4 are synthetic local clients. The natural
  front-end handler and XBE handoff are proven in XEMU/LLE, but the feature is
  not shipping-qualified: real controller assignment, representative-map
  visual coverage, sustained performance, long stability, and staged hardware
  proof remain open. Required
  returned-log markers include `STEFX_HM_SPLIT`, `STEFX_HM_SPLIT_LAUNCH`,
  `STEFX_HM_SPLIT_CMD`, `STEFX_HM_SPLIT_REFDEF`, `STEFX_HM_SPLIT_STATE`,
  `STEFX_HM_SPLIT_RENDER`, `STEFX_HM_SPLIT_RENDER_DONE`,
  `STEFX_HM_SPLIT_FP_FILTER`, `STEFX_HM_SPLIT_HUD`,
  `STEFX_HM_SPLIT_HUD_STATUS`, `STEFX_HM_SPLIT_HUD_DIVIDER`, and
  `STEFX_HM_SPLIT_SNAPSHOT`. Returned Holomatch logs can be checked with
  `python scripts\verify_holomatch_split_log.py <log>`; add `--min-bots 3
  --require-attack --require-positive-snapshot-adds
  --require-fp-filter-slot 1 --require-fp-filter-slot 2
  --require-fp-filter-slot 3 --min-elapsed-seconds 90 --min-heartbeat-fps 15
  --min-largest-free 1048576 --max-used-delta 0` for a stricter qualification
  pass once the match has run long enough for bot fill and first-person weapon
  submissions. By default the split verifier also requires each virtual client
  to emit a distinct command profile, using the returned `move=` and `angles=`
  values from `STEFX_HM_SPLIT_CMD` to prove that P1-P4 are not sharing one
  control stream, and now requires repeated `STEFX_HM_SPLIT_STATE` samples to
  show each virtual-control client moving at least 8 units over the run. The
  engine spaces those state samples on a 500 ms cadence so the movement gate is
  not starved by a burst of adjacent startup frames. It also requires P2-P4 to
  remain marked as local split clients and not bots, four unique local origins,
  a minimum P2-P4 distance from P1 using the logged `p1Dist=`, distinct local
  refdef origins, and distinct completed render-view origins, so four logged
  render slots are not enough if they collapse to the same camera or stacked
  players. It now also parses the logged `ref=` viewport rectangles and
  requires a positive, unique,
  top-left/top-right/bottom-left/bottom-right 4P quadrant layout. The
  render proof also logs `externalClient=<slot>` and the verifier requires
  each completed pane to use the matching local client refdef. HUD remap proof
  parses `dst=` rectangles and requires the same four-quadrant layout, so a
  returned log can catch a collapsed or misplaced HUD without screenshots.
  Split render proof is emitted for a small three-frame startup budget, not a
  single frame and not a hot per-frame diagnostic, so returned logs should
  survive early-frame timing without recreating the old hardware log spam. The
  retail renderer now filters only tracked Holomatch lower/upper/head player
  model handles with an entity-number match to the active external local
  client when suppressing likely local third-person self models from external
  Holomatch split panes and logs `STEFX_HM_SPLIT_SELF_FILTER`; strict returned
  logs checked with repeated `--require-self-filter-slot` must include
  `refNumber=<slot>` and `modelPart=lower|upper|head` for those entries once a
  run confirms the official cgame submits those self bodies. The
  split verifier requires a 4P local Holomatch launch record by default and
  summarizes returned heartbeat FPS, completed-frame progress,
  retail renderer `path=1`, and `mem=` free/largest free/used-delta values; it
  complements but does not replace the full staged hardware production verifier.
  The same gate can be required inside final production verification or the
  combined hardware audit with
  `--require-hm-split-log`. The code-only Holomatch architecture checker now
  also requires the 4P local launch cvars and virtual-control startup path, so
  `python scripts\check_mp_holomatch_ui.py --code-only` should fail if the
  Holomatch button or XBE launch intent drifts back to a non-split startup.
  The official Holomatch player HUD now runs once per local player from that
  slot's authoritative official EF player state and stored split refdef. The
  SP-hosted syscall bridge routes each pass only into its matching quadrant and
  logs `STEFX_HM_SPLIT_HUD: slot=... shared=0`; the verifier requires all four
  independent slots and exact quadrant bounds by default. The old compact top
  diagnostic status overlay has been removed. `STEFX_HM_SPLIT_HUD_STATUS`
  remains monitor-only proof of health, weapon, score, and destination, and
  `STEFX_HM_SPLIT_HUD_DIVIDER` records the centered pane separators.
  Latest XEMU/LLE proof after the external split-refdef, secondary pane clear,
  broad self-model filter, dead-view camera, local split-player fast-respawn,
  and production heartbeat-mirror fixes is
  `scripts/output/hm-split-heartbeat-fps-4p_hm_borg1_20260820_132707.report.txt`
  with contact sheet
  `scripts/output/hm-split-heartbeat-fps-4p_hm_borg1_20260820_132707_contact.png`.
  The run used 2026-08-20 13:24:43 `default.xbe` and 13:26:02 `efmp.xbe`,
  exited alive, and shows all four panes rendering live `hm_borg1` geometry
  across the capture window; the earlier broad P2-P4 HOM failure is not
  reproduced. Log-side FPS now reports 12 active/gameplay samples averaging
  34.1 guest/game-clock FPS, 22.3 minimum, with 4/12 below 30. Delivered XEMU
  wall-FPS averaged 25.0 with screenshot/poll stalls included, so 4P
  Holomatch is alive but not performance-accepted. The latest screenshot-free
  4P audio-listener proof is
  `scripts/output/hm-split-audio-4listeners-4p_hm_borg1_20260820_135837.report.txt`
  using 2026-08-20 13:56:02 `default.xbe` and 13:57:18 `efmp.xbe`;
  it exited alive with Xbox audio backend state `0x4`, 168 sound registrations,
  226 starts, 460 loops, 885 respatializes, 20 voice starts, repeated
  `lipActive` samples, listener state `0x00040004`, and listener update mask
  `0x0000000e` proving P2-P4 listener transforms are fed. This supersedes the
  earlier single-listener proof. Actual audible 4P mix quality, staged hardware
  behavior, and the known voice-line/face-animation case remain unqualified/open.
  The 2026-08-21 split-world investigation disproved collision-map and
  renderer-PVS corruption for the sampled failures. Corrected all-slot proof
  showed 758 collision clusters, five inline models, linked P1-P4 entities,
  valid per-pane render clusters, and empty eye contents. A bounded
  `r_nocull 1` diagnostic still showed P4 among isolated back sides of map
  brushes while airborne at `z=-241`, identifying blind synthetic movement
  into exposed/non-playable map space as the broad black-void source. Virtual
  fallback controls now trace the full player box ahead, probe for floor, turn
  away from walls/drop-offs, and log `STEFX_HM_SPLIT_VIRTUAL_AVOID`; physical
  controller commands bypass this harness. The supporting normal-culling
  90-second proof is
  `scripts/output/hm-virtual-avoid-proof_normal_20260821_010212.report.txt`.
  All six captures were inspected sequentially and show four coherent live
  worlds, independent HUDs, weapons, and effects without the isolated-brush
  black void. The run exited alive; guest/game FPS averaged 26.1 (21.2-33.2)
  and wall throughput averaged 18.5. The retained candidate additionally
  suppresses periodic virtual jumps while avoidance is active. Its exact-binary
  65-second proof is
  `scripts/output/hm-virtual-avoid-finalproof_normal_20260821_010758.report.txt`,
  using `default.xbe` SHA256
  `A92D119388150237F532BCB3C2728CF45492568E291E6691B432D07D73330662`
  and `efmp.xbe` SHA256
  `D6BD5A6A3F6A08D8C1396AB126D64E85CE2CEA5195691154580BD74012CD5403`.
  Its three retained captures were inspected sequentially and show four
  coherent worlds without the prior exposed-space failure. The run exited
  alive; guest/game FPS averaged 26.5 (19.4-30.5) and wall throughput averaged
  18.0 (10.1-31.3). This improves deterministic visual proof, but does not
  close representative-map, real-controller, long-soak, or staged hardware
  qualification.
  The focused split verifier can now make this plumbing a hard gate with
  `--audio-only --require-audio-backend --require-audio-listeners
  --min-audio-starts 100 --min-audio-voice-starts 8
  --require-audio-lip-active` for screenshot-free monitor reports, or with the
  same audio flags alongside the normal split gates for returned full logs; the
  production hardware verifier exposes the same check through
  `--hm-split-require-audio-backend --hm-split-require-audio-listeners
  --hm-split-min-audio-starts 100 --hm-split-min-audio-voice-starts 8
  --hm-split-require-audio-lip-active` when `--require-hm-split-log` is used.
  The same run averaged 37.3 guest/game-clock FPS (35.1 minimum, 0/4 below 30)
  but only 9.9 delivered XEMU wall-FPS, so audio-on 4P still needs hardware
  proof before any performance call.
  Newer XEMU/LLE visual/audio proof after tightening dead split-pane fallback
  and dead virtual-player respawn input is
  `scripts/output/hm-split-deadview-fix-4p_hm_borg1_20260820_143939.report.txt`
  with contact sheet
  `scripts/output/hm-split-deadview-fix-4p_hm_borg1_20260820_143939_contact.png`.
  It used 2026-08-20 14:37:05 `default.xbe` and 14:38:29 `efmp.xbe`, exited
  alive, and the sampled contact sheet shows P2-P4 rendering live `hm_borg1`
  geometry instead of the earlier broad HOM failure; the pass did not catch a
  local death, so the new `STEFX_HM_SPLIT_DEAD_CMD`/
  `STEFX_HM_SPLIT_DEAD_VIEW` breadcrumbs still need a returned death/respawn
  log or a longer soak to prove that specific path. Audio remained active in
  the same visual pass with backend `0x4`, listener state `0x00040004`,
  listener mask `0x0000000e`, 174 registrations, 269 starts, 595 loops, 1048
  respatializes, and 16 voice starts. Guest/game-clock FPS averaged 42.4
  (33.3 minimum, 0/4 below 30), while delivered XEMU wall-FPS averaged 14.6
  with screenshot/poll overhead; staged hardware proof remains required before
  accepting 4P performance.
  Current 4P economy-mode proof after skipping dynamic lights, flares, and
  Xbox world effects only for `stefx_hm_split_economy 1`, plus fast local
  split-player respawn, is
  `scripts/output/hm-split-economy-fastrespawn-4p_hm_borg1_20260820_150454.report.txt`
  with contact sheet
  `scripts/output/hm-split-economy-fastrespawn-4p_hm_borg1_20260820_150454_contact.png`.
  It used 2026-08-20 15:02:27 `default.xbe` and 15:03:49 `efmp.xbe`
  (`efmp.xbe` SHA256
  `151DAE9953D3BBE2C84A6C9F4FAC6353200BAFB3F122BF7F15F2825E68ED5792`),
  exited alive, and the contact sheet shows all four Holomatch panes rendering
  live `hm_borg1` geometry and per-pane HUDs. This is progress proof, not
  signoff: P3/P4 can still expose black void/portal-like gaps from some
  angles, and wall-clock XEMU FPS remains below target. Guest/game-clock FPS
  averaged 36.6 with a 22.0 minimum and 1/7 samples below 30; delivered XEMU
  wall-FPS averaged 26.0 with 4/6 samples below 30. Audio plumbing in the same
  run passed the focused verifier with backend `0x4`, four listeners, listener
  mask `0x0000000e`, 172 registrations, 620 starts, 1348 loops, 2230
  respatializes, 36 voice starts, and `lipActive=1`.
  Newest XEMU/LLE self-filter proof after widening the hosted Holomatch
  secondary-pane self-model suppression is
  `scripts/output/hm-split-self-filter-4p_hm_borg1_20260820_151509.report.txt`
  with contact sheet
  `scripts/output/hm-split-self-filter-4p_hm_borg1_20260820_151509_contact.png`.
  It used 2026-08-20 15:12:35 `default.xbe`
  (`SHA256 9B49A9143D3C018E539C9E0B7C4B8F6126F9E717DB0D6EA13F9CB0EA2ED6E28E`)
  and 15:13:56 `efmp.xbe`
  (`SHA256 891D164B8162FFE6ECA3C623EF9B5AB526D85BB8B829E2A6EA3F1F7DEBB66FE9`),
  exited alive, and the contact sheet shows all four panes with live
  `hm_borg1` geometry and per-pane HUDs. The earlier broad P2-P4 HOM/void
  pattern is not visible in this sample; some panes are still close to dark map
  walls, so this is improved visual proof rather than full map/angle signoff.
  Guest/game-clock FPS averaged 43.0 with a 33.0 minimum and 0/7 samples below
  30; delivered XEMU wall-FPS averaged 29.5 with 3/6 samples below 30. The
  same report passed the focused audio verifier with backend `0x4`, four
  compiled/active listeners, listener mask `0x0000000e`, 173 registrations,
  514 starts, 1239 loops, 2202 respatializes, 37 voice starts, and
  `lipActive=1`. Staged hardware remains required for accepted 4P performance
  and audible mix quality.
  Newest XEMU/LLE view-weapon proof after cloning/rebasing the P1
  first-person weapon entities into each external split refdef is
  `scripts/output/hm-split-viewweapon-4p_hm_borg1_20260820_153113.report.txt`
  with contact sheet
  `scripts/output/hm-split-viewweapon-4p_hm_borg1_20260820_153113_contact.png`.
  It used 2026-08-20 15:28:28 `default.xbe`
  (`SHA256 B62E7049FE508FC56BEFA2C681BE75C0DF9A89E367244CB314E28B2A0689D832`)
  and 15:29:55 `efmp.xbe`
  (`SHA256 9F039285194ACDFFFCA9DD60D479DEB5E4DCFEB80B1CF98B4A5CB7A6B07DFC2E`),
  exited alive, and visually shows P1-P4 with live `hm_borg1` geometry,
  per-pane HUDs, and first-person weapon models in every pane. This fixes the
  observed P2-P4 no-weapon regression in the current XEMU proof window. Guest
  game-clock FPS averaged 43.7 with a 31.2 minimum and 0/7 samples below 30;
  delivered XEMU wall-FPS averaged 28.6 with 3/6 samples below 30. The same
  report passed the focused audio verifier with backend `0x4`, four
  compiled/active listeners, listener mask `0x0000000e`, 176 registrations,
  543 starts, 1408 loops, 2281 respatializes, 33 voice starts, and
  `lipActive=2`. Staged hardware remains required for accepted 4P performance
  and audible mix quality.
  Superseding monitor/visual proof after exporting the split proof arrays for
  no-log XEMU runs and tightening the virtual attack/state proof is
  `scripts/output/hm-split-monitor-proof-4p-v3_hm_borg1_20260820_160457.report.txt`
  plus the visual run
  `scripts/output/hm-split-viewweapon-proof-4p-v3_hm_borg1_20260820_160815.report.txt`
  with contact sheet
  `scripts/output/hm-split-viewweapon-proof-4p-v3_hm_borg1_20260820_160815_contact.png`.
  These used 2026-08-20 16:00:43 `default.xbe`
  (`SHA256 277357E319D1B40610E5E8E4FD92201D1357F4CA2A88B67752771A5BCB48946F`)
  and 16:02:17 `efmp.xbe`
  (`SHA256 55A9969954132F3FAF9C7EEBB8A35FB5ADFF2D46ABDE5DD8CB2FE71FE6CB6A13`).
  Both verifier passes required four local split players, unique P1-P4 virtual
  commands with attack, P2-P4 external refdefs/snapshots, positive P2-P4
  view-weapon clones, per-pane HUD/status/divider proof, self/first-person
  model filtering, memory headroom, and audio backend/listener/voice/lip
  activity. The contact sheet visibly shows live P1-P4 `hm_borg1` quadrants
  with first-person weapon models in every pane, closing the returned P2-P4
  no-weapon regression for this proof window. The monitor run exited alive with
  guest/game FPS 31.3 average, 24.2 minimum, 4/9 below 30, wall-FPS 22.3
  average, 12.1 minimum, 6/8 below 30, largest free block minimum 7.26 MiB,
  `bots=0`, and audio max voice starts 54/`lipActive=1`. The visual run exited
  alive with guest/game FPS 31.1 average, 24.5 minimum, 3/8 below 30, wall-FPS
  25.6 average, 12.1 minimum, 4/7 below 30, largest free block minimum
  7.26 MiB, `bots=0`, and audio max voice starts 67/`lipActive=2`. Bots and
  accepted staged hardware performance remain open.
  The current weapon implementation no longer clones or rebases P1 renderer
  entities for P2-P4. `STEFX_HM_CG_AddSplitViewWeapon` generates each external
  pane's first-person weapon from that slot's official EF player state and its
  stored split refdef. A dedicated opt-in proof grants real ownership/ammo and
  selects phaser, compression rifle, IMOD, and scavenger rifle through normal
  user commands. The fresh XEMU run
  `scripts/output/hm-split-distinct-weapons-4p_hm_borg1_20260820_200111.report.txt`
  and matching contact sheet used `efmp.xbe` SHA256
  `2A38D2BF44CF39C58671DD89938322A8EF5047491080288425BA188E4DB9ED38`,
  exited alive with three bots, and repeatedly reported matching state,
  command, and cgame weapon IDs 1/2/3/4. All five saved frames were reviewed
  individually and show four complete non-HOM worlds plus four visibly
  different first-person weapon models. Texture allocation remained fixed at
  2,985,856 bytes used with 7,499,904 bytes largest-free. Guest/game FPS was
  18.4 average and 13.2 minimum under visual polling; this does not qualify
  performance. Open follow-ups at that point were P4 post-respawn weapon
  reselection and a real per-player official HUD pass.
  A follow-up on the exact current XBE is
  `scripts/output/hm-split-distinct-weapons-respawn-4p_hm_borg1_20260820_200641.report.txt`
  with matching contact sheet and `efmp.xbe` SHA256
  `69C98B693743CF9F7F1BC2FD4646E503311C15F8BA335A5C51675A8F76DE89A0`.
  It exited alive with three bots and fixed 2,985,856-byte texture use. All six
  captures were inspected individually and show four complete worlds and the
  four distinct models. P2 respawned from 3 to 124 health with compression
  restored; P3 transitioned from its transient spawn phaser back to IMOD via
  requested weapon 3; P4 stayed alive on scavenger. The deterministic proof
  controller now releases fire while the authoritative weapon differs from
  the requested weapon, allowing the normal EF switch state machine to finish.
  Guest/game FPS averaged 17.6 with a 13.6 minimum under visual polling, so
  staged hardware and screenshot-free performance evidence remain open.
  The exact-final independent official-HUD build is `efmp.xbe` SHA256
  `7E901F31D15902B9D22DA459F770AE2C39718D8E7FF4CDE29F53E83EBB724AFB`
  with runtime ID `Aug 20 2026 20:26:39`. Its XEMU/LLE proof is
  `scripts/output/hm-split-official-hud-final-4p_hm_borg1_20260820_202836.report.txt`
  with matching contact sheet. The process exited alive with three bots, all
  four distinct command/attack streams, state and view-weapon IDs 1/2/3/4,
  four independent `shared=0` HUD records, fixed 2,985,856-byte texture use,
  6,416,704-byte minimum largest-free memory, and 117,204-byte used-memory
  growth. All four successful 640x480 captures were reviewed sequentially:
  every quadrant has complete non-HOM geometry, a distinct first-person weapon,
  and its own official health/ammo HUD. P3 also died, respawned, restored IMOD,
  and restored its own HUD during the capture window. Guest/game FPS averaged
  18.0 with a 12.5 minimum under screenshot and monitor polling; staged
  hardware remains the performance authority. Per-player zoom state,
  pickup/reward/attacker announcements, real controller assignment, audible
  four-listener mix quality, and staged hardware performance remain open.
  The global-overlay policy and scoreboard localization are now proven on the
  4,190,208-byte `efmp.xbe` with runtime ID `Aug 20 2026 22:01:06` and SHA256
  `CBAF7186CE482F9F1F28B3BFC9AD7EE2ABD2151161C9C52AA07986A25E09D960`.
  Clean-stage visual evidence is
  `scripts/output/hm-split-scoreboard-text-cleanproof2-4p_hm_borg1_20260820_221901.report.txt`
  with contact sheet
  `scripts/output/hm-split-scoreboard-text-cleanproof2-4p_hm_borg1_20260820_221901_contact.png`.
  Its only captured frame was reviewed sequentially and shows one full-screen
  official scoreboard over four complete live worlds, with `NAME`, `SCORE`,
  `TIME`, `PING`, `Players`, `FREE FOR ALL`, and real player rows instead of
  `?` placeholders. Runtime memory reads independently prove the complete
  retail `PAK2.PK3` text asset loaded at 3,117 bytes, parsed all 184 entries,
  and passed validation state `0x1ff`. The smoke harness now offers
  `-CleanStageData` so direct diagnostic runs remove loose seed-data overrides
  without deleting their launch controls. The longer screenshot-free report
  `scripts/output/hm-split-scoreboard-text-cleanqual-4p_hm_borg1_20260820_222201.report.txt`
  passes `verify_holomatch_split_log.py`: all four local players moved and
  attacked independently, P2-P4 produced matching external refdefs/snapshots
  and view weapons, all four independent HUD rectangles passed, three bots
  joined, and the process remained alive. Across six heartbeat samples it
  averaged 18.4 guest/game FPS with a 14.0 minimum; minimum largest-free memory
  was 6,472,928 bytes and used-memory growth was 163,927 bytes. This closes the
  global scoreboard placement/text defect in XEMU, not staged-hardware FPS or
  soak acceptance.
  The current natural front-end/XBE-handoff build is 4,395,008-byte
  `default.xbe` with runtime ID `Aug 20 2026 22:38:08` and SHA256
  `4CFAC4E5C25AF183D1B64DE21DA89E8A79AFC73EF331AB1E6AC08B17C638D22B`,
  plus 4,190,208-byte `efmp.xbe` with runtime ID
  `Aug 20 2026 22:39:49` and SHA256
  `B5C3976DC6E03A5A30280B2D4B6BAA408E80E5F6AF55A08896BA3452A8742F9D`.
  Visual evidence is
  `scripts/output/hm-menu-handoff-visualproof_normal_20260820_225606.report.txt`
  with contact sheet
  `scripts/output/hm-menu-handoff-visualproof_normal_20260820_225606_contact.png`.
  Its four source captures were inspected sequentially. Every capture shows
  four distinct moving `hm_borg1` views with independent official HUD values
  and a phaser model in every quadrant; beams and lens flares are also visible.
  The fourth capture has a localized black/missing-surface-looking patch in the
  top-right far wall and a distorted-looking floor region in the bottom-left,
  so broad P2-P4 HOM is fixed but full visual signoff remains open.
  The exact same immutable ISO then passed monitor proof in
  `scripts/output/hm-menu-handoff-xbetelemetry_normal_20260820_230247.report.txt`.
  `verify_holomatch_split_log.py --require-launch-source xbe` proved four
  local players, four distinct moving/attacking command streams, P2-P4
  refdefs/snapshots/view weapons, positive P1-P4 draw counts, exact independent
  320x240 HUD quadrants, three bots, advancing heartbeats, memory headroom, and
  a live process at shutdown. The report's `map='normal'` field is the harness
  normal-boot context label; the launched `efmp` runtime and visual BSP are
  `hm_borg1`.
  A longer reduced-poll screenshot-free pass is
  `scripts/output/hm-menu-handoff-perfsoak_normal_20260820_230703.report.txt`.
  It passed the strict verifier with `source=xbe`, 90.5 seconds of measured
  gameplay, three bots, all four independent controls/attacks/views/HUDs,
  positive P2-P4 snapshot adds, and a live process at controlled shutdown.
  Used memory grew by 422,978 bytes across the samples; minimum free memory was
  13,151,855 bytes and minimum largest-free memory was 6,430,272 bytes. Xbox
  audio remained active with four listeners and P2-P4 listener updates,
  reaching 219 registrations, 2,152 starts, 5,123 loops, 2,478 respatializes,
  158 voice starts, and `lipActive=2`. This proves audio plumbing, not audible
  mix quality or the separate missing voice-line case. Guest/game FPS averaged
  23.5, fell from 35.9 to 16.0, and had 12/14 samples below 30. Polling also
  reduced delivered wall FPS to 15.8 average. The user's uninstrumented XEMU
  overlay observation of roughly 30 FPS remains useful context, but neither
  measurement accepts 4P performance; staged retail hardware is authoritative.
  The 2026-08-21 guest-phaser correction now keeps P2-P4 entirely on their
  authoritative official EF player-state path. First-person beam submission
  keys from the active split player's `EF_FIRING` state instead of rejecting a
  valid beam after that player's recharge time advances, and each pane filters
  only its own third-person world beam. No P1 weapon entity is cloned or
  rebased, so independent weapon ownership and later pickups remain intact.
  The current release pair is `default.xbe` runtime ID
  `Aug 21 2026 02:46:53`, SHA256
  `CFC800C2C84B521D85D6C55185DCD4F421FFCCDE048D2B4326569EAFD9157CD3`,
  and `efmp.xbe` runtime ID `Aug 21 2026 02:47:00`, SHA256
  `8EEC18D94C36246BE85594258D1B3625FA85403341E0D640E92E50A63CCEFF94`.
  Deterministic proof is
  `scripts/output/hm-phaser-all-guests-proof_hm_borg1_20260821_024807.report.txt`.
  All four retained frames were inspected sequentially and show P2, P3, and
  P4 beams beginning at their own phaser muzzles and converging on their own
  crosshairs. Per-slot bridge counters reached 1,045 first-person lines with
  unique tags `0x53504201`, `0x53504202`, and `0x53504203`; own-pane world
  copies were filtered. The proof averaged 25.0 guest/game FPS and is visual
  correctness evidence, not performance acceptance. A normal three-bot
  regression in
  `scripts/output/hm-post-phaser-normal_hm_borg1_20260821_025108.report.txt`
  retained the corrected muzzle origins during live movement, combat, and
  respawns across all three sequentially inspected frames.
  The longer normal run
  `scripts/output/hm-post-phaser-hom-soak_hm_borg1_20260821_025508.report.txt`
  remained alive for 94.6 seconds with four local players and three bots. All
  21 successful captures were inspected sequentially; every quadrant retained
  coherent `hm_borg1` geometry, HUD, and weapon rendering. The dark P3 region
  at 45.3 seconds is stable patterned doorway/interior geometry, not HOM, and
  subsequent P3 views traverse the corridor cleanly. This bounded sample did
  not reproduce either broad or localized HOM, but representative-map and
  wider angle coverage remain open. Audio stayed active with backend `0x4`,
  four-listener mask `0x0000000e`, 114 voice starts by the final sample, and
  observed lip activity. Guest/game FPS averaged 21.8 (17.2 minimum), while
  screenshot/poll-heavy wall FPS averaged 15.9. Minimum sampled free memory
  was 13,176,703 bytes, minimum largest-free memory was 6,459,360 bytes, and
  sampled used-memory growth was 192,718 bytes. This is visual/audio/HOM soak
  evidence, not a strict four-control pass: only 60.3 seconds landed inside the
  heartbeat measurement window, and P1 remained stationary while P2-P4 moved.
  Real controllers, audible mix quality, representative maps, a materially
  longer strict soak, and staged hardware FPS/stability remain open.
  Representative-map visual coverage now also includes
  `scripts/output/hm-post-phaser-representative-dn1_hm_dn1_20260821_030408.report.txt`
  on the same exact release pair. The process remained alive, reached three
  bots, and held the texture allocator exactly at 3,206,016 bytes used across
  the run. All five successful captures were inspected sequentially and show
  coherent `hm_dn1` geometry in all four panes across atrium, corridor, stairs,
  close-wall, combat, and effect views. P2-P4 retain first-person weapons, and
  captured guest beams start at the matching muzzle. No HOM or void is visible
  in this sample. Guest/game FPS averaged 17.2 (15.3 minimum) under heavy
  visual polling, so it is not performance acceptance. The report contains no
  audio samples and still has stationary virtual P1 input; it is map-rendering
  evidence only.

- Co-op qualification remains open. The earlier v54 long harness run is not
  valid proof of a natural cinematic-to-gameplay transition: generic smoke
  input could also call `CGCam_Disable` once its time window opened. The v56
  harness separates those responsibilities. Movement, attack, and view
  overrides now return immediately while `in_camera` is active; only the
  explicit `stefx_smoke_unlock_player` diagnostic may terminate a camera.
  A 135-second XEMU replay armed an obvious 90-degree view override and kept
  the scripted crawl/camera in control across all eight captures, with the
  process alive at shutdown. Evidence is
  `scripts/output/retail-v56-coop-camera-ownership-gate_normal_20260818_005804.report.txt`
  and
  `scripts/output/retail-v56-coop-camera-ownership-gate_normal_20260818_005804_contact.png`.
  A matched 135-second control replay removed the smoke-input path from the
  staged disc entirely. It followed the same authored crawl progression into
  the Borg environment and showed Munro in-map beneath the still-scrolling
  text at the disputed late capture. That shot is part of the scripted crawl,
  not early harness control. Evidence is
  `scripts/output/retail-v56-coop-zero-input-control_normal_20260818_012014.report.txt`
  and
  `scripts/output/retail-v56-coop-zero-input-control_normal_20260818_012014_contact.png`.
  The concern was rechecked against the current v62 executable with the smoke
  path present but its first command scheduled for 210 seconds, beyond the
  145-second run. Every sampled in-map Munro shot retained `camera=1`, including
  the shots that resemble ordinary third-person play. No harness command was
  eligible to run. Evidence is
  `scripts/output/retail-v62-coop-input-ownership-recheck_normal_20260818_043821.report.txt`
  and
  `scripts/output/retail-v62-coop-input-ownership-recheck_normal_20260818_043821_contact.png`.
  The runner's generic visual check labels this run failed only because one
  captured transition frame was more than 70 percent black; emulation remained
  alive and that frame is not an input/camera failure.
  Treat all future crawl qualification as input-free. A shot that resembles
  ordinary third-person play is still authored crawl footage while the camera
  flag is set; player-control proof begins only after an explicit camera-off
  marker. The harness must record that first eligible-input transition before
  it may inject movement or view commands.
  A separate natural transition and split-screen gameplay/FPS proof is still
  required; do not use co-op FPS from a cinematic or synthetic camera skip as
  a renderer acceptance result. Co-op remains in scope, but its second full
  scene view makes it a downstream performance qualification: first fix the
  shared single-view SP/Holomatch bottleneck, then measure and optimize the
  incremental split-screen cost without reducing either viewport.

- Hardware performance and stability remain the current acceptance gate. The
  canonical controller configuration at
  `C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\BaseEF\default.cfg` is a
  read-only reference and must never be edited or staged by Codex. Its SHA256
  is `3C5B05EBF8B732E1D1065A2337BE63E421560E42C2B4E9CFB28A843F9AB38493`;
  the release and retained hardware-stage copies are byte-identical. A valid
  host-window XEMU input run proved the current release `default.xbe` honors
  the canonical `JOY4 -> uimenu` binding: Start opened the in-game menu and a
  second press returned to live `borg6` gameplay. Evidence is
  `scripts/output/sp_start_binding_hostpath_borg6_20260819_053551.report.txt`
  and its adjacent contact sheet. The previous retained hardware folder
  contained the older v75 diagnostic `default.xbe`
  (`4C8CC815619E2491CBCE6D9F0F62E1B79D703814E9F40999AFA91EA223AA6D99`),
  not the input-proven August 19 release binary
  (`A9B5D1896009543FA6D636D3E19ABC1D3067784FFC44669EE4ADE799832BB233`).
  Do not treat that old package's missing-menu observation as a current-source
  control regression. Its SP and MP logs are preserved under
  `build/analysis/hardware-baseline-20260818`. The one retained hardware folder
  is now refreshed as `Beta-20260819-hm-hotlog-off-v77`. This candidate
  compiles the high-frequency Holomatch bot, prediction, weapon, pickup, and
  sound diagnostics out of production while retaining lifecycle and fault
  breadcrumbs. In the old hardware failure log, those diagnostic families
  produced hundreds of lines before the first-bot freeze. The matched XEMU log
  probe now advances with only one to four writes per five-second sample after
  startup. A 100-second Holomatch soak stayed alive past the old freeze window,
  and a second 65-second `hm_borg1` run traversed several open rooms with
  effects and projectiles while the visible counter held approximately 90 FPS.
  Evidence is
  `scripts/output/hm-hotlog-off-soak_hm_borg1_20260819_073203.report.txt`,
  `scripts/output/hm-hotlog-off-openroom_hm_borg1_20260819_074112.report.txt`,
  and their adjacent contact sheets. A separate spectator pass shows two
  player models together on the lower platform, confirming bot presence while
  the match remained live; it is not close-up or crosshair proof. Evidence is
  `scripts/output/hm-hotlog-off-followbot2_hm_borg1_20260819_075740.report.txt`
  and its adjacent contact sheet. The full shared-source SP relink then
  passed a 55-second `borg2` smoke with four coherent gameplay captures at
  approximately 83.5-89.8 FPS; evidence is
  `scripts/output/shared-hotlog-off-sp_borg2_20260819_074748.report.txt` and its
  adjacent contact sheet. A final 150-second moving/firing `hm_borg1` soak
  remained alive at the timed shutdown. Renderer draw and command counters
  advanced through the last sample, and the texture allocator stayed fixed at
  2,985,856 of 10,485,760 bytes used with 7,499,904 bytes both total and largest
  free. All fourteen captures were reviewed sequentially: the world, HUD,
  weapon, phaser beam, impact marks, and lighting remained coherent, with
  visible FPS ranging from 65.3 to 90.9. The scripted path spent too much of
  the run against nearby geometry and no bot is visually identifiable, so this
  is liveness/render/combat-effect evidence rather than bot-visibility proof.
  Evidence is
  `scripts/output/hm-v77-botcombat-soak_hm_borg1_20260819_140954.report.txt`
  and its adjacent contact sheet.

  Raw release SHA256 values are
  `34E30704B7C891FE5E68B65D2CA39ACA900ADB7563979C6B2B5D3B183FC63D7F`
  (`default.xbe`) and
  `2A3A75874C0D1DAC85EEBE3E1F8CB01EE7BCFF9576373C3CE50E6C60A76EB8FE`
  (`efmp.xbe`). The media-enabled files staged in
  `build/hardware/StarTrekEliteForceX-Beta-20260801` are
  `DDB812AEC497FE73859E77304F8BF330AE3997B2E553E86AA8748B61CCA83E72`
  (`default.xbe`) and
  `E5FEEBB4346E32080831A46F1AA2E026FEC2E7E00C3C05D982A4083280FC00FF`
  (`efmp.xbe`). This is a hardware stability candidate, not a claimed hardware
  FPS gain. The staging pass did not read, copy, or alter the protected
  `default.cfg`; its SHA256 remained exact before and after all builds, runs,
  cleanup, and staging. After the retail run, copy `ef_sp_log.txt` and
  `ef_mp_log.txt` back into the hardware stage and run
  `python scripts\verify_production_hardware_logs.py --stage build\hardware\StarTrekEliteForceX-Beta-20260801`
  to re-check the staged XBE/PK3 integrity, reject unexpected returned runtime
  marker files, and then check the returned log runtime build identity against
  the staged manifest, heartbeat progress, logged map markers,
  required mode markers, complete per-heartbeat `completedFrame`/`realtime`/
  `path`/`mem` telemetry, `path=1`, fatal/OOM markers,
  production-vs-diagnostic log shape, and the hotlog-off Holomatch diagnostic
  families. Only `ef_sp_log.txt` and `ef_mp_log.txt` should be copied back;
  launch markers and runtime command files must not accompany the returned
  logs. The filled observation must list tested maps that overlap the returned
  log's map evidence. This verifier does not replace the required visible FPS
  range or visual/stability observations. When a run has a prescribed
  representative map matrix, the same final command supports repeated
  `--required-sp-map` and `--required-mp-map` flags; those maps must appear in
  both the returned logs and `mapsTested`. The final command also supports
  `--min-sp-visible-fps` and `--min-mp-visible-fps` to enforce an explicit
  observed FPS floor from the filled observation, plus `--min-sp-free`,
  `--min-mp-free`, `--min-sp-largest-free`, `--min-mp-largest-free`,
  `--max-sp-used-delta`, and `--max-mp-used-delta` for heartbeat memory
  thresholds. `--require-evidence-files` requires each mode's observation to
  list at least one existing visual evidence file. Its JSON/text output
  includes SHA256 provenance for the returned logs, filled observation file,
  and any listed visual evidence files. The same verifier now accepts
  `--report-out <path>` so every final pass/fail/missing-log result can be
  written as a durable JSON qualification artifact next to the returned stage
  evidence; missing-log reports still include the staged XBE/PK3 integrity
  result and the requested map/FPS/memory/observation criteria, so package
  drift and the intended acceptance gates are visible before retail logs
  return. Missing-log reports now also verify the observation file's duration,
  FPS, booleans, required map list, and evidence-file references without
  requiring log-map overlap yet. Saved reports include a report type and
  schema version marker so
  older partial JSON outputs can be distinguished from current qualification
  artifacts; they also include verifier script SHA256, command-line
  provenance, loaded hardware manifest path/bytes/SHA256, and the resolved
  report path, so the exact acceptance command, staged manifest, and report
  artifact can be audited later. A local transfer-stage preflight
  now passes with
  `python scripts\verify_hardware_stage.py --stage build\hardware\StarTrekEliteForceX-Beta-20260801`:
  the preflight verifier also accepts `--report-out` for a standalone
  transfer-readiness JSON report with report type, schema version, verifier
  SHA256, command-line provenance, and resolved report path;
  `python scripts\qualify_hardware_stage.py --stage build\hardware\StarTrekEliteForceX-Beta-20260801`
  now runs both the preflight and final production-log gate with the current
  default acceptance criteria and writes a combined audit report. That audit
  report is schema v63 and includes an `acceptanceChecklist` covering transfer
  integrity, runtime build identity binding, SP/MP returned logs, required map
  coverage, `path=1` retail renderer heartbeat proof, memory thresholds, filled
  visual/FPS/gameplay observations, co-op split-screen/P2 observation, visual
  evidence files, current-schema/path-bound subreport provenance with exact
  verifier script SHA256 binding, and the final production verdict. The
  combined audit now requires at least 90 seconds of returned heartbeat elapsed
  time and at least 90 seconds of filled observation duration per mode by
  default; its representative observation map/FPS matrix is SP `borg2`, co-op
  `borg1`, and Holomatch `hm_borg1`, each with at least 30 visible FPS. The
  same audit also writes `retailContractEvidence` with SHA256
  provenance and summary sanity checks for the saved retail `jamp.xbe`
  renderer contract ledger, SP/Holomatch object comparisons, ABI
  enum/state/offset report, and runtime-structure comparison. The ledger is
  structurally validated too: every row must carry a retail renderer address
  range, name/object, high-confidence evidence citing retail/shipping `jamp`
  authority, and a non-empty contract list. The same audit now records a
  retail source contract with SHA256 provenance for `retail_renderer_contract.h`,
  all 11 active `code/renderer/retail_xbox/*_retail.cpp` modules,
  `code/x_exe/x_exe.vcproj`, and `scripts/build_xbox.ps1`; it fails if a retail
  module loses the contract include/namespace wrapper, if the XBE project drops
  one of those modules, or if the build script stops applying the retail ABI
  defines and legacy frame-path replacement filter. It now also
  writes `xemuEvidence` with SHA256 provenance and conservative liveness checks
  for the current saved XEMU SP `borg2`, co-op `borg1` split-screen, and
  Holomatch `hm_borg1` proof reports, contact sheets, and final register dumps;
  schema v63 also includes top-level audit script SHA256/argv provenance,
  records bytes/SHA256 for the child preflight and production report files,
  cross-checks expected child report type/schema constants against the verifier
  source files, and requires both child reports to name the same hardware
  manifest path/bytes/SHA256 and the same audited stage directory, with each
  child verifier's `argv` bound to the exact command line run by the combined
  audit,
  requires successful screenshots to have enough byte-size
  diversity, requires each contact sheet to be a PNG visual artifact with at
  least the default evidence byte count and 320x240 readable dimensions,
  requires each proof report's own `contact=` and `final_registers=` artifact
  links to match the audited files, requires each proof report's
  `proof_context` mode/map, declared duration, positive interval, and
  native-screenshot flag to match the configured evidence slot, and requires
  the co-op XEMU slot to include
  repeated P2 refdef and dual-player model-state telemetry. The co-op XEMU slot is
  intentionally explicit: historical cinematic/camera-ownership runs do not
  satisfy the current split-screen gameplay proof requirement. The current
  co-op proof is
  `scripts/output/shared-v77-coop-splitscreen_normal_20260819_162359.report.txt`
  with its adjacent contact sheet and final-register dump. That run reached
  visible two-player split-screen gameplay with two stacked HUD/viewports,
  10 successful changing screenshots from 171.7s through 260.1s, 18 P2 refdef
  samples, 18 dual-player model-state samples, alive-at-end, and a stable
  texture allocator; the older numeric `split=0/0/0/0` field stayed zero and is
  not used as the acceptance signal. The schema v63 audit also exposes
  `overallStatus` and a top-level `openAcceptanceItems` list so the current
  verdict and remaining proof gaps are visible without scanning the full
  checklist, and it requires co-op observed memoryLargestFreeMinimum/usedDelta
  values from the filled retail observation because co-op runs through
  `default.xbe` rather than a separate returned log. The production verifier
  now rejects zero-byte, non-visual-extension, or bad-signature observation
  evidence files, so every listed screenshot, photo, or video must exist,
  contain at least 1024 bytes by default, use a visual file extension, have a
  matching container signature, and, for still-image evidence with readable
  PNG, JPEG, BMP, or GIF header dimensions, be at least 320x240 by default.
  Production verifier schema v19 also records `modifiedUtc` beside path/bytes/SHA256
  provenance for the stage manifest, returned logs, filled observation JSON, and
  listed visual evidence files, and can optionally require the returned
  Holomatch log to pass the focused 4P split-screen verifier through
  `--require-hm-split-log`. The combined audit should now remain
  blocked only by missing returned retail logs/filled hardware observation, not
  by missing saved XEMU co-op proof;
  the hardware patch manifest path/bytes/SHA256 are reported, the staged
  XBE/PK3 hashes match `HARDWARE_PATCH_MANIFEST.json`, release artifact hashes
  match the manifest source hashes, no runtime source file under `code\` is
  newer than either the hardware patch manifest or the retained
  `build\release` XBEs, no runtime source or package script is newer than the
  retained `build\release\BaseEF\xbox0.pk3`/`xbox1.pk3` packages, both XBEs contain exactly the one-byte media-enable
  patch at the manifest offsets, no stale runtime markers or superseded
  full-stage manifest are present, the canonical controller config hash is
  unchanged, and the Holomatch package/architecture checker passes without
  allowing loose original-image or loose map-override stage fallbacks.
  Staging also extracts each XBE's `STEFX_RUNTIME_BUILD_ID` literal into the
  hardware manifest, and production log verification now fails if either
  `default.xbe` or `efmp.xbe` lacks a manifest `runtimeBuildId` or carries the
  wrong personality/log identity. Returned SP and Holomatch logs must contain
  the matching identity line before their runtime evidence can be accepted. The
  combined audit also rejects saved XEMU proof
  reports that predate newer runtime source files, newer retained
  `build\release` XBEs or `BaseEF\xbox0.pk3`/`xbox1.pk3` packages, newer runtime source files than the retained
  `build\release` XBEs themselves, or newer XEMU proof-harness/gate scripts;
  records path/bytes/SHA256 provenance for those scripts, including
  `build_xbox.ps1`, `build_xbox_patch_pk3.py`, and
  `check_mp_holomatch_ui.py`; and requires
  each proof report's recorded XBE runtime identity path, byte count, SHA256,
  and build-id literal to match the current retained release artifact for that
  mode, so emulator proof must be refreshed
  after renderer, UI, logging, binary, proof-harness, stage-preflight, or
  build-contract verifier changes. After a fresh
  `scripts\build_xbox.ps1 -Target spmp`, use
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\refresh_xemu_qualification_proof.ps1`
  to refresh the required SP `borg2`, co-op `borg1`, and Holomatch `hm_borg1`
  XEMU reports. The helper first runs the same release XBE freshness, release
  PK3 freshness, and runtime-ID preflight as hardware staging, then writes a
  `scripts\output\xemu_qualification_proof_refresh_*.json`
  manifest with those paths and run settings. Unless `-SkipAudit` is used, it
  immediately passes that manifest into the combined audit through
  `--xemu-refresh-report`. A later standalone audit auto-selects the newest
  `scripts\output\xemu_qualification_proof_refresh_*.json` manifest when no
  explicit XEMU report overrides are supplied. Schema v63 records that choice
  in `xemuProofRefreshReport.selectionMode` as `explicit`, `auto`,
  `disabled-by-explicit-proof-reports`, or `none`. Refresh manifest schema v3
  records the successful release freshness preflight command/output before the
  proof reports, including the build graph contract, release XBE freshness,
  release PK3 freshness, and per-XBE runtime build ID identity lines for
  `default.xbe` and `efmp.xbe`. Schema v63 rejects a refresh manifest with a
  missing or malformed `generatedAtUtc`, a missing or failed
  `releasePreflight`, selected proof
  reports newer than that timestamp, a different repo root or hardware stage,
  bad XBLog polling cadence, a proof slot shorter than its configured minimum
  duration, or a derived proof report path outside the audited repo; Schema v63 also
  requires manifest-selected reports to declare matching `proof_context`
  duration and interval values. The helper's
  manifest contract is covered by
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\refresh_xemu_qualification_proof.ps1 -SelfTest`.
  The SP/co-op and MP XEMU wrappers also fail before repack/launch if
  active `build\release` XBEs lack `STEFX_RUNTIME_BUILD_ID` or carry the wrong
  personality/log identity, and repack retained ISOs when their staged XBE/PK3
  payloads are missing, newer than the ISO, or not SHA256-identical to the
  current release artifacts. Direct `ja_xemu_smoke.py --require-runtime-xbe-id`
  calls enforce the same default/efmp personality and log-file identity. Direct
  `--xemu-sp-report`, `--xemu-coop-report`, and `--xemu-mp-report` overrides
  remain available for bounded diagnostics; the normal refreshed-proof path is
  `--xemu-refresh-report`, which also binds the release preflight output. The
  audit derives the adjacent contact sheet and final-register dump paths from
  each report name.
  The staging script refuses to create a new hardware transfer from stale
  `build\release` XBEs or release XBEs missing `STEFX_RUNTIME_BUILD_ID` before
  it clears old returned logs or writes a new patch manifest. The active
  `scripts\build_xbox.ps1 -Target sp` and `-Target spmp` paths also verify
  embedded runtime build IDs and source freshness for their generated release
  XBE(s) before packaging or returning success. The `-Target spmp` build now
  runs two target-scoped passes: a normal SP pass that refreshes `default.xbe`,
  followed by the SP-hosted Holomatch pass that emits `efmp.xbe`, so the shared
  active qualification cannot silently pair a fresh Holomatch XBE with a stale
  SP executable. The `spmp` asset phase now also refreshes both active release
  packages, `BaseEF\xbox0.pk3` for SP/co-op and `BaseEF\xbox1.pk3` for
  Holomatch, then asserts both package outputs are newer than runtime source
  and the package scripts, so the hardware stage cannot pair fresh XBEs with
  stale PK3s. This source-level build graph/package contract is covered by
  `python scripts\verify_build_xbox_contracts.py` and its `--self-test` mode;
  that verifier also protects the forced `xb_log.cpp` rebuild which refreshes
  the `STEFX_RUNTIME_BUILD_ID` `__DATE__`/`__TIME__` literal for each real
  `spmp` build, and verifies that `build_xbox.ps1` still applies the retail
  renderer ABI defines while filtering the legacy frame-path modules replaced
  by `code/renderer/retail_xbox`.
  staging records that verifier's path/bytes/SHA256, the checked build
  script's path/bytes/SHA256, command stdout/stderr, and exit code inside
  `HARDWARE_PATCH_MANIFEST.json`. The stage preflight report is schema v12 and
  rejects a missing, failed, stale, or hash-mismatched `buildScriptContract`
  record, and also reports/rejects stale release PK3 packages through
  `releasePackageFreshness`. Production verifier schema v19 carries that stage-integrity record
  into returned-log reports. Schema v63 also records the current build-contract verifier result
  inside retail contract evidence, so a broken active build graph fails before
  runtime proof can be accepted. Schema v63 also requires schema-v2
  SP/Holomatch object-compare reports with compare-script provenance, root/link
  input records, object-pair records, and per-current/donor-object file hashes.
  It rejects retained object-compare reports when any active retail renderer source,
  `code/x_exe/x_exe.vcproj`, `scripts/build_xbox.ps1`, or
  `scripts/compare_retail_renderer_objects.py` input is newer than the saved
  comparison report.
  When `-ReuseObjects` is used, `code\win32\xb_log.cpp` is still rebuilt so the
  `__DATE__`/`__TIME__` runtime build ID literal cannot be reused from an older
  object. Use
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\stage_hardware_pk3_test.ps1 -CheckFreshnessOnly`
  after rebuilding when you only want to confirm that the active build graph
  contract still holds, the retained release XBEs are newer than runtime
  source, both release PK3s are newer than runtime/package inputs, and both
  release XBEs have the required runtime identities before running the full
  stage. On failure, the release preflight reports XBE freshness, PK3
  freshness, and runtime build ID problems together so a single failed run shows
  the full stale-artifact set to rebuild.
  The staged observation template path/bytes/SHA256 are also reported before
  retail capture. `HARDWARE_OBSERVATION.json` now carries
  `observationSchemaVersion: 3`, and the preflight/production verifiers reject
  older SP/MP-only observation files after the co-op gate was added. The
  preflight also rejects a stale
  `HARDWARE_OBSERVATION.json` whose `manifestVersion` does not match the active
  hardware patch manifest or whose template schema lacks the required
  `mapsTested`/FPS/visual/control/gameplay fields. The staging script refreshes
  a missing, mismatched, or old-shape observation template before transfer.
  `HARDWARE_OBSERVATION.json` is staged beside the transfer package to capture
  the tested map list, optional evidence files, visible FPS range, observed
  memory free/largest-free/used-delta values, loading, HUD, lighting, controls,
  gameplay, stall recovery, co-op split-screen/P2 checks, and Holomatch
  bot/combat observations. This is transfer-readiness
  evidence only; retail runtime
  FPS/stability still requires the filled observation file and returned logs.

  The superseded XEMU-qualified v75 frame-phase diagnostic pair was previously
  staged in `build/hardware/StarTrekEliteForceX-Beta-20260801`. It keeps
  logging memory-only and uses a pulsed eleven-row upper-left overlay that separates
  total main-loop, input, menu/handoff, `Com_Frame`, its unowned remainder,
  server, client, game, renderer front end, renderer back end, draw-surfaces,
  swap, Present, Finish, audio, screen-draw, event-loop, command-buffer,
  client-preamble, client-tail, view setup, leaf marking, world traversal,
  dynamic polygons, projection, entity submission, surface sorting, and debug
  draw milliseconds. Two final rows split indexed draw work into state setup,
  push-buffer reservation, vertex packing, and index packing, then expose total
  `BeginPush` time, longest reservation, and counts above 1 ms and 10 ms. v60 fixed the diagnostic
  ownership bug that cleared audio time before the overlay could display the
  previous completed frame; v61 wires the previously dormant detailed
  front-end counters into the active retail-renderer source files. v62 adds the
  queue-reservation rows after XEMU captured intermittent 21-22-million-cycle
  `BeginPush` waits in Holomatch. v63 adds whole-scene render time plus explicit
  unowned front-end and back-end gaps, so hardware can distinguish time inside
  `RE_RenderScene` from work outside it and identify any still-unmeasured
  back-end interval. v64 removes the last three always-on deep-sample clocks:
  per-command backend timing, `qglFinish`, and `qglEndFrame`/Present now call
  `Sys_Milliseconds()` only on the one deep sample frame every five seconds.
  v65 removes the larger remaining source of diagnostic self-interference: the
  detailed overlay itself submitted thousands of glyph indices into every
  measured frame. A tiny FPS line is now continuous, while the detailed rows
  appear for two seconds every five seconds and display a frozen snapshot of
  the immediately preceding overlay-free frame. The snapshot is formatted
  once per pulse, so the values do not include the text workload they report.
  v66 also removes the periodic textual diagnostics from frame-diagnostic
  builds. The XEMU monitor reads the exported counters directly, so the
  ten-second allocator walk plus two large `CL_Frame` formats and the sampled
  1,536-byte renderer profile format were redundant self-interference. Counter
  collection, sample cadence, frozen snapshots, and the production build all
  remain unchanged.
  This is diagnostic-only measurement cleanup, not a production-path change.
  A matched diagnostics-free v67 production control was rebuilt sequentially
  from the same source and XDK 5558 toolchain. Raw SHA256 values are
  `DCE050D678ECCF4D8739F9A736B8BEFAE90B08A57FDE67D9DDDDAE0E3101BF5F`
  for `default.xbe` and
  `DAB61EDD954E03733C3B95B388C081526E1E7AD444C20B05262873F3D0810159`
  for `efmp.xbe`; preserved copies are
  `build/release/diagnostics/default-production-v67.xbe` and
  `build/release/diagnostics/efmp-production-v67.xbe`. The production SP
  `borg6` smoke remained visually coherent and live, with the small on-screen
  counter near 88 FPS in the fixed representative view. The production
  Holomatch `hm_dn1` smoke also remained live and coherent; a moving scan
  captured an open room at about 65 FPS, while static wall-facing captures
  were about 90 FPS and are not accepted as representative performance
  evidence. Reports are
  `scripts/output/production-v67-sp-matched_borg6_20260818_064747.report.txt`
  and
  `scripts/output/production-v67-hm-scanview_hm_dn1_20260818_070349.report.txt`,
  with adjacent contact sheets. Disabling frame diagnostics did not improve
  the matched XEMU control, so logging/instrumentation is not promoted as the
  hardware root cause. The v66 diagnostic stage remains installed for the
  required hardware phase photograph rather than being replaced by v67.
  Production v68 removes a separate normal-frontend hot path: the startup-only
  direct-map marker probe is now cached after its first result instead of
  reopening every candidate marker file from every `Sys_IsDirectMapBoot()`
  call. Explicit queued SP, co-op, and Holomatch launches still override that
  cached result. A 60-second normal-boot proof stayed at the intact main menu
  near 90 FPS, and a second 35-second proof held the log ring at 87 writes with
  zero additional writes after startup. Direct SP `borg6` then passed at
  approximately 85.7-87.4 FPS, and direct Holomatch `hm_dn1` passed at
  approximately 90.5-91 FPS. Reports are
  `scripts/output/directmap-cache-v68-normal_normal_20260818_080239.report.txt`,
  `scripts/output/directmap-cache-v68-logproof_normal_20260818_080434.report.txt`,
  `scripts/output/directmap-cache-v68-sp_borg6_20260818_080902.report.txt`, and
  `scripts/output/directmap-cache-v68-hm_hm_dn1_20260818_081307.report.txt`,
  with adjacent contact sheets for the visual runs. Preserved production XBE
  hashes are
  `320C83DDEA9A25D5E0D688204AC4E9F6DF86C6E0192538EE40207AEECD70934B`
  (`default.xbe`) and
  `B5D2F59FFF2672B07513336F9307D334A91D808CBB02B2DAEF0F0E772DC45B92`
  (`efmp.xbe`). The older v66 hardware diagnostic stage was superseded by v75
  after the post-v73 renderer-layout correction; v68 is not claimed as a
  hardware FPS gain until measured on the console.
  A fresh object comparison corrected an earlier false lead caused by using a
  modified local JA tree as the donor. Against the untouched Xbox donor under
  `C:\Programming\Tools\xboxrecomp-work\ja-mp-retail\clean-ja-final-donor`,
  `R_CullLocalBox` is 414 instructions in both builds. The production
  `_updateTextures`, `_updateShader`, `_updateMatrices`, and
  `dllDrawElements` routines are instruction-for-instruction matches in both
  SP and Holomatch. Do not alter those routines or culling speculatively. The
  reports are
  `build/analysis/retail-v68-clean-donor-sp-object-compare.json` and
  `build/analysis/retail-v68-clean-donor-hm-object-compare.json`.
  XEMU liveness/readability proofs passed input-free SP `borg6` and Holomatch
  `hm_dn1` for 48 seconds each, with coherent gameplay captures and advancing
  deep-sample counters. Those are instrumentation checks, not hardware
  performance evidence.
  Reports are
  `scripts/output/framephase-v61-sp-borg6_borg6_20260818_040706.report.txt` and
  `scripts/output/framephase-v61-hm-hm_dn1_hm_dn1_20260818_040950.report.txt`;
  contact sheets are the adjacent files ending in `_contact.png`.
  The v62 ten-row overlay proof is
  `scripts/output/framephase-v62-hm-queue_hm_dn1_20260818_043117.report.txt`
  with its adjacent contact sheet.
  The v63 eleven-row ownership proof is
  `scripts/output/framephase-v63-hm-owner-gaps_hm_dn1_20260818_045030.report.txt`
  with its adjacent contact sheet. The matched SP proof is
  `scripts/output/framephase-v63-sp-owner-gaps_borg6_20260818_045555.report.txt`.
  Both personalities remained live, retained coherent lightmapped gameplay,
  and showed all rows upright and readable.
  The v65 pulsed-overlay proofs are
  `scripts/output/framephase-v65-sp-pulsed-snapshot_borg6_20260818_053136.report.txt`
  and
  `scripts/output/framephase-v65-hm-pulsed-snapshot_hm_dn1_20260818_053418.report.txt`,
  with adjacent contact sheets. Both runs were input-free and alive at the
  deliberate harness stop; the sheets visibly contain both FPS-only and full
  snapshot phases.
  The superseding v66 counter-only proofs are
  `scripts/output/framephase-v66-sp-counter-only_borg6_20260818_055801.report.txt`
  and
  `scripts/output/framephase-v66-hm-counter-only_hm_dn1_20260818_060035.report.txt`,
  with adjacent contact sheets. Both remained live through deliberate shutdown,
  retained coherent lightmapped gameplay, showed both overlay phases, and
  advanced the directly polled sample serial without profile-string output.
  Binary inspection confirms neither v66 XBE contains the old
  `FRAME_HEARTBEAT` or frame-profile format marker, proving those constant-false
  paths were removed by the compiler rather than merely bypassed at runtime.
  Raw diagnostic XBE SHA256 values are
  `A0214CB5C16AFB1D89B9347BBCBEDA90D9E1A8EE60E2A83590821521157F6E3D`
  for `default.xbe` and
  `B43F8C6AD7D6D294DC361327CE9C8AAEA0D55F844749C9177BE5AC15668149A3`
  for `efmp.xbe`. The HDD/media-enabled staged hashes are
  `E2BD2C7BDDEE4E283873080E4E91962C166BE5F7668F32313E4AF1EFF079CAB4`
  and
  `26E60767D553A628164C7AB1C36F029291FC69C56350995587B778C2DC4377C2`.
  The exact staged payload passes the Holomatch/runtime architecture checker:
  no `codemp` dependency, shared `code/` renderer/audio/input ownership, zero
  loose MP map overrides, zero loose UI scripts, zero original image formats,
  and 5,405 DDS entries. Both staged hashes verify against the active manifest;
  no stale full-stage manifest remains.
  v75 refreshes the same diagnostic from the current post-v73 source, including
  the restored 16-byte shipping `msurface_t` stride. Input-free XEMU checks kept
  SP alive through the untouched `borg1` campaign crawl at approximately
  83-90 FPS and kept Holomatch alive in `hm_borg1` gameplay at approximately
  91-100 FPS. Reports and contact sheets are
  `scripts/output/stefx-v75-sp-framephase_borg1_20260818_133138.report.txt` and
  `scripts/output/stefx-v75-hm-framephase_hm_borg1_20260818_133344.report.txt`.
  The staged HDD/media-enabled SHA256 values are
  `4C8CC815619E2491CBCE6D9F0F62E1B79D703814E9F40999AFA91EA223AA6D99`
  for `default.xbe` and
  `C90741D4F6765F933C2835FF90B315224EBB6D3F55F2A5CA28697D4CDEC1E974`
  for `efmp.xbe`; the manifest version is
  `Beta-20260818-framephase-v75`. Binary inspection confirms the detailed
  `LP/IN/MM` overlay format is present in both staged XBEs and absent from both
  restored production XBEs.
  Hardware must now wait for and photograph the pulsed detailed snapshots from
  open, representative gameplay views in both personalities; whichever phase owns the roughly 200 ms
  frame determines the next fix. Sustained FPS must be read during the
  FPS-only interval because rendering the full rows can temporarily lower the
  live rate even though their frozen values remain unbiased. v59 also
  corrected diagnostic self-interference:
  the immediate-mode submission path now reads the CPU timestamp counter only
  during the one deep sample frame every five seconds, rather than on every 2D
  draw. That matters most for HUD and cinematic-crawl workloads and does not
  alter production builds.

  The preceding marker-free production v54 pair was staged in the same
  folder before this diagnostic iteration. Its manifest identified
  `Beta-20260817-retail-v54-pk3only`, zero diagnostic markers, and source SHA256
  `7E688B64C41C2AF5B385657864F733E41842D552322B9F3A8424EFB50654566E`
  for `default.xbe` and
  `CC9C895D5092B845392C1DB50D243C94D2895939129710EAA299ECC3310834AA`
  for `efmp.xbe`. The extracted files contain only the expected one-byte XISO
  media-enable patch. The extracted stage contains 58 payload files totaling
  1,767,444,361 bytes; redundant loose maps, models, textures, audio, video,
  UI, scripts, shaders, and debug captures are absent. Its source ISO SHA256 is
  `647CB77444455101B218CA9D014C841EB2ED6627415CA9D081B638675B781E13`.
  Production XEMU smokes stayed alive through deliberate
  shutdown on moving SP `borg6` and moving/firing Holomatch `hm_dn1`; reports
  are `scripts/output/retail-v54-production-sp_borg6_20260817_232737.report.txt`
  and
  `scripts/output/retail-v54-production-hm_hm_dn1_20260817_233009.report.txt`.
  A diagnostic moving `borg6` run covered 56-88 batches and 70-109 indexed
  submissions with zero finish/present wait and no texture-cache churn; its
  report is
  `scripts/output/retail-v54-sp-moving-profile_borg6_20260817_231657.report.txt`.
  The same marker-free ISO passed an immutable 120-second normal-boot smoke and
  remained at the intact main menu across six captures without harness input;
  see
  `scripts/output/retail-v54-pk3only-exact-iso_normal_20260818_000559.report.txt`
  and
  `scripts/output/retail-v54-pk3only-exact-iso_normal_20260818_000559_contact.png`.
  XEMU guest timing is not retail-hardware FPS proof, so no performance
  conclusion should be drawn until this exact staged pair is measured there.

- 2026-08-17 retail 2D projection adapter v50: Xbox `RB_SetGL2D` now uses the
  shipping projection order (`bottom=0`, `top=480`) and leaves the FakeGL/D3D
  viewport adapter to perform the screen-space inversion. This corrects the
  shared 2D coordinate system without applying the previously rejected blanket
  texture-V flip. The Xbox texture packer also preserves the fixed
  `gfx/2d/charsgrid_med` atlas at its required 256x256 dimensions instead of
  resizing it to 128x128. Native XEMU/LLE visual proof now shows upright
  Holomatch HUD/FPS elements in `hm_dn1`, an upright SP `borg1` intro crawl
  moving upward over four successive captures, and upright independent P1/P2
  HUDs after a complete 260-second co-op startup/gameplay run. Reports are
  `scripts/output/retail-v49-2d-projection-hm-proof_hm_dn1_20260817_182820.report.txt`
  `scripts/output/retail-v49-2d-projection-sp-scroll_borg1_20260817_183920.report.txt`,
  `scripts/output/retail-v50-hm-single-viewport-proof_hm_dn1_20260817_190208.report.txt`,
  `scripts/output/retail-v50-sp-gameplay-2d-proof_borg6_20260817_191604.report.txt`,
  and
  `scripts/output/retail-v50-coop-gameplay-2d-proof_normal_20260817_190502.report.txt`.
  The SP proof frames are
  `C:/Games/Emulators/Xemu/JACodex/screenshots/xemu-2026-08-17-18-40-16.png`,
  `...18-40-36.png`, `...18-40-49.png`, and `...18-40-54.png`. The clean
  Holomatch proof frame is `...19-03-21.png`; standalone SP gameplay proof is
  `...19-16-59.png`; the late co-op proof frames are `...19-08-56.png` and
  `...19-09-25.png`. The earlier Holomatch world-view
  partition was inherited co-op split state: both the direct harness and the
  built-in `efmp.xbe` startup now explicitly reset split-screen state before
  launching a match. Sequential XDK 5558 builds produced `default.xbe`
  (`E9A7DF13EAB004A9C8A8469561E3612D7406DAEDF78AD6D739B0272EBB422028`)
  and `efmp.xbe`
  (`9258050A2A2D427FE62FE9B0B9D9C24C2A997957DF18EA1BE6C4EED1E28F3AD9`).
  Retail hardware must still verify the visual fix and FPS before signoff;
  the final two-player XEMU frame showed about 10.5 FPS, so performance remains
  an open qualification item rather than part of this visual pass.

- 2026-08-17 retail numeric/state-contract v45 hardware candidate: the
  compiler-driven shared-renderer audit now covers 52 record sizes, 220 hot
  field offsets, 135 enum values, and all 30 packed GL state masks. All 220
  field offsets, all 30 state masks, and 134 of 135 enum values match the
  clean retail donor exactly. The sole enum delta is the intentional terminal
  surface count: Elite Force appends `SF_MDR` after all 12 common retail
  surface IDs, so its dispatch table correctly contains 13 entries. Texture
  cache invalidation is also bounded to the two entries in the now-retail-
  exact `glstate_t`, closing a latent out-of-bounds state write. Sequential
  XDK 5558 builds produced `default.xbe`
  (`503C43DFFF5D0F8C2313688BCABA125E9E80B047F885E0AD7539C6AE48585E6C`)
  and `efmp.xbe`
  (`70AE58AD29EB6A88AB1A5A2C574C1469E4124FF5A4CB4803F371045F0D7CFF84`).
  XEMU/LLE visual gates passed SP `borg6`, moving/firing Holomatch `hm_dn1`,
  and moving/firing CTF `ctf_breach`; all remained alive through deliberate
  shutdown with coherent
  world geometry, lightmaps, materials, weapons, effects, and HUD output.
  Static SP captures showed approximately 80-86 FPS, while the moving/firing
  Holomatch captures showed approximately 87-89 FPS. These remain emulator
  regression checks rather than retail-hardware performance proof. Reports
  are
  `scripts/output/retail-contract-v45-sp-borg6_borg6_20260817_133446.report.txt`
  and
  `scripts/output/retail-contract-v45-hm-dn1-retry_hm_dn1_20260817_134057.report.txt`;
  the CTF report is
  `scripts/output/retail-contract-v45-ctf-retry_ctf_breach_20260817_141907.report.txt`.
  Its six changing-view captures showed approximately 81-91 FPS with coherent
  shuttle-bay/corridor geometry, lightmaps, pickups, weapons, and CTF HUD.
  A follow-up dynamic SP pass moved from the initial corridor into a populated
  room with NPC and portal/effect rendering at approximately 88-91 FPS, then
  remained alive after the scripted input stopped against a column. Its report
  is
  `scripts/output/retail-contract-v45-sp-dynamic-final_borg6_20260817_142701.report.txt`.
  The first Holomatch launch was discarded because XEMU never exposed its
  monitor socket and did not reach the game. The contract report is
  `build/analysis/retail-v45-renderer-abi-enums-state.json`. The shipping-XBE
  comparison now also reports a layout-independent instruction-structure
  score in `build/analysis/retail-v45-runtime-compare-structure.json`. It
  confirms that `dllDrawElements`, `R_DrawElements`, and `RB_DrawSurfs` are
  structurally exact; `RB_BeginDrawingView` is 99.5%, `R_AddDrawSurfCmd` is
  96.7%, and `RB_RenderDrawSurfList` is 92.6%. `R_IssueRenderCommands` is
  source-identical despite differing generated zeroing and command-buffer
  offsets. `RB_SetGL2D` remains the meaningful adapter outlier and owner of
  the separately logged 2D issue; do not apply another blanket texture-V
  flip without visual proof. A complete hot-path closure audit found no extra
  draw replay, hidden `BlockUntilIdle`, or duplicate presentation in normal
  gameplay. The BeginScene/EndScene/Present chain and principal QGL/D3D
  submission wrappers follow the shipping operation shape. Shipping D3D8
  5558 QFE4 versus local clean-SDK QFE1 differences are limited to private
  device offsets, relocations, and shader snapshot bookkeeping across 164
  comparable runtime symbols; transplanting the embedded QFE4 binary is not
  justified. Hardware must
  still establish performance against the prior roughly 5-8 FPS result. The
  single hardware folder is staged as `Beta-20260817-retail-contract-v45`;
  media-enabled hashes are
  `B7C46AE8527F6BE1247BD8110A86112EECA4D38176481C014C816A58C122DC77`
  and
  `7A8597704B72E2C9F6DE01508A43FDFBDC4C60421554639DBFE574CDCCFE9C77`.
  The known 2D corruption issue remains open. Rough overall retail-renderer
  parity remains approximately 79%.

- 2026-08-17 retail enum/field-contract v44 hardware candidate: the shared
  renderer now preserves the shipping Xbox numeric identities for common
  surface, model, render-command, and fog-policy enums while appending the
  Elite Force-only values after the retail ranges. A compiler-driven audit
  verifies all 220 measured hot renderer field offsets against the donor;
  all 220 match exactly. The broader size audit remains 41 of 52 exact, with
  the 11 differences classified as required Elite Force scene/BSP records,
  split-screen state, or deliberate command/storage capacity differences.
  Sequential XDK 5558 builds produced `default.xbe`
  (`624089516FCB3389C76D1E3457AE1F8045A3A1269DCC32F2DFA3342971C10E96`)
  and `efmp.xbe`
  (`36AF459EAD5D374FD73B347BDD7FD279F3E0D99C403B6C123DE07E33F56CBAD6`).
  XEMU/LLE visual qualification passed SP `borg6` and Holomatch `hm_dn1`;
  both remained alive through deliberate shutdown with coherent geometry,
  lightmaps, materials, weapon, and HUD output. Static-view counters were
  approximately 31-63 FPS in SP and 90 FPS in Holomatch; these are regression
  checks, not retail-hardware performance proof. Reports are
  `scripts/output/retail-enum-v44-sp_borg6_20260817_124959.report.txt` and
  `scripts/output/retail-enum-v44-hm-clean_hm_dn1_20260817_130609.report.txt`.
  The ABI report is
  `build/analysis/retail-v44-renderer-abi-offsets.json`. Hardware must still
  establish performance against the prior roughly 5-8 FPS result, and the
  known 2D corruption issue remains open. Rough overall retail-renderer
  parity is approximately 79%.

- 2026-08-17 retail color-generation contract v42 hardware candidate: Xbox
  builds now omit the unused PC-only `CGEN_SKIP` enum slot, restoring the
  shipping renderer's numeric values for every subsequent color generator
  while preserving the PC definition. No packaged Elite Force shader uses
  `rgbGen skip`, and the token had no Xbox parser or runtime consumer. The
  31-object comparison improved to 2,011 common functions, 1,763 (87.7%) with
  matching instruction counts, and 1,497 (74.4%) exact. `NeedVertexColors`
  is newly instruction-identical and `ComputeColors` now has the retail
  instruction count. Sequential XDK 5558 builds produced source `default.xbe`
  (`2B55ACB925A8339CB9867D8D372B47E0C028A2129708F3F90B3565614B182834`)
  and `efmp.xbe`
  (`118D91519983417ABA4B2CBB6A76AAF13D730B9E98380AA2FF9D3C641E9AB340`).
  XEMU/LLE visual qualification passed SP `borg6` and Holomatch `hm_dn1`;
  both remained alive through deliberate shutdown with coherent geometry,
  lightmaps, materials, weapon, and HUD output. Static-view counters were
  approximately 76-92 FPS for SP and 90 FPS for Holomatch, which is regression
  evidence rather than retail-hardware performance proof. Reports are
  `scripts/output/retail-colorgen-v42-sp_borg6_20260817_113859.report.txt` and
  `scripts/output/retail-colorgen-v42-hm_hm_dn1_20260817_114157.report.txt`.
  The one hardware folder is staged as
  `Beta-20260817-retail-colorgen-v42`; staged media-enabled hashes are
  `F050E23108A3734FE5BE7CC6E6AD7693D113AF6D425381D6791C0DE75F6F3BD3`
  and
  `CF3985D75DEB578CC77E614F7586A6F0D31F6A0B59DFEE03E0D8065FA0618E65`.
  Continue the retail enum/value and field-offset audit; hardware must still
  establish FPS against the prior roughly 5-8 FPS result, and the known 2D
  corruption issue remains open. Rough overall parity remains about 76%.

- 2026-08-17 retail private-ABI v41 hardware candidate: the shared renderer
  now matches the shipping Xbox `glstate_t` exactly (32 bytes, two hardware
  texture units) and removes five unused pointer fields from
  `shaderCommands_t`, restoring its exact 132,056-byte retail layout.
  Compiler-driven ABI comparison now finds 41 of 52 measured renderer records
  exact. The remaining 11 differences are classified Elite Force scene/BSP
  interfaces or deliberate command/storage capacities; they must not be
  overwritten with Jedi Academy layouts. The 31-object machine comparison
  improved to 2,011 common functions, 1,762 (87.6%) with matching instruction
  counts, and 1,496 (74.4%) with exact normalized instruction text. Exact
  gains appear across backend, image, shade calculation/submission, sky,
  surface, and D3D wrapper objects. Sequential XDK 5558 builds produced source
  `default.xbe`
  (`30F3C90AAADCDC2403992EAD11D6B6B32F96FE95CAB872BBFC2FE91F400FF593`)
  and source `efmp.xbe`
  (`5E4232BD0EAB441CFDF30CC260B2DF4890A4A866346688A129FDE0547DC983C9`).
  Native XEMU/LLE visual qualification passed in SP `borg6`, Holomatch
  `hm_dn1`, and CTF `ctf_breach`; all remained alive with coherent lightmaps,
  geometry, materials, models, weapons, and HUDs. Fixed-view counters were
  approximately 88-90 FPS, which is regression evidence rather than retail
  hardware performance proof. Reports are
  `scripts/output/retail-private-abi-v41-sp_borg6_20260817_111943.report.txt`,
  `scripts/output/retail-private-abi-v41-hm_hm_dn1_20260817_112217.report.txt`,
  and
  `scripts/output/retail-private-abi-v41-ctf_ctf_breach_20260817_112451.report.txt`.
  The single hardware folder is staged as
  `Beta-20260817-retail-private-abi-v41`; staged media-enabled hashes are
  `54E42BAC6D031D3BC99471E2A018717F6B0CEEA77000D431D0405BBA7897623A`
  and
  `9A61570E6889E89CF942A9217B65A7583F97F8AA44CCB680981B67E432707A48`.
  Hardware must still establish FPS against the prior roughly 5-8 FPS result;
  the known 2D corruption issue remains open. Rough overall retail-renderer
  parity is now approximately 76%.

- 2026-08-17 retail shader ABI v40 hardware candidate: the shared Xbox
  renderer now uses the shipping `textureBundle_t` (24 bytes),
  `shaderStage_t` (112 bytes), and `shader_t` (156 bytes) layouts. PC-only
  video-map, specular, and dynamic-glow fields no longer shift the retail
  shader records consumed by the linked Xbox modules. Compile-time size
  checks enforce those contracts. Ten legacy renderer objects that supplied
  no symbols to either XBE were removed from the project; retail modules now
  have unambiguous ownership of the backend, command, lighting, scene, shader,
  sky, and world paths while the Elite Force surface extension remains.
  The 31-object comparison improved to 2,009 common functions, 1,760 (87.6%)
  with matching instruction counts, and 1,470 (73.2%) with exact normalized
  instruction text. `R_CopyStage` is newly exact and `CollapseMultitexture`
  now matches the donor instruction count. Sequential XDK 5558 builds produced
  source `default.xbe`
  (`77B671BC94EAE2D3B6D9699B8A9BC94B20C17057CA2488F94E50FE204A71D397`)
  and source `efmp.xbe`
  (`86D3E4DD7BF78F742865451C9319C845D4F18784A011228B63370B09A04C6512`).
  Native XEMU/LLE visual qualification passed in SP `borg6`, Holomatch
  `hm_dn1`, and CTF `ctf_breach`; all remained alive through deliberate
  shutdown with coherent lightmaps, world materials, models, weapons, and
  HUDs. The static-scene on-screen counters were approximately 82-90 FPS,
  which is regression evidence rather than retail-hardware performance proof.
  Reports are
  `scripts/output/retail-shader-abi-v40-sp_borg6_20260817_103153.report.txt`,
  `scripts/output/retail-shader-abi-v40-hm_hm_dn1_20260817_103548.report.txt`,
  and
  `scripts/output/retail-shader-abi-v40-ctf_ctf_breach_20260817_103839.report.txt`.
  The exact pair is staged in the one approved hardware folder as
  `Beta-20260817-retail-shader-abi-v40`; staged media-enabled hashes are
  `82D88EA46365AE2292D8C8DE4CC2E30320057A0405A0002BC7299A744684469A`
  and
  `EDAF1660CBC293015CA82911BEB47A830BB4D6A1AF0E432D8BC3B74451B55011`.
  Hardware must still establish the performance effect against the prior
  roughly 5-8 FPS baseline; the known 2D corruption issue remains open.

- 2026-08-17 retail runtime vertex-contract v39 hardware candidate: the
  shared renderer now uses the shipping Xbox `drawVert_t` contract end to
  end instead of retaining the PC/early-port layout behind retail surface
  assembly. Runtime positions and normals are floats, texture/lightmap
  coordinates use the shipping 1/512 scale, and four lighting styles use
  two packed bytes each. BSP conversion remains Elite Force-owned; its map
  shorts are expanded once while loading rather than once per submitted
  frame. `RB_SurfaceTriangles` and `RB_SurfaceGrid` fell from 207/353 to the
  donor's exact 200/344 instruction counts. The complete 31-object comparison
  now finds 2,009 common functions, 1,759 (87.6%) with matching instruction
  counts, and 1,469 (73.1%) with exact normalized instruction text.
  Sequential XDK 5558 builds produced source `default.xbe`
  (`D91C8622AB139E3191C0DA5CE9FA07C2414760D9F3F7D94333D4A565A4EC476F`)
  and source `efmp.xbe`
  (`E312FC6F4ECEAAB27ADB0A66999C0E482A75D151708F4018B9C1E45888F8C919`).
  Native XEMU/LLE visual qualification passed in SP `borg6`, Holomatch
  `hm_dn1`, and patch/triangle-soup-heavy CTF `ctf_breach`; all three remained
  alive through deliberate shutdown with coherent world geometry, lighting,
  models, weapons, effects, packed colors, and HUDs. Reports are
  `scripts/output/retail-vertex-contract-v39-sp_borg6_20260817_091811.report.txt`,
  `scripts/output/retail-vertex-contract-v39-hm-dn1_hm_dn1_20260817_092514.report.txt`,
  and
  `scripts/output/retail-vertex-contract-v39-ctf-breach_ctf_breach_20260817_092833.report.txt`.
  A separate normal-boot run also reached and held the SP main menu with
  coherent LCARS panels, fonts, contextual button art, and galaxy imagery:
  `scripts/output/retail-vertex-contract-v39-normalboot_normal_20260817_094053.report.txt`.
  This is regression evidence only; the previously reported garbled 2D issue
  remains open until hardware/user confirmation.
  The exact pair is staged in the one approved hardware folder as
  `Beta-20260817-retail-vertex-contract-v39`; staged media-enabled hashes are
  `E6194D8BC586FDEAF0978C07F2F3E6B7CDE5D1E4DFC37A1C0DD71E142FA8E62D`
  and
  `32AC73AC7C5C6C083C25CCFEBEFF50D9D3150F4BE0359D8831E2262EDAD4C180`.
  Hardware must still establish whether removing the sustained vertex
  conversion work materially improves the prior roughly 5-8 FPS baseline.

- 2026-08-17 retail texture-contract v38 hardware candidate: the wholesale
  retail Xbox shader module is now active for both SP/co-op and Holomatch,
  with Elite Force's `scripts/*.shader` filesystem contract preserved. The
  shared renderer now also follows the shipping Xbox texture policy: 1x
  anisotropy and `r_picmip 1` in both personalities. The previous Holomatch
  `r_picmip 0` split came from an early experimental renderer graft rather
  than the retail XBE or either shipping Xbox config. Sequential clean XDK
  5558 builds produced source `default.xbe`
  (`2FFC48E555AEED88030106BA8DAB19AF59B1359FD42C40CF43CAEE37D34CFA16`)
  and source `efmp.xbe`
  (`269B01BE58DE7913F173E848D303D67CD9C90DDAFE39C6821879EC999613663D`).
  Native XEMU/LLE visual qualification passed in SP `borg6` and Holomatch
  `hm_dn1`, with coherent lightmaps, multi-stage materials, weapons, effects,
  HUD, and continuous frame advancement. Reports are
  `scripts/output/retail-texture-contract-v38-sp_borg6_20260817_081112.report.txt`
  and
  `scripts/output/retail-texture-contract-v38-hm-dn1_hm_dn1_20260817_081544.report.txt`.
  The exact pair is staged in the one approved hardware folder,
  `build/hardware/StarTrekEliteForceX-Beta-20260801`, as
  `Beta-20260817-retail-texture-contract-v38`. Hardware must run SP and
  Holomatch for 90-120 seconds each and compare the on-screen FPS and stall
  behavior against the prior roughly 5-8 FPS baseline. Do not stack another
  speculative renderer change before that result; offline retail-parity and
  non-render bottleneck investigation may continue.

- 2026-08-17 shared MDR palette-cache v34 hardware candidate: Elite Force MDR
  models previously decompressed or interpolated their complete bone palette
  once per model surface. The 108 packaged MDRs average 22.15 bones and 3.98
  surfaces, with a worst case of 41 bones and 13 surfaces. A bounded eight-slot
  per-frame cache in `code/renderer/tr_animation.cpp` now prepares each
  entity/model/frame palette once and reuses it for that entity's sorted
  surfaces; skinning math and submitted vertices are unchanged. Sequential XDK
  5558 builds produced `default.xbe`
  (`A670EE013F1E192BBCE9D1DA84B484078AC35570CCE893A4D0D5DCD4AC8ACD12`)
  and `efmp.xbe`
  (`338C66DB44E58677EE3A5625B5A3DF2CA731F0426AA457F976D1CBFB122F16E5`).
  Native XEMU/LLE visual runs passed in SP `borg6`, Holomatch `hm_dn1`, and a
  moving/combat `hm_borg1` model exercise. Valid reports are
  `scripts/output/stefx-mdr-palette-v34-sp_borg6_borg6_20260817_050228.report.txt`,
  `scripts/output/stefx-mdr-palette-v34-hm_hm_dn1-valid_hm_dn1_20260817_050636.report.txt`,
  and
  `scripts/output/stefx-mdr-palette-v34-hm_hm_borg1-models_hm_borg1_20260817_050921.report.txt`.
  An earlier `hm_dn1` report at 05:04 reused the SP-entry ISO and is invalid.
  The smoke runner now records the retained ISO's map/personality/control
  profile and automatically repacks when a later request differs; generated
  cleanup preserves only that profile beside the one retained ISO.
  XEMU remained at the established approximately 19 FPS SP and 30 FPS HM
  scenes, so hardware must determine the CPU-side gain before another
  optimization is stacked on this candidate.

- 2026-08-17 shared sustained-frame telemetry cleanup v33: normal Release
  builds no longer timestamp every server simulation tick, write cgame/VM phase
  markers every rendered frame, or count visible/rejected leaves solely for
  split-screen diagnostics. The functional server catch-up counter and
  split-slot selector remain. Shipping-object comparison now gives
  `R_MarkLeaves` the retail-identical 105-instruction length in both SP and
  Holomatch. Sequential XDK 5558 builds produced `default.xbe`
  (`9FB1DB81AC1D78EE46B192D088D263DA4D2E50C166E74D5D9059072A9E9D93B3`)
  and `efmp.xbe`
  (`8C58C03BC4BF83FC8662A72167C09E7CB96D7FE5A79368FC400092AA4CED6BD8`).
  Sixty-second XEMU/LLE visual runs passed in SP `borg6` and active Holomatch
  `hm_dn1`; reports are
  `scripts/output/stefx-retail-frametelemetry-v33-sp_borg6_20260817_042708.report.txt`
  and
  `scripts/output/stefx-retail-frametelemetry-v33-hm_hm_dn1_20260817_042942.report.txt`.
  Fixed-scene overlays remained approximately 19 FPS SP and 30 FPS Holomatch,
  so no emulator performance gain is claimed. Stage v33 for hardware and
  continue the remaining retail renderer module reconstruction.

- 2026-08-17 shared frame-loop cleanup v32 checkpoint: normal Release builds
  now compile the dormant `Com_Frame`, `CL_Frame`, and `SCR_UpdateScreen`
  phase tracing, volatile profiler writes, and timestamp collection only under
  `STEFX_HW_FRAME_DIAGNOSTICS`. Functional direct-map, smoke-input,
  split-screen, FPS-overlay, heartbeat, client, audio, cinematic, and draw
  calls remain active. `Com_Frame` fell from 566 to 166 instructions and
  `SCR_UpdateScreen` from 267 to 43 in both SP and Holomatch objects; the latter
  also restored retail's null `EndFrame` timing outputs unless `com_speeds` is
  enabled. Sequential XDK 5558 builds produced `default.xbe`
  (`81E8E12E50D8E7AAC35FF00752269CA7309C056859336A0CE8F7B6BF51F2058F`)
  and `efmp.xbe`
  (`E50841D259D8A9D2542C8A79598F9789E05D5E284FC6D71BF0FF975CBF18C80E`).
  Sixty-second native XEMU/LLE visual runs passed in SP `borg6` and Holomatch
  `hm_dn1`, including coherent lightmaps, HUD, movement/input, and phaser-beam
  output. Reports are
  `scripts/output/stefx-retail-frameclean-v32-sp_borg6_20260817_041119.report.txt`
  and
  `scripts/output/stefx-retail-frameclean-v32-hm_hm_dn1_20260817_041348.report.txt`.
  Captured in-game overlays were approximately 19 FPS in SP and 30 FPS in
  Holomatch; these are emulator regression evidence only. Hardware remains the
  performance authority. Continue auditing always-on diagnostics in other
  shared sustained-frame paths before requesting the next bundled hardware
  pass.

- 2026-08-17 non-intrusive retail-parity v31 checkpoint: the always-on client
  frame profiler and texture-residency counters are now compiled only under
  `STEFX_HW_FRAME_DIAGNOSTICS`. Normal SP and Holomatch builds retain only a
  30-second heartbeat plus boot/map/crash breadcrumbs. No allocator behavior
  changed. The 31-object renderer comparison improved to 1,727 of 2,002 common
  functions (86.3%) at identical instruction length and 1,455 (72.7%) exact;
  QGL is 573 of 574 at identical length. Sequential XDK 5558 builds produced
  `default.xbe`
  (`1A9E9F89C9042A64B46B210322EDE14DBAC924E097679E12A21FCDDA45F85F5D`)
  and `efmp.xbe`
  (`D1E5A4221B22B90BCF2D4B920945C62262E47E512F8FAA8EC00269DCE8334090`).
  Sixty-second native XEMU/LLE visual runs passed in SP `borg6` and Holomatch
  `hm_dn1`; reports are
  `scripts/output/stefx-retail-allocator-clean-v31-sp_borg6_20260817_034122.report.txt`
  and
  `scripts/output/stefx-retail-allocator-clean-v31-hm_hm_dn1_20260817_034359.report.txt`.
  Guest overlays reached 103.3 and 127.1 FPS in captured open scenes, but those
  are emulator correctness evidence rather than retail-hardware performance
  claims. Stage this exact pair for hardware, then audit the oversized shared
  `Com_Frame`/`SCR_UpdateScreen` orchestration for the next non-render cost.

- 2026-08-17 aligned retail profiler v25 checkpoint: clean sequential XDK
  5558 builds produced `default.xbe`
  (`96FE2574E14D124E9C5BDFFB18FFD248FBCD134D2C30E163E823DFEE344032A2`)
  and `efmp.xbe`
  (`5FED2B19C7DA8B77F0A89C2C2D676FCA29D47BDE31E14FB8DBD26D9016734111`).
  Native XEMU/LLE proofs passed in SP `borg6` and a correctly repacked direct
  Holomatch `hm_dn1` run, both at approximately 90.9 guest FPS with coherent
  lightmaps, weapons, entities, and HUD output. The real `hm_dn1` trace found
  one intermittent `D3DDevice_BeginPush` wait of 22,196,166 cycles while
  reserving only 763 dwords; aggregate reservation time for that frame was
  22,692,344 cycles. Later frames with the same 537 input surfaces and
  192-193 indexed submissions reserved approximately 100,500 dwords in only
  0.49-0.65 million aggregate cycles. This rules out one oversized draw packet
  and directly identifies intermittent GPU/push-buffer back-pressure. Evidence
  is in
  `scripts/output/stefx-retail-profile-v25-sp-retry_borg6_20260817_010834.report.txt`,
  `scripts/output/stefx-retail-profile-v25-sp-retry_borg6_20260817_010834_xblog_profiles.log`,
  `scripts/output/stefx-retail-profile-v25-hm-repack_hm_dn1_20260817_011350.report.txt`,
  `scripts/output/stefx-retail-profile-v25-hm-repack_hm_dn1_20260817_011350_contact.png`,
  and its adjacent profile log. The single hardware folder is staged as
  `Beta-20260817-retail-profile-v25-xdk5558`. The build uses the retail
  `d3d8.lib`; `d3d8i.lib`, extra stage collapse, JA automap roof culling, and
  forced push-buffer drains remain rejected. The next investigation is the
  submitted pass/state distribution that can make the GPU consume an otherwise
  retail-shaped queue slowly.

- 2026-08-16 sustained-frame parity audit: FakeGL linkage, texture-pool churn,
  compiler mode, shader-loop inflation, executable image size, malformed or
  duplicated packed surfaces, and the apparent `R_CullSurface` size mismatch
  are now closed by direct evidence. The active path is native D3D8; texture
  swap/fetch counters remain zero; release objects use the full speed flags;
  shader and batch loops match retail; both runtime XBEs are smaller than
  shipping `jamp.xbe`; all 116 converted maps preserve source surface counts;
  and the removed culling body is only JA's slow automap screenshot roof
  tracer. The remaining renderer work is limited to live scene/entity ABI
  adaptations, backend state/2D visual parity, and the retail-hardware
  `BeginPush` distribution gate. Full reasoning is recorded in
  `notes/ja_mp_retail_renderer_re_2026-08-14.md`.

- 2026-08-16 retail world-traversal parity checkpoint: removed the remaining
  per-node, per-leaf, per-surface, and per-split-slot diagnostic writes from
  the active shared renderer while preserving functional split-screen slot
  selection. Clean sequential XDK 5558 builds produced `default.xbe`
  (`6C17E550EA14DD0172ECDCB55A1D9610F94E86A7B26044CEBA27B3E7BCCECBA3`)
  and `efmp.xbe`
  (`7FE15B66D21C9B66153D7129C3E4B622BA74E58C9623E6AF0E88526B939F8FF4`).
  Machine comparison against shipping `jamp.xbe` now reports exact
  `dllDrawElements`, exact `R_AddWorldSurfaces`, 0.9048 detail similarity for
  `R_MarkLeaves`, 0.8700 for `R_RecursiveWorldNode`, and 0.9322 for
  `R_RenderView`. `RB_RenderDrawSurfList` remains lower at 0.5814 detail
  similarity, but its source control flow matches retail except for required
  Elite Force render-flag ABI adaptations; copying JA's conflicting flag bits
  would be a regression rather than parity. A 70-second SP `borg6` visual run
  and a 90-second active Holomatch `hm_dn1` visual run passed with coherent
  worlds, lightmaps, weapons, HUDs, movement, bots, and weapon fire. The latter
  visual report is
  `scripts/output/stefx-retail-worldclean-hm_hm_dn1_20260816_215109.report.txt`.
  A later intrusive telemetry run appeared to stall after 67 seconds. That was
  reproduced as a harness fault: perf-only mode still translated and read all
  scattered diagnostic pages. Perf-only polling now reads only the compact
  heartbeat/class-state block and suppresses detailed telemetry. The corrected
  120-second `hm_dn1` repeat advanced continuously from frame 541 to 3543 with
  no stall or exception; its report is
  `scripts/output/stefx-retail-worldclean-hm-compactpoll_hm_dn1_20260816_215907.report.txt`.
  Quantized monitor-derived XEMU wall rate averaged 28.8 FPS and is diagnostic
  only. Retail-hardware sustained FPS remains the acceptance authority. Rough
  overall retail-renderer parity remains approximately 70%; native draw
  submission and hot world traversal are farther along, while backend state
  orchestration, shader/lightmap/2D edge cases, and hardware proof dominate the
  unfinished portion.

- 2026-08-16 packed-world map-name lifetime fix v23 hardware candidate:
  retail-style `.mle` package loading was intermittently invalidated because
  `CM_LoadMap`/`RE_LoadWorldMap` callers pass map names from the shared `va()`
  buffer, while the packed-lump probe performed nested filesystem and log
  formatting that reused that buffer. An older hardware log captured the map
  name changing from `maps/borg1.bsp` into a diagnostic string between probe
  calls, allowing the renderer to reject the packaged world and take the raw
  BSP fallback. `RE_LoadWorldMap_Actual` now owns one local map-name copy,
  probes packed-lump availability once, and reuses that result; the shared
  probe logs its own local copy with `XBLog_WriteCriticalf`. Clean sequential
  XDK 5558 builds succeeded for both executables. XEMU/LLE visual proofs passed
  in SP `borg6` and Holomatch `hm_borg1`; both rendered coherent lightmapped
  gameplay through deliberate harness shutdown. SP recorded 10 gameplay
  samples averaging 85.8 guest FPS (47.9 minimum), and Holomatch recorded 10
  averaging 90.6 (87.8 minimum). These are emulator correctness/stability
  results, not retail-hardware performance claims. Evidence:
  `scripts/output/stefx-packed-namefix-sp-borg6_borg6_20260816_135846_contact.png`,
  `scripts/output/stefx-packed-namefix-sp-borg6_borg6_20260816_135846.report.txt`,
  `scripts/output/stefx-packed-namefix-hm_hm_borg1_20260816_140224_contact.png`,
  and `scripts/output/stefx-packed-namefix-hm_hm_borg1_20260816_140224.report.txt`.
  The single PK3-only hardware folder is staged at
  `build/hardware/StarTrekEliteForceX-Beta-20260801` as
  `Beta-20260816-packed-namefix-v23-xdk5558`. Source SHA256 values are
  `E8E6D6E55E05DBD3DD2D73FB9291F6B12814EF8E4D1D3CA2A1C11D4D1CF307F5`
  (`default.xbe`) and
  `444A02A5EF49C288F42C9AFEB9CC4D02018DED3998B8E5582088200D363B32D6`
  (`efmp.xbe`); staged media-enabled hashes are
  `09BD21F1455A0FC9DFB7D9EA5EBE3B6FEC0E015248D57923F6A835D18B1385C7`
  and
  `FDCC997A1CE09B2266554E197D026A77F73EAA9E32354AD88FEB6B753B5006D1`.
  The remaining acceptance gate is a paired SP/Holomatch retail-hardware run
  from this exact folder, analyzed with
  `scripts/analyze_staged_hardware_profiles.ps1`; do not infer a hardware FPS
  improvement from XEMU. The converter/package audit also passed every
  available map: 83 campaign-side BSPs plus all 33 multiplayer BSPs (116
  total). It verified fixed-record alignment, raw/packed surface-count parity,
  light-grid bounds, light-array counts, and the expanded 16-bit shader-index
  fields. The largest source shader table was `ctf_breach` at 298 entries, and
  the largest packed lump was its 6,866,912-byte vertex lump. This removes a
  malformed packed map as an alternative explanation for the pending hardware
  result.

- 2026-08-16 retail batching/stage parity audit closed: active
  `RB_RenderDrawSurfList` uses the shipping JA Xbox shader/fog/dlight/entity
  batch-break contract without an additional Elite Force break condition.
  Active `RB_IterateStagesGeneric` was compared against shipping
  `jamp.xbe` at the instruction/control-flow level. Both iterate exactly
  `shader->numUnfoggedPasses`; retail advances a `0x70`-byte
  `shaderStage_t`, while Elite Force advances its required `0x78`-byte stage
  containing the additional EF fields. The remaining member offsets and GL
  flag values follow those two legitimate layouts. Therefore the measured
  78-276 indexed submissions per sampled frame are content/shader work, not
  an accidental extra-pass loop or premature batch flush. Do not collapse
  stages or broaden entity merging as a performance experiment: that would
  discard valid lightmap/effect passes and violate the retail renderer
  contract. The v22 hardware `BeginPush` distribution remains the next
  performance decision gate.

- 2026-08-16 `BeginPush` call-distribution profiler v22 hardware candidate:
  extended the aligned sampled-frame record with the maximum individual
  `D3DDevice_BeginPush` duration and reservation size plus counts exceeding
  100,000 cycles, 1 ms, and 10 ms. Clean XEMU/LLE proofs passed in SP `borg6`
  and Holomatch `hm_borg1` with coherent lighting, lightmaps, weapons, and
  correctly oriented HUDs. SP remained alive for 55 seconds at 74.5-90.9 guest
  FPS while submitting a median 268.5 draws per sampled frame; its median
  aggregate `BeginPush` cost was 697,255 cycles and median worst call was
  31,313 cycles. Holomatch made a median 210 submissions; its corresponding
  figures were 645,627 and 27,207 cycles. Neither personality recorded a call
  above 100,000 cycles. Thus XEMU shows distributed call cost rather than one
  push-buffer stall. Broader 70-second visual checks also passed on SP
  `forge5` and Holomatch `hm_dn1`. The settled `forge5` sample submitted a
  median 78 draws with a median worst `BeginPush` of 32,487 cycles; `hm_dn1`
  submitted a median 167 draws with a median worst call of 48,767 cycles.
  Both again recorded zero calls above 100,000 cycles and remained alive until
  deliberate harness shutdown. XEMU host-wall rates varied while polling and
  capturing, so they are not accepted as retail FPS evidence. The
  retail-hardware trace is the decision gate: a large maximum or threshold
  count identifies genuine GPU/back-pressure waits, while many uniformly small
  calls implicate per-call runtime overhead or another system bottleneck. The
  single PK3-only hardware folder supersedes v21 at
  `build/hardware/StarTrekEliteForceX-Beta-20260801` as
  `Beta-20260816-beginpush-stall-v22-xdk5558`. Source SHA256 values are
  `8C42BA5AC1CE92FC19F6BF3D2E5EAC8F9F9871111690196D891C6AF6EE8B6332`
  (`default.xbe`) and
  `DC790D5327096BC7FF1064FF4146B8CEB6B79C38E26BC7CC86F6DDC7D1CF08EB`
  (`efmp.xbe`); staged media-enabled hashes are
  `75111097F17007D3FB5A980821CA3AC526F755BCCD006E937DDBAC115AF59680`
  and
  `9CD90B450EA8601191F230FCC6D059152BE35442B1E2AE29CDE49A1A8026EA7B`.
  Evidence:
  `scripts/output/stefx-v22-beginpush-stall-hm_hm_borg1_20260816_120406_xblog_profiles.log`,
  `scripts/output/stefx-v22-beginpush-stall-hm_hm_borg1_20260816_120406_contact.png`,
  `scripts/output/stefx-v22-beginpush-stall-sp_borg6_20260816_121341_xblog_profiles.log`,
  `scripts/output/stefx-v22-beginpush-stall-sp_borg6_20260816_121341_contact.png`,
  `scripts/output/stefx-v22-beginpush-stall-sp-forge5_forge5_20260816_123558_xblog_profiles.log`,
  `scripts/output/stefx-v22-beginpush-stall-sp-forge5_forge5_20260816_123558_contact.png`,
  `scripts/output/stefx-v22-beginpush-stall-hm-dn1_hm_dn1_20260816_123237_xblog_profiles.log`,
  `scripts/output/stefx-v22-beginpush-stall-hm-dn1_hm_dn1_20260816_123237_contact.png`,
  and the adjacent report files.

  Extended stability qualification is also complete. One continuous 180-second
  SP `borg6` run and one continuous 180-second Holomatch `hm_borg1` run both
  stayed alive through deliberate harness shutdown with stable memory and
  intact visual output. The settled SP samples reported 81 median batches,
  102 median indexed submissions, and a 34,613-cycle worst `BeginPush`; the
  settled Holomatch samples reported 110 median batches, 180 median indexed
  submissions, and a 53,313-cycle worst `BeginPush`. Both retained zero calls
  above 100,000 cycles. Evidence is in
  `scripts/output/stefx-v22-longsoak-sp_borg6_20260816_125443.report.txt`,
  `scripts/output/stefx-v22-longsoak-sp_borg6_20260816_125443_xblog_profiles.log`,
  `scripts/output/stefx-v22-longsoak-hm_hm_borg1_20260816_125943.report.txt`,
  and
  `scripts/output/stefx-v22-longsoak-hm_hm_borg1_20260816_125943_xblog_profiles.log`.
  The smoke runner now clamps perf-only QEMU monitor polling to a five-second
  minimum because more aggressive polling can backlog monitor replies; final
  RAM/log extraction remains the authoritative profile source.

- 2026-08-16 v22 decision-gate audit: no newer retail-hardware log exists, so
  another source optimization is not justified before the staged v22 trace is
  collected. The active build already uses XDK 5558's retail `d3d8.lib`,
  cached `GlobalAlloc` general-zone/BSP/hunk residency, `/Ox /Ob2 /Oi /Ot /Oy`
  speed optimization, and a consistent `FINAL_BUILD` renderer ABI. The prior
  `d3d8i.lib` diagnostic reduced performance, and mixed XDKs, accidental debug
  compilation, logging, audio, whole-program optimization, push-buffer sizing,
  forced GPU drains, and malformed packed BSPs are already rejected by direct
  evidence. An apparent `R_CullSurface` machine-size mismatch is helper
  inlining only: the world traversal is effectively identical, and
  `R_AddWorldSurfaces`, `R_CullLocalBox`, `R_CullLocalPointAndRadius`, and
  `R_CullPointAndRadius` are machine-shape exact against retail JA. For the next
  hardware log, interpret `beginPushDetail=maxCycles/maxDwords/over100K/over1ms/over10ms`:
  nonzero long-call counters identify GPU/push-buffer back-pressure; uniformly
  small maxima with a large aggregate identify distributed D3D/API cost and
  make submission-count reduction the next coherent investigation. Do not
  revive any rejected candidate above without new contradictory evidence.

- 2026-08-16 retail push-reservation profiler v21 hardware candidate: split
  the aligned renderer sample's aggregate D3D reservation cost into
  `SetStreamSource`, `BeginPush`, and post-reservation pointer setup while
  preserving the retail-exact `dllDrawElements` algorithm. Clean sequential
  XDK 5558 builds succeeded for `default.xbe` and `efmp.xbe`. XEMU/LLE SP
  `borg6` and Holomatch `hm_borg1` proofs passed with coherent lightmaps,
  weapons, lighting, and correctly oriented HUDs. In SP, `BeginPush` consumed
  332,238 of 418,842 mean reservation cycles. In Holomatch, its median was
  491,044 of 534,304 cycles (approximately 92%); stream binding and our
  pointer setup were minor. This localizes the measured renderer hotspot below
  the retail-matched draw routine at the Xbox D3D push-buffer reservation
  boundary. The single PK3-only hardware folder is staged at
  `build/hardware/StarTrekEliteForceX-Beta-20260801` as
  `Beta-20260816-retail-push-reserve-v21-xdk5558`. Source SHA256 values are
  `8AAD5A9020F6D9AF16BB3CD9FD99EF2FABC5150216416CF543D567E304357119`
  (`default.xbe`) and
  `9D355B16EB2F0587CDAF5BE8EDC2C3146CA34512EA286C5F01BD3EF1618CA2F9`
  (`efmp.xbe`); staged media-enabled hashes are
  `CE2DDFA152A53EA64B3FF32CD165D23DC4B2FFBD60054E57B37E3FAB7154D853`
  and
  `8CA424605D369FDEF19026050317798041FADA59777D22A89B18E6C9A7C50F66`.
  Evidence:
  `scripts/output/stefx-v21-reserve-split-sp_borg6_20260816_114309_xblog_profiles.log`,
  `scripts/output/stefx-v21-reserve-split-sp_borg6_20260816_114309_contact.png`,
  `scripts/output/stefx-v21-reserve-split-hm_hm_borg1_20260816_114543_xblog_profiles.log`,
  and
  `scripts/output/stefx-v21-reserve-split-hm_hm_borg1_20260816_114543_contact.png`.
  Retail hardware must confirm whether the same `BeginPush` concentration
  appears during the approximately 5 FPS behavior; that trace is the next
  decision gate before changing push-buffer policy or D3D runtime contracts.

- 2026-08-16 aligned retail-renderer profiler v20 hardware candidate: restored
  one sampled frame every five seconds and moved its final record to the true
  post-`CL_Frame` boundary, so frontend, backend, and D3D submission counters
  now describe the same frame. The instrumentation is shared by SP/co-op and
  Holomatch and does no work on unsampled frames. Clean sequential XDK 5558
  builds succeeded for `default.xbe` and `efmp.xbe`. XEMU/LLE SP `borg6` and
  Holomatch `hm_borg1` proofs both passed with coherent lightmapped worlds,
  weapons, and correctly oriented HUDs. The representative SP sample submitted
  359 surfaces in 88 batches and 109 indexed draws; the heavier Holomatch
  samples submitted 1,047 surfaces in 152-160 batches and 268-276 indexed
  draws. Measured frontend traversal and CPU-side D3D preparation remained
  bounded, which shifts the approximately 5 FPS hardware investigation toward
  Xbox D3D execution/synchronization or another system-level stall. Evidence:
  `scripts/output/stefx-v20-aligned-profiler-sp_borg6_20260816_112340_xblog_profiles.log`,
  `scripts/output/stefx-v20-aligned-profiler-sp_borg6_20260816_112340_contact.png`,
  `scripts/output/stefx-v20-aligned-profiler-hm_hm_borg1_20260816_112627_xblog_profiles.log`,
  and
  `scripts/output/stefx-v20-aligned-profiler-hm_hm_borg1_20260816_112627_contact.png`.
  The single hardware folder is staged as
  `Beta-20260816-aligned-retail-profiler-v20-xdk5558`. Source SHA256 values are
  `79F79A70BABFBF1CF4D453E758F724EC47BFEDFCDA38CB04664FEBC890EC5E7C`
  (`default.xbe`) and
  `209FBEFF4BB13316B77CD4B3A4F069F0B03085B43285960D98178E660C96189B`
  (`efmp.xbe`); staged media-enabled hashes are
  `01234EE726E118EFF63BDD97FC0433A21684886AA2E5CC0FE023364B3089FB70`
  and
  `C4C648C1217D3C07A1DFEB22A2C4D6598CCD14634ABB577B846CF8FF6527504E`.
  Retail hardware sustained FPS and the new `STEFX_HW_RENDER_SAMPLE` records
  remain the acceptance gate.

- 2026-08-16 retail startup-order v17 hardware candidate: shipping retail
  `jamp.xbe` disassembly confirmed that Xbox startup configures the 1 MiB / 128
  KiB D3D push buffer before `Sys_Milliseconds`, `Win_Init`, and `Com_Init`,
  while the input subsystem owns the later process-wide `XInitDevices` call.
  Removed the merge-era pre-D3D gamepad/memory-unit initialization from the
  shared executable entry point and restored one-time device initialization in
  `IN_Init`. Clean sequential XDK 5558 builds succeeded for `default.xbe` and
  `efmp.xbe`. XEMU/LLE proofs reached and sustained coherent gameplay for 75
  seconds in SP `borg6` and Holomatch `hm_borg1`; both ended alive at boot
  phase `0x21D` (post-`Com_Init`). The Holomatch heartbeat remained active and
  its wall-clock emulation sample averaged 31.4 FPS, but XEMU timing is not a
  retail-hardware performance claim. Proof is in
  `scripts/output/stefx-v17-startup-order-sp_borg6_20260816_095737_contact.png`
  and
  `scripts/output/stefx-v17-startup-order-hm_hm_borg1_20260816_100119_contact.png`
  with their adjacent report files. The single hardware folder is staged as
  `Beta-20260816-retail-startup-order-v17-xdk5558`. Source SHA256 values are
  `507EB12E0EA5096FEAAA0BFF9B278E8CD5667A3178906EE7A282138BB5CF4033`
  (`default.xbe`) and
  `86B219BACEECA61290105F03E116F76CDC1F46C9B9E161D2F76125ABE6907E7A`
  (`efmp.xbe`); staged media-enabled hashes are
  `5DC342CC469C8A0EDF8B0A0CFF72B75E72F66EA44FB478A302AFDCC847A2F228`
  and
  `017C6E52BD16C0A5E725814357D32AD7496327A1E4125DBC859583DA5BD55598`.
  Retail hardware must still measure sustained SP and Holomatch FPS before
  this candidate is accepted.

- 2026-08-16 live renderer-resource audit: a final-memory scan of the v16
  Holomatch `hm_borg1` run found only `0x2CCF80` bytes (2,936,704 bytes,
  approximately 2.80 MiB) used from the 10 MiB static texture pool and
  `0x3E00` bytes (15.5 KiB) allocated from the 4 MiB skin pool. Texture-skin
  swaps, fetches, waits, reads, and writes were all zero. This rules out
  texture-pool exhaustion, skin swapping, texture disk traffic, and
  texture-manager GPU waits as the explanation for the approximately 5 FPS
  retail-hardware result. The compact evidence is
  `scripts/output/stefx-v16-hm-pool-ram-scan_hm_borg1_20260816_092504.report.txt`;
  visual context is
  `scripts/output/stefx-v16-hm-bootstate-visual_hm_borg1_20260816_091855_contact.png`.
  The disposable 64 MiB RAM image was removed after analysis. Investigation
  now continues at the renderer startup/device execution context and hidden
  synchronization contracts rather than texture residency.

- 2026-08-16 retail model-submission cleanup v16 hardware candidate: removed
  expired actor/LOD diagnostics from the shared renderer's live frame path.
  `R_ComputeLOD` now identifies Elite Force MDR actors by the already-known
  `MOD_MDR` model type instead of repeatedly searching model path strings, and
  keeps those actors at the required highest detail. `R_AddAnimSurfaces` keeps
  the existing coarse-radius fallback that prevents view-angle actor loss, but
  no longer performs dormant post-65-second pathname searches or exhausted
  diagnostic bookkeeping. The periodic LOD-cvar trace was also removed from
  `CL_Frame`; the FPS overlay itself is unchanged. Retail machine-code
  comparison improved `R_ComputeLOD` from 232 current instructions versus 80
  retail at 0.160/0.141 shape/detail similarity to 72 current instructions at
  0.684/0.566. `R_CullModel` remained 152/152 instructions at 1.0/0.980, so
  normal model culling did not regress. Comparison records are
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-default-v16-model-clean-20260816.json`
  and
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-efmp-v16-model-clean-20260816.json`.
  Clean sequential XDK 5558 builds succeeded for `default.xbe` and `efmp.xbe`.
  XEMU/LLE proofs kept SP `borg6` alive and coherent for 95 seconds and
  Holomatch `hm_borg1` alive under active bot combat for 105 seconds. A
  250-second exact-settings co-op qualification advanced through the complete
  campaign crawl, rendered its animated actors, and reached stable independent
  P1/P2 gameplay. An earlier co-op attempt that mixed the gameplay input
  harness into the intro sequence was discarded as invalid harness evidence.
  Proof is in
  `scripts/output/stefx-retail-model-clean-v16-sp_borg6_borg6_20260816_081802_contact.png`,
  `scripts/output/stefx-retail-model-clean-v16-hm-actor_hm_borg1_hm_borg1_20260816_082443_contact.png`,
  and
  `scripts/output/stefx-retail-model-clean-v16-coop-extended_normal_20260816_083416_contact.png`
  with their adjacent report files. XEMU timing is correctness evidence only;
  retail hardware FPS remains required. The single hardware folder is staged
  as `Beta-20260816-retail-model-clean-v16-xdk5558`. Source SHA256 values are
  `5A7F72B2384BDCC9CA51225639C35C7ADB5AE29D5152120468003B4D37AD83FD`
  (`default.xbe`) and
  `60BF62F18BF4F4AB351E130C32566AD36FB1CF424A66566909799CB270D9426E`
  (`efmp.xbe`); staged media-enabled SHA256 values are
  `0A1AFA99FE0265440A79578C52D9FDBD42FFE36E1603E754529CEA2BCFB3E332`
  and
  `7A915AB9F0F2E7D126375113F861BCF7D1F7EC2DEC7284E6438BD78F31C451A7`.

- 2026-08-16 retail Xbox SSE vector-math v15 hardware candidate: corrected
  six dormant Xbox SIMD gates in `code/game/q_shared.h` from the unused
  `_XBOX1` symbol to the real `_XBOX` build symbol. This restores the same
  `DotProduct`, `VectorSubtract`, `VectorAdd`, `VectorScale`, `VectorLength`,
  and `VectorLengthSquared` SSE implementations used by the shipping JA Xbox
  source, across the shared SP/co-op/Holomatch engine. A clean sequential XDK
  5558 rebuild materially converged the linked renderer toward retail JA MP:
  `R_DlightFace` changed from 82/236 retail/current instructions at
  0.151 shape / 0.107 detail similarity to an exact 82/82, 1.0/1.0 match;
  `R_TransformDlights` changed from 87/197 at 0.042/0.021 to an exact
  87/87, 1.0/1.0 match; `R_RecursiveWorldNode` improved from 289/260 at
  0.678/0.488 to 289/288 at 0.894/0.870; `SetFarClip` improved from 78/182
  at 0.123/0.115 to 78/72 at 0.933/0.853; and `R_SetupFrustum` improved from
  141/116 at 0.187/0.148 to 141/145 at 0.853/0.846. The same results are
  present in both executables. Comparison records are
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-default-v15-sse-20260816.json`
  and
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-efmp-v15-sse-20260816.json`.
  XEMU/LLE gameplay proofs passed for SP `borg6` and Holomatch `hm_borg1`:
  both remained alive with coherent lightmapped worlds, weapons, and HUDs.
  The SP/co-op intro crawl also remained visually correct and scrolled in the
  intended direction. A subsequent 245-second co-op pass reached gameplay and
  showed stable, independent P1/P2 cameras and HUDs without reuse of the
  preceding fullscreen draw. Proof is in
  `scripts/output/stefx-retail-sse-v15-sp_borg6_20260816_073923.report.txt`,
  `scripts/output/stefx-retail-sse-v15-sp_borg6_20260816_073923_contact.png`,
  `scripts/output/stefx-retail-sse-v15-hm_hm_borg1_20260816_074217.report.txt`,
  `scripts/output/stefx-retail-sse-v15-hm_hm_borg1_20260816_074217_contact.png`,
  `scripts/output/stefx-retail-sse-v15-coop_normal_20260816_074519.report.txt`,
  `scripts/output/stefx-retail-sse-v15-coop_normal_20260816_074519_contact.png`,
  `scripts/output/stefx-retail-sse-v15-coop-extended_normal_20260816_075045.report.txt`,
  and
  `scripts/output/stefx-retail-sse-v15-coop-extended_normal_20260816_075045_contact.png`.
  XEMU timing is not hardware acceptance evidence and an unrelated XEMU
  workload was active. The single hardware folder is staged as
  `Beta-20260816-retail-sse-v15-xdk5558`. Source SHA256 values are
  `7BD7B86F89877C5A9CEABF63FFE8E3700257751BC649E8CB5F8C53C9FA18B0D9`
  (`default.xbe`) and
  `1F9D52702D4378F0A9D31C92C4E9AC6C89B98B831C5E3CBCA9C459A769A3A647`
  (`efmp.xbe`); staged media-enable-patched hashes are
  `BA08E35429D787E1572AE5603C68438C4F79B2AF37FFEA6F004BDA14787F1533`
  and
  `6A418690770087DC5BD480753AD2A150E23FF47E45B2F52190851CC170FA9BA8`.
  Retail hardware must measure sustained SP and Holomatch FPS against the
  previous approximately 5 FPS result.

- 2026-08-16 retail entity/scene cleanup v14 hardware candidate: removed the
  obsolete entity, model, split-draw, cluster, surface, and view-weapon
  diagnostic branches from the shared production scene path while preserving
  the functional split-screen slot state, per-player entity filtering, two
  independent `R_RenderView` calls, and source-refdef restoration. In linked
  `efmp.xbe`, `R_AddEntitySurfaces` improved from 52/169 retail/current
  instructions at 0.344 shape / 0.299 detail similarity to 52/72 at
  0.742 / 0.581. `RE_AddRefEntityToScene` improved from 49/89 at
  0.536 / 0.406 to 49/55 at 0.673 / 0.519. `RE_RenderScene` improved from
  298/432 at 0.301 / 0.208 to 298/319 at 0.347 / 0.211. The exact or
  near-retail frame, submission, backend-command, and view-orchestration
  functions from v9-v13 did not regress. Comparison records are
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-efmp-v14-20260816.json`
  and
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-default-v14-20260816.json`.
  XDK 5558 XEMU/LLE proofs passed for SP `borg6`, Holomatch `hm_borg1`, and
  co-op split-screen. SP and Holomatch remained alive with coherent lightmapped
  worlds, weapons, and HUDs; Holomatch health changed from 113 to 105, proving
  live combat. The extended co-op proof reached gameplay and showed stable,
  independent P1/P2 cameras and HUDs without reuse of the preceding fullscreen
  draw. Evidence is in
  `scripts/output/stefx-retail-entity-clean-v14-sp_borg6_20260816_070106.report.txt`,
  `scripts/output/stefx-retail-entity-clean-v14-sp_borg6_20260816_070106_contact.png`,
  `scripts/output/stefx-retail-entity-clean-v14-hm_hm_borg1_20260816_070357.report.txt`,
  `scripts/output/stefx-retail-entity-clean-v14-hm_hm_borg1_20260816_070357_contact.png`,
  `scripts/output/stefx-retail-entity-clean-v14-coop-full_normal_20260816_071152.report.txt`,
  and
  `scripts/output/stefx-retail-entity-clean-v14-coop-full_normal_20260816_071152_contact.png`.
  XEMU timing is correctness evidence only because an unrelated XEMU workload
  was active. The single hardware folder is staged as
  `Beta-20260816-retail-entity-clean-v14-xdk5558`. Source SHA256 values are
  `C766CE26D6CCB65FD73E6311DD2CBA19A89CEDC1B186CF1728B7B5DFD694064F`
  (`default.xbe`) and
  `C00535F80EB620BC3B0AE904E850445B4FC79EDABFDF8F4D8F148D6721A75C86`
  (`efmp.xbe`); staged media-enable-patched hashes are
  `82DFAC90034203807194536734A141DD1CA914E96D936DF5DB8397944B9CF400`
  and
  `05CCD2292EC607A471B195C492B82D4D7AFA75AF5034C8C6AD49822F80FBEC21`.
  Retail hardware must still measure sustained SP and Holomatch FPS against the
  previous approximately 5 FPS result; no emulator timing is an acceptance
  claim.

- 2026-08-16 retail renderer profiler-removal v13 hardware candidate:
  removed the remaining production profiling subsystem from shared renderer
  frame setup/finish, world traversal, backend dispatch, D3D presentation, and
  blocking synchronization. Actual scene traversal, command dispatch, draw
  submission, `BeginScene`/`EndScene`, `Present`, and `BlockUntilIdle` behavior
  is unchanged. In linked `efmp.xbe`, `dllEndFrame` and `dllFlush` are now exact
  shipping-retail JA MP machine-code matches. `RE_BeginFrame` is the exact
  retail size and instruction count (285 bytes / 87 instructions) with 0.989
  shape / 0.966 detail similarity; `RE_EndFrame` is likewise the exact retail
  size and instruction count (147 / 36) with 0.972 / 0.917 similarity.
  `R_RecursiveWorldNode` improved from 0.518 / 0.377 to 0.678 / 0.488, while
  the exact draw-submit helpers, near-retail `R_RenderView`, and cleaned
  `RB_ExecuteRenderCommands` retained their v9-v12 results. Comparison records
  are
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-efmp-v13-20260816.json`
  and
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-default-v13-20260816.json`.
  Sequential 65-second XDK 5558 XEMU/LLE proofs passed for SP `borg6` and
  Holomatch `hm_borg1`; both remained alive with coherent lightmapped worlds,
  weapons, and HUDs, and Holomatch health changed from 112 to 103 during the
  capture sequence, proving live combat simulation. Evidence is in
  `scripts/output/stefx-retail-profiler-free-v13-sp_borg6_20260816_064758.report.txt`,
  `scripts/output/stefx-retail-profiler-free-v13-sp_borg6_20260816_064758_contact.png`,
  `scripts/output/stefx-retail-profiler-free-v13-hm_hm_borg1_20260816_065044.report.txt`,
  and
  `scripts/output/stefx-retail-profiler-free-v13-hm_hm_borg1_20260816_065044_contact.png`.
  XEMU timing is correctness evidence only while an unrelated XEMU workload is
  active. The single hardware folder is staged as
  `Beta-20260816-retail-profiler-free-v13-xdk5558`. Source SHA256 values are
  `1E412CF6F306CAAD0C54BA927EF6E9A89315D8F712D49247AE4263EF10C753B5`
  (`default.xbe`) and
  `2D10726BE2219C7CB5655CA8A8A4936CF3CCD3113FC8A5964DD4D0AD06C20E39`
  (`efmp.xbe`); staged media-enable-patched hashes are
  `BBF62FA8357785777DAAE84BFC24AF7FCBF54398848A7FDED8A80C4992FE9E60`
  and
  `BF8B5CB77978B1AA91AC4F28D5364FFE1F68420A8C7000F79BB21E6AA629BE2C`.
  Retail hardware must compare sustained SP and Holomatch FPS against the
  previous approximately 5 FPS result; no emulator timing is an acceptance
  claim.

- 2026-08-16 retail backend-command cleanup v12 hardware candidate: removed
  the sampled per-command `RDTSC` reads, branches, classification, and volatile
  timing writes from the active `RB_ExecuteRenderCommands` loop while retaining
  EF's command dispatch, scissor support, draw submission, buffer swap, and
  frame-total timing. In both linked XBEs the function fell from 638 bytes / 83
  instructions to 337 bytes / 24 instructions, against shipping retail JA MP's
  317 bytes / 28 instructions; retail similarity improved from approximately
  0.306 shape / 0.270 detail to 0.846 / 0.769. The six exact draw-submit helpers
  and near-retail `R_RenderView` body remain unchanged. Machine-code records are
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-efmp-v12-20260816.json`
  and
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-default-v12-20260816.json`.
  Sequential 65-second XDK 5558 XEMU/LLE proofs remained alive with coherent
  lightmapped worlds, weapons, and HUDs in SP `borg6` and Holomatch `hm_borg1`;
  Holomatch health changes also prove the game loop continued. Evidence is in
  `scripts/output/stefx-retail-backend-clean-v12-sp_borg6_20260816_063337.report.txt`,
  `scripts/output/stefx-retail-backend-clean-v12-sp_borg6_20260816_063337_contact.png`,
  `scripts/output/stefx-retail-backend-clean-v12-hm_hm_borg1_20260816_063610.report.txt`,
  and
  `scripts/output/stefx-retail-backend-clean-v12-hm_hm_borg1_20260816_063610_contact.png`.
  XEMU timing remains correctness evidence only while an unrelated workload is
  active. The single hardware folder is staged as
  `Beta-20260816-retail-backend-clean-v12-xdk5558`. Source SHA256 values are
  `ECE0966245896991ACE743F2E6A9BEEA05F49D18CFE448FF989F5C063F2BCF08`
  (`default.xbe`) and
  `87D6CD81A7FA0365082584868A246B569FECF120CCABC9D382728B29F017A0FA`
  (`efmp.xbe`); staged media-enable-patched hashes are
  `4C8CA69C1ECC4D31D68F8FB9F3134CBDE86EA612F9BF30829D09B77358DC9A64`
  and
  `2256486F4AED517E9A196A212DC81007C341EA8A32677E9010BEB57ED0CE793C`.
  Retail hardware must compare sustained SP and Holomatch FPS against the
  previous approximately 5 FPS result.

- 2026-08-16 retail view-orchestration cleanup v11 hardware candidate:
  removed sparse-profiler branches, cycle reads, and volatile phase/workload
  writes from the active retail `R_GenerateDrawSurfs` and `R_RenderView`
  production bodies. EF-specific portal handling remains intact. In the linked
  `efmp.xbe`, `R_RenderView` fell from 600 bytes / 177 instructions to the
  shipping retail size of 231 bytes / 59 instructions, with 0.983 shape and
  0.932 detailed similarity. The v9-exact draw-submit helpers remain exact.
  Machine-code comparison is recorded in
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-efmp-v11-20260816.json`
  and
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-default-v11-20260816.json`.
  Sequential 65-second XDK 5558 XEMU/LLE proofs remained alive and visually
  coherent in SP `borg6` and Holomatch `hm_borg1`; the Holomatch sequence also
  shows live bot damage. Evidence is in
  `scripts/output/stefx-retail-view-clean-v11-sp_borg6_20260816_062226.report.txt`,
  `scripts/output/stefx-retail-view-clean-v11-sp_borg6_20260816_062226_contact.png`,
  `scripts/output/stefx-retail-view-clean-v11-hm_hm_borg1_20260816_062459.report.txt`,
  and
  `scripts/output/stefx-retail-view-clean-v11-hm_hm_borg1_20260816_062459_contact.png`.
  An unrelated XEMU workload was active, so these are correctness/stability
  proofs only. The single hardware folder is staged as
  `Beta-20260816-retail-view-clean-v11-xdk5558`. Source SHA256 values are
  `BACC03A51A8569CAFEC1D7942B809A87A4B094F34E292D7531F6EF4BC91DFE6F`
  (`default.xbe`) and
  `810BA4431E1884012497D3C6F65A5C7D37FC5FAF9DEC703F83D459DA88148C8E`
  (`efmp.xbe`); staged media-enable-patched hashes are
  `8239AE8C406A1D20E08467CD7B6677A7DA9F682E5EEBFBE0718235A35F5A06F5`
  and
  `CECDBE4CB99CD1F8F0CAC02D3325F3184AAA7EF8346F2802A28BBBD53015FEE2`.
  Retail hardware must compare sustained SP and Holomatch FPS against the
  previous approximately 5 FPS result.

- 2026-08-16 retail draw-submission cleanup v9 hardware candidate: removed
  profiler timing, sampling branches, and volatile counter writes from the
  production `dllBeginEXT`, `dllDrawElements`, `dllEnd`,
  `renderObject_Light`, `renderObject_Bump`, `renderObject_Env`, and triangle
  strip submission paths while preserving their actual D3D8 push-buffer work.
  Retail-XBE shape comparison now reports 245 exact functions for `efmp.xbe`
  (up from 238) and 241 for `default.xbe`; the first six named hot functions
  above are each exact 1.0 shape/detail matches with identical byte and
  instruction counts to shipping JA MP. Comparison records are
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-efmp-v9-20260816.json`
  and
  `C:/Programming/Tools/xboxrecomp-work/ja-mp-retail/retail-vs-stefx-default-v9-20260816.json`.
  Sequential XDK 5558 XEMU/LLE checks remained alive for 65 seconds with
  coherent lightmapped worlds, weapons, HUDs, and stable output in SP `borg6`
  and Holomatch `hm_borg1`. Proof is in
  `scripts/output/stefx-retail-submit-clean-v9-sp_borg6_20260816_055840.report.txt`,
  `scripts/output/stefx-retail-submit-clean-v9-sp_borg6_20260816_055840_contact.png`,
  `scripts/output/stefx-retail-submit-clean-v9-hm_hm_borg1_20260816_060322.report.txt`,
  and
  `scripts/output/stefx-retail-submit-clean-v9-hm_hm_borg1_20260816_060322_contact.png`.
  An unrelated XEMU workload remained active during qualification, so these
  runs are correctness/stability evidence only and make no FPS claim. The
  single retained hardware folder is
  `build/hardware/StarTrekEliteForceX-Beta-20260801`, version
  `Beta-20260816-retail-submit-clean-v9-xdk5558`. Source SHA256 values are
  `62F14D683FA01CD6C32A1DC597C5F965FABE18E77C4DB8B7367EF35381B202A2`
  (`default.xbe`) and
  `03B07D28D36948B942B53FF4A193056BA907CA4589477DA06E6F54B03D5D76F6`
  (`efmp.xbe`); staged media-enable-patched hashes are
  `997BE0C57A68CF1B50615197B09CB580CB456A010329AE306DF0986441160721`
  and
  `5BFDB98BDDC105368094AD3C8E53BACE88153AAB3A6D6AC4AF5EC3E5FC26FD3E`.
  Retail hardware must compare sustained SP and Holomatch gameplay FPS against
  the previous approximately 5 FPS result before this candidate is accepted.

- 2026-08-16 sparse retail-hardware profiler v8 candidate: detailed renderer
  traversal, phase, and cycle accounting runs only on the first frame and once
  every ten guest seconds. This pass also gates previously missed `RDTSC` and
  volatile-counter work in the production light, bump, environment, triangle
  strip, `glFinish`, and `glFlush` paths. The real draw submissions and GPU
  waits are unchanged. Ordinary frames retain the lightweight heartbeat and
  completed-workload totals. Sequential XDK 5558 XEMU/LLE checks remained
  alive for 65 seconds with coherent world geometry, lightmaps, weapons, input,
  and in-game HUD in SP `borg6` and Holomatch `hm_borg1`; XEMU timing is
  correctness evidence only. Proof is in
  `scripts/output/stefx-sparseprof-v8-sp_borg6_20260816_051224.report.txt`,
  `scripts/output/stefx-sparseprof-v8-sp_borg6_20260816_051224_contact.png`,
  `scripts/output/stefx-sparseprof-v8-sp_borg6_20260816_051224_xblog_profiles.log`,
  `scripts/output/stefx-sparseprof-v8-mp_hm_borg1_20260816_052002.report.txt`,
  `scripts/output/stefx-sparseprof-v8-mp_hm_borg1_20260816_052002_contact.png`,
  and
  `scripts/output/stefx-sparseprof-v8-mp_hm_borg1_20260816_052002_xblog_profiles.log`.
  The one-folder hardware candidate is staged at
  `build/hardware/StarTrekEliteForceX-Beta-20260801` as
  `Beta-20260816-retail-gfx-dxt5-sparseprof-v8-xdk5558`. Source SHA256 values
  are
  `4AE37F93EAEAD03F94EF4E6FCD5C1742C4A480A3FC83648362A8E93F8A64303E`
  (`default.xbe`) and
  `497E0A1EE7A54A0B4DB48542058217CD9C86974C7E14581E8CAC947355190E47`
  (`efmp.xbe`); staged media-enable-patched hashes are
  `8F387BBDAF0AF48C9197117A67B0C29ED37BC0274D1A37CAB7D1CF65C2E36A3E`
  and
  `0CE4302035E3429AE937A129EF77CB418FBD8EC150A645339CD86D67F3138F9A`.
  The DXT-only package hashes remain
  `1F578927E1F0D9B18C17AAA706A8F29F7CE76025897BB578ADFC8BB57B7B0202`
  and
  `676786A32CD9E0C721E0277A675FC2FC8145055AF94249DD7F563BD1CE726A20`.
  Retail hardware must compare sustained FPS and sampled frame timings against
  the previous approximately 5 FPS result; XEMU timing is not acceptance data.

- 2026-08-16 partially resolved 2D rendering defect: the retail-style package
  conversion from linear BGRA32 to DXT5 removes the previously garbled atlas
  fragments from the in-game SP and Holomatch HUD paths. Nine-frame XEMU/LLE
  contact sheets show coherent HUD numbers, bars, frames, weapon art, and world
  rendering in SP `borg6` and Holomatch `hm_borg1`:
  `scripts/output/stefx-retail-gfx-dxt5-sp_borg6_20260816_040202_contact.png`
  and
  `scripts/output/stefx-retail-gfx-dxt5-mp_hm_borg1_20260816_040715_contact.png`.
  Both 60-second runs remained alive without exceptions. This is not yet proof
  for menus, the campaign crawl, loading typography, every 2D overlay path, or
  retail hardware. Keep those surfaces open and do not reapply the rejected
  global UV-flip experiments.

- 2026-08-16 retail-hardware frame-profile gate: the v6 diagnostic retains the
  complete v5 frame, frontend/backend, draw-cycle, and per-view workload data
  and now counts world BSP nodes, visible leaves, mark surfaces, duplicate
  surfaces, culled surfaces, submitted surfaces, and dynamic-light-tested
  surfaces. These are behavior-neutral counters reset once per frame; brush
  model work is excluded from the world-only cull/add totals. Sequential
  XEMU/LLE checks reached coherent, stable gameplay without exceptions in SP
  `borg6` and Holomatch `hm_borg1`. A settled SP sample reported 1 view, 52
  leaves, 359 input surfaces, 81 batches, 102 submissions, and world work of
  389 nodes / 52 leaves / 610 marks / 183 duplicates / 131 culls / 296 adds / 0
  dynamic-light tests. A settled MP sample reported 1 view, 142 leaves, 493
  input surfaces, approximately 77 batches, approximately 116 submissions, and
  world work of 685 nodes / 142 leaves / 1366 marks / 532 duplicates / 351
  culls / 483 adds / 0 dynamic-light tests. Both workload ledgers balance:
  marks equal duplicates plus culls plus adds. The known garbled 2D overlay
  defect remained unchanged. XEMU timing is correctness evidence only; it does
  not reproduce the retail slowdown. The next retail log must determine whether
  hardware sees similarly bounded workload. If it does, the remaining slowdown
  is per-operation or D3D submission cost rather than runaway BSP traversal.
  Complete v6 evidence is in
  `scripts/output/stefx-profile-v6-sp_borg6_20260816_024027_xblog_profiles.log`,
  `scripts/output/stefx-profile-v6-sp_borg6_20260816_024027_contact.png`,
  `scripts/output/stefx-profile-v6-mp_hm_borg1_20260816_024401_xblog_profiles.log`,
  and
  `scripts/output/stefx-profile-v6-mp_hm_borg1_20260816_024401_contact.png`.
  The one-folder hardware stage is version
  `Beta-20260816-retail-profile-v6-xdk5558`; source SHA256 values are
  `5E98CC8B900071003542E32F20931768882A612A46819C180F90E6A2F4BD1528`
  (`default.xbe`) and
  `7222405FC5C2F27A7006CBE04E1437F84546FDF4159DA872B78F833F17A344B0`
  (`efmp.xbe`); staged media-enable-patched hashes are
  `39B02A6385BEF61B418FAF5BD656DE22CE44C02E458AC05818B6D0958BE0C164`
  and
  `909736B00939259FB570A6D1D9CC512697A14E758BDD64F84E036207820B0758`.

- 2026-08-16 retail DXT5 package-parity candidate: shipping JA Xbox data uses
  DXT5 for nearly every alpha texture; its entire `base/gfx` set contains 426
  DXT1, 425 DXT5, and only one private pre-swizzled BGRA32 DDS. The previous EF
  candidate still forced 276 `gfx/` entries, including font atlases, crosshairs,
  and interface art, to conventional linear BGRA32 even though the JA upload
  path directly copies BGRA32 into swizzled texture memory. The shared package
  builder now converts those alpha assets to DXT5 instead. Fresh `xbox0.pk3`
  and `xbox1.pk3` validation reports identical texture profiles: 4524 DXT1,
  874 DXT5, 7 RGB565, and zero BGRA32 entries. Representative
  `gfx/2d/chars_big.dds`, `gfx/2d/crosshaira.dds`, and
  `gfx/interface/ammobar.dds` are DXT5 in both packages. This removes about
  5.8 MiB from each package and eliminates the known linear-BGRA32 contract
  mismatch. The strict package checker enforces the retail-compatible policy.
  Sequential 60-second XEMU/LLE proofs stayed alive and show coherent in-game
  HUDs in SP `borg6` and Holomatch `hm_borg1`. Reports and contact sheets are
  `scripts/output/stefx-retail-gfx-dxt5-sp_borg6_20260816_040202.report.txt`,
  `scripts/output/stefx-retail-gfx-dxt5-sp_borg6_20260816_040202_contact.png`,
  `scripts/output/stefx-retail-gfx-dxt5-mp_hm_borg1_20260816_040715.report.txt`,
  and
  `scripts/output/stefx-retail-gfx-dxt5-mp_hm_borg1_20260816_040715_contact.png`.
  XEMU timing is correctness evidence only; hardware FPS/profile and broader
  menu/loading/crawl 2D qualification remain open.

  The one-folder hardware candidate is staged at
  `build/hardware/StarTrekEliteForceX-Beta-20260801` as
  `Beta-20260816-retail-gfx-dxt5-full-xdk5558`. Source SHA256 values are
  `5B3921484ADF99BF252FFA4B5D60E5E9114CDF30E3C0DB1130EFCC9B1CC4D0C2`
  (`default.xbe`) and
  `FCF6B5927633A11CFBAEBFC65B7975B64FA91D1A5354B72C4875613B98938499`
  (`efmp.xbe`); staged media-enable-patched hashes are
  `423E9122D7BBC804885D00713F3BB7D1ACDBF5AD9DABDE9B22823E59BF0F775F`
  and
  `462E2FDB8D3C61D0FD8550B84A56A699792037C46A54E270F293F1237C82F34B`.
  The PK3 hashes are
  `1F578927E1F0D9B18C17AAA706A8F29F7CE76025897BB578ADFC8BB57B7B0202`
  and
  `676786A32CD9E0C721E0277A675FC2FC8145055AF94249DD7F563BD1CE726A20`.
  The bounded stage contains six files totaling 583,188,497 bytes; prior
  runtime logs were removed by the staging gate.

  The previous partial-conversion candidate passed independent Pillow decode
  checks across 16 representative EF alpha textures and sequential 60-second
  XEMU/LLE checks stayed alive with coherent 3D output in SP `borg6` and
  Holomatch `hm_borg1`; the known garbled 2D overlay defect was unchanged.
  Historical evidence:
  `scripts/output/stefx-retail-dxt5-sp_borg6_20260815_230936.report.txt`,
  `scripts/output/stefx-retail-dxt5-sp_borg6_20260815_230936_contact.png`,
  `scripts/output/stefx-retail-dxt5-mp_hm_borg1_20260815_231225.report.txt`,
  and
  `scripts/output/stefx-retail-dxt5-mp_hm_borg1_20260815_231225_contact.png`.
  The one-folder hardware stage is
  `build/hardware/StarTrekEliteForceX-Beta-20260801`, version
  `Beta-20260815-retail-dxt5-texture-pools-xdk5558`. Source SHA256 values are
  `FE1BDD615226C1CC505A9345150DEEB97D7598DE71F8E54892E3F4C6969E5003`
  (`default.xbe`) and
  `68BBB077F04BF243A0993EAE1B57F668CEE41A749A15F39A03C159FB264E7DC8`
  (`efmp.xbe`); staged media-enable-patched hashes are
  `CF7D34B1C9B5E610C3EA2A88575A3FF16A0DA57A64469AA9E035B3CDA0C68818`
  and
  `AA2E720777D9FAC318105C9A83EE7D50C7287E2578C7A9D1D44C056AEFA70805`.
  Retail-hardware FPS/profile and soak proof are the acceptance gate; XEMU
  timing is not used to claim a performance gain.

- 2026-08-15 complete hardware frame-profile candidate: retained hardware logs
  localize the low-FPS frame to approximately 49 ms of renderer frontend work
  plus 67-70 ms of backend work; server/gameplay is 2-5 ms, audio is 0 ms, and
  memory is stable. The active behavior-neutral instrumentation now divides the
  frontend into setup, PVS marking, world traversal, polygons, projection,
  entities, sorting, and debug work. It also divides the backend into
  draw-surface, swap, and other commands, with separate `BlockUntilIdle`,
  `Present`, and draw state/reserve/pack/index/submit counters. Sequential XDK
  5558 XEMU/LLE checks remained alive with coherent 3D output: SP `borg6`
  averaged 51.9 wall FPS and Holomatch `hm_borg1` averaged 36.3 wall FPS.
  Reports:
  `scripts/output/stefx-retail-full-profile-sp-borg6_borg6_20260815_220914.report.txt`
  and
  `scripts/output/stefx-retail-full-profile-mp-hm-borg1_hm_borg1_20260815_221330.report.txt`.
  The one-folder PK3-only hardware stage is
  `build/hardware/StarTrekEliteForceX-Beta-20260801`, version
  `Beta-20260815-retail-full-profile-xdk5558`. Source SHA256 values are
  `40D154E35AB3BD38AEC7FCBC36946BBF268B122E75FEB30B844FF8B2F345F4BA`
  (`default.xbe`) and
  `68BBB077F04BF243A0993EAE1B57F668CEE41A749A15F39A03C159FB264E7DC8`
  (`efmp.xbe`); staged media-enable-patched hashes are
  `E6D2E756DFA3E56E287B3FAE014ACCF80F06B1A4F53482694A51EAA20990857E`
  and
  `AA2E720777D9FAC318105C9A83EE7D50C7287E2578C7A9D1D44C056AEFA70805`.
  Run the returned SP or MP log through
  `scripts/analyze_hardware_profile.py`; it now names the dominant measured
  boundary and child phase. Retail-hardware FPS remains authoritative. CXBX-R
  is excluded because unmodified retail JA crashes there too.

- 2026-08-15 retail surface-ownership candidate: the shipping JA Xbox surface
  implementation now owns the live shared surface table and all common surface
  generation in both `default.xbe` and `efmp.xbe`. Elite Force keeps a narrow
  extension hook for its additional procedural entity types and MDR animation;
  the link maps prove that the old common surface implementations are no longer
  runtime dispatch owners. Fresh sequential XDK 5558 builds passed 60-second
  XEMU/LLE gameplay runs in SP `borg6` and Holomatch `hm_borg1`. Both remained
  alive with intact geometry, lightmaps, shaders, weapons, and HUD. Honest wall
  FPS averaged 28.6 (SP) and 29.7 (Holomatch); the guest counter is excluded
  because post-link telemetry relocation is stale. Evidence:
  `scripts/output/retail-surface-owner-sp_borg6_20260815_202752.report.txt`,
  `scripts/output/retail-surface-owner-sp_borg6_20260815_202752_contact.png`,
  `scripts/output/retail-surface-owner-mp_hm_borg1_20260815_203017.report.txt`,
  and
  `scripts/output/retail-surface-owner-mp_hm_borg1_20260815_203017_contact.png`.
  A separate 75-second `borg1` campaign-introduction run remained alive and
  exercised the broader cinematic draw, but reproduced the already-known
  corrupted/scattered font atlas output during the crawl. That visual defect
  remains open and the run is not presentation-parity proof. Evidence:
  `scripts/output/retail-surface-owner-sp-borg1-open_borg1_20260815_204847.report.txt`
  and its contact sheet.
  A follow-up telemetry-only rebuild restored completed-frame workload counters
  under the active retail command owner. The 40-second XEMU/LLE validation
  advanced throughout and reported coherent 768-surface, 120-121-batch,
  approximately 7.7k-vertex and 16k-index frames; it did not alter renderer
  decisions. Evidence:
  `scripts/output/stefx-retail-telemetry-restored-20260815_hm_borg1_20260815_211803.report.txt`.
  Current source XBE SHA256 values are
  `2DB0B6F13C51BA924DB4035F199C0C5B90E3DDC9E710C22E2686863F01BDAC72`
  (`default.xbe`) and
  `EFE67E3452643469E76808B802B76110ADE461F98260B3D3FF2868679ED9C854`
  (`efmp.xbe`). The one-folder PK3-only hardware stage is
  `build/hardware/StarTrekEliteForceX-Beta-20260801`, version
  `Beta-20260815-retail-surface-owner-telemetry-xdk5558`. Hardware FPS/profile and soak
  proof remain the acceptance gate. Do not use CXBX-R for this JA-derived
  renderer: the unmodified retail JA executable crashes there too.

- 2026-08-15 final-source XDK 5558 renderer candidate: fresh sequential SP
  and Holomatch builds passed 60-second XEMU/LLE gameplay runs in `borg6`
  and `hm_borg1`, with intact world rendering, lighting, HUD/weapons, advancing
  gameplay, and no crash or end-of-run stall. Visual contacts are
  `scripts/output/retail-pool-occupancy-sp_borg6_20260815_200424_contact.png`
  and
  `scripts/output/retail-pool-occupancy-mp_hm_borg1_20260815_200918_contact.png`.
  The corresponding reports are beside those contacts. Their classified guest
  FPS and direct allocator reads are not acceptance data because the XEMU
  monitor's post-link symbol relocation is stale; the observed wall rates
  under heavy monitor polling averaged 30.3 FPS (SP) and 31.6 FPS (Holomatch).
  Source XBE SHA256 values are
  `77B2DFCE882CC4F0B4921D40BF5D44D68C92DE9328517F5C03B4462308E58A5C`
  (`default.xbe`) and
  `AD4EDFE654B2C40776AB692B2FDA6A23C5C064BAFB783ADB1DD80AAF6A38B4C8`
  (`efmp.xbe`). The one-folder PK3-only hardware stage is
  `build/hardware/StarTrekEliteForceX-Beta-20260801`, version
  `Beta-20260815-retail-ja-renderer-xdk5558`. Hardware FPS/profile and soak
  proof remain the acceptance gate. Do not use CXBX-R for this renderer:
  unmodified retail JA also crashes there, so the same behavior from its
  renderer contracts is expected emulator incompatibility.

- 2026-08-15 retail skin-swap hardware profiling candidate: the shared texture
  manager now records cumulative player-skin swap, fetch, wait, bytes-written,
  and bytes-read counters in the existing five-second hardware profile line.
  `scripts/analyze_hardware_profile.py` reports first/last/delta/max values for
  those cumulative counters, allowing the next retail run to prove or reject
  skin-pool disk I/O as the source of the severe hardware stalls. This is
  evidence-only instrumentation; it does not alter renderer behavior. Fresh
  XDK 5558 builds passed matched 60-second XEMU/LLE gameplay runs with no
  exception or heartbeat stall: SP `borg6` averaged 27.2 wall FPS (10.3 min,
  29.9 max), and Holomatch `hm_borg1` averaged 27.4 wall FPS (19.9 min, 29.9
  max). Reports:
  `scripts/output/retail-skin-swap-profile-sp-final_borg6_20260815_191911.report.txt`
  and
  `scripts/output/retail-skin-swap-profile-mp_hm_borg1_20260815_191255.report.txt`.
  Source XBE SHA256 values are
  `30CC66EE905D4B18E37D56A77F27236C077A2166177968FF1BBF6DE0E6541B53`
  (`default.xbe`) and
  `EE81F9790246D7182E3DAA23691A1786FAEA2AC0F9EED70359BFC1BDF8C1898C`
  (`efmp.xbe`). The single retained PK3-only hardware stage is
  `build/hardware/StarTrekEliteForceX-Beta-20260801`, version
  `Beta-20260815-retail-skin-swap-profile` (612.4 MiB). Retail-hardware FPS,
  counter deltas, and long-soak proof remain the acceptance gate. Do not use
  CXBX-R for this candidate: retail JA itself crashes there, so a CXBX-R crash
  from JA-derived renderer code is expected emulator incompatibility rather
  than a regression signal.

- 2026-08-15 retail player-skin pool candidate: `RE_RegisterSkin` now follows
  the shipping JA Xbox contract exactly for every `models/players` path
  (including Elite Force `models/players2`): texture registration is bracketed
  by `BeginSkinTextures` / `EndSkinTextures`, placing player skins in the
  reserved 4 MiB swapping pool instead of consuming the 10 MiB static pool.
  Fresh XDK 5558 builds passed 90-second XEMU/LLE gameplay runs in both
  personalities. SP `borg6` held 85.8-90.9 guest FPS (90.1 average), and
  Holomatch `hm_borg1` held 89.6-90.9 (90.7 average) with movement, firing,
  intact world/HUD output, advancing heartbeat, and no exception. Reports:
  `scripts/output/retail-ja-skin-pool-sp_borg6_20260815_182307.report.txt` and
  `scripts/output/retail-ja-skin-pool-mp_hm_borg1_20260815_182646.report.txt`.
  Source XBE SHA256 values are
  `C526907CF53C6ACDA346CD9DBB7DDBF6F61BD3FD3FDEF34C9B8AB8998E94B624`
  (`default.xbe`) and
  `2138D134E5ACF750A77137DD8290AC56C9A224A441DB6993E631D55C3CE5CEF2`
  (`efmp.xbe`). Retail-hardware FPS and long-soak proof remain the acceptance
  gate. Do not use CXBX-R for this qualification: the retail JA executable
  crashes there, so the same behavior from JA-derived renderer code is not a
  regression signal.

- 2026-08-15 retail texture-pool ordering candidate: `default.xbe` and
  `efmp.xbe` now reserve the shipping 10 MiB static and 4 MiB skin contiguous
  pools before sizing the shared general zone. The skin swap-file open remains
  deferred until `GLW_Init`, avoiding the known `_cinit` startup failure while
  restoring retail physical allocation order. SP `borg6` passed 100 seconds in
  XEMU/LLE (90.9 guest FPS, 27.0 wall FPS); Holomatch `hm_borg1` passed 90
  seconds (90.8 guest FPS, 28.5 wall FPS). Hardware FPS and long-soak testing
  remain required. Current transfer stage:
  `build/hardware/StarTrekEliteForceX-Beta-20260801`, version
  `Beta-20260815-cached-zone-retail-ja-renderer`. Fresh final-source 60-second
  XEMU/LLE proofs passed on 2026-08-15 for SP `borg6` and Holomatch `hm_borg1`;
  retail-hardware FPS remains the acceptance gate.
  A separate 90-second `hm_dn1` run with real XEMU keyboard-controller events
  also completed without a heartbeat stall or rendering failure (90.8
  classified guest FPS, 27.1 wall FPS). Its captures remained in the same
  corridor, so this is runtime/input-dispatch stability evidence, not proof of
  player movement or complete control behavior. Report:
  `scripts/output/retail-ja-cached-zone-mp-controls_hm_dn1_20260815_175113.report.txt`.
- 2026-08-15 retail final ABI and memory-residency candidate: all shared
  renderer/bridge objects now use the shipping `FINAL_BUILD`/`_FINAL` ABI, and
  the general game zone has moved from a fixed 22 MiB contiguous D3D allocation
  to retail-equivalent `GlobalAlloc` sizing. The follow-up candidate above now
  reserves the 10 MiB static and 4 MiB skin texture pools before that sizing.
  Fresh XDK 5558 SP and Holomatch builds
  each passed 60-second XEMU/LLE gameplay proof with intact lighting/HUD and no
  stall. Reports are
  `scripts/output/retail-zone-heap-sp_borg6_20260815_111646.report.txt` and
  `scripts/output/retail-zone-heap-mp_hm_borg1_20260815_111157.report.txt`.
  This specifically removes general allocations from scarce contiguous D3D
  memory; retail-hardware FPS, memory, and long-soak proof remain required.
  CXBX-R is excluded from qualification for this JA-derived renderer because
  the unmodified retail JA executable also crashes there. Do not use a CXBX-R
  failure of this code as a regression signal unless it reproduces in XEMU/LLE
  or on retail hardware.
  The follow-up allocation audit confirms why the old placement was hazardous:
  Microsoft Xbox D3D8 source creates contiguous allocations with
  `PAGE_WRITECOMBINE`. No CPU-heavy heap remains there now; only the two retail
  GPU texture pools and the audio DMA buffer still use contiguous memory.
  The one-folder hardware stage is
  `Beta-20260815-cached-zone-retail-ja-renderer`; a representative open-scene hardware
  FPS/profile run is the acceptance gate for this candidate. Analyze each
  returned `ef_sp_log.txt` or `ef_mp_log.txt` with
  `scripts/analyze_hardware_profile.py`; it summarizes sustained FPS, frame and
  renderer phase timing, heartbeat progression, stalls, and zone-memory drift.
  Pass the matching older log through `--baseline` to report FPS gains and
  phase-time reductions directly.
  Matched sparse-telemetry XEMU/LLE runs now also pass in both personalities:
  Holomatch `hm_borg1` averaged 86.7 classified guest FPS with an 80.8 minimum,
  while SP `borg6` held 90.9 classified guest FPS and retained intact world
  lighting, HUD, weapon, and 2D orientation in native captures. These prove the
  staged code path remains functional; they do not replace retail-hardware FPS.
  A later apparent freeze at approximately 16.6 seconds was isolated to the
  temporary guest-side `stefx_smoke_input` diagnostic. The same XBE/map ran
  through 29.7 seconds of guest time without it, and a real XEMU controller
  key held through the same movement window reached 22.6 seconds without a
  stall. Renderer qualification must use real controller events from now on;
  the guest smoke-input path is excluded from acceptance evidence.
  A unified SP-to-co-op mini-soak reached the frontend and invoked
  `ui_ef_coop`, then stopped advancing at the Xbox kernel halt used by
  `XLaunchNewImage("d:\\default.xbe", ...)`. That is an executable relaunch
  boundary, not renderer work or a guest-frame stall. XEMU's old-title
  telemetry cannot qualify the relaunched co-op instance; complete this leg on
  hardware or with a harness that reconnects after the title handoff. Do not
  patch the valid retail handoff merely to keep the old telemetry block alive.

- 2026-08-15 Holomatch process-contract correction: XBE inspection found that
  `patchxbe.py` applied the SP/co-op 0x20000 stack commit to both personalities,
  while shipping `jamp.xbe` commits 0x40000. `default.xbe` remains at its
  established SP value and `efmp.xbe` now uses the retail MP value without
  forking any shared renderer code. A 75-second XEMU/LLE `hm_borg1` run passed
  loading, movement, weapon effects, world/HUD rendering, and process survival.
  Hardware FPS and long-stall evidence remain required.

- 2026-08-15 DDS mipmap hardware-cache candidate: the shared SP and Holomatch
  packages now contain complete DDS mip chains for eligible world, model, and
  environment textures while preserving `nomipmaps`, UI, font, levelshot, and
  fullscreen exclusions. XEMU visual/stability qualification passed in
  `hm_borg1` and SP `borg6`; Holomatch wall FPS was neutral at 29.87 versus
  30.08 in the adjacent control. Retail-hardware A/B proof is still required
  before keeping or rejecting this as a performance change.

- 2026-08-13 corrected hardware performance candidate: the last approximately
  5 FPS hardware binary was `Beta-20260813-flare-finish-sync`, which still
  serialized the CPU and GPU with `KickPushBuffer` plus `BlockUntilIdle`
  before each of its roughly 79-121 triangle submissions per frame. The active
  shared renderer no longer performs that per-draw drain, and normal
  `r_finish=0` gameplay also avoids the frame-end `BlockUntilIdle`. Fresh XDK
  5558 `default.xbe` and `efmp.xbe` builds passed compilation and stayed alive
  in the direct-Holomatch XEMU validation. The single hardware folder now
  contains `Beta-20260813-async-cycle-profile`, with bounded
  five-second `STEFX_HW_FRAME_PROFILE` records that split the renderer frontend
  into setup, PVS marking, world traversal, polygons, projection, entities,
  sorting, and debug work. Existing hardware evidence ranks the remaining
  costs as renderer backend (67-70 ms), renderer frontend (49-50 ms), then
  server/gameplay (3-5 ms); audio measured 0 ms. The next hardware log must
  verify that reservation/finish stalls collapsed before any visual-parity
  work, including the separately tracked lightmap issue, resumes.

- 2026-08-13 asynchronous retail submission candidate: the approximately
  5 FPS hardware result was produced by the preceding
  `Beta-20260813-flare-finish-sync` manifest. That XBE still contained a
  reintroduced `KickPushBuffer` plus `BlockUntilIdle` before every native
  triangle submission. Representative open scenes issue 79-121 submissions
  per frame, so the old binary serialized the CPU and GPU dozens of times per
  frame. The forced drain is absent from the retail JA renderer, the XDK 5558
  `BeginPush` sample, and the project's earlier qualified asynchronous path;
  it is now removed from the shared renderer used by both executables. Forced
  path-1 XEMU qualification passed sequentially in SP `borg6` and Holomatch
  `hm_borg1` with movement, firing, intact world/HUD output, zero explicit
  waits, and no stall. Hardware A/B proof remains required. The prior frame
  labels are nested (`game` inside `server`, frontend/backend inside `client`),
  so further non-renderer diagnosis must use their boundaries rather than
  adding all labels together.

- 2026-08-12 frontend co-op follow-up: the direct frontend launch now completes
  the full `borg1` title sequence and enters live two-player split screen. P2
  is entity 418 with a valid independent refdef, and both renderer slots submit
  distinct world/draw work. The contact sheet is
  `scripts/output/retail-coop-full-handoff_normal_20260812_201104_contact.png`.
  The deterministic pre-handoff freeze was an uninitialized ambient-sound-set
  dereference while Xbox audio was intentionally disabled; the shared ambient
  API now returns silent/no-sound results when that registry is unavailable.
  The resulting heavy `borg1` split view averaged 16.8 guest FPS, so retail
  hardware performance and representative split-screen workload optimization
  remain open even though the handoff and viewport composition pass.
  A subsequent fresh shared-engine relink also passed 100 seconds of the same
  `borg1` title/entity path at 55.3 average guest FPS without the former freeze,
  while the matching `efmp.xbe` passed 100 seconds of scripted `hm_borg1`
  gameplay at 90.6 average guest FPS. Exact post-link reports and hashes are in
  `notes/retail_renderer_qualification_2026-08-12.md`.

- 2026-08-12 retail-renderer qualification: the accepted shared native D3D8
  renderer has now passed fresh XDK 5558 builds and runtime proof in both
  `default.xbe` and `efmp.xbe`. SP `borg6` survived 100 seconds of scripted
  movement/combat at 77.0 average guest FPS, 67.3 minimum, and zero sub-30
  samples. Normal boot reached the intact SP main menu with correctly oriented
  UI at approximately 120-131 FPS. Co-op `borg6` retained two independent,
  correctly clipped viewports and averaged 60.3 FPS. Holomatch/CTF completed a
  continuous five-map cycle in one XEMU process, including repeated renderer
  shutdown/reinitialization, at 86.2 average guest FPS and 52.0 minimum with
  zero sub-30 samples. Lightmaps, shader stages, weapon effects, 2D/HUD
  orientation, loading, and presentation remained intact in captures. Exact
  evidence and hashes are recorded in
  `notes/retail_renderer_qualification_2026-08-12.md`. The one-folder hardware
  candidate has replaced the prior contents under
  `build/hardware/StarTrekEliteForceX-Beta-20260801`; retail-hardware FPS and
  long-play confirmation remain required before a public performance claim.

- 2026-08-13 hardware performance diagnosis: the preceding hardware candidate
  averaged about 5.2 FPS with a stable 187-188 ms frame and no memory growth.
  The timing labels overlap: roughly 25-26 ms of cgame entity work builds render
  submissions, 35-36 ms in `CG_DrawActive` contains renderer frontend work, and
  95-96 ms executes the backend. The separate server tick costs 28-30 ms. A
  matched XEMU cycle probe found pathological draws spending 11-15 million CPU
  cycles almost entirely inside `BeginPush`. Shipping JA MP reserves a 1 MiB
  push buffer with a 128 KiB kickoff threshold, while the shared EF engine had
  stopped calling `Direct3D_SetPushBufferSize` entirely. The exact retail policy
  is restored for both personalities. Current one-folder hardware candidate:
  `Beta-20260813-retail-pushbuffer-frameprofile2`. It also writes one bounded
  `STEFX_HW_FRAME_PROFILE` line every five seconds so the next retail log exposes
  server phases, scene workload, backend/Present time, and draw-cycle phases
  without relying on the already-full heartbeat line. Hardware FPS proof for
  this candidate remains pending.

- 2026-08-13 hardware flare-sync candidate: retail testing of the restored
  1 MiB/128 KiB push-buffer policy still averaged about 5 FPS, so push-buffer
  sizing alone is not the cause. A second shared-renderer synchronization bug
  was then isolated: both flare depth-test paths clear `glState.finishCalled`
  after Xbox `qglReadPixels`, even though that Xbox readback is a no-op. That
  false state makes `RB_SwapBuffers` call `BlockUntilIdle` at frame end. Xbox
  now preserves the finish state in both paths; non-Xbox behavior is unchanged.
  Fresh XDK 5558 `default.xbe` and `efmp.xbe` builds passed sequential XEMU
  checks. SP direct gameplay remained alive for 70 seconds with zero explicit
  finish/Present wait, and Holomatch traversed open `hm_borg1` rooms with three
  bots, movement, weapon fire, and effects for 80 seconds. The Holomatch run
  averaged 29.3 wall FPS in XEMU, had one transient 16.5 sample with immediate
  recovery, and had no sustained stall or visual regression. This is emulator
  qualification, not a hardware gain claim. The one-folder hardware candidate
  is `Beta-20260813-flare-finish-sync`; the next hardware log must compare its
  `STEFX_HW_FRAME_PROFILE` `finish`, `backend`, and total-frame fields against
  the prior approximately 187-188 ms frame before this lead is accepted.

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
  `IDirect3D8::CreateDevice`, explicit retail 1 MiB/128 KiB push-buffer sizing, no startup allocation
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
  Iteration 13 removes retired Holomatch success diagnostics from the game-VM
  adapter, movement, collision, player submission, and client-connect paths.
  Error and invalid-state diagnostics remain. The old build emitted a
  278-write synchronous burst in one sample window; the retained build reduced
  the corresponding window to 17 writes and removed roughly 2,250 startup and
  runtime writes overall. A matched 82-second A/B improved real delivered
  throughput from approximately 27.5 to 28.4 frames per wall second (3.1%).
  The corrected harness now reports guest/game-clock FPS and wall-clock FPS
  separately: the retained no-capture run held 88.4-90.9 guest FPS while XEMU
  averaged 28.4 wall FPS, with one recoverable 19.4-FPS emulator hitch. Five
  visual captures are clean and show intact geometry, lightmaps, weapon, and
  HUD at 87-101 on the in-game counter. Evidence:
  `scripts/output/retail-ja-hotpath13-wallmetric_hm_dn1_20260811_182022.report.txt`
  and
  `scripts/output/retail-ja-hotpath13-visual_hm_dn1_20260811_182244.report.txt`.
  The intermittent XEMU wall-delivery hitch remains open; it is no longer
  hidden behind game-clock-only FPS reporting.
  Iteration 14 removes the remaining recurring hosted-Holomatch heartbeat,
  liveness, centered-bot, and startup-frame success records. It also removes
  a dormant late-frame diagnostic that armed at 82 seconds of game time and
  could synchronously emit up to 1,024 detailed entity records. A 270-second
  wall-clock soak reached 101.9 seconds of game time without that burst,
  freeze, or progressive slowdown: guest FPS averaged 85.8 (83.5 minimum),
  delivered wall FPS averaged 27.5 (24.5 minimum), and log traffic remained
  2-7 writes per sample. The matching visual proof averaged 87.7 guest FPS
  and 29.0 delivered wall FPS with correct geometry, lightmaps, weapon, and
  HUD. Iteration 14 is retained as a stability fix. The brief irregular XEMU
  delivery cadence remains open for hardware comparison. Evidence:
  `scripts/output/retail-ja-hotpath14-lateframe-soak_hm_dn1_20260811_183524.report.txt`
  and
  `scripts/output/retail-ja-hotpath14-visual_hm_dn1_20260811_184108.report.txt`.
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
- The v50 `hm_dn1` diagnostic classified the complete indexed workload without
  changing the shipping JA packet path. All retained samples held essentially
  the same 167-168 draws, 16,518 shader-pass indices, 34 multitexture draws,
  and 88,409 reserved dwords. The 68 opaque plus 99-100 blended/no-depth-write
  call split is consistent with normal lightmapped multipass rendering.
  Draw-path cost still varied by more than 3x and XEMU wall delivery alternated
  between 17.5 and 35.0 FPS with no corresponding workload change. No shader
  state or reservation size predicts the intermittent queue wait; do not remove
  valid blend/lightmap passes as an FPS shortcut. Evidence is in
  `scripts/output/retail-v50-hm-state-workload-profile_hm_dn1_20260817_203907.report.txt`
  and its `_xblog_profiles.log` companion.
- Exclude the earlier direct-co-op diagnostic that started smoke input at
  twelve seconds: it granted control before the campaign crawl finished. The
  harness now shifts the complete movement/attack schedule so its first input
  cannot occur before 210 seconds, including runs with custom smoke timings.
  Relative input timing and durations are preserved. This is a test-harness
  correction only, not a game-code change.
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
- Run `scripts/cleanup_generated.ps1` before and after emulator sessions and after every completed build/package/test cycle. Use `-Aggressive` whenever a beta stage is superseded; keep only the current ISO and active hardware stage.
- Report FPS on every emulator run, but treat raw FPS tuning as post-beta unless
  a functional stall or crash occurs.

## Performance Handoff

- v52 completes the shipping forced-entity-alpha ordering for Holomatch by
  placing translated forced-alpha draw surfaces in the backend post-render
  queue. SP keeps its separate alpha-fade behavior. The current source builds
  are `4D83ED8090B2ED763D9C4EBAA116A02DFB00C79675BA2C6A4365D3F16AEE308B`
  (`default.xbe`) and
  `95569E4F66D9FC782E9F720B1C849A1F5915A78F0C45D6850DB42C533CDF3FC1`
  (`efmp.xbe`). A moving/firing Holomatch smoke passed broad regression, but
  its captures did not contain an active fading player, so focused effect
  proof remains open.
- A sampled-EIP XEMU run is excluded from CPU-hotspot evidence. Its monitor
  cadence sampled the Xbox kernel idle loop between frame deliveries rather
  than the active renderer and therefore cannot rank guest frame ownership.
- No usable shipping D3D8 5558 QFE4 library was found in the supplied retail
  source trees or either local XDK. The `Z:` candidates are tiny Win32 import
  libraries; the only complete Xbox static library is the clean 5558 QFE1
  `d3d8.lib`. The QFE delta remains documented, but there is no complete and
  safe QFE4 linked unit available for transplant.
- v51 restores the shipping renderer's forced-entity-alpha stage behavior for
  the Holomatch personality while leaving SP's distinct alpha-fade contract
  unchanged. `RB_IterateStagesGeneric` is now 399 instructions versus the
  donor's 406; the seven-instruction remainder belongs to JA-only
  `RF_ALPHA_DEPTH` behavior. A 60-second moving/firing `hm_dn1` smoke passed
  liveness and broad visual regression checks. Its frames did not catch an
  active player fade, so focused effect proof remains open.
- The early-control direct-co-op run is invalid evidence. Direct-co-op harness
  input schedules are now shifted as a unit so input begins no earlier than
  210 seconds, after the campaign crawl; this is a test-harness correction and
  does not change game input behavior.
  A corrected 65-second negative-control run supplied the old 12-second input
  schedule and verified that the harness moved it beyond the run. Five native
  captures remained entirely in the advancing campaign crawl with no player
  camera, movement, or attack. Evidence is
  `scripts/output/retail-v54-coop-input-gate_normal_20260817_225710.report.txt`
  and
  `scripts/output/retail-v54-coop-input-gate_normal_20260817_225710_contact.png`.

- Current shipping-contract candidate hashes are
  `1BECDD56398B052FB850468CF37C42DFE003D2651C1DF9D8A76BF29707C8734C`
  (`default.xbe`) and
  `90B18DC9345411E3BFFE8AAB6AA74928E0FB50F5C9CF9F45911FCCC7E19A7C68`
  (`efmp.xbe`). Both executable/game personalities now compile with the clean
  retail `FINAL_BUILD;_FINAL` contract. SP `borg6` and Holomatch `hm_dn1`
  passed 90-second XEMU smokes, but the 28.6/28.7 monitor-derived wall rates
  do not improve on the prior 28.8 Holomatch result. Retain the parity fix;
  do not claim an FPS gain until hardware measures it.
- Current full object-level renderer evidence is 2,002 common functions,
  1,723 same-length bodies (86.1%), and 1,451 exact normalized bodies (72.5%).
  Select remaining work by sustained-frame relevance. Load-time parser and
  asset-format differences are not FPS candidates by themselves.
- Open visual issue, deliberately below sustained-FPS work: 2D atlas output
  remains garbled/flipped in HUD and loading overlays. Do not apply a global
  texture-coordinate flip; that experiment previously broke both 2D layout
  and font rendering. Require screenshot proof for any eventual fix.
- v53 closes texture-pool pressure as the shared sustained-FPS lead. A valid
  visible SP `borg6` diagnostic reached gameplay with no injected input and
  held the static texture pool at 3,978 KiB of 10,240 KiB. The skin pool moved
  only from 118 to 124 KiB of 4,096 KiB; swaps, fetches, GPU waits, disk reads,
  and disk writes all remained zero. The retained profile samples were 8-17 ms
  guest frames. Evidence is
  `scripts/output/retail-v53i-sp-skin-residency_borg6_20260817_224053_xblog_profiles.log`.
  The matching Holomatch v50 diagnostic was likewise churn-free, so neither
  pool overflow nor texture swapping explains the shared retail-hardware FPS.
- Headless v53a-v53g runs are excluded from engine and pool evidence because
  XEMU did not advance the game XBE in that mode. They exposed and fixed a
  harness address bug: PE map symbols must first be translated by section
  offset into the generated XBE, then translated through the live guest page
  tables before monitor reads. The corrected visible run proved the probe and
  the game were both live. The normal non-diagnostic `default.xbe` was restored
  afterward; its current SHA256 is
  `076C25BD1A6DC6D9C08FE0620EACEB78DCED81A997B0DC8018E018B72B571203`.

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

- The current v73 production candidate restores the retail 16-byte
  `msurface_t` record used by the hot world-traversal table. The prior Xbox
  production layout was 48 bytes because diagnostic bounds and shader fields
  were embedded in every record; those fields now exist only in explicit
  `STEFX_XBOX_SURFACE_DIAGNOSTICS` builds. Corrected XDK 5558 builds passed clean,
  input-free XEMU checks in SP `holodeck` and Holomatch `hm_dn1`. Both remained
  visually coherent and live; the representative captures held roughly
  88-90 guest FPS. Evidence is
  `scripts/output/retail-v73-surface-stride-sp_holodeck_20260818_115253.report.txt`,
  `scripts/output/retail-v73-surface-stride-sp_holodeck_20260818_115253_contact.png`,
  `scripts/output/retail-v73-surface-stride-hm_hm_dn1_20260818_115515.report.txt`,
  and
  `scripts/output/retail-v73-surface-stride-hm_hm_dn1_20260818_115515_contact.png`.
  Current production executable hashes after the guard-verification rebuild are
  `1393474A3E68DF9901BD3F2D2EC0E2515AF68C4574398F2CD5B02B79C71E7D98`
  (`default.xbe`) and
  `06DEE1D55DE364422DD7B6E7AB54946DFFF031A9FCA4F4FE07221AA94B2980B9`
  (`efmp.xbe`). This is compatibility proof, not a measured performance win:
  XEMU was already fast, and the retained v66 hardware stage remains
  untouched until a controlled hardware comparison is requested.
  Frame-phase diagnostics no longer imply the separate
  `STEFX_XBOX_SURFACE_DIAGNOSTICS` option, so a future timing build can retain
  the 16-byte production layout while collecting aggregate frame counters.
  A compile-time Xbox assertion now enforces that 16-byte layout. Full XDK
  5558 frame-diagnostic builds of both personalities passed before the normal
  production pair was restored. No new hardware FPS claim has been made.

- 2026-08-19 retail push-buffer pointer correction: the shared Xbox D3D8
  submission paths now retain the packet pointer returned by XDK 5558
  `BeginPush` instead of replacing it with a value read from private device
  memory. The correction covers the central indexed path and the light, bump,
  and environment special-material paths. Clean XDK 5558 builds produced
  `default.xbe`
  (`CB06482410F41CAF304FFC4604EB50278E7317F6A382E0F09AD93D713F3800B8`)
  and `efmp.xbe`
  (`85FDDA71FFEDC56BC89367CC723C9F5EFD8354B39E86783DAF612C66743982E6`).
  SP `borg6` rendered coherent live gameplay at 89.4 guest FPS. Holomatch
  `hm_borg1` remained alive with coherent lightmaps, weapon, and HUD at
  approximately 90.7-90.8 guest FPS. Holding Back produced a visible official
  scoreboard over live gameplay; proof is
  `scripts/output/hm-score-shared-push-visual_borg6_20260819_045324_contact.png`
  with telemetry in the matching report. The canonical controller file
  `C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\BaseEF\default.cfg` is
  read-only to Codex and remained byte-identical across every build and run
  (`SHA256 3C5B05EBF8B732E1D1065A2337BE63E421560E42C2B4E9CFB28A843F9AB38493`).

- 2026-08-19 production scoreboard-probe removal: the shared renderer no
  longer compiles the old Holomatch visual-capture probe unless the explicit
  `STEFX_HM_SCORE_DIAGNOSTICS` build flag is enabled. That probe scanned
  submitted batches and synchronously queried render targets, textures,
  shaders, transforms, and D3D state when the scoreboard was shown. Normal
  scoreboard and HUD drawing are unchanged. Clean XDK 5558 builds produced
  `default.xbe`
  (`6F2A6B13AADC3E91B20B07501A948033DA8D11829A2F78B933581A3EA230092F`)
  and `efmp.xbe`
  (`90C23C653CDD164D6ABFF8FC01FD2D8D8955D68A2205BF63F53E5A6A3F8A88B2`).
  SP `borg6` remained alive with coherent lightmaps and upright HUD across six
  captures at approximately 88-89 guest FPS. Holomatch `hm_borg1` remained
  alive with active bots, coherent lightmaps/HUD, and approximately 89-90
  guest FPS; holding the canonical Back action displayed the official
  scoreboard in two consecutive captures without the probe. Evidence is
  `scripts/output/retail-scoreprobe-off-sp_borg6_20260819_060832.report.txt`,
  `scripts/output/retail-scoreprobe-off-sp_borg6_20260819_060832_contact.png`,
  `scripts/output/retail-scoreprobe-off-hm_hm_borg1_20260819_061236.report.txt`,
  and
  `scripts/output/retail-scoreprobe-off-hm_hm_borg1_20260819_061236_contact.png`.
  A subsequent 150-second `hm_borg1` soak remained alive through deliberate
  shutdown. Eight sequential captures stayed coherent at approximately 90
  guest FPS, the render/score draw counter advanced from 1,091 to 4,592, and
  the texture allocator remained exactly 2,985,856 bytes used throughout with
  no growth. Evidence is
  `scripts/output/retail-scoreprobe-off-hm-soak_hm_borg1_20260819_061907.report.txt`
  and
  `scripts/output/retail-scoreprobe-off-hm-soak_hm_borg1_20260819_061907_contact.png`.
  The one retained hardware-transfer folder is refreshed as
  `Beta-20260819-production-scoreprobe-off-v77`. Its HDD/media-enabled hashes
  are `D9160FE7F38002863E2866E06C467D9B9254F50D1FACEC616C262882A7F1B4F5`
  (`default.xbe`) and
  `1F0EF64866B416548E19EA9C8D546BEE0A38A0FAEEE1432074A1264BFA2F4983`
  (`efmp.xbe`).
  This is a production-path cleanup and compatibility proof, not a claimed
  hardware FPS gain. The protected canonical `default.cfg` remained unchanged.

- 2026-08-19 production scoreboard-telemetry compile-out: the remainder of
  the retired Holomatch visual-capture and command-submission telemetry is now
  absent from normal builds unless `STEFX_HM_SCORE_DIAGNOSTICS` is explicitly
  defined. Lightweight functional scoreboard draw state remains available;
  normal HUD and scoreboard behavior are unchanged. Clean XDK 5558 builds
  produced `default.xbe`
  (`9384B861D5E38B15C0D14B3E3EEC0ABD1BE467EFB82C1F9867F8DF97E81259D9`)
  and `efmp.xbe`
  (`A85C5AB100E1A15B9CEEF5DD301712E2469074A253FF98322078A11B22DD4156`).
  SP `borg6` remained alive and visually coherent across six captures at
  approximately 89 FPS. Holomatch `hm_borg1` remained alive and visually
  coherent across nine captures at approximately 89-90 FPS; holding Back
  displayed the official scoreboard in two captures and returned cleanly to
  gameplay. Evidence is
  `scripts/output/retail-scoretelemetry-off-sp_borg6_20260819_063704.report.txt`,
  `scripts/output/retail-scoretelemetry-off-sp_borg6_20260819_063704_contact.png`,
  `scripts/output/retail-scoretelemetry-off-hm_hm_borg1_20260819_063927.report.txt`,
  and
  `scripts/output/retail-scoretelemetry-off-hm_hm_borg1_20260819_063927_contact.png`.
  A separate controller-path proof used the packaged canonical config without
  modifying it: an emulated Xbox Start press opened the SP in-game menu and
  the game remained live at approximately 89-91 guest FPS. Evidence is
  `scripts/output/canonical-controls-sp-start_borg6_20260819_065130.report.txt`
  and
  `scripts/output/canonical-controls-sp-start_borg6_20260819_065130_contact.png`.
  This confirms the packaged binding and SP dispatch path in XEMU only; it
  does not supersede any retail-hardware controller report.
  The single hardware-transfer folder is refreshed as
  `Beta-20260819-production-scoretelemetry-off-v78`; it is marker-free and
  uses `default.xbe` as the SP/co-op entry with `efmp.xbe` retained for
  Holomatch handoff. Its HDD/media-enabled XBE hashes are
  `862658E3139FF14992AC9858F79DD197FD30651934919EA57A246DAEDAD08C79`
  (`default.xbe`) and
  `22094478092AD510F26D2F4ADDAF61C91377B2208F0A14DA16C028C3E2266899`
  (`efmp.xbe`). This is production-path compatibility proof, not a hardware
  FPS claim. The protected canonical `default.cfg` remained byte-identical at
  SHA256
  `3C5B05EBF8B732E1D1065A2337BE63E421560E42C2B4E9CFB28A843F9AB38493`.

## Recovery North Star

- Commit `cfc5918b` (`Stabilize split-screen EF overlays and capture harness`,
  2026-07-14) remains the SP/co-op source-parity north star.
- Commit `90d64c89` (`Add SP-hosted Holomatch vertical slice`) was never a
  known-good SP baseline; its title must not be treated as proof.
- The current beta restores the intended architecture: shared common systems,
  separate game personalities, and both XBEs on one XISO.
