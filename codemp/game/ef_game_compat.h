#ifndef EF_GAME_COMPAT_H
#define EF_GAME_COMPAT_H

#include "g_local.h"

#define GT_TOURNAMENT GT_DUEL
#define TIME_INTRO 5000

static vmCvar_t stefx_hm_disabled_specialties;
#define g_pModSpecialties stefx_hm_disabled_specialties

static char *EF_Game_COM_Parse( char **data_p )
{
	const char *cursor = *data_p;
	char *token = COM_Parse( &cursor );
	*data_p = (char *)cursor;
	return token;
}

static char *EF_Game_COM_ParseExt( char **data_p, qboolean allowLineBreak )
{
	const char *cursor = *data_p;
	char *token = COM_ParseExt( &cursor, allowLineBreak );
	*data_p = (char *)cursor;
	return token;
}

#define COM_Parse(data_p) EF_Game_COM_Parse(data_p)
#define COM_ParseExt(data_p, allowLineBreak) EF_Game_COM_ParseExt(data_p, allowLineBreak)
#define G_Alloc(size) ((char *)G_Alloc(size))

#define G_CheckMinimumPlayers STEFX_HM_OfficialCheckMinimumPlayers
#define G_CheckBotSpawn STEFX_HM_OfficialCheckBotSpawn

#endif
