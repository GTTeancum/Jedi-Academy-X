#include "stefx_holomatch_game.h"

#include "q_shared.h"
#include "../win32/xb_log.h"

static qboolean s_stefxHolomatchGameActive = qfalse;
static int s_stefxHolomatchFrameLogBudget = 8;

int STEFX_IsHolomatchMap(const char *mapname)
{
	if (!mapname)
	{
		return qfalse;
	}

	return (!Q_stricmpn(mapname, "hm_", 3) ||
		!Q_stricmpn(mapname, "ctf_", 4) ||
		!Q_stricmpn(mapname, "dm_", 3) ||
		!Q_stricmpn(mapname, "team_", 5)) ? qtrue : qfalse;
}

void STEFX_HolomatchGameInit(const char *mapname)
{
	s_stefxHolomatchGameActive = (qboolean)STEFX_IsHolomatchMap(mapname);
	s_stefxHolomatchFrameLogBudget = 8;
	XBLog_Writef("STEFX_HM_SP: game boundary init map='%s' active=%d", mapname ? mapname : "", s_stefxHolomatchGameActive);
}

void STEFX_HolomatchGameFrame(int levelTime)
{
	if (!s_stefxHolomatchGameActive || s_stefxHolomatchFrameLogBudget <= 0)
	{
		return;
	}

	XBLog_Writef("STEFX_HM_SP: game boundary frame time=%d", levelTime);
	--s_stefxHolomatchFrameLogBudget;
}
