#ifndef EF_AI_COMPAT_H
#define EF_AI_COMPAT_H

#include "g_local.h"
#include "ef_shared_compat.h"

// The files in game/ef_ai are untouched EF 1.2 sources.  This header is
// force-included by the Xbox project so carrier IDs remain an Xbox boundary.

#define AIGT_SINGLE_PLAYER 0
#define AIGT_TEAM 1
#define AIGT_OTHER 2

#define MOD_PHASER MOD_BRYAR_PISTOL
#define MOD_PHASER_ALT MOD_BRYAR_PISTOL_ALT
#define MOD_CRIFLE MOD_BLASTER
#define MOD_CRIFLE_SPLASH (MOD_MAX + 1)
#define MOD_CRIFLE_ALT (MOD_MAX + 2)
#define MOD_CRIFLE_ALT_SPLASH (MOD_MAX + 3)
#define MOD_IMOD MOD_DEMP2
#define MOD_IMOD_ALT MOD_DEMP2_ALT
#define MOD_SCAVENGER MOD_BOWCASTER
#define MOD_SCAVENGER_ALT (MOD_MAX + 4)
#define MOD_SCAVENGER_ALT_SPLASH (MOD_MAX + 5)
#define MOD_STASIS MOD_FLECHETTE
#define MOD_STASIS_ALT MOD_FLECHETTE_ALT_SPLASH
#define MOD_GRENADE MOD_THERMAL
#define MOD_GRENADE_ALT (MOD_MAX + 6)
#define MOD_GRENADE_SPLASH MOD_THERMAL_SPLASH
#define MOD_GRENADE_ALT_SPLASH MOD_TRIP_MINE_SPLASH
#define MOD_TETRION MOD_DISRUPTOR
#define MOD_TETRION_ALT MOD_DISRUPTOR_SPLASH
#define MOD_DREADNOUGHT MOD_ROCKET
#define MOD_DREADNOUGHT_ALT MOD_ROCKET_HOMING
#define MOD_QUANTUM MOD_REPEATER
#define MOD_QUANTUM_SPLASH MOD_REPEATER_ALT_SPLASH
#define MOD_QUANTUM_ALT MOD_CONC
#define MOD_QUANTUM_ALT_SPLASH MOD_CONC_ALT
#define MOD_KNOCKOUT (MOD_MAX + 7)
#define MOD_RESPAWN (MOD_MAX + 8)
#define MOD_EXPLOSION (MOD_MAX + 9)
#define MOD_ASSIMILATE (MOD_MAX + 10)
#define MOD_BORG (MOD_MAX + 11)
#define MOD_BORG_ALT (MOD_MAX + 12)
#define MOD_SEEKER (MOD_MAX + 13)

#define EV_TEAM_SOUND EV_GLOBAL_TEAM_SOUND
#define MAX_TEAM_SOUNDS (GTS_TEAMS_ARE_TIED + 1)
#define RETURN_FLAG_SOUND GTS_RED_RETURN

static vmCvar_t ef_ai_disabled_cvar;
#define g_pModDisintegration ef_ai_disabled_cvar
#define g_pModSpecialties ef_ai_disabled_cvar

#if defined(EF_AI_STATE_COPY_BOUNDARY)
static void EF_AI_MirrorCarrierAmmoForOfficialBot(playerState_t *state)
{
	static const int weapons[] = {
		WP_BRYAR_PISTOL,
		WP_BLASTER,
		WP_DEMP2,
		WP_BOWCASTER,
		WP_FLECHETTE,
		WP_THERMAL,
		WP_DISRUPTOR,
		WP_REPEATER,
		WP_ROCKET_LAUNCHER
	};
	int ammoValues[sizeof(weapons) / sizeof(weapons[0])];
	int i;
	static qboolean logged = qfalse;

	for (i = 0; i < (int)(sizeof(weapons) / sizeof(weapons[0])); ++i)
	{
		int ammoIndex = weaponData[weapons[i]].ammoIndex;
		ammoValues[i] = (ammoIndex >= 0 && ammoIndex < MAX_WEAPONS)
			? state->ammo[ammoIndex]
			: 0;
	}
	for (i = 0; i < (int)(sizeof(weapons) / sizeof(weapons[0])); ++i)
	{
		state->ammo[weapons[i]] = ammoValues[i];
	}

	if (!logged)
	{
		G_Printf("STEFX_HM: official EF bot ammo view mirrored from carrier ammo buckets\n");
		logged = qtrue;
	}
}

static void *EF_AI_CopyWithStateBoundary(void *dest, const void *source, size_t bytes)
{
	void *result = ::memcpy(dest, source, bytes);

	if (bytes == sizeof(playerState_t))
	{
		EF_AI_MirrorCarrierAmmoForOfficialBot((playerState_t *)dest);
	}
	return result;
}

#define memcpy EF_AI_CopyWithStateBoundary
#endif

static int EF_AI_OfficialWeaponToCarrier(int weapon)
{
	qboolean alt = qfalse;
	int base = weapon;

	if (weapon >= 11 && weapon <= 19)
	{
		alt = qtrue;
		base -= 10;
	}

	switch (base)
	{
	case 1: base = WP_BRYAR_PISTOL; break;
	case 2: base = WP_BLASTER; break;
	case 3: base = WP_DEMP2; break;
	case 4: base = WP_BOWCASTER; break;
	case 5: base = WP_FLECHETTE; break;
	case 6: base = WP_THERMAL; break;
	case 7: base = WP_DISRUPTOR; break;
	case 8: base = WP_REPEATER; break;
	case 9: base = WP_ROCKET_LAUNCHER; break;
	default: return WP_NONE;
	}

	return alt ? base + WP_NUM_WEAPONS : base;
}

static int EF_AI_CarrierWeaponToOfficial(int weapon)
{
	qboolean alt = qfalse;
	int base = weapon;

	if (base > WP_NUM_WEAPONS)
	{
		alt = qtrue;
		base -= WP_NUM_WEAPONS;
	}

	switch (base)
	{
	case WP_BRYAR_PISTOL: base = 1; break;
	case WP_BLASTER: base = 2; break;
	case WP_DEMP2: base = 3; break;
	case WP_BOWCASTER: base = 4; break;
	case WP_FLECHETTE: base = 5; break;
	case WP_THERMAL: base = 6; break;
	case WP_DISRUPTOR: base = 7; break;
	case WP_REPEATER: base = 8; break;
	case WP_ROCKET_LAUNCHER: base = 9; break;
	default: return 0;
	}

	return alt ? base + 10 : base;
}

static int EF_AI_BotChooseBestFightWeapon(int weaponstate, int *inventory, qboolean meleeRange)
{
	return EF_AI_OfficialWeaponToCarrier(
		game::trap_BotChooseBestFightWeapon(weaponstate, inventory, meleeRange));
}

static void EF_AI_BotGetWeaponInfo(int weaponstate, int weapon, void *weaponinfo)
{
	game::trap_BotGetWeaponInfo(
		weaponstate,
		EF_AI_CarrierWeaponToOfficial(weapon),
		weaponinfo);
}

static void EF_AI_EA_SelectWeapon(int client, int weapon)
{
	if (weapon > WP_NUM_WEAPONS)
	{
		weapon -= WP_NUM_WEAPONS;
	}
	game::trap_EA_SelectWeapon(client, weapon);
}

#define trap_BotChooseBestFightWeapon EF_AI_BotChooseBestFightWeapon
#define trap_BotGetWeaponInfo EF_AI_BotGetWeaponInfo
#define trap_EA_SelectWeapon EF_AI_EA_SelectWeapon

#define G_Alloc(size) ((bot_state_t *)G_Alloc(size))

#endif
