# Elite Force X Beta Qualification Snapshot

Date: 2026-08-01

## Current Qualification Gate - 2026-08-20

The 2026-08-01 beta evidence below is historical. The active qualification
target is now the shared native D3D8 renderer derived from the shipping retail
Jedi Academy multiplayer Xbox renderer, with separate SP/co-op logic in
`default.xbe` and SP-hosted Holomatch logic in `efmp.xbe`.

Current active build command:

```powershell
scripts\build_xbox.ps1 -Target spmp
```

Important current gates:

- `scripts\build_xbox.ps1 -Target spmp` must run two target-scoped passes:
  first a normal SP pass that refreshes `build\release\default.xbe`, then an
  SP-hosted Holomatch pass that emits `build\release\efmp.xbe`.
- The same `spmp` target must refresh both active release packages:
  `build\release\BaseEF\xbox0.pk3` for SP/co-op and
  `build\release\BaseEF\xbox1.pk3` for Holomatch.
- The `sp` and `spmp` targets assert active PK3 freshness after package
  refresh, rejecting packages older than runtime source or package scripts.
- `scripts\verify_build_xbox_contracts.py` verifies that build-graph and
  package-postcondition contract, and protects the forced `xb_log.cpp` rebuild
  that refreshes the `STEFX_RUNTIME_BUILD_ID` `__DATE__`/`__TIME__` literal.
  It also verifies that `build_xbox.ps1` applies the retail renderer ABI defines
  and filters the legacy frame-path modules replaced by `retail_xbox`.
- `scripts\stage_hardware_pk3_test.ps1 -CheckFreshnessOnly` now verifies the
  build-graph contract, release-XBE source freshness, and both
  `STEFX_RUNTIME_BUILD_ID` identities before staging or XEMU proof refresh. It
  also rejects stale `build\release\BaseEF\xbox0.pk3`/`xbox1.pk3` packages
  when runtime source or package scripts are newer than those retained PK3s.
  Release XBE freshness, release PK3 freshness, and runtime build ID failures
  are reported together so one check shows the full release-artifact blocker
  set.
- New hardware stages must contain `buildScriptContract` provenance in
  `HARDWARE_PATCH_MANIFEST.json`.
- `scripts\verify_hardware_stage.py` is preflight schema v12 and rejects
  missing, failed, stale, or hash-mismatched `buildScriptContract` records.
- `scripts\verify_production_hardware_logs.py` is production schema v19.
- `scripts\qualify_hardware_stage.py` is combined audit schema v63 and expects
  stage v12, production v19, and XEMU refresh manifest schema v3.
- The combined audit structurally validates the retail `jamp.xbe` contract
  ledger, including retail address ranges, confidence, retail/shipping evidence,
  and non-empty per-function contract text.
- The same audit validates active retail-source routing: all 11
  `code/renderer/retail_xbox/*_retail.cpp` modules must include
  `retail_renderer_contract.h`, keep the retail namespace wrapper, remain listed
  in `code/x_exe/x_exe.vcproj`, and retain the build-script retail ABI defines
  plus legacy frame-path replacement filter.
- The same audit also requires schema-v2 SP/Holomatch object-compare reports
  with compare-script provenance, root/link input records, object-pair records,
  and per-current/donor-object file hashes. It invalidates saved reports
  when any active retail renderer source, `code/x_exe/x_exe.vcproj`,
  `scripts/build_xbox.ps1`, or `scripts/compare_retail_renderer_objects.py`
  input is newer than the retained comparison report.
- XEMU refresh manifest schema v3 records the successful release freshness
  preflight command/output before proof reports, including build graph, release
  XBE freshness, release PK3 freshness, and per-XBE runtime build ID identity
  lines for `default.xbe` and `efmp.xbe`.
- Saved XEMU proof is invalidated by newer runtime source, newer retained
  release XBEs, newer retained `BaseEF\xbox0.pk3`/`xbox1.pk3` packages, or
  newer proof-harness/gate scripts:
  `build_xbox.ps1`, `build_xbox_patch_pk3.py`, `check_mp_holomatch_ui.py`,
  `ja_xemu_smoke.py`, `run_sp_xemu_smoke.ps1`, `run_mp_xemu_smoke.ps1`,
  `refresh_xemu_qualification_proof.ps1`, `stage_hardware_pk3_test.ps1`, and
  `verify_build_xbox_contracts.py`.

Current retained artifacts are not fully qualified:

- Fresh 2026-08-20 `build\release\default.xbe` and `build\release\efmp.xbe`
  now exist and have focused 4P Holomatch XEMU/LLE proof below. This does not
  replace the broader SP/co-op regression refresh or staged hardware proof.
- Focused 2026-08-21 SP `borg1` qualification closes the reported black
  surfaces 90 degrees right of the first-control view. BSP tracing identified
  `textures/common/sky`; the canonical package had omitted its retail
  `scripts/voyager.shader` definition and therefore allowed the map material
  to resolve through the default-shader path. The package builder and console
  shader list now retain stock `voyager.shader` alongside the focused
  `borg.shader` blend corrections. The rebuilt 257,135,710-byte `xbox1.pk3`
  has SHA256
  `A4A1D63520DCEDDFA2C5D3C8915CAEB791BE5804C9CCFCABBE8A6A947A77D40B`,
  contains the exact retail `skyParms env/stars 512 -` contract, all six star
  cube DDS faces, and `maps/hm_borg1.aas`. The `borg1` material audit reports
  zero used missing assets and zero used unresolved materials. The
  180-second no-screenshot run
  `scripts/output/sp-borg1-skyfix-live_borg1_20260821_111421.report.txt`
  remained alive at controlled shutdown; its extracted `ef_sp_log.txt`
  contains no shader fallback. The user then inspected the exact right-facing
  surface live in a rerun and confirmed it fixed. This is focused SP visual
  proof, not broader SP/co-op, Holomatch, soak, performance, or hardware
  acceptance.
- The retained SP/Holomatch object-compare reports are stale versus current
  retail renderer source/build inputs and must be regenerated before retail
  contract evidence can pass again.
- The retained hardware stage
  `build\hardware\StarTrekEliteForceX-Beta-20260801` predates the
  `buildScriptContract` manifest field and is rejected by the current preflight.
- The current combined audit is expected to fail until the object comparisons,
  broader XEMU matrix, hardware stage, returned retail logs, and filled
  SP/co-op/Holomatch observation evidence are refreshed.

After a fresh `scripts\build_xbox.ps1 -Target spmp`, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\stage_hardware_pk3_test.ps1 -CheckFreshnessOnly
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\refresh_xemu_qualification_proof.ps1
```

The refresh helper writes `scripts\output\xemu_qualification_proof_refresh_*.json`
and, unless `-SkipAudit` is used, immediately runs
`scripts\qualify_hardware_stage.py` with `--xemu-refresh-report` bound to that
manifest. For a later manual audit rerun, pass the latest refresh manifest
explicitly with `--xemu-refresh-report`. If no explicit XEMU report overrides
are supplied, a standalone audit also auto-selects the newest
`scripts\output\xemu_qualification_proof_refresh_*.json` manifest when one is
present. Schema v63 records that choice in `xemuProofRefreshReport.selectionMode`
as `explicit`, `auto`, `disabled-by-explicit-proof-reports`, or `none`.

## Beta Candidate

- Package: `build/beta/StarTrekEliteForceX-Beta-20260801`.
- XISO: `StarTrekEliteForceX-Beta-20260801.iso`.
- XISO SHA256: `F434561D66B4687F2CF06DA36D5707DBC8BB7F6B1F3A25ED713A5B236BD54C83`.
- Entry point: `default.xbe`.
- SP/co-op personality: `default.xbe`.
- Holomatch personality: `efmp.xbe`.
- Shared runtime: `BaseEF`.
- `codemp/` build/runtime dependency: none.
- Diagnostic or smoke markers in XISO: zero.

Component hashes:

| Component | Bytes | SHA256 |
|---|---:|---|
| `default.xbe` | 4,354,048 | `469F0D271B268B1781BF93753C84E63AE98AE9D11D6A87AF37094D4A0A311B89` |
| `efmp.xbe` | 4,091,904 | `0DF4E39C0BAB86F6D1D47908DF50E690CAE2837C30A2990E95D86D84BA7DFAD7` |
| `BaseEF/xbox0.pk3` | 283,522,258 | `4DBD3F330B7E70861D174AC88F70C528202B917B556109C97A86A9C24C4B2E0B` |
| `BaseEF/xbox1.pk3` | 224,620,678 | `D34C209C70B1D41F3D402AAEE6AF715447F30E4BF2CE48442A76799E07E70870` |
| `BaseEF/soundbank/sound.bnk` | 500,246,186 | `D5D2F4024BC74975065B632E4981096EDBC3E0194D25EBA36F84E4975E690389` |
| `BaseEF/soundbank/sound.tbl` | 103,623 | `11153B73334F267CB118F48FBDF27DA6A17A55AB81A44FA55369DE77234EB247` |

The complete machine-readable manifest is
`build/beta/StarTrekEliteForceX-Beta-20260801/release_manifest.json`.

## Build Evidence

- Full SP build: passed.
- Full SP-hosted Holomatch build: passed.
- Build log: `build_spmp_beta.log`.
- `efmp.xbe` is built from the shared `code/` engine plus
  `code/holomatch/official` game/cgame and bot code.
- The package verifier checked 1021 `code/` source files and found no
  `codemp/` dependency.
- `xbox1.pk3` contains:
  - 33 optimized multiplayer BSPs.
  - 33 patched AAS checksums.
  - 1277 DDS entries: 1149 DXT1 and 128 BGRA32.
  - Zero original JPG/TGA/PNG texture entries.
  - Zero legacy UI scripts.
  - Official bot, weapon, pickup, arena, shader, and loading support assets.
- The runtime stage contains:
  - Zero loose MP map overrides.
  - Zero loose original-image fallbacks for `xbox1.pk3`.
  - Zero loose UI scripts.
  - 7971 shared soundbank records: 7947 Xbox ADPCM and 24 preserved PCM.

## SP And Co-op

- Campaign loading, cinematics, gameplay, and main-menu return pass.
- Campaign loading retains the SP layout and localized quoted level title.
- Two-player co-op retains the canonical full-screen introduction before
  splitting into two live viewports.
- P2 has an independent camera, origin, input, HUD, and weapon view.
- P1/P2 presentation and Borg materials are visually intact.
- Focused proof:
  - `scripts/output/stefx-beta-coop-presentation-final_normal_20260801_082758.report.txt`
  - `scripts/output/stefx-beta-coop-p2input_normal_20260801_083709.report.txt`

## Holomatch

- FFA with bots, weapon pickup/ammo, firing, phaser beam, damage, HUD, loading,
  controls, scoreboard, and audio passes.
- CTF boots into a playable team match with three bots; movement, firing, and
  damage pass.
- Loading screens retain the signed-off MP backdrop/metadata presentation and
  shared SP LCARS wheel.
- Focused proof:
  - `scripts/output/stefx-beta-hm-ffa_hm_borg1_20260801_084654.report.txt`
  - `scripts/output/stefx-beta-hm-ffa-combat_hm_borg1_20260801_085108.report.txt`
  - `scripts/output/stefx-beta-hm-ctf-play_ctf_dn1_20260801_085745.report.txt`
- User signoff remains recorded for HUD, loading screens, controls, phaser,
  footsteps, bot visibility/damage, pickups, movement, stairs, and teleporters.

## Cross-mode Qualification

- XBE roundtrip:
  - `scripts/output/stefx-beta-xbe-roundtrip6_normal_20260801_095353.report.txt`
  - SP menu -> Holomatch -> live FFA -> SP menu remained stable.
- Final uninterrupted mini-soak:
  - `scripts/output/stefx-beta-final-minisoak2_borg2_20260801_101352.report.txt`
  - `scripts/output/stefx-beta-final-minisoak2_borg2_20260801_101352_contact.png`
  - One XEMU session covered SP gameplay, menu return, co-op introduction,
    live split-screen with independent P2 movement, menu return, XBE handoff,
    and more than four minutes of stable Holomatch.
- Marker-free release-disc boot:
  - `scripts/output/stefx-beta-cleaniso-final_normal_20260801_114812.report.txt`
  - `scripts/output/stefx-beta-cleaniso-final_normal_20260801_114812_contact.png`
  - Status: pass; correct six-item shared main menu remained stable.

## Deferred

- XEMU/LLE frame-rate optimization is post-beta. Functional stalls, crashes,
  data corruption, or visual/gameplay regressions remain blockers; raw FPS
  improvement does not.
- Four-player local Holomatch split-screen is in progress as of 2026-08-20, not
  shipping-qualified. The current source launches a 4P local FFA-with-bots
  match from the Holomatch button and adds virtual P1-P4 controls plus P2-P4
  local refdefs for runtime proof. Fresh XEMU build, visual, control, HUD,
  bot-fill, memory, audio, and bounded-soak evidence now exists below; real
  controller assignment, representative-map coverage, long soak, and staged
  hardware proof are still required. Returned `ef_mp_log.txt` files should pass
  `python scripts\verify_holomatch_split_log.py <log>`; the stricter runtime
  qualification should also pass `--min-bots 3 --require-attack
  --require-positive-snapshot-adds --require-fp-filter-slot 1
  --require-fp-filter-slot 2 --require-fp-filter-slot 3 --min-elapsed-seconds
  90 --min-heartbeat-fps 15 --min-largest-free 1048576 --max-used-delta 0`
  after a long enough match. For full staged hardware qualification, pass
  `--require-hm-split-log` to `scripts\verify_production_hardware_logs.py` or
  `scripts\qualify_hardware_stage.py`.
  Latest focused XEMU/LLE proof:
  `scripts/output/hm-split-heartbeat-fps-4p_hm_borg1_20260820_132707.report.txt`
  and
  `scripts/output/hm-split-heartbeat-fps-4p_hm_borg1_20260820_132707_contact.png`.
  That run used 2026-08-20 13:24:43 `default.xbe` and 13:26:02 `efmp.xbe`,
  exited alive, and visually shows all four 4P Holomatch panes rendering live
  `hm_borg1` geometry. The earlier broad P2-P4 HOM failure is not reproduced.
  The production heartbeat mirror now gives accepted log-side FPS samples:
  12 active/gameplay samples averaged 34.1 guest/game-clock FPS with a 22.3
  minimum and 4/12 below 30. Delivered XEMU wall-FPS averaged 25.0 with
  screenshot/poll stalls included, so 4P remains performance-risky and staged
  hardware FPS is still required. Split-camera near-wall views remain open.
  The 2026-08-21 all-slot collision and no-cull diagnostics supersede that
  near-wall interpretation for the broad black-void captures. Collision proof
  reported 758 clusters, five inline models, linked P1-P4 entities, valid
  renderer clusters, and empty eye contents. With culling disabled, an affected
  P4 frame still showed isolated back sides of brushes while the local player
  was airborne at `z=-241`; the blind synthetic controller had reached exposed
  map space. The virtual fallback path now uses a full player-box wall trace
  plus a forward floor probe and logs `STEFX_HM_SPLIT_VIRTUAL_AVOID`; real pad
  input is unchanged. Supporting normal-culling visual proof is
  `scripts/output/hm-virtual-avoid-proof_normal_20260821_010212.report.txt`,
  using 4,395,008-byte `default.xbe` SHA256
  `94B634FA3082DB4CD5A5D5757AB39207A8FB5B64A9DC7BF5AE3F65C1D2FAC55A`
  (runtime ID `Aug 21 2026 01:01:18`) and 4,194,304-byte `efmp.xbe` SHA256
  `6056A7399A6E56524A62D4ED4AB65769FA78762469E559A6F0F817E49FDF2EF2`
  (runtime ID `Aug 21 2026 01:01:22`). All six retained frames were inspected
  sequentially and show four coherent worlds, HUDs, weapons, and effects with
  no isolated-brush black void. The 90-second run exited alive; guest/game FPS
  averaged 26.1 (21.2-33.2) and wall throughput averaged 18.5. The retained
  candidate suppresses periodic virtual jumps while avoidance is active. Its
  exact-binary proof is
  `scripts/output/hm-virtual-avoid-finalproof_normal_20260821_010758.report.txt`,
  using 4,395,008-byte `default.xbe` SHA256
  `A92D119388150237F532BCB3C2728CF45492568E291E6691B432D07D73330662`
  (runtime ID `Aug 21 2026 01:07:03`) and 4,194,304-byte `efmp.xbe` SHA256
  `D6BD5A6A3F6A08D8C1396AB126D64E85CE2CEA5195691154580BD74012CD5403`
  (runtime ID `Aug 21 2026 01:07:08`). All three retained frames from the
  65-second run were inspected sequentially. Every quadrant keeps coherent,
  connected world geometry, HUD, weapon, and effects; the first frame's dark
  P2 opening is a grounded, empty-contents position in valid cluster 598, not
  the prior airborne exposed-space failure. The process exited alive;
  guest/game FPS averaged 26.5 (19.4-30.5) and wall throughput averaged 18.0
  (10.1-31.3). Representative maps, real controllers, long soak, and staged
  hardware remain required.
  Follow-up audio-on XEMU/LLE proof:
  `scripts/output/hm-split-audio-4listeners-4p_hm_borg1_20260820_135837.report.txt`
  used 2026-08-20 13:56:02 `default.xbe` and 13:57:18 `efmp.xbe`, exited
  alive, and proved the Xbox audio backend is initialized in Holomatch
  (`backend=0x00000004`) with four active compiled listeners
  (`listener=0x00040004`) and P2-P4 listener updates
  (`listenerMask=0x0000000e`). The same run logged 168 sound registrations,
  226 starts, 460 loops, 885 respatializes, 20 voice starts, and repeated
  `lipActive` samples. This supersedes the earlier single-listener audio proof.
  Actual audible 4P mix quality, staged hardware behavior, and the known
  voice-line/face-animation issue remain open. The focused split verifier can
  now require this evidence from screenshot-free monitor reports with
  `--audio-only --require-audio-backend --require-audio-listeners
  --min-audio-starts 100 --min-audio-voice-starts 8
  --require-audio-lip-active`, or can apply the same audio flags alongside the
  normal split gates for returned full logs. The production hardware verifier
  can require the same audio subset when `--require-hm-split-log` is combined
  with the matching `--hm-split-require-audio-*` options. Guest/game-clock FPS averaged
  37.3 (35.1 minimum), while delivered XEMU wall-FPS averaged 9.9, so staged
  hardware proof is required before making an audio-on 4P performance call.
  Current combined visual/audio XEMU/LLE proof after the dead split-pane
  fallback and dead virtual-player respawn-input change:
  `scripts/output/hm-split-deadview-fix-4p_hm_borg1_20260820_143939.report.txt`
  and
  `scripts/output/hm-split-deadview-fix-4p_hm_borg1_20260820_143939_contact.png`.
  That run used 2026-08-20 14:37:05 `default.xbe` and 14:38:29 `efmp.xbe`,
  exited alive, and the sampled contact sheet shows all active P2-P4 split
  panes rendering live `hm_borg1` geometry instead of the earlier broad HOM
  failure. It did not catch a local death, so the new
  `STEFX_HM_SPLIT_DEAD_CMD`/`STEFX_HM_SPLIT_DEAD_VIEW` breadcrumbs still need
  a returned death/respawn log or longer soak before the death-path fix is fully
  proven. Audio stayed active in the same visual pass with backend `0x4`,
  listener state `0x00040004`, listener mask `0x0000000e`, 174 registrations,
  269 starts, 595 loops, 1048 respatializes, and 16 voice starts. Guest/game
  FPS averaged 42.4 with a 33.3 minimum and 0/4 below 30; delivered XEMU
  wall-FPS averaged 14.6 with screenshot/poll overhead, so staged hardware
  remains the performance authority.
  Current 4P economy-mode proof after skipping dynamic lights, flares, and
  Xbox world effects only for `stefx_hm_split_economy 1`, plus fast local
  split-player respawn:
  `scripts/output/hm-split-economy-fastrespawn-4p_hm_borg1_20260820_150454.report.txt`
  and
  `scripts/output/hm-split-economy-fastrespawn-4p_hm_borg1_20260820_150454_contact.png`.
  That run used 2026-08-20 15:02:27 `default.xbe` and 15:03:49 `efmp.xbe`
  (`efmp.xbe` SHA256
  `151DAE9953D3BBE2C84A6C9F4FAC6353200BAFB3F122BF7F15F2825E68ED5792`),
  exited alive, and visually shows all four Holomatch panes rendering live
  `hm_borg1` geometry and per-pane HUDs. This supersedes the broad P2-P4 HOM
  failure as current progress proof, but it is not visual signoff: lower panes
  can still expose black void/portal-like gaps from some angles. Guest/game FPS
  averaged 36.6 with a 22.0 minimum and 1/7 samples below 30; delivered XEMU
  wall-FPS averaged 26.0 with 4/6 samples below 30. The same report passes
  the focused audio verifier with backend `0x4`, four listeners, listener mask
  `0x0000000e`, 172 registrations, 620 starts, 1348 loops, 2230 respatializes,
  36 voice starts, and `lipActive=1`. Staged hardware remains required before
  accepting 4P performance or audible mix quality.
  Newest XEMU/LLE self-filter proof after widening the hosted Holomatch
  secondary-pane self-model suppression:
  `scripts/output/hm-split-self-filter-4p_hm_borg1_20260820_151509.report.txt`
  and
  `scripts/output/hm-split-self-filter-4p_hm_borg1_20260820_151509_contact.png`.
  That run used 2026-08-20 15:12:35 `default.xbe`
  (`SHA256 9B49A9143D3C018E539C9E0B7C4B8F6126F9E717DB0D6EA13F9CB0EA2ED6E28E`)
  and 15:13:56 `efmp.xbe`
  (`SHA256 891D164B8162FFE6ECA3C623EF9B5AB526D85BB8B829E2A6EA3F1F7DEBB66FE9`),
  exited alive, and visually shows all four panes rendering live `hm_borg1`
  geometry and per-pane HUDs. The previous broad P2-P4 HOM/void pattern is not
  visible in this sample; some views remain close to dark map walls, so this
  improves the visual proof but does not close full map/angle qualification.
  Guest/game FPS averaged 43.0 with a 33.0 minimum and 0/7 samples below 30;
  delivered XEMU wall-FPS averaged 29.5 with 3/6 samples below 30. The same
  report passes the focused audio verifier with backend `0x4`, four
  compiled/active listeners, listener mask `0x0000000e`, 173 registrations,
  514 starts, 1239 loops, 2202 respatializes, 37 voice starts, and
  `lipActive=1`. Staged hardware remains required before accepting 4P
  performance or audible mix quality.
  Newest XEMU/LLE proof after adding per-pane first-person weapon clones for
  external split views:
  `scripts/output/hm-split-viewweapon-4p_hm_borg1_20260820_153113.report.txt`
  and
  `scripts/output/hm-split-viewweapon-4p_hm_borg1_20260820_153113_contact.png`.
  That run used 2026-08-20 15:28:28 `default.xbe`
  (`SHA256 B62E7049FE508FC56BEFA2C681BE75C0DF9A89E367244CB314E28B2A0689D832`)
  and 15:29:55 `efmp.xbe`
  (`SHA256 9F039285194ACDFFFCA9DD60D479DEB5E4DCFEB80B1CF98B4A5CB7A6B07DFC2E`),
  exited alive, and visually shows all four panes rendering live `hm_borg1`
  geometry, per-pane HUDs, and first-person weapons. This closes the immediate
  P2-P4 no-weapon regression for the current XEMU proof window. Guest/game FPS
  averaged 43.7 with a 31.2 minimum and 0/7 samples below 30; delivered XEMU
  wall-FPS averaged 28.6 with 3/6 samples below 30. The same report passes the
  focused audio verifier with backend `0x4`, four compiled/active listeners,
  listener mask `0x0000000e`, 176 registrations, 543 starts, 1408 loops, 2281
  respatializes, 33 voice starts, and `lipActive=2`. Staged hardware remains
  required before accepting 4P performance or audible mix quality.
  Superseding no-log monitor and visual proof after exporting split proof
  arrays and tightening the virtual attack/state evidence:
  `scripts/output/hm-split-monitor-proof-4p-v3_hm_borg1_20260820_160457.report.txt`
  and
  `scripts/output/hm-split-viewweapon-proof-4p-v3_hm_borg1_20260820_160815.report.txt`
  with contact sheet
  `scripts/output/hm-split-viewweapon-proof-4p-v3_hm_borg1_20260820_160815_contact.png`.
  These runs used 2026-08-20 16:00:43 `default.xbe`
  (`SHA256 277357E319D1B40610E5E8E4FD92201D1357F4CA2A88B67752771A5BCB48946F`)
  and 16:02:17 `efmp.xbe`
  (`SHA256 55A9969954132F3FAF9C7EEBB8A35FB5ADFF2D46ABDE5DD8CB2FE71FE6CB6A13`).
  Both reports pass `verify_holomatch_split_log.py` with required four local
  split players, unique P1-P4 virtual commands including attack, P2-P4
  external refdefs/snapshots, positive P2-P4 view-weapon clones, per-pane
  HUD/status/divider proof, self/first-person model filtering, memory headroom,
  and audio backend/listener/voice/lip activity. The contact sheet visually
  shows live P1-P4 `hm_borg1` quadrants with first-person weapons in every
  pane, closing the returned P2-P4 no-weapon regression for this proof window.
  The monitor run exited alive at 31.3 guest/game FPS average, 24.2 minimum,
  4/9 samples below 30, 22.3 wall-FPS average, 12.1 minimum, 6/8 below 30,
  largest free block minimum 7.26 MiB, `bots=0`, and audio max voice starts
  54/`lipActive=1`. The visual run exited alive at 31.1 guest/game FPS average,
  24.5 minimum, 3/8 below 30, 25.6 wall-FPS average, 12.1 minimum, 4/7 below
  30, largest free block minimum 7.26 MiB, `bots=0`, and audio max voice starts
  67/`lipActive=2`. Bots and accepted staged hardware performance remain open.
  Superseding first-person weapon proof now comes from each local player's
  authoritative official EF player state instead of renderer-side P1 clones.
  The opt-in `stefx_hm_split_weapon_proof 1` diagnostic grants real ownership
  and ammo, then requests P1-P4 weapons 1/2/3/4 through the normal
  `usercmd.weapon` and `PM_BeginWeaponChange`/`PM_FinishWeaponChange` path.
  Evidence is
  `scripts/output/hm-split-distinct-weapons-4p_hm_borg1_20260820_200111.report.txt`
  with contact sheet
  `scripts/output/hm-split-distinct-weapons-4p_hm_borg1_20260820_200111_contact.png`.
  The run used `efmp.xbe` SHA256
  `2A38D2BF44CF39C58671DD89938322A8EF5047491080288425BA188E4DB9ED38`,
  exited alive, kept three bots active, and held the texture allocator exactly
  at 2,985,856 bytes used with a 7,499,904-byte largest free block. Every one
  of the five saved 640x480 frames was reviewed individually: all four panes
  contain complete `hm_borg1` geometry and visibly distinct phaser,
  compression-rifle, IMOD, and scavenger-rifle first-person models. Monitor
  samples repeatedly agree across authoritative state, requested command, and
  cgame submission for weapons 1/2/3/4. P2 and P4 briefly showed their spawn
  phasers after deaths while commands continued to request 2/4, then normal
  weapon selection restored P2; longer respawn/reselection proof remains open
  for P4. The run averaged 18.4 guest/game FPS (13.2 minimum); screenshot and
  monitor stalls reduced delivered wall FPS to 13.0 average, so this is visual
  and ownership proof rather than accepted performance evidence. The official
  bottom HUD was still replicated from P1 in this older proof; the newer
  independent official-HUD proof below supersedes that limitation.
  The exact current binary was then rebuilt with deterministic virtual fire
  release during weapon changes and qualified in
  `scripts/output/hm-split-distinct-weapons-respawn-4p_hm_borg1_20260820_200641.report.txt`
  with contact sheet
  `scripts/output/hm-split-distinct-weapons-respawn-4p_hm_borg1_20260820_200641_contact.png`.
  Its `efmp.xbe` SHA256 is
  `69C98B693743CF9F7F1BC2FD4646E503311C15F8BA335A5C51675A8F76DE89A0`.
  The run exited alive with three bots and fixed 2,985,856-byte texture use;
  all six frames were inspected individually and retain complete non-HOM
  geometry plus the four distinct weapon models. It also caught P2 and P3
  respawns: P2 returned from 3 to 124 health with weapon 2 restored, while P3
  exposed a transient spawn weapon 1/requested weapon 3 sample and then
  returned to authoritative weapon 3 and its IMOD view model. P4 remained
  alive on weapon 4 throughout. Guest/game FPS averaged 17.6 with a 13.6
  minimum and screenshot-heavy wall FPS averaged 9.5; staged hardware and a
  screenshot-free comparison remain required for performance.
  Superseding independent official-HUD proof is
  `scripts/output/hm-split-official-hud-final-4p_hm_borg1_20260820_202836.report.txt`
  with contact sheet
  `scripts/output/hm-split-official-hud-final-4p_hm_borg1_20260820_202836_contact.png`.
  It uses 4,182,016-byte `efmp.xbe` SHA256
  `7E901F31D15902B9D22DA459F770AE2C39718D8E7FF4CDE29F53E83EBB724AFB`
  and runtime ID `Aug 20 2026 20:26:39`. The official cgame HUD helper installs
  each local client's authoritative player state, entity weapon, split refdef,
  weapon-select timing, crosshair state, and low-ammo state, then draws that
  player's status/ammo/crosshair/weapon-select/holdable/score/powerup subset.
  The syscall bridge routes each pass only to its slot; all four records report
  `shared=0` and exact 320x240 quadrant bounds. The old compact top diagnostic
  overlay is absent. All four successful frames were inspected sequentially
  and show complete non-HOM `hm_borg1` geometry, phaser/compression/IMOD/
  scavenger first-person models, and visibly different official health/ammo
  values. P3's death/respawn/IMOD-restoration transition also restored its own
  HUD. The focused verifier passes four players, three bots, unique controls
  and attacks, movement, matching external clients, positive draws, distinct
  weapons, independent HUDs, dividers, 30.1 seconds of heartbeat progress,
  6,416,704-byte minimum largest-free memory, and 117,204-byte memory growth.
  Texture use remained fixed at 2,985,856 bytes with 7,499,904 bytes free.
  The process was alive at controlled shutdown. Guest/game FPS averaged 18.0
  with a 12.5 minimum and polling-heavy wall FPS averaged 13.5; this is exact
  binary visual/behavior proof, not hardware performance acceptance.
  Remaining HUD scope is independent zoom state and per-player pickup/reward/
  attacker announcements. Real controller assignment, audible four-listener
  mix quality, and staged hardware FPS/stability remain open.
  The global-overlay policy now draws vote/warmup/center-text/scoreboard once
  at full-screen coordinates while retaining per-player HUD elements in their
  quadrants. Its exact clean-stage build is the 4,190,208-byte `efmp.xbe` with
  runtime ID `Aug 20 2026 22:01:06` and SHA256
  `CBAF7186CE482F9F1F28B3BFC9AD7EE2ABD2151161C9C52AA07986A25E09D960`.
  Visual proof is
  `scripts/output/hm-split-scoreboard-text-cleanproof2-4p_hm_borg1_20260820_221901.report.txt`
  with contact sheet
  `scripts/output/hm-split-scoreboard-text-cleanproof2-4p_hm_borg1_20260820_221901_contact.png`.
  The only captured frame was inspected sequentially and shows one official
  scoreboard spanning four complete live `hm_borg1` worlds. It contains real
  player rows and the fixed labels `NAME`, `SCORE`, `TIME`, `PING`, `Players`,
  and `FREE FOR ALL`; the prior `?` labels are gone. Direct runtime reads prove
  the clean stage selected retail `PAK2.PK3`: `mp_ingametext.dat` loaded at
  3,117 bytes, all 184 entries parsed, validation state reached `0x1ff`, and
  the named text slots contain their expected strings. `run_sp_xemu_smoke.ps1`
  now exposes `-CleanStageData` to apply release-data cleanup without stripping
  direct-test controls, preventing stale loose seed files from overriding PK3
  authority in diagnostic proofs.
  The companion screenshot-free qualification report is
  `scripts/output/hm-split-scoreboard-text-cleanqual-4p_hm_borg1_20260820_222201.report.txt`.
  It passes `verify_holomatch_split_log.py` with four distinct moving/attacking
  control streams, matching P2-P4 external clients/refdefs/snapshots, positive
  P1-P4 draws and P2-P4 view weapons, four independent HUD quadrants, three
  bots, and a live process at controlled shutdown. Six heartbeat samples
  averaged 18.4 guest/game FPS, ranged from 14.0 to 21.1 FPS, and kept minimum
  largest-free memory at 6,472,928 bytes with 163,927 bytes used-memory growth.
  This accepts the global scoreboard placement and English text path in XEMU;
  it does not accept four-player hardware performance or soak stability.
  The natural front-end/XBE handoff is now proven on the fresh release pair:
  4,395,008-byte `default.xbe`, runtime ID `Aug 20 2026 22:38:08`, SHA256
  `4CFAC4E5C25AF183D1B64DE21DA89E8A79AFC73EF331AB1E6AC08B17C638D22B`;
  and 4,190,208-byte `efmp.xbe`, runtime ID `Aug 20 2026 22:39:49`, SHA256
  `B5C3976DC6E03A5A30280B2D4B6BAA408E80E5F6AF55A08896BA3452A8742F9D`.
  The menu smoke path drove three real `A_CURSOR_DOWN` events and one
  `A_ENTER` through `EFFe_HandleMainKey`, then used the same Holomatch accept
  handler as an ordinary front-end selection. Visual evidence is
  `scripts/output/hm-menu-handoff-visualproof_normal_20260820_225606.report.txt`
  with contact sheet
  `scripts/output/hm-menu-handoff-visualproof_normal_20260820_225606_contact.png`.
  All four source captures were inspected sequentially. Each shows four
  distinct live `hm_borg1` views, independent official HUD values, phaser
  models in every quadrant, and visible beam/flare effects. The fourth capture
  still has a localized black/missing-surface-looking far-wall patch in the
  top-right and a distorted-looking bottom-left floor region. This is not the
  former broad P2-P4 HOM failure, but it remains a visual blocker pending a
  targeted angle repro and representative-map coverage.
  Exact-XBE telemetry proof from the same immutable ISO is
  `scripts/output/hm-menu-handoff-xbetelemetry_normal_20260820_230247.report.txt`.
  It passes
  `verify_holomatch_split_log.py --require-launch-source xbe`, proving the
  launched `efmp` personality received the XBE handoff intent and started four
  local players with synthetic P1-P4 controls. All four clients moved and
  attacked with distinct command profiles; P2-P4 had matching external
  refdefs/snapshots and positive view-weapon submissions; all four render
  passes had positive draw counts and exact independent 320x240 HUDs; three
  bots joined; heartbeats and memory advanced; and the process was alive at
  controlled shutdown. Its `map='normal'` field records the harness's normal
  boot context, while the launched Holomatch BSP shown visually is `hm_borg1`.
  The longer reduced-poll run
  `scripts/output/hm-menu-handoff-perfsoak_normal_20260820_230703.report.txt`
  also passes the strict split and audio gates with `source=xbe`, three bots,
  four independent move/attack streams, all four views/HUDs/weapons, positive
  P2-P4 snapshot adds, first-person filter slots 1-3, and 90.5 seconds of
  measured gameplay. It exited alive with 422,978 bytes of sampled used-memory
  growth, 13,151,855-byte minimum free memory, and 6,430,272-byte minimum
  largest-free memory. Audio held backend `0x4`, four compiled/active listeners,
  listener mask `0x0000000e`, 219 registrations, 2,152 starts, 5,123 loops,
  2,478 respatializes, 158 voice starts, and `lipActive=2`. Audible mix quality
  and the separate missing voice-line/face-animation defect remain open.
  Guest/game FPS averaged 23.5, ranged from 16.0 to 35.9, and was below 30 in
  12/14 samples. Polling-heavy wall FPS averaged 15.8. The user's roughly
  30-FPS uninstrumented XEMU overlay observation remains separate context;
  staged retail hardware is still the performance and long-soak authority.
  The 2026-08-21 guest-phaser correction supersedes the earlier high-origin
  P2-P4 beam behavior without cloning or rebasing P1 renderer entities.
  First-person beam submission now follows each active split player's official
  EF `EF_FIRING` state; the split renderer filters only that pane's own
  third-person world beam. Independent official player-state weapon ownership,
  switching, respawn restoration, and pickup behavior remain the authority.
  The exact current release pair is 4,395,008-byte `default.xbe`, runtime ID
  `Aug 21 2026 02:46:53`, SHA256
  `CFC800C2C84B521D85D6C55185DCD4F421FFCCDE048D2B4326569EAFD9157CD3`,
  and 4,194,304-byte `efmp.xbe`, runtime ID `Aug 21 2026 02:47:00`, SHA256
  `8EEC18D94C36246BE85594258D1B3625FA85403341E0D640E92E50A63CCEFF94`.
  Deterministic visual proof is
  `scripts/output/hm-phaser-all-guests-proof_hm_borg1_20260821_024807.report.txt`.
  Each of its four successful captures was inspected sequentially and shows
  P2-P4 beams beginning at the corresponding first-person phaser muzzle and
  converging on that pane's crosshair. Telemetry reached 1,045 first-person
  beam-line submissions for every guest, tagged uniquely as `0x53504201`,
  `0x53504202`, and `0x53504203`, with own-pane world copies filtered. The
  process remained alive and averaged 25.0 guest/game FPS (24.0 minimum); this
  deliberately static proof is not an accepted performance measurement.
  Normal three-bot regression evidence is
  `scripts/output/hm-post-phaser-normal_hm_borg1_20260821_025108.report.txt`.
  All three successful captures were inspected sequentially and retain the
  corrected P2-P4 muzzle origins during ordinary movement, combat, and
  respawns. It remained alive and averaged 21.2 guest/game FPS (17.3 minimum)
  under heavy screenshot/monitor overhead.
  The reduced-poll 94.6-second normal soak is
  `scripts/output/hm-post-phaser-hom-soak_hm_borg1_20260821_025508.report.txt`.
  It remained alive with four local players and three bots. All 21 successful
  captures were inspected sequentially; every pane keeps coherent `hm_borg1`
  world geometry, independent HUDs, and weapons. The dark P3 view at 45.3
  seconds contains stable patterned doorway/interior geometry and lit lower
  supports, and later P3 views traverse the same corridor cleanly, so it is
  not classified as HOM. This bounded sample did not reproduce the earlier
  broad or localized void pattern, although representative maps and wider
  angle coverage remain required for visual signoff. Audio remained active
  with backend `0x4`, listener mask `0x0000000e`, 114 voice starts by the final
  sample, and observed `lipActive=2`. Guest/game FPS averaged 21.8 (17.2
  minimum); screenshot/poll-heavy wall FPS averaged 15.9. Minimum sampled free
  memory was 13,176,703 bytes, minimum largest-free memory was 6,459,360 bytes,
  and sampled used-memory growth was 192,718 bytes. This is visual/audio/HOM
  soak evidence rather than a strict four-control qualification pass: only
  60.3 seconds landed inside the heartbeat measurement window, and P1 remained
  stationary while P2-P4 moved. Real controller assignment, audible
  four-listener mix quality, representative-map coverage, a materially longer
  strict soak, and staged retail hardware FPS/stability remain open.
  A second-map visual pass on the same exact binaries is
  `scripts/output/hm-post-phaser-representative-dn1_hm_dn1_20260821_030408.report.txt`.
  It remained alive, reached three bots, and kept the texture allocator fixed
  at 3,206,016 bytes used. All five successful captures were inspected
  sequentially. Every quadrant retains coherent `hm_dn1` geometry through
  atrium, corridor, stair, close-wall, combat, and effect views; P2-P4 retain
  first-person weapons, and visible guest beams begin at the correct muzzle.
  No HOM or void appears in this bounded sample. Guest/game FPS averaged 17.2
  (15.3 minimum) under heavy visual polling. This is representative-map visual
  evidence only: the report contains no audio samples, P1's virtual command
  remained stationary, and it does not qualify controls, audio, or performance.
