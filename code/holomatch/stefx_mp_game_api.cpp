#include "../game/q_shared.h"
#include "../game/g_public.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/stefx_snapshot_abi.h"
#include "../server/server.h"
#include "../win32/xb_log.h"
#include "stefx_holomatch_bot_bridge.h"

#include <stdarg.h>

extern "C" void QDECL STEFX_HM_Com_Printf( const char *format, ... )
{
	char text[4096];
	va_list args;
	va_start( args, format );
	_vsnprintf( text, sizeof( text ) - 1, format, args );
	va_end( args );
	text[sizeof( text ) - 1] = 0;
	::Com_Printf( "%s", text );
}

extern "C" void QDECL STEFX_HM_Com_Error( int level, const char *format, ... )
{
	char text[4096];
	va_list args;
	va_start( args, format );
	_vsnprintf( text, sizeof( text ) - 1, format, args );
	va_end( args );
	text[sizeof( text ) - 1] = 0;
	XBLog_WriteCriticalf("STEFX_HM_SP: official Com_Error level=%d message='%s'",
		level, text);
	::Com_Error( level, "%s", text );
}

extern "C" int vmMain( int command, int arg0, int arg1, int arg2,
	int arg3, int arg4, int arg5, int arg6 );
extern "C" void dllEntry( int (QDECL *syscallptr)( int arg, ... ) );

extern void SV_GetUsercmd( int clientNum, usercmd_t *cmd );
extern qboolean SV_GetEntityToken( char *buffer, int bufferSize );

static game_import_t *s_stefxHolomatchImport = NULL;
static game_import_t s_stefxHolomatchImportStorage;
static void *s_stefxHolomatchGentities = NULL;
static int s_stefxHolomatchGentitySize = 0;
static int s_stefxHolomatchNumEntities = 0;
static int s_stefxHolomatchSyscallTraceCount = 0;
static int s_stefxHolomatchInvalidTraceTraceCount = 0;
static int s_stefxHolomatchBadTraceInputCount = 0;
static int s_stefxHolomatchBadTraceResultCount = 0;
static int s_stefxHolomatchTraceDetailCount = 0;
static int s_stefxHolomatchPointContentsTraceCount = 0;
static int s_stefxHolomatchClientLinkTraceCount = 0;
static int s_stefxHolomatchClientCollisionTraceCount = 0;
static int s_stefxHolomatchCombatTraceProbeCount = 0;
static int s_stefxHolomatchEntityTokenTraceCount = 0;
static int s_stefxHolomatchHeartbeat = 0;
static int s_stefxHolomatchClientThinkTraceCount = 0;
static int s_stefxHolomatchClientStatePreserveLogBudget = 0;
static const char *s_stefxHolomatchEntityParsePoint = NULL;
static game_export_t s_stefxHolomatchExport;

/*
 * The official EF VM and the SP server do not share the same public entity
 * ABI.  EF puts entityShared_t immediately after entityState_t and uses the
 * EF playerState layout; the SP server uses the later JA layout.  Keep the
 * VM's memory private and expose a synchronized SP-shaped view to the engine.
 */
typedef struct stefx_hm_trajectory_s {
	int trType;
	int trTime;
	int trDuration;
	vec3_t trBase;
	vec3_t trDelta;
} stefx_hm_trajectory_t;

typedef struct stefx_hm_entity_state_s {
	int number;
	int eType;
	int eFlags;
	stefx_hm_trajectory_t pos;
	stefx_hm_trajectory_t apos;
	int time;
	int time2;
	vec3_t origin;
	vec3_t origin2;
	vec3_t angles;
	vec3_t angles2;
	int otherEntityNum;
	int otherEntityNum2;
	int groundEntityNum;
	int constantLight;
	int loopSound;
	int modelindex;
	int modelindex2;
	int clientNum;
	int frame;
	int solid;
	int event;
	int eventParm;
	int powerups;
	int weapon;
	int legsAnim;
	int torsoAnim;
} stefx_hm_entity_state_t;

typedef struct stefx_hm_entity_shared_s {
	qboolean linked;
	int linkcount;
	int svFlags;
	int singleClient;
	qboolean bmodel;
	vec3_t mins;
	vec3_t maxs;
	int contents;
	vec3_t absmin;
	vec3_t absmax;
	vec3_t currentOrigin;
	vec3_t currentAngles;
	int ownerNum;
} stefx_hm_entity_shared_t;

typedef struct stefx_hm_entity_prefix_s {
	stefx_hm_entity_state_t s;
	stefx_hm_entity_shared_t r;
	void *client;
	qboolean inuse;
} stefx_hm_entity_prefix_t;

typedef struct stefx_hm_player_state_s {
	int commandTime;
	int pm_type;
	int bobCycle;
	int pm_flags;
	int pm_time;
	vec3_t origin;
	vec3_t velocity;
	int weaponTime;
	int rechargeTime;
	short useTime;
	int introTime;
	int gravity;
	int speed;
	int delta_angles[3];
	int groundEntityNum;
	int legsTimer;
	int legsAnim;
	int torsoTimer;
	int torsoAnim;
	int movementDir;
	int eFlags;
	int eventSequence;
	int events[4];
	int eventParms[4];
	int externalEvent;
	int externalEventParm;
	int externalEventTime;
	int clientNum;
	int weapon;
	int weaponstate;
	vec3_t viewangles;
	int viewheight;
	int damageEvent;
	int damageYaw;
	int damagePitch;
	int damageCount;
	int damageShieldCount;
	int stats[16];
	int persistant[16];
	int powerups[16];
	int ammo[16];
	int ping;
	int entityEventSequence;
} stefx_hm_player_state_t;

#define STEFX_HM_MAX_PS_EVENTS 4

typedef struct stefx_hm_usercmd_s {
	int serverTime;
	byte buttons;
	byte weapon;
	int angles[3];
	signed char forwardmove;
	signed char rightmove;
	signed char upmove;
} stefx_hm_usercmd_t;

#define STEFX_HM_MAX_CLIENT_STATES 128
static byte s_stefxHolomatchMirrorStorage[MAX_GENTITIES * sizeof(gentity_t)];
static playerState_t s_stefxHolomatchMirrorClientStates[STEFX_HM_MAX_CLIENT_STATES];

static gentity_t *STEFX_HolomatchMirrorEntity(int entityNum)
{
	if (entityNum < 0 || entityNum >= MAX_GENTITIES)
	{
		return NULL;
	}
	return (gentity_t *)(s_stefxHolomatchMirrorStorage + sizeof(gentity_t) * entityNum);
}

static int STEFX_HolomatchEntityIndex(const void *entity)
{
	unsigned int base;
	unsigned int address;
	unsigned int stride;
	unsigned int delta;

	if (!entity || !s_stefxHolomatchGentities || s_stefxHolomatchGentitySize <= 0)
	{
		return -1;
	}
	base = (unsigned int)s_stefxHolomatchGentities;
	address = (unsigned int)entity;
	stride = (unsigned int)s_stefxHolomatchGentitySize;
	if (address < base)
	{
		return -1;
	}
	delta = address - base;
	if ((delta % stride) != 0 || (delta / stride) >= (unsigned int)MAX_GENTITIES)
	{
		return -1;
	}
	return (int)(delta / stride);
}

static void *STEFX_HolomatchOfficialEntity(int entityNum)
{
	if (!s_stefxHolomatchGentities || s_stefxHolomatchGentitySize <= 0 ||
		entityNum < 0 || entityNum >= MAX_GENTITIES)
	{
		return NULL;
	}
	return (void *)((byte *)s_stefxHolomatchGentities +
		s_stefxHolomatchGentitySize * entityNum);
}

static void STEFX_HolomatchCopyOfficialPlayerStateToSP(playerState_t *dst,
	const stefx_hm_player_state_t *src)
{
	int i;
	memset(dst, 0, sizeof(*dst));
	dst->commandTime = src->commandTime;
	dst->pm_type = src->pm_type;
	dst->bobCycle = src->bobCycle;
	dst->pm_flags = src->pm_flags;
	dst->pm_time = src->pm_time;
	VectorCopy(src->origin, dst->origin);
	VectorCopy(src->velocity, dst->velocity);
	dst->weaponTime = src->weaponTime;
	dst->rechargeTime = src->rechargeTime;
	dst->useTime = src->useTime;
	dst->gravity = src->gravity;
	dst->speed = src->speed;
	for (i = 0; i < 3; ++i)
	{
		dst->delta_angles[i] = src->delta_angles[i];
	}
	dst->groundEntityNum = src->groundEntityNum;
	dst->legsAnimTimer = src->legsTimer;
	dst->legsAnim = src->legsAnim;
	dst->torsoAnimTimer = src->torsoTimer;
	dst->torsoAnim = src->torsoAnim;
	dst->movementDir = src->movementDir;
	dst->eFlags = src->eFlags;
	dst->eventSequence = src->eventSequence;
	for (i = 0; i < MAX_PS_EVENTS && i < STEFX_HM_MAX_PS_EVENTS; ++i)
	{
		dst->events[i] = src->events[i];
		dst->eventParms[i] = src->eventParms[i];
	}
	dst->externalEvent = src->externalEvent;
	dst->externalEventParm = src->externalEventParm;
	dst->externalEventTime = src->externalEventTime;
	dst->clientNum = src->clientNum;
	dst->weapon = src->weapon;
	dst->weaponstate = src->weaponstate;
	VectorCopy(src->viewangles, dst->viewangles);
	dst->viewheight = src->viewheight;
	dst->damageEvent = src->damageEvent;
	dst->damageYaw = src->damageYaw;
	dst->damagePitch = src->damagePitch;
	dst->damageCount = src->damageCount;
	for (i = 0; i < 16; ++i)
	{
		dst->stats[i] = src->stats[i];
		dst->persistant[i] = src->persistant[i];
		dst->powerups[i] = src->powerups[i];
	}
	for (i = 0; i < MAX_AMMO && i < 16; ++i)
	{
		dst->ammo[i] = src->ammo[i];
	}
	dst->ping = src->ping;
}

static void STEFX_HolomatchCopyOfficialStateToSP(entityState_t *dst,
	const stefx_hm_entity_state_t *src)
{
	memset(dst, 0, sizeof(*dst));
	dst->number = src->number;
	dst->eType = src->eType;
	dst->eFlags = src->eFlags;
	dst->pos.trType = (trType_t)STEFX_OfficialTrajectoryTypeToSP(src->pos.trType);
	dst->pos.trTime = src->pos.trTime;
	dst->pos.trDuration = src->pos.trDuration;
	VectorCopy(src->pos.trBase, dst->pos.trBase);
	VectorCopy(src->pos.trDelta, dst->pos.trDelta);
	dst->apos.trType = (trType_t)STEFX_OfficialTrajectoryTypeToSP(src->apos.trType);
	dst->apos.trTime = src->apos.trTime;
	dst->apos.trDuration = src->apos.trDuration;
	VectorCopy(src->apos.trBase, dst->apos.trBase);
	VectorCopy(src->apos.trDelta, dst->apos.trDelta);
	dst->time = src->time;
	dst->time2 = src->time2;
	VectorCopy(src->origin, dst->origin);
	VectorCopy(src->origin2, dst->origin2);
	VectorCopy(src->angles, dst->angles);
	VectorCopy(src->angles2, dst->angles2);
	dst->otherEntityNum = src->otherEntityNum;
	dst->otherEntityNum2 = src->otherEntityNum2;
	dst->groundEntityNum = src->groundEntityNum;
	dst->constantLight = src->constantLight;
	dst->loopSound = src->loopSound;
	dst->modelindex = src->modelindex;
	dst->modelindex2 = src->modelindex2;
	dst->clientNum = src->clientNum;
	dst->frame = src->frame;
	dst->solid = src->solid;
	dst->event = src->event;
	dst->eventParm = src->eventParm;
	dst->powerups = src->powerups;
	dst->weapon = src->weapon;
	dst->legsAnim = src->legsAnim;
	dst->torsoAnim = src->torsoAnim;
}

static void STEFX_HolomatchCopySPStateToOfficial(stefx_hm_entity_state_t *dst,
	const entityState_t *src)
{
	dst->number = src->number;
	dst->eType = src->eType;
	dst->eFlags = src->eFlags;
	dst->pos.trType = STEFX_SPTrajectoryTypeToOfficial(src->pos.trType);
	dst->pos.trTime = src->pos.trTime;
	dst->pos.trDuration = src->pos.trDuration;
	VectorCopy(src->pos.trBase, dst->pos.trBase);
	VectorCopy(src->pos.trDelta, dst->pos.trDelta);
	dst->apos.trType = STEFX_SPTrajectoryTypeToOfficial(src->apos.trType);
	dst->apos.trTime = src->apos.trTime;
	dst->apos.trDuration = src->apos.trDuration;
	VectorCopy(src->apos.trBase, dst->apos.trBase);
	VectorCopy(src->apos.trDelta, dst->apos.trDelta);
	dst->time = src->time;
	dst->time2 = src->time2;
	VectorCopy(src->origin, dst->origin);
	VectorCopy(src->origin2, dst->origin2);
	VectorCopy(src->angles, dst->angles);
	VectorCopy(src->angles2, dst->angles2);
	dst->otherEntityNum = src->otherEntityNum;
	dst->otherEntityNum2 = src->otherEntityNum2;
	dst->groundEntityNum = src->groundEntityNum;
	dst->constantLight = src->constantLight;
	dst->loopSound = src->loopSound;
	dst->modelindex = src->modelindex;
	dst->modelindex2 = src->modelindex2;
	dst->clientNum = src->clientNum;
	dst->frame = src->frame;
	dst->solid = src->solid;
	dst->event = src->event;
	dst->eventParm = src->eventParm;
	dst->powerups = src->powerups;
	dst->weapon = src->weapon;
	dst->legsAnim = src->legsAnim;
	dst->torsoAnim = src->torsoAnim;
}

static void STEFX_HolomatchSyncOfficialEntityToMirror(int entityNum)
{
	stefx_hm_entity_prefix_t *official;
	gentity_t *mirror;
	stefx_hm_player_state_t *officialPS;

	if (entityNum < 0 || entityNum >= MAX_GENTITIES)
	{
		return;
	}
	official = (stefx_hm_entity_prefix_t *)STEFX_HolomatchOfficialEntity(entityNum);
	if (!official)
	{
		return;
	}
	mirror = STEFX_HolomatchMirrorEntity(entityNum);
	memset(mirror, 0, sizeof(*mirror));
	STEFX_HolomatchCopyOfficialStateToSP(&mirror->s, &official->s);
	mirror->inuse = official->inuse;
	mirror->linked = official->r.linked;
	mirror->svFlags = official->r.svFlags;
	mirror->singleClient = official->r.singleClient;
	mirror->bmodel = official->r.bmodel;
	VectorCopy(official->r.mins, mirror->mins);
	VectorCopy(official->r.maxs, mirror->maxs);
	mirror->contents = official->r.contents;
	VectorCopy(official->r.absmin, mirror->absmin);
	VectorCopy(official->r.absmax, mirror->absmax);
	VectorCopy(official->r.currentOrigin, mirror->currentOrigin);
	VectorCopy(official->r.currentAngles, mirror->currentAngles);
	mirror->owner = NULL;
	if (official->r.ownerNum >= 0 && official->r.ownerNum < MAX_GENTITIES &&
		official->r.ownerNum != ENTITYNUM_NONE)
	{
		mirror->owner = STEFX_HolomatchMirrorEntity(official->r.ownerNum);
	}
	if (official->client && entityNum < STEFX_HM_MAX_CLIENT_STATES)
	{
		officialPS = (stefx_hm_player_state_t *)official->client;
		STEFX_HolomatchCopyOfficialPlayerStateToSP(&s_stefxHolomatchMirrorClientStates[entityNum], officialPS);
		mirror->client = &s_stefxHolomatchMirrorClientStates[entityNum];
	}
}

static void STEFX_HolomatchSyncMirrorToOfficial(int entityNum)
{
	stefx_hm_entity_prefix_t *official;
	gentity_t *mirror;

	if (entityNum < 0 || entityNum >= MAX_GENTITIES)
	{
		return;
	}
	official = (stefx_hm_entity_prefix_t *)STEFX_HolomatchOfficialEntity(entityNum);
	if (!official)
	{
		return;
	}
	mirror = STEFX_HolomatchMirrorEntity(entityNum);
	STEFX_HolomatchCopySPStateToOfficial(&official->s, &mirror->s);
	official->inuse = mirror->inuse;
	official->r.linked = mirror->linked;
	official->r.svFlags = mirror->svFlags;
	official->r.singleClient = mirror->singleClient;
	official->r.bmodel = mirror->bmodel;
	VectorCopy(mirror->mins, official->r.mins);
	VectorCopy(mirror->maxs, official->r.maxs);
	official->r.contents = mirror->contents;
	VectorCopy(mirror->absmin, official->r.absmin);
	VectorCopy(mirror->absmax, official->r.absmax);
	VectorCopy(mirror->currentOrigin, official->r.currentOrigin);
	VectorCopy(mirror->currentAngles, official->r.currentAngles);
	official->r.ownerNum = mirror->owner ? mirror->owner->s.number : ENTITYNUM_NONE;
	if (official->client && entityNum < STEFX_HM_MAX_CLIENT_STATES && mirror->client &&
		s_stefxHolomatchClientStatePreserveLogBudget > 0)
	{
		const stefx_hm_player_state_t *officialPS =
			(const stefx_hm_player_state_t *)official->client;
		const playerState_t *mirrorPS = (const playerState_t *)mirror->client;
		XBLog_WriteCriticalf("STEFX_HM_STATE: preserve client=%d eventSeq=%d cursor=%d mirrorEventSeq=%d intro=%d shieldDamage=%d",
			entityNum,
			officialPS->eventSequence,
			officialPS->entityEventSequence,
			mirrorPS->eventSequence,
			officialPS->introTime,
			officialPS->damageShieldCount);
		--s_stefxHolomatchClientStatePreserveLogBudget;
	}
	/* Engine entity imports update linkage and bounds only.  The official
	 * Holomatch player state remains authoritative; the SP-shaped client is a
	 * read-only projection and omits private EF fields such as introTime and
	 * entityEventSequence. */
}

static void STEFX_HolomatchSyncAllToMirror(void)
{
	int i;
	int count = s_stefxHolomatchNumEntities;
	if (count < 0 || count > MAX_GENTITIES)
	{
		count = MAX_GENTITIES;
	}
	for (i = 0; i < count; ++i)
	{
		STEFX_HolomatchSyncOfficialEntityToMirror(i);
	}
}

static void STEFX_HolomatchRefreshExport(void)
{
	s_stefxHolomatchExport.gentities = s_stefxHolomatchGentities ? STEFX_HolomatchMirrorEntity(0) : NULL;
	s_stefxHolomatchExport.gentitySize = s_stefxHolomatchGentities ? sizeof(gentity_t) : 0;
	s_stefxHolomatchExport.num_entities = s_stefxHolomatchNumEntities;
}

extern "C" void STEFX_HolomatchMarkBot(int clientNum)
{
	stefx_hm_entity_prefix_t *official =
		(stefx_hm_entity_prefix_t *)STEFX_HolomatchOfficialEntity(clientNum);

	if (!official)
	{
		return;
	}
	official->r.svFlags |= SVF_BOT;
	official->inuse = qtrue;
	STEFX_HolomatchSyncOfficialEntityToMirror(clientNum);
	STEFX_HolomatchRefreshExport();
}

enum stefxHolomatchGameImportCommand_e
{
	STEFX_HM_G_PRINT = 0,
	STEFX_HM_G_ERROR,
	STEFX_HM_G_MILLISECONDS,
	STEFX_HM_G_CVAR_REGISTER,
	STEFX_HM_G_CVAR_UPDATE,
	STEFX_HM_G_CVAR_SET,
	STEFX_HM_G_CVAR_VARIABLE_INTEGER_VALUE,
	STEFX_HM_G_CVAR_VARIABLE_STRING_BUFFER,
	STEFX_HM_G_ARGC,
	STEFX_HM_G_ARGV,
	STEFX_HM_G_FS_FOPEN_FILE,
	STEFX_HM_G_FS_READ,
	STEFX_HM_G_FS_WRITE,
	STEFX_HM_G_FS_FCLOSE_FILE,
	STEFX_HM_G_SEND_CONSOLE_COMMAND,
	STEFX_HM_G_LOCATE_GAME_DATA,
	STEFX_HM_G_DROP_CLIENT,
	STEFX_HM_G_SEND_SERVER_COMMAND,
	STEFX_HM_G_SET_CONFIGSTRING,
	STEFX_HM_G_GET_CONFIGSTRING,
	STEFX_HM_G_GET_USERINFO,
	STEFX_HM_G_SET_USERINFO,
	STEFX_HM_G_GET_SERVERINFO,
	STEFX_HM_G_SET_BRUSH_MODEL,
	STEFX_HM_G_TRACE,
	STEFX_HM_G_POINT_CONTENTS,
	STEFX_HM_G_IN_PVS,
	STEFX_HM_G_IN_PVS_IGNORE_PORTALS,
	STEFX_HM_G_ADJUST_AREA_PORTAL_STATE,
	STEFX_HM_G_AREAS_CONNECTED,
	STEFX_HM_G_LINKENTITY,
	STEFX_HM_G_UNLINKENTITY,
	STEFX_HM_G_ENTITIES_IN_BOX,
	STEFX_HM_G_ENTITY_CONTACT,
	STEFX_HM_G_BOT_ALLOCATE_CLIENT,
	STEFX_HM_G_BOT_FREE_CLIENT,
	STEFX_HM_G_GET_USERCMD,
	STEFX_HM_G_GET_ENTITY_TOKEN,
	STEFX_HM_G_FS_GETFILELIST,
	STEFX_HM_G_DEBUG_POLYGON_CREATE,
	STEFX_HM_G_DEBUG_POLYGON_DELETE,

	STEFX_HM_BOTLIB_SETUP = 200,
	STEFX_HM_BOTLIB_SHUTDOWN,
	STEFX_HM_BOTLIB_LIBVAR_SET,
	STEFX_HM_BOTLIB_LIBVAR_GET,
	STEFX_HM_BOTLIB_DEFINE,
	STEFX_HM_BOTLIB_START_FRAME,
	STEFX_HM_BOTLIB_LOAD_MAP,
	STEFX_HM_BOTLIB_UPDATE_ENTITY,
	STEFX_HM_BOTLIB_TEST,
	STEFX_HM_BOTLIB_GET_SNAPSHOT_ENTITY,
	STEFX_HM_BOTLIB_GET_CONSOLE_MESSAGE,
	STEFX_HM_BOTLIB_USER_COMMAND,
	STEFX_HM_BOTLIB_AAS_ENTITY_VISIBLE = 300,
	STEFX_HM_BOTLIB_AAS_IN_FIELD_OF_VISION,
	STEFX_HM_BOTLIB_AAS_VISIBLE_CLIENTS,
	STEFX_HM_BOTLIB_AAS_ENTITY_INFO,
	STEFX_HM_BOTLIB_AAS_INITIALIZED,
	STEFX_HM_BOTLIB_AAS_PRESENCE_TYPE_BOUNDING_BOX,
	STEFX_HM_BOTLIB_AAS_TIME,
	STEFX_HM_BOTLIB_AAS_POINT_AREA_NUM,
	STEFX_HM_BOTLIB_AAS_TRACE_AREAS,
	STEFX_HM_BOTLIB_AAS_POINT_CONTENTS,
	STEFX_HM_BOTLIB_AAS_NEXT_BSP_ENTITY,
	STEFX_HM_BOTLIB_AAS_VALUE_FOR_BSP_EPAIR_KEY,
	STEFX_HM_BOTLIB_AAS_VECTOR_FOR_BSP_EPAIR_KEY,
	STEFX_HM_BOTLIB_AAS_FLOAT_FOR_BSP_EPAIR_KEY,
	STEFX_HM_BOTLIB_AAS_INT_FOR_BSP_EPAIR_KEY,
	STEFX_HM_BOTLIB_AAS_AREA_REACHABILITY,
	STEFX_HM_BOTLIB_AAS_AREA_TRAVEL_TIME_TO_GOAL_AREA,
	STEFX_HM_BOTLIB_AAS_SWIMMING,
	STEFX_HM_BOTLIB_AAS_PREDICT_CLIENT_MOVEMENT,
	STEFX_HM_BOTLIB_EA_SAY = 400,
	STEFX_HM_BOTLIB_EA_SAY_TEAM,
	STEFX_HM_BOTLIB_EA_USE_ITEM,
	STEFX_HM_BOTLIB_EA_DROP_ITEM,
	STEFX_HM_BOTLIB_EA_USE_INV,
	STEFX_HM_BOTLIB_EA_DROP_INV,
	STEFX_HM_BOTLIB_EA_GESTURE,
	STEFX_HM_BOTLIB_EA_COMMAND,
	STEFX_HM_BOTLIB_EA_SELECT_WEAPON,
	STEFX_HM_BOTLIB_EA_TALK,
	STEFX_HM_BOTLIB_EA_ATTACK,
	STEFX_HM_BOTLIB_EA_USE,
	STEFX_HM_BOTLIB_EA_RESPAWN,
	STEFX_HM_BOTLIB_EA_JUMP,
	STEFX_HM_BOTLIB_EA_DELAYED_JUMP,
	STEFX_HM_BOTLIB_EA_CROUCH,
	STEFX_HM_BOTLIB_EA_MOVE_UP,
	STEFX_HM_BOTLIB_EA_MOVE_DOWN,
	STEFX_HM_BOTLIB_EA_MOVE_FORWARD,
	STEFX_HM_BOTLIB_EA_MOVE_BACK,
	STEFX_HM_BOTLIB_EA_MOVE_LEFT,
	STEFX_HM_BOTLIB_EA_MOVE_RIGHT,
	STEFX_HM_BOTLIB_EA_MOVE,
	STEFX_HM_BOTLIB_EA_VIEW,
	STEFX_HM_BOTLIB_EA_END_REGULAR,
	STEFX_HM_BOTLIB_EA_GET_INPUT,
	STEFX_HM_BOTLIB_EA_RESET_INPUT
};

static int QDECL STEFX_HolomatchSyscall( int command, ... )
{
	va_list args;
	va_start(args, command);
	int result = 0;

	if (!s_stefxHolomatchImport)
	{
		va_end(args);
		return 0;
	}
	if (s_stefxHolomatchSyscallTraceCount < 96)
	{
		++s_stefxHolomatchSyscallTraceCount;
		XBLF("STEFX: SP-hosted official syscall enter command=%d", command);
	}

	switch (command)
	{
	case STEFX_HM_G_PRINT:
		{
			const char *text = va_arg(args, const char *);
			if (text && (strstr(text, "bots parsed") ||
				strstr(text, "Missing { in info file") ||
				strstr(text, "Unexpected end of info file") ||
				strstr(text, "Max infos exceeded")))
			{
				XBLog_WriteCriticalf("STEFX_HM_BOT: official print text='%s'", text);
			}
			s_stefxHolomatchImport->Printf("%s", text ? text : "");
		}
		break;
	case STEFX_HM_G_ERROR:
		s_stefxHolomatchImport->Error(ERR_DROP, "%s", va_arg(args, const char *));
		break;
	case STEFX_HM_G_MILLISECONDS:
		result = s_stefxHolomatchImport->Milliseconds();
		break;
	case STEFX_HM_G_CVAR_REGISTER:
		{
			vmCvar_t *vmCvar = (vmCvar_t *)va_arg(args, void *);
			const char *name = va_arg(args, const char *);
			const char *defaultValue = va_arg(args, const char *);
			int flags = va_arg(args, int);
			XBLog_WriteCriticalf("STEFX_HM_SP: cvar register enter name='%s' default='%s' flags=%d vm=%p",
				name ? name : "", defaultValue ? defaultValue : "", flags, vmCvar);
			Cvar_Register(vmCvar, name, defaultValue, flags);
			XBLog_WriteCriticalf("STEFX_HM_SP: cvar register exit name='%s' handle=%d mod=%d int=%d",
				name ? name : "", vmCvar ? vmCvar->handle : -1,
				vmCvar ? vmCvar->modificationCount : -1,
				vmCvar ? vmCvar->integer : 0);
		}
		break;
	case STEFX_HM_G_CVAR_UPDATE:
		Cvar_Update((vmCvar_t *)va_arg(args, void *));
		break;
	case STEFX_HM_G_CVAR_SET:
		{
			const char *name = va_arg(args, const char *);
			const char *value = va_arg(args, const char *);
			XBLog_WriteCriticalf("STEFX_HM_SP: cvar_set enter name='%s' value='%s' import=%p target=%p",
				name ? name : "", value ? value : "", s_stefxHolomatchImport,
				s_stefxHolomatchImport ? (void *)s_stefxHolomatchImport->cvar_set : NULL);
			s_stefxHolomatchImport->cvar_set(name, value);
			XBLog_WriteCritical("STEFX_HM_SP: cvar_set exit");
		}
		break;
	case STEFX_HM_G_CVAR_VARIABLE_INTEGER_VALUE:
		{
			const char *name = va_arg(args, const char *);
			XBLF("STEFX: SP-hosted cvar_int enter name='%s'", name);
			result = s_stefxHolomatchImport->Cvar_VariableIntegerValue(name);
			XBLF("STEFX: SP-hosted cvar_int exit name='%s' value=%d", name, result);
		}
		break;
	case STEFX_HM_G_CVAR_VARIABLE_STRING_BUFFER:
		{
			const char *name = va_arg(args, const char *);
			char *buffer = va_arg(args, char *);
			int bufferSize = va_arg(args, int);
			s_stefxHolomatchImport->Cvar_VariableStringBuffer(name, buffer, bufferSize);
		}
		break;
	case STEFX_HM_G_ARGC:
		result = s_stefxHolomatchImport->argc();
		break;
	case STEFX_HM_G_ARGV:
		{
			int n = va_arg(args, int);
			char *buffer = va_arg(args, char *);
			int bufferLength = va_arg(args, int);
			Q_strncpyz(buffer, s_stefxHolomatchImport->argv(n), bufferLength);
		}
		break;
	case STEFX_HM_G_FS_FOPEN_FILE:
		{
			const char *qpath = va_arg(args, const char *);
			fileHandle_t *file = (fileHandle_t *)va_arg(args, void *);
			fsMode_t mode = (fsMode_t)va_arg(args, int);
			XBLog_WriteCriticalf("STEFX_HM_SP: fs open enter path='%s' mode=%d file=%p",
				qpath ? qpath : "", mode, file);
			result = s_stefxHolomatchImport->FS_FOpenFile(qpath, file, mode);
			XBLog_WriteCriticalf("STEFX_HM_SP: fs open exit path='%s' len=%d handle=%d",
				qpath ? qpath : "", result, file ? *file : -1);
		}
		break;
	case STEFX_HM_G_FS_READ:
		{
			void *buffer = va_arg(args, void *);
			int len = va_arg(args, int);
			fileHandle_t file = va_arg(args, fileHandle_t);
			XBLog_WriteCriticalf("STEFX_HM_SP: fs read enter buffer=%p len=%d handle=%d",
				buffer, len, file);
			s_stefxHolomatchImport->FS_Read(buffer, len, file);
			XBLog_WriteCritical("STEFX_HM_SP: fs read exit");
		}
		break;
	case STEFX_HM_G_FS_WRITE:
		{
			const void *buffer = va_arg(args, const void *);
			int len = va_arg(args, int);
			fileHandle_t file = va_arg(args, fileHandle_t);
			result = s_stefxHolomatchImport->FS_Write(buffer, len, file);
		}
		break;
	case STEFX_HM_G_FS_FCLOSE_FILE:
		{
			fileHandle_t file = va_arg(args, fileHandle_t);
			XBLog_WriteCriticalf("STEFX_HM_SP: fs close enter handle=%d", file);
			s_stefxHolomatchImport->FS_FCloseFile(file);
			XBLog_WriteCritical("STEFX_HM_SP: fs close exit");
		}
		break;
	case STEFX_HM_G_SEND_CONSOLE_COMMAND:
		{
			int execWhen = va_arg(args, int);
			const char *text = va_arg(args, const char *);
			XBLog_WriteCriticalf("STEFX_HM_BOT: official console command exec=%d text='%s'",
				execWhen, text ? text : "");
			Cbuf_ExecuteText(execWhen, text ? text : "");
		}
		break;
	case STEFX_HM_G_LOCATE_GAME_DATA:
		s_stefxHolomatchGentities = va_arg(args, void *);
		s_stefxHolomatchNumEntities = va_arg(args, int);
		s_stefxHolomatchGentitySize = va_arg(args, int);
		va_arg(args, void *);
		va_arg(args, int);
		STEFX_HolomatchSyncAllToMirror();
		STEFX_HolomatchRefreshExport();
		XBLF("STEFX_HM_SP: G_LOCATE_GAME_DATA official=%p count=%d stride=%d mirror=%p mirrorStride=%d",
			s_stefxHolomatchGentities,
			s_stefxHolomatchNumEntities,
			s_stefxHolomatchGentitySize,
			STEFX_HolomatchMirrorEntity(0),
			(int)sizeof(gentity_t));
		break;
	case STEFX_HM_G_DROP_CLIENT:
		{
			int clientNum = va_arg(args, int);
			const char *reason = va_arg(args, const char *);
			s_stefxHolomatchImport->DropClient(clientNum, reason);
		}
		break;
	case STEFX_HM_G_SEND_SERVER_COMMAND:
		{
			int clientNum = va_arg(args, int);
			const char *commandText = va_arg(args, const char *);
			s_stefxHolomatchImport->SendServerCommand(clientNum, "%s", commandText);
		}
		break;
	case STEFX_HM_G_SET_CONFIGSTRING:
		{
			int num = va_arg(args, int);
			const char *value = va_arg(args, const char *);
			XBLog_WriteCriticalf("STEFX_HM_SP: config set num=%d value='%s'",
				num, value ? value : "");
			s_stefxHolomatchImport->SetConfigstring(num, value);
		}
		break;
	case STEFX_HM_G_GET_CONFIGSTRING:
		{
			int num = va_arg(args, int);
			char *buffer = va_arg(args, char *);
			int bufferSize = va_arg(args, int);
			s_stefxHolomatchImport->GetConfigstring(num, buffer, bufferSize);
			if (num == 288 || num == 289 || num == 290 || num == 543 || num == 544)
			{
				XBLog_WriteCriticalf("STEFX_HM_SP: config get num=%d value='%s'", num,
					buffer ? buffer : "");
			}
		}
		break;
	case STEFX_HM_G_GET_USERINFO:
		{
			int clientNum = va_arg(args, int);
			char *buffer = va_arg(args, char *);
			int bufferSize = va_arg(args, int);
			XBLog_WriteCriticalf("STEFX_HM_SP: get userinfo enter client=%d buffer=%p size=%d",
				clientNum, buffer, bufferSize);
			s_stefxHolomatchImport->GetUserinfo(clientNum, buffer, bufferSize);
			XBLog_WriteCriticalf("STEFX_HM_SP: get userinfo exit client=%d value='%s'",
				clientNum, buffer ? buffer : "");
		}
		break;
	case STEFX_HM_G_SET_USERINFO:
		{
			int clientNum = va_arg(args, int);
			const char *userinfo = va_arg(args, const char *);
			s_stefxHolomatchImport->SetUserinfo(clientNum, userinfo);
		}
		break;
	case STEFX_HM_G_GET_SERVERINFO:
		{
			char *buffer = va_arg(args, char *);
			int bufferSize = va_arg(args, int);
			s_stefxHolomatchImport->GetServerinfo(buffer, bufferSize);
		}
		break;
	case STEFX_HM_G_SET_BRUSH_MODEL:
		{
			void *officialEntity = va_arg(args, void *);
			const char *name = va_arg(args, const char *);
			int entityNum = STEFX_HolomatchEntityIndex(officialEntity);
			STEFX_HolomatchSyncOfficialEntityToMirror(entityNum);
			if (entityNum >= 0)
			{
				s_stefxHolomatchImport->SetBrushModel(STEFX_HolomatchMirrorEntity(entityNum), name);
				STEFX_HolomatchSyncMirrorToOfficial(entityNum);
			}
		}
		break;
	case STEFX_HM_G_TRACE:
		{
			trace_t *traceResult = (trace_t *)va_arg(args, void *);
			const float *start = va_arg(args, const float *);
			const float *mins = va_arg(args, const float *);
			const float *maxs = va_arg(args, const float *);
			const float *end = va_arg(args, const float *);
			int passEntityNum = va_arg(args, int);
			int contentmask = va_arg(args, int);
			int traceSequence = s_stefxHolomatchTraceDetailCount++;
			qboolean combatTraceProbe = qfalse;
			int combatTraceClientMask = 0;
			int combatTraceEntityCount = 0;
			if (start && end && !mins && !maxs &&
				(passEntityNum == 1 || passEntityNum == 2) &&
				s_stefxHolomatchCombatTraceProbeCount < 48)
			{
				vec3_t delta;
				VectorSubtract(end, start, delta);
				combatTraceProbe = (qboolean)(VectorLengthSquared(delta) > (1024.0f * 1024.0f));
			}
			if (combatTraceProbe)
			{
				vec3_t queryMins;
				vec3_t queryMaxs;
				gentity_t *queryEntities[64];
				int queryIndex;
				int axis;
				for (axis = 0; axis < 3; ++axis)
				{
					queryMins[axis] = (start[axis] < end[axis] ? start[axis] : end[axis]) - 1.0f;
					queryMaxs[axis] = (start[axis] > end[axis] ? start[axis] : end[axis]) + 1.0f;
				}
				combatTraceEntityCount = s_stefxHolomatchImport->EntitiesInBox(
					queryMins, queryMaxs, queryEntities, 64);
				for (queryIndex = 0; queryIndex < combatTraceEntityCount; ++queryIndex)
				{
					if (queryEntities[queryIndex] == STEFX_HolomatchMirrorEntity(0))
					{
						combatTraceClientMask |= 1;
					}
					else if (queryEntities[queryIndex] == STEFX_HolomatchMirrorEntity(1))
					{
						combatTraceClientMask |= 2;
					}
					else if (queryEntities[queryIndex] == STEFX_HolomatchMirrorEntity(2))
					{
						combatTraceClientMask |= 4;
					}
				}
				{
					gentity_t *localMirror = STEFX_HolomatchMirrorEntity(0);
					XBLog_WriteCriticalf("STEFX_HM_COMBAT_TRACE phase=before pass=%d mask=0x%x areaCount=%d clientMask=0x%x localLinked=%d localContents=0x%x localOrigin=(%g,%g,%g) localAbsmin=(%g,%g,%g) localAbsmax=(%g,%g,%g)",
						passEntityNum,
						contentmask,
						combatTraceEntityCount,
						combatTraceClientMask,
						localMirror ? localMirror->linked : -1,
						localMirror ? localMirror->contents : 0,
						localMirror ? localMirror->currentOrigin[0] : 0.0f,
						localMirror ? localMirror->currentOrigin[1] : 0.0f,
						localMirror ? localMirror->currentOrigin[2] : 0.0f,
						localMirror ? localMirror->absmin[0] : 0.0f,
						localMirror ? localMirror->absmin[1] : 0.0f,
						localMirror ? localMirror->absmin[2] : 0.0f,
						localMirror ? localMirror->absmax[0] : 0.0f,
						localMirror ? localMirror->absmax[1] : 0.0f,
						localMirror ? localMirror->absmax[2] : 0.0f);
				}
			}
			if (traceSequence < 48)
			{
				XBLog_WriteCriticalf("STEFX_HM_SP: trace enter seq=%d pass=%d mask=0x%x start=(%g,%g,%g) mins=(%g,%g,%g) maxs=(%g,%g,%g) end=(%g,%g,%g)",
					traceSequence, passEntityNum, contentmask,
					start ? start[0] : 0.0f, start ? start[1] : 0.0f, start ? start[2] : 0.0f,
					mins ? mins[0] : 0.0f, mins ? mins[1] : 0.0f, mins ? mins[2] : 0.0f,
					maxs ? maxs[0] : 0.0f, maxs ? maxs[1] : 0.0f, maxs ? maxs[2] : 0.0f,
					end ? end[0] : 0.0f, end ? end[1] : 0.0f, end ? end[2] : 0.0f);
			}
			if ((passEntityNum < 0 || passEntityNum >= MAX_GENTITIES) &&
				s_stefxHolomatchInvalidTraceTraceCount < 32)
			{
				XBLog_WriteCriticalf("STEFX_HM_SP: invalid official trace pass=%d mask=0x%x start=(%g,%g,%g) end=(%g,%g,%g)",
					passEntityNum, contentmask,
					start ? start[0] : 0.0f, start ? start[1] : 0.0f, start ? start[2] : 0.0f,
					end ? end[0] : 0.0f, end ? end[1] : 0.0f, end ? end[2] : 0.0f);
				++s_stefxHolomatchInvalidTraceTraceCount;
			}
			if (s_stefxHolomatchBadTraceInputCount < 32 &&
				((start && (IS_NAN(start[0]) || IS_NAN(start[1]) || IS_NAN(start[2]))) ||
				 (mins && (IS_NAN(mins[0]) || IS_NAN(mins[1]) || IS_NAN(mins[2]))) ||
				 (maxs && (IS_NAN(maxs[0]) || IS_NAN(maxs[1]) || IS_NAN(maxs[2]))) ||
				 (end && (IS_NAN(end[0]) || IS_NAN(end[1]) || IS_NAN(end[2])))))
			{
				XBLog_WriteCriticalf("STEFX_HM_SP: invalid official trace input pass=%d mask=0x%x start=(%g,%g,%g) mins=(%g,%g,%g) maxs=(%g,%g,%g) end=(%g,%g,%g)",
					passEntityNum, contentmask,
					start ? start[0] : 0.0f, start ? start[1] : 0.0f, start ? start[2] : 0.0f,
					mins ? mins[0] : 0.0f, mins ? mins[1] : 0.0f, mins ? mins[2] : 0.0f,
					maxs ? maxs[0] : 0.0f, maxs ? maxs[1] : 0.0f, maxs ? maxs[2] : 0.0f,
					end ? end[0] : 0.0f, end ? end[1] : 0.0f, end ? end[2] : 0.0f);
				++s_stefxHolomatchBadTraceInputCount;
			}
			s_stefxHolomatchImport->trace(traceResult, start, mins, maxs, end,
				passEntityNum, contentmask);
			if (combatTraceProbe)
			{
				XBLog_WriteCriticalf("STEFX_HM_COMBAT_TRACE phase=after pass=%d areaCount=%d clientMask=0x%x hit=%d fraction=%g startsolid=%d allsolid=%d end=(%g,%g,%g)",
					passEntityNum,
					combatTraceEntityCount,
					combatTraceClientMask,
					traceResult ? traceResult->entityNum : -1,
					traceResult ? traceResult->fraction : -1.0f,
					traceResult ? traceResult->startsolid : 0,
					traceResult ? traceResult->allsolid : 0,
					traceResult ? traceResult->endpos[0] : 0.0f,
					traceResult ? traceResult->endpos[1] : 0.0f,
					traceResult ? traceResult->endpos[2] : 0.0f);
				++s_stefxHolomatchCombatTraceProbeCount;
			}
			if (traceResult && passEntityNum >= 0 && passEntityNum < 3 &&
				((traceResult->entityNum >= 0 && traceResult->entityNum < 3 &&
				  traceResult->entityNum != passEntityNum) || traceResult->startsolid) &&
				s_stefxHolomatchClientCollisionTraceCount < 96)
			{
				gentity_t *hit = (traceResult->entityNum >= 0 && traceResult->entityNum < 3) ?
					STEFX_HolomatchMirrorEntity(traceResult->entityNum) : NULL;
				XBLog_WriteCriticalf("STEFX_HM_COLLISION pass=%d hit=%d fraction=%g startsolid=%d "
					"mask=0x%x hitLinked=%d hitContents=0x%x hitOrigin=(%g,%g,%g)",
					passEntityNum, traceResult->entityNum, traceResult->fraction,
					traceResult->startsolid, contentmask,
					hit ? hit->linked : -1, hit ? hit->contents : 0,
					hit ? hit->currentOrigin[0] : 0.0f,
					hit ? hit->currentOrigin[1] : 0.0f,
					hit ? hit->currentOrigin[2] : 0.0f);
				++s_stefxHolomatchClientCollisionTraceCount;
			}
			if (traceSequence < 48 && traceResult)
			{
				XBLog_WriteCriticalf("STEFX_HM_SP: trace exit seq=%d allsolid=%d startsolid=%d fraction=%g end=(%g,%g,%g) plane=(%g,%g,%g) dist=%g entity=%d",
					traceSequence, traceResult->allsolid, traceResult->startsolid, traceResult->fraction,
					traceResult->endpos[0], traceResult->endpos[1], traceResult->endpos[2],
					traceResult->plane.normal[0], traceResult->plane.normal[1], traceResult->plane.normal[2],
					traceResult->plane.dist, traceResult->entityNum);
			}
			if (traceResult &&
				(IS_NAN(traceResult->fraction) ||
				 IS_NAN(traceResult->endpos[0]) ||
				 IS_NAN(traceResult->endpos[1]) ||
				 IS_NAN(traceResult->endpos[2])) &&
				s_stefxHolomatchBadTraceResultCount < 32)
			{
				XBLog_WriteCriticalf("STEFX_HM_SP: invalid SP trace result pass=%d mask=0x%x fraction=%g end=(%g,%g,%g) entity=%d",
					passEntityNum, contentmask, traceResult->fraction,
					traceResult->endpos[0], traceResult->endpos[1], traceResult->endpos[2],
					traceResult->entityNum);
				++s_stefxHolomatchBadTraceResultCount;
			}
			if (traceResult && traceResult->fraction < 1.0f &&
				(traceResult->fraction < 0.0f ||
				 traceResult->plane.normal[0] < -1.01f || traceResult->plane.normal[0] > 1.01f ||
				 traceResult->plane.normal[1] < -1.01f || traceResult->plane.normal[1] > 1.01f ||
				 traceResult->plane.normal[2] < -1.01f || traceResult->plane.normal[2] > 1.01f ||
				 IS_NAN(traceResult->plane.normal[0]) ||
				 IS_NAN(traceResult->plane.normal[1]) ||
				 IS_NAN(traceResult->plane.normal[2]) ||
				 IS_NAN(traceResult->plane.dist)) &&
				s_stefxHolomatchBadTraceResultCount < 32)
			{
				XBLog_WriteCriticalf("STEFX_HM_SP: invalid SP trace plane pass=%d fraction=%g normal=(%g,%g,%g) dist=%g entity=%d",
					passEntityNum, traceResult->fraction,
					traceResult->plane.normal[0], traceResult->plane.normal[1], traceResult->plane.normal[2],
					traceResult->plane.dist, traceResult->entityNum);
				++s_stefxHolomatchBadTraceResultCount;
			}
		}
		break;
	case STEFX_HM_G_POINT_CONTENTS:
		{
			const float *point = va_arg(args, const float *);
			int passEntityNum = va_arg(args, int);
			int pointContentsSequence = s_stefxHolomatchPointContentsTraceCount++;
			if (pointContentsSequence < 48)
			{
				XBLog_WriteCriticalf("STEFX_HM_SP: pointcontents enter seq=%d pass=%d point=(%g,%g,%g) fn=%p",
					pointContentsSequence, passEntityNum,
					point ? point[0] : 0.0f, point ? point[1] : 0.0f, point ? point[2] : 0.0f,
					s_stefxHolomatchImport->pointcontents);
			}
			result = s_stefxHolomatchImport->pointcontents(point, passEntityNum);
			if (pointContentsSequence < 48)
			{
				XBLog_WriteCriticalf("STEFX_HM_SP: pointcontents exit seq=%d result=%d",
					pointContentsSequence, result);
			}
		}
		break;
	case STEFX_HM_G_IN_PVS:
		{
			const float *point1 = va_arg(args, const float *);
			const float *point2 = va_arg(args, const float *);
			result = s_stefxHolomatchImport->inPVS(point1, point2);
		}
		break;
	case STEFX_HM_G_IN_PVS_IGNORE_PORTALS:
		{
			const float *point1 = va_arg(args, const float *);
			const float *point2 = va_arg(args, const float *);
			result = s_stefxHolomatchImport->inPVSIgnorePortals(point1, point2);
		}
		break;
	case STEFX_HM_G_ADJUST_AREA_PORTAL_STATE:
		{
			void *officialEntity = va_arg(args, void *);
			qboolean open = (qboolean)va_arg(args, int);
			int entityNum = STEFX_HolomatchEntityIndex(officialEntity);
			STEFX_HolomatchSyncOfficialEntityToMirror(entityNum);
			if (entityNum >= 0)
			{
				s_stefxHolomatchImport->AdjustAreaPortalState(STEFX_HolomatchMirrorEntity(entityNum), open);
			}
		}
		break;
	case STEFX_HM_G_AREAS_CONNECTED:
		{
			int area1 = va_arg(args, int);
			int area2 = va_arg(args, int);
			result = s_stefxHolomatchImport->AreasConnected(area1, area2);
		}
		break;
	case STEFX_HM_G_LINKENTITY:
		{
			int entityNum = STEFX_HolomatchEntityIndex(va_arg(args, void *));
			STEFX_HolomatchSyncOfficialEntityToMirror(entityNum);
			if (entityNum >= 0)
			{
				gentity_t *mirror = STEFX_HolomatchMirrorEntity(entityNum);
				if (entityNum < 3 && s_stefxHolomatchClientLinkTraceCount < 48)
				{
					XBLog_WriteCriticalf("STEFX_HM_LINK phase=before ent=%d linked=%d contents=0x%x "
						"mins=(%g,%g,%g) maxs=(%g,%g,%g) origin=(%g,%g,%g)",
						entityNum, mirror->linked, mirror->contents,
						mirror->mins[0], mirror->mins[1], mirror->mins[2],
						mirror->maxs[0], mirror->maxs[1], mirror->maxs[2],
						mirror->currentOrigin[0], mirror->currentOrigin[1], mirror->currentOrigin[2]);
				}
				s_stefxHolomatchImport->linkentity(mirror);
				if (entityNum < 3 && s_stefxHolomatchClientLinkTraceCount < 48)
				{
					XBLog_WriteCriticalf("STEFX_HM_LINK phase=after ent=%d linked=%d contents=0x%x "
						"absmin=(%g,%g,%g) absmax=(%g,%g,%g)",
						entityNum, mirror->linked, mirror->contents,
						mirror->absmin[0], mirror->absmin[1], mirror->absmin[2],
						mirror->absmax[0], mirror->absmax[1], mirror->absmax[2]);
					++s_stefxHolomatchClientLinkTraceCount;
				}
				STEFX_HolomatchSyncMirrorToOfficial(entityNum);
			}
		}
		break;
	case STEFX_HM_G_UNLINKENTITY:
		{
			int entityNum = STEFX_HolomatchEntityIndex(va_arg(args, void *));
			STEFX_HolomatchSyncOfficialEntityToMirror(entityNum);
			if (entityNum >= 0)
			{
				s_stefxHolomatchImport->unlinkentity(STEFX_HolomatchMirrorEntity(entityNum));
				STEFX_HolomatchSyncMirrorToOfficial(entityNum);
			}
		}
		break;
	case STEFX_HM_G_ENTITIES_IN_BOX:
		{
			const float *mins = va_arg(args, const float *);
			const float *maxs = va_arg(args, const float *);
			int *entityList = va_arg(args, int *);
			int maxcount = va_arg(args, int);
			gentity_t *mirrorList[MAX_GENTITIES];
			int i;
			if (maxcount > MAX_GENTITIES)
			{
				maxcount = MAX_GENTITIES;
			}
			result = s_stefxHolomatchImport->EntitiesInBox(mins, maxs, mirrorList, maxcount);
			for (i = 0; i < result; ++i)
			{
				entityList[i] = mirrorList[i] ? mirrorList[i]->s.number : ENTITYNUM_NONE;
			}
		}
		break;
	case STEFX_HM_G_ENTITY_CONTACT:
		{
			const float *mins = va_arg(args, const float *);
			const float *maxs = va_arg(args, const float *);
			int entityNum = STEFX_HolomatchEntityIndex(va_arg(args, const void *));
			STEFX_HolomatchSyncOfficialEntityToMirror(entityNum);
			result = (entityNum >= 0) ? s_stefxHolomatchImport->EntityContact(mins, maxs,
				STEFX_HolomatchMirrorEntity(entityNum)) : qfalse;
		}
		break;
	case STEFX_HM_G_BOT_ALLOCATE_CLIENT:
		result = SV_BotAllocateClient();
		if (result >= 0 && svs.clients)
		{
			client_t *client = &svs.clients[result];
			client->state = CS_ACTIVE;
			client->gentity = STEFX_HolomatchMirrorEntity(result);
			client->rate = 25000;
			client->snapshotMsec = 50;
			client->lastPacketTime = sv.time;
			client->lastConnectTime = sv.time;
			XBLog_WriteCriticalf("STEFX_HM_BOT: allocated official client=%d mirror=%p",
				result, client->gentity);
		}
		break;
	case STEFX_HM_G_BOT_FREE_CLIENT:
		SV_BotFreeClient(va_arg(args, int));
		break;
	case STEFX_HM_G_GET_USERCMD:
		{
			static int s_stefxLastSpWeaponButtons[3] = { -1, -1, -1 };
			static int s_stefxLastOfficialWeaponButtons[3] = { -1, -1, -1 };
			static int s_stefxWeaponUsercmdLogBudget = 192;
			int clientNum = va_arg(args, int);
			usercmd_t spCommand;
			stefx_hm_usercmd_t *officialCommand;
			SV_GetUsercmd(clientNum, &spCommand);
			officialCommand = (stefx_hm_usercmd_t *)va_arg(args, void *);
			if (officialCommand)
			{
				officialCommand->serverTime = spCommand.serverTime;
				officialCommand->buttons = STEFX_SPButtonsToOfficial(spCommand.buttons);
				officialCommand->weapon = spCommand.weapon;
				officialCommand->angles[0] = spCommand.angles[0];
				officialCommand->angles[1] = spCommand.angles[1];
				officialCommand->angles[2] = spCommand.angles[2];
				officialCommand->forwardmove = spCommand.forwardmove;
				officialCommand->rightmove = spCommand.rightmove;
				officialCommand->upmove = spCommand.upmove;
			}
			if (clientNum >= 0 && clientNum < 3 && officialCommand)
			{
				const int spWeaponButtons = spCommand.buttons & (BUTTON_ATTACK | BUTTON_ALT_ATTACK);
				const int officialWeaponButtons = officialCommand->buttons & (1 | 32);
				if (spWeaponButtons != s_stefxLastSpWeaponButtons[clientNum] ||
					officialWeaponButtons != s_stefxLastOfficialWeaponButtons[clientNum])
				{
					if (s_stefxWeaponUsercmdLogBudget > 0)
					{
						XBLog_WriteCriticalf("STEFX_WEAPON: game usercmd edge client=%d time=%d spButtons=0x%x spFire=0x%x officialButtons=0x%x officialFire=0x%x weapon=%d",
							clientNum,
							spCommand.serverTime,
							spCommand.buttons,
							spWeaponButtons,
							officialCommand->buttons,
							officialWeaponButtons,
							officialCommand->weapon);
						--s_stefxWeaponUsercmdLogBudget;
					}
					s_stefxLastSpWeaponButtons[clientNum] = spWeaponButtons;
					s_stefxLastOfficialWeaponButtons[clientNum] = officialWeaponButtons;
				}
			}
		}
		break;
	case STEFX_HM_G_GET_ENTITY_TOKEN:
		{
			char *buffer = va_arg(args, char *);
			int bufferSize = va_arg(args, int);
			if (s_stefxHolomatchEntityParsePoint)
			{
				const char *token = COM_Parse(&s_stefxHolomatchEntityParsePoint);
				Q_strncpyz(buffer, token, bufferSize);
				result = (s_stefxHolomatchEntityParsePoint || token[0]) ? qtrue : qfalse;
			}
			else
			{
				result = SV_GetEntityToken(buffer, bufferSize);
			}
			if (s_stefxHolomatchEntityTokenTraceCount < 24)
			{
				++s_stefxHolomatchEntityTokenTraceCount;
				XBLog_WriteCriticalf("STEFX_HM_SP: entity token result=%d token='%s'",
					result, buffer ? buffer : "");
			}
		}
		break;
	case STEFX_HM_G_FS_GETFILELIST:
		{
			const char *path = va_arg(args, const char *);
			const char *extension = va_arg(args, const char *);
			char *list = va_arg(args, char *);
			int bufferSize = va_arg(args, int);
			XBLog_WriteCriticalf("STEFX_HM_SP: fs list enter path='%s' extension='%s' buffer=%p size=%d",
				path ? path : "", extension ? extension : "", list, bufferSize);
			result = s_stefxHolomatchImport->FS_GetFileList(path, extension, list, bufferSize);
			XBLog_WriteCriticalf("STEFX_HM_SP: fs list exit count=%d first='%s'", result,
				(list && list[0]) ? list : "");
		}
		break;
	case STEFX_HM_G_DEBUG_POLYGON_CREATE:
		va_arg(args, int);
		va_arg(args, int);
		va_arg(args, void *);
		result = 0;
		break;
	case STEFX_HM_G_DEBUG_POLYGON_DELETE:
		va_arg(args, int);
		break;
	default:
		if (!STEFX_HolomatchBotSyscall(command, args, &result))
		{
			XBLog_WriteCriticalf("STEFX_HM_SP: unhandled official game syscall=%d", command);
		}
		break;
	}

	va_end(args);
	return result;
}

static int STEFX_HolomatchVM( int command, int arg0 = 0, int arg1 = 0, int arg2 = 0 )
{
	return vmMain(command, arg0, arg1, arg2, 0, 0, 0, 0);
}

static void STEFX_HolomatchInit( const char *mapname, const char *spawntarget,
	int checksum, const char *entstring, int levelTime, int randomSeed, int globalTime,
	SavedGameJustLoaded_e savedGame, qboolean loadTransition )
{
	(void)mapname;
	(void)spawntarget;
	(void)checksum;
	(void)globalTime;
	(void)savedGame;
	(void)loadTransition;
	s_stefxHolomatchEntityParsePoint = entstring;
	XBLF("STEFX: SP-hosted official VM Init enter levelTime=%d randomSeed=%d", levelTime, randomSeed);
	STEFX_HolomatchVM(0, levelTime, randomSeed, 0);
	STEFX_HolomatchSyncAllToMirror();
	STEFX_HolomatchRefreshExport();
	XBLF("STEFX: SP-hosted official VM Init exit official=%p mirror=%p count=%d stride=%d",
		s_stefxHolomatchGentities,
		s_stefxHolomatchExport.gentities,
		s_stefxHolomatchExport.num_entities,
		s_stefxHolomatchGentitySize,
		s_stefxHolomatchExport.gentitySize);
}

static void STEFX_HolomatchShutdown()
{
	STEFX_HolomatchVM(1, 0, 0, 0);
}

void STEFX_HolomatchBotFrame( int levelTime )
{
	static int s_botFrameLogBudget = 12;
	int result;
	if (!s_stefxHolomatchImport)
	{
		return;
	}
	result = STEFX_HolomatchVM(10, levelTime, 0, 0);
	if (s_botFrameLogBudget > 0)
	{
		XBLog_WriteCriticalf("STEFX_HM_BOT: official AI frame time=%d result=%d",
			levelTime, result);
		--s_botFrameLogBudget;
	}
	STEFX_HolomatchSyncAllToMirror();
	STEFX_HolomatchRefreshExport();
}

static char *STEFX_HolomatchClientConnect( int clientNum, qboolean firstTime, SavedGameJustLoaded_e savedGame )
{
	XBLog_WriteCriticalf("STEFX_HM_SP: official ClientConnect enter client=%d", clientNum);
	char *result = (char *)STEFX_HolomatchVM(2, clientNum, firstTime, savedGame);
	XBLog_WriteCriticalf("STEFX_HM_SP: official ClientConnect exit client=%d denied=%p", clientNum, result);
	STEFX_HolomatchSyncAllToMirror();
	STEFX_HolomatchRefreshExport();
	return result;
}

static void STEFX_HolomatchClientBegin( int clientNum, usercmd_t *cmd, SavedGameJustLoaded_e savedGame )
{
	(void)cmd;
	(void)savedGame;
	XBLog_WriteCriticalf("STEFX_HM_SP: official ClientBegin enter client=%d", clientNum);
	STEFX_HolomatchVM(3, clientNum, 0, 0);
	XBLog_WriteCriticalf("STEFX_HM_SP: official ClientBegin exit client=%d", clientNum);
	STEFX_HolomatchSyncAllToMirror();
	STEFX_HolomatchRefreshExport();
}

static void STEFX_HolomatchClientUserinfoChanged( int clientNum )
{
	XBLog_WriteCriticalf("STEFX_HM_SP: official ClientUserinfoChanged enter client=%d", clientNum);
	STEFX_HolomatchVM(4, clientNum, 0, 0);
	XBLog_WriteCriticalf("STEFX_HM_SP: official ClientUserinfoChanged exit client=%d", clientNum);
	STEFX_HolomatchSyncAllToMirror();
	STEFX_HolomatchRefreshExport();
}

static void STEFX_HolomatchClientDisconnect( int clientNum )
{
	STEFX_HolomatchVM(5, clientNum, 0, 0);
	STEFX_HolomatchSyncAllToMirror();
	STEFX_HolomatchRefreshExport();
}

static void STEFX_HolomatchClientCommand( int clientNum )
{
	STEFX_HolomatchVM(6, clientNum, 0, 0);
	STEFX_HolomatchSyncAllToMirror();
	STEFX_HolomatchRefreshExport();
}

static void STEFX_HolomatchClientThink( int clientNum, usercmd_t *cmd )
{
	(void)cmd;
	if (s_stefxHolomatchClientThinkTraceCount < 4)
	{
		XBLog_WriteCriticalf("STEFX_HM_SP: official ClientThink enter client=%d", clientNum);
	}
	STEFX_HolomatchVM(7, clientNum, 0, 0);
	if (s_stefxHolomatchClientThinkTraceCount < 4)
	{
		XBLog_WriteCriticalf("STEFX_HM_SP: official ClientThink exit client=%d", clientNum);
	}
	++s_stefxHolomatchClientThinkTraceCount;
	STEFX_HolomatchSyncAllToMirror();
	STEFX_HolomatchRefreshExport();
}

static void STEFX_HolomatchConnectNavs( const char *mapname, int checksum )
{
	/* The SP host owns this export slot.  Holomatch does not expose the SP
	 * navigator service, and the direct-map bot harness does not consume it. */
	XBLog_WriteCriticalf("STEFX_HM_SP: ConnectNavs skipped map='%s' checksum=%d",
		mapname ? mapname : "", checksum);
}

static void STEFX_HolomatchRunFrame( int levelTime )
{
	++s_stefxHolomatchHeartbeat;
	if (s_stefxHolomatchHeartbeat <= 8 || (s_stefxHolomatchHeartbeat % 60) == 0)
	{
		XBLog_WriteCriticalf("STEFX_HM_SP: heartbeat frame=%d levelTime=%d",
			s_stefxHolomatchHeartbeat, levelTime);
	}
	if (s_stefxHolomatchHeartbeat <= 8)
	{
		XBLog_WriteCriticalf("STEFX_HM_SP: official RunFrame enter frame=%d levelTime=%d",
			s_stefxHolomatchHeartbeat, levelTime);
	}
	STEFX_HolomatchVM(8, levelTime, 0, 0);
	if (s_stefxHolomatchHeartbeat <= 8)
	{
		XBLog_WriteCriticalf("STEFX_HM_SP: official RunFrame exit frame=%d levelTime=%d",
			s_stefxHolomatchHeartbeat, levelTime);
	}
	STEFX_HolomatchSyncAllToMirror();
	STEFX_HolomatchRefreshExport();
}

static qboolean STEFX_HolomatchConsoleCommand()
{
	return (qboolean)STEFX_HolomatchVM(9, 0, 0, 0);
}

game_export_t *STEFX_GetHolomatchGameAPI( game_import_t *import )
{
	if (import)
	{
		/* SV_InitGameProgs passes a stack-local import table.  The official VM
		 * retains the adapter between calls, so keep the function pointers in
		 * storage that outlives that stack frame. */
		memcpy(&s_stefxHolomatchImportStorage, import, sizeof(s_stefxHolomatchImportStorage));
		s_stefxHolomatchImport = &s_stefxHolomatchImportStorage;
	}
	else
	{
		s_stefxHolomatchImport = NULL;
	}
	s_stefxHolomatchGentities = NULL;
	s_stefxHolomatchGentitySize = 0;
	s_stefxHolomatchNumEntities = 0;
	s_stefxHolomatchEntityTokenTraceCount = 0;
	s_stefxHolomatchHeartbeat = 0;
	s_stefxHolomatchClientThinkTraceCount = 0;
	s_stefxHolomatchClientStatePreserveLogBudget = 192;
	s_stefxHolomatchEntityParsePoint = NULL;
	s_stefxHolomatchSyscallTraceCount = 0;
	s_stefxHolomatchInvalidTraceTraceCount = 0;
	s_stefxHolomatchBadTraceInputCount = 0;
	s_stefxHolomatchBadTraceResultCount = 0;
	s_stefxHolomatchTraceDetailCount = 0;
	s_stefxHolomatchClientLinkTraceCount = 0;
	s_stefxHolomatchClientCollisionTraceCount = 0;
	s_stefxHolomatchCombatTraceProbeCount = 0;
	memset(s_stefxHolomatchMirrorStorage, 0, sizeof(s_stefxHolomatchMirrorStorage));
	memset(s_stefxHolomatchMirrorClientStates, 0, sizeof(s_stefxHolomatchMirrorClientStates));
	memset(&s_stefxHolomatchExport, 0, sizeof(s_stefxHolomatchExport));
	STEFX_HolomatchBotReset();

	dllEntry(STEFX_HolomatchSyscall);

	s_stefxHolomatchExport.apiversion = GAME_API_VERSION;
	s_stefxHolomatchExport.Init = STEFX_HolomatchInit;
	s_stefxHolomatchExport.Shutdown = STEFX_HolomatchShutdown;
	s_stefxHolomatchExport.ClientConnect = STEFX_HolomatchClientConnect;
	s_stefxHolomatchExport.ClientBegin = STEFX_HolomatchClientBegin;
	s_stefxHolomatchExport.ClientUserinfoChanged = STEFX_HolomatchClientUserinfoChanged;
	s_stefxHolomatchExport.ClientDisconnect = STEFX_HolomatchClientDisconnect;
	s_stefxHolomatchExport.ClientCommand = STEFX_HolomatchClientCommand;
	s_stefxHolomatchExport.ClientThink = STEFX_HolomatchClientThink;
	s_stefxHolomatchExport.RunFrame = STEFX_HolomatchRunFrame;
	s_stefxHolomatchExport.ConsoleCommand = STEFX_HolomatchConsoleCommand;
	s_stefxHolomatchExport.ConnectNavs = STEFX_HolomatchConnectNavs;
	STEFX_HolomatchRefreshExport();

	XBLog_Write("STEFX: SP-hosted official EF VM game API selected; SP engine services attached");
	return &s_stefxHolomatchExport;
}
