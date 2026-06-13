#ifndef STEFX_SNAPSHOT_ABI_H
#define STEFX_SNAPSHOT_ABI_H

#include "../game/q_shared.h"

#define STEFX_PS_MAX_AMMO 4
#define STEFX_MAX_ENTITIES_IN_SNAPSHOT 256

typedef struct stefxPlayerState_s {
	int			commandTime;
	int			pm_type;
	int			bobCycle;
	int			pm_flags;
	int			pm_time;

	vec3_t		origin;
	vec3_t		velocity;
	int			weaponTime;
	int			rechargeTime;
	short		useTime;
	int			gravity;
	signed char	leanofs;
	short		friction;
	int			speed;
	int			delta_angles[3];

	int			groundEntityNum;
	int			legsAnim;
	int			legsAnimTimer;
	int			torsoAnim;
	int			torsoAnimTimer;
	int			scale;
	int			movementDir;

	int			eFlags;

	int			eventSequence;
	int			events[MAX_PS_EVENTS];
	int			eventParms[MAX_PS_EVENTS];

	int			externalEvent;
	int			externalEventParm;
	int			externalEventTime;

	int			clientNum;
	int			weapon;
	int			weaponstate;

	vec3_t		viewangles;
	int			viewheight;

	int			damageEvent;
	int			damageYaw;
	int			damagePitch;
	int			damageCount;

	int			stats[MAX_STATS];
	int			persistant[MAX_PERSISTANT];
	int			powerups[MAX_POWERUPS];
	int			ammo[STEFX_PS_MAX_AMMO];
	int			borgAdaptHits[MAX_WEAPONS];

	vec3_t		pushVec;
	int			ping;
	byte		leanStopDebounceTime;
} stefxPlayerState_t;

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

static void STEFX_CopyVec3(vec3_t dst, const vec3_t src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
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
	STEFX_CopyVec3(dst->origin, src->origin);
	STEFX_CopyVec3(dst->velocity, src->velocity);
	dst->weaponTime = src->weaponTime;
	dst->weaponChargeTime = 0;
	dst->rechargeTime = src->rechargeTime;
	dst->gravity = src->gravity;
	dst->leanofs = src->leanofs;
	dst->friction = src->friction;
	dst->speed = src->speed;
	for (i = 0; i < 3; ++i) {
		dst->delta_angles[i] = src->delta_angles[i];
	}
	dst->groundEntityNum = src->groundEntityNum;
	dst->legsAnim = src->legsAnim;
	dst->legsAnimTimer = src->legsAnimTimer;
	dst->torsoAnim = src->torsoAnim;
	dst->torsoAnimTimer = src->torsoAnimTimer;
	dst->movementDir = src->movementDir;
	dst->eFlags = src->eFlags;
	dst->eventSequence = src->eventSequence;
	for (i = 0; i < MAX_PS_EVENTS; ++i) {
		dst->events[i] = src->events[i];
		dst->eventParms[i] = src->eventParms[i];
	}
	dst->externalEvent = src->externalEvent;
	dst->externalEventParm = src->externalEventParm;
	dst->externalEventTime = src->externalEventTime;
	dst->clientNum = src->clientNum;
	dst->weapon = src->weapon;
	dst->weaponstate = src->weaponstate;
	STEFX_CopyVec3(dst->viewangles, src->viewangles);
	dst->viewheight = src->viewheight;
	dst->damageEvent = src->damageEvent;
	dst->damageYaw = src->damageYaw;
	dst->damagePitch = src->damagePitch;
	dst->damageCount = src->damageCount;
	for (i = 0; i < MAX_STATS; ++i) {
		dst->stats[i] = src->stats[i];
	}
	for (i = 0; i < MAX_PERSISTANT; ++i) {
		dst->persistant[i] = src->persistant[i];
	}
	for (i = 0; i < MAX_POWERUPS; ++i) {
		dst->powerups[i] = src->powerups[i];
	}
	for (i = 0; i < STEFX_PS_MAX_AMMO; ++i) {
		dst->ammo[i] = src->ammo[i];
	}
	dst->useTime = src->useTime;
	dst->ping = src->ping;
	dst->leanStopDebounceTime = src->leanStopDebounceTime;
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
	STEFX_CopyVec3(dst->origin, src->origin);
	STEFX_CopyVec3(dst->velocity, src->velocity);
	dst->weaponTime = src->weaponTime;
	dst->rechargeTime = src->rechargeTime;
	dst->useTime = (short)src->useTime;
	dst->gravity = src->gravity;
	dst->leanofs = (signed char)src->leanofs;
	dst->friction = (short)src->friction;
	dst->speed = src->speed;
	for (i = 0; i < 3; ++i) {
		dst->delta_angles[i] = src->delta_angles[i];
	}
	dst->groundEntityNum = src->groundEntityNum;
	dst->legsAnim = src->legsAnim;
	dst->legsAnimTimer = src->legsAnimTimer;
	dst->torsoAnim = src->torsoAnim;
	dst->torsoAnimTimer = src->torsoAnimTimer;
	dst->scale = 100;
	dst->movementDir = src->movementDir;
	dst->eFlags = src->eFlags;
	dst->eventSequence = src->eventSequence;
	for (i = 0; i < MAX_PS_EVENTS; ++i) {
		dst->events[i] = src->events[i];
		dst->eventParms[i] = src->eventParms[i];
	}
	dst->externalEvent = src->externalEvent;
	dst->externalEventParm = src->externalEventParm;
	dst->externalEventTime = src->externalEventTime;
	dst->clientNum = src->clientNum;
	dst->weapon = src->weapon;
	dst->weaponstate = src->weaponstate;
	STEFX_CopyVec3(dst->viewangles, src->viewangles);
	dst->viewheight = src->viewheight;
	dst->damageEvent = src->damageEvent;
	dst->damageYaw = src->damageYaw;
	dst->damagePitch = src->damagePitch;
	dst->damageCount = src->damageCount;
	for (i = 0; i < MAX_STATS; ++i) {
		dst->stats[i] = src->stats[i];
	}
	for (i = 0; i < MAX_PERSISTANT; ++i) {
		dst->persistant[i] = src->persistant[i];
	}
	for (i = 0; i < MAX_POWERUPS; ++i) {
		dst->powerups[i] = src->powerups[i];
	}
	for (i = 0; i < STEFX_PS_MAX_AMMO; ++i) {
		dst->ammo[i] = src->ammo[i];
	}
	dst->ping = src->ping;
	dst->leanStopDebounceTime = (byte)src->leanStopDebounceTime;
}

#endif
