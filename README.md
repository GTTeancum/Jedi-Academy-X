# Star Trek: Elite Force X

Star Trek: Elite Force X brings the Elite Force campaign, two-player
cooperative play, and Holomatch to the original Xbox.

The project uses one shared Xbox engine/runtime source tree under `code/` while
keeping the game personalities separate:

- `default.xbe` runs single-player and two-player co-op.
- `efmp.xbe` runs Holomatch.
- Both executables use the shared `BaseEF` runtime.
- `codemp/` is historical and is not part of the active build.

## Beta Candidate

The current qualified package is:

`build/beta/StarTrekEliteForceX-Beta-20260801`

Launch `default.xbe`. The shared main menu can start campaign, cooperative
play, or hand off to `efmp.xbe` for Holomatch.

See `HOLOMATCH_QUALIFICATION.md` for current hashes, build checks, runtime
proof, and user signoffs. See `GAME_TODO.md` for post-beta work.

## Building

Requirements:

- Visual Studio 2005 compiler tools
- Xbox Development Kit headers, libraries, and tools
- Python 3
- FFmpeg for audio conversion
- `extract-xiso` for XISO creation
- Legally obtained Elite Force runtime data

Build SP/co-op:

```powershell
scripts\build_xbox.ps1 -Target sp
```

Build Holomatch from the shared engine:

```powershell
scripts\build_xbox.ps1 -Target spmp
```

Create a marker-free release XISO:

```powershell
scripts\run_sp_xemu_smoke.ps1 -CleanReleaseIso `
  -Iso build\xemu\StarTrekEliteForceX_Beta.iso
```

Create the checksum/manifest beta folder:

```powershell
scripts\package_beta.ps1
```

## Qualification

Current integration testing uses XEMU/LLE and XEMU-native screenshots. The
release path covers campaign, co-op split-screen, FFA/CTF Holomatch with bots,
and the `default.xbe`/`efmp.xbe` handoff in one continuous session.

Run `scripts\cleanup_generated.ps1` before and after emulator work. Keep one
current XEMU ISO and do not retain per-run staging trees.

Frame-rate optimization remains post-beta. Functional stalls, crashes, data
corruption, and visual/gameplay regressions remain release blockers.

## License

Source derived from the released Raven Software code remains subject to the
GNU GPLv2 terms in `LICENSE.txt`. Runtime game data is not provided by the
source repository.
