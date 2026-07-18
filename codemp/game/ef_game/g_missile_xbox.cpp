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

#undef EF_MISSILE_STICK
#define EF_BOUNCE 0x00000010
#define EF_BOUNCE_HALF 0x00000020
#define EF_MISSILE_STICK 0x00000040

#define PERS_ACCURACY_HITS 15

#define EV_GRENADE_EXPLODE EV_MISSILE_HIT
#define EV_GRENADE_SHRAPNEL_EXPLODE EV_MISSILE_HIT
#define EV_GRENADE_SHRAPNEL EV_MISSILE_MISS

static void VectorShort( vec3_t vector )
{
	VectorNormalize( vector );
	VectorScale( vector, 8191.0f, vector );
	SnapVector( vector );
}

void STEFX_HM_OfficialDamage( gentity_t *targ, gentity_t *inflictor,
	gentity_t *attacker, vec3_t dir, vec3_t point, int damage, int dflags, int mod );
qboolean STEFX_HM_OfficialRadiusDamage( vec3_t origin, gentity_t *attacker,
	float damage, float radius, gentity_t *ignore, int dflags, int mod );

#define G_Damage STEFX_HM_OfficialDamage
#define G_RadiusDamage STEFX_HM_OfficialRadiusDamage
#define G_RunMissile STEFX_HM_OfficialRunMissile

#include "g_missile.c"

#undef G_Damage
#undef G_RadiusDamage
#undef G_RunMissile

void G_RunMissile( gentity_t *ent )
{
	static qboolean logged = qfalse;
	if ( !logged )
	{
		G_Printf( "STEFX_HM: official EF missile simulation active\n" );
		logged = qtrue;
	}
	STEFX_HM_OfficialRunMissile( ent );
}

enum
{
	STEFX_RETIRED_CREATE_MISSILE = 1 << 0,
	STEFX_RETIRED_REFLECT_MISSILE = 1 << 1
};

static unsigned int stefx_hm_retiredMissileHooksLogged;

static void STEFX_HM_LogRetiredMissileHook( unsigned int bit, const char *name )
{
	if ( !( stefx_hm_retiredMissileHooksLogged & bit ) )
	{
		stefx_hm_retiredMissileHooksLogged |= bit;
		G_Printf( "STEFX_HM: retired JA carrier missile hook invoked name='%s'\n", name );
	}
}

gentity_t *CreateMissile( vec3_t origin, vec3_t direction, float velocity,
	int life, gentity_t *owner, qboolean altFire )
{
	gentity_t *missile;

	STEFX_HM_LogRetiredMissileHook( STEFX_RETIRED_CREATE_MISSILE,
		"CreateMissile" );
	missile = G_Spawn();
	missile->nextthink = level.time + life;
	missile->think = G_FreeEntity;
	missile->s.eType = ET_MISSILE;
	missile->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	missile->parent = owner;
	missile->r.ownerNum = owner->s.number;
	if ( altFire )
	{
		missile->s.eFlags |= EF_ALT_FIRING;
	}
	missile->s.pos.trType = TR_LINEAR;
	missile->s.pos.trTime = level.time;
	missile->target_ent = NULL;
	SnapVector( origin );
	VectorCopy( origin, missile->s.pos.trBase );
	VectorScale( direction, velocity, missile->s.pos.trDelta );
	VectorCopy( origin, missile->r.currentOrigin );
	SnapVector( missile->s.pos.trDelta );
	return missile;
}

void G_ReflectMissile( gentity_t *ent, gentity_t *missile, vec3_t forward )
{
	(void)ent;
	(void)missile;
	(void)forward;
	STEFX_HM_LogRetiredMissileHook( STEFX_RETIRED_REFLECT_MISSILE,
		"G_ReflectMissile" );
}
