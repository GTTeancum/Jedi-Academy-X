/*
 * xb_log.h  —  Jedi Academy Xbox debug logging
 *
 * Usage:
 *   XBL("Win_Init: device ready\n");
 *   XBLF("renderer: %d textures loaded\n", count);
 *
 * Output goes to:
 *   - OutputDebugStringA  (visible in CXBX-Reloaded / xemu GDB)
 *   - \Device\Harddisk0\Partition1\ja_sp_log.txt  (E:\ root on retail hardware)
 *
 * Call XBLog_Init() once at the top of main() before any XBL usage.
 * Call XBLog_Shutdown() on exit.
 */

#ifndef XB_LOG_H
#define XB_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

void XBLog_PreCRTProbe(void);  /* called from ASM before _mainCRTStartup */
void XBLog_PostCRTProbe(void); /* called from ASM after  _mainCRTStartup returns */
void XBLog_Init(void);
void XBLog_Shutdown(void);
void XBLog_Print(const char *msg);
void XBLog_Printf(const char *fmt, ...);
const char *XBLog_GetPath(void);

/* Convenience macros — VC71 C89 mode doesn't support __VA_ARGS__,
   so XBLF is a direct function alias rather than a variadic macro. */
#define XBL(msg)  XBLog_Print(msg)
#define XBLF      XBLog_Printf

/* ── Backward-compat shims for existing call sites ─────────────────────
   XBLog_Write/XBLog_Writef auto-append \n so old callers don't need to
   change.  New code should use XBL("msg\n") / XBLF("fmt\n", ...).     */
void XBLog_Write(const char *msg);
void XBLog_Writef(const char *fmt, ...);

#ifndef SP_XBOX_HOT_TELEMETRY
#define SP_XBOX_HOT_TELEMETRY 0
#endif

#ifndef SP_XBOX_VERBOSE_RUNTIME_LOGS
#define SP_XBOX_VERBOSE_RUNTIME_LOGS 0
#endif

#ifndef SP_XBOX_SMOKE_AUTOMATION
#define SP_XBOX_SMOKE_AUTOMATION 0
#endif

#ifndef SP_XBOX_RENDER_TIMERS
#define SP_XBOX_RENDER_TIMERS SP_XBOX_VERBOSE_RUNTIME_LOGS
#endif

#ifndef SP_XBOX_END_FRAME_YIELD
#define SP_XBOX_END_FRAME_YIELD 0
#endif

#if SP_XBOX_HOT_TELEMETRY
#define SPXB_HOT_INC(counter)      do { ++(counter); } while (0)
#define SPXB_HOT_ADD(counter, val) do { (counter) += (val); } while (0)
#define SPXB_HOT_SET(counter, val) do { (counter) = (val); } while (0)
#else
#define SPXB_HOT_INC(counter)      do { } while (0)
#define SPXB_HOT_ADD(counter, val) do { } while (0)
#define SPXB_HOT_SET(counter, val) do { } while (0)
#endif

#ifndef XB_LOG_IMPLEMENTATION
extern volatile unsigned int g_SPXBKeyCatchers;
extern volatile unsigned int g_SPXBKeyLastKey;
extern volatile unsigned int g_SPXBKeyLastDown;
extern volatile unsigned int g_SPXBKeyLastPhaseHash;
extern volatile unsigned int g_SPXBKeyTraceCount;
extern volatile unsigned int g_SPXBSmokeButtonCount;
extern volatile unsigned int g_SPXBSmokeButtonPressCount;
extern volatile unsigned int g_SPXBSmokeButtonReleaseCount;
extern volatile unsigned int g_SPXBSmokeButtonUiStartMs;
extern volatile unsigned int g_SPXBSmokeButtonLast;
extern volatile unsigned int g_SPXBUISetActiveCount;
extern volatile unsigned int g_SPXBUIActiveMenuHash;
extern volatile unsigned int g_SPXBUIActiveResult;
extern volatile unsigned int g_SPXBUIMainMenuCount;
extern volatile unsigned int g_SPXBUIKeyEventCount;
extern volatile unsigned int g_SPXBUIKeyLast;
extern volatile unsigned int g_SPXBFrontEndPhase;
extern volatile unsigned int g_SPXBFrontEndMenuHash;
extern volatile unsigned int g_SPXBFrontEndItemHash;
extern volatile unsigned int g_SPXBFrontEndScriptHash;
extern volatile unsigned int g_SPXBFrontEndPopup;
extern volatile unsigned int g_SPXBFrontEndResponse;
extern volatile unsigned int g_SPXBFrontEndController;
extern volatile unsigned int g_SPXBSkyIterCalls;
extern volatile unsigned int g_SPXBSkyPortalMainFallbacks;
extern volatile unsigned int g_SPXBSkyClipCalls;
extern volatile unsigned int g_SPXBSkyBoxDrawCalls;
extern volatile unsigned int g_SPXBSkyBoxSidesDrawn;
extern volatile unsigned int g_SPXBSkyNoOuterBox;
extern volatile unsigned int g_SPXBSkyCloudBuilds;
extern volatile unsigned int g_SPXBSkyGenericCalls;
extern volatile unsigned int g_SPXBWorldSurfaceAddCalls;
extern volatile unsigned int g_SPXBWorldSkySurfaceAdds;
extern volatile unsigned int g_SPXBWorldPortalSurfaceAdds;
extern volatile unsigned int g_SPXBDrawSurfTotalAdds;
extern volatile unsigned int g_SPXBDrawSurfSkyAdds;
extern volatile unsigned int g_SPXBDrawSurfPortalAdds;
extern volatile unsigned int g_SPXBDrawSurfForceSightSkips;
extern volatile unsigned int g_SPXBCGameRenderCalls;
extern volatile unsigned int g_SPXBCGameDrawFrameReturns;
extern volatile unsigned int g_SPXBRenderSceneCalls;
extern volatile unsigned int g_SPXBRenderSceneNoWorld;
extern volatile unsigned int g_SPXBRenderViewCalls;
extern volatile unsigned int g_SPXBRenderViewWorldCalls;
extern volatile unsigned int g_SPXBScreenDrawCalls;
extern volatile unsigned int g_SPXBScreenForceDirectCalls;
extern volatile unsigned int g_SPXBScreenFullscreenSkips;
extern volatile unsigned int g_SPXBScreenCinematicDraws;
extern volatile unsigned int g_SPXBScreenCGameCalls;
extern volatile unsigned int g_SPXBScreenUIRefreshes;
extern volatile unsigned int g_SPXBScreenDirectReturns;
extern volatile unsigned int g_SPXBCLFrameEnterCalls;
extern volatile unsigned int g_SPXBCLFrameDirectReturns;
extern volatile unsigned int g_SPXBCLFrameBeforeScreen;
extern volatile unsigned int g_SPXBCLFrameAfterScreen;
extern volatile unsigned int g_SPXBCLFrameCompleted;
extern volatile unsigned int g_SPXBRenderRegistrationState;
extern volatile unsigned int g_SPXBRenderStretchPicCalls;
extern volatile unsigned int g_SPXBRenderStretchPicCmdNull;
extern volatile unsigned int g_SPXBRenderBeginFrameCalls;
extern volatile unsigned int g_SPXBRenderBeginFrameUnregistered;
extern volatile unsigned int g_SPXBRenderEndFrameCalls;
extern volatile unsigned int g_SPXBRenderEndFrameUnregistered;
extern volatile unsigned int g_SPXBRenderEndFrameCmdNull;
extern volatile unsigned int g_SPXBRenderEndFrameBeginFail;
extern volatile unsigned int g_SPXBRenderIssueCalls;
extern volatile unsigned int g_SPXBRenderIssueCmdUsed;
extern volatile unsigned int g_SPXBCompatBeginFrameCalls;
extern volatile unsigned int g_SPXBCompatEndFrameCalls;
extern volatile unsigned int g_SPXBFakeSwapBuffersCalls;
extern volatile unsigned int g_SPXBDx8BeginFrameCalls;
extern volatile unsigned int g_SPXBDx8EndFrameCalls;
extern volatile unsigned int g_SPXBDx8PresentCalls;
extern volatile unsigned int g_SPXBDx8PresentHr;
extern volatile unsigned int g_SPXBDx8FramebufferUpdates;
extern volatile unsigned int g_SPXBDx8FramebufferBackBufferFail;
#endif

#ifdef __cplusplus
}
#endif

#endif /* XB_LOG_H */
