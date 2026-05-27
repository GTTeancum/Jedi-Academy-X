// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"



#include "..\client\vmachine.h"
#include "server.h"

#ifdef _XBOX
#include "../win32/xb_log.h"
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
#endif
}

//rww - bg_public.h won't cooperate in here
#define EF_PERMANENT			0x00080000

#ifdef _XBOX
static qboolean s_xboxSnapshotCameraView = qfalse;
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
#endif

	// during an error shutdown message we may need to transmit
	// the shutdown message after the server has shutdown, so
	// specfically check for it
	if ( !sv.state ) {
		return;
	}

	leafnum = CM_PointLeafnum (origin);
	clientarea = CM_LeafArea (leafnum);
	clientcluster = CM_LeafCluster (leafnum);

	// calculate the visible areas
	frame->areabytes = CM_WriteAreaBits( frame->areabits, clientarea );

	clientpvs = CM_ClusterPVS (clientcluster);

	c_fullsend = 0;

	if ( !portal )
	{//not if this if through a portal...???  James said to do this...
		if ( (frame->ps.forcePowersActive&(1<<FP_SEE)) )
		{
			sightOn = qtrue;
		}
	}

	for ( e = 0 ; e < ge->num_entities ; e++ ) {
		ent = SV_GentityNum(e);
#ifdef _XBOX
		qboolean xboxIsMover = qfalse;
		qboolean xboxFocusMover = qfalse;
		qboolean xboxIsMissile = qfalse;
		qboolean xboxLogMissile = qfalse;
#endif

		if (!ent->inuse) {
			continue;
		}
#ifdef _XBOX
		xboxIsMover = (ent->s.eType == ET_MOVER);
		xboxIsMissile = (ent->s.eType == ET_MISSILE);
		xboxYavinFocusEnt = (!Q_stricmp(sv_mapname->string, "yavin1") && e >= 48 && e <= 60 && s_xboxYavinSnapshotFocusBudget > 0);
		xboxLogMissile = (xboxIsMissile && !portal && s_xboxSnapshotMissileBudget > 0);
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
			xboxFocusIndex = XboxMoverFocusIndex( ent->s.modelindex );
			if (xboxFocusIndex >= 0 && !portal)
			{
				XboxMoverFocusRecord( xboxFocusIndex, ent, SV_SvEntityForGentity( ent ), clientarea, clientcluster, XBOX_MOVER_STAT_FIELD(noClient) );
			}
#endif
			continue;
		}

		svEnt = SV_SvEntityForGentity( ent );
#ifdef _XBOX
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

		// broadcast entities are always sent, and so is the main player so we don't see noclip weirdness
		if ( ent->svFlags & SVF_BROADCAST || !e) {
			SV_AddEntToSnapshot( svEnt, ent, eNums );
#ifdef _XBOX
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

	// bump the counter used to prevent double adding
	sv.snapshotCounter++;

	// this is the frame we are creating
	frame = &client->frames[ client->netchan.outgoingSequence & PACKET_MASK ];

	// clear everything in this snapshot
	entityNumbers.numSnapshotEntities = 0;
	memset( frame->areabits, 0, sizeof( frame->areabits ) );

	clent = client->gentity;
	if ( !clent ) {
		return frame;
	}

	// grab the current playerState_t
	frame->ps = *clent->client;

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
	if ( VM_Call( CG_CAMERA_POS, org))
	{
	#ifdef _XBOX
		s_xboxSnapshotCameraView = qtrue;
	#endif
		//org[2] += clent->client->viewheight;
	}
	else 
	{ 
	#ifdef _XBOX
		s_xboxSnapshotCameraView = qfalse;
	#endif
		VectorCopy( clent->client->origin, org );
		org[2] += clent->client->viewheight;

//============
		// need to account for lean, or areaportal doors don't draw properly... -slc
		if (frame->ps.leanofs != 0)
		{
			vec3_t	right;
			//add leaning offset			
			vec3_t v3ViewAngles;
			VectorCopy(clent->client->viewangles, v3ViewAngles);
			v3ViewAngles[2] += (float)frame->ps.leanofs/2;
			AngleVectors(v3ViewAngles, NULL, right, NULL);
			VectorMA(org, (float)frame->ps.leanofs, right, org);
		}
//============
	}
	VectorCopy( org, frame->ps.serverViewOrg );
	VectorCopy( org, clent->client->serverViewOrg );

	// add all the entities directly visible to the eye, which
	// may include portal entities that merge other viewpoints
	SV_AddEntitiesVisibleFromPoint( org, frame, &entityNumbers, qfalse );
	#ifdef _XBOX
	s_xboxSnapshotCameraView = qfalse;
	#endif

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
		svs.nextSnapshotEntities++;
		frame->num_entities++;
	}

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

	// build the snapshot
	SV_BuildClientSnapshot( client );

	// bots need to have their snapshots build, but
	// the query them directly without needing to be sent
	if ( client->gentity && client->gentity->svFlags & SVF_BOT ) {
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
}


/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages( void ) {
	int			i;
	client_t	*c;

	// send a message to each connected client
	for (i=0, c = svs.clients ; i < 1 ; i++, c++) {
		if (!c->state) {
			continue;		// not connected
		}

		if ( sv.time < c->nextSnapshotTime ) {
			continue;		// not time yet
		}

		if ( c->state != CS_ACTIVE ) {
			if ( c->state != CS_ZOMBIE ) {
				SV_SendClientEmptyMessage( c );
			}
			continue;
		}

		SV_SendClientSnapshot( c );
	}
}

