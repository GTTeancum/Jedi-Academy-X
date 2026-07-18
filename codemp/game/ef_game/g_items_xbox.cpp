#include "../ef_ai_compat.h"

#define EF_BOUNCE_HALF 0x00000020

#define PERS_REWARD_COUNT PERS_DEFEND_COUNT
#define PERS_REWARD PERS_ASSIST_COUNT
#define REWARD_DENIED 3

#define HI_MEDKIT HI_MEDPAC
#define DROPPED_FLAG_SOUND RETURN_FLAG_SOUND

static vmCvar_t stefx_hm_disabled_item_mod;
#define g_pModActionHero stefx_hm_disabled_item_mod
#define g_pModAssimilation stefx_hm_disabled_item_mod

#define G_SpawnItem STEFX_HM_OfficialSpawnItem

#include "g_items.c"

#undef G_SpawnItem

void G_SpawnItem( gentity_t *ent, gitem_t *item )
{
	static qboolean logged = qfalse;
	if ( !logged )
	{
		G_Printf( "STEFX_HM: official EF item lifecycle active\n" );
		logged = qtrue;
	}
	STEFX_HM_OfficialSpawnItem( ent, item );
}

static void STEFX_HM_LogRetiredCarrierItemHook( const char *name )
{
	static unsigned int loggedHooks = 0;
	unsigned int hook = 0;

	if ( !Q_stricmp( name, "Jetpack_Off" ) )
	{
		hook = 1u << 0;
	}
	else if ( !Q_stricmp( name, "ItemUse_Jetpack" ) )
	{
		hook = 1u << 1;
	}
	else if ( !Q_stricmp( name, "ItemUse_UseDisp" ) )
	{
		hook = 1u << 2;
	}
	else if ( !Q_stricmp( name, "CheckItemCanBePickedUpByNPC" ) )
	{
		hook = 1u << 3;
	}

	if ( hook && !(loggedHooks & hook) )
	{
		G_Printf( "STEFX_HM: retired JA carrier item hook invoked name=%s\n", name );
		loggedHooks |= hook;
	}
}

void Jetpack_Off( gentity_t *ent )
{
	STEFX_HM_LogRetiredCarrierItemHook( "Jetpack_Off" );
	if ( ent && ent->client )
	{
		ent->client->jetPackOn = qfalse;
	}
}

void ItemUse_Jetpack( gentity_t *ent )
{
	STEFX_HM_LogRetiredCarrierItemHook( "ItemUse_Jetpack" );
	Jetpack_Off( ent );
}

void ItemUse_UseDisp( gentity_t *ent, int type )
{
	(void)ent;
	(void)type;
	STEFX_HM_LogRetiredCarrierItemHook( "ItemUse_UseDisp" );
}

qboolean CheckItemCanBePickedUpByNPC( gentity_t *item, gentity_t *pickerupper )
{
	(void)item;
	(void)pickerupper;
	STEFX_HM_LogRetiredCarrierItemHook( "CheckItemCanBePickedUpByNPC" );
	return qfalse;
}
