# Retail Renderer Qualification - 2026-08-12

## Candidate

- Shared native D3D8 renderer: `code/win32/win_qgl_dx8.cpp` and the common
  `code/renderer` frontend used by both game personalities.
- Game ownership remains separate: `default.xbe` hosts campaign/co-op and
  `efmp.xbe` hosts Holomatch. `codemp` is not a build or runtime dependency.
- Toolchain: clean XDK 5558 at `C:/XDK_5558/XDK`.
- Retail authority: shipping Jedi Academy MP Xbox `jamp.xbe`, especially the
  indexed push-buffer function at `0x000B3640` (1,924 bytes, 448 instructions).
- Production geometry uses retail-style immediate push packets and canonical
  `PushIndices` batches. Experimental UP and dynamic-ring paths are not
  reachable from production.

## Runtime Proof

### Single Player

- Map: `borg6`, 100-second active movement/combat run.
- Guest FPS: 77.0 average, 67.3 minimum, 68.1 p10, zero samples below 30.
- Process remained alive; health and ammunition changed under scripted input.
- Report:
  `scripts/output/retail-renderer-final-sp-borg6_borg6_20260812_080935.report.txt`
- Visual contact sheet:
  `scripts/output/retail-renderer-final-sp-borg6_borg6_20260812_080935_contact.png`
- Fresh fixed-view visual proof:
  `scripts/output/retail-renderer-final-sp-loading_borg6_20260812_081316_contact.png`
- Exact final-relink confirmation: 84.4 average guest FPS, 76.4 minimum,
  zero samples below 30, active movement/combat, and clean captures:
  `scripts/output/retail-renderer-final-relink-sp_borg6_20260812_082110.report.txt`

### Normal Boot and UI

- `default.xbe` reached the SP main menu through normal boot.
- Main-menu LCARS, text, backdrop, selection controls, and 2D orientation were
  intact. Captured overlay values were approximately 120-131 FPS.
- Report:
  `scripts/output/retail-renderer-final-normal-boot_normal_20260812_081601.report.txt`
- Visual contact sheet:
  `scripts/output/retail-renderer-final-normal-boot_normal_20260812_081601_contact.png`

### Cooperative Split Screen

- Map: `borg6`, 100 seconds.
- Two independent viewports rendered with correct half-screen clipping; P2 did
  not replay the prior full-screen draw. World lightmaps and HUD confinement
  remained correct.
- Guest FPS: 60.3 average, 58.1 minimum, zero samples below 30.
- Report:
  `scripts/output/retail-renderer-coop-borg6-qualified_borg6_20260812_071622.report.txt`
- Visual contact sheet:
  `scripts/output/retail-renderer-coop-borg6-qualified_borg6_20260812_071622_contact.png`

#### Frontend-to-Co-op Handoff

- A normal frontend-driven cooperative launch was followed through the complete
  `borg1` title sequence into live split-screen play in one unchanged process.
- The engine queued `ui_ef_coop` through the normal menu-map handoff. Runtime
  telemetry then identified P2 as entity 418, reported a valid P2 refdef, and
  showed independent slot-0/slot-1 draw and world counters advancing. Captures
  show two separately composed half-height views rather than a replay of the
  previous full-screen frame.
- This heavier `borg1` split-screen view averaged 16.8 guest FPS after the
  title sequence. It is retained as representative workload evidence and does
  not replace the 60.3-FPS `borg6` qualification above.
- Report:
  `scripts/output/retail-coop-full-handoff_normal_20260812_201104.report.txt`
- Visual contact sheet:
  `scripts/output/retail-coop-full-handoff_normal_20260812_201104_contact.png`

#### Disabled-Audio Lifecycle Guard

- Xbox audio is currently disabled before ambient-set initialization. The
  title sequence previously reached an `ET_GENERAL` local-set entity and
  dereferenced the uninitialized ambient-set registry, producing a deterministic
  freeze before the cooperative handoff.
- The shared ambient API now treats an unavailable registry as silent audio:
  `S_AddLocalSet` returns the current client time and `AS_GetBModelSound`
  returns no sound. No map, cgame, or renderer workaround was added.
- The corrected build passed the former stop and completed the full title and
  split-screen handoff in the report above.
- A fresh shared-engine relink was then exercised directly on `borg1` for 100
  seconds.  It crossed the former entity-82 stop repeatedly, retained an
  advancing heartbeat, and finished alive.  The title camera averaged 55.3
  guest FPS with a 46.5 minimum and no sub-30 guest samples.  This validates
  the repaired title/entity path; it does not replace the playable `borg6`
  movement/combat qualification above.
- Report:
  `scripts/output/retail-final-sp-borg1_borg1_20260812_202115.report.txt`
- Visual contact sheet:
  `scripts/output/retail-final-sp-borg1_borg1_20260812_202115_contact.png`

### Holomatch and CTF Reinitialization

- One continuous `efmp.xbe` process cycled through `hm_borg1`, `hm_dn1`,
  `ctf_dn1`, `ctf_voy1`, and `hm_scav1` using a test-only cvar-controlled host
  cycle. The cycle is inert in ordinary play.
- The process survived repeated map shutdown/reinitialization and ended alive.
- Guest FPS: 86.2 average, 52.0 minimum, 62.8 p10, zero samples below 30.
- Contact captures show CTF and Holomatch geometry, MP loading, lightmaps,
  weapon/projectile effects, blending, and correctly oriented HUD after the
  transitions.
- Report:
  `scripts/output/retail-renderer-mp-five-map-hostcycle_hm_borg1_20260812_075626.report.txt`
- Visual contact sheet:
  `scripts/output/retail-renderer-mp-five-map-hostcycle_hm_borg1_20260812_075626_contact.png`
- Exact final-relink confirmation: 80.4 average guest FPS, 51.9 minimum,
  62.4 p10, zero samples below 30, and alive at 120 seconds:
  `scripts/output/retail-renderer-final-relink-mp-five-map_hm_borg1_20260812_082438.report.txt`
- Final-relink visual contact sheet:
  `scripts/output/retail-renderer-final-relink-mp-five-map_hm_borg1_20260812_082438_contact.png`
- The same post-ambient-guard shared-engine relink completed a fresh
  100-second `hm_borg1` run with scripted movement and fire. It ended alive at
  90.6 average guest FPS (89.4 minimum). XEMU wall delivery averaged 30.3 FPS;
  that host-emulation figure is retained separately from engine timing. The
  harness eventually pressed against a nearby wall, so this run is retained as
  post-link stability/input proof, not as a representative FPS benchmark. The
  open-scene five-map run above remains the Holomatch performance evidence.
- Report:
  `scripts/output/retail-final-mp-hm-borg1_hm_borg1_20260812_202447.report.txt`
- Visual contact sheet:
  `scripts/output/retail-final-mp-hm-borg1_hm_borg1_20260812_202447_contact.png`

## Package and Architecture Checks

- `scripts/check_mp_holomatch_ui.py` passes against `efmp.xbe` and `xbox1.pk3`.
- No `codemp` dependency was found among 1,024 checked `code` source files.
- `xbox1.pk3` contains 5,381 DDS entries and zero original image entries.
- All 33 multiplayer maps have optimized package entries.
- Elite Force map ownership remains explicit: the packed-map audit accepted all
  116 EF maps (83 SP and 33 MP) as native IBSP v46 with EF's 17-lump layout and
  104-byte drawsurfaces.  No JKA RBSP-v1 lump or drawsurface assumptions are
  used by the EF loader; the retail JKA authority starts only after EF has
  produced renderer geometry.
- The obsolete requirement for the removed
  `STEFX_HM_STATE: preserve client=` success log was deleted from the checker;
  the source-level authoritative-state/read-only-projection contract remains.

## Neptune Sky Investigation

- `ctf_neptune` uses the original two-stage `textures/rig/sky` shader: a
  repeating `clouds2` base followed by a clamped `sunset` image blended with
  `GL_DST_COLOR, GL_ZERO`.
- Both packaged images were verified as 128x128 DXT1 DDS payloads.  Their byte
  checksums matched the textures bound by the renderer, and the localized
  magenta sunset image retained black edge pixels after conversion.
- Separate stage captures proved the cloud pass and localized sunset pass are
  individually valid.  A black-target test proved destination-color blending
  is applied correctly.  Direct texture binding and a direct NV2A stage-0
  clamp packet produced images identical to the ordinary retail submission,
  ruling out stale texture and sampler state.
- The final direct-clamp control remained alive at 66-67 FPS with the same sky:
  `scripts/output/retail-sky-direct-clamp_ctf_neptune_20260812_170033_contact.png`.
  Temporary sky probes and diagnostic state overrides were removed afterward.
  Without a canonical PC/PS2 capture, the shader-authored magenta composition
  is not being rewritten as an inferred renderer fix.

## Hardware Stage

### Hardware frame-time diagnosis

The 2026-08-12 retail-Xbox log established a stable approximately 5.2-5.3 FPS
baseline rather than a progressive leak.  Representative steady frames cost
187-188 ms: 28-30 ms in the server and 158-160 ms in the client.  Within the
client, renderer frontend work cost 35-37 ms and backend work cost 95-96 ms;
audio was disabled and cost zero.  Memory remained stable at approximately
10.54 MiB used.

An XEMU control of the same `borg1` path reports a small submitted workload:
one view, four visible BSP leaves, 845 input surfaces collapsed to 73 material
batches, 77-78 logical draw submissions during sampled camera frames, 4,658
vertices, and 9,201 indices.  This rules out a runaway BSP traversal or raw
geometry count as the explanation for the hardware backend time.

Cycle-level instrumentation then separated each indexed draw into state,
push-buffer reservation, stream packing, index emission, and submission.  In
ordinary XEMU frames, `BeginPush` reservation was already the largest part of
native submission.  Two longer backend frames were almost entirely reservation
waits: one recorded 11,591,433 of 12,165,271 draw cycles in `BeginPush`, and a
second recorded 14,974,885 of 15,524,443 cycles there.  Packing, indexing, and
`EndPush` remained small.  This identifies push-buffer backpressure rather than
vertex copying or index encoding as the hidden interval charged to the backend.

The corresponding startup audit found a concrete retail-parity regression.
Shipping JA MP calls `Direct3D_SetPushBufferSize(1024*1024, 128*1024)` at the
start of `main`; the shared Elite Force engine no longer called it at all.  It
had been removed during the SP-hosted Holomatch merge to match SP behavior, and
the earlier note claiming a 1024/32 KiB configuration was therefore stale.
The shared startup now restores the exact shipping JA 1 MiB primary and 128 KiB
kickoff policy for both `default.xbe` and `efmp.xbe`.

The current XBEs add hardware heartbeat fields for visible leaves, input
surfaces, material batches, logical submissions, vertices, indices, and a
separate render-command/Present time pair.  The existing 95-96 ms backend timer
ends before the real frame-end `Present`, so it already identifies CPU-side
render command processing as the dominant measured interval.  The next hardware
log adds a direct Present measurement and exact workload counts to that result.

The timing buckets overlap by design and must not be added blindly. The
approximately 63 ms SP cgame interval includes about 25-26 ms building visible
entity render submissions and about 35-36 ms in `CG_DrawActive`; the latter
contains the renderer frontend's `R_RenderScene` call. The later 95-96 ms
backend interval executes the queued render commands. Consequently, the
hardware result remains primarily a shared render-pipeline problem. The clearly
separate server tick is about 28-30 ms. At five rendered frames per second each
frame is late enough to run a full 20 Hz server tick; improving renderer
throughput will also reduce the average server charge per displayed frame.

The heartbeat text buffer was already near capacity, so the current candidate
does not append more fields to that line. Instead it emits one bounded
`STEFX_HW_FRAME_PROFILE` record every five seconds. That record preserves SP
game pre/entity/post phases, visible workload, backend and Present milliseconds,
and cycle totals for draw state, `BeginPush`, packing, index emission, and
`EndPush` without per-frame file I/O.

Single transfer folder:

`build/hardware/StarTrekEliteForceX-Beta-20260801`

Manifest version: `Beta-20260813-retail-pushbuffer-frameprofile2`.

| File | Bytes | Staged SHA-256 | Source SHA-256 |
| --- | ---: | --- | --- |
| `default.xbe` | 4,280,320 | `83C845FA9BA8F718962CFD6C0A53A1EB7F71F51EED72243FE549C086E8513671` | `FB9413D79DC469C0E6B17943FEFFE41C7DD1153EA63D8CD46305C6F2DD8930A3` |
| `efmp.xbe` | 4,055,040 | `C6B076CD3F9B69B8CA0F7307A354BD0E187C271652DEAD4B4BC1C604B5FE826B` | `AD1C2DC043FBC21668A4985193C8D6A71CAC8D321AE4E73D2A566473FF2CF4B7` |
| `BaseEF/xbox0.pk3` | 334,562,064 | `AC97662522FFCC0F11B27CFF2CFD29C239A5AAD7021AA0DA0D913FA62484DF91` | same |
| `BaseEF/xbox1.pk3` | 272,476,982 | `5EEC3DFF8197B255E3F97426299C5A0CF0B18591401973DE046E6EF6A9E35242` | same |

The staged XBEs carry exactly the expected one-byte media-enable patch at
offsets 1,090,142 (`default.xbe`) and 1,716,718 (`efmp.xbe`); PK3 hashes are
byte-identical to release outputs. The single transfer folder contains six
files totaling 615,368,751 bytes. Old runtime logs were removed so the next
hardware test cannot be confused with evidence from a prior candidate.

### Flare finish-state follow-up

Hardware testing of the restored retail 1 MiB/128 KiB push-buffer policy still
averaged about 5 FPS. That result closes push-buffer sizing as a sufficient fix.
It does not overturn the earlier profile showing that most measured time is in
the shared render pipeline, but it does require looking beyond raw draw count
and push-buffer capacity.

The next audit found a concrete forced synchronization in both flare depth-test
paths. Xbox `qglReadPixels` is a no-op, but `RB_TestFlare` and `RB_TestZFlare`
still cleared `glState.finishCalled` as if a real synchronous readback had
occurred. `RB_SwapBuffers` interprets that state as an unfinished frame and
calls `qglFinish`; the Xbox implementation executes `BlockUntilIdle`. Both
paths now preserve the finish state on Xbox while retaining the original
behavior on other platforms.

Fresh XDK 5558 builds completed for both personalities. The package gate again
confirmed that `efmp.xbe` has no `codemp` dependency, all 33 MP maps are in the
optimized package, and `xbox1.pk3` remains DDS-only. Sequential XEMU proof then
completed without a fatal or sustained stall:

- `scripts/output/flare-sync-sp-gameplay_borg6_20260813_124955.report.txt`
  remained alive for 70 seconds of direct SP gameplay. The harness ultimately
  reached a wall, so its FPS is retained as stability evidence only, not as a
  representative performance comparison. Sampled explicit finish/Present wait
  remained `0/0`.
- `scripts/output/flare-sync-mp-open_hm_borg1_20260813_125305.report.txt`
  remained alive for 80 seconds with three bots, movement, firing, open-room
  traversal, and visible weapon effects. XEMU wall throughput averaged 29.3
  FPS, with one transient 16.5 sample followed by immediate recovery. The
  contact sheet is
  `scripts/output/flare-sync-mp-open_hm_borg1_20260813_125305_contact.png`.

These runs prove compatibility and forward progress; they do not establish a
retail-Xbox FPS gain. The current single-folder hardware manifest is
`Beta-20260813-flare-finish-sync` with these staged payloads:

| File | Bytes | Staged SHA-256 | Source SHA-256 |
| --- | ---: | --- | --- |
| `default.xbe` | 4,280,320 | `28724BA6CE62B41854EB01BB8A504417D6F6038DE712568DC761C1C466AB7ECA` | `3DC727B034DE43D62E217650D34918E9294A7597D95C60FCF13F7B4A792E0971` |
| `efmp.xbe` | 4,055,040 | `CFFBADD2E2B80A553DC79F4E188D356350788C44721A1C21B24C6C15B5F4E806` | `8E2226BE0ED195B5A5D97127859003704A1B8FE143F9F95B30E2333A29466B06` |
| `BaseEF/xbox0.pk3` | 334,562,064 | `AC97662522FFCC0F11B27CFF2CFD29C239A5AAD7021AA0DA0D913FA62484DF91` | same |
| `BaseEF/xbox1.pk3` | 272,476,982 | `5EEC3DFF8197B255E3F97426299C5A0CF0B18591401973DE046E6EF6A9E35242` | same |

The folder contains six files totaling 615,376,909 bytes. Stale root-level
runtime logs were removed during staging. Hardware acceptance requires a fresh
open-scene run and `STEFX_HW_FRAME_PROFILE` comparison of total, backend,
finish, and Present time against the earlier 187-188 ms frame.

This is deliberately a four-payload overlay, not a standalone game install.
It omits the already-installed retail `pak[x].pk3` and SP soundbank files. The
package/XBE gate passes against the replacement payloads; invoking the
standalone-runtime stage gate against this overlay alone is expected to report
the absent soundbank. `TRANSFER_README.txt` requires applying the four payloads
over the existing retail data.

### Post-guard release artifacts

The shared-engine relink qualified above produced these unpatched release
artifacts before hardware staging:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `default.xbe` | 4,276,224 | `1D62652002F9D8CDCA9ADD83EF3E8D41774033D150828DE00B25E7E7B7697ACC` |
| `efmp.xbe` | 4,050,944 | `F66754DDC1ACD1314E2CCD1218A9EDA859290E4868D65902E95A649B2DC9FD07` |

## Remaining External Proof

- Transfer the four manifest-listed files over the existing retail `pak[x].pk3`
  installation and run sustained campaign and Holomatch sessions on hardware.
- Confirm advancing heartbeats, no OOM/fatal records, no progressive stall,
  and representative hardware FPS. Emulator figures above are qualification
  data, not a retail-hardware performance claim.

### Asynchronous BeginPush restoration

The roughly 5 FPS hardware result reported on 2026-08-13 came from manifest
`Beta-20260813-flare-finish-sync`. Its source hashes predate this correction.
Inspection found that the wholesale renderer checkpoint had reintroduced a
`KickPushBuffer` and `BlockUntilIdle` immediately before each native triangle
submission. That was not part of the shipping JA Xbox submission path or the
XDK 5558 `BeginPush` sample, and it had already been removed and visually
qualified earlier in this project. Open SP scenes submit 79-121 such batches
per frame, making this a repeated full GPU drain rather than a harmless fence.

The drain has been removed from the one shared renderer. Fresh XDK 5558 builds
produced release hashes `D36F35D34B3DA4F1A941AD86048CC501CCA6EB26950AF99BEA8E3125E31B2D90`
for `default.xbe` and
`763FCFF1C8C62A92C189B5BD02CE2D3C82858E53AC597CD420F9DC3029CA045E`
for `efmp.xbe`. Sequential forced path-1 XEMU qualification passed:

- `scripts/output/async-push-sp-borg6_borg6_20260813_131423.report.txt`
  stayed alive through movement and firing with intact world/HUD captures,
  79-121 submissions in representative samples, 3-9 ms backend time, and
  zero explicit waits.
- `scripts/output/async-push-hm-borg1_hm_borg1_20260813_131059.report.txt`
  stayed alive with bots, movement, firing, phaser/projectile effects, and
  intact world/HUD output.

This establishes compatibility, not a retail-Xbox gain. The profiler labels
are also nested: `game` is contained by `server`, while renderer frontend and
backend are contained by `client`. The earlier 187-188 ms frame therefore
cannot be diagnosed by summing those labels. Hardware proof must compare the
new manifest directly against `Beta-20260813-flare-finish-sync` and then use
the nested phase boundaries for any remaining non-renderer cost.

The one-folder hardware overlay is now manifest version
`Beta-20260813-async-beginpush`. It retains the same two PK3 payloads and
contains only the expected media-enable byte difference between each release
XBE and its staged counterpart:

| File | Staged SHA-256 | Source SHA-256 |
| --- | --- | --- |
| `default.xbe` | `61A4A9831E12A2815353AC2A97A7D82300A3A0006AA48EECAC74149E7F68EE8A` | `D36F35D34B3DA4F1A941AD86048CC501CCA6EB26950AF99BEA8E3125E31B2D90` |
| `efmp.xbe` | `AD1A0A041E2645A0C97A16B052EBE190D09FB8851FD629B5CD5BB3694EEB59E9` | `763FCFF1C8C62A92C189B5BD02CE2D3C82858E53AC597CD420F9DC3029CA045E` |

No additional runtime optimization was folded into this stage, preserving a
one-variable hardware comparison against the reported 5 FPS build.
