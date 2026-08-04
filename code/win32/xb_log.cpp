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

#include "xb_log.h"

#if defined(STEFX_SP_HOSTED_MP)
#define STEFX_XB_LOG_FILE "ef_mp_log.txt"
#define STEFX_XB_LOG_TITLE "Star Trek: Elite Force Xbox Holomatch log"
#else
#define STEFX_XB_LOG_FILE "ef_sp_log.txt"
#define STEFX_XB_LOG_TITLE "Star Trek: Elite Force Xbox SP log"
#endif

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
#define XBL_FILE_SOFT_CAP_BYTES (12 * 1024 * 1024)
#define XBL_FLUSH_EVERY_WRITES 32
#define XBL_FLUSH_EVERY_BYTES (64 * 1024)
static HANDLE g_hLogFile     = INVALID_HANDLE_VALUE;
static int    g_logIsNt      = 0;   /* 1 = NtCreateFile handle, 0 = CreateFileA */
static const char *g_logPath = NULL;
static HANDLE g_hMirrorLogFile = INVALID_HANDLE_VALUE;
static const char *g_mirrorLogPath = NULL;
static int g_verboseLog = 0;
static int g_debugStringMirror = 0;
static int g_memoryRingOnly = 0;
static unsigned int g_fileLogBytes = 0;
static unsigned int g_fileLogFlushBytes = 0;
static unsigned int g_fileLogFlushWrites = 0;
static int g_fileLogCapNotified = 0;

extern "C" {
__declspec(dllexport) volatile unsigned int g_SPXBLogMagic = 0x53504546; /* 'SPEF' */
__declspec(dllexport) volatile unsigned int g_SPXBBootPhase = 0x11110001;
__declspec(dllexport) volatile unsigned int g_SPXBLogMirrorPos = 0x11110002;
__declspec(dllexport) volatile unsigned int g_SPXBLogWriteCount = 0x11110003;
__declspec(dllexport) volatile unsigned int g_SPXBHeartbeatMagic = 0x48424653; /* 'SFBH' */
__declspec(dllexport) volatile unsigned int g_SPXBHeartbeatCount = 0x11110004;
__declspec(dllexport) volatile unsigned int g_SPXBHeartbeatFrame = 0x11110005;
__declspec(dllexport) volatile unsigned int g_SPXBHeartbeatRealtime = 0x11110006;
__declspec(dllexport) volatile unsigned int g_SPXBHeartbeatServerTime = 0x11110007;
__declspec(dllexport) volatile unsigned int g_SPXBHeartbeatFps10 = 0x11110008;
__declspec(dllexport) volatile unsigned int g_SPXBUIStateMagic = 0x55495354; /* 'UIST' */
__declspec(dllexport) volatile unsigned int g_SPXBUIStarted = 0x11118001;
__declspec(dllexport) volatile unsigned int g_SPXBUIKeyCatcher = 0x11118002;
__declspec(dllexport) volatile unsigned int g_SPXBUIPauseActive = 0x11118003;
__declspec(dllexport) volatile unsigned int g_SPXBUIQmenuActive = 0x11118004;
__declspec(dllexport) volatile unsigned int g_SPXBUIRefreshCount = 0x11118005;
__declspec(dllexport) volatile unsigned int g_SPXBUIPauseOpenCount = 0x11118006;
__declspec(dllexport) volatile unsigned int g_SPXBUIPauseDrawCount = 0x11118007;
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
__declspec(dllexport) volatile unsigned int g_SPXBNativeUpCalls = 0x11110031;
__declspec(dllexport) volatile unsigned int g_SPXBNativeUpBytes = 0x11110032;
__declspec(dllexport) volatile unsigned int g_SPXBNativePushCalls = 0x11110033;
__declspec(dllexport) volatile unsigned int g_SPXBNativePushBytes = 0x11110034;
__declspec(dllexport) volatile unsigned int g_SPXBNativePushReuse = 0x11110035;
__declspec(dllexport) volatile unsigned int g_SPXBNativePushFallbacks = 0x11110036;
__declspec(dllexport) volatile unsigned int g_SPXBNativeRingCalls = 0x11110037;
__declspec(dllexport) volatile unsigned int g_SPXBNativeRingBytes = 0x11110038;
__declspec(dllexport) volatile unsigned int g_SPXBNativeRingWraps = 0x11110039;
__declspec(dllexport) volatile unsigned int g_SPXBNativeRingFallbacks = 0x1111003A;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawMode = 0x1111003B;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawCount = 0x1111003C;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawSourceVertices = 0x1111003D;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawMaxIndex = 0x1111003E;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawStride = 0x1111003F;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawIndicesPtr = 0x11110040;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawVerticesPtr = 0x11110041;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawPath = 0x11110042;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawShader = 0x11110043;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawVertexOffset = 0x11110044;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawIndexOffset = 0x11110045;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawVertexBytes = 0x11110046;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawIndexBytes = 0x11110047;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawLockFlags = 0x11110048;
__declspec(dllexport) volatile unsigned int g_SPXBNativeDrawMinIndex = 0x11110049;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSplitShader = 0x1111002B;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSplitFog = 0x1111002C;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSplitDlight = 0x1111002D;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSplitEntity = 0x1111002E;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSplitFinal = 0x1111002F;
__declspec(dllexport) volatile unsigned int g_SPXBRenderSplitFlush = 0x11110030;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlotActive = 0x11110060;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot0DrawDelta = 0x11110061;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1DrawDelta = 0x11110062;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot0WorldDelta = 0x11110063;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1WorldDelta = 0x11110064;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot0Cluster = 0x11110065;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1Cluster = 0x11110066;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1WorldRetryDelta = 0x11110074;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1WorldFallback = 0x11110075;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot0MarkedLeaves = 0x11110076;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1MarkedLeaves = 0x11110077;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot0PvsRejected = 0x11110078;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1PvsRejected = 0x11110079;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot0AreaRejected = 0x1111007A;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1AreaRejected = 0x1111007B;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot0RootVis = 0x1111007C;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1RootVis = 0x1111007D;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot0WorldAttempts = 0x1111007E;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1WorldAttempts = 0x1111007F;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot0WorldCulled = 0x11110080;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1WorldCulled = 0x11110081;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot0WorldAlready = 0x11110082;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1WorldAlready = 0x11110083;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot0WorldAdded = 0x11110084;
__declspec(dllexport) volatile unsigned int g_SPXBSplitSlot1WorldAdded = 0x11110085;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2Ent = 0x11110067;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2TraceFrac1000 = 0x11110068;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2ViewX = 0x11110069;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2ViewY = 0x1111006A;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2ViewZ = 0x1111006B;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2PsX = 0x1111006C;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2PsY = 0x1111006D;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2PsZ = 0x1111006E;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2CurX = 0x1111006F;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2CurY = 0x11110070;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2CurZ = 0x11110071;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2AnglesPitch = 0x11110072;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2AnglesYaw = 0x11110073;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2RefdefValid = 0x11110090;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2SceneConsidered = 0x11110091;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2SceneAdded = 0x11110092;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2SceneSelfAdded = 0x11110093;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2ModelEnter = 0x11110094;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2ModelReturn = 0x11110095;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2ModelInfoValid = 0x11110096;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2ModelSubmitted = 0x11110097;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2ModelLegs = 0x11110098;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2ModelTorso = 0x11110099;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2ModelHead = 0x1111009A;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2ModelRenderfx = 0x1111009B;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2RendererRefs = 0x1111009C;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2RendererLastModel = 0x1111009D;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2RendererLastRenderfx = 0x1111009E;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2RendererLastZ = 0x1111009F;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP1Adds = 0x111100AB;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP2Adds = 0x111100AC;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP1Skips = 0x111100AD;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP2Skips = 0x111100AE;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP1Model = 0x111100AF;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP2Model = 0x111100B0;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP1Renderfx = 0x111100B1;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP2Renderfx = 0x111100B2;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP1RendererAdds = 0x111100B3;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP2RendererAdds = 0x111100B4;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP1RendererFiltered = 0x111100B5;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP2RendererFiltered = 0x111100B6;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP1LastSkip = 0x111100B7;
__declspec(dllexport) volatile unsigned int g_SPXBViewWeaponP2LastSkip = 0x111100B8;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponRegWeapon = 0x111100B9;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponRegPathHash = 0x111100BA;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponRegViewModel = 0x111100BB;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponRegWorldModel = 0x111100BC;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponRegHandsModel = 0x111100BD;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponRegFailCode = 0x111100BE;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponModelTraceStage = 0x111100BF;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponModelTracePathHash = 0x111100C0;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponModelTraceDiskLen = 0x111100C1;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponModelTraceDiskSuccess = 0x111100C2;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponModelTraceIdent = 0x111100C3;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponModelTraceVersion = 0x111100C4;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponModelTraceSize = 0x111100C5;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponModelTraceLoaded = 0x111100C6;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponModelTraceHandle = 0x111100C7;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponModelTraceFailCode = 0x111100C8;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponLoadStage = 0x111100C9;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponLoadReadLen = 0x111100CA;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponLoadTypeWeapon = 0x111100CB;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponLoadModelWeapon = 0x111100CC;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponLoadModelHash = 0x111100CD;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponLoadSlot4Hash = 0x111100CE;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponLoadSlot4Ammo = 0x111100CF;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponLoadSlot4First4 = 0x111100D0;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponRegFirst4 = 0x111100D1;
__declspec(dllexport) volatile unsigned int g_SPXBWeaponRegClassHash = 0x111100D2;
__declspec(dllexport) volatile unsigned int g_SPXBSplitCameraMode = 0x111100A0;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP1TraceFrac1000 = 0x111100A1;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP1LocalX1000 = 0x111100A2;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP1LocalY1000 = 0x111100A3;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP1LocalZ1000 = 0x111100A4;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2LocalX1000 = 0x111100A5;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2LocalY1000 = 0x111100A6;
__declspec(dllexport) volatile unsigned int g_SPXBSplitP2LocalZ1000 = 0x111100A7;
__declspec(dllexport) volatile unsigned int g_SPXBSplitLocalDiffX1000 = 0x111100A8;
__declspec(dllexport) volatile unsigned int g_SPXBSplitLocalDiffY1000 = 0x111100A9;
__declspec(dllexport) volatile unsigned int g_SPXBSplitLocalDiffZ1000 = 0x111100AA;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferData = 0x11110031;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferPitch = 0x11110032;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferWidth = 0x11110033;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferHeight = 0x11110034;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferFormat = 0x11110035;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferSize = 0x11110036;
__declspec(dllexport) volatile unsigned int g_SPXBSkyTraceMagic = 0x534B5921; /* 'SKY!' */
__declspec(dllexport) volatile unsigned int g_SPXBSkyOuterPresentMask = 0x11110101;
__declspec(dllexport) volatile unsigned int g_SPXBSkyOuterFallbackMask = 0x11110102;
__declspec(dllexport) volatile unsigned int g_SPXBSkyOuterTexMask = 0x11110103;
__declspec(dllexport) volatile unsigned int g_SPXBSkyOuterDrawMask = 0x11110104;
__declspec(dllexport) volatile unsigned int g_SPXBSkyLastPasses = 0x11110105;
__declspec(dllexport) volatile unsigned int g_SPXBSkyLastSort = 0x11110106;
__declspec(dllexport) volatile unsigned int g_SPXBSkyResolveMagic = 0x534B5952; /* 'SKYR' */
__declspec(dllexport) volatile unsigned int g_SPXBSkyResolveCount = 0x11110111;
__declspec(dllexport) volatile unsigned int g_SPXBSkyResolveShaderNum = 0x11110112;
__declspec(dllexport) volatile unsigned int g_SPXBSkyResolveMapHash = 0x11110113;
__declspec(dllexport) volatile unsigned int g_SPXBSkyResolveResolvedHash = 0x11110114;
__declspec(dllexport) volatile unsigned int g_SPXBSkyResolveSurfaceFlags = 0x11110115;
__declspec(dllexport) volatile unsigned int g_SPXBSkyResolveDefault = 0x11110116;
__declspec(dllexport) volatile unsigned int g_SPXBSkyResolveExplicit = 0x11110117;
__declspec(dllexport) volatile unsigned int g_SPXBSkyResolveHasSky = 0x11110118;
__declspec(dllexport) volatile unsigned int g_SPXBSkyResolvePasses = 0x11110119;
__declspec(dllexport) volatile unsigned int g_SPXBSkyResolveSortX1000 = 0x1111011A;
__declspec(dllexport) volatile unsigned int g_SPXBSkyResolveLightmap0 = 0x1111011B;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanMagic = 0x53484452; /* 'SHDR' */
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanScriptsFound = 0x11110131;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanShadersFound = 0x11110132;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanLoaded = 0x11110133;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanBytes = 0x11110134;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanEntries = 0x11110135;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanSkyLightSeen = 0x11110136;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanJunkSkySeen = 0x11110137;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanManifestActive = 0x11110138;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanManifestReadLen = 0x11110139;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanManifestCount = 0x1111013A;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanRawBytes = 0x1111013B;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanVoyagerListed = 0x1111013C;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanVoyagerReadLen = 0x1111013D;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanVoyagerSkyToken = 0x1111013E;
__declspec(dllexport) volatile unsigned int g_SPXBShaderScanCommonReadLen = 0x1111013F;
__declspec(dllexport) volatile unsigned int g_SPXBShaderLookupMagic = 0x534C4B50; /* 'SLKP' */
__declspec(dllexport) volatile unsigned int g_SPXBShaderLookupCount = 0x11110141;
__declspec(dllexport) volatile unsigned int g_SPXBShaderLookupHash = 0x11110142;
__declspec(dllexport) volatile unsigned int g_SPXBShaderLookupIndexedFound = 0x11110143;
__declspec(dllexport) volatile unsigned int g_SPXBShaderLookupLinearFound = 0x11110144;
__declspec(dllexport) volatile unsigned int g_SPXBShaderLookupEntries = 0x11110145;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetP1Submitted = 0x11110150;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetP2Submitted = 0x11110151;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetP1Attached = 0x11110152;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetP2Attached = 0x11110153;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetP1Model = 0x11110154;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetP2Model = 0x11110155;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetP1Renderfx = 0x11110156;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetP2Renderfx = 0x11110157;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetRendererRefs = 0x11110158;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetRendererSurfaces = 0x11110159;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetRendererFiltered = 0x1111015A;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetRendererLastModel = 0x1111015B;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetRendererLastRenderfx = 0x1111015C;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetRendererLastEnt = 0x1111015D;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetRendererLastFilter = 0x1111015E;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetRendererLastSurfaceModel = 0x1111015F;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetGameP1Ensure = 0x11110160;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetGameP2Ensure = 0x11110161;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetGameP1Slot = 0x11110162;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetGameP2Slot = 0x11110163;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetCgameP1Slot0 = 0x11110164;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetCgameP1Slot1 = 0x11110165;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetCgameP2Slot0 = 0x11110166;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetCgameP2Slot1 = 0x11110167;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetBoltOnLoadLen = 0x11110168;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetBoltOnCount = 0x11110169;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetBoltOnHelmetIndex = 0x1111016A;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetAddAttempts = 0x1111016B;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetAddKnownIndex = 0x1111016C;
__declspec(dllexport) volatile unsigned int g_SPXBHelmetAddFailCode = 0x1111016D;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackTraceMagic = 0x46424B21; /* 'FBK!' */
__declspec(dllexport) volatile unsigned int g_SPXBFallbackStageCount = 0x11110121;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackLastShaderHash = 0x11110122;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackLastImageHash = 0x11110123;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackLastStage = 0x11110124;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackLastPasses = 0x11110125;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackLastFlags = 0x11110126;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackLastTexnum = 0x11110127;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackLastLightmap = 0x11110128;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackLastStateBits = 0x11110129;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackLastIndexes = 0x1111012A;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackLastX1000 = 0x1111012B;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackLastY1000 = 0x1111012C;
__declspec(dllexport) volatile unsigned int g_SPXBFallbackLastZ1000 = 0x1111012D;
__declspec(dllexport) volatile unsigned int g_SPXBCinPhase = 0x11110031;
__declspec(dllexport) volatile unsigned int g_SPXBCinHandle = 0x11110032;
__declspec(dllexport) volatile unsigned int g_SPXBCinStatus = 0x11110033;
__declspec(dllexport) volatile unsigned int g_SPXBCinLoopCount = 0x11110034;
__declspec(dllexport) volatile unsigned int g_SPXBDirectMapStatus = 0x11110035;
__declspec(dllexport) volatile unsigned int g_SPXBDirectMapHash = 0x11110036;
__declspec(dllexport) volatile unsigned int g_SPXBDirectMapQueuedCount = 0x11110037;
__declspec(dllexport) volatile unsigned int g_SPXBGameDetailTraceEnabled = 0;
__declspec(dllexport) volatile unsigned int g_SPXBMiniSoakMagic = 0x4D534F4B; /* 'MSOK' */
__declspec(dllexport) volatile unsigned int g_SPXBMiniSoakStage = 0x11130001;
__declspec(dllexport) volatile unsigned int g_SPXBMiniSoakTransitions = 0x11130002;
__declspec(dllexport) volatile unsigned int g_SPXBMiniSoakActiveMsec = 0x11130003;
__declspec(dllexport) volatile unsigned int g_SPXBMiniSoakFlags = 0x11130004;
__declspec(dllexport) volatile unsigned int g_SPXBSVProbeMagic = 0x53565052; /* 'SVPR' */
__declspec(dllexport) volatile unsigned int g_SPXBSVProbePhase = 0x11120001;
__declspec(dllexport) volatile unsigned int g_SPXBSVProbeSubphase = 0x11120002;
__declspec(dllexport) volatile unsigned int g_SPXBSVProbeA = 0x11120003;
__declspec(dllexport) volatile unsigned int g_SPXBSVProbeB = 0x11120004;
__declspec(dllexport) volatile unsigned int g_SPXBSVProbeC = 0x11120005;
__declspec(dllexport) volatile unsigned int g_SPXBSVProbeD = 0x11120006;
__declspec(dllexport) volatile unsigned int g_SPXBPerfFrameMsec = 0x11120010;
__declspec(dllexport) volatile unsigned int g_SPXBPerfServerMsec = 0x11120011;
__declspec(dllexport) volatile unsigned int g_SPXBPerfClientMsec = 0x11120012;
__declspec(dllexport) volatile unsigned int g_SPXBPerfGameMsec = 0x11120013;
__declspec(dllexport) volatile unsigned int g_SPXBPerfFrontendMsec = 0x11120014;
__declspec(dllexport) volatile unsigned int g_SPXBPerfBackendMsec = 0x11120015;
__declspec(dllexport) volatile unsigned int g_SPXBPerfAudioMsec = 0x11120016;
__declspec(dllexport) volatile unsigned int g_SPXBCameraActive = 0x11120017;
__declspec(dllexport) volatile unsigned int g_SPXBPerfServerTicks = 0x11120018;
__declspec(dllexport) volatile unsigned int g_SPXBPerfServerLastGameMsec = 0x11120019;
__declspec(dllexport) volatile unsigned int g_SPXBPerfServerMaxGameMsec = 0x1112001A;
__declspec(dllexport) volatile unsigned int g_SPXBPerfGamePreMsec = 0x1112001B;
__declspec(dllexport) volatile unsigned int g_SPXBPerfGameEntitiesMsec = 0x1112001C;
__declspec(dllexport) volatile unsigned int g_SPXBPerfGamePostMsec = 0x1112001D;
__declspec(dllexport) volatile unsigned int g_SPXBPerfGameEntitiesVisited = 0x1112001E;
__declspec(dllexport) volatile unsigned int g_SPXBPerfGameMissiles = 0x1112001F;
__declspec(dllexport) volatile unsigned int g_SPXBPerfGameItems = 0x11120020;
__declspec(dllexport) volatile unsigned int g_SPXBPerfGameMovers = 0x11120021;
__declspec(dllexport) volatile unsigned int g_SPXBPerfGameClients = 0x11120022;
__declspec(dllexport) volatile unsigned int g_SPXBPerfGameThinkDue = 0x11120023;
__declspec(dllexport) volatile unsigned int g_SPXBPerfGameScripted = 0x11120024;
__declspec(dllexport) volatile unsigned int g_SPXBPerfGameOther = 0x11120025;
__declspec(dllexport) volatile unsigned int g_SPXBPerfScreenDrawMsec = 0x11120026;
__declspec(dllexport) volatile unsigned int g_SPXBPerfEndFrameMsec = 0x11120027;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderTotalMsec = 0x11120030;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderSetupMsec = 0x11120031;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderMarkLeavesMsec = 0x11120032;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderWorldMsec = 0x11120033;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderPolysMsec = 0x11120034;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderProjectionMsec = 0x11120035;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderEntitiesMsec = 0x11120036;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderSortMsec = 0x11120037;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderDebugMsec = 0x11120038;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderViews = 0x11120039;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderPortals = 0x1112003A;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderDrawSurfs = 0x1112003B;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderRefEntities = 0x1112003C;
__declspec(dllexport) volatile unsigned int g_SPXBPerfRenderLeafs = 0x1112003D;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedCount = 0x11120100;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedEnt = 0x11120101;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedSpawnflags = 0x11120102;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedAnim = 0x11120103;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedLegsModel = 0x11120104;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedTorsoModel = 0x11120105;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedHeadModel = 0x11120106;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedLegsSkin = 0x11120107;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedTorsoSkin = 0x11120108;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedHeadSkin = 0x11120109;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedLegsNameHash = 0x1112010A;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedTorsoNameHash = 0x1112010B;
__declspec(dllexport) volatile unsigned int g_SPXBBorgPluggedHeadNameHash = 0x1112010C;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveCount = 0x11120110;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveEnt = 0x11120111;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveSpawnflags = 0x11120112;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveAnim = 0x11120113;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveLegsModel = 0x11120114;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveTorsoModel = 0x11120115;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveHeadModel = 0x11120116;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveLegsSkin = 0x11120117;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveTorsoSkin = 0x11120118;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveHeadSkin = 0x11120119;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveLegsNameHash = 0x1112011A;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveTorsoNameHash = 0x1112011B;
__declspec(dllexport) volatile unsigned int g_SPXBBorgActiveHeadNameHash = 0x1112011C;
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

static int xbl_budgeted_prefix(const char *msg, const char *prefix, int *budget)
{
    if (!xbl_starts_with(msg, prefix)) return -1;
    if (*budget > 0) {
        --(*budget);
        return 0;
    }
    return 1;
}

static int xbl_IsHighFrequencyDrawNoise(const char *msg)
{
    if (!msg) return 0;
    return strstr(msg, "HUD Draw2D") ||
        strstr(msg, "HUD DrawStats") ||
        strstr(msg, "HUD DrawArmor") ||
        strstr(msg, "HUD DrawHealth") ||
        strstr(msg, "HUD DrawAmmo") ||
        strstr(msg, "HUD Crosshair") ||
        strstr(msg, "HUD InterfaceDraw") ||
        strstr(msg, "RE_StretchPic queued") ||
        strstr(msg, "RB_StretchPic") ||
        strstr(msg, "RB_IterateStagesGeneric overlay state") ||
        strstr(msg, "RB_PrepareOverlayStage") ||
        strstr(msg, "OVERLAY_DRAW_SUBMIT") ||
        strstr(msg, "RB_ForceOverlayD3D skipped") ||
        strstr(msg, "renderer pre-present capture requested") ||
        strstr(msg, "renderer screenshot retry blank") ||
        strstr(msg, "STEFX_FRONTEND_2D_BACKEND") ||
        strstr(msg, "STEFX_CG_UPDATESCREEN presenting EF loadscreen") ||
        strstr(msg, "SPL: SP_DrawSPLoadScreen entry") ||
        strstr(msg, "SPL: drawing EF LCARS load screen") ||
        strstr(msg, "JA: RE_EndFrame: skipping dissolve for direct-map boot") ||
        strstr(msg, "FXLine::Draw") ||
        strstr(msg, "R_AddPolygonSurfaces") ||
        strstr(msg, "RB_SurfacePolychain") ||
        strstr(msg, "R_LerpTag MDR ok") ||
        strstr(msg, "RE_AddPolyToScene shader") ||
        strstr(msg, "CGCam_UpdateFade") ||
        strstr(msg, "CGCam_DrawFades") ||
        strstr(msg, "CG_FillRect2") ||
        strstr(msg, "engine EF DrawStretchPic large") ||
        strstr(msg, "PVS cluster=") ||
        strstr(msg, "worldvis");
}

static int xbl_ShouldDropVerbose(const char *msg)
{
    int budgeted;
    static int s_playerBudget = 48;
    static int s_frameBudget = 192;
    static int s_renderBudget = 48;
    static int s_textureEvidenceBudget = 96;
    static int s_cgameBudget = 48;
    static int s_assetBudget = 48;
    static int s_stefxClientThinkBudget = 96;
    static int s_stefxPmoveBudget = 12;
    static int s_stefxTouchBudget = 64;
    static int s_stefxClipBudget = 8;
    static int s_stefxTriggerBudget = 96;
    static int s_stefxCgBudget = 8;
    static int s_stefxCgInitBudget = 96;
    static int s_stefxModelBudget = 24;
    static int s_efModelBudget = 16;
    static int s_stefxGameFrameBudget = 24;
    static int s_stefxNpcBudget = 24;
    static int s_stefxNpcEnemyBudget = 32;
    static int s_stefxTraceBudget = 8;
    static int s_stefxCinematicGateBudget = 24;
    static int s_stefxAttackProbeBudget = 48;
    static int s_stefxSmokeAimBudget = 64;
    static int s_stefxPmStateBudget = 16;
    static int s_stefxInputBudget = 96;
    static int s_stefxDamageBudget = 64;
    static int s_stefxSyscallBudget = 24;
    static int s_stefxRunFrameBudget = 24;
    static int s_stefxWalkBudget = 4;
    static int s_stefxAirBudget = 4;
    static int s_stefxClientPmBudget = 96;
    static int s_stefxUserMoveBudget = 16;
    static int s_stefxSmokeStageBudget = 20;
    static int s_stefxAudioRuntimeBudget = 160;
    static int s_stefxVoiceTraceBudget = 96;
    static int s_stefxFaceTraceBudget = 96;
    static int s_stefxPlayerRenderBudget = 160;
    static int s_stefxSaveLoadBudget = 64;
    static int s_stefxIntroRuntimeBudget = 160;
    static int s_stefxFrontendBudget = 128;
    static int s_stefxFsConfigBudget = 64;
    static int s_stefxMusicRuntimeBudget = 96;
    static int s_stefxHudBudget = 32;
    static int s_stefxViewWeaponBudget = 96;
    static int s_stefxSmokeCameraBudget = 96;
    static int s_stefxIcarusRuntimeBudget = 24;
    static int s_stefxWeaponProofBudget = 48;
    static int s_stefxProjectileProofBudget = 16;
    static int s_stefxSnapshotEventBudget = 32;
    static int s_stefxCaptureBudget = 64;
    static int s_stefxShaderTraceBudget = 256;
    static int s_stefxSurfaceTraceBudget = 256;
    static int s_stefxSurfaceSubmitTraceBudget = 256;
    static int s_stefxDrawStageTraceBudget = 96;
    static int s_stefxDrawContextTraceBudget = 256;
    static int s_stefxMaterialPathTraceBudget = 256;
    static int s_stefxLightingTraceBudget = 256;
    static int s_stefxTextureProofBudget = 256;
    static int s_stefxAlphaProofBudget = 64;
    static int s_stefxScriptPanelTraceBudget = 512;
    static int s_stefxInputTraceBudget = 256;
    static int s_stefxSplitTraceBudget = 256;
    static int s_stefxSkyTraceBudget = 96;
    static int s_stefxSkyDrawTraceBudget = 256;
    static int s_stefxWorldLoadBudget = 256;
    static int s_stefxMapLoadBudget = 256;
    static int s_stefxNpcStateBudget = 8;
    static int s_stefxActorTraceBudget = 8;
    static int s_stefxLodTraceBudget = 256;
    static int s_stefxThirdPersonTraceBudget = 160;
    static int s_stefxLipTraceBudget = 8;
    static int s_efFastDrawBudget = 24;
    static int s_stefxAnimVisibleBudget = 128;
    static int s_stefxAnimCullBudget = 48;
    static int s_jaComPhaseBudget = 48;
    static int s_jaMainTightBudget = 48;
    static int s_jaEventBudget = 32;
    static int s_jaClFrameBudget = 192;
    static int s_jaClEarlyBudget = 64;
    static int s_jaClEarlyEfBudget = 64;
    static int s_jaScrBudget = 96;
    static int s_jaCgDrawBudget = 64;
    static int s_jaComActiveBudget = 48;

    if (!msg) return 1;
    if (!g_verboseLog) {
        if (strstr(msg, "STEFX: CG_RegisterGraphics entity=")) {
            return 1;
        }
        if (strstr(msg, "FRAME_HEARTBEAT") ||
            strstr(msg, "STEFX_HW_CHECKPOINT") ||
            strstr(msg, "SMOKE_BUTTON") ||
            strstr(msg, "SOAK_COMMAND") ||
            strstr(msg, "SOAKTRACE") ||
            strstr(msg, "SOAKMEM") ||
            strstr(msg, "SV_Map") ||
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
            strstr(msg, "S_LoadSound guard") ||
            strstr(msg, "S_CancelLoadSound") ||
            strstr(msg, "SND_DetachSFXFromChannels") ||
            strstr(msg, "SND_RegisterAudio_LevelLoadEnd") ||
            strstr(msg, "RE_RegisterModels_LevelLoadEnd") ||
            strstr(msg, "RE_RegisterImages_LevelLoadEnd") ||
            strstr(msg, "Sys_Stream") ||
            strstr(msg, "FATAL") ||
            strstr(msg, "ERROR") ||
            strstr(msg, "ERR_FATAL") ||
            strstr(msg, "ERR_DROP") ||
            strstr(msg, "ASSERT") ||
            strstr(msg, "Received Exception") ||
            strstr(msg, "EIP") ||
            strstr(msg, "Out of memory") ||
            strstr(msg, "Z_Malloc():") ||
            strstr(msg, "allocation failed") ||
            strstr(msg, "alloc failed") ||
            strstr(msg, "overBudget") ||
            strstr(msg, "crash")) {
            return 0;
        }
        return 1;
    }

    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF main menu", &s_stefxFrontendBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF LCARS", &s_stefxFrontendBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: FS default.cfg", &s_stefxFsConfigBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_SHADER_", &s_stefxShaderTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_SURFACE_SUBMIT", &s_stefxSurfaceSubmitTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_SURFACE", &s_stefxSurfaceTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_DRAW_STAGE", &s_stefxDrawStageTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_DRAW_CONTEXT", &s_stefxDrawContextTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: POST_STAGE_PATH", &s_stefxMaterialPathTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: TRACE_BRANCH", &s_stefxMaterialPathTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: PLAYER_MODEL", &s_stefxMaterialPathTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "EF: ACTIVE_STAGE", &s_stefxMaterialPathTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_LIGHTGRID", &s_stefxLightingTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_ENTITY_LIGHT", &s_stefxLightingTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_DIFFUSE", &s_stefxLightingTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: DDS_TRACE", &s_stefxTextureProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: R_LoadImage DDS patch", &s_stefxTextureProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: R_LoadImage JPG", &s_stefxTextureProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: R_LoadImage TGA", &s_stefxTextureProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_ALPHA_STATE", &s_stefxAlphaProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_SCRIPT_PANEL", &s_stefxScriptPanelTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_INPUT", &s_stefxInputTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_SPLIT", &s_stefxSplitTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_JUNK", &s_stefxSkyTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_SKY_ITER", &s_stefxSkyDrawTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_SKYBOX", &s_stefxSkyDrawTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_SKY", &s_stefxSkyTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "EF: CM_LoadMap", &s_stefxMapLoadBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CM_LoadMap", &s_stefxMapLoadBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CM raw BSP", &s_stefxMapLoadBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "EF: RE_LoadWorldMap", &s_stefxWorldLoadBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: RE_LoadWorldMap", &s_stefxWorldLoadBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_NPC_STATE", &s_stefxNpcStateBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_ACTOR", &s_stefxActorTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_LOD", &s_stefxLodTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_THIRD_PERSON", &s_stefxThirdPersonTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_LIPTRACE", &s_stefxLipTraceBudget);
    if (budgeted >= 0) return budgeted;

    if (strstr(msg, "STEFX: CG bolt section") ||
        strstr(msg, "STEFX: CG helmet slots") ||
        strstr(msg, "STEFX: CG helmet boltOn") ||
        strstr(msg, "STEFX: helmet command") ||
        strstr(msg, "STEFX: helmet bolton") ||
        strstr(msg, "STEFX_SPLIT_COOP helmet ensure") ||
        strstr(msg, "STEFX_SPLIT_MODEL part=head")) {
        return 0;
    }

    if (strstr(msg, "STEFX: MDR memory") ||
        strstr(msg, "STEFX: RE_RegisterModel MDR hazard preflight fallback") ||
        strstr(msg, "STEFX: RE_RegisterModel MDR default player model cannot fit")) {
        return 0;
    }

    if ((strstr(msg, "borg") || strstr(msg, "Borg")) &&
        (strstr(msg, "STEFX: model disk fetch") ||
         strstr(msg, "STEFX: model disk lower retry") ||
         strstr(msg, "STEFX: RE_RegisterModel") ||
         strstr(msg, "STEFX: R_LoadMDR"))) {
        return 0;
    }

    if (strstr(msg, "STEFX: ICARUS visual") ||
        strstr(msg, "STEFX: EF servercmd st scrolltext") ||
        strstr(msg, "STEFX: Q3_SetScrollTextColor") ||
        strstr(msg, "STEFX: Q3_ScrollText") ||
        strstr(msg, "STEFX: Q3_SetPrecacheFile") ||
        strstr(msg, "STEFX: func_usable script target") ||
        strstr(msg, "STEFX: func_usable script brush") ||
        strstr(msg, "STEFX: ICARUS Camera") ||
        strstr(msg, "STEFX: Q3_Camera") ||
        strstr(msg, "STEFX: CGCam_Init") ||
        strstr(msg, "STEFX: CGCam_Enable") ||
        strstr(msg, "STEFX: CGCam_StartRoff") ||
        strstr(msg, "STEFX: CGCam_RenderScene") ||
        strstr(msg, "STEFX: CG_ScrollText") ||
        strstr(msg, "STEFX: CG_DrawScrollText") ||
        strstr(msg, "STEFX: INTRO_IMAGE") ||
        strstr(msg, "STEFX: INTRO_DRAW") ||
        strstr(msg, "STEFX: RB_ForceOverlayD3D") ||
        strstr(msg, "STEFX: RB_PrepareOverlayStage") ||
        strstr(msg, "STEFX: RB_IterateStagesGeneric overlay state") ||
        strstr(msg, "STEFX: DrawMultitextured overlay state") ||
        strstr(msg, "EF: OVERLAY_DRAW_SUBMIT") ||
        strstr(msg, "EF: FAST_DRAW_SUBMIT") ||
        strstr(msg, "EF: FAST_DRAW_SAMPLE") ||
        strstr(msg, "STEFX: EF CG_SNAPSHOT") ||
        strstr(msg, "STEFX: EF CG_AddPacketEntities bmodel") ||
        strstr(msg, "STEFX: EF CG_BMODEL") ||
        strstr(msg, "STEFX: EF AddRef bridge model") ||
        strstr(msg, "STEFX: renderer AddRef accepted") ||
        strstr(msg, "STEFX: INTRO_MODEL_SURF") ||
        strstr(msg, "JA: R_BMODEL_FORCE_NOCULL") ||
        strstr(msg, "JA: R_BMODEL ent=") ||
        strstr(msg, "STEFX: SV_AddEntToSnapshot bmodel") ||
        strstr(msg, "STEFX: SV_EmitPacketEntities bmodel") ||
        strstr(msg, "STEFX: CL_ParsePacket bmodel") ||
        strstr(msg, "STEFX: engine EF CL_GetSnapshot bmodel")) {
        return 0;
    }

    if (strstr(msg, "JkaGlTexImage2D: converted BGRA32 DDS") ||
        strstr(msg, "JkaGlTexImage2D: small DXT5 DDS using RGBA decode path")) {
        return 0;
    }

    if (strstr(msg, "JA: fakegl texture memory") ||
        strstr(msg, "JA: fakegl registered texture denied") ||
        strstr(msg, "JA: fakegl registered retry succeeded")) {
        return 0;
    }

    if ((strstr(msg, "STEFX: FS loose asset") || strstr(msg, "STEFX: FS stdio fallback") ||
        strstr(msg, "STEFX: FS whole-file") || strstr(msg, "STEFX: WF_Open")) &&
        (strstr(msg, "models\\players") || strstr(msg, "models/players"))) {
        return 0;
    }

    budgeted = xbl_budgeted_prefix(msg, "STEFX: HUD Draw2D", &s_stefxHudBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: HUD DrawStats", &s_stefxHudBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: HUD DrawArmor", &s_stefxHudBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: HUD DrawHealth", &s_stefxHudBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: HUD DrawAmmo", &s_stefxHudBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: HUD Crosshair", &s_stefxHudBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: HUD InterfaceDraw", &s_stefxHudBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: renderer screenshot", &s_stefxCaptureBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: renderer pre-present capture requested", &s_stefxCaptureBudget);
    if (budgeted >= 0) return budgeted;

    if (xbl_IsHighFrequencyDrawNoise(msg)) return 1;

    if (strstr(msg, "FRAME_HEARTBEAT") ||
        strstr(msg, "FATAL") ||
        strstr(msg, "ERROR") ||
        strstr(msg, "Out of memory") ||
        strstr(msg, "texture allocation failures") ||
        strstr(msg, "JA: fakegl framebuffer sample") ||
        strstr(msg, "repaired nonfinite") ||
        strstr(msg, "exit nonfinite") ||
        strstr(msg, "exit invalid") ||
        strstr(msg, "abort lerp guard")) {
        return 0;
    }

    budgeted = xbl_budgeted_prefix(msg, "STEFX: controller", &s_stefxInputBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: direct-map main controller selected", &s_stefxInputBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: direct-map input gate cleared", &s_stefxInputBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: G_Damage", &s_stefxDamageBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: PM_Weapon probe", &s_stefxWeaponProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: PM_AddEvent fire", &s_stefxWeaponProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: ClientEvents fire", &s_stefxWeaponProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: FireWeapon enter", &s_stefxWeaponProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: WP_FireCompression", &s_stefxProjectileProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_FireWeapon", &s_stefxProjectileProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_EntityEvent", &s_stefxProjectileProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: FX_CompressionShot", &s_stefxProjectileProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: FX_CompressionHit", &s_stefxProjectileProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: FX_CompressionExplosion", &s_stefxProjectileProofBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_EVENT", &s_stefxSnapshotEventBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_AddEntToSnapshot", &s_stefxSnapshotEventBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CL_ParsePacket event", &s_stefxSnapshotEventBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_CheckEvents", &s_stefxSnapshotEventBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: ClientThink PM state", &s_stefxPmStateBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CLIENT_PM", &s_stefxClientPmBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: ClientThink", &s_stefxClientThinkBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: adapter ClientThink", &s_stefxClientThinkBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: ClientThink player cinematic gate", &s_stefxCinematicGateBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: ClientThink player cmd", &s_stefxCinematicGateBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: ClientThink player attack probe", &s_stefxAttackProbeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: smoke aim", &s_stefxSmokeAimBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: smoke input applied", &s_stefxSmokeStageBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: smoke ready weapon", &s_stefxSmokeStageBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: smoke stage enemy", &s_stefxSmokeStageBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: smoke wake", &s_stefxSmokeAimBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: smoke unlock", &s_stefxSmokeAimBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: Pmove ", &s_stefxPmoveBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: G_TouchTriggersLerped", &s_stefxTouchBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_ClipMoveToEntities", &s_stefxClipBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: Trigger", &s_stefxTriggerBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_TRIGGER_LINK", &s_stefxTriggerBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_SetBrushModel", &s_stefxTriggerBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_AreaEntities", &s_stefxTriggerBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_LinkEntity bad sector", &s_stefxTriggerBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_LinkEntity bad absbox", &s_stefxTriggerBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_UserMove header", &s_stefxUserMoveBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_UserMove drop", &s_stefxUserMoveBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_UserMove decoded", &s_stefxUserMoveBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_ClientThink", &s_stefxUserMoveBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CL_CreateCmd", &s_stefxUserMoveBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: R_LoadImage intro resolve", &s_stefxCgInitBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: R_CreateImage intro", &s_stefxCgInitBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: INTRO_IMAGE", &s_stefxIntroRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF DrawStretchPic large", &s_stefxIntroRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: G_UseTargets2 target", &s_stefxIntroRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: func_usable broadcast", &s_stefxIntroRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: func_usable", &s_stefxIntroRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF S_STARTSOUND bridge", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF S_STARTLOCALSOUND bridge", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: cg_vmMain enter command=", &s_stefxCgBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: IT_LoadItemParms", &s_stefxCgInitBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: ClearRegisteredItems", &s_stefxCgInitBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: FindItemForWeapon", &s_stefxCgInitBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: Navigator", &s_stefxCgInitBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SpawnEntities", &s_stefxCgInitBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: Spawn entity", &s_stefxCgInitBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: G_CallSpawn", &s_stefxCgInitBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF cgame R_RegisterModel", &s_stefxModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_RegisterClientModelname", &s_stefxModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_RegisterClientRenderInfo", &s_stefxModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: dynamic client model", &s_stefxModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_RegisterGraphics", &s_stefxCgInitBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_GameStateReceived", &s_stefxCgInitBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_AddViewWeapon added", &s_stefxViewWeaponBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_AddViewWeapon skip", &s_stefxViewWeaponBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_ViewWeapon decision", &s_stefxViewWeaponBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_RegisterWeapon", &s_stefxViewWeaponBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG smoke camera disable", &s_stefxSmokeCameraBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG smoke camera gate", &s_stefxSmokeCameraBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG smoke harness", &s_stefxSmokeCameraBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CGCam_Disable", &s_stefxSmokeCameraBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF cgame syscall", &s_stefxSyscallBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: model disk fetch", &s_efModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: model disk lower retry", &s_efModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: FS whole-file", &s_efModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: RE_RegisterModel", &s_efModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: R_LoadMDR", &s_efModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: MDR memory", &s_efModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "EF: RE_RegisterModel accepted MDR placeholder", &s_efModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: R_LoadMDR overbudget placeholder", &s_efModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: G_RunFrame", &s_stefxGameFrameBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: G_RunThink", &s_stefxGameFrameBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: NPC_SetEnemy", &s_stefxNpcEnemyBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: NPC_", &s_stefxNpcBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: QAL attach", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: QAL play", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: Q3_PlaySound", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: G_SoundOnEnt", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: S_RegisterSound", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: S_StartSound", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: S_StartLoadSound", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: S_EndLoadSound", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_VOICE_TRACE", &s_stefxVoiceTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: Xbox voice", &s_stefxVoiceTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF face skin", &s_stefxFaceTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF head skin extensions", &s_stefxFaceTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF CG_Player", &s_stefxPlayerRenderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF CG_AddCEntity", &s_stefxPlayerRenderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF CG_AddPacketEntities", &s_stefxPlayerRenderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF CG_PACKET_PLAYER_DIRECT", &s_stefxPlayerRenderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF CG_SNAPSHOT", &s_stefxPlayerRenderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: EF CG_CENTITY_PLAYER", &s_stefxPlayerRenderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_Player", &s_stefxPlayerRenderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX_SAVELOAD", &s_stefxSaveLoadBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: WAV info", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: loose sound direct fallback", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: loose sound OS read", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: loose sound FS read", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: loose sound read", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: CG_StartMusic", &s_stefxMusicRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: cgame background music syscall", &s_stefxMusicRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: S_StartBackgroundTrack", &s_stefxMusicRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: S_MusicFileExists", &s_stefxMusicRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: Xbox music", &s_stefxMusicRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: Xbox WAV music", &s_stefxMusicRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: Xbox music update", &s_stefxMusicRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "WARNING: Invalid format in music", &s_stefxMusicRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: QAL wave stream", &s_stefxMusicRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: QAL MP3", &s_stefxMusicRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: deferring client custom sounds", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: deferring NPC custom sounds", &s_stefxAudioRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: deferring temp NPC precache model", &s_stefxModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: ICARUS Wait", &s_stefxIcarusRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: ICARUS CallbackCommand", &s_stefxIcarusRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: ICARUS SeqCallback", &s_stefxIcarusRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: ICARUS SetCommand", &s_stefxIcarusRuntimeBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: Trace ", &s_stefxTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: SV_Trace", &s_stefxTraceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: adapter before EF RunFrame", &s_stefxRunFrameBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: adapter after EF RunFrame", &s_stefxRunFrameBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: PM_WalkMove", &s_stefxWalkBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: PM_AirMove", &s_stefxAirBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: COM_PHASE", &s_jaComPhaseBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: MAIN_TIGHT", &s_jaMainTightBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: Com_EventLoop", &s_jaEventBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: Com_Frame: CL_Frame", &s_jaClFrameBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: CL_Frame:", &s_jaClFrameBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: CL_EARLY EF ", &s_jaClEarlyEfBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: CL_EARLY", &s_jaClEarlyBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: SCR_UpdateScreen", &s_jaScrBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: CG_DrawActiveFrame", &s_jaCgDrawBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: fakegl CPU partial", &s_assetBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: COM_ACTIVE", &s_jaComActiveBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: engine EF CL_GetSnapshot", &s_frameBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "EF: ACTIVE_MTEXTURE", &s_textureEvidenceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "EF: CM_LoadMap raw BSP", &s_textureEvidenceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "EF: R_LoadRawLightmaps", &s_textureEvidenceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "EF: RAW_LIGHTMAP_STATS", &s_textureEvidenceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: FORCE_TEXTURE_REBIND", &s_textureEvidenceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "EF: TEX_STAGE_APPLY", &s_textureEvidenceBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: R_AddAnimSurfaces visible", &s_stefxAnimVisibleBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: R_AddAnimSurfaces surface", &s_stefxAnimVisibleBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: R_AddAnimSurfaces cull out", &s_stefxAnimCullBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "STEFX: R_AddAnimSurfaces", &s_renderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "EF: skipping MDR placeholder render", &s_efModelBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: fakegl glBindTexture", &s_renderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: fakegl select texture", &s_renderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: fakegl stage state", &s_renderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "EF: FAST_DRAW_SAMPLE", &s_efFastDrawBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: R_RenderView", &s_renderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: R_GenerateDrawSurfs", &s_renderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: R_AddWorldSurfaces", &s_renderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: VV_R_RecursiveWorldNode", &s_renderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: R_LoadNodesAndLeafs", &s_renderBudget);
    if (budgeted >= 0) return budgeted;
    budgeted = xbl_budgeted_prefix(msg, "JA: R_LoadFaces", &s_renderBudget);
    if (budgeted >= 0) return budgeted;

    if (!g_verboseLog) {
        if (strstr(msg, "FRAME_HEARTBEAT") ||
            strstr(msg, "direct-map boot") ||
            strstr(msg, "Com_Init") ||
            strstr(msg, "Swap_Init") ||
            strstr(msg, "Cbuf_Init") ||
            strstr(msg, "CL_InitRef") ||
            strstr(msg, "R_Register") ||
            strstr(msg, "GLimp_Init") ||
            strstr(msg, "SP_DoLicense") ||
            strstr(msg, "Cmd_Init") ||
            strstr(msg, "Cvar_Init") ||
            strstr(msg, "Com_StartupVariable") ||
            strstr(msg, "CL_InitKeyCommands") ||
            strstr(msg, "Sys_InitFileCodes") ||
            strstr(msg, "Sys_StreamInit") ||
            strstr(msg, "TheGhoul2InfoArray") ||
            strstr(msg, "FS_InitFilesystem") ||
            strstr(msg, "R_InitWorldEffects") ||
            strstr(msg, "exec default.cfg") ||
            strstr(msg, "Cbuf_Execute") ||
            strstr(msg, "Config execution") ||
            strstr(msg, "Com_InitHunkMemory") ||
            strstr(msg, "SE_Init") ||
            strstr(msg, "Sys_Init") ||
            strstr(msg, "Netchan_Init") ||
            strstr(msg, "SV_Init") ||
            strstr(msg, "SV_Map_") ||
            strstr(msg, "SV_SpawnServer") ||
            strstr(msg, "CL_MapLoading") ||
            strstr(msg, "CL_FlushMemory") ||
            strstr(msg, "SPL:") ||
            strstr(msg, "CL_Init") ||
            strstr(msg, "CL_Frame") ||
            strstr(msg, "UI_Init") ||
            strstr(msg, "_UI_Init") ||
            strstr(msg, "Menu_Cache") ||
            strstr(msg, "UI_LoadMenus") ||
            strstr(msg, "AssetCache") ||
            strstr(msg, "UI_BuildPlayerModel_List") ||
            strstr(msg, "String_Init") ||
            strstr(msg, "Menus_CloseAll") ||
            strstr(msg, "CL_StartSound") ||
            strstr(msg, "fully initialized") ||
            strstr(msg, "Server:") ||
            strstr(msg, "SV_InitGameProgs") ||
            strstr(msg, "InitGame") ||
            strstr(msg, "G_AllocGentities") ||
            strstr(msg, "G_SpawnEntitiesFromString") ||
            strstr(msg, "CL_StartHunkUsers") ||
            strstr(msg, "CL_InitCGame") ||
            strstr(msg, "CL_SetCGameTime") ||
            strstr(msg, "Com_EventLoop") ||
            strstr(msg, "COM_PHASE") ||
            strstr(msg, "CIN_PHASE") ||
            strstr(msg, "CIN_RunCinematic") ||
            strstr(msg, "BinkVideo::Start") ||
            strstr(msg, "BinkVideo::Run") ||
            strstr(msg, "STEFX_FRONTEND_2D") ||
            strstr(msg, "STEFX_MENU_") ||
            strstr(msg, "RB_XboxForce2DOverlayState") ||
            strstr(msg, "ICARUS_RUN") ||
            strstr(msg, "CAMERA_") ||
            strstr(msg, "MAIN_TIGHT") ||
            strstr(msg, "fakegl CreateTexture") ||
            strstr(msg, "fakegl CPU partial") ||
            strstr(msg, "fakegl using fallback") ||
            strstr(msg, "CG_DRAW_ACTIVE_FRAME") ||
            strstr(msg, "CG_Init") ||
            strstr(msg, "CG_GameStateReceived") ||
            strstr(msg, "CG_RegisterGraphics") ||
            strstr(msg, "CG_NewClientinfo") ||
            strstr(msg, "VM_Call(CG_INIT)") ||
            strstr(msg, "R_FindShader:") ||
            strstr(msg, "ParseShader:") ||
            strstr(msg, "ParseStage:") ||
            strstr(msg, "FinishShader:") ||
            strstr(msg, "RE_RegisterShaderNoMip:") ||
            strstr(msg, "RE_RegisterShader:") ||
            strstr(msg, "cls.state = CA_PRIMED") ||
        strstr(msg, "cls.state = CA_ACTIVE - GAME IS RUNNING") ||
        strstr(msg, "R_Register forcing r_") ||
            strstr(msg, "R_Register Xbox lighting baseline") ||
            strstr(msg, "XBOX_WORLD_STAGE") ||
            strstr(msg, "XBOX_LIGHTMAP_STATS") ||
            strstr(msg, "R_SetColorMappings") ||
            strstr(msg, "FATAL") ||
            strstr(msg, "ERROR") ||
            strstr(msg, "Out of memory") ||
            strstr(msg, "Received Exception") ||
            strstr(msg, "EIP") ||
            strstr(msg, "Z_Malloc():") ||
            strstr(msg, "texture allocation failures")) {
            return 0;
        }
        if (strstr(msg, "STEFX: MDR memory") ||
            strstr(msg, "STEFX: RE_RegisterModel MDR hazard preflight fallback") ||
            strstr(msg, "STEFX: RE_RegisterModel MDR default player model cannot fit")) {
            return 0;
        }
        return 1;
    }

    if (xbl_starts_with(msg, "JA: CG_Player ") ||
        xbl_starts_with(msg, "STEFX: CG_Player ") ||
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
        xbl_starts_with(msg, "STEFX: CG_AddCEntity") ||
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
		xbl_starts_with(msg, "STEFX_BORG_ALPHA_UPLOAD") ||
		xbl_starts_with(msg, "STEFX_FAKEGL_FORMAT") ||
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
        xbl_starts_with(msg, "JA: CL_StartHunkUsers") ||
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
    if (!g_verboseLog) {
        return strstr(fmt, "STEFX_HW_CHECKPOINT") ||
            strstr(fmt, "FRAME_HEARTBEAT") ||
            strstr(fmt, "SMOKE_BUTTON") ||
            strstr(fmt, "SOAK_COMMAND") ||
            strstr(fmt, "SOAKTRACE") ||
            strstr(fmt, "SOAKMEM") ||
            strstr(fmt, "SV_Map") ||
            strstr(fmt, "direct-map boot") ||
            strstr(fmt, "Server:") ||
            strstr(fmt, "SV_InitGameProgs") ||
            strstr(fmt, "InitGame") ||
            strstr(fmt, "G_AllocGentities") ||
            strstr(fmt, "G_SpawnEntitiesFromString") ||
            strstr(fmt, "CL_Init:") ||
            strstr(fmt, "CL_StartHunkUsers: ready fast path") ||
            strstr(fmt, "CL_InitUI") ||
            strstr(fmt, "CL_InitCGame") ||
            strstr(fmt, "SCR_Init") ||
            strstr(fmt, "UI_Init") ||
            strstr(fmt, "_UI_Init") ||
            strstr(fmt, "UI_SetActiveMenu") ||
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
            strstr(fmt, "cls.state = CA_PRIMED") ||
            strstr(fmt, "cls.state = CA_ACTIVE - GAME IS RUNNING") ||
            strstr(fmt, "S_LoadSound guard") ||
            strstr(fmt, "S_CancelLoadSound") ||
            strstr(fmt, "SND_DetachSFXFromChannels") ||
            strstr(fmt, "SND_RegisterAudio_LevelLoadEnd") ||
            strstr(fmt, "RE_RegisterModels_LevelLoadEnd") ||
            strstr(fmt, "RE_RegisterImages_LevelLoadEnd") ||
            strstr(fmt, "Sys_Stream") ||
            strstr(fmt, "FATAL") ||
            strstr(fmt, "ERROR") ||
            strstr(fmt, "ERR_FATAL") ||
            strstr(fmt, "ERR_DROP") ||
            strstr(fmt, "ASSERT") ||
            strstr(fmt, "Received Exception") ||
            strstr(fmt, "EIP") ||
            strstr(fmt, "Out of memory") ||
            strstr(fmt, "Z_Malloc():") ||
            strstr(fmt, "allocation failed") ||
            strstr(fmt, "alloc failed") ||
            strstr(fmt, "overBudget") ||
            strstr(fmt, "crash");
    }
	if (strstr(fmt, "STEFX_DRAW_STAGE") ||
        strstr(fmt, "STEFX_DRAW_CONTEXT") ||
		strstr(fmt, "STEFX_SHADER_") ||
		strstr(fmt, "STEFX_SURFACE") ||
		strstr(fmt, "STEFX_LIGHTGRID") ||
		strstr(fmt, "STEFX_ENTITY_LIGHT") ||
		strstr(fmt, "STEFX_DIFFUSE") ||
		strstr(fmt, "STEFX: DDS_TRACE") ||
		strstr(fmt, "STEFX_BORG_ALPHA_UPLOAD") ||
		strstr(fmt, "STEFX_ALPHA_STATE") ||
		strstr(fmt, "STEFX_FAKEGL_FORMAT") ||
		strstr(fmt, "STEFX_SCRIPT_PANEL") ||
        strstr(fmt, "STEFX_MENU_INPUT") ||
        strstr(fmt, "STEFX_MENU_") ||
        strstr(fmt, "STEFX_INPUT") ||
        strstr(fmt, "STEFX_SPLIT") ||
        strstr(fmt, "STEFX_LOD") ||
        strstr(fmt, "STEFX_THIRD_PERSON") ||
        strstr(fmt, "STEFX_LIPTRACE") ||
        strstr(fmt, "STEFX_VOICE_TRACE") ||
        strstr(fmt, "STEFX_CIN_DRAW") ||
        strstr(fmt, "STEFX_SAVELOAD") ||
        strstr(fmt, "STEFX_NPC_STATE") ||
        strstr(fmt, "STEFX: SV_ACTOR") ||
        strstr(fmt, "STEFX_JUNK") ||
        strstr(fmt, "STEFX_SKY")) {
        return 1;
    }
    if (strstr(fmt, "STEFX: HUD ") ||
        strstr(fmt, "STEFX: CG_AddViewWeapon") ||
        strstr(fmt, "STEFX: CG_ViewWeapon") ||
        strstr(fmt, "STEFX: CG smoke camera disable") ||
        strstr(fmt, "STEFX: CG smoke camera gate") ||
        strstr(fmt, "STEFX: CG smoke harness") ||
        strstr(fmt, "STEFX: CGCam_Disable")) {
        return 1;
    }
    if (xbl_IsHighFrequencyDrawNoise(fmt)) return 0;
    return strstr(fmt, "FRAME_HEARTBEAT") ||
        strstr(fmt, "EF:") ||
        strstr(fmt, "STEFX:") ||
        strstr(fmt, "direct-map boot") ||
        strstr(fmt, "Com_Init") ||
        strstr(fmt, "Swap_Init") ||
        strstr(fmt, "Cbuf_Init") ||
        strstr(fmt, "CL_InitRef") ||
        strstr(fmt, "R_Register") ||
        strstr(fmt, "GLimp_Init") ||
        strstr(fmt, "SP_DoLicense") ||
        strstr(fmt, "Cmd_Init") ||
        strstr(fmt, "Cvar_Init") ||
        strstr(fmt, "Com_StartupVariable") ||
        strstr(fmt, "CL_InitKeyCommands") ||
        strstr(fmt, "Sys_InitFileCodes") ||
        strstr(fmt, "Sys_StreamInit") ||
        strstr(fmt, "TheGhoul2InfoArray") ||
        strstr(fmt, "FS_InitFilesystem") ||
        strstr(fmt, "R_InitWorldEffects") ||
        strstr(fmt, "exec default.cfg") ||
        strstr(fmt, "Cbuf_Execute") ||
        strstr(fmt, "Config execution") ||
        strstr(fmt, "Com_InitHunkMemory") ||
        strstr(fmt, "SE_Init") ||
        strstr(fmt, "Sys_Init") ||
        strstr(fmt, "Netchan_Init") ||
        strstr(fmt, "SV_Init") ||
        strstr(fmt, "SV_Map_") ||
        strstr(fmt, "SV_SpawnServer") ||
        strstr(fmt, "CL_MapLoading") ||
        strstr(fmt, "CL_FlushMemory") ||
        strstr(fmt, "SPL:") ||
        strstr(fmt, "CL_Init") ||
        strstr(fmt, "CL_Frame") ||
        strstr(fmt, "CL_EARLY") ||
        strstr(fmt, "UI_Init") ||
        strstr(fmt, "_UI_Init") ||
        strstr(fmt, "Menu_Cache") ||
        strstr(fmt, "UI_LoadMenus") ||
        strstr(fmt, "AssetCache") ||
        strstr(fmt, "UI_BuildPlayerModel_List") ||
        strstr(fmt, "String_Init") ||
        strstr(fmt, "Menus_CloseAll") ||
        strstr(fmt, "CL_StartSound") ||
        strstr(fmt, "fully initialized") ||
        strstr(fmt, "Server:") ||
        strstr(fmt, "SV_InitGameProgs") ||
        strstr(fmt, "InitGame") ||
        strstr(fmt, "G_AllocGentities") ||
        strstr(fmt, "G_SpawnEntitiesFromString") ||
        strstr(fmt, "CL_StartHunkUsers") ||
        strstr(fmt, "CL_InitCGame") ||
        strstr(fmt, "CL_SetCGameTime") ||
        strstr(fmt, "Com_EventLoop") ||
        strstr(fmt, "COM_PHASE") ||
        strstr(fmt, "COM_ACTIVE") ||
        strstr(fmt, "CMD_TRACE") ||
        strstr(fmt, "CIN_PHASE") ||
        strstr(fmt, "CIN_RunCinematic") ||
        strstr(fmt, "BinkVideo::Start") ||
        strstr(fmt, "BinkVideo::Run") ||
        strstr(fmt, "STEFX_FRONTEND_2D") ||
        strstr(fmt, "RB_XboxForce2DOverlayState") ||
        strstr(fmt, "MAIN_TIGHT") ||
        strstr(fmt, "fakegl CreateTexture") ||
        strstr(fmt, "fakegl CPU partial") ||
        strstr(fmt, "fakegl using fallback") ||
        strstr(fmt, "CG_Init") ||
        strstr(fmt, "CG_GameStateReceived") ||
        strstr(fmt, "CG_RegisterGraphics") ||
        strstr(fmt, "CG_NewClientinfo") ||
        strstr(fmt, "VM_Call(CG_INIT)") ||
        strstr(fmt, "R_FindShader:") ||
        strstr(fmt, "ParseShader:") ||
        strstr(fmt, "ParseStage:") ||
        strstr(fmt, "FinishShader:") ||
        strstr(fmt, "RE_RegisterShaderNoMip:") ||
        strstr(fmt, "RE_RegisterShader:") ||
        strstr(fmt, "cls.state = CA_PRIMED") ||
        strstr(fmt, "cls.state = CA_ACTIVE - GAME IS RUNNING") ||
        strstr(fmt, "FATAL") ||
        strstr(fmt, "ERROR") ||
        strstr(fmt, "Out of memory") ||
        strstr(fmt, "Received Exception") ||
        strstr(fmt, "EIP") ||
        strstr(fmt, "Z_Malloc():") ||
        strstr(fmt, "texture allocation failures");
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
        0x20 | 0x02);               /* SYNCHRONOUS_IO_NONALERT | NON_DIRECTORY */
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

static int xbl_IsCriticalLogLine(const char *msg)
{
    if (!msg) return 0;
    return strstr(msg, "STEFX_HW_CHECKPOINT") ||
        strstr(msg, "FATAL") ||
        strstr(msg, "ERROR") ||
        strstr(msg, "ERR_FATAL") ||
        strstr(msg, "ERR_DROP") ||
        strstr(msg, "ASSERT") ||
        strstr(msg, "Received Exception") ||
        strstr(msg, "EIP") ||
        strstr(msg, "Out of memory") ||
        strstr(msg, "Z_Malloc():") ||
        strstr(msg, "allocation failed") ||
        strstr(msg, "alloc failed") ||
        strstr(msg, "denied") ||
        strstr(msg, "overBudget") ||
        strstr(msg, "XBLog file cap reached") ||
        strstr(msg, "crash") ||
        strstr(msg, "failed");
}

static int xbl_ShouldFlushWrite(const char *msg, DWORD len)
{
    if (!msg) return 0;
    if (xbl_IsCriticalLogLine(msg)) {
        g_fileLogFlushBytes = 0;
        g_fileLogFlushWrites = 0;
        return 1;
    }
    /*
     * Retail performance runs are commonly ended with a title reset.  Keep
     * the sparse ten-second heartbeat durable so the final steady-state
     * sample is not left only in the process cache.
     */
    if (strstr(msg, "FRAME_HEARTBEAT")) {
        g_fileLogFlushBytes = 0;
        g_fileLogFlushWrites = 0;
        return 1;
    }

    g_fileLogFlushBytes += len;
    ++g_fileLogFlushWrites;

    if (g_fileLogFlushWrites >= XBL_FLUSH_EVERY_WRITES ||
        g_fileLogFlushBytes >= XBL_FLUSH_EVERY_BYTES) {
        g_fileLogFlushBytes = 0;
        g_fileLogFlushWrites = 0;
        return 1;
    }

    return 0;
}

static int xbl_ShouldWriteFileLine(const char **msg, DWORD *len)
{
    static const char s_capMsg[] =
        "STEFX: XBLog file cap reached; file logging now keeps memory mirror and critical lines only\n";

    if (!msg || !*msg || !len) return 0;
    if (xbl_IsCriticalLogLine(*msg)) return 1;

    if (g_fileLogBytes + *len <= XBL_FILE_SOFT_CAP_BYTES) {
        return 1;
    }

    if (!g_fileLogCapNotified) {
        g_fileLogCapNotified = 1;
        *msg = s_capMsg;
        *len = (DWORD)strlen(s_capMsg);
        return 1;
    }

    return 0;
}

static int xbl_IsLogMarkerAt(const char *p)
{
    if (!p) return 0;
    return xbl_starts_with(p, "STEFX:") ||
        xbl_starts_with(p, "STEFX_") ||
        xbl_starts_with(p, "EF:") ||
        xbl_starts_with(p, "JA:");
}

void XBLog_Init(void)
{
    int  i;
    int  memoryRingRequested;
    long status;
    ULARGE_INTEGER dFreeBytes;
    ULARGE_INTEGER dTotalBytes;
    ULARGE_INTEGER dTotalFreeBytes;

    g_SPXBBootPhase = 0x201;
    g_hLogFile = INVALID_HANDLE_VALUE;
    g_logIsNt  = 0;
    g_memoryRingOnly = 0;
    g_logPath  = NULL;
    g_hMirrorLogFile = INVALID_HANDLE_VALUE;
    g_mirrorLogPath = NULL;
    g_verboseLog = SP_XBOX_VERBOSE_RUNTIME_LOGS ? 1 : 0;
    g_fileLogBytes = 0;
    g_fileLogFlushBytes = 0;
    g_fileLogFlushWrites = 0;
    g_fileLogCapNotified = 0;
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
    g_SPXBNativeUpCalls = 0;
    g_SPXBNativeUpBytes = 0;
    g_SPXBNativePushCalls = 0;
    g_SPXBNativePushBytes = 0;
    g_SPXBNativePushReuse = 0;
    g_SPXBNativePushFallbacks = 0;
    g_SPXBNativeRingCalls = 0;
    g_SPXBNativeRingBytes = 0;
    g_SPXBNativeRingWraps = 0;
    g_SPXBNativeRingFallbacks = 0;
    g_SPXBNativeDrawMode = 0;
    g_SPXBNativeDrawCount = 0;
    g_SPXBNativeDrawSourceVertices = 0;
    g_SPXBNativeDrawMaxIndex = 0;
    g_SPXBNativeDrawStride = 0;
    g_SPXBNativeDrawIndicesPtr = 0;
    g_SPXBNativeDrawVerticesPtr = 0;
    g_SPXBNativeDrawPath = 0;
    g_SPXBNativeDrawShader = 0;
    g_SPXBNativeDrawVertexOffset = 0;
    g_SPXBNativeDrawIndexOffset = 0;
    g_SPXBNativeDrawVertexBytes = 0;
    g_SPXBNativeDrawIndexBytes = 0;
    g_SPXBNativeDrawLockFlags = 0;
    g_SPXBNativeDrawMinIndex = 0;
    g_SPXBRenderSplitShader = 0;
    g_SPXBRenderSplitFog = 0;
    g_SPXBRenderSplitDlight = 0;
    g_SPXBRenderSplitEntity = 0;
    g_SPXBRenderSplitFinal = 0;
    g_SPXBRenderSplitFlush = 0;
    g_SPXBSplitSlotActive = 0;
    g_SPXBSplitSlot0DrawDelta = 0;
    g_SPXBSplitSlot1DrawDelta = 0;
    g_SPXBSplitSlot0WorldDelta = 0;
    g_SPXBSplitSlot1WorldDelta = 0;
    g_SPXBSplitSlot0Cluster = 0;
    g_SPXBSplitSlot1Cluster = 0;
    g_SPXBSplitSlot1WorldRetryDelta = 0;
    g_SPXBSplitSlot1WorldFallback = 0;
    g_SPXBSplitSlot0MarkedLeaves = 0;
    g_SPXBSplitSlot1MarkedLeaves = 0;
    g_SPXBSplitSlot0PvsRejected = 0;
    g_SPXBSplitSlot1PvsRejected = 0;
    g_SPXBSplitSlot0AreaRejected = 0;
    g_SPXBSplitSlot1AreaRejected = 0;
    g_SPXBSplitSlot0RootVis = 0;
    g_SPXBSplitSlot1RootVis = 0;
    g_SPXBSplitSlot0WorldAttempts = 0;
    g_SPXBSplitSlot1WorldAttempts = 0;
    g_SPXBSplitSlot0WorldCulled = 0;
    g_SPXBSplitSlot1WorldCulled = 0;
    g_SPXBSplitSlot0WorldAlready = 0;
    g_SPXBSplitSlot1WorldAlready = 0;
    g_SPXBSplitSlot0WorldAdded = 0;
    g_SPXBSplitSlot1WorldAdded = 0;
    g_SPXBSplitP2Ent = 0;
    g_SPXBSplitP2TraceFrac1000 = 0;
    g_SPXBSplitP2ViewX = 0;
    g_SPXBSplitP2ViewY = 0;
    g_SPXBSplitP2ViewZ = 0;
    g_SPXBSplitP2PsX = 0;
    g_SPXBSplitP2PsY = 0;
    g_SPXBSplitP2PsZ = 0;
    g_SPXBSplitP2CurX = 0;
    g_SPXBSplitP2CurY = 0;
    g_SPXBSplitP2CurZ = 0;
    g_SPXBSplitP2AnglesPitch = 0;
    g_SPXBSplitP2AnglesYaw = 0;
    g_SPXBSplitP2RefdefValid = 0;
    g_SPXBSplitP2SceneConsidered = 0;
    g_SPXBSplitP2SceneAdded = 0;
    g_SPXBSplitP2SceneSelfAdded = 0;
    g_SPXBSplitP2ModelEnter = 0;
    g_SPXBSplitP2ModelReturn = 0;
    g_SPXBSplitP2ModelInfoValid = 0;
    g_SPXBSplitP2ModelSubmitted = 0;
    g_SPXBSplitP2ModelLegs = 0;
    g_SPXBSplitP2ModelTorso = 0;
    g_SPXBSplitP2ModelHead = 0;
    g_SPXBSplitP2ModelRenderfx = 0;
    g_SPXBSplitP2RendererRefs = 0;
    g_SPXBSplitP2RendererLastModel = 0;
    g_SPXBSplitP2RendererLastRenderfx = 0;
    g_SPXBSplitP2RendererLastZ = 0;
    g_SPXBViewWeaponP1Adds = 0;
    g_SPXBViewWeaponP2Adds = 0;
    g_SPXBViewWeaponP1Skips = 0;
    g_SPXBViewWeaponP2Skips = 0;
    g_SPXBViewWeaponP1Model = 0;
    g_SPXBViewWeaponP2Model = 0;
    g_SPXBViewWeaponP1Renderfx = 0;
    g_SPXBViewWeaponP2Renderfx = 0;
    g_SPXBViewWeaponP1RendererAdds = 0;
    g_SPXBViewWeaponP2RendererAdds = 0;
    g_SPXBViewWeaponP1RendererFiltered = 0;
    g_SPXBViewWeaponP2RendererFiltered = 0;
    g_SPXBViewWeaponP1LastSkip = 0;
    g_SPXBViewWeaponP2LastSkip = 0;
    g_SPXBWeaponRegWeapon = 0;
    g_SPXBWeaponRegPathHash = 0;
    g_SPXBWeaponRegViewModel = 0;
    g_SPXBWeaponRegWorldModel = 0;
    g_SPXBWeaponRegHandsModel = 0;
    g_SPXBWeaponRegFailCode = 0;
    g_SPXBWeaponModelTraceStage = 0;
    g_SPXBWeaponModelTracePathHash = 0;
    g_SPXBWeaponModelTraceDiskLen = 0;
    g_SPXBWeaponModelTraceDiskSuccess = 0;
    g_SPXBWeaponModelTraceIdent = 0;
    g_SPXBWeaponModelTraceVersion = 0;
    g_SPXBWeaponModelTraceSize = 0;
    g_SPXBWeaponModelTraceLoaded = 0;
    g_SPXBWeaponModelTraceHandle = 0;
    g_SPXBWeaponModelTraceFailCode = 0;
    g_SPXBWeaponLoadStage = 0;
    g_SPXBWeaponLoadReadLen = 0;
    g_SPXBWeaponLoadTypeWeapon = 0;
    g_SPXBWeaponLoadModelWeapon = 0;
    g_SPXBWeaponLoadModelHash = 0;
    g_SPXBWeaponLoadSlot4Hash = 0;
    g_SPXBWeaponLoadSlot4Ammo = 0;
    g_SPXBWeaponLoadSlot4First4 = 0;
    g_SPXBWeaponRegFirst4 = 0;
    g_SPXBWeaponRegClassHash = 0;
    g_SPXBSplitCameraMode = 0;
    g_SPXBSplitP1TraceFrac1000 = 0;
    g_SPXBSplitP1LocalX1000 = 0;
    g_SPXBSplitP1LocalY1000 = 0;
    g_SPXBSplitP1LocalZ1000 = 0;
    g_SPXBSplitP2LocalX1000 = 0;
    g_SPXBSplitP2LocalY1000 = 0;
    g_SPXBSplitP2LocalZ1000 = 0;
    g_SPXBSplitLocalDiffX1000 = 0;
    g_SPXBSplitLocalDiffY1000 = 0;
    g_SPXBSplitLocalDiffZ1000 = 0;
    g_SPXBFramebufferData = 0;
    g_SPXBFramebufferPitch = 0;
    g_SPXBFramebufferWidth = 0;
    g_SPXBFramebufferHeight = 0;
    g_SPXBFramebufferFormat = 0;
    g_SPXBFramebufferSize = 0;
    g_SPXBSkyTraceMagic = 0x534B5921; /* 'SKY!' */
    g_SPXBSkyOuterPresentMask = 0;
    g_SPXBSkyOuterFallbackMask = 0;
    g_SPXBSkyOuterTexMask = 0;
    g_SPXBSkyOuterDrawMask = 0;
    g_SPXBSkyLastPasses = 0;
    g_SPXBSkyLastSort = 0;
    g_SPXBSkyResolveMagic = 0x534B5952; /* 'SKYR' */
    g_SPXBSkyResolveCount = 0;
    g_SPXBSkyResolveShaderNum = 0;
    g_SPXBSkyResolveMapHash = 0;
    g_SPXBSkyResolveResolvedHash = 0;
    g_SPXBSkyResolveSurfaceFlags = 0;
    g_SPXBSkyResolveDefault = 0;
    g_SPXBSkyResolveExplicit = 0;
    g_SPXBSkyResolveHasSky = 0;
    g_SPXBSkyResolvePasses = 0;
    g_SPXBSkyResolveSortX1000 = 0;
    g_SPXBSkyResolveLightmap0 = 0;
    g_SPXBShaderScanMagic = 0x53484452; /* 'SHDR' */
    g_SPXBShaderScanScriptsFound = 0;
    g_SPXBShaderScanShadersFound = 0;
    g_SPXBShaderScanLoaded = 0;
    g_SPXBShaderScanBytes = 0;
    g_SPXBShaderScanEntries = 0;
    g_SPXBShaderScanSkyLightSeen = 0;
    g_SPXBShaderScanJunkSkySeen = 0;
    g_SPXBShaderScanManifestActive = 0;
    g_SPXBShaderScanManifestReadLen = 0;
    g_SPXBShaderScanManifestCount = 0;
    g_SPXBShaderScanRawBytes = 0;
    g_SPXBShaderScanVoyagerListed = 0;
    g_SPXBShaderScanVoyagerReadLen = 0;
    g_SPXBShaderScanVoyagerSkyToken = 0;
    g_SPXBShaderScanCommonReadLen = 0;
    g_SPXBShaderLookupMagic = 0x534C4B50; /* 'SLKP' */
    g_SPXBShaderLookupCount = 0;
    g_SPXBShaderLookupHash = 0;
    g_SPXBShaderLookupIndexedFound = 0;
    g_SPXBShaderLookupLinearFound = 0;
    g_SPXBShaderLookupEntries = 0;
    g_SPXBHelmetP1Submitted = 0;
    g_SPXBHelmetP2Submitted = 0;
    g_SPXBHelmetP1Attached = 0;
    g_SPXBHelmetP2Attached = 0;
    g_SPXBHelmetP1Model = 0;
    g_SPXBHelmetP2Model = 0;
    g_SPXBHelmetP1Renderfx = 0;
    g_SPXBHelmetP2Renderfx = 0;
    g_SPXBHelmetRendererRefs = 0;
    g_SPXBHelmetRendererSurfaces = 0;
    g_SPXBHelmetRendererFiltered = 0;
    g_SPXBHelmetRendererLastModel = 0;
    g_SPXBHelmetRendererLastRenderfx = 0;
    g_SPXBHelmetRendererLastEnt = 0;
    g_SPXBHelmetRendererLastFilter = 0;
    g_SPXBHelmetRendererLastSurfaceModel = 0;
    g_SPXBHelmetGameP1Ensure = 0;
    g_SPXBHelmetGameP2Ensure = 0;
    g_SPXBHelmetGameP1Slot = 0;
    g_SPXBHelmetGameP2Slot = 0;
    g_SPXBHelmetCgameP1Slot0 = 0;
    g_SPXBHelmetCgameP1Slot1 = 0;
    g_SPXBHelmetCgameP2Slot0 = 0;
    g_SPXBHelmetCgameP2Slot1 = 0;
    g_SPXBHelmetBoltOnLoadLen = 0;
    g_SPXBHelmetBoltOnCount = 0;
    g_SPXBHelmetBoltOnHelmetIndex = 0;
    g_SPXBHelmetAddAttempts = 0;
    g_SPXBHelmetAddKnownIndex = 0;
    g_SPXBHelmetAddFailCode = 0;
    g_SPXBFallbackTraceMagic = 0x46424B21; /* 'FBK!' */
    g_SPXBFallbackStageCount = 0;
    g_SPXBFallbackLastShaderHash = 0;
    g_SPXBFallbackLastImageHash = 0;
    g_SPXBFallbackLastStage = 0;
    g_SPXBFallbackLastPasses = 0;
    g_SPXBFallbackLastFlags = 0;
    g_SPXBFallbackLastTexnum = 0;
    g_SPXBFallbackLastLightmap = 0;
    g_SPXBFallbackLastStateBits = 0;
    g_SPXBFallbackLastIndexes = 0;
    g_SPXBFallbackLastX1000 = 0;
    g_SPXBFallbackLastY1000 = 0;
    g_SPXBFallbackLastZ1000 = 0;
    g_SPXBCinPhase = 0;
    g_SPXBCinHandle = 0;
    g_SPXBCinStatus = 0;
    g_SPXBCinLoopCount = 0;
    g_SPXBDirectMapStatus = 0;
    g_SPXBDirectMapHash = 0;
    g_SPXBDirectMapQueuedCount = 0;
    g_SPXBGameDetailTraceEnabled = 0;
    g_SPXBMiniSoakStage = 0;
    g_SPXBMiniSoakTransitions = 0;
    g_SPXBMiniSoakActiveMsec = 0;
    g_SPXBMiniSoakFlags = 0;
    g_SPXBSVProbeMagic = 0x53565052; /* 'SVPR' */
    g_SPXBSVProbePhase = 0;
    g_SPXBSVProbeSubphase = 0;
    g_SPXBSVProbeA = 0;
    g_SPXBSVProbeB = 0;
    g_SPXBSVProbeC = 0;
    g_SPXBSVProbeD = 0;
    g_SPXBPerfFrameMsec = 0;
    g_SPXBPerfServerMsec = 0;
    g_SPXBPerfClientMsec = 0;
    g_SPXBPerfGameMsec = 0;
    g_SPXBPerfFrontendMsec = 0;
    g_SPXBPerfBackendMsec = 0;
    g_SPXBPerfAudioMsec = 0;
    g_SPXBPerfServerTicks = 0;
    g_SPXBPerfServerLastGameMsec = 0;
    g_SPXBPerfServerMaxGameMsec = 0;
    g_SPXBPerfGamePreMsec = 0;
    g_SPXBPerfGameEntitiesMsec = 0;
    g_SPXBPerfGamePostMsec = 0;
    g_SPXBPerfGameEntitiesVisited = 0;
    g_SPXBPerfGameMissiles = 0;
    g_SPXBPerfGameItems = 0;
    g_SPXBPerfGameMovers = 0;
    g_SPXBPerfGameClients = 0;
    g_SPXBPerfGameThinkDue = 0;
    g_SPXBPerfGameScripted = 0;
    g_SPXBPerfGameOther = 0;
    g_SPXBPerfScreenDrawMsec = 0;
    g_SPXBPerfEndFrameMsec = 0;
    g_SPXBPerfRenderTotalMsec = 0;
    g_SPXBPerfRenderSetupMsec = 0;
    g_SPXBPerfRenderMarkLeavesMsec = 0;
    g_SPXBPerfRenderWorldMsec = 0;
    g_SPXBPerfRenderPolysMsec = 0;
    g_SPXBPerfRenderProjectionMsec = 0;
    g_SPXBPerfRenderEntitiesMsec = 0;
    g_SPXBPerfRenderSortMsec = 0;
    g_SPXBPerfRenderDebugMsec = 0;
    g_SPXBPerfRenderViews = 0;
    g_SPXBPerfRenderPortals = 0;
    g_SPXBPerfRenderDrawSurfs = 0;
    g_SPXBPerfRenderRefEntities = 0;
    g_SPXBPerfRenderLeafs = 0;
    g_SPXBCameraActive = 0;
    g_SPXBBorgPluggedCount = 0;
    g_SPXBBorgPluggedEnt = 0;
    g_SPXBBorgPluggedSpawnflags = 0;
    g_SPXBBorgPluggedAnim = 0;
    g_SPXBBorgPluggedLegsModel = 0;
    g_SPXBBorgPluggedTorsoModel = 0;
    g_SPXBBorgPluggedHeadModel = 0;
    g_SPXBBorgPluggedLegsSkin = 0;
    g_SPXBBorgPluggedTorsoSkin = 0;
    g_SPXBBorgPluggedHeadSkin = 0;
    g_SPXBBorgPluggedLegsNameHash = 0;
    g_SPXBBorgPluggedTorsoNameHash = 0;
    g_SPXBBorgPluggedHeadNameHash = 0;
    g_SPXBBorgActiveCount = 0;
    g_SPXBBorgActiveEnt = 0;
    g_SPXBBorgActiveSpawnflags = 0;
    g_SPXBBorgActiveAnim = 0;
    g_SPXBBorgActiveLegsModel = 0;
    g_SPXBBorgActiveTorsoModel = 0;
    g_SPXBBorgActiveHeadModel = 0;
    g_SPXBBorgActiveLegsSkin = 0;
    g_SPXBBorgActiveTorsoSkin = 0;
    g_SPXBBorgActiveHeadSkin = 0;
    g_SPXBBorgActiveLegsNameHash = 0;
    g_SPXBBorgActiveTorsoNameHash = 0;
    g_SPXBBorgActiveHeadNameHash = 0;
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
    g_SPXBBootPhase = 0x202;

    /*
     * The XEMU harness polls the exported memory ring directly.  Avoid
     * synchronous title-volume/HDD logging in that mode: XEMU can block a
     * FlushFileBuffers/NtFlushBuffersFile request while the DVD is servicing
     * a map load, which stalls the game thread.  Retail and normal emulator
     * launches still use the D: then E: file paths below.
     */
    g_SPXBBootPhase = 0x203;
    memoryRingRequested = xbl_FileExists("D:\\stefx_xemu_memory_log.txt");
    g_SPXBBootPhase = 0x204;
    if (!memoryRingRequested) {
        memoryRingRequested = xbl_FileExists("E:\\stefx_xemu_memory_log.txt");
    }
    g_SPXBBootPhase = 0x205;
    if (memoryRingRequested) {
        g_logPath = "memory-ring";
        g_mirrorLogPath = NULL;
        g_memoryRingOnly = 1;
        XBLog_WriteRingMarker("STEFX: XEMU memory-ring logging active");
        g_SPXBBootPhase = 0x206;
        return;
    }

    /*
     * CXBX-R and softmod launches expose the writable title directory as D:.
     * XEMU and disc launches expose D: as read-only DVD media; use the HDD
     * fallback there. Keep exactly one live handle so map transitions never
     * wait on two independent synchronous flushes.
     */
    g_SPXBBootPhase = 0x207;
    if (GetDiskFreeSpaceExA("D:\\", &dFreeBytes, &dTotalBytes, &dTotalFreeBytes) &&
        dFreeBytes.QuadPart != 0) {
        g_hLogFile = CreateFileA("D:\\" STEFX_XB_LOG_FILE, FILE_APPEND_DATA, FILE_SHARE_READ,
            NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_hLogFile != INVALID_HANDLE_VALUE) {
            SetFilePointer(g_hLogFile, 0, NULL, FILE_END);
            g_logIsNt = 0;
            g_logPath = "D:\\" STEFX_XB_LOG_FILE;
            XBL("=== " STEFX_XB_LOG_TITLE " ===\n");
            return;
        }
    }

    /*
     * Strategy 1: NtCreateFile to raw device paths (retail hw + CXBX-R).
     * Use FILE_OPEN_IF (3) + FILE_APPEND_DATA so we append to the file that
     * XBLog_PreCRTProbe already created, preserving the "precrt_ok" line.
     * Falls back to FILE_OVERWRITE_IF if the file doesn't exist yet.
     */
    {
        g_SPXBBootPhase = 0x208;
        static const char *ntPaths[] = {
            "\\Device\\Harddisk0\\Partition1\\" STEFX_XB_LOG_FILE,   /* E:\ */
            "\\Device\\Harddisk0\\Partition6\\" STEFX_XB_LOG_FILE,   /* F:\ */
            "\\Device\\Harddisk0\\Partition7\\" STEFX_XB_LOG_FILE,   /* G:\ */
            NULL
        };
        for (i = 0; ntPaths[i]; ++i) {
            XBL_STR  name;
            XBL_OA   oa;
            XBL_IOSB iosb;
            name.Buffer        = (char*)ntPaths[i];
            name.Length        = (unsigned short)strlen(ntPaths[i]);
            name.MaximumLength = name.Length + 1;
            oa.RootDirectory   = NULL;
            oa.ObjectName      = &name;
            oa.Attributes      = 0x40;
            /* FILE_APPEND_DATA | SYNCHRONIZE, FILE_OPEN_IF (3) = open existing or create */
            status = NtCreateFile(&g_hLogFile,
                0x04 | 0x00100000,   /* FILE_APPEND_DATA | SYNCHRONIZE */
                &oa, &iosb, NULL,
                FILE_ATTRIBUTE_NORMAL, 0,
                3,                   /* FILE_OPEN_IF */
                0x20 | 0x02);
            if (status >= 0) {
                g_logIsNt = 1;
                g_logPath = ntPaths[i];
                XBL("=== " STEFX_XB_LOG_TITLE " ===\n");
                return;
            }
        }
    }

    /* Strategy 2: CreateFileA with drive letters — append if exists, create if not */
    {
        g_SPXBBootPhase = 0x209;
        static const char *caPaths[] = {
            "E:\\" STEFX_XB_LOG_FILE,
            "T:\\" STEFX_XB_LOG_FILE,
            STEFX_XB_LOG_FILE,
            NULL
        };
        for (i = 0; caPaths[i]; ++i) {
            g_hLogFile = CreateFileA(caPaths[i], FILE_APPEND_DATA, FILE_SHARE_READ,
                NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (g_hLogFile != INVALID_HANDLE_VALUE) {
                /* Seek to end so we append after the precrt line */
                SetFilePointer(g_hLogFile, 0, NULL, FILE_END);
                g_logIsNt = 0;
                g_logPath = caPaths[i];
                XBL("=== " STEFX_XB_LOG_TITLE " ===\n");
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
    if (g_debugStringMirror) {
        OutputDebugStringA(msg);
    }
    len = (DWORD)strlen(msg);
    xbl_MirrorWrite(msg, len);
    if (g_memoryRingOnly) {
        return;
    }
    if (!xbl_ShouldWriteFileLine(&msg, &len)) {
        return;
    }
    g_fileLogBytes += len;
    const int forceFlush = xbl_ShouldFlushWrite(msg, len);
    if (g_hMirrorLogFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(g_hMirrorLogFile, msg, len, &written, NULL);
        if (forceFlush) xbl_FlushHandle(g_hMirrorLogFile, 0);
    }
    if (g_hLogFile == INVALID_HANDLE_VALUE) return;
    if (g_logIsNt) {
        XBL_IOSB iosb;
        NtWriteFile(g_hLogFile, NULL, NULL, NULL, &iosb, (void*)msg, len, NULL);
        if (forceFlush) xbl_FlushHandle(g_hLogFile, 1);
    } else {
        DWORD written;
        WriteFile(g_hLogFile, msg, len, &written, NULL);
        if (forceFlush) xbl_FlushHandle(g_hLogFile, 0);
    }
}

static void XBLog_PrintFilteredRecords(const char *msg)
{
    const char *start;
    const char *p;

    if (!msg) return;
    if (g_verboseLog) {
        XBLog_Print(msg);
        return;
    }

    start = msg;
    p = msg;
    while (*p) {
        if (p > start && (*p == '\n' || *p == '\r' || xbl_IsLogMarkerAt(p))) {
            char chunk[XBL_BUF_SIZE];
            int len = (int)(p - start);
            if (len > 0) {
                while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == '\r')) {
                    --len;
                }
                if (len > 0) {
                    if (len > (int)sizeof(chunk) - 2) {
                        len = (int)sizeof(chunk) - 2;
                    }
                    memcpy(chunk, start, len);
                    chunk[len++] = '\n';
                    chunk[len] = '\0';
                    if (!xbl_ShouldDropVerbose(chunk)) {
                        XBLog_Print(chunk);
                    }
                }
            }
            while (*p == '\n' || *p == '\r') {
                ++p;
            }
            start = p;
            continue;
        }
        ++p;
    }

    if (p > start) {
        char chunk[XBL_BUF_SIZE];
        int len = (int)(p - start);
        while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == '\r')) {
            --len;
        }
        if (len > 0) {
            if (len > (int)sizeof(chunk) - 2) {
                len = (int)sizeof(chunk) - 2;
            }
            memcpy(chunk, start, len);
            chunk[len++] = '\n';
            chunk[len] = '\0';
            if (!xbl_ShouldDropVerbose(chunk)) {
                XBLog_Print(chunk);
            }
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
    if (g_memoryRingOnly) {
        XBLog_WriteRingMarker(buf);
        return;
    }
    XBLog_PrintFilteredRecords(buf);
}

void XBLog_WriteCritical(const char *msg)
{
    char buf[XBL_BUF_SIZE];
    int len;
    if (!msg) return;
    _snprintf(buf, sizeof(buf) - 2, "%s", msg);
    buf[sizeof(buf) - 2] = '\0';
    len = (int)strlen(buf);
    buf[len] = '\n';
    buf[len + 1] = '\0';
    XBLog_Print(buf);
}

void XBLog_WriteCriticalf(const char *fmt, ...)
{
    char buf[XBL_BUF_SIZE];
    va_list args;
    if (!fmt) return;
    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf) - 2, fmt, args);
    va_end(args);
    buf[sizeof(buf) - 2] = '\0';
    if (!g_verboseLog &&
        strstr(buf, "STEFX_MODEL_BOOT:") &&
        !xbl_IsCriticalLogLine(buf) &&
        !strstr(buf, "cannot fit")) {
        return;
    }
    XBLog_WriteCritical(buf);
}

void XBLog_WriteRingMarker(const char *msg)
{
    char buf[XBL_BUF_SIZE];
    int len;
    if (!msg) return;
    _snprintf(buf, sizeof(buf) - 2, "%s", msg);
    buf[sizeof(buf) - 2] = '\0';
    len = (int)strlen(buf);
    buf[len] = '\n';
    buf[len + 1] = '\0';
    xbl_MirrorWrite(buf, len + 1);
}

const char *XBLog_GetPath(void)
{
    return g_logPath;
}

/*
 * XBLog_PreCRTProbe — called from ASM _WinMainCRTStartup BEFORE _mainCRTStartup.
 * Creates the target-specific log and writes the first line.
 * No C runtime, no heap, no globals — pure NT syscalls only.
 * XBLog_Init() later re-opens the same file in append mode and continues writing.
 * If only "precrt_ok" appears in the log, a static ctor is crashing before main().
 */
extern "C" void XBLog_PreCRTProbe(void)
{
    g_SPXBBootPhase = 1;
    static const char path[] = "\\Device\\Harddisk0\\Partition1\\" STEFX_XB_LOG_FILE;
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
    static const char path[] = "\\Device\\Harddisk0\\Partition1\\" STEFX_XB_LOG_FILE;
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
    if (!g_verboseLog && !xbl_FormatMayBeCritical(msg)) return;
    if (g_memoryRingOnly) {
        XBLog_WriteRingMarker(msg);
        return;
    }
    _snprintf(buf, sizeof(buf) - 2, "%s", msg);
    buf[sizeof(buf) - 2] = '\0';
    int len = (int)strlen(buf);
    buf[len] = '\n';
    buf[len + 1] = '\0';
    XBLog_PrintFilteredRecords(buf);
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
    if (g_memoryRingOnly) {
        XBLog_WriteRingMarker(buf);
        return;
    }
    /* Append \n so old callers that omit it still get line breaks. */
    int len = (int)strlen(buf);
    buf[len]     = '\n';
    buf[len + 1] = '\0';
    XBLog_PrintFilteredRecords(buf);
}
