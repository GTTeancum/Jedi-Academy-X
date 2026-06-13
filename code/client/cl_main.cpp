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
#endif

#include "client.h"
#include "client_ui.h"
#include <errno.h>
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

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
extern void FS_STEFX_PrecacheFile(const char *qpath);
#endif

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
static int CL_STEFX_ActiveCommandServerTime(void)
{
	static qboolean initialized = qfalse;
	static int serverTime = 72000;

	if (!initialized)
	{
		FILE *timeFile = fopen("D:\\ef_sp_active_command_time.txt", "r");
		initialized = qtrue;
		if (timeFile)
		{
			char line[64];
			if (fgets(line, sizeof(line), timeFile))
			{
				const int parsed = atoi(line);
				if (parsed >= 0)
				{
					serverTime = parsed;
				}
			}
			fclose(timeFile);
		}
		XBLF("STEFX: active command serverTime gate=%d", serverTime);
	}

	return serverTime;
}

static int CL_STEFX_QueueActiveCommands(void)
{
	const char *activeCommandPaths[] = {
		"D:\\ef_sp_active_commands.txt",
		"E:\\ef_sp_active_commands.txt",
		NULL
	};
	char commandLine[1024];
	int pathIndex;
	int queued = 0;

	for (pathIndex = 0; activeCommandPaths[pathIndex]; ++pathIndex)
	{
		FILE *activeCommandFile = fopen(activeCommandPaths[pathIndex], "r");
		if (!activeCommandFile)
		{
			XBLF("STEFX: active command file missing '%s' errno=%d winerr=%lu",
				activeCommandPaths[pathIndex], errno, GetLastError());
			continue;
		}

		XBLF("STEFX: active command file opened '%s'", activeCommandPaths[pathIndex]);
		while (fgets(commandLine, sizeof(commandLine), activeCommandFile))
		{
			commandLine[strcspn(commandLine, "\r\n")] = '\0';
			if (!commandLine[0])
			{
				continue;
			}

			XBLF("STEFX: queue active command '%s' from %s",
				commandLine, activeCommandPaths[pathIndex]);
			Cbuf_AddText(commandLine);
			Cbuf_AddText("\n");
			++queued;
		}
		fclose(activeCommandFile);
		break;
	}

	XBLF("STEFX: active command hook complete queued=%d state=%d cgame=%d sv=%d",
		queued, (int)cls.state, (int)cls.cgameStarted, (int)com_sv_running->integer);
	return queued;
}
#endif

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

#ifdef _XBOX
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

	// clear sounds (moved higher up within this func to avoid the odd sound stutter)
	S_DisableSounds();

	// unload the old VM
	CL_ShutdownCGame();

	CL_ShutdownUI();

	if ( re.Shutdown ) {
		re.Shutdown( qfalse );		// don't destroy window or context
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
	if ( !com_cl_running->integer ) {
		return;
	}

	Con_Close();
	cls.keyCatchers = 0;

	// if we are already connected to the local host, stay connected
	if ( cls.state >= CA_CONNECTED && !Q_stricmp( cls.servername, "localhost" ) )  {
		cls.state = CA_CONNECTED;		// so the connect screen is drawn
		memset( cls.updateInfoString, 0, sizeof( cls.updateInfoString ) );
//		memset( clc.serverMessage, 0, sizeof( clc.serverMessage ) );
		memset( &cl.gameState, 0, sizeof( cl.gameState ) );
		clc.lastPacketSentTime = -9999;
		SCR_UpdateScreen();
	} else {
		// clear nextmap so the cinematic shutdown doesn't execute it
		Cvar_Set( "nextmap", "" );
#ifdef _XBOX	// This was done at E3 time - it's nasty, but we may just keep it.
		connstate_t oldState = cls.state;
		cls.state = CA_CHALLENGING;
		XBLog_Write("JA: CL_MapLoading skipping transitional SCR_UpdateScreen on Xbox");
		cls.state = oldState;
#endif
		CL_Disconnect();
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

	CL_FlushMemory();
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
	if (s_xboxCLPacketLogs < 32)
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
	if (s_xboxCLPacketLogs < 32)
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
	if (s_xboxCLPacketLogs < 32)
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
	static int s_xboxFrameHeartbeat = 0;
	static int s_xboxLastClPhaseTime = 0;
	static qboolean s_xboxTraceClPhase = qfalse;
	static qboolean s_xboxTraceClTight = qfalse;
	s_xboxTraceClPhase = qfalse;
	s_xboxTraceClTight = qfalse;
	g_SPXBClFrameCount++;
	g_SPXBClsState = (unsigned int)cls.state;
	g_SPXBClServerTime = (unsigned int)cl.serverTime;
	g_SPXBClsFrameCount = (unsigned int)cls.framecount;
	g_SPXBPhaseLast = 0x434C4631; /* 'CLF1' */
	const qboolean xboxTraceEarlyActive = (cls.state == CA_ACTIVE && cls.framecount >= 54 && cls.framecount < 70);
	if (xboxTraceEarlyActive)
	{
		XBLF("JA: CL_EARLY enter staticFrame=%u clsFrame=%d realtime=%d serverTime=%d msec=%d frac=%g state=%d",
			frameCount, cls.framecount, cls.realtime, cl.serverTime, msec, fractionMsec, (int)cls.state);
	}
#endif

#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before checkAutoSave");
#endif
	checkAutoSave();	//saves the game immediately after starting a level
#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY after checkAutoSave");
#endif

	if ( !com_cl_running->integer ) {
#ifdef _XBOX
		g_SPXBPhaseLast = 0x434C4630; /* 'CLF0' */
#endif
		return;
	}

#if defined (_XBOX)// && !defined(_DEBUG)
	// Optional test harness: if D:\ja_sp_level.txt names a map, boot straight
	// into it before the normal UI path does any renderer work. Empty/missing
	// file means a normal menu boot.
	extern bool Sys_QuickStart( void );
	extern bool g_xboxDirectMapBootQueued;
	extern bool Sys_IsDirectMapBoot(void);
	static bool firstRun = true;
	if(firstRun)
	{
		if (Sys_IsDirectMapBoot())
		{
			XBLog_Write("JA: CL_Frame firstRun: direct-map boot already queued by main");
			firstRun = false;
		}
		else
		{
		char startupMap[MAX_QPATH];
		startupMap[0] = '\0';
		const char *startupMapPaths[] = {
			"D:\\ef_sp_level.txt",
			"D:\\ja_sp_level.txt",
			NULL
		};
		int startupMapPathIndex;
		for (startupMapPathIndex = 0; startupMapPaths[startupMapPathIndex] && !startupMap[0]; ++startupMapPathIndex)
		{
			FILE *startupMapFile = fopen(startupMapPaths[startupMapPathIndex], "r");
			if (startupMapFile)
			{
				char fileMap[MAX_QPATH];
				if (fgets(fileMap, sizeof(fileMap), startupMapFile))
				{
					fileMap[strcspn(fileMap, "\r\n\t ")] = '\0';
					if (fileMap[0])
					{
						Q_strncpyz(startupMap, fileMap, sizeof(startupMap));
						XBLF("JA: CL_Frame firstRun: startup map '%s' from %s", startupMap, startupMapPaths[startupMapPathIndex]);
					}
				}
				fclose(startupMapFile);
			}
		}

		const char *startupCommandPaths[] = {
			"D:\\ef_sp_commands.txt",
			"D:\\ja_sp_commands.txt",
			NULL
		};
		int startupCommandPathIndex;
		for (startupCommandPathIndex = 0; startupCommandPaths[startupCommandPathIndex]; ++startupCommandPathIndex)
		{
			FILE *startupCommandFile = fopen(startupCommandPaths[startupCommandPathIndex], "r");
			if (startupCommandFile)
			{
				char commandLine[1024];
				while (fgets(commandLine, sizeof(commandLine), startupCommandFile))
				{
					commandLine[strcspn(commandLine, "\r\n")] = '\0';
					if (commandLine[0])
					{
						XBLF("JA: CL_Frame firstRun: queue startup command '%s' from %s", commandLine, startupCommandPaths[startupCommandPathIndex]);
						Cbuf_AddText(commandLine);
						Cbuf_AddText("\n");
					}
				}
				fclose(startupCommandFile);
			}
		}
		if (startupMap[0])
		{
			XBLF("JA: CL_Frame firstRun: ja_sp_level.txt requested devmap %s before CL_StartHunkUsers", startupMap);
			Cbuf_AddText(va("devmap %s\n", startupMap));
			firstRun = false;
			return;
		}
		XBLog_Write("JA: CL_Frame firstRun: no ja_sp_level.txt map, continuing normal UI boot");
		}
	}
	
#endif

	// load the ref / cgame if needed
#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before CL_StartHunkUsers");
#endif
	CL_StartHunkUsers();
#ifdef _XBOX
	if (xboxTraceEarlyActive)
	{
		XBLF("JA: CL_EARLY after CL_StartHunkUsers state=%d ui=%d cgame=%d sv=%d",
			(int)cls.state, (int)cls.uiStarted, (int)cls.cgameStarted,
			(int)com_sv_running->integer);
	}
#endif

#if defined (_XBOX)	//xbox doesn't load ui in StartHunkUsers, so check it here
	static int s_xboxClFrameHunkLogBudget = 0;
	const qboolean xboxTraceClFrameHunk = (s_xboxClFrameHunkLogBudget > 0);
	if (xboxTraceClFrameHunk)
	{
		XBLF("JA: CL_Frame: CL_StartHunkUsers returned state=%d ui=%d cgame=%d sv=%d",
			(int)cls.state,
			(int)cls.uiStarted,
			(int)cls.cgameStarted,
			(int)com_sv_running->integer);
	}
	// load ui if needed
	if ( !cls.uiStarted && cls.state != CA_CINEMATIC &&
		((cls.keyCatchers & KEYCATCH_UI) || (cls.state == CA_DISCONNECTED && !com_sv_running->integer)) ) {
		XBLog_Write("JA: CL_Frame: starting Xbox UI init path");
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

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	static qboolean s_stefxActiveCommandsQueued = qfalse;
	static int s_stefxActiveCommandAttempts = 0;
	if (!s_stefxActiveCommandsQueued && cls.state == CA_ACTIVE && cls.cgameStarted
		&& cl.serverTime >= CL_STEFX_ActiveCommandServerTime())
	{
		++s_stefxActiveCommandAttempts;
		if (CL_STEFX_QueueActiveCommands() > 0 || s_stefxActiveCommandAttempts >= 20)
		{
			s_stefxActiveCommandsQueued = qtrue;
			XBLF("STEFX: active command hook armed-off attempts=%d", s_stefxActiveCommandAttempts);
		}
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
#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before CL_XboxAutoSmokeTick");
#endif
	CL_XboxAutoSmokeTick();
#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY after CL_XboxAutoSmokeTick");
#endif
#endif


	// if recording an avi, lock to a fixed fps
#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLF("JA: CL_EARLY before avidemo value=%d", cl_avidemo->integer);
#endif
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
#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLF("JA: CL_EARLY after avidemo msec=%d", msec);
#endif

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
#ifdef _XBOX
	if (xboxTraceEarlyActive)
	{
		XBLF("JA: CL_EARLY before lod staticFrame=%u avg=%g framerate=%g bias=%d inCamera=%d",
			frameCount, avgFrametime, framerate, bias, in_camera ? 1 : 0);
	}
#endif
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
#ifdef _XBOX
			if (xboxTraceEarlyActive) XBLF("JA: CL_EARLY before Cvar_SetValue lowfps bias=%d framerate=%g", bias + 1, framerate);
#endif
			bias++;
			Cvar_SetValue("r_lodbias", bias);
#ifdef _XBOX
			if (xboxTraceEarlyActive) XBLF("JA: CL_EARLY after Cvar_SetValue lowfps bias=%d", bias);
#endif
			lodFrameCount = -1;
		}

		lodFrameCount++;
		if(lodFrameCount==5 && bias > 0)
		{
#ifdef _XBOX
			if (xboxTraceEarlyActive) XBLF("JA: CL_EARLY before Cvar_SetValue recover bias=%d", bias - 1);
#endif
			bias--;
			Cvar_SetValue("r_lodbias", bias);
#ifdef _XBOX
			if (xboxTraceEarlyActive) XBLF("JA: CL_EARLY after Cvar_SetValue recover bias=%d", bias);
#endif
			lodFrameCount = 0;
		}
	}
	frameCount++;
#ifdef _XBOX
	if (xboxTraceEarlyActive)
	{
		XBLF("JA: CL_EARLY after lod staticFrame=%u bias=%d lodFrameCount=%d", frameCount, bias, lodFrameCount);
	}
#endif

	if(in_camera)
	{
		// No LOD stuff during cutscenes
		if ( !wasInCamera || bias != 0 )
		{
#ifdef _XBOX
			if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before camera Cvar_SetValue r_lodbias 0");
#endif
			Cvar_SetValue("r_lodbias", 0);
#ifdef _XBOX
			if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY after camera Cvar_SetValue r_lodbias 0");
#endif
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
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before CL_UpdateHotSwap");
	CL_UpdateHotSwap();
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY after CL_UpdateHotSwap");
#endif

	// see if we need to update any userinfo
#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before CL_CheckUserinfo");
#endif
	CL_CheckUserinfo();
#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY after CL_CheckUserinfo");
#endif

	// if we haven't gotten a packet in a long time,
	// drop the connection
#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before CL_CheckTimeout");
#endif
	CL_CheckTimeout();
#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY after CL_CheckTimeout");
#endif

	// send intentions now
#ifdef _XBOX
	if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE before CL_SendCmd");
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before CL_SendCmd");
#endif
	CL_SendCmd();
#ifdef _XBOX
	if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE after CL_SendCmd");
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY after CL_SendCmd");
#endif

	// resend a connection request if necessary
#ifdef _XBOX
	if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE before CL_CheckForResend");
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before CL_CheckForResend");
#endif
	CL_CheckForResend();
#ifdef _XBOX
	if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE after CL_CheckForResend");
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY after CL_CheckForResend");
#endif

	// decide on the serverTime to render
#ifdef _XBOX
	if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE before CL_SetCGameTime");
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before CL_SetCGameTime");
#endif
	CL_SetCGameTime();
#ifdef _XBOX
	if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE after CL_SetCGameTime");
	if (xboxTraceEarlyActive)
	{
		XBLF("JA: CL_EARLY after CL_SetCGameTime state=%d realtime=%d serverTime=%d pano=%d panoShots=%d skip=%d skipMod=%d end=%d dev=%d",
			(int)cls.state, cls.realtime, cl.serverTime,
			cl_pano ? cl_pano->integer : -1,
			cl_panoNumShots ? cl_panoNumShots->integer : -1,
			cl_skippingcin ? cl_skippingcin->integer : -1,
			cl_skippingcin ? cl_skippingcin->modified : -1,
			cl_endcredits ? cl_endcredits->integer : -1,
			com_developer ? com_developer->integer : -1);
	}
#endif

#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before pano check");
#endif
	if (cl_pano->integer && cls.state == CA_ACTIVE) {	//grab some panoramic shots
		int i = 1;
		int pref = cl_pano->integer;
		int oldnoprint = cl_noprint->integer;
#ifdef _XBOX
		if (xboxTraceEarlyActive) XBLF("JA: CL_EARLY pano branch shots=%d pref=%d", cl_panoNumShots->integer, pref);
#endif
		Con_Close();
		cl_noprint->integer = 1;	//hide the screen shot msgs
		for (; i <= cl_panoNumShots->integer; i++) {
#ifdef _XBOX
			if (xboxTraceEarlyActive) XBLF("JA: CL_EARLY pano before SCR_UpdateScreen shot=%d", i);
#endif
			Cvar_SetValue( "pano", i );
			SCR_UpdateScreen();// update the screen
#ifdef _XBOX
			if (xboxTraceEarlyActive) XBLF("JA: CL_EARLY pano after SCR_UpdateScreen shot=%d", i);
#endif
			Cbuf_ExecuteText( EXEC_NOW, va("screenshot %dpano%02d\n", pref, i) );	//grab this screen
		}
		Cvar_SetValue( "pano", 0 );	//done
		cl_noprint->integer = oldnoprint;
	}
#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY after pano check");
#endif

#ifdef _XBOX
	if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before skippingcin check");
#endif
	if (cl_skippingcin->integer && !cl_endcredits->integer && !com_developer->integer ) {
#ifdef _XBOX
		if (xboxTraceEarlyActive) XBLF("JA: CL_EARLY skippingcin branch modified=%d", cl_skippingcin->modified);
#endif
		if (cl_skippingcin->modified){
			S_StopSounds();		//kill em all but music	
			cl_skippingcin->modified=qfalse;
			Com_Printf (va(S_COLOR_YELLOW"%s"), SE_GetString("CON_TEXT_SKIPPING"));
#ifdef _XBOX
			if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY skippingcin before SCR_UpdateScreen");
#endif
			SCR_UpdateScreen();
#ifdef _XBOX
			if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY skippingcin after SCR_UpdateScreen");
#endif
		}
	} else {
#ifdef _XBOX
		if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY normal screen branch");
#endif
		// update the screen
#ifdef _XBOX
		if (cls.state < CA_LOADING && !(cls.uiStarted && (cls.keyCatchers & KEYCATCH_UI)))
		{
			static int s_xboxSkipPreLoadScreens = 0;
			if (s_xboxSkipPreLoadScreens < 8)
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
			const qboolean xboxTraceActiveScreen = (cls.state == CA_ACTIVE &&
				(s_xboxActiveScreenBoundaryCount < 10 || (cls.framecount >= 18 && cls.framecount < 30) || (cls.framecount >= 35 && cls.framecount < 70)));
			const qboolean xboxTraceLoadScreen = (cls.state >= CA_LOADING && cls.state < CA_ACTIVE && s_xboxLoadScreenBoundaryCount < 32);
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
			if (xboxTraceEarlyActive)
			{
				XBLF("JA: CL_EARLY before SCR_UpdateScreen state=%d activeCount=%d realtime=%d serverTime=%d",
					(int)cls.state, s_xboxActiveScreenBoundaryCount, cls.realtime, cl.serverTime);
			}
			SCR_UpdateScreen();
			if (xboxTraceEarlyActive)
			{
				XBLF("JA: CL_EARLY after SCR_UpdateScreen state=%d activeCount=%d realtime=%d serverTime=%d",
					(int)cls.state, s_xboxActiveScreenBoundaryCount, cls.realtime, cl.serverTime);
			}
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
		const qboolean xboxTraceActiveTail = (cls.state == CA_ACTIVE &&
			(s_xboxActiveFrameTailCount < 10 || (cls.framecount >= 18 && cls.framecount < 30) || (cls.framecount >= 35 && cls.framecount < 70)));
		static qboolean s_xboxLoggedAudioSkip = qfalse;
		static int s_xboxBootTailLogBudget = 0;
		const qboolean xboxTraceBootTail = (s_xboxBootTailLogBudget > 0);
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
		if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before S_Update");
		S_Update();
		if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY after S_Update");
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
		if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before SCR_RunCinematic");
		SCR_RunCinematic();
		if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY after SCR_RunCinematic");
		if (s_xboxTraceClTight) XBLF("JA: CL_TIGHT frame=%u after SCR_RunCinematic", frameCount);
		else if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE after SCR_RunCinematic");
		else if (xboxTraceActiveTail) XBLog_Write("JA: CL_Frame: after SCR_RunCinematic");
		else if (xboxTraceBootTail) XBLog_Write("JA: CL_BOOT_TAIL after SCR_RunCinematic");

		if (s_xboxTraceClTight) XBLF("JA: CL_TIGHT frame=%u before Con_RunConsole", frameCount);
		else if (s_xboxTraceClPhase) XBLog_Write("JA: CL_PHASE before Con_RunConsole");
		else if (xboxTraceActiveTail) XBLog_Write("JA: CL_Frame: before Con_RunConsole");
		else if (xboxTraceBootTail) XBLog_Write("JA: CL_BOOT_TAIL before Con_RunConsole");
		if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY before Con_RunConsole");
		Con_RunConsole();
		if (xboxTraceEarlyActive) XBLog_Write("JA: CL_EARLY after Con_RunConsole");
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
		if (s_xboxBootTailLogBudget > 0)
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
	g_SPXBClsState = (unsigned int)cls.state;
	g_SPXBClServerTime = (unsigned int)cl.serverTime;
	g_SPXBClsFrameCount = (unsigned int)cls.framecount;
	g_SPXBPhaseLast = 0x434C4632; /* 'CLF2' */
	{
		static int s_xboxCompletedFrameLogBudget = 0;
		if (s_xboxCompletedFrameLogBudget > 0)
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
		const int elapsed = cls.realtime - s_xboxLastCompletedHeartbeatTime;

		if (elapsed >= 1000 || s_xboxLastCompletedHeartbeatTime == 0)
		{
			const int frameDelta = cls.framecount - s_xboxLastCompletedHeartbeatFrame;
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

			_snprintf(msg, sizeof(msg),
				"JA: FRAME_HEARTBEAT completedFrame=%d realtime=%d serverTime=%d fd=%d el=%d fps=%d.%d r=%d cg=%d dl=%u surf=%u end=%u prim=%u verts=%u state=%u be=%u split=%u/%u/%u/%u final=%u flush=%u\n",
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
			msg[sizeof(msg) - 1] = '\0';
			XBLog_Write(msg);

			s_xboxLastCompletedHeartbeatTime = cls.realtime;
			s_xboxLastCompletedHeartbeatFrame = cls.framecount;
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
		}
	}
	if (cls.state == CA_ACTIVE)
	{
		static int s_xboxActiveCLExitLogBudget = 16;
		if (s_xboxActiveCLExitLogBudget > 0 || (cls.framecount >= 35 && cls.framecount < 70))
		{
			XBLF("JA: CL_Frame exit active framecount=%d realtime=%d serverTime=%d newSnapshots=%d frameValid=%d",
				cls.framecount, cls.realtime, cl.serverTime, (int)cl.newSnapshots, (int)cl.frame.valid);
			if (s_xboxActiveCLExitLogBudget > 0)
			{
				--s_xboxActiveCLExitLogBudget;
			}
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
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
static void CL_STEFX_PrecacheBorg1VerticalSliceFiles(void)
{
	static qboolean s_done = qfalse;
	static const char *files[] =
	{
		"ext_data/weapons.dat",
		"ext_data/items.dat",
		"ext_data/infostrings.dat",
		"ext_data/objectives.dat",
		"ext_data/tactical.dat",
		"ext_data/boltOns.cfg",
		"ext_data/NPCs.cfg",
		"ext_data/addon.npc",
		"maps/borg1.nav",
		"maps/borg1.sqd",
		"real_scripts/common/alarm.IBI",
		"real_scripts/common/anglemovetoggle.IBI",
		"real_scripts/common/bdead.IBI",
		"real_scripts/common/borgtalk.IBI",
		"real_scripts/common/cinematicMode.IBI",
		"real_scripts/common/console1.IBI",
		"real_scripts/common/console3.IBI",
		"real_scripts/common/cower.IBI",
		"real_scripts/common/crouchfight.IBI",
		"real_scripts/common/csatdie.IBI",
		"real_scripts/common/disrupted.IBI",
		"real_scripts/common/drillaspawn.IBI",
		"real_scripts/common/endlevel.IBI",
		"real_scripts/common/fadetobrig.IBI",
		"real_scripts/common/ffire2brig.IBI",
		"real_scripts/common/ffire2brigvoy.IBI",
		"real_scripts/common/ffire2brigvoy2.IBI",
		"real_scripts/common/ffire2brigvoy3.IBI",
		"real_scripts/common/fightback.IBI",
		"real_scripts/common/fighting_mad.IBI",
		"real_scripts/common/getupwalkANIM.IBI",
		"real_scripts/common/guard.IBI",
		"real_scripts/common/harvestermad.IBI",
		"real_scripts/common/init_assim.IBI",
		"real_scripts/common/invis_cinematic.IBI",
		"real_scripts/common/invisible.IBI",
		"real_scripts/common/runtoparm1.IBI",
		"real_scripts/common/scavbeamout.IBI",
		"real_scripts/common/security.IBI",
		"real_scripts/common/setupsecurity.IBI",
		"real_scripts/common/stumbledie.IBI",
		"real_scripts/common/useparm1.IBI",
		"real_scripts/common/vermin_idle.IBI",
		"real_scripts/common/workalook.IBI",
		"real_scripts/borg1/2for1.IBI",
		"real_scripts/borg1/alcovevengeance.IBI",
		"real_scripts/borg1/ambush.IBI",
		"real_scripts/borg1/backin5.IBI",
		"real_scripts/borg1/beamout.IBI",
		"real_scripts/borg1/behaved.pre",
		"real_scripts/borg1/borghunt.IBI",
		"real_scripts/borg1/borghuntgo.IBI",
		"real_scripts/borg1/caged.IBI",
		"real_scripts/borg1/console.IBI",
		"real_scripts/borg1/defendmachine.IBI",
		"real_scripts/borg1/die_munro_die.IBI",
		"real_scripts/borg1/disable.IBI",
		"real_scripts/borg1/dogmeat.IBI",
		"real_scripts/borg1/elevator.IBI",
		"real_scripts/borg1/endfield.IBI",
		"real_scripts/borg1/extra.pre",
		"real_scripts/borg1/getmeout.IBI",
		"real_scripts/borg1/goinghomeyay.IBI",
		"real_scripts/borg1/heyblue.IBI",
		"real_scripts/borg1/holdthefort.IBI",
		"real_scripts/borg1/intro.IBI",
		"real_scripts/borg1/killsplat.IBI",
		"real_scripts/borg1/mad_shaggy.IBI",
		"real_scripts/borg1/mrfixit.IBI",
		"real_scripts/borg1/mysavior.IBI",
		"real_scripts/borg1/niceshootin.IBI",
		"real_scripts/borg1/nodedestruct.IBI",
		"real_scripts/borg1/nodedestruct2.IBI",
		"real_scripts/borg1/noderepair.IBI",
		"real_scripts/borg1/notfair.IBI",
		"real_scripts/borg1/objective.IBI",
		"real_scripts/borg1/ow_im_dead.IBI",
		"real_scripts/borg1/pipemachine.IBI",
		"real_scripts/borg1/plugged.IBI",
		"real_scripts/borg1/protect.IBI",
		"real_scripts/borg1/protect2.IBI",
		"real_scripts/borg1/protect2a.IBI",
		"real_scripts/borg1/protecta.IBI",
		"real_scripts/borg1/repair.IBI",
		"real_scripts/borg1/retaliation.IBI",
		"real_scripts/borg1/rotate_splattamatron.IBI",
		"real_scripts/borg1/rounds.IBI",
		"real_scripts/borg1/setupworld.IBI",
		"real_scripts/borg1/sfflee.IBI",
		"real_scripts/borg1/sffleeleave.IBI",
		"real_scripts/borg1/splatconsole.IBI",
		"real_scripts/borg1/splatshift.IBI",
		"real_scripts/borg1/strutdestruction.IBI",
		"real_scripts/borg1/unimatrix.IBI",
		"real_scripts/borg1/vengeance.IBI",
		"real_scripts/borg1/waitvengeance.IBI",
		"real_scripts/borg1/yeah_get_some.IBI",

		"models/players/avatar/lower.mdr",
		"models/players/avatar/upper.mdr",
		"models/players/avatar/head.md3",
		"models/players/avatar/lower_default.skin",
		"models/players/avatar/upper_default.skin",
		"models/players/avatar/head_default.skin",
		"models/players/avatar/animation.cfg",

		"models/players/biessman/head.md3",
		"models/players/biessman/head_default.skin",
		"models/players/munro/head.md3",
		"models/players/munro/head_default.skin",
		"models/players/tuvok/head.md3",
		"models/players/tuvok/head_default.skin",
		"models/players/tuvok_h/head.md3",
		"models/players/tuvok_h/head_default.skin",

		"models/weaphits/explosion.md3",
		"models/weapons2/phaser/phaser.md3",
		"models/weapons2/phaser/phaser_hand.md3",
		"models/weapons2/phaser/phaser_flash.md3",
		"models/weapons2/prifle/prifle.md3",
		"models/weapons2/prifle/prifle_hand.md3",
		"models/weapons2/prifle/prifle_flash.md3",
		"models/weapons2/imod/imod2.md3",
		"models/weapons2/imod/imod2_hand.md3",
		"models/weapons2/imod/imod2_flash.md3",
		"models/weapons2/borg/claw-1.md3",
		"models/weapons2/borg/pincers.md3",
		"models/weapons2/borg/hand.md3",
		"models/weapons2/borg/drill.md3",
		NULL
	};
	int i;

	if (s_done)
	{
		return;
	}
	s_done = qtrue;

	XBLog_Write("STEFX: borg1 vertical slice file precache begin");
	for (i = 0; files[i]; ++i)
	{
		FS_STEFX_PrecacheFile(files[i]);
	}
	XBLog_Write("STEFX: borg1 vertical slice file precache done");
}
#endif

void CL_StartHunkUsers( void ) {
#ifdef _XBOX
	qboolean xboxTraceStartHunk = (!cls.rendererStarted || !cls.soundStarted || !cls.soundRegistered ||
		(!cls.cgameStarted && cls.state > CA_CONNECTED && (cls.state != CA_CINEMATIC && !CL_IsRunningInGameCinematic())));
	if (xboxTraceStartHunk)
	{
		XBLog_Write("JA: CL_StartHunkUsers entered");
	}
#endif
	if ( !com_cl_running->integer ) {
#ifdef _XBOX
		if (xboxTraceStartHunk)
		{
			XBLog_Write("JA: CL_StartHunkUsers: cl_running=0, early return");
		}
#endif
		return;
	}

	if ( !cls.rendererStarted ) {
#ifdef _XBOX
		XBLog_Write("JA: CL_StartHunkUsers: re.BeginRegistration (calls R_Init)...");
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
		XBLog_Write("JA: CL_StartHunkUsers: re.BeginRegistration done");
		XBLog_Write("JA: CL_StartHunkUsers: RegisterShaderNoMip charsgrid_med...");
#endif

		// load character sets
//		cls.charSetShader = re.RegisterShaderNoMip( "gfx/2d/bigchars" );
		cls.charSetShader = re.RegisterShaderNoMip( "gfx/2d/charsgrid_med" );
#ifdef _XBOX
		XBLF("JA: CL_StartHunkUsers: charSetShader=%d, RegisterShader white...", cls.charSetShader);
#endif
		cls.whiteShader = re.RegisterShader( "white" );
#ifdef _XBOX
		XBLF("JA: CL_StartHunkUsers: whiteShader=%d", cls.whiteShader);
#endif
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		CL_STEFX_PrecacheBorg1VerticalSliceFiles();
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
			XBLog_Write("JA: CL_StartHunkUsers: Sys_In_Restart_f...");
#endif
			extern void Sys_In_Restart_f( void );
			Sys_In_Restart_f();
#ifdef _XBOX
			XBLog_Write("JA: CL_StartHunkUsers: Sys_In_Restart_f done");
#endif
		}

#ifdef _XBOX
		if (vidRestartReloadMap)
		{
			int checksum;
			CM_LoadMap(va("maps/%s.bsp", cl_mapname->string), qfalse, &checksum);
			RE_LoadWorldMap(va("maps/%s.bsp", cl_mapname->string));
			vidRestartReloadMap = qfalse;
		}
#endif // _XBOX

#endif // _IMMERSION
	}

	if ( !cls.soundStarted ) {
		cls.soundStarted = qtrue;
#ifdef _XBOX
		XBLog_Write("JA: CL_StartHunkUsers: S_Init...");
#endif
		S_Init();
#ifdef _XBOX
		XBLog_Write("JA: CL_StartHunkUsers: S_Init done");
#endif
	}

	if ( !cls.soundRegistered ) {
		cls.soundRegistered = qtrue;
#ifdef _XBOX
		XBLog_Write("JA: CL_StartHunkUsers: S_BeginRegistration...");
#endif
		S_BeginRegistration();
#ifdef _XBOX
		XBLog_Write("JA: CL_StartHunkUsers: S_BeginRegistration done");
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
		XBLog_Write("JA: CL_StartHunkUsers: CL_InitCGame...");
#endif
		CL_InitCGame();
#ifdef _XBOX
		XBLog_Write("JA: CL_StartHunkUsers: CL_InitCGame done");
#endif
	}
#ifdef _XBOX
	if (xboxTraceStartHunk)
	{
		XBLog_Write("JA: CL_StartHunkUsers: COMPLETE");
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
#endif

	CL_StartHunkUsers();
#ifdef _XBOX
	XBLog_Write("JA: CL_Init: CL_StartHunkUsers done; SCR_Init...");
#endif

	SCR_Init ();
#ifdef _XBOX
	XBLog_Write("JA: CL_Init: SCR_Init done; Cbuf_Execute...");
#endif

	Cbuf_Execute ();
#ifdef _XBOX
	XBLog_Write("JA: CL_Init: Cbuf_Execute done");
#endif

	Cvar_Set( "cl_running", "1" );

#ifdef _XBOX
	Com_Printf( "Initializing Cinematics...\n");
	XBLog_Write("JA: CL_Init: CIN_Init (allocates Bink mem)...");
	CIN_Init();
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
		if( cls.uiStarted && cls.state == CA_ACTIVE && SG_GameAllowedToSaveHere(qfalse)
			&& Cvar_VariableIntegerValue("disableAutoSave") == 0 )
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
}
