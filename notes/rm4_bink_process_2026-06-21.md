# RM4/Jade Xbox Bink Process Notes - 2026-06-21

Sources read end to end:
- `Z:\Programming\RM4+JadeSrc\Libraries\GX8\bink\Player.cpp` (913 lines)
- `Z:\Programming\RM4+JadeSrc\Libraries\GX8\bink\player.h` (16 lines)
- `Z:\Programming\RM4+JadeSrc\Libraries\GX8\bink\xb xrad3d.cpp` equivalent path: `xbxrad3d.cpp` (613 lines)
- `Z:\Programming\RM4+JadeSrc\Libraries\GX8\bink\rad3d.h` (139 lines)
- `Z:\Programming\RM4+JadeSrc\Libraries\GX8\bink\bink.h` (726 lines)
- Caller excerpt from `Z:\Programming\RM4+JadeSrc\Libraries\GX8\GX8init.c` around the attract/intro Bink call.

## Full-screen Bink path

The synchronous/full-screen path is `BinkPlayer(VideoIds)`, not `MoviePlayerThreadBink`.

Call wrapper:
- `AI_VideoLauncher(VideoIds)` calls `Mem_CreateForBink`, mutes/pauses game sound, calls `BinkPlayer`, restores Bink memory, unmutes/unpauses sound.
- `GX8init.c` attract/intro path does the same manually: mute, pause tracks, `Mem_CreateForBink`, `BinkPlayer`, `Mem_RestoreForBink`, unmute.

Open:
- `BinkSetSoundTrack(1, &TID)` selects the localized or default audio track before open.
- Normal path calls `BINK_SUBOpen(Globalfilename)`.
- Non-5.1 path opens with `BinkOpen(Globalfilename, BINKSNDTRACK | BINKIOSIZE)` and loops on disc error until it succeeds.
- A narrowed source search found no `BinkSetIOSize` call in RM4's GX8 Bink path; RAD docs say `BINKIOSIZE` is meant to be paired with a previous `BinkSetIOSize` call.
- 5.1 path opens with `BINKSNDTRACK`, then assigns mixbins per track.

Setup:
- `D3D->SetViewport(640x480)`.
- Clear and present once before playback.
- `BinkUnloadConverter(BINKCONVERTERSALL)`, then `BinkLoadConverter(BINKSURFACEYUY2)`.
- Create two D3D textures with `CreateTexture(Bink->Width, Bink->Height, 1, 0, D3DFMT_YUY2, 0, ...)`.
- Enable overlay before getting surface levels.
- Get surface level 0 from both textures.

Per-frame:
- Loop blocks inside `BinkPlayer`; it is not distributed across the game render loop.
- If `!BinkWait(Bink)` or fast-play is set, fade volume up with `BinkSetVolume(Bink, Piste, VolumeFade)`.
- Decompress into the alternate texture:
  - `BinkDoFrame(Bink)`
  - `texture->LockRect(0, &lock_rect, 0, 0)`
  - `BinkCopyToBuffer(Bink, lock_rect.pBits, lock_rect.Pitch, Bink->Height, 0, 0, flags)`
  - `BINK_DrawSUB(...)`
  - unlock
- If the copy was not skipped, toggle current image and call `UpdateOverlay(surface, src, dst, FALSE, 0)`.
- Break when `Bink->FrameNum == Bink->Frames` and not looping.
- Then call `BinkNextFrame(Bink)`.
- If `Bink->ReadError`, close, show file error, reopen, enable overlay, and `BinkGoto` the saved frame.
- Skip input is polled in the same loop; for normal videos it is only honored after 3 seconds.

Teardown:
- `BlockUntilVerticalBlank()`.
- `EnableOverlay(FALSE)`.
- Release both overlay surfaces.
- Release both textures.
- `BinkClose(Bink)`.
- `BINK_SUBClose()`.

## Texture/world-load path

The threaded `MoviePlayerThreadBink` path is only for video-in-texture during world load:
- `Gx8_BeginWorldLoad` searches OVR/OVR2/default BIK files, loads the full BIK into memory, opens with `BINKFROMMEMORY`, creates one RAD3D YUY2 texture image, mutes game sound, starts the Bink thread.
- The thread uses `Decompress_frameTex`, `Show_frameTex`, and `Blit_RAD_3D_image`, not D3D overlay.
- `Blit_RAD_3D_image` sets `D3DRS_YUVENABLE` according to `RAD3DSURFACEYUY2`.
- `Gx8_EndWorldLoad` signals the thread via `GLOB`, waits for it to exit, closes/free Bink memory and RAD image, restores sound and render-target scratch resources.

## Current JA comparison

Already matching:
- `code/client/bink.h` matches the RM4 Bink 1.7b API shape, including `BinkSetVolume(HBINK, trackid, volume)`.
- `BinkVideo::Start` selects track 0 before open, loads the YUY2 converter, opens without alpha for full-screen, and creates two `D3DFMT_YUY2` textures through D3D.
- `BinkVideo::DecompressFrame` uses `LockRect` pitch and `BINKSURFACEYUY2 | BINKCOPYALL`.

Mismatches to address first:
- JA full-screen playback is split across `CIN_RunCinematic`, `SCR_UpdateScreen`, and `SCR_DrawCinematic`; RM4 full-screen playback is one blocking loop that owns wait/decompress/draw/advance/stop.
- JA `CIN_RunCinematic` currently returns `FMV_PLAY` based only on `bVideo.GetStatus()`, while `bVideo.Run()` may call `Stop()` during `SCR_DrawCinematic` later in the same loop. This can leave the outer loop doing another screen/input/event pass after EOF.
- JA `Stop()` was closing Bink before overlay teardown and clearing/presenting while overlay was still enabled. It has now been reordered toward RM4, but this needs build/test after the full lifecycle patch.
- JA does not call `BINK_SUBOpen`/`BINK_SUBClose`; Jedi Academy BIKs may not need RM4 subtitles, so do not add this unless a source or symbol proves JA uses it.
- JA does not pass `BINKIOSIZE`; RM4 does, but without a visible `BinkSetIOSize` call in the GX8 Bink path. Keep this unchanged until retail JA or runtime logs prove open/read buffering is the failure.

Next action:
- Make `CIN_PlayAllFrames` drive `bVideo.Run()` directly in the blocking loop, immediately after `CIN_RunCinematic(Handle)` reports/play-starts, so full-screen Bink follows the RM4 ownership model instead of depending on `SCR_UpdateScreen` to call back into `SCR_DrawCinematic`. Done in current working tree.
- Keep logs around `CIN_RunCinematic`, direct `bVideo.Run`, and stop so hangs point to the exact phase.

## Retail JA SP check

- Retail `default.xbe` anchors `BinkVideo::Start` through the `"logos"` string xref at `0x00011450`.
- That retail function calls `RADSetMemory` at `0x0001148B`, then pushes `0x00100000` and the filename before calling the Bink open routine at `0x0029B050`.
- `0x00100000` is `BINKALPHA`. No `BINKSNDTRACK`, `BINKIOSIZE`, `BinkSetSoundTrack`, or Bink converter load/unload call appears in this retail SP start path.
- Conclusion for JA SP: keep RM4/MP sound-track setup out of SP unless later retail RE proves a separate path needs it.
