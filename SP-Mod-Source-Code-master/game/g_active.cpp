
#include "g_local.h"
#include "g_functions.h"
#include "..\cgame\cg_local.h"
#include "Q3_Interface.h"
#ifdef _XBOX
#include "../../code/win32/xb_log.h"
#endif

#define	SLOWDOWN_DIST	128.0f
#define	MIN_NPC_SPEED	16.0f

extern qboolean Q3_TaskIDPending( gentity_t *ent, taskID_t taskType );
extern void G_MaintainFormations(gentity_t *self);
extern void BG_CalculateOffsetAngles( gentity_t *ent, usercmd_t *ucmd );//in bg_pangles.cpp
extern void TryUse( gentity_t *ent );
extern void ChangeWeapon( gentity_t *ent, int newWeapon );
extern void ScoreBoardReset(void);
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
extern void G_SetEnemy( gentity_t *self, gentity_t *enemy );
#endif

extern	bool		in_camera;
extern	qboolean	player_locked;
extern qboolean	stop_icarus;
extern	cvar_t		*g_spskill;

#ifdef _XBOX
static qboolean STEFX_Vec3Bad(const vec3_t v)
{
	return (qboolean)(IS_NAN(v[0]) || IS_NAN(v[1]) || IS_NAN(v[2]));
}

static qboolean STEFX_BoundsBad(const vec3_t mins, const vec3_t maxs)
{
	if (STEFX_Vec3Bad(mins) || STEFX_Vec3Bad(maxs))
	{
		return qtrue;
	}
	if (mins[0] > maxs[0] || mins[1] > maxs[1] || mins[2] > maxs[2])
	{
		return qtrue;
	}
	if ((maxs[0] - mins[0]) > 1024.0f ||
		(maxs[1] - mins[1]) > 1024.0f ||
		(maxs[2] - mins[2]) > 1024.0f)
	{
		return qtrue;
	}
	return qfalse;
}

#if defined(STEFX_ELITE_FORCE_SP)
static gentity_t *STEFX_Borg1SliceBestTarget(gentity_t *ent, int *nearCount, float *bestDistSq)
{
	gentity_t *best = NULL;
	float bestSq = 999999999.0f;
	int count = 0;
	int i;

	if (!ent || !ent->client)
	{
		if (nearCount)
		{
			*nearCount = 0;
		}
		if (bestDistSq)
		{
			*bestDistSq = bestSq;
		}
		return NULL;
	}

	for (i = 1; i < globals.num_entities; ++i)
	{
		gentity_t *target = &g_entities[i];
		vec3_t delta;
		float distSq;

		if (!target->inuse || !target->client || !target->NPC || target->health <= 0)
		{
			continue;
		}

		VectorSubtract(target->currentOrigin, ent->client->ps.origin, delta);
		distSq = VectorLengthSquared(delta);
		if (distSq < 262144.0f)
		{
			count++;
		}
		if (distSq < bestSq)
		{
			bestSq = distSq;
			best = target;
		}
	}

	if (nearCount)
	{
		*nearCount = count;
	}
	if (bestDistSq)
	{
		*bestDistSq = bestSq;
	}
	return best;
}

static void STEFX_Borg1SliceHeartbeat(gentity_t *ent)
{
	static int s_lastHeartbeatTime = -100000;
	gentity_t *best;
	float bestDistSq;
	int nearCount;
	int borgEnemy;
	int borgBehavior;
	int borgTemp;
	int borgWeapon;
	int borgFlags;

	if (!ent || !ent->client || level.time - s_lastHeartbeatTime < 1000)
	{
		return;
	}

	best = STEFX_Borg1SliceBestTarget(ent, &nearCount, &bestDistSq);
	borgEnemy = (best && best->enemy) ? best->enemy->s.number : -1;
	borgBehavior = (best && best->NPC) ? best->NPC->behaviorState : -1;
	borgTemp = (best && best->NPC) ? best->NPC->tempBehavior : -1;
	borgWeapon = (best && best->client) ? best->client->ps.weapon : -1;
	borgFlags = (best && best->NPC) ? best->NPC->scriptFlags : 0;

	XBLF("STEFX: borg1 slice heartbeat time=%d playerOrigin=(%g,%g,%g) view=(%g,%g,%g) nearBorgs=%d bestBorg=%d distSq=%g shots=0 weapon=%d health=%d in_camera=%d borgHealth=%d borgEnemy=%d borgBehavior=%d borgTemp=%d borgWeapon=%d borgFlags=0x%x",
		level.time,
		ent->client->ps.origin[0], ent->client->ps.origin[1], ent->client->ps.origin[2],
		ent->client->ps.viewangles[0], ent->client->ps.viewangles[1], ent->client->ps.viewangles[2],
		nearCount,
		best ? best->s.number : -1,
		bestDistSq,
		ent->client->ps.weapon,
		ent->health,
		in_camera ? 1 : 0,
		best ? best->health : 0,
		borgEnemy,
		borgBehavior,
		borgTemp,
		borgWeapon,
		borgFlags);
	s_lastHeartbeatTime = level.time;
}

static void STEFX_Borg1SliceWarp(gentity_t *ent, usercmd_t *ucmd)
{
	static qboolean s_warped = qfalse;
	vec3_t origin;
	vec3_t angles;
	gclient_t *client;

	if (!ent || !ent->client || ent->s.number != 0 || !ucmd)
	{
		return;
	}
	if (Q_stricmp(level.mapname, "borg1"))
	{
		s_warped = qfalse;
		return;
	}
	if (!gi.Cvar_VariableIntegerValue("stefx_borg1_slice_warp"))
	{
		s_warped = qfalse;
		return;
	}
	if (level.time < 2400)
	{
		return;
	}

	client = ent->client;
	if (!s_warped)
	{
		XBLF("STEFX: borg1 slice warp begin time=%d playerOrigin=(%g,%g,%g) in_camera=%d locked=%d weapon=%d health=%d",
			level.time,
			client->ps.origin[0], client->ps.origin[1], client->ps.origin[2],
			in_camera ? 1 : 0,
			player_locked ? 1 : 0,
			client->ps.weapon,
			ent->health);
		if (in_camera || player_locked)
		{
			XBLF("STEFX: borg1 slice camera clear time=%d in_camera=%d locked=%d",
				level.time,
				in_camera ? 1 : 0,
				player_locked ? 1 : 0);
		}

		in_camera = false;
		player_locked = qfalse;
		gi.cvar_set("skippingCinematic", "0");

		VectorSet(origin, 104.0f, 704.0f, -91.0f);
		VectorSet(angles, 12.9529f, 90.0f, 0.0f);
		G_SetOrigin(ent, origin);
		SetClientViewAngle(ent, angles);
		VectorClear(client->ps.velocity);
		client->ps.pm_type = PM_NORMAL;
		client->ps.pm_time = 0;
		client->ps.pm_flags = 0;
		client->ps.weapon = WP_COMPRESSION_RIFLE;
		ent->s.weapon = WP_COMPRESSION_RIFLE;
		client->ps.weaponstate = WEAPON_READY;
		client->ps.weaponTime = 0;
		client->fireDelay = 0;
		client->ps.stats[STAT_WEAPONS] |= (1 << WP_PHASER);
		client->ps.stats[STAT_WEAPONS] |= (1 << WP_COMPRESSION_RIFLE);
		client->ps.ammo[AMMO_STARFLEET] = ammoData[AMMO_STARFLEET].max;
		client->ps.ammo[AMMO_ALIEN] = ammoData[AMMO_ALIEN].max;
		client->ps.ammo[AMMO_PHASER] = ammoData[AMMO_PHASER].max;
		ent->health = client->ps.stats[STAT_HEALTH] = 100;
		if (client->ps.stats[STAT_MAX_HEALTH] < 100)
		{
			client->ps.stats[STAT_MAX_HEALTH] = 100;
		}
		ent->flags &= ~FL_NOTARGET;
		ucmd->weapon = WP_COMPRESSION_RIFLE;
		ucmd->forwardmove = 0;
		ucmd->rightmove = 0;
		ucmd->upmove = 0;
		gi.linkentity(ent);

		XBLF("STEFX: borg1 slice warp done time=%d playerOrigin=(%g,%g,%g) view=(%g,%g,%g) in_camera=%d locked=%d weapon=%d ammoStarfleet=%d",
			level.time,
			client->ps.origin[0], client->ps.origin[1], client->ps.origin[2],
			client->ps.viewangles[0], client->ps.viewangles[1], client->ps.viewangles[2],
			in_camera ? 1 : 0,
			player_locked ? 1 : 0,
			client->ps.weapon,
			client->ps.ammo[AMMO_STARFLEET]);
		s_warped = qtrue;
	}

	ucmd->weapon = WP_COMPRESSION_RIFLE;
	STEFX_Borg1SliceHeartbeat(ent);
}

static qboolean STEFX_SmokeControlWindowActive(const usercmd_t *ucmd)
{
	int startTime;
	int endTime;

	if (!ucmd)
	{
		return qfalse;
	}
	if (!gi.Cvar_VariableIntegerValue("stefx_smoke_unlock_player"))
	{
		return qfalse;
	}

	startTime = gi.Cvar_VariableIntegerValue("stefx_smoke_input_start");
	endTime = gi.Cvar_VariableIntegerValue("stefx_smoke_input_end");
	if (startTime <= 0)
	{
		startTime = 71000;
	}

	return (qboolean)(ucmd->serverTime >= startTime && (endTime <= 0 || ucmd->serverTime <= endTime));
}

static void STEFX_SmokeUnlockPlayerControl(gentity_t *ent, usercmd_t *ucmd)
{
	static int s_stefxSmokeUnlockBudget = 16;

	if (!ent || ent->s.number != 0 || !STEFX_SmokeControlWindowActive(ucmd))
	{
		return;
	}
	if (!in_camera && !player_locked)
	{
		return;
	}

	if (s_stefxSmokeUnlockBudget > 0)
	{
		XBLF("STEFX: smoke unlock player control ent=%d serverTime=%d inCamera=%d playerLocked=%d buttons=0x%x move=(%d,%d,%d)",
			ent->s.number,
			ucmd ? ucmd->serverTime : -1,
			in_camera ? 1 : 0,
			player_locked ? 1 : 0,
			ucmd ? ucmd->buttons : 0,
			ucmd ? ucmd->forwardmove : 0,
			ucmd ? ucmd->rightmove : 0,
			ucmd ? ucmd->upmove : 0);
		s_stefxSmokeUnlockBudget--;
	}

	in_camera = false;
	player_locked = qfalse;
	gi.cvar_set("skippingCinematic", "0");
}

static void STEFX_SmokeReadyPlayerWeapon(gentity_t *ent, usercmd_t *ucmd)
{
	static int s_stefxSmokeReadyWeaponBudget = 32;
	gclient_t *client;
	int oldWeapon;
	int oldWeaponState;
	int oldWeaponTime;
	int oldPmFlags;
	int oldFireDelay;
	int weapon;
	int ammoIndex;
	int oldAmmo = -9999;
	int newAmmo = -9999;
	int minAmmo;

	if (!ent || ent->s.number != 0 || !ent->client || !ucmd)
	{
		return;
	}
	if (!STEFX_SmokeControlWindowActive(ucmd))
	{
		return;
	}
	if (!gi.Cvar_VariableIntegerValue("stefx_smoke_ready_weapon"))
	{
		return;
	}

	client = ent->client;
	oldWeapon = client->ps.weapon;
	oldWeaponState = client->ps.weaponstate;
	oldWeaponTime = client->ps.weaponTime;
	oldPmFlags = client->ps.pm_flags;
	oldFireDelay = client->fireDelay;

	weapon = client->ps.weapon;
	if (weapon <= WP_NONE || weapon >= WP_TRICORDER || weapon >= WP_NUM_WEAPONS)
	{
		weapon = WP_COMPRESSION_RIFLE;
	}

	client->ps.weapon = weapon;
	ent->s.weapon = weapon;
	ucmd->weapon = weapon;
	if (weapon > WP_NONE && weapon < 31)
	{
		client->ps.stats[STAT_WEAPONS] |= (1 << weapon);
	}

	client->ps.pm_flags &= ~PMF_RESPAWNED;
	client->ps.weaponTime = 0;
	client->fireDelay = 0;
	if (client->ps.weaponstate != WEAPON_READY && client->ps.weaponstate != WEAPON_FIRING)
	{
		client->ps.weaponstate = WEAPON_READY;
	}

	ammoIndex = weaponData[weapon].ammoIndex;
	if (ammoIndex >= 0 && ammoIndex < MAX_AMMO)
	{
		oldAmmo = client->ps.ammo[ammoIndex];
		minAmmo = weaponData[weapon].energyPerShot;
		if (ucmd->buttons & BUTTON_ALT_ATTACK)
		{
			minAmmo = weaponData[weapon].altEnergyPerShot;
		}
		if (minAmmo < 1)
		{
			minAmmo = 1;
		}
		if (client->ps.ammo[ammoIndex] != -1 && client->ps.ammo[ammoIndex] < minAmmo)
		{
			client->ps.ammo[ammoIndex] = ammoData[ammoIndex].max > minAmmo ? ammoData[ammoIndex].max : minAmmo;
		}
		newAmmo = client->ps.ammo[ammoIndex];
	}

	if (s_stefxSmokeReadyWeaponBudget > 0)
	{
		XBLF("STEFX: smoke ready weapon ent=%d time=%d serverTime=%d buttons=0x%x oldWeapon=%d newWeapon=%d oldState=%d newState=%d oldWeaponTime=%d newWeaponTime=%d oldPmFlags=0x%x newPmFlags=0x%x oldFireDelay=%d newFireDelay=%d ammoIndex=%d oldAmmo=%d newAmmo=%d cmdWeapon=%d",
			ent->s.number,
			level.time,
			ucmd->serverTime,
			ucmd->buttons,
			oldWeapon,
			client->ps.weapon,
			oldWeaponState,
			client->ps.weaponstate,
			oldWeaponTime,
			client->ps.weaponTime,
			oldPmFlags,
			client->ps.pm_flags,
			oldFireDelay,
			client->fireDelay,
			ammoIndex,
			oldAmmo,
			newAmmo,
			ucmd->weapon);
		s_stefxSmokeReadyWeaponBudget--;
	}
}

static qboolean STEFX_SmokeStageEnemyIsUsable(gentity_t *ent, gentity_t *target, qboolean allowSameTeam)
{
	if (!ent || !target || !target->inuse || !target->client || !target->NPC)
	{
		return qfalse;
	}
	if (target->health <= 0)
	{
		return qfalse;
	}
	if (!allowSameTeam && OnSameTeam(ent, target))
	{
		return qfalse;
	}
	return qtrue;
}

static gentity_t *STEFX_SmokeFindStageEnemy(gentity_t *ent, int previousTarget)
{
	int pass;
	int i;

	if (previousTarget > 0 && previousTarget < globals.num_entities)
	{
		gentity_t *target = &g_entities[previousTarget];
		if (STEFX_SmokeStageEnemyIsUsable(ent, target, qfalse))
		{
			return target;
		}
	}

	for (pass = 0; pass < 2; ++pass)
	{
		qboolean allowSameTeam = (qboolean)(pass != 0);
		for (i = 1; i < globals.num_entities; ++i)
		{
			gentity_t *target = &g_entities[i];
			if (STEFX_SmokeStageEnemyIsUsable(ent, target, allowSameTeam))
			{
				return target;
			}
		}
	}

	return NULL;
}

static int s_stefxSmokeStageAimEnt = ENTITYNUM_NONE;
static int s_stefxSmokeStageAimTime = -100000;
static vec3_t s_stefxSmokeStageAimPoint = { 0.0f, 0.0f, 0.0f };

static void STEFX_SmokeStageEnemy(gentity_t *ent, usercmd_t *ucmd)
{
	static int s_stefxStageBudget = 36;
	static int s_stefxStageLogBudget = 24;
	static int s_stefxStageNoTargetBudget = 8;
	static int s_stefxStageTarget = ENTITYNUM_NONE;
	gentity_t *target;
	vec3_t forward;
	vec3_t right;
	vec3_t up;
	vec3_t oldOrigin;
	vec3_t newOrigin;
	vec3_t start;
	vec3_t end;
	vec3_t eye;
	vec3_t targetPoint;
	trace_t floorTrace;
	trace_t losTrace;
	float stageDistance;

	if (!ent || !ent->client || ent->s.number != 0 || !ucmd)
	{
		return;
	}
	if (!STEFX_SmokeControlWindowActive(ucmd))
	{
		return;
	}
	if (!gi.Cvar_VariableIntegerValue("stefx_smoke_stage_enemy"))
	{
		return;
	}
	if (s_stefxStageBudget <= 0)
	{
		return;
	}

	target = STEFX_SmokeFindStageEnemy(ent, s_stefxStageTarget);
	if (!target)
	{
		if (s_stefxStageNoTargetBudget > 0)
		{
			XBLF("STEFX: smoke stage enemy no usable target time=%d entities=%d playerOrigin=(%g,%g,%g)",
				level.time,
				globals.num_entities,
				ent->client->ps.origin[0], ent->client->ps.origin[1], ent->client->ps.origin[2]);
			s_stefxStageNoTargetBudget--;
		}
		return;
	}

	AngleVectors(ent->client->ps.viewangles, forward, right, up);
	forward[2] = 0.0f;
	if (VectorNormalize(forward) < 0.1f)
	{
		forward[0] = 1.0f;
		forward[1] = 0.0f;
		forward[2] = 0.0f;
	}
	right[0] = -forward[1];
	right[1] = forward[0];
	right[2] = 0.0f;

	VectorCopy(target->currentOrigin, oldOrigin);
	stageDistance = (ucmd->buttons & (BUTTON_ATTACK | BUTTON_ALT_ATTACK)) ? 36.0f : 48.0f;
	VectorMA(ent->client->ps.origin, stageDistance, forward, newOrigin);
	VectorMA(newOrigin, 4.0f, right, newOrigin);
	newOrigin[2] = ent->client->ps.origin[2];

	VectorCopy(newOrigin, start);
	start[2] += 64.0f;
	VectorCopy(newOrigin, end);
	end[2] -= 160.0f;
	gi.trace(&floorTrace, start, target->mins, target->maxs, end, ent->s.number, MASK_PLAYERSOLID);
	if (!floorTrace.startsolid && !floorTrace.allsolid && floorTrace.fraction < 1.0f)
	{
		VectorCopy(floorTrace.endpos, newOrigin);
	}

	target->svFlags &= ~SVF_NOCLIENT;
	target->svFlags |= SVF_BROADCAST;
	target->flags &= ~FL_NOTARGET;
	target->s.eFlags &= ~EF_NODRAW;
	VectorSet(target->mins, -20.0f, -20.0f, -40.0f);
	VectorSet(target->maxs, 20.0f, 20.0f, 72.0f);
	target->contents = CONTENTS_BODY;
	target->clipmask = MASK_NPCSOLID;
	target->takedamage = qtrue;
	if (target->health < 80)
	{
		target->health = 80;
	}
	target->client->ps.stats[STAT_HEALTH] = target->health;
	target->client->ps.pm_type = PM_NORMAL;
	VectorClear(target->client->ps.velocity);

	if (ent->flags & FL_NOTARGET)
	{
		XBLF("STEFX: smoke stage enemy cleared player FL_NOTARGET ent=%d flags=0x%x time=%d",
			ent->s.number,
			ent->flags,
			level.time);
		ent->flags &= ~FL_NOTARGET;
	}

	G_SetOrigin(target, newOrigin);
	gi.linkentity(target);
	G_SetEnemy(target, ent);
	if (target->enemy != ent)
	{
		target->enemy = ent;
	}
	s_stefxSmokeStageAimEnt = target->s.number;
	s_stefxSmokeStageAimTime = level.time;
	VectorCopy(newOrigin, s_stefxSmokeStageAimPoint);
	s_stefxSmokeStageAimPoint[2] += 32.0f;
	XBLF("STEFX: NPC_SetEnemy smoke stage self=%d class='%s' enemy=%d contents=0x%x clipmask=0x%x time=%d",
		target->s.number,
		target->classname ? target->classname : "<null>",
		ent->s.number,
		target->contents,
		target->clipmask,
		level.time);
	s_stefxStageTarget = target->s.number;
	s_stefxStageBudget--;

	if (VectorLengthSquared(ent->client->renderInfo.eyePoint) > 1.0f)
	{
		VectorCopy(ent->client->renderInfo.eyePoint, eye);
	}
	else
	{
		VectorCopy(ent->client->ps.origin, eye);
		eye[2] += ent->client->ps.viewheight;
	}
	VectorCopy(newOrigin, targetPoint);
	targetPoint[2] += target->client->ps.viewheight * 0.65f;
	gi.trace(&losTrace, eye, NULL, NULL, targetPoint, ent->s.number, MASK_SHOT);

	if (s_stefxStageLogBudget > 0)
	{
		XBLF("STEFX: smoke stage enemy target=%d class='%s' targetname='%s' old=(%g,%g,%g) new=(%g,%g,%g) player=(%g,%g,%g) floorFrac=%g floorStartSolid=%d losEnt=%d losFrac=%g health=%d team=%d playerTeam=%d time=%d remaining=%d",
			target->s.number,
			target->classname ? target->classname : "<null>",
			target->targetname ? target->targetname : "<null>",
			oldOrigin[0], oldOrigin[1], oldOrigin[2],
			newOrigin[0], newOrigin[1], newOrigin[2],
			ent->client->ps.origin[0], ent->client->ps.origin[1], ent->client->ps.origin[2],
			floorTrace.fraction,
			floorTrace.startsolid ? 1 : 0,
			losTrace.entityNum,
			losTrace.fraction,
			target->health,
			target->client ? target->client->playerTeam : -1,
			ent->client ? ent->client->playerTeam : -1,
			level.time,
			s_stefxStageBudget);
		s_stefxStageLogBudget--;
	}
}

static void STEFX_ClientThinkPMStateLog(const char *phase, gentity_t *ent, const usercmd_t *ucmd, const pmove_t *pmove)
{
	gclient_t *client;
	int weapon;
	int ammoIndex;
	int ammoValue;

	if (!ent || !ent->client)
	{
		return;
	}

	client = ent->client;
	weapon = client->ps.weapon;
	ammoIndex = (weapon >= 0 && weapon < MAX_WEAPONS) ? weaponData[weapon].ammoIndex : -1;
	ammoValue = (ammoIndex >= 0 && ammoIndex < MAX_AMMO) ? client->ps.ammo[ammoIndex] : -9999;

	XBLF("STEFX: ClientThink PM state phase=%s ent=%d time=%d buttons=0x%x cmdWeapon=%d psWeapon=%d weaponstate=%d weaponTime=%d pmFlags=0x%x pmType=%d health=%d ammoIndex=%d ammo=%d fireDelay=%d eventSeq=%d origin=(%g,%g,%g) vel=(%g,%g,%g) pmTouch=%d water=%d/%d ground=%d",
		phase ? phase : "<null>",
		ent->s.number,
		level.time,
		ucmd ? ucmd->buttons : 0,
		ucmd ? ucmd->weapon : -1,
		client->ps.weapon,
		client->ps.weaponstate,
		client->ps.weaponTime,
		client->ps.pm_flags,
		client->ps.pm_type,
		client->ps.stats[STAT_HEALTH],
		ammoIndex,
		ammoValue,
		client->fireDelay,
		client->ps.eventSequence,
		client->ps.origin[0], client->ps.origin[1], client->ps.origin[2],
		client->ps.velocity[0], client->ps.velocity[1], client->ps.velocity[2],
		pmove ? pmove->numtouch : -1,
		pmove ? pmove->waterlevel : -1,
		pmove ? pmove->watertype : -1,
		client->ps.groundEntityNum);
}

static void STEFX_SmokeAimAtLiveEnemy(gentity_t *ent, usercmd_t *ucmd)
{
	static int s_stefxSmokeAimBudget = 64;
	gentity_t *bestTarget = NULL;
	gentity_t *bestTraceTarget = NULL;
	float bestDistSq = 999999999.0f;
	float bestTraceDistSq = 999999999.0f;
	vec3_t start;
	vec3_t bestPoint;
	vec3_t bestTracePoint;
	int bestTraceEnt = ENTITYNUM_NONE;
	int bestTraceTargetEnt = ENTITYNUM_NONE;
	float bestTraceFrac = 1.0f;
	float bestTraceTargetFrac = 1.0f;
	int i;
	float range;
	float rangeSq;

	if (!ent || !ent->client || !ucmd)
	{
		return;
	}
	if (ent->s.number != 0)
	{
		return;
	}
	if (!(ucmd->buttons & (BUTTON_ATTACK | BUTTON_ALT_ATTACK)))
	{
		return;
	}
	if (!gi.Cvar_VariableIntegerValue("stefx_smoke_aim"))
	{
		return;
	}

	if (VectorLengthSquared(ent->client->renderInfo.eyePoint) > 1.0f)
	{
		VectorCopy(ent->client->renderInfo.eyePoint, start);
	}
	else
	{
		VectorCopy(ent->client->ps.origin, start);
		start[2] += ent->client->ps.viewheight;
	}

	range = weaponData[WP_PHASER].range > 0 ? (float)weaponData[WP_PHASER].range : 2048.0f;
	if (ent->client->ps.weapon > WP_NONE && ent->client->ps.weapon < WP_NUM_WEAPONS && weaponData[ent->client->ps.weapon].range > 0)
	{
		range = (float)weaponData[ent->client->ps.weapon].range;
	}
	range += 96.0f;
	rangeSq = range * range;

	if (gi.Cvar_VariableIntegerValue("stefx_smoke_stage_enemy") &&
		s_stefxSmokeStageAimEnt > 0 &&
		s_stefxSmokeStageAimEnt < globals.num_entities &&
		level.time - s_stefxSmokeStageAimTime <= 45000)
	{
		gentity_t *target = &g_entities[s_stefxSmokeStageAimEnt];
		vec3_t targetPoint;
		vec3_t delta;
		trace_t tr;
		float distSq;

		if (STEFX_SmokeStageEnemyIsUsable(ent, target, qfalse))
		{
			VectorCopy(s_stefxSmokeStageAimPoint, targetPoint);
			VectorSubtract(targetPoint, start, delta);
			distSq = VectorLengthSquared(delta);
			if (distSq <= rangeSq)
			{
				gi.trace(&tr, start, NULL, NULL, targetPoint, ent->s.number, MASK_SHOT);
				bestDistSq = distSq;
				bestTarget = target;
				VectorCopy(targetPoint, bestPoint);
				bestTraceEnt = tr.entityNum;
				bestTraceFrac = tr.fraction;
				if (tr.entityNum == target->s.number)
				{
					bestTraceDistSq = distSq;
					bestTraceTarget = target;
					VectorCopy(targetPoint, bestTracePoint);
					bestTraceTargetEnt = tr.entityNum;
					bestTraceTargetFrac = tr.fraction;
				}
			}
		}
	}

	for (i = 1; !bestTarget && !bestTraceTarget && i < globals.num_entities; ++i)
	{
		gentity_t *target = &g_entities[i];
		vec3_t targetPoint;
		vec3_t delta;
		trace_t tr;
		float distSq;

		if (!target->inuse || !target->client || !target->NPC)
		{
			continue;
		}
		if (!target->takedamage || target->health <= 0 || (target->flags & FL_NOTARGET))
		{
			continue;
		}
		if (OnSameTeam(ent, target))
		{
			continue;
		}

		{
			vec3_t candidates[5];
			int candidateCount = 0;
			int candidateIndex;

			VectorCopy(target->currentOrigin, targetPoint);
			targetPoint[2] += 32.0f;
			VectorCopy(targetPoint, candidates[candidateCount++]);

			VectorCopy(target->currentOrigin, targetPoint);
			targetPoint[2] += 24.0f;
			VectorCopy(targetPoint, candidates[candidateCount++]);

			VectorCopy(target->currentOrigin, targetPoint);
			targetPoint[2] += 40.0f;
			VectorCopy(targetPoint, candidates[candidateCount++]);

			VectorCopy(target->currentOrigin, targetPoint);
			targetPoint[2] += 16.0f;
			VectorCopy(targetPoint, candidates[candidateCount++]);

			for (candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
			{
				VectorCopy(candidates[candidateIndex], targetPoint);
				VectorSubtract(targetPoint, start, delta);
				distSq = VectorLengthSquared(delta);
				if (distSq > rangeSq)
				{
					continue;
				}

				gi.trace(&tr, start, NULL, NULL, targetPoint, ent->s.number, MASK_SHOT);
				if (tr.entityNum == target->s.number)
				{
					if (distSq < bestTraceDistSq)
					{
						bestTraceDistSq = distSq;
						bestTraceTarget = target;
						VectorCopy(targetPoint, bestTracePoint);
						bestTraceTargetEnt = tr.entityNum;
						bestTraceTargetFrac = tr.fraction;
					}
				}
				else if (distSq < bestDistSq)
				{
					bestDistSq = distSq;
					bestTarget = target;
					VectorCopy(targetPoint, bestPoint);
					bestTraceEnt = tr.entityNum;
					bestTraceFrac = tr.fraction;
				}
			}
		}
	}

	if (bestTraceTarget)
	{
		bestTarget = bestTraceTarget;
		VectorCopy(bestTracePoint, bestPoint);
		bestDistSq = bestTraceDistSq;
		bestTraceEnt = bestTraceTargetEnt;
		bestTraceFrac = bestTraceTargetFrac;
	}

	if (bestTarget)
	{
		static int s_stefxSmokeWakeBudget = 24;
		vec3_t dir;
		vec3_t desired;

		VectorSubtract(bestPoint, start, dir);
		vectoangles(dir, desired);
		ucmd->angles[PITCH] = ANGLE2SHORT(desired[PITCH]) - ent->client->ps.delta_angles[PITCH];
		ucmd->angles[YAW] = ANGLE2SHORT(desired[YAW]) - ent->client->ps.delta_angles[YAW];
		if (gi.Cvar_VariableIntegerValue("stefx_smoke_stage_enemy"))
		{
			ucmd->forwardmove = 0;
			ucmd->rightmove = 0;
			ucmd->upmove = 0;
		}

		if (gi.Cvar_VariableIntegerValue("stefx_smoke_wake_ai") && bestTarget->NPC && bestTarget->enemy != ent)
		{
			if (ent->flags & FL_NOTARGET)
			{
				XBLF("STEFX: smoke wake cleared player FL_NOTARGET ent=%d flags=0x%x time=%d",
					ent->s.number,
					ent->flags,
					level.time);
				ent->flags &= ~FL_NOTARGET;
			}
			if (s_stefxSmokeWakeBudget > 0)
			{
				XBLF("STEFX: smoke wake enemy self=%d class='%s' team=%d enemyTeam=%d target=%d playerTeam=%d time=%d",
					bestTarget->s.number,
					bestTarget->classname ? bestTarget->classname : "<null>",
					bestTarget->client ? bestTarget->client->playerTeam : -1,
					bestTarget->client ? bestTarget->client->enemyTeam : -1,
					ent->s.number,
					ent->client ? ent->client->playerTeam : -1,
					level.time);
				s_stefxSmokeWakeBudget--;
			}
			G_SetEnemy(bestTarget, ent);
		}

		if (s_stefxSmokeAimBudget > 0)
		{
			XBLF("STEFX: smoke aim target ent=%d class='%s' targetname='%s' dist=%g desired=(%g,%g,%g) cmdAngles=(%d,%d,%d) traceEnt=%d frac=%g start=(%g,%g,%g) point=(%g,%g,%g)",
				bestTarget->s.number,
				bestTarget->classname ? bestTarget->classname : "<null>",
				bestTarget->targetname ? bestTarget->targetname : "<null>",
				sqrt(bestDistSq),
				desired[0], desired[1], desired[2],
				ucmd->angles[PITCH],
				ucmd->angles[YAW],
				ucmd->angles[ROLL],
				bestTraceEnt,
				bestTraceFrac,
				start[0], start[1], start[2],
				bestPoint[0], bestPoint[1], bestPoint[2]);
			s_stefxSmokeAimBudget--;
		}
	}
	else if (s_stefxSmokeAimBudget > 0)
	{
		XBLF("STEFX: smoke aim no live enemy ent=%d time=%d origin=(%g,%g,%g) range=%g entities=%d",
			ent->s.number,
			level.time,
			start[0], start[1], start[2],
			range,
			globals.num_entities);
		s_stefxSmokeAimBudget--;
	}
}
#endif
#endif

/*
===============
G_DamageFeedback

Called just before a snapshot is sent to the given player.
Totals up all damage and generates both the player_state_t
damage values to that client for pain blends and kicks, and
global pain sound events for all clients.
===============
*/
void P_DamageFeedback( gentity_t *player ) {
	gclient_t	*client;
	float	count;
	vec3_t	angles;

	client = player->client;
	if ( client->ps.pm_type == PM_DEAD ) {
		return;
	}

	// total points of damage shot at the player this frame
	count = client->damage_blood + client->damage_armor;
	if ( count == 0 ) {
		return;		// didn't take any damage
	}

	if ( count > 255 ) {
		count = 255;
	}

	// send the information to the client

	// world damage (falling, slime, etc) uses a special code
	// to make the blend blob centered instead of positional
	if ( client->damage_fromWorld ) {
		client->ps.damagePitch = 255;
		client->ps.damageYaw = 255;

		client->damage_fromWorld = qfalse;
	} else {
		vectoangles( client->damage_from, angles );
		client->ps.damagePitch = angles[PITCH]/360.0 * 256;
		client->ps.damageYaw = angles[YAW]/360.0 * 256;
	}

	// play an apropriate pain sound
	if ( (level.time > player->painDebounceTime) && !(player->flags & FL_GODMODE) ) 
	{
		player->painDebounceTime = level.time + 700;
		client->ps.damageEvent++;
		if ( !Q3_TaskIDPending( player, TID_CHAN_VOICE ) )
		{
			G_AddEvent( player, EV_PAIN, player->health );
		}
	}


	client->ps.damageCount = count;

	//
	// clear totals
	//
	client->damage_blood = 0;
	client->damage_armor = 0;
	client->damage_knockback = 0;
}



/*
=============
P_WorldEffects

Check for lava / slime contents and drowning
=============
*/
void P_WorldEffects( gentity_t *ent ) {
	int			waterlevel;

	if ( ent->client->noclip ) {
		ent->client->airOutTime = level.time + 12000;	// don't need air
		return;
	}

	waterlevel = ent->waterlevel;
	//
	// check for drowning
	//
	if ( waterlevel == 3 && !(ent->watertype&CONTENTS_LADDER) ) {

		// if out of air, start drowning
		if ( ent->client->airOutTime < level.time) {
			// drown!
			ent->client->airOutTime += 1000;
			if ( ent->health > 0 ) {
				// take more damage the longer underwater
				ent->damage += 2;
				if (ent->damage > 15)
					ent->damage = 15;

				// play a gurp sound instead of a normal pain sound
				if (ent->health <= ent->damage) {
					G_Sound(ent, G_SoundIndex("sound/player/hm_male/drown.wav"));
				} else if (rand()&1) {
					G_Sound(ent, G_SoundIndex("sound/player/gurp1.wav"));
				} else {
					G_Sound(ent, G_SoundIndex("sound/player/gurp2.wav"));
				}

				// don't play a normal pain sound
				ent->painDebounceTime = level.time + 200;

				G_Damage (ent, NULL, NULL, NULL, NULL, 
					ent->damage, DAMAGE_NO_ARMOR, MOD_WATER);
			}
		}
	} else {
		ent->client->airOutTime = level.time + 12000;
		ent->damage = 2;
	}

	//
	// check for sizzle damage (move to pmove?)
	//
	if (waterlevel && 
		(ent->watertype&(CONTENTS_LAVA|CONTENTS_SLIME)) ) {
		if (ent->health > 0
			&& ent->painDebounceTime < level.time	) {

			if (ent->watertype & CONTENTS_LAVA) {
				G_Damage (ent, NULL, NULL, NULL, NULL, 
					15*waterlevel, 0, MOD_LAVA);
			}

			if (ent->watertype & CONTENTS_SLIME) {
				G_Damage (ent, NULL, NULL, NULL, NULL, 
					1, 0, MOD_SLIME);
			}
		}
	}
}



/*
===============
G_SetClientSound
===============
*/
void G_SetClientSound( gentity_t *ent ) {
//	if (ent->waterlevel && (ent->watertype&(CONTENTS_LAVA|CONTENTS_SLIME)) )
		ent->s.loopSound = G_SoundIndex("sound/weapons/stasis/electricloop.wav");

//	else
//		ent->s.loopSound = 0;
}



//==============================================================

/*
==============
ClientImpacts
==============
*/
void ClientImpacts( gentity_t *ent, pmove_t *pm ) {
	int		i, j;
	trace_t	trace;
	gentity_t	*other;
#ifdef _XBOX
	static int s_stefxClientImpactsLogCount = 0;
	qboolean stefxLog = (s_stefxClientImpactsLogCount < 64);

	if ( !ent || !pm )
	{
		if (stefxLog)
		{
			XBLF("STEFX: ClientImpacts observed invalid ent=%08x pm=%08x",
				(unsigned int)ent, (unsigned int)pm);
		}
#if defined(STEFX_XBOX_SURVIVAL_HACKS)
		s_stefxClientImpactsLogCount++;
		return;
#endif
	}
	if (pm->numtouch < 0 || pm->numtouch > MAXTOUCH)
	{
		if (stefxLog)
		{
			XBLF("STEFX: ClientImpacts observed out-of-range numtouch ent=%d numtouch=%d max=%d",
				ent->s.number, pm->numtouch, MAXTOUCH);
		}
#if defined(STEFX_XBOX_SURVIVAL_HACKS)
		pm->numtouch = (pm->numtouch < 0) ? 0 : MAXTOUCH;
#endif
	}
#endif

	memset( &trace, 0, sizeof( trace ) );
	for (i=0 ; i<pm->numtouch ; i++) {
		for (j=0 ; j<i ; j++) {
			if (pm->touchents[j] == pm->touchents[i] ) {
				break;
			}
		}
		if (j != i) {
			continue;	// duplicated
		}
#ifdef _XBOX
		if (pm->touchents[i] < 0 || pm->touchents[i] >= MAX_GENTITIES)
		{
			if (stefxLog)
			{
				XBLF("STEFX: ClientImpacts observed ent=%d touch[%d]=%d out of range",
					ent->s.number, i, pm->touchents[i]);
			}
#if defined(STEFX_XBOX_SURVIVAL_HACKS)
			continue;
#endif
		}
#endif
		other = &g_entities[ pm->touchents[i] ];
#ifdef _XBOX
		if (!other->inuse)
		{
			if (stefxLog)
			{
				XBLF("STEFX: ClientImpacts observed ent=%d touch[%d]=%d not inuse",
					ent->s.number, i, pm->touchents[i]);
			}
#if defined(STEFX_XBOX_SURVIVAL_HACKS)
			continue;
#endif
		}
#endif

		if ( ( ent->NPC != NULL ) && ( ent->e_TouchFunc != touchF_NULL ) ) {	// last check unneccessary
#ifdef _XBOX
			if (stefxLog)
			{
				XBLF("STEFX: ClientImpacts ent touch ent=%d other=%d func=%d",
					ent->s.number, other->s.number, ent->e_TouchFunc);
			}
#endif
			GEntity_TouchFunc( ent, other, &trace );
		}

		if ( other->e_TouchFunc == touchF_NULL ) {	// not needed, but I'll leave it I guess (cache-hit issues)
			continue;
		}
#ifdef _XBOX
		if (stefxLog)
		{
			XBLF("STEFX: ClientImpacts other touch ent=%d other=%d func=%d",
				ent->s.number, other->s.number, other->e_TouchFunc);
		}
#endif
		GEntity_TouchFunc( other, ent, &trace );
	}
#ifdef _XBOX
	if (stefxLog)
	{
		XBLF("STEFX: ClientImpacts exit ent=%d numtouch=%d", ent->s.number, pm->numtouch);
	}
	s_stefxClientImpactsLogCount++;
#endif

}

/*
============
G_TouchTriggersLerped

Find all trigger entities that ent's current position touches.
Spectators will only interact with teleporters.

This version checks at 6 unit steps between last and current origins
============
*/
void	G_TouchTriggersLerped( gentity_t *ent ) {
	int			i, num;
	float		dist, curDist = 0, step;
	int			lerpGuard = 0;
	gentity_t	*touch[MAX_GENTITIES], *hit;
	trace_t		trace;
	vec3_t		end, mins, maxs, diff;
	const vec3_t	range = { 40, 40, 52 };
	qboolean	touched[MAX_GENTITIES];
	qboolean	done = qfalse;
#ifdef _XBOX
	static int s_stefxTouchLerpedLogCount = 0;
	qboolean stefxLog = (s_stefxTouchLerpedLogCount < 64);
	if (stefxLog)
	{
		XBLF("STEFX: G_TouchTriggersLerped enter count=%d ent=%d client=%08x health=%d origin=(%g,%g,%g) last=(%g,%g,%g) mins=(%g,%g,%g) maxs=(%g,%g,%g)",
			s_stefxTouchLerpedLogCount,
			ent ? ent->s.number : -1,
			(ent && ent->client) ? (unsigned int)ent->client : 0,
			(ent && ent->client) ? ent->client->ps.stats[STAT_HEALTH] : -999,
			ent ? ent->currentOrigin[0] : 0.0f,
			ent ? ent->currentOrigin[1] : 0.0f,
			ent ? ent->currentOrigin[2] : 0.0f,
			ent ? ent->lastOrigin[0] : 0.0f,
			ent ? ent->lastOrigin[1] : 0.0f,
			ent ? ent->lastOrigin[2] : 0.0f,
			ent ? ent->mins[0] : 0.0f,
			ent ? ent->mins[1] : 0.0f,
			ent ? ent->mins[2] : 0.0f,
			ent ? ent->maxs[0] : 0.0f,
			ent ? ent->maxs[1] : 0.0f,
			ent ? ent->maxs[2] : 0.0f);
	}
	s_stefxTouchLerpedLogCount++;
#endif

	if ( !ent->client ) {
#ifdef _XBOX
		if (stefxLog) XBLF("STEFX: G_TouchTriggersLerped ent=%d exit no client", ent ? ent->s.number : -1);
#endif
		return;
	}

	// dead clients don't activate triggers!
	if ( ent->client->ps.stats[STAT_HEALTH] <= 0 ) {
#ifdef _XBOX
		if (stefxLog) XBLF("STEFX: G_TouchTriggersLerped ent=%d exit dead", ent->s.number);
#endif
		return;
	}

#ifdef _XBOX
	if (STEFX_Vec3Bad(ent->currentOrigin) || STEFX_Vec3Bad(ent->lastOrigin) ||
		STEFX_Vec3Bad(ent->mins) || STEFX_Vec3Bad(ent->maxs))
	{
		XBLF("STEFX: G_TouchTriggersLerped ent=%d observed nonfinite origin=(%g,%g,%g) last=(%g,%g,%g) mins=(%g,%g,%g) maxs=(%g,%g,%g)",
			ent->s.number,
			ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2],
			ent->lastOrigin[0], ent->lastOrigin[1], ent->lastOrigin[2],
			ent->mins[0], ent->mins[1], ent->mins[2],
			ent->maxs[0], ent->maxs[1], ent->maxs[2]);
#if defined(STEFX_XBOX_SURVIVAL_HACKS)
		return;
#endif
	}
#endif

	VectorSubtract( ent->currentOrigin, ent->lastOrigin, diff );
	dist = VectorNormalize( diff );
	step = (float)ent->maxs[1] / 2.0f;
#ifdef _XBOX
	if (stefxLog)
	{
		XBLF("STEFX: G_TouchTriggersLerped ent=%d dist=%g step=%g diff=(%g,%g,%g)",
			ent->s.number, dist, step, diff[0], diff[1], diff[2]);
	}
	if (IS_NAN(dist) || IS_NAN(step) || step <= 0.0f)
	{
		XBLF("STEFX: G_TouchTriggersLerped ent=%d observed invalid dist/step dist=%g step=%g",
			ent->s.number, dist, step);
#if defined(STEFX_XBOX_SURVIVAL_HACKS)
		return;
#endif
	}
#endif

	memset (touched, qfalse, sizeof(touched) );

	for ( curDist = 0; !done; curDist += step )
	{
#ifdef _XBOX
		if (++lerpGuard > 256)
		{
			XBLF("STEFX: G_TouchTriggersLerped ent=%d observed lerp guard overflow cur=%g dist=%g step=%g",
				ent->s.number, curDist, dist, step);
#if defined(STEFX_XBOX_SURVIVAL_HACKS)
			break;
#endif
		}
#endif
		if ( curDist >= dist )
		{
			VectorCopy( ent->currentOrigin, end );
			done = qtrue;
		}
		else
		{
			VectorMA( ent->lastOrigin, curDist, diff, end );
		}
		VectorSubtract( end, range, mins );
		VectorAdd( end, range, maxs );

#ifdef _XBOX
		if (stefxLog)
		{
			XBLF("STEFX: G_TouchTriggersLerped ent=%d before EntitiesInBox cur=%g end=(%g,%g,%g) queryMins=(%g,%g,%g) queryMaxs=(%g,%g,%g)",
				ent->s.number, curDist, end[0], end[1], end[2],
				mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);
		}
#endif
		num = gi.EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
#ifdef _XBOX
		if (stefxLog)
		{
			XBLF("STEFX: G_TouchTriggersLerped ent=%d after EntitiesInBox num=%d", ent->s.number, num);
		}
#endif

		// can't use ent->absmin, because that has a one unit pad
		VectorAdd( end, ent->mins, mins );
		VectorAdd( end, ent->maxs, maxs );

		for ( i=0 ; i<num ; i++ ) {
			hit = touch[i];
#ifdef _XBOX
			if (stefxLog && i < 16)
			{
				XBLF("STEFX: G_TouchTriggersLerped ent=%d touch[%d]=%08x hitNum=%d class='%s' contents=0x%x hitTouch=%d entTouch=%d",
					ent->s.number,
					i,
					(unsigned int)hit,
					hit ? hit->s.number : -1,
					(hit && hit->classname) ? hit->classname : "",
					hit ? hit->contents : 0,
					hit ? hit->e_TouchFunc : -1,
					ent->e_TouchFunc);
			}
#endif

			if ( (hit->e_TouchFunc == touchF_NULL) && (ent->e_TouchFunc == touchF_NULL) ) {
				continue;
			}
			if ( !( hit->contents & CONTENTS_TRIGGER ) ) {
				continue;
			}

			if ( touched[i] == qtrue ) {
				continue;//already touched this move
			}
			// use seperate code for determining if an item is picked up
			// so you don't have to actually contact its bounding box
			/*
			if ( hit->s.eType == ET_ITEM ) {
				if ( !BG_PlayerTouchesItem( &ent->client->ps, &hit->s, level.time ) ) {
					continue;
				}
			} else */
			{
#ifdef _XBOX
				if (stefxLog && i < 16)
				{
					XBLF("STEFX: G_TouchTriggersLerped ent=%d touch[%d] before EntityContact mins=(%g,%g,%g) maxs=(%g,%g,%g)",
						ent->s.number, i, mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);
				}
#endif
				if ( !gi.EntityContact( mins, maxs, hit ) ) {
#ifdef _XBOX
					if (stefxLog && i < 16)
					{
						XBLF("STEFX: G_TouchTriggersLerped ent=%d touch[%d] EntityContact false", ent->s.number, i);
					}
#endif
					continue;
				}
#ifdef _XBOX
				if (stefxLog && i < 16)
				{
					XBLF("STEFX: G_TouchTriggersLerped ent=%d touch[%d] EntityContact true", ent->s.number, i);
				}
#endif
			}

			touched[i] = qtrue;

			memset( &trace, 0, sizeof(trace) );

			if ( hit->e_TouchFunc != touchF_NULL ) {
#ifdef _XBOX
				if (stefxLog && i < 16)
				{
					XBLF("STEFX: G_TouchTriggersLerped ent=%d touch[%d] before hit touchFunc=%d", ent->s.number, i, hit->e_TouchFunc);
				}
#endif
				GEntity_TouchFunc(hit, ent, &trace);
#ifdef _XBOX
				if (stefxLog && i < 16)
				{
					XBLF("STEFX: G_TouchTriggersLerped ent=%d touch[%d] after hit touch", ent->s.number, i);
				}
#endif
			}

			if ( ( ent->NPC != NULL ) && ( ent->e_TouchFunc != touchF_NULL ) ) {
#ifdef _XBOX
				if (stefxLog && i < 16)
				{
					XBLF("STEFX: G_TouchTriggersLerped ent=%d touch[%d] before ent touchFunc=%d", ent->s.number, i, ent->e_TouchFunc);
				}
#endif
				GEntity_TouchFunc( ent, hit, &trace );
#ifdef _XBOX
				if (stefxLog && i < 16)
				{
					XBLF("STEFX: G_TouchTriggersLerped ent=%d touch[%d] after ent touch", ent->s.number, i);
				}
#endif
			}
		}
	}
#ifdef _XBOX
	if (stefxLog)
	{
		XBLF("STEFX: G_TouchTriggersLerped exit ent=%d", ent->s.number);
	}
#endif
}

/*
============
G_TouchTriggers

Find all trigger entities that ent's current position touches.
Spectators will only interact with teleporters.
============
*/
void	G_TouchTriggers( gentity_t *ent ) {
	int			i, num;
	gentity_t	*touch[MAX_GENTITIES], *hit;
	trace_t		trace;
	vec3_t		mins, maxs;
	const vec3_t	range = { 40, 40, 52 };

	if ( !ent->client ) {
		return;
	}

	// dead clients don't activate triggers!
	if ( ent->client->ps.stats[STAT_HEALTH] <= 0 ) {
		return;
	}

	VectorSubtract( ent->client->ps.origin, range, mins );
	VectorAdd( ent->client->ps.origin, range, maxs );

	num = gi.EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );

	// can't use ent->absmin, because that has a one unit pad
	VectorAdd( ent->client->ps.origin, ent->mins, mins );
	VectorAdd( ent->client->ps.origin, ent->maxs, maxs );

	for ( i=0 ; i<num ; i++ ) {
		hit = touch[i];

		if ( (hit->e_TouchFunc == touchF_NULL) && (ent->e_TouchFunc == touchF_NULL) ) {
			continue;
		}
		if ( !( hit->contents & CONTENTS_TRIGGER ) ) {
			continue;
		}

		// use seperate code for determining if an item is picked up
		// so you don't have to actually contact its bounding box
		/*
		if ( hit->s.eType == ET_ITEM ) {
			if ( !BG_PlayerTouchesItem( &ent->client->ps, &hit->s, level.time ) ) {
				continue;
			}
		} else */
		{
			if ( !gi.EntityContact( mins, maxs, hit ) ) {
				continue;
			}
		}

		memset( &trace, 0, sizeof(trace) );

		if ( hit->e_TouchFunc != touchF_NULL ) {
			GEntity_TouchFunc(hit, ent, &trace);
		}

		if ( ( ent->NPC != NULL ) && ( ent->e_TouchFunc != touchF_NULL ) ) {
			GEntity_TouchFunc( ent, hit, &trace );
		}
	}
}


/*
============
G_MoverTouchTriggers

Find all trigger entities that ent's current position touches.
Spectators will only interact with teleporters.
============
*/
void G_MoverTouchTeleportTriggers( gentity_t *ent, vec3_t oldOrg ) 
{
	int			i, num;
	float		step, stepSize, dist;
	gentity_t	*touch[MAX_GENTITIES], *hit;
	trace_t		trace;
	vec3_t		mins, maxs, dir, size, checkSpot;
	const vec3_t	range = { 40, 40, 52 };

	// non-moving movers don't hit triggers!
	if ( !VectorLengthSquared( ent->s.pos.trDelta ) ) 
	{
		return;
	}

	VectorSubtract( ent->mins, ent->maxs, size );
	stepSize = VectorLength( size );
	if ( stepSize < 1 )
	{
		stepSize = 1;
	}

	VectorSubtract( ent->currentOrigin, oldOrg, dir );
	dist = VectorNormalize( dir );
	for ( step = 0; step <= dist; step += stepSize )
	{
		VectorMA( ent->currentOrigin, step, dir, checkSpot );
		VectorSubtract( checkSpot, range, mins );
		VectorAdd( checkSpot, range, maxs );

		num = gi.EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );

		// can't use ent->absmin, because that has a one unit pad
		VectorAdd( checkSpot, ent->mins, mins );
		VectorAdd( checkSpot, ent->maxs, maxs );

		for ( i=0 ; i<num ; i++ ) 
		{
			hit = touch[i];

			if ( hit->s.eType != ET_TELEPORT_TRIGGER )
			{
				continue;
			}

			if ( hit->e_TouchFunc == touchF_NULL ) 
			{
				continue;
			}

			if ( !( hit->contents & CONTENTS_TRIGGER ) ) 
			{
				continue;
			}


			if ( !gi.EntityContact( mins, maxs, hit ) ) 
			{
				continue;
			}

			memset( &trace, 0, sizeof(trace) );

			if ( hit->e_TouchFunc != touchF_NULL ) 
			{
				GEntity_TouchFunc(hit, ent, &trace);
			}
		}
	}
}

void G_NPCMunroMatchPlayerWeapon( gentity_t *ent )
{
	//special uber hack for cinematic Munro's to match player's weapon
	if ( !in_camera )
	{
		if ( ent && ent->client && ent->NPC && (ent->NPC->aiFlags&NPCAI_MATCHPLAYERWEAPON) )
		{//we're a Munro NPC
			int newWeap;
			if ( g_entities[0].client->ps.weapon == WP_PHASER || g_entities[0].client->ps.weapon > WP_DREADNOUGHT )//WP_VOYAGER_HYPO
			{
				newWeap = WP_COMPRESSION_RIFLE;
			}
			else
			{
				newWeap = g_entities[0].client->ps.weapon;
			}
			if ( newWeap != WP_NONE && ent->client->ps.weapon != newWeap )
			{
				ent->client->ps.stats[STAT_WEAPONS] = ( 1 << newWeap );
				ent->client->ps.ammo[weaponData[newWeap].ammoIndex] = 999;
				ChangeWeapon( ent, newWeap );
				ent->client->ps.weapon = newWeap;
				ent->client->ps.weaponstate = WEAPON_READY;
			}
		}
	}
}
/*
=================
ClientInactivityTimer

Returns qfalse if the client is dropped
=================
*/
qboolean ClientInactivityTimer( gclient_t *client ) {
	if ( ! g_inactivity->integer ) {
		// give everyone some time, so if the operator sets g_inactivity during
		// gameplay, everyone isn't kicked
		client->inactivityTime = level.time + 60 * 1000;
		client->inactivityWarning = qfalse;
	} else if ( client->usercmd.forwardmove || 
		client->usercmd.rightmove || 
		client->usercmd.upmove ||
		(client->usercmd.buttons & BUTTON_ATTACK) ||
		(client->usercmd.buttons & BUTTON_ALT_ATTACK) ) {
		client->inactivityTime = level.time + g_inactivity->integer * 1000;
		client->inactivityWarning = qfalse;
	} else if ( !client->pers.localClient ) {
		if ( level.time > client->inactivityTime ) {
			gi.DropClient( client - level.clients, "Dropped due to inactivity" );
			return qfalse;
		}
		if ( level.time > client->inactivityTime - 10000 && !client->inactivityWarning ) {
			client->inactivityWarning = qtrue;
			gi.SendServerCommand( client - level.clients, "cp \"Ten seconds until inactivity drop!\n\"" );
		}
	}
	return qtrue;
}

/*
==================
ClientTimerActions

Actions that happen once a second
==================
*/
void ClientTimerActions( gentity_t *ent, int msec ) {
	gclient_t *client;

	client = ent->client;
	client->timeResidual += msec;

	while ( client->timeResidual >= 1000 ) {
		client->timeResidual -= 1000;
	}
}

/*
====================
ClientIntermissionThink
====================
*/
static qboolean ClientCinematicThink( gclient_t *client ) {
	client->ps.eFlags &= ~EF_TALK;
	client->ps.eFlags &= ~EF_FIRING;

	// swap button actions
	client->oldbuttons = client->buttons;
	client->buttons = client->usercmd.buttons;
	if ( client->buttons & ( /*BUTTON_ATTACK |*/ BUTTON_USE_HOLDABLE ) & ( client->oldbuttons ^ client->buttons ) ) {
		return( qtrue );
	}
	return( qfalse );
}


/*
================
ClientEvents

Events will be passed on to the clients for presentation,
but any server game effects are handled here
================
*/
void ClientEvents( gentity_t *ent, int oldEventSequence ) {
	int		i;
	int		event;
	gclient_t *client;
	int		damage;
	qboolean	fired;

	client = ent->client;

	fired = qfalse;

	for ( i = oldEventSequence ; i < client->ps.eventSequence ; i++ ) {
		event = client->ps.events[ i & (MAX_PS_EVENTS-1) ];

		switch ( event ) {
		case EV_FALL_MEDIUM:
		case EV_FALL_FAR://these come from bg_pmove, PM_CrashLand
			if ( ent->s.eType != ET_PLAYER ) {
				break;		// not in the player model
			}
			//FIXME: isn't there a more accurate way to calculate damage from falls?
			if ( event == EV_FALL_FAR ) 
			{
				damage = 50;
			} 
			else 
			{
				damage = 25;
			}
			ent->painDebounceTime = level.time + 200;	// no normal pain sound
			G_Damage (ent, NULL, NULL, NULL, NULL, damage, 0, MOD_FALLING);
			break;

		case EV_FIRE_WEAPON:
#ifdef _XBOX
			{
				static int s_stefxClientFireLogBudget = 48;
				if (s_stefxClientFireLogBudget > 0)
				{
					int ammoIndex = weaponData[client->ps.weapon].ammoIndex;
					int ammoValue = (ammoIndex >= 0 && ammoIndex < MAX_AMMO) ? client->ps.ammo[ammoIndex] : -9999;
					XBLF("STEFX: ClientEvents fire ent=%d event=%d seq=%d oldSeq=%d weapon=%d weaponstate=%d weaponTime=%d buttons=0x%x ammoIndex=%d ammo=%d",
						ent->s.number,
						event,
						client->ps.eventSequence,
						oldEventSequence,
						client->ps.weapon,
						client->ps.weaponstate,
						client->ps.weaponTime,
						client->buttons,
						ammoIndex,
						ammoValue);
					--s_stefxClientFireLogBudget;
				}
			}
#endif
#ifndef FINAL_BUILD
			if ( fired ) {
				gi.Printf( "DOUBLE EV_FIRE_WEAPON AND-OR EV_ALT_FIRE!!\n" );
			}
#endif
			fired = qtrue;
			FireWeapon( ent, qfalse );
			break;

		case EV_ALT_FIRE:
#ifdef _XBOX
			{
				static int s_stefxClientAltFireLogBudget = 24;
				if (s_stefxClientAltFireLogBudget > 0)
				{
					int ammoIndex = weaponData[client->ps.weapon].ammoIndex;
					int ammoValue = (ammoIndex >= 0 && ammoIndex < MAX_AMMO) ? client->ps.ammo[ammoIndex] : -9999;
					XBLF("STEFX: ClientEvents altfire ent=%d event=%d seq=%d oldSeq=%d weapon=%d weaponstate=%d weaponTime=%d buttons=0x%x ammoIndex=%d ammo=%d",
						ent->s.number,
						event,
						client->ps.eventSequence,
						oldEventSequence,
						client->ps.weapon,
						client->ps.weaponstate,
						client->ps.weaponTime,
						client->buttons,
						ammoIndex,
						ammoValue);
					--s_stefxClientAltFireLogBudget;
				}
			}
#endif
#ifndef FINAL_BUILD
			if ( fired ) {
				gi.Printf( "DOUBLE EV_FIRE_WEAPON AND-OR EV_ALT_FIRE!!\n" );
			}
#endif
			fired = qtrue;
			FireWeapon( ent, qtrue );
			break;

		default:
			break;
		}
	}

}

void BG_AddPushVecToUcmd(gentity_t *self, usercmd_t *ucmd)
{
	vec3_t	forward, right, moveDir;
	float	pushSpeed, fMove, rMove;

	pushSpeed = VectorLengthSquared(self->s.pushVec);
	if(!pushSpeed)
	{//not being pushed
		return;
	}

	AngleVectors(self->client->ps.viewangles, forward, right, NULL);
	VectorScale(forward, ucmd->forwardmove/127.0f * self->client->ps.speed, moveDir);
	VectorMA(moveDir, ucmd->rightmove/127.0f * self->client->ps.speed, right, moveDir);
	//moveDir is now our intended move velocity

	VectorAdd(moveDir, self->s.pushVec, moveDir);
	self->client->ps.speed = VectorNormalize(moveDir);
	//moveDir is now our intended move velocity plus our push Vector

	fMove = 127.0 * DotProduct(forward, moveDir);
	rMove = 127.0 * DotProduct(right, moveDir);
	ucmd->forwardmove = floor(fMove);//If in the same dir , will be positive
	ucmd->rightmove = floor(rMove);//If in the same dir , will be positive

	VectorClear(self->s.pushVec);
}

void NPC_Accelerate( gentity_t *ent, qboolean fullWalkAcc, qboolean fullRunAcc )
{
	if ( !ent->client || !ent->NPC )
	{
		return;
	}

	if ( !ent->NPC->stats.acceleration )
	{//No acceleration means just start and stop
		ent->NPC->currentSpeed = ent->NPC->desiredSpeed;
	}
	//FIXME:  in cinematics always accel/decel?
	else if ( ent->NPC->desiredSpeed <= ent->NPC->stats.walkSpeed )
	{//Only accelerate if at walkSpeeds
		if ( ent->NPC->desiredSpeed > ent->NPC->currentSpeed + ent->NPC->stats.acceleration )
		{
			//ent->client->ps.friction = 0;
			ent->NPC->currentSpeed += ent->NPC->stats.acceleration;
		}
		else if ( ent->NPC->desiredSpeed > ent->NPC->currentSpeed )
		{
			//ent->client->ps.friction = 0;
			ent->NPC->currentSpeed = ent->NPC->desiredSpeed;
		}
		else if ( fullWalkAcc && ent->NPC->desiredSpeed < ent->NPC->currentSpeed - ent->NPC->stats.acceleration )
		{//decelerate even when walking
			ent->NPC->currentSpeed -= ent->NPC->stats.acceleration;
		}
		else if ( ent->NPC->desiredSpeed < ent->NPC->currentSpeed )
		{//stop on a dime
			ent->NPC->currentSpeed = ent->NPC->desiredSpeed;
		}
	}
	else//  if ( ent->NPC->desiredSpeed > ent->NPC->stats.walkSpeed )
	{//Only decelerate if at runSpeeds
		if ( fullRunAcc && ent->NPC->desiredSpeed > ent->NPC->currentSpeed + ent->NPC->stats.acceleration )
		{//Accelerate to runspeed
			//ent->client->ps.friction = 0;
			ent->NPC->currentSpeed += ent->NPC->stats.acceleration;
		}
		else if ( ent->NPC->desiredSpeed > ent->NPC->currentSpeed )
		{//accelerate instantly
			//ent->client->ps.friction = 0;
			ent->NPC->currentSpeed = ent->NPC->desiredSpeed;
		}
		else if ( fullRunAcc && ent->NPC->desiredSpeed < ent->NPC->currentSpeed - ent->NPC->stats.acceleration )
		{
			ent->NPC->currentSpeed -= ent->NPC->stats.acceleration;
		}
		else if ( ent->NPC->desiredSpeed < ent->NPC->currentSpeed )
		{
			ent->NPC->currentSpeed = ent->NPC->desiredSpeed;
		}
	}
}

/*
-------------------------
NPC_GetWalkSpeed
-------------------------
*/

static int NPC_GetWalkSpeed( gentity_t *ent )
{
	int	walkSpeed = 0;

	if ( ( ent->client == NULL ) || ( ent->NPC == NULL ) )
		return 0;

	switch ( ent->client->playerTeam )
	{
	case TEAM_BORG:	//To shutup compiler, will add entries later (this is stub code)
	default:
		walkSpeed = ent->NPC->stats.walkSpeed;
		break;
	}

	return walkSpeed;
}

/*
-------------------------
NPC_GetRunSpeed
-------------------------
*/
#define	BORG_RUN_INCR		25
#define SPECIES_RUN_INCR	25	
#define STASIS_RUN_INCR		20
#define	WARBOT_RUN_INCR		20

static int NPC_GetRunSpeed( gentity_t *ent )
{
	int	runSpeed = 0;

	if ( ( ent->client == NULL ) || ( ent->NPC == NULL ) )
		return 0;

	switch ( ent->client->playerTeam )
	{
	case TEAM_BORG:
		runSpeed = ent->NPC->stats.runSpeed;

		runSpeed += BORG_RUN_INCR * (g_spskill->integer%3);
		break;

	case TEAM_8472:
		runSpeed = ent->NPC->stats.runSpeed;
		runSpeed += SPECIES_RUN_INCR * (g_spskill->integer%3);
		break;

	case TEAM_STASIS:
		runSpeed = ent->NPC->stats.runSpeed;
		runSpeed += STASIS_RUN_INCR * (g_spskill->integer%3);
		break;

	case TEAM_BOTS:
		
		//Only for warbot
		if ( ( Q_stricmp( ent->NPC_type, "warriorbot" ) == 0 ) || ( Q_stricmp( ent->NPC_type, "warriorbot_boss" ) == 0 ) )
		{
			runSpeed = ent->NPC->stats.runSpeed;
			runSpeed += WARBOT_RUN_INCR * (g_spskill->integer%3);
			break;
		}
		
		//NOTENOTE: Falls through for other bots

	default:
		runSpeed = ent->NPC->stats.runSpeed;
		break;
	}

	return runSpeed;
}

/*
==============
ClientThink

This will be called once for each client frame, which will
usually be a couple times for each server frame on fast clients.

==============
*/

void ClientThink_real( gentity_t *ent, usercmd_t *ucmd ) 
{
	gclient_t	*client;
	pmove_t		pm;
	vec3_t		oldOrigin;
	int			oldEventSequence;
	int			msec;
#ifdef _XBOX
	static int	s_stefxPostMoveProbeBudget = 2;
	static int	s_stefxPlayerAttackProbeBudget = 96;
	static int	s_stefxPlayerCameraGateBudget = 96;
	static int	s_stefxPlayerMoveProbeBudget = 96;
	qboolean	stefxPostMoveProbe = qfalse;

	if ( s_stefxPostMoveProbeBudget > 0 )
	{
		s_stefxPostMoveProbeBudget--;
		stefxPostMoveProbe = qtrue;
	}
#endif

#ifdef _XBOX
	XBLF("STEFX: ClientThink_real enter ent=%d ptr=%08x class=%s client=%08x npc=%08x ucmd=%08x serverTime=%d",
		ent ? ent->s.number : -1,
		(unsigned int)ent,
		(ent && ent->classname) ? ent->classname : "<null>",
		ent ? (unsigned int)ent->client : 0,
		ent ? (unsigned int)ent->NPC : 0,
		(unsigned int)ucmd,
		ucmd ? ucmd->serverTime : -1);
#endif

	//Don't let the player do anything if in a camera
	if ( ent->s.number == 0 ) {
extern cvar_t	*g_skippingcin;
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		STEFX_Borg1SliceWarp(ent, ucmd);
		STEFX_SmokeUnlockPlayerControl(ent, ucmd);
#endif
		if ( in_camera )
		{
			qboolean cinematicAdvance = ClientCinematicThink(ent->client);
#ifdef _XBOX
			int logButtons = ucmd ? (ucmd->buttons & ~BUTTON_WALKING) : 0;
			qboolean logCameraGate =
				(s_stefxPlayerCameraGateBudget > 88) ||
				(ucmd && (ucmd->forwardmove || ucmd->rightmove || ucmd->upmove || logButtons));
			if ( s_stefxPlayerCameraGateBudget > 0 && logCameraGate )
			{
				XBLF("STEFX: ClientThink player cinematic gate time=%d cmdTime=%d buttons=0x%x move=(%d,%d,%d) result=%d skipping=%d weapon=%d weaponstate=%d weaponTime=%d",
					level.time,
					ucmd ? ucmd->serverTime : -1,
					ucmd ? ucmd->buttons : 0,
					ucmd ? ucmd->forwardmove : 0,
					ucmd ? ucmd->rightmove : 0,
					ucmd ? ucmd->upmove : 0,
					cinematicAdvance ? 1 : 0,
					g_skippingcin ? g_skippingcin->integer : -1,
					ent->client ? ent->client->ps.weapon : -1,
					ent->client ? ent->client->ps.weaponstate : -1,
					ent->client ? ent->client->ps.weaponTime : -1);
				s_stefxPlayerCameraGateBudget--;
			}
#endif
			// watch the code here, you MUST "return" within this IF(), *unless* you're stopping the cinematic skip.
			//
			if ( cinematicAdvance )
			{
				if (g_skippingcin->integer)	// already doing cinematic skip?
				{
					// yes...   so stop skipping...
					gi.cvar_set("skippingCinematic", "0");
					gi.cvar_set("timescale", "1");
				}
				else
				{
					// no... so start skipping...
					gi.cvar_set("skippingCinematic", "1");
					gi.cvar_set("timescale", "100");
					return;
				}
			}
			else
			{
				return;
			}
		}
		else 
		{
			if ( g_skippingcin->integer )
			{//We're skipping the cinematic and it's over now
				gi.cvar_set("timescale", "1");
				gi.cvar_set("skippingCinematic", "0");
			}
			if ( ent->client->ps.pm_type == PM_DEAD && cg.missionStatusDeadTime < level.time )
			{//mission status screen is up because player is dead, stop all scripts
				if (Q_stricmpn(level.mapname,"_holo",5)) {
					stop_icarus = qtrue;
				}
			}
		}

		// Don't allow the player to adjust the pitch when they are in third person overhead cam.
extern vmCvar_t cg_thirdPerson;
		if ( cg_thirdPerson.integer == 2 )
		{
			ucmd->angles[PITCH] = 0;
		}

		if ( player_locked && ent->client->ps.pm_type < PM_DEAD ) {//lock out player control unless dead
			VectorClear(ucmd->angles) ;
			ucmd->forwardmove = 0;
			ucmd->rightmove = 0;
			ucmd->buttons = 0;
			ucmd->upmove = 0;
		}
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		STEFX_SmokeReadyPlayerWeapon(ent, ucmd);
		STEFX_SmokeStageEnemy(ent, ucmd);
		STEFX_SmokeAimAtLiveEnemy(ent, ucmd);
		{
			static int s_stefxPlayerCmdBudget = 80;
			int logButtons = ucmd->buttons & ~BUTTON_WALKING;
			qboolean interestingCmd =
				(ucmd->forwardmove != 0) ||
				(ucmd->rightmove != 0) ||
				(ucmd->upmove != 0) ||
				(logButtons != 0);

			if ( s_stefxPlayerCmdBudget > 0 && ( interestingCmd || s_stefxPlayerCmdBudget > 72 ) )
			{
				XBLF("STEFX: ClientThink player cmd time=%d serverTime=%d move=(%d,%d,%d) buttons=0x%x weapon=%d pmType=%d origin=(%g,%g,%g) locked=%d inCamera=%d",
					level.time,
					ucmd->serverTime,
					ucmd->forwardmove,
					ucmd->rightmove,
					ucmd->upmove,
					ucmd->buttons,
					ent->client->ps.weapon,
					ent->client->ps.pm_type,
					ent->client->ps.origin[0],
					ent->client->ps.origin[1],
					ent->client->ps.origin[2],
					player_locked ? 1 : 0,
					in_camera ? 1 : 0);
				s_stefxPlayerCmdBudget--;
			}

			if ( interestingCmd && s_stefxPlayerMoveProbeBudget > 0 )
			{
				stefxPostMoveProbe = qtrue;
				s_stefxPlayerMoveProbeBudget--;
			}

			if ( (ucmd->buttons & (BUTTON_ATTACK | BUTTON_ALT_ATTACK)) && s_stefxPlayerAttackProbeBudget > 0 )
			{
				stefxPostMoveProbe = qtrue;
				s_stefxPlayerAttackProbeBudget--;
				XBLF("STEFX: ClientThink player attack probe armed remaining=%d time=%d buttons=0x%x weapon=%d weaponstate=%d weaponTime=%d fireDelay=%d eventSeq=%d",
					s_stefxPlayerAttackProbeBudget,
					level.time,
					ucmd->buttons,
					ent->client->ps.weapon,
					ent->client->ps.weaponstate,
					ent->client->ps.weaponTime,
					ent->client->fireDelay,
					ent->client->ps.eventSequence);
			}
		}
#endif
	}
	else
	{
#ifdef _XBOX
		XBLF("STEFX: ClientThink_real ent=%d before G_NPCMunroMatchPlayerWeapon", ent->s.number);
#endif
		G_NPCMunroMatchPlayerWeapon( ent );
#ifdef _XBOX
		XBLF("STEFX: ClientThink_real ent=%d after G_NPCMunroMatchPlayerWeapon", ent->s.number);
#endif
	}
	client = ent->client;

	// mark the time, so the connection sprite can be removed
#ifdef _XBOX
	XBLF("STEFX: ClientThink_real ent=%d before lastCommand client=%08x", ent->s.number, (unsigned int)client);
#endif
	client->lastCmdTime = level.time;
	client->pers.lastCommand = *ucmd;
#ifdef _XBOX
	XBLF("STEFX: ClientThink_real ent=%d after lastCommand commandTime=%d psWeapon=%d health=%d maxHealth=%d",
		ent->s.number, client->ps.commandTime, client->ps.weapon, client->ps.stats[STAT_HEALTH], client->ps.stats[STAT_MAX_HEALTH]);
#endif

	// sanity check the command time to prevent speedup cheating
	if ( ucmd->serverTime > level.time + 200 ) 
	{
		ucmd->serverTime = level.time + 200;
	}
	if ( ucmd->serverTime < level.time - 1000 ) 
	{
		ucmd->serverTime = level.time - 1000;
	} 

	msec = ucmd->serverTime - client->ps.commandTime;
	if ( msec < 1 ) 
	{
		msec = 1;
	}
	if ( msec > 200 ) 
	{
		msec = 200;
	}

#ifdef _XBOX
	XBLF("STEFX: ClientThink_real ent=%d before inactivity msec=%d serverTime=%d commandTime=%d",
		ent->s.number, msec, ucmd->serverTime, client->ps.commandTime);
#endif
	// check for inactivity timer, but never drop the local client of a non-dedicated server
	if ( !ClientInactivityTimer( client ) ) 
		return;
#ifdef _XBOX
	XBLF("STEFX: ClientThink_real ent=%d after inactivity", ent->s.number);
#endif

	if ( client->noclip ) 
	{
		client->ps.pm_type = PM_NOCLIP;
	} 
	else if ( client->ps.stats[STAT_HEALTH] <= 0 ) 
	{
		client->ps.pm_type = PM_DEAD;
	} 
	else 
	{
		client->ps.pm_type = PM_NORMAL;
	}

	//FIXME: if global gravity changes this should update everyone's personal gravity...
	if ( !(ent->svFlags & SVF_CUSTOM_GRAVITY) )
	{
		client->ps.gravity = g_gravity->value;
	}

	// set speed
	if ( ent->NPC != NULL )
	{//we don't actually scale the ucmd, we use actual speeds
		if ( ent->NPC->combatMove == qfalse )
		{
			if ( !(ucmd->buttons & BUTTON_USE) )
			{//Not leaning
				qboolean Flying = (ucmd->upmove && ent->NPC->stats.moveType == MT_FLYSWIM);
				qboolean Climbing = (ucmd->upmove && ent->watertype&CONTENTS_LADDER );

				client->ps.friction = 6;

				if ( ucmd->forwardmove || ucmd->rightmove || Flying )
				{
					if ( ent->NPC->behaviorState != BS_FORMATION )
					{//In - Formation NPCs set thier desiredSpeed themselves
						if ( ucmd->buttons & BUTTON_WALKING )
						{
							ent->NPC->desiredSpeed = NPC_GetWalkSpeed( ent );//ent->NPC->stats.walkSpeed;
						}
						else//running
						{
							ent->NPC->desiredSpeed = NPC_GetRunSpeed( ent );//ent->NPC->stats.runSpeed;
						}

						if ( ent->NPC->currentSpeed >= 80 )
						{//At higher speeds, need to slow down close to stuff
							//Slow down as you approach your goal
							if ( ent->NPC->distToGoal < SLOWDOWN_DIST && client->race != RACE_BORG && !(ent->NPC->aiFlags&NPCAI_NO_SLOWDOWN) )//128
							{
								if ( ent->NPC->desiredSpeed > MIN_NPC_SPEED )
								{
									float slowdownSpeed = ((float)ent->NPC->desiredSpeed) * ent->NPC->distToGoal / SLOWDOWN_DIST;

									ent->NPC->desiredSpeed = ceil(slowdownSpeed);
									if ( ent->NPC->desiredSpeed < MIN_NPC_SPEED )
									{//don't slow down too much
										ent->NPC->desiredSpeed = MIN_NPC_SPEED;
									}
								}
							}
						}
					}
				}
				else if ( Climbing )
				{
					ent->NPC->desiredSpeed = ent->NPC->stats.walkSpeed;
				}
				else
				{//We want to stop
					ent->NPC->desiredSpeed = 0;
				}

				NPC_Accelerate( ent, (ent->NPC->behaviorState==BS_FORMATION), (ent->NPC->behaviorState==BS_FORMATION) );

				if ( ent->NPC->currentSpeed <= 24 && ent->NPC->desiredSpeed < ent->NPC->currentSpeed )
				{//No-one walks this slow
					client->ps.speed = ent->NPC->currentSpeed = 0;//Full stop
					ucmd->forwardmove = 0;
					ucmd->rightmove = 0;
				}
				else
				{
					if ( ent->NPC->currentSpeed <= ent->NPC->stats.walkSpeed )
					{//Play the walkanim
						ucmd->buttons |= BUTTON_WALKING;
					}
					else
					{
						ucmd->buttons &= ~BUTTON_WALKING;
					}

					if ( ent->NPC->currentSpeed > 0 )
					{//We should be moving
						if ( Climbing || Flying )
						{
							if ( !ucmd->upmove )
							{//We need to force them to take a couple more steps until stopped
								ucmd->upmove = ent->NPC->last_ucmd.upmove;//was last_upmove;
							}
						}
						else if ( !ucmd->forwardmove && !ucmd->rightmove )
						{//We need to force them to take a couple more steps until stopped
							ucmd->forwardmove = ent->NPC->last_ucmd.forwardmove;//was last_forwardmove;
							ucmd->rightmove = ent->NPC->last_ucmd.rightmove;//was last_rightmove;
						}
					}

					client->ps.speed = ent->NPC->currentSpeed;
					//Slow down on turns - don't orbit!!!
					float turndelta = (180 - fabs( AngleDelta( ent->currentAngles[YAW], ent->NPC->desiredYaw ) ))/180;
					
					if ( turndelta < 0.75f )
					{
						client->ps.speed = 0;
					}
					else if ( ent->NPC->distToGoal < 100 && turndelta < 1.0 )
					{//Turn is greater than 45 degrees or closer than 100 to goal
						client->ps.speed = floor(((float)(client->ps.speed))*turndelta);
					}
				}
			}
		}
		else
		{	
			ent->NPC->desiredSpeed = ( ucmd->buttons & BUTTON_WALKING ) ? NPC_GetWalkSpeed( ent ) : NPC_GetRunSpeed( ent );

			client->ps.speed = ent->NPC->desiredSpeed;
		}
	}
	else
	{//Client sets ucmds and such for speed alterations
		client->ps.speed = g_speed->value;//default is 320
	}

#ifdef _XBOX
	XBLF("STEFX: ClientThink_real ent=%d after speed speed=%d currentSpeed=%d desiredSpeed=%d cmd=(%d,%d,%d) buttons=0x%x",
		ent->s.number,
		client->ps.speed,
		ent->NPC ? ent->NPC->currentSpeed : -1,
		ent->NPC ? ent->NPC->desiredSpeed : -1,
		ucmd->forwardmove, ucmd->rightmove, ucmd->upmove, ucmd->buttons);
#endif

	//Apply forced movement
	if ( client->forced_forwardmove )
	{
		ucmd->forwardmove = client->forced_forwardmove;
		if ( !client->ps.speed )
		{
			if ( ent->NPC != NULL )
			{
				client->ps.speed = ent->NPC->stats.runSpeed;
			}
			else
			{
				client->ps.speed = g_speed->value;//default is 320
			}
		}
	}

	if ( client->forced_rightmove )
	{
		ucmd->rightmove = client->forced_rightmove;
		if ( !client->ps.speed )
		{
			if ( ent->NPC != NULL  )
			{
				client->ps.speed = ent->NPC->stats.runSpeed;
			}
			else
			{
				client->ps.speed = g_speed->value;//default is 320
			}
		}
	}

	//FIXME: need to do this before check to avoid walls and cliffs (or just cliffs?)
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBLF("STEFX: ClientThink_real ent=%d before BG_AddPushVecToUcmd", ent->s.number);
#endif
	BG_AddPushVecToUcmd( ent, ucmd );
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBLF("STEFX: ClientThink_real ent=%d after BG_AddPushVecToUcmd before BG_CalculateOffsetAngles", ent->s.number);
#endif

	BG_CalculateOffsetAngles( ent, ucmd );
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBLF("STEFX: ClientThink_real ent=%d after BG_CalculateOffsetAngles", ent->s.number);
#endif

	// set up for pmove
	oldEventSequence = client->ps.eventSequence;

	memset( &pm, 0, sizeof(pm) );

	pm.gent = ent;
	pm.ps = &client->ps;
	pm.cmd = *ucmd;
//	pm.tracemask = MASK_PLAYERSOLID;	// used differently for navgen
	pm.tracemask = ent->clipmask;
	pm.trace = gi.trace;
	pm.pointcontents = gi.pointcontents;
	pm.debugLevel = g_debugMove->integer;
	pm.noFootsteps = 0;//( g_dmflags->integer & DF_NO_FOOTSTEPS ) > 0;

	VectorCopy( client->ps.origin, oldOrigin );

	// perform a pmove
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBLF("STEFX: ClientThink_real ent=%d before Pmove origin=(%g,%g,%g) clipmask=0x%x pmType=%d",
		ent->s.number, client->ps.origin[0], client->ps.origin[1], client->ps.origin[2], ent->clipmask, client->ps.pm_type);
#if defined(STEFX_ELITE_FORCE_SP)
	if ( stefxPostMoveProbe ) STEFX_ClientThinkPMStateLog("before_pmove", ent, ucmd, &pm);
#endif
#endif
	Pmove( &pm );
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBLF("STEFX: ClientThink_real ent=%d after Pmove numtouch=%d water=%d/%d ground=%d",
		ent->s.number, pm.numtouch, pm.waterlevel, pm.watertype, client->ps.groundEntityNum);
#if defined(STEFX_ELITE_FORCE_SP)
	if ( stefxPostMoveProbe ) STEFX_ClientThinkPMStateLog("after_pmove", ent, ucmd, &pm);
#endif
	if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM after Pmove\n");
#endif

	// save results of pmove
	if ( ent->client->ps.eventSequence != oldEventSequence ) 
	{
		ent->eventTime = level.time;
		{
			int		seq;

			seq = (ent->client->ps.eventSequence-1) & (MAX_PS_EVENTS-1);
			ent->s.event = ent->client->ps.events[ seq ] | ( ( ent->client->ps.eventSequence & 3 ) << 8 );
			ent->s.eventParm = ent->client->ps.eventParms[ seq ];
		}
	}
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM after event copy\n");
#endif
	PlayerStateToEntityState( &ent->client->ps, &ent->s );
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM after PlayerStateToEntityState\n");
#endif

	VectorCopy ( ent->currentOrigin, ent->lastOrigin );
#if 1
	// use the precise origin for linking
	VectorCopy( ent->client->ps.origin, ent->currentOrigin );
#else
	//We don't use prediction anymore, so screw this
	// use the snapped origin for linking so it matches client predicted versions
	VectorCopy( ent->s.pos.trBase, ent->currentOrigin );
#endif

	VectorCopy (pm.mins, ent->mins);
	VectorCopy (pm.maxs, ent->maxs);
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM after origin and bounds copy\n");
#endif

	ent->waterlevel = pm.waterlevel;
	ent->watertype = pm.watertype;

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if ( stefxPostMoveProbe )
	{
		vec3_t	stefxMoveDelta;
		qboolean	stefxMoved;

		VectorSubtract( client->ps.origin, oldOrigin, stefxMoveDelta );
		stefxMoved = (qboolean)(VectorLengthSquared( stefxMoveDelta ) > 0.01f);
		XBLF("STEFX: ClientThink PM state result ent=%d time=%d serverTime=%d moved=%d oldOrigin=(%g,%g,%g) newOrigin=(%g,%g,%g) delta=(%g,%g,%g) velocity=(%g,%g,%g) move=(%d,%d,%d) buttons=0x%x events=%d->%d pmFlags=0x%x weapon=%d weaponTime=%d weaponstate=%d ground=%d",
			ent->s.number,
			level.time,
			ucmd->serverTime,
			stefxMoved ? 1 : 0,
			oldOrigin[0], oldOrigin[1], oldOrigin[2],
			client->ps.origin[0], client->ps.origin[1], client->ps.origin[2],
			stefxMoveDelta[0], stefxMoveDelta[1], stefxMoveDelta[2],
			client->ps.velocity[0], client->ps.velocity[1], client->ps.velocity[2],
			ucmd->forwardmove, ucmd->rightmove, ucmd->upmove,
			ucmd->buttons,
			oldEventSequence,
			client->ps.eventSequence,
			client->ps.pm_flags,
			client->ps.weapon,
			client->ps.weaponTime,
			client->ps.weaponstate,
			client->ps.groundEntityNum);
	}
#endif

	VectorCopy( ucmd->angles, client->pers.cmd_angles );

	// execute client events
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM before ClientEvents\n");
#endif
	ClientEvents( ent, oldEventSequence );
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM after ClientEvents\n");
#endif

	if ( pm.useEvent )
	{
		//TODO: Use
#ifdef _XBOX
		if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM before TryUse\n");
#endif
		TryUse( ent );
#ifdef _XBOX
		if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM after TryUse\n");
#endif
	}

	// link entity now, after any personal teleporters have been used
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBLF("STEFX: ClientThink_real ent=%d before post-Pmove linkentity", ent->s.number);
	if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM before linkentity\n");
#endif
	gi.linkentity( ent );
	ent->client->hiddenDist = 0;
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBLF("STEFX: ClientThink_real ent=%d after post-Pmove linkentity before triggers", ent->s.number);
	if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM after linkentity\n");
#endif
	if ( !ent->client->noclip ) 
	{
#ifdef _XBOX
		if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM before G_TouchTriggersLerped\n");
#endif
		G_TouchTriggersLerped( ent );
#ifdef _XBOX
		if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM after G_TouchTriggersLerped\n");
#endif
	}

	// touch other objects
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBLF("STEFX: ClientThink_real ent=%d before ClientImpacts", ent->s.number);
	if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM before ClientImpacts\n");
#endif
	ClientImpacts( ent, &pm );
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBLF("STEFX: ClientThink_real ent=%d after ClientImpacts", ent->s.number);
	if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM after ClientImpacts\n");
#endif

	// swap and latch button actions
	client->oldbuttons = client->buttons;
	client->buttons = ucmd->buttons;
	client->latched_buttons |= client->buttons & ~client->oldbuttons;

	// check for respawning
	if ( client->ps.stats[STAT_HEALTH] <= 0 ) 
	{
		// wait for the attack button to be pressed
		if ( ent->NPC == NULL && level.time > client->respawnTime ) 
		{
			// don't allow respawn if they are still flying through the
			// air, unless 10 extra seconds have passed, meaning something
			// strange is going on, like the corpse is caught in a wind tunnel
			if ( level.time < client->respawnTime + 10000 ) 
			{
				if ( client->ps.groundEntityNum == ENTITYNUM_NONE ) 
				{
					return;
				}
			}

			// pressing attack or use is the normal respawn method
			if ( ucmd->buttons & ( BUTTON_ATTACK | BUTTON_USE_HOLDABLE ) ) 
			{
				respawn( ent );
			}
		}
		return;
	}

	if ((cg.missionStatusShow) && ((cg.missionStatusDeadTime + 1) < level.time))
	{
		if ( ucmd->buttons & ( BUTTON_ATTACK | BUTTON_USE_HOLDABLE ) ) 
		{
			cg.missionStatusShow = 0;
			ScoreBoardReset();
//			Q3_TaskIDComplete( ent, TID_MISSIONSTATUS );
		}
	}
	// perform once-a-second actions
	//ClientTimerActions( ent, msec );
#ifdef _XBOX
	if ( stefxPostMoveProbe ) XBL("STEFX: CLIENT_PM ClientThink_real exit\n");
#endif

	//DEBUG INFO
/*
	if ( client->ps.clientNum < 1 )
	{//Only a player
		if ( ucmd->buttons & BUTTON_USE )
		{
			NAV_PrintLocalWpDebugInfo( ent );
		}
	}
*/
}

/*
==================
ClientThink

A new command has arrived from the client
==================
*/
void ClientThink( int clientNum, usercmd_t *ucmd ) {
	gentity_t *ent;

#ifdef _XBOX
	static int s_stefxClientThinkEntryLogBudget = 8;
	qboolean stefxClientThinkEntryLog = (s_stefxClientThinkEntryLogBudget > 0);
	if (stefxClientThinkEntryLog) XBLF("STEFX: ClientThink enter clientNum=%d ucmd=%08x serverTime=%d g_entities=%08x",
		clientNum, (unsigned int)ucmd, ucmd ? ucmd->serverTime : -1, (unsigned int)g_entities);
#endif
	ent = g_entities + clientNum;
#ifdef _XBOX
	if (stefxClientThinkEntryLog) XBLF("STEFX: ClientThink ent resolved clientNum=%d ent=%08x client=%08x",
		clientNum, (unsigned int)ent, ent ? (unsigned int)ent->client : 0);
#endif
	ent->client->usercmd = *ucmd;
#ifdef _XBOX
	if (stefxClientThinkEntryLog) XBLF("STEFX: ClientThink ent=%d after usercmd copy before real", ent->s.number);
#endif
//	if ( !g_syncronousClients->integer ) 
	{
		ClientThink_real( ent, ucmd );
	}
#ifdef _XBOX
	static int s_clientThinkReturnLogBudget = 8;
	if (s_clientThinkReturnLogBudget > 0)
	{
		XBL("STEFX: CLIENT_PM ClientThink returned\n");
		s_clientThinkReturnLogBudget--;
	}
	if (stefxClientThinkEntryLog)
	{
		XBLF("STEFX: ClientThink ent=%d complete", ent->s.number);
		s_stefxClientThinkEntryLogBudget--;
	}
#endif
}


/*
==============
ClientEndFrame

Called at the end of each server frame for each connected client
A fast client will have multiple ClientThink for each ClientEdFrame,
while a slow client may have multiple ClientEndFrame between ClientThink.
==============
*/
void ClientEndFrame( gentity_t *ent ) {
	int			i;

	// turn off any expired powerups
	for ( i = 0 ; i < MAX_POWERUPS ; i++ ) {
		if ( ent->client->ps.powerups[ i ] < level.time ) {
			ent->client->ps.powerups[ i ] = 0;
		}
	}

	//
	// If the end of unit layout is displayed, don't give
	// the player any normal movement attributes
	//

	// burn from lava, etc
	P_WorldEffects (ent);

	// apply all the damage taken this frame
	P_DamageFeedback (ent);

	// add the EF_CONNECTION flag if we haven't gotten commands recently
	if ( level.time - ent->client->lastCmdTime > 1000 ) {
		ent->s.eFlags |= EF_CONNECTION;
	} else {
		ent->s.eFlags &= ~EF_CONNECTION;
	}

	ent->client->ps.stats[STAT_HEALTH] = ent->health;	// FIXME: get rid of ent->health...

//	G_SetClientSound (ent);
}
