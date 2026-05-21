// Bryar Pistol Weapon Effects

// this line must stay at top so the whole PCH thing works...
#include "cg_headers.h"

//#include "cg_local.h"
#include "cg_media.h"
#include "FxScheduler.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
#endif

/*
-------------------------

	MAIN FIRE

-------------------------
FX_BryarProjectileThink
-------------------------
*/
void FX_BryarProjectileThink(  centity_t *cent, const struct weaponInfo_s *weapon )
{
	vec3_t forward;
#ifdef _XBOX
	static int s_xboxBryarProjectileLogBudget = 64;
	qboolean xboxLogBryar = (s_xboxBryarProjectileLogBudget > 0);
#endif

	if ( VectorNormalize2( cent->gent->s.pos.trDelta, forward ) == 0.0f )
	{
		if ( VectorNormalize2( cent->currentState.pos.trDelta, forward ) == 0.0f )
		{
			forward[2] = 1.0f;
		}
	}

	// hack the scale of the forward vector if we were just fired or bounced...this will shorten up the tail for a split second so tails don't clip so harshly
	int dif = cg.time - cent->gent->s.pos.trTime;

	if ( dif < 75 )
	{
		if ( dif < 0 )
		{
			dif = 0;
		}

		float scale = ( dif / 75.0f ) * 0.95f + 0.05f;

		VectorScale( forward, scale, forward );
	}

#ifdef _XBOX
	if ( xboxLogBryar )
	{
		XBLF("JA: FX_BryarProjectileThink ent=%d owner=%d effect=%d origin=%g,%g,%g forward=%g,%g,%g trDelta=%g,%g,%g dif=%d",
			cent ? cent->currentState.number : -1,
			(cent && cent->gent && cent->gent->owner) ? cent->gent->owner->s.number : -1,
			cgs.effects.bryarShotEffect,
			cent ? cent->lerpOrigin[0] : 0.0f,
			cent ? cent->lerpOrigin[1] : 0.0f,
			cent ? cent->lerpOrigin[2] : 0.0f,
			forward[0], forward[1], forward[2],
			cent ? cent->currentState.pos.trDelta[0] : 0.0f,
			cent ? cent->currentState.pos.trDelta[1] : 0.0f,
			cent ? cent->currentState.pos.trDelta[2] : 0.0f,
			dif);
		--s_xboxBryarProjectileLogBudget;
	}
#endif

	if ( cent->gent && cent->gent->owner && cent->gent->owner->s.number > 0 )
	{
		theFxScheduler.PlayEffect( "bryar/NPCshot", cent->lerpOrigin, forward );
	}
	else
	{
		theFxScheduler.PlayEffect( cgs.effects.bryarShotEffect, cent->lerpOrigin, forward );
	}
}

/*
-------------------------
FX_BryarHitWall
-------------------------
*/
void FX_BryarHitWall( vec3_t origin, vec3_t normal )
{
#ifdef _XBOX
	static int s_xboxBryarHitWallLogBudget = 32;
	if ( s_xboxBryarHitWallLogBudget > 0 )
	{
		XBLF("JA: FX_BryarHitWall effect=%d origin=%g,%g,%g normal=%g,%g,%g",
			cgs.effects.bryarWallImpactEffect,
			origin[0], origin[1], origin[2],
			normal[0], normal[1], normal[2]);
		--s_xboxBryarHitWallLogBudget;
	}
#endif
	theFxScheduler.PlayEffect( cgs.effects.bryarWallImpactEffect, origin, normal );
}

/*
-------------------------
FX_BryarHitPlayer
-------------------------
*/
void FX_BryarHitPlayer( vec3_t origin, vec3_t normal, qboolean humanoid )
{
#ifdef _XBOX
	static int s_xboxBryarHitPlayerLogBudget = 32;
	if ( s_xboxBryarHitPlayerLogBudget > 0 )
	{
		XBLF("JA: FX_BryarHitPlayer effect=%d humanoid=%d origin=%g,%g,%g normal=%g,%g,%g",
			cgs.effects.bryarFleshImpactEffect,
			(int)humanoid,
			origin[0], origin[1], origin[2],
			normal[0], normal[1], normal[2]);
		--s_xboxBryarHitPlayerLogBudget;
	}
#endif
	theFxScheduler.PlayEffect( cgs.effects.bryarFleshImpactEffect, origin, normal );
}


/*
-------------------------

	ALT FIRE

-------------------------
FX_BryarAltProjectileThink
-------------------------
*/
void FX_BryarAltProjectileThink(  centity_t *cent, const struct weaponInfo_s *weapon )
{
	vec3_t forward;

	if ( VectorNormalize2( cent->gent->s.pos.trDelta, forward ) == 0.0f )
	{
		if ( VectorNormalize2( cent->currentState.pos.trDelta, forward ) == 0.0f )
		{
			forward[2] = 1.0f;
		}
	}

	// hack the scale of the forward vector if we were just fired or bounced...this will shorten up the tail for a split second so tails don't clip so harshly
	int dif = cg.time - cent->gent->s.pos.trTime;

	if ( dif < 75 )
	{
		if ( dif < 0 )
		{
			dif = 0;
		}

		float scale = ( dif / 75.0f ) * 0.95f + 0.05f;

		VectorScale( forward, scale, forward );
	}

	// see if we have some sort of extra charge going on
	for ( int t = 1; t < cent->gent->count; t++ )
	{
		// just add ourselves over, and over, and over when we are charged
		theFxScheduler.PlayEffect( cgs.effects.bryarPowerupShotEffect, cent->lerpOrigin, forward );
	}

	theFxScheduler.PlayEffect( cgs.effects.bryarShotEffect, cent->lerpOrigin, forward );
}

/*
-------------------------
FX_BryarAltHitWall
-------------------------
*/
void FX_BryarAltHitWall( vec3_t origin, vec3_t normal, int power )
{
	switch( power )
	{
	case 4:
	case 5:
		theFxScheduler.PlayEffect( cgs.effects.bryarWallImpactEffect3, origin, normal );
		break;

	case 2:
	case 3:
		theFxScheduler.PlayEffect( cgs.effects.bryarWallImpactEffect2, origin, normal );
		break;

	default:
		theFxScheduler.PlayEffect( cgs.effects.bryarWallImpactEffect, origin, normal );
		break;
	}
}

/*
-------------------------
FX_BryarAltHitPlayer
-------------------------
*/
void FX_BryarAltHitPlayer( vec3_t origin, vec3_t normal, qboolean humanoid )
{
	theFxScheduler.PlayEffect( cgs.effects.bryarFleshImpactEffect, origin, normal );
}
