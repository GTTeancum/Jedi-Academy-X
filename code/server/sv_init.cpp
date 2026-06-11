
// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"

#ifdef _XBOX
#include "../win32/xb_log.h"
extern "C" volatile unsigned int g_SPXBMapPhase;
extern "C" volatile unsigned int g_SPXBMapHash;
extern "C" volatile char g_SPXBMapLast[64];

static unsigned int SV_InitXboxHashText(const char *text)
{
	unsigned int h = 2166136261u;
	if (!text) {
		return 0;
	}
	while (*text) {
		h ^= (unsigned char)*text++;
		h *= 16777619u;
	}
	return h;
}

static void SV_InitXboxCopyLast(volatile char *dest, unsigned int destSize, const char *src)
{
	unsigned int i = 0;
	if (!dest || destSize == 0) {
		return;
	}
	if (src) {
		while (src[i] && i < destSize - 1) {
			dest[i] = src[i];
			++i;
		}
	}
	dest[i] = 0;
}
#endif

#include "../client/snd_music.h"	// didn't want to put this in snd_local because of rebuild times etc.
#include "server.h"
/*
Ghoul2 Insert Start
*/
#if !defined(TR_LOCAL_H)
	#include "../renderer/tr_local.h"
#endif

#if !defined (MINIHEAP_H_INC)
	#include "../qcommon/miniheap.h"
#endif

void CM_CleanLeafCache(void);
extern void SV_FreeClient(client_t*);

CMiniHeap *G2VertSpaceServer = NULL;
/*
Ghoul2 Insert End
*/


/*
===============
SV_SetConfigstring

===============
*/
void SV_SetConfigstring (int index, const char *val) {
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error (ERR_DROP, "SV_SetConfigstring: bad index %i\n", index);
	}

	if ( !val ) {
		val = "";
	}

	// don't bother broadcasting an update if no change
	if ( !strcmp( val, sv.configstrings[ index ] ) ) {
		return;
	}

	// change the string in sv
	Z_Free( sv.configstrings[index] );
	sv.configstrings[index] = CopyString( val );

	// send it to all the clients if we aren't
	// spawning a new server
	if ( sv.state == SS_GAME ) {
		SV_SendServerCommand( NULL, "cs %i \"%s\"\n", index, val );
	}
}



/*
===============
SV_GetConfigstring

===============
*/
void SV_GetConfigstring( int index, char *buffer, int bufferSize ) {
	if ( bufferSize < 1 ) {
		Com_Error( ERR_DROP, "SV_GetConfigstring: bufferSize == %i", bufferSize );
	}
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error (ERR_DROP, "SV_GetConfigstring: bad index %i\n", index);
	}
	if ( !sv.configstrings[index] ) {
		buffer[0] = 0;
		return;
	}

	Q_strncpyz( buffer, sv.configstrings[index], bufferSize );
}


/*
===============
SV_SetUserinfo

===============
*/
void SV_SetUserinfo( int index, const char *val ) {
	if ( index < 0 || index >= 1 ) {
		Com_Error (ERR_DROP, "SV_SetUserinfo: bad index %i\n", index);
	}

	if ( !val ) {
		val = "";
	}

	Q_strncpyz( svs.clients[ index ].userinfo, val, sizeof( svs.clients[ index ].userinfo ) );
}



/*
===============
SV_GetUserinfo

===============
*/
void SV_GetUserinfo( int index, char *buffer, int bufferSize ) {
	if ( bufferSize < 1 ) {
		Com_Error( ERR_DROP, "SV_GetUserinfo: bufferSize == %i", bufferSize );
	}
	if ( index < 0 || index >= 1 ) {
		Com_Error (ERR_DROP, "SV_GetUserinfo: bad index %i\n", index);
	}
	Q_strncpyz( buffer, svs.clients[ index ].userinfo, bufferSize );
}


/*
================
SV_CreateBaseline

Entity baselines are used to compress non-delta messages
to the clients -- only the fields that differ from the
baseline will be transmitted
================
*/
void SV_CreateBaseline( void ) {
	gentity_t			*svent;
	int				entnum;	

	for ( entnum = 0; entnum < ge->num_entities ; entnum++ ) {
		svent = SV_GentityNum(entnum);
		if (!svent->inuse) {
			continue;
		}
		if (!svent->linked) {
			continue;
		}
		svent->s.number = entnum;

		//
		// take current state as baseline
		//
		sv.svEntities[entnum].baseline = svent->s;
	}
}




/*
===============
SV_Startup

Called when a game is about to begin
===============
*/
void SV_Startup( void ) {
	if ( svs.initialized ) {
		Com_Error( ERR_FATAL, "SV_Startup: svs.initialized" );
	}

	svs.clients = (struct client_s *) Z_Malloc ( sizeof(client_t) * 1, TAG_CLIENTS, qtrue );
	svs.numSnapshotEntities = 2 * 4 * 64;
	svs.initialized = qtrue;

	Cvar_Set( "sv_running", "1" );
}


#ifdef _XBOX
//Xbox-only memory freeing.
extern void R_ModelFree(void);
extern void Sys_IORequestQueueClear(void);
extern void Music_Free(void);
extern void AS_FreePartial(void);
extern void G_ASPreCacheFree(void);
extern void Ghoul2InfoArray_Free(void);
extern void Ghoul2InfoArray_Reset(void);
extern void Menu_Reset(void);
extern void G2_FreeRag(void);
extern void ClearAllNavStructures(void);
extern void ClearModelsAlreadyDone(void);
extern void CL_FreeServerCommands(void);
extern void CL_FreeReliableCommands(void);
extern void CM_Free(void);
extern void ShaderEntryPtrs_Clear(void);
extern void G_FreeRoffs(void);
extern void BG_ClearVehicles(void);
extern void ClearHStringPool(void);
extern void ClearTheBonePool(void);
extern char cinematicSkipScript[64];
extern HANDLE s_BCThread;
extern void IN_HotSwap1Off(void);
extern void IN_HotSwap2Off(void);
extern void IN_HotSwap3Off(void);
extern int	cg_saberOnSoundTime[MAX_GENTITIES];
extern char current_speeders;
extern int zfFaceShaders[3];
extern int tfTorsoShader;
extern bool dontPillarPush;

void SV_ClearLastLevel(void)
{
	Menu_Reset();
	Z_TagFree(TAG_G_ALLOC);
	Z_TagFree(TAG_UI_ALLOC);
	G_FreeRoffs();
	R_ModelFree();
	Music_Free();
	Sys_IORequestQueueClear();
	AS_FreePartial();
	G_ASPreCacheFree();
#if !defined(STEFX_ELITE_FORCE_SP)
	Ghoul2InfoArray_Free();
	G2_FreeRag();
#endif
	ClearAllNavStructures();
	ClearModelsAlreadyDone();
	CL_FreeServerCommands();
	CL_FreeReliableCommands();
	CM_Free();
	ShaderEntryPtrs_Clear();
#if !defined(STEFX_ELITE_FORCE_SP)
	ClearTheBonePool();
#endif
	BG_ClearVehicles();

	cinematicSkipScript[0] = 0;

	if (svs.clients)
	{
		SV_FreeClient( svs.clients );
	}

	ClearHStringPool();

	// The bink copier thread is so trivial as to not have any communication
	// Rather than polling constantly to clean it up, we just check here.
	// This code should only happen ONCE:
	if (s_BCThread != INVALID_HANDLE_VALUE)
	{
		DWORD status;
		if (GetExitCodeThread( s_BCThread, &status ) && (status != STILL_ACTIVE))
		{
			// Thread is done. Clean up after ourselves:
			CloseHandle( s_BCThread );
			s_BCThread = INVALID_HANDLE_VALUE;
		}
	}

	IN_HotSwap1Off();
	IN_HotSwap2Off();
	IN_HotSwap3Off();

	memset(&cg_saberOnSoundTime, 0, MAX_GENTITIES);
	memset(zfFaceShaders, -1, sizeof(zfFaceShaders));
	tfTorsoShader = -1;

	current_speeders = 0;

	dontPillarPush = false;
}
#endif

qboolean CM_SameMap(char *server);
qboolean CM_HasTerrain(void);
void Cvar_Defrag(void);

// Load-time animation hackery:
struct OVERLAYINFO
{
	void *texture;
	void *surface;
};

OVERLAYINFO Image;
static int loadingX = 290;

void InitLoadingAnimation( void )
{
/*
	// Make our two textures:
	Image.texture = new IDirect3DTexture9;

	// Fill in the texture headers:
	DWORD pixelSize = 
	XGSetTextureHeader( 4,
						4,
						1,
						0,
						D3DFMT_YUY2,
						0,
						Image.texture,
						0,
						0 );

	// Get pixel data, texNum is unused:
	byte *pixels = (byte *)gTextures.Allocate( pixelSize, 0 );

	// texNum is unused:
	Image.texture->Register( pixels );

	// Turn on overlays:
	glw_state->device->EnableOverlay( TRUE );

	// Get surface pointers:
	Image.texture->GetSurfaceLevel( 0, &Image.surface );

	D3DLOCKED_RECT lock;
	Image.surface->LockRect( &lock, NULL, D3DLOCK_TILED );

	// Grey?
	memset( lock.pBits, 0x7f7f7f7f, lock.Pitch * 4 );

	Image.surface->UnlockRect();

	// Just to be safe:
//	loadingIndex = 0;
*/
}

void UpdateLoadingAnimation( void )
{
/*
	// Draw the image tiny, in the bottom of the screen:
	RECT dst_rect = { loadingX, 390, loadingX + 8, 398 };
	RECT src_rect = { 0, 0, 4, 4 };

	// Update this bugger.
	glw_state->device->UpdateOverlay( Image.surface, &src_rect, &dst_rect, FALSE, 0 );
	loadingX += 4;
	if (loadingX > 342 )
		loadingX = 290;
*/
}

void StopLoadingAnimation( void )
{
/*
	// Release surfaces:
	Image.surface->Release();
	Image.surface = NULL;

	// Clean up the textures we made for the overlay stuff:
	Image.texture->BlockUntilNotBusy();
	delete Image.texture;
	Image.texture = NULL;

	// Turn overlays back off:
	glw_state->device->EnableOverlay( FALSE );
*/
}

/*
================
SV_SpawnServer

Change the server to a new map, taking all connected
clients along with it.
================
*/
void SV_SpawnServer( char *iServer, ForceReload_e eForceReload, qboolean bAllowScreenDissolve )
{
	int			i;
	int			checksum;
	char		server[64];

	Q_strncpyz( server, iServer, sizeof(server), qtrue );
#ifdef _XBOX
	g_SPXBMapPhase = 10;
	g_SPXBMapHash = SV_InitXboxHashText(server);
	SV_InitXboxCopyLast(g_SPXBMapLast, sizeof(g_SPXBMapLast), server);
#endif
	XBLog_Write("JA: SV_SpawnServer entered:");
	XBLog_Write(server);

#ifdef XBOX_DEMO
	// Pause the timer if "someone is playing"
	extern void Demo_TimerPause( bool bPaused );
	Demo_TimerPause( true );
#endif

// The following fixes for potential issues only work on Xbox
#ifdef _XBOX
	extern qboolean stop_icarus;
	stop_icarus = qfalse;

	//Broken scripts may leave the player locked.  I think that's always bad.
	extern qboolean player_locked;
	player_locked = qfalse;

	//If you quit while in Matrix Mode, this never gets cleared!
	extern qboolean MatrixMode;
	MatrixMode = qfalse;

	// Failsafe to ensure that we don't have rumbling during level load
	extern void IN_KillRumbleScripts( void );
	IN_KillRumbleScripts();
#endif

	RE_RegisterMedia_LevelLoadBegin( server, eForceReload, bAllowScreenDissolve );


	Cvar_SetValue( "cl_paused", 0 );
	Cvar_Set( "timescale", "1" );//jic we were skipping

	// shut down the existing game if it is running
	SV_ShutdownGameProgs(qtrue);

	Com_Printf ("------ Server Initialization ------\n%s\n", com_version->string);
	Com_Printf ("Server: %s\n",server);	
	Cvar_Set( "ui_mapname", server );

#ifndef FINAL_BUILD
//	extern unsigned long texturePointMax;
//	Com_Printf ("Texture pool highwater mark: %u\n", texturePointMax);
#endif

#ifdef _XBOX
	g_SPXBMapPhase = 11;
	// disable vsync during load for speed
	glDisable(GL_VSYNC);
#endif

	// Hope this is correct - InitGame gets called later, which does this,
	// but UI_DrawConnect (in CL_MapLoading) needs it now, to properly
	// mimic CG_DrawInformation:
	extern SavedGameJustLoaded_e g_eSavedGameJustLoaded;
	g_eSavedGameJustLoaded = eSavedGameJustLoaded;

	// don't let sound stutter and dump all stuff on the hunk
	CL_MapLoading();

	if (!CM_SameMap(server))
	{ //rww - only clear if not loading the same map
		CM_ClearMap();
	}
#ifndef _XBOX
	else if (CM_HasTerrain())
	{ //always clear when going between maps with terrain
		CM_ClearMap();
	}
#endif

#if !defined(STEFX_ELITE_FORCE_SP)
	// Miniheap never changes sizes, so I just put it really early in mem.
	G2VertSpaceServer->ResetHeap();
#else
	XBLog_Write("STEFX: Ghoul2 server miniheap reset skipped");
#endif

#ifdef _XBOX
	// Deletes all textures
	R_DeleteTextures();
#endif
	Hunk_Clear();

	// Moved up from below to help reduce fragmentation
	if (svs.snapshotEntities)
	{
		Z_Free(svs.snapshotEntities);
		svs.snapshotEntities = NULL;
	}

	// wipe the entire per-level structure
	// Also moved up, trying to do all freeing before new allocs
	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( sv.configstrings[i] ) {
			Z_Free( sv.configstrings[i] );
			sv.configstrings[i] = NULL;
		}
	}

#ifdef _XBOX
	SV_ClearLastLevel();
#endif

	// Collect all the small allocations done by the cvar system
	// This frees, then allocates. Make it the last thing before other
	// allocations begin!
	Cvar_Defrag();

/*
		This is useful for debugging memory fragmentation.  Please don't
	   remove it.
*/
#ifdef _XBOX
	g_SPXBMapPhase = 12;
#if !defined(STEFX_ELITE_FORCE_SP)
	// We've over-freed the info array above, this puts it back into a working state
	Ghoul2InfoArray_Reset();
#else
	XBLog_Write("STEFX: Ghoul2 info array reset skipped");
#endif

	extern void Z_DumpMemMap_f(void);
	extern void Z_Details_f(void);
	extern void Z_TagPointers(memtag_t);
	Z_DumpMemMap_f();
//	Z_TagPointers(TAG_ALL);
	Z_Details_f();
#endif

	InitLoadingAnimation();
	UpdateLoadingAnimation();

	// init client structures and svs.numSnapshotEntities
	// This is moved down quite a bit, but should be safe. And keeps
	// svs.clients right at the beginning of memory
	if ( !Cvar_VariableIntegerValue("sv_running") ) {
		SV_Startup();
	}

 	// clear out those shaders, images and Models
//	R_InitImages();
//	R_InitShaders();
//	R_ModelInit();

	// allocate the snapshot entities 
	svs.snapshotEntities = (entityState_t *) Z_Malloc (sizeof(entityState_t)*svs.numSnapshotEntities, TAG_CLIENTS, qtrue );

	Music_SetLevelName(server);

	// toggle the server bit so clients can detect that a
	// server has changed
//!@	svs.snapFlagServerBit ^= SNAPFLAG_SERVERCOUNT;

	// set nextmap to the same map, but it may be overriden
	// by the game startup or another console command
	Cvar_Set( "nextmap", va("map %s", server) );


	memset (&sv, 0, sizeof(sv));


	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		sv.configstrings[i] = CopyString("");
	}

	sv.time = 1000;
#if !defined(STEFX_ELITE_FORCE_SP)
	G2API_SetTime(sv.time,G2T_SV_TIME);
#endif

#ifdef _XBOX
	g_SPXBMapPhase = 13;
	XBLog_Write("JA: CL_StartHunkUsers...");
	UpdateLoadingAnimation();
	CL_StartHunkUsers();
	UpdateLoadingAnimation();
	g_SPXBMapPhase = 14;
#if !defined(STEFX_ELITE_FORCE_SP)
	XBLog_Write("JA: SV_SpawnServer: precache humanoid GLA before BSP load...");
	{
		const qhandle_t normalHumanoid = RE_RegisterModel("models/players/_humanoid/_humanoid.gla");
		char cinematicHumanoid[MAX_QPATH];
		Com_sprintf(cinematicHumanoid, sizeof(cinematicHumanoid),
			"models/players/_humanoid_%s/_humanoid_%s.gla", server, server);
		const qhandle_t cinematicHumanoidHandle = RE_RegisterModel(cinematicHumanoid);
		XBLF("JA: SV_SpawnServer: humanoid GLA handles normal=%d cinematic=%d path='%s'",
			normalHumanoid, cinematicHumanoidHandle, cinematicHumanoid);
	}
#else
	XBLog_Write("STEFX: Ghoul2 humanoid GLA precache skipped");
#endif
	g_SPXBMapPhase = 15;
	XBLog_Write("JA: CM_LoadMap...");
	CM_LoadMap( va("maps/%s.bsp", server), qfalse, &checksum );
	g_SPXBMapPhase = 16;
	XBLog_Write("JA: CM_LoadMap done");
	UpdateLoadingAnimation();
	g_SPXBMapPhase = 17;
	XBLog_Write("JA: RE_LoadWorldMap...");
	RE_LoadWorldMap(va("maps/%s.bsp", server));
	g_SPXBMapPhase = 18;
	XBLog_Write("JA: RE_LoadWorldMap done");
	UpdateLoadingAnimation();
#else
	CM_LoadMap( va("maps/%s.bsp", server), qfalse, &checksum, qfalse );
#endif

	// set serverinfo visible name
	Cvar_Set( "mapname", server );

	Cvar_Set( "sv_mapChecksum", va("%i",checksum) );

	// serverid should be different each time
	sv.serverId = com_frameTime;
	Cvar_Set( "sv_serverid", va("%i", sv.serverId ) );

	// clear physics interaction links
	SV_ClearWorld ();

	// media configstring setting should be done during
	// the loading stage, so connected clients don't have
	// to load during actual gameplay
	sv.state = SS_LOADING;

	// load and spawn all other entities
#ifdef _XBOX
	g_SPXBMapPhase = 19;
#endif
	XBLog_Write("JA: SV_InitGameProgs...");
	SV_InitGameProgs();
#ifdef _XBOX
	g_SPXBMapPhase = 20;
#endif
	XBLog_Write("JA: SV_InitGameProgs done");

	// run a few frames to allow everything to settle
#ifdef _XBOX
	g_SPXBMapPhase = 21;
	XBLog_Write("JA: SV_SpawnServer before settle RunFrame loop");
#endif
	for ( i = 0 ;i < 3 ; i++ ) {
#ifdef _XBOX
		XBLF("JA: SV_SpawnServer settle RunFrame %d before time=%d", i, sv.time);
#endif
		ge->RunFrame( sv.time );
#ifdef _XBOX
		XBLF("JA: SV_SpawnServer settle RunFrame %d after time=%d", i, sv.time);
#endif
		sv.time += 100;
#if !defined(STEFX_ELITE_FORCE_SP)
		G2API_SetTime(sv.time,G2T_SV_TIME);
#endif
	}
#ifdef _XBOX
	g_SPXBMapPhase = 22;
	XBLog_Write("JA: SV_SpawnServer before ge->ConnectNavs");
#endif
	ge->ConnectNavs(sv_mapname->string, sv_mapChecksum->integer);
#ifdef _XBOX
	XBLog_Write("JA: SV_SpawnServer after ge->ConnectNavs");
#endif

	// create a baseline for more efficient communications
#ifdef _XBOX
	g_SPXBMapPhase = 23;
	XBLog_Write("JA: SV_SpawnServer before SV_CreateBaseline");
#endif
	SV_CreateBaseline ();
#ifdef _XBOX
	XBLog_Write("JA: SV_SpawnServer after SV_CreateBaseline");
#endif

	for (i=0 ; i<1 ; i++) {
		// clear all time counters, because we have reset sv.time
		svs.clients[i].lastPacketTime = 0;
		svs.clients[i].lastConnectTime = 0;
		svs.clients[i].nextSnapshotTime = 0;

		// send the new gamestate to all connected clients
		if (svs.clients[i].state >= CS_CONNECTED) {
			char	*denied;

			// connect the client again
#ifdef _XBOX
			XBLF("JA: SV_SpawnServer before ClientConnect client=%d state=%d", i, svs.clients[i].state);
#endif
			denied = ge->ClientConnect( i, qfalse, eNO/*qfalse*/ );	// firstTime = qfalse, qbFromSavedGame
#ifdef _XBOX
			XBLF("JA: SV_SpawnServer after ClientConnect client=%d denied=%s", i, denied ? denied : "(null)");
#endif
			if ( denied ) {
				// this generally shouldn't happen, because the client
				// was connected before the level change
				SV_DropClient( &svs.clients[i], denied );
			} else {
				svs.clients[i].state = CS_CONNECTED;
				// when we get the next packet from a connected client,
				// the new gamestate will be sent
			}
		}
	}	

	// run another frame to allow things to look at all connected clients
#ifdef _XBOX
	g_SPXBMapPhase = 24;
	XBLog_Write("JA: SV_SpawnServer before post-client RunFrame");
#endif
	ge->RunFrame( sv.time );
#ifdef _XBOX
	XBLog_Write("JA: SV_SpawnServer after post-client RunFrame");
#endif
	sv.time += 100;
#if !defined(STEFX_ELITE_FORCE_SP)
	G2API_SetTime(sv.time,G2T_SV_TIME);
#endif


	// save systeminfo and serverinfo strings
#ifdef _XBOX
	XBLog_Write("JA: SV_SpawnServer before CS_SYSTEMINFO");
#endif
	SV_SetConfigstring( CS_SYSTEMINFO, Cvar_InfoString( CVAR_SYSTEMINFO ) );
	cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;

#ifdef _XBOX
	XBLog_Write("JA: SV_SpawnServer before CS_SERVERINFO");
#endif
	SV_SetConfigstring( CS_SERVERINFO, Cvar_InfoString( CVAR_SERVERINFO ) );
	cvar_modifiedFlags &= ~CVAR_SERVERINFO;
#ifdef _XBOX
	XBLog_Write("JA: SV_SpawnServer after serverinfo configstrings");
#endif

	// any media configstring setting now should issue a warning
	// and any configstring changes should be reliably transmitted
	// to all clients
	sv.state = SS_GAME;
	
	// send a heartbeat now so the master will get up to date info
	svs.nextHeartbeatTime = -9999999;

	Hunk_SetMark();
#ifndef _XBOX
	Z_Validate();
	Z_Validate();
	Z_Validate();
#endif

	StopLoadingAnimation();
#ifdef _XBOX
	g_SPXBMapPhase = 25;
	XBLog_Write("JA: SV_SpawnServer complete");
#endif

	Com_Printf ("-----------------------------------\n");
}

/*
===============
SV_Init

Only called at main exe startup, not for each game
===============
*/
void SV_Init (void) {
	XBLog_Write("JA: SV_Init entered");
	SV_AddOperatorCommands ();

	// serverinfo vars
	Cvar_Get ("protocol", va("%i", PROTOCOL_VERSION), CVAR_SERVERINFO | CVAR_ROM);
	sv_mapname = Cvar_Get ("mapname", "nomap", CVAR_SERVERINFO | CVAR_ROM);

	// systeminfo
	Cvar_Get ("helpUsObi", "0", CVAR_SYSTEMINFO );
	sv_serverid = Cvar_Get ("sv_serverid", "0", CVAR_SYSTEMINFO | CVAR_ROM );

	// server vars
	sv_fps = Cvar_Get ("sv_fps", "20", CVAR_TEMP );
	sv_timeout = Cvar_Get ("sv_timeout", "120", CVAR_TEMP );
	sv_zombietime = Cvar_Get ("sv_zombietime", "2", CVAR_TEMP );
	Cvar_Get ("nextmap", "", CVAR_TEMP );
	sv_spawntarget = Cvar_Get ("spawntarget", "", 0 );

	sv_reconnectlimit = Cvar_Get ("sv_reconnectlimit", "3", 0);
	sv_showloss = Cvar_Get ("sv_showloss", "0", 0);
	sv_killserver = Cvar_Get ("sv_killserver", "0", 0);
	sv_mapChecksum = Cvar_Get ("sv_mapChecksum", "", CVAR_ROM);
	sv_testsave = Cvar_Get ("sv_testsave", "0", 0);
	sv_compress_saved_games = Cvar_Get ("sv_compress_saved_games", "1", 0);

#if defined(STEFX_ELITE_FORCE_SP)
	G2VertSpaceServer = NULL;
#ifdef _XBOX
	XBLog_Write("STEFX: Ghoul2 server miniheap disabled");
#endif
#else
	// Only allocated once, no point in moving it around and fragmenting
	// create a heap for Ghoul2 to use for game side model vertex transforms used in collision detection
	{
		static CMiniHeap singleton(132096);
		G2VertSpaceServer = &singleton;
	}
#endif
}


/*
==================
SV_FinalMessage

Used by SV_Shutdown to send a final message to all
connected clients before the server goes down.  The messages are sent immediately,
not just stuck on the outgoing message list, because the server is going
to totally exit after returning from this function.
==================
*/
void SV_FinalMessage( char *message ) {
	int			i, j;
	client_t	*cl;
	
	SV_SendServerCommand( NULL, "print \"%s\"", message );
	SV_SendServerCommand( NULL, "disconnect" );

	// send it twice, ignoring rate
	for ( j = 0 ; j < 2 ; j++ ) {
		for (i=0, cl = svs.clients ; i < 1 ; i++, cl++) {
			if (cl->state >= CS_CONNECTED) {
				// force a snapshot to be sent
				cl->nextSnapshotTime = -1;
				SV_SendClientSnapshot( cl );
			}
		}
	}
}


/*
================
SV_Shutdown

Called when each game quits,
before Sys_Quit or Sys_Error
================
*/
void SV_Shutdown( char *finalmsg ) {
	int i;

	if ( !com_sv_running || !com_sv_running->integer ) {
		return;
	}

	//Com_Printf( "----- Server Shutdown -----\n" );

	if ( svs.clients && !com_errorEntered ) {
		SV_FinalMessage( finalmsg );
	}

	SV_RemoveOperatorCommands();
	SV_ShutdownGameProgs(qfalse);

	if (svs.snapshotEntities)
	{
		Z_Free(svs.snapshotEntities);
		svs.snapshotEntities = NULL;
	}

	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( sv.configstrings[i] ) {
			Z_Free( sv.configstrings[i] );
		}
	}

	// free current level
	memset( &sv, 0, sizeof( sv ) );

	// free server static data
	if ( svs.clients ) {
		SV_FreeClient(svs.clients);
		Z_Free( svs.clients );
	}
	memset( &svs, 0, sizeof( svs ) );

	// Ensure we free any memory used by the leaf cache.
	CM_CleanLeafCache();

	Cvar_Set( "sv_running", "0" );

	//Com_Printf( "---------------------------\n" );
}
