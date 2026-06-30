/*
 * xb_log.cpp  —  Jedi Academy Xbox debug logging
 *
 * Strategy 1: NtCreateFile to raw HDD device paths.
 *   Works on retail hardware and CXBX-R. Bypasses drive-letter symlinks.
 *   \Device\Harddisk0\Partition1\ = E:\ on a standard retail Xbox.
 *
 * Strategy 2: CreateFileA with drive letters.
 *   Fallback for environments where drive letters are already mapped
 *   (dashboard, devkit).
 *
 * Do NOT use XeImageFileName — causes KeBugCheck on CXBX-R.
 */

#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define XB_LOG_IMPLEMENTATION
#include "xb_log.h"

/* ── NT kernel types (minimal subset) ── */
typedef struct { unsigned short Length; unsigned short MaximumLength; char *Buffer; } XBL_STR;
typedef struct { HANDLE RootDirectory; XBL_STR *ObjectName; unsigned long Attributes; } XBL_OA;
typedef struct { union { long Status; void *Pointer; }; unsigned long Information; } XBL_IOSB;

extern "C" long __stdcall NtCreateFile(HANDLE*, unsigned long, XBL_OA*, XBL_IOSB*,
    LARGE_INTEGER*, unsigned long, unsigned long, unsigned long, unsigned long);
extern "C" long __stdcall NtClose(HANDLE);
extern "C" long __stdcall NtWriteFile(HANDLE, HANDLE, void*, void*, XBL_IOSB*,
    void*, unsigned long, LARGE_INTEGER*);
extern "C" long __stdcall NtFlushBuffersFile(HANDLE, XBL_IOSB*);

/* ── State ── */
#define XBL_BUF_SIZE 512
static HANDLE g_hLogFile     = INVALID_HANDLE_VALUE;
static int    g_logIsNt      = 0;   /* 1 = NtCreateFile handle, 0 = CreateFileA */
static const char *g_logPath = NULL;
static HANDLE g_hMirrorLogFile = INVALID_HANDLE_VALUE;
static const char *g_mirrorLogPath = NULL;
static int g_verboseLog = 0;

#define XBL_DISK_LOG_SOFT_LIMIT (4u * 1024u * 1024u)
#define XBL_DISK_LOG_HARD_LIMIT (5u * 1024u * 1024u)
static unsigned int g_logDiskBytes = 0;
static unsigned int g_mirrorLogDiskBytes = 0;
static int g_logDiskLimitAnnounced = 0;
static int g_mirrorLogDiskLimitAnnounced = 0;

extern "C" {
__declspec(dllexport) volatile unsigned int g_SPXBLogMagic = 0x53504A41; /* 'SPJA' */
__declspec(dllexport) volatile unsigned int g_SPXBBootPhase = 0x11110001;
__declspec(dllexport) volatile unsigned int g_SPXBLogMirrorPos = 0x11110002;
__declspec(dllexport) volatile unsigned int g_SPXBLogWriteCount = 0x11110003;
__declspec(dllexport) volatile unsigned int g_SPXBHeartbeatMagic = 0x48424653; /* 'SFBH' */
__declspec(dllexport) volatile unsigned int g_SPXBHeartbeatCount = 0x11110004;
__declspec(dllexport) volatile unsigned int g_SPXBHeartbeatFrame = 0x11110005;
__declspec(dllexport) volatile unsigned int g_SPXBHeartbeatRealtime = 0x11110006;
__declspec(dllexport) volatile unsigned int g_SPXBHeartbeatServerTime = 0x11110007;
__declspec(dllexport) volatile unsigned int g_SPXBHeartbeatFps10 = 0x11110008;
__declspec(dllexport) volatile unsigned int g_SPXBPhaseMagic = 0x50485350; /* 'PSHP' */
__declspec(dllexport) volatile unsigned int g_SPXBMainLoopCount = 0x11110009;
__declspec(dllexport) volatile unsigned int g_SPXBComFrameCount = 0x1111000A;
__declspec(dllexport) volatile unsigned int g_SPXBSvFrameCount = 0x1111000B;
__declspec(dllexport) volatile unsigned int g_SPXBClFrameCount = 0x1111000C;
__declspec(dllexport) volatile unsigned int g_SPXBClsState = 0x1111000D;
__declspec(dllexport) volatile unsigned int g_SPXBClServerTime = 0x1111000E;
__declspec(dllexport) volatile unsigned int g_SPXBClsFrameCount = 0x1111000F;
__declspec(dllexport) volatile unsigned int g_SPXBPhaseLast = 0x11110010;
__declspec(dllexport) volatile unsigned int g_SPXBComSubphase = 0x11110011;
__declspec(dllexport) volatile unsigned int g_SPXBComSpinCount = 0x11110012;
__declspec(dllexport) volatile unsigned int g_SPXBComMsec = 0x11110013;
__declspec(dllexport) volatile unsigned int g_SPXBComFrameTime = 0x11110014;
__declspec(dllexport) volatile unsigned int g_SPXBComLastTime = 0x11110015;
__declspec(dllexport) volatile unsigned int g_SPXBCbufExecCount = 0x11110016;
__declspec(dllexport) volatile unsigned int g_SPXBCmdExecCount = 0x11110017;
__declspec(dllexport) volatile unsigned int g_SPXBCmdPhase = 0x11110018;
__declspec(dllexport) volatile unsigned int g_SPXBCmdHash = 0x11110019;
__declspec(dllexport) volatile unsigned int g_SPXBCmdArgc = 0x1111001A;
__declspec(dllexport) volatile unsigned int g_SPXBCmdLoopIndex = 0x1111001B;
__declspec(dllexport) volatile unsigned int g_SPXBCmdLoopNameHash = 0x1111001C;
__declspec(dllexport) volatile unsigned int g_SPXBCmdFunctionPtr = 0x1111001D;
__declspec(dllexport) volatile unsigned int g_SPXBCmdArgv0First4 = 0x1111001E;
__declspec(dllexport) volatile unsigned int g_SPXBCmdNameFirst4 = 0x1111001F;
__declspec(dllexport) volatile unsigned int g_SPXBMapPhase = 0x1111001A;
__declspec(dllexport) volatile unsigned int g_SPXBMapHash = 0x1111001B;
__declspec(dllexport) volatile unsigned int g_SPXBDirectMapState = 0x1111001C;
__declspec(dllexport) volatile unsigned int g_SPXBDirectMapLastError = 0x1111001D;
__declspec(dllexport) volatile unsigned int g_SPXBDirectMapPathIndex = 0x1111001E;
__declspec(dllexport) volatile unsigned int g_SPXBDirectMapFirst4 = 0x1111001F;
__declspec(dllexport) volatile unsigned int g_SPXBSvMapState = 0x11110020;
__declspec(dllexport) volatile unsigned int g_SPXBClHunkState = 0x11110021;
__declspec(dllexport) volatile unsigned int g_SPXBComErrorCode = 0x11110022;
__declspec(dllexport) volatile unsigned int g_SPXBComErrorHash = 0x11110023;
__declspec(dllexport) volatile unsigned int g_SPXBComErrorFirst4 = 0x11110024;
__declspec(dllexport) volatile unsigned int g_SPXBComErrorNext4 = 0x11110025;
__declspec(dllexport) volatile unsigned int g_SPXBClHunkCaller = 0x11110026;
__declspec(dllexport) volatile unsigned int g_SPXBClHunkCallCount = 0x11110027;
__declspec(dllexport) volatile unsigned int g_SPXBCmLoadState = 0x11110028;
__declspec(dllexport) volatile unsigned int g_SPXBCmLoadLumpHash = 0x11110029;
__declspec(dllexport) volatile unsigned int g_SPXBCmLoadLumpLen = 0x1111002A;
__declspec(dllexport) volatile unsigned int g_SPXBGamePhase = 0x1111001C;
__declspec(dllexport) volatile unsigned int g_SPXBGameEntityCount = 0x1111001D;
__declspec(dllexport) volatile unsigned int g_SPXBGameClassHash = 0x1111001E;
__declspec(dllexport) volatile unsigned int g_SPXBGentitiesPtr = 0x1111001F;
__declspec(dllexport) volatile unsigned int g_SPXBClientsPtr = 0x11110020;
__declspec(dllexport) volatile unsigned int g_SPXBGentitySize = 0x11110021;
__declspec(dllexport) volatile unsigned int g_SPXBClientFieldBefore = 0x11110022;
__declspec(dllexport) volatile unsigned int g_SPXBClientFieldAfter = 0x11110023;
__declspec(dllexport) volatile unsigned int g_SPXBRenderDrawSurfLists = 0x11110024;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSurfaces = 0x11110025;
__declspec(dllexport) volatile unsigned int g_SPXBRenderEndSurfaces = 0x11110026;
__declspec(dllexport) volatile unsigned int g_SPXBRenderBackendMsec = 0x11110027;
__declspec(dllexport) volatile unsigned int g_SPXBFakeGLPrimitiveCalls = 0x11110028;
__declspec(dllexport) volatile unsigned int g_SPXBFakeGLPrimitiveVerts = 0x11110029;
__declspec(dllexport) volatile unsigned int g_SPXBFakeGLStateFlushes = 0x1111002A;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSplitShader = 0x1111002B;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSplitFog = 0x1111002C;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSplitDlight = 0x1111002D;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSplitEntity = 0x1111002E;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSplitFinal = 0x1111002F;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSplitFlush = 0x11110030;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferData = 0x11110031;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferPitch = 0x11110032;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferWidth = 0x11110033;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferHeight = 0x11110034;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferFormat = 0x11110035;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferSize = 0x11110036;
__declspec(dllexport) volatile unsigned int g_SPXBCinPhase = 0x11110031;
__declspec(dllexport) volatile unsigned int g_SPXBCinHandle = 0x11110032;
__declspec(dllexport) volatile unsigned int g_SPXBCinStatus = 0x11110033;
__declspec(dllexport) volatile unsigned int g_SPXBCinLoopCount = 0x11110034;
__declspec(dllexport) volatile unsigned int g_SPXBBinkPhase = 0x11110100;
__declspec(dllexport) volatile unsigned int g_SPXBBinkRunCount = 0x11110101;
__declspec(dllexport) volatile unsigned int g_SPXBBinkWaitLoops = 0x11110102;
__declspec(dllexport) volatile unsigned int g_SPXBBinkWaitBreaks = 0x11110103;
__declspec(dllexport) volatile unsigned int g_SPXBBinkFrameNum = 0x11110104;
__declspec(dllexport) volatile unsigned int g_SPXBBinkFrames = 0x11110105;
__declspec(dllexport) volatile unsigned int g_SPXBBinkOpenFlags = 0x11110106;
__declspec(dllexport) volatile unsigned int g_SPXBBinkWidth = 0x11110107;
__declspec(dllexport) volatile unsigned int g_SPXBBinkHeight = 0x11110108;
__declspec(dllexport) volatile unsigned int g_SPXBBinkAlpha = 0x11110109;
__declspec(dllexport) volatile unsigned int g_SPXBBinkCopySkipped = 0x1111010A;
__declspec(dllexport) volatile unsigned int g_SPXBBinkStartResult = 0x1111010B;
__declspec(dllexport) volatile unsigned int g_SPXBBinkStatus = 0x1111010C;
__declspec(dllexport) volatile unsigned int g_SPXBSurfaceTypeCounts[16] = {
    0x11110040, 0x11110041, 0x11110042, 0x11110043,
    0x11110044, 0x11110045, 0x11110046, 0x11110047,
    0x11110048, 0x11110049, 0x1111004A, 0x1111004B,
    0x1111004C, 0x1111004D, 0x1111004E, 0x1111004F
};
__declspec(dllexport) volatile unsigned int g_SPXBEntityTypeCounts[16] = {
    0x11110050, 0x11110051, 0x11110052, 0x11110053,
    0x11110054, 0x11110055, 0x11110056, 0x11110057,
    0x11110058, 0x11110059, 0x1111005A, 0x1111005B,
    0x1111005C, 0x1111005D, 0x1111005E, 0x1111005F
};
__declspec(dllexport) volatile unsigned int g_SPXBKeyCatchers = 0x11110060;
__declspec(dllexport) volatile unsigned int g_SPXBKeyLastKey = 0x11110061;
__declspec(dllexport) volatile unsigned int g_SPXBKeyLastDown = 0x11110062;
__declspec(dllexport) volatile unsigned int g_SPXBKeyLastPhaseHash = 0x11110063;
__declspec(dllexport) volatile unsigned int g_SPXBKeyTraceCount = 0x11110064;
__declspec(dllexport) volatile unsigned int g_SPXBSmokeButtonCount = 0x11110065;
__declspec(dllexport) volatile unsigned int g_SPXBSmokeButtonPressCount = 0x11110066;
__declspec(dllexport) volatile unsigned int g_SPXBSmokeButtonReleaseCount = 0x11110067;
__declspec(dllexport) volatile unsigned int g_SPXBSmokeButtonUiStartMs = 0x11110068;
__declspec(dllexport) volatile unsigned int g_SPXBSmokeButtonLast = 0x11110069;
__declspec(dllexport) volatile unsigned int g_SPXBUISetActiveCount = 0x1111006A;
__declspec(dllexport) volatile unsigned int g_SPXBUIActiveMenuHash = 0x1111006B;
__declspec(dllexport) volatile unsigned int g_SPXBUIActiveResult = 0x1111006C;
__declspec(dllexport) volatile unsigned int g_SPXBUIMainMenuCount = 0x1111006D;
__declspec(dllexport) volatile unsigned int g_SPXBUIKeyEventCount = 0x1111006E;
__declspec(dllexport) volatile unsigned int g_SPXBUIKeyLast = 0x1111006F;
__declspec(dllexport) volatile unsigned int g_SPXBFrontEndPhase = 0x111100AD;
__declspec(dllexport) volatile unsigned int g_SPXBFrontEndMenuHash = 0x111100AE;
__declspec(dllexport) volatile unsigned int g_SPXBFrontEndItemHash = 0x111100AF;
__declspec(dllexport) volatile unsigned int g_SPXBFrontEndScriptHash = 0x111100B0;
__declspec(dllexport) volatile unsigned int g_SPXBFrontEndPopup = 0x111100B1;
__declspec(dllexport) volatile unsigned int g_SPXBFrontEndResponse = 0x111100B2;
__declspec(dllexport) volatile unsigned int g_SPXBFrontEndController = 0x111100B3;
__declspec(dllexport) volatile unsigned int g_SPXBRenderDrawSurfCount = 0x11110070;
__declspec(dllexport) volatile unsigned int g_SPXBRenderDrawSurfDelta = 0x11110071;
__declspec(dllexport) volatile unsigned int g_SPXBRenderLeafCount = 0x11110072;
__declspec(dllexport) volatile unsigned int g_SPXBRenderCullPatch = 0x11110073;
__declspec(dllexport) volatile unsigned int g_SPXBRenderCullMd3 = 0x11110074;
__declspec(dllexport) volatile unsigned int g_SPXBRenderCullBox = 0x11110075;
__declspec(dllexport) volatile unsigned int g_SPXBRenderDlightSurfaces = 0x11110076;
__declspec(dllexport) volatile unsigned int g_SPXBRenderDlightCulled = 0x11110077;
__declspec(dllexport) volatile unsigned int g_SPXBSkyIterCalls = 0x11110078;
__declspec(dllexport) volatile unsigned int g_SPXBSkyPortalMainFallbacks = 0x11110079;
__declspec(dllexport) volatile unsigned int g_SPXBSkyClipCalls = 0x1111007A;
__declspec(dllexport) volatile unsigned int g_SPXBSkyBoxDrawCalls = 0x1111007B;
__declspec(dllexport) volatile unsigned int g_SPXBSkyBoxSidesDrawn = 0x1111007C;
__declspec(dllexport) volatile unsigned int g_SPXBSkyNoOuterBox = 0x1111007D;
__declspec(dllexport) volatile unsigned int g_SPXBSkyCloudBuilds = 0x1111007E;
__declspec(dllexport) volatile unsigned int g_SPXBSkyGenericCalls = 0x1111007F;
__declspec(dllexport) volatile unsigned int g_SPXBWorldSurfaceAddCalls = 0x11110080;
__declspec(dllexport) volatile unsigned int g_SPXBWorldSkySurfaceAdds = 0x11110081;
__declspec(dllexport) volatile unsigned int g_SPXBWorldPortalSurfaceAdds = 0x11110082;
__declspec(dllexport) volatile unsigned int g_SPXBDrawSurfTotalAdds = 0x11110083;
__declspec(dllexport) volatile unsigned int g_SPXBDrawSurfSkyAdds = 0x11110084;
__declspec(dllexport) volatile unsigned int g_SPXBDrawSurfPortalAdds = 0x11110085;
__declspec(dllexport) volatile unsigned int g_SPXBDrawSurfForceSightSkips = 0x11110086;
__declspec(dllexport) volatile unsigned int g_SPXBCGameRenderCalls = 0x11110087;
__declspec(dllexport) volatile unsigned int g_SPXBCGameDrawFrameReturns = 0x11110088;
__declspec(dllexport) volatile unsigned int g_SPXBLoadingInfoFrames = 0x111100B4;
__declspec(dllexport) volatile unsigned int g_SPXBLoadingSnapshotsProcessed = 0x111100B5;
__declspec(dllexport) volatile unsigned int g_SPXBLoadingStateInfoHandoffs = 0x111100B6;
__declspec(dllexport) volatile unsigned int g_SPXBLoadingTransitionCommands = 0x111100B7;
__declspec(dllexport) volatile unsigned int g_SPXBLoadingTransitionScreenUpdates = 0x111100B8;
__declspec(dllexport) volatile unsigned int g_SPXBLoadingLastClientState = 0x111100B9;
__declspec(dllexport) volatile unsigned int g_SPXBLoadingLastServerTime = 0x111100BA;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSceneCalls = 0x11110089;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSceneNoWorld = 0x1111008A;
__declspec(dllexport) volatile unsigned int g_SPXBRenderViewCalls = 0x1111008B;
__declspec(dllexport) volatile unsigned int g_SPXBRenderViewWorldCalls = 0x1111008C;
__declspec(dllexport) volatile unsigned int g_SPXBScreenDrawCalls = 0x1111008D;
__declspec(dllexport) volatile unsigned int g_SPXBScreenForceDirectCalls = 0x1111008E;
__declspec(dllexport) volatile unsigned int g_SPXBScreenFullscreenSkips = 0x1111008F;
__declspec(dllexport) volatile unsigned int g_SPXBScreenCinematicDraws = 0x11110090;
__declspec(dllexport) volatile unsigned int g_SPXBScreenCGameCalls = 0x11110091;
__declspec(dllexport) volatile unsigned int g_SPXBScreenUIRefreshes = 0x11110092;
__declspec(dllexport) volatile unsigned int g_SPXBScreenDirectReturns = 0x11110093;
__declspec(dllexport) volatile unsigned int g_SPXBCLFrameEnterCalls = 0x11110094;
__declspec(dllexport) volatile unsigned int g_SPXBCLFrameDirectReturns = 0x11110095;
__declspec(dllexport) volatile unsigned int g_SPXBCLFrameBeforeScreen = 0x11110096;
__declspec(dllexport) volatile unsigned int g_SPXBCLFrameAfterScreen = 0x11110097;
__declspec(dllexport) volatile unsigned int g_SPXBCLFrameCompleted = 0x11110098;
__declspec(dllexport) volatile unsigned int g_SPXBRenderRegistrationState = 0x11110099;
__declspec(dllexport) volatile unsigned int g_SPXBRenderStretchPicCalls = 0x1111009A;
__declspec(dllexport) volatile unsigned int g_SPXBRenderStretchPicCmdNull = 0x1111009B;
__declspec(dllexport) volatile unsigned int g_SPXBRenderBeginFrameCalls = 0x1111009C;
__declspec(dllexport) volatile unsigned int g_SPXBRenderBeginFrameUnregistered = 0x1111009D;
__declspec(dllexport) volatile unsigned int g_SPXBRenderEndFrameCalls = 0x1111009E;
__declspec(dllexport) volatile unsigned int g_SPXBRenderEndFrameUnregistered = 0x1111009F;
__declspec(dllexport) volatile unsigned int g_SPXBRenderEndFrameCmdNull = 0x111100A0;
__declspec(dllexport) volatile unsigned int g_SPXBRenderEndFrameBeginFail = 0x111100A1;
__declspec(dllexport) volatile unsigned int g_SPXBRenderIssueCalls = 0x111100A2;
__declspec(dllexport) volatile unsigned int g_SPXBRenderIssueCmdUsed = 0x111100A3;
__declspec(dllexport) volatile unsigned int g_SPXBCompatBeginFrameCalls = 0x111100A4;
__declspec(dllexport) volatile unsigned int g_SPXBCompatEndFrameCalls = 0x111100A5;
__declspec(dllexport) volatile unsigned int g_SPXBFakeSwapBuffersCalls = 0x111100A6;
__declspec(dllexport) volatile unsigned int g_SPXBDx8BeginFrameCalls = 0x111100A7;
__declspec(dllexport) volatile unsigned int g_SPXBDx8EndFrameCalls = 0x111100A8;
__declspec(dllexport) volatile unsigned int g_SPXBDx8PresentCalls = 0x111100A9;
__declspec(dllexport) volatile unsigned int g_SPXBDx8PresentHr = 0x111100AA;
__declspec(dllexport) volatile unsigned int g_SPXBDx8FramebufferUpdates = 0x111100AB;
__declspec(dllexport) volatile unsigned int g_SPXBDx8FramebufferBackBufferFail = 0x111100AC;
__declspec(dllexport) volatile char g_SPXBLogMirror[32768];
__declspec(dllexport) volatile char g_SPXBLogLastLine[512];
__declspec(dllexport) volatile char g_SPXBCmdLast[128];
__declspec(dllexport) volatile char g_SPXBCmdTextLast[128];
__declspec(dllexport) volatile char g_SPXBCmdFunctionNameLast[64];
__declspec(dllexport) volatile char g_SPXBCinArgLast[64];
__declspec(dllexport) volatile char g_SPXBMapLast[64];
__declspec(dllexport) volatile char g_SPXBGameClassLast[64];
}

static void xbl_CopyVolatile(volatile char *dest, unsigned int destSize, const char *src)
{
    if (!dest || destSize == 0) {
        return;
    }

    unsigned int i = 0;
    if (src) {
        while (src[i] && i < destSize - 1) {
            dest[i] = src[i];
            ++i;
        }
    }
    dest[i] = 0;
}

static void xbl_MirrorWrite(const char *msg, DWORD len)
{
    if (!msg || len == 0) {
        return;
    }

    unsigned int pos = g_SPXBLogMirrorPos;
    for (DWORD i = 0; i < len; ++i) {
        g_SPXBLogMirror[pos & (sizeof(g_SPXBLogMirror) - 1)] = msg[i];
        ++pos;
    }
    g_SPXBLogMirror[pos & (sizeof(g_SPXBLogMirror) - 1)] = 0;
    g_SPXBLogMirrorPos = pos;
    xbl_CopyVolatile(g_SPXBLogLastLine, sizeof(g_SPXBLogLastLine), msg);
    ++g_SPXBLogWriteCount;
}

static int xbl_starts_with(const char *msg, const char *prefix)
{
    while (*prefix) {
        if (*msg++ != *prefix++) return 0;
    }
    return 1;
}

static int xbl_ShouldDropVerbose(const char *msg)
{
    static int s_playerBudget = 160;
    static int s_frameBudget = 256;
    static int s_renderBudget = 128;
    static int s_cgameBudget = 128;
    static int s_assetBudget = 96;

    if (!msg) return 1;
    if (!g_verboseLog) {
        if (strstr(msg, "FRAME_HEARTBEAT") ||
            strstr(msg, "SMOKE_BUTTON") ||
            strstr(msg, "direct-map boot") ||
            strstr(msg, "Server:") ||
            strstr(msg, "SV_InitGameProgs") ||
            strstr(msg, "InitGame") ||
            strstr(msg, "G_AllocGentities") ||
            strstr(msg, "G_SpawnEntitiesFromString") ||
            strstr(msg, "CL_Init:") ||
            strstr(msg, "CL_StartHunkUsers: ready fast path") ||
            strstr(msg, "CL_InitUI") ||
            strstr(msg, "CL_InitCGame") ||
            strstr(msg, "SCR_Init") ||
            strstr(msg, "UI_Init") ||
            strstr(msg, "_UI_Init") ||
            strstr(msg, "UI_SetActiveMenu") ||
            strstr(msg, "BinkVideo::Start") ||
            strstr(msg, "CG_Init") ||
            strstr(msg, "CG_LoadHudMenu") ||
            strstr(msg, "CG_LoadMenus") ||
            strstr(msg, "CG_Load_Menu") ||
            strstr(msg, "CG_ParseMenu") ||
            strstr(msg, "cgi_UI_StartParseSession") ||
            strstr(msg, "VM_DllSyscall CG_UI_STARTPARSE") ||
            strstr(msg, "CG trap UI_STARTPARSE") ||
            strstr(msg, "UI Menu_New") ||
            strstr(msg, "UI Menu_Parse") ||
            strstr(msg, "UI PC_StartParseSession") ||
            strstr(msg, "UI PC_EndParseSession") ||
            strstr(msg, "UI Menus_CloseAll") ||
            strstr(msg, "UI_LoadMenus") ||
            strstr(msg, "CG_GameStateReceived") ||
            strstr(msg, "CG_RegisterGraphics") ||
            strstr(msg, "CG_NewClientinfo") ||
            strstr(msg, "VM_Call(CG_INIT)") ||
            strstr(msg, "cls.state = CA_PRIMED") ||
        strstr(msg, "cls.state = CA_ACTIVE - GAME IS RUNNING") ||
        strstr(msg, "R_Register forcing r_") ||
        strstr(msg, "R_Register Xbox lighting baseline") ||
        strstr(msg, "FATAL") ||
        strstr(msg, "ERROR") ||
            strstr(msg, "Out of memory") ||
            strstr(msg, "Received Exception") ||
            strstr(msg, "EIP") ||
            strstr(msg, "Z_Malloc():") ||
            strstr(msg, "texture allocation failures") ||
            strstr(msg, "XBLog disk cap") ||
            strstr(msg, "S_LoadSound guard") ||
            strstr(msg, "S_CancelLoadSound") ||
            strstr(msg, "SND_DetachSFXFromChannels") ||
            strstr(msg, "SND_RegisterAudio_LevelLoadEnd") ||
            strstr(msg, "RE_RegisterModels_LevelLoadEnd") ||
            strstr(msg, "RE_RegisterImages_LevelLoadEnd") ||
            strstr(msg, "Sys_Stream")) {
            return 0;
        }
        return 1;
    }

    if (strstr(msg, "FRAME_HEARTBEAT") ||
        strstr(msg, "SMOKE_BUTTON") ||
        strstr(msg, "KEY_TRACE") ||
        strstr(msg, "UI_TRACE") ||
        strstr(msg, "FATAL") ||
        strstr(msg, "ERROR") ||
        strstr(msg, "Out of memory") ||
        strstr(msg, "texture allocation failures")) {
        return 0;
    }

    if (xbl_starts_with(msg, "JA: CG_Player ") ||
        xbl_starts_with(msg, "JA: CG_AddSaberBladeGo ")) {
        if (s_playerBudget > 0) {
            s_playerBudget--;
            return 0;
        }
        return 1;
    }

    if (xbl_starts_with(msg, "JA: RB_RenderDrawSurfList") ||
        xbl_starts_with(msg, "JA: fakegl DrawPrimitiveUP submit") ||
        xbl_starts_with(msg, "JA: fakegl SwapBuffers") ||
        xbl_starts_with(msg, "JA: compat glEndFrame") ||
        xbl_starts_with(msg, "JA: fakegl stage state") ||
        xbl_starts_with(msg, "JA: fakegl SetTexture") ||
        xbl_starts_with(msg, "JA: DrawMultitextured") ||
        xbl_starts_with(msg, "JA: RB_StageIteratorGeneric") ||
        xbl_starts_with(msg, "JA: R_DrawElements chunk")) {
        if (s_renderBudget > 0) {
            s_renderBudget--;
            return 0;
        }
        return 1;
    }

    if (xbl_starts_with(msg, "JA: CG_AddPacketEntities") ||
        xbl_starts_with(msg, "JA: CG_AddCEntity") ||
        xbl_starts_with(msg, "JA: CG_AddMarks") ||
        xbl_starts_with(msg, "JA: CG_G2") ||
        xbl_starts_with(msg, "JA: CG_RegisterWeapon") ||
        xbl_starts_with(msg, "JA: Com_EventLoop") ||
        xbl_starts_with(msg, "JA: CL_PacketEvent") ||
        xbl_starts_with(msg, "JA: CL_ParseServerMessage") ||
        xbl_starts_with(msg, "JA: SV_ExecuteClientMessage")) {
        if (s_cgameBudget > 0) {
            s_cgameBudget--;
            return 0;
        }
        return 1;
    }

    if (xbl_starts_with(msg, "JA: Upload32") ||
        xbl_starts_with(msg, "JkaGlTexImage2D:") ||
        xbl_starts_with(msg, "JA: fakegl DDS top-mip skip") ||
        xbl_starts_with(msg, "JA: fakegl DDS single-mip cap") ||
        xbl_starts_with(msg, "JA: fakegl DDS CreateTexture pre") ||
        xbl_starts_with(msg, "JA: fakegl DDS CreateTexture post") ||
        xbl_starts_with(msg, "JA: fakegl DDS registered direct copy") ||
        xbl_starts_with(msg, "JA: RE_RegisterModels_Malloc") ||
        xbl_starts_with(msg, "JA: FX RegisterEffect") ||
        xbl_starts_with(msg, "JA: FX ParseEffect")) {
        if (s_assetBudget > 0) {
            s_assetBudget--;
            return 0;
        }
        return 1;
    }

    if (xbl_starts_with(msg, "JA: RE_EndFrame") ||
        xbl_starts_with(msg, "JA: RE_BeginFrame") ||
        xbl_starts_with(msg, "JA: CL_Frame") ||
        xbl_starts_with(msg, "JA: CL_SendCmd") ||
        xbl_starts_with(msg, "JA: R_IssueRenderCommands") ||
        xbl_starts_with(msg, "JA: RB_ExecuteRenderCommands") ||
        xbl_starts_with(msg, "JA: RB_DrawSurfs") ||
        xbl_starts_with(msg, "JA: RB_SwapBuffers") ||
        xbl_starts_with(msg, "JA: SCR_DrawScreenField") ||
        xbl_starts_with(msg, "JA: SCR_UpdateScreen") ||
        xbl_starts_with(msg, "JA: CG_DrawActiveFrame") ||
        xbl_starts_with(msg, "JA: CL_CGameRendering") ||
        xbl_starts_with(msg, "JA: VM_Call ")) {
        if (s_frameBudget > 0) {
            s_frameBudget--;
            return 0;
        }
        return 1;
    }

    return 0;
}

static int xbl_FormatMayBeCritical(const char *fmt)
{
    if (!fmt) return 0;
    return strstr(fmt, "FRAME_HEARTBEAT") ||
        strstr(fmt, "SMOKE_BUTTON") ||
        strstr(fmt, "direct-map boot") ||
        strstr(fmt, "Server:") ||
        strstr(fmt, "SV_InitGameProgs") ||
        strstr(fmt, "InitGame") ||
        strstr(fmt, "G_AllocGentities") ||
        strstr(fmt, "G_SpawnEntitiesFromString") ||
        strstr(fmt, "CL_Init:") ||
        strstr(fmt, "CL_InitUI") ||
        strstr(fmt, "CL_InitCGame") ||
        strstr(fmt, "SCR_Init") ||
        strstr(fmt, "UI_Init") ||
        strstr(fmt, "_UI_Init") ||
        strstr(fmt, "UI_SetActiveMenu") ||
        strstr(fmt, "CMD_TRACE") ||
        strstr(fmt, "BinkVideo::Start") ||
        strstr(fmt, "CG_Init") ||
        strstr(fmt, "CG_LoadHudMenu") ||
        strstr(fmt, "CG_LoadMenus") ||
        strstr(fmt, "CG_Load_Menu") ||
        strstr(fmt, "CG_ParseMenu") ||
        strstr(fmt, "cgi_UI_StartParseSession") ||
        strstr(fmt, "VM_DllSyscall CG_UI_STARTPARSE") ||
        strstr(fmt, "CG trap UI_STARTPARSE") ||
        strstr(fmt, "UI PC_StartParseSession") ||
        strstr(fmt, "UI PC_EndParseSession") ||
        strstr(fmt, "UI Menus_CloseAll") ||
        strstr(fmt, "UI_LoadMenus") ||
        strstr(fmt, "CG_GameStateReceived") ||
        strstr(fmt, "CG_RegisterGraphics") ||
        strstr(fmt, "CG_NewClientinfo") ||
        strstr(fmt, "VM_Call(CG_INIT)") ||
        strstr(fmt, "CAMERA_") ||
        strstr(fmt, "cls.state = CA_PRIMED") ||
        strstr(fmt, "cls.state = CA_ACTIVE - GAME IS RUNNING") ||
        strstr(fmt, "FATAL") ||
        strstr(fmt, "ERROR") ||
        strstr(fmt, "Out of memory") ||
        strstr(fmt, "Received Exception") ||
        strstr(fmt, "EIP") ||
        strstr(fmt, "Z_Malloc():") ||
        strstr(fmt, "texture allocation failures") ||
        strstr(fmt, "XBLog disk cap") ||
        strstr(fmt, "S_LoadSound guard") ||
        strstr(fmt, "S_CancelLoadSound") ||
        strstr(fmt, "SND_DetachSFXFromChannels") ||
        strstr(fmt, "SND_RegisterAudio_LevelLoadEnd") ||
        strstr(fmt, "RE_RegisterModels_LevelLoadEnd") ||
        strstr(fmt, "RE_RegisterImages_LevelLoadEnd") ||
        strstr(fmt, "Sys_Stream");
}

static int xbl_FileExists(const char *path)
{
    DWORD attr = GetFileAttributesA(path);
    return attr != 0xFFFFFFFF && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static long xbl_NtCreate(const char *path, HANDLE *out)
{
    XBL_STR  name;
    XBL_OA   oa;
    XBL_IOSB iosb;
    name.Buffer        = (char*)path;
    name.Length        = (unsigned short)strlen(path);
    name.MaximumLength = name.Length + 1;
    oa.RootDirectory   = NULL;
    oa.ObjectName      = &name;
    oa.Attributes      = 0x40;   /* OBJ_CASE_INSENSITIVE */
    return NtCreateFile(out,
        GENERIC_WRITE | 0x00100000,   /* GENERIC_WRITE | SYNCHRONIZE */
        &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, 0,
        5,                            /* FILE_OVERWRITE_IF */
        0x20 | 0x02 | 0x40);         /* SYNCHRONOUS_IO_NONALERT | WRITE_THROUGH | NON_DIRECTORY */
}

static void xbl_FlushHandle(HANDLE h, int isNt)
{
    if (h == INVALID_HANDLE_VALUE) return;
    if (isNt) {
        XBL_IOSB iosb;
        NtFlushBuffersFile(h, &iosb);
    } else {
        FlushFileBuffers(h);
    }
}

static int xbl_ShouldFlushWrite(const char *msg)
{
    if (!msg) return 0;
    if (strstr(msg, "=== Jedi Academy Xbox SP log ===")) return 1;
    if (strstr(msg, "FATAL")) return 1;
    if (strstr(msg, "ERROR")) return 1;
    if (strstr(msg, "Out of memory")) return 1;
    if (strstr(msg, "Received Exception")) return 1;
    if (strstr(msg, "EIP")) return 1;
    if (strstr(msg, "Z_Malloc():")) return 1;
    if (strstr(msg, "texture allocation failures")) return 1;
    return 0;
}

static int xbl_PrepareDiskWrite(const char **msg, DWORD *len, unsigned int *bytes,
    int *limitAnnounced, int critical)
{
    static const char limitMsg[] = "JA: XBLog disk cap reached; continuing in memory mirror only\n";

    if (!msg || !*msg || !len || !bytes || !limitAnnounced) return 0;

    if (*bytes + *len <= XBL_DISK_LOG_SOFT_LIMIT) {
        *bytes += *len;
        return 1;
    }

    if (critical && *bytes + *len <= XBL_DISK_LOG_HARD_LIMIT) {
        *bytes += *len;
        return 1;
    }

    if (!*limitAnnounced) {
        *msg = limitMsg;
        *len = sizeof(limitMsg) - 1;
        *limitAnnounced = 1;
        if (*bytes + *len <= XBL_DISK_LOG_HARD_LIMIT) {
            *bytes += *len;
            return 1;
        }
    }

    return 0;
}

void XBLog_Init(void)
{
    int  i;
    long status;

    g_SPXBBootPhase = 0x30;
    g_hLogFile = INVALID_HANDLE_VALUE;
    g_logIsNt  = 0;
    g_logPath  = NULL;
    g_hMirrorLogFile = INVALID_HANDLE_VALUE;
    g_mirrorLogPath = NULL;
    g_verboseLog = SP_XBOX_VERBOSE_RUNTIME_LOGS ? 1 : 0;
    g_logDiskBytes = 0;
    g_mirrorLogDiskBytes = 0;
    g_logDiskLimitAnnounced = 0;
    g_mirrorLogDiskLimitAnnounced = 0;
    g_SPXBLogMirrorPos = 0;
    g_SPXBLogWriteCount = 0;
    g_SPXBHeartbeatCount = 0;
    g_SPXBHeartbeatFrame = 0;
    g_SPXBHeartbeatRealtime = 0;
    g_SPXBHeartbeatServerTime = 0;
    g_SPXBHeartbeatFps10 = 0;
    g_SPXBMainLoopCount = 0;
    g_SPXBComFrameCount = 0;
    g_SPXBSvFrameCount = 0;
    g_SPXBClFrameCount = 0;
    g_SPXBClsState = 0;
    g_SPXBClServerTime = 0;
    g_SPXBClsFrameCount = 0;
    g_SPXBPhaseLast = 0;
    g_SPXBComSubphase = 0;
    g_SPXBComSpinCount = 0;
    g_SPXBComMsec = 0;
    g_SPXBComFrameTime = 0;
    g_SPXBComLastTime = 0;
    g_SPXBCbufExecCount = 0;
    g_SPXBCmdExecCount = 0;
    g_SPXBCmdPhase = 0;
    g_SPXBCmdHash = 0;
    g_SPXBCmdArgc = 0;
    g_SPXBCmdLoopIndex = 0;
    g_SPXBCmdLoopNameHash = 0;
    g_SPXBCmdFunctionPtr = 0;
    g_SPXBCmdArgv0First4 = 0;
    g_SPXBCmdNameFirst4 = 0;
    g_SPXBCmdTextLast[0] = 0;
    g_SPXBCmdFunctionNameLast[0] = 0;
    g_SPXBMapPhase = 0;
    g_SPXBMapHash = 0;
    g_SPXBDirectMapState = 0;
    g_SPXBDirectMapLastError = 0;
    g_SPXBDirectMapPathIndex = 0;
    g_SPXBDirectMapFirst4 = 0;
    g_SPXBSvMapState = 0;
    g_SPXBClHunkState = 0;
    g_SPXBComErrorCode = 0;
    g_SPXBComErrorHash = 0;
    g_SPXBComErrorFirst4 = 0;
    g_SPXBComErrorNext4 = 0;
    g_SPXBClHunkCaller = 0;
    g_SPXBClHunkCallCount = 0;
    g_SPXBCmLoadState = 0;
    g_SPXBCmLoadLumpHash = 0;
    g_SPXBCmLoadLumpLen = 0;
    g_SPXBGamePhase = 0;
    g_SPXBGameEntityCount = 0;
    g_SPXBGameClassHash = 0;
    g_SPXBGentitiesPtr = 0;
    g_SPXBClientsPtr = 0;
    g_SPXBGentitySize = 0;
    g_SPXBClientFieldBefore = 0;
    g_SPXBClientFieldAfter = 0;
    g_SPXBRenderDrawSurfLists = 0;
    g_SPXBRenderSurfaces = 0;
    g_SPXBRenderEndSurfaces = 0;
    g_SPXBRenderBackendMsec = 0;
    g_SPXBFakeGLPrimitiveCalls = 0;
    g_SPXBFakeGLPrimitiveVerts = 0;
    g_SPXBFakeGLStateFlushes = 0;
    g_SPXBRenderSplitShader = 0;
    g_SPXBRenderSplitFog = 0;
    g_SPXBRenderSplitDlight = 0;
    g_SPXBRenderSplitEntity = 0;
    g_SPXBRenderSplitFinal = 0;
    g_SPXBRenderSplitFlush = 0;
    g_SPXBFrontEndPhase = 0;
    g_SPXBFrontEndMenuHash = 0;
    g_SPXBFrontEndItemHash = 0;
    g_SPXBFrontEndScriptHash = 0;
    g_SPXBFrontEndPopup = 0;
    g_SPXBFrontEndResponse = 0;
    g_SPXBFrontEndController = 0;
    g_SPXBRenderDrawSurfCount = 0;
    g_SPXBRenderDrawSurfDelta = 0;
    g_SPXBRenderLeafCount = 0;
    g_SPXBRenderCullPatch = 0;
    g_SPXBRenderCullMd3 = 0;
    g_SPXBRenderCullBox = 0;
    g_SPXBRenderDlightSurfaces = 0;
    g_SPXBRenderDlightCulled = 0;
    g_SPXBSkyIterCalls = 0;
    g_SPXBSkyPortalMainFallbacks = 0;
    g_SPXBSkyClipCalls = 0;
    g_SPXBSkyBoxDrawCalls = 0;
    g_SPXBSkyBoxSidesDrawn = 0;
    g_SPXBSkyNoOuterBox = 0;
    g_SPXBSkyCloudBuilds = 0;
    g_SPXBSkyGenericCalls = 0;
    g_SPXBWorldSurfaceAddCalls = 0;
    g_SPXBWorldSkySurfaceAdds = 0;
    g_SPXBWorldPortalSurfaceAdds = 0;
    g_SPXBDrawSurfTotalAdds = 0;
    g_SPXBDrawSurfSkyAdds = 0;
    g_SPXBDrawSurfPortalAdds = 0;
    g_SPXBDrawSurfForceSightSkips = 0;
    g_SPXBCGameRenderCalls = 0;
    g_SPXBCGameDrawFrameReturns = 0;
    g_SPXBLoadingInfoFrames = 0;
    g_SPXBLoadingSnapshotsProcessed = 0;
    g_SPXBLoadingStateInfoHandoffs = 0;
    g_SPXBLoadingTransitionCommands = 0;
    g_SPXBLoadingTransitionScreenUpdates = 0;
    g_SPXBLoadingLastClientState = 0;
    g_SPXBLoadingLastServerTime = 0;
    g_SPXBRenderSceneCalls = 0;
    g_SPXBRenderSceneNoWorld = 0;
    g_SPXBRenderViewCalls = 0;
    g_SPXBRenderViewWorldCalls = 0;
    g_SPXBScreenDrawCalls = 0;
    g_SPXBScreenForceDirectCalls = 0;
    g_SPXBScreenFullscreenSkips = 0;
    g_SPXBScreenCinematicDraws = 0;
    g_SPXBScreenCGameCalls = 0;
    g_SPXBScreenUIRefreshes = 0;
    g_SPXBScreenDirectReturns = 0;
    g_SPXBCLFrameEnterCalls = 0;
    g_SPXBCLFrameDirectReturns = 0;
    g_SPXBCLFrameBeforeScreen = 0;
    g_SPXBCLFrameAfterScreen = 0;
    g_SPXBCLFrameCompleted = 0;
    g_SPXBRenderRegistrationState = 0;
    g_SPXBRenderStretchPicCalls = 0;
    g_SPXBRenderStretchPicCmdNull = 0;
    g_SPXBRenderBeginFrameCalls = 0;
    g_SPXBRenderBeginFrameUnregistered = 0;
    g_SPXBRenderEndFrameCalls = 0;
    g_SPXBRenderEndFrameUnregistered = 0;
    g_SPXBRenderEndFrameCmdNull = 0;
    g_SPXBRenderEndFrameBeginFail = 0;
    g_SPXBRenderIssueCalls = 0;
    g_SPXBRenderIssueCmdUsed = 0;
    g_SPXBCompatBeginFrameCalls = 0;
    g_SPXBCompatEndFrameCalls = 0;
    g_SPXBFakeSwapBuffersCalls = 0;
    g_SPXBDx8BeginFrameCalls = 0;
    g_SPXBDx8EndFrameCalls = 0;
    g_SPXBDx8PresentCalls = 0;
    g_SPXBDx8PresentHr = 0;
    g_SPXBDx8FramebufferUpdates = 0;
    g_SPXBDx8FramebufferBackBufferFail = 0;
    g_SPXBFramebufferData = 0;
    g_SPXBFramebufferPitch = 0;
    g_SPXBFramebufferWidth = 0;
    g_SPXBFramebufferHeight = 0;
    g_SPXBFramebufferFormat = 0;
    g_SPXBFramebufferSize = 0;
    g_SPXBCinPhase = 0;
    g_SPXBCinHandle = 0;
    g_SPXBCinStatus = 0;
    g_SPXBCinLoopCount = 0;
    g_SPXBBinkPhase = 0;
    g_SPXBBinkRunCount = 0;
    g_SPXBBinkWaitLoops = 0;
    g_SPXBBinkWaitBreaks = 0;
    g_SPXBBinkFrameNum = 0;
    g_SPXBBinkFrames = 0;
    g_SPXBBinkOpenFlags = 0;
    g_SPXBBinkWidth = 0;
    g_SPXBBinkHeight = 0;
    g_SPXBBinkAlpha = 0;
    g_SPXBBinkCopySkipped = 0;
    g_SPXBBinkStartResult = 0;
    g_SPXBBinkStatus = 0;
    g_SPXBCinArgLast[0] = 0;
    for (i = 0; i < 16; ++i) {
        g_SPXBSurfaceTypeCounts[i] = 0;
        g_SPXBEntityTypeCounts[i] = 0;
    }
    for (i = 0; i < (int)sizeof(g_SPXBLogMirror); ++i) {
        g_SPXBLogMirror[i] = 0;
    }
    for (i = 0; i < (int)sizeof(g_SPXBLogLastLine); ++i) {
        g_SPXBLogLastLine[i] = 0;
    }

    g_SPXBBootPhase = 0x31;
    /*
     * CXBX-R maps D: to the title directory.  Retail D: is read-only, so this
     * quietly fails there, but on emulator it gives us a fresh log beside
     * default.xbe for each boot like the Unreal Tournament Xbox port does.
     */
    g_hMirrorLogFile = CreateFileA("D:\\ja_sp_log.txt", FILE_APPEND_DATA, FILE_SHARE_READ,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    g_SPXBBootPhase = 0x32;
    if (g_hMirrorLogFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(g_hMirrorLogFile, 0, NULL, FILE_END);
        g_mirrorLogPath = "D:\\ja_sp_log.txt";
    }

    g_SPXBBootPhase = 0x33;
    /*
     * Strategy 1: NtCreateFile to raw device paths (retail hw + CXBX-R).
     * E: appends to the file XBLog_PreCRTProbe already created, preserving
     * the "precrt_ok" line. F:/G: are fallback-only and start fresh so a
     * long run cannot keep appending to an old fallback log forever.
     */
    {
        static const char *ntPaths[] = {
            "\\Device\\Harddisk0\\Partition1\\ja_sp_log.txt",   /* E:\ */
            "\\Device\\Harddisk0\\Partition6\\ja_sp_log.txt",   /* F:\ */
            "\\Device\\Harddisk0\\Partition7\\ja_sp_log.txt",   /* G:\ */
            NULL
        };
        for (i = 0; ntPaths[i]; ++i) {
            g_SPXBBootPhase = 0x340 + (unsigned int)i;
            XBL_STR  name;
            XBL_OA   oa;
            XBL_IOSB iosb;
            name.Buffer        = (char*)ntPaths[i];
            name.Length        = (unsigned short)strlen(ntPaths[i]);
            name.MaximumLength = name.Length + 1;
            oa.RootDirectory   = NULL;
            oa.ObjectName      = &name;
            oa.Attributes      = 0x40;
            const unsigned long createDisposition = (i == 0) ? 3 : 5;
            /* E: FILE_OPEN_IF, fallback drives: FILE_OVERWRITE_IF. */
            status = NtCreateFile(&g_hLogFile,
                0x04 | 0x00100000,   /* FILE_APPEND_DATA | SYNCHRONIZE */
                &oa, &iosb, NULL,
                FILE_ATTRIBUTE_NORMAL, 0,
                createDisposition,
                0x20 | 0x02 | 0x40);
            if (status >= 0) {
                g_logIsNt = 1;
                g_logPath = ntPaths[i];
                g_SPXBBootPhase = 0x35;
                XBL("=== Jedi Academy Xbox SP log ===\n");
                XBL("JA: XBLog disk cap soft=4194304 hard=5242880\n");
                return;
            }
        }
    }

    /* Strategy 2: CreateFileA with drive letters. Start fresh on fallback paths. */
    g_SPXBBootPhase = 0x36;
    {
        static const char *caPaths[] = {
            "D:\\ja_sp_log.txt",
            "E:\\ja_sp_log.txt",
            "T:\\ja_sp_log.txt",
            "ja_sp_log.txt",
            NULL
        };
        for (i = 0; caPaths[i]; ++i) {
            g_SPXBBootPhase = 0x370 + (unsigned int)i;
            g_hLogFile = CreateFileA(caPaths[i], FILE_APPEND_DATA, FILE_SHARE_READ,
                NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
            if (g_hLogFile != INVALID_HANDLE_VALUE) {
                /* FILE_APPEND_DATA keeps writes at EOF even for a fresh file. */
                SetFilePointer(g_hLogFile, 0, NULL, FILE_END);
                g_logIsNt = 0;
                g_logPath = caPaths[i];
                g_SPXBBootPhase = 0x38;
                XBL("=== Jedi Academy Xbox SP log ===\n");
                XBL("JA: XBLog disk cap soft=4194304 hard=5242880\n");
                return;
            }
        }
    }

    OutputDebugStringA("XBLog_Init: all log paths failed\n");
}

void XBLog_Shutdown(void)
{
    XBL("=== log end ===\n");
    if (g_hMirrorLogFile != INVALID_HANDLE_VALUE) {
        xbl_FlushHandle(g_hMirrorLogFile, 0);
        CloseHandle(g_hMirrorLogFile);
        g_hMirrorLogFile = INVALID_HANDLE_VALUE;
    }
    if (g_hLogFile != INVALID_HANDLE_VALUE) {
        xbl_FlushHandle(g_hLogFile, g_logIsNt);
        if (g_logIsNt) NtClose(g_hLogFile);
        else           CloseHandle(g_hLogFile);
        g_hLogFile = INVALID_HANDLE_VALUE;
    }
    g_logPath = NULL;
    g_mirrorLogPath = NULL;
}

void XBLog_Print(const char *msg)
{
    DWORD len;
    if (!msg) return;
    OutputDebugStringA(msg);
    len = (DWORD)strlen(msg);
    xbl_MirrorWrite(msg, len);
    const int forceFlush = xbl_ShouldFlushWrite(msg);
    if (g_hMirrorLogFile != INVALID_HANDLE_VALUE) {
        const char *diskMsg = msg;
        DWORD diskLen = len;
        DWORD written;
        if (xbl_PrepareDiskWrite(&diskMsg, &diskLen, &g_mirrorLogDiskBytes,
            &g_mirrorLogDiskLimitAnnounced, forceFlush)) {
            WriteFile(g_hMirrorLogFile, diskMsg, diskLen, &written, NULL);
            if (forceFlush) xbl_FlushHandle(g_hMirrorLogFile, 0);
        }
    }
    if (g_hLogFile == INVALID_HANDLE_VALUE) return;
    if (g_logIsNt) {
        const char *diskMsg = msg;
        DWORD diskLen = len;
        XBL_IOSB iosb;
        if (xbl_PrepareDiskWrite(&diskMsg, &diskLen, &g_logDiskBytes,
            &g_logDiskLimitAnnounced, forceFlush)) {
            NtWriteFile(g_hLogFile, NULL, NULL, NULL, &iosb, (void*)diskMsg, diskLen, NULL);
            if (forceFlush) xbl_FlushHandle(g_hLogFile, 1);
        }
    } else {
        const char *diskMsg = msg;
        DWORD diskLen = len;
        DWORD written;
        if (xbl_PrepareDiskWrite(&diskMsg, &diskLen, &g_logDiskBytes,
            &g_logDiskLimitAnnounced, forceFlush)) {
            WriteFile(g_hLogFile, diskMsg, diskLen, &written, NULL);
            if (forceFlush) xbl_FlushHandle(g_hLogFile, 0);
        }
    }
}

void XBLog_Printf(const char *fmt, ...)
{
    char    buf[XBL_BUF_SIZE];
    va_list args;
    if (!g_verboseLog && !xbl_FormatMayBeCritical(fmt)) return;
    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';
    XBLog_Print(buf);
}

const char *XBLog_GetPath(void)
{
    return g_logPath;
}

/*
 * XBLog_PreCRTProbe — called from ASM _WinMainCRTStartup BEFORE _mainCRTStartup.
 * Creates ja_sp_log.txt (overwrites any previous run) and writes the first line.
 * No C runtime, no heap, no globals — pure NT syscalls only.
 * XBLog_Init() later re-opens the same file in append mode and continues writing.
 * If only "precrt_ok" appears in the log, a static ctor is crashing before main().
 */
extern "C" void XBLog_PreCRTProbe(void)
{
    g_SPXBBootPhase = 1;
    static const char path[] = "\\Device\\Harddisk0\\Partition1\\ja_sp_log.txt";
    static const char data[] = "precrt_ok\n";
    HANDLE    h;
    XBL_STR   name;
    XBL_OA    oa;
    XBL_IOSB  iosb;

    name.Buffer        = (char*)path;
    name.Length        = sizeof(path) - 1;
    name.MaximumLength = sizeof(path);
    oa.RootDirectory   = NULL;
    oa.ObjectName      = &name;
    oa.Attributes      = 0x40;
    h = INVALID_HANDLE_VALUE;

    /* FILE_OVERWRITE_IF (5): create fresh log for this boot */
    if (NtCreateFile(&h, GENERIC_WRITE | 0x00100000, &oa, &iosb, NULL,
            FILE_ATTRIBUTE_NORMAL, 0, 5, 0x20 | 0x02 | 0x40) >= 0) {
        NtWriteFile(h, NULL, NULL, NULL, &iosb, (void*)data, sizeof(data) - 1, NULL);
        NtFlushBuffersFile(h, &iosb);
        NtClose(h);
    }
}

/*
 * XBLog_PostCRTProbe — called from ASM _WinMainCRTStartup AFTER _mainCRTStartup returns.
 * If this line appears in the log, CreateThread succeeded and the game thread was spawned.
 * If only "precrt_ok" appears, _mainCRTStartup called XapiBootToDash (thread creation
 * failed) or crashed before returning.
 * Uses raw NT append — no CRT, no heap, no globals needed.
 */
extern "C" void XBLog_PostCRTProbe(void)
{
    g_SPXBBootPhase = 9;
    static const char path[] = "\\Device\\Harddisk0\\Partition1\\ja_sp_log.txt";
    static const char data[] = "post_crt\n";
    HANDLE    h;
    XBL_STR   name;
    XBL_OA    oa;
    XBL_IOSB  iosb;

    name.Buffer        = (char*)path;
    name.Length        = sizeof(path) - 1;
    name.MaximumLength = sizeof(path);
    oa.RootDirectory   = NULL;
    oa.ObjectName      = &name;
    oa.Attributes      = 0x40;
    h = INVALID_HANDLE_VALUE;

    /* FILE_OPEN_IF (3) + FILE_APPEND_DATA: append after precrt_ok line */
    if (NtCreateFile(&h, 0x04 | 0x00100000, &oa, &iosb, NULL,
            FILE_ATTRIBUTE_NORMAL, 0, 3, 0x20 | 0x02 | 0x40) >= 0) {
        NtWriteFile(h, NULL, NULL, NULL, &iosb, (void*)data, sizeof(data) - 1, NULL);
        NtFlushBuffersFile(h, &iosb);
        NtClose(h);
    }
}

/* Backward-compat: auto-append \n so old call sites don't need changes. */
void XBLog_Write(const char *msg)
{
    char buf[XBL_BUF_SIZE];
    if (!msg) return;
    if (!g_verboseLog && xbl_ShouldDropVerbose(msg)) return;
    _snprintf(buf, sizeof(buf) - 2, "%s", msg);
    buf[sizeof(buf) - 2] = '\0';
    int len = (int)strlen(buf);
    buf[len] = '\n';
    buf[len + 1] = '\0';
    XBLog_Print(buf);
}

void XBLog_Writef(const char *fmt, ...)
{
    char    buf[XBL_BUF_SIZE];
    va_list args;
    if (!g_verboseLog && !xbl_FormatMayBeCritical(fmt)) return;
    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf) - 2, fmt, args);
    va_end(args);
    buf[sizeof(buf) - 2] = '\0';
    /* Append \n so old callers that omit it still get line breaks. */
    int len = (int)strlen(buf);
    buf[len]     = '\n';
    buf[len + 1] = '\0';
    XBLog_Print(buf);
}
