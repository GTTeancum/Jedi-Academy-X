#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../client/client.h"
#include "win_local.h"
#include "resource.h"
#include <float.h>
#include <stdio.h>
#include <malloc.h>
#include "../game/g_public.h"
#include <xonline.h>
#include "glw_win_dx8.h"
#include "../qcommon/xb_settings.h"

#ifdef _XBOX
#include <IO.h>
#include <xtl.h>
#include "../win32/xb_log.h"
#define NEWDECL __cdecl
#if defined(_MSC_VER) && !defined(_M_PPC)
extern "C" void *_ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)
#endif
extern "C" volatile unsigned int g_SPXBBootPhase;
extern "C" volatile unsigned int g_SPXBMainLoopCount;
extern "C" volatile unsigned int g_SPXBClsState;
extern "C" volatile unsigned int g_SPXBPhaseLast;
extern "C" volatile unsigned int g_SPXBComSubphase;
extern "C" volatile unsigned int g_SPXBComFrameCount;

/* NT kernel prototypes for the early main() probe (file-scope required by VC71) */
extern "C" long __stdcall NtCreateFile(void**, unsigned long, void*, void*,
    void*, unsigned long, unsigned long, unsigned long, unsigned long);
extern "C" long __stdcall NtWriteFile(void*, void*, void*, void*, void*,
    void*, unsigned long, void*);
extern "C" long __stdcall NtClose(void*);

#ifndef FINAL_BUILD
#include "dbg_console_xbox.h"
#endif

#endif

int gLaunchController = 0;

extern int eventHead, eventTail;
extern sysEvent_t eventQue[MAX_QUED_EVENTS];
extern byte		sys_packetReceived[MAX_MSGLEN];

#ifdef _XBOX
bool g_xboxDirectMapBootQueued = false;

static bool Sys_XboxDirectMapRequested(void)
{
	char startupMap[MAX_QPATH];
	startupMap[0] = '\0';

	FILE *startupMapFile = fopen("D:\\ja_sp_level.txt", "r");
	if (!startupMapFile)
	{
		return false;
	}

	if (fgets(startupMap, sizeof(startupMap), startupMapFile))
	{
		startupMap[strcspn(startupMap, "\r\n\t ")] = '\0';
	}
	fclose(startupMapFile);

	return startupMap[0] != '\0';
}

bool Sys_IsDirectMapBoot(void)
{
	return g_xboxDirectMapBootQueued || Sys_XboxDirectMapRequested();
}

static bool Sys_XboxQueueDirectMapBoot(void)
{
	char startupMap[MAX_QPATH];
	startupMap[0] = '\0';

	FILE *startupMapFile = fopen("D:\\ja_sp_level.txt", "r");
	if (startupMapFile)
	{
		if (fgets(startupMap, sizeof(startupMap), startupMapFile))
		{
			startupMap[strcspn(startupMap, "\r\n\t ")] = '\0';
		}
		fclose(startupMapFile);
	}

	FILE *startupCommandFile = fopen("D:\\ja_sp_commands.txt", "r");
	if (startupCommandFile)
	{
		char commandLine[1024];
		while (fgets(commandLine, sizeof(commandLine), startupCommandFile))
		{
			commandLine[strcspn(commandLine, "\r\n")] = '\0';
			if (commandLine[0])
			{
				XBLF("JA: direct-map boot: queue startup command '%s'", commandLine);
				Cbuf_AddText(commandLine);
				Cbuf_AddText("\n");
			}
		}
		fclose(startupCommandFile);
	}

	if (!startupMap[0])
	{
		XBL("JA: direct-map boot: no ja_sp_level.txt map, normal boot\n");
		return false;
	}

	XBLF("JA: direct-map boot: queue devmap %s before first Com_Frame", startupMap);
	Cbuf_AddText(va("devmap %s\n", startupMap));
	g_xboxDirectMapBootQueued = true;
	return true;
}
#endif

void *NEWDECL operator new(size_t size)
{
#if defined(_XBOX)
	void *retaddr = NULL;
#if defined(_MSC_VER) && !defined(_M_PPC)
	retaddr = _ReturnAddress();
#endif
	if (size >= (16 * 1024 * 1024))
	{
		char msg[160];
		_snprintf(msg, sizeof(msg) - 1, "EFALLOC: operator new size=%u phase=%u caller=%p\n", (unsigned int)size, (unsigned int)g_SPXBBootPhase, retaddr);
		msg[sizeof(msg) - 1] = '\0';
		XBLog_Print(msg);
	}
#endif
	return Z_Malloc(size, TAG_NEWDEL, qfalse);
}


void *NEWDECL operator new[](size_t size)
{
#if defined(_XBOX)
	void *retaddr = NULL;
#if defined(_MSC_VER) && !defined(_M_PPC)
	retaddr = _ReturnAddress();
#endif
	if (size >= (16 * 1024 * 1024))
	{
		char msg[160];
		_snprintf(msg, sizeof(msg) - 1, "EFALLOC: operator new[] size=%u phase=%u caller=%p\n", (unsigned int)size, (unsigned int)g_SPXBBootPhase, retaddr);
		msg[sizeof(msg) - 1] = '\0';
		XBLog_Print(msg);
	}
#endif
	return Z_Malloc(size, TAG_NEWDEL, qfalse);
}


void NEWDECL operator delete[](void *ptr)
{
	if (ptr)
		Z_Free(ptr);
}


void NEWDECL operator delete(void *ptr)
{
	if (ptr)
		Z_Free(ptr);
}

/*
================
Sys_Init

Called after the common systems (cvars, files, etc)
are initialized
================
*/
extern void Sys_In_Restart_f(void);
extern void Sys_Net_Restart_f(void);
void Sys_Init( void ) 
{
	Cmd_AddCommand ("in_restart", Sys_In_Restart_f);
	Cmd_AddCommand ("net_restart", Sys_Net_Restart_f);
}

#ifdef XBOX_DEMO
// When we're a demo, we're not running from D:\, so we need some hacks:
char demoBasePath[64];
#endif

char *Sys_Cwd( void )
{
	static char cwd[MAX_OSPATH];

#ifdef XBOX_DEMO
	strcpy( cwd, demoBasePath );
#else
	strcpy(cwd, "d:");
#endif

	return cwd;
}

/*
=================
Sys_In_Restart_f

Restart the input subsystem
=================
*/
void Sys_In_Restart_f( void ) {
}



/*
=============
Sys_Error

Show the early console as an error dialog
=============
*/
void Sys_Error( const char *error, ... ) {
        va_list                argptr;
        char                text[256];

        va_start (argptr, error);
        vsprintf (text, error, argptr);
        va_end (argptr);

#ifdef _GAMECUBE
        printf(text);
#else
        OutputDebugString(text);
#endif
#ifdef _XBOX
        XBLF("JA: Sys_Error exit: %s", text);
#endif

#if 0 // UN-PORT
        Com_ShutdownZoneMemory();
        Com_ShutdownHunkMemory();
#endif

        exit (1);
}


/*
================
Sys_GetEvent

================
*/
sysEvent_t Sys_GetEvent( void ) {
        sysEvent_t        ev;

        // return if we have data
        if ( eventHead > eventTail ) {
                eventTail++;
                return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
        }

        // check for network packets
        msg_t                netmsg;
        MSG_Init( &netmsg, sys_packetReceived, sizeof( sys_packetReceived ) );

        // return if we have data
        if ( eventHead > eventTail ) {
                eventTail++;
                return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
        }

        // create an empty event to return
        memset( &ev, 0, sizeof( ev ) );
        ev.evTime = Sys_Milliseconds();

        return ev;
}


void Sys_Print(const char *msg)
{
#ifdef _GAMECUBE
	printf(msg);
#elif defined(_XBOX)
	XBLog_Write(msg);
#else
	OutputDebugString(msg);
#endif
}

/*
==============
Sys_Log
==============
*/
void Sys_Log( const char *file, const char *msg ) {
	Sys_Log(file, msg, strlen(msg), strchr(msg, '\n') ? true : false);
}

/*
==============
Sys_Log
==============
*/
void Sys_Log( const char *file, const void *buffer, int size, bool flush ) {
#ifndef FINAL_BUILD
	static bool unableToLog = false;

	// Once we've failed to write to the log files once, bail out.
	// This lets us put release builds on DVD without recompiling.
	if (unableToLog)
		return;

	struct FileInfo
	{
		char name[MAX_QPATH];
		FILE *handle;
	};

	const int LOG_MAX_FILES = 4;
	static FileInfo files[LOG_MAX_FILES];
	static int num_files = 0;

	FileInfo* cur = NULL;
	for (int f = 0; f < num_files; ++f)
	{
		if (!stricmp(file, files[f].name))
		{
			cur = &files[f];
			break;
		}
	}

	if (cur == NULL)
	{
		if (num_files >= LOG_MAX_FILES)
		{
			Sys_Print("Too many log files!\n");
			return;
		}

		cur = &files[num_files++];
		strcpy(cur->name, file);
		cur->handle = NULL;
	}

	char fullname[MAX_QPATH];
	sprintf(fullname, "d:\\%s", cur->name);
	if (!cur->handle)
	{
		cur->handle = fopen(fullname, "wb");
		if (cur->handle == NULL)
		{
			Sys_Print("Unable to open log file!\n");
			unableToLog = true;
			return;
		}
	}

	if (size == 1) fputc(*(char*)buffer, cur->handle);
	else fwrite(buffer, size, 1, cur->handle);

	if (flush)
	{
		fflush(cur->handle);
	}
#endif
}

#ifdef _XBOX
HANDLE Sys_FileStreamMutex = INVALID_HANDLE_VALUE;
#endif

void Win_Init(void)
{
#ifdef _XBOX
	XBL("Win_Init: CreateMutex...\n");
	Sys_FileStreamMutex = CreateMutex(NULL, FALSE, NULL);
	XBLF("Win_Init: mutex handle=0x%x\n", (unsigned)Sys_FileStreamMutex);
#endif
}

/*
=====================

XBE SWITCHING SUPPORT

=====================
*/

#ifdef XBOX_DEMO
// Filled in when we're launched from CDX
LD_DEMO demoLaunchData;
bool demoLaunchDataValid = false;

// If we were launched by the user, or someone has pressed a key
// then the timer should not count down during movies/loading:
bool demoTimerAlways = false;
int demoTimer = 0;
#endif

// Takes a filename (relative to ".") and pre-pends the right path. This
// is needed for demos where the game won't be running from D:
const char *Sys_RemapPath( const char *filename )
{
#ifdef XBOX_DEMO
	return va( "%s\\%s", demoBasePath, filename );
#else
	return va( "D:\\%s", filename );
#endif
}

// Despite what you may think, this function actually just returns
// a value telling you if you *should* quick-boot -- ie skip intro
// cinematics and such. Only supposed to XGetLaunchInfo once per
// boot, so we cache the results.
//
// This function always gets called at startup, so we also use it
// to retrieve the launch info for a demo
#define LAUNCH_MAGIC "J3D1"
bool Sys_QuickStart( void )
{
	static bool retVal = false;
	static bool initialized = false;

	if( initialized )
		return retVal;

	initialized = true;

#ifdef XBOX_DEMO
	// Default to D:\, this gets replaced below if CDX started us
	strcpy( demoBasePath, "D:" );

	gLaunchController = 0;	// Irrelevant in demo
	retVal = false;			// We never come from MP (eg), so always false
	DWORD launchType;

	DWORD result = XGetLaunchInfo( &launchType, (LAUNCH_DATA *) &demoLaunchData );
	if( result == ERROR_SUCCESS && launchType == LDT_TITLE )
	{
		// We were launched by CDX:
		demoLaunchDataValid = true;

		// How we were launched affects timer behavior:
		demoTimerAlways = (demoLaunchData.dwRunmode == XLDEMO_RUNMODE_KIOSKMODE);

		// Need to re-map paths, as D:\\ doesn't work now:
		Q_strncpyz( demoBasePath, demoLaunchData.szLaunchedXBE, sizeof(demoBasePath), qtrue );

		// Find our executable name in the path, and truncate the string there:
		char *pXBE = strstr( demoBasePath, "\\default.xbe" );
		if( !pXBE )
			Com_Error( ERR_FATAL, "Error re-mapping D drive\n" );
		*pXBE = 0;

		// Fix the video path:
		extern char XBOX_VIDEO_PATH[64];
		strcpy( XBOX_VIDEO_PATH, demoBasePath );
		strcat( XBOX_VIDEO_PATH, "\\base\\video\\" );
	}

	return retVal;
#else
	DWORD launchType;
	LAUNCH_DATA ld;

	if( (XGetLaunchInfo( &launchType, &ld ) != ERROR_SUCCESS) ||
		(launchType != LDT_TITLE) ||
		strcmp((const char *)&ld.Data[1], LAUNCH_MAGIC) )
		return (retVal = false);
	
	gLaunchController = ld.Data[0];

	// Magic number to disable settings/saving
	if( ld.Data[5] == 0x42 )
		Settings.Disable();

	return (retVal = true);
#endif
}

extern int IN_GetMainController(void);
extern void SP_DrawMPLoadScreen(void);

// Takes an extra parameter so that the accepted invite code can pass
// in the XONLINE_ACCEPTED_INVITE to be copied into launch data.
//
// For the demo, the only valid reason is "demo", and pData should be NULL
void Sys_Reboot( const char *reason, const void *pData )
{
#ifdef XBOX_DEMO
	if( pData || !demoLaunchDataValid || Q_stricmp( reason, "demo" ) != 0 )
		Com_Error( ERR_DROP, "Invalid Sys_Reboot call\n" );

	// Kill off the sound and stream threads:
	S_Shutdown();
	extern void Sys_StreamShutdown(void);
	Sys_StreamShutdown();

	// Now return to CDX
	XLaunchNewImage( demoLaunchData.szLauncherXBE, (LAUNCH_DATA *) &demoLaunchData );
	// Should never return!
#else
	LAUNCH_DATA ld;
	const char *path = NULL;
	int controller;

	memset( &ld, 0, sizeof(ld) );
	controller = IN_GetMainController();
	ld.Data[0] = (byte) controller;
	Com_Printf("\tController %d Passed\n",controller); 

	if (!Q_stricmp(reason, "multiplayer"))
	{
		path = "d:\\jamp.xbe";
		SP_DrawMPLoadScreen();

		// Set a magic number if saving is disabled
		if( Settings.IsDisabled() )
			ld.Data[1] = 0x42;

		// Flag that there is no invite in the launch data:
		ld.Data[2] = 0;
	}
	else if (!Q_stricmp(reason, "invite"))
	{
		path = "d:\\jamp.xbe";
		SP_DrawMPLoadScreen();

		// Set a magic number if saving is disabled
		if( Settings.IsDisabled() )
			ld.Data[1] = 0x42;

		// Flag that we're including an invite with the launch data:
		ld.Data[2] = 1;

		memcpy( &ld.Data[3], pData, sizeof(XONLINE_ACCEPTED_GAMEINVITE) );
	}
	else
	{
		Com_Error( ERR_FATAL, "Unknown reboot code %s\n", reason );
	}

	// Title should not be doing ANYTHING in the background.
	// Shutting down sound ensures that the sound thread is gone
	S_Shutdown();
	// Similarly, kill off the streaming thread
	extern void Sys_StreamShutdown(void);
	Sys_StreamShutdown();

	// Keep the loading screen up while we reboot!
	glw_state->device->PersistDisplay();

	XLaunchNewImage(path, &ld);

	// This function should not return!
	Com_Error( ERR_FATAL, "ERROR: XLaunchNewImage returned\n" );
#endif
}

static XONLINE_ACCEPTED_GAMEINVITE acceptedGameInvite;

// Used to check for the presence of an accepted invite for our game on the HD.
// Can only return true on the FIRST call.
bool Sys_InviteExists( void )
{
#ifdef XBOX_DEMO
	return false;
#else
	static bool initialized = false;
	if( initialized )
		return false;
	initialized = true;

	// If we just came from the MP XBE, don't auto-reboot again. That's just silly.
	if( Sys_QuickStart() )
		return false;

	// Try to retrieve an invitation from the HD (this requires that we start XOnline):
	XOnlineStartup( NULL );
	HRESULT hr = XOnlineFriendsGetAcceptedGameInvite( &acceptedGameInvite );
	XOnlineCleanup();

	return (hr == S_OK);
#endif
}

// Reboot to MP to join the game that we have an invite for:
void Sys_JoinInvite( void )
{
	// Aha. Well, XTL seems to blow this away now, so we need to copy it to launch_data. Bleh.
	Sys_Reboot( "invite", &acceptedGameInvite );
	// Never returns!
}

#ifdef XBOX_DEMO
// Timer code for the demo:
static int lastTime = 0;
static bool demoTimerPaused = false;

// Notify the demo timer of a keypress, which resets the timer, and ensures
// that the timer no longer runs during FMV/loading (if it was before)
void Demo_TimerKeypress( void )
{
	demoTimerAlways = false;

	// Reset the timer:
	demoTimer = demoLaunchData.dwTimeout;

	// Stamp the diff-timer
	lastTime = Sys_Milliseconds();
}

// Update the timer, and check to see if we should reboot:
void Demo_TimerUpdate( void )
{
	// Handle first call correctly
	if( !lastTime )
	{
		demoTimer = demoLaunchData.dwTimeout;
		lastTime = Sys_Milliseconds();
	}

	int newTime = Sys_Milliseconds();
	int diffTime = newTime - lastTime;
	lastTime = newTime;

	// If the timer isn't supposed to run, and we're "paused", don't update:
	extern bool in_camera;
	if( !demoTimerAlways && (demoTimerPaused || in_camera) )
		return;

	// If we weren't even launched by CDX in the first place, or were given
	// a zero timeout, do nothing:
	if( !demoLaunchDataValid || !demoLaunchData.dwTimeout )
		return;

	// Time ran out?
	if( demoTimer < diffTime )
		Sys_Reboot( "demo", NULL );

	demoTimer -= diffTime;
}

// Pause/unpause the demo timer when entering/exiting non-interactive state:
void Demo_TimerPause( bool bPaused )
{
	// Always stamp the timer right before we change state:
	Demo_TimerUpdate();

	demoTimerPaused = bPaused;
}

#endif

/*
==================
WinMain

==================
*/
#if defined (_XBOX)
int __cdecl main()
#elif defined (_GAMECUBE)
int main(int argc, char* argv[])
#endif
{
//	Z_SetFreeOSMem();

#ifdef _XBOX
	g_SPXBBootPhase = 2;
	/* Raw NT probe — appends "main_reached" to ef_sp_log.txt before XBLog_Init.
	   If ef_sp_log.txt only has "precrt_ok", a static ctor crashed before main(). */
	{
		struct { unsigned short Len, MaxLen; char *Buf; } oname;
		struct { void *Root; void *Name; unsigned long Attr; } oa;
		struct { union { long Status; void *Ptr; }; unsigned long Info; } iosb;
		static const char path[] = "\\Device\\Harddisk0\\Partition1\\ef_sp_log.txt";
		static const char data[] = "main_reached\n";
		void *h = (void*)-1;
		oname.Buf = (char*)path; oname.Len = sizeof(path)-1; oname.MaxLen = sizeof(path);
		oa.Root = 0; oa.Name = &oname; oa.Attr = 0x40;
		/* FILE_OPEN_IF (3) + FILE_APPEND_DATA — append after precrt_ok line */
		if (NtCreateFile(&h, 0x00100004, &oa, &iosb, 0, 0x80, 0, 3, 0x62) >= 0) {
			NtWriteFile(h, 0, 0, 0, &iosb, (void*)data, sizeof(data)-1, 0);
			NtClose(h);
		}
	}
#endif

	XBLog_Init();
	XBLF("Log: %s\n", XBLog_GetPath() ? XBLog_GetPath() : "(none)");
	XBL("main() entered\n");

#ifdef _XBOX
	/* Plan-B (OpenJKDF2 1:1): XInitDevices MUST come before Direct3D
	 * init.  Per OpenJKDF2's main_xbox.c comment (lines 58-64): "XDK
	 * requirement: XInitDevices must be called before
	 * Direct3D_CreateDevice (ordering required by the USB host
	 * controller initialisation sequence on NV2A hardware)."
	 *
	 * JKA's original IN_Init calls XInitDevices AFTER GLW_Init has
	 * already done CreateDevice — wrong order — which causes
	 * FakeSwapBuffers / Present to hang in the stability loop (USB
	 * host controller never properly armed, GPU never advances).
	 *
	 * Match OpenJKDF2's preallocation: 4 gamepads + 8 memory units. */
	{
		XDEVICE_PREALLOC_TYPE xdpt[2];
		xdpt[0].DeviceType      = XDEVICE_TYPE_GAMEPAD;
		xdpt[0].dwPreallocCount = 4;
		xdpt[1].DeviceType      = XDEVICE_TYPE_MEMORY_UNIT;
		xdpt[1].dwPreallocCount = 8;
		XBL("Plan-B: calling XInitDevices BEFORE D3D init\n");
		XInitDevices(2, xdpt);
		XBL("Plan-B: XInitDevices done\n");
	}
	/* Mark in win_input_xbox.cpp's static flag so IN_Init doesn't
	 * call XInitDevices again (the XDK only allows ONE call). */
	{
		extern bool g_XInitDevicesAlreadyCalled;
		g_XInitDevicesAlreadyCalled = true;
	}
#endif

	/* Plan-B: Direct3D_SetPushBufferSize removed.
	 *
	 * Original justification (plan v1-v2 era) was to fix a CDevice::Init
	 * KickOff/HwGet spin-loop hang when JKA's pre-Plan-B renderer created
	 * its own D3D8 device.  Under Plan-B fakegl owns the device and
	 * fakeglx.cpp:1098 calls m_pD3D->SetPushBufferSize(768K, 128K) ITSELF
	 * inside InitD3DX (between Direct3DCreate8 and CreateDevice — the
	 * correct ordering).
	 *
	 * Calling SetPushBufferSize HERE (before fakegl runs) sets it once,
	 * then fakegl sets it AGAIN with different values inside InitD3DX.
	 * Two competing push-buffer configs leaves the GPU in a state where
	 * Present's push-buffer flush never completes — observed as the
	 * SDT: glEndFrame hang on CXBX-R 2026-05-16 22:33.  OpenJKDF2 does
	 * NOT call SetPushBufferSize from main (only fakegl does internally);
	 * matching that pattern. */
	XBL("Plan-B: SetPushBufferSize delegated to fakegl InitD3DX\n");

	// get the initial time base
	Sys_Milliseconds();

	// Fetch game settings early — path remapping required before renderer start.
	Sys_QuickStart();
	XBL("Sys_QuickStart done\n");

	Win_Init();
	XBL("Win_Init done\n");

	XBL("EF: before Com_Init\n");
	Com_Init( "" );
	XBL("Com_Init done\n");

	extern void G_AllocGentities( void );
	G_AllocGentities();
	XBL("G_AllocGentities done\n");

#ifdef _XBOX
	Sys_XboxQueueDirectMapBoot();
#endif

	// Run one frame to finish loading (calls CL_StartHunkUsers).
	IN_Frame();
	Com_Frame();
	XBL("First frame done\n");

	// Copy planet bink videos to Z: drive.
	extern void Sys_BinkCopyInit(void);
#ifdef _XBOX
	if (Sys_XboxDirectMapRequested())
	{
		XBL("Sys_BinkCopyInit skipped for direct-map boot\n");
	}
	else if (FILE *autoSmokeMarker = fopen("D:\\ja_sp_autosmoke.txt", "r"))
	{
		fclose(autoSmokeMarker);
		XBL("Sys_BinkCopyInit skipped for SP autosmoke boot\n");
	}
	else
#endif
	{
		XBL("Sys_BinkCopyInit begin\n");
		Sys_BinkCopyInit();
		XBL("Sys_BinkCopyInit done\n");
	}

	XBL("Entering main game loop\n");

	// main game loop
	int xboxMainLoopCount = 0;
	while( 1 ) {
		const bool xboxTraceMainLoop = (xboxMainLoopCount < 8);
		const bool xboxTraceMainLoopLate = (cls.state == CA_ACTIVE && cls.framecount >= 8 && cls.framecount <= 40);
#ifdef _XBOX
		g_SPXBMainLoopCount = (unsigned int)xboxMainLoopCount;
		g_SPXBClsState = (unsigned int)cls.state;
		g_SPXBPhaseLast = 0x4D414931; /* 'MAI1' */
#endif
		if (xboxTraceMainLoopLate) XBLF("JA: CL_EARLY MAIN_TIGHT loop=%d top clsFrame=%d state=%d realtime=%d cframe=%u", xboxMainLoopCount, cls.framecount, (int)cls.state, cls.realtime, g_SPXBComFrameCount);
		if (xboxTraceMainLoop) XBLF("JA: MAIN_TIGHT loop=%d before IN_Frame", xboxMainLoopCount);
		if (xboxTraceMainLoopLate) XBLF("JA: CL_EARLY MAIN_TIGHT loop=%d before IN_Frame clsFrame=%d", xboxMainLoopCount, cls.framecount);
		IN_Frame();
		if (xboxTraceMainLoopLate) XBLF("JA: CL_EARLY MAIN_TIGHT loop=%d after IN_Frame clsFrame=%d", xboxMainLoopCount, cls.framecount);
		if (xboxTraceMainLoop) XBLF("JA: MAIN_TIGHT loop=%d after IN_Frame", xboxMainLoopCount);
	#ifdef _XBOX
		g_SPXBPhaseLast = 0x4D434631; /* 'MCF1' */
		g_SPXBComSubphase = 0;
	#endif
		if (xboxTraceMainLoop) XBLF("JA: MAIN_TIGHT loop=%d before Com_Frame phase=%08x sub=%u cframe=%u state=%d sv=%d", xboxMainLoopCount, g_SPXBPhaseLast, g_SPXBComSubphase, g_SPXBComFrameCount, (int)cls.state, com_sv_running ? com_sv_running->integer : -1);
		if (xboxTraceMainLoopLate) XBLF("JA: CL_EARLY MAIN_TIGHT loop=%d before Com_Frame phase=%08x sub=%u cframe=%u clsFrame=%d state=%d sv=%d", xboxMainLoopCount, g_SPXBPhaseLast, g_SPXBComSubphase, g_SPXBComFrameCount, cls.framecount, (int)cls.state, com_sv_running ? com_sv_running->integer : -1);
		Com_Frame();
#ifdef _XBOX
		g_SPXBClsState = (unsigned int)cls.state;
		g_SPXBPhaseLast = 0x4D434632; /* 'MCF2' */
#endif
		if (xboxTraceMainLoopLate) XBLF("JA: CL_EARLY MAIN_TIGHT loop=%d after Com_Frame phase=%08x sub=%u cframe=%u clsFrame=%d state=%d sv=%d", xboxMainLoopCount, g_SPXBPhaseLast, g_SPXBComSubphase, g_SPXBComFrameCount, cls.framecount, (int)cls.state, com_sv_running ? com_sv_running->integer : -1);
		if (xboxTraceMainLoop) XBLF("JA: MAIN_TIGHT loop=%d after Com_Frame phase=%08x sub=%u cframe=%u state=%d sv=%d", xboxMainLoopCount, g_SPXBPhaseLast, g_SPXBComSubphase, g_SPXBComFrameCount, (int)cls.state, com_sv_running ? com_sv_running->integer : -1);
#ifdef _XBOX
		const DWORD xboxMainLoopYieldMs = (cls.state >= CA_ACTIVE) ? 0 : 1;
		if (xboxTraceMainLoop) XBLF("JA: MAIN_TIGHT loop=%d before first yield ms=%lu state=%d", xboxMainLoopCount, xboxMainLoopYieldMs, (int)cls.state);
		if (xboxTraceMainLoopLate) XBLF("JA: CL_EARLY MAIN_TIGHT loop=%d before first yield ms=%lu state=%d clsFrame=%d", xboxMainLoopCount, xboxMainLoopYieldMs, (int)cls.state, cls.framecount);
		Sleep(xboxMainLoopYieldMs);
		if (xboxTraceMainLoopLate) XBLF("JA: CL_EARLY MAIN_TIGHT loop=%d after first yield clsFrame=%d", xboxMainLoopCount, cls.framecount);
		if (xboxTraceMainLoop) XBLF("JA: MAIN_TIGHT loop=%d after first yield", xboxMainLoopCount);
#else
		Sleep(1);
#endif

		// Poll debug console for new commands
#ifndef FINAL_BUILD
		if (xboxTraceMainLoop) XBLF("JA: MAIN_TIGHT loop=%d before DebugConsoleHandleCommands", xboxMainLoopCount);
		if (xboxTraceMainLoopLate) XBLF("JA: CL_EARLY MAIN_TIGHT loop=%d before DebugConsoleHandleCommands clsFrame=%d", xboxMainLoopCount, cls.framecount);
		DebugConsoleHandleCommands();
		if (xboxTraceMainLoopLate) XBLF("JA: CL_EARLY MAIN_TIGHT loop=%d after DebugConsoleHandleCommands clsFrame=%d", xboxMainLoopCount, cls.framecount);
		if (xboxTraceMainLoop) XBLF("JA: MAIN_TIGHT loop=%d after DebugConsoleHandleCommands", xboxMainLoopCount);
#endif
#ifdef _XBOX
		if (xboxTraceMainLoop) XBLF("JA: MAIN_TIGHT loop=%d before second yield ms=%lu state=%d", xboxMainLoopCount, xboxMainLoopYieldMs, (int)cls.state);
		if (xboxTraceMainLoopLate) XBLF("JA: CL_EARLY MAIN_TIGHT loop=%d before second yield ms=%lu state=%d clsFrame=%d", xboxMainLoopCount, xboxMainLoopYieldMs, (int)cls.state, cls.framecount);
		Sleep(xboxMainLoopYieldMs);
		if (xboxTraceMainLoopLate) XBLF("JA: CL_EARLY MAIN_TIGHT loop=%d after second yield clsFrame=%d", xboxMainLoopCount, cls.framecount);
		if (xboxTraceMainLoop) XBLF("JA: MAIN_TIGHT loop=%d after second yield", xboxMainLoopCount);
#else
		Sleep(1);
#endif
		if (xboxTraceMainLoopLate) XBLF("JA: CL_EARLY MAIN_TIGHT loop=%d bottom before increment clsFrame=%d state=%d", xboxMainLoopCount, cls.framecount, (int)cls.state);
		xboxMainLoopCount++;
	}

	return 0;
}


char *Sys_GetClipboardData(void) { return NULL; }

void Sys_StartProcess(char *, qboolean) {}

void Sys_OpenURL(char *, int) {}

void Sys_Quit(void)
{
#ifdef _XBOX
	XBL("JA: Sys_Quit called\n");
#endif
}

void Sys_ShowConsole(int, int) {}

void Sys_Mkdir(const char *) {}

int Sys_LowPhysicalMemory(void) { return 0; }

void Sys_FreeFileList(char **filelist)
{
	// All strings in a file list are allocated at once, so we just need to
	// do two frees, one for strings, one for the pointers.
	if ( filelist )
	{
		if ( filelist[0] )
			Z_Free( filelist[0] );

		Z_Free( filelist );
	}
}

#ifdef _JK2MP
char** Sys_ListFiles(const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs)
#else
char** Sys_ListFiles(const char *directory, const char *extension, int *numfiles, qboolean wantsubs)
#endif
{
#ifdef _JK2MP
	// MP has extra filter paramter. We don't support that.
	if (filter)
	{
		assert(!"Sys_ListFiles doesn't support filter on console!");
		return NULL;
	}
#endif

	// Hax0red console version of Sys_ListFiles. We mangle our arguments to get a standard filename
	// That file should exist, and contain the list of files that meet this search criteria.
	char	listFilename[MAX_OSPATH];
	char	*listFile, *curFile, *end;
	int		nfiles;
	char	**retList;

	// S00per hack
#ifdef XBOX_DEMO
	const char *basePath = Sys_RemapPath( "base\\" );
	if (strstr(directory, basePath))
		directory += strlen( basePath );
#else
	if (strstr(directory, "d:\\base\\"))
		directory += 8;
#endif

	if (!extension)
	{
		extension = "";
	}
	else if (extension[0] == '/' && extension[1] == 0)
	{
		// Passing a slash as extension will find directories
		extension = "dir";
	}
	else if (extension[0] == '.')
	{
		// Skip over leading .
		extension++;
	}

	// Build our filename
	Com_sprintf(listFilename, sizeof(listFilename), "%s\\_console_%s_list_", directory, extension);
	if (FS_ReadFile( listFilename, (void**)&listFile ) <= 0)
	{
		if(listFile) {
			FS_FreeFile(listFile);
		}
		Com_Printf( "WARNING: List file %s not found\n", listFilename );
		if (numfiles)
			*numfiles = 0;
		return NULL;
	}

	// Do a first pass to count number of files in the list
	nfiles = 0;
	curFile = listFile;
	while (true)
	{
		// Find end of line
		end = strchr(curFile, '\r');
		if (end)
		{
			// Should have a \n next -- skip them both
			end += 2;
		}
		else
		{
			end = strchr(curFile, '\n');
			if (end) end++;
			else end = curFile + strlen(curFile);
		}

		// Is the line empty?  If so, we're done.
		if (!curFile || !curFile[0]) break;
		++nfiles;

		// Advance to next line
		curFile = end;
	}

	// Fill in caller's pointer for number of files found
	if (numfiles) *numfiles = nfiles;

	// Did we find any files at all?
	if (nfiles == 0)
	{
		FS_FreeFile(listFile);
		return NULL;
	}

	// Allocate a file list, and quick string pool, but use LISTFILES
	retList = (char **) Z_Malloc( ( nfiles + 1 ) * sizeof( *retList ), TAG_LISTFILES, qfalse);
	// Our string pool is actually slightly too large, but it's temporary, and that's better
	// than slightly too small
	char *stringPool = (char *) Z_Malloc( strlen(listFile) + 1, TAG_LISTFILES, qfalse );

	// Now go through the list of files again, and fill in the list to be returned
	nfiles = 0;
	curFile = listFile;
	while (true)
	{
		// Find end of line
		end = strchr(curFile, '\r');
		if (end)
		{
			// Should have a \n next -- skip them both
			*end++ = '\0';
			*end++ = '\0';
		}
		else
		{
			end = strchr(curFile, '\n');
			if (end) *end++ = '\0';
			else end = curFile + strlen(curFile);
		}

		// Is the line empty?  If so, we're done.
		int curStrSize = strlen(curFile);
		if (curStrSize < 1)
		{
			retList[nfiles] = NULL;
			break;
		}

		// Alloc a small copy
		//retList[nfiles++] = CopyString( curFile );
		retList[nfiles++] = stringPool;
		strcpy(stringPool, curFile);
		stringPool += (curStrSize + 1);

		// Advance to next line
		curFile = end;
	}

	// Free the special file's buffer
	FS_FreeFile( listFile );

	return retList;
}

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP) && !defined(_JK2MP)
typedef struct stefx_trace_s {
	qboolean	allsolid;
	qboolean	startsolid;
	float		fraction;
	vec3_t		endpos;
	cplane_t	plane;
	int			surfaceFlags;
	int			contents;
	int			entityNum;
} stefx_trace_t;

typedef struct stefx_game_import_s {
	void	(*Printf)( const char *fmt, ... );
	void	(*WriteCam)( const char *text );
	void	(*Error)( int, const char *fmt, ... );
	int		(*Milliseconds)( void );
	cvar_t	*(*cvar)( const char *var_name, const char *value, int flags );
	void	(*cvar_set)( const char *var_name, const char *value );
	int		(*Cvar_VariableIntegerValue)( const char *var_name );
	void	(*Cvar_VariableStringBuffer)( const char *var_name, char *buffer, int bufsize );
	int		(*argc)( void );
	char	*(*argv)( int n );
	int		(*FS_FOpenFile)( const char *qpath, fileHandle_t *file, fsMode_t mode );
	int		(*FS_Read)( void *buffer, int len, fileHandle_t f );
	int		(*FS_Write)( const void *buffer, int len, fileHandle_t f );
	void	(*FS_FCloseFile)( fileHandle_t f );
	int		(*FS_ReadFile)( const char *name, void **buf );
	void	(*FS_FreeFile)( void *buf );
	int		(*FS_GetFileList)( const char *path, const char *extension, char *listbuf, int bufsize );
	qboolean	(*AppendToSaveGame)(unsigned long chid, void *data, int length);
	int		(*ReadFromSaveGame)(unsigned long chid, void *pvAddress, int iLength, void **ppvAddressPtr);
	int		(*ReadFromSaveGameOptional)(unsigned long chid, void *pvAddress, int iLength, void **ppvAddressPtr);
	void	(*SendConsoleCommand)( const char *text );
	void	(*DropClient)( int clientNum, const char *reason );
	void	(*SendServerCommand)( int clientNum, const char *fmt, ... );
	void	(*SetConfigstring)( int num, const char *string );
	void	(*GetConfigstring)( int num, char *buffer, int bufferSize );
	void	(*GetUserinfo)( int num, char *buffer, int bufferSize );
	void	(*SetUserinfo)( int num, const char *buffer );
	void	(*GetServerinfo)( char *buffer, int bufferSize );
	void	(*SetBrushModel)( gentity_t *ent, const char *name );
	void	(*trace)( stefx_trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask );
	int		(*pointcontents)( const vec3_t point, int passEntityNum );
	qboolean	(*inPVS)( const vec3_t p1, const vec3_t p2 );
	qboolean	(*inPVSIgnorePortals)( const vec3_t p1, const vec3_t p2 );
	void	(*AdjustAreaPortalState)( gentity_t *ent, qboolean open );
	qboolean	(*AreasConnected)( int area1, int area2 );
	void	(*linkentity)( gentity_t *ent );
	void	(*unlinkentity)( gentity_t *ent );
	int		(*EntitiesInBox)( const vec3_t mins, const vec3_t maxs, gentity_t **list, int maxcount );
	qboolean	(*EntityContact)( const vec3_t mins, const vec3_t maxs, const gentity_t *ent );
	int		*S_Override;
	void	*(*Malloc)( int bytes );
	void	(*Free)( void *buf );
} stefx_game_import_t;

typedef struct stefx_game_export_s {
	int			apiversion;
	void		(*Init)( const char *mapname, const char *spawntarget, int checkSum, const char *entstring, int levelTime, int randomSeed, int globalTime, SavedGameJustLoaded_e eSavedGameJustLoaded, qboolean qbLoadTransition );
	void		(*Shutdown) (void);
	void		(*WriteLevel) (qboolean qbAutosave);
	void		(*ReadLevel)  (qboolean qbAutosave, qboolean qbLoadTransition);
	qboolean	(*GameAllowedToSaveHere)(void);
	char		*(*ClientConnect)( int clientNum, qboolean firstTime, SavedGameJustLoaded_e eSavedGameJustLoaded );
	void		(*ClientBegin)( int clientNum, usercmd_t *cmd, SavedGameJustLoaded_e eSavedGameJustLoaded);
	void		(*ClientUserinfoChanged)( int clientNum );
	void		(*ClientDisconnect)( int clientNum );
	void		(*ClientCommand)( int clientNum );
	void		(*ClientThink)( int clientNum, usercmd_t *cmd );
	void		(*RunFrame)( int levelTime );
	qboolean	(*ConsoleCommand)( void );
	gentity_t	*gentities;
	int			gentitySize;
	int			num_entities;
} stefx_game_export_t;

static Trace_Functor_t s_stefxTrace;
static stefx_game_export_t *s_stefxEfGame = NULL;
static game_export_t s_stefxJaGame;
static int s_stefxRunFrameLogCount = 0;

extern int SG_Read(unsigned long chid, void *pvAddress, int iLength, void **ppvAddressPtr);
extern int SG_ReadOptional(unsigned long chid, void *pvAddress, int iLength, void **ppvAddressPtr);

static void STEFX_SyncGameExport(void)
{
	if (!s_stefxEfGame)
	{
		s_stefxJaGame.gentities = NULL;
		s_stefxJaGame.gentitySize = 0;
		s_stefxJaGame.num_entities = 0;
		return;
	}

	s_stefxJaGame.gentities = s_stefxEfGame->gentities;
	s_stefxJaGame.gentitySize = s_stefxEfGame->gentitySize;
	s_stefxJaGame.num_entities = s_stefxEfGame->num_entities;
}

static void *STEFX_GameMalloc(int bytes)
{
	return Z_Malloc(bytes, TAG_G_ALLOC, qfalse);
}

static void STEFX_GameFree(void *buf)
{
	if (buf)
	{
		Z_Free(buf);
	}
}

static void STEFX_CopyTraceToEf(stefx_trace_t *dst, const trace_t *src)
{
	if (!dst || !src)
	{
		return;
	}

	dst->allsolid = src->allsolid;
	dst->startsolid = src->startsolid;
	dst->fraction = src->fraction;
	VectorCopy(src->endpos, dst->endpos);
	dst->plane = src->plane;
	dst->surfaceFlags = src->surfaceFlags;
	dst->contents = src->contents;
	dst->entityNum = src->entityNum;
}

static void STEFX_Trace(stefx_trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask)
{
	static int s_traceLogCount = 0;
	trace_t jaTrace;
	memset(&jaTrace, 0, sizeof(jaTrace));
	if (s_traceLogCount < 64)
	{
		XBLF("STEFX: Trace enter count=%d fn=%08x pass=%d mask=0x%x efSize=%d jaSize=%d start=(%g,%g,%g) end=(%g,%g,%g) mins=%08x maxs=%08x",
			s_traceLogCount,
			(unsigned int)s_stefxTrace.trace_func,
			passEntityNum,
			contentmask,
			sizeof(stefx_trace_t),
			sizeof(trace_t),
			start[0], start[1], start[2],
			end[0], end[1], end[2],
			(unsigned int)mins,
			(unsigned int)maxs);
	}
	s_stefxTrace(&jaTrace, start, mins, maxs, end, passEntityNum, contentmask, G2_NOCOLLIDE, 0);
	STEFX_CopyTraceToEf(results, &jaTrace);
	if (s_traceLogCount < 64)
	{
		XBLF("STEFX: Trace exit count=%d fraction=%g allsolid=%d startsolid=%d ent=%d",
			s_traceLogCount,
			jaTrace.fraction,
			jaTrace.allsolid,
			jaTrace.startsolid,
			jaTrace.entityNum);
	}
	s_traceLogCount++;
}

static void STEFX_Init(const char *mapname, const char *spawntarget, int checkSum, const char *entstring, int levelTime, int randomSeed, int globalTime, SavedGameJustLoaded_e eSavedGameJustLoaded, qboolean qbLoadTransition)
{
	XBLF("STEFX: adapter before EF Init map='%s' checksum=%d", mapname ? mapname : "(null)", checkSum);
	s_stefxRunFrameLogCount = 0;
	if (s_stefxEfGame && s_stefxEfGame->Init)
	{
		s_stefxEfGame->Init(mapname, spawntarget, checkSum, entstring, levelTime, randomSeed, globalTime, eSavedGameJustLoaded, qbLoadTransition);
	}
	STEFX_SyncGameExport();
	XBLF("STEFX: adapter after EF Init gentities=%p gentitySize=%d num_entities=%d",
		s_stefxJaGame.gentities, s_stefxJaGame.gentitySize, s_stefxJaGame.num_entities);
}

static void STEFX_Shutdown(void)
{
	XBLog_Write("STEFX: adapter Shutdown");
	if (s_stefxEfGame && s_stefxEfGame->Shutdown)
	{
		s_stefxEfGame->Shutdown();
	}
	STEFX_SyncGameExport();
}

static void STEFX_WriteLevel(qboolean qbAutosave)
{
	if (s_stefxEfGame && s_stefxEfGame->WriteLevel)
	{
		s_stefxEfGame->WriteLevel(qbAutosave);
	}
	STEFX_SyncGameExport();
}

static void STEFX_ReadLevel(qboolean qbAutosave, qboolean qbLoadTransition)
{
	if (s_stefxEfGame && s_stefxEfGame->ReadLevel)
	{
		s_stefxEfGame->ReadLevel(qbAutosave, qbLoadTransition);
	}
	STEFX_SyncGameExport();
}

static qboolean STEFX_GameAllowedToSaveHere(void)
{
	return (s_stefxEfGame && s_stefxEfGame->GameAllowedToSaveHere) ? s_stefxEfGame->GameAllowedToSaveHere() : qfalse;
}

static char *STEFX_ClientConnect(int clientNum, qboolean firstTime, SavedGameJustLoaded_e eSavedGameJustLoaded)
{
	char *result = NULL;
	if (s_stefxEfGame && s_stefxEfGame->ClientConnect)
	{
		result = s_stefxEfGame->ClientConnect(clientNum, firstTime, eSavedGameJustLoaded);
	}
	STEFX_SyncGameExport();
	return result;
}

static void STEFX_ClientBegin(int clientNum, usercmd_t *cmd, SavedGameJustLoaded_e eSavedGameJustLoaded)
{
	if (s_stefxEfGame && s_stefxEfGame->ClientBegin)
	{
		s_stefxEfGame->ClientBegin(clientNum, cmd, eSavedGameJustLoaded);
	}
	STEFX_SyncGameExport();
}

static void STEFX_ClientUserinfoChanged(int clientNum)
{
	if (s_stefxEfGame && s_stefxEfGame->ClientUserinfoChanged)
	{
		s_stefxEfGame->ClientUserinfoChanged(clientNum);
	}
	STEFX_SyncGameExport();
}

static void STEFX_ClientDisconnect(int clientNum)
{
	if (s_stefxEfGame && s_stefxEfGame->ClientDisconnect)
	{
		s_stefxEfGame->ClientDisconnect(clientNum);
	}
	STEFX_SyncGameExport();
}

static void STEFX_ClientCommand(int clientNum)
{
	if (s_stefxEfGame && s_stefxEfGame->ClientCommand)
	{
		s_stefxEfGame->ClientCommand(clientNum);
	}
	STEFX_SyncGameExport();
}

static void STEFX_ClientThink(int clientNum, usercmd_t *cmd)
{
	if (s_stefxEfGame && s_stefxEfGame->ClientThink)
	{
		s_stefxEfGame->ClientThink(clientNum, cmd);
	}
	STEFX_SyncGameExport();
}

static void STEFX_RunFrame(int levelTime)
{
	if (s_stefxRunFrameLogCount < 16)
	{
		XBLF("STEFX: adapter before EF RunFrame count=%d time=%d", s_stefxRunFrameLogCount, levelTime);
	}
	if (s_stefxEfGame && s_stefxEfGame->RunFrame)
	{
		s_stefxEfGame->RunFrame(levelTime);
	}
	STEFX_SyncGameExport();
	if (s_stefxRunFrameLogCount < 16)
	{
		XBLF("STEFX: adapter after EF RunFrame count=%d time=%d entities=%d", s_stefxRunFrameLogCount, levelTime, s_stefxJaGame.num_entities);
	}
	s_stefxRunFrameLogCount++;
}

static void STEFX_ConnectNavs(const char *mapname, int checkSum)
{
	XBLF("STEFX: adapter ConnectNavs no-op map='%s' checksum=%d", mapname ? mapname : "(null)", checkSum);
	STEFX_SyncGameExport();
}

static qboolean STEFX_ConsoleCommand(void)
{
	qboolean result = qfalse;
	if (s_stefxEfGame && s_stefxEfGame->ConsoleCommand)
	{
		result = s_stefxEfGame->ConsoleCommand();
	}
	STEFX_SyncGameExport();
	return result;
}

static void STEFX_GameSpawnRMGEntity(char *s)
{
	XBLF("STEFX: adapter GameSpawnRMGEntity no-op entity='%s'", s ? s : "(null)");
	STEFX_SyncGameExport();
}

static void STEFX_BuildJaExportAdapter(void)
{
	memset(&s_stefxJaGame, 0, sizeof(s_stefxJaGame));
	s_stefxJaGame.apiversion = GAME_API_VERSION;
	s_stefxJaGame.Init = STEFX_Init;
	s_stefxJaGame.Shutdown = STEFX_Shutdown;
	s_stefxJaGame.WriteLevel = STEFX_WriteLevel;
	s_stefxJaGame.ReadLevel = STEFX_ReadLevel;
	s_stefxJaGame.GameAllowedToSaveHere = STEFX_GameAllowedToSaveHere;
	s_stefxJaGame.ClientConnect = STEFX_ClientConnect;
	s_stefxJaGame.ClientBegin = STEFX_ClientBegin;
	s_stefxJaGame.ClientUserinfoChanged = STEFX_ClientUserinfoChanged;
	s_stefxJaGame.ClientDisconnect = STEFX_ClientDisconnect;
	s_stefxJaGame.ClientCommand = STEFX_ClientCommand;
	s_stefxJaGame.ClientThink = STEFX_ClientThink;
	s_stefxJaGame.RunFrame = STEFX_RunFrame;
	s_stefxJaGame.ConnectNavs = STEFX_ConnectNavs;
	s_stefxJaGame.ConsoleCommand = STEFX_ConsoleCommand;
	s_stefxJaGame.GameSpawnRMGEntity = STEFX_GameSpawnRMGEntity;
	STEFX_SyncGameExport();
}
#endif

/*
=================
Sys_UnloadGame
=================
*/
void Sys_UnloadGame( void ) {
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP) && !defined(_JK2MP)
	s_stefxEfGame = NULL;
	memset(&s_stefxJaGame, 0, sizeof(s_stefxJaGame));
#endif
}

/*
=================
Sys_GetGameAPI

Loads the game dll
=================
*/
#ifndef _JK2MP
void *Sys_GetGameAPI (void *parms)
{
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	extern game_export_t *GetGameAPI( game_import_t *import );
	game_import_t *jaImport = (game_import_t *)parms;
	stefx_game_import_t efImport;
	game_export_t *rawExport;

	memset(&efImport, 0, sizeof(efImport));
	s_stefxTrace = jaImport->trace;

	efImport.Printf = jaImport->Printf;
	efImport.WriteCam = jaImport->WriteCam;
	efImport.Error = jaImport->Error;
	efImport.Milliseconds = jaImport->Milliseconds;
	efImport.cvar = jaImport->cvar;
	efImport.cvar_set = jaImport->cvar_set;
	efImport.Cvar_VariableIntegerValue = jaImport->Cvar_VariableIntegerValue;
	efImport.Cvar_VariableStringBuffer = jaImport->Cvar_VariableStringBuffer;
	efImport.argc = jaImport->argc;
	efImport.argv = jaImport->argv;
	efImport.FS_FOpenFile = jaImport->FS_FOpenFile;
	efImport.FS_Read = jaImport->FS_Read;
	efImport.FS_Write = jaImport->FS_Write;
	efImport.FS_FCloseFile = jaImport->FS_FCloseFile;
	efImport.FS_ReadFile = jaImport->FS_ReadFile;
	efImport.FS_FreeFile = jaImport->FS_FreeFile;
	efImport.FS_GetFileList = jaImport->FS_GetFileList;
	efImport.AppendToSaveGame = (qboolean (*)(unsigned long, void *, int))jaImport->AppendToSaveGame;
	efImport.ReadFromSaveGame = SG_Read;
	efImport.ReadFromSaveGameOptional = SG_ReadOptional;
	efImport.SendConsoleCommand = jaImport->SendConsoleCommand;
	efImport.DropClient = jaImport->DropClient;
	efImport.SendServerCommand = jaImport->SendServerCommand;
	efImport.SetConfigstring = jaImport->SetConfigstring;
	efImport.GetConfigstring = jaImport->GetConfigstring;
	efImport.GetUserinfo = jaImport->GetUserinfo;
	efImport.SetUserinfo = jaImport->SetUserinfo;
	efImport.GetServerinfo = jaImport->GetServerinfo;
	efImport.SetBrushModel = jaImport->SetBrushModel;
	efImport.trace = STEFX_Trace;
	efImport.pointcontents = jaImport->pointcontents;
	efImport.inPVS = jaImport->inPVS;
	efImport.inPVSIgnorePortals = jaImport->inPVSIgnorePortals;
	efImport.AdjustAreaPortalState = jaImport->AdjustAreaPortalState;
	efImport.AreasConnected = jaImport->AreasConnected;
	efImport.linkentity = jaImport->linkentity;
	efImport.unlinkentity = jaImport->unlinkentity;
	efImport.EntitiesInBox = jaImport->EntitiesInBox;
	efImport.EntityContact = jaImport->EntityContact;
	efImport.S_Override = jaImport->VoiceVolume;
	efImport.Malloc = STEFX_GameMalloc;
	efImport.Free = STEFX_GameFree;

	XBLF("STEFX: Sys_GetGameAPI before EF GetGameAPI import=%p efImport=%p", jaImport, &efImport);
	rawExport = GetGameAPI((game_import_t *)&efImport);
	s_stefxEfGame = (stefx_game_export_t *)rawExport;
	XBLF("STEFX: Sys_GetGameAPI after EF GetGameAPI raw=%p api=%d", rawExport, s_stefxEfGame ? s_stefxEfGame->apiversion : -1);
	STEFX_BuildJaExportAdapter();
	XBLF("STEFX: Sys_GetGameAPI returning JA adapter=%p api=%d efGentitySize=%d",
		&s_stefxJaGame, s_stefxJaGame.apiversion, s_stefxJaGame.gentitySize);
	return &s_stefxJaGame;
#else
	extern game_export_t *GetGameAPI( game_import_t *import );
	return GetGameAPI((game_import_t *)parms);
#endif
}
#endif

/*
=================
Sys_LoadCgame

Used to hook up a development dll
=================
*/
// void * Sys_LoadCgame( void ) 
#ifndef _JK2MP
void * Sys_LoadCgame( int (**entryPoint)(int, ...), int (*systemcalls)(int, ...) )
{
	extern void CG_PreInit();
	extern void cg_dllEntry( int (*syscallptr)( int arg,... ) );
	extern int cg_vmMain( int command, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7 );
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	XBLF("STEFX: Sys_LoadCgame entryOut=%p syscall=%p cg_vmMain=%p cg_dllEntry=%p",
		entryPoint, systemcalls, cg_vmMain, cg_dllEntry);
#endif
	cg_dllEntry(systemcalls);
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	XBLF("STEFX: Sys_LoadCgame after cg_dllEntry");
#endif
	*entryPoint = (int (*)(int,...))cg_vmMain;
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	XBLF("STEFX: Sys_LoadCgame entry assigned=%p", *entryPoint);
#endif
//	CG_PreInit();
	return 0;
}
#endif

/* VVFIXME: More stubs */
qboolean Sys_FileOutOfDate( LPCSTR psFinalFileName /* dest */, LPCSTR psDataFileName /* src */ )
{
	return qfalse;
}

qboolean Sys_CopyFile(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, qboolean bOverwrite)
{
	return qfalse;
}

qboolean Sys_CheckCD( void )
{
	return qtrue;
}
