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

## Cleanup

Removed transient `repack_sp_*_20260704_*.log` scratch logs after preserving
the contact sheets, report files, and final memory dumps.

## Status

The RC visual soak is improved but not complete. Remaining work should include
longer XEMU co-op duration runs and directed human review of third-person
camera behavior, especially borg3 and borg6.
