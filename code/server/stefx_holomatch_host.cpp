#include "server.h"

#include "../game/stefx_holomatch_game.h"
#include "../win32/xb_log.h"

static qboolean s_stefxHolomatchHostActive = qfalse;
static int s_stefxHolomatchHostFrameLogBudget = 8;
static int s_stefxHolomatchHostHeartbeat = 0;
static const int STEFX_HOLOMATCH_CENTERED_BOT_LOG_BUDGET = 32;
static int s_stefxHolomatchCenteredBotLogBudget = STEFX_HOLOMATCH_CENTERED_BOT_LOG_BUDGET;
static qboolean s_stefxHolomatchBotActiveLogged = qfalse;
static int s_stefxHolomatchSmokeCombatLogBudget = 24;

static void STEFX_HolomatchLogActiveBot(int levelTime)
{
	int clientNum;

	if (s_stefxHolomatchBotActiveLogged || !svs.clients)
	{
		return;
	}

	for (clientNum = 1; clientNum < MAX_CLIENTS; ++clientNum)
	{
		client_t *client = &svs.clients[clientNum];
		playerState_t *player = client->gentity ? client->gentity->client : NULL;

		if (client->state != CS_ACTIVE || !client->gentity || !player ||
			!(client->gentity->svFlags & SVF_BOT) || !client->gentity->linked)
		{
			continue;
		}

		XBLog_WriteCriticalf(
			"STEFX_HM_SWEEP: bot active client=%d levelTime=%d health=%d weapon=%d",
			clientNum, levelTime, player->stats[STAT_HEALTH], player->weapon);
		s_stefxHolomatchBotActiveLogged = qtrue;
		return;
	}
}

static void STEFX_HolomatchLogCenteredBots(int levelTime)
{
	client_t *localClient = svs.clients ? &svs.clients[0] : NULL;
	playerState_t *localPlayer = (localClient && localClient->gentity) ? localClient->gentity->client : NULL;
	vec3_t eye;
	vec3_t forward;
	int clientNum;

	if (!localPlayer || s_stefxHolomatchCenteredBotLogBudget <= 0)
	{
		return;
	}

	VectorCopy(localPlayer->origin, eye);
	eye[2] += localPlayer->viewheight;
	AngleVectors(localPlayer->viewangles, forward, NULL, NULL);
	for (clientNum = 1; clientNum <= 2; ++clientNum)
	{
		client_t *botClient = &svs.clients[clientNum];
		playerState_t *botPlayer = botClient->gentity ? botClient->gentity->client : NULL;
		vec3_t bodyCenter;
		vec3_t direction;
		float distance;
		float viewDot;

		if (botClient->state != CS_ACTIVE || !botClient->gentity || !botPlayer)
		{
			continue;
		}

		VectorCopy(botPlayer->origin, bodyCenter);
		bodyCenter[2] += 24.0f;
		VectorSubtract(bodyCenter, eye, direction);
		distance = VectorNormalize(direction);
		viewDot = DotProduct(forward, direction);
		if (distance <= 256.0f && viewDot >= 0.985f)
		{
			XBLog_WriteCriticalf("STEFX_HM_CENTER phase=server time=%d client=%d distance=%g dot=%g linked=%d contents=0x%x health=%d view=(%g,%g,%g) eye=(%g,%g,%g) body=(%g,%g,%g)",
				levelTime,
				clientNum,
				distance,
				viewDot,
				botClient->gentity->linked,
				botClient->gentity->contents,
				botPlayer->stats[STAT_HEALTH],
				localPlayer->viewangles[0], localPlayer->viewangles[1], localPlayer->viewangles[2],
				eye[0], eye[1], eye[2],
				bodyCenter[0], bodyCenter[1], bodyCenter[2]);
			--s_stefxHolomatchCenteredBotLogBudget;
		}
	}
}

static void STEFX_HolomatchLogLiveness(int levelTime, const char *phase)
{
	client_t *localClient = svs.clients ? &svs.clients[0] : NULL;
	client_t *clientOne = svs.clients ? &svs.clients[1] : NULL;
	client_t *clientTwo = svs.clients ? &svs.clients[2] : NULL;
	playerState_t *localPlayer = (localClient && localClient->gentity) ? localClient->gentity->client : NULL;
	playerState_t *playerOne = (clientOne && clientOne->gentity) ? clientOne->gentity->client : NULL;
	playerState_t *playerTwo = (clientTwo && clientTwo->gentity) ? clientTwo->gentity->client : NULL;

	if (s_stefxHolomatchHostHeartbeat <= 8 ||
		(s_stefxHolomatchHostHeartbeat % 60) == 0)
	{
		XBLog_WriteCriticalf("STEFX_HM_SP: liveness phase=%s hostFrame=%d levelTime=%d "
			"localState=%d localEnt=%d localType=%d localFlags=0x%x localLinked=%d localContents=0x%x localHealth=%d localWeapon=%d localOrigin=(%g,%g,%g) localAbsmin=(%g,%g,%g) localAbsmax=(%g,%g,%g) "
			"client1State=%d client1Ent=%d client1Type=%d client1Flags=0x%x client1Linked=%d client1Health=%d client1Weapon=%d client1Origin=(%g,%g,%g) "
			"client2State=%d client2Ent=%d client2Type=%d client2Flags=0x%x client2Linked=%d client2Health=%d client2Weapon=%d client2Origin=(%g,%g,%g)",
			phase ? phase : "unknown",
			s_stefxHolomatchHostHeartbeat,
			levelTime,
			localClient ? localClient->state : -1,
			(localClient && localClient->gentity) ? localClient->gentity->s.number : -1,
			(localClient && localClient->gentity) ? localClient->gentity->s.eType : -1,
			(localClient && localClient->gentity) ? localClient->gentity->svFlags : 0,
			(localClient && localClient->gentity) ? (int)localClient->gentity->linked : 0,
			(localClient && localClient->gentity) ? localClient->gentity->contents : 0,
			localPlayer ? localPlayer->stats[STAT_HEALTH] : -1,
			localPlayer ? localPlayer->weapon : -1,
			localPlayer ? localPlayer->origin[0] : 0.0f,
			localPlayer ? localPlayer->origin[1] : 0.0f,
			localPlayer ? localPlayer->origin[2] : 0.0f,
			(localClient && localClient->gentity) ? localClient->gentity->absmin[0] : 0.0f,
			(localClient && localClient->gentity) ? localClient->gentity->absmin[1] : 0.0f,
			(localClient && localClient->gentity) ? localClient->gentity->absmin[2] : 0.0f,
			(localClient && localClient->gentity) ? localClient->gentity->absmax[0] : 0.0f,
			(localClient && localClient->gentity) ? localClient->gentity->absmax[1] : 0.0f,
			(localClient && localClient->gentity) ? localClient->gentity->absmax[2] : 0.0f,
			clientOne ? clientOne->state : -1,
			(clientOne && clientOne->gentity) ? clientOne->gentity->s.number : -1,
			(clientOne && clientOne->gentity) ? clientOne->gentity->s.eType : -1,
			(clientOne && clientOne->gentity) ? clientOne->gentity->svFlags : 0,
			(clientOne && clientOne->gentity) ? (int)clientOne->gentity->linked : 0,
			playerOne ? playerOne->stats[STAT_HEALTH] : -1,
			playerOne ? playerOne->weapon : -1,
			playerOne ? playerOne->origin[0] : 0.0f,
			playerOne ? playerOne->origin[1] : 0.0f,
			playerOne ? playerOne->origin[2] : 0.0f,
			clientTwo ? clientTwo->state : -1,
			(clientTwo && clientTwo->gentity) ? clientTwo->gentity->s.number : -1,
			(clientTwo && clientTwo->gentity) ? clientTwo->gentity->s.eType : -1,
			(clientTwo && clientTwo->gentity) ? clientTwo->gentity->svFlags : 0,
			(clientTwo && clientTwo->gentity) ? (int)clientTwo->gentity->linked : 0,
			playerTwo ? playerTwo->stats[STAT_HEALTH] : -1,
			playerTwo ? playerTwo->weapon : -1,
			playerTwo ? playerTwo->origin[0] : 0.0f,
			playerTwo ? playerTwo->origin[1] : 0.0f,
			playerTwo ? playerTwo->origin[2] : 0.0f);
	}
}

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
	s_stefxHolomatchHostFrameLogBudget = 8;
	s_stefxHolomatchHostHeartbeat = 0;
	s_stefxHolomatchCenteredBotLogBudget = STEFX_HOLOMATCH_CENTERED_BOT_LOG_BUDGET;
	s_stefxHolomatchBotActiveLogged = qfalse;
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

	++s_stefxHolomatchHostHeartbeat;
	if (s_stefxHolomatchHostHeartbeat <= 8 ||
		(s_stefxHolomatchHostHeartbeat % 60) == 0)
	{
		XBLog_WriteCriticalf("STEFX_HM_SP: host heartbeat frame=%d levelTime=%d",
			s_stefxHolomatchHostHeartbeat, levelTime);
	}
	STEFX_HolomatchLogLiveness(levelTime, "begin");

	if (s_stefxHolomatchHostHeartbeat <= 2)
	{
		XBLog_WriteCritical("STEFX_HM_SP: host before official bot frame");
	}
	STEFX_HolomatchBotFrame(levelTime);
	if (s_stefxHolomatchHostHeartbeat <= 2)
	{
		XBLog_WriteCritical("STEFX_HM_SP: host after official bot frame");
	}
	STEFX_HolomatchGameFrame(levelTime);
	if (s_stefxHolomatchHostHeartbeat <= 2)
	{
		XBLog_WriteCritical("STEFX_HM_SP: host frame complete");
	}
	STEFX_HolomatchLogLiveness(levelTime, "complete");
	STEFX_HolomatchStageSmokeCombatProof(levelTime);
	if (s_stefxHolomatchHostFrameLogBudget > 0)
	{
		XBLog_Writef("STEFX_HM_SP: SP host frame time=%d", levelTime);
		--s_stefxHolomatchHostFrameLogBudget;
	}
}

void STEFX_HolomatchHostAfterGameFrame(int levelTime)
{
	if (!s_stefxHolomatchHostActive)
	{
		return;
	}
	STEFX_HolomatchLogCenteredBots(levelTime);
	STEFX_HolomatchLogActiveBot(levelTime);
}
