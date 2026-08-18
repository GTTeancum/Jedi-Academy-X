// common.c -- misc functions used in client and server

#include "../game/q_shared.h"
#include "qcommon.h"
#include "../qcommon/sstring.h"	// to get Gil's string class, because MS's doesn't compile properly in here
#include "stv_version.h"

#ifdef _XBOX
#include "../win32/win_file.h"
#include "../ui/ui_splash.h"
#include "../win32/xb_log.h"
extern "C" volatile unsigned int g_SPXBComFrameCount;
extern "C" volatile unsigned int g_SPXBPhaseLast;
extern "C" volatile unsigned int g_SPXBBootPhase;
extern "C" volatile unsigned int g_SPXBComSubphase;
extern "C" volatile unsigned int g_SPXBComSpinCount;
extern "C" volatile unsigned int g_SPXBComMsec;
extern "C" volatile unsigned int g_SPXBComFrameTime;
extern "C" volatile unsigned int g_SPXBComLastTime;
extern "C" volatile unsigned int g_SPXBMapPhase;
extern "C" volatile unsigned int g_SPXBComErrorCode;
extern "C" volatile unsigned int g_SPXBComErrorHash;
extern "C" volatile unsigned int g_SPXBComErrorFirst4;
extern "C" volatile unsigned int g_SPXBComErrorNext4;
extern "C" volatile unsigned int g_SPXBClHunkCaller;
extern "C" volatile unsigned int g_SPXBClHunkCallCount;
extern bool Sys_IsDirectMapBoot(void);
#endif

#include "platform.h"

#define	MAXPRINTMSG	4096

#define MAX_NUM_ARGVS	50

#ifdef _XBOX
static qboolean Com_XboxDiagnosticPrintfIsCritical(const char *fmt) {
	if (!fmt) {
		return qfalse;
	}

	const qboolean critical =
		(strstr(fmt, "FRAME_HEARTBEAT") ||
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
		strstr(fmt, "BinkVideo::Start") ||
		strstr(fmt, "CG_Init") ||
		strstr(fmt, "CG_GameStateReceived") ||
		strstr(fmt, "CG_RegisterGraphics") ||
		strstr(fmt, "CG_NewClientinfo") ||
		strstr(fmt, "VM_Call(CG_INIT)") ||
		strstr(fmt, "cls.state = CA_PRIMED") ||
		strstr(fmt, "cls.state = CA_ACTIVE - GAME IS RUNNING") ||
		strstr(fmt, "FATAL") ||
		strstr(fmt, "ERROR") ||
		strstr(fmt, "Out of memory") ||
		strstr(fmt, "Received Exception") ||
		strstr(fmt, "EIP") ||
		strstr(fmt, "Z_Malloc():") ||
		strstr(fmt, "texture allocation failures")) ? qtrue : qfalse;

	return critical;
}

static qboolean Com_XboxShouldSkipDiagnosticPrintf(const char *fmt) {
#if SP_XBOX_VERBOSE_RUNTIME_LOGS
	return qfalse;
#else
	if (!fmt) {
		return qfalse;
	}
	if (Com_XboxDiagnosticPrintfIsCritical(fmt)) {
		return qfalse;
	}
	if (!strncmp(fmt, "JA: ", 4) ||
		!strncmp(fmt, "JAMP: ", 6) ||
		!strncmp(fmt, "QGLBridge:", 10) ||
		!strncmp(fmt, "JkaGlTexImage2D:", 16)) {
		return qtrue;
	}
	return qfalse;
#endif
}
#endif

int		com_argc;
char	*com_argv[MAX_NUM_ARGVS+1];

#ifndef _XBOX
static fileHandle_t	logfile;
static fileHandle_t	speedslog;
static fileHandle_t	camerafile;
fileHandle_t	com_journalFile;
fileHandle_t	com_journalDataFile;		// config files are written here
#endif

// Global language setting - this should be used instead of the myriad language
// cvars. Will be one of the Xbox values: XC_LANGUAGE_(ENGLISH|FRENCH|GERMAN)
DWORD	g_dwLanguage;

cvar_t	*com_viewlog;
cvar_t	*com_speeds;
cvar_t	*com_developer;
cvar_t	*com_timescale;
cvar_t	*com_fixedtime;
cvar_t	*com_maxfps;
cvar_t	*com_sv_running;
cvar_t	*com_cl_running;
cvar_t	*com_logfile;		// 1 = buffer log, 2 = flush after each print
cvar_t	*com_showtrace;
cvar_t	*com_terrainPhysics;
cvar_t	*com_version;
cvar_t	*com_buildScript;	// for automated data building scripts
cvar_t	*cl_paused;
cvar_t	*sv_paused;
cvar_t	*com_skippingcin;
cvar_t	*com_speedslog;		// 1 = buffer log, 2 = flush after each print
extern cvar_t *inSplashMenu;
extern cvar_t *controllerOut;

#ifdef G2_PERFORMANCE_ANALYSIS
cvar_t	*com_G2Report;
#endif


// com_speeds times
int		time_game;
int		time_frontend;		// renderer frontend time
int		time_backend;		// renderer backend time

int		timeInTrace;
int		timeInPVSCheck;
int		numTraces;

int			com_frameTime;
int			com_frameMsec;
int			com_frameNumber = 0;

qboolean	com_errorEntered;
qboolean	com_fullyInitialized = qfalse;

char	com_errorMessage[MAXPRINTMSG];

void Com_WriteConfig_f( void );

//============================================================================

#ifndef _XBOX
static char	*rd_buffer;
static int	rd_buffersize;
static void	(*rd_flush)( char *buffer );

void Com_BeginRedirect (char *buffer, int buffersize, void (*flush)( char *) )
{
	if (!buffer || !buffersize || !flush)
		return;
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_flush = flush;

	*rd_buffer = 0;
}

void Com_EndRedirect (void)
{
	if ( rd_flush ) {
		rd_flush(rd_buffer);
	}

	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
}
#ifndef FINAL_BUILD
#define OUTPUT_TO_BUILD_WINDOW
#endif
#endif	//not xbox

/*
=============
Com_Printf

Both client and server can use this, and it will output
to the apropriate place.

A raw string should NEVER be passed as fmt, because of "%f" type crashers.
=============
*/
void QDECL Com_Printf( const char *fmt, ... ) {
#ifndef FINAL_BUILD
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	vsprintf (msg,fmt,argptr);
	va_end (argptr);

#ifndef _XBOX
	if ( rd_buffer ) {
		if ((strlen (msg) + strlen(rd_buffer)) > (rd_buffersize - 1)) {
			rd_flush(rd_buffer);
			*rd_buffer = 0;
		}
		strcat (rd_buffer, msg);
		return;
	}
#endif

	CL_ConsolePrint( msg );

	// echo to dedicated console and early console
	Sys_Print( msg );

#ifdef OUTPUT_TO_BUILD_WINDOW
	OutputDebugString(msg);
#endif

#ifndef _XBOX
	// logfile
	if ( com_logfile && com_logfile->integer ) {
		if ( !logfile ) {
			logfile = FS_FOpenFileWrite( "qconsole.log" );
			if ( com_logfile->integer > 1 ) {
				// force it to not buffer so we get valid
				// data even if we are crashing
				FS_ForceFlush(logfile);
			}
		}
		if ( logfile ) {
			FS_Write(msg, strlen(msg), logfile);
		}
	}
#endif
#endif
}

void QDECL Com_PrintfAlways( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

#ifdef _XBOX
	if (Com_XboxShouldSkipDiagnosticPrintf(fmt)) {
		return;
	}
#endif

	va_start (argptr,fmt);
	vsprintf (msg,fmt,argptr);
	va_end (argptr);

#ifndef _XBOX
	if ( rd_buffer ) {
		if ((strlen (msg) + strlen(rd_buffer)) > (rd_buffersize - 1)) {
			rd_flush(rd_buffer);
			*rd_buffer = 0;
		}
		strcat (rd_buffer, msg);
		return;
	}
#endif

	CL_ConsolePrint( msg );

	// echo to dedicated console and early console
#ifndef FINAL_BUILD
	Sys_Print( msg );

#ifdef OUTPUT_TO_BUILD_WINDOW
	OutputDebugString(msg);
#endif
#endif

#ifndef _XBOX
	// logfile
	if ( com_logfile && com_logfile->integer ) {
		if ( !logfile ) {
			logfile = FS_FOpenFileWrite( "qconsole.log" );
			if ( com_logfile->integer > 1 ) {
				// force it to not buffer so we get valid
				// data even if we are crashing
				FS_ForceFlush(logfile);
			}
		}
		if ( logfile ) {
			FS_Write(msg, strlen(msg), logfile);
		}
	}
#endif
}



/*
================
Com_DPrintf

A Com_Printf that only shows up if the "developer" cvar is set
================
*/
void QDECL Com_DPrintf( const char *fmt, ...) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];
		
	if ( !com_developer || !com_developer->integer ) {
		return;			// don't confuse non-developers with techie stuff...
	}

	va_start (argptr,fmt);
	vsprintf (msg,fmt,argptr);
	va_end (argptr);
	
	Com_Printf ("%s", msg);
}

void Com_WriteCam ( const char *text )
{
#ifndef _XBOX
	static	char	mapname[MAX_QPATH];
	// camerafile
	if ( !camerafile ) 
	{
		extern	cvar_t	*sv_mapname;

		//NOTE: always saves in working dir if using one...
		sprintf( mapname, "maps/%s_cam.map", sv_mapname->string );
		camerafile = FS_FOpenFileWrite( mapname );
	}

	if ( camerafile ) 
	{
		FS_Printf( camerafile, "%s", text );
	}

	Com_Printf( "%s\n", mapname );
#endif
}

void Com_FlushCamFile()
{
#ifndef _XBOX
	if (!camerafile)
	{
		// nothing to flush, right?
		Com_Printf("No cam file available\n");
		return;
	}
	FS_ForceFlush(camerafile);
	FS_FCloseFile (camerafile);
	camerafile = 0;

	static	char	flushedMapname[MAX_QPATH];
	extern	cvar_t	*sv_mapname;
	sprintf( flushedMapname, "maps/%s_cam.map", sv_mapname->string );
	Com_Printf("flushed all cams to %s\n", flushedMapname);
#endif
}

/*
=============
Com_Error

Both client and server can use this, and it will
do the apropriate things.
=============
*/

void SG_WipeSavegame(const char *name);	// pretty sucky, but that's how SoF did it...<g>
void SG_Shutdown();
//void SCR_UnprecacheScreenshot();

void QDECL Com_Error( int code, const char *fmt, ... ) {
	va_list		argptr;

#if defined(_WIN32) && defined(_DEBUG)
	if ( code != ERR_DISCONNECT && code != ERR_NEED_CD ) {
//		if (com_noErrorInterrupt && !com_noErrorInterrupt->integer) 
		{
			__asm {
				int 0x03
			}
		}
	}
#endif

	// when we are running automated scripts, make sure we
	// know if anything failed
	if ( com_buildScript && com_buildScript->integer ) {
		code = ERR_FATAL;
	}

	if ( com_errorEntered ) {
		Sys_Error( "recursive error after: %s", com_errorMessage );
	}
	
	com_errorEntered = qtrue;

	//reset some game stuff here
//	SCR_UnprecacheScreenshot();

	va_start (argptr,fmt);
	vsprintf (com_errorMessage,fmt,argptr);
	va_end (argptr);	

#ifdef _XBOX
	{
		unsigned int errorHash = 2166136261u;
		const unsigned char *errorScan = (const unsigned char *)com_errorMessage;
		while (*errorScan)
		{
			errorHash ^= *errorScan++;
			errorHash *= 16777619u;
		}
		g_SPXBComErrorCode = (unsigned int)code;
		g_SPXBComErrorHash = errorHash;
		g_SPXBComErrorFirst4 = *((unsigned int *)&com_errorMessage[0]);
		g_SPXBComErrorNext4 = *((unsigned int *)&com_errorMessage[4]);
	}
	XBLF("JA: Com_Error code=%d message=%s", code, com_errorMessage);
#endif

	if ( code != ERR_DISCONNECT ) {
		Cvar_Get("com_errorMessage", "", CVAR_ROM);	//give com_errorMessage a default so it won't come back to life after a resetDefaults
		Cvar_Set("com_errorMessage", com_errorMessage);
	}

	SG_Shutdown();				// close any file pointers
	if ( code == ERR_DISCONNECT ) {
		SV_Shutdown("Disconnect");
		CL_Disconnect();
		CL_FlushMemory();
#ifdef _XBOX
		g_SPXBClHunkCaller = 4;
		g_SPXBClHunkCallCount++;
#endif
		CL_StartHunkUsers();
		com_errorEntered = qfalse;
		throw ("DISCONNECTED\n");
	} else if ( code == ERR_DROP ) {
		// If loading/saving caused the crash/error - delete the temp file
	//	SG_WipeSavegame("current");	// delete file

		SV_Shutdown (va("Server crashed: %s\n",  com_errorMessage));
		CL_Disconnect();
		if ( com_cl_running && com_cl_running->integer ) {
			CL_FlushMemory();
#ifdef _XBOX
			g_SPXBClHunkCaller = 4;
			g_SPXBClHunkCallCount++;
#endif
			CL_StartHunkUsers();
		}
		Com_Printf (S_COLOR_RED"********************\n"S_COLOR_MAGENTA"ERROR: %s\n"S_COLOR_RED"********************\n", com_errorMessage);
		com_errorEntered = qfalse;
		throw ("DROPPED\n");
	} else if ( code == ERR_NEED_CD ) {
		SV_Shutdown( "Server didn't have CD\n" );
		if ( com_cl_running && com_cl_running->integer ) {
			CL_Disconnect();
			CL_FlushMemory();
#ifdef _XBOX
			g_SPXBClHunkCaller = 4;
			g_SPXBClHunkCallCount++;
#endif
			CL_StartHunkUsers();
			com_errorEntered = qfalse;
		} else {
			Com_Printf("Server didn't have CD\n" );
		}
		throw ("NEED CD\n");
	} else {
		CL_Shutdown ();
		SV_Shutdown (va(S_COLOR_RED"Server fatal crashed: %s\n", com_errorMessage));
	}

	Com_Shutdown ();

	Sys_Error ("%s", com_errorMessage);
}


/*
=============
Com_Quit_f

Both client and server can use this, and it will
do the apropriate things.
=============
*/
void Com_Quit_f( void ) {
#ifdef _XBOX
	XBLog_Write("JA: Com_Quit_f entered");
#endif
	// don't try to shutdown if we are in a recursive error
	if ( !com_errorEntered ) {
		SV_Shutdown ("Server quit\n");
		CL_Shutdown ();
		Com_Shutdown ();
	}
	Sys_Quit ();
}



/*
============================================================================

COMMAND LINE FUNCTIONS

+ characters seperate the commandLine string into multiple console
command lines.

All of these are valid:

quake3 +set test blah +map test
quake3 set test blah+map test
quake3 set test blah + map test

============================================================================
*/

#define	MAX_CONSOLE_LINES	32
int		com_numConsoleLines;
char	*com_consoleLines[MAX_CONSOLE_LINES];

/*
==================
Com_ParseCommandLine

Break it up into multiple console lines
==================
*/
void Com_ParseCommandLine( char *commandLine ) {
	com_consoleLines[0] = commandLine;
	com_numConsoleLines = 1;

	while ( *commandLine ) {
		// look for a + seperating character
		// if commandLine came from a file, we might have real line seperators
		if ( *commandLine == '+' || *commandLine == '\n' ) {
			if ( com_numConsoleLines == MAX_CONSOLE_LINES ) {
				return;
			}
			com_consoleLines[com_numConsoleLines] = commandLine + 1;
			com_numConsoleLines++;
			*commandLine = 0;
		}
		commandLine++;
	}
}


/*
===================
Com_SafeMode

Check for "safe" on the command line, which will
skip loading of jaconfig.cfg
===================
*/
qboolean Com_SafeMode( void ) {
	int		i;

	for ( i = 0 ; i < com_numConsoleLines ; i++ ) {
		Cmd_TokenizeString( com_consoleLines[i] );
		if ( !Q_stricmp( Cmd_Argv(0), "safe" )
			|| !Q_stricmp( Cmd_Argv(0), "cvar_restart" ) ) {
			com_consoleLines[i][0] = 0;
			return qtrue;
		}
	}
	return qfalse;
}


/*
===============
Com_StartupVariable

Searches for command line parameters that are set commands.
If match is not NULL, only that cvar will be looked for.
That is necessary because cddir and basedir need to be set
before the filesystem is started, but all other sets should
be after execing the config and default.
===============
*/
void Com_StartupVariable( const char *match ) {
	int		i;
	char	*s;
	cvar_t	*cv;

	for (i=0 ; i < com_numConsoleLines ; i++) {
		Cmd_TokenizeString( com_consoleLines[i] );
		if ( strcmp( Cmd_Argv(0), "set" ) ) {
			continue;
		}

		s = Cmd_Argv(1);
		if ( !match || !stricmp( s, match ) ) {
			Cvar_Set( s, Cmd_Argv(2) );
			cv = Cvar_Get( s, "", 0 );
			cv->flags |= CVAR_USER_CREATED;
//			com_consoleLines[i] = 0;
		}
	}
}


/*
=================
Com_AddStartupCommands

Adds command line parameters as script statements
Commands are seperated by + signs

Returns qtrue if any late commands were added, which
will keep the demoloop from immediately starting
=================
*/
qboolean Com_AddStartupCommands( void ) {
	int		i;
	qboolean	added;

	added = qfalse;
	// quote every token, so args with semicolons can work
	for (i=0 ; i < com_numConsoleLines ; i++) {
		if ( !com_consoleLines[i] || !com_consoleLines[i][0] ) {
			continue;
		}

		// set commands won't override menu startup
		if ( Q_stricmpn( com_consoleLines[i], "set", 3 ) ) {
			added = qtrue;
		}
		Cbuf_AddText( com_consoleLines[i] );
		Cbuf_AddText( "\n" );
	}

	return added;
}


//============================================================================


void Info_Print( const char *s ) {
	char	key[512];
	char	value[512];
	char	*o;
	int		l;

	if (*s == '\\')
		s++;
	while (*s)
	{
		o = key;
		while (*s && *s != '\\')
			*o++ = *s++;

		l = o - key;
		if (l < 20)
		{
			memset (o, ' ', 20-l);
			key[20] = 0;
		}
		else
			*o = 0;
		Com_Printf ("%s", key);

		if (!*s)
		{
			Com_Printf ("MISSING VALUE\n");
			return;
		}

		o = value;
		s++;
		while (*s && *s != '\\')
			*o++ = *s++;
		*o = 0;

		if (*s)
			s++;
		Com_Printf ("%s\n", value);
	}
}

/*
============
Com_StringContains
============
*/
char *Com_StringContains(char *str1, char *str2, int casesensitive) {
	int len, i, j;

	len = strlen(str1) - strlen(str2);
	for (i = 0; i <= len; i++, str1++) {
		for (j = 0; str2[j]; j++) {
			if (casesensitive) {
				if (str1[j] != str2[j]) {
					break;
				}
			}
			else {
				if (toupper(str1[j]) != toupper(str2[j])) {
					break;
				}
			}
		}
		if (!str2[j]) {
			return str1;
		}
	}
	return NULL;
}

/*
============
Com_Filter
============
*/
int Com_Filter(char *filter, char *name, int casesensitive) {
	char buf[MAX_TOKEN_CHARS];
	char *ptr;
	int i;

	while(*filter) {
		if (*filter == '*') {
			filter++;
			for (i = 0; *filter; i++) {
				if (*filter == '*' || *filter == '?') {
					break;
				}
				buf[i] = *filter;
				filter++;
			}
			buf[i] = '\0';
			if (strlen(buf)) {
				ptr = Com_StringContains(name, buf, casesensitive);
				if (!ptr) {
					return qfalse;
				}
				name = ptr + strlen(buf);
			}
		}
		else if (*filter == '?') {
			filter++;
			name++;
		}
		else {
			if (casesensitive) {
				if (*filter != *name) {
					return qfalse;
				}
			}
			else {
				if (toupper(*filter) != toupper(*name)) {
					return qfalse;
				}
			}
			filter++;
			name++;
		}
	}
	return qtrue;
}



/*
=================
Com_InitHunkMemory
=================
*/
#if !defined(_XBOX)
void Com_InitHunkMemory( void )
{
	Hunk_Clear();

//	Cmd_AddCommand( "meminfo", Z_Details_f );
}

// I'm leaving this in just in case we ever need to remember where's a good place to hook something like this in.
//
void Com_ShutdownHunkMemory(void)
{
}


/*
===================
Hunk_SetMark

The server calls this after the level and game VM have been loaded
===================
*/
void Hunk_SetMark( void ) 
{
}



/*
=================
Hunk_ClearToMark

The client calls this before starting a vid_restart or snd_restart
=================
*/
void Hunk_ClearToMark( void ) 
{
	Z_TagFree(TAG_HUNKALLOC);	
//	Z_TagFree(TAG_HUNKMISCMODELS);
}



/*
=================
Hunk_Clear

The server calls this before shutting down or loading a new map
=================
*/
void Hunk_Clear( void ) 
{
	Z_TagFree(TAG_HUNKALLOC);
//	Z_TagFree(TAG_HUNKMISCMODELS);

	extern void CIN_CloseAllVideos();
				CIN_CloseAllVideos();

	extern void R_ClearStuffToStopGhoul2CrashingThings(void);
				R_ClearStuffToStopGhoul2CrashingThings();
}
#endif






/*
===================================================================

EVENTS AND JOURNALING

In addition to these events, .cfg files are also copied to the
journaled file
===================================================================
*/

#define	MAX_PUSHED_EVENTS	64
int			com_pushedEventsHead, com_pushedEventsTail;
sysEvent_t	com_pushedEvents[MAX_PUSHED_EVENTS];

/*
=================
Com_GetRealEvent
=================
*/
sysEvent_t	Com_GetRealEvent( void ) {
	sysEvent_t	ev;

	// get an event from the system
		ev = Sys_GetEvent();

	return ev;
}

/*
=================
Com_PushEvent
=================
*/
void Com_PushEvent( sysEvent_t *event ) {
	sysEvent_t		*ev;
	static int		printedWarning;

	ev = &com_pushedEvents[ com_pushedEventsHead & (MAX_PUSHED_EVENTS-1) ];

	if ( com_pushedEventsHead - com_pushedEventsTail >= MAX_PUSHED_EVENTS ) {

		// don't print the warning constantly, or it can give time for more...
		if ( !printedWarning ) {
			printedWarning = qtrue;
			Com_Printf( "WARNING: Com_PushEvent overflow\n" );
		}

		if ( ev->evPtr ) {
			Z_Free( ev->evPtr );
		}
		com_pushedEventsTail++;
	} else {
		printedWarning = qfalse;
	}

	*ev = *event;
	com_pushedEventsHead++;
}

/*
=================
Com_GetEvent
=================
*/
sysEvent_t	Com_GetEvent( void ) {
	if ( com_pushedEventsHead > com_pushedEventsTail ) {
		com_pushedEventsTail++;
		return com_pushedEvents[ (com_pushedEventsTail-1) & (MAX_PUSHED_EVENTS-1) ];
	}
	return Com_GetRealEvent();
}

/*
=================
Com_RunAndTimeServerPacket
=================
*/
void Com_RunAndTimeServerPacket( netadr_t *evFrom, msg_t *buf ) {
	int		t1, t2, msec;

#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBBootPhase, 0x760);
#endif
	t1 = 0;

	if ( com_speeds->integer ) {
		t1 = Sys_Milliseconds ();
	}

#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBBootPhase, 0x761);
#endif
	SV_PacketEvent( *evFrom, buf );
#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBBootPhase, 0x762);
#endif

	if ( com_speeds->integer ) {
		t2 = Sys_Milliseconds ();
		msec = t2 - t1;
		if ( com_speeds->integer == 3 ) {
			Com_Printf( "SV_PacketEvent time: %i\n", msec );
		}
	}
}

/*
=================
Com_EventLoop

Returns last event time
=================
*/
int Com_EventLoop( void ) {
	sysEvent_t	ev;
	netadr_t	evFrom;
	byte		bufData[MAX_MSGLEN];
	msg_t		buf;
#ifdef _XBOX
#define SP_EVENT_BOOTPHASE(x) do { SPXB_HOT_SET(g_SPXBBootPhase, (x)); } while (0)
	SP_EVENT_BOOTPHASE(0x720);
	static int s_xboxEventLoopTraceBudget = SP_XBOX_VERBOSE_RUNTIME_LOGS ? 24 : 0;
	const qboolean xboxTraceEventLoop = (s_xboxEventLoopTraceBudget > 0);
	if (xboxTraceEventLoop)
	{
		Com_PrintfAlways("JA: Com_EventLoop enter pushed=%d/%d\n", com_pushedEventsHead, com_pushedEventsTail);
	}
#endif
#ifndef _XBOX
#define SP_EVENT_BOOTPHASE(x) do { } while (0)
#endif

	SP_EVENT_BOOTPHASE(0x721);
	MSG_Init( &buf, bufData, sizeof( bufData ) );
	SP_EVENT_BOOTPHASE(0x722);

	while ( 1 ) {
#ifdef _XBOX
		SP_EVENT_BOOTPHASE(0x723);
		if (xboxTraceEventLoop) Com_PrintfAlways("JA: Com_EventLoop before Com_GetEvent\n");
#endif
		ev = Com_GetEvent();
#ifdef _XBOX
		SP_EVENT_BOOTPHASE(0x724);
		if (xboxTraceEventLoop)
		{
			Com_PrintfAlways("JA: Com_EventLoop got event type=%d time=%d value=%d value2=%d ptr=%p len=%d\n",
				ev.evType, ev.evTime, ev.evValue, ev.evValue2, ev.evPtr, ev.evPtrLength);
		}
#endif

		// if no more events are available
		if ( ev.evType == SE_NONE ) {
#ifdef _XBOX
			SP_EVENT_BOOTPHASE(0x725);
			if (xboxTraceEventLoop) Com_PrintfAlways("JA: Com_EventLoop SE_NONE before NS_CLIENT drain\n");
			int xboxClientLoopPackets = 0;
			qboolean xboxClientLoopCapped = qfalse;
#endif
			// manually send packet events for the loopback channel
			while ( NET_GetLoopPacket( NS_CLIENT, &evFrom, &buf ) ) {
#ifdef _XBOX
				SP_EVENT_BOOTPHASE(0x726);
				if (xboxClientLoopPackets++ >= 128) {
					xboxClientLoopCapped = qtrue;
					break;
				}
#endif
#ifdef _XBOX
				static int s_xboxClientLoopLogs = 0;
				if (SP_XBOX_VERBOSE_RUNTIME_LOGS && s_xboxClientLoopLogs < 16)
				{
					Com_PrintfAlways("JA: Com_EventLoop dispatch NS_CLIENT loop packet size=%d\n", buf.cursize);
					++s_xboxClientLoopLogs;
				}
#endif
				CL_PacketEvent( evFrom, &buf );
#ifdef _XBOX
				SP_EVENT_BOOTPHASE(0x727);
				if (xboxTraceEventLoop) Com_PrintfAlways("JA: Com_EventLoop after NS_CLIENT packet\n");
#endif
			}
#ifdef _XBOX
			if (xboxClientLoopCapped) {
				static int s_xboxClientLoopCapLogs = 0;
				if (SP_XBOX_VERBOSE_RUNTIME_LOGS && s_xboxClientLoopCapLogs < 8) {
					Com_PrintfAlways("JA: Com_EventLoop capped NS_CLIENT drain at %d packets\n", xboxClientLoopPackets);
					++s_xboxClientLoopCapLogs;
				}
			}
#endif

#ifdef _XBOX
			SP_EVENT_BOOTPHASE(0x728);
			if (xboxTraceEventLoop) Com_PrintfAlways("JA: Com_EventLoop before NS_SERVER drain\n");
			int xboxServerLoopPackets = 0;
			qboolean xboxServerLoopCapped = qfalse;
#endif
			while ( NET_GetLoopPacket( NS_SERVER, &evFrom, &buf ) ) {
#ifdef _XBOX
				SP_EVENT_BOOTPHASE(0x729);
				if (xboxServerLoopPackets++ >= 128) {
					xboxServerLoopCapped = qtrue;
					break;
				}
#endif
				// if the server just shut down, flush the events
				if ( com_sv_running->integer ) {
#ifdef _XBOX
					SP_EVENT_BOOTPHASE(0x732);
					static int s_xboxServerLoopLogs = 0;
					if (SP_XBOX_VERBOSE_RUNTIME_LOGS && s_xboxServerLoopLogs < 16)
					{
						SP_EVENT_BOOTPHASE(0x733);
						Com_PrintfAlways("JA: Com_EventLoop dispatch NS_SERVER loop packet size=%d\n", buf.cursize);
						SP_EVENT_BOOTPHASE(0x734);
						++s_xboxServerLoopLogs;
					}
#endif
					SP_EVENT_BOOTPHASE(0x735);
					Com_RunAndTimeServerPacket( &evFrom, &buf );
#ifdef _XBOX
					SP_EVENT_BOOTPHASE(0x72A);
					if (xboxTraceEventLoop) Com_PrintfAlways("JA: Com_EventLoop after NS_SERVER packet\n");
#endif
				}
			}
#ifdef _XBOX
			if (xboxServerLoopCapped) {
				static int s_xboxServerLoopCapLogs = 0;
				if (SP_XBOX_VERBOSE_RUNTIME_LOGS && s_xboxServerLoopCapLogs < 8) {
					Com_PrintfAlways("JA: Com_EventLoop capped NS_SERVER drain at %d packets\n", xboxServerLoopPackets);
					++s_xboxServerLoopCapLogs;
				}
			}
#endif

#ifdef _XBOX
			SP_EVENT_BOOTPHASE(0x72B);
			if (xboxTraceEventLoop)
			{
				Com_PrintfAlways("JA: Com_EventLoop return time=%d\n", ev.evTime);
				--s_xboxEventLoopTraceBudget;
			}
#endif
			return ev.evTime;
		}


		switch ( ev.evType ) {
		default:
			Com_Error( ERR_FATAL, "Com_EventLoop: bad event type %i", ev.evTime );
			break;
        case SE_NONE:
            break;
		case SE_KEY:
			SP_EVENT_BOOTPHASE(0x72C);
			CL_KeyEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
		case SE_CHAR:
			SP_EVENT_BOOTPHASE(0x72D);
			CL_CharEvent( ev.evValue );
			break;
		case SE_MOUSE:
			SP_EVENT_BOOTPHASE(0x72E);
			CL_MouseEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
		case SE_JOYSTICK_AXIS:
			SP_EVENT_BOOTPHASE(0x72F);
			CL_JoystickEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
		case SE_CONSOLE:
			SP_EVENT_BOOTPHASE(0x730);
			Cbuf_AddText( (char *)ev.evPtr );
			Cbuf_AddText( "\n" );
			break;
		case SE_PACKET:
			SP_EVENT_BOOTPHASE(0x731);
			evFrom = *(netadr_t *)ev.evPtr;
			buf.cursize = ev.evPtrLength - sizeof( evFrom );

			// we must copy the contents of the message out, because
			// the event buffers are only large enough to hold the
			// exact payload, but channel messages need to be large
			// enough to hold fragment reassembly
			if ( (unsigned)buf.cursize > buf.maxsize ) {
				Com_Printf("Com_EventLoop: oversize packet\n");
				continue;
			}
			memcpy( buf.data, (byte *)((netadr_t *)ev.evPtr + 1), buf.cursize );
			if ( com_sv_running->integer ) {
				Com_RunAndTimeServerPacket( &evFrom, &buf );
			} else {
				CL_PacketEvent( evFrom, &buf );
			}
			break;
		}

		// free any block data
		if ( ev.evPtr ) {
			Z_Free( ev.evPtr );
		}
	}
#undef SP_EVENT_BOOTPHASE
}

/*
================
Com_Milliseconds

Can be used for profiling, but will be journaled accurately
================
*/
int Com_Milliseconds (void) {
	sysEvent_t	ev;

	// get events and push them until we get a null event with the current time
	do {

		ev = Com_GetRealEvent();
		if ( ev.evType != SE_NONE ) {
			Com_PushEvent( &ev );
		}
	} while ( ev.evType != SE_NONE );
	
	return ev.evTime;
}

//============================================================================

/*
=============
Com_Error_f

Just throw a fatal error to
test error shutdown procedures
=============
*/
static void Com_Error_f (void) {
	if ( Cmd_Argc() > 1 ) {
		Com_Error( ERR_DROP, "Testing drop error" );
	} else {
		Com_Error( ERR_FATAL, "Testing fatal error" );
	}
}


/*
=============
Com_Freeze_f

Just freeze in place for a given number of seconds to test
error recovery
=============
*/
static void Com_Freeze_f (void) {
	float	s;
	int		start, now;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "freeze <seconds>\n" );
		return;
	}
	s = atof( Cmd_Argv(1) );

	start = Com_Milliseconds();

	while ( 1 ) {
		now = Com_Milliseconds();
		if ( ( now - start ) * 0.001 > s ) {
			break;
		}
	}
}

/*
=================
Com_Crash_f

A way to force a bus error for development reasons
=================
*/
static void Com_Crash_f( void ) {
	* ( int * ) 0 = 0x12345678;
}

/*
=================
Com_Init
=================
*/
extern void Com_InitZoneMemory();
extern void R_InitWorldEffects();
void Com_Init( char *commandLine ) {
	char	*s;

	g_SPXBBootPhase = 0x40;
	XBLog_Write("JA: Com_Init entered");
	Com_Printf( "%s %s %s\n", Q3_VERSION, CPUSTRING, __DATE__ );

	try {
		// Grab the user's langauge preference from the dashboard right away!
		XBLog_Write("JA: XGetLanguage...");
		g_SPXBBootPhase = 0x41;
		g_dwLanguage = XGetLanguage();
		if( g_dwLanguage != XC_LANGUAGE_FRENCH && g_dwLanguage != XC_LANGUAGE_GERMAN )
			g_dwLanguage = XC_LANGUAGE_ENGLISH;
		XBLog_Write("JA: XGetLanguage done");

		// prepare enough of the subsystems to handle
		// cvar and command buffer management
		XBLog_Write("JA: Com_ParseCommandLine...");
		Com_ParseCommandLine( commandLine );

		XBLog_Write("JA: Swap_Init...");
		Swap_Init ();
		XBLog_Write("JA: Cbuf_Init...");
		Cbuf_Init ();

		XBLog_Write("JA: Com_InitZoneMemory...");
		g_SPXBBootPhase = 0x42;
		Com_InitZoneMemory();
		XBLog_Write("JA: Com_InitZoneMemory done");

#ifdef _XBOX
		XBLog_Write("JA: WF_Init...");
		WF_Init();
		XBLog_Write("JA: WF_Init done");
		XBLog_Write("JA: CL_InitRef...");
		g_SPXBBootPhase = 0x43;
		// set up ri
		extern void CL_InitRef( void );
		CL_InitRef();
		g_SPXBBootPhase = 0x44;
		XBLog_Write("JA: CL_InitRef done");
		XBLog_Write("JA: R_Register...");
		g_SPXBBootPhase = 0x45;
		// register renderer cvars
		extern void R_Register(void);
		R_Register();
		g_SPXBBootPhase = 0x46;
		XBLog_Write("JA: R_Register done");
		XBLog_Write("JA: GLimp_Init...");
		g_SPXBBootPhase = 0x47;
		// start the gl render layer
		extern void GLimp_Init(void);
		GLimp_Init();
		g_SPXBBootPhase = 0x48;
		XBLog_Write("JA: GLimp_Init done");
		// put up the license screen
		g_SPXBBootPhase = 0x49;
		extern bool Sys_IsDirectMapBoot(void);
		if (Sys_IsDirectMapBoot())
		{
			XBLog_Write("JA: SP_DoLicense skipped for direct-map boot");
		}
		else
		{
			XBLog_Write("JA: SP_DoLicense...");
			g_SPXBBootPhase = 0x4A;
			SP_DoLicense();
			g_SPXBBootPhase = 0x4B;
			XBLog_Write("JA: SP_DoLicense done");
		}
		g_SPXBBootPhase = 0x4C;
#endif

		g_SPXBBootPhase = 0x4C1;
		XBLog_Write("JA: Cmd_Init...");
		g_SPXBBootPhase = 0x4C2;
		Cmd_Init ();
		g_SPXBBootPhase = 0x4C3;
		XBLog_Write("JA: Cvar_Init...");
		g_SPXBBootPhase = 0x4C4;
		Cvar_Init ();
		g_SPXBBootPhase = 0x4C5;

		// get the commandline cvars set
		g_SPXBBootPhase = 0x4D0;
		XBLog_Write("JA: Com_StartupVariable...");
		Com_StartupVariable( NULL );
		g_SPXBBootPhase = 0x4D1;

		// done early so bind command exists
		XBLog_Write("JA: CL_InitKeyCommands...");
		CL_InitKeyCommands();
		g_SPXBBootPhase = 0x4D2;

#ifdef _XBOX
		XBLog_Write("JA: Sys_InitFileCodes...");
		extern void Sys_FilecodeScan_f();
		Sys_InitFileCodes();
		Cmd_AddCommand("filecodes", Sys_FilecodeScan_f);
		XBLog_Write("JA: Sys_InitFileCodes done");
		g_SPXBBootPhase = 0x4D3;

		g_SPXBBootPhase = 0x4D31;
		XBLog_Write("JA: Sys_StreamInit...");
		g_SPXBBootPhase = 0x4D32;
		extern void Sys_StreamInit();
		Sys_StreamInit();
		g_SPXBBootPhase = 0x4D33;
		XBLog_Write("JA: Sys_StreamInit done");
		g_SPXBBootPhase = 0x4D4;

		// This just forces the static singleton in the function to call
		// its constructor, which allocates a stupid 12 byte block of
		// memory that never gets freed. Otherwise, it ends up stranded in
		// the middle of the zone:
		XBLog_Write("JA: TheGhoul2InfoArray...");
		TheGhoul2InfoArray();
		XBLog_Write("JA: TheGhoul2InfoArray done");
		g_SPXBBootPhase = 0x4D5;
#endif

		XBLog_Write("JA: FS_InitFilesystem...");
		FS_InitFilesystem ();	//uses z_malloc
		XBLog_Write("JA: FS_InitFilesystem done");
		g_SPXBBootPhase = 0x4D6;
		XBLog_Write("JA: R_InitWorldEffects...");
		R_InitWorldEffects();   // this doesn't do much but I want to be sure certain variables are intialized.
		g_SPXBBootPhase = 0x4D7;
		
		XBLog_Write("JA: exec default.cfg...");
		Cbuf_AddText ("exec default.cfg\n");

		// skip the jaconfig.cfg if "safe" is on the command line
		if ( !Com_SafeMode() ) {
			Cbuf_AddText ("exec jaconfig.cfg\n");
		}

		Cbuf_AddText ("exec autoexec.cfg\n");

		XBLog_Write("JA: Cbuf_Execute (configs)...");
		Cbuf_Execute ();
		XBLog_Write("JA: Config execution done");
		g_SPXBBootPhase = 0x4D8;

		// override anything from the config files with command line args
		Com_StartupVariable( NULL );
		g_SPXBBootPhase = 0x4D9;

		// allocate the stack based hunk allocator
		XBLog_Write("JA: Com_InitHunkMemory...");
		Com_InitHunkMemory();
		XBLog_Write("JA: Com_InitHunkMemory done");
		g_SPXBBootPhase = 0x4DA;

		// if any archived cvars are modified after this, we will trigger a writing
		// of the config file
		cvar_modifiedFlags &= ~CVAR_ARCHIVE;
		
		//
		// init commands and vars
		//
		Cmd_AddCommand ("quit", Com_Quit_f);
		Cmd_AddCommand ("writeconfig", Com_WriteConfig_f );
		
		com_maxfps = Cvar_Get ("com_maxfps", "85", CVAR_ARCHIVE);
		
		com_developer = Cvar_Get ("developer", "0", CVAR_TEMP );
		com_logfile = Cvar_Get ("logfile", "0", CVAR_TEMP );
		com_speedslog = Cvar_Get ("speedslog", "0", CVAR_TEMP );
		
		com_timescale = Cvar_Get ("timescale", "1", CVAR_CHEAT );
		com_fixedtime = Cvar_Get ("fixedtime", "0", CVAR_CHEAT);
		com_showtrace = Cvar_Get ("com_showtrace", "0", CVAR_CHEAT);
		com_terrainPhysics = Cvar_Get ("com_terrainPhysics", "1", CVAR_CHEAT);
		com_viewlog = Cvar_Get( "viewlog", "0", CVAR_TEMP );
		com_speeds = Cvar_Get ("com_speeds", "0", 0);
		
#ifdef G2_PERFORMANCE_ANALYSIS
		com_G2Report = Cvar_Get("com_G2Report", "0", 0);
#endif

		cl_paused	   = Cvar_Get ("cl_paused", "0", CVAR_ROM);
		sv_paused	   = Cvar_Get ("sv_paused", "0", CVAR_ROM);
		com_sv_running = Cvar_Get ("sv_running", "0", CVAR_ROM);
		com_cl_running = Cvar_Get ("cl_running", "0", CVAR_ROM);
		com_skippingcin = Cvar_Get ("skippingCinematic", "0", CVAR_ROM);
		com_buildScript = Cvar_Get( "com_buildScript", "0", 0 );
		
		if ( com_developer && com_developer->integer ) {
			Cmd_AddCommand ("error", Com_Error_f);
			Cmd_AddCommand ("crash", Com_Crash_f );
			Cmd_AddCommand ("freeze", Com_Freeze_f);
		}
		
		s = va("%s %s %s", Q3_VERSION, CPUSTRING, __DATE__ );
		com_version = Cvar_Get ("version", s, CVAR_ROM | CVAR_SERVERINFO );


		// So any controller can skip the logo movies:
		inSplashMenu = Cvar_Get( "inSplashMenu", "1", 0 );
		controllerOut= Cvar_Get( "ControllerOutNum", "-1", 0);

#ifdef XBOX_DEMO
		// Cvar used to hide "QUIT TO DEMOS MENU" options if we weren't started by CDX
		extern bool demoLaunchDataValid;
		if( demoLaunchDataValid )
			Cvar_SetValue( "ui_allowDemoQuit", 1 );
		else
			Cvar_SetValue( "ui_allowDemoQuit", 0 );
#endif

		XBLog_Write("JA: SE_Init...");
		SE_Init();	// Initialize StringEd
		XBLog_Write("JA: SE_Init done");

		XBLog_Write("JA: Sys_Init...");
		Sys_Init();	// this also detects CPU type, so I can now do this CPU check below...
		XBLog_Write("JA: Sys_Init done");

		XBLog_Write("JA: Netchan_Init...");
		Netchan_Init( Com_Milliseconds() & 0xffff );	// pick a port value that should be nice and random
//	VM_Init();
		XBLog_Write("JA: SV_Init...");
		SV_Init();
		XBLog_Write("JA: SV_Init done");

		XBLog_Write("JA: CL_Init...");
		CL_Init();
		XBLog_Write("JA: CL_Init done");

#ifdef _XBOX
		// Experiment. Sound memory never gets freed, move it earlier. This
		// will also let us play movies sooner, if we need to.
		XBLog_Write("JA: CL_StartSound...");
		extern void CL_StartSound(void);
		CL_StartSound();
		XBLog_Write("JA: CL_StartSound done");
#endif

		Sys_ShowConsole( com_viewlog->integer, qfalse );

		// set com_frameTime so that if a map is started on the
		// command line it will still be able to count on com_frameTime
		// being random enough for a serverid
		com_frameTime = Com_Milliseconds();
		XBLog_Write("JA: Com_Init fully initialized");

		// add + commands from command line
#ifndef _XBOX
		if ( !Com_AddStartupCommands() ) {
#ifdef NDEBUG
			// if the user didn't give any commands, run default action
//			if ( !com_dedicated->integer ) 
			{
				Cbuf_AddText ("cinematic openinglogos\n");
//				if( !com_introPlayed->integer ) {
//					Cvar_Set( com_introPlayed->name, "1" );
//					Cvar_Set( "nextmap", "cinematic intro" );
//				}
			}
#endif	
		}
#endif
		com_fullyInitialized = qtrue;
		Com_Printf ("--- Common Initialization Complete ---\n");

//HACKERY FOR THE DEUTSCH		
		//if ( (Cvar_VariableIntegerValue("ui_iscensored") == 1) 	//if this was on before, set it again so it gets its flags
		//	)
		//{
		//	Cvar_Get( "ui_iscensored",   "1", CVAR_ARCHIVE|CVAR_ROM|CVAR_INIT|CVAR_CHEAT|CVAR_NORESTART);
		//	Cvar_Set( "ui_iscensored",   "1");	//just in case it was archived
		//	// NOTE : I also create this in UI_Init()
		//	Cvar_Get( "g_dismemberment", "0", CVAR_ARCHIVE|CVAR_ROM|CVAR_INIT|CVAR_CHEAT);
		//	Cvar_Set( "g_dismemberment", "0");	//just in case it was archived
		//}
	}

	catch (const char* reason) {
		Sys_Error ("Error during initialization %s", reason);
	}

#ifdef _XBOX
	//Load these early to keep them at the beginning of memory.  Perhaps
	//here is too early though.  After the license screen would be better.
	extern void SE_CheckForLanguageUpdates(void);
	SE_CheckForLanguageUpdates();
#endif

}

//==================================================================

void Com_WriteConfigToFile( const char *filename ) {
#ifndef _XBOX
	fileHandle_t	f;

	f = FS_FOpenFileWrite( filename );
	if ( !f ) {
		Com_Printf ("Couldn't write %s.\n", filename );
		return;
	}

	FS_Printf (f, "// generated by Star Wars Jedi Academy, do not modify\n");
	Key_WriteBindings (f);
	Cvar_WriteVariables (f);
	FS_FCloseFile( f );
#endif
}


/*
===============
Com_WriteConfiguration

Writes key bindings and archived cvars to config file if modified
===============
*/
void Com_WriteConfiguration( void ) {
	// if we are quiting without fully initializing, make sure
	// we don't write out anything
	if ( !com_fullyInitialized ) {
		return;
	}

	if ( !(cvar_modifiedFlags & CVAR_ARCHIVE ) ) {
		return;
	}
	cvar_modifiedFlags &= ~CVAR_ARCHIVE;

	Com_WriteConfigToFile( "jaconfig.cfg" );
}


/*
===============
Com_WriteConfig_f

Write the config file to a specific name
===============
*/
void Com_WriteConfig_f( void ) {
	char	filename[MAX_QPATH];

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: writeconfig <filename>\n" );
		return;
	}

	Q_strncpyz( filename, Cmd_Argv(1), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".cfg" );
	Com_Printf( "Writing %s.\n", filename );
	Com_WriteConfigToFile( filename );
}

/*
================
Com_ModifyMsec
================
*/


int Com_ModifyMsec( int msec, float &fraction ) 
{
	int		clampTime;

	fraction=0.0f;

	//
	// modify time for debugging values
	//
	if ( com_fixedtime->integer ) 
	{
		msec = com_fixedtime->integer;
	} 
	else if ( com_timescale->value ) 
	{
		fraction=(float)msec;
		fraction*=com_timescale->value;
		msec=(int)floor(fraction);
		fraction-=(float)msec;
	}
	
	// don't let it scale below 1 msec
	if ( msec < 1 ) 
	{
		msec = 1;
		fraction=0.0f;
	}

	if ( com_skippingcin->integer ) {
		// we're skipping ahead so let it go a bit faster
		clampTime = 500;
	} else {
		// for local single player gaming
		// we may want to clamp the time to prevent players from
		// flying off edges when something hitches.
		clampTime = 200;
	}

	if ( msec > clampTime ) {
		msec = clampTime;
		fraction=0.0f;
	}

	return msec;
}

/*
=================
Com_Frame
=================
*/
static vec3_t corg;
static vec3_t cangles;
static bool bComma;
void Com_SetOrgAngles(vec3_t org,vec3_t angles)
{
	VectorCopy(org,corg);
	VectorCopy(angles,cangles);
}

#ifdef G2_PERFORMANCE_ANALYSIS
void G2Time_ResetTimers(void);
void G2Time_ReportTimers(void);
#endif

#pragma warning (disable: 4701)	//local may have been used without init (timing info vars)
void Com_Frame( void ) {
try
{
#ifdef _XBOX
	SPXB_HOT_INC(g_SPXBComFrameCount);
	SPXB_HOT_SET(g_SPXBBootPhase, 0x700);
	SPXB_HOT_SET(g_SPXBPhaseLast, 0x434F4D31); /* 'COM1' */
	SPXB_HOT_SET(g_SPXBComSubphase, 1);
#endif
	int		timeBeforeFirstEvents, timeBeforeServer, timeBeforeEvents, timeBeforeClient, timeAfter;
	int		msec, minMsec;
	static int	lastTime;
	static int	frameCount = 0;
	bool firstFrames = (frameCount < 8);
#ifdef _XBOX
	static int s_xboxLastComPhaseTime = 0;
	static bool s_xboxTraceComPhase = false;
	SPXB_HOT_SET(g_SPXBBootPhase, 0x701);
	SPXB_HOT_SET(g_SPXBComSubphase, 2);
	const int xboxPhaseNow = firstFrames ? Sys_Milliseconds() : 0;
	SPXB_HOT_SET(g_SPXBBootPhase, 0x702);
	SPXB_HOT_SET(g_SPXBComSubphase, 3);
	s_xboxTraceComPhase = firstFrames;
	if (s_xboxTraceComPhase)
	{
		s_xboxLastComPhaseTime = xboxPhaseNow;
		XBLF("JA: COM_PHASE frame=%d enter realtime=%d", frameCount, com_frameTime);
	}
#endif
	if (firstFrames) {
		XBLog_Write(va("JA: Com_Frame #%d entered", frameCount));
	}
	frameCount++;

	// write config file if anything changed
#ifndef _XBOX
	Com_WriteConfiguration(); 

	// if "viewlog" has been modified, show or hide the log console
	if ( com_viewlog->modified ) {
		Sys_ShowConsole( com_viewlog->integer, qfalse );
		com_viewlog->modified = qfalse;
	}
#endif

	//
	// main event loop
	//
	if ( com_speeds->integer ) {
		timeBeforeFirstEvents = Sys_Milliseconds ();
	}

#ifdef _XBOX
	if ( Sys_IsDirectMapBoot() && ( !com_sv_running || !com_sv_running->integer ) ) {
		g_SPXBBootPhase = 0x713;
		g_SPXBComSubphase = 0x13;
		g_SPXBMapPhase = 710;
		XBLog_Write("JA: COM_PHASE direct-map pre-event Cbuf_Execute");
		Cbuf_Execute();
		g_SPXBMapPhase = 711;
	}
#endif

	// we may want to spin here if things are going too fast
	if ( com_maxfps->integer > 0 ) {
		minMsec = 1000 / com_maxfps->integer;
	} else {
		minMsec = 1;
	}
#ifdef _XBOX
	int xboxFirstEventSpinCount = 0;
	SPXB_HOT_SET(g_SPXBBootPhase, 0x703);
	SPXB_HOT_SET(g_SPXBComSubphase, 4);
	SPXB_HOT_SET(g_SPXBComSpinCount, 0);
#endif
	do {
#ifdef _XBOX
		SPXB_HOT_SET(g_SPXBBootPhase, 0x704);
		SPXB_HOT_SET(g_SPXBComSubphase, 5);
		SPXB_HOT_SET(g_SPXBComSpinCount, (unsigned int)xboxFirstEventSpinCount);
		if (s_xboxTraceComPhase && xboxFirstEventSpinCount == 0) XBLog_Write("JA: COM_PHASE before first Com_EventLoop");
#endif
		com_frameTime = Com_EventLoop();
#ifdef _XBOX
		SPXB_HOT_SET(g_SPXBBootPhase, 0x705);
		SPXB_HOT_SET(g_SPXBComSubphase, 6);
		SPXB_HOT_SET(g_SPXBComFrameTime, (unsigned int)com_frameTime);
		SPXB_HOT_SET(g_SPXBComLastTime, (unsigned int)lastTime);
		if (s_xboxTraceComPhase && xboxFirstEventSpinCount == 0) XBLog_Write("JA: COM_PHASE after first Com_EventLoop");
#endif
		if ( lastTime > com_frameTime ) {
			lastTime = com_frameTime;		// possible on first frame
		}
		msec = com_frameTime - lastTime;
#ifdef _XBOX
		SPXB_HOT_SET(g_SPXBBootPhase, 0x706);
		SPXB_HOT_SET(g_SPXBComSubphase, 7);
		SPXB_HOT_SET(g_SPXBComMsec, (unsigned int)msec);
		SPXB_HOT_SET(g_SPXBComFrameTime, (unsigned int)com_frameTime);
		SPXB_HOT_SET(g_SPXBComLastTime, (unsigned int)lastTime);
		if ( msec < minMsec && ++xboxFirstEventSpinCount > 1024 )
		{
			com_frameTime = lastTime + minMsec;
			msec = minMsec;
			SPXB_HOT_SET(g_SPXBBootPhase, 0x707);
			SPXB_HOT_SET(g_SPXBComSubphase, 8);
			SPXB_HOT_SET(g_SPXBComMsec, (unsigned int)msec);
			SPXB_HOT_SET(g_SPXBComFrameTime, (unsigned int)com_frameTime);
			SPXB_HOT_SET(g_SPXBComSpinCount, (unsigned int)xboxFirstEventSpinCount);
			if (s_xboxTraceComPhase) XBLF("JA: COM_PHASE first event timer stalled; forced msec=%d", msec);
			break;
		}
#endif
	} while ( msec < minMsec );
#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBBootPhase, 0x708);
	SPXB_HOT_SET(g_SPXBComSubphase, 9);
	if (s_xboxTraceComPhase) XBLog_Write("JA: COM_PHASE before first Cbuf_Execute");
#endif
	Cbuf_Execute ();
#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBBootPhase, 0x709);
	SPXB_HOT_SET(g_SPXBComSubphase, 10);
	if (s_xboxTraceComPhase) XBLog_Write("JA: COM_PHASE after first Cbuf_Execute");
#endif

	lastTime = com_frameTime;
	SPXB_HOT_SET(g_SPXBComLastTime, (unsigned int)lastTime);

	// mess with msec if needed
	com_frameMsec = msec;
	float fractionMsec=0.0f;
	msec = Com_ModifyMsec( msec, fractionMsec);
	
	//
	// server side
	//
	if ( com_speeds->integer ) {
		timeBeforeServer = Sys_Milliseconds ();
	}

	if (firstFrames) XBLog_Write("JA: Com_Frame: SV_Frame...");
#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBBootPhase, 0x70A);
	SPXB_HOT_SET(g_SPXBComSubphase, 11);
	if (s_xboxTraceComPhase) XBLog_Write("JA: COM_PHASE before SV_Frame");
#endif
	SV_Frame (msec, fractionMsec);
#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBBootPhase, 0x70B);
	SPXB_HOT_SET(g_SPXBComSubphase, 12);
	if (s_xboxTraceComPhase) XBLog_Write("JA: COM_PHASE after SV_Frame");
#endif
	if (firstFrames) XBLog_Write("JA: Com_Frame: SV_Frame done");


	//
	// client system
	//
#ifdef _XBOX
	extern bool TestDemoTimer();
	extern void PlayDemo();
	if ( TestDemoTimer())
	{
		PlayDemo();
	}
#endif
//	if ( !com_dedicated->integer ) 
	{
		//
		// run event loop a second time to get server to client packets
		// without a frame of latency
		//
		if ( com_speeds->integer ) {
			timeBeforeEvents = Sys_Milliseconds ();
		}
#ifdef _XBOX
		SPXB_HOT_SET(g_SPXBBootPhase, 0x70C);
		SPXB_HOT_SET(g_SPXBComSubphase, 13);
		if (s_xboxTraceComPhase) XBLog_Write("JA: COM_PHASE before second Com_EventLoop");
#endif
		Com_EventLoop();
#ifdef _XBOX
		SPXB_HOT_SET(g_SPXBBootPhase, 0x70D);
		SPXB_HOT_SET(g_SPXBComSubphase, 14);
		if (s_xboxTraceComPhase) XBLog_Write("JA: COM_PHASE after second Com_EventLoop");
#endif
#ifdef _XBOX
		SPXB_HOT_SET(g_SPXBBootPhase, 0x70E);
		SPXB_HOT_SET(g_SPXBComSubphase, 15);
		if (s_xboxTraceComPhase) XBLog_Write("JA: COM_PHASE before second Cbuf_Execute");
#endif
		Cbuf_Execute ();
#ifdef _XBOX
		SPXB_HOT_SET(g_SPXBBootPhase, 0x70F);
		SPXB_HOT_SET(g_SPXBComSubphase, 16);
		if (s_xboxTraceComPhase) XBLog_Write("JA: COM_PHASE after second Cbuf_Execute");
#endif


		//
		// client side
		//
		if ( com_speeds->integer ) {
			timeBeforeClient = Sys_Milliseconds ();
		}

		if (firstFrames) XBLog_Write("JA: Com_Frame: CL_Frame...");
#ifdef _XBOX
		SPXB_HOT_SET(g_SPXBBootPhase, 0x710);
		SPXB_HOT_SET(g_SPXBComSubphase, 17);
		if (s_xboxTraceComPhase) XBLog_Write("JA: COM_PHASE before CL_Frame");
#endif
		CL_Frame (msec, fractionMsec);
#ifdef _XBOX
		SPXB_HOT_SET(g_SPXBBootPhase, 0x711);
		SPXB_HOT_SET(g_SPXBComSubphase, 18);
		if (s_xboxTraceComPhase) XBLog_Write("JA: COM_PHASE after CL_Frame");
#endif
		if (firstFrames) XBLog_Write("JA: Com_Frame: CL_Frame done");

		if ( com_speeds->integer ) {
			timeAfter = Sys_Milliseconds ();
		}
	}
	if (firstFrames) XBLog_Write(va("JA: Com_Frame #%d returning", frameCount-1));
#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBBootPhase, 0x712);
	SPXB_HOT_SET(g_SPXBComSubphase, 19);
	if (s_xboxTraceComPhase) XBLF("JA: COM_PHASE frame=%d exit", frameCount - 1);
#endif


	//
	// report timing information
	//
	if ( com_speeds->integer ) {
		int			all, sv, ev, cl;

		all = timeAfter - timeBeforeServer;
		sv = timeBeforeEvents - timeBeforeServer;
		ev = timeBeforeServer - timeBeforeFirstEvents + timeBeforeClient - timeBeforeEvents;
		cl = timeAfter - timeBeforeClient;
		sv -= time_game;
		cl -= time_frontend + time_backend;

		Com_Printf("fr:%i all:%3i sv:%3i ev:%3i cl:%3i gm:%3i tr:%3i pvs:%3i rf:%3i bk:%3i\n", 
					com_frameNumber, all, sv, ev, cl, time_game, timeInTrace, timeInPVSCheck, time_frontend, time_backend);

#ifndef _XBOX
		// speedslog
		if ( com_speedslog && com_speedslog->integer )
		{
			if(!speedslog)
			{
				speedslog = FS_FOpenFileWrite("speeds.log");
				FS_Write("data={\n", strlen("data={\n"), speedslog);
				bComma=false;
				if ( com_speedslog->integer > 1 ) 
				{
					// force it to not buffer so we get valid
					// data even if we are crashing
					FS_ForceFlush(logfile);
				}
			}
			if (speedslog)
			{
				char		msg[MAXPRINTMSG];

				if(bComma)
				{
					FS_Write(",\n", strlen(",\n"), speedslog);
					bComma=false;
				}
				FS_Write("{", strlen("{"), speedslog);
				Com_sprintf(msg,sizeof(msg),
							"%8.4f,%8.4f,%8.4f,%8.4f,%8.4f,%8.4f,",corg[0],corg[1],corg[2],cangles[0],cangles[1],cangles[2]);
				FS_Write(msg, strlen(msg), speedslog);
				Com_sprintf(msg,sizeof(msg),
					"%i,%3i,%3i,%3i,%3i,%3i,%3i,%3i,%3i,%3i}", 
					com_frameNumber, all, sv, ev, cl, time_game, timeInTrace, timeInPVSCheck, time_frontend, time_backend);
				FS_Write(msg, strlen(msg), speedslog);
				bComma=true;
			}
		}
#endif

		timeInTrace = timeInPVSCheck = 0;
	}

	//
	// trace optimization tracking
	//
	if ( com_showtrace->integer ) {
		extern	int c_traces, c_brush_traces, c_patch_traces;
		extern	int	c_pointcontents;

		/*
		Com_Printf( "%4i non-sv_traces, %4i sv_traces, %4i ms, ave %4.2f ms\n", c_traces - numTraces, numTraces, timeInTrace, (float)timeInTrace/(float)numTraces );
		timeInTrace = numTraces = 0;
		c_traces = 0;
		*/
		
		Com_Printf ("%4i traces  (%ib %ip) %4i points\n", c_traces,
			c_brush_traces, c_patch_traces, c_pointcontents);
		c_traces = 0;
		c_brush_traces = 0;
		c_patch_traces = 0;
		c_pointcontents = 0;
	}

	com_frameNumber++;
}//try
	catch (const char* reason) {
		Com_Printf (reason);
		return;			// an ERR_DROP was thrown
	}

#ifdef G2_PERFORMANCE_ANALYSIS
	if (com_G2Report && com_G2Report->integer)
	{
		G2Time_ReportTimers();
	}

	G2Time_ResetTimers();
#endif

	{
		static cvar_t *levelSelectCheat = NULL;
		if ( !levelSelectCheat ) {
			levelSelectCheat = Cvar_Get("levelSelectCheat", "-1", CVAR_SAVEGAME | CVAR_ARCHIVE);
		}
	}

#ifdef XBOX_DEMO
	// This is for the code that auto-reboots back to CDX after a timeout:
	extern void Demo_TimerUpdate( void );
	Demo_TimerUpdate();
#endif
}

#pragma warning (default: 4701)	//local may have been used without init

/*
=================
Com_Shutdown
=================
*/
extern void CM_FreeShaderText(void);
void Com_Shutdown (void) {
	CM_ClearMap();

#ifndef _XBOX
	CM_FreeShaderText();

	if (logfile) {
		FS_FCloseFile (logfile);
		logfile = 0;
	}

	if (speedslog) {
		FS_Write("\n};", strlen("\n};"), speedslog);
		FS_FCloseFile (speedslog);
		speedslog = 0;
	}

	if (camerafile) {
		FS_FCloseFile (camerafile);
		camerafile = 0;
	}

	if ( com_journalFile ) {
		FS_FCloseFile( com_journalFile );
		com_journalFile = 0;
	}
#endif

#ifdef _XBOX
	extern void Sys_StreamShutdown();
	Sys_StreamShutdown();
	Sys_ShutdownFileCodes();
#endif

//	SE_ShutDown();//close the string packages

	extern void Netchan_Shutdown();
	Netchan_Shutdown();
}

/*
============
ParseTextFile
============
*/

bool Com_ParseTextFile(const char *file, class CGenericParser2 &parser, bool cleanFirst)
{
	fileHandle_t	f;
	int				length = 0;
	char			*buf = 0, *bufParse = 0;

	length = FS_FOpenFileByMode( file, &f, FS_READ );
	if (!f || !length)		
	{
		return false;
	}

	buf = new char [length + 1];
	FS_Read( buf, length, f );
	buf[length] = 0;

	bufParse = buf;
	parser.Parse(&bufParse, cleanFirst);
	delete buf;

	FS_FCloseFile( f );

	return true;
}

void Com_ParseTextFileDestroy(class CGenericParser2 &parser)
{
	parser.Clean();
}

CGenericParser2 *Com_ParseTextFile(const char *file, bool cleanFirst, bool writeable)
{
	fileHandle_t	f;
	int				length = 0;
	char			*buf = 0, *bufParse = 0;
	CGenericParser2 *parse;

	length = FS_FOpenFileByMode( file, &f, FS_READ );
	if (!f || !length)		
	{
		return 0;
	}

	buf = new char [length + 1];
	FS_Read( buf, length, f );
	FS_FCloseFile( f );
	buf[length] = 0;

	bufParse = buf;

	parse = new CGenericParser2;
	if (!parse->Parse(&bufParse, cleanFirst, writeable))
	{
		delete parse;
		parse = 0;
	}

	delete buf;

	return parse;
}
