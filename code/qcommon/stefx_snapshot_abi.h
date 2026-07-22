#ifndef STEFX_SNAPSHOT_ABI_H
#define STEFX_SNAPSHOT_ABI_H

#include "../game/q_shared.h"

#if defined(STEFX_SP_HOSTED_MP)

#define STEFX_PS_MAX_AMMO 16
#define STEFX_PS_MAX_EVENTS 4
#define STEFX_MAX_ENTITIES_IN_SNAPSHOT 256

typedef struct stefxTrajectory_s {
	int		trType;
	int		trTime;
	int		trDuration;
	vec3_t	trBase;
	vec3_t	trDelta;
} stefxTrajectory_t;

typedef struct stefxUsercmd_s {
	int		serverTime;
	byte	buttons;
	byte	weapon;
	int		angles[3];
	signed char forwardmove;
	signed char rightmove;
	signed char upmove;
} stefxUsercmd_t;

typedef struct stefxPlayerState_s {
	int		commandTime;
	int		pm_type;
	int		bobCycle;
	int		pm_flags;
	int		pm_time;
	vec3_t	origin;
	vec3_t	velocity;
	int		weaponTime;
	int		rechargeTime;
	short	useTime;
	int		introTime;
	int		gravity;
	int		speed;
	int		delta_angles[3];
	int		groundEntityNum;
	int		legsTimer;
	int		legsAnim;
	int		torsoTimer;
	int		torsoAnim;
	int		movementDir;
	int		eFlags;
	int		eventSequence;
	int		events[4];
	int		eventParms[4];
	int		externalEvent;
	int		externalEventParm;
	int		externalEventTime;
	int		clientNum;
	int		weapon;
	int		weaponstate;
	vec3_t	viewangles;
	int		viewheight;
	int		damageEvent;
	int		damageYaw;
	int		damagePitch;
	int		damageCount;
	int		damageShieldCount;
	int		stats[16];
	int		persistant[16];
	int		powerups[16];
	int		ammo[16];
	int		ping;
	int		entityEventSequence;
} stefxPlayerState_t;

typedef struct stefxEntityState_s {
	int		number;
	int		eType;
	int		eFlags;
	stefxTrajectory_t pos;
	stefxTrajectory_t apos;
	int		time;
	int		time2;
	vec3_t	origin;
	vec3_t	origin2;
	vec3_t	angles;
	vec3_t	angles2;
	int		otherEntityNum;
	int		otherEntityNum2;
	int		groundEntityNum;
	int		constantLight;
	int		loopSound;
	int		modelindex;
	int		modelindex2;
	int		clientNum;
	int		frame;
	int		solid;
	int		event;
	int		eventParm;
	int		powerups;
	int		weapon;
	int		legsAnim;
	int		torsoAnim;
} stefxEntityState_t;

typedef struct stefxSnapshot_s {
	int				snapFlags;
	int				ping;
	int				serverTime;
	byte			areamask[MAX_MAP_AREA_BYTES];
	stefxPlayerState_t ps;
	int				numEntities;
	stefxEntityState_t entities[STEFX_MAX_ENTITIES_IN_SNAPSHOT];
	int				numServerCommands;
	int				serverCommandSequence;
} stefxSnapshot_t;

static int STEFX_OfficialTrajectoryTypeToSP(int type)
{
	switch (type)
	{
	case 4: return TR_SINE;
	case 5: return TR_GRAVITY;
	default: return type;
	}
}

static int STEFX_SPTrajectoryTypeToOfficial(int type)
{
	switch (type)
	{
	case TR_NONLINEAR_STOP: return 3;
	case TR_SINE: return 4;
	case TR_GRAVITY: return 5;
	default: return type;
	}
}

static byte STEFX_SPButtonsToOfficial(int buttons)
{
	byte result = (byte)(buttons & (1 | 2 | 4 | 8 | 16));
	if (buttons & 32) result |= 64;
	if (buttons & 128) result |= 32;
	if (buttons & 256) result |= 128;
	return result;
}

static void STEFX_CopyJaUsercmdToEf(stefxUsercmd_t *dst, const usercmd_t *src)
{
	memset(dst, 0, sizeof(*dst));
	dst->serverTime = src->serverTime;
	dst->buttons = STEFX_SPButtonsToOfficial(src->buttons);
	dst->weapon = src->weapon;
	dst->angles[0] = src->angles[0];
	dst->angles[1] = src->angles[1];
	dst->angles[2] = src->angles[2];
	dst->forwardmove = src->forwardmove;
	dst->rightmove = src->rightmove;
	dst->upmove = src->upmove;
}

static void STEFX_CopyOfficialTrajectoryToSP(trajectory_t *dst, const stefxTrajectory_t *src)
{
	dst->trType = (trType_t)STEFX_OfficialTrajectoryTypeToSP(src->trType);
	dst->trTime = src->trTime;
	dst->trDuration = src->trDuration;
	VectorCopy(src->trBase, dst->trBase);
	VectorCopy(src->trDelta, dst->trDelta);
}

static void STEFX_CopySPTrajectoryToOfficial(stefxTrajectory_t *dst, const trajectory_t *src)
{
	dst->trType = STEFX_SPTrajectoryTypeToOfficial(src->trType);
	dst->trTime = src->trTime;
	dst->trDuration = src->trDuration;
	VectorCopy(src->trBase, dst->trBase);
	VectorCopy(src->trDelta, dst->trDelta);
}

static void STEFX_NormalizeOfficialPhaserBeamState(stefxPlayerState_t *state)
{
	enum
	{
		STEFX_OFFICIAL_WP_PHASER = 1,
		STEFX_OFFICIAL_EF_FIRING = 0x00000100
	};

	if (state->weapon == STEFX_OFFICIAL_WP_PHASER &&
		(state->eFlags & STEFX_OFFICIAL_EF_FIRING))
	{
		state->rechargeTime = 0;
	}
}

static void STEFX_CopyEfPlayerStateToJa(playerState_t *dst, const stefxPlayerState_t *src)
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
	for (i = 0; i < 3; ++i) dst->delta_angles[i] = src->delta_angles[i];
	dst->groundEntityNum = src->groundEntityNum;
	dst->legsAnimTimer = src->legsTimer;
	dst->legsAnim = src->legsAnim;
	dst->torsoAnimTimer = src->torsoTimer;
	dst->torsoAnim = src->torsoAnim;
	dst->scale = 100;
	dst->movementDir = src->movementDir;
	dst->eFlags = src->eFlags;
	dst->eventSequence = src->eventSequence;
	for (i = 0; i < MAX_PS_EVENTS && i < STEFX_PS_MAX_EVENTS; ++i) {
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
	for (i = 0; i < 16; ++i) {
		dst->stats[i] = src->stats[i];
		dst->persistant[i] = src->persistant[i];
		dst->powerups[i] = src->powerups[i];
	}
	for (i = 0; i < MAX_AMMO && i < 16; ++i) dst->ammo[i] = src->ammo[i];
	dst->ping = src->ping;
}

static void STEFX_CopyJaPlayerStateToEf(stefxPlayerState_t *dst, const playerState_t *src)
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
	dst->introTime = 0;
	dst->gravity = src->gravity;
	dst->speed = src->speed;
	for (i = 0; i < 3; ++i) dst->delta_angles[i] = src->delta_angles[i];
	dst->groundEntityNum = src->groundEntityNum;
	dst->legsTimer = src->legsAnimTimer;
	dst->legsAnim = src->legsAnim;
	dst->torsoTimer = src->torsoAnimTimer;
	dst->torsoAnim = src->torsoAnim;
	dst->movementDir = src->movementDir;
	dst->eFlags = src->eFlags;
	dst->eventSequence = src->eventSequence;
	for (i = 0; i < STEFX_PS_MAX_EVENTS && i < MAX_PS_EVENTS; ++i) {
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
	for (i = 0; i < 16; ++i) {
		dst->stats[i] = src->stats[i];
		dst->persistant[i] = src->persistant[i];
		dst->powerups[i] = src->powerups[i];
	}
	for (i = 0; i < 16 && i < MAX_AMMO; ++i) dst->ammo[i] = src->ammo[i];
	dst->ping = src->ping;
	STEFX_NormalizeOfficialPhaserBeamState(dst);
}

static void STEFX_CopyJaEntityStateToEf(stefxEntityState_t *dst, const entityState_t *src)
{
	memset(dst, 0, sizeof(*dst));
	dst->number = src->number;
	dst->eType = src->eType;
	dst->eFlags = src->eFlags;
	STEFX_CopySPTrajectoryToOfficial(&dst->pos, &src->pos);
	STEFX_CopySPTrajectoryToOfficial(&dst->apos, &src->apos);
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

#else

#define STEFX_PS_MAX_AMMO MAX_AMMO
#define STEFX_MAX_ENTITIES_IN_SNAPSHOT 256

typedef playerState_t stefxPlayerState_t;

typedef struct stefxSnapshot_s {
	int				snapFlags;
	int				ping;
	int				serverTime;
	byte			areamask[MAX_MAP_AREA_BYTES];
	int				cmdNum;
	stefxPlayerState_t ps;
	int				numEntities;
	entityState_t	entities[STEFX_MAX_ENTITIES_IN_SNAPSHOT];
	int				numConfigstringChanges;
	int				configstringNum;
	int				numServerCommands;
	int				serverCommandSequence;
} stefxSnapshot_t;

static void STEFX_CopyEfPlayerStateToJa(playerState_t *dst, const stefxPlayerState_t *src)
{
	*dst = *src;
}

static void STEFX_CopyJaPlayerStateToEf(stefxPlayerState_t *dst, const playerState_t *src)
{
	*dst = *src;
}

#endif

#endif
