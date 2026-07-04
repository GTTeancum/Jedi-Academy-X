# XEMU RC Visual Soak - 2026-07-04

This note records the framebuffer evidence captured after commits `ecbfea3c`
and `742ea8ee`.

## Commands

- First-person sweep:
  `scripts/run_sp_xemu_smoke.ps1 -Repack -Maps borg3,borg6,forge5 -Name xemu_rc_firstperson_post_ringfix -Duration 90 -Interval 20 -Port 4520 -PollXBlog -XBlogAutoDumps -Command "set stefx_splitScreen 1,set stefx_splitScreenPlayers 2,set cg_thirdPerson 0,set stefx_splitScreenTestP2Input 1,set stefx_smoke_fasttime 0,timescale 1"`
- Third-person sweep:
  `scripts/run_sp_xemu_smoke.ps1 -Repack -Maps borg3,borg6,forge5 -Name xemu_rc_thirdperson_post_ringfix -Duration 90 -Interval 20 -Port 4521 -PollXBlog -XBlogAutoDumps -Command "set stefx_splitScreen 1,set stefx_splitScreenPlayers 2,set cg_thirdPerson 1,set stefx_splitScreenTestP2Input 1,set stefx_smoke_fasttime 0,timescale 1"`

## Evidence

All six XEMU runs exited with code 0 and `alive_at_end`.

- First person:
  - `scripts/output/xemu_rc_firstperson_post_ringfix_borg3_20260704_145921_contact.png`
  - `scripts/output/xemu_rc_firstperson_post_ringfix_borg6_20260704_150347_contact.png`
  - `scripts/output/xemu_rc_firstperson_post_ringfix_forge5_20260704_150726_contact.png`
- Third person:
  - `scripts/output/xemu_rc_thirdperson_post_ringfix_borg3_20260704_151338_contact.png`
  - `scripts/output/xemu_rc_thirdperson_post_ringfix_borg6_20260704_151725_contact.png`
  - `scripts/output/xemu_rc_thirdperson_post_ringfix_forge5_20260704_152122_contact.png`

## Findings

- The large black/red compression rifle muzzle disk did not recur.
- First-person split-screen weapons rendered in both player viewports on borg3,
  borg6, and forge5.
- Third-person P2 camera no longer showed the earlier broad black/void camera
  failure on forge5.
- Borg3 third-person still has occasional obstructed camera composition when P2
  moves close to structure. This needs human sign-off or a later camera-tuning
  pass, but it did not stall the run.
- Borg6 still reports fallback-looking P2 raw state in telemetry (`wp=2/0` and
  zero P2 viewheight in some samples), while the visible first-person weapon
  still renders via fallback. Treat this as a watch item for co-op gameplay
  correctness, not a closed release item.

## Follow-up: P2 duplicate-add and raw-state audit

- The old duplicate-add/supplement path was a split-screen bootstrap for cases
  where the primary snapshot did not include the P2 body or nearby actors from
  P2's view. It could add the same P2 actor once as world geometry and again as
  the view-local self/body proxy, which explains the tearing/duplication seen
  when both paths survived in the same frame.
- The weapon draw path is separate from the scene supplement path. First-person
  weapon telemetry is recorded under `vw=...`, while the P2 body supplement is
  recorded under `scene=...`; limiting duplicate body adds should not be treated
  as a weapon-render fix.
- Borg6 telemetry still shows the renderer relying on effective P2 fallbacks:
  `hgt=28/0/32/16`, `wp=2/0`, and `eff=28/0/2`. That means the visible frame can
  look acceptable while P2's raw client state remains incomplete. This needs a
  state hygiene fix or a source-of-truth fix before calling the RC soak clean.

## Follow-up: Borg6 game-side P2 telemetry

- Added game-side P2 telemetry (`game=weapon/viewheight/stateWeapon/clientNum`)
  next to the existing renderer-side `hgt`, `wp`, and `eff` fields.
- A Borg6 proof with startup split commands still showed renderer-side P2
  fallback (`hgt=28/0/32/16`, `wp=2/0`, `eff=28/0/2`) and game-side zero state
  (`game=0/0/0/0`).
- A Borg6 proof with the same split commands moved to post-map command timing
  also still showed `game=0/0/0/0`, so the failure is deeper than startup-vs-
  post-map command ordering.
- Current evidence points to the game-side co-op lifecycle not owning the live
  Borg6 P2 actor, while cgame still renders an entity selected by
  `stefx_splitScreenP2Entity`. The next fix should instrument or repair the
  game-side P2 selection/activation path rather than adding more renderer
  fallbacks.

Evidence:

- `scripts/output/xemu_verify_borg6_p2_game_state_nodump_borg6_20260704_155648.report.txt`
- `scripts/output/xemu_verify_borg6_p2_state_hygiene_borg6_20260704_161201.report.txt`
- `scripts/output/xemu_verify_borg6_p2_state_postmap_borg6_20260704_161822.report.txt`

## Follow-up: Borg6 P2 lifecycle deadlock

- Added game-side P2 lifecycle telemetry (`glife=stage/split/players/p2ent/cache/p1ready`) to separate missing telemetry from missing simulation.
- A Borg6 proof before the fix reached `glife=60/1/2/238/238/1`, meaning P2 existed and P1 was ready, but the loop stopped at the P2 ready-for-control gate.
- The ready gate was circular: it rejected P2 while `EF_NODRAW` was still set, but the takeover path that clears `EF_NODRAW` only ran after that gate.
- Moved P2 takeover before the gate and added a one-shot flag transition breadcrumb in `STEFX_SplitCoopTakeControl`.
- The fixed Borg6 XEMU proof reached `glife=90/1/2/238/238/1`, with game-side `game=2/28/2/238`, renderer-side `hgt=28/28/32/16`, `wp=2/2`, and three successful 640x480 framebuffer captures.

Evidence:

- `scripts/output/xemu_verify_borg6_p2_lifecycle_borg6_20260704_165100.report.txt`
- `scripts/output/xemu_verify_borg6_p2_takecontrol_borg6_20260704_170946.report.txt`
- `scripts/output/xemu_verify_borg6_p2_takecontrol_capture_borg6_20260704_171330.report.txt`
- `scripts/output/xemu_verify_borg6_p2_takecontrol_capture_borg6_20260704_171330_contact.png`

## Follow-up: Post-takeover XEMU sweep

- Ran a third-person XEMU sweep across borg3, borg6, and forge5 after commit
  `88c250f1`. All three runs reached gameplay and exited cleanly with P2
  game-side state populated (`game=2/28/2/238` on borg6) and renderer-side P2
  state no longer relying on zero-height/zero-weapon fallbacks.
- The giant red/black weapon flare did not recur in these contact sheets, and
  P2 no longer appeared to be missing entirely from gameplay frames.
- Remaining visual concern: P2's lower viewport can still be crowded by team
  bodies or pushed into nearby walls/void-adjacent composition in third person.
  This looks like camera placement/composition rather than missing renderer PVS,
  because the renderer split path applies P1/source PVS data to the P2 refdef.
- A matching run named `xemu_rc_post_takecontrol_firstperson` was not valid
  first-person proof. The run did not repack the ISO, telemetry still reported
  `cam=1`, and the contact sheets were visually third-person. Future command
  changes that must affect the XEMU ISO need `-Repack` or another verified
  command-injection path.
- The duplicate-add/supplement path exists because the normal SP entity loop is
  built from P1's snapshot. In split-screen, P2 can need a body, actors, movers,
  items, or missiles that were not present in P1's snapshot. The risk is that
  supplementing P2's own actor in third person can overlap with the view-local
  body/self proxy path, which can look like duplicate tearing if both survive
  the same frame.

Evidence:

- `scripts/output/xemu_rc_post_takecontrol_thirdperson_borg3_20260704_172033_contact.png`
- `scripts/output/xemu_rc_post_takecontrol_thirdperson_borg6_20260704_172200_contact.png`
- `scripts/output/xemu_rc_post_takecontrol_thirdperson_forge5_20260704_172328_contact.png`
- `scripts/output/xemu_rc_post_takecontrol_firstperson_borg3_20260704_172942_contact.png`
- `scripts/output/xemu_rc_post_takecontrol_firstperson_borg6_20260704_173110_contact.png`
- `scripts/output/xemu_rc_post_takecontrol_firstperson_forge5_20260704_173237_contact.png`

## Follow-up: Repacked first-person XEMU proof

- Reran the first-person proof with `-Repack` so the XEMU ISO actually carried
  `cg_thirdPerson 0`. This replaces the invalid no-repack run above.
- Borg3, borg6, and forge5 all reached gameplay and produced three valid
  640x480 framebuffer captures each. The contact sheets show split HUD and
  first-person weapon rendering in both player viewports.
- Telemetry proves this was first-person (`cam=0`), with populated P2 state on
  all three maps: borg3 `game=2/28/2/251`, borg6 `game=2/28/2/238`, forge5
  `game=2/28/2/266`, and `glife=90/1/2/...`.
- The large red/black weapon flare still did not recur.
- The remaining release-readiness concern is composition rather than weapon
  draw: the lower/P2 viewport can be crowded by nearby bodies or wall geometry,
  especially with `stefx_splitScreenTestP2Input 1` driving automated movement.
  Treat this as a P2 spacing/camera/automation review item, not as solved.

Evidence:

- `scripts/output/xemu_rc_post_takecontrol_firstperson_repack_borg3_20260704_174448_contact.png`
- `scripts/output/xemu_rc_post_takecontrol_firstperson_repack_borg3_20260704_174448.report.txt`
- `scripts/output/xemu_rc_post_takecontrol_firstperson_repack_borg6_20260704_174739_contact.png`
- `scripts/output/xemu_rc_post_takecontrol_firstperson_repack_borg6_20260704_174739.report.txt`
- `scripts/output/xemu_rc_post_takecontrol_firstperson_repack_forge5_20260704_175002_contact.png`
- `scripts/output/xemu_rc_post_takecontrol_firstperson_repack_forge5_20260704_175002.report.txt`

## Follow-up: First-person baseline without automated P2 movement

- Reran the same first-person XEMU sweep with `stefx_splitScreenTestP2Input 0`
  to separate baseline camera/render quality from the automated P2 stress
  driver.
- Borg3, borg6, and forge5 all reached gameplay and produced valid framebuffer
  captures. Telemetry stayed in first person (`cam=0`) with populated P2 state:
  borg3 `game=2/28/2/251`, borg6 `game=2/28/2/238`, forge5
  `game=2/28/2/266`.
- Borg3 baseline looks clean: both viewports show stable first-person weapons,
  HUD, and readable world composition.
- Borg6 and forge5 remain visually compromised at rest: P2 is valid and armed,
  but the lower viewport starts too close to wall/column geometry. This makes
  the next release-readiness fix a P2 initial placement/camera-composition
  problem, not a missing weapon or missing split render problem.
- Removed the large scratch repack logs after preserving contact sheets and
  reports.

Evidence:

- `scripts/output/xemu_rc_firstperson_no_p2_auto_borg3_20260704_180731_contact.png`
- `scripts/output/xemu_rc_firstperson_no_p2_auto_borg3_20260704_180731.report.txt`
- `scripts/output/xemu_rc_firstperson_no_p2_auto_borg6_20260704_181130_contact.png`
- `scripts/output/xemu_rc_firstperson_no_p2_auto_borg6_20260704_181130.report.txt`
- `scripts/output/xemu_rc_firstperson_no_p2_auto_forge5_20260704_181504_contact.png`
- `scripts/output/xemu_rc_firstperson_no_p2_auto_forge5_20260704_181504.report.txt`

## Cleanup

Removed transient `repack_sp_*_20260704_*.log` scratch logs after preserving
the contact sheets, report files, and final memory dumps.

## Status

The RC visual soak is improved but not complete. Remaining work should include
longer XEMU co-op duration runs and directed human review of third-person
camera behavior, especially borg3 and borg6.
