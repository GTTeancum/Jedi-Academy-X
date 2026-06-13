//
// gameinfo.c
//

// *** This file is used by both the game and the user interface ***


#include "gameinfo.h"
#include "..\game\weapons.h"
#ifdef _XBOX
#include "..\..\code\win32\xb_log.h"
#endif


gameinfo_import_t	gi;

weaponData_t weaponData[WP_NUM_WEAPONS];
ammoData_t ammoData[AMMO_MAX];

extern void WP_LoadWeaponParms (void);

//
// Initialization - Read in files and parse into infos
//

/*
===============
GI_Init
===============
*/
void GI_Init( gameinfo_import_t *import ) {
#ifdef _XBOX
	XBLog_Write("STEFX: GI_Init enter");
#endif
	gi = *import;

#ifdef _XBOX
	XBLog_Write("STEFX: GI_Init before WP_LoadWeaponParms");
#endif
	WP_LoadWeaponParms ();
#ifdef _XBOX
	XBLog_Write("STEFX: GI_Init done");
#endif
}
