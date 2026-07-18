#include "../q_shared.h"
#include "../bg_public.h"
#include "../ef_shared_compat.h"

#ifdef QAGAME
#include "../g_local.h"
extern void Q3_SetParm( int entID, int parmNum, const char *parmValue );
char *G_NewString( const char *string );
#else
#include "../../cgame/cg_local.h"
char *CG_NewString( const char *string );
#endif

extern void *Z_Malloc( int iSize, memtag_t eTag, qboolean bZeroit, int iAlign );
extern void Z_Free( void *pvAddress );

#include "../../namespace_begin.h"

static void STEFX_HM_BeginParseSession( void )
{
	COM_BeginParseSession( "official EF bg_misc" );
}

static char *STEFX_HM_ParseExt( char **data, qboolean allowLineBreaks )
{
	const char *cursor = *data;
	char *token = COM_ParseExt( &cursor, allowLineBreaks );
	*data = (char *)cursor;
	return token;
}

static char *STEFX_HM_Parse( char **data )
{
	const char *cursor = *data;
	char *token = COM_Parse( &cursor );
	*data = (char *)cursor;
	return token;
}

#define COM_BeginParseSession STEFX_HM_BeginParseSession
#define COM_ParseExt STEFX_HM_ParseExt
#define COM_Parse STEFX_HM_Parse
#define Max_Ammo STEFX_HM_OfficialMaxAmmo
#define BG_CanItemBeGrabbed STEFX_HM_OfficialCanItemBeGrabbed

#include "bg_misc.c"

#undef BG_CanItemBeGrabbed
#undef Max_Ammo
#undef COM_Parse
#undef COM_ParseExt
#undef COM_BeginParseSession

static void STEFX_HM_MapOfficialMaxAmmoToCarrierIds( void )
{
	static qboolean initialized = qfalse;

	if ( initialized )
	{
		return;
	}

	memset( STEFX_HM_OfficialMaxAmmo, 0, sizeof( STEFX_HM_OfficialMaxAmmo ) );
	STEFX_HM_OfficialMaxAmmo[WP_PHASER] = 50;
	STEFX_HM_OfficialMaxAmmo[WP_COMPRESSION_RIFLE] = 128;
	STEFX_HM_OfficialMaxAmmo[WP_IMOD] = 60;
	STEFX_HM_OfficialMaxAmmo[WP_SCAVENGER_RIFLE] = 100;
	STEFX_HM_OfficialMaxAmmo[WP_STASIS] = 50;
	STEFX_HM_OfficialMaxAmmo[WP_GRENADE_LAUNCHER] = 30;
	STEFX_HM_OfficialMaxAmmo[WP_TETRION_DISRUPTOR] = 120;
	STEFX_HM_OfficialMaxAmmo[WP_QUANTUM_BURST] = 20;
	STEFX_HM_OfficialMaxAmmo[WP_DREADNOUGHT] = 120;
	STEFX_HM_OfficialMaxAmmo[WP_VOYAGER_HYPO] = 100;
	initialized = qtrue;
}

qboolean BG_CanItemBeGrabbed( const entityState_t *ent, const playerState_t *ps )
{
	static qboolean logged = qfalse;

	STEFX_HM_MapOfficialMaxAmmoToCarrierIds();
	if ( !logged )
	{
		Com_Printf( "STEFX_HM: official EF shared item table and pickup rules active\n" );
		logged = qtrue;
	}
	return STEFX_HM_OfficialCanItemBeGrabbed( ent, ps );
}

float vectoyaw( const vec3_t vec )
{
	float yaw;

	if ( vec[YAW] == 0 && vec[PITCH] == 0 )
	{
		yaw = 0;
	}
	else
	{
		if ( vec[PITCH] )
		{
			yaw = ( atan2( vec[YAW], vec[PITCH] ) * 180 / M_PI );
		}
		else if ( vec[YAW] > 0 )
		{
			yaw = 90;
		}
		else
		{
			yaw = 270;
		}
		if ( yaw < 0 )
		{
			yaw += 360;
		}
	}

	return yaw;
}

void BG_TouchJumpPad( playerState_t *ps, entityState_t *jumppad )
{
	if ( ps->pm_type != PM_NORMAL && ps->pm_type != PM_JETPACK && ps->pm_type != PM_FLOAT )
	{
		return;
	}

	ps->jumppad_ent = jumppad->number;
	ps->jumppad_frame = ps->pmove_framecount;
	VectorCopy( jumppad->origin2, ps->velocity );
}

// These symbols belong to JA systems that are still linked by carrier files.
// Keep the boundary explicit and inert until those owners are replaced.
const char *bgToggleableSurfaces[BG_NUM_TOGGLEABLE_SURFACES] = { NULL };
const int bgToggleableSurfaceDebris[BG_NUM_TOGGLEABLE_SURFACES] = { 0 };
const char *bg_customSiegeSoundNames[MAX_CUSTOM_SIEGE_SOUNDS] = { NULL };

char *forceMasteryLevels[NUM_FORCE_MASTERY_LEVELS] =
{
	"MASTERY0", "MASTERY1", "MASTERY2", "MASTERY3",
	"MASTERY4", "MASTERY5", "MASTERY6", "MASTERY7"
};
int forceMasteryPoints[NUM_FORCE_MASTERY_LEVELS] = { 0 };
int bgForcePowerCost[NUM_FORCE_POWERS][NUM_FORCE_POWER_LEVELS] = { 0 };
int forcePowerSorted[NUM_FORCE_POWERS] = { 0 };
int forcePowerDarkLight[NUM_FORCE_POWERS] = { 0 };

int WeaponReadyAnim[WP_NUM_WEAPONS] =
{
	TORSO_DROPWEAP1, TORSO_WEAPONREADY3, TORSO_WEAPONREADY3, BOTH_STAND2,
	TORSO_WEAPONREADY2, TORSO_WEAPONREADY3, TORSO_WEAPONREADY3,
	TORSO_WEAPONREADY3, TORSO_WEAPONREADY3, TORSO_WEAPONREADY3,
	TORSO_WEAPONREADY3, TORSO_WEAPONREADY3, TORSO_WEAPONREADY10,
	TORSO_WEAPONREADY10, TORSO_WEAPONREADY10, TORSO_WEAPONREADY3,
	TORSO_WEAPONREADY2, BOTH_STAND1, TORSO_WEAPONREADY1
};

int WeaponReadyLegsAnim[WP_NUM_WEAPONS] =
{
	BOTH_STAND1, BOTH_STAND1, BOTH_STAND1, BOTH_STAND2, BOTH_STAND1,
	BOTH_STAND1, BOTH_STAND1, BOTH_STAND1, BOTH_STAND1, BOTH_STAND1,
	BOTH_STAND1, BOTH_STAND1, BOTH_STAND1, BOTH_STAND1, BOTH_STAND1,
	BOTH_STAND1, BOTH_STAND1, BOTH_STAND1, BOTH_STAND1
};

int WeaponAttackAnim[WP_NUM_WEAPONS] =
{
	BOTH_ATTACK1, BOTH_ATTACK3, BOTH_ATTACK3, BOTH_STAND2, BOTH_ATTACK2,
	BOTH_ATTACK3, BOTH_ATTACK3, BOTH_ATTACK3, BOTH_ATTACK3, BOTH_ATTACK3,
	BOTH_ATTACK3, BOTH_ATTACK3, BOTH_THERMAL_THROW, BOTH_ATTACK3,
	BOTH_ATTACK3, BOTH_ATTACK2, BOTH_STAND1, BOTH_ATTACK1
};

void BG_ParseField( BG_field_t *fields, const char *key, const char *value, byte *ent )
{
	BG_field_t *field;
	byte *base;
	vec3_t vec;

	for ( field = fields; field->name; ++field )
	{
		if ( Q_stricmp( field->name, key ) )
		{
			continue;
		}

		base = ent;
		switch ( field->type )
		{
		case F_LSTRING:
#ifdef QAGAME
			*(char **)(base + field->ofs) = G_NewString( value );
#else
			*(char **)(base + field->ofs) = CG_NewString( value );
#endif
			break;
		case F_VECTOR:
			sscanf( value, "%f %f %f", &vec[0], &vec[1], &vec[2] );
			VectorCopy( vec, (float *)(base + field->ofs) );
			break;
		case F_INT:
			*(int *)(base + field->ofs) = atoi( value );
			break;
		case F_FLOAT:
			*(float *)(base + field->ofs) = atof( value );
			break;
		case F_ANGLEHACK:
			((float *)(base + field->ofs))[0] = 0;
			((float *)(base + field->ofs))[1] = atof( value );
			((float *)(base + field->ofs))[2] = 0;
			break;
#ifdef QAGAME
		case F_PARM1: case F_PARM2: case F_PARM3: case F_PARM4:
		case F_PARM5: case F_PARM6: case F_PARM7: case F_PARM8:
		case F_PARM9: case F_PARM10: case F_PARM11: case F_PARM12:
		case F_PARM13: case F_PARM14: case F_PARM15: case F_PARM16:
			Q3_SetParm( ((gentity_t *)ent)->s.number, field->type - F_PARM1, value );
			break;
#endif
		default:
			break;
		}
		return;
	}
}

int BG_GetItemIndexByTag( int tag, int type )
{
	int i;
	for ( i = 0; i < bg_numItems; ++i )
	{
		if ( bg_itemlist[i].giTag == tag && bg_itemlist[i].giType == type )
		{
			return i;
		}
	}
	return 0;
}

qboolean BG_IsItemSelectable( playerState_t *ps, int item )
{
	(void)ps;
	(void)item;
	return qtrue;
}

void BG_CycleInven( playerState_t *ps, int direction )
{
	(void)ps;
	(void)direction;
}

qboolean BG_HasYsalamiri( int gametype, playerState_t *ps )
{
	(void)gametype;
	(void)ps;
	return qfalse;
}

qboolean BG_CanUseFPNow( int gametype, playerState_t *ps, int time, forcePowers_t power )
{
	(void)gametype;
	(void)ps;
	(void)time;
	(void)power;
	return qfalse;
}

int BG_ProperForceIndex( int power )
{
	(void)power;
	return -1;
}

int BG_EmplacedView( vec3_t baseAngles, vec3_t angles, float *newYaw, float constraint )
{
	float difference = AngleSubtract( baseAngles[YAW], angles[YAW] );
	if ( difference > constraint || difference < -constraint )
	{
		float amount = difference > constraint
			? difference - constraint
			: difference + constraint;
		difference = difference > constraint ? constraint : -constraint;
		*newYaw = AngleSubtract( angles[YAW], -difference );
		return (amount > 1.0f || amount < -1.0f) ? 2 : 1;
	}
	return 0;
}

int BG_ModelCache( const char *modelName, const char *skinName )
{
	(void)modelName;
	(void)skinName;
	return 0;
}

gitem_t *BG_FindItemForAmmo( ammo_t ammo )
{
	(void)ammo;
	return NULL;
}

void *BG_Alloc( int size )
{
	return Z_Malloc( size, TAG_BG_ALLOC, qfalse, 4 );
}

void *BG_AllocUnaligned( int size )
{
	return Z_Malloc( size, TAG_BG_ALLOC, qfalse, 4 );
}

#define STEFX_HM_MAX_TEMP_ALLOCS 3
static void *stefx_hm_tempAllocPointers[STEFX_HM_MAX_TEMP_ALLOCS] = { NULL };
static int stefx_hm_tempAllocSizes[STEFX_HM_MAX_TEMP_ALLOCS] = { 0 };

void *BG_TempAlloc( int size )
{
	int i;
	for ( i = 0; i < STEFX_HM_MAX_TEMP_ALLOCS; ++i )
	{
		if ( !stefx_hm_tempAllocPointers[i] )
		{
			stefx_hm_tempAllocPointers[i] = Z_Malloc( size, TAG_TEMP_WORKSPACE, qfalse, 4 );
			stefx_hm_tempAllocSizes[i] = size;
			return stefx_hm_tempAllocPointers[i];
		}
	}
	Com_Error( ERR_DROP, "BG_TempAlloc: exhausted temporary allocation slots" );
	return NULL;
}

void BG_TempFree( int size )
{
	int i;
	for ( i = STEFX_HM_MAX_TEMP_ALLOCS - 1; i >= 0; --i )
	{
		if ( stefx_hm_tempAllocPointers[i] && stefx_hm_tempAllocSizes[i] == size )
		{
			Z_Free( stefx_hm_tempAllocPointers[i] );
			stefx_hm_tempAllocPointers[i] = NULL;
			stefx_hm_tempAllocSizes[i] = 0;
			return;
		}
	}
}

char *BG_StringAlloc( const char *source )
{
	char *dest = (char *)BG_Alloc( strlen( source ) + 1 );
	strcpy( dest, source );
	return dest;
}

qboolean BG_OutOfMemory( void )
{
	return qfalse;
}

#include "../../namespace_end.h"
