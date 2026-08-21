#include "server.h"

#include "../game/stefx_holomatch_game.h"
#include "../renderer/tr_types.h"
#include "../win32/xb_log.h"

extern int STEFX_HolomatchPrepareSplitWeaponProof(int clientNum, int slot);

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
void RE_STEFX_SplitScreen_SetLocalRefdef(int slot, const refdef_t *refdef, qboolean valid);
qboolean CL_STEFX_SplitScreen_BuildHolomatchUsercmd(int localSlot, usercmd_t *cmd, const vec3_t currentAngles, const int deltaAngles[3], int serverTime, int *sourcePort, int *weaponDelta, vec3_t outAngles);
#else
static void RE_STEFX_SplitScreen_SetLocalRefdef(int slot, const refdef_t *refdef, qboolean valid)
{
	(void)slot;
	(void)refdef;
	(void)valid;
}
#endif

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP) && defined(STEFX_SP_HOSTED_MP)
void S_STEFX_SplitScreen_SetLocalListener(int slot, int entityNum, const vec3_t head, const vec3_t axis[3], qboolean valid);
#else
static void S_STEFX_SplitScreen_SetLocalListener(int slot, int entityNum, const vec3_t head, const vec3_t axis[3], qboolean valid)
{
	(void)slot;
	(void)entityNum;
	(void)head;
	(void)axis;
	(void)valid;
}
#endif

static qboolean s_stefxHolomatchHostActive = qfalse;
static int s_stefxHolomatchSmokeCombatLogBudget = 24;
static int s_stefxHolomatchSmokeMapStartTime = 0;
static qboolean s_stefxHolomatchSmokeMapQueued = qfalse;
static int s_stefxHolomatchLocalClientLogBudget = 24;
static int s_stefxHolomatchLocalCmdLogBudget = 32;
static unsigned int s_stefxHolomatchLocalCmdAttackMask = 0;
static unsigned int s_stefxHolomatchPhaserProofLogMask = 0;
static int s_stefxHolomatchSplitRefdefLogBudget = 24;
static int s_stefxHolomatchSplitDeadViewLogBudget = 16;
static int s_stefxHolomatchLocalDeadCmdLogBudget = 16;
static int s_stefxHolomatchSplitStateLogBudget = 16;
static int s_stefxHolomatchSplitStateLastLogTime = 0;
static int s_stefxHolomatchSplitStateSample = 0;
static unsigned int s_stefxHolomatchVirtualSourceLogMask = 0;
static vec3_t s_stefxHolomatchVirtualAngles[4];
static qboolean s_stefxHolomatchVirtualAnglesValid[4];
static int s_stefxHolomatchVirtualAvoidUntil[4];
static int s_stefxHolomatchVirtualAvoidTurn[4];
static int s_stefxHolomatchVirtualAvoidLogBudget = 32;
static int s_stefxHolomatchSmokePinViewLogBudget = 0;

enum
{
	STEFX_HM_WP_PHASER = 1,
	STEFX_HM_PHASER_AMMO_MAX = 50
};

static int STEFX_HolomatchLocalPlayerCount(void);

#if defined(_XBOX) && defined(STEFX_SP_HOSTED_MP)
extern "C" volatile unsigned int g_SPXBHMSplitLaunch[9];
extern "C" volatile unsigned int g_SPXBHMSplitBotProof[32];
extern "C" volatile unsigned int g_SPXBHMSplitStateSerial[4];
extern "C" volatile unsigned int g_SPXBHMSplitStatePlayers[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateBots[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateClientState[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateFlags[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateHealth[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateWeapon[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateP1Dist[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateOriginX[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateOriginY[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateOriginZ[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateViewPitch[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateViewYaw[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateViewRoll[4];
extern "C" volatile unsigned int g_SPXBHMSplitStateTime[4];
extern "C" volatile unsigned int g_SPXBHMSplitCollision[48];
extern "C" volatile unsigned int g_SPXBHMSplitCmdSerial[4];
extern "C" volatile unsigned int g_SPXBHMSplitCmdTime[4];
extern "C" volatile unsigned int g_SPXBHMSplitCmdMoveX[4];
extern "C" volatile unsigned int g_SPXBHMSplitCmdMoveY[4];
extern "C" volatile unsigned int g_SPXBHMSplitCmdMoveZ[4];
extern "C" volatile unsigned int g_SPXBHMSplitCmdButtons[4];
extern "C" volatile unsigned int g_SPXBHMSplitCmdWeapon[4];
extern "C" volatile unsigned int g_SPXBHMSplitCmdAnglePitch[4];
extern "C" volatile unsigned int g_SPXBHMSplitCmdAngleYaw[4];
extern "C" volatile unsigned int g_SPXBHMSplitCmdAngleRoll[4];
extern "C" volatile unsigned int g_SPXBHMSplitRefdefSerial[4];
extern "C" volatile unsigned int g_SPXBHMSplitRefdefX[4];
extern "C" volatile unsigned int g_SPXBHMSplitRefdefY[4];
extern "C" volatile unsigned int g_SPXBHMSplitRefdefZ[4];
extern "C" volatile unsigned int g_SPXBHMSplitRefdefPitch[4];
extern "C" volatile unsigned int g_SPXBHMSplitRefdefYaw[4];
extern "C" volatile unsigned int g_SPXBHMSplitRefdefRoll[4];
#endif

static int STEFX_HolomatchPointLeafCluster(const vec3_t origin, int *area)
{
	int leaf;

	if (area)
	{
		*area = -1;
	}
	if (!origin)
	{
		return -1;
	}

	leaf = CM_PointLeafnum(origin);
	if (area)
	{
		*area = CM_LeafArea(leaf);
	}
	return CM_LeafCluster(leaf);
}

static qboolean STEFX_HolomatchClusterInPVS(int sourceCluster, int testCluster)
{
	const byte *pvs;

	if (sourceCluster < 0 || testCluster < 0)
	{
		return qfalse;
	}
	pvs = CM_ClusterPVS(sourceCluster);
	if (!pvs)
	{
		return qfalse;
	}
	return (pvs[testCluster >> 3] & (1 << (testCluster & 7))) ? qtrue : qfalse;
}

static void STEFX_HolomatchBuildSplitView(const playerState_t *player, int clientNum, refdef_t *refdef)
{
	vec3_t angles;
	float viewheight;

	VectorCopy(player->origin, refdef->vieworg);
	viewheight = (float)player->viewheight;
	if (player->stats[STAT_HEALTH] <= 0 && viewheight < 24.0f)
	{
		viewheight = 36.0f;
	}
	refdef->vieworg[2] += viewheight;
	VectorCopy(player->viewangles, angles);

	if (player->stats[STAT_HEALTH] <= 0)
	{
		angles[PITCH] = 0.0f;
		angles[YAW] = (float)player->stats[STAT_DEAD_YAW];
		angles[ROLL] = 0.0f;
		if (s_stefxHolomatchSplitDeadViewLogBudget > 0)
		{
			XBLog_WriteCriticalf("STEFX_HM_SPLIT_DEAD_VIEW: slot=%d health=%d rawViewheight=%d viewheight=%g origin=(%g,%g,%g) deadYaw=%d",
				clientNum,
				player->stats[STAT_HEALTH],
				player->viewheight,
				viewheight,
				refdef->vieworg[0],
				refdef->vieworg[1],
				refdef->vieworg[2],
				player->stats[STAT_DEAD_YAW]);
			--s_stefxHolomatchSplitDeadViewLogBudget;
		}
	}

	AnglesToAxis(angles, refdef->viewaxis);
}

qboolean STEFX_HolomatchGetSplitHudState(int slot, int *health, int *weapon, int *score)
{
	client_t *client;
	playerState_t *player;

	if (slot < 0 || slot >= 4 || !svs.clients)
	{
		return qfalse;
	}
	if (STEFX_HolomatchLocalPlayerCount() <= slot)
	{
		return qfalse;
	}

	client = &svs.clients[slot];
	player = client->gentity ? client->gentity->client : NULL;
	if (client->state != CS_ACTIVE || !player)
	{
		return qfalse;
	}

	if (health)
	{
		*health = player->stats[STAT_HEALTH];
		if (*health < 0)
		{
			*health = 0;
		}
	}
	if (weapon)
	{
		*weapon = player->weapon;
	}
	if (score)
	{
		*score = player->persistant[PERS_SCORE];
	}
	return qtrue;
}

static void STEFX_HolomatchClearSplitRefdefs(void)
{
	int slot;

	for (slot = 1; slot < 4; ++slot)
	{
		RE_STEFX_SplitScreen_SetLocalRefdef(slot, NULL, qfalse);
		S_STEFX_SplitScreen_SetLocalListener(slot, slot, NULL, NULL, qfalse);
	}
}

static void STEFX_HolomatchFreeInactiveLocalClients(void)
{
	int clientNum;

	if (!svs.clients)
	{
		return;
	}

	for (clientNum = 1; clientNum < 4 && clientNum < MAX_CLIENTS; ++clientNum)
	{
		client_t *client = &svs.clients[clientNum];
		if (!client->stefxHolomatchLocal)
		{
			continue;
		}
		XBLog_WriteCriticalf("STEFX_HM_SPLIT: free inactive local client=%d state=%d",
			clientNum, client->state);
		memset(client, 0, sizeof(*client));
		client->state = CS_FREE;
	}
}

static int STEFX_HolomatchLocalPlayerCount(void)
{
	const char *mode;
	int players;

	if (!Cvar_VariableIntegerValue("stefx_splitScreen"))
	{
		return 1;
	}

	mode = Cvar_VariableString("stefx_splitScreenMode");
	if (!mode || Q_stricmp(mode, "holomatch"))
	{
		return 1;
	}

	players = Cvar_VariableIntegerValue("stefx_hmLocalPlayers");
	if (players <= 0)
	{
		players = Cvar_VariableIntegerValue("stefx_splitScreenPlayers");
	}
	if (players < 1)
	{
		players = 1;
	}
	if (players > 4)
	{
		players = 4;
	}
	return players;
}

static int STEFX_HolomatchVirtualMoveHazard(const playerState_t *player, float yaw,
	int forwardMove, int rightMove, float *wallFraction, float *floorFraction)
{
	static const vec3_t playerMins = { -15.0f, -15.0f, -24.0f };
	static const vec3_t playerMaxs = { 15.0f, 15.0f, 32.0f };
	trace_t wallTrace;
	trace_t floorTrace;
	vec3_t angles;
	vec3_t forward;
	vec3_t right;
	vec3_t direction;
	vec3_t moveEnd;
	vec3_t floorStart;
	vec3_t floorEnd;
	int hazard = 0;

	if (!player)
	{
		return 0;
	}

	VectorClear(angles);
	angles[YAW] = yaw;
	AngleVectors(angles, forward, right, NULL);
	VectorScale(forward, (float)forwardMove, direction);
	VectorMA(direction, (float)rightMove, right, direction);
	direction[2] = 0.0f;
	if (VectorNormalize(direction) == 0.0f)
	{
		return 0;
	}

	VectorMA(player->origin, 56.0f, direction, moveEnd);
	CM_BoxTrace(&wallTrace, player->origin, moveEnd, playerMins, playerMaxs, 0, CONTENTS_SOLID);
	if (wallTrace.startsolid || wallTrace.allsolid || wallTrace.fraction < 0.45f)
	{
		hazard |= 1;
	}

	VectorMA(player->origin, 42.0f, direction, floorStart);
	VectorCopy(floorStart, floorEnd);
	floorStart[2] += 4.0f;
	floorEnd[2] -= 112.0f;
	CM_BoxTrace(&floorTrace, floorStart, floorEnd, vec3_origin, vec3_origin, 0, CONTENTS_SOLID);
	if (player->groundEntityNum != ENTITYNUM_NONE &&
		!floorTrace.startsolid && !floorTrace.allsolid && floorTrace.fraction >= 1.0f)
	{
		hazard |= 2;
	}

	if (wallFraction)
	{
		*wallFraction = wallTrace.fraction;
	}
	if (floorFraction)
	{
		*floorFraction = floorTrace.fraction;
	}
	return hazard;
}

static float STEFX_HolomatchPhaserProofAimYaw(const playerState_t *player, float *bestFraction)
{
	trace_t trace;
	vec3_t angles;
	vec3_t forward;
	vec3_t start;
	vec3_t end;
	float selectedYaw = 0.0f;
	float selectedFraction = -1.0f;
	int direction;

	VectorCopy(player->origin, start);
	start[2] += (float)player->viewheight;
	for (direction = 0; direction < 16; ++direction)
	{
		const float yaw = (float)direction * 22.5f;

		VectorClear(angles);
		angles[YAW] = yaw;
		AngleVectors(angles, forward, NULL, NULL);
		VectorMA(start, 1536.0f, forward, end);
		CM_BoxTrace(&trace, start, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID);
		if (!trace.startsolid && !trace.allsolid && trace.fraction > selectedFraction)
		{
			selectedYaw = yaw;
			selectedFraction = trace.fraction;
		}
	}

	if (bestFraction)
	{
		*bestFraction = selectedFraction;
	}
	return selectedYaw;
}

#if defined(_XBOX) && defined(STEFX_SP_HOSTED_MP)
static unsigned int STEFX_HolomatchProofInt(float value)
{
	return (unsigned int)((int)value);
}

static void STEFX_HolomatchUpdateSplitLaunchProof(int localPlayers, int levelTime)
{
	const char *launchSource = Cvar_VariableString("stefx_hm_launch_source");

	g_SPXBHMSplitLaunch[0] = s_stefxHolomatchHostActive ? 1u : 0u;
	g_SPXBHMSplitLaunch[1] = (unsigned int)Cvar_VariableIntegerValue("stefx_splitScreen");
	g_SPXBHMSplitLaunch[2] = (unsigned int)Cvar_VariableIntegerValue("stefx_splitScreenPlayers");
	g_SPXBHMSplitLaunch[3] = (unsigned int)localPlayers;
	g_SPXBHMSplitLaunch[4] = (unsigned int)Cvar_VariableIntegerValue("stefx_hm_split_virtual_controls");
	g_SPXBHMSplitLaunch[5] = (unsigned int)Cvar_VariableIntegerValue("stefx_hm_split_virtual_controls_p1");
	g_SPXBHMSplitLaunch[6] = (unsigned int)Cvar_VariableIntegerValue("stefx_hm_split_economy");
	g_SPXBHMSplitLaunch[7] = !Q_stricmp(launchSource, "xbe") ? 1u
		: (!Q_stricmp(launchSource, "menu") ? 2u
		: (!Q_stricmp(launchSource, "direct") ? 3u : 0u));
	g_SPXBHMSplitLaunch[8] = (unsigned int)levelTime;
}
#endif

static int STEFX_HolomatchCycleOwnedWeapon(const playerState_t *player, int direction)
{
	int candidate;
	int step;

	if (!player || !direction)
	{
		return player ? player->weapon : 0;
	}
	candidate = player->weapon;
	for (step = 0; step < WP_NUM_WEAPONS; ++step)
	{
		candidate += direction;
		if (candidate <= WP_NONE)
		{
			candidate = WP_NUM_WEAPONS - 1;
		}
		else if (candidate >= WP_NUM_WEAPONS)
		{
			candidate = WP_NONE + 1;
		}
		if (player->stats[STAT_WEAPONS] & (1 << candidate))
		{
			return candidate;
		}
	}
	return player->weapon;
}

static qboolean STEFX_HolomatchBuildLocalUsercmd(int clientNum, int levelTime, usercmd_t *cmd)
{
	client_t *client;
	playerState_t *player;
	int proofWeapon = 0;
	int sourcePort = -1;
	int weaponDelta = 0;
	vec3_t padAngles;
	float phase;
	float yawRate;
	float wallFraction = 1.0f;
	float floorFraction = 1.0f;
	float proofAimFraction = 0.0f;
	int virtualHazard;
	qboolean forceVirtualP1;

	memset(cmd, 0, sizeof(*cmd));
	cmd->serverTime = levelTime;

	if (!svs.clients || clientNum < 0 || clientNum >= 4 || clientNum >= MAX_CLIENTS)
	{
		return qfalse;
	}

	client = &svs.clients[clientNum];
	player = client->gentity ? client->gentity->client : NULL;
	forceVirtualP1 = (qboolean)(clientNum == 0 &&
		Cvar_VariableIntegerValue("stefx_hm_split_virtual_controls") &&
		Cvar_VariableIntegerValue("stefx_hm_split_virtual_controls_p1"));
	if (player && !s_stefxHolomatchVirtualAnglesValid[clientNum])
	{
		VectorCopy(player->viewangles, s_stefxHolomatchVirtualAngles[clientNum]);
		s_stefxHolomatchVirtualAnglesValid[clientNum] = qtrue;
	}

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if (!forceVirtualP1 && player && CL_STEFX_SplitScreen_BuildHolomatchUsercmd(clientNum,
		cmd,
		player->viewangles,
		player->delta_angles,
		levelTime,
		&sourcePort,
		&weaponDelta,
		padAngles))
	{
		VectorCopy(padAngles, s_stefxHolomatchVirtualAngles[clientNum]);
		s_stefxHolomatchVirtualAnglesValid[clientNum] = qtrue;
		if (Cvar_VariableIntegerValue("stefx_hm_split_weapon_proof"))
		{
			proofWeapon = STEFX_HolomatchPrepareSplitWeaponProof(clientNum, clientNum);
		}
		cmd->weapon = (byte)(proofWeapon ? proofWeapon : STEFX_HolomatchCycleOwnedWeapon(player, weaponDelta));
		return qtrue;
	}
#endif
	if (forceVirtualP1 && !(s_stefxHolomatchVirtualSourceLogMask & 1u))
	{
		XBLog_WriteCriticalf("STEFX_HM_SPLIT_VIRTUAL_SOURCE: client=0 virtualP1=1 realPadBypassed=1 time=%d",
			levelTime);
		s_stefxHolomatchVirtualSourceLogMask |= 1u;
	}

	if (!Cvar_VariableIntegerValue("stefx_hm_split_virtual_controls") ||
		(clientNum == 0 && !Cvar_VariableIntegerValue("stefx_hm_split_virtual_controls_p1")))
	{
		return qfalse;
	}

	phase = (float)((levelTime + clientNum * 731) % 5000) / 5000.0f;
	yawRate = (clientNum == 0 || clientNum == 2) ? -1.0f : 1.0f;
	s_stefxHolomatchVirtualAngles[clientNum][PITCH] = 0.0f;
	s_stefxHolomatchVirtualAngles[clientNum][YAW] += yawRate * (0.55f + 0.20f * (clientNum + 1));
	s_stefxHolomatchVirtualAngles[clientNum][ROLL] = 0.0f;

	if (s_stefxHolomatchVirtualAngles[clientNum][YAW] > 360.0f)
	{
		s_stefxHolomatchVirtualAngles[clientNum][YAW] -= 360.0f;
	}
	else if (s_stefxHolomatchVirtualAngles[clientNum][YAW] < 0.0f)
	{
		s_stefxHolomatchVirtualAngles[clientNum][YAW] += 360.0f;
	}

	cmd->forwardmove = (phase < 0.50f) ? 90 : -40;
	cmd->rightmove = (clientNum == 0) ? -36 : ((clientNum == 1) ? 44 : ((clientNum == 2) ? -54 : 28));
	virtualHazard = 0;
	if (player && player->stats[STAT_HEALTH] > 0 && player->pm_type == PM_NORMAL &&
		levelTime >= s_stefxHolomatchVirtualAvoidUntil[clientNum])
	{
		virtualHazard = STEFX_HolomatchVirtualMoveHazard(player,
			s_stefxHolomatchVirtualAngles[clientNum][YAW],
			cmd->forwardmove,
			cmd->rightmove,
			&wallFraction,
			&floorFraction);
		if (virtualHazard)
		{
			s_stefxHolomatchVirtualAvoidTurn[clientNum] = (clientNum & 1) ? 1 : -1;
			s_stefxHolomatchVirtualAvoidUntil[clientNum] = levelTime + 700;
			s_stefxHolomatchVirtualAngles[clientNum][YAW] = AngleNormalize360(
				s_stefxHolomatchVirtualAngles[clientNum][YAW] +
				(float)(s_stefxHolomatchVirtualAvoidTurn[clientNum] * 70));
			if (s_stefxHolomatchVirtualAvoidLogBudget > 0)
			{
				XBLog_WriteCriticalf("STEFX_HM_SPLIT_VIRTUAL_AVOID: slot=%d time=%d reason=0x%x wallFraction=%g floorFraction=%g ground=%d origin=(%g,%g,%g)",
					clientNum,
					levelTime,
					virtualHazard,
					wallFraction,
					floorFraction,
					player->groundEntityNum,
					player->origin[0], player->origin[1], player->origin[2]);
				--s_stefxHolomatchVirtualAvoidLogBudget;
			}
		}
	}
	if (player && levelTime < s_stefxHolomatchVirtualAvoidUntil[clientNum])
	{
		cmd->forwardmove = -48;
		cmd->rightmove = (signed char)(s_stefxHolomatchVirtualAvoidTurn[clientNum] * 76);
	}
	if (phase < 0.25f || phase > 0.72f)
	{
		cmd->buttons |= BUTTON_ATTACK;
	}
	if (levelTime >= s_stefxHolomatchVirtualAvoidUntil[clientNum] &&
		phase > 0.38f && phase < 0.45f)
	{
		cmd->upmove = 32;
	}
	if (player && player->stats[STAT_HEALTH] <= 0)
	{
		cmd->buttons |= BUTTON_ATTACK | BUTTON_USE_HOLDABLE;
		cmd->forwardmove = 0;
		cmd->rightmove = 0;
		cmd->upmove = 0;
		if (s_stefxHolomatchLocalDeadCmdLogBudget > 0)
		{
			XBLog_WriteCriticalf("STEFX_HM_SPLIT_DEAD_CMD: client=%d time=%d health=%d respawnButtons=0x%x",
				clientNum,
				levelTime,
				player->stats[STAT_HEALTH],
				cmd->buttons);
			--s_stefxHolomatchLocalDeadCmdLogBudget;
		}
	}
	if (player && player->stats[STAT_HEALTH] > 0 &&
		Cvar_VariableIntegerValue("stefx_hm_split_phaser_proof"))
	{
		const unsigned int clientBit = 1u << clientNum;
		player->ammo[STEFX_HM_WP_PHASER] = STEFX_HM_PHASER_AMMO_MAX;
		s_stefxHolomatchVirtualAngles[clientNum][PITCH] = 0.0f;
		s_stefxHolomatchVirtualAngles[clientNum][YAW] =
			STEFX_HolomatchPhaserProofAimYaw(player, &proofAimFraction);
		s_stefxHolomatchVirtualAngles[clientNum][ROLL] = 0.0f;
		cmd->forwardmove = 0;
		cmd->rightmove = 0;
		cmd->upmove = 0;
		if (player->pm_flags & PMF_RESPAWNED)
		{
			cmd->buttons &= ~BUTTON_ATTACK;
		}
		else
		{
			cmd->buttons |= BUTTON_ATTACK;
		}
		if (!(s_stefxHolomatchPhaserProofLogMask & clientBit))
		{
			XBLog_WriteCriticalf("STEFX_HM_SPLIT_PHASER_PROOF: slot=%d time=%d weapon=%d buttons=0x%x hold=1 respawnRelease=%d aimYaw=%g clearFraction=%g",
				clientNum, levelTime, player->weapon, cmd->buttons,
				(player->pm_flags & PMF_RESPAWNED) ? 1 : 0,
				s_stefxHolomatchVirtualAngles[clientNum][YAW], proofAimFraction);
			s_stefxHolomatchPhaserProofLogMask |= clientBit;
		}
	}
	if (player && Cvar_VariableIntegerValue("stefx_hm_split_weapon_proof"))
	{
		proofWeapon = STEFX_HolomatchPrepareSplitWeaponProof(clientNum, clientNum);
		if (proofWeapon && player->weapon != proofWeapon)
		{
			cmd->buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK);
		}
	}
	cmd->weapon = (byte)(proofWeapon ? proofWeapon : (player ? player->weapon : 0));
	cmd->angles[PITCH] = ANGLE2SHORT(s_stefxHolomatchVirtualAngles[clientNum][PITCH]) - (player ? player->delta_angles[PITCH] : 0);
	cmd->angles[YAW] = ANGLE2SHORT(s_stefxHolomatchVirtualAngles[clientNum][YAW]) - (player ? player->delta_angles[YAW] : 0);
	cmd->angles[ROLL] = ANGLE2SHORT(s_stefxHolomatchVirtualAngles[clientNum][ROLL]) - (player ? player->delta_angles[ROLL] : 0);
	return qtrue;
}

static void STEFX_HolomatchEnsureLocalClients(int localPlayers, int levelTime)
{
	int clientNum;

	if (!svs.clients || !ge)
	{
		return;
	}

	for (clientNum = 1; clientNum < localPlayers && clientNum < MAX_CLIENTS; ++clientNum)
	{
		client_t *client = &svs.clients[clientNum];
		usercmd_t cmd;
		const char *denied;

		if (client->state >= CS_CONNECTED &&
			(client->stefxHolomatchBot ||
			 (client->gentity && (client->gentity->svFlags & SVF_BOT))))
		{
			if (s_stefxHolomatchLocalClientLogBudget > 0)
			{
				XBLog_WriteCriticalf("STEFX_HM_SPLIT: reclaim bot slot for local client=%d oldState=%d time=%d",
					clientNum, client->state, levelTime);
				--s_stefxHolomatchLocalClientLogBudget;
			}
			if (client->gentity)
			{
				ge->ClientDisconnect(clientNum);
			}
			memset(client, 0, sizeof(*client));
			client->state = CS_FREE;
		}

		if (client->state == CS_ACTIVE && client->gentity)
		{
			client->stefxHolomatchBot = qfalse;
			client->stefxHolomatchLocal = qtrue;
			client->lastPacketTime = levelTime;
			client->timeoutCount = 0;
			client->nextSnapshotTime = 0x7fffffff;
			continue;
		}

		if (client->state == CS_FREE || client->state == CS_ZOMBIE)
		{
			memset(client, 0, sizeof(*client));
			client->state = CS_CONNECTED;
			client->stefxHolomatchBot = qfalse;
			client->stefxHolomatchLocal = qtrue;
			client->snapshotMsec = 50;
			client->lastPacketTime = levelTime;
			client->lastConnectTime = levelTime;
			client->nextSnapshotTime = 0x7fffffff;
			client->gamestateMessageNum = -1;
			Com_sprintf(client->userinfo, sizeof(client->userinfo),
				"\\name\\Player %d\\rate\\25000\\snaps\\20\\model\\munro/default",
				clientNum + 1);
			denied = ge->ClientConnect(clientNum, qtrue, eNO);
			if (denied)
			{
				XBLog_WriteCriticalf("STEFX_HM_SPLIT: local client denied client=%d reason='%s'",
					clientNum, denied);
				client->state = CS_FREE;
				continue;
			}
			SV_UserinfoChanged(client);
		}

		(void)STEFX_HolomatchBuildLocalUsercmd(clientNum, levelTime, &cmd);
		SV_ClientEnterWorld(client, &cmd, eNO);
		if (s_stefxHolomatchLocalClientLogBudget > 0)
		{
			XBLog_WriteCriticalf("STEFX_HM_SPLIT: local client active client=%d state=%d time=%d origin=(%g,%g,%g)",
				clientNum,
				client->state,
				levelTime,
				client->gentity && client->gentity->client ? client->gentity->client->origin[0] : 0.0f,
				client->gentity && client->gentity->client ? client->gentity->client->origin[1] : 0.0f,
				client->gentity && client->gentity->client ? client->gentity->client->origin[2] : 0.0f);
			--s_stefxHolomatchLocalClientLogBudget;
		}
	}
}

static void STEFX_HolomatchRunLocalUsercmds(int localPlayers, int levelTime)
{
	int clientNum;
	int firstClient;

	if (!svs.clients)
	{
		return;
	}

	firstClient = 0;
	for (clientNum = firstClient; clientNum < localPlayers && clientNum < 4 && clientNum < MAX_CLIENTS; ++clientNum)
	{
		client_t *client = &svs.clients[clientNum];
		usercmd_t cmd;
		if (client->state != CS_ACTIVE || !client->gentity || !client->gentity->client)
		{
			continue;
		}

		client->lastPacketTime = levelTime;
		client->timeoutCount = 0;
		if (clientNum > 0)
		{
			client->nextSnapshotTime = 0x7fffffff;
		}
		if (!STEFX_HolomatchBuildLocalUsercmd(clientNum, levelTime, &cmd))
		{
			continue;
		}
		SV_ClientThink(client, &cmd);
#if defined(_XBOX) && defined(STEFX_SP_HOSTED_MP)
		if (clientNum >= 0 && clientNum < 4)
		{
			++g_SPXBHMSplitCmdSerial[clientNum];
			g_SPXBHMSplitCmdTime[clientNum] = (unsigned int)levelTime;
			g_SPXBHMSplitCmdMoveX[clientNum] = (unsigned int)((int)cmd.forwardmove);
			g_SPXBHMSplitCmdMoveY[clientNum] = (unsigned int)((int)cmd.rightmove);
			g_SPXBHMSplitCmdMoveZ[clientNum] = (unsigned int)((int)cmd.upmove);
			g_SPXBHMSplitCmdButtons[clientNum] = (unsigned int)cmd.buttons;
			g_SPXBHMSplitCmdWeapon[clientNum] = (unsigned int)cmd.weapon;
			g_SPXBHMSplitCmdAnglePitch[clientNum] = (unsigned int)((int)cmd.angles[PITCH]);
			g_SPXBHMSplitCmdAngleYaw[clientNum] = (unsigned int)((int)cmd.angles[YAW]);
			g_SPXBHMSplitCmdAngleRoll[clientNum] = (unsigned int)((int)cmd.angles[ROLL]);
		}
#endif
		{
			const qboolean attackCommand = (cmd.buttons & BUTTON_ATTACK) ? qtrue : qfalse;
			const unsigned int clientBit = (clientNum >= 0 && clientNum < 32) ? (1u << clientNum) : 0u;
			const qboolean firstAttackProof =
				(attackCommand && clientBit && !(s_stefxHolomatchLocalCmdAttackMask & clientBit)) ? qtrue : qfalse;

			if (s_stefxHolomatchLocalCmdLogBudget > 0 || firstAttackProof)
			{
				XBLog_WriteCriticalf("STEFX_HM_SPLIT_CMD: client=%d time=%d move=(%d,%d,%d) buttons=0x%x weapon=%d angles=(%d,%d,%d) attackProof=%d",
					clientNum,
					levelTime,
					cmd.forwardmove,
					cmd.rightmove,
					cmd.upmove,
					cmd.buttons,
					cmd.weapon,
					cmd.angles[PITCH],
					cmd.angles[YAW],
					cmd.angles[ROLL],
					firstAttackProof ? 1 : 0);
				if (firstAttackProof)
				{
					s_stefxHolomatchLocalCmdAttackMask |= clientBit;
				}
				else
				{
					--s_stefxHolomatchLocalCmdLogBudget;
				}
			}
		}
	}
}

static void STEFX_HolomatchUpdateSplitRefdefs(int localPlayers, int levelTime)
{
	int slot;

	for (slot = 1; slot < 4; ++slot)
	{
		client_t *client;
		playerState_t *player;
		refdef_t refdef;
		int area;

		if (!svs.clients || slot >= localPlayers || slot >= MAX_CLIENTS)
		{
			RE_STEFX_SplitScreen_SetLocalRefdef(slot, NULL, qfalse);
			S_STEFX_SplitScreen_SetLocalListener(slot, slot, NULL, NULL, qfalse);
			continue;
		}

		client = &svs.clients[slot];
		player = client->gentity ? client->gentity->client : NULL;
		if (client->state != CS_ACTIVE || !player)
		{
			RE_STEFX_SplitScreen_SetLocalRefdef(slot, NULL, qfalse);
			S_STEFX_SplitScreen_SetLocalListener(slot, slot, NULL, NULL, qfalse);
			continue;
		}

		memset(&refdef, 0, sizeof(refdef));
		refdef.x = 0;
		refdef.y = 0;
		refdef.width = 640;
		refdef.height = 480;
		refdef.fov_x = 90.0f;
		refdef.fov_y = 73.739792f;
		STEFX_HolomatchBuildSplitView(player, slot, &refdef);
		refdef.time = levelTime;
		STEFX_HolomatchPointLeafCluster(refdef.vieworg, &area);
		if (area >= 0)
		{
			int maskWord;
			CM_WriteAreaBits(refdef.areamask, area);
			for (maskWord = 0; maskWord < MAX_MAP_AREA_BYTES / 4; ++maskWord)
			{
				((int *)refdef.areamask)[maskWord] = ((int *)refdef.areamask)[maskWord] ^ -1;
			}
		}
		RE_STEFX_SplitScreen_SetLocalRefdef(slot, &refdef, qtrue);
		S_STEFX_SplitScreen_SetLocalListener(slot, slot, refdef.vieworg, refdef.viewaxis, qtrue);
#if defined(_XBOX) && defined(STEFX_SP_HOSTED_MP)
		++g_SPXBHMSplitRefdefSerial[slot];
		g_SPXBHMSplitRefdefX[slot] = STEFX_HolomatchProofInt(refdef.vieworg[0]);
		g_SPXBHMSplitRefdefY[slot] = STEFX_HolomatchProofInt(refdef.vieworg[1]);
		g_SPXBHMSplitRefdefZ[slot] = STEFX_HolomatchProofInt(refdef.vieworg[2]);
		g_SPXBHMSplitRefdefPitch[slot] = STEFX_HolomatchProofInt(player->viewangles[0]);
		g_SPXBHMSplitRefdefYaw[slot] = STEFX_HolomatchProofInt(player->viewangles[1]);
		g_SPXBHMSplitRefdefRoll[slot] = STEFX_HolomatchProofInt(player->viewangles[2]);
#endif

		if (s_stefxHolomatchSplitRefdefLogBudget > 0)
		{
			XBLog_WriteCriticalf("STEFX_HM_SPLIT_REFDEF: slot=%d client=%d time=%d origin=(%g,%g,%g) angles=(%g,%g,%g)",
				slot,
				slot,
				levelTime,
				refdef.vieworg[0],
				refdef.vieworg[1],
				refdef.vieworg[2],
				player->viewangles[0],
				player->viewangles[1],
				player->viewangles[2]);
			--s_stefxHolomatchSplitRefdefLogBudget;
		}
	}
}

static void STEFX_HolomatchLogSplitState(int localPlayers, int levelTime)
{
	int slot;
	int botCount = 0;
	int p1Area = -1;
	int p1Cluster = -1;
	qboolean logState;
	vec3_t p1ViewOrg;

	if (!svs.clients)
	{
		return;
	}

	logState = qfalse;
	if (s_stefxHolomatchSplitStateLogBudget > 0 &&
		(!s_stefxHolomatchSplitStateLastLogTime ||
		 levelTime - s_stefxHolomatchSplitStateLastLogTime >= 500))
	{
		logState = qtrue;
		s_stefxHolomatchSplitStateLastLogTime = levelTime;
		++s_stefxHolomatchSplitStateSample;
	}

	VectorClear(p1ViewOrg);
	if (svs.clients[0].gentity && svs.clients[0].gentity->client)
	{
		playerState_t *p1 = svs.clients[0].gentity->client;
		VectorCopy(p1->origin, p1ViewOrg);
		p1ViewOrg[2] += p1->viewheight;
		p1Cluster = STEFX_HolomatchPointLeafCluster(p1ViewOrg, &p1Area);
	}

	for (slot = localPlayers; slot < MAX_CLIENTS; ++slot)
	{
		client_t *client = &svs.clients[slot];
		if (client->state == CS_ACTIVE &&
			client->gentity &&
			(client->gentity->svFlags & SVF_BOT))
		{
			++botCount;
		}
	}

	for (slot = 0; slot < localPlayers && slot < 4 && slot < MAX_CLIENTS; ++slot)
	{
		client_t *client = &svs.clients[slot];
		playerState_t *player = client->gentity ? client->gentity->client : NULL;
		vec3_t viewOrg;
		int area = -1;
		int cluster = -1;
		int collisionLeaf = 0;
		int originContents = 0;
		int viewContents = 0;
		float p1Distance = 0.0f;
		qboolean inP1Pvs = (slot == 0) ? qtrue : qfalse;

		VectorClear(viewOrg);
		if (player)
		{
			VectorCopy(player->origin, viewOrg);
			viewOrg[2] += player->viewheight;
			cluster = STEFX_HolomatchPointLeafCluster(viewOrg, &area);
			collisionLeaf = CM_PointLeafnum(viewOrg);
			originContents = CM_PointContents(player->origin, 0);
			viewContents = CM_PointContents(viewOrg, 0);
			p1Distance = Distance(p1ViewOrg, viewOrg);
			inP1Pvs = (slot == 0) ? qtrue : STEFX_HolomatchClusterInPVS(p1Cluster, cluster);
		}

#if defined(_XBOX) && defined(STEFX_SP_HOSTED_MP)
		++g_SPXBHMSplitStateSerial[slot];
		g_SPXBHMSplitStatePlayers[slot] = (unsigned int)localPlayers;
		g_SPXBHMSplitStateBots[slot] = (unsigned int)botCount;
		g_SPXBHMSplitStateClientState[slot] = (unsigned int)client->state;
		g_SPXBHMSplitStateFlags[slot] =
			(client->stefxHolomatchLocal ? 1u : 0u) |
			(client->stefxHolomatchBot ? 2u : 0u);
		g_SPXBHMSplitStateHealth[slot] = (unsigned int)(player ? player->stats[STAT_HEALTH] : 0);
		g_SPXBHMSplitStateWeapon[slot] = (unsigned int)(player ? player->weapon : 0);
		g_SPXBHMSplitStateP1Dist[slot] = STEFX_HolomatchProofInt(p1Distance);
		g_SPXBHMSplitStateOriginX[slot] = STEFX_HolomatchProofInt(player ? player->origin[0] : 0.0f);
		g_SPXBHMSplitStateOriginY[slot] = STEFX_HolomatchProofInt(player ? player->origin[1] : 0.0f);
		g_SPXBHMSplitStateOriginZ[slot] = STEFX_HolomatchProofInt(player ? player->origin[2] : 0.0f);
		g_SPXBHMSplitStateViewPitch[slot] = STEFX_HolomatchProofInt(player ? player->viewangles[0] : 0.0f);
		g_SPXBHMSplitStateViewYaw[slot] = STEFX_HolomatchProofInt(player ? player->viewangles[1] : 0.0f);
		g_SPXBHMSplitStateViewRoll[slot] = STEFX_HolomatchProofInt(player ? player->viewangles[2] : 0.0f);
		g_SPXBHMSplitStateTime[slot] = (unsigned int)levelTime;
		{
			const int collisionBase = slot * 12;
			++g_SPXBHMSplitCollision[collisionBase + 0];
			g_SPXBHMSplitCollision[collisionBase + 1] = (unsigned int)CM_NumClusters();
			g_SPXBHMSplitCollision[collisionBase + 2] = (unsigned int)CM_NumInlineModels();
			g_SPXBHMSplitCollision[collisionBase + 3] = (unsigned int)collisionLeaf;
			g_SPXBHMSplitCollision[collisionBase + 4] = (unsigned int)area;
			g_SPXBHMSplitCollision[collisionBase + 5] = (unsigned int)cluster;
			g_SPXBHMSplitCollision[collisionBase + 6] = (unsigned int)originContents;
			g_SPXBHMSplitCollision[collisionBase + 7] = (unsigned int)viewContents;
			g_SPXBHMSplitCollision[collisionBase + 8] = (unsigned int)(client->gentity ? client->gentity->contents : 0);
			g_SPXBHMSplitCollision[collisionBase + 9] = (unsigned int)(client->gentity ? client->gentity->linked : 0);
			g_SPXBHMSplitCollision[collisionBase + 10] = (unsigned int)(player ? player->pm_type : -1);
			g_SPXBHMSplitCollision[collisionBase + 11] = (unsigned int)(player ? player->groundEntityNum : -1);
		}
#endif

		if (logState)
		{
			XBLog_WriteCriticalf("STEFX_HM_SPLIT_STATE: slot=%d players=%d bots=%d state=%d local=%d bot=%d svFlags=0x%x health=%d weapon=%d area=%d cluster=%d p1Area=%d p1Cluster=%d p1Pvs=%d p1Dist=%g origin=(%g,%g,%g) view=(%g,%g,%g) time=%d sample=%d interval=500",
				slot,
				localPlayers,
				botCount,
				client->state,
				client->stefxHolomatchLocal ? 1 : 0,
				client->stefxHolomatchBot ? 1 : 0,
				client->gentity ? client->gentity->svFlags : 0,
				player ? player->stats[STAT_HEALTH] : 0,
				player ? player->weapon : 0,
				area,
				cluster,
				p1Area,
				p1Cluster,
				inP1Pvs ? 1 : 0,
				p1Distance,
				player ? player->origin[0] : 0.0f,
				player ? player->origin[1] : 0.0f,
				player ? player->origin[2] : 0.0f,
				player ? player->viewangles[0] : 0.0f,
				player ? player->viewangles[1] : 0.0f,
				player ? player->viewangles[2] : 0.0f,
				levelTime,
				s_stefxHolomatchSplitStateSample);
			XBLog_WriteCriticalf("STEFX_HM_SPLIT_COLLISION: slot=%d cmClusters=%d inlineModels=%d leaf=%d area=%d cluster=%d originContents=0x%x viewContents=0x%x entContents=0x%x linked=%d pmType=%d ground=%d origin=(%g,%g,%g) view=(%g,%g,%g)",
				slot,
				CM_NumClusters(),
				CM_NumInlineModels(),
				collisionLeaf,
				area,
				cluster,
				originContents,
				viewContents,
				client->gentity ? client->gentity->contents : 0,
				client->gentity ? client->gentity->linked : 0,
				player ? player->pm_type : -1,
				player ? player->groundEntityNum : -1,
				player ? player->origin[0] : 0.0f,
				player ? player->origin[1] : 0.0f,
				player ? player->origin[2] : 0.0f,
				viewOrg[0],
				viewOrg[1],
				viewOrg[2]);
		}
	}
#if defined(_XBOX) && defined(STEFX_SP_HOSTED_MP)
	g_SPXBHMSplitBotProof[15] = (unsigned int)botCount;
#endif
	if (logState)
	{
		--s_stefxHolomatchSplitStateLogBudget;
	}
}

static void STEFX_HolomatchRunSmokeMapCycle(int levelTime)
{
	static const char *maps[] = {
		"hm_borg1",
		"hm_dn1",
		"ctf_dn1",
		"ctf_voy1",
		"hm_scav1"
	};
	const int mapCount = sizeof(maps) / sizeof(maps[0]);
	const int holdMsec = Cvar_VariableIntegerValue("stefx_hm_smoke_mapcycle_msec");
	const char *currentMap;
	int mapIndex;

	if (!Cvar_VariableIntegerValue("stefx_hm_smoke_mapcycle") ||
		s_stefxHolomatchSmokeMapQueued || holdMsec <= 0 ||
		levelTime - s_stefxHolomatchSmokeMapStartTime < holdMsec)
	{
		return;
	}

	currentMap = Cvar_VariableString("mapname");
	for (mapIndex = 0; mapIndex < mapCount; ++mapIndex)
	{
		if (!Q_stricmp(currentMap, maps[mapIndex]))
		{
			break;
		}
	}

	if (mapIndex >= mapCount - 1)
	{
		Cvar_Set("stefx_hm_smoke_mapcycle", "0");
		XBLog_WriteCriticalf("STEFX_HM_SWEEP: continuous cycle complete map='%s' time=%d",
			currentMap ? currentMap : "", levelTime);
		return;
	}

	s_stefxHolomatchSmokeMapQueued = qtrue;
	XBLog_WriteCriticalf("STEFX_HM_SWEEP: continuous cycle queue from='%s' to='%s' time=%d",
		currentMap ? currentMap : "", maps[mapIndex + 1], levelTime);
	Cbuf_AddText(va("set g_gametype %d\nmap %s\n",
		!Q_stricmpn(maps[mapIndex + 1], "ctf_", 4) ? 4 : 0,
		maps[mapIndex + 1]));
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
	const int localPlayers = STEFX_HolomatchLocalPlayerCount();
	int yawIndex;
	int botClientNum;

	if (!Cvar_VariableIntegerValue("stefx_hm_smoke_combat_proof") || !svs.clients)
	{
		return;
	}

	localClient = &svs.clients[0];
	botClient = NULL;
	for (botClientNum = localPlayers; botClientNum < MAX_CLIENTS; ++botClientNum)
	{
		if (svs.clients[botClientNum].state == CS_ACTIVE &&
			svs.clients[botClientNum].gentity &&
			(svs.clients[botClientNum].gentity->svFlags & SVF_BOT))
		{
			botClient = &svs.clients[botClientNum];
			break;
		}
	}
	if (!botClient)
	{
		return;
	}
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
		XBLog_WriteCriticalf("STEFX_HM_SMOKE_COMBAT: staged target client=%d time=%d localOrigin=(%g,%g,%g) view=(%g,%g,%g) targetOrigin=(%g,%g,%g) bestYaw=%g bestDistance=%g health=%d linked=%d",
			(int)(botClient - svs.clients),
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

static void STEFX_HolomatchPinSmokeP1View(int levelTime)
{
	client_t *client;
	playerState_t *player;
	float yaw;

	if (!Cvar_VariableIntegerValue("stefx_hm_smoke_pin_p1_view") || !svs.clients)
	{
		return;
	}

	client = &svs.clients[0];
	player = (client->state == CS_ACTIVE && client->gentity) ? client->gentity->client : NULL;
	if (!player)
	{
		return;
	}

	yaw = Cvar_VariableValue("stefx_hm_smoke_pin_p1_yaw");
	VectorClear(player->viewangles);
	player->viewangles[YAW] = yaw;
	player->delta_angles[PITCH] = ANGLE2SHORT(0.0f) - client->lastUsercmd.angles[PITCH];
	player->delta_angles[YAW] = ANGLE2SHORT(yaw) - client->lastUsercmd.angles[YAW];
	player->delta_angles[ROLL] = ANGLE2SHORT(0.0f) - client->lastUsercmd.angles[ROLL];

	if (s_stefxHolomatchSmokePinViewLogBudget > 0)
	{
		XBLog_WriteCriticalf("STEFX_HM_SMOKE_PIN_VIEW: client=0 yaw=%g cmdYaw=%d deltaYaw=%d view=(%g,%g,%g) time=%d",
			yaw,
			client->lastUsercmd.angles[YAW],
			player->delta_angles[YAW],
			player->viewangles[PITCH], player->viewangles[YAW], player->viewangles[ROLL],
			levelTime);
		--s_stefxHolomatchSmokePinViewLogBudget;
	}
}

void STEFX_HolomatchHostAfterGameInit(const char *mapname)
{
	s_stefxHolomatchHostActive = (qboolean)STEFX_IsHolomatchMap(mapname);
	s_stefxHolomatchSmokeCombatLogBudget = 24;
	s_stefxHolomatchLocalClientLogBudget = 24;
	s_stefxHolomatchLocalCmdLogBudget = 32;
	s_stefxHolomatchLocalCmdAttackMask = 0;
	s_stefxHolomatchPhaserProofLogMask = 0;
	s_stefxHolomatchSplitRefdefLogBudget = 24;
	s_stefxHolomatchSplitDeadViewLogBudget = 16;
	s_stefxHolomatchLocalDeadCmdLogBudget = 16;
	s_stefxHolomatchSplitStateLogBudget = 16;
	s_stefxHolomatchSplitStateLastLogTime = 0;
	s_stefxHolomatchSplitStateSample = 0;
	s_stefxHolomatchVirtualSourceLogMask = 0;
	s_stefxHolomatchSmokeMapStartTime = sv.time;
	s_stefxHolomatchSmokeMapQueued = qfalse;
	memset(s_stefxHolomatchVirtualAngles, 0, sizeof(s_stefxHolomatchVirtualAngles));
	memset(s_stefxHolomatchVirtualAnglesValid, 0, sizeof(s_stefxHolomatchVirtualAnglesValid));
	memset(s_stefxHolomatchVirtualAvoidUntil, 0, sizeof(s_stefxHolomatchVirtualAvoidUntil));
	memset(s_stefxHolomatchVirtualAvoidTurn, 0, sizeof(s_stefxHolomatchVirtualAvoidTurn));
	s_stefxHolomatchVirtualAvoidLogBudget = 32;
	s_stefxHolomatchSmokePinViewLogBudget = 8;
	STEFX_HolomatchClearSplitRefdefs();
	if (!s_stefxHolomatchHostActive)
	{
		STEFX_HolomatchFreeInactiveLocalClients();
	}
	STEFX_HolomatchGameInit(mapname);
	XBLog_WriteCriticalf("STEFX_HM_SWEEP: map active name='%s' active=%d",
		mapname ? mapname : "", s_stefxHolomatchHostActive);
	XBLog_Writef("STEFX_HM_SP: SP host game init map='%s' active=%d ge=%p", mapname ? mapname : "", s_stefxHolomatchHostActive, ge);
}

void STEFX_HolomatchHostRunFrame(int levelTime)
{
	int localPlayers;

	if (!s_stefxHolomatchHostActive)
	{
		return;
	}

	localPlayers = STEFX_HolomatchLocalPlayerCount();
#if defined(_XBOX) && defined(STEFX_SP_HOSTED_MP)
	STEFX_HolomatchUpdateSplitLaunchProof(localPlayers, levelTime);
#endif
	STEFX_HolomatchEnsureLocalClients(localPlayers, levelTime);
	STEFX_HolomatchRunLocalUsercmds(localPlayers, levelTime);
	STEFX_HolomatchBotFrame(levelTime);
	STEFX_HolomatchGameFrame(levelTime);
	STEFX_HolomatchPinSmokeP1View(levelTime);
	STEFX_HolomatchUpdateSplitRefdefs(localPlayers, levelTime);
	STEFX_HolomatchLogSplitState(localPlayers, levelTime);
	STEFX_HolomatchStageSmokeCombatProof(levelTime);
	STEFX_HolomatchRunSmokeMapCycle(levelTime);
}

void STEFX_HolomatchHostAfterGameFrame(int levelTime)
{
	if (!s_stefxHolomatchHostActive)
	{
		return;
	}
	(void)levelTime;
}
