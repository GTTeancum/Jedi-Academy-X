// Blaster Weapon

// this line must stay at top so the whole PCH thing works...
#include "cg_headers.h"

#include "cg_local.h"
#include "cg_media.h"
#include "FxScheduler.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
#endif

/*
-------------------------
FX_BlasterProjectileThink
-------------------------
*/

void FX_BlasterProjectileThink( centity_t *cent, const struct weaponInfo_s *weapon )
{
	vec3_t forward;
#ifdef _XBOX
	static int s_xboxBlasterProjectileLogCount = 0;
	const qboolean xboxLogProjectile = (s_xboxBlasterProjectileLogCount < 96);
#endif

 	if (cent->currentState.eFlags & EF_USE_ANGLEDELTA)
	{
		AngleVectors(cent->currentState.angles, forward, 0, 0);
	}
	else
	{
		if ( VectorNormalize2( cent->gent->s.pos.trDelta, forward ) == 0.0f )
		{
			if ( VectorNormalize2( cent->currentState.pos.trDelta, forward ) == 0.0f )
			{
				forward[2] = 1.0f;
			}
		}
	}
#ifdef _XBOX
	if ( xboxLogProjectile )
	{
		XBLF("JA: FX_BlasterProjectileThink #%d ent=%d owner=%d alt=%d origin=%g,%g,%g forward=%g,%g,%g effectMain=%d weaponTrail=%p",
			s_xboxBlasterProjectileLogCount,
			cent ? cent->currentState.number : -1,
			(cent && cent->gent && cent->gent->owner) ? cent->gent->owner->s.number : -1,
			(cent && cent->gent) ? (int)cent->gent->alt_fire : -1,
			cent ? cent->lerpOrigin[0] : 0.0f,
			cent ? cent->lerpOrigin[1] : 0.0f,
			cent ? cent->lerpOrigin[2] : 0.0f,
			forward[0],
			forward[1],
			forward[2],
			cgs.effects.blasterShotEffect,
			weapon ? weapon->missileTrailFunc : NULL);
		++s_xboxBlasterProjectileLogCount;
	}
#endif

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

	if ( cent->gent && cent->gent->owner && cent->gent->owner->s.number > 0 )
	{
		theFxScheduler.PlayEffect( "blaster/NPCshot", cent->lerpOrigin, forward );
	}
	else
	{
		theFxScheduler.PlayEffect( cgs.effects.blasterShotEffect, cent->lerpOrigin, forward );
	}
}

/*
-------------------------
FX_BlasterAltFireThink
-------------------------
*/
void FX_BlasterAltFireThink( centity_t *cent, const struct weaponInfo_s *weapon )
{
	FX_BlasterProjectileThink( cent, weapon );
}

/*
-------------------------
FX_BlasterWeaponHitWall
-------------------------
*/
void FX_BlasterWeaponHitWall( vec3_t origin, vec3_t normal )
{
	theFxScheduler.PlayEffect( cgs.effects.blasterWallImpactEffect, origin, normal );
}

/*
-------------------------
FX_BlasterWeaponHitPlayer
-------------------------
*/
void FX_BlasterWeaponHitPlayer( gentity_t *hit, vec3_t origin, vec3_t normal, qboolean humanoid )
{
	//temporary? just testing out the damage skin stuff -rww
	if ( hit && hit->client && hit->ghoul2.size() )
	{
		CG_AddGhoul2Mark(cgs.media.bdecal_burnmark1, flrand(3.5, 4.0), origin, normal, hit->s.number,
			hit->client->ps.origin, hit->client->renderInfo.legsYaw, hit->ghoul2, hit->s.modelScale, Q_irand(10000, 13000));
	}
        
	theFxScheduler.PlayEffect( cgs.effects.blasterFleshImpactEffect, origin, normal );
}
