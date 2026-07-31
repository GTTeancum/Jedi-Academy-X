# Non-JA Xbox Performance Candidate Ledger

Baseline: commit `b58046a` (`Cache redundant Xbox render state uploads`).

Qualification map: `borg1` in XEMU/LLE. The accepted gameplay result is
18.0 FPS average, 16.9 minimum, 17.5 p10, and 20.6 maximum.

Apply and qualify one candidate at a time. Commit a candidate only after it
improves the baseline without a visual, gameplay, stability, or mode-parity
regression. Remove a failed candidate before beginning the next one.

## Ranked Candidates

1. Persistent indexed vertex/index streaming without a per-frame GPU fence.
   - Basis: persistent dynamic buffers used by Mercenaries and Unreal
     Championship 2.
   - Expected gain: high if it replaces Xbox `DrawIndexedPrimitiveUP`, whose
     platform implementation expands and copies indexed data.
   - Risk: high; the first fence-based version did not reach the capture
     surface and was removed.

2. Pushbuffer capacity tuning.
   - Basis: Mercenaries uses a 768 KiB primary pushbuffer and Unreal
     Championship 2 uses 1024 KiB, both with a 32 KiB secondary buffer.
   - Expected gain: medium if the current frame incurs pushbuffer kicks or
     stalls.
   - Risk: low to medium; measure kick/stall counters before changing sizes.

3. Presentation and backbuffer policy tuning.
   - Basis: retail Xbox titles explicitly choose presentation interval,
     buffering, and swap policy around their frame targets.
   - Expected gain: medium only if the measured `EndFrame` cost is a wait
     policy rather than actual GPU work.
   - Risk: high; preserve image fidelity, pacing, split-screen, cinematics, and
     campaign/Holomatch loading layouts.

## Rejected Candidate: Fence-Based Persistent Indexed Stream

Date: 2026-07-31.

The candidate allocated two persistent 1 MiB vertex buffers and two 256 KiB
index buffers, appended indexed draws during each frame, inserted a fence at
frame end, and waited before reusing a slot. It compiled and linked.

The XEMU qualification harness produced no report and no screenshot before its
outer timeout. A second diagnostic launch was unavailable because the tool
usage gate rejected the emulator launch. The candidate therefore had no valid
runtime proof and was not committed.

All candidate code was removed. The accepted source was rebuilt successfully
with:

`scripts\build_xbox.ps1 -Target sp -ReuseObjects -SkipAssets`

## Rejected Candidate: XDK 5558 Whole-Program Optimization/LTCG

Date: 2026-07-31.

The candidate followed the XDK's non-JA `Glass` and `GlobalFX` sample
configurations: compile every C/C++ unit with `/GL` and `LTCG`, link with
`/LTCG`, and replace the normal graphics libraries with `d3d8ltcg.lib` and
`xgraphicsltcg.lib`.

The build completed whole-program code generation and produced a valid XBE
tagged with `D3D8LTCG` and `XGRAPHCL`. Native XEMU captures showed correct
`borg1` gameplay rendering. The user observed approximately 18 FPS after the
game entered actual gameplay, matching the accepted 18.0 FPS baseline.

LTCG therefore provided no material gain. The build-mode changes were removed
and the normal non-LTCG XBE was rebuilt.
