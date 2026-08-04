# Elite Force X Beta Qualification Snapshot

Date: 2026-08-01

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
- Four-player split-screen remains future work.
