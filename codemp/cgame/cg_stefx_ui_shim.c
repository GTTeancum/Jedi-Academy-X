#include "cg_local.h"

#if defined(STEFX_ELITE_FORCE_MP)

#include "../ui/ui_shared.h"
#include "../namespace_begin.h"

displayContextDef_t *DC = NULL;

static qboolean stefxCgUiShimLogged = qfalse;

static void STEFX_CGUiShimLog(const char *operation, const char *name)
{
	if (!stefxCgUiShimLogged)
	{
		stefxCgUiShimLogged = qtrue;
		CG_PrintfAlways("STEFX_HM: cgame UI parser shim is reject-only; shared SP UI owns menus op='%s' name='%s'\n",
			operation ? operation : "",
			name ? name : "");
	}
}

const char *String_Alloc(const char *p)
{
	return p ? p : "";
}

void String_Init(void)
{
	STEFX_CGUiShimLog("string-init", "");
}

void String_Report(void)
{
}

void Init_Display(displayContextDef_t *dc)
{
	DC = dc;
}

displayContextDef_t *Display_GetContext(void)
{
	return DC;
}

void Display_ExpandMacros(char *buff)
{
	(void)buff;
}

void *Display_CaptureItem(int x, int y)
{
	(void)x;
	(void)y;
	return NULL;
}

qboolean Display_MouseMove(void *p, int x, int y)
{
	(void)p;
	(void)x;
	(void)y;
	return qfalse;
}

int Display_CursorType(int x, int y)
{
	(void)x;
	(void)y;
	return CURSOR_NONE;
}

void Display_HandleKey(int key, qboolean down, int x, int y)
{
	(void)key;
	(void)down;
	(void)x;
	(void)y;
}

void Display_CacheAll(void)
{
}

void Menu_Init(menuDef_t *menu)
{
	if (menu)
	{
		memset(menu, 0, sizeof(*menu));
	}
}

void Item_Init(itemDef_t *item)
{
	if (item)
	{
		memset(item, 0, sizeof(*item));
	}
}

void Menu_PostParse(menuDef_t *menu)
{
	(void)menu;
}

void Menu_New(int handle)
{
	(void)handle;
	STEFX_CGUiShimLog("new", "");
}

int Menu_Count(void)
{
	return 0;
}

void Menu_Reset(void)
{
}

void Menu_PaintAll(void)
{
}

void Menu_Paint(menuDef_t *menu, qboolean forcePaint)
{
	(void)menu;
	(void)forcePaint;
}

menuDef_t *Menu_GetFocused(void)
{
	return NULL;
}

itemDef_t *Menu_GetFocusedItem(menuDef_t *menu)
{
	(void)menu;
	return NULL;
}

itemDef_t *Menu_SetPrevCursorItem(menuDef_t *menu)
{
	(void)menu;
	return NULL;
}

itemDef_t *Menu_FindItemByName(menuDef_t *menu, const char *p)
{
	(void)menu;
	(void)p;
	return NULL;
}

void Menu_HandleKey(menuDef_t *menu, int key, qboolean down)
{
	(void)menu;
	(void)key;
	(void)down;
}

void Menu_HandleMouseMove(menuDef_t *menu, float x, float y)
{
	(void)menu;
	(void)x;
	(void)y;
}

void Menu_ScrollFeeder(menuDef_t *menu, int feeder, qboolean down)
{
	(void)menu;
	(void)feeder;
	(void)down;
}

void Menu_SetFeederSelection(menuDef_t *menu, int feeder, int index, const char *name)
{
	(void)menu;
	(void)feeder;
	(void)index;
	(void)name;
}

void Menu_SetItemBackground(const menuDef_t *menu, const char *itemName, const char *background)
{
	(void)menu;
	(void)itemName;
	(void)background;
}

menuDef_t *Menus_ActivateByName(const char *p)
{
	STEFX_CGUiShimLog("activate", p);
	return NULL;
}

void Menus_Activate(menuDef_t *menu)
{
	(void)menu;
}

void Menus_OpenByName(const char *p)
{
	STEFX_CGUiShimLog("open", p);
}

menuDef_t *Menus_FindByName(const char *p)
{
	(void)p;
	return NULL;
}

void Menus_ShowByName(const char *p)
{
	STEFX_CGUiShimLog("show", p);
}

void Menus_CloseByName(const char *p)
{
	(void)p;
}

void Menus_CloseAll(void)
{
}

qboolean Menus_AnyFullScreenVisible(void)
{
	return qfalse;
}

void Item_RunScript(itemDef_t *item, const char *s)
{
	(void)item;
	STEFX_CGUiShimLog("script", s);
}

qboolean Float_Parse(char **p, float *f)
{
	(void)p;
	if (f)
	{
		*f = 0.0f;
	}
	return qfalse;
}

qboolean Color_Parse(char **p, vec4_t *c)
{
	(void)p;
	if (c)
	{
		(*c)[0] = 0.0f;
		(*c)[1] = 0.0f;
		(*c)[2] = 0.0f;
		(*c)[3] = 0.0f;
	}
	return qfalse;
}

qboolean Int_Parse(char **p, int *i)
{
	(void)p;
	if (i)
	{
		*i = 0;
	}
	return qfalse;
}

qboolean Rect_Parse(char **p, rectDef_t *r)
{
	(void)p;
	if (r)
	{
		memset(r, 0, sizeof(*r));
	}
	return qfalse;
}

qboolean String_Parse(char **p, const char **out)
{
	(void)p;
	if (out)
	{
		*out = "";
	}
	return qfalse;
}

qboolean Script_Parse(char **p, const char **out)
{
	return String_Parse(p, out);
}

qboolean PC_Float_Parse(int handle, float *f)
{
	(void)handle;
	if (f)
	{
		*f = 0.0f;
	}
	return qfalse;
}

qboolean PC_Color_Parse(int handle, vec4_t *c)
{
	(void)handle;
	if (c)
	{
		(*c)[0] = 0.0f;
		(*c)[1] = 0.0f;
		(*c)[2] = 0.0f;
		(*c)[3] = 0.0f;
	}
	return qfalse;
}

qboolean PC_Int_Parse(int handle, int *i)
{
	(void)handle;
	if (i)
	{
		*i = 0;
	}
	return qfalse;
}

qboolean PC_Rect_Parse(int handle, rectDef_t *r)
{
	(void)handle;
	if (r)
	{
		memset(r, 0, sizeof(*r));
	}
	return qfalse;
}

qboolean PC_String_Parse(int handle, const char **out)
{
	(void)handle;
	if (out)
	{
		*out = "";
	}
	return qfalse;
}

qboolean PC_Script_Parse(int handle, const char **out)
{
	return PC_String_Parse(handle, out);
}

void LerpColor(vec4_t a, vec4_t b, vec4_t c, float t)
{
	c[0] = a[0] + t * (b[0] - a[0]);
	c[1] = a[1] + t * (b[1] - a[1]);
	c[2] = a[2] + t * (b[2] - a[2]);
	c[3] = a[3] + t * (b[3] - a[3]);
}

void AddDeferedCommand(char *command)
{
	(void)command;
}

#include "../namespace_end.h"

int CG_GetSelectedPlayer(void)
{
	if (cg_currentSelectedPlayer.integer < 0 || cg_currentSelectedPlayer.integer >= numSortedTeamPlayers)
	{
		cg_currentSelectedPlayer.integer = 0;
	}
	return cg_currentSelectedPlayer.integer;
}

qhandle_t CG_StatusHandle(int task)
{
	switch (task)
	{
	case TEAMTASK_DEFENSE:
		return cgs.media.defendShader;
	case TEAMTASK_PATROL:
		return cgs.media.patrolShader;
	case TEAMTASK_FOLLOW:
		return cgs.media.followShader;
	case TEAMTASK_CAMP:
		return cgs.media.campShader;
	case TEAMTASK_RETRIEVE:
		return cgs.media.retrieveShader;
	case TEAMTASK_ESCORT:
		return cgs.media.escortShader;
	case TEAMTASK_OFFENSE:
	default:
		return cgs.media.assaultShader;
	}
}

float CG_GetValue(int ownerDraw)
{
	centity_t *cent;
	playerState_t *ps;
	clientInfo_t *ci;

	if (!cg || !cg->snap)
	{
		return -1.0f;
	}

	cent = &cg_entities[cg->snap->ps.clientNum];
	ps = &cg->snap->ps;
	switch (ownerDraw)
	{
	case CG_SELECTEDPLAYER_ARMOR:
		ci = cgs.clientinfo + sortedTeamPlayers[CG_GetSelectedPlayer()];
		return ci->armor;
	case CG_SELECTEDPLAYER_HEALTH:
		ci = cgs.clientinfo + sortedTeamPlayers[CG_GetSelectedPlayer()];
		return ci->health;
	case CG_PLAYER_ARMOR_VALUE:
		return ps->stats[STAT_ARMOR];
	case CG_PLAYER_AMMO_VALUE:
		if (cent->currentState.weapon)
		{
			return ps->ammo[weaponData[cent->currentState.weapon].ammoIndex];
		}
		return 0.0f;
	case CG_PLAYER_SCORE:
		return ps->persistant[PERS_SCORE];
	case CG_PLAYER_HEALTH:
		return ps->stats[STAT_HEALTH];
	case CG_RED_SCORE:
		return cgs.scores1;
	case CG_BLUE_SCORE:
		return cgs.scores2;
	case CG_PLAYER_FORCE_VALUE:
		return ps->fd.forcePower;
	default:
		return -1.0f;
	}
}

qboolean CG_OtherTeamHasFlag(void)
{
	int team;

	if (!cg || !cg->snap || (cgs.gametype != GT_CTF && cgs.gametype != GT_CTY))
	{
		return qfalse;
	}

	team = cg->snap->ps.persistant[PERS_TEAM];
	if (team == TEAM_RED && cgs.redflag == FLAG_TAKEN)
	{
		return qtrue;
	}
	if (team == TEAM_BLUE && cgs.blueflag == FLAG_TAKEN)
	{
		return qtrue;
	}
	return qfalse;
}

qboolean CG_YourTeamHasFlag(void)
{
	int team;

	if (!cg || !cg->snap || (cgs.gametype != GT_CTF && cgs.gametype != GT_CTY))
	{
		return qfalse;
	}

	team = cg->snap->ps.persistant[PERS_TEAM];
	if (team == TEAM_RED && cgs.blueflag == FLAG_TAKEN)
	{
		return qtrue;
	}
	if (team == TEAM_BLUE && cgs.redflag == FLAG_TAKEN)
	{
		return qtrue;
	}
	return qfalse;
}

qboolean CG_OwnerDrawVisible(int flags)
{
	if (!cg || !cg->snap)
	{
		return qfalse;
	}

	if (flags & CG_SHOW_ANYTEAMGAME)
	{
		return (qboolean)(cgs.gametype >= GT_TEAM);
	}
	if (flags & CG_SHOW_ANYNONTEAMGAME)
	{
		return (qboolean)(cgs.gametype < GT_TEAM);
	}
	if (flags & CG_SHOW_CTF)
	{
		return (qboolean)(cgs.gametype == GT_CTF || cgs.gametype == GT_CTY);
	}
	if (flags & CG_SHOW_HEALTHCRITICAL)
	{
		return (qboolean)(cg->snap->ps.stats[STAT_HEALTH] < 25);
	}
	if (flags & CG_SHOW_HEALTHOK)
	{
		return (qboolean)(cg->snap->ps.stats[STAT_HEALTH] >= 25);
	}
	if (flags & CG_SHOW_IF_PLAYER_HAS_FLAG)
	{
		return (qboolean)(cg->snap->ps.powerups[PW_REDFLAG] || cg->snap->ps.powerups[PW_BLUEFLAG] || cg->snap->ps.powerups[PW_NEUTRALFLAG]);
	}
	return qfalse;
}

const char *CG_GetKillerText(void)
{
	static char s[128];

	if (cg && cg->killerName[0])
	{
		Com_sprintf(s, sizeof(s), "Killed by %s", cg->killerName);
		return s;
	}
	return "";
}

const char *CG_GetGameStatusText(void)
{
	return "";
}

const char *CG_GameTypeString(void)
{
	switch (cgs.gametype)
	{
	case GT_FFA:
		return "HOLOMATCH";
	case GT_TEAM:
		return "TEAM HOLOMATCH";
	case GT_CTF:
		return "CAPTURE THE FLAG";
	default:
		return "HOLOMATCH";
	}
}

void CG_OwnerDraw(float x, float y, float w, float h, float text_x, float text_y, int ownerDraw, int ownerDrawFlags, int align, float special, float scale, vec4_t color, qhandle_t shader, int textStyle, int font)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)text_x;
	(void)text_y;
	(void)ownerDraw;
	(void)ownerDrawFlags;
	(void)align;
	(void)special;
	(void)scale;
	(void)color;
	(void)shader;
	(void)textStyle;
	(void)font;
}

void CG_MouseEvent(int x, int y)
{
	(void)x;
	(void)y;
	cgs.capturedItem = NULL;
}

void CG_EventHandling(int type)
{
	cgs.eventHandling = type;
	if (type == CGAME_EVENT_NONE)
	{
		cgs.capturedItem = NULL;
	}
}

void CG_KeyEvent(int key, qboolean down)
{
	(void)key;
	if (!down)
	{
		return;
	}
	cgs.capturedItem = NULL;
}

int CG_ClientNumFromName(const char *p)
{
	int i;

	if (!p || !p[0])
	{
		return -1;
	}

	for (i = 0; i < cgs.maxclients; i++)
	{
		if (cgs.clientinfo[i].infoValid && !Q_stricmp(cgs.clientinfo[i].name, p))
		{
			return i;
		}
	}
	return -1;
}

void CG_RunMenuScript(char **args)
{
	(void)args;
}

qboolean CG_DeferMenuScript(char **args)
{
	(void)args;
	return qfalse;
}

void CG_GetTeamColor(vec4_t *color)
{
	int team;

	if (!color)
	{
		return;
	}

	team = (cg && cg->snap) ? cg->snap->ps.persistant[PERS_TEAM] : TEAM_FREE;
	if (team == TEAM_RED)
	{
		(*color)[0] = 1.0f;
		(*color)[1] = 0.0f;
		(*color)[2] = 0.0f;
		(*color)[3] = 0.25f;
	}
	else if (team == TEAM_BLUE)
	{
		(*color)[0] = 0.0f;
		(*color)[1] = 0.0f;
		(*color)[2] = 1.0f;
		(*color)[3] = 0.25f;
	}
	else
	{
		(*color)[0] = 0.0f;
		(*color)[1] = 0.17f;
		(*color)[2] = 0.0f;
		(*color)[3] = 0.25f;
	}
}

#else

int stefx_cgame_ui_shim_anchor;

#endif
