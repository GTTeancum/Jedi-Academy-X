#include "g_local.h"

gentity_t *droppedRedFlag = NULL;
gentity_t *droppedBlueFlag = NULL;

void B_InitAlloc(void)
{
	G_Printf("STEFX_HM: official EF bot AI allocation state initialized\n");
}

void B_CleanupAlloc(void)
{
	G_Printf("STEFX_HM: official EF bot AI allocation state released\n");
}

void RemoveAllWP(void)
{
}

void LoadPath_ThisLevel(void)
{
	G_Printf("STEFX_HM: JA waypoint loader retired; official EF AAS route active\n");
}

void G_TestLine(vec3_t start, vec3_t end, int color, int time)
{
	gentity_t *te = G_TempEntity(start, EV_TESTLINE);

	VectorCopy(start, te->s.origin);
	VectorCopy(end, te->s.origin2);
	te->s.time2 = time;
	te->s.weapon = color;
	te->r.svFlags |= SVF_BROADCAST;
}

int OrgVisible(vec3_t org1, vec3_t org2, int ignore)
{
	trace_t tr;

	trap_Trace(&tr, org1, NULL, NULL, org2, ignore, MASK_SOLID);
	return tr.fraction == 1.0f;
}

void Bot_SetForcedMovement(int bot, int forward, int right, int up)
{
	(void)bot;
	(void)forward;
	(void)right;
	(void)up;
}

int AcceptBotCommand(char *cmd, gentity_t *player)
{
	(void)cmd;
	(void)player;
	return qfalse;
}

void BotDamageNotification(gclient_t *bot, gentity_t *attacker)
{
	(void)bot;
	(void)attacker;
}
