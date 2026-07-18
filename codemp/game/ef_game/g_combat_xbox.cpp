#include "../ef_ai_compat.h"

enum
{
	STEFX_CARRIER_DAMAGE_RADIUS = DAMAGE_RADIUS,
	STEFX_CARRIER_DAMAGE_NO_ARMOR = DAMAGE_NO_ARMOR,
	STEFX_CARRIER_DAMAGE_NO_KNOCKBACK = DAMAGE_NO_KNOCKBACK,
	STEFX_CARRIER_DAMAGE_NO_PROTECTION = DAMAGE_NO_PROTECTION,
	STEFX_CARRIER_DAMAGE_IGNORE_TEAM = DAMAGE_IGNORE_TEAM
};

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

#define PERS_SHIELDS PERS_ATTACKEE_ARMOR
#define PERS_REWARD_COUNT PERS_DEFEND_COUNT
#define PERS_REWARD PERS_ASSIST_COUNT
#define PERS_STREAK_COUNT PERS_GAUNTLET_FRAG_COUNT

#define REWARD_BAD 0
#define REWARD_IMPRESSIVE 1
#define REWARD_EXCELLENT 2
#define REWARD_DENIED 3
#define REWARD_FIRST_STRIKE 4
#define REWARD_STREAK 5

#define STREAK_ACE 5
#define STREAK_EXPERT 10
#define STREAK_MASTER 15
#define STREAK_CHAMPION 20

#define EF_AWARD_EXCELLENT 0
#define EF_AWARD_IMPRESSIVE 0
#define EF_AWARD_FIRSTSTRIKE 0
#define EF_AWARD_ACE 0
#define EF_AWARD_EXPERT 0
#define EF_AWARD_MASTER 0
#define EF_AWARD_CHAMPION 0
#define EF_AWARD_STREAK_MASK 0
#define EF_AWARD_MASK 0
#define EF_ELIMINATED 0
#define REWARD_STREAK_SPRITE_TIME 3000

#define PW_OUCH PW_SHIELDHIT
#define PW_DISINTEGRATE PW_DISINT_4
#define PW_EXPLODE PW_DISINT_4
#define PW_ARCWELD_DISINT PW_DISINT_4
#define PW_BORG_ADAPT PW_BATTLESUIT

#define EV_EXPLODESHELL EV_GIB_PLAYER
#define EV_DISINTEGRATION EV_GIB_PLAYER
#define EV_DISINTEGRATION2 EV_GIB_PLAYER
#define EV_ARCWELD_DISINT EV_GIB_PLAYER

#define MOD_DETPACK MOD_DET_PACK_SPLASH

#define ANIM_TOGGLEBIT 0
#define PIERCED_ARMOR_PROTECTION 0.50f
#define irandom Q_irand

static vmCvar_t stefx_hm_disabled_elimination;
#define g_pModElimination stefx_hm_disabled_elimination
#define g_pModAssimilation stefx_hm_disabled_elimination
#define g_pModActionHero stefx_hm_disabled_elimination

static vmCvar_t stefx_hm_damage_multiplier = { 0, 0, 1.0f, 1, "1" };
#define g_dmgmult stefx_hm_damage_multiplier

static void STEFX_HM_CalculateRanks( qboolean noUpdate )
{
	(void)noUpdate;
	CalculateRanks();
}
#define CalculateRanks(noUpdate) STEFX_HM_CalculateRanks(noUpdate)

#define AddScore STEFX_HM_OfficialAddScore
#define G_Damage STEFX_HM_OfficialDamage
#define G_RadiusDamage STEFX_HM_OfficialRadiusDamage

#include "g_combat.c"

#undef AddScore
#undef G_Damage
#undef G_RadiusDamage
#undef CalculateRanks

int actionHeroClientNum = -1;
int borgQueenClientNum = -1;
int numKilled = 0;

void G_RandomActionHero( int ignoreClientNum )
{
	(void)ignoreClientNum;
}

void SetClass( gentity_t *ent, char *className, char *teamName )
{
	(void)ent;
	(void)className;
	(void)teamName;
}

static int STEFX_HM_TranslateDamageFlags( int flags )
{
	int translated = 0;

	if ( flags & STEFX_CARRIER_DAMAGE_RADIUS ) translated |= DAMAGE_RADIUS;
	if ( flags & STEFX_CARRIER_DAMAGE_NO_ARMOR ) translated |= DAMAGE_NO_ARMOR;
	if ( flags & STEFX_CARRIER_DAMAGE_NO_KNOCKBACK ) translated |= DAMAGE_NO_KNOCKBACK;
	if ( flags & STEFX_CARRIER_DAMAGE_NO_PROTECTION ) translated |= DAMAGE_NO_PROTECTION;
	if ( flags & STEFX_CARRIER_DAMAGE_IGNORE_TEAM ) translated |= DAMAGE_ALL_TEAMS;
	return translated;
}

void AddScore( gentity_t *ent, vec3_t origin, int score )
{
	(void)origin;
	STEFX_HM_OfficialAddScore( ent, score );
}

void G_Damage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker,
	vec3_t dir, vec3_t point, int damage, int dflags, int mod )
{
	STEFX_HM_OfficialDamage( targ, inflictor, attacker, dir, point, damage,
		STEFX_HM_TranslateDamageFlags( dflags ), mod );
}

qboolean G_RadiusDamage( vec3_t origin, gentity_t *attacker, float damage,
	float radius, gentity_t *ignore, gentity_t *missile, int mod )
{
	(void)missile;
	return STEFX_HM_OfficialRadiusDamage( origin, attacker, damage, radius, ignore, 0, mod );
}

enum
{
	STEFX_RETIRED_DISMEMBER_BY_NUM = 1 << 0,
	STEFX_RETIRED_ALERT_TEAM = 1 << 1,
	STEFX_RETIRED_CHECK_DISMEMBER = 1 << 2,
	STEFX_RETIRED_KNOCKDOWN = 1 << 3,
	STEFX_RETIRED_DISMEMBER = 1 << 4,
	STEFX_RETIRED_TOSS_WEAPON = 1 << 5,
	STEFX_RETIRED_HEAVY_MELEE = 1 << 6,
	STEFX_RETIRED_HIT_LOCATION = 1 << 7,
	STEFX_RETIRED_TRIPWIRE = 1 << 8
};

static unsigned int stefx_hm_retiredCarrierHooksLogged;

static void STEFX_HM_LogRetiredCarrierHook( unsigned int bit, const char *name )
{
	if ( !( stefx_hm_retiredCarrierHooksLogged & bit ) )
	{
		stefx_hm_retiredCarrierHooksLogged |= bit;
		G_Printf( "STEFX_HM: retired JA carrier combat hook invoked name='%s'\n", name );
	}
}

void ObjectDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker,
	int damage, int meansOfDeath )
{
	(void)inflictor;
	(void)damage;
	(void)meansOfDeath;
	if ( self->target )
	{
		G_UseTargets( self, attacker );
	}
	G_FreeEntity( self );
}

void G_ApplyKnockback( gentity_t *targ, vec3_t newDir, float knockback )
{
	vec3_t kvel;
	float mass;

	if ( targ->physicsBounce > 0 )
		mass = targ->physicsBounce;
	else
		mass = 200;

	if ( g_gravity.value > 0 )
	{
		VectorScale( newDir, g_knockback.value * knockback / mass * 0.8f, kvel );
		kvel[2] = newDir[2] * g_knockback.value * knockback / mass * 1.5f;
	}
	else
	{
		VectorScale( newDir, g_knockback.value * knockback / mass, kvel );
	}

	if ( targ->client )
	{
		VectorAdd( targ->client->ps.velocity, kvel, targ->client->ps.velocity );
	}
	else if ( targ->s.pos.trType != TR_STATIONARY &&
		targ->s.pos.trType != TR_LINEAR_STOP &&
		targ->s.pos.trType != TR_NONLINEAR_STOP )
	{
		VectorAdd( targ->s.pos.trDelta, kvel, targ->s.pos.trDelta );
		VectorCopy( targ->r.currentOrigin, targ->s.pos.trBase );
		targ->s.pos.trTime = level.time;
	}

	if ( targ->client && !targ->client->ps.pm_time )
	{
		int time = (int)( knockback * 2.0f );
		if ( time < 50 ) time = 50;
		if ( time > 200 ) time = 200;
		targ->client->ps.pm_time = time;
		targ->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	}
}

int gGAvoidDismember = 1;
int gPainMOD = 0;
int gPainHitLoc = -1;
vec3_t gPainPoint = { 0.0f, 0.0f, 0.0f };

void DismembermentByNum( gentity_t *self, int num )
{
	(void)self;
	(void)num;
	STEFX_HM_LogRetiredCarrierHook( STEFX_RETIRED_DISMEMBER_BY_NUM, "DismembermentByNum" );
}

void G_AlertTeam( gentity_t *victim, gentity_t *attacker, float radius, float soundDist )
{
	(void)victim;
	(void)attacker;
	(void)radius;
	(void)soundDist;
	STEFX_HM_LogRetiredCarrierHook( STEFX_RETIRED_ALERT_TEAM, "G_AlertTeam" );
}

void G_CheckForDismemberment( gentity_t *ent, gentity_t *enemy, vec3_t point,
	int damage, int deathAnim, qboolean postDeath )
{
	(void)ent;
	(void)enemy;
	(void)point;
	(void)damage;
	(void)deathAnim;
	(void)postDeath;
	STEFX_HM_LogRetiredCarrierHook( STEFX_RETIRED_CHECK_DISMEMBER, "G_CheckForDismemberment" );
}

void G_Knockdown( gentity_t *victim )
{
	(void)victim;
	STEFX_HM_LogRetiredCarrierHook( STEFX_RETIRED_KNOCKDOWN, "G_Knockdown" );
}

void G_Dismember( gentity_t *ent, gentity_t *enemy, vec3_t point, int limbType,
	float limbRollBase, float limbPitchBase, int deathAnim, qboolean postDeath )
{
	(void)ent;
	(void)enemy;
	(void)point;
	(void)limbType;
	(void)limbRollBase;
	(void)limbPitchBase;
	(void)deathAnim;
	(void)postDeath;
	STEFX_HM_LogRetiredCarrierHook( STEFX_RETIRED_DISMEMBER, "G_Dismember" );
}

void TossClientWeapon( gentity_t *self, vec3_t direction, float speed )
{
	(void)self;
	(void)direction;
	(void)speed;
	STEFX_HM_LogRetiredCarrierHook( STEFX_RETIRED_TOSS_WEAPON, "TossClientWeapon" );
}

qboolean G_HeavyMelee( gentity_t *attacker )
{
	(void)attacker;
	STEFX_HM_LogRetiredCarrierHook( STEFX_RETIRED_HEAVY_MELEE, "G_HeavyMelee" );
	return qfalse;
}

int G_GetHitLocation( gentity_t *target, vec3_t point )
{
	(void)target;
	(void)point;
	STEFX_HM_LogRetiredCarrierHook( STEFX_RETIRED_HIT_LOCATION, "G_GetHitLocation" );
	return HL_NONE;
}

void tripwireThink( gentity_t *ent )
{
	(void)ent;
	STEFX_HM_LogRetiredCarrierHook( STEFX_RETIRED_TRIPWIRE, "tripwireThink" );
}
