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

## XDK Instrumented-D3D Finding

Date: 2026-07-31.

An isolated XDK 5558 `d3d8i.lib` build sampled the driver over repeated
120-frame windows on `borg1`. A captured complete window reported:

- 66,837,660 pushbuffer bytes, or 556,981 bytes per frame.
- 19,680 `SetStreamSource` calls, or 164 per frame.
- 9,605 `SetTexture` calls, or about 80 per frame.
- 16 pushbuffer segments across the 120 frames.
- Zero pushbuffer, Present, object-lock, idle, vblank, fence, and CPU-spin
  waits.

The instrumented library reduced gameplay from the accepted 18 FPS baseline to
about 16 FPS, so its framerate is diagnostic overhead and not a candidate
result. The absence of driver waits rules out a hidden synchronization or
presentation stall as the primary deficit. The unusually large command stream
instead supports candidate 1: the current `DrawPrimitiveUP` and
`DrawIndexedPrimitiveUP` submission path copies vertex/index payloads into the
pushbuffer, work that CXBX-R HLE largely bypasses but XEMU/LLE must process.

The diagnostic source and build-mode changes were removed after capture.

## Rejected Candidate: Fence-Free Persistent Primitive Streams

Date: 2026-08-01.

Two fence-free append-only stream variants were qualified against the accepted
18.0 FPS `borg1` gameplay baseline:

- Combined vertex/index streaming rendered correctly and survived its soak,
  but gameplay averaged 17.0 FPS.
- A narrower vertex-only stream left indexed gameplay submission untouched and
  also rendered correctly, but gameplay averaged 14.7 FPS, with a 10.6 FPS
  minimum and 17.0 FPS maximum.

Both variants used `D3DLOCK_NOOVERWRITE`, tested `IsBusy` only on wrap, and
fell back to the existing immediate path instead of waiting. The results show
that replacing `Draw*PrimitiveUP` with lock/copy/bind calls increases LLE cost
in this engine even without an explicit synchronization fence. All stream code
was removed.

## Runtime Logging Cost

Date: 2026-07-31.

The normal release logger is not a leading explanation for the gameplay
framerate. In the measured `borg1` run, the shipping logger produced 17 writes
over the final 45 seconds of gameplay, or about 0.38 writes per second. Text
heartbeats are emitted only once every 10 seconds and explicitly do not flush
the file; noncritical records flush only after 32 writes or 64 KiB.

The instrumented D3D run did reduce gameplay from about 18 FPS to about 16 FPS,
but that was deliberately high-volume diagnostic overhead. It establishes an
upper bound for intrusive logging, not the cost of the normal sparse release
path.

## Rejected Candidate: Minimal Runtime Logger

Date: 2026-08-01.

The remaining CPU cost of suppressed logging was tested directly. In the
candidate build, the three legacy filtered entry points returned immediately
when verbose logging was disabled, bypassing their critical-format substring
scan as well as all normal ring and file writes. Explicit critical logging,
exported heartbeat fields, and exported performance counters remained active.

The candidate completed the controlled `borg1` qualification and rendered
correctly. Settled gameplay averaged 17.6 FPS with a 16.4 FPS minimum and
16.9 FPS p10, below the accepted 18.0 FPS baseline. Runtime log filtering and
normal log I/O are therefore not a material contributor to the current frame
cost. The logger was restored exactly and the candidate was not committed.

## Rejected Candidate: Remove Redundant Begin-Frame Depth/Stencil Clear

Date: 2026-08-01.

The compatibility layer's frame-start clear was reduced from color, depth, and
stencil to color only. The renderer still cleared depth and conditionally
stencil in `RB_BeginDrawingView`, while the retained color clear preserved the
campaign title-crawl background. The candidate rendered correctly but settled
`borg1` gameplay averaged 17.8 FPS versus the current-code 19.0 FPS reference.
The full clear was restored and the candidate was not committed.

## Rejected Candidate: Immediate Presentation

Date: 2026-08-01.

The Xbox device's `FullScreen_PresentationInterval` was changed in isolation
from `D3DPRESENT_INTERVAL_DEFAULT` to `D3DPRESENT_INTERVAL_IMMEDIATE`. XDK 5558
performance and pushbuffer samples use `IMMEDIATE`, so this directly tested
whether the normal `EndFrame` cost was primarily presentation pacing.

The candidate rendered correctly and completed the `borg1` soak, but settled
gameplay averaged 17.4 FPS with a 16.2 FPS minimum and 17.1 FPS p10, below the
accepted 18.0 FPS baseline. The setting was restored to `DEFAULT`; no candidate
code was committed.

## Rejected Candidate: Vertex-Shader/FVF State Cache

Date: 2026-08-01.

The accepted render-state cache was extended in isolation to fixed-function
vertex-shader/FVF selection. All direct setters in the active renderer were
routed through one coherent cache so special effects could not leave stale
state.

The candidate rendered correctly. An attached 120-second settled gameplay
sample averaged 17.86 FPS with a 17.5 FPS minimum and p10, effectively matching
but not improving the accepted 18.0 FPS baseline. This indicates the Xbox
driver already makes redundant FVF selection inexpensive relative to the
remaining workload. The cache candidate was fully removed and was not
committed.

## Rejected Retail Candidate: Remove Recurring Diagnostic Filesystem Polls

Date: 2026-08-02.

Retail photographs established a substantially worse baseline than XEMU:
approximately 0.5-1.1 FPS in the campaign title crawl, 2.3 FPS during loading,
3.0-3.7 FPS in scripted scenes, and 2.0 FPS in first-person gameplay. This
invalidates the earlier assumption that XEMU performance represented the
retail target.

The normal runtime was performing synchronous diagnostic marker probes during
ordinary play. The renderer checked up to three runtime drives every frame for
each of the render-probe, screenshot, and CXBX-throttle markers. Client and
server automation also polled command files during the first 20 active
seconds, even when no smoke harness was installed. These accesses are cheap on
an emulator's host filesystem but may be severe on retail optical or mounted
game volumes.

The candidate caches one-shot marker states, makes recurring screenshot checks
opt-in and one-second rate-limited, gates active-command polling behind the
smoke-harness marker, and reduces automatic framebuffer telemetry to the first
two frames. It changes both `default.xbe` and `efmp.xbe` through shared code and
does not alter packaged assets.

The follow-up retail run remained at approximately 2 FPS in first-person
gameplay. The frame heartbeat reported roughly 300-390 ms total frame times,
with 100-165 ms in server work and 180-240 ms in client work. The filesystem
poll cleanup is retained because those probes do not belong in normal play,
but it is rejected as the explanation for the retail performance deficit.

## Pending Retail Candidate: Native Inline Pushbuffer Submission

Date: 2026-08-02.

The retail profile reports approximately 365 immediate primitive submissions
per rendered frame. Those submissions currently use `DrawPrimitiveUP`, which
copies the supplied vertex payload into the Xbox command stream. The shared
FakeGL renderer already contains a direct `BeginPush` implementation matching
the XDK 5558 `Graphics\BeginPush` sample, but it was disabled while restoring
emulator rendering.

`Beta-20260802-retail-inline-push` enables that native path for non-indexed
primitives only. Its packet shape remains the XDK form: begin/end method words,
`D3DPUSH_NOINCREMENT_FLAG | D3DPUSH_INLINE_ARRAY`, and five DWORDs of packet
overhead. The line-strip vertex count was corrected from `primitiveCount + 2`
to `primitiveCount + 1` before activation. Indexed submissions, game code,
assets, and presentation settings are unchanged.

This candidate is intentionally hardware-first. The earlier disable was based
on emulator compatibility; the new retail timing data is the evidence that
justifies testing the Xbox-native path again on its actual target.
