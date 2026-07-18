#include "../ef_ai_compat.h"

#undef DAMAGE_RADIUS
#undef DAMAGE_NO_ARMOR
#undef DAMAGE_NO_KNOCKBACK
#undef DAMAGE_NO_PROTECTION

#define DAMAGE_RADIUS 0x00000001
#define DAMAGE_NO_ARMOR 0x00000002
#define DAMAGE_NO_KNOCKBACK 0x00000008
#define DAMAGE_NO_PROTECTION 0x00000020
#define DAMAGE_NOT_ARMOR_PIERCING 0x00000000
#define DAMAGE_ARMOR_PIERCING 0x00000040
#define DAMAGE_NO_INVULNERABILITY 0x00000080
#define DAMAGE_HALF_NOTLOS 0x00000100
#define DAMAGE_ALL_TEAMS 0x00000200

#define EF_AWARD_MASK 0
#define EF_ELIMINATED 0

#define CONTENTS_NONE 0
#define SVF_SHIELD_BBOX 0

#define EV_POWERUP_REGEN EV_POWERUP_BATTLESUIT
#define EV_POWERUP_SEEKER_FIRE EV_POWERUP_BATTLESUIT
#define EV_GRENADE_EXPLODE EV_MISSILE_HIT
#define EV_DETPACK EV_MISSILE_HIT
#define EV_BORG_TELEPORT EV_PLAYER_TELEPORT_IN

#define MOD_DETPACK MOD_DET_PACK_SPLASH

#define LEGS_IDLE BOTH_STAND1
#define TORSO_STAND BOTH_STAND1

#define TP_NORMAL 0
#define TP_BORG 1

#define irandom Q_irand

static vmCvar_t stefx_hm_disabled_active_mod;
#define g_pModAssimilation stefx_hm_disabled_active_mod
#define g_pModElimination stefx_hm_disabled_active_mod

static vmCvar_t stefx_hm_active_damage_multiplier = { 0, 0, 1.0f, 1, "1" };
#define g_dmgmult stefx_hm_active_damage_multiplier

static void STEFX_HM_OfficialSound( gentity_t *entity, int soundIndex )
{
	G_Sound( entity, CHAN_AUTO, soundIndex );
}
#define G_Sound(entity, soundIndex) STEFX_HM_OfficialSound(entity, soundIndex)

static void STEFX_HM_OfficialTeleportPlayer( gentity_t *player, vec3_t origin,
	vec3_t angles, int teleportType )
{
	(void)teleportType;
	TeleportPlayer( player, origin, angles );
}
#define TeleportPlayer STEFX_HM_OfficialTeleportPlayer

qboolean SeekerAcquiresTarget( gentity_t *ent, vec3_t pos );
void FireSeeker( gentity_t *owner, gentity_t *target, vec3_t origin );

void STEFX_HM_OfficialDamage( gentity_t *targ, gentity_t *inflictor,
	gentity_t *attacker, vec3_t dir, vec3_t point, int damage, int dflags, int mod );
qboolean STEFX_HM_OfficialRadiusDamage( vec3_t origin, gentity_t *attacker,
	float damage, float radius, gentity_t *ignore, int dflags, int mod );

static void STEFX_HM_CalculateRanks( qboolean noUpdate )
{
	(void)noUpdate;
	CalculateRanks();
}

#define CalculateRanks(noUpdate) STEFX_HM_CalculateRanks(noUpdate)
#define G_Damage STEFX_HM_OfficialDamage
#define G_RadiusDamage STEFX_HM_OfficialRadiusDamage
#define ClientThink STEFX_HM_OfficialClientThink

#include "g_active.c"

#undef CalculateRanks
#undef G_Damage
#undef G_RadiusDamage
#undef ClientThink
#undef G_Sound
#undef TeleportPlayer

void G_CheckClientTimeouts( gentity_t *ent )
{
	if ( !g_timeouttospec.integer ||
		ent->client->sess.sessionTeam == TEAM_SPECTATOR )
	{
		return;
	}
	if ( level.time - ent->client->pers.cmd.serverTime >
		g_timeouttospec.integer * 1000 )
	{
		SetTeam( ent, "spectator" );
	}
}

void ClientThink( int clientNum, usercmd_t *ucmd )
{
	static qboolean logged = qfalse;
	gentity_t *ent = g_entities + clientNum;

	if ( clientNum < MAX_CLIENTS )
	{
		trap_GetUsercmd( clientNum, &ent->client->pers.cmd );
	}
	if ( ucmd )
	{
		ent->client->pers.cmd = *ucmd;
	}
	ent->client->lastCmdTime = level.time;
	if ( !logged )
	{
		G_Printf( "STEFX_HM: official EF client activity active with Xbox usercmd boundary\n" );
		logged = qtrue;
	}
	if ( !( ent->r.svFlags & SVF_BOT ) && !g_synchronousClients.integer )
	{
		ClientThink_real( ent );
	}
	else if ( clientNum >= MAX_CLIENTS )
	{
		ClientThink_real( ent );
	}
}

enum
{
	STEFX_ACTIVE_BOUNDARY_AMMO_STATION = 1 << 0,
	STEFX_ACTIVE_BOUNDARY_BORG_TRANSPORT = 1 << 3,
	STEFX_ACTIVE_BOUNDARY_CHEAP_WEAPON = 1 << 4,
	STEFX_ACTIVE_BOUNDARY_OBJECT_IMPACT = 1 << 5,
	STEFX_ACTIVE_BOUNDARY_PUSH_TRIGGER = 1 << 6,
	STEFX_ACTIVE_BOUNDARY_BRUSH_IMPACT = 1 << 7
};

static unsigned int stefx_hm_activeBoundaryHooksLogged;

static void STEFX_HM_LogActiveBoundary( unsigned int bit, const char *name )
{
	if ( !( stefx_hm_activeBoundaryHooksLogged & bit ) )
	{
		stefx_hm_activeBoundaryHooksLogged |= bit;
		G_Printf( "STEFX_HM: pending official EF activity boundary invoked name='%s'\n", name );
	}
}

void SP_misc_ammo_station( gentity_t *ent )
{
	STEFX_HM_LogActiveBoundary( STEFX_ACTIVE_BOUNDARY_AMMO_STATION,
		"SP_misc_ammo_station" );
	G_FreeEntity( ent );
}

void ammo_station_finish_spawning( gentity_t *ent )
{
	STEFX_HM_LogActiveBoundary( STEFX_ACTIVE_BOUNDARY_AMMO_STATION,
		"ammo_station_finish_spawning" );
	G_FreeEntity( ent );
}

qboolean BG_BorgTransporting( playerState_t *ps )
{
	(void)ps;
	STEFX_HM_LogActiveBoundary( STEFX_ACTIVE_BOUNDARY_BORG_TRANSPORT,
		"BG_BorgTransporting" );
	return qfalse;
}

void G_CheapWeaponFire( int entityNum, int event )
{
	(void)entityNum;
	(void)event;
	STEFX_HM_LogActiveBoundary( STEFX_ACTIVE_BOUNDARY_CHEAP_WEAPON,
		"G_CheapWeaponFire" );
}

void DoImpact( gentity_t *self, gentity_t *other, qboolean damageSelf )
{
	(void)self;
	(void)other;
	(void)damageSelf;
	STEFX_HM_LogActiveBoundary( STEFX_ACTIVE_BOUNDARY_OBJECT_IMPACT,
		"DoImpact" );
}

void G_MoverTouchPushTriggers( gentity_t *ent, vec3_t oldOrigin )
{
	(void)ent;
	(void)oldOrigin;
	STEFX_HM_LogActiveBoundary( STEFX_ACTIVE_BOUNDARY_PUSH_TRIGGER,
		"G_MoverTouchPushTriggers" );
}

void Client_CheckImpactBBrush( gentity_t *self, gentity_t *other )
{
	(void)self;
	(void)other;
	STEFX_HM_LogActiveBoundary( STEFX_ACTIVE_BOUNDARY_BRUSH_IMPACT,
		"Client_CheckImpactBBrush" );
}
