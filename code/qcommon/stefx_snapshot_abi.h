#ifndef STEFX_SNAPSHOT_ABI_H
#define STEFX_SNAPSHOT_ABI_H

#include "../game/q_shared.h"

#define STEFX_PS_MAX_AMMO MAX_AMMO
#define STEFX_MAX_ENTITIES_IN_SNAPSHOT 256

typedef playerState_t stefxPlayerState_t;

typedef struct stefxSnapshot_s {
	int				snapFlags;
	int				ping;
	int				serverTime;
	byte			areamask[MAX_MAP_AREA_BYTES];
	int				cmdNum;
	stefxPlayerState_t ps;
	int				numEntities;
	entityState_t	entities[STEFX_MAX_ENTITIES_IN_SNAPSHOT];
	int				numConfigstringChanges;
	int				configstringNum;
	int				numServerCommands;
	int				serverCommandSequence;
} stefxSnapshot_t;

static void STEFX_CopyEfPlayerStateToJa(playerState_t *dst, const stefxPlayerState_t *src)
{
	*dst = *src;
}

static void STEFX_CopyJaPlayerStateToEf(stefxPlayerState_t *dst, const playerState_t *src)
{
	*dst = *src;
}

#endif
