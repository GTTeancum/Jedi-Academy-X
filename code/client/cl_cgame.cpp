// cl_cgame.c  -- client system interaction with client game

// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"

#ifdef _XBOX
#include "../win32/xb_log.h"
#endif

#include "../ui/ui_shared.h"

#include "../RMG/RM_Headers.h"

#ifdef VV_LIGHTING
#include "../renderer/tr_lightmanager.h"
#endif
	   		

#include "client.h"
#if defined(STEFX_ELITE_FORCE_SP)
#include "../qcommon/stefx_snapshot_abi.h"
#endif

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
static qboolean CL_STEFX_IsVisibleBrushMoverEntity( const entityState_t *state )
{
	if ( !state )
	{
		return qfalse;
	}
	return state->eType == ET_MOVER && state->solid == SOLID_BMODEL && !(state->eFlags & EF_NODRAW);
}
#endif

#ifdef _IMMERSION
#include "../ff/cl_ff.h"
#include "../ff/ff.h"
#else
#include "fffx.h"
#endif // _IMMERSION
#include "vmachine.h"

vm_t	cgvm;
/*
Ghoul2 Insert Start
*/

#if !defined(G2_H_INC)
	#include "../ghoul2/G2.h"
#endif

/*
Ghoul2 Insert End
*/

//FIXME: Temp
extern void S_UpdateAmbientSet ( const char *name, vec3_t origin );
extern int S_AddLocalSet( const char *name, vec3_t listener_origin, vec3_t origin, int entID, int time );
extern void AS_ParseSets( void );
extern sfxHandle_t AS_GetBModelSound( const char *name, int stage );
extern void	AS_AddPrecacheEntry( const char *name );
extern menuDef_t *Menus_FindByName(const char *p);

extern qboolean R_inPVS( vec3_t p1, vec3_t p2 );

void UI_SetActiveMenu( const char* menuname,const char *menuID );

/*
====================
CL_GetGameState
====================
*/
void CL_GetGameState( gameState_t *gs ) {
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	typedef struct stefxGameState_s {
		int			stringOffsets[1024];
		char		stringData[MAX_GAMESTATE_CHARS];
		int			dataCount;
	} stefxGameState_t;

	stefxGameState_t *efGs = (stefxGameState_t *)gs;
	memset( efGs, 0, sizeof(*efGs) );
	for (int i = 0; i < 1024 && i < MAX_CONFIGSTRINGS; i++)
	{
		efGs->stringOffsets[i] = cl.gameState.stringOffsets[i];
	}
	memcpy( efGs->stringData, cl.gameState.stringData, sizeof(efGs->stringData) );
	efGs->dataCount = cl.gameState.dataCount;
	XBLF("STEFX: CL_GetGameState marshalled EF layout dataCount=%d cs0=%d cs1=%d serverinfo='%s'",
		efGs->dataCount,
		efGs->stringOffsets[0],
		efGs->stringOffsets[1],
		efGs->stringData + efGs->stringOffsets[0]);
#else
	*gs = cl.gameState;
#endif
}

/*
====================
CL_GetGlconfig
====================
*/
void CL_GetGlconfig( glconfig_t *glconfig ) {
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	typedef struct stefxGlconfig_s {
		char		renderer_string[MAX_STRING_CHARS];
		char		vendor_string[MAX_STRING_CHARS];
		char		version_string[MAX_STRING_CHARS];
		char		extensions_string[2 * MAX_STRING_CHARS];
		int			maxTextureSize;
		int			maxActiveTextures;
		int			colorBits, depthBits, stencilBits;
		int			driverType;
		int			hardwareType;
		qboolean	deviceSupportsGamma;
		int			textureCompression;
		qboolean	textureEnvAddAvailable;
		qboolean	textureFilterAnisotropicAvailable;
		qboolean	clampToEdgeAvailable;
		int			vidWidth, vidHeight;
		float		windowAspect;
		int			displayFrequency;
		qboolean	isFullscreen;
		qboolean	stereoEnabled;
		qboolean	smpActive;
	} stefxGlconfig_t;

	stefxGlconfig_t *efGlconfig = (stefxGlconfig_t *)glconfig;
	memset( efGlconfig, 0, sizeof(*efGlconfig) );
	Q_strncpyz( efGlconfig->renderer_string, cls.glconfig.renderer_string ? cls.glconfig.renderer_string : "", sizeof(efGlconfig->renderer_string) );
	Q_strncpyz( efGlconfig->vendor_string, cls.glconfig.vendor_string ? cls.glconfig.vendor_string : "", sizeof(efGlconfig->vendor_string) );
	Q_strncpyz( efGlconfig->version_string, cls.glconfig.version_string ? cls.glconfig.version_string : "", sizeof(efGlconfig->version_string) );
	Q_strncpyz( efGlconfig->extensions_string, cls.glconfig.extensions_string ? cls.glconfig.extensions_string : "", sizeof(efGlconfig->extensions_string) );

	efGlconfig->maxTextureSize = cls.glconfig.maxTextureSize;
	efGlconfig->maxActiveTextures = cls.glconfig.maxActiveTextures > 0 ? cls.glconfig.maxActiveTextures : 1;
	efGlconfig->colorBits = cls.glconfig.colorBits;
	efGlconfig->depthBits = cls.glconfig.depthBits;
	efGlconfig->stencilBits = cls.glconfig.stencilBits;
	efGlconfig->driverType = 0;		// GLDRV_ICD
	efGlconfig->hardwareType = 0;	// GLHW_GENERIC
	efGlconfig->deviceSupportsGamma = cls.glconfig.deviceSupportsGamma;
	efGlconfig->textureCompression = cls.glconfig.textureCompression != TC_NONE ? 1 : 0;
	efGlconfig->textureEnvAddAvailable = cls.glconfig.textureEnvAddAvailable;
	efGlconfig->textureFilterAnisotropicAvailable = cls.glconfig.textureFilterAnisotropicAvailable;
	efGlconfig->clampToEdgeAvailable = cls.glconfig.clampToEdgeAvailable;
	efGlconfig->vidWidth = cls.glconfig.vidWidth > 0 ? cls.glconfig.vidWidth : 640;
	efGlconfig->vidHeight = cls.glconfig.vidHeight > 0 ? cls.glconfig.vidHeight : 480;
	efGlconfig->windowAspect = (float)efGlconfig->vidWidth / (float)efGlconfig->vidHeight;
	efGlconfig->displayFrequency = cls.glconfig.displayFrequency;
	efGlconfig->isFullscreen = cls.glconfig.isFullscreen;
	efGlconfig->stereoEnabled = cls.glconfig.stereoEnabled;
	efGlconfig->smpActive = qfalse;

	XBLF("STEFX: CL_GetGlconfig marshalled EF layout %dx%d aspect=%g maxTex=%d activeTex=%d renderer='%s'",
		efGlconfig->vidWidth,
		efGlconfig->vidHeight,
		efGlconfig->windowAspect,
		efGlconfig->maxTextureSize,
		efGlconfig->maxActiveTextures,
		efGlconfig->renderer_string);
#else
	*glconfig = cls.glconfig;
#endif
}


/*
====================
CL_GetUserCmd
====================
*/
qboolean CL_GetUserCmd( int cmdNumber, usercmd_t *ucmd ) {
	// cmds[cmdNumber] is the last properly generated command

	// can't return anything that we haven't created yet
	if ( cmdNumber > cl.cmdNumber ) {
		Com_Error( ERR_DROP, "CL_GetUserCmd: %i >= %i", cmdNumber, cl.cmdNumber );
	}

	// the usercmd has been overwritten in the wrapping
	// buffer because it is too far out of date
	if ( cmdNumber <= cl.cmdNumber - CMD_BACKUP ) {
		return qfalse;
	}

	*ucmd = cl.cmds[ cmdNumber & CMD_MASK ];

	return qtrue;
}

int CL_GetCurrentCmdNumber( void ) {
	return cl.cmdNumber;
}


/*
====================
CL_GetParseEntityState
====================
*/
/*
qboolean	CL_GetParseEntityState( int parseEntityNumber, entityState_t *state ) {
	// can't return anything that hasn't been parsed yet
	if ( parseEntityNumber >= cl.parseEntitiesNum ) {
		Com_Error( ERR_DROP, "CL_GetParseEntityState: %i >= %i",
			parseEntityNumber, cl.parseEntitiesNum );
	}

	// can't return anything that has been overwritten in the circular buffer
	if ( parseEntityNumber <= cl.parseEntitiesNum - MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	*state = cl.parseEntities[ parseEntityNumber & ( MAX_PARSE_ENTITIES - 1 ) ];
	return qtrue;
}
*/

/*
====================
CL_GetCurrentSnapshotNumber
====================
*/
void	CL_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime ) {
	*snapshotNumber = cl.frame.messageNum;
	*serverTime = cl.frame.serverTime;
}

/*
====================
CL_GetSnapshot
====================
*/
qboolean	CL_GetSnapshot( int snapshotNumber, snapshot_t *snapshot ) {
	clSnapshot_t	*clSnap;
	int				i, count;
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	static int s_stefxEngineSnapshotLayoutLogged = 0;
	static int s_stefxGetSnapshotLogBudget = 80;
	if ( !s_stefxEngineSnapshotLayoutLogged )
	{
		XBLF("STEFX: engine snapshot layout snapshot=%d ps=%d entity=%d numOfs=%d entitiesOfs=%d max=%d",
			(int)sizeof(snapshot_t),
			(int)sizeof(playerState_t),
			(int)sizeof(entityState_t),
			(int)((byte *)&(((snapshot_t *)0)->numEntities)),
			(int)((byte *)&(((snapshot_t *)0)->entities)),
			MAX_ENTITIES_IN_SNAPSHOT);
		s_stefxEngineSnapshotLayoutLogged = 1;
	}
#endif

	if ( snapshotNumber > cl.frame.messageNum ) {
		Com_Error( ERR_DROP, "CL_GetSnapshot: snapshotNumber > cl.frame.messageNum" );
	}

	// if the frame has fallen out of the circular buffer, we can't return it
	if ( cl.frame.messageNum - snapshotNumber >= PACKET_BACKUP ) {
		return qfalse;
	}

	// if the frame is not valid, we can't return it
	clSnap = &cl.frames[snapshotNumber & PACKET_MASK];
	if ( !clSnap->valid ) {
		return qfalse;
	}

	// if the entities in the frame have fallen out of their
	// circular buffer, we can't return it
	if ( cl.parseEntitiesNum - clSnap->parseEntitiesNum >= MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	// write the snapshot
	snapshot->snapFlags = clSnap->snapFlags;
	snapshot->serverCommandSequence = clSnap->serverCommandNum;
	snapshot->ping = clSnap->ping;
	snapshot->serverTime = clSnap->serverTime;
	memcpy( snapshot->areamask, clSnap->areamask, sizeof( snapshot->areamask ) );
	snapshot->cmdNum = clSnap->cmdNum;
	snapshot->ps = clSnap->ps;
	count = clSnap->numEntities;
	if ( count > MAX_ENTITIES_IN_SNAPSHOT ) {
		Com_DPrintf( "CL_GetSnapshot: truncated %i entities to %i\n", count, MAX_ENTITIES_IN_SNAPSHOT );
		count = MAX_ENTITIES_IN_SNAPSHOT;
	}
	snapshot->numEntities = count;
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if ( s_stefxGetSnapshotLogBudget > 0 )
	{
		XBLF("STEFX: engine CL_GetSnapshot num=%d clMsg=%d clSnapValid=%d clSnapEntities=%d copyCount=%d parseBase=%d parseNow=%d",
			snapshotNumber,
			cl.frame.messageNum,
			(int)clSnap->valid,
			clSnap->numEntities,
			count,
			clSnap->parseEntitiesNum,
			cl.parseEntitiesNum);
		--s_stefxGetSnapshotLogBudget;
	}
#endif
/*
Ghoul2 Insert Start
*/
 	for ( i = 0 ; i < count ; i++ ) 
	{

		int entNum =  ( clSnap->parseEntitiesNum + i ) & (MAX_PARSE_ENTITIES-1) ;		
		snapshot->entities[i] = cl.parseEntities[ entNum ];
	}
/*
Ghoul2 Insert End
*/


	// FIXME: configstring changes and server commands!!!

	return qtrue;
}

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
qboolean CL_STEFX_GetSnapshot( int snapshotNumber, void *snapshotBuffer ) {
	clSnapshot_t	*clSnap;
	stefxSnapshot_t	*snapshot;
	int				i, count;
	static int		s_stefxLayoutLogged = 0;
	static int		s_stefxGetSnapshotLogBudget = 80;

	if ( snapshotNumber > cl.frame.messageNum ) {
		Com_Error( ERR_DROP, "CL_STEFX_GetSnapshot: snapshotNumber > cl.frame.messageNum" );
	}

	if ( cl.frame.messageNum - snapshotNumber >= PACKET_BACKUP ) {
		return qfalse;
	}

	clSnap = &cl.frames[snapshotNumber & PACKET_MASK];
	if ( !clSnap->valid ) {
		return qfalse;
	}

	if ( cl.parseEntitiesNum - clSnap->parseEntitiesNum >= MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	snapshot = (stefxSnapshot_t *)snapshotBuffer;
	memset( snapshot, 0, sizeof( *snapshot ) );

	if ( !s_stefxLayoutLogged )
	{
		XBLF("STEFX: engine EF snapshot adapter layout snapshot=%d ps=%d entity=%d numOfs=%d entitiesOfs=%d max=%d",
			(int)sizeof(stefxSnapshot_t),
			(int)sizeof(stefxPlayerState_t),
			(int)sizeof(entityState_t),
			(int)((byte *)&(((stefxSnapshot_t *)0)->numEntities)),
			(int)((byte *)&(((stefxSnapshot_t *)0)->entities)),
			STEFX_MAX_ENTITIES_IN_SNAPSHOT);
		s_stefxLayoutLogged = 1;
	}

	snapshot->snapFlags = clSnap->snapFlags;
	snapshot->ping = clSnap->ping;
	snapshot->serverTime = clSnap->serverTime;
	memcpy( snapshot->areamask, clSnap->areamask, sizeof( snapshot->areamask ) );
	snapshot->cmdNum = clSnap->cmdNum;
	STEFX_CopyJaPlayerStateToEf( &snapshot->ps, &clSnap->ps );

	count = clSnap->numEntities;
	if ( count > STEFX_MAX_ENTITIES_IN_SNAPSHOT ) {
		Com_DPrintf( "CL_STEFX_GetSnapshot: truncated %i entities to %i\n", count, STEFX_MAX_ENTITIES_IN_SNAPSHOT );
		count = STEFX_MAX_ENTITIES_IN_SNAPSHOT;
	}
	snapshot->numEntities = count;
	snapshot->serverCommandSequence = clSnap->serverCommandNum;

	if ( s_stefxGetSnapshotLogBudget > 0 )
	{
		XBLF("STEFX: engine EF CL_GetSnapshot num=%d clMsg=%d valid=%d clSnapEntities=%d copyCount=%d parseBase=%d parseNow=%d psWeapon=%d psHealth=%d",
			snapshotNumber,
			cl.frame.messageNum,
			(int)clSnap->valid,
			clSnap->numEntities,
			count,
			clSnap->parseEntitiesNum,
			cl.parseEntitiesNum,
			snapshot->ps.weapon,
			snapshot->ps.stats[STAT_HEALTH]);
		--s_stefxGetSnapshotLogBudget;
	}

	for ( i = 0 ; i < count ; i++ )
	{
		int entNum = ( clSnap->parseEntitiesNum + i ) & (MAX_PARSE_ENTITIES-1);
		snapshot->entities[i] = cl.parseEntities[ entNum ];
		if ( snapshot->entities[i].eType == ET_PLAYER )
		{
			static int s_stefxGetSnapshotActorBudget = 256;
			if ( s_stefxGetSnapshotActorBudget > 0 )
			{
				XBLF("STEFX: engine EF CL_GetSnapshot actor ent=%d clientNum=%d eType=%d eFlags=0x%x weapon=%d model=%d model2=%d outIndex=%d parseIndex=%d origin=(%g,%g,%g) current=(%g,%g,%g)",
					snapshot->entities[i].number,
					snapshot->entities[i].clientNum,
					snapshot->entities[i].eType,
					snapshot->entities[i].eFlags,
					snapshot->entities[i].weapon,
					snapshot->entities[i].modelindex,
					snapshot->entities[i].modelindex2,
					i,
					entNum,
					snapshot->entities[i].pos.trBase[0],
					snapshot->entities[i].pos.trBase[1],
					snapshot->entities[i].pos.trBase[2],
					snapshot->entities[i].origin[0],
					snapshot->entities[i].origin[1],
					snapshot->entities[i].origin[2]);
				--s_stefxGetSnapshotActorBudget;
			}
		}
		if ( CL_STEFX_IsVisibleBrushMoverEntity( &snapshot->entities[i] ) )
		{
			static int s_stefxGetSnapshotBModelBudget = 160;
			if ( s_stefxGetSnapshotBModelBudget > 0 )
			{
				XBLF("STEFX: engine EF CL_GetSnapshot bmodel ent=%d eType=%d model=%d model2=%d solid=0x%x eFlags=0x%x outIndex=%d parseIndex=%d pos=(%g,%g,%g) apos=(%g,%g,%g)",
					snapshot->entities[i].number,
					snapshot->entities[i].eType,
					snapshot->entities[i].modelindex,
					snapshot->entities[i].modelindex2,
					snapshot->entities[i].solid,
					snapshot->entities[i].eFlags,
					i,
					entNum,
					snapshot->entities[i].pos.trBase[0],
					snapshot->entities[i].pos.trBase[1],
					snapshot->entities[i].pos.trBase[2],
					snapshot->entities[i].apos.trBase[0],
					snapshot->entities[i].apos.trBase[1],
					snapshot->entities[i].apos.trBase[2]);
				--s_stefxGetSnapshotBModelBudget;
			}
		}
		if ( snapshot->entities[i].eType > ET_EVENTS )
		{
			static int s_stefxGetSnapshotEventBudget = 128;
			if ( s_stefxGetSnapshotEventBudget > 0 )
			{
				XBLF("STEFX: engine EF CL_GetSnapshot event ent=%d eType=%d event=%d weapon=%d outIndex=%d parseIndex=%d origin=(%g,%g,%g) origin2=(%g,%g,%g)",
					snapshot->entities[i].number,
					snapshot->entities[i].eType,
					snapshot->entities[i].eType - ET_EVENTS,
					snapshot->entities[i].weapon,
					i,
					entNum,
					snapshot->entities[i].pos.trBase[0],
					snapshot->entities[i].pos.trBase[1],
					snapshot->entities[i].pos.trBase[2],
					snapshot->entities[i].origin2[0],
					snapshot->entities[i].origin2[1],
					snapshot->entities[i].origin2[2]);
				--s_stefxGetSnapshotEventBudget;
			}
		}
	}

	return qtrue;
}
#endif

//bg_public.h won't cooperate in here
#define EF_PERMANENT   0x00080000

qboolean CL_GetDefaultState(int index, entityState_t *state)
{
	if (index < 0 || index >= MAX_GENTITIES)
	{
		return qfalse;
	}

	// Is this safe? I think so. But it's still ugly as sin.
	if (!(sv.svEntities[index].baseline.eFlags & EF_PERMANENT))
//	if (!(cl.entityBaselines[index].eFlags & EF_PERMANENT))
	{
		return qfalse;
	}

	*state = sv.svEntities[index].baseline;
//	*state = cl.entityBaselines[index];

	return qtrue;
}

extern float cl_mPitchOverride;
extern float cl_mYawOverride;
void CL_SetUserCmdValue( int userCmdValue, float sensitivityScale, float mPitchOverride, float mYawOverride ) {
	cl.cgameUserCmdValue = userCmdValue;
	cl.cgameSensitivity = sensitivityScale;
	cl_mPitchOverride = mPitchOverride;
	cl_mYawOverride = mYawOverride;
}

extern vec3_t cl_overriddenAngles;
extern qboolean cl_overrideAngles;
void CL_SetUserCmdAngles( float pitchOverride, float yawOverride, float rollOverride ) {
	cl_overriddenAngles[PITCH] = pitchOverride;
	cl_overriddenAngles[YAW] = yawOverride;
	cl_overriddenAngles[ROLL] = rollOverride;
	cl_overrideAngles = qtrue;
}

void CL_AddCgameCommand( const char *cmdName ) {
	Cmd_AddCommand( cmdName, NULL );
}

void CL_CgameError( const char *string ) {
	Com_Error( ERR_DROP, "%s", string );
}


/*
=====================
CL_ConfigstringModified
=====================
*/
void CL_ConfigstringModified( void ) {
	char		*old, *s;
	int			i, index;
	char		*dup;
	gameState_t	oldGs;
	int			len;

	index = atoi( Cmd_Argv(1) );
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "configstring > MAX_CONFIGSTRINGS" );
	}
	s = Cmd_Argv(2);

	old = cl.gameState.stringData + cl.gameState.stringOffsets[ index ];
	if ( !strcmp( old, s ) ) {
		return;		// unchanged
	}

	// build the new gameState_t
	oldGs = cl.gameState;

	memset( &cl.gameState, 0, sizeof( cl.gameState ) );

	// leave the first 0 for uninitialized strings
	cl.gameState.dataCount = 1;
		
	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( i == index ) {
			dup = s;
		} else {
			dup = oldGs.stringData + oldGs.stringOffsets[ i ];
		}
		if ( !dup[0] ) {
			continue;		// leave with the default empty string
		}

		len = strlen( dup );

		if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
			Com_Error( ERR_DROP, "MAX_GAMESTATE_CHARS exceeded" );
		}

		// append it to the gameState string buffer
		cl.gameState.stringOffsets[ i ] = cl.gameState.dataCount;
		memcpy( cl.gameState.stringData + cl.gameState.dataCount, dup, len + 1 );
		cl.gameState.dataCount += len + 1;
	}

	if ( index == CS_SYSTEMINFO ) {
		// parse serverId and other cvars
		CL_SystemInfoChanged();
	}

}


/*
===================
CL_GetServerCommand

Set up argc/argv for the given command
===================
*/
qboolean CL_GetServerCommand( int serverCommandNumber ) {
	char	*s;
	char	*cmd;

	// if we have irretrievably lost a reliable command, drop the connection
	if ( serverCommandNumber <= clc.serverCommandSequence - MAX_RELIABLE_COMMANDS ) {
		Com_Error( ERR_DROP, "CL_GetServerCommand: a reliable command was cycled out" );
		return qfalse;
	}

	if ( serverCommandNumber > clc.serverCommandSequence ) {
		Com_Error( ERR_DROP, "CL_GetServerCommand: requested a command not received" );
		return qfalse;
	}

	s = clc.serverCommands[ serverCommandNumber & ( MAX_RELIABLE_COMMANDS - 1 ) ];

	Com_DPrintf( "serverCommand: %i : %s\n", serverCommandNumber, s );

	Cmd_TokenizeString( s );
	cmd = Cmd_Argv(0);

	if ( !strcmp( cmd, "disconnect" ) ) {
		Com_Error (ERR_DISCONNECT,"Server disconnected\n");
	}

	if ( !strcmp( cmd, "cs" ) ) {
		CL_ConfigstringModified();
		// reparse the string, because CL_ConfigstringModified may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		return qtrue;
	}

	// the clientLevelShot command is used during development
	// to generate 128*128 screenshots from the intermission
	// point of levels for the menu system to use
	// we pass it along to the cgame to make apropriate adjustments,
	// but we also clear the console and notify lines here
	if ( !strcmp( cmd, "clientLevelShot" ) ) {
		// don't do it if we aren't running the server locally,
		// otherwise malicious remote servers could overwrite
		// the existing thumbnails
		if ( !com_sv_running->integer ) {
			return qfalse;
		}
		// close the console
		Con_Close();
		// take a special screenshot next frame
		Cbuf_AddText( "wait ; wait ; wait ; wait ; screenshot levelshot\n" );
		return qtrue;
	}

	// we may want to put a "connect to other server" command here

	// cgame can now act on the command
	return qtrue;
}


/*
====================
CL_CM_LoadMap

Just adds default parameters that cgame doesn't need to know about
====================
*/
#ifdef _XBOX
void CL_CM_LoadMap( const char *mapname ) {
	int		checksum;

	CM_LoadMap( mapname, qtrue, &checksum );
}
#else
void CL_CM_LoadMap( const char *mapname, qboolean subBSP ) {
	int		checksum;

	CM_LoadMap( mapname, qtrue, &checksum, subBSP );
}
#endif // _XBOX

/*
====================
CL_ShutdonwCGame

====================
*/
void CL_ShutdownCGame( void ) {
	cls.cgameStarted = qfalse;

	if ( !cgvm.entryPoint) {
		return;
	}
	VM_Call( CG_SHUTDOWN );
#ifndef _XBOX	// Not using it
	RM_ShutdownTerrain();
#endif

//	VM_Free( cgvm );
//	cgvm = NULL;
}

//RMG
CCMLandScape *CM_RegisterTerrain(const char *config, bool server);
void RE_InitRendererTerrain( const char *info );
//RMG

extern float tr_distortionAlpha; //tr_shadows.cpp
extern float tr_distortionStretch; //tr_shadows.cpp
extern qboolean tr_distortionPrePost; //tr_shadows.cpp
extern qboolean tr_distortionNegate; //tr_shadows.cpp

float g_oldRangedFog = 0.0f;
/*
====================
CL_CgameSystemCalls

The cgame module is making a system call
====================
*/
void *VM_ArgPtr( int intValue );
void CM_SnapPVS(vec3_t origin,byte *buffer);
#ifdef _XBOX
extern bool Sys_IsDirectMapBoot(void);
#endif
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
static void CL_STEFX_DrawDirectMapLoadScreen( const char *source )
{
	static int s_directMapLoadScreenBudget = 16;
	extern void SP_DrawSPLoadScreen( void );

	if ( s_directMapLoadScreenBudget > 0 )
	{
		XBLF("STEFX: %s presenting EF loadscreen during direct-map boot state=%d realtime=%d",
			source ? source : "CG_UPDATESCREEN", (int)cls.state, cls.realtime);
		--s_directMapLoadScreenBudget;
	}

	XBLog_Write("STEFX: direct-map loadscreen before SP_DrawSPLoadScreen");
	SP_DrawSPLoadScreen();
	XBLog_Write("STEFX: direct-map loadscreen after SP_DrawSPLoadScreen before EndFrame");
	re.EndFrame( NULL, NULL );
	XBLog_Write("STEFX: direct-map loadscreen EndFrame done");
}
#endif
//#define	VMA(x) VM_ArgPtr(args[x])
#define	VMA(x) ((void*)args[x])
#define	VMF(x)	((float *)args)[x]

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
enum stefxCgImport_t
{
	STEFX_CG_PRINT,
	STEFX_CG_ERROR,
	STEFX_CG_MILLISECONDS,
	STEFX_CG_CVAR_REGISTER,
	STEFX_CG_CVAR_UPDATE,
	STEFX_CG_CVAR_SET,
	STEFX_CG_ARGC,
	STEFX_CG_ARGV,
	STEFX_CG_ARGS,
	STEFX_CG_FS_FOPENFILE,
	STEFX_CG_FS_READ,
	STEFX_CG_FS_WRITE,
	STEFX_CG_FS_FCLOSEFILE,
	STEFX_CG_SENDCONSOLECOMMAND,
	STEFX_CG_ADDCOMMAND,
	STEFX_CG_SENDCLIENTCOMMAND,
	STEFX_CG_UPDATESCREEN,
	STEFX_CG_CM_LOADMAP,
	STEFX_CG_CM_NUMINLINEMODELS,
	STEFX_CG_CM_INLINEMODEL,
	STEFX_CG_CM_TEMPBOXMODEL,
	STEFX_CG_CM_POINTCONTENTS,
	STEFX_CG_CM_TRANSFORMEDPOINTCONTENTS,
	STEFX_CG_CM_BOXTRACE,
	STEFX_CG_CM_TRANSFORMEDBOXTRACE,
	STEFX_CG_CM_MARKFRAGMENTS,
	STEFX_CG_S_STARTSOUND,
	STEFX_CG_S_STARTLOCALSOUND,
	STEFX_CG_S_CLEARLOOPINGSOUNDS,
	STEFX_CG_S_ADDLOOPINGSOUND,
	STEFX_CG_S_UPDATEENTITYPOSITION,
	STEFX_CG_S_RESPATIALIZE,
	STEFX_CG_S_REGISTERSOUND,
	STEFX_CG_S_STARTBACKGROUNDTRACK,
	STEFX_CG_FF_STARTFX,
	STEFX_CG_FF_ENSUREFX,
	STEFX_CG_FF_STOPFX,
	STEFX_CG_FF_STOPALLFX,
	STEFX_CG_R_LOADWORLDMAP,
	STEFX_CG_R_REGISTERMODEL,
	STEFX_CG_R_REGISTERSKIN,
	STEFX_CG_R_REGISTERSHADER,
	STEFX_CG_R_REGISTERSHADERNOMIP,
	STEFX_CG_R_CLEARSCENE,
	STEFX_CG_R_ADDREFENTITYTOSCENE,
	STEFX_CG_R_GETLIGHTING,
	STEFX_CG_R_ADDPOLYTOSCENE,
	STEFX_CG_R_ADDLIGHTTOSCENE,
	STEFX_CG_R_RENDERSCENE,
	STEFX_CG_R_SETCOLOR,
	STEFX_CG_R_DRAWSTRETCHPIC,
	STEFX_CG_R_DRAWSCREENSHOT,
	STEFX_CG_R_MODELBOUNDS,
	STEFX_CG_R_LERPTAG,
	STEFX_CG_R_DRAWROTATEPIC,
	STEFX_CG_R_SCISSOR,
	STEFX_CG_GETGLCONFIG,
	STEFX_CG_GETGAMESTATE,
	STEFX_CG_GETCURRENTSNAPSHOTNUMBER,
	STEFX_CG_GETSNAPSHOT,
	STEFX_CG_GETSERVERCOMMAND,
	STEFX_CG_GETCURRENTCMDNUMBER,
	STEFX_CG_GETUSERCMD,
	STEFX_CG_SETUSERCMDVALUE,
	STEFX_CG_MEMORY_REMAINING,
	STEFX_CG_S_UPDATEAMBIENTSET,
	STEFX_CG_S_ADDLOCALSET,
	STEFX_CG_AS_PARSESETS,
	STEFX_CG_AS_ADDENTRY,
	STEFX_CG_AS_GETBMODELSOUND,
	STEFX_CG_S_GETSAMPLELENGTH
};

typedef struct stefxRefdef_s {
	int			x, y, width, height;
	float		fov_x, fov_y;
	vec3_t		vieworg;
	vec3_t		viewaxis[3];
	int			time;
	int			rdflags;
	byte		areamask[MAX_MAP_AREA_BYTES];
} stefxRefdef_t;

static void CL_STEFX_CopyRefdef( refdef_t *out, const stefxRefdef_t *in )
{
	memset( out, 0, sizeof( *out ) );
	if ( !in )
	{
		return;
	}

	out->x = in->x;
	out->y = in->y;
	out->width = in->width;
	out->height = in->height;
	out->fov_x = in->fov_x;
	out->fov_y = in->fov_y;
	VectorCopy( in->vieworg, out->vieworg );
	VectorCopy( in->viewaxis[0], out->viewaxis[0] );
	VectorCopy( in->viewaxis[1], out->viewaxis[1] );
	VectorCopy( in->viewaxis[2], out->viewaxis[2] );
	out->viewContents = 0;
	out->time = in->time;
	out->rdflags = in->rdflags;
	memcpy( out->areamask, in->areamask, sizeof( out->areamask ) );
}

static clipHandle_t CL_SafeCgameInlineModel( const char *tag, int index )
{
	int count = CM_NumInlineModels();
	if ( index < 0 || index >= count )
	{
		XBLF("STEFX: %s CM_InlineModel rejected index=%d count=%d; returning world", tag, index, count);
		return 0;
	}

	return CM_InlineModel( index );
}

static int CL_STEFX_CgameSystemCalls( int *args )
{
	static int s_syscallLogCount = 0;
	if (s_syscallLogCount < 96)
	{
		XBLF("STEFX: EF cgame syscall #%d trap=%d a1=%08x a2=%08x a3=%08x",
			s_syscallLogCount, args[0], args[1], args[2], args[3]);
	}
	s_syscallLogCount++;

	switch ( args[0] )
	{
	case STEFX_CG_PRINT:
		Com_Printf( "%s", VMA(1) );
		return 0;
	case STEFX_CG_ERROR:
		Com_Error( ERR_DROP, S_COLOR_RED"%s", VMA(1) );
		return 0;
	case STEFX_CG_MILLISECONDS:
		return Sys_Milliseconds();
	case STEFX_CG_CVAR_REGISTER:
		Cvar_Register( (vmCvar_t *) VMA(1), (const char *) VMA(2), (const char *) VMA(3), args[4] );
		return 0;
	case STEFX_CG_CVAR_UPDATE:
		Cvar_Update( (vmCvar_t *) VMA(1) );
		return 0;
	case STEFX_CG_CVAR_SET:
		Cvar_Set( (const char *) VMA(1), (const char *) VMA(2) );
		return 0;
	case STEFX_CG_ARGC:
		return Cmd_Argc();
	case STEFX_CG_ARGV:
		Cmd_ArgvBuffer( args[1], (char *) VMA(2), args[3] );
		return 0;
	case STEFX_CG_ARGS:
		Cmd_ArgsBuffer( (char *) VMA(1), args[2] );
		return 0;
	case STEFX_CG_FS_FOPENFILE:
		return FS_FOpenFileByMode( (const char *) VMA(1), (int *) VMA(2), (fsMode_t) args[3] );
	case STEFX_CG_FS_READ:
		FS_Read( VMA(1), args[2], args[3] );
		return 0;
	case STEFX_CG_FS_WRITE:
		FS_Write( VMA(1), args[2], args[3] );
		return 0;
	case STEFX_CG_FS_FCLOSEFILE:
		FS_FCloseFile( args[1] );
		return 0;
	case STEFX_CG_SENDCONSOLECOMMAND:
		Cbuf_AddText( (const char *) VMA(1) );
		return 0;
	case STEFX_CG_ADDCOMMAND:
		CL_AddCgameCommand( (const char *) VMA(1) );
		return 0;
	case STEFX_CG_SENDCLIENTCOMMAND:
		CL_AddReliableCommand( (const char *) VMA(1) );
		return 0;
	case STEFX_CG_UPDATESCREEN:
		Com_EventLoop();
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		if ( Sys_IsDirectMapBoot() )
		{
			CL_STEFX_DrawDirectMapLoadScreen( "STEFX_CG_UPDATESCREEN" );
			return 0;
		}
#endif
		SCR_UpdateScreen();
		return 0;
	case STEFX_CG_CM_LOADMAP:
		CL_CM_LoadMap( (const char *) VMA(1) );
		return 0;
	case STEFX_CG_CM_NUMINLINEMODELS:
		return CM_NumInlineModels();
	case STEFX_CG_CM_INLINEMODEL:
		return CL_SafeCgameInlineModel( "EF", args[1] );
	case STEFX_CG_CM_TEMPBOXMODEL:
		return CM_TempBoxModel( (const float *) VMA(1), (const float *) VMA(2) );
	case STEFX_CG_CM_POINTCONTENTS:
		return CM_PointContents( (float *) VMA(1), args[2] );
	case STEFX_CG_CM_TRANSFORMEDPOINTCONTENTS:
		return CM_TransformedPointContents( (const float *) VMA(1), args[2], (const float *) VMA(3), (const float *) VMA(4) );
	case STEFX_CG_CM_BOXTRACE:
		CM_BoxTrace( (trace_t *) VMA(1), (const float *) VMA(2), (const float *) VMA(3), (const float *) VMA(4), (const float *) VMA(5), args[6], args[7] );
		return 0;
	case STEFX_CG_CM_TRANSFORMEDBOXTRACE:
		CM_TransformedBoxTrace( (trace_t *) VMA(1), (const float *) VMA(2), (const float *) VMA(3), (const float *) VMA(4), (const float *) VMA(5), args[6], args[7], (const float *) VMA(8), (const float *) VMA(9) );
		return 0;
	case STEFX_CG_CM_MARKFRAGMENTS:
		return re.MarkFragments( args[1], (float(*)[3]) VMA(2), (const float *) VMA(3), args[4], (float *) VMA(5), args[6], (markFragment_t *) VMA(7) );
	case STEFX_CG_S_STARTSOUND:
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		if (!cls.cgameStarted)
		{
			XBLF("STEFX: EF S_STARTSOUND bridge early started=%d ent=%d chan=%d handle=%d origin=%08x",
				cls.cgameStarted ? 1 : 0,
				args[2],
				args[3],
				args[4],
				args[1]);
		}
#endif
		S_StartSound( (float *) VMA(1), args[2], (soundChannel_t)args[3], args[4] );
		return 0;
	case STEFX_CG_S_STARTLOCALSOUND:
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		if (!cls.cgameStarted)
		{
			XBLF("STEFX: EF S_STARTLOCALSOUND bridge early started=%d handle=%d chan=%d",
				cls.cgameStarted ? 1 : 0,
				args[1],
				args[2]);
		}
#endif
		S_StartLocalSound( args[1], args[2] );
		return 0;
	case STEFX_CG_S_CLEARLOOPINGSOUNDS:
		S_ClearLoopingSounds();
		return 0;
	case STEFX_CG_S_ADDLOOPINGSOUND:
		if (!cls.cgameStarted)
		{
			return 0;
		}
		S_AddLoopingSound( args[1], (const float *) VMA(2), (const float *) VMA(3), args[4], (soundChannel_t)args[5] );
		return 0;
	case STEFX_CG_S_UPDATEENTITYPOSITION:
		S_UpdateEntityPosition( args[1], (const float *) VMA(2) );
		return 0;
	case STEFX_CG_S_RESPATIALIZE:
		S_Respatialize( args[1], (const float *) VMA(2), (float(*)[3]) VMA(3), args[4] );
		return 0;
	case STEFX_CG_S_REGISTERSOUND:
		return S_RegisterSound( (const char *) VMA(1) );
	case STEFX_CG_S_STARTBACKGROUNDTRACK:
#ifdef _XBOX
		XBLog_Writef("STEFX: cgame background music syscall stefx intro='%s' loop='%s'",
			(const char *) VMA(1),
			(const char *) VMA(2));
#endif
		S_StartBackgroundTrack( (const char *) VMA(1), (const char *) VMA(2), qfalse );
		return 0;
	case STEFX_CG_FF_STARTFX:
		FFFX_START( (ffFX_e) args[1] );
		return 0;
	case STEFX_CG_FF_ENSUREFX:
		FFFX_ENSURE( (ffFX_e) args[1] );
		return 0;
	case STEFX_CG_FF_STOPFX:
		FFFX_STOP( (ffFX_e) args[1] );
		return 0;
	case STEFX_CG_FF_STOPALLFX:
		FFFX_STOPALL;
		return 0;
	case STEFX_CG_R_LOADWORLDMAP:
		re.LoadWorld( (const char *) VMA(1) );
		return 0;
	case STEFX_CG_R_REGISTERMODEL:
		{
			const char *modelName = (const char *) VMA(1);
			qhandle_t modelHandle = re.RegisterModel( modelName );
			if (modelName && (strstr(modelName, ".mdr") || strstr(modelName, "models/players/")))
			{
				XBLF("STEFX: EF cgame R_RegisterModel '%s' -> %d", modelName, modelHandle);
			}
			return modelHandle;
		}
	case STEFX_CG_R_REGISTERSKIN:
		return re.RegisterSkin( (const char *) VMA(1) );
	case STEFX_CG_R_REGISTERSHADER:
		{
			const char *shaderName = (const char *) VMA(1);
			return re.RegisterShader( shaderName );
		}
	case STEFX_CG_R_REGISTERSHADERNOMIP:
		{
			const char *shaderName = (const char *) VMA(1);
			return re.RegisterShaderNoMip( shaderName );
		}
	case STEFX_CG_R_CLEARSCENE:
		re.ClearScene();
		return 0;
	case STEFX_CG_R_ADDREFENTITYTOSCENE:
		{
			const refEntity_t *refEnt = (const refEntity_t *) VMA(1);
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
			if (refEnt && refEnt->reType == RT_MODEL && refEnt->hModel >= 2 && refEnt->hModel <= 8)
			{
				static int s_stefxAddRefBridgeBudget = 96;
				if (s_stefxAddRefBridgeBudget > 0)
				{
					XBLog_Writef("STEFX: EF AddRef bridge model ent=%d hModel=%d renderfx=0x%x origin=(%g,%g,%g) axis0=(%g,%g,%g)",
						refEnt->number,
						refEnt->hModel,
						refEnt->renderfx,
						refEnt->origin[0], refEnt->origin[1], refEnt->origin[2],
						refEnt->axis[0][0], refEnt->axis[0][1], refEnt->axis[0][2]);
					--s_stefxAddRefBridgeBudget;
				}
			}
#endif
			re.AddRefEntityToScene( refEnt );
		}
		return 0;
	case STEFX_CG_R_GETLIGHTING:
		return re.GetLighting( (const float * ) VMA(1), (float *) VMA(2), (float *) VMA(3), (float *) VMA(4) );
	case STEFX_CG_R_ADDPOLYTOSCENE:
		re.AddPolyToScene( args[1], args[2], (const polyVert_t *) VMA(3) );
		return 0;
	case STEFX_CG_R_ADDLIGHTTOSCENE:
#ifdef VV_LIGHTING
		VVLightMan.RE_AddLightToScene( (const float *) VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
#else
		re.AddLightToScene( (const float *) VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
#endif
		return 0;
	case STEFX_CG_R_RENDERSCENE:
		{
			const stefxRefdef_t *efRefdef = (const stefxRefdef_t *) VMA(1);
			refdef_t jaRefdef;
			static int s_stefxRenderSceneLogBudget = 32;
			CL_STEFX_CopyRefdef( &jaRefdef, efRefdef );
			if (s_stefxRenderSceneLogBudget > 0)
			{
				XBLF("STEFX: EF RenderScene marshal #%d ef=%08x time=%d rd=0x%x view=(%g,%g,%g) fov=(%g,%g) rect=%d,%d %dx%d",
					32 - s_stefxRenderSceneLogBudget,
					(unsigned int)efRefdef,
					jaRefdef.time,
					jaRefdef.rdflags,
					jaRefdef.vieworg[0], jaRefdef.vieworg[1], jaRefdef.vieworg[2],
					jaRefdef.fov_x, jaRefdef.fov_y,
					jaRefdef.x, jaRefdef.y, jaRefdef.width, jaRefdef.height);
				--s_stefxRenderSceneLogBudget;
			}
			re.RenderScene( &jaRefdef );
		}
		return 0;
	case STEFX_CG_R_SETCOLOR:
		re.SetColor( (const float *) VMA(1) );
		return 0;
	case STEFX_CG_R_DRAWSTRETCHPIC:
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		{
			float w = VMF(3);
			float h = VMF(4);
			static int s_stefxStretchPicBudget = 48;
			if ( s_stefxStretchPicBudget > 0 && w >= 600.0f && h >= 400.0f )
			{
				XBLF("STEFX: EF DrawStretchPic large shader=%d xy=(%g,%g) wh=(%g,%g) st=(%g,%g,%g,%g)",
					args[9],
					VMF(1), VMF(2), w, h,
					VMF(5), VMF(6), VMF(7), VMF(8));
				--s_stefxStretchPicBudget;
			}
		}
#endif
		re.DrawStretchPic( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), args[9] );
		return 0;
	case STEFX_CG_R_DRAWSCREENSHOT:
		return 0;
	case STEFX_CG_R_MODELBOUNDS:
		re.ModelBounds( args[1], (float *) VMA(2), (float *) VMA(3) );
		return 0;
	case STEFX_CG_R_LERPTAG:
		re.LerpTag( (orientation_t *) VMA(1), args[2], args[3], args[4], VMF(5), (const char *) VMA(6) );
		return 0;
	case STEFX_CG_R_DRAWROTATEPIC:
		re.DrawRotatePic( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), VMF(9), args[10] );
		return 0;
	case STEFX_CG_R_SCISSOR:
		re.Scissor( VMF(1), VMF(2), VMF(3), VMF(4) );
		return 0;
	case STEFX_CG_GETGLCONFIG:
		CL_GetGlconfig( (glconfig_t *) VMA(1) );
		return 0;
	case STEFX_CG_GETGAMESTATE:
		CL_GetGameState( (gameState_t *) VMA(1) );
		return 0;
	case STEFX_CG_GETCURRENTSNAPSHOTNUMBER:
		CL_GetCurrentSnapshotNumber( (int *) VMA(1), (int *) VMA(2) );
		return 0;
	case STEFX_CG_GETSNAPSHOT:
		return CL_STEFX_GetSnapshot( args[1], VMA(2) );
	case STEFX_CG_GETSERVERCOMMAND:
		{
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
			static int s_stefxServerCommandSyscallBudget = 96;
			if ( s_stefxServerCommandSyscallBudget > 0 )
			{
				XBLF("STEFX: engine EF GetServerCommand request seq=%d current=%d",
					args[1], clc.serverCommandSequence);
			}
#endif
			qboolean gotCommand = CL_GetServerCommand( args[1] );
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
			if ( s_stefxServerCommandSyscallBudget > 0 )
			{
				XBLF("STEFX: engine EF GetServerCommand result seq=%d got=%d argv0='%s' argv1='%s' argv2='%s'",
					args[1], gotCommand ? 1 : 0,
					Cmd_Argv(0), Cmd_Argv(1), Cmd_Argv(2));
				--s_stefxServerCommandSyscallBudget;
			}
#endif
			return gotCommand;
		}
	case STEFX_CG_GETCURRENTCMDNUMBER:
		return CL_GetCurrentCmdNumber();
	case STEFX_CG_GETUSERCMD:
		return CL_GetUserCmd( args[1], (usercmd_s *) VMA(2) );
	case STEFX_CG_SETUSERCMDVALUE:
		CL_SetUserCmdValue( args[1], VMF(2), 0.0f, 0.0f );
		return 0;
	case STEFX_CG_MEMORY_REMAINING:
		return Hunk_MemoryRemaining();
	case STEFX_CG_S_UPDATEAMBIENTSET:
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		{
			static int s_stefxAmbientSyscallBudget = 48;
			if ( s_stefxAmbientSyscallBudget > 0 )
			{
				XBLF("STEFX: engine EF S_UpdateAmbientSet name='%s' started=%d",
					(const char *) VMA(1),
					cls.cgameStarted ? 1 : 0);
				--s_stefxAmbientSyscallBudget;
			}
		}
#endif
		if (!cls.cgameStarted)
		{
			return 0;
		}
		S_UpdateAmbientSet( (const char *) VMA(1), (float *) VMA(2) );
		return 0;
	case STEFX_CG_S_ADDLOCALSET:
		{
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
			static int s_stefxLocalSetSyscallBudget = 48;
			if ( s_stefxLocalSetSyscallBudget > 0 )
			{
				XBLF("STEFX: engine EF S_AddLocalSet enter name='%s' ent=%d time=%d",
					(const char *) VMA(1), args[4], args[5]);
			}
#endif
			int localSetTime = S_AddLocalSet( (const char *) VMA(1), (float *) VMA(2), (float *) VMA(3), args[4], args[5] );
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
			if ( s_stefxLocalSetSyscallBudget > 0 )
			{
				XBLF("STEFX: engine EF S_AddLocalSet done ent=%d result=%d",
					args[4], localSetTime);
				--s_stefxLocalSetSyscallBudget;
			}
#endif
			return localSetTime;
		}
	case STEFX_CG_AS_PARSESETS:
		AS_ParseSets();
		return 0;
	case STEFX_CG_AS_ADDENTRY:
		AS_AddPrecacheEntry( (const char *) VMA(1) );
		return 0;
	case STEFX_CG_AS_GETBMODELSOUND:
		return AS_GetBModelSound( (const char *) VMA(1), args[2] );
	case STEFX_CG_S_GETSAMPLELENGTH:
		return S_GetSampleLengthInMilliSeconds( args[1] );
	default:
		Com_Error( ERR_DROP, "Bad EF cgame system trap: %i", args[0] );
		return 0;
	}
}
#endif

int CL_CgameSystemCalls( int *args ) {
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	return CL_STEFX_CgameSystemCalls( args );
#endif
	switch( args[0] ) {
	case CG_PRINT:
		Com_Printf( "%s", VMA(1) );
		return 0;
	case CG_ERROR:
		Com_Error( ERR_DROP, S_COLOR_RED"%s", VMA(1) );
		return 0;
	case CG_MILLISECONDS:
		return Sys_Milliseconds();
	case CG_CVAR_REGISTER:
		Cvar_Register( (vmCvar_t *) VMA(1), (const char *) VMA(2), (const char *) VMA(3), args[4] ); 
		return 0;
	case CG_CVAR_UPDATE:
		Cvar_Update( (vmCvar_t *) VMA(1) );
		return 0;
	case CG_CVAR_SET:
		Cvar_Set( (const char *) VMA(1), (const char *) VMA(2) );
		return 0;
	case CG_ARGC:
		return Cmd_Argc();
	case CG_ARGV:
		Cmd_ArgvBuffer( args[1], (char *) VMA(2), args[3] );
		return 0;
	case CG_ARGS:
		Cmd_ArgsBuffer( (char *) VMA(1), args[2] );
		return 0;
	case CG_FS_FOPENFILE:
		return FS_FOpenFileByMode( (const char *) VMA(1), (int *) VMA(2), (fsMode_t) args[3] );
	case CG_FS_READ:
		FS_Read( VMA(1), args[2], args[3] );
		return 0;
	case CG_FS_WRITE:
		FS_Write( VMA(1), args[2], args[3] );
		return 0;
	case CG_FS_FCLOSEFILE:
		FS_FCloseFile( args[1] );
		return 0;
	case CG_SENDCONSOLECOMMAND:
		Cbuf_AddText( (const char *) VMA(1) );
		return 0;
	case CG_ADDCOMMAND:
		CL_AddCgameCommand( (const char *) VMA(1) );
		return 0;
	case CG_SENDCLIENTCOMMAND:
		CL_AddReliableCommand( (const char *) VMA(1) );
		return 0;
	case CG_UPDATESCREEN:
		// this is used during lengthy level loading, so pump message loop
		Com_EventLoop();	// FIXME: if a server restarts here, BAD THINGS HAPPEN!
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		if ( Sys_IsDirectMapBoot() )
		{
			CL_STEFX_DrawDirectMapLoadScreen( "CG_UPDATESCREEN" );
			return 0;
		}
#endif
		SCR_UpdateScreen();
		return 0;

#ifdef _XBOX
	case CG_RMG_INIT:
	case CG_CM_REGISTER_TERRAIN:
	case CG_RE_INIT_RENDERER_TERRAIN:
		Com_Error( ERR_FATAL, "ERROR: Terrain unsupported on Xbox.\n" );
#else
	case CG_RMG_INIT:
		/*
		if (!com_sv_running->integer)
		{	// don't do this if we are connected locally
			if (!TheRandomMissionManager)
			{
				TheRandomMissionManager = new CRMManager;
			}
			TheRandomMissionManager->SetLandScape( cmg.landScapes[args[1]] );
			TheRandomMissionManager->LoadMission(qfalse);
			TheRandomMissionManager->SpawnMission(qfalse);
			cmg.landScapes[args[1]]->UpdatePatches();
		}
		*/ //this is SP.. I guess we're always the client and server.
//		cl.mRMGChecksum = cm.landScapes[args[1]]->get_rand_seed();
		RM_CreateRandomModels(args[1], (const char *)VMA(2));
		//cmg.landScapes[args[1]]->rand_seed(cl.mRMGChecksum);		// restore it, in case we do a vid restart
		cmg.landScape->rand_seed(cmg.landScape->get_rand_seed());
//		TheRandomMissionManager->CreateMap();
		return 0;
	case CG_CM_REGISTER_TERRAIN:
		return CM_RegisterTerrain((const char *)VMA(1), false)->GetTerrainId();

	case CG_RE_INIT_RENDERER_TERRAIN:
		RE_InitRendererTerrain((const char *)VMA(1));
		return 0;
#endif	// _XBOX

	case CG_CM_LOADMAP:
#ifdef _XBOX
		CL_CM_LoadMap( (const char *) VMA(1) );
#else
		CL_CM_LoadMap( (const char *) VMA(1), args[2] );
#endif
		return 0;
	case CG_CM_NUMINLINEMODELS:
		return CM_NumInlineModels();
	case CG_CM_INLINEMODEL:
		return CL_SafeCgameInlineModel( "JA", args[1] );
	case CG_CM_TEMPBOXMODEL:
		return CM_TempBoxModel( (const float *) VMA(1), (const float *) VMA(2) );//, (int) VMA(3) );
	case CG_CM_POINTCONTENTS:
		return CM_PointContents( (float *)VMA(1), args[2] );
	case CG_CM_TRANSFORMEDPOINTCONTENTS:
		return CM_TransformedPointContents( (const float *) VMA(1), args[2], (const float *) VMA(3), (const float *) VMA(4) );
	case CG_CM_BOXTRACE:
		CM_BoxTrace( (trace_t *) VMA(1), (const float *) VMA(2), (const float *) VMA(3), (const float *) VMA(4), (const float *) VMA(5), args[6], args[7] );
		return 0;
	case CG_CM_TRANSFORMEDBOXTRACE:
		CM_TransformedBoxTrace( (trace_t *) VMA(1), (const float *) VMA(2), (const float *) VMA(3), (const float *) VMA(4), (const float *) VMA(5), args[6], args[7], (const float *) VMA(8), (const float *) VMA(9) );
		return 0;
	case CG_CM_MARKFRAGMENTS:
		return re.MarkFragments( args[1], (float(*)[3]) VMA(2), (const float *) VMA(3), args[4], (float *) VMA(5), args[6], (markFragment_t *) VMA(7) );
	case CG_CM_SNAPPVS:
		CM_SnapPVS((float(*))VMA(1),(byte *) VMA(2));
		return 0;
	case CG_S_STOPSOUNDS:
		S_StopSounds( );
		return 0;

	case CG_S_STARTSOUND:
		// stops an ERR_DROP internally if called illegally from game side, but note that it also gets here 
		//	legally during level start where normally the internal s_soundStarted check would return. So ok to hit this.
		if (!cls.cgameStarted){
			return 0;	
		}
		S_StartSound( (float *) VMA(1), args[2], (soundChannel_t)args[3], args[4] );
		return 0;
	case CG_S_UPDATEAMBIENTSET:
		// stops an ERR_DROP internally if called illegally from game side, but note that it also gets here 
		//	legally during level start where normally the internal s_soundStarted check would return. So ok to hit this.
		if (!cls.cgameStarted){
			return 0;
		}
		S_UpdateAmbientSet( (const char *) VMA(1), (float *) VMA(2) );
		return 0;
	case CG_S_ADDLOCALSET:
		return S_AddLocalSet( (const char *) VMA(1), (float *) VMA(2), (float *) VMA(3), args[4], args[5] );
	case CG_AS_PARSESETS:
		AS_ParseSets();
		return 0;
	case CG_AS_ADDENTRY:
		AS_AddPrecacheEntry( (const char *) VMA(1) );
		return 0;
	case CG_AS_GETBMODELSOUND:
		return AS_GetBModelSound( (const char *) VMA(1), args[2] );	
	case CG_S_STARTLOCALSOUND:
		// stops an ERR_DROP internally if called illegally from game side, but note that it also gets here 
		//	legally during level start where normally the internal s_soundStarted check would return. So ok to hit this.
		if (!cls.cgameStarted){
			return 0;
		}
		S_StartLocalSound( args[1], args[2] );
		return 0;
	case CG_S_CLEARLOOPINGSOUNDS:
		S_ClearLoopingSounds();
		return 0;
	case CG_S_ADDLOOPINGSOUND:
		// stops an ERR_DROP internally if called illegally from game side, but note that it also gets here 
		//	legally during level start where normally the internal s_soundStarted check would return. So ok to hit this.
		if (!cls.cgameStarted){
			return 0;
		}
		S_AddLoopingSound( args[1], (const float *) VMA(2), (const float *) VMA(3), args[4], (soundChannel_t)args[5] );
		return 0;
	case CG_S_UPDATEENTITYPOSITION:
		S_UpdateEntityPosition( args[1], (const float *) VMA(2) );
		return 0;
	case CG_S_RESPATIALIZE:
		S_Respatialize( args[1], (const float *) VMA(2), (float(*)[3]) VMA(3), args[4] );
		return 0;
	case CG_S_REGISTERSOUND:
		return S_RegisterSound( (const char *) VMA(1) );
	case CG_S_STARTBACKGROUNDTRACK:
#ifdef STEFX_ELITE_FORCE_SP
#ifdef _XBOX
		XBLog_Writef("STEFX: cgame background music syscall ef intro='%s' loop='%s'",
			(const char *) VMA(1),
			(const char *) VMA(2));
#endif
		S_StartBackgroundTrack( (const char *) VMA(1), (const char *) VMA(2), qfalse);
#else
		S_StartBackgroundTrack( (const char *) VMA(1), (const char *) VMA(2), args[3]);
#endif
		return 0;
	case CG_S_GETSAMPLELENGTH:
		return S_GetSampleLengthInMilliSeconds(  args[1]);
#ifdef _IMMERSION
	case CG_FF_START:
		CL_FF_Start( (ffHandle_t) args[1], (int) args[2] );
		return 0;
	case CG_FF_STOP:
		CL_FF_Stop( (ffHandle_t) args[1], (int) args[2] );
		return 0;
	case CG_FF_STOPALL:
		FF_StopAll();
		return 0;
	case CG_FF_SHAKE:
		FF_Shake( (int) args[1], (int) args[2] );
		return 0;
	case CG_FF_REGISTER:
		return FF_Register( (const char *) VMA(1), (int) args[2] );
	case CG_FF_ADDLOOPINGFORCE:
		CL_FF_AddLoopingForce( (ffHandle_t) args[1], (int) args[2] );
		return 0;
#else
	case CG_FF_STARTFX:
		FFFX_START( (ffFX_e) args[1] );
		return 0;
	case CG_FF_ENSUREFX:
		FFFX_ENSURE( (ffFX_e) args[1] );
		return 0;
	case CG_FF_STOPFX:
		FFFX_STOP( (ffFX_e) args[1] );
		return 0;
	case CG_FF_STOPALLFX:
		FFFX_STOPALL;
		return 0;
#endif // _IMMERSION
#ifdef _XBOX
	case CG_FF_XBOX_SHAKE:
		FF_XboxShake( VMF(1), (int) args[2] );
		return 0;
	case CG_FF_XBOX_DAMAGE:
		FF_XboxDamage( (int) args[1], VMF(2) );
		return 0;
#endif
	case CG_R_LOADWORLDMAP:
		re.LoadWorld( (const char *) VMA(1) );
		return 0; 
	case CG_R_REGISTERMODEL:
		return re.RegisterModel( (const char *) VMA(1) );
	case CG_R_REGISTERSKIN:
		return re.RegisterSkin( (const char *) VMA(1) );
	case CG_R_REGISTERSHADER:
		return re.RegisterShader( (const char *) VMA(1) );
	case CG_R_REGISTERSHADERNOMIP:
		return re.RegisterShaderNoMip( (const char *) VMA(1) );
	case CG_R_REGISTERFONT:
		return re.RegisterFont( (const char *) VMA(1) );
	case CG_R_FONTSTRLENPIXELS:
		return re.Font_StrLenPixels( (const char *) VMA(1), args[2], VMF(3) );
	case CG_R_FONTSTRLENCHARS:
		return re.Font_StrLenChars( (const char *) VMA(1) );
	case CG_R_FONTHEIGHTPIXELS:
		return re.Font_HeightPixels( args[1], VMF(2) );
	case CG_R_FONTDRAWSTRING:
		re.Font_DrawString(args[1],args[2], (const char *) VMA(3), (float*)args[4], args[5], args[6], VMF(7));
		return 0;
	case CG_LANGUAGE_ISASIAN:
		return re.Language_IsAsian();
	case CG_LANGUAGE_USESSPACES:
		return re.Language_UsesSpaces();
	case CG_ANYLANGUAGE_READFROMSTRING:
		return re.AnyLanguage_ReadCharFromString( (const char *) VMA(1), (int *) VMA(2), (qboolean *) VMA(3) );
	case CG_R_SETREFRACTIONPROP:
		tr_distortionAlpha = VMF(1);
		tr_distortionStretch = VMF(2);
		tr_distortionPrePost = (qboolean)args[3];
		tr_distortionNegate = (qboolean)args[4];
		return 0;
	case CG_R_CLEARSCENE:
		re.ClearScene();
		return 0;
	case CG_R_ADDREFENTITYTOSCENE:
		re.AddRefEntityToScene( (const refEntity_t *) VMA(1) );
		return 0;

	case CG_R_INPVS:
		return R_inPVS((float *) VMA(1), (float *) VMA(2));

	case CG_R_GETLIGHTING:
		return re.GetLighting( (const float * ) VMA(1), (float *) VMA(2), (float *) VMA(3), (float *) VMA(4) );
	case CG_R_ADDPOLYTOSCENE:
		re.AddPolyToScene( args[1], args[2], (const polyVert_t *) VMA(3) );
		return 0;
	case CG_R_ADDLIGHTTOSCENE:
#ifdef VV_LIGHTING
		VVLightMan.RE_AddLightToScene ( (const float *) VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
#else
		re.AddLightToScene( (const float *) VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
#endif
		return 0;
	case CG_R_RENDERSCENE:
		re.RenderScene( (const refdef_t *) VMA(1) );
		return 0;
	case CG_R_SETCOLOR:
		re.SetColor( (const float *) VMA(1) );
		return 0;
	case CG_R_DRAWSTRETCHPIC:
		re.DrawStretchPic( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), args[9] );
		return 0;
	//case CG_R_DRAWSCREENSHOT:
	//	re.DrawStretchRaw( VMF(1), VMF(2), VMF(3), VMF(4), SG_SCR_WIDTH, SG_SCR_HEIGHT, SCR_GetScreenshot(0), 0, qtrue);
	//	return 0;
	case CG_R_MODELBOUNDS:
		re.ModelBounds( args[1], (float *) VMA(2), (float *) VMA(3) );
		return 0;
	case CG_R_LERPTAG:
		re.LerpTag( (orientation_t *) VMA(1), args[2], args[3], args[4], VMF(5), (const char *) VMA(6) );
		return 0;
	case CG_R_DRAWROTATEPIC:
		re.DrawRotatePic( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), VMF(9), args[10] );
		return 0;
	case CG_R_DRAWROTATEPIC2:
		re.DrawRotatePic2( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), VMF(9), args[10] );
		return 0;
	case CG_R_SETRANGEFOG:
		if (tr.rangedFog <= 0.0f)
		{
			g_oldRangedFog = tr.rangedFog;
		}
		tr.rangedFog = VMF(1);
		if (tr.rangedFog == 0.0f && g_oldRangedFog)
		{ //restore to previous state if applicable
			tr.rangedFog = g_oldRangedFog;
		}
		return 0;
	case CG_R_LA_GOGGLES:
		re.LAGoggles();
		return 0;
	case CG_R_SCISSOR:
		re.Scissor( VMF(1), VMF(2), VMF(3), VMF(4));
		return 0;
	case CG_GETGLCONFIG:
		CL_GetGlconfig( (glconfig_t *) VMA(1) );
		return 0;
	case CG_GETGAMESTATE:
		CL_GetGameState( (gameState_t *) VMA(1) );
		return 0;
	case CG_GETCURRENTSNAPSHOTNUMBER:
		CL_GetCurrentSnapshotNumber( (int *) VMA(1), (int *) VMA(2) );
		return 0;
	case CG_GETSNAPSHOT:
		return CL_GetSnapshot( args[1], (snapshot_t *) VMA(2) );

	case CG_GETDEFAULTSTATE:
		return CL_GetDefaultState(args[1], (entityState_t *)VMA(2));

	case CG_GETSERVERCOMMAND:
		return CL_GetServerCommand( args[1] );
	case CG_GETCURRENTCMDNUMBER:
		return CL_GetCurrentCmdNumber();
	case CG_GETUSERCMD:
		return CL_GetUserCmd( args[1], (usercmd_s *) VMA(2) );
	case CG_SETUSERCMDVALUE:
		CL_SetUserCmdValue( args[1], VMF(2), VMF(3), VMF(4) );
		return 0;
	case CG_SETUSERCMDANGLES:
		CL_SetUserCmdAngles( VMF(1), VMF(2), VMF(3) );
		return 0;
	case COM_SETORGANGLES:
		Com_SetOrgAngles((float *)VMA(1),(float *)VMA(2));
		return 0;
/*
Ghoul2 Insert Start
*/
		
	case CG_G2_LISTSURFACES:
		G2API_ListSurfaces( (CGhoul2Info *) VMA(1) );
		return 0;

	case CG_G2_LISTBONES:
		G2API_ListBones( (CGhoul2Info *) VMA(1), args[2]);
		return 0;

	case CG_G2_HAVEWEGHOULMODELS:
		return G2API_HaveWeGhoul2Models( *((CGhoul2Info_v *)VMA(1)) );

	case CG_G2_SETMODELS:
		G2API_SetGhoul2ModelIndexes( *((CGhoul2Info_v *)VMA(1)),(qhandle_t *)VMA(2),(qhandle_t *)VMA(3));
		return 0;

/*
Ghoul2 Insert End
*/

	case CG_R_GET_LIGHT_STYLE:
		re.GetLightStyle(args[1], (byte*) VMA(2) );
		return 0;
	case CG_R_SET_LIGHT_STYLE:
		re.SetLightStyle(args[1], args[2] );
		return 0;

	case CG_R_GET_BMODEL_VERTS:
		re.GetBModelVerts( args[1], (float (*)[3])VMA(2), (float *)VMA(3) );
		return 0;
	
	case CG_R_WORLD_EFFECT_COMMAND:
		re.WorldEffectCommand( (const char *) VMA(1) );
		return 0;

	case CG_CIN_PLAYCINEMATIC:
	  return CIN_PlayCinematic( (const char *) VMA(1), args[2], args[3], args[4], args[5], args[6], (const char *) VMA(7));

	case CG_CIN_STOPCINEMATIC:
	  return CIN_StopCinematic(args[1]);

	case CG_CIN_RUNCINEMATIC:
	  return CIN_RunCinematic(args[1]);

#ifndef _XBOX
	case CG_CIN_DRAWCINEMATIC:
	  CIN_DrawCinematic(args[1]);
	  return 0;
#endif

	case CG_CIN_SETEXTENTS:
	  CIN_SetExtents(args[1], args[2], args[3], args[4], args[5]);
	  return 0;

	case CG_Z_MALLOC:
		return (int)Z_Malloc(args[1], (memtag_t) args[2], qfalse);

	case CG_Z_FREE:
		Z_Free((void *) VMA(1));
		return 0;

	case CG_UI_SETACTIVE_MENU:
		UI_SetActiveMenu((const char *) VMA(1),NULL);
		return 0;

	case CG_UI_MENU_OPENBYNAME:
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		XBLF("STEFX: CG_UI_MENU_OPENBYNAME routing through EF active menu name='%s'", (const char *) VMA(1));
		UI_SetActiveMenu((const char *) VMA(1), NULL);
#else
		Menus_OpenByName((const char *) VMA(1));
#endif
		return 0;

	case CG_UI_MENU_RESET:
		Menu_Reset();
		return 0;

	case CG_UI_MENU_NEW:
		Menu_New((char *) VMA(1));
		return 0;

	case CG_UI_PARSE_INT:
		PC_ParseInt((int *) VMA(1));
		return 0;

	case CG_UI_PARSE_STRING:
		PC_ParseString((const char **) VMA(1));
		return 0;

	case CG_UI_PARSE_FLOAT:
		PC_ParseFloat((float *) VMA(1));
		return 0;

	case CG_UI_STARTPARSESESSION:
	{
#ifdef _XBOX
		const char *parseName = (const char *) VMA(1);
		Com_PrintfAlways("JA: CG trap UI_STARTPARSE begin file=%s\n", parseName ? parseName : "(null)");
#endif
		int parseLen = PC_StartParseSession((char *) VMA(1),(char **) VMA(2));
#ifdef _XBOX
		Com_PrintfAlways("JA: CG trap UI_STARTPARSE done file=%s len=%d\n", parseName ? parseName : "(null)", parseLen);
#endif
		return parseLen;
	}

	case CG_UI_ENDPARSESESSION:
		PC_EndParseSession((char *) VMA(1));
		return 0;

	case CG_UI_PARSEEXT:
		char **holdPtr;

		holdPtr = (char **) VMA(1);
		*holdPtr = PC_ParseExt();
		return 0;

	case CG_UI_MENUCLOSE_ALL:
		Menus_CloseAll();
		return 0;

	case CG_UI_MENUPAINT_ALL:
		Menu_PaintAll();
		return 0;

	case CG_UI_STRING_INIT:
		String_Init();
		return 0;

	case CG_UI_GETMENUINFO:
		menuDef_t *menu;
		int		*xPos,*yPos,*w,*h,result;

		menu = Menus_FindByName((char *) VMA(1));	// Get menu 
		if (menu)
		{
			xPos = (int *) VMA(2);
			*xPos = (int) menu->window.rect.x;
			yPos = (int *) VMA(3);
			*yPos = (int) menu->window.rect.y;
			w = (int *) VMA(4);
			*w = (int) menu->window.rect.w;
			h = (int *) VMA(5);
			*h = (int) menu->window.rect.h;
			result = qtrue;
		}
		else
		{
			result = qfalse;
		}

		return result;

	case CG_UI_GETITEMTEXT:
		itemDef_t *item;
		menu = Menus_FindByName((char *) VMA(1));	// Get menu 

		if (menu)
		{
			item = (itemDef_s *) Menu_FindItemByName((menuDef_t *) menu, (char *) VMA(2));
			if (item)
			{
				Q_strncpyz( (char *) VMA(3), item->text, 256 );
				result = qtrue;
			}
			else
			{
				result = qfalse;
			}
		}
		else
		{
			result = qfalse;
		}

		return result;

	case CG_UI_GETITEMINFO:
		menu = Menus_FindByName((char *) VMA(1));	// Get menu 

		if (menu)
		{
			qhandle_t *background;

			item = (itemDef_s *) Menu_FindItemByName((menuDef_t *) menu, (char *) VMA(2));
			if (item)
			{
				xPos = (int *) VMA(3);
				*xPos = (int) item->window.rect.x;
				yPos = (int *) VMA(4);
				*yPos = (int) item->window.rect.y;
				w = (int *) VMA(5);
				*w = (int) item->window.rect.w;
				h = (int *) VMA(6);
				*h = (int) item->window.rect.h;

				vec4_t *color;

				color = (vec4_t *) VMA(7);
				if (!color)
				{
					return qfalse;
				}

				(*color)[0] = (float) item->window.foreColor[0];
				(*color)[1] = (float) item->window.foreColor[1];
				(*color)[2] = (float) item->window.foreColor[2];
				(*color)[3] = (float) item->window.foreColor[3];
				background = (qhandle_t *) VMA(8);
				if (!background)
				{
					return qfalse;
				}
				*background = item->window.background;

				result = qtrue;
			}
			else
			{
				result = qfalse;
			}
		}
		else
		{
			result = qfalse;
		}

		return result;
		
	case CG_SP_GETSTRINGTEXTSTRING:
		const char* text;

		assert(VMA(1));	
		text = SE_GetString( (const char *) VMA(1) );

		if (VMA(2))	// only if dest buffer supplied...
		{
			if ( text[0] )
			{
				Q_strncpyz( (char *) VMA(2), text, args[3] );				
			}
			else 
			{
				Com_sprintf( (char *) VMA(2), args[3], "??%s", VMA(1) );
			}
		}
		return strlen(text);
		//break;
	default:
		Com_Error( ERR_DROP, "Bad cgame system trap: %i", args[0] );
	}
	return 0;
}


/*
====================
CL_InitCGame

Should only be called by CL_StartHunkUsers
====================
*/
extern qboolean Sys_LowPhysicalMemory();
void CL_InitCGame( void ) {
	const char			*info;
	const char			*mapname;
	int		t1, t2;

	XBLog_Write("JA: CL_InitCGame entered");

	t1 = Sys_Milliseconds();

	// put away the console
	Con_Close();

	// find the current mapname
	info = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
	mapname = Info_ValueForKey( info, "mapname" );
	Com_sprintf( cl.mapname, sizeof( cl.mapname ), "maps/%s.bsp", mapname );
	XBLog_Write("JA: Loading map:");
	XBLog_Write(cl.mapname);

	cls.state = CA_LOADING;
	XBLog_Write("JA: cls.state = CA_LOADING");

	// init for this gamestate
	XBLog_Write("JA: VM_Call(CG_INIT)...");
	VM_Call( CG_INIT, clc.serverCommandSequence );
	XBLog_Write("JA: VM_Call(CG_INIT) done");

	// we will send a usercmd this frame, which
	// will cause the server to send us the first snapshot
	cls.state = CA_PRIMED;
	XBLog_Write("JA: cls.state = CA_PRIMED");

	t2 = Sys_Milliseconds();

	//Com_Printf( "CL_InitCGame: %5.2f seconds\n", (t2-t1)/1000.0 );
	// have the renderer touch all its images, so they are present
	// on the card even if the driver does deferred loading
	re.EndRegistration();

	// make sure everything is paged in
//	if (!Sys_LowPhysicalMemory()) 
	{
		Com_TouchMemory();
	}

	// clear anything that got printed
	Con_ClearNotify ();
}


/*
====================
CL_GameCommand

See if the current console command is claimed by the cgame
====================
*/
qboolean CL_GameCommand( void ) {
	if ( cls.state != CA_ACTIVE ) {
		return qfalse;
	}

	return VM_Call( CG_CONSOLE_COMMAND );
}



/*
=====================
CL_CGameRendering
=====================
*/
void CL_CGameRendering( stereoFrame_t stereo ) {
#if 0
	if ( cls.state == CA_ACTIVE ) {
		static int counter;

		if ( ++counter == 40 ) {
			VM_Debug( 2 );
		}
	}
#endif
#ifdef _XBOX
	static int s_xboxCGameRenderCount = 0;
	const int xboxLogLateFrame = (cls.state == CA_ACTIVE && cl.serverTime >= 3600 && cl.serverTime <= 4600);
	const int xboxLogThisFrame = (cls.state == CA_ACTIVE && (s_xboxCGameRenderCount < 24 || xboxLogLateFrame));
	if (xboxLogThisFrame)
	{
		XBLF("JA: CL_CGameRendering #%d enter state=%d serverTime=%d stereo=%d",
			s_xboxCGameRenderCount, (int)cls.state, cl.serverTime, (int)stereo);
	}
#endif
	int timei=cl.serverTime;
	if (timei>60)
	{
		timei-=0;
	}
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if (xboxLogThisFrame)
	{
		XBLog_Write("STEFX: Ghoul2 cgame time tick skipped");
	}
#else
	G2API_SetTime(cl.serverTime,G2T_CG_TIME);
#endif
#ifdef _XBOX
	if (xboxLogThisFrame)
	{
		XBLog_Write("JA: CL_CGameRendering: VM_Call(CG_DRAW_ACTIVE_FRAME)...");
	}
#endif
	VM_Call( CG_DRAW_ACTIVE_FRAME,timei, stereo, qfalse );
#ifdef _XBOX
	if (xboxLogThisFrame)
	{
		XBLog_Write("JA: CL_CGameRendering: VM_Call(CG_DRAW_ACTIVE_FRAME) returned");
	}
	s_xboxCGameRenderCount++;
#endif
//	VM_Debug( 0 );
}


/*
=================
CL_AdjustTimeDelta

Adjust the clients view of server time.

We attempt to have cl.serverTime exactly equal the server's view
of time plus the timeNudge, but with variable latencies over
the internet it will often need to drift a bit to match conditions.

Our ideal time would be to have the adjusted time aproach, but not pass,
the very latest snapshot.

Adjustments are only made when a new snapshot arrives, which keeps the
adjustment process framerate independent and prevents massive overadjustment
during times of significant packet loss.
=================
*/

#define	RESET_TIME	300

void CL_AdjustTimeDelta( void ) {
/*
	cl.newSnapshots = qfalse;
	// if the current time is WAY off, just correct to the current value
	if ( cls.realtime + cl.serverTimeDelta < cl.frame.serverTime - RESET_TIME 
		|| cls.realtime + cl.serverTimeDelta > cl.frame.serverTime + RESET_TIME  ) {
		cl.serverTimeDelta = cl.frame.serverTime - cls.realtime;
		cl.oldServerTime = cl.frame.serverTime;
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<RESET> " );
		}
	}

	// if any of the frames between this and the previous snapshot
	// had to be extrapolated, nudge our sense of time back a little
	if ( cl.extrapolatedSnapshot ) {
		cl.extrapolatedSnapshot = qfalse;
		cl.serverTimeDelta -= 2;
	} else {
		// otherwise, move our sense of time forward to minimize total latency
		cl.serverTimeDelta++;
	}

	if ( cl_showTimeDelta->integer ) {
		Com_Printf( "%i ", cl.serverTimeDelta );
	}
*/
	int		resetTime;
	int		newDelta;
	int		deltaDelta;

	cl.newSnapshots = qfalse;
	
	// if the current time is WAY off, just correct to the current value
	if ( com_sv_running->integer ) {
		resetTime = 100;
	} else {
		resetTime = RESET_TIME;
	}

	newDelta = cl.frame.serverTime - cls.realtime;
	deltaDelta = abs( newDelta - cl.serverTimeDelta );

	if ( deltaDelta > RESET_TIME ) {
		cl.serverTimeDelta = newDelta;
		cl.oldServerTime = cl.frame.serverTime;	// FIXME: is this a problem for cgame?
		cl.serverTime = cl.frame.serverTime;
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<RESET> " );
		}
	} else if ( deltaDelta > 100 ) {
		// fast adjust, cut the difference in half
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<FAST> " );
		}
		cl.serverTimeDelta = ( cl.serverTimeDelta + newDelta ) >> 1;
	} else {
		// slow drift adjust, only move 1 or 2 msec

		// if any of the frames between this and the previous snapshot
		// had to be extrapolated, nudge our sense of time back a little
		// the granularity of +1 / -2 is too high for timescale modified frametimes
		if ( com_timescale->value == 0 || com_timescale->value == 1 ) {
			if ( cl.extrapolatedSnapshot ) {
				cl.extrapolatedSnapshot = qfalse;
				cl.serverTimeDelta -= 2;
			} else {
				// otherwise, move our sense of time forward to minimize total latency
				cl.serverTimeDelta++;
			}
		}
	}

	if ( cl_showTimeDelta->integer ) {
		Com_Printf( "%i ", cl.serverTimeDelta );
	}
}

// The UI sets this to a non-negative value, when the level select cheat is used
// to start a level. It will be the rank of neutral force powers that we should have.
// All others will be rank 3. =)
//int levelSelectCheat = -1;

/*
==================
CL_FirstSnapshot
==================
*/
void CL_FirstSnapshot( void ) {
	XBLog_Write("JA: CL_FirstSnapshot entered");

	RE_RegisterMedia_LevelLoadEnd();

	cls.state = CA_ACTIVE;
	XBLog_Write("JA: cls.state = CA_ACTIVE - GAME IS RUNNING");
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if ( Sys_IsDirectMapBoot() && (cls.keyCatchers & KEYCATCH_UI) )
	{
		XBLF("STEFX: direct-map first snapshot clearing UI catcher=0x%x", (unsigned int)cls.keyCatchers);
		UI_SetActiveMenu(NULL, NULL);
		XBLF("STEFX: direct-map first snapshot UI cleared catcher=0x%x", (unsigned int)cls.keyCatchers);
	}
#endif

	// set the timedelta so we are exactly on this first frame
	cl.serverTimeDelta = cl.frame.serverTime - cls.realtime;
	cl.oldServerTime = cl.frame.serverTime;

	// if this is the first frame of active play,
	// execute the contents of activeAction now
	// this is to allow scripting a timedemo to start right
	// after loading
	if ( cl_activeAction->string[0] ) {
		Cbuf_AddText( cl_activeAction->string );
		Cvar_Set( "activeAction", "" );
	}
	
	Sys_BeginProfiling();

#ifdef _XBOX
	// turn vsync back on - tearing is ugly
	glEnable(GL_VSYNC);
#endif

#ifdef XBOX_DEMO
	// It's convenient, so I call this the "end of loading" for timer pausing
	extern void Demo_TimerPause( bool bPaused );
	Demo_TimerPause( false );

	playerState_t *pState = svs.clients[0].gentity->client;

	// Give us the right weapons, and max ammo:
	extern int demoWeapon1;
	extern int demoWeapon2;
	extern int demoThrowable;
	pState->stats[STAT_WEAPONS] = 1 << WP_SABER;
	Cbuf_ExecuteText( EXEC_APPEND, "give weaponnum 2\n" );
	Cbuf_ExecuteText( EXEC_APPEND, va("give weaponnum %d\n", demoWeapon1) );
	Cbuf_ExecuteText( EXEC_APPEND, va("give weaponnum %d\n", demoWeapon2) );
	Cbuf_ExecuteText( EXEC_APPEND, va("give weaponnum %d\n", demoThrowable) );
	Cbuf_ExecuteText( EXEC_APPEND, "give ammo\n" );
#else
	// Goodies?
	cvar_t*	levelSelectCheat	= Cvar_Get("levelSelectCheat", "-1", CVAR_SAVEGAME);
	if(  levelSelectCheat->integer >= 0 )
	{
		int n = levelSelectCheat->integer;
		//levelSelectCheat = -1;

		// Set all Light powers to level 3:
		Cbuf_ExecuteText( EXEC_APPEND, "setForceHeal 3\nsetMindTrick 3\nsetForceProtect 3\nsetForceAbsorb 3\n" );
		// Set all Dark powers to level 3:
		Cbuf_ExecuteText( EXEC_APPEND, "setForceGrip 3\nsetForceLightning 3\nsetForceRage 3\nsetForceDrain 3\n" );

		// Special case for yavin1b - we need saber powers, but no other neutral
		if( n == 0 )
			Cbuf_ExecuteText( EXEC_APPEND, "setSaberOffense 1\nsetSaberDefense 1\nsetSaberThrow 1\n" );
		else
			Cbuf_ExecuteText( EXEC_APPEND, va("setSaberOffense %d\nsetSaberDefense %d\nsetSaberThrow %d\n", n, n, n) );

		// Set all remaining neutral powers to cheat level:
		Cbuf_ExecuteText( EXEC_APPEND, va("setForcePush %d\nsetForcePull %d\nsetForceSpeed %d\nsetForceJump %d\nsetForceSight %d\n", n, n, n, n, n) );
	}
#endif
}

/*
==================
CL_SetCGameTime
==================
*/
void CL_SetCGameTime( void ) {
#ifdef _XBOX
	static int s_xboxPrimedSetTimeLogCount = 0;
#endif

	// getting a valid frame message ends the connection process
	if ( cls.state != CA_ACTIVE ) {
		if ( cls.state != CA_PRIMED ) {
			return;
		}
#ifdef _XBOX
		if (s_xboxPrimedSetTimeLogCount < 32 || (s_xboxPrimedSetTimeLogCount & 63) == 0)
		{
			XBLF("JA: CL_SetCGameTime primed count=%d newSnapshots=%d frameValid=%d frameMsg=%d serverTime=%d realtime=%d",
				s_xboxPrimedSetTimeLogCount,
				(int)cl.newSnapshots,
				(int)cl.frame.valid,
				cl.frame.messageNum,
				cl.frame.serverTime,
				cls.realtime);
		}
		s_xboxPrimedSetTimeLogCount++;
#endif
		if ( cl.newSnapshots ) {
			cl.newSnapshots = qfalse;
			CL_FirstSnapshot();
		}

		if ( cls.state != CA_ACTIVE ) {
#ifdef _XBOX
			if (s_xboxPrimedSetTimeLogCount < 32 || (s_xboxPrimedSetTimeLogCount & 63) == 0)
			{
				XBLog_Write("JA: CL_SetCGameTime still not active after primed check");
			}
#endif
			return;
		}
	}	

	// if we have gotten to this point, cl.frame is guaranteed to be valid
	if ( !cl.frame.valid ) {
		Com_Error( ERR_DROP, "CL_SetCGameTime: !cl.snap.valid" );
	}

	// allow pause in single player
	static int pauseStart = 0;
	static int pauseServerTimeDelta;
	if ( sv_paused->integer && cl_paused->integer && com_sv_running->integer ) {
		// paused
		if(!pauseStart) {
			pauseServerTimeDelta = cl.serverTimeDelta;
			pauseStart = cls.realtime;
		}
		return;
	}

	if ( cl.frame.serverTime < cl.oldFrameServerTime ) {
		Com_Error( ERR_DROP, "cl.frame.serverTime < cl.oldFrameServerTime" );
	}
	cl.oldFrameServerTime = cl.frame.serverTime;


	// get our current view of time

	// cl_timeNudge is a user adjustable cvar that allows more
	// or less latency to be added in the interest of better 
	// smoothness or better responsiveness.
	cl.serverTime = cls.realtime + cl.serverTimeDelta - cl_timeNudge->integer;

	//If we were paused, subtract out pause time once since we won't yet
	//have an updated server time.  Otherwise the camera gets confused when
	//time leaps forward for a frame and then resets back to normal.
	if(pauseStart && pauseServerTimeDelta == cl.serverTimeDelta) {
		cl.serverTime -= cls.realtime - pauseStart;
	} else {
		pauseStart = 0;
		pauseServerTimeDelta = 0;
	}

	// guarantee that time will never flow backwards, even if
	// serverTimeDelta made an adjustment or cl_timeNudge was changed
	if ( cl.serverTime < cl.oldServerTime ) {
		cl.serverTime = cl.oldServerTime;
	}
	cl.oldServerTime = cl.serverTime;

	// note if we are almost past the latest frame (without timeNudge),
	// so we will try and adjust back a bit when the next snapshot arrives
	if ( cls.realtime + cl.serverTimeDelta >= cl.frame.serverTime - 5 ) {
		cl.extrapolatedSnapshot = qtrue;
	}

	// if we have gotten new snapshots, drift serverTimeDelta
	// don't do this every frame, or a period of packet loss would
	// make a huge adjustment
	if ( cl.newSnapshots ) {
		CL_AdjustTimeDelta();
	}
}
