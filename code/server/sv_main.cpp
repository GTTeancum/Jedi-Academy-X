// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"



#include "server.h"

#if defined(STEFX_SP_HOSTED_MP)
extern void STEFX_HolomatchHostRunFrame(int levelTime);
extern void STEFX_HolomatchHostAfterGameFrame(int levelTime);
#endif
#ifdef _XBOX
#include "../win32/xb_log.h"
extern "C" volatile unsigned int g_SPXBSvFrameCount;
extern "C" volatile unsigned int g_SPXBPhaseLast;
extern "C" volatile unsigned int g_SPXBPerfServerTicks;
extern "C" volatile unsigned int g_SPXBPerfServerLastGameMsec;
extern "C" volatile unsigned int g_SPXBPerfServerMaxGameMsec;
#endif
/*
Ghoul2 Insert Start
*/
#if !defined (MINIHEAP_H_INC)
	#include "../qcommon/miniheap.h"
#endif
/*
Ghoul2 Insert End
*/

serverStatic_t	svs;				// persistant server info
server_t		sv;					// local server
game_export_t	*ge;

cvar_t	*sv_fps;				// time rate for running non-clients
cvar_t	*sv_timeout;			// seconds without any message
cvar_t	*sv_zombietime;			// seconds to sink messages after disconnect
cvar_t	*sv_reconnectlimit;		// minimum seconds between connect messages
cvar_t	*sv_showloss;			// report when usercmds are lost
cvar_t	*sv_killserver;			// menu system can set to 1 to shut server down
cvar_t	*sv_mapname;
cvar_t	*sv_spawntarget;
cvar_t	*sv_mapChecksum;
cvar_t	*sv_serverid;
cvar_t	*sv_testsave;			// Run the savegame enumeration every game frame
cvar_t	*sv_compress_saved_games;	// compress the saved games on the way out (only affect saver, loader can read both)

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
static qboolean SV_STEFX_SmokeHarnessEnabled(void)
{
	static qboolean initialized = qfalse;
	static qboolean enabled = qfalse;

	if (!initialized)
	{
		const char *paths[] = {
			"D:\\ef_sp_smoke_harness.txt",
			"E:\\ef_sp_smoke_harness.txt",
			NULL
		};
		int pathIndex;
		initialized = qtrue;
		for (pathIndex = 0; paths[pathIndex]; ++pathIndex)
		{
			FILE *marker = fopen(paths[pathIndex], "r");
			if (marker)
			{
				fclose(marker);
				enabled = qtrue;
				break;
			}
		}
	}

	return enabled;
}

static int SV_STEFX_ActiveCommandServerTime(void)
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
		XBLF("STEFX_SAVELOAD: server active command gate=%d", serverTime);
	}

	return serverTime;
}

static int SV_STEFX_QueueActiveCommands(void)
{
	const char *activeCommandPaths[] = {
		"D:\\ef_sp_active_commands.txt",
		"E:\\ef_sp_active_commands.txt",
		NULL
	};
	char commandLine[1024];
	int pathIndex;
	int queued = 0;
	static int s_missingLogBudget = 2;

	for (pathIndex = 0; activeCommandPaths[pathIndex]; ++pathIndex)
	{
		FILE *activeCommandFile = fopen(activeCommandPaths[pathIndex], "r");
		if (!activeCommandFile)
		{
			if (s_missingLogBudget > 0)
			{
				XBLF("STEFX_SAVELOAD: server active command missing '%s'", activeCommandPaths[pathIndex]);
				--s_missingLogBudget;
			}
			continue;
		}

		XBLF("STEFX_SAVELOAD: server active command opened '%s'", activeCommandPaths[pathIndex]);
		while (fgets(commandLine, sizeof(commandLine), activeCommandFile))
		{
			commandLine[strcspn(commandLine, "\r\n")] = '\0';
			if (!commandLine[0])
			{
				continue;
			}

			XBLF("STEFX_SAVELOAD: server queue active command '%s'", commandLine);
			Cbuf_AddText(commandLine);
			Cbuf_AddText("\n");
			++queued;
		}
		fclose(activeCommandFile);
		if (queued > 0)
		{
			if (remove(activeCommandPaths[pathIndex]) == 0)
			{
				XBLF("STEFX_SAVELOAD: server active command consumed '%s'", activeCommandPaths[pathIndex]);
			}
			else
			{
				XBLF("STEFX_SAVELOAD: server active command consume failed '%s' errno=%d", activeCommandPaths[pathIndex], errno);
			}
		}
		break;
	}

	if (queued > 0)
	{
		XBLF("STEFX_SAVELOAD: server active command queued=%d state=%d svTime=%d", queued, sv.state, sv.time);
	}
	return queued;
}
#endif

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/

/*
===============
SV_ExpandNewlines

Converts newlines to "\n" so a line prints nicer
===============
*/
char	*SV_ExpandNewlines( char *in ) {
	static	char	string[1024];
	int		l;

	l = 0;
	while ( *in && l < sizeof(string) - 3 ) {
		if ( *in == '\n' ) {
			string[l++] = '\\';
			string[l++] = 'n';
		} else {
			string[l++] = *in;
		}
		in++;
	}
	string[l] = 0;

	return string;
}

/*
======================
SV_AddServerCommand

The given command will be transmitted to the client, and is guaranteed to
not have future snapshot_t executed before it is executed
======================
*/
void SV_AddServerCommand( client_t *client, const char *cmd ) {
	int		index;

	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	if ( client->reliableSequence - client->reliableAcknowledge > MAX_RELIABLE_COMMANDS ) {
		SV_DropClient( client, "Server command overflow" );
		return;
	}
	client->reliableSequence++;
	index = client->reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	if ( client->reliableCommands[ index ] ) {
		Z_Free( client->reliableCommands[ index ] );
	}
	client->reliableCommands[ index ] = CopyString( cmd );
}


/*
=================
SV_SendServerCommand

Sends a reliable command string to be interpreted by 
the client game module: "cp", "print", "chat", etc
A NULL client will broadcast to all clients
=================
*/
void SV_SendServerCommand(client_t *cl, const char *fmt, ...) {
	va_list		argptr;
	byte		message[MAX_MSGLEN];
	int			len;
	client_t	*client;
	int			j;
	
	message[0] = svc_serverCommand;

	va_start (argptr,fmt);
	vsprintf ((char *)message+1, fmt,argptr);
	va_end (argptr);
	len = strlen( (char *)message ) + 1;

	if ( cl != NULL ) {
		SV_AddServerCommand( cl, (char *)message );
		return;
	}

	// send the data to all relevent clients
	for (j = 0, client = svs.clients; j < STEFX_SERVER_CLIENT_SLOTS ; j++, client++) {
		if ( client->state < CS_PRIMED ) {
			continue;
		}
		SV_AddServerCommand( client, (char *)message );
	}
}



/*
==============================================================================

CONNECTIONLESS COMMANDS

==============================================================================
*/

/*
================
SVC_Status

Responds with all the info that qplug or qspy can see about the server
and all connected players.  Used for getting detailed information after
the simple info query.
================
*/
void SVC_Status( netadr_t from ) {
	char	player[1024];
	char	status[MAX_MSGLEN];
	int		i;
	client_t	*cl;
	int		statusLength;
	int		playerLength;
	int		score;
	char	infostring[MAX_INFO_STRING];

	strcpy( infostring, Cvar_InfoString( CVAR_SERVERINFO ) );

	// echo back the parameter to status. so servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv(1) );

	status[0] = 0;
	statusLength = 0;

	for (i=0 ; i < STEFX_SERVER_CLIENT_SLOTS ; i++) {
		cl = &svs.clients[i];
		if ( cl->state >= CS_CONNECTED ) {
			if ( cl->gentity && cl->gentity->client ) {
				score = cl->gentity->client->persistant[PERS_SCORE];
			} else {
				score = 0;
			}
			Com_sprintf (player, sizeof(player), "%i %i \"%s\"\n", 
				score, cl->ping, cl->name);
			playerLength = strlen(player);
			if (statusLength + playerLength >= sizeof(status) ) {
				break;		// can't hold any more
			}
			strcpy (status + statusLength, player);
			statusLength += playerLength;
		}
	}

	NET_OutOfBandPrint( NS_SERVER, from, "statusResponse\n%s\n%s", infostring, status );
}

/*
================
SVC_Info

Responds with a short info message that should be enough to determine
if a user is interested in a server to do a full status
================
*/
static void SVC_Info( netadr_t from ) {
	int		i, count;
	char	infostring[MAX_INFO_STRING];

	count = 0;
	for ( i = 0 ; i < STEFX_SERVER_CLIENT_SLOTS ; i++ ) {
		if ( svs.clients[i].state >= CS_CONNECTED ) {
			count++;
		}
	}

	infostring[0] = 0;

	// echo back the parameter to status. so servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv(1) );

	Info_SetValueForKey( infostring, "protocol", va("%i", PROTOCOL_VERSION) );
	//Info_SetValueForKey( infostring, "hostname", sv_hostname->string );
	Info_SetValueForKey( infostring, "mapname", sv_mapname->string );
	Info_SetValueForKey( infostring, "clients", va("%i", count) );
	Info_SetValueForKey( infostring, "sv_maxclients", va("%i", STEFX_SERVER_CLIENT_SLOTS) );

	NET_OutOfBandPrint( NS_SERVER, from, "infoResponse\n%s", infostring );
}


/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
static void SV_ConnectionlessPacket( netadr_t from, msg_t *msg ) {
	char	*s;
	char	*c;

	MSG_BeginReading( msg );
	MSG_ReadLong( msg );		// skip the -1 marker

	s = MSG_ReadStringLine( msg );

	Cmd_TokenizeString( s );

	c = Cmd_Argv(0);
	Com_DPrintf ("SV packet %s : %s\n", NET_AdrToString(from), c);

	if (!strcmp(c,"getstatus")) {
		SVC_Status( from  );
	} else if (!strcmp(c,"getinfo")) {
		SVC_Info( from );
	} else if (!strcmp(c,"connect")) {
		SV_DirectConnect( from );
	} else if (!strcmp(c,"disconnect")) {
		// if a client starts up a local server, we may see some spurious
		// server disconnect messages when their new server sees our final
		// sequenced messages to the old client
	} else {
		Com_DPrintf ("bad connectionless packet from %s:\n%s\n"
		, NET_AdrToString (from), s);
	}
}


//============================================================================

/*
=================
SV_ReadPackets
=================
*/
void SV_PacketEvent( netadr_t from, msg_t *msg ) {
#ifdef _XBOX
	static int s_xboxSVPacketLogs = 0;
	if (s_xboxSVPacketLogs < 16)
	{
		Com_PrintfAlways("JA: SV_PacketEvent enter fromType=%d size=%d read=%d\n",
			(int)from.type, msg ? msg->cursize : -1, msg ? msg->readcount : -1);
		++s_xboxSVPacketLogs;
	}
#endif
	int			i;
	client_t	*cl;
	int			qport;

	// check for connectionless packet (0xffffffff) first
	if ( msg->cursize >= 4 && *(int *)msg->data == -1) {
		SV_ConnectionlessPacket( from, msg );
		return;
	}

	// read the qport out of the message so we can fix up
	// stupid address translating routers
	MSG_BeginReading( msg );
	MSG_ReadLong( msg );				// sequence number
	MSG_ReadLong( msg );				// sequence number
	qport = MSG_ReadShort( msg ) & 0xffff;

	// find which client the message is from
	for (i=0, cl=svs.clients ; i < STEFX_SERVER_CLIENT_SLOTS ; i++,cl++) {
		if (cl->state == CS_FREE) {
			continue;
		}
		if ( !NET_CompareBaseAdr( from, cl->netchan.remoteAddress ) ) {
			continue;
		}
		// it is possible to have multiple clients from a single IP
		// address, so they are differentiated by the qport variable
		if (cl->netchan.qport != qport) {
			continue;
		}

		// the IP port can't be used to differentiate them, because
		// some address translating routers periodically change UDP
		// port assignments
		if (cl->netchan.remoteAddress.port != from.port) {
			Com_Printf( "SV_ReadPackets: fixing up a translated port\n" );
			cl->netchan.remoteAddress.port = from.port;
		}

		// make sure it is a valid, in sequence packet
		if (Netchan_Process(&cl->netchan, msg)) {
			// zombie clients stil neet to do the Netchan_Process
			// to make sure they don't need to retransmit the final
			// reliable message, but they don't do any other processing
			if (cl->state != CS_ZOMBIE) {
				cl->lastPacketTime = sv.time;	// don't timeout
				cl->frames[ cl->netchan.incomingAcknowledged & PACKET_MASK ]
					.messageAcked = sv.time;
				SV_ExecuteClientMessage( cl, msg );
			}
		}
		return;
	}
	
	// if we received a sequenced packet from an address we don't reckognize,
	// send an out of band disconnect packet to it
	NET_OutOfBandPrint( NS_SERVER, from, "disconnect" );
}


/*
===================
SV_CalcPings

Updates the cl->ping variables
===================
*/
void SV_CalcPings (void) {
	int			i, j;
	client_t	*cl;
	int			total, count;
	int			delta;

	for (i=0 ; i < STEFX_SERVER_CLIENT_SLOTS ; i++) {
		cl = &svs.clients[i];
		if ( cl->state != CS_ACTIVE ) {
			continue;
		}
		if ( cl->gentity->svFlags & SVF_BOT ) {
			continue;
		}

		total = 0;
		count = 0;
		for ( j = 0 ; j < PACKET_BACKUP ; j++ ) {
			delta = cl->frames[j].messageAcked - cl->frames[j].messageSent;
			if ( delta >= 0 ) {
				count++;
				total += delta;
			}
		}
		if (!count) {
			cl->ping = 999;
		} else {
			cl->ping = total/count;
			if ( cl->ping > 999 ) {
				cl->ping = 999;
			}
		}

		// let the game dll know about the ping
		cl->gentity->client->ping = cl->ping;
	}
}

/*
==================
SV_CheckTimeouts

If a packet has not been received from a client for timeout->integer 
seconds, drop the conneciton.  Server time is used instead of
realtime to avoid dropping the local client while debugging.

When a client is normally dropped, the client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
void SV_CheckTimeouts( void ) {
	int		i;
	client_t	*cl;
	int			droppoint;
	int			zombiepoint;

	droppoint = sv.time - 1000 * sv_timeout->integer;
	zombiepoint = sv.time - 1000 * sv_zombietime->integer;

	for (i=0,cl=svs.clients ; i < STEFX_SERVER_CLIENT_SLOTS ; i++,cl++) {
		// message times may be wrong across a changelevel
		if (cl->lastPacketTime > sv.time) {
			cl->lastPacketTime = sv.time;
		}

		if (cl->state == CS_ZOMBIE
		&& cl->lastPacketTime < zombiepoint) {
			cl->state = CS_FREE;	// can now be reused
			continue;
		}
		if ( cl->state >= CS_CONNECTED && cl->lastPacketTime < droppoint) {
			// wait several frames so a debugger session doesn't
			// cause a timeout
			if ( ++cl->timeoutCount > 5 ) {
				SV_DropClient (cl, "timed out"); 
				cl->state = CS_FREE;	// don't bother with zombie state
			}
		} else {
			cl->timeoutCount = 0;
		}
	}
}


/*
==================
SV_CheckPaused
==================
*/
qboolean SV_CheckPaused( void ) {
	if ( !cl_paused->integer ) {
		return qfalse;
	}

	sv_paused->integer = 1;
	return qtrue;
}

/*
This wonderful hack is needed to avoid rendering frames until several camera related things
have wended their way through the network. The problem is basically that the server asks the
client where the camera is to decide what entities down to the client. However right after
certain transitions the client tends to give a wrong answer. CGCam_Disable is one such time/
When this happens we want to dump all rendered frame until these things have happened, in 
order:

0) (This state will mean that we are awaiting state 1)
1) The server has run a frame and built a packet
2) The client has computed a camera position
3) The server has run a frame and built a packet
4) The client has recieved a packet (This state also means the game is running normally).

We will keep track of this here:

*/


/*
==================
SV_Frame

Player movement occurs as a result of packet events, which
happen before SV_Frame is called
==================
*/
extern cvar_t	*cl_newClock;
void SV_Frame( int msec,float fractionMsec ) {
	int		frameMsec;
	int		startTime=0;
#ifdef _XBOX
	unsigned int xboxGameTicks = 0;
	unsigned int xboxLastGameMsec = 0;
	unsigned int xboxMaxGameMsec = 0;
#endif
#ifdef _XBOX
	g_SPXBSvFrameCount++;
	g_SPXBPhaseLast = 0x53564631; /* 'SVF1' */
	static int s_xboxSVFrameLogBudget = 8;
	const qboolean xboxTraceSVFrame = (s_xboxSVFrameLogBudget > 0);
	if (xboxTraceSVFrame)
	{
		Com_PrintfAlways("JA: SV_Frame enter msec=%d running=%d time=%d residual=%d clientState=%d\n",
			msec,
			com_sv_running ? com_sv_running->integer : -1,
			sv.time,
			sv.timeResidual,
			svs.clients ? svs.clients[0].state : -1);
	}
#endif
	
	// the menu kills the server with this cvar
	if ( sv_killserver->integer ) {
#ifdef _XBOX
		if (xboxTraceSVFrame) Com_PrintfAlways("JA: SV_Frame exit killserver\n");
#endif
		SV_Shutdown ("Server was killed.\n");
		Cvar_Set( "sv_killserver", "0" );
		return;
	}

	if ( !com_sv_running->integer ) {
#ifdef _XBOX
		if (xboxTraceSVFrame) Com_PrintfAlways("JA: SV_Frame exit not-running\n");
#endif
		return;
	}

 	extern void SE_CheckForLanguageUpdates(void);
#ifdef _XBOX
	if (xboxTraceSVFrame) Com_PrintfAlways("JA: SV_Frame before language updates\n");
#endif
	SE_CheckForLanguageUpdates();	// will fast-return else load different language if menu changed it
#ifdef _XBOX
	if (xboxTraceSVFrame) Com_PrintfAlways("JA: SV_Frame after language updates\n");
#endif

	// allow pause if only the local client is connected
#ifdef _XBOX
	if (xboxTraceSVFrame) Com_PrintfAlways("JA: SV_Frame before pause check\n");
#endif
	if ( SV_CheckPaused() ) {
#ifdef _XBOX
		if (xboxTraceSVFrame) Com_PrintfAlways("JA: SV_Frame exit paused\n");
#endif
		return;
	}
#ifdef _XBOX
	if (xboxTraceSVFrame) Com_PrintfAlways("JA: SV_Frame after pause check\n");
#endif

	// go ahead and let time slip if the server really hitched badly
	if ( msec > 1000 ) {
		Com_DPrintf( "SV_Frame: Truncating msec of %i to 1000\n", msec );
		msec = 1000;
	}

	// if it isn't time for the next frame, do nothing
	if ( sv_fps->integer < 1 ) {
		Cvar_Set( "sv_fps", "10" );
	}
	frameMsec = 1000 / sv_fps->integer ;

	sv.timeResidual += msec;
	sv.timeResidualFraction+=fractionMsec;
	if (sv.timeResidualFraction>=1.0f)
	{
		sv.timeResidualFraction-=1.0f;
		if (cl_newClock&&cl_newClock->integer)
		{
			sv.timeResidual++;
		}
	}
	if ( sv.timeResidual < frameMsec ) {
#ifdef _XBOX
		if (xboxTraceSVFrame)
		{
			Com_PrintfAlways("JA: SV_Frame exit residual-wait residual=%d frameMsec=%d\n",
				sv.timeResidual, frameMsec);
			--s_xboxSVFrameLogBudget;
		}
#endif
		return;
	}

	// if time is about to hit the 32nd bit, restart the
	// level, which will force the time back to zero, rather
	// than checking for negative time wraparound everywhere.
	// 2giga-milliseconds = 23 days, so it won't be too often
	if ( sv.time > 0x70000000 ) {
		SV_Shutdown( "Restarting server due to time wrapping" );
		Com_Printf("You win.  if you can play this long and not die, you deserve to win.\n");
		return;
	}

	// update infostrings if anything has been changed
	if ( cvar_modifiedFlags & CVAR_SERVERINFO ) {
		SV_SetConfigstring( CS_SERVERINFO, Cvar_InfoString( CVAR_SERVERINFO ) );
		cvar_modifiedFlags &= ~CVAR_SERVERINFO;
	}
	if ( cvar_modifiedFlags & CVAR_SYSTEMINFO ) {
		SV_SetConfigstring( CS_SYSTEMINFO, Cvar_InfoString( CVAR_SYSTEMINFO ) );
		cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;
	}

	if ( com_speeds->integer ) {
		startTime = Sys_Milliseconds ();
	}

//	SV_BotFrame( sv.time );

	// run the game simulation in chunks
	while ( sv.timeResidual >= frameMsec ) {
#ifdef _XBOX
		const int xboxGameStart = Sys_Milliseconds();
#endif
		sv.timeResidual -= frameMsec;
		sv.time += frameMsec;
#if !defined(STEFX_ELITE_FORCE_SP)
		G2API_SetTime(sv.time,G2T_SV_TIME);
#endif

		// let everything in the world think and move
#ifdef _XBOX
		if (xboxTraceSVFrame)
		{
			Com_PrintfAlways("JA: SV_Frame before ge->RunFrame time=%d residual=%d\n",
				sv.time, sv.timeResidual);
		}
#endif
#if defined(STEFX_SP_HOSTED_MP)
		STEFX_HolomatchHostRunFrame(sv.time);
#endif
		ge->RunFrame( sv.time );
#ifdef _XBOX
		xboxLastGameMsec = (unsigned int)(Sys_Milliseconds() - xboxGameStart);
		if (xboxLastGameMsec > xboxMaxGameMsec)
		{
			xboxMaxGameMsec = xboxLastGameMsec;
		}
		xboxGameTicks++;
#endif
#if defined(STEFX_SP_HOSTED_MP)
		STEFX_HolomatchHostAfterGameFrame(sv.time);
#endif
#ifdef _XBOX
		if (xboxTraceSVFrame)
		{
			Com_PrintfAlways("JA: SV_Frame after ge->RunFrame time=%d residual=%d\n",
				sv.time, sv.timeResidual);
		}
#endif
	}
#ifdef _XBOX
	g_SPXBPerfServerTicks = xboxGameTicks;
	g_SPXBPerfServerLastGameMsec = xboxLastGameMsec;
	g_SPXBPerfServerMaxGameMsec = xboxMaxGameMsec;
#endif

	if ( com_speeds->integer ) {
		time_game = Sys_Milliseconds () - startTime;
	}

	SG_TestSave();	// returns immediately if not active, used for fake-save-every-cycle to test (mainly) Icarus disk code

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP) && !defined(STEFX_SP_HOSTED_MP)
	{
		static qboolean s_activeCommandsQueued = qfalse;
		static int s_activeCommandAttempts = 0;
		static int s_activeCommandNextPollTime = 0;
		static qboolean s_activeCommandStateLogged = qfalse;
		if (!s_activeCommandsQueued && sv.state == SS_GAME && !s_activeCommandStateLogged)
		{
			s_activeCommandStateLogged = qtrue;
			XBLF("STEFX_SAVELOAD: server active command state entered state=%d svTime=%d residual=%d clientState=%d",
				sv.state,
				sv.time,
				sv.timeResidual,
				svs.clients ? svs.clients[0].state : -1);
		}
		if (SV_STEFX_SmokeHarnessEnabled()
			&& !s_activeCommandsQueued && sv.state == SS_GAME
			&& sv.time >= SV_STEFX_ActiveCommandServerTime()
			&& sv.time >= s_activeCommandNextPollTime)
		{
			++s_activeCommandAttempts;
			s_activeCommandNextPollTime = sv.time + 1000;
			if (SV_STEFX_QueueActiveCommands() > 0 || s_activeCommandAttempts >= 20)
			{
				s_activeCommandsQueued = qtrue;
				XBLF("STEFX_SAVELOAD: server active command armed-off attempts=%d", s_activeCommandAttempts);
			}
		}
	}
#endif

	// check timeouts
	SV_CheckTimeouts ();

	// update ping based on the last known frame from all clients
	SV_CalcPings ();

	// send messages back to the clients
#ifdef _XBOX
	if (xboxTraceSVFrame) Com_PrintfAlways("JA: SV_Frame before SV_SendClientMessages\n");
#endif
	SV_SendClientMessages ();
#ifdef _XBOX
	if (xboxTraceSVFrame)
	{
		Com_PrintfAlways("JA: SV_Frame exit sent time=%d residual=%d clientState=%d nextSnapshot=%d\n",
			sv.time,
			sv.timeResidual,
			svs.clients ? svs.clients[0].state : -1,
			svs.clients ? svs.clients[0].nextSnapshotTime : -1);
		--s_xboxSVFrameLogBudget;
	}
#endif
}

//============================================================================

