// cl_main.c  -- client main loop

// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"

#ifdef _XBOX
#include "../win32/xb_log.h"
extern "C" volatile unsigned int g_SPXBHeartbeatCount;
extern "C" volatile unsigned int g_SPXBHeartbeatFrame;
extern "C" volatile unsigned int g_SPXBHeartbeatRealtime;
extern "C" volatile unsigned int g_SPXBHeartbeatServerTime;
extern "C" volatile unsigned int g_SPXBHeartbeatFps10;
extern "C" volatile unsigned int g_SPXBClFrameCount;
extern "C" volatile unsigned int g_SPXBClsState;
extern "C" volatile unsigned int g_SPXBClServerTime;
extern "C" volatile unsigned int g_SPXBClsFrameCount;
extern "C" volatile unsigned int g_SPXBPhaseLast;
extern "C" volatile unsigned int g_SPXBMapPhase;
extern "C" volatile unsigned int g_SPXBClHunkState;
extern "C" volatile unsigned int g_SPXBClHunkCaller;
extern "C" volatile unsigned int g_SPXBClHunkCallCount;
extern "C" volatile unsigned int g_SPXBRenderDrawSurfLists;
extern "C" volatile unsigned int g_SPXBRenderSurfaces;
extern "C" volatile unsigned int g_SPXBRenderEndSurfaces;
extern "C" volatile unsigned int g_SPXBRenderBackendMsec;
extern "C" volatile unsigned int g_SPXBFakeGLPrimitiveCalls;
extern "C" volatile unsigned int g_SPXBFakeGLPrimitiveVerts;
extern "C" volatile unsigned int g_SPXBFakeGLStateFlushes;
extern "C" volatile unsigned int g_SPXBRenderSplitShader;
extern "C" volatile unsigned int g_SPXBRenderSplitFog;
extern "C" volatile unsigned int g_SPXBRenderSplitDlight;
extern "C" volatile unsigned int g_SPXBRenderSplitEntity;
extern "C" volatile unsigned int g_SPXBRenderSplitFinal;
extern "C" volatile unsigned int g_SPXBRenderSplitFlush;
extern bool Sys_IsDirectMapBoot(void);
#endif

#include "client.h"
#include "client_ui.h"
#include <limits.h>
#ifdef _IMMERSION
#include "../ff/ff.h"
#include "../ff/cl_ff.h"
#else
#include "fffx.h"
#endif // _IMMERSION
#include "../ghoul2/g2.h"

#include "../qcommon/xb_settings.h"

#include "../RMG/RM_Headers.h"

#ifdef _XBOX
#include "../ui/ui_splash.h"

#if !defined(FINAL_BUILD) && !defined(_XBOX_VC71_MIGRATION)
#include <d3d8perf.h>
#endif

#endif

#define	RETRANSMIT_TIMEOUT	3000	// time between connection packet retransmits

cvar_t	*cl_nodelta;
cvar_t	*cl_debugMove;

cvar_t	*cl_noprint;

cvar_t	*cl_timeout;
cvar_t	*cl_maxpackets;
cvar_t	*cl_packetdup;
cvar_t	*cl_timeNudge;
cvar_t	*cl_showTimeDelta;
cvar_t	*cl_newClock=0;

cvar_t	*cl_shownet;
cvar_t	*cl_avidemo;

cvar_t	*cl_pano;
cvar_t	*cl_panoNumShots;
cvar_t	*cl_skippingcin;
cvar_t	*cl_endcredits;

cvar_t	*cl_freelook;
cvar_t	*cl_sensitivity;
#ifdef _XBOX
cvar_t	*cl_sensitivityY;
#endif

//cvar_t	*cl_mouseAccel;
//cvar_t	*cl_showMouseRate;
cvar_t  *cl_VideoQuality;
cvar_t	*cl_VidFadeUp;	// deliberately kept as "Vid" rather than "Video" so tab-matching matches only VideoQuality
cvar_t	*cl_VidFadeDown;
cvar_t	*cl_framerate;

cvar_t	*m_pitch;
cvar_t	*m_yaw;
cvar_t	*m_forward;
cvar_t	*m_side;
//cvar_t	*m_filter;

#ifdef _XBOX
//MAP HACK
cvar_t	*cl_mapname;
qboolean vidRestartReloadMap = qfalse;
#endif

cvar_t	*cl_activeAction;

cvar_t	*cl_updateInfoString;

cvar_t	*cl_ingameVideo;

cvar_t	*cl_thumbStickMode;

clientActive_t		cl;
clientConnection_t	clc;
clientStatic_t		cls;

// Structure containing functions exported from refresh DLL
refexport_t	re;

ping_t	cl_pinglist[MAX_PINGREQUESTS];

void CL_ShutdownRef( void );
void CL_InitRef( void );
void CL_CheckForResend( void );

#if defined(_XBOX) && SP_XBOX_SMOKE_AUTOMATION
#define XBOX_SMOKE_MAX_SOAK_COMMANDS 16
#define XBOX_SMOKE_SOAK_COMMAND_TEXT 96

struct xboxSmokeSoakCommand_t
{
	int atMs;
	qboolean executed;
	char command[XBOX_SMOKE_SOAK_COMMAND_TEXT];
};

static xboxSmokeSoakCommand_t s_xboxSmokeSoakCommands[XBOX_SMOKE_MAX_SOAK_COMMANDS];
static int s_xboxSmokeSoakCommandCount = 0;
static qboolean s_xboxSmokeSoakCommandsLoaded = qfalse;
static int s_xboxSmokeSoakStartMs = 0;

static unsigned int CL_XboxSmokeHashText( const char *text )
{
	unsigned int hash = 2166136261u;
	if ( !text )
	{
		return 0;
	}
	while ( *text )
	{
		hash ^= (unsigned char)*text++;
		hash *= 16777619u;
	}
	return hash;
}

static void CL_XboxSmokeLoadSoakCommands( void )
{
	if ( s_xboxSmokeSoakCommandsLoaded )
	{
		return;
	}

	s_xboxSmokeSoakCommandsLoaded = qtrue;
	s_xboxSmokeSoakStartMs = Sys_Milliseconds();
	g_SPXBSoakCommandCount = 0;
	g_SPXBSoakCommandExecuted = 0;
	g_SPXBSoakCommandLastHash = 0;
	g_SPXBSoakCommandLastAtMs = 0;
	g_SPXBSoakCommandLastElapsed = 0;

	FILE *f = fopen( "D:\\ja_sp_soak_commands.txt", "r" );
	if ( !f )
	{
		return;
	}

	char line[160];
	while ( s_xboxSmokeSoakCommandCount < XBOX_SMOKE_MAX_SOAK_COMMANDS && fgets( line, sizeof( line ), f ) )
	{
		char *cursor = line;
		while ( *cursor == ' ' || *cursor == '\t' )
		{
			cursor++;
		}
		if ( !*cursor || *cursor == '#' || *cursor == '\r' || *cursor == '\n' )
		{
			continue;
		}

		char *timeToken = cursor;
		if ( *cursor == '+' )
		{
			cursor++;
		}
		if ( *cursor < '0' || *cursor > '9' )
		{
			XBLF( "JA: SOAK_COMMAND invalid time line '%s'", line );
			continue;
		}

		while ( *cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\r' && *cursor != '\n' )
		{
			cursor++;
		}
		if ( *cursor )
		{
			*cursor++ = '\0';
		}
		while ( *cursor == ' ' || *cursor == '\t' )
		{
			cursor++;
		}
		if ( !*cursor || *cursor == '\r' || *cursor == '\n' )
		{
			XBLF( "JA: SOAK_COMMAND missing command at '%s'", timeToken );
			continue;
		}

		char *end = cursor + strlen( cursor );
		while ( end > cursor && ( end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t' ) )
		{
			*--end = '\0';
		}

		xboxSmokeSoakCommand_t *event = &s_xboxSmokeSoakCommands[s_xboxSmokeSoakCommandCount++];
		event->atMs = atoi( timeToken );
		if ( event->atMs < 0 )
		{
			event->atMs = 0;
		}
		event->executed = qfalse;
		Q_strncpyz( event->command, cursor, sizeof( event->command ) );
		XBLF( "JA: SOAK_COMMAND loaded idx=%d at=%d command='%s'",
			s_xboxSmokeSoakCommandCount - 1, event->atMs, event->command );
	}

	fclose( f );
	g_SPXBSoakCommandCount = (unsigned int)s_xboxSmokeSoakCommandCount;
	XBLF( "JA: SOAK_COMMAND loaded count=%d startMs=%d", s_xboxSmokeSoakCommandCount, s_xboxSmokeSoakStartMs );
}

static void CL_XboxSmokeSoakCommandTick( void )
{
	CL_XboxSmokeLoadSoakCommands();
	if ( s_xboxSmokeSoakCommandCount <= 0 )
	{
		return;
	}

	const int now = Sys_Milliseconds();
	const int elapsed = now - s_xboxSmokeSoakStartMs;
	for ( int i = 0; i < s_xboxSmokeSoakCommandCount; ++i )
	{
		xboxSmokeSoakCommand_t *event = &s_xboxSmokeSoakCommands[i];
		if ( event->executed || elapsed < event->atMs )
		{
			continue;
		}

		event->executed = qtrue;
		++g_SPXBSoakCommandExecuted;
		g_SPXBSoakCommandLastHash = CL_XboxSmokeHashText( event->command );
		g_SPXBSoakCommandLastAtMs = (unsigned int)event->atMs;
		g_SPXBSoakCommandLastElapsed = (unsigned int)elapsed;
		XBLF( "JA: SOAK_COMMAND execute elapsed=%d idx=%d at=%d state=%d serverTime=%d command='%s'",
			elapsed, i, event->atMs, (int)cls.state, cl.serverTime, event->command );
		Cbuf_ExecuteText( EXEC_APPEND, event->command );
		Cbuf_ExecuteText( EXEC_APPEND, "\n" );
	}
}

static qboolean CL_XboxAutoSmokeEnabled( void )
{
	static qboolean s_checked = qfalse;
	static qboolean s_enabled = qfalse;

	if ( !s_checked )
	{
		FILE *marker = fopen( "D:\\ja_sp_autosmoke.txt", "r" );
		s_checked = qtrue;
		if ( marker )
		{
			fclose( marker );
			s_enabled = qtrue;
			XBLog_Write( "JA: SP autosmoke enabled by D:\\ja_sp_autosmoke.txt" );
		}
		else
		{
			XBLog_Write( "JA: SP autosmoke disabled; marker missing" );
		}
	}

	return s_enabled;
}

static void CL_XboxAutoSmokePressKey( int key, const char *name )
{
	XBLF( "JA: SP autosmoke press %s key=%d state=%d keyCatchers=0x%x realtime=%d",
		name, key, (int)cls.state, (unsigned int)cls.keyCatchers, cls.realtime );
	CL_KeyEvent( key, qtrue, cls.realtime );
	CL_KeyEvent( key, qfalse, cls.realtime + 1 );
}

static void CL_XboxAutoSmokeTick( void )
{
	static qboolean s_done = qfalse;
	static int s_lastPressTime = -2000;
	static int s_pressCount = 0;
	static int s_lastState = -1;
	static int s_lastKeyCatchers = -1;
	static qboolean s_loggedLoadStop = qfalse;
	static qboolean s_loggedPlayerControl = qfalse;
	static qboolean s_seenPostLoadCinematic = qfalse;
	static qboolean s_loggedWaitingForCinematic = qfalse;
	const int maxPresses = 24;
	const int pressIntervalMsec = 1500;

	if ( s_done || !CL_XboxAutoSmokeEnabled() )
	{
		return;
	}

	if ( s_lastState != (int)cls.state || s_lastKeyCatchers != (int)cls.keyCatchers )
	{
		s_lastState = (int)cls.state;
		s_lastKeyCatchers = (int)cls.keyCatchers;
		XBLF( "JA: SP autosmoke state state=%d keyCatchers=0x%x ui=%d cgame=%d sv=%d presses=%d",
			(int)cls.state, (unsigned int)cls.keyCatchers, (int)cls.uiStarted,
			(int)cls.cgameStarted, (int)com_sv_running->integer, s_pressCount );
	}

	if ( cls.state >= CA_LOADING )
	{
		if ( cls.state == CA_CINEMATIC || CL_IsRunningInGameCinematic() )
		{
			if ( !s_seenPostLoadCinematic )
			{
				s_seenPostLoadCinematic = qtrue;
				XBLF( "JA: SP autosmoke observed post-load cinematic state=%d", (int)cls.state );
			}
		}

		if ( !s_loggedLoadStop )
		{
			s_loggedLoadStop = qtrue;
			XBLF( "JA: SP autosmoke reached load/game state=%d; input automation paused", (int)cls.state );
		}

		if ( cls.state == CA_ACTIVE )
		{
			extern bool in_camera;
			if ( !in_camera && !s_loggedPlayerControl )
			{
				if ( s_seenPostLoadCinematic )
				{
					s_loggedPlayerControl = qtrue;
					s_done = qtrue;
					XBLog_Write( "JA: SP autosmoke reached post-cinematic CA_ACTIVE with in_camera=0; player control likely available" );
				}
				else if ( !s_loggedWaitingForCinematic )
				{
					s_loggedWaitingForCinematic = qtrue;
					XBLog_Write( "JA: SP autosmoke reached early CA_ACTIVE with in_camera=0; waiting for post-load cinematic" );
				}
			}
		}
		return;
	}

	if ( s_pressCount >= maxPresses )
	{
		s_done = qtrue;
		XBLF( "JA: SP autosmoke stopped after max presses state=%d keyCatchers=0x%x",
			(int)cls.state, (unsigned int)cls.keyCatchers );
		return;
	}

	if ( cls.realtime - s_lastPressTime < pressIntervalMsec )
	{
		return;
	}

	if ( ( cls.keyCatchers & KEYCATCH_UI ) ||
		cls.state == CA_CINEMATIC || CL_IsRunningInGameCinematic() )
	{
		const qboolean useMouseAccept = ( ( s_pressCount & 1 ) == 0 );
		s_lastPressTime = cls.realtime;
		++s_pressCount;
		CL_XboxAutoSmokePressKey( useMouseAccept ? A_MOUSE1 : A_ENTER,
			useMouseAccept ? "A_MOUSE1" : "A_ENTER" );
	}
}
#endif

/*
=======================================================================

CLIENT RELIABLE COMMAND COMMUNICATION

=======================================================================
*/

/*
======================
CL_AddReliableCommand

The given command will be transmitted to the server, and is gauranteed to
not have future usercmd_t executed before it is executed
======================
*/
void CL_AddReliableCommand( const char *cmd ) {
	int		index;

	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	if ( clc.reliableSequence - clc.reliableAcknowledge > MAX_RELIABLE_COMMANDS ) {
		Com_Error( ERR_DROP, "Client command overflow" );
	}
	clc.reliableSequence++;
	index = clc.reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	if ( clc.reliableCommands[ index ] ) {
		Z_Free( clc.reliableCommands[ index ] );
	}
	clc.reliableCommands[ index ] = CopyString( cmd );
}

//======================================================================

/*
==================
CL_NextDemo

Called when a demo or cinematic finishes
If the "nextdemo" cvar is set, that command will be issued
==================
*/
void CL_NextDemo( void ) {
	char	v[MAX_STRING_CHARS];

	Q_strncpyz( v, Cvar_VariableString ("nextdemo"), sizeof(v) );
	v[MAX_STRING_CHARS-1] = 0;
	Com_DPrintf("CL_NextDemo: %s\n", v );
	if (!v[0]) {
		return;
	}

	Cvar_Set ("nextdemo","");
	Cbuf_AddText (v);
	Cbuf_AddText ("\n");
	Cbuf_Execute();
}

//======================================================================

/*
=================
CL_FlushMemory

Called by CL_MapLoading, CL_Connect_f, and CL_ParseGamestate the only
ways a client gets into a game
Also called by Com_Error
=================
*/
void CL_FlushMemory( void ) {

#ifdef _XBOX
	XBLog_SoakTrace("CL_FlushMemory", "enter", cls.servername,
		(int)cls.state, (int)cls.rendererStarted, (int)cls.soundStarted, (int)cls.cgameStarted);
	XBLog_SoakTrace("CL_FlushMemory", "before-S_DisableSounds", cls.servername,
		(int)cls.state, (int)cls.soundRegistered, (int)cls.uiStarted, (int)cls.cgameStarted);
#endif
	// clear sounds (moved higher up within this func to avoid the odd sound stutter)
	S_DisableSounds();
#ifdef _XBOX
	XBLog_SoakTrace("CL_FlushMemory", "after-S_DisableSounds", cls.servername,
		(int)cls.state, (int)cls.soundRegistered, (int)cls.soundStarted, (int)cls.cgameStarted);
	XBLog_SoakTrace("CL_FlushMemory", "before-CL_ShutdownCGame", cls.servername,
		(int)cls.state, (int)cls.rendererStarted, (int)cls.soundStarted, (int)cls.cgameStarted);
#endif

	// unload the old VM
	CL_ShutdownCGame();
#ifdef _XBOX
	XBLog_SoakTrace("CL_FlushMemory", "after-CL_ShutdownCGame", cls.servername,
		(int)cls.state, (int)cls.rendererStarted, (int)cls.soundStarted, (int)cls.cgameStarted);
	XBLog_SoakTrace("CL_FlushMemory", "before-CL_ShutdownUI", cls.servername,
		(int)cls.state, (int)cls.uiStarted, (int)cls.keyCatchers, (int)cls.cgameStarted);
#endif

	CL_ShutdownUI();
#ifdef _XBOX
	XBLog_SoakTrace("CL_FlushMemory", "after-CL_ShutdownUI", cls.servername,
		(int)cls.state, (int)cls.uiStarted, (int)cls.keyCatchers, (int)cls.cgameStarted);
#endif

	if ( re.Shutdown ) {
#ifdef _XBOX
		XBLog_SoakTrace("CL_FlushMemory", "before-re.Shutdown", cls.servername,
			(int)cls.state, (int)cls.rendererStarted, (int)cls.glconfig.vidWidth, (int)cls.cgameStarted);
#endif
		re.Shutdown( qfalse );		// don't destroy window or context
#ifdef _XBOX
		XBLog_SoakTrace("CL_FlushMemory", "after-re.Shutdown", cls.servername,
			(int)cls.state, (int)cls.rendererStarted, (int)cls.soundStarted, (int)cls.cgameStarted);
#endif
	}

	//rwwFIXMEFIXME: The game server appears to continue running, so clearing common bsp data causes crashing and other bad things
	/*
	CM_ClearMap();
	*/

	cls.soundRegistered = qfalse;
	cls.rendererStarted = qfalse;
#ifdef _IMMERSION
	CL_ShutdownFF();
	cls.forceStarted = qfalse;
#endif // _IMMERSION
#ifdef _XBOX
	XBLog_SoakTrace("CL_FlushMemory", "done", cls.servername,
		(int)cls.state, (int)cls.rendererStarted, (int)cls.soundRegistered, (int)cls.cgameStarted);
#endif
}

/*
=====================
CL_MapLoading

A local server is starting to load a map, so update the
screen to let the user know about it, then dump all client
memory on the hunk from cgame, ui, and renderer
=====================
*/
void CL_MapLoading( void ) {
	XBLog_Write("JA: CL_MapLoading entered");
#ifdef _XBOX
	XBLog_SoakTrace("CL_MapLoading", "enter", cls.servername,
		(int)cls.state, (int)(com_cl_running ? com_cl_running->integer : -1),
		(int)cls.rendererStarted, (int)cls.cgameStarted);
#endif
	if ( !com_cl_running->integer ) {
#ifdef _XBOX
		XBLog_SoakTrace("CL_MapLoading", "cl-not-running", cls.servername,
			(int)cls.state, 0, 0, 0);
#endif
		return;
	}

#ifdef _XBOX
	XBLog_SoakTrace("CL_MapLoading", "before-close-console", cls.servername,
		(int)cls.state, (int)cls.keyCatchers, (int)cls.uiStarted, (int)cls.cgameStarted);
#endif
	Con_Close();
	cls.keyCatchers = 0;

	// if we are already connected to the local host, stay connected
	if ( cls.state >= CA_CONNECTED && !Q_stricmp( cls.servername, "localhost" ) )  {
#ifdef _XBOX
		XBLog_SoakTrace("CL_MapLoading", "localhost-connected-path", cls.servername,
			(int)cls.state, (int)clc.lastPacketSentTime, (int)cl.gameState.dataCount, (int)cl.serverTime);
#endif
		cls.state = CA_CONNECTED;		// so the connect screen is drawn
		memset( cls.updateInfoString, 0, sizeof( cls.updateInfoString ) );
//		memset( clc.serverMessage, 0, sizeof( clc.serverMessage ) );
		memset( &cl.gameState, 0, sizeof( cl.gameState ) );
		clc.lastPacketSentTime = -9999;
#ifdef _XBOX
		XBLog_SoakTrace("CL_MapLoading", "before-local-SCR_UpdateScreen", cls.servername,
			(int)cls.state, (int)clc.lastPacketSentTime, (int)cls.keyCatchers, (int)cls.cgameStarted);
#endif
		SCR_UpdateScreen();
#ifdef _XBOX
		XBLog_SoakTrace("CL_MapLoading", "after-local-SCR_UpdateScreen", cls.servername,
			(int)cls.state, (int)clc.lastPacketSentTime, (int)cls.keyCatchers, (int)cls.cgameStarted);
#endif
	} else {
		// clear nextmap so the cinematic shutdown doesn't execute it
		Cvar_Set( "nextmap", "" );
#ifdef _XBOX	// This was done at E3 time - it's nasty, but we may just keep it.
		connstate_t oldState = cls.state;
		cls.state = CA_CHALLENGING;
		XBLog_Write("JA: CL_MapLoading transitional SCR_UpdateScreen begin");
		XBLog_SoakTrace("CL_MapLoading", "before-transitional-screen", cls.servername,
			(int)oldState, (int)cls.state, (int)cls.keyCatchers, (int)cls.cgameStarted);
		SCR_UpdateScreen();
		XBLog_Write("JA: CL_MapLoading transitional SCR_UpdateScreen done");
		XBLog_SoakTrace("CL_MapLoading", "after-transitional-screen", cls.servername,
			(int)oldState, (int)cls.state, (int)cls.keyCatchers, (int)cls.cgameStarted);
		cls.state = oldState;
#endif
#ifdef _XBOX
		XBLog_SoakTrace("CL_MapLoading", "before-CL_Disconnect", cls.servername,
			(int)cls.state, (int)cls.rendererStarted, (int)cls.soundStarted, (int)cls.cgameStarted);
#endif
		CL_Disconnect();
#ifdef _XBOX
		XBLog_SoakTrace("CL_MapLoading", "after-CL_Disconnect", cls.servername,
			(int)cls.state, (int)cls.rendererStarted, (int)cls.soundStarted, (int)cls.cgameStarted);
#endif
		Q_strncpyz( cls.servername, "localhost", sizeof(cls.servername) );
		cls.state = CA_CHALLENGING;		// so the connect screen is drawn
		cls.keyCatchers = 0;
#ifndef _XBOX
		SCR_UpdateScreen();
#endif
		clc.connectTime = -RETRANSMIT_TIMEOUT;
		NET_StringToAdr( cls.servername, &clc.serverAddress);
		// we don't need a challenge on the localhost

		CL_CheckForResend();
	}

#ifdef _XBOX
	XBLog_SoakTrace("CL_MapLoading", "before-CL_FlushMemory", cls.servername,
		(int)cls.state, (int)cls.rendererStarted, (int)cls.soundRegistered, (int)cls.cgameStarted);
#endif
	CL_FlushMemory();
#ifdef _XBOX
	XBLog_SoakTrace("CL_MapLoading", "after-CL_FlushMemory", cls.servername,
		(int)cls.state, (int)cls.rendererStarted, (int)cls.soundRegistered, (int)cls.cgameStarted);
#endif
}

/*
=====================
CL_ClearState

Called before parsing a gamestate
=====================
*/
void CL_ClearState (void) {
	CL_ShutdownCGame();

	S_StopAllSounds();

	memset( &cl, 0, sizeof( cl ) );
}

/*
=====================
CL_FreeReliableCommands

Wipes all reliableCommands strings from clc
=====================
*/
void CL_FreeReliableCommands( void )
{
	// wipe the client connection
	for ( int i = 0 ; i < MAX_RELIABLE_COMMANDS ; i++ ) {
		if ( clc.reliableCommands[i] ) {
			Z_Free( clc.reliableCommands[i] );
		 	clc.reliableCommands[i] = NULL;
		}
	}
}


/*
=====================
CL_Disconnect

Called when a connection, or cinematic is being terminated.
Goes from a connected state to either a menu state or a console state
Sends a disconnect message to the server
This is also called on Com_Error and Com_Quit, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect( void ) {
	XBLog_Write("JA: CL_Disconnect entered");
	if ( !com_cl_running || !com_cl_running->integer ) {
		XBLog_Write("JA: CL_Disconnect - cl not running, early return");
		return;
	}

#ifdef _XBOX
	XBLog_Write("JA: CL_Disconnect - Cvar_Set r_norefresh");
	Cvar_Set("r_norefresh", "0");

	// Make sure to stop all rumbling! - Prevents bug when quitting game during rumble:
	XBLog_Write("JA: CL_Disconnect - IN_KillRumbleScripts");
	extern void IN_KillRumbleScripts( void );
	IN_KillRumbleScripts();
#endif

	XBLog_Write("JA: CL_Disconnect - UI_SetActiveMenu");
	if (cls.uiStarted)
		UI_SetActiveMenu( NULL,NULL );

	XBLog_Write("JA: CL_Disconnect - SCR_StopCinematic");
	SCR_StopCinematic ();
	XBLog_Write("JA: CL_Disconnect - S_ClearSoundBuffer");
	S_ClearSoundBuffer();

#ifdef _XBOX
	XBLog_Write("JA: CL_Disconnect - R_DeleteTextures");
	R_DeleteTextures();
#endif

	// send a disconnect message to the server
	// send it a few times in case one is dropped
	if ( cls.state >= CA_CONNECTED ) {
		CL_AddReliableCommand( "disconnect" );
		CL_WritePacket();
		CL_WritePacket();
		CL_WritePacket();
	}

	XBLog_Write("JA: CL_Disconnect - CL_ClearState");
	CL_ClearState ();

	CL_FreeReliableCommands();

	extern void CL_FreeServerCommands(void);
	CL_FreeServerCommands();

	memset( &clc, 0, sizeof( clc ) );

	cls.state = CA_DISCONNECTED;

	// allow cheats locally
	Cvar_Set( "timescale", "1" );//jic we were skipping
	Cvar_Set( "skippingCinematic", "0" );//jic we were skipping
	XBLog_Write("JA: CL_Disconnect done");
}


/*
===================
CL_ForwardCommandToServer

adds the current command line as a clientCommand
things like godmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
===================
*/
void CL_ForwardCommandToServer( void ) {
	char	*cmd;
	char	string[MAX_STRING_CHARS];

	cmd = Cmd_Argv(0);

	// ignore key up commands
	if ( cmd[0] == '-' ) {
		return;
	}

	if ( cls.state != CA_ACTIVE || cmd[0] == '+' ) {
		Com_Printf ("Unknown command \"%s\"\n", cmd);
		return;
	}

	if ( Cmd_Argc() > 1 ) {
		Com_sprintf( string, sizeof(string), "%s %s", cmd, Cmd_Args() );
	} else {
		Q_strncpyz( string, cmd, sizeof(string) );
	}

	CL_AddReliableCommand( string );
}


/*
======================================================================

CONSOLE COMMANDS

======================================================================
*/

/*
==================
CL_ForwardToServer_f
==================
*/
void CL_ForwardToServer_f( void ) {
	if ( cls.state != CA_ACTIVE ) {
		Com_Printf ("Not connected to a server.\n");
		return;
	}
	
	// don't forward the first argument
	if ( Cmd_Argc() > 1 ) {
		CL_AddReliableCommand( Cmd_Args() );
	}
}

/*
==================
CL_Disconnect_f
==================
*/
void CL_Disconnect_f( void ) {
	SCR_StopCinematic();	

	//FIXME:
	// TA codebase added additional CA_CINEMATIC check below, presumably so they could play cinematics
	//	in the menus when disconnected, although having the SCR_StopCinematic() call above is weird.
	// Either there's a bug, or the new version of that function conditionally-doesn't stop cinematics...
	//
	if ( cls.state != CA_DISCONNECTED && cls.state != CA_CINEMATIC ) {
		Com_Error (ERR_DISCONNECT, "Disconnected from server");
	}
}


/*
=================
CL_Vid_Restart_f

Restart the video subsystem
=================
*/
void CL_Vid_Restart_f( void ) {
	S_StopAllSounds();		// don't let them loop during the restart
	S_BeginRegistration();	// all sound handles are now invalid
	CL_ShutdownRef();
	CL_ShutdownUI();
	CL_ShutdownCGame();

	//rww - sof2mp does this here, but it seems to cause problems in this codebase.
//	CM_ClearMap();

	CL_InitRef();

	cls.rendererStarted = qfalse;
	cls.uiStarted = qfalse;
	cls.cgameStarted = qfalse;
	cls.soundRegistered = qfalse;

#ifdef _IMMERSION
	CL_ShutdownFF();
	cls.forceStarted = qfalse;
#endif // _IMMERSION

#ifdef _XBOX
	vidRestartReloadMap = qtrue;
#endif

	// unpause so the cgame definately gets a snapshot and renders a frame
	Cvar_Set( "cl_paused", "0" );
}

/*
=================
CL_Snd_Restart_f

Restart the sound subsystem
The cgame and game must also be forced to restart because
handles will be invalid
=================
*/
void CL_Snd_Restart_f( void ) {
	S_Shutdown();

	S_Init();

//	CL_Vid_Restart_f();

	extern qboolean	s_soundMuted;
	s_soundMuted = qfalse;		// we can play again

	S_RestartMusic();

	extern void S_ReloadAllUsedSounds(void);
	S_ReloadAllUsedSounds();

	extern void AS_ParseSets(void);
	AS_ParseSets();
}
#ifdef _IMMERSION
/*
=================
CL_FF_Restart_f
=================
*/
void CL_FF_Restart_f( void ) {

	if ( FF_IsInitialized() )
	{
		// Apply cvar changes w/o losing registered effects
		// Allows changing devices in-game without restarting the map
		if ( !FF_Init() )
			FF_Shutdown();	// error (shouldn't happen)
	}
	else if ( cls.state >= CA_PRIMED )	// maybe > CA_DISCONNECTED
	{
		// Restart map or menu
		CL_Vid_Restart_f();
	}
	else if ( cls.uiStarted )
	{
		// Restart menu
		CL_ShutdownUI();
		cls.forceStarted = qfalse;
	}
}
#endif // _IMMERSION
/*
==================
CL_Configstrings_f
==================
*/
void CL_Configstrings_f( void ) {
	int		i;
	int		ofs;

	if ( cls.state != CA_ACTIVE ) {
		Com_Printf( "Not connected to a server.\n");
		return;
	}

	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		ofs = cl.gameState.stringOffsets[ i ];
		if ( !ofs ) {
			continue;
		}
		Com_Printf( "%4i: %s\n", i, cl.gameState.stringData + ofs );
	}
}

/*
==============
CL_Clientinfo_f
==============
*/
void CL_Clientinfo_f( void ) {
	Com_Printf( "--------- Client Information ---------\n" );
	Com_Printf( "state: %i\n", cls.state );
	Com_Printf( "Server: %s\n", cls.servername );
	Com_Printf ("User info settings:\n");
	Info_Print( Cvar_InfoString( CVAR_USERINFO ) );
	Com_Printf( "--------------------------------------\n" );
}


//====================================================================

void UI_UpdateConnectionString( char *string );

/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out
=================
*/
void CL_CheckForResend( void ) {
	int		port;
	char	info[MAX_INFO_STRING];

//	if ( cls.state == CA_CINEMATIC )  
	if ( cls.state == CA_CINEMATIC || CL_IsRunningInGameCinematic())
	{
		return;
	}

	// resend if we haven't gotten a reply yet
	if ( cls.state < CA_CONNECTING || cls.state > CA_CHALLENGING ) {
		return;
	}

	if ( cls.realtime - clc.connectTime < RETRANSMIT_TIMEOUT ) {
		return;
	}

	clc.connectTime = cls.realtime;	// for retransmit requests
	clc.connectPacketCount++;

	// requesting a challenge
	switch ( cls.state ) {
	case CA_CONNECTING:
		UI_UpdateConnectionString( va("(%i)", clc.connectPacketCount ) );

		NET_OutOfBandPrint(NS_CLIENT, clc.serverAddress, "getchallenge");
		break;

	case CA_CHALLENGING:
	// sending back the challenge
		port = Cvar_VariableIntegerValue("qport");

//		UI_UpdateConnectionString( va("(%i)", clc.connectPacketCount ) );

		Q_strncpyz( info, Cvar_InfoString( CVAR_USERINFO ), sizeof( info ) );
		Info_SetValueForKey( info, "protocol", va("%i", PROTOCOL_VERSION ) );
		Info_SetValueForKey( info, "qport", va("%i", port ) );
		Info_SetValueForKey( info, "challenge", va("%i", clc.challenge ) );
		NET_OutOfBandPrint( NS_CLIENT, clc.serverAddress, "connect \"%s\"", info );
		// the most current userinfo has been sent, so watch for any
		// newer changes to userinfo variables
		cvar_modifiedFlags &= ~CVAR_USERINFO;
		break;

	default:
		Com_Error( ERR_FATAL, "CL_CheckForResend: bad cls.state" );
	}
}


/*
===================
CL_DisconnectPacket

Sometimes the server can drop the client and the netchan based
disconnect can be lost.  If the client continues to send packets
to the server, the server will send out of band disconnect packets
to the client so it doesn't have to wait for the full timeout period.
===================
*/
void CL_DisconnectPacket( netadr_t from ) {
	if ( cls.state != CA_ACTIVE ) {
		return;
	}

	// if not from our server, ignore it
	if ( !NET_CompareAdr( from, clc.netchan.remoteAddress ) ) {
		return;
	}

	// if we have received packets within three seconds, ignore it
	// (it might be a malicious spoof)
	if ( cls.realtime - clc.lastPacketTime < 3000 ) {
		return;
	}

	// drop the connection (FIXME: connection dropped dialog)
	Com_Printf( "Server disconnected for unknown reason\n" );
	CL_Disconnect();
}


/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc
=================
*/
void CL_ConnectionlessPacket( netadr_t from, msg_t *msg ) {
	char	*s;
	char	*c;
	
	MSG_BeginReading( msg );
	MSG_ReadLong( msg );	// skip the -1

	s = MSG_ReadStringLine( msg );

	Cmd_TokenizeString( s );

	c = Cmd_Argv(0);

	Com_DPrintf ("CL packet %s: %s\n", NET_AdrToString(from), c);

	// challenge from the server we are connecting to
	if ( !strcmp(c, "challengeResponse") ) {
		if ( cls.state != CA_CONNECTING ) {
			Com_Printf( "Unwanted challenge response received.  Ignored.\n" );
		} else {
			// start sending challenge repsonse instead of challenge request packets
			clc.challenge = atoi(Cmd_Argv(1));
			cls.state = CA_CHALLENGING;
			clc.connectPacketCount = 0;
			clc.connectTime = -99999;

			// take this address as the new server address.  This allows
			// a server proxy to hand off connections to multiple servers
			clc.serverAddress = from;
		}
		return;
	}

	// server connection
	if ( !strcmp(c, "connectResponse") ) {
		if ( cls.state >= CA_CONNECTED ) {
			Com_Printf ("Dup connect received.  Ignored.\n");
			return;
		}
		if ( cls.state != CA_CHALLENGING ) {
			Com_Printf ("connectResponse packet while not connecting.  Ignored.\n");
			return;
		}
		if ( !NET_CompareBaseAdr( from, clc.serverAddress ) ) {
			Com_Printf( "connectResponse from a different address.  Ignored.\n" );
			Com_Printf( "%s should have been %s\n", NET_AdrToString( from ), 
				NET_AdrToString( clc.serverAddress ) );
			return;
		}
		Netchan_Setup (NS_CLIENT, &clc.netchan, from, Cvar_VariableIntegerValue( "qport" ) );
		cls.state = CA_CONNECTED;
		clc.lastPacketSentTime = -9999;		// send first packet immediately
		return;
	}

	// a disconnect message from the server, which will happen if the server
	// dropped the connection but it is still getting packets from us
	if (!strcmp(c, "disconnect")) {
		CL_DisconnectPacket( from );
		return;
	}

	// echo request from server
	if ( !strcmp(c, "echo") ) {
		NET_OutOfBandPrint( NS_CLIENT, from, "%s", Cmd_Argv(1) );
		return;
	}

	// print request from server
	if ( !strcmp(c, "print") ) {
		s = MSG_ReadString( msg );
		UI_UpdateConnectionMessageString( s );
		Com_Printf( "%s", s );
		return;
	}


	Com_DPrintf ("Unknown connectionless packet command.\n");
}


/*
=================
CL_PacketEvent

A packet has arrived from the main event loop
=================
*/
void CL_PacketEvent( netadr_t from, msg_t *msg ) {
	int		headerBytes;

	clc.lastPacketTime = cls.realtime;
#ifdef _XBOX
	static int s_xboxCLPacketLogs = 0;
	if (SP_XBOX_VERBOSE_RUNTIME_LOGS && s_xboxCLPacketLogs < 32)
	{
		Com_PrintfAlways("JA: CL_PacketEvent enter fromType=%d size=%d state=%d read=%d\n",
			(int)from.type, msg ? msg->cursize : -1, (int)cls.state, msg ? msg->readcount : -1);
		++s_xboxCLPacketLogs;
	}
#endif

	if ( msg->cursize >= 4 && *(int *)msg->data == -1 ) {
		CL_ConnectionlessPacket( from, msg );
		return;
	}

	if ( cls.state < CA_CONNECTED ) {
		return;		// can't be a valid sequenced packet
	}

	if ( msg->cursize < 8 ) {
		Com_Printf ("%s: Runt packet\n",NET_AdrToString( from ));
		return;
	}

	//
	// packet from server
	//
	if ( !NET_CompareAdr( from, clc.netchan.remoteAddress ) ) {
		Com_DPrintf ("%s:sequenced packet without connection\n"
			,NET_AdrToString( from ) );
		// FIXME: send a client disconnect?
		return;
	}

	if (!Netchan_Process( &clc.netchan, msg) ) {
		return;		// out of order, duplicated, etc
	}
#ifdef _XBOX
	if (SP_XBOX_VERBOSE_RUNTIME_LOGS && s_xboxCLPacketLogs < 32)
	{
		Com_PrintfAlways("JA: CL_PacketEvent Netchan_Process ok read=%d cursize=%d\n",
			msg->readcount, msg->cursize);
		++s_xboxCLPacketLogs;
	}
#endif

	// the header is different lengths for reliable and unreliable messages
	headerBytes = msg->readcount;

	clc.lastPacketTime = cls.realtime;
	CL_ParseServerMessage( msg );
#ifdef _XBOX
	if (SP_XBOX_VERBOSE_RUNTIME_LOGS && s_xboxCLPacketLogs < 32)
	{
		Com_PrintfAlways("JA: CL_PacketEvent parse done state=%d\n", (int)cls.state);
		++s_xboxCLPacketLogs;
	}
#endif
}

/*
==================
CL_CheckTimeout

==================
*/
void CL_CheckTimeout( void ) {
	//
	// check timeout
	//
	if ( ( !cl_paused->integer || !sv_paused->integer ) 
//		&& cls.state >= CA_CONNECTED && cls.state != CA_CINEMATIC
		&& cls.state >= CA_CONNECTED && (cls.state != CA_CINEMATIC && !CL_IsRunningInGameCinematic())
		&& cls.realtime - clc.lastPacketTime > cl_timeout->value*1000) {
		if (++cl.timeoutcount > 5) {	// timeoutcount saves debugger
			Com_Printf ("\nServer connection timed out.\n");
			CL_Disconnect ();
			return;
		}
	} else {
		cl.timeoutcount = 0;
	}
}


//============================================================================

/*
==================
CL_CheckUserinfo

==================
*/
void CL_CheckUserinfo( void ) {
	if ( cls.state < CA_CHALLENGING ) {
		return;
	}

	// send a reliable userinfo update if needed
	if ( cvar_modifiedFlags & CVAR_USERINFO ) {
		cvar_modifiedFlags &= ~CVAR_USERINFO;
		CL_AddReliableCommand( va("userinfo \"%s\"", Cvar_InfoString( CVAR_USERINFO ) ) );
	}

}


/*
==================
CL_Frame

==================
*/

extern cvar_t	*cl_newClock;
static unsigned int frameCount;
float avgFrametime=0.0;
void CL_Frame ( int msec,float fractionMsec ) {
#ifdef _XBOX
	static int s_xboxEarlyFrameTraceBudget = 0;
#if SP_XBOX_VERBOSE_RUNTIME_LOGS
	static int s_xboxFrameHeartbeat = 0;
	static int s_xboxLastClPhaseTime = 0;
	static qboolean s_xboxTraceClPhase = qfalse;
	static qboolean s_xboxTraceClTight = qfalse;
	s_xboxTraceClPhase = qfalse;
	s_xboxTraceClTight = qfalse;
	const qboolean xboxTraceEarlyFrame = (cls.state >= CA_LOADING && s_xboxEarlyFrameTraceBudget < 16);
#else
	const qboolean s_xboxTraceClPhase = qfalse;
	const qboolean s_xboxTraceClTight = qfalse;
	const qboolean xboxTraceEarlyFrame = (cls.state >= CA_LOADING && s_xboxEarlyFrameTraceBudget < 16);
#endif
	SPXB_HOT_INC(g_SPXBClFrameCount);
	SPXB_HOT_INC(g_SPXBCLFrameEnterCalls);
	SPXB_HOT_SET(g_SPXBClsState, (unsigned int)cls.state);
	SPXB_HOT_SET(g_SPXBClServerTime, (unsigned int)cl.serverTime);
	SPXB_HOT_SET(g_SPXBClsFrameCount, (unsigned int)cls.framecount);
	SPXB_HOT_SET(g_SPXBPhaseLast, 0x434C4631); /* 'CLF1' */
	if (xboxTraceEarlyFrame)
	{
		XBLF("JA: CL_Frame early before checkAutoSave budget=%d state=%d ui=%d cgame=%d sv=%d realtime=%d serverTime=%d",
			s_xboxEarlyFrameTraceBudget,
			(int)cls.state,
			(int)cls.uiStarted,
			(int)cls.cgameStarted,
			(int)(com_sv_running ? com_sv_running->integer : -1),
			cls.realtime,
			cl.serverTime);
	}
#endif

	checkAutoSave();	//saves the game immediately after starting a level

#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBPhaseLast, 0x434C4641); /* 'CLFA' */
	if (xboxTraceEarlyFrame)
	{
		XBLF("JA: CL_Frame early after checkAutoSave state=%d ui=%d cgame=%d sv=%d",
			(int)cls.state,
			(int)cls.uiStarted,
			(int)cls.cgameStarted,
			(int)(com_sv_running ? com_sv_running->integer : -1));
	}
#endif

	if ( !com_cl_running->integer ) {
#ifdef _XBOX
		SPXB_HOT_SET(g_SPXBPhaseLast, 0x434C4630); /* 'CLF0' */
#endif
		return;
	}

#if defined (_XBOX)// && !defined(_DEBUG)
	// Optional test harness: direct-map startup is queued from main after one
	// warm-up frame, so the normal renderer/client hunk users exist first.
	extern bool Sys_QuickStart( void );
	extern bool g_xboxDirectMapBootQueued;
	static bool firstRun = true;
	static bool directMapWarmupLogged = false;
	if(firstRun && !directMapWarmupLogged)
	{
		XBLog_Write("JA: CL_Frame firstRun: direct-map queue deferred until after warm-up frame");
		directMapWarmupLogged = true;
	}
	
#endif

#ifdef _XBOX
	if (g_xboxDirectMapBootQueued && (!com_sv_running || !com_sv_running->integer))
	{
		SPXB_HOT_SET(g_SPXBMapPhase, 1610);
		SPXB_HOT_SET(g_SPXBPhaseLast, 0x434C4644); /* 'CLFD' */
		SPXB_HOT_INC(g_SPXBCLFrameDirectReturns);
		return;
	}
#endif

	// load the ref / cgame if needed
#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBPhaseLast, 0x434C4648); /* 'CLFH' */
	if (xboxTraceEarlyFrame)
	{
		XBLF("JA: CL_Frame early before CL_StartHunkUsers state=%d ui=%d cgame=%d",
			(int)cls.state,
			(int)cls.uiStarted,
			(int)cls.cgameStarted);
	}
	SPXB_HOT_SET(g_SPXBClHunkCaller, 2);
	SPXB_HOT_INC(g_SPXBClHunkCallCount);
#endif
	CL_StartHunkUsers();
#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBPhaseLast, 0x434C4668); /* 'CLFh' */
	if (xboxTraceEarlyFrame)
	{
		XBLF("JA: CL_Frame early after CL_StartHunkUsers state=%d ui=%d cgame=%d sv=%d",
			(int)cls.state,
			(int)cls.uiStarted,
			(int)cls.cgameStarted,
			(int)(com_sv_running ? com_sv_running->integer : -1));
		++s_xboxEarlyFrameTraceBudget;
	}
#endif
#ifdef _XBOX
	if (g_xboxDirectMapBootQueued && (!com_sv_running || !com_sv_running->integer))
	{
		SPXB_HOT_SET(g_SPXBMapPhase, 1500);
	}
#endif

#if defined (_XBOX)	//xbox doesn't load ui in StartHunkUsers, so check it here
	static int s_xboxClFrameHunkLogBudget = 0;
	const qboolean xboxTraceClFrameHunk = (SP_XBOX_VERBOSE_RUNTIME_LOGS && s_xboxClFrameHunkLogBudget > 0);
	if (xboxTraceClFrameHunk)
	{
		XBLF("JA: CL_Frame: CL_StartHunkUsers returned state=%d ui=%d cgame=%d sv=%d",
			(int)cls.state,
			(int)cls.uiStarted,
			(int)cls.cgameStarted,
			(int)com_sv_running->integer);
	}
	// load ui if needed
	if ( !cls.uiStarted && cls.state != CA_CINEMATIC ) {
		XBLF("JA: CL_Frame: starting Xbox UI init path state=%d inGameLoad=%d",
			(int)cls.state,
			(int)(cls.state > CA_DISCONNECTED && cls.state <= CA_ACTIVE));
		cls.uiStarted = qtrue;
		XBLog_Write("JA: CL_Frame: SCR_StopCinematic...");
		SCR_StopCinematic();
		XBLog_Write("JA: CL_Frame: SCR_StopCinematic done; CL_InitUI...");
		CL_InitUI();
		XBLog_Write("JA: CL_Frame: CL_InitUI done");
	} else {
		if (xboxTraceClFrameHunk)
		{
			XBLF("JA: CL_Frame: UI init skipped state=%d ui=%d keyCatchers=0x%x",
				(int)cls.state, (int)cls.uiStarted, (unsigned int)cls.keyCatchers);
		}
	}
	if (xboxTraceClFrameHunk)
	{
		--s_xboxClFrameHunkLogBudget;
	}
#endif

	if ( cls.state == CA_DISCONNECTED && !( cls.keyCatchers & KEYCATCH_UI )
		&& !com_sv_running->integer ) {		
		// if disconnected, bring up the menu
#ifdef _XBOX
		if (firstRun && !Sys_QuickStart())
		{
			// Fresh boot
			UI_SetActiveMenu("splashMenu", NULL);
		}
		else if (firstRun)
		{
			// Came from MP:
			UI_SetActiveMenu("mainMenu", NULL);
			extern void XB_Startup( XBStartupState startupState );
			XB_Startup( STARTUP_LOAD_SETTINGS );
		}
		else
		{
#ifdef XBOX_DEMO
			// Quitting the demo returns to the IIS, and restores settings:
			Settings.RestoreDefaults();
			Settings.SetAll();
			UI_SetActiveMenu("splashMenu", NULL);
#else
			UI_SetActiveMenu("mainMenu", NULL);
#endif
		}
#else
		if (!CL_CheckPendingCinematic())	// this avoid having the menu flash for one frame before pending cinematics
		{
			UI_SetActiveMenu("mainMenu", NULL);
		}
#endif
		S_StartBackgroundTrack("music/mp/MP_action4.mp3","",0);
	}

#ifdef _XBOX
	firstRun = false;
#if SP_XBOX_VERBOSE_RUNTIME_LOGS
	if (qfalse && cls.state == CA_ACTIVE && cls.realtime - s_xboxLastClPhaseTime >= 5000)
	{
		s_xboxLastClPhaseTime = cls.realtime;
		s_xboxTraceClPhase = qtrue;
		XBLF("JA: CL_PHASE frame=%u enter realtime=%d serverTime=%d msec=%d",
			frameCount, cls.realtime, cl.serverTime, msec);
	}
	if (qfalse && cls.state == CA_ACTIVE && cls.realtime >= 35000 && cls.realtime <= 70000)
	{
		s_xboxTraceClTight = qtrue;
	}
	s_xboxFrameHeartbeat++;
#endif
#if SP_XBOX_SMOKE_AUTOMATION
	CL_XboxAutoSmokeTick();
	CL_XboxSmokeSoakCommandTick();
#endif
#endif


	// if recording an avi, lock to a fixed fps
	if ( cl_avidemo->integer ) {
		// save the current screen
		if ( cls.state == CA_ACTIVE ) {
			if (cl_avidemo->integer > 0) {
				Cbuf_ExecuteText( EXEC_NOW, "screenshot silent\n" );
			} else {
				Cbuf_ExecuteText( EXEC_NOW, "screenshot_tga silent\n" );
			}
		}
		// fixed time for next frame
		if (cl_avidemo->integer > 0) {
			msec = 1000 / cl_avidemo->integer;
		} else {
			msec = 1000 / -cl_avidemo->integer;
		}
	}

	// save the msec before checking pause
	cls.realFrametime = msec;

	// decide the simulation time
	cls.frametime = msec;
	//if(cl_framerate->integer)
	//{
	//	avgFrametime+=msec;
	//	char mess[256];
	//	if(!(frameCount&0x1f))
	//	{
	//		sprintf(mess,"Frame rate=%f\n\n",1000.0f*(1.0/(avgFrametime/32.0f)));
	////		OutputDebugString(mess);
	//		Com_Printf(mess);
	//		avgFrametime=0.0f;
	//	}
	//	frameCount++;
	//}
	// Always calculate framerate, bias the LOD if low
	avgFrametime+=msec;
	extern bool in_camera;
	float framerate = 1000.0f*(1.0/(avgFrametime/32.0f));
	static int lodFrameCount = 0;
	static cvar_t *lodBiasCvar = NULL;
	if ( !lodBiasCvar )
	{
		lodBiasCvar = Cvar_Get( "r_lodbias", "0", CVAR_ARCHIVE );
	}
	int bias = lodBiasCvar ? lodBiasCvar->integer : 0;
	static qboolean wasInCamera = qfalse;
	if(!(frameCount&0x1f))
	{
        if(cl_framerate->integer)
		{
			char mess[256];
			sprintf(mess,"Frame rate=%f LOD=%d\n\n",framerate,bias);
			Com_Printf(mess);
		}
		avgFrametime=0.0f;

		// If we drop below 20FPS, pull down the LOD bias
		if(framerate < 20.0f && bias == 0)
		{
			bias++;
			Cvar_SetValue("r_lodbias", bias);
			lodFrameCount = -1;
		}

		lodFrameCount++;
		if(lodFrameCount==5 && bias > 0)
		{
			bias--;
			Cvar_SetValue("r_lodbias", bias);
			lodFrameCount = 0;
		}
	}
	frameCount++;

	if(in_camera)
	{
		// No LOD stuff during cutscenes
		if ( !wasInCamera || bias != 0 )
		{
			Cvar_SetValue("r_lodbias", 0);
		}
	}
	wasInCamera = in_camera ? qtrue : qfalse;

	cls.frametimeFraction=fractionMsec;
	cls.realtime += msec;
	cls.realtimeFraction+=fractionMsec;
	if (cls.realtimeFraction>=1.0f)
	{
		if (cl_newClock&&cl_newClock->integer)
		{
			cls.realtime++;
		}
		cls.realtimeFraction-=1.0f;
	}
#ifndef _XBOX
	if ( cl_timegraph->integer ) {
		SCR_DebugGraph ( cls.realFrametime * 0.25, 0 );
	}
#endif

#ifdef _XBOX
	//Check on the hot swappable button states.
	CL_UpdateHotSwap();
#endif

	// see if we need to update any userinfo
	CL_CheckUserinfo();

	// if we haven't gotten a packet in a long time,
	// drop the connection
	CL_CheckTimeout();

	// send intentions now
#ifdef _XBOX
	if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE before CL_SendCmd");
#endif
	CL_SendCmd();
#ifdef _XBOX
	if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE after CL_SendCmd");
#endif

	// resend a connection request if necessary
#ifdef _XBOX
	if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE before CL_CheckForResend");
#endif
	CL_CheckForResend();
#ifdef _XBOX
	if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE after CL_CheckForResend");
#endif

	// decide on the serverTime to render
#ifdef _XBOX
	if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE before CL_SetCGameTime");
#endif
	CL_SetCGameTime();
#ifdef _XBOX
	if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE after CL_SetCGameTime");
#endif

	if (cl_pano->integer && cls.state == CA_ACTIVE) {	//grab some panoramic shots
		int i = 1;
		int pref = cl_pano->integer;
		int oldnoprint = cl_noprint->integer;
		Con_Close();
		cl_noprint->integer = 1;	//hide the screen shot msgs
		for (; i <= cl_panoNumShots->integer; i++) {
			Cvar_SetValue( "pano", i );
			SCR_UpdateScreen();// update the screen
			Cbuf_ExecuteText( EXEC_NOW, va("screenshot %dpano%02d\n", pref, i) );	//grab this screen
		}
		Cvar_SetValue( "pano", 0 );	//done
		cl_noprint->integer = oldnoprint;
	}

	if (cl_skippingcin->integer && !cl_endcredits->integer && !com_developer->integer ) {
		if (cl_skippingcin->modified){
			S_StopSounds();		//kill em all but music	
			cl_skippingcin->modified=qfalse;
			Com_Printf (va(S_COLOR_YELLOW"%s"), SE_GetString("CON_TEXT_SKIPPING"));
			SCR_UpdateScreen();
		}
	} else {
		// update the screen
#ifdef _XBOX
		if (cls.state < CA_LOADING && !(cls.uiStarted && (cls.keyCatchers & KEYCATCH_UI)))
		{
			static int s_xboxSkipPreLoadScreens = 0;
			if (SP_XBOX_VERBOSE_RUNTIME_LOGS && s_xboxSkipPreLoadScreens < 8)
			{
				XBLF("JA: CL_Frame: skipping pre-load SCR_UpdateScreen state=%d", (int)cls.state);
				++s_xboxSkipPreLoadScreens;
			}
		}
		else
#endif
#ifdef _XBOX
		{
			static int s_xboxActiveScreenBoundaryCount = 0;
			static int s_xboxLoadScreenBoundaryCount = 0;
			const qboolean xboxTraceActiveScreen = qfalse;
			const qboolean xboxTraceLoadScreen = (SP_XBOX_VERBOSE_RUNTIME_LOGS && cls.state >= CA_LOADING && cls.state < CA_ACTIVE && s_xboxLoadScreenBoundaryCount < 32);
			if (s_xboxTraceClTight)
			{
				XBLF("JA: CL_TIGHT frame=%u before SCR_UpdateScreen realtime=%d serverTime=%d",
					frameCount, cls.realtime, cl.serverTime);
			}
			else if (s_xboxTraceClPhase)
			{
				XBLF("JA: CL_PHASE before SCR_UpdateScreen realtime=%d serverTime=%d",
					cls.realtime, cl.serverTime);
			}
			else if (xboxTraceActiveScreen)
			{
				XBLF("JA: CL_Frame: before SCR_UpdateScreen active count=%d realtime=%d serverTime=%d",
					s_xboxActiveScreenBoundaryCount, cls.realtime, cl.serverTime);
			}
			else if (xboxTraceLoadScreen)
			{
				XBLF("JA: CL_Frame: before SCR_UpdateScreen loading count=%d state=%d realtime=%d serverTime=%d",
					s_xboxLoadScreenBoundaryCount, (int)cls.state, cls.realtime, cl.serverTime);
			}
			SPXB_HOT_INC(g_SPXBCLFrameBeforeScreen);
			SCR_UpdateScreen();
			SPXB_HOT_INC(g_SPXBCLFrameAfterScreen);
			if (s_xboxTraceClTight)
			{
				XBLF("JA: CL_TIGHT frame=%u after SCR_UpdateScreen realtime=%d serverTime=%d",
					frameCount, cls.realtime, cl.serverTime);
			}
			else if (s_xboxTraceClPhase)
			{
				XBLF("JA: CL_PHASE after SCR_UpdateScreen realtime=%d serverTime=%d",
					cls.realtime, cl.serverTime);
			}
			else if (xboxTraceActiveScreen)
			{
				XBLF("JA: CL_Frame: after SCR_UpdateScreen active count=%d realtime=%d serverTime=%d",
					s_xboxActiveScreenBoundaryCount, cls.realtime, cl.serverTime);
				s_xboxActiveScreenBoundaryCount++;
			}
			else if (xboxTraceLoadScreen)
			{
				XBLF("JA: CL_Frame: after SCR_UpdateScreen loading count=%d state=%d realtime=%d serverTime=%d",
					s_xboxLoadScreenBoundaryCount, (int)cls.state, cls.realtime, cl.serverTime);
				s_xboxLoadScreenBoundaryCount++;
			}
		}
#else
		SCR_UpdateScreen();
#endif

#if defined(_XBOX) && !defined(FINAL_BUILD) && !defined(_XBOX_VC71_MIGRATION)
		if (cls.state >= CA_LOADING && D3DPERF_QueryRepeatFrame())
			SCR_UpdateScreen();
#endif
	}
	// update audio
#ifdef _XBOX
	{
		static int s_xboxActiveFrameTailCount = 0;
		const qboolean xboxTraceActiveTail = qfalse;
		static qboolean s_xboxLoggedAudioSkip = qfalse;
		static int s_xboxBootTailLogBudget = 0;
		const qboolean xboxTraceBootTail = (SP_XBOX_VERBOSE_RUNTIME_LOGS && s_xboxBootTailLogBudget > 0);
		if (!s_xboxLoggedAudioSkip)
		{
			XBLog_Write("JA: CL_Frame: running silent S_Update on Xbox smoke build");
			s_xboxLoggedAudioSkip = qtrue;
		}
		if (xboxTraceBootTail)
		{
			XBLF("JA: CL_BOOT_TAIL before S_Update frame=%u state=%d ui=%d cgame=%d sv=%d keyCatchers=0x%x",
				frameCount, (int)cls.state, (int)cls.uiStarted, (int)cls.cgameStarted,
				(int)com_sv_running->integer, (unsigned int)cls.keyCatchers);
		}
		S_Update();
		if (xboxTraceBootTail)
		{
			XBLF("JA: CL_BOOT_TAIL after S_Update frame=%u state=%d", frameCount, (int)cls.state);
		}

#ifdef _IMMERSION
		if (xboxTraceActiveTail) XBLog_Write("JA: CL_Frame: before FF_Update");
		FF_Update();
		if (xboxTraceActiveTail) XBLog_Write("JA: CL_Frame: after FF_Update");
#endif // _IMMERSION
		// advance local effects for next frame
		if (s_xboxTraceClTight) XBLF("JA: CL_TIGHT frame=%u before SCR_RunCinematic", frameCount);
		else if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE before SCR_RunCinematic");
		else if (xboxTraceActiveTail) XBLog_Write("JA: CL_Frame: before SCR_RunCinematic");
		else if (xboxTraceBootTail) XBLog_Write("JA: CL_BOOT_TAIL before SCR_RunCinematic");
		SCR_RunCinematic();
		if (s_xboxTraceClTight) XBLF("JA: CL_TIGHT frame=%u after SCR_RunCinematic", frameCount);
		else if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE after SCR_RunCinematic");
		else if (xboxTraceActiveTail) XBLog_Write("JA: CL_Frame: after SCR_RunCinematic");
		else if (xboxTraceBootTail) XBLog_Write("JA: CL_BOOT_TAIL after SCR_RunCinematic");

		if (s_xboxTraceClTight) XBLF("JA: CL_TIGHT frame=%u before Con_RunConsole", frameCount);
		else if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE before Con_RunConsole");
		else if (xboxTraceActiveTail) XBLog_Write("JA: CL_Frame: before Con_RunConsole");
		else if (xboxTraceBootTail) XBLog_Write("JA: CL_BOOT_TAIL before Con_RunConsole");
		Con_RunConsole();
		if (s_xboxTraceClTight)
		{
			XBLF("JA: CL_TIGHT frame=%u after Con_RunConsole", frameCount);
		}
		else if (s_xboxTraceClPhase)
		{
			XBLog_Write("JA: CL_PHASE after Con_RunConsole");
		}
		else if (xboxTraceActiveTail)
		{
			XBLog_Write("JA: CL_Frame: after Con_RunConsole");
			s_xboxActiveFrameTailCount++;
		}
		else if (xboxTraceBootTail)
		{
			XBLog_Write("JA: CL_BOOT_TAIL after Con_RunConsole");
		}
		if (SP_XBOX_VERBOSE_RUNTIME_LOGS && s_xboxBootTailLogBudget > 0)
		{
			--s_xboxBootTailLogBudget;
		}
	}
#else
	S_Update();

#ifdef _IMMERSION
	FF_Update();
#endif // _IMMERSION
	// advance local effects for next frame
	SCR_RunCinematic();

	Con_RunConsole();
#endif

	cls.framecount++;
#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBClsState, (unsigned int)cls.state);
	SPXB_HOT_SET(g_SPXBClServerTime, (unsigned int)cl.serverTime);
	SPXB_HOT_SET(g_SPXBClsFrameCount, (unsigned int)cls.framecount);
	SPXB_HOT_SET(g_SPXBPhaseLast, 0x434C4632); /* 'CLF2' */
	SPXB_HOT_INC(g_SPXBCLFrameCompleted);
	{
		static int s_xboxCompletedFrameLogBudget = 0;
		if (SP_XBOX_VERBOSE_RUNTIME_LOGS && s_xboxCompletedFrameLogBudget > 0)
		{
			XBLF("JA: CL_Frame completed framecount=%d state=%d realtime=%d serverTime=%d newSnapshots=%d frameValid=%d",
				cls.framecount, (int)cls.state, cls.realtime, cl.serverTime,
				(int)cl.newSnapshots, (int)cl.frame.valid);
			--s_xboxCompletedFrameLogBudget;
		}
	}
	if (cls.state == CA_ACTIVE)
	{
		static int s_xboxLastCompletedHeartbeatTime = 0;
		static int s_xboxLastCompletedHeartbeatFrame = 0;
#if SP_XBOX_VERBOSE_RUNTIME_LOGS
		static unsigned int s_xboxLastDrawSurfLists = 0;
		static unsigned int s_xboxLastRenderSurfaces = 0;
		static unsigned int s_xboxLastEndSurfaces = 0;
		static unsigned int s_xboxLastPrimitiveCalls = 0;
		static unsigned int s_xboxLastPrimitiveVerts = 0;
		static unsigned int s_xboxLastStateFlushes = 0;
		static unsigned int s_xboxLastSplitShader = 0;
		static unsigned int s_xboxLastSplitFog = 0;
		static unsigned int s_xboxLastSplitDlight = 0;
		static unsigned int s_xboxLastSplitEntity = 0;
		static unsigned int s_xboxLastSplitFinal = 0;
		static unsigned int s_xboxLastSplitFlush = 0;
#endif
		const int elapsed = cls.realtime - s_xboxLastCompletedHeartbeatTime;

		if (elapsed >= 1000 || s_xboxLastCompletedHeartbeatTime == 0)
		{
			const int frameDelta = cls.framecount - s_xboxLastCompletedHeartbeatFrame;
#if SP_XBOX_VERBOSE_RUNTIME_LOGS
			const unsigned int drawLists = g_SPXBRenderDrawSurfLists - s_xboxLastDrawSurfLists;
			const unsigned int surfaces = g_SPXBRenderSurfaces - s_xboxLastRenderSurfaces;
			const unsigned int endSurfaces = g_SPXBRenderEndSurfaces - s_xboxLastEndSurfaces;
			const unsigned int primitiveCalls = g_SPXBFakeGLPrimitiveCalls - s_xboxLastPrimitiveCalls;
			const unsigned int primitiveVerts = g_SPXBFakeGLPrimitiveVerts - s_xboxLastPrimitiveVerts;
			const unsigned int stateFlushes = g_SPXBFakeGLStateFlushes - s_xboxLastStateFlushes;
			const unsigned int splitShader = g_SPXBRenderSplitShader - s_xboxLastSplitShader;
			const unsigned int splitFog = g_SPXBRenderSplitFog - s_xboxLastSplitFog;
			const unsigned int splitDlight = g_SPXBRenderSplitDlight - s_xboxLastSplitDlight;
			const unsigned int splitEntity = g_SPXBRenderSplitEntity - s_xboxLastSplitEntity;
			const unsigned int splitFinal = g_SPXBRenderSplitFinal - s_xboxLastSplitFinal;
			const unsigned int splitFlush = g_SPXBRenderSplitFlush - s_xboxLastSplitFlush;
#endif
			int fps10 = 0;
			char msg[512];

			if (elapsed > 0)
			{
				fps10 = (frameDelta * 10000) / elapsed;
			}

			g_SPXBHeartbeatCount++;
			g_SPXBHeartbeatFrame = cls.framecount;
			g_SPXBHeartbeatRealtime = cls.realtime;
			g_SPXBHeartbeatServerTime = cl.serverTime;
			g_SPXBHeartbeatFps10 = fps10;

#if SP_XBOX_VERBOSE_RUNTIME_LOGS
			_snprintf(msg, sizeof(msg),
				"JA: FRAME_HEARTBEAT frame=%d rt=%d st=%d fd=%d el=%d fps=%d.%d r=%d cg=%d dl=%u surf=%u end=%u prim=%u verts=%u state=%u be=%u split=%u/%u/%u/%u final=%u flush=%u\n",
				cls.framecount,
				cls.realtime,
				cl.serverTime,
				frameDelta,
				elapsed,
				fps10 / 10,
				fps10 % 10,
				(int)cls.rendererStarted,
				(int)cls.cgameStarted,
				drawLists,
				surfaces,
				endSurfaces,
				primitiveCalls,
				primitiveVerts,
				stateFlushes,
				(unsigned int)g_SPXBRenderBackendMsec,
				splitShader,
				splitFog,
				splitDlight,
				splitEntity,
				splitFinal,
				splitFlush);
#else
			_snprintf(msg, sizeof(msg),
				"JA: FRAME_HEARTBEAT frame=%d rt=%d st=%d fd=%d el=%d fps=%d.%d\n",
				cls.framecount,
				cls.realtime,
				cl.serverTime,
				frameDelta,
				elapsed,
				fps10 / 10,
				fps10 % 10);
#endif
			msg[sizeof(msg) - 1] = '\0';
			XBLog_Write(msg);

			s_xboxLastCompletedHeartbeatTime = cls.realtime;
			s_xboxLastCompletedHeartbeatFrame = cls.framecount;
#if SP_XBOX_VERBOSE_RUNTIME_LOGS
			s_xboxLastDrawSurfLists = g_SPXBRenderDrawSurfLists;
			s_xboxLastRenderSurfaces = g_SPXBRenderSurfaces;
			s_xboxLastEndSurfaces = g_SPXBRenderEndSurfaces;
			s_xboxLastPrimitiveCalls = g_SPXBFakeGLPrimitiveCalls;
			s_xboxLastPrimitiveVerts = g_SPXBFakeGLPrimitiveVerts;
			s_xboxLastStateFlushes = g_SPXBFakeGLStateFlushes;
			s_xboxLastSplitShader = g_SPXBRenderSplitShader;
			s_xboxLastSplitFog = g_SPXBRenderSplitFog;
			s_xboxLastSplitDlight = g_SPXBRenderSplitDlight;
			s_xboxLastSplitEntity = g_SPXBRenderSplitEntity;
			s_xboxLastSplitFinal = g_SPXBRenderSplitFinal;
			s_xboxLastSplitFlush = g_SPXBRenderSplitFlush;
#endif
		}
	}
#endif
}


//============================================================================

/*
================
VID_Printf

DLL glue
================
*/
#define	MAXPRINTMSG	4096
void VID_Printf (int print_level, const char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];
	
	va_start (argptr,fmt);
	vsprintf (msg,fmt,argptr);
	va_end (argptr);

	if ( print_level == PRINT_ALL ) {
		Com_Printf ("%s", msg);
	} else if ( print_level == PRINT_WARNING ) {
		Com_Printf (S_COLOR_YELLOW "%s", msg);		// yellow
	} else if ( print_level == PRINT_DEVELOPER ) {
		Com_DPrintf (S_COLOR_RED"%s", msg);
	}
}



/*
============
CL_ShutdownRef
============
*/
void CL_ShutdownRef( void ) {
	if ( !re.Shutdown ) {
		return;
	}
	re.Shutdown( qtrue );
	memset( &re, 0, sizeof( re ) );
}

/*
============================
CL_StartSound

Convenience function for the sound system to be started
REALLY early on Xbox, helps with memory fragmentation.
============================
*/
void CL_StartSound( void ) {
	if ( !cls.soundStarted ) {
		cls.soundStarted = qtrue;
		S_Init();
	}

	if ( !cls.soundRegistered ) {
		cls.soundRegistered = qtrue;
		S_BeginRegistration();
	}
}

/*
============================
CL_StartHunkUsers

After the server has cleared the hunk, these will need to be restarted
This is the only place that any of these functions are called from
============================
*/
void CL_StartHunkUsers( void ) {
#ifdef _XBOX
	qboolean xboxTraceStartHunk = (!cls.rendererStarted || !cls.soundStarted || !cls.soundRegistered ||
		(!cls.cgameStarted && cls.state > CA_CONNECTED && (cls.state != CA_CINEMATIC && !CL_IsRunningInGameCinematic())));
	if (xboxTraceStartHunk)
	{
		g_SPXBClHunkState = 100;
		g_SPXBMapPhase = 1400;
		g_SPXBPhaseLast = 0x43534830; /* 'CSH0' */
		XBLog_Write("JA: CL_StartHunkUsers entered");
		XBLog_SoakTrace("CL_StartHunkUsers", "enter", cls.servername,
			(int)cls.state, (int)cls.rendererStarted, (int)cls.soundStarted, (int)cls.cgameStarted);
	}
#endif
	if ( !com_cl_running->integer ) {
#ifdef _XBOX
		g_SPXBClHunkState = 101;
		if (xboxTraceStartHunk)
		{
			XBLog_Write("JA: CL_StartHunkUsers: cl_running=0, early return");
			XBLog_SoakTrace("CL_StartHunkUsers", "cl-not-running", cls.servername,
				(int)cls.state, (int)cls.rendererStarted, (int)cls.soundStarted, (int)cls.cgameStarted);
		}
#endif
		return;
	}

#ifdef _XBOX
	if ( cls.rendererStarted &&
		cls.soundStarted &&
		cls.soundRegistered &&
		( cls.cgameStarted || cls.state <= CA_CONNECTED || cls.state == CA_CINEMATIC || CL_IsRunningInGameCinematic() ) )
	{
		static qboolean s_xboxLoggedReadyFastPath = qfalse;
		if (!s_xboxLoggedReadyFastPath)
		{
			s_xboxLoggedReadyFastPath = qtrue;
			XBLog_Write("JA: CL_StartHunkUsers: ready fast path active");
		}
		XBLog_SoakTrace("CL_StartHunkUsers", "ready-fast-path", cls.servername,
			(int)cls.state, (int)cls.rendererStarted, (int)cls.soundRegistered, (int)cls.cgameStarted);
		return;
	}
#endif

	if ( !cls.rendererStarted ) {
#ifdef _XBOX
		g_SPXBClHunkState = 110;
		g_SPXBMapPhase = 1410;
		g_SPXBPhaseLast = 0x43534852; /* 'CSHR' */
		XBLog_Write("JA: CL_StartHunkUsers: re.BeginRegistration (calls R_Init)...");
		XBLog_SoakTrace("CL_StartHunkUsers", "before-re.BeginRegistration", cls.servername,
			(int)cls.state, (int)cls.rendererStarted, (int)cls.soundStarted, (int)cls.cgameStarted);
#endif
#ifdef _XBOX
		//if ((!com_sv_running->integer || com_errorEntered) && !vidRestartReloadMap)
		//{
		//	// free up some memory
		//	extern void SV_ClearLastLevel(void);
		//	SV_ClearLastLevel();
		//}
#endif

		cls.rendererStarted = qtrue;
		re.BeginRegistration( &cls.glconfig );
#ifdef _XBOX
		g_SPXBClHunkState = 111;
		g_SPXBMapPhase = 1411;
		g_SPXBPhaseLast = 0x43534831; /* 'CSH1' */
		XBLog_Write("JA: CL_StartHunkUsers: re.BeginRegistration done");
		XBLog_SoakTrace("CL_StartHunkUsers", "after-re.BeginRegistration", cls.servername,
			(int)cls.state, (int)cls.glconfig.vidWidth, (int)cls.glconfig.vidHeight, (int)cls.cgameStarted);
		XBLog_Write("JA: CL_StartHunkUsers: RegisterShaderNoMip charsgrid_med...");
#endif

		// load character sets
//		cls.charSetShader = re.RegisterShaderNoMip( "gfx/2d/bigchars" );
#ifdef _XBOX
		XBLog_SoakTrace("CL_StartHunkUsers", "before-RegisterShaderNoMip", "gfx/2d/charsgrid_med",
			(int)cls.state, (int)cls.rendererStarted, (int)cls.charSetShader, (int)cls.whiteShader);
#endif
		cls.charSetShader = re.RegisterShaderNoMip( "gfx/2d/charsgrid_med" );
#ifdef _XBOX
		XBLF("JA: CL_StartHunkUsers: charSetShader=%d, RegisterShader white...", cls.charSetShader);
		XBLog_SoakTrace("CL_StartHunkUsers", "after-RegisterShaderNoMip", "gfx/2d/charsgrid_med",
			(int)cls.state, (int)cls.rendererStarted, (int)cls.charSetShader, (int)cls.whiteShader);
#endif
#ifdef _XBOX
		XBLog_SoakTrace("CL_StartHunkUsers", "before-RegisterShader", "white",
			(int)cls.state, (int)cls.rendererStarted, (int)cls.charSetShader, (int)cls.whiteShader);
#endif
		cls.whiteShader = re.RegisterShader( "white" );
#ifdef _XBOX
		XBLF("JA: CL_StartHunkUsers: whiteShader=%d", cls.whiteShader);
		XBLog_SoakTrace("CL_StartHunkUsers", "after-RegisterShader", "white",
			(int)cls.state, (int)cls.rendererStarted, (int)cls.charSetShader, (int)cls.whiteShader);
#endif
//		cls.consoleShader = re.RegisterShader( "console" );
		g_console_field_width = cls.glconfig.vidWidth / SMALLCHAR_WIDTH - 2;
		kg.g_consoleField.widthInChars = g_console_field_width;
#ifndef _IMMERSION
		//-------
		//	The latest Immersion Force Feedback system initializes here, not through
		//	win32 input system. Therefore, the window handle is valid :)
		//-------

		// now that the renderer has started up we know that the global hWnd is now valid,
		//	so we can now go ahead and (re)setup the input stuff that needs hWnds for DI...
		//  (especially Force feedback)...
		//
		static qboolean bOnceOnly = qfalse;	// only do once, not every renderer re-start
		if (!bOnceOnly)
		{
			bOnceOnly = qtrue;
#ifdef _XBOX
			g_SPXBClHunkState = 112;
			g_SPXBMapPhase = 1412;
			g_SPXBPhaseLast = 0x43534849; /* 'CSHI' */
			XBLog_Write("JA: CL_StartHunkUsers: Sys_In_Restart_f...");
			XBLog_SoakTrace("CL_StartHunkUsers", "before-Sys_In_Restart_f", cls.servername,
				(int)cls.state, (int)cls.mainGamepad, (int)cls.rendererStarted, (int)cls.cgameStarted);
#endif
			extern void Sys_In_Restart_f( void );
			Sys_In_Restart_f();
#ifdef _XBOX
			g_SPXBClHunkState = 113;
			XBLog_Write("JA: CL_StartHunkUsers: Sys_In_Restart_f done");
			XBLog_SoakTrace("CL_StartHunkUsers", "after-Sys_In_Restart_f", cls.servername,
				(int)cls.state, (int)cls.mainGamepad, (int)cls.rendererStarted, (int)cls.cgameStarted);
#endif
		}

#ifdef _XBOX
		if (vidRestartReloadMap)
		{
			int checksum;
			XBLog_SoakTrace("CL_StartHunkUsers", "vidrestart-before-CM_LoadMap", cl_mapname->string,
				(int)cls.state, (int)vidRestartReloadMap, (int)cls.rendererStarted, (int)cls.cgameStarted);
			CM_LoadMap(va("maps/%s.bsp", cl_mapname->string), qfalse, &checksum);
			XBLog_SoakTrace("CL_StartHunkUsers", "vidrestart-after-CM_LoadMap", cl_mapname->string,
				(int)cls.state, checksum, (int)cls.rendererStarted, (int)cls.cgameStarted);
			XBLog_SoakTrace("CL_StartHunkUsers", "vidrestart-before-RE_LoadWorldMap", cl_mapname->string,
				(int)cls.state, checksum, (int)cls.rendererStarted, (int)cls.cgameStarted);
			RE_LoadWorldMap(va("maps/%s.bsp", cl_mapname->string));
			XBLog_SoakTrace("CL_StartHunkUsers", "vidrestart-after-RE_LoadWorldMap", cl_mapname->string,
				(int)cls.state, checksum, (int)cls.rendererStarted, (int)cls.cgameStarted);
			vidRestartReloadMap = qfalse;
		}
#endif // _XBOX

#endif // _IMMERSION
	}

	if ( !cls.soundStarted ) {
		cls.soundStarted = qtrue;
#ifdef _XBOX
		g_SPXBClHunkState = 120;
		g_SPXBMapPhase = 1420;
		g_SPXBPhaseLast = 0x43534832; /* 'CSH2' */
		XBLog_Write("JA: CL_StartHunkUsers: S_Init...");
		XBLog_SoakTrace("CL_StartHunkUsers", "before-S_Init", cls.servername,
			(int)cls.state, (int)cls.soundStarted, (int)cls.soundRegistered, (int)cls.cgameStarted);
#endif
		S_Init();
#ifdef _XBOX
		g_SPXBClHunkState = 121;
		XBLog_Write("JA: CL_StartHunkUsers: S_Init done");
		XBLog_SoakTrace("CL_StartHunkUsers", "after-S_Init", cls.servername,
			(int)cls.state, (int)cls.soundStarted, (int)cls.soundRegistered, (int)cls.cgameStarted);
#endif
	}

	if ( !cls.soundRegistered ) {
		cls.soundRegistered = qtrue;
#ifdef _XBOX
		g_SPXBClHunkState = 130;
		g_SPXBMapPhase = 1430;
		g_SPXBPhaseLast = 0x43534833; /* 'CSH3' */
		XBLog_Write("JA: CL_StartHunkUsers: S_BeginRegistration...");
		XBLog_SoakTrace("CL_StartHunkUsers", "before-S_BeginRegistration", cls.servername,
			(int)cls.state, (int)cls.soundStarted, (int)cls.soundRegistered, (int)cls.cgameStarted);
#endif
		S_BeginRegistration();
#ifdef _XBOX
		g_SPXBClHunkState = 131;
		XBLog_Write("JA: CL_StartHunkUsers: S_BeginRegistration done");
		XBLog_SoakTrace("CL_StartHunkUsers", "after-S_BeginRegistration", cls.servername,
			(int)cls.state, (int)cls.soundStarted, (int)cls.soundRegistered, (int)cls.cgameStarted);
#endif
	}

#ifdef _IMMERSION
	if ( !cls.forceStarted ) {
		cls.forceStarted = qtrue;
		CL_InitFF();
	}
#endif // _IMMERSION

#if !defined (_XBOX)	//i guess xbox doesn't want the ui loaded all the time?
	//we require the ui to be loaded here or else it crashes trying to access the ui on command line map loads
	if ( !cls.uiStarted ) {
		cls.uiStarted = qtrue;
		CL_InitUI();
	}
#endif

//	if ( !cls.cgameStarted && cls.state > CA_CONNECTED && cls.state != CA_CINEMATIC ) {
	if ( !cls.cgameStarted && cls.state > CA_CONNECTED && (cls.state != CA_CINEMATIC && !CL_IsRunningInGameCinematic()) )
	{
		cls.cgameStarted = qtrue;
#ifdef _XBOX
		g_SPXBClHunkState = 140;
		g_SPXBMapPhase = 1440;
		g_SPXBPhaseLast = 0x43534834; /* 'CSH4' */
		XBLog_Write("JA: CL_StartHunkUsers: CL_InitCGame...");
		XBLog_SoakTrace("CL_StartHunkUsers", "before-CL_InitCGame", cls.servername,
			(int)cls.state, (int)cls.rendererStarted, (int)cls.soundRegistered, (int)cls.cgameStarted);
#endif
		CL_InitCGame();
#ifdef _XBOX
		g_SPXBClHunkState = 141;
		XBLog_Write("JA: CL_StartHunkUsers: CL_InitCGame done");
		XBLog_SoakTrace("CL_StartHunkUsers", "after-CL_InitCGame", cls.servername,
			(int)cls.state, (int)cls.rendererStarted, (int)cls.soundRegistered, (int)cls.cgameStarted);
#endif
	}
#ifdef _XBOX
	if (xboxTraceStartHunk)
	{
		g_SPXBClHunkState = 190;
		g_SPXBMapPhase = 1499;
		g_SPXBPhaseLast = 0x43534844; /* 'CSHD' */
		g_SPXBMapPhase = 1501;
		g_SPXBClHunkState = 191;
		XBLog_Write("JA: CL_StartHunkUsers: COMPLETE");
		XBLog_SoakTrace("CL_StartHunkUsers", "complete", cls.servername,
			(int)cls.state, (int)cls.rendererStarted, (int)cls.soundRegistered, (int)cls.cgameStarted);
	}
#endif
}

/*
============
CL_InitRef
============
*/
void CL_InitRef( void ) {
	refexport_t	*ret;

	Com_Printf( "----- Initializing Renderer ----\n" );

	// cinematic stuff

	ret = GetRefAPI( REF_API_VERSION );

	Com_Printf( "-------------------------------\n");

	if ( !ret ) {
		Com_Error (ERR_FATAL, "Couldn't initialize refresh" );
	}

	re = *ret;

	// unpause so the cgame definately gets a snapshot and renders a frame
	Cvar_Set( "cl_paused", "0" );
}


//===========================================================================================

/*
====================
CL_Init
====================
*/
void CL_Init( void ) {
	XBLog_Write("JA: CL_Init entered");
	Com_Printf( "----- Client Initialization -----\n" );

	XBLog_Write("JA: Con_Init...");
	Con_Init ();

	CL_ClearState ();

	cls.state = CA_DISCONNECTED;	// no longer CA_UNINITIALIZED
	XBLog_Write("JA: cls.state = CA_DISCONNECTED");
	cls.keyCatchers = KEYCATCH_CONSOLE;
	cls.realtime = 0;
	cls.realtimeFraction=0.0f;	// fraction of a msec accumulated

	CL_InitInput ();

#ifndef _XBOX	// No terrain on Xbox
	RM_InitTerrain();
#endif

	//
	// register our variables
	//
	cl_noprint = Cvar_Get( "cl_noprint", "0", 0 );

	cl_timeout = Cvar_Get ("cl_timeout", "125", 0);

	cl_timeNudge = Cvar_Get ("cl_timeNudge", "0", CVAR_TEMP );
	cl_shownet = Cvar_Get ("cl_shownet", "0", CVAR_TEMP );
	cl_showTimeDelta = Cvar_Get ("cl_showTimeDelta", "0", CVAR_TEMP );
	cl_newClock = Cvar_Get ("cl_newClock", "1", 0);
	cl_activeAction = Cvar_Get( "activeAction", "", CVAR_TEMP );
	
	cl_avidemo = Cvar_Get ("cl_avidemo", "0", 0);
	cl_pano = Cvar_Get ("pano", "0", 0);
	cl_panoNumShots= Cvar_Get ("panoNumShots", "10", CVAR_ARCHIVE);
	cl_skippingcin = Cvar_Get ("skippingCinematic", "0", CVAR_ROM);
	cl_endcredits = Cvar_Get ("cg_endcredits", "0", 0);

	cl_yawspeed = Cvar_Get ("cl_yawspeed", "140", CVAR_ARCHIVE);
	cl_pitchspeed = Cvar_Get ("cl_pitchspeed", "140", CVAR_ARCHIVE);
	cl_anglespeedkey = Cvar_Get ("cl_anglespeedkey", "1.5", CVAR_ARCHIVE);

	cl_maxpackets = Cvar_Get ("cl_maxpackets", "30", CVAR_ARCHIVE );
	cl_packetdup = Cvar_Get ("cl_packetdup", "1", CVAR_ARCHIVE );

	cl_run = Cvar_Get ("cl_run", "1", CVAR_ARCHIVE);
	cl_sensitivity = Cvar_Get ("sensitivity", "2", CVAR_ARCHIVE);

#ifdef _XBOX
	cl_sensitivityY = Cvar_Get ("sensitivityY", "2", CVAR_ARCHIVE);
#endif

//	cl_mouseAccel = Cvar_Get ("cl_mouseAccel", "0", CVAR_ARCHIVE);
	cl_freelook = Cvar_Get( "cl_freelook", "1", CVAR_ARCHIVE );

//	cl_showMouseRate = Cvar_Get ("cl_showmouserate", "0", 0);

	cl_ingameVideo = Cvar_Get ("cl_ingameVideo", "1", CVAR_ARCHIVE);
	cl_VideoQuality = Cvar_Get ("cl_VideoQuality", "0", CVAR_ARCHIVE);
	cl_VidFadeUp	= Cvar_Get ("cl_VidFadeUp", "1", CVAR_TEMP);
	cl_VidFadeDown	= Cvar_Get ("cl_VidFadeDown", "1", CVAR_TEMP);
	cl_framerate	= Cvar_Get ("cl_framerate", "0", CVAR_TEMP);

	cl_thumbStickMode = Cvar_Get ("ui_thumbStickMode", "0", CVAR_ARCHIVE);

	// init autoswitch so the ui will have it correctly even
	// if the cgame hasn't been started
	Cvar_Get ("cg_autoswitch", "1", CVAR_ARCHIVE);
//JLF
#ifdef _XBOX
	Cvar_Get ("cl_autolevel","0",CVAR_ARCHIVE);
#endif

	m_pitch = Cvar_Get ("m_pitch", "0.022", CVAR_ARCHIVE);
	m_yaw = Cvar_Get ("m_yaw", "0.022", CVAR_ARCHIVE);
	m_forward = Cvar_Get ("m_forward", "0.25", CVAR_ARCHIVE);
	m_side = Cvar_Get ("m_side", "0.25", CVAR_ARCHIVE);
//	m_filter = Cvar_Get ("m_filter", "0", CVAR_ARCHIVE);

#ifdef _XBOX
	cl_mapname = Cvar_Get ("cl_mapname", "t3_bounty", CVAR_TEMP);
#endif

	cl_updateInfoString = Cvar_Get( "cl_updateInfoString", "", CVAR_ROM );

	// userinfo
	Cvar_Get ("name", "Jaden", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get ("snaps", "20", CVAR_USERINFO | CVAR_ARCHIVE );
	
	Cvar_Get ("sex", "f", CVAR_USERINFO | CVAR_ARCHIVE | CVAR_SAVEGAME );
	Cvar_Get ("snd", "jaden_fmle", CVAR_USERINFO | CVAR_ARCHIVE | CVAR_SAVEGAME | CVAR_NORESTART );//UI_SetSexandSoundForModel changes to match sounds.cfg for model
	Cvar_Get ("handicap", "100", CVAR_USERINFO | CVAR_ARCHIVE | CVAR_SAVEGAME );

	// Hot-swap (programmable) buttons:
	Cvar_Get ("hotswap0", "", CVAR_ARCHIVE);
	Cvar_Get ("hotswap1", "", CVAR_ARCHIVE);
	Cvar_Get ("hotswap2", "", CVAR_ARCHIVE);

	//
	// register our commands
	//
	Cmd_AddCommand ("cmd", CL_ForwardToServer_f);
	Cmd_AddCommand ("configstrings", CL_Configstrings_f);
	Cmd_AddCommand ("clientinfo", CL_Clientinfo_f);
	Cmd_AddCommand ("snd_restart", CL_Snd_Restart_f);
	Cmd_AddCommand ("vid_restart", CL_Vid_Restart_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("cinematic", CL_PlayCinematic_f);
	Cmd_AddCommand ("ingamecinematic", CL_PlayInGameCinematic_f);
	Cmd_AddCommand ("uimenu", CL_GenericMenu_f);
	Cmd_AddCommand ("datapad", CL_DataPad_f);
	Cmd_AddCommand ("endscreendissolve", CL_EndScreenDissolve_f);
#ifdef _IMMERSION
	Cmd_AddCommand ("ff_restart", CL_FF_Restart_f);
#endif // _IMMERSION

#ifdef _XBOX
	XBLog_Write("JA: CL_Init: CL_InitRef...");
#endif
	CL_InitRef();
#ifdef _XBOX
	XBLog_Write("JA: CL_Init: CL_InitRef done; CL_StartHunkUsers...");
	g_SPXBPhaseLast = 0x434C4931; /* 'CLI1' */
#endif

	{
#ifdef _XBOX
		g_SPXBClHunkCaller = 3;
		g_SPXBClHunkCallCount++;
#endif
		CL_StartHunkUsers();
	}
#ifdef _XBOX
	g_SPXBPhaseLast = 0x434C4932; /* 'CLI2' */
	XBLog_Write("JA: CL_Init: CL_StartHunkUsers done; SCR_Init...");
#endif

	SCR_Init ();
#ifdef _XBOX
	g_SPXBPhaseLast = 0x434C4933; /* 'CLI3' */
	XBLog_Write("JA: CL_Init: SCR_Init done; Cbuf_Execute...");
#endif

	Cbuf_Execute ();
#ifdef _XBOX
	g_SPXBPhaseLast = 0x434C4934; /* 'CLI4' */
	XBLog_Write("JA: CL_Init: Cbuf_Execute done");
#endif

	Cvar_Set( "cl_running", "1" );

#ifdef _XBOX
	g_SPXBPhaseLast = 0x434C4935; /* 'CLI5' */
	Com_Printf( "Initializing Cinematics...\n");
	XBLog_Write("JA: CL_Init: CIN_Init (allocates Bink mem)...");
	CIN_Init();
	g_SPXBPhaseLast = 0x434C4936; /* 'CLI6' */
	XBLog_Write("JA: CL_Init: CIN_Init done");
#endif
//JLF MPMOVED
#ifdef _XBOX
//	initProfile();
#endif

	Cvar_Get("levelSelectCheat", "-1", CVAR_ARCHIVE | CVAR_SAVEGAME);

	Com_Printf( "----- Client Initialization Complete -----\n" );
}


/*
===============
CL_Shutdown

===============
*/
void CL_Shutdown( void ) {
	static qboolean recursive = qfalse;
	
	if ( !com_cl_running || !com_cl_running->integer ) {
		return;
	}

	Com_Printf( "----- CL_Shutdown -----\n" );

	if ( recursive ) {
		printf ("recursive shutdown\n");
		return;
	}
	recursive = qtrue;

	CL_ShutdownUI();
	CL_Disconnect();

	S_Shutdown();
	CL_ShutdownRef();

#ifdef _IMMERSION
	CL_ShutdownFF();
#endif // _IMMERSION
	Cmd_RemoveCommand ("cmd");
	Cmd_RemoveCommand ("configstrings");
	Cmd_RemoveCommand ("clientinfo");
	Cmd_RemoveCommand ("snd_restart");
	Cmd_RemoveCommand ("vid_restart");
	Cmd_RemoveCommand ("disconnect");
	Cmd_RemoveCommand ("cinematic");	
	Cmd_RemoveCommand ("ingamecinematic");
	Cmd_RemoveCommand ("pause");

	Cvar_Set( "cl_running", "0" );

	recursive = qfalse;

	memset( &cls, 0, sizeof( cls ) );

	Com_Printf( "-----------------------\n" );
}


/*
==================
CL_GetPing
==================
*/
void CL_GetPing( int n, char *adrstr, int *pingtime )
{
	const char*	str;
	int		time;

	if (!cl_pinglist[n].adr.port)
	{
		// empty slot
		adrstr[0] = '\0';
		*pingtime = 0;
		return;
	}

	str = NET_AdrToString( cl_pinglist[n].adr );
	strcpy( adrstr, str );

	time = cl_pinglist[n].time;
	if (!time)
	{
		// check for timeout
		time = cls.realtime - cl_pinglist[n].start;
		if (time < 500)
		{
			// not timed out yet
			time = 0;
		}
	}

	*pingtime = time;
}

/*
==================
CL_ClearPing
==================
*/
void CL_ClearPing( int n )
{
	if (n < 0 || n >= MAX_PINGREQUESTS)
		return;

	cl_pinglist[n].adr.port = 0;
}

/*
==================
CL_GetPingQueueCount
==================
*/
int CL_GetPingQueueCount( void )
{
	int		i;
	int		count;
	ping_t*	pingptr;

	count   = 0;
	pingptr = cl_pinglist;
	for (i=0; i<MAX_PINGREQUESTS; i++, pingptr++ )
		if (pingptr->adr.port)
			count++;

	return (count);
}

/*
==================
CL_GetFreePing
==================
*/
ping_t* CL_GetFreePing( void )
{
	ping_t*	pingptr;
	ping_t*	best;	
	int		oldest;
	int		i;
	int		time;

	pingptr = cl_pinglist;
	for (i=0; i<MAX_PINGREQUESTS; i++, pingptr++ )
	{
		// find free ping slot
		if (pingptr->adr.port)
		{
			if (!pingptr->time)
			{
				if (cls.realtime - pingptr->start < 500)
				{
					// still waiting for response
					continue;
				}
			}
			else if (pingptr->time < 500)
			{
				// results have not been queried
				continue;
			}
		}

		// clear it
		pingptr->adr.port = 0;
		return (pingptr);
	}

	// use oldest entry
	pingptr = cl_pinglist;
	best    = cl_pinglist;
	oldest  = INT_MIN;
	for (i=0; i<MAX_PINGREQUESTS; i++, pingptr++ )
	{
		// scan for oldest
		time = cls.realtime - pingptr->start;
		if (time > oldest)
		{
			oldest = time;
			best   = pingptr;
		}
	}

	return (best);
}

bool autosaveTrigger = false;
bool doAutoSave = false;
bool allowNormalAutosave = true;

static void checkAutoSave()
{
	static int timeToCheckpoint = 0;
	static int delayCountdown = 3;		// delay a few frames before saving
#ifdef _XBOX
	static int s_xboxAutoSaveTraceBudget = 0;
	const qboolean xboxTraceAutoSave = (cls.state >= CA_LOADING && s_xboxAutoSaveTraceBudget < 16);
	SPXB_HOT_SET(g_SPXBPhaseLast, 0x41535630); /* 'ASV0' */
	if (xboxTraceAutoSave)
	{
		XBLF("JA: checkAutoSave enter budget=%d state=%d ui=%d autosave=%d do=%d allow=%d sv.time=%d checkpoint=%d delay=%d",
			s_xboxAutoSaveTraceBudget,
			(int)cls.state,
			(int)cls.uiStarted,
			(int)autosaveTrigger,
			(int)doAutoSave,
			(int)allowNormalAutosave,
			sv.time,
			timeToCheckpoint,
			delayCountdown);
	}
#endif

	if(sv.time < timeToCheckpoint && timeToCheckpoint != 0)
	{
		allowNormalAutosave = false;
	}
	else
	{
		allowNormalAutosave = true;
		timeToCheckpoint = 0;
	}

	if(autosaveTrigger)
	{
		qboolean canAutoSave = qfalse;
		if( cls.uiStarted && cls.state == CA_ACTIVE )
		{
#ifdef _XBOX
			SPXB_HOT_SET(g_SPXBPhaseLast, 0x41535632); /* 'ASV2' */
			if (xboxTraceAutoSave)
			{
				XBLog_Write("JA: checkAutoSave before SG_GameAllowedToSaveHere");
			}
#endif
			canAutoSave = SG_GameAllowedToSaveHere(qfalse);
#ifdef _XBOX
			SPXB_HOT_SET(g_SPXBPhaseLast, 0x41535633); /* 'ASV3' */
			if (xboxTraceAutoSave)
			{
				XBLF("JA: checkAutoSave after SG_GameAllowedToSaveHere allowed=%d", (int)canAutoSave);
			}
#endif
			if (canAutoSave)
			{
#ifdef _XBOX
				SPXB_HOT_SET(g_SPXBPhaseLast, 0x41535634); /* 'ASV4' */
#endif
				canAutoSave = (Cvar_VariableIntegerValue("disableAutoSave") == 0);
			}
		}
#ifdef _XBOX
		else
		{
			SPXB_HOT_SET(g_SPXBPhaseLast, 0x41535631); /* 'ASV1' */
		}
#endif
		if( canAutoSave )
		{
			if(delayCountdown <= 0)
			{
				if(doAutoSave)
				{
					CG_CenterPrint( "@SP_INGAME_CHECKPOINT", SCREEN_HEIGHT * 0.25 );	//jump the network
					Cbuf_AddText( "save auto\n" );
				}
				timeToCheckpoint = sv.time + 10000;
				autosaveTrigger = false;
				doAutoSave = false;
				allowNormalAutosave = false;
				delayCountdown = 3;
			}
			else
			{
				delayCountdown--;
			}
		}
		else
		{
			delayCountdown = 3;
		}

	}
#ifdef _XBOX
	SPXB_HOT_SET(g_SPXBPhaseLast, 0x41535639); /* 'ASV9' */
	if (xboxTraceAutoSave)
	{
		XBLF("JA: checkAutoSave exit state=%d autosave=%d do=%d allow=%d delay=%d",
			(int)cls.state,
			(int)autosaveTrigger,
			(int)doAutoSave,
			(int)allowNormalAutosave,
			delayCountdown);
		++s_xboxAutoSaveTraceBudget;
	}
#endif
}
