#include "../ef_game_compat.h"
#include "g_bot.c"

#undef G_CheckMinimumPlayers
#undef G_CheckBotSpawn

int checkminimumplayers_time = 0;

void G_CheckMinimumPlayers( void )
{
	int minplayers;
	int humanplayers, botplayers;

	if ( level.intermissiontime ) return;
	if ( checkminimumplayers_time > level.time - 10000 )
	{
		return;
	}
	checkminimumplayers_time = level.time;
	trap_Cvar_Update( &bot_minplayers );
	minplayers = bot_minplayers.integer;
	if ( minplayers <= 0 ) return;

	if ( g_gametype.integer >= GT_TEAM )
	{
		if ( minplayers >= g_maxclients.integer / 2 )
		{
			minplayers = ( g_maxclients.integer / 2 ) - 1;
		}

		humanplayers = G_CountHumanPlayers( TEAM_RED );
		botplayers = G_CountBotPlayers( TEAM_RED );
		if ( humanplayers + botplayers < minplayers )
		{
			G_AddRandomBot( TEAM_RED );
		}
		else if ( humanplayers + botplayers > minplayers && botplayers )
		{
			G_RemoveRandomBot( TEAM_RED );
		}

		humanplayers = G_CountHumanPlayers( TEAM_BLUE );
		botplayers = G_CountBotPlayers( TEAM_BLUE );
		if ( humanplayers + botplayers < minplayers )
		{
			G_AddRandomBot( TEAM_BLUE );
		}
		else if ( humanplayers + botplayers > minplayers && botplayers )
		{
			G_RemoveRandomBot( TEAM_BLUE );
		}
	}
	else if ( g_gametype.integer == GT_TOURNAMENT )
	{
		if ( minplayers >= g_maxclients.integer )
		{
			minplayers = g_maxclients.integer - 1;
		}
		humanplayers = G_CountHumanPlayers( -1 );
		botplayers = G_CountBotPlayers( -1 );
		if ( humanplayers + botplayers < minplayers )
		{
			G_AddRandomBot( TEAM_FREE );
		}
		else if ( humanplayers + botplayers > minplayers && botplayers )
		{
			if ( !G_RemoveRandomBot( TEAM_SPECTATOR ) )
			{
				G_RemoveRandomBot( -1 );
			}
		}
	}
	else if ( g_gametype.integer == GT_FFA )
	{
		if ( minplayers >= g_maxclients.integer )
		{
			minplayers = g_maxclients.integer - 1;
		}
		humanplayers = G_CountHumanPlayers( TEAM_FREE );
		botplayers = G_CountBotPlayers( TEAM_FREE );
		if ( humanplayers + botplayers < minplayers )
		{
			G_AddRandomBot( TEAM_FREE );
		}
		else if ( humanplayers + botplayers > minplayers && botplayers )
		{
			G_RemoveRandomBot( TEAM_FREE );
		}
	}
}

void G_CheckBotSpawn( void )
{
	int n;
	char userinfo[MAX_INFO_VALUE];

	G_CheckMinimumPlayers();

	for ( n = 0; n < BOT_SPAWN_QUEUE_DEPTH; n++ )
	{
		if ( !botSpawnQueue[n].spawnTime || botSpawnQueue[n].spawnTime > level.time )
		{
			continue;
		}
		ClientBegin( botSpawnQueue[n].clientNum, qfalse );
		botSpawnQueue[n].spawnTime = 0;

		if ( g_gametype.integer == GT_SINGLE_PLAYER )
		{
			trap_GetUserinfo( botSpawnQueue[n].clientNum, userinfo, sizeof( userinfo ) );
			PlayerIntroSound( Info_ValueForKey( userinfo, "model" ) );
		}
	}
}

static int STEFX_HM_GetMapTypeBits( char *type )
{
	int typeBits = 0;

	if ( *type )
	{
		if ( strstr( type, "ffa" ) )
		{
			typeBits |= ( 1 << GT_FFA );
			typeBits |= ( 1 << GT_TEAM );
		}
		if ( strstr( type, "holocron" ) )
		{
			typeBits |= ( 1 << GT_HOLOCRON );
		}
		if ( strstr( type, "jedimaster" ) )
		{
			typeBits |= ( 1 << GT_JEDIMASTER );
		}
		if ( strstr( type, "duel" ) )
		{
			typeBits |= ( 1 << GT_DUEL );
			typeBits |= ( 1 << GT_POWERDUEL );
		}
		if ( strstr( type, "powerduel" ) )
		{
			typeBits |= ( 1 << GT_DUEL );
			typeBits |= ( 1 << GT_POWERDUEL );
		}
		if ( strstr( type, "ctf" ) )
		{
			typeBits |= ( 1 << GT_CTF );
		}
		if ( strstr( type, "cty" ) )
		{
			typeBits |= ( 1 << GT_CTY );
		}
	}
	else
	{
		typeBits |= ( 1 << GT_FFA );
	}

	return typeBits;
}

qboolean G_DoesMapSupportGametype( const char *mapname, int gametype )
{
	int n;

	if ( !mapname || !mapname[0] )
	{
		return qfalse;
	}

	for ( n = 0; n < g_numArenas; n++ )
	{
		char *arenaMap = Info_ValueForKey( g_arenaInfos[n], "map" );

		if ( !Q_stricmp( mapname, arenaMap ) )
		{
			char *type = Info_ValueForKey( g_arenaInfos[n], "type" );
			return ( STEFX_HM_GetMapTypeBits( type ) & ( 1 << gametype ) ) ? qtrue : qfalse;
		}
	}

	return qfalse;
}

const char *G_RefreshNextMap( int gametype, qboolean forced )
{
	vmCvar_t mapname;
	int current = 0;
	int offset;

	if ( ( !g_autoMapCycle.integer && !forced ) || g_numArenas <= 0 )
	{
		return NULL;
	}

	trap_Cvar_Register( &mapname, "mapname", "", CVAR_SERVERINFO | CVAR_ROM );
	for ( offset = 0; offset < g_numArenas; offset++ )
	{
		if ( !Q_stricmp( mapname.string, Info_ValueForKey( g_arenaInfos[offset], "map" ) ) )
		{
			current = offset;
			break;
		}
	}

	for ( offset = 1; offset < g_numArenas; offset++ )
	{
		int candidate = ( current + offset ) % g_numArenas;
		char *type = Info_ValueForKey( g_arenaInfos[candidate], "type" );

		if ( STEFX_HM_GetMapTypeBits( type ) & ( 1 << gametype ) )
		{
			const char *next = Info_ValueForKey( g_arenaInfos[candidate], "map" );
			trap_Cvar_Set( "nextmap", va( "map %s", next ) );
			return next;
		}
	}

	trap_Cvar_Set( "nextmap", "map_restart 0" );
	return Info_ValueForKey( g_arenaInfos[current], "map" );
}

void G_RemoveQueuedBotBegin( int clientNum )
{
	int n;

	for ( n = 0; n < BOT_SPAWN_QUEUE_DEPTH; n++ )
	{
		if ( botSpawnQueue[n].clientNum == clientNum )
		{
			botSpawnQueue[n].spawnTime = 0;
			return;
		}
	}
}

void G_InitBotMetadataOnly( qboolean restart )
{
	G_Printf( "STEFX_HM: official EF bot metadata-only init begin restart=%d\n", restart );
	G_LoadBots();
	G_LoadArenas();
	trap_Cvar_Register( &bot_minplayers, "bot_minplayers", "0", CVAR_SERVERINFO );
	G_Printf( "STEFX_HM: official EF bot metadata-only init done bots=%d arenas=%d bot_minplayers=%d\n",
		g_numBots,
		g_numArenas,
		bot_minplayers.integer );
}
