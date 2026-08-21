# Non-JA Xbox Performance Candidate Ledger

Baseline: commit `b58046a` (`Cache redundant Xbox render state uploads`).

Qualification map: `borg1` in XEMU/LLE. The accepted gameplay result is
18.0 FPS average, 16.9 minimum, 17.5 p10, and 20.6 maximum.

Apply and qualify one candidate at a time. Commit a candidate only after it
improves the baseline without a visual, gameplay, stability, or mode-parity
regression. Remove a failed candidate before beginning the next one.

## ACCEPTED: Bound The Server Catch-Up Loop

Date: 2026-08-06. Retail measured, first accepted candidate in this ledger.

`Com_ModifyMsec` clamps elapsed time to 200 ms and `sv_fps` is 20, so
`frameMsec` is 50 and `SV_Frame`'s `while (sv.timeResidual >= frameMsec)` was
free to run the whole game simulation four times per rendered frame. Because
each pass costs ~26 ms, those passes made the next frame later, which bought
another pass - the loop fed itself.

`sv_main.cpp` now caps the loop at one tick per frame and discards the unrun
backlog rather than carrying it forward. Client timing is untouched, so view and
input stay as responsive as the frame rate allows; only world simulation eases
off. Deliberately narrower than lowering `clampTime`, which would slow those too.

Retail, borg1, same content before and after:

```text
before  svtick=4/26/26  perf=254/106/111/0/16/50  fps=5.0
after   svtick=1/26/26  perf=130/29/101/0/16/41   fps=6.3-6.7
```

Server fell from 106 ms to 29 ms, frame from 254 ms to ~130 ms, and framerate
rose about 28%. Cost is a pacing change: while overloaded the world advances
slower than the wall clock. Raise `stefxMaxCatchupTicks` toward 4 to trade the
gain back for wall-clock accuracy.

This was the only change in its build, so the attribution is clean.

Where the frame now sits: the client is dominant at roughly 100 ms of a 130 ms
frame - about 59 ms assembling the scene, 41 ms submitting it. The single
server tick is 25-53 ms across 543 entities, all of it in the entity loop
(`gph=0/26/0`), with 112 movers and 23 clients unchanged run to run.

## The Original Xbox Renderer Has No Vertex Buffer At All

Date: 2026-08-05. Source:
`C:\Programming\GitHub\Jedi-Academy-X\clean-mp-original-build\codemp\win32\win_qgl_dx8.cpp`.

This is the unmodified Xbox MP renderer, before this project's edits. Its
`dllDrawElements` (L2353) is one path with no alternatives:

- `BeginPush(vert_size + index_size + 60, &glw_state->drawArray)` reserves
  pushbuffer space.
- `tess.xyz`, `tess.normal`, `tess.svars.colors` and `tess.svars.texcoords[]`
  are `memcpy`'d directly into that reservation.
- A jump address and `CMD_STREAM_STRIDEANDTYPE0` block set up what the comment
  calls "our own fake vertex buffer" pointing into the pushbuffer itself.
- Indices follow in the same reservation.

There is no `CreateVertexBuffer`, no `CreateIndexBuffer`, no `Lock`, no
`SetStreamSource` to a real buffer, no `DrawIndexedPrimitive`, no ring, and no
wrap. `BlockUntilIdle` appears exactly twice in the whole file - in `dllFinish`
and `dllFlush`, the `glFinish`/`glFlush` implementations. Never in a draw path.
There is also no gate: no `CanUseDrawElementsPush`, no reject reasons, no
fallback submitter.

Everything this ledger has been tuning - ring capacity, wrap stalls, lock flags,
buffer rotation, `SetStreamSource` caching, fences - belongs to a submission
design the shipping Xbox renderer never had. The measured retail spread across
ring shapes (0-2, 3-4 and 19-51 FPS) is the cost of that design, not a property
of the hardware.

The current tree does contain a `dllDrawElementsPush` reimplementation of this
approach, selected by `r_nativeDrawPath 1`, but it is gated by
`CanUseDrawElementsPush`, which rejects with reason 6 whenever
`texCoordPointer[0] != tess.svars.texcoords[0]` and routes the batch to
`dllDrawElementsUP` instead. On XEMU that rejection fired on every sampled
batch. The original never checks this because it copies from the tess arrays
unconditionally; restoring the original behaviour means copying from the bound
pointers rather than declining the batch.

## Why Every Candidate In This Ledger Reads "No Gain"

Date: 2026-08-05.

XEMU reports GPU synchronization as free, so no candidate that changes GPU
synchronization can be ranked on it. This is demonstrated, not assumed.

A `borg1` run forced to `r_nativeDrawPath 1` reported `path=1537` in
`xblognative`. That value is `r_nativeDrawPath | (s_nativePushRejectReason << 8)`
- reject reason 6, meaning `CanUseDrawElementsPush` rejected every sampled batch
because `texCoordPointer[0] != tess.svars.texcoords[0]`. Every draw therefore
fell through to `dllDrawElementsUP`, which submits via
`SubmitNativeTriangleListPush`, which begins with:

```c
glw_state->device->KickPushBuffer();
glw_state->device->BlockUntilIdle();
```

A full GPU idle per draw call. XEMU rendered that at 61.5 FPS average - within
noise of the 61.0 FPS the ring path scored on the same map. The worst possible
submission strategy and the intended one are indistinguishable there.

Every rejected candidate above was qualified on XEMU. That history is therefore
evidence about XEMU, not about the Xbox. Anything touching fences, locks,
`BlockUntilIdle`, `BlockUntilNotBusy`, presentation interval, or buffer reuse
must be measured on hardware or not at all.

## Corrections To Earlier Entries In This Ledger

- Pushbuffer capacity is **not** an open candidate. `win_qgl_dx8.cpp` already
  calls `SetPushBufferSize(1024 * 1024, 32 * 1024)` before `CreateDevice`,
  identical to Unreal Championship 2004's `D3DRenderDevice.cpp:1239`.
  Mercenaries uses `768*1024, 32*1024` in `xboxRedRenderer.cpp:264`. This was
  briefly and wrongly recorded as unset after a truncated search.
- Texture residency is not a candidate. `xbox_texture_man.h` is Vicarious
  Visions' original static allocator: one contiguous pool, no eviction, so
  there is no in-play re-upload path to thrash.

## Applied: Two Deviations From Shipped Xbox Practice, Corrected

Date: 2026-08-05. Both implemented and verified for correctness on XEMU. Neither
can be *scored* there, per the section above - they are hardware-only for FPS.

**1. Removed the full GPU flush from `SubmitNativeTriangleListPush`.**
It opened with `KickPushBuffer()` then `BlockUntilIdle()`, draining the GPU
before every batch. Inline-array submission does not require it: `BeginPush`
reserves command-stream space and the vertex payload is copied into that
reservation, not referenced from memory the GPU may still be reading.

Verified by forcing `r_nativeDrawPath 1`, where `CanUseDrawElementsPush` rejects
with reason 6 and every batch therefore takes this exact submitter. Rendering is
pixel-identical to the ring path with the idle gone, which is the proof the idle
was never load-bearing.

**2. Reshaped the dynamic buffers to the rotating form UC2004 ships.**
Was: one 256 KiB vertex ring plus one 128 KiB index ring, with an unconditional
`BlockUntilNotBusy` on every wrap. Now: four 64 KiB vertex buffers and four
32 KiB index buffers, rotating on overflow via `AdvanceRingBuffer`, which tests
`IsBusy()` and moves to a free buffer instead of waiting. Only if all four are
in flight does it block, and that residual case is still timed into
`g_SPXBNativeRingStallUsec`.

Total memory is 384 KiB, byte-for-byte identical to the 256 + 128 it replaces.
Worst-case single batch fits comfortably: `SHADER_MAX_VERTEXES` 1000 at the
44-byte maximum stride is 43 KiB against a 64 KiB buffer, and
`SHADER_MAX_INDEXES` 6000 at 2 bytes is 12 KiB against 32 KiB - so no batch can
be forced onto the fallback path by the smaller size. Observed offsets in a live
run stayed inside both bounds (`vbOff=39456`, `ibOff=14334`) with `path=2` and no
reject code, confirming the rotation is active rather than degrading.

## Original Findings Behind Those Two Changes

1. **Full GPU idle in the fallback submitter.** `SubmitNativeTriangleListPush`
   calls `KickPushBuffer` then `BlockUntilIdle` before every batch. No shipped
   reference does this, and the XDK `Graphics\BeginPush` sample it is modelled on
   does not. It is the fallback for any batch `CanUseDrawElementsPush` rejects,
   so its cost is paid per draw whenever the tessellator layout does not match
   the narrow accepted shape. Retail heartbeats have shown `f0`, so it may be
   dormant in the sampled scenes - but reject reason 6 fires readily on XEMU,
   so the gate is not as tight as `f0` suggests.

2. **Dynamic vertex buffer shape.** This renderer uses one 256 KiB ring and
   calls `BlockUntilNotBusy` unconditionally on wrap. Unreal Championship 2004
   uses 16 KiB dynamic vertex buffers on Xbox, tests `IsBusy()`, moves to
   another buffer rather than blocking, and never blocks on this path.
   `D3DResource.cpp:1483` carries the instrumented justification:

   ```c
   //scion jg -- With some instrumentation I found that many smaller
   // dynamic VBs performed better than a few large ones.
   #define INITIAL_DYNAMIC_VERTEXBUFFER_SIZE 16*1024   // Xbox
   #define INITIAL_DYNAMIC_VERTEXBUFFER_SIZE 65536     // PC
   ```

   Note the direction: smaller on Xbox than on PC. The rejected 2 MiB candidate
   above moved the opposite way.

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

## Rejected Retail Candidate: Native Ring Capacity vs Frame Payload

Date: 2026-08-05. Rejected the same day on retail evidence.

Outcome first: the sizing change did exactly what it was designed to do, it did
not matter, and it broke emulator qualification. It has been fully reverted to
256 KiB / 128 KiB.

The 2 MiB / 512 KiB rings hung XEMU at boot: black screen, guest frozen at
frame 9 with `writes` static and poll `delta=0` for a full 150 s run. The same
build ran normally on retail hardware, so the fault is emulator-specific, but it
costs the entire automated qualification path and is not worth carrying for a
candidate that gained nothing. Reverting the sizes restored XEMU immediately -
the next run reached live `borg1` gameplay at 57 FPS average with counters
advancing normally.

On the measurement itself: Retail heartbeats after the change reported wraps falling from
about 1.7 per frame to between zero and two per *second*, but the accompanying
stall measurement read `s0ms` on nearly every heartbeat, with isolated 10 ms and
17 ms windows. The wrap stalls this candidate removed were already cheap, so
removing them bought nothing. The premise below was wrong.

Keep the measurement, not the conclusion. The same run showed where the time
actually goes, and it is not in ring management:

```text
screen=7/17  be=17  perf=30/0/20/0/3/17   fps=19.4
screen=9/27  be=27  perf=50/30/41/0/4/27  fps=22.6
screen=5/5   be=5   perf=20/0/10/0/4/5    fps=50.0
```

The second `screen=` value is time inside `re.EndFrame` alone - `cl_scrn.cpp`
restarts its phase timer immediately before that call. Building the frame costs
5-9 ms; `EndFrame` costs 5-27 ms and tracks scene complexity. That is the real
target, and it is why every CPU-side candidate in this ledger has failed: they
were all optimizing the 5-9 ms, not the 5-27 ms.

`EndFrame` is now split by a follow-up instrumentation change. `dllEndFrame`
times `EndScene` and `Present` separately into `g_SPXBEndSceneUsec` and
`g_SPXBPresentUsec`. Both reach the heartbeat as `swap=<endscene>/<present>ms`
and, more usefully, reach the XEMU harness: `scripts\ja_xemu_smoke.py` polls
them by symbol name and emits them as `swapUsec=<endscene>/<present>` on its
`xblogperf` line.

## XEMU Result: The Swap Is Free There, And XEMU Cannot Settle This

Date: 2026-08-05.

A clean automated `borg1` run with the split in place:

```text
t=  16.1 frame= 11ms  build=  6ms  endFrame=  5ms | cumPresent=40367us
t=  70.2 frame= 16ms  build= 12ms  endFrame=  3ms | cumPresent=52603us
t= 141.1 frame= 13ms  build=  6ms  endFrame=  6ms | cumPresent=72850us
```

`Present` accumulated 37.6 ms across 141 s of wall time. At roughly 61 FPS that
is about 4.4 microseconds per frame. `EndScene` accumulated 169 us total. So on
XEMU the swap costs nothing, and `EndFrame` is entirely backend command
submission - which matches `endFrame` and `backend` tracking each other to
within a millisecond in every sample.

That does not transfer to hardware, and the shape of the difference is the
reason to be careful:

| phase | XEMU | retail |
|---|---|---|
| build picture | 6-12 ms | 5-9 ms |
| finish frame | 3-7 ms | 17-27 ms |
| FPS | 61 | 19-50 |

The CPU-side phase is comparable on both. Only `EndFrame` diverges, by 3-5x. If
the deficit were CPU-side, a 733 MHz Pentium III would be slower than a Ryzen
across both phases, not just one. An asymmetry confined to the phase that
contains GPU submission and presentation points at the NV2A, which XEMU replaces
with a modern Radeon and therefore cannot reproduce.

That is an inference, not a measurement. The measurement now exists and takes
one hardware run of the current build: read `swap=` in the heartbeat. Present
dominating confirms GPU-bound and every remaining CPU-side submission candidate
below should be abandoned. Present near zero on hardware too means the cost is
backend command building and those candidates stay live.

Historical premise, retained because the arithmetic was sound even though the
conclusion was not:

Date: 2026-08-05.

Branch: `native-d3d8-perf`.

The active gameplay submission path is `dllDrawElementsRing` in
`code\win32\win_qgl_dx8.cpp`, selected by `r_nativeDrawPath 2`. It is not the
fakegl `DrawIndexedPrimitiveUPXbox` boundary; the last heartbeat before this
candidate reported `state=0` with `prim` equal to `ring`, meaning every
gameplay draw went through the ring and no fakegl state flush occurred.

The rings were sized below one frame of payload. A settled `borg1` sample
reported 9,360 ring calls and 20,219 KiB of ring payload across 52 frames,
which is about 344 KiB of vertices and 44 KiB of indices per frame against a
256 KiB vertex ring and a 128 KiB index ring. The same sample reported 90 wraps
across those 52 frames, or about 1.7 per frame, matching that arithmetic.

Every wrap calls `BlockUntilNotBusy` on the ring, which waits for the GPU to
finish reading data the CPU submitted moments earlier. A ring that cannot hold
a full frame therefore forces at least one CPU-to-GPU serialization per frame
and removes the pipelining the ring exists to provide. This is close to free
under XEMU/LLE, whose GPU runs nearly in lockstep with the CPU, and is paid in
full on retail hardware. It is a specific mechanism for the retail-versus-
emulator gap that the earlier candidates did not isolate.

The candidate raises the vertex ring to 2 MiB and the index ring to 512 KiB,
about three frames of payload at the higher retail draw rate, so a wrapped
region is long consumed before it is rewritten. Cost is roughly 2.1 MiB of
additional `D3DPOOL_DEFAULT` memory. Ring creation failure is already handled:
the ring path declines and submission falls back to the existing UP route, and
the failure is logged.

Two supporting changes ship with it:

- Wrap waits are routed through `StallOnRingWrap`, which times the wait with
  `QueryPerformanceCounter` and accumulates it into
  `g_SPXBNativeRingStallUsec`. The frame heartbeat now reports it as the new
  `s<N>ms` field inside `ring=`.
- `dllDrawElementsRing` no longer scans every index to narrow the
  `g_SPXBNativeDrawMinIndex` and `g_SPXBNativeDrawMaxIndex` diagnostics. The
  draw declares the whole tess range regardless, and `CanUseDrawElementsPush`
  has already rejected any batch with an out-of-range index, so the scan was a
  second full per-draw pass over the indices that changed nothing.

No rendering behavior changes. Vertex layout, stage state, texture selection,
matrices, presentation, and packaged assets are untouched.

Qualification for this candidate is the heartbeat `ring=` field on retail:

- `w` should fall to roughly zero to one per frame.
- `s<N>ms` is the direct measurement. If it was large before and is near zero
  after, with sustained gameplay FPS up, the mechanism is confirmed.
- If `s<N>ms` is near zero even at the old ring sizes, the wrap stall is not
  the retail deficit and this candidate should be rejected regardless of the
  frame rate, with the search moving to the per-draw state and packing costs
  listed below.

Not yet attempted, ranked, and deliberately held back so this candidate can be
attributed on its own:

1. Pack vertices straight into the locked ring instead of packing into
   `s_ringVertices` and then memcpying. Removes one full copy of the frame's
   vertex payload per draw.
2. Cache `SetStreamSource`. The instrumented sample reported 164 calls per
   frame with the same buffer and stride.
3. Value-cache the D3D texture stage states in `_updateTextures`, which issues
   fourteen `SetTextureStageState` calls plus `SetTexture` per dirty stage.
   Note that `code\cgame\win_highdynamicrange.cpp`,
   `code\renderer\tr_WorldEffects.cpp`, and `code\win32\win_stencilshadow.cpp`
   write texture stage state directly on the shared device, so any such cache
   needs an explicit invalidation hook at those sites.
