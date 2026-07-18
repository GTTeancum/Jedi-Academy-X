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

#define PERS_ACCURACY_SHOTS 15
#define PERS_ACCURACY_HITS 15
#define PERS_REWARD_COUNT PERS_DEFEND_COUNT
#define PERS_REWARD PERS_ASSIST_COUNT
#define REWARD_IMPRESSIVE 1

#define EF_AWARD_IMPRESSIVE 0
#define EF_AWARD_MASK 0
#define EF_BOUNCE ( 1 << 5 )
#define EF_BOUNCE_HALF ( 1 << 6 )

#define EV_COMPRESSION_RIFLE EV_BULLET
#define EV_COMPRESSION_RIFLE_ALT EV_MISSILE_HIT
#define EV_IMOD EV_MISSILE_MISS
#define EV_IMOD_HIT EV_MISSILE_HIT
#define EV_IMOD_ALTFIRE EV_MISSILE_MISS
#define EV_IMOD_ALTFIRE_HIT EV_MISSILE_HIT
#define EV_STASIS EV_MISSILE_HIT
#define EV_GRENADE_EXPLODE EV_MISSILE_HIT
#define EV_GRENADE_SHRAPNEL_EXPLODE EV_MISSILE_HIT
#define EV_TETRION EV_MISSILE_HIT
#define EV_DREADNOUGHT_MISS EV_MISSILE_MISS
#define EV_BORG_ALT_WEAPON EV_MISSILE_HIT
#define EV_HYPO_PUFF EV_MISSILE_MISS

#define SOUND_DIR "sound/weapons/"

static float STEFX_HM_OfficialFlrandom( float minValue, float maxValue )
{
	return ( ( rand() * ( maxValue - minValue ) ) / 32768.0f ) + minValue;
}

static void VectorShort( vec3_t vector )
{
	VectorNormalize( vector );
	VectorScale( vector, 8191.0f, vector );
	SnapVector( vector );
}

#define flrandom STEFX_HM_OfficialFlrandom
#define irandom Q_irand
#define right stefx_hm_weapon_right

static vmCvar_t stefx_hm_disabled_assimilation;
#define g_pModAssimilation stefx_hm_disabled_assimilation

static void STEFX_HM_OfficialSound( gentity_t *entity, int soundIndex )
{
	G_Sound( entity, CHAN_AUTO, soundIndex );
}
#define G_Sound(entity, soundIndex) STEFX_HM_OfficialSound(entity, soundIndex)

void STEFX_HM_OfficialDamage( gentity_t *targ, gentity_t *inflictor,
	gentity_t *attacker, vec3_t dir, vec3_t point, int damage, int dflags, int mod );
qboolean STEFX_HM_OfficialRadiusDamage( vec3_t origin, gentity_t *attacker,
	float damage, float radius, gentity_t *ignore, int dflags, int mod );

#define G_Damage STEFX_HM_OfficialDamage
#define G_RadiusDamage STEFX_HM_OfficialRadiusDamage
#define CalcMuzzlePoint STEFX_HM_OfficialCalcMuzzlePoint
#define FireWeapon STEFX_HM_OfficialFireWeapon

#include "g_weapon.c"

#undef G_Damage
#undef G_RadiusDamage
#undef CalcMuzzlePoint
#undef FireWeapon
#undef G_Sound
#undef right

void FireWeapon( gentity_t *ent, qboolean altFire )
{
	static qboolean logged = qfalse;
	if ( !logged )
	{
		G_Printf( "STEFX_HM: official EF weapon dispatcher active\n" );
		logged = qtrue;
	}
	STEFX_HM_OfficialFireWeapon( ent, altFire );
}

void CalcMuzzlePoint( gentity_t *ent, vec3_t forwardVector, vec3_t rightVector,
	vec3_t upVector, vec3_t muzzlePoint )
{
	STEFX_HM_OfficialCalcMuzzlePoint( ent, forwardVector, rightVector, upVector,
		muzzlePoint, 0.0f );
}

enum
{
	STEFX_RETIRED_BLASTER_MISSILE = 1 << 0,
	STEFX_RETIRED_TURRET_MISSILE = 1 << 1,
	STEFX_RETIRED_GENERIC_BLASTER = 1 << 2,
	STEFX_RETIRED_TURBO_LASER = 1 << 3,
	STEFX_RETIRED_MISSILE_SPEED = 1 << 4,
	STEFX_RETIRED_EMPLACED_GUN = 1 << 5,
	STEFX_RETIRED_VEH_MUZZLE = 1 << 6,
	STEFX_RETIRED_VEH_WEAPON = 1 << 7,
	STEFX_RETIRED_VEH_FIRE_FX = 1 << 8,
	STEFX_RETIRED_LASER_TRAP_STICK = 1 << 9
};

static unsigned int stefx_hm_retiredWeaponHooksLogged;

static void STEFX_HM_LogRetiredWeaponHook( unsigned int bit, const char *name )
{
	if ( !( stefx_hm_retiredWeaponHooksLogged & bit ) )
	{
		stefx_hm_retiredWeaponHooksLogged |= bit;
		G_Printf( "STEFX_HM: retired JA carrier weapon hook invoked name='%s'\n", name );
	}
}

void WP_FireBlasterMissile( gentity_t *ent, vec3_t start, vec3_t dir,
	qboolean altFire )
{
	(void)ent;
	(void)start;
	(void)dir;
	(void)altFire;
	STEFX_HM_LogRetiredWeaponHook( STEFX_RETIRED_BLASTER_MISSILE,
		"WP_FireBlasterMissile" );
}

void WP_FireTurretMissile( gentity_t *ent, vec3_t start, vec3_t dir,
	qboolean altFire, int damage, int velocity, int mod, gentity_t *ignore )
{
	(void)ent;
	(void)start;
	(void)dir;
	(void)altFire;
	(void)damage;
	(void)velocity;
	(void)mod;
	(void)ignore;
	STEFX_HM_LogRetiredWeaponHook( STEFX_RETIRED_TURRET_MISSILE,
		"WP_FireTurretMissile" );
}

void WP_FireGenericBlasterMissile( gentity_t *ent, vec3_t start, vec3_t dir,
	qboolean altFire, int damage, int velocity, int mod )
{
	(void)ent;
	(void)start;
	(void)dir;
	(void)altFire;
	(void)damage;
	(void)velocity;
	(void)mod;
	STEFX_HM_LogRetiredWeaponHook( STEFX_RETIRED_GENERIC_BLASTER,
		"WP_FireGenericBlasterMissile" );
}

void WP_FireTurboLaserMissile( gentity_t *ent, vec3_t start, vec3_t dir )
{
	(void)ent;
	(void)start;
	(void)dir;
	STEFX_HM_LogRetiredWeaponHook( STEFX_RETIRED_TURBO_LASER,
		"WP_FireTurboLaserMissile" );
}

float WP_SpeedOfMissileForWeapon( int weapon, qboolean altFire )
{
	(void)weapon;
	(void)altFire;
	STEFX_HM_LogRetiredWeaponHook( STEFX_RETIRED_MISSILE_SPEED,
		"WP_SpeedOfMissileForWeapon" );
	return 0.0f;
}

void SP_emplaced_gun( gentity_t *ent )
{
	STEFX_HM_LogRetiredWeaponHook( STEFX_RETIRED_EMPLACED_GUN,
		"SP_emplaced_gun" );
	G_FreeEntity( ent );
}

void WP_CalcVehMuzzle( gentity_t *ent, int muzzleNum )
{
	(void)ent;
	(void)muzzleNum;
	STEFX_HM_LogRetiredWeaponHook( STEFX_RETIRED_VEH_MUZZLE,
		"WP_CalcVehMuzzle" );
}

gentity_t *WP_FireVehicleWeapon( gentity_t *ent, vec3_t start, vec3_t dir,
	vehWeaponInfo_t *vehWeapon, qboolean altFire, qboolean isTurretWeapon )
{
	(void)ent;
	(void)start;
	(void)dir;
	(void)vehWeapon;
	(void)altFire;
	(void)isTurretWeapon;
	STEFX_HM_LogRetiredWeaponHook( STEFX_RETIRED_VEH_WEAPON,
		"WP_FireVehicleWeapon" );
	return NULL;
}

void G_VehMuzzleFireFX( gentity_t *ent, gentity_t *broadcaster,
	int muzzlesFired )
{
	(void)ent;
	(void)broadcaster;
	(void)muzzlesFired;
	STEFX_HM_LogRetiredWeaponHook( STEFX_RETIRED_VEH_FIRE_FX,
		"G_VehMuzzleFireFX" );
}

void laserTrapStick( gentity_t *ent, vec3_t endpos, vec3_t normal )
{
	(void)ent;
	(void)endpos;
	(void)normal;
	STEFX_HM_LogRetiredWeaponHook( STEFX_RETIRED_LASER_TRAP_STICK,
		"laserTrapStick" );
}
