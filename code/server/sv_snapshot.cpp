// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"



#include "..\client\vmachine.h"
#include "server.h"
#if defined(STEFX_ELITE_FORCE_SP)
#include "../qcommon/stefx_snapshot_abi.h"
#endif

#ifdef _XBOX
#include "../win32/xb_log.h"
extern bool g_xboxDirectMapBootQueued;
extern bool Sys_IsDirectMapBoot(void);
#endif

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
static qboolean SV_STEFX_IsBrushMoverState( const entityState_t *state );
static qboolean SV_STEFX_IsBroadcastBrushMover( const gentity_t *ent );
#endif


/*
=============================================================================

Delta encode a client frame onto the network channel

A normal server packet will look like:

4	sequence number (high bit set if an oversize fragment)
<optional reliable commands>
1	svc_snapshot
4	last client reliable command
4	serverTime
1	lastframe for delta compression
1	snapFlags
1	areaBytes
<areabytes>
<playerstate>
<packetentities>

=============================================================================
*/

/*
=============
SV_EmitPacketEntities

Writes a delta update of an entityState_t list to the message.
=============
*/
static void SV_EmitPacketEntities( clientSnapshot_t *from, clientSnapshot_t *to, msg_t *msg ) {
	entityState_t	*oldent, *newent;
	int		oldindex, newindex;
	int		oldnum, newnum;
	int		from_num_entities;

	// generate the delta update
	if ( !from ) {
		from_num_entities = 0;
	} else {
		from_num_entities = from->num_entities;
	}

	newent = NULL;
	oldent = NULL;
	newindex = 0;
	oldindex = 0;
	const int num2Send = to->num_entities >= svs.numSnapshotEntities ? svs.numSnapshotEntities : to->num_entities;

	while ( newindex < num2Send || oldindex < from_num_entities ) {
		if ( newindex >= num2Send ) {
			newnum = 9999;
		} else {
			newent = &svs.snapshotEntities[(to->first_entity+newindex) % svs.numSnapshotEntities];
			newnum = newent->number;
		}

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		if ( SV_STEFX_IsBrushMoverState( newent ) )
		{
			static int s_stefxEmitBrushBudget = 160;
			if ( s_stefxEmitBrushBudget > 0 )
			{
				XBLF("STEFX: SV_EmitPacketEntities bmodel ent=%d model=%d eType=%d solid=0x%x eFlags=0x%x newindex=%d first=%d num=%d fromNum=%d",
					newent->number,
					newent->modelindex,
					newent->eType,
					newent->solid,
					newent->eFlags,
					newindex,
					to ? to->first_entity : -1,
					to ? to->num_entities : -1,
					from_num_entities);
				--s_stefxEmitBrushBudget;
			}
		}
#endif

		if ( oldindex >= from_num_entities ) {
			oldnum = 9999;
		} else {
			oldent = &svs.snapshotEntities[(from->first_entity+oldindex) % svs.numSnapshotEntities];
			oldnum = oldent->number;
		}

		if ( newnum == oldnum ) {
			// delta update from old position
			// because the force parm is qfalse, this will not result
			// in any bytes being emited if the entity has not changed at all
			MSG_WriteEntity(msg, newent, 0);
			oldindex++;
			newindex++;
			continue;
		}

		if ( newnum < oldnum ) {
			// this is a new entity, send it from the baseline
			MSG_WriteEntity (msg, newent, 0);
			newindex++;
			continue;
		}

		if ( newnum > oldnum ) {
			// the old entity isn't present in the new message
			if(oldent) {
				MSG_WriteEntity (msg, NULL, oldent->number);
			}
			oldindex++;
			continue;
		}
	}

	MSG_WriteBits( msg, (MAX_GENTITIES-1), GENTITYNUM_BITS );	// end of packetentities
}



/*
==================
SV_WriteSnapshotToClient
==================
*/
static void SV_WriteSnapshotToClient( client_t *client, msg_t *msg ) {
	clientSnapshot_t	*frame, *oldframe;
	int					lastframe;
	int					snapFlags;

	// this is the snapshot we are creating
	frame = &client->frames[ client->netchan.outgoingSequence & PACKET_MASK ];

	// try to use a previous frame as the source for delta compressing the snapshot
	if ( client->deltaMessage <= 0 || client->state != CS_ACTIVE ) {
		// client is asking for a retransmit
		oldframe = NULL;
		lastframe = 0;
	} else if ( client->netchan.outgoingSequence - client->deltaMessage 
		>= (PACKET_BACKUP - 3) ) {
		// client hasn't gotten a good message through in a long time
		Com_DPrintf ("%s: Delta request from out of date packet.\n", client->name);
		oldframe = NULL;
		lastframe = 0;
	} else {
		// we have a valid snapshot to delta from
		oldframe = &client->frames[ client->deltaMessage & PACKET_MASK ];
		lastframe = client->netchan.outgoingSequence - client->deltaMessage;

		// the snapshot's entities may still have rolled off the buffer, though
		if ( oldframe->first_entity <= svs.nextSnapshotEntities - svs.numSnapshotEntities ) {
			Com_DPrintf ("%s: Delta request from out of date entities.\n", client->name);
			oldframe = NULL;
			lastframe = 0;
		}
	}

	MSG_WriteByte (msg, svc_snapshot);

	// let the client know which reliable clientCommands we have received
	MSG_WriteLong( msg, client->lastClientCommand );

	// send over the current server time so the client can drift
	// its view of time to try to match
	MSG_WriteLong (msg, sv.time);

	// we must write a message number, because recorded demos won't have
	// the same network message sequences
	MSG_WriteLong (msg, client->netchan.outgoingSequence );
	MSG_WriteByte (msg, lastframe);				// what we are delta'ing from
	MSG_WriteLong (msg, client->cmdNum);		// we have executed up to here

	snapFlags = client->rateDelayed | ( client->droppedCommands << 1 );
	client->droppedCommands = 0;

	MSG_WriteByte (msg, snapFlags);

	// send over the areabits
	MSG_WriteByte (msg, frame->areabytes);
	MSG_WriteData (msg, frame->areabits, frame->areabytes);

	// delta encode the playerstate
	if ( oldframe ) {
		MSG_WriteDeltaPlayerstate( msg, &oldframe->ps, &frame->ps );
	} else {
		MSG_WriteDeltaPlayerstate( msg, NULL, &frame->ps );
	}

	// delta encode the entities
	SV_EmitPacketEntities (oldframe, frame, msg);
}


/*
==================
SV_UpdateServerCommandsToClient

(re)send all server commands the client hasn't acknowledged yet
==================
*/
static void SV_UpdateServerCommandsToClient( client_t *client, msg_t *msg ) {
	int		i;

	// write any unacknowledged serverCommands
	for ( i = client->reliableAcknowledge + 1 ; i <= client->reliableSequence ; i++ ) {
		MSG_WriteByte( msg, svc_serverCommand );
		MSG_WriteLong( msg, i );
		MSG_WriteString( msg, client->reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
	}
}

/*
=============================================================================

Build a client snapshot structure

=============================================================================
*/

#define	MAX_SNAPSHOT_ENTITIES	1024
typedef struct {
	int		numSnapshotEntities;
	int		snapshotEntities[MAX_SNAPSHOT_ENTITIES];	
} snapshotEntityNumbers_t;

/*
=======================
SV_QsortEntityNumbers
=======================
*/
static int SV_QsortEntityNumbers( const void *a, const void *b ) {
	int	*ea, *eb;

	ea = (int *)a;
	eb = (int *)b;

	if ( *ea == *eb ) {
		Com_Error( ERR_DROP, "SV_QsortEntityStates: duplicated entity" );
	}

	if ( *ea < *eb ) {
		return -1;
	}

	return 1;
}


/*
===============
SV_AddEntToSnapshot
===============
*/
static void SV_AddEntToSnapshot( svEntity_t *svEnt, gentity_t *gEnt, snapshotEntityNumbers_t *eNums ) {
#ifdef _XBOX
	static int s_xboxAddMissileBudget = 96;
	qboolean xboxLogMissile = (gEnt && gEnt->s.eType == ET_MISSILE && s_xboxAddMissileBudget > 0);
#if defined(STEFX_ELITE_FORCE_SP)
	static int s_stefxAddEventBudget = 128;
	static int s_stefxAddBrushMoverBudget = 128;
	qboolean stefxLogEvent = (sv_mapname && !Q_stricmp(sv_mapname->string, "borg1") &&
		gEnt && gEnt->s.eType > ET_EVENTS && s_stefxAddEventBudget > 0);
	qboolean stefxLogBrushMover = (SV_STEFX_IsBroadcastBrushMover( gEnt ) &&
		s_stefxAddBrushMoverBudget > 0);
#endif
#endif
	// if we have already added this entity to this snapshot, don't add again
	if ( svEnt->snapshotCounter == sv.snapshotCounter ) {
#ifdef _XBOX
		if (xboxLogMissile)
		{
			XBLF("JA: SV_AddEntToSnapshot missile duplicate ent=%d weapon=%d snapshot=%d",
				gEnt->s.number,
				gEnt->s.weapon,
				sv.snapshotCounter);
			--s_xboxAddMissileBudget;
		}
#if defined(STEFX_ELITE_FORCE_SP)
		if (stefxLogEvent)
		{
			XBLF("STEFX: SV_AddEntToSnapshot event duplicate ent=%d eType=%d weapon=%d sv=0x%x snapshot=%d",
				gEnt->s.number,
				gEnt->s.eType,
				gEnt->s.weapon,
				gEnt->svFlags,
				sv.snapshotCounter);
			--s_stefxAddEventBudget;
		}
		if (stefxLogBrushMover)
		{
			XBLF("STEFX: SV_AddEntToSnapshot bmodel duplicate ent=%d model=%d eType=%d solid=0x%x sv=0x%x eFlags=0x%x snapshotCounter=%d",
				gEnt->s.number,
				gEnt->s.modelindex,
				gEnt->s.eType,
				gEnt->s.solid,
				gEnt->svFlags,
				gEnt->s.eFlags,
				sv.snapshotCounter);
			--s_stefxAddBrushMoverBudget;
		}
#endif
#endif
		return;
	}
	svEnt->snapshotCounter = sv.snapshotCounter;

	// if we are full, silently discard entities
	if ( eNums->numSnapshotEntities == MAX_SNAPSHOT_ENTITIES ) {
#ifdef _XBOX
		if (xboxLogMissile)
		{
			XBLF("JA: SV_AddEntToSnapshot missile full ent=%d weapon=%d num=%d max=%d",
				gEnt->s.number,
				gEnt->s.weapon,
				eNums->numSnapshotEntities,
				MAX_SNAPSHOT_ENTITIES);
			--s_xboxAddMissileBudget;
		}
#if defined(STEFX_ELITE_FORCE_SP)
		if (stefxLogEvent)
		{
			XBLF("STEFX: SV_AddEntToSnapshot event full ent=%d eType=%d weapon=%d num=%d max=%d",
				gEnt->s.number,
				gEnt->s.eType,
				gEnt->s.weapon,
				eNums->numSnapshotEntities,
				MAX_SNAPSHOT_ENTITIES);
			--s_stefxAddEventBudget;
		}
		if (stefxLogBrushMover)
		{
			XBLF("STEFX: SV_AddEntToSnapshot bmodel full ent=%d model=%d eType=%d solid=0x%x sv=0x%x eFlags=0x%x num=%d max=%d",
				gEnt->s.number,
				gEnt->s.modelindex,
				gEnt->s.eType,
				gEnt->s.solid,
				gEnt->svFlags,
				gEnt->s.eFlags,
				eNums->numSnapshotEntities,
				MAX_SNAPSHOT_ENTITIES);
			--s_stefxAddBrushMoverBudget;
		}
#endif
#endif
		return;
	}

	if (sv.snapshotCounter &1 && eNums->numSnapshotEntities == svs.numSnapshotEntities-1)
	{	//we're full, and about to wrap around and stomp ents, so half the time send the first set without stomping.
#ifdef _XBOX
		if (xboxLogMissile)
		{
			XBLF("JA: SV_AddEntToSnapshot missile ring-full ent=%d weapon=%d num=%d ring=%d",
				gEnt->s.number,
				gEnt->s.weapon,
				eNums->numSnapshotEntities,
				svs.numSnapshotEntities);
			--s_xboxAddMissileBudget;
		}
#if defined(STEFX_ELITE_FORCE_SP)
		if (stefxLogEvent)
		{
			XBLF("STEFX: SV_AddEntToSnapshot event ring-full ent=%d eType=%d weapon=%d num=%d ring=%d",
				gEnt->s.number,
				gEnt->s.eType,
				gEnt->s.weapon,
				eNums->numSnapshotEntities,
				svs.numSnapshotEntities);
			--s_stefxAddEventBudget;
		}
		if (stefxLogBrushMover)
		{
			XBLF("STEFX: SV_AddEntToSnapshot bmodel ring-full ent=%d model=%d eType=%d solid=0x%x sv=0x%x eFlags=0x%x num=%d ring=%d",
				gEnt->s.number,
				gEnt->s.modelindex,
				gEnt->s.eType,
				gEnt->s.solid,
				gEnt->svFlags,
				gEnt->s.eFlags,
				eNums->numSnapshotEntities,
				svs.numSnapshotEntities);
			--s_stefxAddBrushMoverBudget;
		}
#endif
#endif
		return;
	}

	eNums->snapshotEntities[ eNums->numSnapshotEntities ] = gEnt->s.number;
	eNums->numSnapshotEntities++;
#ifdef _XBOX
	if (xboxLogMissile)
	{
		XBLF("JA: SV_AddEntToSnapshot missile add ent=%d weapon=%d snapshotIndex=%d sv=0x%x area=%d/%d clusters=%d last=%d",
			gEnt->s.number,
			gEnt->s.weapon,
			eNums->numSnapshotEntities - 1,
			gEnt->svFlags,
			svEnt->areanum,
			svEnt->areanum2,
			svEnt->numClusters,
			svEnt->lastCluster);
		--s_xboxAddMissileBudget;
	}
#if defined(STEFX_ELITE_FORCE_SP)
	if (stefxLogEvent)
	{
		XBLF("STEFX: SV_AddEntToSnapshot event add ent=%d eType=%d weapon=%d snapshotIndex=%d sv=0x%x area=%d/%d clusters=%d last=%d origin=(%g,%g,%g) origin2=(%g,%g,%g)",
			gEnt->s.number,
			gEnt->s.eType,
			gEnt->s.weapon,
			eNums->numSnapshotEntities - 1,
			gEnt->svFlags,
			svEnt->areanum,
			svEnt->areanum2,
			svEnt->numClusters,
			svEnt->lastCluster,
			gEnt->currentOrigin[0], gEnt->currentOrigin[1], gEnt->currentOrigin[2],
			gEnt->s.origin2[0], gEnt->s.origin2[1], gEnt->s.origin2[2]);
		--s_stefxAddEventBudget;
	}
	if (stefxLogBrushMover)
	{
		XBLF("STEFX: SV_AddEntToSnapshot bmodel add ent=%d model=%d eType=%d solid=0x%x snapshotIndex=%d sv=0x%x eFlags=0x%x area=%d/%d clusters=%d last=%d origin=(%g,%g,%g)",
			gEnt->s.number,
			gEnt->s.modelindex,
			gEnt->s.eType,
			gEnt->s.solid,
			eNums->numSnapshotEntities - 1,
			gEnt->svFlags,
			gEnt->s.eFlags,
			svEnt->areanum,
			svEnt->areanum2,
			svEnt->numClusters,
			svEnt->lastCluster,
			gEnt->s.pos.trBase[0], gEnt->s.pos.trBase[1], gEnt->s.pos.trBase[2]);
		--s_stefxAddBrushMoverBudget;
	}
#endif
#endif
}

//rww - bg_public.h won't cooperate in here
#define EF_PERMANENT			0x00080000

#ifdef _XBOX
static qboolean s_xboxSnapshotCameraView = qfalse;
#endif

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
static qboolean SV_STEFX_IsBrushMoverState( const entityState_t *state )
{
	if ( !state || (state->eFlags & EF_NODRAW) )
	{
		return qfalse;
	}
	return ( state->eType == ET_MOVER && state->solid == SOLID_BMODEL ) ? qtrue : qfalse;
}

static qboolean SV_STEFX_IsBroadcastBrushMover( const gentity_t *ent )
{
	if ( !ent || (ent->svFlags & SVF_NOCLIENT) || (ent->s.eFlags & EF_NODRAW) )
	{
		return qfalse;
	}
	if ( ent->s.eType != ET_MOVER || ent->s.solid != SOLID_BMODEL )
	{
		return qfalse;
	}
	return (ent->svFlags & SVF_BROADCAST) ? qtrue : qfalse;
}

static qboolean SV_STEFX_IsPlayerEntity( const gentity_t *ent )
{
	return ( ent && ent->s.eType == ET_PLAYER );
}

enum stefxRemoteSnapshotDecision_t
{
	STEFX_SNAP_REJECT_PERMANENT = 0,
	STEFX_SNAP_REJECT_UNLINKED,
	STEFX_SNAP_REJECT_NOCLIENT,
	STEFX_SNAP_SKIP_DUPLICATE,
	STEFX_SNAP_SENT_BYPASS,
	STEFX_SNAP_SENT_BROADCAST,
	STEFX_SNAP_REJECT_AREA,
	STEFX_SNAP_REJECT_NOCLUSTERS,
	STEFX_SNAP_REJECT_PVS_OVERFLOW,
	STEFX_SNAP_REJECT_PVS,
	STEFX_SNAP_SENT_PVS
};

static void SV_STEFX_LogRemoteSnapshotDecision( int decision, const char *phase,
	int entNum, const gentity_t *ent, const svEntity_t *svEnt,
	const vec3_t viewOrigin, int viewerClient, int clientarea, int clientcluster, qboolean portal )
{
	static int s_lastDecision[3] = { -1, -1, -1 };
	static int s_lastLogTime[3] = { -1, -1, -1 };
	static int s_lastClientArea[3] = { -999, -999, -999 };
	static int s_lastClientCluster[3] = { -999, -999, -999 };
	static int s_lastEntityArea[3] = { -999, -999, -999 };
	static int s_lastEntityCluster[3] = { -999, -999, -999 };
	static int s_lastLiveArea[3] = { -999, -999, -999 };
	static int s_lastLiveCluster[3] = { -999, -999, -999 };
	int now;
	int entityArea;
	int entityCluster;
	int liveLeaf;
	int liveArea;
	int liveCluster;
	int firstCluster;
	int mapClusterCount;
	int cachedPvsVisible;
	int livePvsVisible;
	int clusterIndex;
	const byte *diagnosticPvs;
	qboolean changed;
	trace_t worldTrace;
	vec3_t bodyCenter;
	const playerState_t *ps;

	if ( portal || viewerClient != 0 || entNum < 1 || entNum > 2 || !ent || !sv_mapname ||
		Q_stricmp( sv_mapname->string, "hm_borg1" ) )
	{
		return;
	}

	now = sv.time;
	entityArea = svEnt ? svEnt->areanum : -999;
	firstCluster = (svEnt && svEnt->numClusters > 0) ? svEnt->clusternums[0] : -999;
	entityCluster = firstCluster;
	liveLeaf = CM_PointLeafnum( ent->currentOrigin );
	liveArea = CM_LeafArea( liveLeaf );
	liveCluster = CM_LeafCluster( liveLeaf );
	mapClusterCount = CM_NumClusters();
	diagnosticPvs = (clientcluster >= 0 && clientcluster < mapClusterCount) ?
		CM_ClusterPVS( clientcluster ) : NULL;
	cachedPvsVisible = 0;
	if ( diagnosticPvs && svEnt )
	{
		for ( clusterIndex = 0; clusterIndex < svEnt->numClusters; ++clusterIndex )
		{
			const int cachedCluster = svEnt->clusternums[clusterIndex];
			if ( cachedCluster >= 0 && cachedCluster < mapClusterCount &&
				(diagnosticPvs[cachedCluster >> 3] & (1 << (cachedCluster & 7))) )
			{
				cachedPvsVisible = 1;
				break;
			}
		}
	}
	livePvsVisible = (diagnosticPvs && liveCluster >= 0 && liveCluster < mapClusterCount &&
		(diagnosticPvs[liveCluster >> 3] & (1 << (liveCluster & 7)))) ? 1 : 0;
	changed = (s_lastDecision[entNum] != decision ||
		s_lastClientArea[entNum] != clientarea ||
		s_lastClientCluster[entNum] != clientcluster ||
		s_lastEntityArea[entNum] != entityArea ||
		s_lastEntityCluster[entNum] != entityCluster ||
		s_lastLiveArea[entNum] != liveArea ||
		s_lastLiveCluster[entNum] != liveCluster);
	if ( !changed && s_lastLogTime[entNum] >= 0 && now >= s_lastLogTime[entNum] &&
		now - s_lastLogTime[entNum] < 10000 )
	{
		return;
	}

	VectorCopy( ent->currentOrigin, bodyCenter );
	bodyCenter[2] += 24.0f;
	CM_BoxTrace( &worldTrace, viewOrigin, bodyCenter, NULL, NULL, 0, CONTENTS_SOLID );
	ps = ent->client;
	XBLog_WriteCriticalf("STEFX_SNAP decision=%s code=%d time=%d snap=%d viewer=%d ent=%d "
		"linked=%d inuse=%d sv=0x%x clientArea=%d clientCluster=%d "
		"cachedArea=%d/%d cachedClusters=%d first=%d last=%d cachedPvs=%d "
		"liveLeaf=%d liveArea=%d liveCluster=%d livePvs=%d mapClusters=%d los=%g "
		"view=(%g,%g,%g) current=(%g,%g,%g) ps=(%g,%g,%g)",
		phase ? phase : "unknown", decision, now, sv.snapshotCounter, viewerClient, entNum,
		(int)ent->linked, (int)ent->inuse, ent->svFlags,
		clientarea, clientcluster,
		svEnt ? svEnt->areanum : -999,
		svEnt ? svEnt->areanum2 : -999,
		svEnt ? svEnt->numClusters : -999,
		firstCluster,
		svEnt ? svEnt->lastCluster : -999,
		cachedPvsVisible,
		liveLeaf, liveArea, liveCluster, livePvsVisible, mapClusterCount, worldTrace.fraction,
		viewOrigin[0], viewOrigin[1], viewOrigin[2],
		ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2],
		ps ? ps->origin[0] : 0.0f,
		ps ? ps->origin[1] : 0.0f,
		ps ? ps->origin[2] : 0.0f);

	s_lastDecision[entNum] = decision;
	s_lastLogTime[entNum] = now;
	s_lastClientArea[entNum] = clientarea;
	s_lastClientCluster[entNum] = clientcluster;
	s_lastEntityArea[entNum] = entityArea;
	s_lastEntityCluster[entNum] = entityCluster;
	s_lastLiveArea[entNum] = liveArea;
	s_lastLiveCluster[entNum] = liveCluster;
}

static void SV_STEFX_LogSnapshotPlayer( const char *phase, int entNum, const gentity_t *ent,
										const svEntity_t *svEnt, int clientarea, int clientcluster,
										const snapshotEntityNumbers_t *eNums )
{
	static int s_stefxSnapshotPlayerBudget = 640;

	if ( !SV_STEFX_IsPlayerEntity( ent ) || s_stefxSnapshotPlayerBudget <= 0 )
	{
		return;
	}

	XBLF("STEFX: SV_SNAPSHOT_PLAYER %s ent=%d clientNum=%d linked=%d inuse=%d sv=0x%x eFlags=0x%x solid=0x%x client=%p weapon=%d model=%d model2=%d area=%d/%d clusters=%d last=%d clientArea=%d clientCluster=%d snapCount=%d origin=(%g,%g,%g) current=(%g,%g,%g) mins=(%g,%g,%g) maxs=(%g,%g,%g)",
		phase ? phase : "<null>",
		entNum,
		ent->s.clientNum,
		(int)ent->linked,
		(int)ent->inuse,
		ent->svFlags,
		ent->s.eFlags,
		ent->s.solid,
		ent->client,
		ent->s.weapon,
		ent->s.modelindex,
		ent->s.modelindex2,
		svEnt ? svEnt->areanum : -1,
		svEnt ? svEnt->areanum2 : -1,
		svEnt ? svEnt->numClusters : -1,
		svEnt ? svEnt->lastCluster : -1,
		clientarea,
		clientcluster,
		eNums ? eNums->numSnapshotEntities : -1,
		ent->s.origin[0], ent->s.origin[1], ent->s.origin[2],
		ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2],
		ent->mins[0], ent->mins[1], ent->mins[2],
		ent->maxs[0], ent->maxs[1], ent->maxs[2]);
	--s_stefxSnapshotPlayerBudget;
}

static qboolean SV_STEFX_IsActorCandidate( const gentity_t *ent )
{
	return ( ent && ( ent->client || ( ent->svFlags & SVF_NPC ) || ent->s.eType == ET_PLAYER ) );
}

static void SV_STEFX_LogSnapshotActor( const char *phase, int entNum, const gentity_t *ent,
										const svEntity_t *svEnt, int clientarea, int clientcluster,
										const snapshotEntityNumbers_t *eNums )
{
	static int s_stefxSnapshotActorBudget = 768;

	if ( !SV_STEFX_IsActorCandidate( ent ) || s_stefxSnapshotActorBudget <= 0 )
	{
		return;
	}

	XBLF("STEFX: SV_ACTOR_SCAN %s ent=%d clientNum=%d linked=%d inuse=%d sv=0x%x eType=%d eFlags=0x%x solid=0x%x client=%p weapon=%d model=%d model2=%d area=%d/%d clusters=%d last=%d clientArea=%d clientCluster=%d snapCount=%d origin=(%g,%g,%g) current=(%g,%g,%g) mins=(%g,%g,%g) maxs=(%g,%g,%g)",
		phase ? phase : "<null>",
		entNum,
		ent->s.clientNum,
		(int)ent->linked,
		(int)ent->inuse,
		ent->svFlags,
		ent->s.eType,
		ent->s.eFlags,
		ent->s.solid,
		ent->client,
		ent->s.weapon,
		ent->s.modelindex,
		ent->s.modelindex2,
		svEnt ? svEnt->areanum : -1,
		svEnt ? svEnt->areanum2 : -1,
		svEnt ? svEnt->numClusters : -1,
		svEnt ? svEnt->lastCluster : -1,
		clientarea,
		clientcluster,
		eNums ? eNums->numSnapshotEntities : -1,
		ent->s.origin[0], ent->s.origin[1], ent->s.origin[2],
		ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2],
		ent->mins[0], ent->mins[1], ent->mins[2],
		ent->maxs[0], ent->maxs[1], ent->maxs[2]);
	--s_stefxSnapshotActorBudget;
}

static qboolean SV_STEFX_Borg1RawBspSnapshotBypass( const gentity_t *ent, int entNum, const vec3_t origin,
													int *actorBudget, int *itemBudget,
													int *missileBudget, int *eventBudget, int *modelBudget,
													const char **reason ) {
	static int corpseSkipLogBudget = 12;
	float dx, dy, dz, distSq;

	if ( !sv_mapname || Q_stricmp( sv_mapname->string, "borg1" ) ) {
		return qfalse;
	}
	if ( !ent || entNum <= 0 ) {
		return qfalse;
	}

	if ( ent->s.eType == ET_PLAYER ) {
		if ( !ent->client || ent->contents == CONTENTS_CORPSE || (ent->s.eFlags & 0x00000001) ) {
			if ( corpseSkipLogBudget > 0 ) {
				XBLF("STEFX: borg1 snapshot bypass skip corpse/player-shell ent=%d client=%p contents=0x%x eFlags=0x%x sv=0x%x origin=(%g,%g,%g)",
					entNum,
					ent->client,
					ent->contents,
					ent->s.eFlags,
					ent->svFlags,
					ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2]);
				--corpseSkipLogBudget;
			}
			return qfalse;
		}
		if ( ent->currentOrigin[2] < origin[2] - 160.0f ) {
			if ( corpseSkipLogBudget > 0 ) {
				XBLF("STEFX: borg1 snapshot bypass skip actor-below-view ent=%d client=%p sv=0x%x viewZ=%g actorZ=%g origin=(%g,%g,%g)",
					entNum,
					ent->client,
					ent->svFlags,
					origin[2],
					ent->currentOrigin[2],
					ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2]);
				--corpseSkipLogBudget;
			}
			return qfalse;
		}
		if ( *actorBudget <= 0 ) {
			return qfalse;
		}
		(*actorBudget)--;
		*reason = "actor";
		return qtrue;
	}

	if ( ent->s.eType == ET_MISSILE ) {
		if ( *missileBudget <= 0 ) {
			return qfalse;
		}
		(*missileBudget)--;
		*reason = "missile";
		return qtrue;
	}

	if ( ent->s.eType > ET_EVENTS ) {
		if ( *eventBudget <= 0 ) {
			return qfalse;
		}
		(*eventBudget)--;
		*reason = "event";
		return qtrue;
	}

	if ( ent->s.eType == ET_ITEM ) {
		if ( *itemBudget <= 0 ) {
			return qfalse;
		}
		(*itemBudget)--;
		*reason = "item";
		return qtrue;
	}

	if ( !ent->s.modelindex && !ent->s.modelindex2 ) {
		return qfalse;
	}
	if ( ent->s.eType != ET_GENERAL && ent->s.eType != ET_MOVER && ent->s.eType != ET_BEAM ) {
		return qfalse;
	}
	if ( *modelBudget <= 0 ) {
		return qfalse;
	}

	dx = ent->currentOrigin[0] - origin[0];
	dy = ent->currentOrigin[1] - origin[1];
	dz = ent->currentOrigin[2] - origin[2];
	distSq = dx * dx + dy * dy + dz * dz;

	if ( distSq > (2200.0f * 2200.0f) && entNum > 180 ) {
		return qfalse;
	}

	(*modelBudget)--;
	*reason = "model";
	return qtrue;
}
#endif

float sv_sightRangeForLevel[6] =
{
	0,//FORCE_LEVEL_0
    1024.f, //FORCE_LEVEL_1
	2048.0f,//FORCE_LEVEL_2
	4096.0f,//FORCE_LEVEL_3
	4096.0f,//FORCE_LEVEL_4
	4096.0f//FORCE_LEVEL_5
};

qboolean SV_PlayerCanSeeEnt( gentity_t *ent, int sightLevel )
{//return true if this ent is in view
	//NOTE: this is similar to the func CG_PlayerCanSeeCent in cg_players
	vec3_t viewOrg, viewAngles, viewFwd, dir2Ent;
	if ( !ent )
	{
		return qfalse;
	}
#ifdef _XBOX
	if (Sys_IsDirectMapBoot())
	{
		return qfalse;
	}
#endif
	if ( VM_Call( CG_CAMERA_POS, viewOrg))
	{
		if ( VM_Call( CG_CAMERA_ANG, viewAngles))
		{
			float dot = 0.25f;//1.0f;
			float range = sv_sightRangeForLevel[sightLevel];

			VectorSubtract( ent->currentOrigin, viewOrg, dir2Ent );
			float entDist = VectorNormalize( dir2Ent );

			if ( (ent->s.eFlags&EF_FORCE_VISIBLE) )
			{//no dist check on them?
			}
			else
			{
				if ( entDist < 128.0f )
				{//can always see them if they're really close
					return qtrue;
				}

				if ( entDist > range )
				{//too far away to see them
					return qfalse;
				}
			}

			dot += (0.99f-dot)*entDist/range;//the farther away they are, the more in front they have to be

			AngleVectors( viewAngles, viewFwd, NULL, NULL );
			if ( DotProduct( viewFwd, dir2Ent ) < dot )
			{
				return qfalse;
			}
			return qtrue;
		}
	}
	return qfalse;
}
/*
===============
SV_AddEntitiesVisibleFromPoint
===============
*/
#ifdef _XBOX
typedef struct xboxMoverFocusStats_s {
	int candidate;
	int sent;
	int skippedSnapshot;
	int areaReject;
	int pvsReject;
	int noClusters;
	int unlinked;
	int noClient;
	int broadcastSent;
	int portalSent;
	int sightSent;
	int lastEnt;
	int lastArea;
	int lastArea2;
	int lastClientArea;
	int lastClientCluster;
} xboxMoverFocusStats_t;

static const int s_xboxMoverFocusModels[] = {
	139, 140, 141, 142, 143, 144, 145, 146,
	147, 148, 149, 150, 151, 152,
	172, 193, 197, 202, 203
};

static xboxMoverFocusStats_t s_xboxMoverFocusStats[sizeof(s_xboxMoverFocusModels) / sizeof(s_xboxMoverFocusModels[0])];
static int s_xboxMoverFocusLastPrintTime = 0;
static int s_xboxMoverFocusPrintBudget = 0;

static int XboxMoverFocusIndex( int modelindex )
{
	int i;
	for ( i = 0; i < (int)(sizeof(s_xboxMoverFocusModels) / sizeof(s_xboxMoverFocusModels[0])); i++ )
	{
		if ( s_xboxMoverFocusModels[i] == modelindex )
		{
			return i;
		}
	}
	return -1;
}

static void XboxMoverFocusRecord( int idx, gentity_t *ent, svEntity_t *svEnt, int clientarea, int clientcluster, int fieldOffset )
{
	xboxMoverFocusStats_t *stats;
	if ( idx < 0 )
	{
		return;
	}
	stats = &s_xboxMoverFocusStats[idx];
	*((int *)stats + fieldOffset) += 1;
	stats->lastEnt = ent ? ent->s.number : -1;
	stats->lastArea = svEnt ? svEnt->areanum : -999;
	stats->lastArea2 = svEnt ? svEnt->areanum2 : -999;
	stats->lastClientArea = clientarea;
	stats->lastClientCluster = clientcluster;
}

#define XBOX_MOVER_STAT_FIELD(field) ((int)(&((xboxMoverFocusStats_t *)0)->field) / (int)sizeof(int))

static void XboxMoverFocusMaybePrintSummary( int clientarea, int clientcluster )
{
	int i;
	if ( s_xboxMoverFocusPrintBudget <= 0 )
	{
		return;
	}
	if ( sv.time - s_xboxMoverFocusLastPrintTime < 1000 )
	{
		return;
	}
	s_xboxMoverFocusLastPrintTime = sv.time;
	s_xboxMoverFocusPrintBudget--;
	for ( i = 0; i < (int)(sizeof(s_xboxMoverFocusModels) / sizeof(s_xboxMoverFocusModels[0])); i++ )
	{
		xboxMoverFocusStats_t *stats = &s_xboxMoverFocusStats[i];
		if ( stats->candidate || stats->sent || stats->areaReject || stats->pvsReject || stats->noClusters )
		{
			XBLF("JA: SV_MOVER_MODEL_SUMMARY time=%d model=%d cand=%d sent=%d area=%d pvs=%d noClusters=%d unlinked=%d noClient=%d snapSkip=%d broadcast=%d portal=%d sight=%d lastEnt=%d entArea=%d/%d clientArea=%d/%d clientCluster=%d/%d",
				sv.time,
				s_xboxMoverFocusModels[i],
				stats->candidate,
				stats->sent,
				stats->areaReject,
				stats->pvsReject,
				stats->noClusters,
				stats->unlinked,
				stats->noClient,
				stats->skippedSnapshot,
				stats->broadcastSent,
				stats->portalSent,
				stats->sightSent,
				stats->lastEnt,
				stats->lastArea,
				stats->lastArea2,
				clientarea,
				stats->lastClientArea,
				clientcluster,
				stats->lastClientCluster);
		}
	}
}
#endif

static void SV_AddEntitiesVisibleFromPoint( vec3_t origin, clientSnapshot_t *frame, 
									snapshotEntityNumbers_t *eNums, qboolean portal ) {
	int		e, i;
	gentity_t	*ent;
	svEntity_t	*svEnt;
	int		l;
	int		clientarea, clientcluster;
	int		leafnum;
	int		c_fullsend;
	const byte *clientpvs;
	const byte *bitvector;
	qboolean sightOn = qfalse;
#ifdef _XBOX
	static int s_xboxSnapshotMoverFrameBudget = 0;
	static int s_xboxSnapshotMoverDetailBudget = 0;
	static int s_xboxSnapshotMoverFocusBudget = 0;
	static int s_xboxSnapshotMissileBudget = 160;
	static int s_xboxYavinSnapshotFocusBudget = 220;
	static int s_xboxYavinCinematicActorBudget = 80;
	int xboxMoverTotal = 0;
	int xboxMoverSent = 0;
	int xboxMoverUnlinked = 0;
	int xboxMoverNoClient = 0;
	int xboxMoverAreaRejected = 0;
	int xboxMoverPvsRejected = 0;
	int xboxMoverNoClusters = 0;
	qboolean xboxTraceMovers = (s_xboxSnapshotMoverFrameBudget > 0 && !portal);
	int xboxFocusIndex = -1;
	qboolean xboxYavinFocusEnt = qfalse;
	static int s_xboxVisibleLogBudget = 0;
	const qboolean xboxTraceVisible = (!portal && s_xboxVisibleLogBudget > 0);
#if defined(STEFX_ELITE_FORCE_SP)
	int stefxBorg1ActorBudget = 96;
	int stefxBorg1ItemBudget = 48;
	int stefxBorg1MissileBudget = 64;
	int stefxBorg1EventBudget = 96;
	int stefxBorg1ModelBudget = 128;
	int stefxBorg1BypassSent = 0;
	static int s_stefxBorg1BypassLogBudget = 32;
	static int s_stefxSnapshotEventScanBudget = 256;
#if defined(STEFX_SP_HOSTED_MP)
	static int s_stefxEventRouteLogBudget = 32;
#endif
#endif
	if (xboxTraceVisible)
	{
		Com_PrintfAlways("JA: SV_AddEntitiesVisibleFromPoint enter portal=%d org=%g,%g,%g ge=%d\n",
			(int)portal, origin[0], origin[1], origin[2], ge ? ge->num_entities : -1);
	}
#endif

	// during an error shutdown message we may need to transmit
	// the shutdown message after the server has shutdown, so
	// specfically check for it
	if ( !sv.state ) {
#ifdef _XBOX
		if (xboxTraceVisible)
		{
			Com_PrintfAlways("JA: SV_AddEntitiesVisibleFromPoint exit no-sv-state\n");
			--s_xboxVisibleLogBudget;
		}
#endif
		return;
	}

	leafnum = CM_PointLeafnum (origin);
	clientarea = CM_LeafArea (leafnum);
	clientcluster = CM_LeafCluster (leafnum);
#ifdef _XBOX
	if (xboxTraceVisible)
	{
		Com_PrintfAlways("JA: SV_AddEntitiesVisibleFromPoint leaf=%d area=%d cluster=%d\n",
			leafnum, clientarea, clientcluster);
	}
#endif

	// calculate the visible areas
	frame->areabytes = CM_WriteAreaBits( frame->areabits, clientarea );

	clientpvs = CM_ClusterPVS (clientcluster);
#ifdef _XBOX
	if (xboxTraceVisible)
	{
		Com_PrintfAlways("JA: SV_AddEntitiesVisibleFromPoint pvs ready areaBytes=%d\n",
			frame->areabytes);
	}
#endif

	c_fullsend = 0;

	if ( !portal )
	{//not if this if through a portal...???  James said to do this...
#if !defined(STEFX_ELITE_FORCE_SP)
		if ( (frame->ps.forcePowersActive&(1<<FP_SEE)) )
		{
			sightOn = qtrue;
		}
#endif
	}

	for ( e = 0 ; e < ge->num_entities ; e++ ) {
#ifdef _XBOX
		if (xboxTraceVisible && (e == 0 || e == 64 || e == 128 || e == 192 || e == ge->num_entities - 1))
		{
			Com_PrintfAlways("JA: SV_AddEntitiesVisibleFromPoint scan e=%d count=%d\n",
				e, eNums ? eNums->numSnapshotEntities : -1);
		}
#endif
		ent = SV_GentityNum(e);
#ifdef _XBOX
		qboolean xboxIsMover = qfalse;
		qboolean xboxFocusMover = qfalse;
		qboolean xboxIsMissile = qfalse;
		qboolean xboxLogMissile = qfalse;
#if defined(STEFX_ELITE_FORCE_SP)
		qboolean stefxSnapshotEvent = qfalse;
		qboolean stefxLogSnapshotEvent = qfalse;
		qboolean stefxSnapshotPlayer = qfalse;
		qboolean stefxSnapshotActor = qfalse;
#endif
#endif

		if (!ent->inuse) {
			continue;
		}
#ifdef _XBOX
		xboxIsMover = (ent->s.eType == ET_MOVER);
		xboxIsMissile = (ent->s.eType == ET_MISSILE);
		xboxYavinFocusEnt = (!Q_stricmp(sv_mapname->string, "yavin1") && e >= 48 && e <= 60 && s_xboxYavinSnapshotFocusBudget > 0);
		xboxLogMissile = (xboxIsMissile && !portal && s_xboxSnapshotMissileBudget > 0);
#if defined(STEFX_ELITE_FORCE_SP)
		stefxSnapshotActor = (!portal && SV_STEFX_IsActorCandidate( ent ));
		if (stefxSnapshotActor)
		{
			SV_STEFX_LogSnapshotActor( "candidate", e, ent, NULL, clientarea, clientcluster, eNums );
		}
		stefxSnapshotPlayer = (!portal && SV_STEFX_IsPlayerEntity( ent ));
		if (stefxSnapshotPlayer)
		{
			SV_STEFX_LogSnapshotPlayer( "candidate", e, ent, NULL, clientarea, clientcluster, eNums );
		}
		stefxSnapshotEvent = (sv_mapname && !Q_stricmp(sv_mapname->string, "borg1") && ent->s.eType > ET_EVENTS);
		stefxLogSnapshotEvent = (stefxSnapshotEvent && !portal && s_stefxSnapshotEventScanBudget > 0);
		if (stefxLogSnapshotEvent)
		{
			XBLF("STEFX: SV_EVENT candidate ent=%d linked=%d inuse=%d eType=%d sv=0x%x model=%d weapon=%d origin=(%g,%g,%g) current=(%g,%g,%g) origin2=(%g,%g,%g)",
				e,
				(int)ent->linked,
				(int)ent->inuse,
				ent->s.eType,
				ent->svFlags,
				ent->s.modelindex,
				ent->s.weapon,
				ent->s.origin[0], ent->s.origin[1], ent->s.origin[2],
				ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2],
				ent->s.origin2[0], ent->s.origin2[1], ent->s.origin2[2]);
			--s_stefxSnapshotEventScanBudget;
		}
#endif
		if (xboxYavinFocusEnt)
		{
			XBLF("JA: SV_YAVIN_SNAPSHOT candidate pass=%s ent=%d linked=%d inuse=%d sv=0x%x eType=%d model=%d origin=%g,%g,%g current=%g,%g,%g",
				portal ? "extra" : "main",
				e,
				(int)ent->linked,
				(int)ent->inuse,
				ent->svFlags,
				ent->s.eType,
				ent->s.modelindex,
				ent->s.origin[0], ent->s.origin[1], ent->s.origin[2],
				ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2]);
			--s_xboxYavinSnapshotFocusBudget;
		}
		if (xboxIsMover && xboxTraceMovers)
		{
			xboxMoverTotal++;
			if (s_xboxSnapshotMoverDetailBudget > 0)
			{
				XBLF("JA: SV_SNAPSHOT_MOVER candidate ent=%d linked=%d bmodel=%d svFlags=0x%x eFlags=0x%x solid=%d model=%d model2=%d contents=0x%x origin=%g,%g,%g",
					e,
					(int)ent->linked,
					(int)ent->bmodel,
					ent->svFlags,
					ent->s.eFlags,
					ent->s.solid,
					ent->s.modelindex,
					ent->s.modelindex2,
					ent->contents,
					ent->s.origin[0], ent->s.origin[1], ent->s.origin[2]);
				s_xboxSnapshotMoverDetailBudget--;
			}
		}
#endif

		if (ent->s.eFlags & EF_PERMANENT)
		{	// he's permanent, so don't send him down!
#ifdef _XBOX
			if (xboxYavinFocusEnt)
			{
				XBLF("JA: SV_YAVIN_SNAPSHOT reject-permanent pass=%s ent=%d eFlags=0x%x",
					portal ? "extra" : "main", e, ent->s.eFlags);
			}
#if defined(STEFX_ELITE_FORCE_SP)
			if (stefxSnapshotPlayer)
			{
				SV_STEFX_LogSnapshotPlayer( "reject-permanent", e, ent, NULL, clientarea, clientcluster, eNums );
			}
			if (stefxSnapshotActor)
			{
				SV_STEFX_LogSnapshotActor( "reject-permanent", e, ent, NULL, clientarea, clientcluster, eNums );
			}
			SV_STEFX_LogRemoteSnapshotDecision( STEFX_SNAP_REJECT_PERMANENT,
				"reject-permanent", e, ent, NULL, origin, frame->ps.clientNum, clientarea, clientcluster, portal );
			if (stefxLogSnapshotEvent)
			{
				XBLF("STEFX: SV_EVENT reject-permanent ent=%d eType=%d eFlags=0x%x",
					e, ent->s.eType, ent->s.eFlags);
			}
#endif
#endif
			continue;
		}

		if (ent->s.number != e) {
			Com_DPrintf ("FIXING ENT->S.NUMBER!!!\n");
			ent->s.number = e;
		}

		// never send entities that aren't linked in
		if ( !ent->linked ) {
#ifdef _XBOX
			if (xboxYavinFocusEnt)
			{
				XBLF("JA: SV_YAVIN_SNAPSHOT reject-unlinked pass=%s ent=%d",
					portal ? "extra" : "main", e);
			}
			if (xboxIsMover && xboxTraceMovers) xboxMoverUnlinked++;
			if (xboxLogMissile)
			{
				XBLF("JA: SV_SNAPSHOT_MISSILE reject-unlinked ent=%d weapon=%d sv=0x%x origin=%g,%g,%g",
					e, ent->s.weapon, ent->svFlags,
					ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2]);
				--s_xboxSnapshotMissileBudget;
			}
#if defined(STEFX_ELITE_FORCE_SP)
			if (stefxSnapshotPlayer)
			{
				SV_STEFX_LogSnapshotPlayer( "reject-unlinked", e, ent, NULL, clientarea, clientcluster, eNums );
			}
			if (stefxSnapshotActor)
			{
				SV_STEFX_LogSnapshotActor( "reject-unlinked", e, ent, NULL, clientarea, clientcluster, eNums );
			}
			SV_STEFX_LogRemoteSnapshotDecision( STEFX_SNAP_REJECT_UNLINKED,
				"reject-unlinked", e, ent, NULL, origin, frame->ps.clientNum, clientarea, clientcluster, portal );
			if (stefxLogSnapshotEvent)
			{
				XBLF("STEFX: SV_EVENT reject-unlinked ent=%d eType=%d sv=0x%x current=(%g,%g,%g)",
					e,
					ent->s.eType,
					ent->svFlags,
					ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2]);
			}
#endif
			xboxFocusIndex = XboxMoverFocusIndex( ent->s.modelindex );
			if (xboxFocusIndex >= 0 && !portal)
			{
				XboxMoverFocusRecord( xboxFocusIndex, ent, NULL, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(unlinked) );
			}
#endif
			continue;
		}

		// entities can be flagged to explicitly not be sent to the client
		if ( ent->svFlags & SVF_NOCLIENT ) {
#ifdef _XBOX
			if (xboxYavinFocusEnt)
			{
				XBLF("JA: SV_YAVIN_SNAPSHOT reject-noclient pass=%s ent=%d sv=0x%x",
					portal ? "extra" : "main", e, ent->svFlags);
			}
			if (xboxIsMover && xboxTraceMovers) xboxMoverNoClient++;
			if (xboxLogMissile)
			{
				XBLF("JA: SV_SNAPSHOT_MISSILE reject-noclient ent=%d weapon=%d sv=0x%x",
					e, ent->s.weapon, ent->svFlags);
				--s_xboxSnapshotMissileBudget;
			}
#if defined(STEFX_ELITE_FORCE_SP)
			if (stefxSnapshotPlayer)
			{
				SV_STEFX_LogSnapshotPlayer( "reject-noclient", e, ent, NULL, clientarea, clientcluster, eNums );
			}
			if (stefxSnapshotActor)
			{
				SV_STEFX_LogSnapshotActor( "reject-noclient", e, ent, NULL, clientarea, clientcluster, eNums );
			}
			SV_STEFX_LogRemoteSnapshotDecision( STEFX_SNAP_REJECT_NOCLIENT,
				"reject-noclient", e, ent, NULL, origin, frame->ps.clientNum, clientarea, clientcluster, portal );
			if (stefxLogSnapshotEvent)
			{
				XBLF("STEFX: SV_EVENT reject-noclient ent=%d eType=%d sv=0x%x",
					e, ent->s.eType, ent->svFlags);
			}
#endif
			xboxFocusIndex = XboxMoverFocusIndex( ent->s.modelindex );
			if (xboxFocusIndex >= 0 && !portal)
			{
				XboxMoverFocusRecord( xboxFocusIndex, ent, SV_SvEntityForGentity( ent ), clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(noClient) );
			}
#endif
			continue;
		}

#if defined(STEFX_SP_HOSTED_MP)
		// The SP entity ABI normally has no per-client routing. Holomatch uses
		// it so predicted events are not echoed back to their originating client.
		if ( ( ( ent->svFlags & SVF_SINGLECLIENT ) &&
			   ent->singleClient != frame->ps.clientNum ) ||
			 ( ( ent->svFlags & SVF_NOTSINGLECLIENT ) &&
			   ent->singleClient == frame->ps.clientNum ) )
		{
			if ( ent->s.eType > ET_EVENTS && s_stefxEventRouteLogBudget > 0 )
			{
				XBLog_WriteCriticalf("STEFX_HM_EVENT_ROUTE: reject viewer=%d source=%d ent=%d flags=0x%x single=%d eType=%d",
					frame->ps.clientNum,
					ent->s.clientNum,
					e,
					ent->svFlags,
					ent->singleClient,
					ent->s.eType);
				--s_stefxEventRouteLogBudget;
			}
			continue;
		}
#endif

		svEnt = SV_SvEntityForGentity( ent );
#ifdef _XBOX
#if defined(STEFX_ELITE_FORCE_SP)
		if (stefxSnapshotActor)
		{
			SV_STEFX_LogSnapshotActor( "linked", e, ent, svEnt, clientarea, clientcluster, eNums );
		}
#endif
		if (xboxLogMissile)
		{
			XBLF("JA: SV_SNAPSHOT_MISSILE candidate ent=%d weapon=%d linked=%d sv=0x%x area=%d/%d clusters=%d last=%d clientArea=%d clientCluster=%d origin=%g,%g,%g",
				e,
				ent->s.weapon,
				(int)ent->linked,
				ent->svFlags,
				svEnt->areanum,
				svEnt->areanum2,
				svEnt->numClusters,
				svEnt->lastCluster,
				clientarea,
				clientcluster,
				ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2]);
			--s_xboxSnapshotMissileBudget;
		}
		xboxFocusIndex = XboxMoverFocusIndex( ent->s.modelindex );
		if (xboxFocusIndex >= 0 && !portal)
		{
			XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(candidate) );
		}
		xboxFocusMover = (xboxTraceMovers && xboxIsMover && s_xboxSnapshotMoverFocusBudget > 0 &&
			((ent->s.modelindex >= 139 && ent->s.modelindex <= 152) ||
			 ent->s.modelindex == 172 ||
			 ent->s.modelindex == 193 ||
			 ent->s.modelindex == 197 ||
			 ent->s.modelindex == 202 ||
			 ent->s.modelindex == 203));
		if (xboxFocusMover)
		{
			XBLF("JA: SV_MOVER_FOCUS candidate ent=%d model=%d flags=0x%x sv=0x%x area=%d/%d clusters=%d last=%d clientArea=%d clientCluster=%d",
				e,
				ent->s.modelindex,
				ent->s.eFlags,
				ent->svFlags,
				svEnt->areanum,
				svEnt->areanum2,
				svEnt->numClusters,
				svEnt->lastCluster,
				clientarea,
				clientcluster);
			s_xboxSnapshotMoverFocusBudget--;
		}
#endif

		// don't double add an entity through portals
		if ( svEnt->snapshotCounter == sv.snapshotCounter ) {
#ifdef _XBOX
			if (xboxYavinFocusEnt)
			{
				XBLF("JA: SV_YAVIN_SNAPSHOT skip-duplicate pass=%s ent=%d",
					portal ? "extra" : "main", e);
			}
			if (xboxLogMissile)
			{
				XBLF("JA: SV_SNAPSHOT_MISSILE skip-duplicate ent=%d weapon=%d", e, ent->s.weapon);
				--s_xboxSnapshotMissileBudget;
			}
#if defined(STEFX_ELITE_FORCE_SP)
			if (stefxLogSnapshotEvent)
			{
				XBLF("STEFX: SV_EVENT skip-duplicate ent=%d eType=%d sv=0x%x",
					e, ent->s.eType, ent->svFlags);
			}
			if (stefxSnapshotPlayer)
			{
				SV_STEFX_LogSnapshotPlayer( "skip-duplicate", e, ent, svEnt, clientarea, clientcluster, eNums );
			}
			if (stefxSnapshotActor)
			{
				SV_STEFX_LogSnapshotActor( "skip-duplicate", e, ent, svEnt, clientarea, clientcluster, eNums );
			}
			SV_STEFX_LogRemoteSnapshotDecision( STEFX_SNAP_SKIP_DUPLICATE,
				"skip-duplicate", e, ent, svEnt, origin, frame->ps.clientNum, clientarea, clientcluster, portal );
#endif
			if (xboxFocusIndex >= 0 && !portal)
			{
				XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(skippedSnapshot) );
			}
			if (xboxFocusMover)
			{
				XBLF("JA: SV_MOVER_FOCUS_SKIP_SNAPSHOT ent=%d model=%d", e, ent->s.modelindex);
			}
#endif
			continue;
		}

#ifdef _XBOX
		if (s_xboxSnapshotCameraView && !Q_stricmp(sv_mapname->string, "yavin1") && ent->s.eType == ET_PLAYER && e > 0)
		{
			SV_AddEntToSnapshot( svEnt, ent, eNums );
			if (s_xboxYavinCinematicActorBudget > 0)
			{
				XBLF("JA: SV_YAVIN_CINEMATIC_ACTOR_SENT ent=%d snapshot=%d sv=0x%x origin=%g,%g,%g current=%g,%g,%g",
					e,
					eNums->numSnapshotEntities - 1,
					ent->svFlags,
					ent->s.origin[0], ent->s.origin[1], ent->s.origin[2],
					ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2]);
				--s_xboxYavinCinematicActorBudget;
			}
			continue;
		}
#endif

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		if ( !portal )
		{
			const char *stefxBypassReason = NULL;
			if ( SV_STEFX_Borg1RawBspSnapshotBypass( ent, e, origin,
					&stefxBorg1ActorBudget, &stefxBorg1ItemBudget,
					&stefxBorg1MissileBudget, &stefxBorg1EventBudget, &stefxBorg1ModelBudget,
					&stefxBypassReason ) )
			{
				SV_AddEntToSnapshot( svEnt, ent, eNums );
				++stefxBorg1BypassSent;
				if (stefxSnapshotPlayer)
				{
					SV_STEFX_LogSnapshotPlayer( "sent-borg1-bypass", e, ent, svEnt, clientarea, clientcluster, eNums );
				}
				if (stefxSnapshotActor)
				{
					SV_STEFX_LogSnapshotActor( "sent-borg1-bypass", e, ent, svEnt, clientarea, clientcluster, eNums );
				}
				SV_STEFX_LogRemoteSnapshotDecision( STEFX_SNAP_SENT_BYPASS,
					"sent-borg1-bypass", e, ent, svEnt, origin, frame->ps.clientNum, clientarea, clientcluster, portal );
				if ( s_stefxBorg1BypassLogBudget > 0 )
				{
					XBLF("STEFX: borg1 snapshot bypass send ent=%d reason=%s eType=%d model=%d model2=%d weapon=%d client=%p sv=0x%x origin=(%g,%g,%g) count=%d",
						e,
						stefxBypassReason ? stefxBypassReason : "?",
						ent->s.eType,
						ent->s.modelindex,
						ent->s.modelindex2,
						ent->s.weapon,
						ent->client,
						ent->svFlags,
						ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2],
						eNums ? eNums->numSnapshotEntities : -1);
					--s_stefxBorg1BypassLogBudget;
				}
				if (xboxIsMover && xboxTraceMovers) xboxMoverSent++;
				if (xboxFocusIndex >= 0 && !portal)
				{
					XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(sent) );
				}
				continue;
			}
		}
#endif

		// broadcast entities are always sent, and so is the main player so we don't see noclip weirdness
		if ( ent->svFlags & SVF_BROADCAST || !e) {
			SV_AddEntToSnapshot( svEnt, ent, eNums );
#ifdef _XBOX
#if defined(STEFX_ELITE_FORCE_SP)
			if (stefxSnapshotPlayer)
			{
				SV_STEFX_LogSnapshotPlayer( "sent-broadcast", e, ent, svEnt, clientarea, clientcluster, eNums );
			}
			if (stefxSnapshotActor)
			{
				SV_STEFX_LogSnapshotActor( "sent-broadcast", e, ent, svEnt, clientarea, clientcluster, eNums );
			}
			SV_STEFX_LogRemoteSnapshotDecision( STEFX_SNAP_SENT_BROADCAST,
				"sent-broadcast", e, ent, svEnt, origin, frame->ps.clientNum, clientarea, clientcluster, portal );
#endif
			if (xboxYavinFocusEnt)
			{
				XBLF("JA: SV_YAVIN_SNAPSHOT sent-broadcast pass=%s ent=%d snapshot=%d",
					portal ? "extra" : "main", e, eNums->numSnapshotEntities - 1);
			}
			if (xboxIsMover && xboxTraceMovers) xboxMoverSent++;
			if (xboxFocusIndex >= 0 && !portal)
			{
				XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(sent) );
				XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(broadcastSent) );
			}
#endif
			continue;
		}

#if !defined(STEFX_ELITE_FORCE_SP)
		if (ent->s.isPortalEnt)
		{ //rww - portal entities are always sent as well
			SV_AddEntToSnapshot( svEnt, ent, eNums );
#ifdef _XBOX
			if (xboxYavinFocusEnt)
			{
				XBLF("JA: SV_YAVIN_SNAPSHOT sent-portalent pass=%s ent=%d snapshot=%d",
					portal ? "extra" : "main", e, eNums->numSnapshotEntities - 1);
			}
			if (xboxIsMover && xboxTraceMovers) xboxMoverSent++;
			if (xboxFocusIndex >= 0 && !portal)
			{
				XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(sent) );
				XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(portalSent) );
			}
#endif
			continue;
		}
#endif

#if !defined(STEFX_ELITE_FORCE_SP)
		if ( sightOn )
		{//force sight is on, sees through portals, so draw them always if in radius
			if ( SV_PlayerCanSeeEnt( ent, frame->ps.forcePowerLevel[FP_SEE] ) )
			{//entity is visible
				SV_AddEntToSnapshot( svEnt, ent, eNums );
#ifdef _XBOX
				if (xboxIsMover && xboxTraceMovers) xboxMoverSent++;
				if (xboxFocusIndex >= 0 && !portal)
				{
					XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(sent) );
					XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(sightSent) );
				}
#endif
				continue;
			}
		}
#endif

		// ignore if not touching a PV leaf
		// check area
		if ( !CM_AreasConnected( clientarea, svEnt->areanum ) ) {
			// doors can legally straddle two areas, so
			// we may need to check another one
			if ( !CM_AreasConnected( clientarea, svEnt->areanum2 ) ) {
#ifdef _XBOX
				qboolean xboxCameraAreaBypass = (s_xboxSnapshotCameraView && !Q_stricmp(sv_mapname->string, "yavin1"));
				if (xboxCameraAreaBypass)
				{
					if (xboxYavinFocusEnt)
					{
						XBLF("JA: SV_YAVIN_SNAPSHOT camera-bypass-area pass=%s ent=%d clientArea=%d entArea=%d/%d clientCluster=%d clusters=%d last=%d",
							portal ? "extra" : "main",
							e,
							clientarea,
							svEnt->areanum,
							svEnt->areanum2,
							clientcluster,
							svEnt->numClusters,
							svEnt->lastCluster);
					}
				}
				else
				{
#if defined(STEFX_ELITE_FORCE_SP)
					if (stefxSnapshotPlayer)
					{
						SV_STEFX_LogSnapshotPlayer( "reject-area", e, ent, svEnt, clientarea, clientcluster, eNums );
					}
					if (stefxSnapshotActor)
					{
						SV_STEFX_LogSnapshotActor( "reject-area", e, ent, svEnt, clientarea, clientcluster, eNums );
					}
					SV_STEFX_LogRemoteSnapshotDecision( STEFX_SNAP_REJECT_AREA,
						"reject-area", e, ent, svEnt, origin, frame->ps.clientNum, clientarea, clientcluster, portal );
#endif
					if (xboxYavinFocusEnt)
					{
						XBLF("JA: SV_YAVIN_SNAPSHOT reject-area pass=%s ent=%d clientArea=%d entArea=%d/%d clientCluster=%d clusters=%d last=%d",
							portal ? "extra" : "main",
							e,
							clientarea,
							svEnt->areanum,
							svEnt->areanum2,
							clientcluster,
							svEnt->numClusters,
							svEnt->lastCluster);
					}
					if (xboxIsMover && xboxTraceMovers) xboxMoverAreaRejected++;
					if (xboxLogMissile)
					{
						XBLF("JA: SV_SNAPSHOT_MISSILE reject-area ent=%d weapon=%d clientArea=%d entArea=%d/%d",
							e, ent->s.weapon, clientarea, svEnt->areanum, svEnt->areanum2);
						--s_xboxSnapshotMissileBudget;
					}
					if (xboxFocusIndex >= 0 && !portal)
					{
						XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(areaReject) );
					}
					if (xboxFocusMover)
					{
						XBLF("JA: SV_MOVER_FOCUS_REJECT_AREA ent=%d model=%d clientArea=%d entArea=%d/%d",
							e,
							ent->s.modelindex,
							clientarea,
							svEnt->areanum,
							svEnt->areanum2);
					}
					continue;		// blocked by a door
				}
#else
				continue;		// blocked by a door
#endif
			}
		}

		bitvector = clientpvs;

		// check individual leafs
		if ( !svEnt->numClusters ) {
#ifdef _XBOX
#if defined(STEFX_ELITE_FORCE_SP)
			if (stefxSnapshotPlayer)
			{
				SV_STEFX_LogSnapshotPlayer( "reject-noclusters", e, ent, svEnt, clientarea, clientcluster, eNums );
			}
			if (stefxSnapshotActor)
			{
				SV_STEFX_LogSnapshotActor( "reject-noclusters", e, ent, svEnt, clientarea, clientcluster, eNums );
			}
			SV_STEFX_LogRemoteSnapshotDecision( STEFX_SNAP_REJECT_NOCLUSTERS,
				"reject-noclusters", e, ent, svEnt, origin, frame->ps.clientNum, clientarea, clientcluster, portal );
#endif
			if (xboxYavinFocusEnt)
			{
				XBLF("JA: SV_YAVIN_SNAPSHOT reject-noclusters pass=%s ent=%d area=%d/%d",
					portal ? "extra" : "main", e, svEnt->areanum, svEnt->areanum2);
			}
			if (xboxIsMover && xboxTraceMovers) xboxMoverNoClusters++;
			if (xboxLogMissile)
			{
				XBLF("JA: SV_SNAPSHOT_MISSILE reject-noclusters ent=%d weapon=%d area=%d/%d",
					e, ent->s.weapon, svEnt->areanum, svEnt->areanum2);
				--s_xboxSnapshotMissileBudget;
			}
			if (xboxFocusIndex >= 0 && !portal)
			{
				XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(noClusters) );
			}
			if (xboxFocusMover)
			{
				XBLF("JA: SV_MOVER_FOCUS_REJECT_NOCLUSTERS ent=%d model=%d", e, ent->s.modelindex);
			}
#endif
			continue;
		}
		l = 0;
#ifdef _XBOX
		if(bitvector) {
#endif
		for ( i=0 ; i < svEnt->numClusters ; i++ ) {
			l = svEnt->clusternums[i];
			if ( bitvector[l >> 3] & (1 << (l&7) ) ) {
				break;
			}
		}
#ifdef _XBOX
		}
#endif

		// if we haven't found it to be visible,
		// check overflow clusters that coudln't be stored
#ifdef _XBOX
		if ( bitvector && i == svEnt->numClusters ) {
#else
		if ( i == svEnt->numClusters ) {
#endif
			if ( svEnt->lastCluster ) {
				for ( ; l <= svEnt->lastCluster ; l++ ) {
					if ( bitvector[l >> 3] & (1 << (l&7) ) ) {
						break;
					}
				}
				if ( l == svEnt->lastCluster ) {
#ifdef _XBOX
#if defined(STEFX_ELITE_FORCE_SP)
					if (stefxSnapshotPlayer)
					{
						SV_STEFX_LogSnapshotPlayer( "reject-pvs-overflow", e, ent, svEnt, clientarea, clientcluster, eNums );
					}
					if (stefxSnapshotActor)
					{
						SV_STEFX_LogSnapshotActor( "reject-pvs-overflow", e, ent, svEnt, clientarea, clientcluster, eNums );
					}
					SV_STEFX_LogRemoteSnapshotDecision( STEFX_SNAP_REJECT_PVS_OVERFLOW,
						"reject-pvs-overflow", e, ent, svEnt, origin, frame->ps.clientNum, clientarea, clientcluster, portal );
#endif
					if (xboxYavinFocusEnt)
					{
						XBLF("JA: SV_YAVIN_SNAPSHOT reject-pvs-overflow pass=%s ent=%d clientCluster=%d last=%d",
							portal ? "extra" : "main", e, clientcluster, svEnt->lastCluster);
					}
					if (xboxIsMover && xboxTraceMovers) xboxMoverPvsRejected++;
					if (xboxLogMissile)
					{
						XBLF("JA: SV_SNAPSHOT_MISSILE reject-pvs-overflow ent=%d weapon=%d last=%d",
							e, ent->s.weapon, svEnt->lastCluster);
						--s_xboxSnapshotMissileBudget;
					}
					if (xboxFocusIndex >= 0 && !portal)
					{
						XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(pvsReject) );
					}
					if (xboxFocusMover)
					{
						XBLF("JA: SV_MOVER_FOCUS_REJECT_PVS_OVERFLOW ent=%d model=%d last=%d",
							e,
							ent->s.modelindex,
							svEnt->lastCluster);
					}
#endif
					continue;		// not visible
				}
			} else {
#ifdef _XBOX
#if defined(STEFX_ELITE_FORCE_SP)
				if (stefxSnapshotPlayer)
				{
					SV_STEFX_LogSnapshotPlayer( "reject-pvs", e, ent, svEnt, clientarea, clientcluster, eNums );
				}
				if (stefxSnapshotActor)
				{
					SV_STEFX_LogSnapshotActor( "reject-pvs", e, ent, svEnt, clientarea, clientcluster, eNums );
				}
				SV_STEFX_LogRemoteSnapshotDecision( STEFX_SNAP_REJECT_PVS,
					"reject-pvs", e, ent, svEnt, origin, frame->ps.clientNum, clientarea, clientcluster, portal );
#endif
				if (xboxYavinFocusEnt)
				{
					XBLF("JA: SV_YAVIN_SNAPSHOT reject-pvs pass=%s ent=%d clientCluster=%d clusters=%d firstCluster=%d area=%d/%d",
						portal ? "extra" : "main",
						e,
						clientcluster,
						svEnt->numClusters,
						svEnt->numClusters > 0 ? svEnt->clusternums[0] : -1,
						svEnt->areanum,
						svEnt->areanum2);
				}
				if (xboxIsMover && xboxTraceMovers) xboxMoverPvsRejected++;
				if (xboxLogMissile)
				{
					XBLF("JA: SV_SNAPSHOT_MISSILE reject-pvs ent=%d weapon=%d clusters=%d",
						e, ent->s.weapon, svEnt->numClusters);
					--s_xboxSnapshotMissileBudget;
				}
				if (xboxFocusIndex >= 0 && !portal)
				{
					XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(pvsReject) );
				}
				if (xboxFocusMover)
				{
					XBLF("JA: SV_MOVER_FOCUS_REJECT_PVS ent=%d model=%d clusters=%d",
						e,
						ent->s.modelindex,
						svEnt->numClusters);
				}
#endif
				continue;
			}
		}

		// add it
		SV_AddEntToSnapshot( svEnt, ent, eNums );
#ifdef _XBOX
#if defined(STEFX_ELITE_FORCE_SP)
		if (stefxSnapshotPlayer)
		{
			SV_STEFX_LogSnapshotPlayer( "sent-pvs", e, ent, svEnt, clientarea, clientcluster, eNums );
		}
		if (stefxSnapshotActor)
		{
			SV_STEFX_LogSnapshotActor( "sent-pvs", e, ent, svEnt, clientarea, clientcluster, eNums );
		}
		SV_STEFX_LogRemoteSnapshotDecision( STEFX_SNAP_SENT_PVS,
			"sent-pvs", e, ent, svEnt, origin, frame->ps.clientNum, clientarea, clientcluster, portal );
#endif
		if (xboxYavinFocusEnt)
		{
			XBLF("JA: SV_YAVIN_SNAPSHOT sent-pvs pass=%s ent=%d snapshot=%d clientArea=%d clientCluster=%d area=%d/%d clusters=%d firstCluster=%d",
				portal ? "extra" : "main",
				e,
				eNums->numSnapshotEntities - 1,
				clientarea,
				clientcluster,
				svEnt->areanum,
				svEnt->areanum2,
				svEnt->numClusters,
				svEnt->numClusters > 0 ? svEnt->clusternums[0] : -1);
		}
		if (xboxIsMover && xboxTraceMovers) xboxMoverSent++;
		if (xboxFocusIndex >= 0 && !portal)
		{
			XboxMoverFocusRecord( xboxFocusIndex, ent, svEnt, clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(sent) );
		}
		if (xboxFocusMover)
		{
			XBLF("JA: SV_MOVER_FOCUS_SENT ent=%d model=%d", e, ent->s.modelindex);
		}
#endif

		// if its a portal entity, add everything visible from its camera position
		if ( ent->svFlags & SVF_PORTAL ) {
			SV_AddEntitiesVisibleFromPoint( ent->s.origin2, frame, eNums, qtrue );
#ifdef _XBOX
			//Must get clientpvs again since above call destroyed it.
		clientpvs = CM_ClusterPVS (clientcluster);
#endif
		}
	}
#ifdef _XBOX
	if (!portal)
	{
		XboxMoverFocusMaybePrintSummary( clientarea, clientcluster );
	}
	if (xboxTraceMovers)
	{
		XBLF("JA: SV_SNAPSHOT_MOVER_SUMMARY origin=%g,%g,%g area=%d cluster=%d total=%d sent=%d unlinked=%d noclient=%d areaReject=%d pvsReject=%d noClusters=%d",
			origin[0], origin[1], origin[2],
			clientarea,
			clientcluster,
			xboxMoverTotal,
			xboxMoverSent,
			xboxMoverUnlinked,
			xboxMoverNoClient,
			xboxMoverAreaRejected,
			xboxMoverPvsRejected,
			xboxMoverNoClusters);
		s_xboxSnapshotMoverFrameBudget--;
	}
#if defined(STEFX_ELITE_FORCE_SP)
	if ( stefxBorg1BypassSent > 0 && !portal )
	{
		static int s_stefxBorg1BypassSummaryBudget = 16;
		if ( s_stefxBorg1BypassSummaryBudget > 0 )
		{
			XBLF("STEFX: borg1 snapshot bypass summary sent=%d snapshotCount=%d remaining actor=%d item=%d missile=%d event=%d model=%d clientArea=%d clientCluster=%d",
				stefxBorg1BypassSent,
				eNums ? eNums->numSnapshotEntities : -1,
				stefxBorg1ActorBudget,
				stefxBorg1ItemBudget,
				stefxBorg1MissileBudget,
				stefxBorg1EventBudget,
				stefxBorg1ModelBudget,
				clientarea,
				clientcluster);
			--s_stefxBorg1BypassSummaryBudget;
		}
	}
#endif
	if (xboxTraceVisible)
	{
		Com_PrintfAlways("JA: SV_AddEntitiesVisibleFromPoint exit portal=%d count=%d\n",
			(int)portal, eNums ? eNums->numSnapshotEntities : -1);
		--s_xboxVisibleLogBudget;
	}
#endif
}

/*
=============
SV_BuildClientSnapshot

Decides which entities are going to be visible to the client, and
copies off the playerstate and areabits.

This properly handles multiple recursive portals, but the render
currently doesn't.

For viewing through other player's eyes, clent can be something other than client->gentity
=============
*/
static clientSnapshot_t *SV_BuildClientSnapshot( client_t *client ) {
	vec3_t						org;
	clientSnapshot_t			*frame;
	snapshotEntityNumbers_t		entityNumbers;
	int							i;
	gentity_t					*ent;
	entityState_t				*state;
	gentity_t					*clent;
#ifdef _XBOX
	static int s_xboxBuildSnapshotLogBudget = 0;
	const qboolean xboxTraceBuild = (s_xboxBuildSnapshotLogBudget > 0);
#if defined(STEFX_ELITE_FORCE_SP)
	static int s_stefxHolomatchPlayerStateLogBudget = 12;
#endif
	if (xboxTraceBuild)
	{
		Com_PrintfAlways("JA: SV_BuildClientSnapshot enter outgoing=%d state=%d gentity=%p\n",
			client ? client->netchan.outgoingSequence : -1,
			client ? client->state : -1,
			client ? client->gentity : NULL);
	}
#endif

	// bump the counter used to prevent double adding
	sv.snapshotCounter++;

	// this is the frame we are creating
	frame = &client->frames[ client->netchan.outgoingSequence & PACKET_MASK ];

	// clear everything in this snapshot
	entityNumbers.numSnapshotEntities = 0;
	memset( frame->areabits, 0, sizeof( frame->areabits ) );

	clent = client->gentity;
	if ( !clent ) {
#ifdef _XBOX
		if (xboxTraceBuild)
		{
			Com_PrintfAlways("JA: SV_BuildClientSnapshot exit no-client-entity\n");
			--s_xboxBuildSnapshotLogBudget;
		}
#endif
		return frame;
	}

	// grab the current playerState_t
#if defined(STEFX_ELITE_FORCE_SP)
	// The Holomatch game bridge has already converted the official EF state into
	// the SP playerState_t owned by the mirror entity.
	frame->ps = *clent->client;
#ifdef _XBOX
	if (s_stefxHolomatchPlayerStateLogBudget > 0)
	{
		Com_PrintfAlways("STEFX: snapshot mirror ps sourceHealth=%d sourceWeapon=%d frameHealth=%d frameWeapon=%d clientNum=%d\n",
			clent->client->stats[STAT_HEALTH], clent->client->weapon,
			frame->ps.stats[STAT_HEALTH], frame->ps.weapon, frame->ps.clientNum);
		--s_stefxHolomatchPlayerStateLogBudget;
	}
#endif
#else
	frame->ps = *clent->client;
#endif
#ifdef _XBOX
	if (xboxTraceBuild)
	{
#if defined(STEFX_ELITE_FORCE_SP)
		Com_PrintfAlways("JA: SV_BuildClientSnapshot after ps copy clientNum=%d origin=%g,%g,%g viewheight=%d\n",
			frame->ps.clientNum,
			frame->ps.origin[0], frame->ps.origin[1], frame->ps.origin[2],
			frame->ps.viewheight);
#else
		Com_PrintfAlways("JA: SV_BuildClientSnapshot after ps copy clientNum=%d origin=%g,%g,%g viewheight=%d viewEntity=%d\n",
			frame->ps.clientNum,
			clent->client->origin[0], clent->client->origin[1], clent->client->origin[2],
			clent->client->viewheight,
			frame->ps.viewEntity);
#endif
	}
#endif

#if defined(STEFX_SP_HOSTED_MP)
	{
		const int clientNum = frame->ps.clientNum;
		if ( clientNum < 0 || clientNum >= MAX_GENTITIES )
		{
			Com_Error( ERR_DROP, "SV_SvEntityForGentity: bad gEnt" );
		}
		// EF Holomatch regenerates the local entity from playerstate. Preserve the
		// SP looping-sound behavior above for SP, but do not submit a second local
		// player body to the official multiplayer cgame.
		sv.svEntities[clientNum].snapshotCounter = sv.snapshotCounter;
	}
#endif

	// this stops the main client entity playerstate from being sent across, which has the effect of breaking
	// looping sounds for the main client. So I took it out.
/*	{
		int							clientNum;
		svEntity_t					*svEnt;
		clientNum = frame->ps.clientNum;
		if ( clientNum < 0 || clientNum >= MAX_GENTITIES ) {
			Com_Error( ERR_DROP, "SV_SvEntityForGentity: bad gEnt" );
		}
		svEnt = &sv.svEntities[ clientNum ];
		// never send client's own entity, because it can
		// be regenerated from the playerstate
		svEnt->snapshotCounter = sv.snapshotCounter;
	}
*/
	// find the client's viewpoint

	//if in camera mode use camera position instead
#ifdef _XBOX
	qboolean xboxUseCameraPos = qfalse;
#if defined(STEFX_ELITE_FORCE_SP)
	static int s_stefxCameraPvsLogBudget = 24;
#endif
	if (xboxTraceBuild)
	{
		Com_PrintfAlways("JA: SV_BuildClientSnapshot camera gate direct=%d\n",
			Sys_IsDirectMapBoot() ? 1 : 0);
	}
	if (xboxTraceBuild) Com_PrintfAlways("JA: SV_BuildClientSnapshot before CG_CAMERA_POS\n");
	xboxUseCameraPos = VM_Call( CG_CAMERA_POS, org) ? qtrue : qfalse;
#if defined(STEFX_ELITE_FORCE_SP)
	if ( s_stefxCameraPvsLogBudget > 0 && ( xboxUseCameraPos || Sys_IsDirectMapBoot() ) )
	{
		XBLF("STEFX: SV_BuildClientSnapshot camera-pvs direct=%d used=%d org=(%g,%g,%g)",
			Sys_IsDirectMapBoot() ? 1 : 0,
			xboxUseCameraPos ? 1 : 0,
			org[0], org[1], org[2]);
		--s_stefxCameraPvsLogBudget;
	}
#endif
	if (xboxUseCameraPos)
#else
	if (VM_Call( CG_CAMERA_POS, org))
#endif
	{
	#ifdef _XBOX
		s_xboxSnapshotCameraView = qtrue;
		if (xboxTraceBuild)
		{
			Com_PrintfAlways("JA: SV_BuildClientSnapshot CG_CAMERA_POS true org=%g,%g,%g\n",
				org[0], org[1], org[2]);
		}
	#endif
		//org[2] += clent->client->viewheight;
	}
	else 
	{ 
	#ifdef _XBOX
		s_xboxSnapshotCameraView = qfalse;
		if (xboxTraceBuild) Com_PrintfAlways("JA: SV_BuildClientSnapshot CG_CAMERA_POS false\n");
	#endif
		VectorCopy( frame->ps.origin, org );
		org[2] += frame->ps.viewheight;

//============
		// need to account for lean, or areaportal doors don't draw properly... -slc
		if (frame->ps.leanofs != 0)
		{
			vec3_t	right;
			//add leaning offset			
			vec3_t v3ViewAngles;
			VectorCopy(frame->ps.viewangles, v3ViewAngles);
			v3ViewAngles[2] += (float)frame->ps.leanofs/2;
			AngleVectors(v3ViewAngles, NULL, right, NULL);
			VectorMA(org, (float)frame->ps.leanofs, right, org);
		}
//============
	}
#if !defined(STEFX_ELITE_FORCE_SP)
	VectorCopy( org, frame->ps.serverViewOrg );
	VectorCopy( org, clent->client->serverViewOrg );
#endif
#ifdef _XBOX
	if (xboxTraceBuild)
	{
		Com_PrintfAlways("JA: SV_BuildClientSnapshot before visible org=%g,%g,%g\n",
			org[0], org[1], org[2]);
	}
#endif

	// add all the entities directly visible to the eye, which
	// may include portal entities that merge other viewpoints
	SV_AddEntitiesVisibleFromPoint( org, frame, &entityNumbers, qfalse );
#ifdef _XBOX
	if (xboxTraceBuild)
	{
		Com_PrintfAlways("JA: SV_BuildClientSnapshot after visible count=%d areaBytes=%d\n",
			entityNumbers.numSnapshotEntities,
			frame->areabytes);
	}
#endif
	#ifdef _XBOX
	s_xboxSnapshotCameraView = qfalse;
	#endif

#if !defined(STEFX_ELITE_FORCE_SP)
	// A scripted viewEntity can move the rendered camera far from the player's
	// body. Build an additional visibility set from that camera entity so
	// nearby cinematic actors are actually sent to cgame.
	if ( frame->ps.viewEntity > 0 && frame->ps.viewEntity < ENTITYNUM_WORLD )
	{
		gentity_t *viewEnt = SV_GentityNum( frame->ps.viewEntity );
		if ( viewEnt && viewEnt->inuse && viewEnt->linked && !(viewEnt->svFlags & SVF_NOCLIENT) )
		{
			vec3_t viewOrg;
			vec3_t viewDelta;
			byte mainAreaBits[MAX_MAP_AREA_BYTES];
			int mainAreaBytes = frame->areabytes;

			VectorCopy( viewEnt->currentOrigin, viewOrg );
			VectorSubtract( viewOrg, org, viewDelta );
			memcpy( mainAreaBits, frame->areabits, sizeof( mainAreaBits ) );

#ifdef _XBOX
			if ( !Q_stricmp(sv_mapname->string, "yavin1") )
			{
				XBLF("JA: SV_YAVIN_VIEWENTITY_PVS begin viewEntity=%d eType=%d sv=0x%x model=%d org=%g,%g,%g viewOrg=%g,%g,%g deltaLenSq=%g entsBefore=%d",
					frame->ps.viewEntity,
					viewEnt->s.eType,
					viewEnt->svFlags,
					viewEnt->s.modelindex,
					org[0], org[1], org[2],
					viewOrg[0], viewOrg[1], viewOrg[2],
					DotProduct( viewDelta, viewDelta ),
					entityNumbers.numSnapshotEntities);
			}
#endif

			SV_AddEntitiesVisibleFromPoint( viewOrg, frame, &entityNumbers, qtrue );

			if ( frame->areabytes < mainAreaBytes )
			{
				frame->areabytes = mainAreaBytes;
			}
			for ( i = 0 ; i < frame->areabytes ; i++ )
			{
				frame->areabits[i] |= mainAreaBits[i];
			}

#ifdef _XBOX
			if ( !Q_stricmp(sv_mapname->string, "yavin1") )
			{
				XBLF("JA: SV_YAVIN_VIEWENTITY_PVS end viewEntity=%d entsAfter=%d areaBytes=%d",
					frame->ps.viewEntity,
					entityNumbers.numSnapshotEntities,
					frame->areabytes);
			}
#endif
		}
#ifdef _XBOX
		else if ( !Q_stricmp(sv_mapname->string, "yavin1") )
		{
			XBLF("JA: SV_YAVIN_VIEWENTITY_PVS skip viewEntity=%d valid=%d inuse=%d linked=%d sv=0x%x",
				frame->ps.viewEntity,
				(int)(viewEnt != NULL),
				viewEnt ? (int)viewEnt->inuse : 0,
				viewEnt ? (int)viewEnt->linked : 0,
				viewEnt ? viewEnt->svFlags : 0);
		}
#endif
	}
#endif
	/*
	//was in here for debugging- print list of all entities in snapshot when you go over the limit
	if ( entityNumbers.numSnapshotEntities >= 256 )
	{
		for ( int xxx = 0; xxx < entityNumbers.numSnapshotEntities; xxx++ )
		{	
			Com_Printf("%d - ", xxx );
			ge->PrintEntClassname( entityNumbers.snapshotEntities[xxx] );
		}
	}
	else if ( entityNumbers.numSnapshotEntities >= 200 )
	{
		Com_Printf(S_COLOR_RED"%d snapshot entities!", entityNumbers.numSnapshotEntities );
	}
	else if ( entityNumbers.numSnapshotEntities >= 128 )
	{
		Com_Printf(S_COLOR_YELLOW"%d snapshot entities", entityNumbers.numSnapshotEntities );
	}
	*/

	// if there were portals visible, there may be out of order entities
	// in the list which will need to be resorted for the delta compression
	// to work correctly.  This also catches the error condition
	// of an entity being included twice.
	qsort( entityNumbers.snapshotEntities, entityNumbers.numSnapshotEntities, 
		sizeof( entityNumbers.snapshotEntities[0] ), SV_QsortEntityNumbers );
#ifdef _XBOX
	if (xboxTraceBuild)
	{
		Com_PrintfAlways("JA: SV_BuildClientSnapshot after qsort count=%d\n",
			entityNumbers.numSnapshotEntities);
	}
#endif

	// now that all viewpoint's areabits have been OR'd together, invert
	// all of them to make it a mask vector, which is what the renderer wants
	for ( i = 0 ; i < MAX_MAP_AREA_BYTES/4 ; i++ ) {
		((int *)frame->areabits)[i] = ((int *)frame->areabits)[i] ^ -1;
	}

	// copy the entity states out
	frame->num_entities = 0;
	frame->first_entity = svs.nextSnapshotEntities;
	for ( i = 0 ; i < entityNumbers.numSnapshotEntities ; i++ ) {
		ent = SV_GentityNum(entityNumbers.snapshotEntities[i]);
		state = &svs.snapshotEntities[svs.nextSnapshotEntities % svs.numSnapshotEntities];
		*state = ent->s;
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		if ( sv_mapname && !Q_stricmp( sv_mapname->string, "borg1" ) && state->eType > ET_EVENTS )
		{
			static int s_stefxSnapshotEventCopyBudget = 128;
			if ( s_stefxSnapshotEventCopyBudget > 0 )
			{
				XBLF("STEFX: SV_EVENT copy ent=%d eType=%d weapon=%d frameIndex=%d ring=%d origin=(%g,%g,%g) origin2=(%g,%g,%g)",
					state->number,
					state->eType,
					state->weapon,
					i,
					svs.nextSnapshotEntities % svs.numSnapshotEntities,
					state->pos.trBase[0], state->pos.trBase[1], state->pos.trBase[2],
					state->origin2[0], state->origin2[1], state->origin2[2]);
				--s_stefxSnapshotEventCopyBudget;
			}
		}
#endif
		svs.nextSnapshotEntities++;
		frame->num_entities++;
	}
#ifdef _XBOX
#if defined(STEFX_ELITE_FORCE_SP)
	if ( sv_mapname && !Q_stricmp( sv_mapname->string, "borg1" ) )
	{
		static int s_stefxBorg1BuildSummaryBudget = 16;
		if ( s_stefxBorg1BuildSummaryBudget > 0 )
		{
			XBLF("STEFX: borg1 build snapshot frameEntities=%d gathered=%d first=%d next=%d psClient=%d psWeapon=%d viewOrg=(%g,%g,%g)",
				frame->num_entities,
				entityNumbers.numSnapshotEntities,
				frame->first_entity,
				svs.nextSnapshotEntities,
				frame->ps.clientNum,
				frame->ps.weapon,
				org[0], org[1], org[2]);
			--s_stefxBorg1BuildSummaryBudget;
		}
	}
#endif
	if (xboxTraceBuild)
	{
		Com_PrintfAlways("JA: SV_BuildClientSnapshot exit num_entities=%d first=%d next=%d\n",
			frame->num_entities,
			frame->first_entity,
			svs.nextSnapshotEntities);
		--s_xboxBuildSnapshotLogBudget;
	}
#endif

	return frame;
}


/*
=======================
SV_SendMessageToClient

Called by SV_SendClientSnapshot and SV_SendClientGameState
=======================
*/
#define	HEADER_RATE_BYTES	48		// include our header, IP header, and some overhead
void SV_SendMessageToClient( msg_t *msg, client_t *client ) {
	int			rateMsec;

	// record information about the message
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageSize = msg->cursize;
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageSent = sv.time;

	// send the datagram
	Netchan_Transmit( &client->netchan, msg->cursize, msg->data );

	// set nextSnapshotTime based on rate and requested number of updates

	// local clients get snapshots every frame (FIXME: also treat LAN clients)
	if ( client->netchan.remoteAddress.type == NA_LOOPBACK ) {
		client->nextSnapshotTime = sv.time - 1;
		return;
	}

	// normal rate / snapshotMsec calculation
	rateMsec = ( msg->cursize + HEADER_RATE_BYTES ) * 1000 / client->rate;
	if ( rateMsec < client->snapshotMsec ) {
		rateMsec = client->snapshotMsec;
		client->rateDelayed = qfalse;
	} else {
		client->rateDelayed = qtrue;
	}

	client->nextSnapshotTime = sv.time + rateMsec;

	// if we haven't gotten a message from the client in over a second, we will
	// drop to only sending one snapshot a second until they timeout
	if ( sv.time - client->lastPacketTime > 1000 || client->state != CS_ACTIVE ) {
		if ( client->nextSnapshotTime < sv.time + 1000 ) {
			client->nextSnapshotTime = sv.time + 1000;
		}
		return;
	}

}

/*
=======================
SV_SendClientEmptyMessage

This is just an empty message so that we can tell if
the client dropped the gamestate that went out before
=======================
*/
void SV_SendClientEmptyMessage( client_t *client ) {
	msg_t	msg;
	byte	buffer[10];

	MSG_Init( &msg, buffer, sizeof( buffer ) );
	SV_SendMessageToClient( &msg, client );
}

/*
=======================
SV_SendClientSnapshot
=======================
*/
void SV_SendClientSnapshot( client_t *client ) {
	byte		msg_buf[MAX_MSGLEN];
	msg_t		msg;
#ifdef _XBOX
	static int s_xboxSnapshotLogBudget = 0;
	const qboolean xboxTraceSnapshot = (s_xboxSnapshotLogBudget > 0);
	if (xboxTraceSnapshot)
	{
		Com_PrintfAlways("JA: SV_SendClientSnapshot enter state=%d outgoing=%d svTime=%d nextSnapshot=%d\n",
			client ? client->state : -1,
			client ? client->netchan.outgoingSequence : -1,
			sv.time,
			client ? client->nextSnapshotTime : -1);
	}
#endif

	// build the snapshot
	SV_BuildClientSnapshot( client );

	// bots need to have their snapshots build, but
	// the query them directly without needing to be sent
	if ( client->gentity && client->gentity->svFlags & SVF_BOT ) {
#ifdef _XBOX
		if (xboxTraceSnapshot)
		{
			Com_PrintfAlways("JA: SV_SendClientSnapshot exit bot\n");
			--s_xboxSnapshotLogBudget;
		}
#endif
		return;
	}

	MSG_Init (&msg, msg_buf, sizeof(msg_buf));
	msg.allowoverflow = qtrue;

	// (re)send any reliable server commands
	SV_UpdateServerCommandsToClient( client, &msg );

	// send over all the relevant entityState_t
	// and the playerState_t
	SV_WriteSnapshotToClient( client, &msg );

	// check for overflow
	if ( msg.overflowed ) {
		Com_Printf ("WARNING: msg overflowed for %s\n", client->name);
		MSG_Clear (&msg);
	}

	SV_SendMessageToClient( &msg, client );
#ifdef _XBOX
	if (xboxTraceSnapshot)
	{
		Com_PrintfAlways("JA: SV_SendClientSnapshot sent size=%d overflow=%d outgoing=%d\n",
			msg.cursize,
			(int)msg.overflowed,
			client ? client->netchan.outgoingSequence : -1);
		--s_xboxSnapshotLogBudget;
	}
#endif
}


/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages( void ) {
	int			i;
	client_t	*c;
#ifdef _XBOX
	static int s_xboxMessagesLogBudget = 0;
	const qboolean xboxTraceMessages = (s_xboxMessagesLogBudget > 0);
	if (xboxTraceMessages)
	{
		Com_PrintfAlways("JA: SV_SendClientMessages enter svTime=%d clientState=%d nextSnapshot=%d\n",
			sv.time,
			svs.clients ? svs.clients[0].state : -1,
			svs.clients ? svs.clients[0].nextSnapshotTime : -1);
	}
#endif

	// send a message to each connected client
	for (i=0, c = svs.clients ; i < MAX_CLIENTS ; i++, c++) {
		if (!c->state) {
#ifdef _XBOX
			if (xboxTraceMessages) Com_PrintfAlways("JA: SV_SendClientMessages skip disconnected i=%d\n", i);
#endif
			continue;		// not connected
		}

		if ( sv.time < c->nextSnapshotTime ) {
#ifdef _XBOX
			if (xboxTraceMessages)
			{
				Com_PrintfAlways("JA: SV_SendClientMessages skip wait i=%d svTime=%d next=%d\n",
					i, sv.time, c->nextSnapshotTime);
			}
#endif
			continue;		// not time yet
		}

		if ( c->state != CS_ACTIVE ) {
#ifdef _XBOX
			if (xboxTraceMessages)
			{
				Com_PrintfAlways("JA: SV_SendClientMessages nonactive i=%d state=%d\n",
					i, c->state);
			}
#endif
			if ( c->state != CS_ZOMBIE ) {
				SV_SendClientEmptyMessage( c );
			}
			continue;
		}

		SV_SendClientSnapshot( c );
	}
#ifdef _XBOX
	if (xboxTraceMessages)
	{
		Com_PrintfAlways("JA: SV_SendClientMessages exit\n");
		--s_xboxMessagesLogBudget;
	}
#endif
}

