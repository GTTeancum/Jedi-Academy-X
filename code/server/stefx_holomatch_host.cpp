#include "server.h"

#include "../game/stefx_holomatch_game.h"
#include "../win32/xb_log.h"

static qboolean s_stefxHolomatchHostActive = qfalse;
static int s_stefxHolomatchSmokeCombatLogBudget = 24;

static void STEFX_HolomatchStageSmokeCombatProof(int levelTime)
{
	client_t *localClient;
	client_t *botClient;
	playerState_t *localPlayer;
	playerState_t *botPlayer;
	trace_t trace;
	vec3_t angles;
	vec3_t forward;
	vec3_t eye;
	vec3_t end;
	vec3_t stagedOrigin;
	float bestYaw;
	float bestDistance = 0.0f;
	const int smokeTraceMask = CONTENTS_SOLID | CONTENTS_BODY | CONTENTS_SHOTCLIP;
	int yawIndex;

	if (!Cvar_VariableIntegerValue("stefx_hm_smoke_combat_proof") || !svs.clients)
	{
		return;
	}

	localClient = &svs.clients[0];
	botClient = &svs.clients[1];
	localPlayer = localClient->gentity ? localClient->gentity->client : NULL;
	botPlayer = botClient->gentity ? botClient->gentity->client : NULL;
	if (localClient->state != CS_ACTIVE || botClient->state != CS_ACTIVE ||
		!localClient->gentity || !botClient->gentity || !localPlayer || !botPlayer ||
		!(botClient->gentity->svFlags & SVF_BOT))
	{
		return;
	}

	bestYaw = localPlayer->viewangles[YAW];
	VectorCopy(localPlayer->origin, eye);
	eye[2] += localPlayer->viewheight;
	for (yawIndex = 0; yawIndex < 8; ++yawIndex)
	{
		float yaw = (float)(yawIndex * 45);
		float distance;
		VectorClear(angles);
		angles[YAW] = yaw;
		AngleVectors(angles, forward, NULL, NULL);
		VectorMA(eye, 192.0f, forward, end);
		SV_Trace(&trace, eye, NULL, NULL, end, localClient->gentity->s.number, smokeTraceMask);
		distance = trace.fraction * 192.0f;
		if (distance > bestDistance)
		{
			bestDistance = distance;
			bestYaw = yaw;
		}
	}
	VectorClear(localPlayer->viewangles);
	localPlayer->viewangles[YAW] = bestYaw;
	AngleVectors(localPlayer->viewangles, forward, NULL, NULL);
	VectorMA(localPlayer->origin, Com_Clamp(48.0f, 112.0f, bestDistance - 24.0f), forward, stagedOrigin);
	stagedOrigin[2] = localPlayer->origin[2];
	VectorCopy(stagedOrigin, botPlayer->origin);
	VectorClear(botPlayer->velocity);
	VectorCopy(stagedOrigin, botClient->gentity->currentOrigin);
	VectorCopy(stagedOrigin, botClient->gentity->s.origin);
	VectorCopy(stagedOrigin, botClient->gentity->s.pos.trBase);
	botClient->gentity->s.pos.trType = TR_STATIONARY;
	botClient->gentity->s.pos.trTime = levelTime;
	SV_LinkEntity(botClient->gentity);

	if (s_stefxHolomatchSmokeCombatLogBudget > 0)
	{
		XBLog_WriteCriticalf("STEFX_HM_SMOKE_COMBAT: staged target client=1 time=%d localOrigin=(%g,%g,%g) view=(%g,%g,%g) targetOrigin=(%g,%g,%g) bestYaw=%g bestDistance=%g health=%d linked=%d",
			levelTime,
			localPlayer->origin[0], localPlayer->origin[1], localPlayer->origin[2],
			localPlayer->viewangles[0], localPlayer->viewangles[1], localPlayer->viewangles[2],
			stagedOrigin[0], stagedOrigin[1], stagedOrigin[2],
			bestYaw,
			bestDistance,
			botPlayer->stats[STAT_HEALTH],
			botClient->gentity->linked);
		--s_stefxHolomatchSmokeCombatLogBudget;
	}
}

void STEFX_HolomatchHostAfterGameInit(const char *mapname)
{
	s_stefxHolomatchHostActive = (qboolean)STEFX_IsHolomatchMap(mapname);
	s_stefxHolomatchSmokeCombatLogBudget = 24;
	STEFX_HolomatchGameInit(mapname);
	XBLog_WriteCriticalf("STEFX_HM_SWEEP: map active name='%s' active=%d",
		mapname ? mapname : "", s_stefxHolomatchHostActive);
	XBLog_Writef("STEFX_HM_SP: SP host game init map='%s' active=%d ge=%p", mapname ? mapname : "", s_stefxHolomatchHostActive, ge);
}

void STEFX_HolomatchHostRunFrame(int levelTime)
{
	if (!s_stefxHolomatchHostActive)
	{
		return;
	}

	STEFX_HolomatchBotFrame(levelTime);
	STEFX_HolomatchGameFrame(levelTime);
	STEFX_HolomatchStageSmokeCombatProof(levelTime);
}

void STEFX_HolomatchHostAfterGameFrame(int levelTime)
{
	if (!s_stefxHolomatchHostActive)
	{
		return;
	}
	(void)levelTime;
}
