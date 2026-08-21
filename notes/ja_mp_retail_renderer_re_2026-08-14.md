# Retail JA MP Xbox Renderer RE Status - 2026-08-14

## Authority

- Shipping binary: `C:\Programming\GitHub\!!ARCHIVED\Jedi-Academy-X\Star Wars Jedi Academy game\jamp.xbe`
- SHA-256: `74003C42786C021438C1F27833839F599DCE23E22F086E971C84038C504E7FB3`
- Embedded retail build path: `c:\dev\ja\codemp\x_exe\finalbuild\jamp-final.exe`
- Compiler and SDK donor: the clean JA MP renderer built with the retail FinalBuild flags and the unmodified XDK 5558 VC71 toolchain.
- Generated analysis lives outside the repository under `C:\Programming\Tools\xboxrecomp-work\ja-mp-retail`.

## Current Coverage

- The renderer occupies `0x00071700` through `0x000B62F0` (exclusive). `0x000B62F0` is the first CRT body after the renderer.
- The current authority ledger is `renderer-function-seeds-v41.json`.
- The current seeded disassembly is `disasm-renderer-seeded-v12`.
- The corrected range contains 744 functions and 271,495 executable bytes:
  - 688 functions are identified;
  - 56 functions remain unnamed;
  - 267,117 bytes, or 98.39%, belong to identified functions;
  - 442 functions are normalized byte-exact FinalBuild donor matches.
- The 56 unnamed functions occupy 4,378 bytes. They are concentrated in generated STL/vector helpers, small constructors/destructors, and Xbox SDK wrapper bodies. No major lightmap, shader-stage, world traversal, scene, frame, texture-upload, or draw-submission body remains unidentified.

## Current Implementation Validation

- Production Xbox builds now restore the shipping 16-byte `msurface_t` layout. The previous 48-byte layout embedded diagnostic shader and bounds fields in every world-surface record, tripling the stride of a table traversed throughout visibility and rendering. Those fields remain available only when the dedicated `STEFX_XBOX_SURFACE_DIAGNOSTICS` option is explicitly enabled; aggregate frame diagnostics retain the production layout.
- Corrected XDK 5558 v73 builds passed input-free XEMU checks in SP `holodeck` and Holomatch `hm_dn1`, with clean lightmaps, shader output, weapons, and HUD. Captured guest rates were approximately 88-90 FPS in both runs.
- Proof: `scripts/output/retail-v73-surface-stride-sp_holodeck_20260818_115253_contact.png` and `scripts/output/retail-v73-surface-stride-hm_hm_dn1_20260818_115515_contact.png`, with matching `.report.txt` files.
- A compile-time Xbox assertion now enforces the 16-byte production layout. Full XDK 5558 frame-diagnostic builds of both SP and Holomatch succeeded with that assertion before the normal production pair was restored.
- Current production SHA256 values are `1393474A3E68DF9901BD3F2D2EC0E2515AF68C4574398F2CD5B02B79C71E7D98` for `default.xbe` and `06DEE1D55DE364422DD7B6E7AB54946DFFF031A9FCA4F4FE07221AA94B2980B9` for `efmp.xbe`.
- This establishes compatibility only. XEMU was already near its guest-rate ceiling, so only a controlled retail-hardware comparison can determine whether the reduced traversal-table footprint improves the five-FPS hardware baseline. The retained v66 hardware stage has not been overwritten.

## Donor Recovery

- `scripts/re_build_clean_ja_renderer_donor.ps1` compiles all 31 renderer translation units with `/Ox /Ob2 /Oi /Ot /Oy /G6 /arch:SSE /GF /Gy /MT /Z7` and the original FinalBuild defines.
- The complete donor is:
  - `clean-ja-final-donor\clean-ja-final-renderer.dll`
  - `clean-ja-final-donor\clean-ja-final-renderer.map`
- The earlier 29-unit count omitted `win_highdynamicrange.cpp` and `win_lighteffects.cpp`. Both are part of the retail renderer and are now represented in the donor and ledger.
- Anchor-constrained object and sequence alignment is used only between proven monotonic symbols. Normalized instruction structure preserves register use and small field offsets while discarding relocated absolute addresses.
- Candidate promotion requires reciprocal best agreement, no mapped call-graph contradictions, and strong machine/size evidence or multiple corroborating graph edges. Weak navigation matches remain unnamed.

## Proven Entry Points

- `MC_UnCompressQuat`: `0x00071700`
- `RB_ExecuteRenderCommands`: `0x000735D0`
- `RE_BeginFrame`: `0x00076B50`
- `RE_EndFrame`: `0x00076C70`
- `GL_SetDefaultState`: `0x00085D80`
- `GetRefAPI`: `0x000870B0`
- `RE_RegisterMedia_GetLevel`: `0x0008D480`
- `RE_RenderScene`: `0x00092170`
- `R_DrawElements`: `0x00092850`
- `DrawMultitextured`: `0x00092B70`
- `RB_FogPass`: `0x00093120`
- `ComputeTexCoords`: `0x00093B60`
- `ForceAlpha`: `0x00093EC0`
- `RB_IterateStagesGeneric`: `0x00093F00`
- `RB_StageIteratorGeneric`: `0x00094510`
- `RB_SurfaceEntity`: `0x000A21C0`
- `GLW_LoadOpenGL`: `0x000AACC0`
- `BeginSkinTextures`: `0x000ADDF0`
- `EndSkinTextures`: `0x000ADE00`
- `_convertBlendFactor`: `0x000ADF20`
- `setCap`: `0x000AFB00`
- `dllDrawArrays`: `0x000AFDB0`
- `dllEndFrame`: `0x000B0250`
- `dllFinish` / `dllFlush`: `0x000B02A0`
- `QGL_Shutdown`: `0x000B1F70`
- `getLightEffects`: `0x000B2A60`
- `GLW_Init`: `0x000B2AC0`
- `dllBeginEXT`: `0x000B3410`
- `dllDrawElements`: `0x000B3640`
- `_texImageRGBA`: `0x000B49C0`
- `QGL_Init`: `0x000B5550`

## Disassembly Corrections

- XboxRecomp's original linear sweep discarded explicit seeds that landed inside an earlier decoded instruction. The local analyzer now re-decodes authoritative seed bytes and resynchronizes at the next matching boundary.
- Function extents now use bounded control-flow traversal so forward switch arms and shared continuations are not truncated.
- The corrections recover starts hidden by jump-table data, including `GL_SetDefaultState`, `ComputeTexCoords`, `_convertBlendFactor`, and `_texImageRGBA`.
- Synthetic regression tests and the immediate-reference test pass.
- All 686 seeded renderer addresses are exact function starts and every direct call target in the corrected renderer range resolves to a function start.

## Important Interpretation Notes

- The body at `0x00085150` is `RE_RegisterImages_LevelLoadEnd`. Its 237 executable bytes are instruction-for-instruction identical to the FinalBuild donor. The earlier `R_Images_DeleteLightMaps` guess was wrong.
- `R_InitWorldEffects` at `0x000A92B0` is exact. `R_ShutdownWorldEffects` is a tail jump to that body and may be folded rather than occupying an independent retail function range.
- The small bodies immediately after `R_InitWorldEffects` are generated constructors/destructors, not missing world-effects features.
- `RB_SurfaceEntity` at `0x000A21C0` is the retail entity-type jump dispatcher. Its executable instructions and jump targets match the donor; donor padding accounts for the apparent size difference.
- `RE_RegisterMedia_GetLevel` at `0x0008D480` is the six-byte retail getter for the current media-registration level.
- Pointer-table order and normalized byte-exact matches are authoritative. Address proximity and single fuzzy matches are not.

## Implementation Boundary

- The shared Xbox renderer backend is the common-system integration target for SP/co-op and Holomatch.
- The retail common backend includes `win_qgl_dx8.cpp`, `win_glimp_console.cpp`, `win_highdynamicrange.cpp`, `win_lighteffects.cpp`, and the texture-allocation interfaces they share.
- Elite Force BSPs are not Jedi Academy BSPs. Retail JA high-level BSP parsing and game-specific renderer data structures must not replace Elite Force equivalents wholesale.
- The active `win_highdynamicrange.cpp` is byte-for-byte identical to the clean
  FinalBuild JA donor. `win_lighteffects.cpp` is likewise wholesale donor code;
  its only substantive source adaptation is the Elite Force view-orientation
  member name (`tr.viewParms.or` instead of JA's `tr.viewParms.ori`). The old
  stub warning is obsolete.
- Runtime fixes should be justified against the machine-code ledger and introduced as a coherent backend integration, not isolated speculative substitutions.

### Current Common-Bridge Closure (2026-08-16)

- The current retail-vs-`efmp.xbe` machine comparison contains 146 common
  `win_qgl_dx8` entries: 138 are shape-exact and 116 are detail-exact.
- Six nominally non-matching zero-size current entries are linker-folded aliases
  (`dllColor4ubv`, `dllColor4uiv`, `dllIsTexture`, `dllColor3bv`, `dllColor3i`,
  and `dllColor3sv`), not missing implementations.
- `_updateTextures` has identical source and the same 515-byte/140-instruction
  body. Its residual machine difference is the intentional Elite Force
  `TextureInfo` stride (`0x78` rather than JA's `0x70`).
- `GLW_Init` retains the required Elite Force startup ownership and deferred
  texture-pool initialization. That integration accounts for the remaining
  size/shape difference; it is not an unported draw path.
- No major low-level D3D draw body remains absent. In particular,
  `dllDrawElements` retains the complete retail 448-instruction body. The next
  hardware evidence must therefore distinguish excessive scene workload from
  slow submission rather than assuming another missing QGL renderer function.
- The active `tr_world_retail.cpp` differs from the clean JA source by only 54
  added and 7 removed lines. Those changes are include/namespace integration,
  the Elite Force `tr.or` member name, scoped calls back into the shared EF
  renderer, removal of JA-only automap screenshot code, and count-only profile
  telemetry. The BSP walk, PVS/frustum traversal, surface de-duplication,
  culling, dynamic-light classification, and draw-surface emission remain the
  wholesale JA bodies.

## Remaining RE Closure

1. Promote the small set of semantic bodies whose machine code is already directly proven (`RE_RegisterMedia_GetLevel`, `RB_SurfaceEntity`, texture lifecycle wrappers, and light-effects access).
2. Keep low-confidence generated-template identities explicitly unnamed; their disassembly remains complete and their behavior is available through their callers and donor source.
3. Regenerate the seeded disassembly and final implementation manifest from the corrected range and v39-or-later ledger.
4. Integrate the retail common backend while preserving Elite Force BSP and game-library ownership boundaries.

## Retail Frame Scheduler Finding

- The shipping `main` body at retail VA `0x00139520` is an exact clean-donor
  match. Its active loop is `IN_Frame(); Com_Frame(); DebugConsoleHandleCommands();`
  with no sleep or explicit scheduler yield.
- The shared Elite Force Xbox loop had accumulated two unconditional
  `Sleep(xboxMainLoopYieldMs)` calls per frame. The active value was zero, but
  `Sleep(0)` still yields the thread's remaining time slice on Xbox.
- The active shared loop now follows the retail order exactly, retaining only
  `Sys_XboxExecuteMenuMap()` between input and `Com_Frame()` for the unified
  SP/co-op/Holomatch executable handoff contract.
- `efmp.xbe` (`SHA-256 4CFF5AF2226CFB17B451F0C936D6FD3B93172B7647C8AA5DCAED6CBF55614298`)
  passed a 90-second `hm_borg1` XEMU smoke with no heartbeat stall. Wall output
  remained approximately 28-31 FPS while the guest heartbeat reported roughly
  89-91 FPS.
- `default.xbe` (`SHA-256 72EE4BA728BCBD83A5FD1208260E8825F077519E16CFB1D3D6F1AB655C125EB2`)
  passed a 90-second `borg6` XEMU smoke with coherent visual output and no
  heartbeat stall. From the first stable sample through the last, it produced
  2,540 frames over 85.9 wall seconds (29.57 wall FPS) with an 89.07 FPS guest
  average.
- A second 90-second `hm_borg1` pass used scripted movement, continuous yaw,
  strafing, and attack input. It averaged 26.58 wall FPS overall (28 FPS
  median, 30.7 maximum) and 87.73 guest FPS while scene workload varied from
  283 to 763 surfaces and 83 to 158 batches. The heartbeat reached the end
  without a lockup. Brief low wall samples coincided with native screenshot
  capture and recovered immediately.
- These results establish the retail scheduling contract in both shared-engine
  personalities. They do not yet establish retail-hardware FPS; the hardware
  package must be refreshed and tested separately after the remaining coherent
  frame-path audit.

## Frame-Control RE Boundary

- `scripts/re_frame_retail_ledger_v1.csv` records retail ranges and confidence
  for `CL_CheckDeferedCmds`, `CL_Frame`, `CL_Init`, `CL_Shutdown`,
  `SCR_UpdateScreen`, `Com_Freeze`, `Com_Init`, `Com_Frame`, `main`,
  `Sys_Milliseconds`, `SV_CheckPaused`, and `SV_Frame`.
- Exact clean-donor anchors currently include `main`, `SCR_UpdateScreen`,
  `SV_Frame`, `Sys_Milliseconds`, `CL_CheckDeferedCmds`, and `CL_Shutdown`.
- The current-vs-retail report is
  `C:\Programming\Tools\xboxrecomp-work\ja-mp-retail\retail-vs-stefx-frame-current.json`.
  The large shape differences in `CL_Frame` and `Com_Frame` include legitimate
  Elite Force SP/co-op ownership and diagnostic code, so those bodies must be
  audited as a complete control-flow set rather than copied wholesale from the
  multiplayer game.

## Post-Renderer Frame Audit

- The apparent `borg6` stop near 16.6 seconds did not reproduce under a
  controlled repeat. Dynamic lights enabled and disabled both completed their
  timed runs with advancing heartbeats, so the retail dlight architecture was
  left unchanged. Evidence:
  - `scripts/output/retail-world-dlight-off-sp_borg6_20260814_205500.report.txt`
  - `scripts/output/retail-world-dlight-pc-snapshot-sp_borg6_20260814_210251.report.txt`
- A later pair of 16.6-second stops was reproduced and isolated independently
  of the renderer. With `stefx_smoke_input` enabled, SP halted at frame 1423,
  realtime 16635, with the CPU in the Xbox kernel halt path and CR2
  `0xFFF0007B`. The identical XBE and map without that guest diagnostic reached
  frame 2612/realtime 29652. A third run used XEMU's controller mapping to hold
  real forward input for eight seconds and reached frame 1973/realtime 22646
  without a stall. Evidence:
  - `scripts/output/retail-contract-sp-smokeonly-control_borg6_20260815_134536.report.txt`
  - `scripts/output/retail-contract-sp-noinput-control_borg6_20260815_134242.report.txt`
  - `scripts/output/retail-contract-sp-realinput-control_borg6_20260815_134933.report.txt`
- The temporary guest smoke-input path is therefore excluded from renderer
  qualification. Automated gameplay proof must use XEMU controller events;
  hardware proof uses normal controller input. This distinction prevents a
  diagnostic-only fault from being misreported as a retail-renderer stall.
- The common D3D bridge is already substantially closed: 134 of 144 compared
  `win_qgl_dx8.obj` functions have exact normalized instruction shape. The
  shipping 1,924-byte indexed push-buffer body is active, while
  `R_DrawElements`, `R_DlightGrid`, and the major stage/backend dispatchers are
  exact or near-exact retail matches.
- The zero-log experiment did not disable the separate always-on performance
  profiler. A matched compile-out trial removed its client, server, screen,
  audio, and renderer-stage clocks without changing gameplay behavior. It was
  rejected: wall rate remained 29.7 FPS versus 29.0 and 30.3 FPS in the two
  adjacent controls, while removing the probe globals invalidated the existing
  diagnostic address map. The profiler source was restored unchanged.
- Rejected-trial evidence:
  `scripts/output/retail-frame-profiler-off-sp-borg6_borg6_20260814_212207.report.txt`.

## DDS Mipmap Hardware-Cache Candidate

- The runtime DDS path already carries the file mip count through `LoadDDS`,
  `R_CreateImage`, `Upload32`, `_texImageDDS`, and `XGSetTextureHeader`, but the
  generated `xbox0.pk3` and `xbox1.pk3` packages previously contained only
  single-level DDS images. That left the retail D3D8 sampler's default
  `GL_LINEAR_MIPMAP_NEAREST` policy without lower-resolution levels.
- The shared package builder now emits full DDS mip chains for eligible
  `textures/`, `models/`, and `env/` assets. It preserves single-level DDS for
  shader `nomipmaps` references and for UI, fonts, levelshots, fullscreen
  images, and specification assets. The package remains DDS-only and uses only
  DXT1, BGRA32, and RGB565; no DXT5 or source-image fallback was introduced.
- `xbox1.pk3` contains 5,381 DDS images: 4,635 mipmapped and 746 single-level.
  Its 120-second `hm_borg1` XEMU run completed without an OOM or heartbeat
  stall, retained lightmaps/HUD/weapon effects, and measured 29.87 wall FPS
  versus 30.08 in the adjacent single-level control. Evidence:
  `scripts/output/mipmaps_hm_borg1_hm_borg1_20260815_090539.report.txt` and its
  contact sheet.
- `xbox0.pk3` contains 5,405 DDS images: 4,635 mipmapped and 770 single-level.
  Direct SP `borg6` reached live gameplay with intact lightmaps, world textures,
  weapon, and HUD. Later identical captures followed the scripted player
  walking into geometry; the SP address probe ceased advancing after the map
  transition, so this run is visual/stability evidence, not an FPS result.
  Evidence: `scripts/output/mipmaps_sp_borg6_borg6_20260815_091614.report.txt`
  and its contact sheet.
- XEMU is neutral on this change, as expected for an emulator with host texture
  caching. The candidate remains specifically for a controlled retail-hardware
  texture-cache test; no hardware gain is claimed yet.

## Retail MP Zone And Texture Residency Contract

- Shipping `jamp.xbe` `Com_InitZoneMemory` is the 413-byte body at retail VA
  `0x0004E240`. It allocates the static and model texture pools first with
  `D3D_AllocContiguousMemory`: exactly `0x00A00000` and `0x00400000` bytes.
- After the texture reservations, retail queries available physical memory and
  computes the zone as `dwAvailPhys - 0x00C55020`. That constant is the source
  `ZONE_HEAP_FREE` reserve minus the FinalBuild 640x480x4 persisted framebuffer
  adjustment. It then calls the Xbox `GlobalAlloc` wrapper at `0x000DBB5D`.
- The shared tree had drifted to a fixed 22 MiB
  `D3D_AllocContiguousMemory` zone and matching contiguous free. This was not a
  shipping MP contract and placed the general-purpose game zone in the same
  scarce contiguous resource class as D3D textures.
- An integration trial restored that ordering and dynamic general-zone sizing,
  first with `GlobalAlloc` and then the equivalent linked CRT heap entry. A
  no-texture-pool control reached `main()`, but enabling the exact 10 MiB and
  4 MiB pool calls caused both CXBX HLE and XEMU LLE to stop after the CRT
  thread-launch probe and before `main()`.
- PE/XBE comparison showed an identical 25-entry constructor table and an
  unchanged game `main` address. The decisive source-level cause is earlier:
  static constructors allocate through `Z_Malloc`, whose lazy initialization
  can enter `Com_InitZoneMemory` before `main`. Putting the retail skin-pool
  setup there therefore creates `z:\\skintextures` during `_cinit` and kills
  the spawned game thread before the raw `main_reached` probe.
- The general zone remains on the previously booting path for now. The exact
  10 MiB static and 4 MiB skin pool initialization is deferred to `GLW_Init`,
  after CRT/XAPI startup but before device and texture creation. Pool ownership
  persists across renderer restarts, which reset the allocation cursors rather
  than leaking another pair of contiguous allocations.

## Retail Final ABI And Qualified Residency Integration

- Shipping JA MP compiles the renderer family with both `FINAL_BUILD` and
  `_FINAL`. This is an ABI requirement, not just dead-code removal:
  `image_t::imgName` is absent under `FINAL_BUILD`. The active build now applies
  that contract consistently to every renderer translation unit plus the four
  common Xbox bridge files, so renderer objects no longer disagree about
  structure layout.
- Elite Force image-name diagnostics now resolve through the existing Xbox
  image-name map. Image deletion removes entries by `image_t *`, preserving
  cache ownership without restoring the debug-only field to the retail ABI.
- Xbox model-performance hooks are disabled in all Xbox builds. They are not a
  retail FinalBuild facility and Elite Force has no Ghoul2 gameplay ownership;
  leaving callers active while final renderer objects omit the implementations
  also breaks the link contract.
- The retail texture-pool ordering is now restored without repeating the
  `_cinit` failure. `Com_InitZoneMemory` reserves the exact 10 MiB static and
  4 MiB skin contiguous pools before measuring available physical memory and
  allocating the cached general zone with `GlobalAlloc`. Only the unsafe
  `z:\\skintextures` file reset remains deferred until `GLW_Init`, after
  CRT/XAPI startup. This preserves the shipping physical allocation order
  without opening a scratch file from a static constructor.
- Fresh XDK 5558 builds passed in both personalities. Holomatch `hm_borg1`
  completed 60 seconds with advancing heartbeats, intact lightmaps/HUD, 90.9
  guest FPS, and 28.5 average XEMU wall FPS:
  `scripts/output/retail-zone-heap-mp_hm_borg1_20260815_111157.report.txt`.
- SP `borg6` completed the matching 60-second proof with intact world lighting,
  animated weapon texture, and HUD, at 90.9 guest FPS and 28.6 average XEMU
  wall FPS:
  `scripts/output/retail-zone-heap-sp_borg6_20260815_111646.report.txt`.
- CXBX-R is not a qualification authority for this JA-derived renderer because
  the retail JA executable itself crashes there. XEMU/LLE remains the active
  emulator authority and retail Xbox hardware is the final performance and
  stability authority.

## Retail Texture-Pool Ordering Qualification

- Fresh XDK 5558 builds of both `default.xbe` and `efmp.xbe` completed with the
  shipping-style `d3d8.lib` path. Link maps attribute the active D3D symbols to
  the retail D3D objects rather than instrumented `d3d8i.lib`.
- Direct Holomatch `hm_borg1` completed a 90-second no-input XEMU/LLE proof
  with advancing heartbeats and visibly animated frames. Guest FPS averaged
  90.8; XEMU wall FPS averaged 28.5. Lightmaps, HUD, weapon, and world textures
  remained intact. Evidence:
  `scripts/output/retail_pool_order_mp_noinput_hm_borg1_20260815_145914.report.txt`
  and its contact sheet.
- Direct SP `borg6` completed a 100-second XEMU/LLE gameplay proof with
  advancing heartbeats and animated weapon texture. Guest FPS held 90.9; XEMU
  wall FPS averaged 27.0. Evidence:
  `scripts/output/retail_pool_order_sp_gameplay_borg6_20260815_150242.report.txt`
  and its contact sheet.
- A preceding Holomatch run froze at approximately 16.6 seconds only because
  it enabled the already-rejected guest `stefx_smoke_input` diagnostic. The
  unchanged XBE passed the full no-input repeat, so that run is not renderer or
  allocator regression evidence.
- The single hardware transfer folder was refreshed as
  `Beta-20260815-retail-pool-order`. Retail-hardware performance and long-soak
  behavior remain the final acceptance gate.

## D3D8 QFE Audit

- Shipping `jamp.xbe` records `D3D8` version 1.0.5558 QFE4. The current
  `default.xbe` and `efmp.xbe` record 1.0.5558 QFE1 and link the full retail
  `C:\XDK_5558\XDK\xbox\lib\d3d8.lib`; they do not link `d3d8i.lib`.
- The QFE difference is real but is not currently a credible explanation for
  the approximately 5 FPS hardware result. The compared hot functions have
  identical sizes and control-flow shapes, including `D3DDevice_MakeSpace`,
  `D3DDevice_Swap`, `CDevice_KickOff`, `D3DDevice_DrawVerticesUP`, and the
  texture/stream state setters.
- The recurring instruction differences are device-structure offsets shifted
  by 0x10 bytes and relocated data/call targets. QFE4 carries extra
  shader-snapshot state, but the push-buffer, fence, swap, and fast-copy
  algorithms used by the game remain structurally the same.
- The one substantive hot-helper delta is internal `D3D_SetFence`: QFE4 is
  184 bytes and QFE1 is 171 bytes, with a revised fence-packet calculation.
  `D3DDevice_Swap(0)` can reach that helper, so this exception remains
  documented. Both surrounding swap/wait paths are instruction-shape matches,
  however, and there is no evidence that the complete Microsoft QFE1 helper
  explains a sustained tens-of-milliseconds frame cost.
- No authentic 5558 QFE4 library is present in the local XDK or retail-source
  archives. Do not transplant the embedded D3D section from `jamp.xbe`: its
  code, writable D3D data, relocations, and private structure layout are one
  linked runtime unit. Keep the QFE mismatch documented, but require stronger
  evidence before attempting a complete D3D runtime replacement.

## Clean-Fingerprint Qualification

- The repository build now fingerprints every C/C++ object with the complete
  compiler identity and command line. `-ReuseObjects` also rejects objects
  older than the newest project header. This closes the prior possibility of
  silently linking stale ABI or optimization variants after renderer/header
  changes.
- A full sequential XDK 5558 rebuild produced source XBEs with SHA-256 values
  `1E4DDA8270006946C822B9B80B47B0DE9267CC17D76588DDD8F6175FD36C0741`
  (`default.xbe`) and
  `3110043B4FD0358ABCC763A5A00288061C98AB9BF366A36E6C4039FB80CACDAF`
  (`efmp.xbe`). A follow-up reuse build reused 284 objects and compiled zero,
  proving the fingerprints are stable for an unchanged command line.
- SP `borg6` and Holomatch `hm_borg1` each reached live gameplay in XEMU/LLE
  with intact world, lighting, weapon, and HUD output. The Holomatch process
  remained alive through the 75-second harness window and responded to
  scripted movement. Evidence:
  `scripts/output/fingerprint-clean-sp_borg6_20260815_124230_contact.png` and
  `scripts/output/fingerprint-clean-mp_hm_borg1_20260815_124530_contact.png`.
- The single hardware folder now contains the media-enable-patched forms of
  those exact source XBEs under version
  `Beta-20260815-cached-zone-retail-abi-stack`. `default.xbe` retains the
  working SP/co-op 0x20000 stack commit; `efmp.xbe` now matches shipping JA MP
  at 0x40000. The corrected Holomatch XBE passed a 75-second `hm_borg1` run
  with movement, weapon effects, and intact world/HUD output:
  `scripts/output/retail-mp-stack-contract_hm_borg1_20260815_130711_contact.png`.
  Retail FPS and stability remain the acceptance gate; XEMU's approximately
  125-162 FPS overlay is only emulator correctness evidence.

## General-Heap Residency Audit

- Microsoft Xbox D3D8 source at
  `Z:\Programming\xbox\private\windows\directx\dxg\d3d8\se\memory.hpp`
  implements `D3D_AllocContiguousMemory` through
  `MmAllocateContiguousMemoryEx(..., PAGE_READWRITE | PAGE_WRITECOMBINE)`.
  The former fixed 22 MiB STEFX zone therefore placed read-heavy BSP, collision,
  entity, shader, hunk, and renderer working data in write-combined memory.
- A full current-tree allocation audit found no remaining general CPU heap in
  that resource class. BSP/hunk/collision allocations route through the cached
  `GlobalAlloc` zone; model binaries use `HeapAlloc`; Ghoul2 transient storage
  comes from the same cached zone. The only surviving contiguous allocations
  are the retail 10 MiB static and 4 MiB skin texture pools, plus the audio DMA
  packet buffer, all of which are GPU/device-facing by design.
- `code/xbox_re/re_memory.cpp` described an earlier STEFX binary and incorrectly
  treated its fixed contiguous zone as shipping JA behavior. It is now marked
  as superseded so it cannot override the shipping `jamp.xbe` contract above.
- This makes cached-zone residency the strongest current explanation for the
  emulator-versus-hardware CPU-time delta. It is qualified in XEMU but still
  requires a retail-hardware FPS/profile run before any gain is claimed.
- The single hardware folder is staged as
  `Beta-20260815-cached-zone-retail-abi-stack`. Release source hashes are
  `1E4DDA8270006946C822B9B80B47B0DE9267CC17D76588DDD8F6175FD36C0741`
  for `default.xbe` and
  `18461997D4D317E6448F1518BB4BFEB9AC5F67C3F482678EA746D5D17F4B00BE`
  for `efmp.xbe`. The staged media-enable-patched hashes are
  `74F101DAC65335607A66E6261898D51E4C87B1C100BEE50A12FD60984BB6D5DB`
  and
  `1316E71381193C1470763B7679CED7DC48C849646B359D22BA0359242F785311`.

## Matched XEMU Telemetry Proof

- CXBX-R is excluded from renderer qualification because the unmodified retail
  JA executable also crashes there. No CXBX-R result can veto an otherwise
  correct retail contract, and no CXBX-R pass can replace XEMU/LLE or hardware.
- Perf-only heartbeat polling now reads the client-state classification and the
  single distant camera-state word without fetching the full diagnostic block.
  The poll cadence is configurable so monitor traffic does not masquerade as a
  renderer slowdown.
- Holomatch `hm_borg1` remained in active gameplay with movement and firing for
  the complete run. Six classified samples averaged 86.7 guest FPS with an
  80.8 minimum and no sub-30 sample:
  `scripts/output/retail-contract-fps-sampled-v3_hm_borg1_20260815_132120.report.txt`.
- SP `borg6` passed the matching run with movement and firing. Seven classified
  gameplay samples were all 90.9 guest FPS, and the native contact sheet shows
  intact world lighting, HUD, weapon, and 2D orientation at approximately
  110-112 XEMU display FPS:
  `scripts/output/retail-contract-sp-sampled_borg6_20260815_132511.report.txt`
  and
  `scripts/output/retail-contract-sp-sampled_borg6_20260815_132511_contact.png`.
- XEMU wall-FPS values from monitor-instrumented runs remain diagnostic only;
  monitor reads and native captures reduce emulation speed. Retail Xbox FPS,
  frame-phase timings, memory stability, and long-stall behavior are still the
  acceptance gate.
- The unified mini-soak reached its first frontend dwell and dispatched
  `ui_ef_coop`, after which the old `default.xbe` telemetry stopped with the CPU
  in the Xbox kernel halt. Source inspection confirms that command deliberately
  calls `XLaunchNewImage("d:\\default.xbe", ...)` with the co-op launch intent.
  The old-title halt is therefore an XBE handoff boundary, not renderer-frame
  evidence. Co-op qualification must follow the relaunched title or use retail
  hardware; the valid handoff must not be rewritten just to preserve telemetry.
- No CXBX-R qualification is permitted for the JA-derived path. Retail
  `jamp.xbe` itself crashes CXBX-R, so only XEMU/LLE reproduction or retail
  hardware can establish that a matching failure belongs to this renderer.

## Release Optimization Audit

- The active shared-engine build graph contains only `code/x_game` and
  `code/x_exe`; archived tool projects and `codemp` configurations are not
  linked into either current XBE.
- Every active C/C++ source compiles with speed optimization. The executable
  and renderer family use `/Ox /Ob2 /Oi /Ot /G6 /arch:SSE /Oy`; the shared game
  library uses the same `/Ox` profile. No active per-file `/Od` override exists.
- `build_xbox.ps1` now rejects any Release C/C++ source whose final effective
  optimization flag is not `/O2` or `/Ox`. This prevents a future file-level
  override from silently reintroducing an unoptimized renderer, collision,
  cgame, or allocator object.
- Accidental debug compilation is therefore closed as an explanation for the
  existing retail-hardware 5-12 FPS evidence. Cached general-zone residency
  remains the strongest untested hardware candidate.

## XDK 5558 Provenance Audit

- The current build invokes only the untouched XDK 5558 `vc71` compiler,
  librarian, and linker under `C:\XDK_5558\XDK\xbox\bin\vc71`.
- The 5558 Xbox include directory is prepended, and legacy `C:\XDK` include
  entries inherited from project files are explicitly filtered out.
- Every linked Xbox system library (`d3d8`, `d3dx8`, `dsound`, `xboxkrnl`,
  `xgraphics`, `xonline`, `libc`, `xapilib`, and `dmusic`) resolves uniquely
  from `C:\XDK_5558\XDK\xbox\lib`. Neither `build/release` nor
  `code/x_exe/Release` contains a shadow copy of any of those names.
- Mixed 5849/5558 tools, headers, or system libraries are therefore closed as
  an explanation for the current hardware slowdown.

## Renderer Lifecycle Contract Closure

- The remaining retail lifecycle symbols were audited as one ownership group,
  not transplanted piecemeal. `BeginSkinTextures`, `EndSkinTextures`, `setCap`,
  `getLightEffects`, `RE_RegisterMedia_GetLevel`, `R_Images_Clear`, and
  `R_DeleteTextures` retain the shipping JA behavior. STEFX additions are
  startup-order guards and diagnostic counters; they do not alter draw or
  teardown semantics.
- `R_ModelFree` intentionally differs in representation while preserving the
  contract. Shipping JA owns a heap `HashTable` and deletes it there; Elite
  Force owns a fixed 1,024-bucket table populated from the hunk. Applying JA's
  delete path to that EF table would be invalid. Both paths release the cached
  model map and reset their respective model lookup storage at the owning
  lifecycle boundary.
- `RE_RegisterServerSkin` is a JA MP server-only wrapper around normal skin
  registration and has no active caller or export in the unified EF SP-hosted
  executable. It is therefore not a missing runtime renderer path and cannot
  explain shared SP/MP frame time.
- A full source diff confirms that the active D3D8 push-buffer submission code
  in `win_qgl_dx8.cpp`, including `dllDrawElements`, stream packing,
  `PushIndices`, and `BeginPush`/`EndPush`, remains line-for-line with the JA
  Xbox donor. The active differences are EF readback ABI, deferred texture-pool
  startup, bounded counters, and save-thumbnail helpers outside world drawing.
- These contracts are closed unless new XEMU/LLE or retail-hardware evidence
  points directly back to them. CXBX-R remains excluded: retail JA itself
  crashes there, so the shared JA-derived renderer is expected to do the same.

## Current Retail Runtime Comparison And Telemetry

- The current `efmp.xbe` was compared again against the shipping retail JA MP
  XBE after the wholesale surface-owner integration. The machine-readable
  result is outside the repository at
  `C:\Programming\Tools\xboxrecomp-work\ja-mp-retail\retail-vs-stefx-efmp-current-20260815.json`.
  It matches 513 named renderer functions; 243 are instruction-shape exact and
  170 are detail exact. Ratios below exact are not automatically defects: EF
  data structures, SP/co-op entry points, split-screen ownership, and relocated
  calls require explicit adaptations.
- The hottest native submission body, `dllDrawElements`, is a 448-instruction
  detail-exact match to the retail executable. `R_AddWorldSurfaces` is also
  exact. `RB_StageIteratorGeneric` and `R_RenderView` retain near-exact control
  flow while carrying the required Elite Force interfaces.
- The retail command owner now publishes the completed frame's real backend
  surface, batch, vertex, and index counts. A 40-second XEMU/LLE Holomatch run
  advanced continuously and reported approximately 768 surfaces, 120-121
  batches, 7,745-7,753 vertices, and 16,026-16,038 indexes per frame. This
  replaces stale diagnostic sentinels without changing rendering decisions.
  Evidence:
  `scripts/output/stefx-retail-telemetry-restored-20260815_hm_borg1_20260815_211803.report.txt`.
- XEMU's monitor can read the title through physical-memory translation but
  does not service the same title addresses through its virtual-memory read
  command. The harness therefore resolves one executable/map personality and
  validates exact expected addresses before interpreting diagnostic values.
  Distant allocations are not polled through a single assumed physical delta.
- CXBX-R remains excluded from all acceptance decisions. The unmodified retail
  JA MP XBE crashes there; reproducing that behavior with its renderer code is
  expected emulator compatibility risk, not evidence against the Xbox path.
- Only eight retail-named entries in the current comparison have no separately
  sized current body. Six color/texture GL wrappers are linker-folded onto
  equivalent implementations; `RB_SurfaceDisplayList` is folded onto EF's
  identical display-list thunk. `R_LoadShaders` is an intentional BSP-format
  boundary: EF's collision loader expands its different shader lump directly
  into `s_worldData` before renderer world finalization, whereas JA's function
  consumes a JA-format lump. Substituting the JA loader would duplicate or
  misinterpret ownership and is not a missing draw-path optimization.
- The current one-folder hardware stage is
  `Beta-20260815-retail-surface-owner-telemetry-xdk5558`. Source hashes are
  `2DB0B6F13C51BA924DB4035F199C0C5B90E3DDC9E710C22E2686863F01BDAC72`
  (`default.xbe`) and
  `EFE67E3452643469E76808B802B76110ADE461F98260B3D3FF2868679ED9C854`
  (`efmp.xbe`). The media-enable-patched staged hashes are
  `EC82D8D1BDE396FDE3C301FFB01D3EFC4090D99869FCEE8EA3A2DA652CB5DF17`
  and
  `575A7F6B5C9E7BF4DD8A6CA6513F664BD936287B1BD101B45C6BBAC6C7C1292C`.

## Backend Boundary Hardware Profile

- Retained hardware logs put ordinary frames at approximately 119-126 ms:
  server/gameplay consumes 2-5 ms, renderer frontend 48-49 ms, and renderer
  backend 66-70 ms. Audio is 0 ms and zone memory is stable. This rules out
  bots, sound, logging, and an active leak as the primary sustained bottleneck.
- Shipping `jamp.xbe` and the current shared renderer both avoid the frame-end
  `qglFinish` path during normal `r_finish=0` gameplay. The retail Xbox flare
  path also uses D3D visibility tests rather than the non-Xbox readback path.
  A speculative frame-end synchronization rewrite is therefore not justified.
- Behavior-neutral timers now divide `RB_ExecuteRenderCommands` into
  draw-surface, swap, and other command time. `dllFinish` and the `Present`
  call inside `dllEndFrame` are timed independently. The hardware profile line
  exposes them as `backendPhases=draw/swap/other`, `finish`, and `present`.
  `scripts/analyze_hardware_profile.py` reports those values as
  `backendDrawSurfs`, `backendSwap`, `backendOther`, `finish`, and `present`.
- Sequential XEMU/LLE qualification passed for SP `borg6` and Holomatch
  `hm_borg1`. Both stayed alive and produced intact world, lightmap, weapon,
  and HUD captures. Honest wall-delivery rates averaged 33.0 FPS for SP and
  29.0 FPS for Holomatch; guest FPS is not used as hardware evidence. Reports:
  `scripts/output/stefx-retail-backend-boundary-sp-borg6_borg6_20260815_215121.report.txt`
  and
  `scripts/output/stefx-retail-backend-boundary-mp-hm-borg1_hm_borg1_20260815_215340.report.txt`.
- Source SHA256 values are
  `0466E3E40F43B880A83CEE831D5D14082903EE5CB79FF933304B4A40E353A330`
  (`default.xbe`) and
  `3DF802A5CF6FA190D0C8CB2928CA3139CA713FB43455E89F61CCCCD64833FD69`
  (`efmp.xbe`). The media-enable-patched staged hashes are
  `6A7AB0A6FE1CC47B6D679DE51B5599CDEC0BD6683A3C57D3F525B0647E576B30`
  and
  `8994EE2E707CD6C60A636279FECEF028FF751A0904B8506AF0ACD31EDABEFD86`.
  The single transfer folder is version
  `Beta-20260815-retail-backend-boundary-profile-xdk5558`.
- Millisecond phase counters can remain zero in XEMU because guest `rdtsc`
  does not account for host-side GPU emulation delay at this granularity. The
  XEMU runs qualify behavior and visuals; the next retail log is the authority
  for the backend split and FPS.
- CXBX-R remains excluded. Retail JA itself crashes there, so an equivalent
  crash from the JA-derived renderer does not identify an Xbox renderer defect.

## Complete Hardware Phase Profile

- The active retail `R_RenderView` now populates the existing frontend phase
  counters. The previous EF owner had the timing probes, but it is no longer
  linked as the common frame owner, so those logged fields had silently become
  zero. The retail call order and rendering decisions are unchanged.
- Combined with the backend-boundary counters, one returned hardware log now
  separates setup, leaf marking, world traversal, polygons, projection,
  entities, sorting, debug drawing, draw-surface execution, swap/other backend
  commands, `BlockUntilIdle`, `Present`, and the state/reserve/pack/index/submit
  portions of `dllDrawElements`.
- Sequential XDK 5558 XEMU/LLE checks passed after the instrumentation move.
  SP `borg6` stayed alive and averaged 51.9 wall FPS; Holomatch `hm_borg1`
  stayed alive and averaged 36.3 wall FPS. Evidence:
  `scripts/output/stefx-retail-full-profile-sp-borg6_borg6_20260815_220914.report.txt`
  and
  `scripts/output/stefx-retail-full-profile-mp-hm-borg1_hm_borg1_20260815_221330.report.txt`.
  These are correctness checks only; retail hardware remains the performance
  authority.
- 2D atlas output remains an explicitly open visual defect. HUD, font, loading,
  and other overlay quads can be garbled even while the 3D scene is coherent.
  It is deferred behind the current FPS diagnosis and requires screenshot proof
  before any future fix is accepted.
- `scripts/analyze_hardware_profile.py` now consumes the complete phase record,
  keeps millisecond phases separate from draw-cycle counters, reports scene
  workload, and names the dominant top-level boundary and child phase. This
  turns the next returned hardware log into a direct branch decision rather
  than another speculative renderer change.
- The single PK3-only hardware folder contains
  `Beta-20260815-retail-full-profile-xdk5558`. Source SHA256 values are
  `40D154E35AB3BD38AEC7FCBC36946BBF268B122E75FEB30B844FF8B2F345F4BA`
  (`default.xbe`) and
  `68BBB077F04BF243A0993EAE1B57F668CEE41A749A15F39A03C159FB264E7DC8`
  (`efmp.xbe`). Media-enable-patched staged hashes are
  `E6D2E756DFA3E56E287B3FAE014ACCF80F06B1A4F53482694A51EAA20990857E`
  and
  `AA2E720777D9FAC318105C9A83EE7D50C7287E2578C7A9D1D44C056AEFA70805`.

## Retail Alpha Texture Format Parity

- Shipping retail JA Xbox assets establish that high-resolution textures are
  not inherently excluded from the target: its extracted package contains
  4,580 DXT1 and 970 DXT5 textures, with only one uncompressed RGBA texture.
  The prior Elite Force Xbox package instead contained approximately 1,000
  BGRA32 alpha textures, accounting for about 39.7 MiB of raw DDS payload.
- The active uploader already had a clean end-to-end DXT5 path: DDS parsing
  selects `GL_DDS5_EXT`, and `_texImageDDS` maps it directly to
  `D3DFMT_DXT5`. Sixteen representative generated EF DXT5 assets independently
  decoded through Pillow with valid dimensions, colors, and alpha. Alpha RMSE
  was generally 0-3; expected block-compression loss was bounded.
- The package builder now defaults alpha textures to DXT5 while preserving
  forced BGRA32 for `gfx/` and other 2D-sensitive paths. `xbox0.pk3` contains
  4,351 DXT1, 771 DXT5, 276 BGRA32, and 7 RGB565 textures; `xbox1.pk3`
  contains 4,351 DXT1, 770 DXT5, 277 BGRA32, and 7 RGB565 textures. Both are
  DDS-only, have complete payloads, and contain no original-image fallbacks.
- Raw packaged DDS payload is now approximately 32.31 MiB DXT1, 8.17 MiB
  DXT5, 6.96 MiB BGRA32, and 4.37 MiB RGB565. Compared with the prior alpha
  package this removes roughly 24.6 MiB of runtime payload that could otherwise
  churn the retail 10 MiB static texture pool.
- SP `borg6` and Holomatch `hm_borg1` each completed a 60-second XEMU/LLE
  correctness smoke with coherent world, lightmaps, weapons, and alpha output.
  The known 2D atlas corruption remains visible and is not claimed fixed.
  Reports and contacts use the `stefx-retail-dxt5-sp` and
  `stefx-retail-dxt5-mp` prefixes under `scripts/output`.
- The single hardware stage is
  `Beta-20260815-retail-dxt5-texture-pools-xdk5558`. Retail FPS, complete frame
  profiling, and soak behavior determine whether this candidate is retained;
  no performance claim is inferred from XEMU.

## World Traversal Parity And Compact Qualification

- The active shared `tr_world_retail.cpp` no longer performs volatile phase or
  split-slot counter writes for every BSP node, leaf, and surface. Those values
  had no functional readers and existed only for expired diagnostics. PVS,
  area-mask, frustum, dynamic-light, draw-surface, and functional split-slot
  behavior are unchanged.
- Corrected runtime-address comparison against shipping `jamp.xbe` uses an
  image shift of `0x3F0000`. The current results are exact for
  `dllDrawElements` and `R_AddWorldSurfaces`; detail similarity is 0.9048 for
  `R_MarkLeaves`, 0.8700 for `R_RecursiveWorldNode`, 0.9322 for
  `R_RenderView`, 0.7692 for `RB_ExecuteRenderCommands`, and 0.5814 for
  `RB_RenderDrawSurfList`. The draw-list source retains the retail batching
  contract. Its remaining machine mismatch includes required Elite Force
  render-flag ABI differences, because JA's `RF_FORCEPOST` and
  `RF_FORCE_ENT_ALPHA` values collide with unrelated EF flags.
- Fresh sequential XDK 5558 builds succeeded for both personalities. Source
  SHA256 values are
  `6C17E550EA14DD0172ECDCB55A1D9610F94E86A7B26044CEBA27B3E7BCCECBA3`
  (`default.xbe`) and
  `7FE15B66D21C9B66153D7129C3E4B622BA74E58C9623E6AF0E88526B939F8FF4`
  (`efmp.xbe`).
- Native XEMU captures passed for SP `borg6` and active Holomatch `hm_dn1`.
  The Holomatch sequence visibly changed rooms, rendered bots and a corpse,
  and exercised the phaser beam while preserving world lighting and HUD
  output. Its 90-second report is
  `scripts/output/stefx-retail-worldclean-hm_hm_dn1_20260816_215109.report.txt`.
- A later apparent Holomatch stall was caused by the smoke runner's perf-only
  path still translating and reading every scattered telemetry page. The
  runner now reads only the compact heartbeat/class-state block and emits one
  concise sample line. The corrected 120-second repeat advanced from frame 541
  to frame 3543 without a repeated heartbeat, exception, or hang:
  `scripts/output/stefx-retail-worldclean-hm-compactpoll_hm_dn1_20260816_215907.report.txt`.
  Its quantized monitor-derived wall rate averaged 28.8 FPS. This is useful
  XEMU evidence but not a retail-hardware performance claim.
- Overall renderer parity is approximately 70%. Native D3D8 draw submission
  and hot world traversal are substantially closer than that aggregate; the
  remaining work is concentrated in backend state orchestration, visual edge
  cases including the deferred 2D atlas issue, and retail-hardware performance
  qualification.

## Object-Level Parity Correction

- Linked-XBE function extents are not authoritative for current-vs-retail
  similarity after COMDAT folding and section reordering. The prior report
  incorrectly measured `dllDrawElements` as 679 instructions and
  `R_RenderView` as 159 by crossing their actual function boundaries.
- `scripts/compare_retail_renderer_objects.py` now compares the current and
  clean FinalBuild COFF objects directly through the XDK 5558 linker
  disassembler. Direct evidence gives 448 instructions for `dllDrawElements`
  in both objects and 59 for `R_RenderView` in both objects.
- Across the 14 shared retail backend objects, Holomatch has 1,059 common
  disassembled functions: 970, or 91.6%, have identical instruction counts and
  830 have identical normalized instruction text. SP reports the same 970
  same-length bodies across 1,057 common functions. The small common-count
  difference is personality-specific surface linkage, not a second backend.
- The approximately 70% estimate remains intentionally conservative for the
  complete renderer, including Elite Force format owners and unfinished visual
  behavior. The common native D3D8 backend itself is approximately 85-90%
  structurally aligned with retail JA MP.
- `GLW_Init` uses the retail 640x480 backbuffer, A8R8G8B8 color, LIN_D24S8
  depth, no multisampling, discard swap effect, hardware vertex processing,
  and default presentation interval. Its current difference is deferred
  texture-pool startup ownership, which executes once and cannot explain
  sustained gameplay frame time.

## Sustained-Frame Candidate Closure

- The active executable does not compile or link the experimental FakeGL
  backend. QGL dispatch resolves directly to the native `win_qgl_dx8.cpp`
  implementation in both personalities.
- Texture residency is not churning during the measured gameplay path. The
  retained runtime scan reports approximately 2.80 MiB in the static texture
  pool and 15.5 KiB in the skin pool, with zero swap, fetch, wait, read, or
  write events. The retail `models/players` skin classification also includes
  Elite Force's `models/players2` names without an adapter.
- Both renderer personalities are compiled with the release speed settings
  `/Ox /Ob2 /Oi /Ot /Oy /G6 /arch:SSE`; stale or debug-optimized objects are
  rejected by the build fingerprints. Compiler mode is therefore closed.
- Shader-stage execution uses the retail pass count and batch-break contract.
  The low object-level similarity in `tr_shader.obj` is concentrated in
  load-time parsing and Elite Force shader-format ownership, not an extra
  sustained stage loop. Collapsing additional passes would remove valid
  lightmap/effect work.
- Runtime image size is not abnormal: the current `default.xbe` is 4,313,088
  bytes and `efmp.xbe` is 4,087,808 bytes, both smaller than shipping
  `jamp.xbe` at 4,988,928 bytes. The previously observed approximately 15 MiB
  artifact was the external PDB and consumes no Xbox runtime image memory.
- Packed BSP conversion preserves source surfaces one-for-one. The complete
  116-map audit already proved raw/packed surface-count parity; focused
  `borg6`, `hm_borg1`, and `hm_dn1` conversions also retain their original
  face, patch, flare, vertex, and index granularity. The live sort/decompose,
  begin-surface, stage-iterator, and ordinary face-emission functions all have
  retail-identical instruction counts. No conversion-side surface duplication
  or accidental extra renderer pass was found.
- The apparent `R_CullSurface` size discrepancy is beneficial dead-code
  removal, not a missing visibility test. The omitted body is JA's very slow
  automap screenshot roof tracer; normal face-plane, patch, triangle,
  frustum, PVS, area-mask, and duplicate-surface culling remain intact.
- These closures leave live scene/entity adaptation, backend state/2D visual
  parity, and retail-hardware push-reservation behavior as the unresolved
  renderer areas. None of the closed candidates above should be revived
  without new contradictory runtime evidence.

## Shipping Build-Mode Qualification

- The clean retail JA MP donor was compiled with both `FINAL_BUILD` and
  `_FINAL`. The active SP and Holomatch executable/game projects now use that
  same shipping contract; renderer translation units were already using it.
  This removes debug/profile/assert branches without changing EF game-library
  ownership or the shared native D3D8 renderer architecture.
- The corrected build improves the six-function frame/system object sample
  from 38 to 44 same-length bodies and from 28 to 34 exact bodies out of 100
  common functions. `Com_Frame` falls from 604 to 566 instructions, but still
  remains materially larger than retail JA MP at 251. `SCR_UpdateScreen`
  remains 267 instructions versus retail's 81, so the shipping defines do not
  close the whole per-frame orchestration gap.
- Fresh sequential XDK 5558 builds succeeded. Source XBE SHA256 values are
  `1BECDD56398B052FB850468CF37C42DFE003D2651C1DF9D8A76BF29707C8734C`
  (`default.xbe`) and
  `90B18DC9345411E3BFFE8AAB6AA74928E0FB50F5C9CF9F45911FCCC7E19A7C68`
  (`efmp.xbe`).
- SP `borg6` and Holomatch `hm_dn1` each passed a 90-second native XEMU/LLE
  smoke with coherent lighting, weapons, HUD, entities, and sustained frame
  advancement. Reports are
  `scripts/output/stefx-retail-finalmode-sp_borg6_20260817_001359.report.txt`
  and
  `scripts/output/stefx-retail-finalmode-hm_hm_dn1_20260817_001709.report.txt`.
  Guest counters averaged 90.5 and 90.9 FPS respectively; monitor-derived wall
  rates averaged 28.6 and 28.7 FPS.
- The prior comparable Holomatch wall rate was 28.8 FPS. The shipping build
  contract is therefore retained as correctness/parity work, but it is not a
  measurable XEMU performance breakthrough. Retail hardware still decides
  whether the removed branches affect the previously observed frame cost.
- A full 31-object comparison now finds 2,002 common functions, 1,723 (86.1%)
  with identical instruction counts, and 1,451 (72.5%) with exact normalized
  instruction text. The next reconstruction target must be selected from live
  per-frame mismatches, not aggregate differences dominated by load-time BSP,
  image, font, or shader parsing.

## Aligned Push-Backpressure Checkpoint v25

- The profiler was rebuilt for real after discovering that
  `scripts/build_xbox.ps1 -Clean` removes intermediates and then returns from
  each project build; `-Clean` is cleanup-only and must not be treated as a
  clean rebuild. Sequential non-`-Clean` XDK 5558 builds produced source hashes
  `96FE2574E14D124E9C5BDFFB18FFD248FBCD134D2C30E163E823DFEE344032A2`
  (`default.xbe`) and
  `5FED2B19C7DA8B77F0A89C2C2D676FCA29D47BDE31E14FB8DBD26D9016734111`
  (`efmp.xbe`).
- SP `borg6` and correctly repacked Holomatch `hm_dn1` XEMU/LLE runs both
  reached sustained gameplay with intact visual output and approximately 90.9
  guest FPS. The Holomatch proof is
  `scripts/output/stefx-retail-profile-v25-hm-repack_hm_dn1_20260817_011350_contact.png`.
- The first active Holomatch sample captured a 22,196,166-cycle individual
  `D3DDevice_BeginPush` call for a reservation of only 763 dwords. Its frame
  reserved 101,954 dwords total and spent 22,692,344 cycles in reservation.
  Later frames with essentially identical geometry reserved about 100,500
  dwords in 0.49-0.65 million aggregate cycles and had no call over one
  millisecond. This is direct evidence of intermittent queue back-pressure,
  not a malformed oversized packet or CPU vertex/index packing loop.
- The active link remains XDK 5558 retail `d3d8.lib`. XDK project templates
  confirm `d3d8i.lib` belongs to Profile configurations, but that diagnostic
  runtime was already isolated, measured slower, and removed. The world cull
  size gap is JA's disabled automap roof tracer, and `CollapseMultitexture` is
  an authoritative shipping-source match; neither is a valid performance fix.
- The next bounded measurement must classify indexed submissions by shader
  pass/state and texture usage so a persistent expensive GPU workload can be
  separated from an occasional queue-consumption stall without changing
  rendering behavior.

## Non-Intrusive Retail-Parity Checkpoint v31

- The always-on client frame profiler in `cl_main.cpp` is now compiled only
  when `STEFX_HW_FRAME_DIAGNOSTICS` is explicitly enabled. Normal release
  builds retain a lightweight 30-second heartbeat but no per-frame timestamp,
  aggregation, or exit logging. This closes a shared SP/Holomatch CPU cost that
  was never present in the shipping renderer.
- Texture-residency profiling in `win_qgl_dx8.cpp` and
  `xbox_texture_man.h` is gated by the same define. Normal builds no longer
  update swap, fetch, wait, byte-transfer, or pool counters in hot allocator
  paths. Pool capacities, allocation order, residency decisions, and swap
  behavior are unchanged.
- The complete 31-object renderer comparison now finds 2,002 common functions,
  1,727 (86.3%) with identical instruction counts, and 1,455 (72.7%) with
  exact normalized instruction text. QGL has 573 of 574 common functions at
  identical length; only load-time `GLW_Init` differs in length. Texture
  `Allocate` and `EndSkinTextures` are exact, while `Fetch` and
  `_swapAllTexturesToDisk` have retail-identical instruction counts.
- Sequential XDK 5558 builds produced source hashes
  `1A9E9F89C9042A64B46B210322EDE14DBAC924E097679E12A21FCDDA45F85F5D`
  (`default.xbe`) and
  `D1E5A4221B22B90BCF2D4B920945C62262E47E512F8FAA8EC00269DCE8334090`
  (`efmp.xbe`).
- Native XEMU/LLE visual qualification passed for 60 seconds in SP `borg6`
  and Holomatch `hm_dn1`, with both processes alive at deliberate shutdown and
  coherent worlds, weapons, lightmaps, entities, and HUD output. Reports are
  `scripts/output/stefx-retail-allocator-clean-v31-sp_borg6_20260817_034122.report.txt`
  and
  `scripts/output/stefx-retail-allocator-clean-v31-hm_hm_dn1_20260817_034359.report.txt`.
- This raises measured structural parity, but the conservative overall
  renderer estimate remains approximately 80-85% because Elite Force format
  ownership, 2D edge cases, and retail-hardware behavior still require proof.
  The allocator/profile cleanup has not yet demonstrated a hardware FPS gain.
  The next shared-system audit is the oversized `Com_Frame` and
  `SCR_UpdateScreen` orchestration outside the renderer.

## Shared Frame-Loop Cleanup Checkpoint v32

- Normal Release builds now gate the dormant per-frame phase breadcrumbs,
  volatile profiler writes, and timestamp collection in `Com_Frame`,
  `CL_Frame`, and `SCR_UpdateScreen` behind `STEFX_HW_FRAME_DIAGNOSTICS`.
  Functional direct-map and automated-smoke control, split-screen state,
  client/server advancement, input, audio, cinematics, console animation,
  rendering, the temporary FPS overlay, and the 30-second heartbeat remain.
- `SCR_UpdateScreen` now passes timing output pointers to `re.EndFrame` only
  for a diagnostic build or when `com_speeds` requests them. Ordinary Release
  frames use the retail `re.EndFrame(NULL, NULL)` path.
- The shared SP and Holomatch object shapes agree. Against the clean shipping
  JA MP frame donor, `Com_Frame` is 166 instructions versus 251 retail, down
  from 566 before cleanup. `SCR_UpdateScreen` is 43 versus 81 retail, down from
  267. The three-object sample still has 57 common functions, 23 with matching
  instruction counts and 17 exact; the target functions are now smaller than
  retail because Elite Force does not own every JA MP orchestration branch.
- Sequential XDK 5558 builds produced SHA256
  `81E8E12E50D8E7AAC35FF00752269CA7309C056859336A0CE8F7B6BF51F2058F`
  for `default.xbe` and
  `E50841D259D8A9D2542C8A79598F9789E05D5E284FC6D71BF0FF975CBF18C80E`
  for `efmp.xbe`.
- Native XEMU/LLE qualification passed for 60 seconds in SP `borg6` and
  Holomatch `hm_dn1`. Both processes remained alive until deliberate shutdown;
  inspected frames show coherent lightmaps, world geometry, weapons, HUDs,
  movement, and the Holomatch phaser beam. Reports are
  `scripts/output/stefx-retail-frameclean-v32-sp_borg6_20260817_041119.report.txt`
  and
  `scripts/output/stefx-retail-frameclean-v32-hm_hm_dn1_20260817_041348.report.txt`.
- The captured XEMU in-game overlay read approximately 19 FPS for the SP scene
  and 30 FPS for the Holomatch scene. Those values are suitable only for
  emulator regression checks. They do not establish a retail-hardware gain;
  the next hardware run must test the exact staged v32 pair.

## Shared Sustained-Frame Telemetry Cleanup v33

- Normal Release builds no longer collect dormant per-tick server timing,
  write cgame/VM phase markers around every rendered frame, or count PVS
  outcomes for every world leaf solely to populate split-screen diagnostics.
  The functional Xbox server catch-up tick count and renderer split-slot
  selector are unchanged.
- The renderer comparison now gives `R_MarkLeaves` 105 instructions in SP,
  Holomatch, and the shipping JA MP donor. The complete Holomatch renderer
  sample remains 2,002 common functions, 1,727 with matching instruction
  counts, and 1,455 exact. This cleanup removes non-shipping work without
  inflating the aggregate parity claim.
- Sequential XDK 5558 builds produced SHA256
  `9FB1DB81AC1D78EE46B192D088D263DA4D2E50C166E74D5D9059072A9E9D93B3`
  for `default.xbe` and
  `8C58C03BC4BF83FC8662A72167C09E7CB96D7FE5A79368FC400092AA4CED6BD8`
  for `efmp.xbe`.
- SP `borg6` and active Holomatch `hm_dn1` each passed a 60-second native
  XEMU/LLE visual run with continuous frame advancement and intact world,
  lightmaps, HUD, pickups, movement, and weapon effects. Reports are
  `scripts/output/stefx-retail-frametelemetry-v33-sp_borg6_20260817_042708.report.txt`
  and
  `scripts/output/stefx-retail-frametelemetry-v33-hm_hm_dn1_20260817_042942.report.txt`.
- The fixed SP scene remained about 19 FPS and active Holomatch about 30 FPS in
  XEMU. No emulator performance gain is attributed to v33; retail hardware is
  still required to judge whether removing volatile writes and timer calls
  matters on the target CPU.

## Shared MDR Palette Cache v34

- Elite Force's MDR path is shared by SP and Holomatch but has no equivalent in
  the retail JA renderer donor. The original `RB_SurfaceAnim` implementation
  rebuilt the complete bone palette for every surface of an MDR model. Runtime
  package inventory found 108 MDRs averaging 22.15 bones and 3.98 surfaces,
  with maxima of 41 bones and 13 surfaces.
- `code/renderer/tr_animation.cpp` now uses a bounded eight-slot per-frame
  palette cache keyed by backend entity, MDR header, frame pair, interpolation,
  and render time. Compressed and interpolated palettes are prepared once per
  entity and reused by its sorted surfaces. Uncompressed non-interpolated
  frames still use the source palette directly. Skinning math and vertex output
  are unchanged.
- Sequential XDK 5558 builds produced SHA256
  `A670EE013F1E192BBCE9D1DA84B484078AC35570CCE893A4D0D5DCD4AC8ACD12`
  for `default.xbe` and
  `338C66DB44E58677EE3A5625B5A3DF2CA731F0426AA457F976D1CBFB122F16E5`
  for `efmp.xbe`.
- SP `borg6`, Holomatch `hm_dn1`, and moving/combat Holomatch `hm_borg1` all
  passed native XEMU/LLE visual runs with intact world, lightmaps, HUD, weapon,
  and continuous frame advancement. The valid reports are
  `scripts/output/stefx-mdr-palette-v34-sp_borg6_borg6_20260817_050228.report.txt`,
  `scripts/output/stefx-mdr-palette-v34-hm_hm_dn1-valid_hm_dn1_20260817_050636.report.txt`,
  and
  `scripts/output/stefx-mdr-palette-v34-hm_hm_borg1-models_hm_borg1_20260817_050921.report.txt`.
  The earlier 05:04 `hm_dn1` run accidentally reused the SP-entry ISO and is
  explicitly excluded. The XEMU smoke runner now stores a compact profile for
  the retained ISO and forces repacking whenever the requested entry
  personality, map, or control data differs. Generated cleanup preserves only
  that profile beside the one retained ISO.
- Fixed-scene XEMU overlays remained approximately 19 FPS in SP and 30 FPS in
  Holomatch. This establishes visual/stability safety only. The hardware stage
  is the performance authority for this CPU-side optimization.

## Retail Shader And Texture Contract v38

- The shipping JA MP shader module is now the active shared parser and shader
  construction implementation. Its filesystem scan was adapted only at the
  game-data boundary: Elite Force stores shader files under `scripts/`, while
  the donor searched `shaders/`. SP `borg6`, Holomatch `hm_borg1`, and
  Holomatch `hm_dn1` all reached gameplay with coherent materials after that
  correction.
- The shipping Xbox package overrides the renderer source default from 16x to
  1x anisotropy. The previous Elite Force path clamped that unoverridden
  source default to 4x, so the shared renderer now explicitly applies the
  shipping 1x policy. Both shipping JA Xbox configurations also use
  `r_picmip 1`; the historical Holomatch-only `r_picmip 0` exception came from
  an early experimental graft and has been removed.
- Sequential XDK 5558 builds produced source hashes
  `2FFC48E555AEED88030106BA8DAB19AF59B1359FD42C40CF43CAEE37D34CFA16`
  (`default.xbe`) and
  `269B01BE58DE7913F173E848D303D67CD9C90DDAFE39C6821879EC999613663D`
  (`efmp.xbe`). Native XEMU/LLE visual qualification reports are
  `scripts/output/retail-texture-contract-v38-sp_borg6_20260817_081112.report.txt`
  and
  `scripts/output/retail-texture-contract-v38-hm-dn1_hm_dn1_20260817_081544.report.txt`.
- The exact pair is staged in the single approved hardware folder as
  `Beta-20260817-retail-texture-contract-v38`. Emulator frame rate did not
  separate v37 from v38, so only the retail Xbox run can determine whether
  texture sampling and residency traffic explain a material portion of the
  prior 67-70 ms backend cost.
- The packed Elite Force BSP path does not reconstruct surface geometry during
  sustained rendering. Its live SPARC use is the shipping-style visibility
  cluster cache, and the prior hardware trace assigned only 3-5 ms to server
  work. Converted-lump reconstruction is therefore closed as the source of the
  measured 49 ms renderer frontend cost unless new runtime evidence contradicts
  that result.

## Retail Runtime Vertex Contract v39

- The retail surface module was previously consuming a different runtime
  vertex ABI than the active Elite Force loaders produced. Shipping Xbox
  `drawVert_t` stores float positions and normals, short texture/lightmap
  coordinates scaled by 512, and two packed color bytes per lighting style.
  The inherited path retained short positions/normals, a scale of 128, and
  four color bytes per style. That made the retail assembler compensate with
  sustained integer-to-float work and made the color stride incompatible with
  the donor contract.
- The shared runtime type now follows the shipping ABI. Elite Force BSP
  ownership is preserved: map-format shorts are expanded to runtime floats
  once in `ParseTriSurf`, rather than treating the maps as JA BSPs or adding
  per-frame conversions. Patch subdivision and mark projection use the same
  float runtime vertices. Compact color storage is active across the loader,
  patch, legacy, and retail surface paths.
- The object comparison moved from 2,010 common / 1,752 same-length / 1,462
  exact functions at v38 to 2,009 / 1,759 / 1,469 at v39. In particular,
  `RB_SurfaceTriangles` moved from 207 instructions to the donor's 200, and
  `RB_SurfaceGrid` moved from 353 to the donor's 344. `LerpMeshVertexes`
  remains at the donor's exact 370-instruction length.
- Sequential XDK 5558 builds produced source SHA256
  `D91C8622AB139E3191C0DA5CE9FA07C2414760D9F3F7D94333D4A565A4EC476F`
  for `default.xbe` and
  `E312FC6F4ECEAAB27ADB0A66999C0E482A75D151708F4018B9C1E45888F8C919`
  for `efmp.xbe`. The complete comparison is
  `build/analysis/retail-v39-full-compare.json`.
- Native XEMU/LLE proofs passed for SP `borg6`, Holomatch `hm_dn1`, and CTF
  `ctf_breach`. The latter deliberately covers patches and triangle soups,
  which the simpler maps do not. All processes remained alive through the
  harness shutdown and inspected frames retained coherent geometry, lighting,
  models, weapon/effect output, HUDs, and colors. Reports are
  `scripts/output/retail-vertex-contract-v39-sp_borg6_20260817_091811.report.txt`,
  `scripts/output/retail-vertex-contract-v39-hm-dn1_hm_dn1_20260817_092514.report.txt`,
  and
  `scripts/output/retail-vertex-contract-v39-ctf-breach_ctf_breach_20260817_092833.report.txt`.
- A normal-boot regression run reached and held the SP main menu with coherent
  LCARS panels, fonts, contextual button art, and galaxy imagery. Its report is
  `scripts/output/retail-vertex-contract-v39-normalboot_normal_20260817_094053.report.txt`.
  This does not close the previously reported 2D issue without hardware/user
  confirmation.
- The single hardware folder is staged as
  `Beta-20260817-retail-vertex-contract-v39`. Its media-enabled XBE hashes are
  `E6194D8BC586FDEAF0978C07F2F3E6B7CDE5D1E4DFC37A1C0DD71E142FA8E62D`
  and
  `32AC73AC7C5C6C083C25CCFEBEFF50D9D3150F4BE0359D8831E2262EDAD4C180`.
  Emulator correctness is established; retail hardware still decides the
  performance value of removing the sustained vertex conversions.

## Retail Shader ABI v40

- The active shader records still included five PC-only members omitted by
  the shipping Xbox renderer: video-map state and handle in
  `textureBundle_t`, specular and glow flags in `shaderStage_t`, and the
  aggregate glow flag in `shader_t`. The donor source also excludes their
  parsing and backend use under `_XBOX`; no packaged Elite Force shader uses
  the disabled video-map or specular directives.
- The shared header now reproduces the donor's 24-byte bundle, 112-byte stage,
  and 156-byte shader layouts, with compile-time Xbox size assertions. This
  keeps stage copies, collapse logic, and backend traversal on one exact ABI
  for both SP/co-op and Holomatch.
- Link-map review showed that the legacy backend, commands, light, main,
  scene, shade, shade-calc, shader, sky, and world objects supplied zero
  symbols to both `default.xbe` and `efmp.xbe`. Their project entries were
  removed, leaving the retail counterparts as explicit owners. The Elite
  Force `tr_surface.obj` remains because it supplies 77 extension symbols in
  addition to the retail surface module.
- The complete comparison is
  `build/analysis/retail-v40-shader-abi-compare.json`: 2,009 common functions,
  1,760 with matching instruction counts, and 1,470 exact. `R_CopyStage`
  became exact and `CollapseMultitexture` reached the donor instruction count.
- Sequential XDK 5558 source hashes are
  `77B671BC94EAE2D3B6D9699B8A9BC94B20C17057CA2488F94E50FE204A71D397`
  (`default.xbe`) and
  `86D3E4DD7BF78F742865451C9319C845D4F18784A011228B63370B09A04C6512`
  (`efmp.xbe`). Visual proof passed in SP `borg6`, Holomatch `hm_dn1`, and CTF
  `ctf_breach`; all three remained alive with coherent lighting, materials,
  models, weapons, and HUDs. Reports are
  `scripts/output/retail-shader-abi-v40-sp_borg6_20260817_103153.report.txt`,
  `scripts/output/retail-shader-abi-v40-hm_hm_dn1_20260817_103548.report.txt`,
  and
  `scripts/output/retail-shader-abi-v40-ctf_ctf_breach_20260817_103839.report.txt`.
- The single hardware folder is staged as
  `Beta-20260817-retail-shader-abi-v40`; hardware remains the authority for
  performance against the prior roughly 5-8 FPS result. The known 2D
  corruption issue is deliberately still open.

## Retail Private ABI v41

- A compiler-driven 52-type ABI probe separated genuine private-renderer
  drift from required Elite Force differences. `glstate_t` still reserved four
  texture-unit cache entries even though the shipping Xbox renderer exposes
  two and every active caller selects only units zero or one. It now matches
  the donor's exact 32-byte layout. `shaderCommands_t` also retained five
  unused Xbox pointer members with no readers or writers; removing them
  restores the exact 132,056-byte shipping layout. Compile-time assertions
  guard both contracts.
- ABI parity is now 41 of 52 measured records. The remaining differences are
  classified rather than blindly copied: Elite Force scene/entity records,
  its distinct BSP surface/grid/world contracts, split-screen backend state,
  and deliberate render-command/storage capacities. In particular, Jedi
  Academy's grid stitching and renderer-owned entity-string fields do not
  describe Elite Force's BSP loader.
- The complete comparison is
  `build/analysis/retail-v41-private-abi-compare.json`: 2,011 common functions,
  1,762 (87.6%) with matching instruction counts, and 1,496 (74.4%) exact.
  Exact gains landed in backend, image, shade calculation/submission, sky,
  surface, and D3D wrapper objects. The accompanying ABI report is
  `build/analysis/retail-v41-renderer-abi-sizes.json`.
- Sequential XDK 5558 source hashes are
  `30F3C90AAADCDC2403992EAD11D6B6B32F96FE95CAB872BBFC2FE91F400FF593`
  (`default.xbe`) and
  `5E4232BD0EAB441CFDF30CC260B2DF4890A4A866346688A129FDE0547DC983C9`
  (`efmp.xbe`). Visual proof passed in SP `borg6`, Holomatch `hm_dn1`, and CTF
  `ctf_breach`; all three remained alive with coherent geometry, lightmaps,
  materials, models, weapons, and HUDs. Reports are
  `scripts/output/retail-private-abi-v41-sp_borg6_20260817_111943.report.txt`,
  `scripts/output/retail-private-abi-v41-hm_hm_dn1_20260817_112217.report.txt`,
  and
  `scripts/output/retail-private-abi-v41-ctf_ctf_breach_20260817_112451.report.txt`.
- The single hardware folder is staged as
  `Beta-20260817-retail-private-abi-v41`; media-enabled hashes are
  `54E42BAC6D031D3BC99471E2A018717F6B0CEEA77000D431D0405BBA7897623A`
  and
  `9A61570E6889E89CF942A9217B65A7583F97F8AA44CCB680981B67E432707A48`.
  Hardware remains the FPS authority. Rough overall retail-renderer parity is
  approximately 76%; the known 2D corruption issue remains open.

## Retail Color-Generation Contract v42

- `colorGen_t` retained the PC-only `CGEN_SKIP` slot in Xbox builds even though
  shipping JA Xbox omits it. That shifted every later generator value consumed
  by shader parsing and backend color calculation. A complete repository and
  packaged-shader audit found no Xbox consumer and no Elite Force shader using
  `rgbGen skip`, so the slot is now restricted to non-Xbox builds.
- The complete comparison is
  `build/analysis/retail-v42-colorgen-compare.json`: 2,011 common functions,
  1,763 (87.7%) with matching instruction counts, and 1,497 (74.4%) exact.
  `NeedVertexColors` became exact, while `ComputeColors` moved from a
  different-length implementation to the retail instruction count.
- Sequential XDK 5558 source hashes are
  `2B55ACB925A8339CB9867D8D372B47E0C028A2129708F3F90B3565614B182834`
  (`default.xbe`) and
  `118D91519983417ABA4B2CBB6A76AAF13D730B9E98380AA2FF9D3C641E9AB340`
  (`efmp.xbe`). SP `borg6` and Holomatch `hm_dn1` passed native XEMU/LLE
  visual and end-of-run liveness gates. Reports are
  `scripts/output/retail-colorgen-v42-sp_borg6_20260817_113859.report.txt` and
  `scripts/output/retail-colorgen-v42-hm_hm_dn1_20260817_114157.report.txt`.
- The single hardware folder is staged as
  `Beta-20260817-retail-colorgen-v42`; media-enabled hashes are
  `F050E23108A3734FE5BE7CC6E6AD7693D113AF6D425381D6791C0DE75F6F3BD3`
  and
  `CF3985D75DEB578CC77E614F7586A6F0D31F6A0B59DFEE03E0D8065FA0618E65`.
  Hardware remains the performance authority; this checkpoint proves the
  contract and visual behavior, not a retail-console FPS gain.

## Retail Enum and Field Contracts v43-v44

- Common surface values now retain the shipping Xbox order. The Elite
  Force-only `SF_MDR` value is appended after the common range, and both the
  retail and extension dispatch tables follow the corrected identities.
  The v43 object comparison is
  `build/analysis/retail-v43-surface-enum-compare.json`: 2,011 common
  functions, 1,763 with matching instruction counts, and 1,498 exact.
- The remaining shared enum audit moved Elite Force-only model kinds and
  `RC_SCISSOR` after the retail common ranges and restored the donor's
  appended Xbox fog-policy value. Compile-time assertions now guard the
  common model and render-command identities.
- `scripts/compare_retail_renderer_abi.py` now emits compiler-derived
  `offsetof` probes for 220 fields in hot shared renderer records. The v44
  report, `build/analysis/retail-v44-renderer-abi-offsets.json`, matches all
  220 donor offsets. The type-size result remains 41 of 52 exact; the 11
  known differences are required Elite Force scene/BSP records,
  split-screen backend state, or deliberate command/storage capacities.
- Sequential XDK 5558 builds produced source SHA256
  `624089516FCB3389C76D1E3457AE1F8045A3A1269DCC32F2DFA3342971C10E96`
  for `default.xbe` and
  `36AF459EAD5D374FD73B347BDD7FD279F3E0D99C403B6C123DE07E33F56CBAD6`
  for `efmp.xbe`. The first Holomatch package pass encountered Windows error
  1450 while rebuilding the sound bank; the standalone retry completed and
  restored the full 500,246,186-byte, 7,971-record bank.
- SP `borg6` and Holomatch `hm_dn1` passed native XEMU/LLE visual and
  end-of-run liveness gates with coherent geometry, lightmaps, materials,
  weapons, and HUDs. Reports are
  `scripts/output/retail-enum-v44-sp_borg6_20260817_124959.report.txt` and
  `scripts/output/retail-enum-v44-hm-clean_hm_dn1_20260817_130609.report.txt`.
  Static-view counters were approximately 31-63 FPS in SP and 90 FPS in
  Holomatch. These numbers are emulator regression evidence only; retail
  hardware remains the performance authority. Rough overall retail-renderer
  parity is approximately 79%, and the known 2D corruption issue remains
  open.

## Retail Numeric and State Contracts v45

- The compiler-driven ABI probe now includes all 30 packed `GLS_*` values
  carried in shader `stateBits`. They match the clean retail Xbox donor
  exactly. The existing results remain 220 of 220 hot field offsets exact and
  134 of 135 enum values exact.
- The one enum difference is expected and classified explicitly rather than
  treated as unexplained drift: retail has 12 surface implementations, while
  Elite Force appends `SF_MDR` at index 12 and therefore requires a 13-entry
  dispatch table. Every common surface ID remains retail-identical.
- `GL_InvalidateCurrentTexture` and `GL_InvalidateTextureUnit` now enforce the
  two-unit bound of the retail-exact `glstate_t`. The routines currently have
  no active callers, so this closes a latent adjacent-state overwrite without
  changing the qualified draw path.
- Sequential XDK 5558 source builds produced SHA256
  `503C43DFFF5D0F8C2313688BCABA125E9E80B047F885E0AD7539C6AE48585E6C`
  for `default.xbe` and
  `70AE58AD29EB6A88AB1A5A2C574C1469E4124FF5A4CB4803F371045F0D7CFF84`
  for `efmp.xbe`. The complete audit is
  `build/analysis/retail-v45-renderer-abi-enums-state.json`.
- Native XEMU/LLE visual gates passed SP `borg6` and a moving, turning, firing
  Holomatch `hm_dn1` run. Both were alive at deliberate shutdown with coherent
  geometry, lightmaps, materials, weapon/effect output, and HUDs. The reports
  are
  `scripts/output/retail-contract-v45-sp-borg6_borg6_20260817_133446.report.txt`
  and
  `scripts/output/retail-contract-v45-hm-dn1-retry_hm_dn1_20260817_134057.report.txt`.
  The SP screenshots showed approximately 80-86 FPS in a static wall view;
  the changing Holomatch views showed approximately 87-89 FPS. These are
  emulator regression checks only. A preceding Holomatch attempt is excluded
  because XEMU did not expose its monitor socket and never reached gameplay.
- A subsequent v45 CTF `ctf_breach` run with three bot clients and scripted
  movement, turning, and firing also passed through deliberate shutdown. Six
  changing-view captures showed coherent shuttle-bay and corridor geometry,
  lightmaps, materials, pickups, weapons, and CTF HUD at approximately 81-91
  guest FPS. Its report is
  `scripts/output/retail-contract-v45-ctf-retry_ctf_breach_20260817_141907.report.txt`.
  Two attempted dynamic SP retries are excluded: XEMU exited before guest boot
  because another host process held the shared `xbox_hdd.qcow2`; neither is a
  renderer or game result.
- After that host lock cleared, the retained dynamic SP ISO passed. Early
  `borg6` captures show movement from the initial corridor into a populated
  room with NPC and portal/effect rendering at approximately 88-91 guest FPS;
  later frames show the scripted player stopped against a column and are used
  only for liveness. The run remained alive through deliberate shutdown. Its
  report is
  `scripts/output/retail-contract-v45-sp-dynamic-final_borg6_20260817_142701.report.txt`.
- Retail hardware is still required to establish whether this cumulative
  renderer reconstruction improves the prior roughly 5-8 FPS result. The
  known 2D corruption issue remains open, and rough overall parity remains
  approximately 79%.
- The single approved hardware folder is staged as
  `Beta-20260817-retail-contract-v45`. Its media-enabled SHA256 values are
  `B7C46AE8527F6BE1247BD8110A86112EECA4D38176481C014C816A58C122DC77`
  (`default.xbe`) and
  `7A8597704B72E2C9F6DE01508A43FDFBDC4C60421554639DBFE574CDCCFE9C77`
  (`efmp.xbe`). The staging verifier confirmed a single `0x7D` to `0xEB`
  media-enable patch in each XBE and byte-identical `xbox0.pk3`/`xbox1.pk3`.

### Layout-independent runtime structure comparison

`scripts/re_compare_retail_renderer_runtime.py` now reports an additional
instruction-structure ratio that ignores register allocation, stack slots,
record-field offsets, and literal values while retaining operation and operand
classes. Against the shipping `jamp.xbe`, the current v45 backend reports:

- `dllDrawElements`, `R_DrawElements`, and `RB_DrawSurfs`: 100% structure.
- `RB_BeginDrawingView`: 99.5%.
- `R_AddDrawSurfCmd`: 96.7%.
- `RB_RenderDrawSurfList`: 92.6%.
- `RB_ExecuteRenderCommands`: 88.5%.
- `RB_SetGL2D`: 71.4%.

`R_IssueRenderCommands` is source-identical to retail even though generated
code differs because the current compiler inlines performance-counter clears
and the Elite Force command buffer is deliberately larger. It is not a
runtime rewrite candidate. `RB_SetGL2D` contains required widescreen and
split-screen adaptation and remains associated with the known 2D corruption;
that issue must be solved with captured visual evidence rather than a blanket
texture-coordinate flip. The complete report is
`build/analysis/retail-v45-runtime-compare-structure.json`.

### Hot-path closure audit

- The normal 3D frame path contains no unaccounted synchronization drain.
  `dllBeginFrame` performs the retail `BeginScene`, `dllEndFrame` performs the
  retail `EndScene`/viewport/`Present` sequence, and the shared renderer does
  not insert a hidden `BlockUntilIdle` while `r_finish` remains at its retail
  default of zero. The explicit finish/flush wrappers block exactly as the
  shipping renderer does, but are not part of ordinary gameplay frames.
- The command submission chain is now classified rather than merely similar:
  `RE_EndFrame`, `dllEndFrame`, and the draw-element entry points are
  structurally exact; `RB_SwapBuffers` has the same operation shape; and
  `R_IssueRenderCommands` is source-identical after accounting for the larger
  Elite Force command buffer and inlined counter clearing.
- The hot QGL/D3D wrapper audit found no extra draw replay, state flush, or
  presentation call. The shipping XBE identifies D3D8 5558 QFE4 while the
  clean local SDK supplies QFE1, but control-flow comparison of 164 common
  runtime symbols found the principal submission calls identical in size and
  shape. The remaining differences are private-device offsets, relocations,
  and shader snapshot bookkeeping; without an authentic QFE4 library they do
  not justify embedding or transplanting the shipping D3D section.
- The old hardware profile that measured roughly 5-8 FPS predates v45 and was
  built with substantially heavier instrumentation. Its median frame was
  about 126 ms, split across frontend, backend, and end-frame work rather than
  one isolated blocking call. v45 compiles the detailed frame profiler out and
  retains only the bounded heartbeat. A new retail-hardware run of the exact
  staged v45 pair is therefore the next authoritative performance gate; the
  old log cannot establish that the current candidate is still slow.
- With that audit closed, the remaining parity estimate stays at 79% rather
  than rising on code similarity alone. The unresolved acceptance buckets are
  representative dynamic/CTF qualification, EF-owned frame and world
  adapters, the separately tracked 2D/HUD path, soak stability, and current
  retail-hardware FPS.

### Post-v45 bottleneck isolation

- A controlled `borg6` XEMU comparison injected `r_drawentities` only after
  the client reached `CA_ACTIVE`, avoiding the renderer-registration reset
  that invalidated the first attempt. The enabled control held approximately
  107-114 guest FPS at a fixed corridor view. The visually verified disabled
  run removed the view weapon and all dynamic entities but fell to 65.5 guest
  FPS instead of improving. XEMU timing makes the inverted magnitude
  unsuitable as a hardware prediction, but removing the complete entity path
  produced no gain and therefore does not support animated-entity work as the
  next primary bottleneck target. Evidence:
  `scripts/output/retail-v45-entity-ab-on-active-static-final_borg6_20260817_161521_contact.png`
  and
  `scripts/output/retail-v45-entity-ab-off-active_borg6_20260817_160649_contact.png`.
- A subsequent `r_drawworld 0` diagnostic is rejected: the command was present
  in the retained ISO profile, but every screenshot still contained the full
  BSP world. Its FPS values are not world-disabled evidence and are excluded
  from the candidate ranking.
- The shared server frame loop already limits Xbox catch-up to one simulation
  tick and discards excess residual time. This closes the earlier multi-tick
  feedback loop as an explanation for sustained v45 frame cost.
- The remaining decisive performance gate is a retail-hardware profile of the
  exact staged v45 pair. The available 5-8 FPS hardware profile predates the
  cached-zone residency correction and the compiled-out detailed profiler, so
  it cannot measure the current candidate.

### v45 2D parity boundary

- The current `RB_StretchPic` vertex and texture-coordinate ordering matches
  the retail Xbox source, and the low-level texture upload, vertex, and
  viewport wrappers are instruction-identical to the shipping renderer. A
  blanket U/V inversion is therefore excluded; that approach previously
  corrupted the wheel, fonts, and other overlays.
- `RB_SetGL2D` is the remaining meaningful 2D adapter. Retail uses four global
  viewport values updated for the active Xbox view, while Elite Force derives
  a split-screen viewport from `backEnd.viewParms` and otherwise uses the
  full display. The shipping function is 88 instructions and the adapted
  function is 108, with the additional control flow accounting for
  widescreen, full-screen menu, and split-screen decisions.
- Any 2D correction must preserve the retail projection and texture-coordinate
  conventions and change only that viewport-selection adapter. It requires
  captured full-screen and split-screen proof before acceptance; no source
  change is justified by the current static comparison alone.

## Elite Force 2D Viewport Adapter v47

- `RB_SetGL2D` now restores the full display viewport and top-left 640x480
  projection used by the last known-good Elite Force SP renderer. Split-screen
  scene rendering continues to use the independent per-player 3D viewports;
  only the already-positioned 2D command stream is returned to full-screen
  coordinates. No texture-coordinate inversion was added.
- Sequential clean-XDK-5558 source builds produced SHA256
  `72A289B50EF8D344590F4E9E18AE8FCC2D0ADC1AD0F039FD68E0CC83D2BE308B`
  for `default.xbe` and
  `1114906CEDE9EA3640975C89755F4F848FDA66F341CCEA29D21E46A6787BFEA4`
  for `efmp.xbe`.
- Native XEMU/LLE visual proof passed SP `borg6` and moving/firing Holomatch
  `hm_dn1`; both retained upright, correctly placed HUDs and coherent world,
  weapon, material, lighting, and effect output. Reports are
  `scripts/output/retail-v47-full2d-sp_borg6_20260817_170946.report.txt` and
  `scripts/output/retail-v47-full2d-hm-retry_hm_dn1_20260817_171806.report.txt`.
  A preceding Holomatch attempt is excluded because XEMU never exposed its
  monitor socket and did not reach gameplay.
- A 260-second direct co-op run reached two-player gameplay and remained alive
  through deliberate shutdown. Native frames show two independent 3D views
  with P1 and P2 HUDs correctly occupying the top-left corner of their own
  half-screen instead of being compressed or offset by a second viewport
  transform. Its report is
  `scripts/output/retail-v47-full2d-coop_normal_20260817_172109.report.txt`.
- The same co-op run isolates one separate presentation defect: the intro
  crawl shader is crisp but vertically inverted over an otherwise correct 3D
  backdrop. `CIN_AddTextCrawl` submits that text as a world-space polygon
  through `RE_RenderScene`, not through `RB_SetGL2D`, and its source is
  identical to the SP north-star checkpoint. It remains open as a dedicated
  poly/texture-orientation contract and does not invalidate the HUD proof.
- Rough renderer parity is now approximately 81%. Overall goal completion is
  approximately 75%; current retail-hardware FPS, the crawl orientation,
  remaining adapter edges, and final soak/packaging qualification remain open.

## Indexed submission workload profile v50

- Diagnostic-only phase and render-state counters were compiled into one
  Holomatch `hm_dn1` XEMU run. Production behavior and the shipping JA indexed
  push-buffer packet format were not changed.
- The retained render samples submitted 167-168 indexed draws, 4,537 vertices,
  16,518 shader-pass indices, 34 multitexture draws, and approximately 88,409
  reserved push-buffer dwords per frame. The workload was effectively constant
  across every sample.
- Render-state classification was likewise stable: 68 opaque and 99-100
  blended calls, one alpha-tested call, 99-100 no-depth-write calls, 18-19
  no-depth-test calls, and four two-sided calls. Blended/no-depth-write work
  covered 7,674 of 16,518 shader-pass indices, consistent with the expected
  second pass of lightmapped multipass materials rather than an accidental
  all-blended world.
- Indexed draw-path cost still varied from 0.90 to 2.98 million cycles while
  that workload remained constant. The worst individual `BeginPush` observed
  in this run was 431,126 cycles; unlike the earlier 21-million-cycle outlier,
  no reservation exceeded one millisecond. The evidence therefore does not
  identify a shader state, pass type, or reservation size that predicts the
  intermittent long wait.
- Guest timing held 89.2-90.9 FPS. XEMU delivered 17.5-35.0 wall FPS in an
  alternating cadence despite the stable guest workload, reinforcing that
  XEMU wall delivery is not a retail-hardware throughput proxy.
- Evidence:
  `scripts/output/retail-v50-hm-state-workload-profile_hm_dn1_20260817_203907.report.txt`
  and
  `scripts/output/retail-v50-hm-state-workload-profile_hm_dn1_20260817_203907_xblog_profiles.log`.
- A prior direct-co-op diagnostic that began simulated player input at twelve
  seconds is excluded from all parity evidence because it allowed control
  before the campaign crawl completed. The smoke harness now shifts the whole
  movement/attack schedule so its first input cannot occur before 210 seconds,
  even when custom timings are supplied. Relative timing and durations remain
  unchanged; this is a harness-only correction.

## Forced entity alpha parity v51

- The Holomatch render-flag translator already mapped official Elite Force
  `RF_FORCE_ENT_ALPHA` to the shared engine's reserved
  `RF_STEFX_FORCE_ENT_ALPHA`, but the retail stage iterator did not consume the
  translated flag. The retail forced-alpha branch is now restored for the
  Holomatch personality only; SP retains its separate `RF_ALPHA_FADE`
  contract.
- `RB_IterateStagesGeneric` moved from 386 to 399 instructions against the
  shipping JA renderer's 406. The remaining donor branch is JA's
  `RF_ALPHA_DEPTH`, which is not an Elite Force render contract and was not
  imported.
- Sequential clean-XDK-5558 source builds produced SHA256
  `4D83ED8090B2ED763D9C4EBAA116A02DFB00C79675BA2C6A4365D3F16AEE308B`
  for `default.xbe` and
  `16BCB810940F27BE6416BB50A44A5FEDC7991D3968D5257C6BEF59CD87B8425E`
  for `efmp.xbe`.
- A moving/firing `hm_dn1` XEMU smoke stayed alive through deliberate
  shutdown with coherent world, weapon, HUD, materials, and lighting. Its six
  retained frames did not contain a fading player, so this is regression
  proof only, not visual signoff of the forced-alpha effect itself. Evidence:
  `scripts/output/retail-v51-force-alpha-visual_hm_dn1_20260817_210859.report.txt`
  and
  `scripts/output/retail-v51-force-alpha-visual_hm_dn1_20260817_210859_contact.png`.

## Forced entity alpha ordering v52

- Holomatch forced-alpha draw surfaces now enter the same backend post-render
  queue used by the shipping renderer. Together with v51's stage-alpha branch,
  this restores both halves of the retail ordering contract while leaving the
  SP alpha-fade path unchanged.
- Sequential clean-XDK-5558 source builds produced SHA256
  `4D83ED8090B2ED763D9C4EBAA116A02DFB00C79675BA2C6A4365D3F16AEE308B`
  for `default.xbe` and
  `95569E4F66D9FC782E9F720B1C849A1F5915A78F0C45D6850DB42C533CDF3FC1`
  for `efmp.xbe`.
- A moving/firing Holomatch smoke passed broad visual and liveness regression.
  The captures did not contain an active fading player, so focused visual proof
  of the forced-alpha effect remains open.

## Runtime-library and hotspot audit closure

- No complete shipping D3D8 5558 QFE4 library is present in the supplied
  retail source trees or either local XDK. The apparent `Z:` candidates are
  tiny Win32 import libraries. The only complete Xbox static library found is
  the clean 5558 QFE1 `d3d8.lib`, so the known QFE delta is not a safe binary
  transplant opportunity.
- A sampled-EIP XEMU run repeatedly observed the Xbox kernel idle loop between
  delivered frames. That monitor cadence did not sample active frame work and
  is excluded from CPU-hotspot ranking.

## Texture residency audit v53

- A visible, zero-input SP `borg6` run reached gameplay and measured the
  retail allocator directly. Static texture use stabilized at 3,978 KiB of
  10,240 KiB. Skin texture use rose from 118 to 124 KiB of 4,096 KiB with zero
  swaps, fetches, waits, writes, or reads. Together with the churn-free v50
  Holomatch profile, this excludes texture-pool exhaustion and scratch-disk
  swapping as the shared hardware-FPS bottleneck.
- The retained diagnostic profile is
  `scripts/output/retail-v53i-sp-skin-residency_borg6_20260817_224053_xblog_profiles.log`.
  Its settled guest frames were 8-17 ms and submitted 102-109 indexed draws;
  these XEMU timings are diagnostic context, not a retail-hardware FPS claim.
- Several preceding headless samples are invalid because XEMU kept the process
  alive without advancing the game XBE. They also exposed a monitor-address
  bug. `ja_xemu_smoke.py` now translates a linked PE symbol to its generated
  XBE section-relative address and then uses `gva2gpa` before physical-memory
  reads. The corrected visible run produced coherent live counters and clean
  gameplay captures.
- The normal non-diagnostic SP build was restored after extraction. Current
  `default.xbe` SHA256:
  `076C25BD1A6DC6D9C08FE0620EACEB78DCED81A997B0DC8018E018B72B571203`.

## Corrected co-op input gate v54

- The earlier direct-co-op run that injected movement during the campaign
  crawl is invalid evidence. The harness now shifts the complete movement and
  attack schedule so the first synthetic input is no earlier than 210 seconds.
- A 65-second negative-control run intentionally supplied the rejected
  12-second schedule. Five native captures remained in the normally advancing
  crawl and showed no player camera, movement, or firing. The corrected proof
  is
  `scripts/output/retail-v54-coop-input-gate_normal_20260817_225710.report.txt`
  with contact sheet
  `scripts/output/retail-v54-coop-input-gate_normal_20260817_225710_contact.png`.

## Moving gameplay profile v54

- A diagnostic-only `borg6` run used delayed forward movement and continuous
  yaw so the retained samples covered an open room, a nearby actor, changing
  geometry, and a later close wall rather than one fixed low-workload view.
  The valid scene samples submitted 56-88 batches, 70-109 indexed draws,
  3,066-4,960 vertices, and 9,234-13,212 shader-pass indices per frame.
- Guest-side frame samples remained 7-14 ms, with 2-6 ms in the renderer
  backend, zero explicit finish/present wait, and approximately 0.49-1.55
  million measured draw-path cycles. Texture residency stayed unchanged and
  reported no skin-cache swap, fetch, wait, read, or write activity.
- XEMU delivered an average 29.1 wall FPS while the guest counters reported
  90.9 FPS. These values establish internal phase and workload relationships,
  not retail-hardware throughput. The final monitor snapshot found the Xbox
  kernel idle loop after the heartbeat stopped; it did not find a renderer
  loop or D3D wait. Treat that late stop as an emulator/harness event unless a
  non-instrumented repeat reproduces it independently.
- Evidence is
  `scripts/output/retail-v54-sp-moving-profile_borg6_20260817_231657.report.txt`,
  `scripts/output/retail-v54-sp-moving-profile_borg6_20260817_231657_xblog_profiles.log`,
  and
  `scripts/output/retail-v54-sp-moving-profile_borg6_20260817_231657_contact.png`.
- The active build uses the unmodified XDK 5558 shipping `d3d8.lib`, not
  `d3d8i.lib`. Shipping `jamp.xbe` records D3D8 5558 QFE4 while the available
  clean SDK is QFE1; the previously completed hot-runtime comparison remains
  authoritative and found no differing submission, fence, or swap algorithm
  that justifies an unsafe binary transplant.

## Production and hardware stage v54

- Sequential non-diagnostic XDK 5558 builds restored production SHA256
  `7E688B64C41C2AF5B385657864F733E41842D552322B9F3A8424EFB50654566E`
  for `default.xbe` and
  `CC9C895D5092B845392C1DB50D243C94D2895939129710EAA299ECC3310834AA`
  for `efmp.xbe`.
- The production SP `borg6` smoke remained alive through 70 seconds and showed
  coherent lighting, materials, HUD, weapon, actor, and eventual movement.
  The production Holomatch `hm_dn1` smoke remained alive through 80 seconds;
  four retained frames show changing viewpoints and visible weapon fire.
  Evidence is
  `scripts/output/retail-v54-production-sp_borg6_20260817_232737.report.txt`,
  `scripts/output/retail-v54-production-sp_borg6_20260817_232737_contact.png`,
  `scripts/output/retail-v54-production-hm_hm_dn1_20260817_233009.report.txt`,
  and
  `scripts/output/retail-v54-production-hm_hm_dn1_20260817_233009_contact.png`.
- Clean-release ISO generation now removes loose `.menu` and `.txt` parser
  artifacts from `BaseEF/ui`. Package validation checks the built XBE/PK3,
  while hardware staging validates the actual extracted ISO tree, closing the
  gap that previously allowed stale generated files to confuse release checks.
- The one retained hardware transfer directory is
  `build/hardware/StarTrekEliteForceX-Beta-20260801`. Its manifest version is
  `Beta-20260817-retail-v54-pk3only`; it reports zero diagnostic markers, zero
  loose runtime asset trees, and source hashes matching both production XBEs.
  The extracted stage contains 58 payload files totaling 1,767,444,361 bytes.
  Its source ISO SHA256 is
  `647CB77444455101B218CA9D014C841EB2ED6627415CA9D081B638675B781E13`.
  The extracted XBE hashes differ only at the expected XISO media-enable byte.
- The marker-free ISO passed an immutable 120-second normal-boot smoke without
  repacking or harness input. All six captures show the intact main menu, and
  the process remained alive through deliberate shutdown. Evidence is
  `scripts/output/retail-v54-pk3only-exact-iso_normal_20260818_000559.report.txt`
  and
  `scripts/output/retail-v54-pk3only-exact-iso_normal_20260818_000559_contact.png`.
- A complete 270-second direct-co-op gate run closed the earlier harness
  ambiguity only at the time-clock level. Captures through 170 seconds remain
  in the cinematic/crawl sequence and split-screen gameplay appears by 195
  seconds, but this is no longer accepted as a natural transition proof:
  generic smoke input was also permitted to call `CGCam_Disable` when its
  server-time threshold opened. Evidence retained for historical comparison is
  `scripts/output/retail-v54-production-coop-gate_normal_20260817_234239.report.txt`
  and
  `scripts/output/retail-v54-production-coop-gate_normal_20260817_234239_contact.png`.

## Co-op camera ownership gate v56

- Synthetic movement, attack, and view input now returns without modifying a
  command while `in_camera` is active. The cgame camera path no longer treats
  generic `stefx_smoke_input` as authority to terminate a cinematic; only the
  explicit `stefx_smoke_unlock_player` diagnostic retains that capability.
- A 135-second direct-co-op replay armed a conspicuous 90-degree view override.
  Eight captures show the crawl and subsequent scripted in-map camera advancing
  normally with no synthetic camera takeover, movement, or attack. The process
  remained alive through deliberate shutdown. Evidence is
  `scripts/output/retail-v56-coop-camera-ownership-gate_normal_20260818_005804.report.txt`
  and
  `scripts/output/retail-v56-coop-camera-ownership-gate_normal_20260818_005804_contact.png`.
- A matched 135-second replay with no smoke-input path staged on the disc
  followed the same authored crawl progression into the Borg environment and
  showed Munro in-map while the text was still scrolling. The disputed late
  shot is scripted crawl footage rather than early player control. Evidence is
  `scripts/output/retail-v56-coop-zero-input-control_normal_20260818_012014.report.txt`
  and
  `scripts/output/retail-v56-coop-zero-input-control_normal_20260818_012014_contact.png`.
- This closes the harness ownership defect, not co-op qualification. A natural
  camera completion followed by independent split-screen movement and FPS proof
  remains open.

## Hardware frame-phase diagnostic v58

- The normal `FINAL_BUILD` print path was audited before adding new telemetry.
  `Com_Printf`, `Com_PrintfAlways`, and `Sys_Print` do not perform sustained
  runtime disk writes in the active release builds, so ordinary console
  logging is closed as the shared roughly five-FPS hardware bottleneck.
- The large apparent `R_CullSurface` donor delta is JA's disabled automap roof
  tracing body. It is not active gameplay culling work and is also closed as a
  performance lead.
- Diagnostic-only builds now show coarse frame ownership directly in the
  upper-left overlay: total main loop, input, menu/handoff, `Com_Frame`, its
  unowned remainder, server, client, game, renderer front end, renderer back
  end, draw-surfaces, swap, Present, Finish, audio, and total screen draw. The
  outer-loop values close the original v57 blind spot before `Com_Frame`.
  `XBLog_Init` forces memory-ring mode in these builds so the timing experiment
  cannot be contaminated by synchronous disk logging.
- Sequential XDK 5558 builds produced raw SHA256
  `6BF7FD4587A2B79B3F084EB322DD416293DD457C8E838826E609E9E9ADB620E3`
  for `default.xbe` and
  `E0AF5B1FC906976F615A39C8908322C85B8DCC3ED347627A1750E4CC864B6D56`
  for `efmp.xbe`. The HDD/media-enabled staged hashes are
  `08CC5036019BC352E6D38F34C5CC8ACA42AD074A37AE42681E6DB7B6C4BC6899`
  and
  `89D87BA73540D5D020F028BD0D10DD60C7742B1AF7FFB79F0DA081266F183B93`.
- XEMU/LLE liveness and overlay-readability gates passed SP `borg6` and
  Holomatch `hm_dn1` for 45 seconds each. Representative frames showed
  `LP 10 / FR 10 / CL 6 / OT 4` for SP and
  `LP 10 / FR 10 / CL 10 / OT 0` for Holomatch. These are plumbing checks only
  because XEMU's guest clock does not reproduce the retail hardware frame
  time.
- Evidence reports are
  `scripts/output/framephase_v58_sp_borg6_20260818_021325.report.txt` and
  `scripts/output/framephase_v58_hm_hm_dn1_20260818_021535.report.txt`.
  The one retained hardware folder is staged for the decisive hardware phase
  split. Its exact payload passes the architecture/package check with no
  `codemp` dependency, no loose MP map or UI overrides, no original image
  formats, and 5,405 DDS entries. Both staged XBE hashes match the active
  manifest and the stale full-stage manifest is absent. Co-op remains deferred
  until one-view SP and Holomatch throughput are healthy; it is not removed
  from scope.

### v58 non-render frame-path audit

- The apparent early player control in the co-op crawl is closed by the matched
  zero-input capture above. The same in-map Munro shot appears while the crawl
  is still active with no smoke-input marker or synthetic command path staged;
  it is authored cinematic footage, not an input-gating failure.
- The outer-loop audit found no hidden sustained filesystem or handoff work.
  `IN_Frame` polls each connected Xbox controller once, then drains the UI
  queue and rumble state. `Sys_XboxExecuteMenuMap` returns after one boolean
  check on ordinary frames and performs synchronous map work only when a menu
  handoff is explicitly queued. Startup-map, command, mini-soak, and smoke
  marker files are cached once or polled only while their diagnostic harness is
  armed.
- `Com_EventLoop` retains bounded startup breadcrumbs and 128-packet safety
  caps, but its verbose trace budget expires after 24 calls. The normal
  hardware slowdown occurs after those paths are dormant. The shared server
  loop already limits catch-up to one simulation tick, and ordinary release
  frame profiling remains compiled out.
- Therefore input, menu handoff, harness file probes, startup event logging,
  and server catch-up are closed as independent explanations for sustained
  roughly five-FPS hardware gameplay. The staged v58 overlay remains unchanged;
  hardware `LP/FR/CL/FE/BE/DR/PR` ownership is the next authoritative split.

### v59 diagnostic self-interference correction

- `dllBeginEXT` previously read the timestamp counter for every immediate-mode
  draw whenever frame diagnostics were compiled, even though deep submission
  accounting is active for only one frame every five seconds. That made the
  diagnostic itself disproportionately expensive in HUD and cinematic-crawl
  workloads.
- The timestamp read is now guarded by `g_SPXBPerfSampleActive`, matching the
  indexed submission path. Production builds are unaffected.
- XEMU liveness and overlay-readability qualification passed for SP `borg6`
  and Holomatch `hm_dn1`:
  `scripts/output/framephase_v59_sp_borg6_borg6_20260818_024804.report.txt`
  and
  `scripts/output/framephase_v59_hm_hm_dn1_hm_dn1_20260818_025007.report.txt`.
- The exact PK3-only hardware stage is
  `Beta-20260818-framephase-v59`. Raw SHA256 values are
  `4B53D74A45057B7090EC0EBDF0412DB7821B6F5B568B2BA776003A761D8E96F7`
  (`default.xbe`) and
  `27220715FAC4DB434859920DF0098FD82E3CDB0A196237589FD770B0AC8C3001`
  (`efmp.xbe`); media-enabled staged hashes are
  `EC84E76E1B827933733710E1FC60B4C2792DCC40FBAB0E5E81DB23B4773DA108`
  and
  `BB0B390ADAD0583985C4A1062350FCD37116A74E6641878F20352DD17F8DAE46`.
- Hardware photographs in representative open SP and Holomatch gameplay are
  still the authority for the roughly 200 ms frame. XEMU host-speed FPS is not
  an acceptance measurement.

### v60 frame-ownership correction and expansion

- The diagnostic overlay displayed audio as zero because `CL_Frame` cleared
  the previous frame's audio bucket before `SCR_UpdateScreen` drew it. The
  clear is removed; audio now follows the same previous-completed-frame
  convention as the other displayed phases.
- Four diagnostic-only buckets now expose the remaining coarse shared-frame
  ownership boundaries: `Com_EventLoop`, command-buffer execution, the client
  preamble before the primary gameplay screen draw, and the client tail after
  that draw. Production builds remain unchanged.
- Sequential XDK 5558 builds produced raw SHA256
  `9CCAC6B0F92E41EE0548FE6A644B6CE4FF23E0859774E9FF2D2D663B1FE0CB91`
  for `default.xbe` and
  `976F9CC835D8A44A81F123DFCEBDB2D49FA35C9588A3A6F94DAB599D3F9B1477`
  for `efmp.xbe`. The HDD/media-enabled staged hashes are
  `CC2CFAAD3D55AC38C5CC3FE6DDBDB6B871953DEBCDAE91B8F569600F6E85D2E6`
  and
  `32A861CC36D1081CA51511F5235F6BCAB1B37E480C4793C7FD512036F8514C0A`.
- Seventy-second XEMU/LLE liveness and overlay-readability checks passed SP
  `borg6` and Holomatch `hm_dn1`, with five clean gameplay captures each:
  `scripts/output/framephase-v60-sp-borg6_borg6_20260818_031549.report.txt`
  and
  `scripts/output/framephase-v60-hm-hm_dn1_hm_dn1_20260818_031931.report.txt`.
  The adjacent contact sheets visually confirm intact gameplay and readable
  overlays. These are plumbing checks; retail hardware remains the FPS and
  phase-ownership authority.
- The one retained hardware folder is staged as
  `Beta-20260818-framephase-v60`. It supersedes v59.

### v61 active front-end phase instrumentation

- The detailed front-end fields had been initialized and reported but were not
  populated by the active `retail_xbox` renderer files. Diagnostic-only timing
  now covers view setup, leaf marking, world surfaces, dynamic polygons,
  projection, entity submission, sorting, and debug draw in those active files.
  It also records scene views, portals, draw-surface count, and submitted
  entities. Production builds are unchanged.
- Sequential XDK 5558 builds produced raw SHA256
  `B06FB3656B4586050E0B0CCD08C07EEB44702D2150CCAEA16143DC100F034C8B`
  for `default.xbe` and
  `BA7E0A540F3CF92210EF17B545B11802CA2D2AC7026BB1B0FDA9FAB5CD9AEC0F`
  for `efmp.xbe`. The HDD/media-enabled staged hashes are
  `E5FC43A255DD037DF4CD5C39F6867D9748ACBA19E614B0BFA9A1E8FDB2DF3CBA`
  and
  `59A83890AA6E3B805E81D348303D124CDAE427EFA74AB619145D4F108A887FCA`.
- Seventy-second XEMU/LLE checks passed with five clean captures each:
  `scripts/output/framephase-v61-sp-borg6_borg6_20260818_040706.report.txt`
  and
  `scripts/output/framephase-v61-hm-hm_dn1_hm_dn1_20260818_040950.report.txt`.
  SP reported one view, 359-362 draw surfaces, and 26 submitted entities.
  Holomatch reported one view, 751 draw surfaces, and 54 submitted entities.
  The XEMU front end remained approximately one millisecond; hardware phase
  photographs are still required because XEMU timing is not retail-hardware
  performance evidence.
- The single retained hardware stage is now
  `Beta-20260818-framephase-v61`; it supersedes v60.

### v62 push-buffer wait overlay

- The v61 Holomatch deep samples exposed intermittent `BeginPush` reservation
  stalls: total reservation time reached 21-22 million CPU cycles, the longest
  individual reservation crossed 10 ms, and state setup, vertex packing, index
  packing, submission, Finish, and Present did not own that interval. Matched SP
  samples did not show the same long reservation.
- Two diagnostic-only overlay rows now report draw-state, reservation, vertex
  packing, and index-packing milliseconds plus total `BeginPush` time, longest
  reservation, and counts above 1 ms and 10 ms. This makes the intermittent GPU
  queue-space wait visible on retail hardware without disk logging.
- Raw SHA256 values are
  `FF9A227CD8F0C6454BA7580620C50B7600A3166227768AB0478F661D199FAAC2`
  for `default.xbe` and
  `9A250C8774563F38569249C71CCB2FAF4AAB0E356176166B88C7A40E9BB456EE`
  for `efmp.xbe`. The media-enabled staged hashes are
  `A3080C84A0E7CF18EF8C30BE8E97D6DA4C382DF99E24039E5885A464DFED5729`
  and
  `A75A1A467FFA11F4E3199EF6E414EB955FD01553809CCDA0BB65E4BB760FD052`.
- A 50-second Holomatch XEMU visual proof kept gameplay live and showed all ten
  rows upright and readable:
  `scripts/output/framephase-v62-hm-queue_hm_dn1_20260818_043117.report.txt`.
  The sampled reservations in that shorter replay remained below the prior
  >10 ms event, confirming the stall is intermittent rather than constant.
- The one retained hardware stage is
  `Beta-20260818-framephase-v62`; it supersedes v61.

### v62 co-op camera ownership recheck

- The current v62 `default.xbe` repeated direct co-op for 145 wall seconds with
  the smoke-input path deliberately present. Its first synthetic command was
  scheduled for guest time 210000, beyond the run, so no harness movement,
  attack, or view command was eligible.
- The sampled camera flag remained `camera=1` throughout the crawl and every
  later in-map Munro shot. The close Munro footage that resembles player control
  is therefore still owned by the authored camera sequence.
- Evidence is
  `scripts/output/retail-v62-coop-input-ownership-recheck_normal_20260818_043821.report.txt`
  and the adjacent contact sheet. The generic visual checker reported failure
  only because a legitimate black transition frame crossed its 70-percent-black
  threshold; the XEMU process was alive at the deliberate diagnostic stop.

### v63 complete renderer ownership gaps

- The prior overlay exposed detailed renderer phases but did not show the
  already-collected whole-scene total or the portions of front-end and back-end
  time left outside the named buckets. Diagnostic-only row 11 now reports `RT`
  (whole `RE_RenderScene` time), `FG` (front end minus `RT`), `BO` (backend
  command types outside draw-surfaces and swap), and `BG` (backend time still
  outside draw-surfaces, swap, and `BO`). Production builds are unchanged.
- Sequential XDK 5558 builds produced raw SHA256
  `7642DA8C20A540A8205605EAFD41182E04B991942E735BDE90AACA9969D7971A`
  for `default.xbe` and
  `8CAB79765DD3C9E34B3F94FF89E00C5AAFEE95B479D4785D02A3222AD5B8FC4E`
  for `efmp.xbe`. The HDD/media-enabled staged hashes are
  `B864A6CCBED692F147EF8E4AAA7A1A306E6A161A984943DC2D3FFF368B95CF0A`
  and
  `162C4E13CBA9D792A817888D35762F803A72DBB7E8C678E069A6046640B29A27`.
- A 48-second Holomatch XEMU/LLE proof kept `hm_dn1` live and captured five
  coherent gameplay frames with all eleven rows upright and readable:
  `scripts/output/framephase-v63-hm-owner-gaps_hm_dn1_20260818_045030.report.txt`
  and the adjacent contact sheet. The matching 48-second SP `borg6` proof also
  captured five coherent gameplay frames and remained live:
  `scripts/output/framephase-v63-sp-owner-gaps_borg6_20260818_045555.report.txt`.
  These prove diagnostic plumbing only; XEMU guest and wall rates are not
  retail-hardware performance evidence.
- The one retained hardware stage is now
  `Beta-20260818-framephase-v63`; it supersedes v62. Hardware interpretation is
  direct: high `RT` keeps investigation inside scene traversal, high `FG`
  identifies front-end work outside the scene call, high `BO` identifies a
  non-draw backend command, and high `BG` exposes a remaining unmeasured backend
  interval.

### v64 deep-sample clock gating

- A complete audit of the compiled frame-diagnostic path found three remaining
  deep-sample clocks that still called `Sys_Milliseconds()` on every frame:
  backend render-command timing, `qglFinish`, and `qglEndFrame`/Present. Their
  measurements were accumulated only during a deep sample, but acquiring the
  clocks between samples still distorted the diagnostic build.
- `tr_backend_retail.cpp` and `tr_cmds_retail.cpp` now acquire those clocks only
  while `g_SPXBPerfSampleActive` is set. The coarse per-frame ownership clocks
  remain intentional because they feed the live hardware overlay. This changes
  diagnostic measurement overhead only; production rendering is unchanged.
- Sequential clean XDK 5558 builds produced raw SHA256
  `753A6DBAA39F334AF186BC2AEFBD5E9628DD2F94AFE5FCD1936CC71BAD6A5EA3`
  for `default.xbe` and
  `282401034AF236A30FF57154CB38B7B91EB73FBC6274091CF86B6636132FDD7B`
  for `efmp.xbe`. The HDD/media-enabled staged hashes are
  `22451DDFE82591A3102A201DEAF038C9F00C6C6657FCFEA290FA0DF21F6D9B0B`
  and
  `80444E61C15C2694FFDE86DB5D3F0B9FE81D28A60FF59F817E0B78E262C4275E`.
- Input-free XEMU/LLE smokes remained live through deliberate shutdown and
  retained coherent, lightmapped gameplay with upright readable overlays. SP
  proof is
  `scripts/output/framephase-v64-sp-sample-gate_borg6_20260818_051317.report.txt`;
  Holomatch proof is
  `scripts/output/framephase-v64-hm-sample-gate_hm_dn1_20260818_051551.report.txt`.
  Adjacent contact sheets provide the visual evidence. The sample counter
  advanced in both personalities, proving sampled and unsampled execution.
- The one retained hardware stage is now
  `Beta-20260818-framephase-v64`. Hardware photos from open representative SP
  and Holomatch views remain the authority for choosing the next bottleneck.

### v65 pulsed overlay-free snapshots

- The full eleven-row hardware overlay was itself a substantial renderer
  workload: every row emitted shadow and foreground glyph quads into every
  measured frame. That could inflate the screen/backend phases and obscure the
  original bottleneck.
- The tiny FPS line now remains continuous. The detailed rows are hidden for
  three seconds, then shown for two seconds. At the start of each pulse they
  freeze and format the immediately preceding overlay-free frame exactly once;
  the displayed phase values therefore exclude the detailed text they report.
  Sustained FPS is read during the FPS-only interval because drawing the rows
  can still depress the live rate while they are visible.
- Sequential XDK 5558 builds produced raw SHA256
  `1EB2329DED791BD52182A2F2346E8A79029485A3D0EBD9A556F33741674AF7F4`
  for `default.xbe` and
  `59DA782C93F771AF2EAF6958A927CCF7C7E915C7924CB3EB896B8B0FBDDE72F3`
  for `efmp.xbe`. The HDD/media-enabled staged hashes are
  `F042C8DE7C4BE6B65807A6B6CC1AED4FF37119CA989A5D7EF8358E6045BAC6FC`
  and
  `D85F4862AF13CB1D8C0A900EE5C526DB267B595B6E07D0FF9BEFB850BBBEB2B9`.
- Input-free XEMU/LLE qualifications remained live through deliberate shutdown
  with coherent gameplay and advancing sample counters. Their contact sheets
  visibly prove both the FPS-only and detailed-pulse phases:
  `scripts/output/framephase-v65-sp-pulsed-snapshot_borg6_20260818_053136.report.txt`
  and
  `scripts/output/framephase-v65-hm-pulsed-snapshot_hm_dn1_20260818_053418.report.txt`.
- The single retained hardware stage is now
  `Beta-20260818-framephase-v65`. The package checker confirms shared `code/`
  renderer/audio/input ownership, no `codemp` dependency, no loose MP map or UI
  overrides, no original image formats, and 5,405 DDS entries.

### v66 counter-only hardware diagnostics

- The XEMU monitor already polls the exported frame-phase globals directly.
  Diagnostic text was therefore redundant: every ten seconds `CL_Frame`
  performed an allocator walk and formatted two large records, while every
  deep sample formatted a 1,536-byte renderer profile for the memory ring.
- Frame-diagnostic builds now keep the counters, cadence, exported heartbeat,
  frozen overlay snapshots, and direct monitor polling while omitting those
  periodic text records. Non-diagnostic production behavior is unchanged.
- Sequential clean XDK 5558 builds produced raw SHA256
  `A0214CB5C16AFB1D89B9347BBCBEDA90D9E1A8EE60E2A83590821521157F6E3D`
  for `default.xbe` and
  `B43F8C6AD7D6D294DC361327CE9C8AAEA0D55F844749C9177BE5AC15668149A3`
  for `efmp.xbe`. The HDD/media-enabled staged hashes are
  `E2BD2C7BDDEE4E283873080E4E91962C166BE5F7668F32313E4AF1EFF079CAB4`
  and
  `26E60767D553A628164C7AB1C36F029291FC69C56350995587B778C2DC4377C2`.
- Input-free XEMU/LLE proofs remained live through deliberate shutdown,
  retained coherent lightmapped gameplay, visibly exercised both overlay
  phases, and advanced the directly polled sample serial without depending on
  formatted profile strings:
  `scripts/output/framephase-v66-sp-counter-only_borg6_20260818_055801.report.txt`
  and
  `scripts/output/framephase-v66-hm-counter-only_hm_dn1_20260818_060035.report.txt`.
  Adjacent contact sheets provide visual proof. XEMU timing is plumbing
  evidence only; retail hardware remains the FPS authority.
- Binary inspection of both raw and media-enabled v66 XBEs finds neither the
  `FRAME_HEARTBEAT` nor frame-profile format marker. The compiler eliminated
  the constant-false text paths completely; this is not a runtime branch-only
  reduction.
- The one retained hardware folder is staged as
  `Beta-20260818-framephase-v66`. Its architecture/package check passes with
  shared `code/` renderer/audio/input ownership, no `codemp` dependency, no
  loose MP map or UI overrides, no original image formats, and 5,405 DDS
  entries. It supersedes v65.

### v67 diagnostics-free production control

- Sequential XDK 5558 production rebuilds compiled out
  `STEFX_HW_FRAME_DIAGNOSTICS` for both personalities. Preserved raw binaries
  are `build/release/diagnostics/default-production-v67.xbe` and
  `build/release/diagnostics/efmp-production-v67.xbe`, with SHA256
  `DCE050D678ECCF4D8739F9A736B8BEFAE90B08A57FDE67D9DDDDAE0E3101BF5F`
  and `DAB61EDD954E03733C3B95B388C081526E1E7AD444C20B05262873F3D0810159`.
- A clean Holomatch rebuild exposed a PowerShell argument-boundary defect in
  the forced botlib compatibility include. `scripts/build_xbox.ps1` now emits
  `/FI` and its path as separate compiler arguments instead of passing literal
  quote characters to VC71. This is build plumbing only.
- Production SP `borg6` stayed alive and rendered coherent lightmapped
  gameplay throughout
  `scripts/output/production-v67-sp-matched_borg6_20260818_064747.report.txt`.
  Its adjacent contact sheet shows the small counter at approximately 88 FPS.
- Production Holomatch `hm_dn1` stayed alive and rendered coherently through
  `scripts/output/production-v67-hm-scanview_hm_dn1_20260818_070349.report.txt`.
  A continuous post-load scan produced a representative open-room capture at
  approximately 65 FPS. Static wall-facing captures read about 90 FPS and are
  excluded as performance evidence.
- The diagnostics-free control did not improve XEMU throughput over v66. That
  result rejects frame logging/overlay instrumentation as the leading
  explanation for the approximately 5 FPS retail-hardware result, but XEMU is
  not the final performance authority. The staged hardware payload remains v66
  so its pulsed phase snapshot can identify which subsystem owns the roughly
  200 ms hardware frame before another production candidate replaces it.

### v68 startup direct-map probe cache

- Normal frontend frames called `Sys_IsDirectMapBoot()` even after startup.
  With no direct-map marker present, that path reopened each candidate marker
  file on every call. Direct-map harness runs short-circuited through their
  explicit queued state, so this shared frontend cost was absent from several
  otherwise comparable direct-map measurements.
- The marker result is now probed once per process and cached. Explicit queued
  SP, co-op, and Holomatch requests still take precedence, and disconnect still
  clears the active direct-map state. Mid-process creation of a startup marker
  is intentionally unsupported; all runners stage markers before launch.
- A 60-second normal-boot XEMU/LLE proof remained on the intact main menu near
  90 FPS. A separate 35-second log-poll proof held at 87 ring writes with zero
  subsequent writes, confirming no recurring startup probe/log activity.
  Direct SP `borg6` passed at approximately 85.7-87.4 FPS, and direct
  Holomatch `hm_dn1` passed at approximately 90.5-91 FPS. Evidence:
  `scripts/output/directmap-cache-v68-normal_normal_20260818_080239.report.txt`,
  `scripts/output/directmap-cache-v68-logproof_normal_20260818_080434.report.txt`,
  `scripts/output/directmap-cache-v68-sp_borg6_20260818_080902.report.txt`, and
  `scripts/output/directmap-cache-v68-hm_hm_dn1_20260818_081307.report.txt`.
  Adjacent contact sheets visually verify the normal menu and both gameplay
  personalities.
- Preserved production binaries are
  `build/release/diagnostics/default-production-v68.xbe` and
  `build/release/diagnostics/efmp-production-v68.xbe`, with SHA256
  `320C83DDEA9A25D5E0D688204AC4E9F6DF86C6E0192538EE40207AEECD70934B`
  and `B5D2F59FFF2672B07513336F9307D334A91D808CBB02B2DAEF0F0E772DC45B92`.
  The one retained hardware folder remains v66 so its unbiased phase pulse can
  select the next bottleneck; v68 has no claimed hardware gain yet.

### Historical hardware ownership cross-check

- The retained historical Holomatch hardware log contains two settled frame
  heartbeats at 119 and 126 ms (8.2 and 7.4 FPS). Server work was only 3 ms in
  both samples. The renderer front end measured 49 and 48 ms, while the back
  end measured 67 and 70 ms. Audio was disabled. This confirms that the old
  single-view hardware slowdown was renderer-owned rather than bot, server,
  game, or audio work.
- The production draw submission body was compared again with the clean JA MP
  Xbox donor. With `STEFX_HW_FRAME_DIAGNOSTICS` compiled out, the indexed
  `BeginPush` packet path is unchanged. Device creation also matches: 640x480,
  one backbuffer, no multisampling, discard swap, hardware vertex processing,
  and the same depth format and presentation interval. Do not spend another
  trial on device-creation settings or rewrite the already-matching packet body
  without contradictory hardware evidence.
- A fresh shipping-XBE-versus-v67 runtime comparison found 134 common D3D/XG
  functions. The hot functions have matching byte lengths and control-flow
  shapes. `D3DDevice_BeginPush`, `D3DDevice_EndPush`, and
  `D3DDevice_MakeSpace` are instruction-for-instruction matches after address
  relocation. `CDevice::KickOff` retains the same control flow; its field
  offsets differ consistently with the known shipping QFE4 versus clean-SDK
  QFE1 private-device layout. The complete report is
  `build/analysis/retail-v67-d3d-runtime-compare.json`.
- The v66 hardware pulse remains the next discriminator. Its draw-reservation,
  back-end child, and unowned-gap rows can distinguish a push-buffer queue wait
  from state setup, surface drawing, swap/present, or an unmeasured boundary.

### Clean-donor object correction

- A first object comparison used the modified local Jedi Academy Xbox tree and
  incorrectly made `R_CullLocalBox` appear to be a large divergence. That tree
  contains later experimental work and is not a retail authority.
- Fresh SP and Holomatch comparisons use the untouched donor at
  `C:\Programming\Tools\xboxrecomp-work\ja-mp-retail\clean-ja-final-donor\objects`.
  `R_CullLocalBox` is 414 instructions on both sides. The remaining byte
  differences are relocated references, not a different culling algorithm.
- The hottest FakeGL/native-D3D bridge routines match exactly in both game
  personalities: `_updateTextures` (141 instructions), `_updateShader` (27),
  `_updateMatrices` (53), and `dllDrawElements` (448). The complete reports are
  `build/analysis/retail-v68-clean-donor-sp-object-compare.json` and
  `build/analysis/retail-v68-clean-donor-hm-object-compare.json`.
- `RB_EndSurface` differs by one instruction (103 versus 104), while the known
  personality-specific `RB_IterateStagesGeneric` bodies remain 386/406 for SP
  and 399/406 for Holomatch. These small, understood deltas do not support
  rewriting the already-matching draw packet path.
- A separate clean frame-shell comparison is recorded in
  `build/analysis/retail-v68-clean-donor-frame-sp-object-compare.json`.
  Current `SCR_UpdateScreen`, `Com_EventLoop`, `Com_Frame`,
  `Com_GetRealEvent`, and `Sys_GetEvent` are mostly shorter than their retail
  MP counterparts; instruction counts alone expose no plausible tenfold CPU
  burden in that shell.
- Therefore the next code change must be selected by the v66 retail-hardware
  pulse. `FE`/`RT` with `WO` or `EN` identifies front-end traversal/submission;
  `BE`/`DR` with high `DS`, `PK`, or `IX` identifies draw work; high `RQ`, `BP`,
  `MX`, `O1`, or `O10` identifies push-buffer reservation/queue pressure; high
  `SW`/`PR`/`FN` identifies presentation synchronization; and a large `FG` or
  `BG` identifies a still-unowned interval that must be instrumented before it
  is changed.

### Crawl harness ownership rule

- Co-op crawl footage may put Munro visibly inside the map before the authored
  camera sequence ends. Visual resemblance to normal third-person gameplay is
  not evidence that the harness obtained control.
- The v62 recheck scheduled its first possible injected command for guest time
  210000, beyond the 145-second run, and retained `camera=1` in every sampled
  in-map frame. No harness input occurred.
- Future crawl qualifications are input-free until an explicit camera-off
  marker is logged. Only then may a separate gameplay-control phase inject
  movement or view commands.

### Coarse XEMU execution-sample cross-check

- Input-free v68 SP `borg6` and Holomatch `hm_dn1` runs sampled EIP through
  XEMU's QEMU monitor after the maps had settled. The monitor completes only
  about one sample every 0.30 seconds and perturbs execution, so these runs are
  ownership cross-checks rather than hardware timing or optimization evidence.
- SP produced 116 retained samples after 20 seconds: 62.1% title code and
  37.9% kernel code. Its title samples were diffuse; no object exceeded 6.9%.
  The largest named renderer entries were `RB_SurfaceAnim` and
  `RB_SurfaceFace`, neither large enough to explain a tenfold slowdown.
- Holomatch produced 116 retained samples after 20 seconds: 77.6% title code
  and 22.4% kernel code. Its largest title-side groups were
  `win_qgl_dx8.obj` (10.3%), `stefx_mp_game_api.obj` (8.6%), and the D3D
  miniport interrupt object (8.6%). In the settled 40-second window,
  `dllDrawElements` was 6% and no title function exceeded 6%.
- The personalities therefore do not share a dominant game-side sampled hot
  function. This rejects using the crawl, smoke input, sound, or one sampled
  renderer routine as the common hardware-FPS diagnosis. The v66 hardware
  pulse remains the authority for choosing the next implementation boundary.
- Evidence:
  `scripts/output/v68-openview-eip_borg6_20260818_083850.report.txt` and
  `scripts/output/v68-hm-openview-eip_hm_dn1_20260818_084856.report.txt`.

### First-party D3D pusher contract cross-check

- The authorized Xbox archive contains Microsoft's D3D8 runtime source under
  `Z:\Programming\xbox\private\windows\directx\dxg\d3d8\se`. Its 2002
  timestamp means it is architectural evidence, not an exact substitute for
  shipping JA's 5558 QFE4 library.
- The source documents the intended asynchronous contract: every inserted
  fence must be followed by a kickoff, `MakeSpace` waits only when reclaiming
  an occupied push-buffer segment, and each kickoff flushes write-combined
  memory before publishing the hardware put pointer. Microsoft's performance
  test also identifies Quake plus a forced wait after every primitive as an
  easy way to expose write-combine synchronization trouble.
- STEFX does not deliberately force that pathological wait in ordinary world
  frames. The shared engine uses JA's 1 MiB/128 KiB push-buffer policy, and
  `r_finish 0` marks the frame as already synchronized so `RB_SwapBuffers`
  does not call the `qglFinish`/`BlockUntilIdle` path. The active indexed
  `BeginPush`/`EndPush` packet body remains instruction-exact with the clean JA
  donor.
- This source therefore does not justify replacing the Microsoft runtime or
  altering the draw packet speculatively. The retained v66 hardware pulse is
  still the deciding evidence: `RQ`/`BP`/`MX` select pusher-space pressure,
  `DR`/`PK`/`IX` select submission work, and `SW`/`PR`/`FN` select explicit
  end-of-frame synchronization.

### Original Xbox compiler-contract cross-check

- The preserved Xbox projects (`code/x_exe/x_exe.vcproj.old` and
  `code/x_exe/x_exe.vcproj.7.10.old`) compile Release and FinalBuild with
  `/Ox`, global optimization, full inlining, intrinsics, speed preference,
  frame-pointer omission, function-level linking, `/OPT:REF`, and `/OPT:ICF`.
  They do not enable `/GL` or `/LTCG`.
- The active XDK 5558 build already applies the runtime-relevant equivalents:
  `/Ox /Ob2 /Oi /Ot /G6 /arch:SSE /Oy /Gy` plus `/OPT:REF /OPT:ICF`.
  Therefore a missing whole-program-optimization switch is ruled out as the
  retail-hardware FPS multiplier; do not spend another candidate on it without
  new binary evidence.

### D3D QFE fence-runtime triangulation

- A read-only object comparison now covers clean 5558 QFE1 `pusher.obj` and
  `present.obj` against the later local Xbox D3D archive. The later archive is
  not linked or treated as an approved SDK; it is used only to identify the
  direction of Microsoft's private-runtime revisions. The report is
  `build/analysis/d3d-qfe-triangulation/qfe1-vs-later.json`.
- Shipping JA QFE4's 58-instruction `D3D_SetFence` core agrees with the later
  runtime on the encoded push address and the shifted fence/segment fields.
  Clean 5558 QFE1 uses the older 55-instruction encoding and older private
  `CDevice` offsets. The later object surrounds the same QFE4-shaped core with
  additional validation and bookkeeping, so it is not a byte-for-byte donor.
- This is a coordinated private-layout revision, not a safe isolated function
  replacement. `SetFence`, `KickOff`, push-segment state, and Present's swap
  throttling all consume that layout. Replacing one body while retaining the
  QFE1 object graph would write the wrong fields; linking the later archive
  would violate the clean-5558 toolchain requirement and mix runtime ABIs.
- Keep the official clean 5558 QFE1 runtime for now. Only reopen a complete
  QFE4-compatible runtime reconstruction if the v66 hardware rows show the
  missing frame time in `RQ`/`BP`/`MX` or `SW`/`PR`; front-end or packing time
  would not be explained by this QFE delta.

### Final XBE layout cross-check

- A read-only parse of the current production `default.xbe` and `efmp.xbe`
  compared their image headers, section tables, and library-version records
  with the preserved shipping JA MP metadata. The performance-sensitive
  layout contract matches: `.text` is preloaded, executable, and read-only;
  D3D/D3DX/XGRPH are writable, preloaded, and executable; `.rdata` is
  preloaded and read-only; `.data` is writable and preloaded; and both current
  images request the utility drive plus the 64 MiB retail memory limit.
- Both current personalities consistently link the clean 5558 QFE1 runtime.
  Shipping JA records QFE4, but that coordinated private-runtime delta is
  already isolated above and is not evidence of a bad imagebld or section
  mapping policy.
- Current virtual `.data` is larger than shipping JA MP: approximately
  12.14 MiB for SP and 10.27 MiB for Holomatch versus 5.47 MiB in JA MP. Map
  ownership shows that most of the difference is expected static game state
  and the existing 2.5 MiB temporary allocator plus 1.37 MiB bone pool. These
  are reserved BSS ranges rather than recurring frame copies, and runtime
  memory has remained stable. Do not shrink or relocate them as an FPS trial
  without a measured memory-pressure fault.
- This closes executable packaging, preload flags, and section permissions as
  the common frame-rate explanation. The retained v66 hardware pulse remains
  the authority for selecting queue reservation, draw work, or presentation
  synchronization as the next implementation boundary.

### Live texture-pool residency cross-check

- A production Holomatch `hm_dn1` run read the allocator objects directly from
  guest memory every two seconds for one minute. The static pool held at
  3,206,016 of 10,485,760 bytes; the skin pool held at 15,872 of 4,194,304
  bytes with seven registered textures.
- The skin-pool disk offset remained zero for every sample, proving that this
  representative run performed no pool eviction, disk write, or later fetch.
  Gameplay remained visually coherent at approximately 90 FPS in XEMU.
- Evidence is
  `scripts/output/retail-v74-texture-pool-live_20260818_125448.report.txt`, its
  adjacent contact sheet, and the final `texture-tail` memory dump.
- This independently agrees with the earlier v53 instrumented result and
  closes texture-pool thrashing as the shared hardware-FPS explanation. Do not
  enlarge the pools or alter their residency policy without contrary hardware
  evidence.

### Final application-to-D3D boundary audit

- The current `dllEndFrame` and the clean JA donor each compile to 24
  instructions. Their only disassembly difference is the expected four-byte
  shift of `glConfig.vidWidth` and `glConfig.vidHeight`; both execute the same
  viewport, `D3DDevice_Swap`, and blend-state restoration sequence. There is
  no extra Present, finish, idle wait, or frame replay in the application
  boundary.
- The remaining `RB_ExecuteRenderCommands` and `RB_RenderDrawSurfList`
  differences were classified against the donor objects. They are the Elite
  Force scissor command, required SP/MP structure offsets, and split-screen or
  force-alpha handling. They do not add a second world draw or an ordinary
  frame synchronization point.
- The clean 5558 `XDK\lib\d3d8-xbox.lib` is not a leaner replacement for
  `XDK\xbox\lib\d3d8.lib`. Its `pusher.obj` is 77,828 bytes versus 40,560
  bytes and expands `EndPush` from 3 to 56 instructions while enabling debug
  push bookkeeping. `present.obj` is also a different private-runtime build.
  It is not the missing QFE4 retail library and must not be linked as an FPS
  experiment.
- The authorized Microsoft tree contains an April 2002 `d3d8.lib` and D3D
  source, both older than clean 5558 and shipping JA QFE4. The other supplied
  retail-source directories contain only PC DirectX import libraries. No
  authentic 5558 QFE4 static library was found in the approved local sources.
- These results leave the current hardware phase counters as the deciding
  evidence. Refresh that diagnostic from the post-v73 source before the next
  Xbox run so the 16-byte shipping `msurface_t` stride correction is included.

### Post-v73 hardware diagnostic refresh (v75)

- Both personalities were rebuilt sequentially with the clean XDK 5558 and
  `STEFX_HW_FRAME_DIAGNOSTICS`, then checked in XEMU before staging. The SP
  process remained alive through the input-free `borg1` campaign crawl at
  approximately 83-90 FPS. Holomatch remained alive in `hm_borg1` gameplay at
  approximately 91-100 FPS.
- Evidence is
  `scripts/output/stefx-v75-sp-framephase_borg1_20260818_133138.report.txt` and
  `scripts/output/stefx-v75-hm-framephase_hm_borg1_20260818_133344.report.txt`,
  with adjacent contact sheets. No smoke input was enabled during the SP run,
  so the harness could not terminate or take ownership from the campaign
  camera.
- The one retained hardware folder now contains manifest version
  `Beta-20260818-framephase-v75`. Its HDD/media-enabled XBE SHA256 values are
  `4C8CC815619E2491CBCE6D9F0F62E1B79D703814E9F40999AFA91EA223AA6D99`
  for SP and
  `C90741D4F6765F933C2835FF90B315224EBB6D3F55F2A5CA28697D4CDEC1E974`
  for Holomatch. The PK3 hashes match their release sources exactly.
- Ordinary production XBEs were rebuilt after staging. Their SHA256 values are
  `877106A886213A7BD8CAE8F4EC2A0F72C87F6D56DDEE873BB2CFF6B5076768A6`
  for `default.xbe` and
  `1281519C4284624655403F5CE9CEFDAF41C92D80FB07E12FBB75B9E9DA529081`
  for `efmp.xbe`. The detailed `LP/IN/MM` overlay format exists in both staged
  diagnostic XBEs and neither production XBE, proving the two artifact classes
  were separated correctly.

### Retail D3D control identity recheck

- The authoritative retail MP control remains the 4,988,928-byte XBE with
  SHA256 `74003C42786C021438C1F27833839F599DCE23E22F086E971C84038C504E7FB3`.
  It is retained both at the authority path above and under the archived
  `build/xemu/retail_jamp_control_stage/default.xbe` path.
- The loose 5,652,480-byte `C:\Games\Emulators\CXBX\Jedi Academy rebuild\jamp.xbe`
  is a different rebuilt binary and does not match the retail symbol cache.
  It must not be used with that cache for machine-code comparisons.
- A fresh comparison against the correctly paired retail XBE confirms that
  texture binding, swap, vertical-blank wait, push-space reservation, and
  kickoff retain the same instruction counts and control-flow contracts as the
  clean 5558 runtime. Their differences are private QFE field offsets and
  symbol relocation. The one substantive `D3D_SetFence` delta is the already
  documented coordinated QFE1/QFE4 private-layout revision, not an isolated
  application-side optimization candidate.

### Post-v73 scene-workload classification

- Direct counter polling on open `borg1` and `hm_dn1` views rules out runaway
  scene submission. SP submitted 66-69 calls for 169 draw surfaces, while
  Holomatch submitted 161-180 calls for 280-487 draw surfaces. Both remained
  live near the configured guest-frame cap in XEMU/LLE.
- SP's campaign crawl used 48 immediate-mode submissions per sampled frame;
  Holomatch used indexed submissions exclusively in the sampled gameplay
  frames. This makes the immediate path relevant to the slow crawl and 2D
  defects, but it cannot explain Holomatch's shared hardware slowdown by
  itself.
- Long cycle intervals observed while polling are debugger artifacts: the QEMU
  monitor pauses the guest while a deep sample can be active, charging that
  host pause to whichever renderer phase was interrupted. They are not valid
  hardware queue-wait measurements. Draw counts and submission classes remain
  valid; retail-hardware v75 pulse values are required for phase timing.
- Evidence is
  `scripts/output/retail-v76-sp-workload_borg1_20260818_140553.report.txt`,
  `scripts/output/retail-v76-hm-workload_hm_dn1_20260818_140315.report.txt`,
  `scripts/output/retail-v76-sp-drawphase2_borg1_20260818_141252.report.txt`,
  and
  `scripts/output/retail-v76-hm-drawphase2_hm_dn1_20260818_141448.report.txt`.
