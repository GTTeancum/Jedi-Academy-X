# Holomatch Qualification Snapshot

Date: 2026-07-22

## Current Staged Build

- Target: SP-hosted Holomatch development XBE.
- XBE: `C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\efmp.xbe`
- XBE SHA256: `3A5C0408F1B1D7B58234082BC2FEDBAF78BD897617D4B9F9D39D7734B4EFE96B`
- Package: `C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\BaseEF\xbox1.pk3`
- Package SHA256: `2E6E87F9C77E551195314BFA609DC6B180167AE3BE6B9778DD7B6451FF128DC6`
- Direct boot map: `hm_borg1`

## Build / Dependency Evidence

- Build command: `scripts\build_xbox.ps1 spmp`
- Build log: `build_spmp_hm_borg1_direct_restore_20260722.log`
- Build result: passed.
- Stage result: `efmp.xbe`, `BaseEF\xbox1.pk3`, and SP soundbank staged for CXBX-R.
- SP/co-op preservation: build reported `SP/co-op default.xbe was not touched`.
- `default.xbe` SHA256, repo and staged: `0116A113C44154B2CDAACB98AC6881C0E52DA2765AE390FB0479F471268E4C88`
- Code-only verifier:
  - `directBoot=hm_borg1`
  - `codempDependency=false`
  - 1021 `code/` source files checked
  - 33 optimized MP maps present
  - 33 AAS checksum patches present
  - 0 staged original images
  - 0 staged UI scripts

## Runtime Evidence

- CXBX/CodexCapture session: `stefx-all-mp-maps-20260722-134807`
- Result folder: `build/proofs/holomatch-map-sweep-20260722-134807`
- Map: `hm_borg1`
- Status: PASS
- Runtime:
  - 82 heartbeats
  - 65.0 FPS average
  - 60.0 FPS minimum
  - 0 low-FPS samples
  - memory 12008 KB -> 11796 KB
  - target BSP loaded
  - bots active
  - no fatal marker
  - no frame-rate issue
  - no memory issue
- Gameplay counters:
  - 34 ammo pickup events
  - 190 weapon-fire events
  - 46 damage events
  - 28 bot-to-player damage events
  - 18 phaser client-target events
- Visual proof:
  - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-134807\proofs\20260722-134920-hm_borg1.png`

## Final Loading-Screen Evidence

- CXBX/CodexCapture session: `stefx-all-mp-maps-20260722-140018`
- Result folder: `build/proofs/holomatch-map-sweep-20260722-140018`
- Maps: `hm_borg1`, `hm_for2`, `hm_raven`, `hm_dn1`, `ctf_dn1`
- Status: PASS for loading-screen visual signoff.
- Runtime:
  - all five target BSPs loaded
  - bots active on all five maps
  - no fatal marker
  - no memory issue
  - no capture issue
  - FPS stayed acceptable across the sequence
- Loading captures:
  - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-140018\proofs\20260722-140024-hm_borg1-loading-01.png`
  - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-140018\proofs\20260722-140055-hm_for2-loading-01.png`
  - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-140018\proofs\20260722-140126-hm_raven-loading-01.png`
  - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-140018\proofs\20260722-140154-hm_dn1-loading-01.png`
  - `C:\Games\Emulators\CXBX-CodexCapture\captures\stefx-all-mp-maps-20260722-140018\proofs\20260722-140212-ctf_dn1-loading-01.png`

## Signoff

- User signoff is complete for the current drivable Holomatch vertical slice.
- Final loading-screen signoff was received after the five-map continuous loading proof.
- The vertical-slice goal was marked complete on 2026-07-22.
