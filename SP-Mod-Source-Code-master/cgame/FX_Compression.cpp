//Compression rifle weapon effects

#include "cg_local.h"
#include "FX_Public.h"
#ifdef _XBOX
#include "../../code/win32/xb_log.h"
#endif

/*
-------------------------
FX_CompressionShot
-------------------------
*/

void FX_CompressionShockRing( vec3_t start, vec3_t end )
{
	vec3_t	shot_dir, org;

	// Faint shock ring created by shooting..looks like start and end got switched around...sigh
	VectorSubtract( start, end, shot_dir );
	VectorNormalize( shot_dir );
	VectorMA( end, 18, shot_dir, org );

	FX_AddQuad( org, shot_dir, 
				NULL, NULL, 
				0.1f, 64.0f, 
				0.4f, 0.0, 
				0.0, 0.0, 
				0.0, 
				100, 
				cgs.media.compressionRingShader );
}

/*
-------------------------
FX_CompressionShot
-------------------------
*/

void FX_CompressionShot( vec3_t start, vec3_t end )
{
#ifdef _XBOX
	{
		static int s_stefxCompressionFxLogBudget = 32;
		if ( s_stefxCompressionFxLogBudget > 0 )
		{
			XBLF("STEFX: FX_CompressionShot start=(%g,%g,%g) end=(%g,%g,%g) spark=%d ring=%d white=%d xboxBeam=%d",
				start[0], start[1], start[2],
				end[0], end[1], end[2],
				cgs.media.sparkShader,
				cgs.media.compressionRingShader,
				cgs.media.whiteLaserShader,
#if defined(STEFX_ELITE_FORCE_SP)
				1
#else
				0
#endif
				);
			s_stefxCompressionFxLogBudget--;
		}
	}
#endif
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	{
		vec3_t core = { 0.85f, 0.95f, 1.0f };
		vec3_t glow = { 0.20f, 0.55f, 1.0f };
		vec3_t delta;
		float len;
		FXLine *glowLine;
		FXLine *coreLine;

		VectorSubtract( start, end, delta );
		len = VectorLength( delta );

		glowLine = FX_AddLine( start, end,
						1.0f,
						5.0f, -2.0f,
						0.95f, 0.0f,
						glow, glow,
						300.0f,
						cgs.media.whiteLaserShader );

		coreLine = FX_AddLine( start, end,
						1.0f,
						2.0f, -0.5f,
						1.0f, 0.0f,
						core, core,
						240.0f,
						cgs.media.sparkShader );

		{
			static int s_stefxCompressionFxLineBudget = 128;
			if ( s_stefxCompressionFxLineBudget > 0 )
			{
				XBLF("STEFX: FX_CompressionShot lines len=%g glowLine=%p coreLine=%p glowWidth=5->3 glowAlpha=0.95->0 glowKill=300 glowShader=%d coreWidth=2->1.5 coreAlpha=1->0 coreKill=240 coreShader=%d startIsImpact=1 endIsMuzzle=1",
					len,
					glowLine,
					coreLine,
					cgs.media.whiteLaserShader,
					cgs.media.sparkShader);
				s_stefxCompressionFxLineBudget--;
			}
		}
	}
#else
	FX_AddLine( start, end, 
					1.0f, 
					2.0f, 0.0f, 
					1.0f, 0.0f, 
					100.0f, 
					cgs.media.sparkShader );
#endif

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	{
		static int s_stefxCompressionRingSkipBudget = 16;
		if ( s_stefxCompressionRingSkipBudget > 0 )
		{
			XBLF("STEFX: FX_CompressionShot skip shock ring xbox alpha path start=(%g,%g,%g) end=(%g,%g,%g)",
				start[0], start[1], start[2],
				end[0], end[1], end[2]);
			--s_stefxCompressionRingSkipBudget;
		}
	}
#else
	FX_CompressionShockRing( start, end );
#endif
}

/*
-------------------------
FX_CompressionShot
-------------------------
*/
void FX_CompressionAltShot( vec3_t start, vec3_t end )
{
	float length = 20;
	vec3_t orange = {1.0f,0.7f,0.3f}, white = {1.0,1.0,1.0};
	vec3_t dir, vec2;

	FX_AddLine( start, end, 
					1.0f, 
					1.5f, 2.0f, 
					1.0f, 0.2f, 
					orange, orange,
					275.0f, 
					cgs.media.whiteLaserShader );

	FX_AddLine( start, end, 
					1.0f, 
					1.0f, -1.0f, 
					1.0f, 0.2f, 
					white, white,
					200.0f, 
					cgs.media.whiteLaserShader );

	FX_AddLine( start, end, 
					1.0f, 
					4.0f, 0.0f, 
					1.0f, 0.2f, 
					250.0f, 
					cgs.media.orangeParticleShader );

	VectorSubtract( start, end, dir );
	VectorNormalize( dir );

	FXCylinder *fx = FX_AddCylinder( end, dir, length, 0, 5, 4, 10, 64,
						0.4f, 0.0, 300, cgs.media.compressionAltBlastShader );

	if ( fx )
	{
		fx->SetFlags( FXF_WRAP );
		fx->SetSTScale( 1.0f );
	}

	VectorMA( end, length, dir, vec2 );

	fx = FX_AddCylinder( vec2, dir, length * 0.5f, 0, 10, 64, 5, 4,
						0.4f, 0.0, 300, cgs.media.compressionAltBlastShader );
	if ( fx )
	{
		fx->SetFlags( FXF_WRAP );
		fx->SetSTScale( 1.0f );
	}

}

/*
-------------------------
FX_CompressionExplosion
-------------------------
*/

void FX_CompressionExplosion( vec3_t origin, vec3_t normal )
{
	localEntity_t	*le;
	FXTrail			*particle;
	vec3_t			direction, new_org;
	vec3_t			velocity		=	{ 0, 0, 8 };
	float			scale, dscale;
	int				i, numSparks;

	//Sparks
	numSparks = 4 + (random() * 4.0f);
#ifdef _XBOX
	{
		static int s_stefxCompressionExplosionBudget = 96;
		if ( s_stefxCompressionExplosionBudget > 0 )
		{
			XBLF("STEFX: FX_CompressionExplosion origin=(%g,%g,%g) normal=(%g,%g,%g) sparks=%d markShader=%d explosionShader=%d",
				origin[0], origin[1], origin[2],
				normal[0], normal[1], normal[2],
				numSparks,
				cgs.media.compressionMarkShader,
				cgs.media.electricalExplosionSlowShader);
			s_stefxCompressionExplosionBudget--;
		}
	}
#endif

	for ( i = 0; i < numSparks; i++ )
	{	
		scale = 0.25f + (random() * 2.0f);
		dscale = -scale * 0.5f;

		particle = FX_AddTrail( origin,
								NULL,
								NULL,
								16.0f,
								-32.0f,
								scale,
								-scale,
								1.0f,
								1.0f,
								0.25f,
								2000.0f,
								cgs.media.sparkShader,
								rand() & FXF_BOUNCE );

		if ( particle == NULL )
			return;

		FXE_Spray( normal, 200, 50, 0.5f, 512, (FXPrimitive *) particle );
	}

	// Smoke puff
	VectorMA( origin, 8, normal, new_org );

	FX_AddSprite( new_org, 
					velocity, NULL, 
					16.0f, 16.0f,
					1.0f, 0.0f,
					random()*45.0f,
					0.0f,
					1000.0f,
					cgs.media.steamShader );

	scale = 0.4f + ( random() * 0.2f);

	//Orient the explosions to face the camera
	VectorSubtract( cg.refdef.vieworg, origin, direction );
	VectorNormalize( direction );

	// Add in the explosion and tag it with a light
	le = CG_MakeExplosion( origin, direction, cgs.media.explosionModel, 6, cgs.media.electricalExplosionSlowShader, 475, qfalse, scale );
	le->light = 150;
	le->refEntity.renderfx |= RF_NOSHADOW;
	VectorSet( le->lightColor, 0.8f, 0.8f, 1.0f );

	// Scorch mark
	CG_ImpactMark( cgs.media.compressionMarkShader, origin, normal, random()*360, 1,1,1,1, qfalse, 4, qfalse );

	CG_ExplosionEffects( origin, 1.0f, 150 );
}

/*
-------------------------
FX_CompressionHit
-------------------------
*/

void FX_CompressionHit( vec3_t origin )
{
#ifdef _XBOX
	{
		static int s_stefxCompressionHitBudget = 96;
		if ( s_stefxCompressionHitBudget > 0 )
		{
			XBLF("STEFX: FX_CompressionHit origin=(%g,%g,%g) shader=%d spriteScale=32->-128 kill=250",
				origin[0], origin[1], origin[2],
				cgs.media.prifleImpactShader);
			s_stefxCompressionHitBudget--;
		}
	}
#endif
	FX_AddSprite( origin,
					NULL,
					NULL,
					32.0f,
					-128.0f,
					1.0f,
					1.0f,
					random()*360,
					0.0f,
					250.0f,
					cgs.media.prifleImpactShader );
}

/*
-------------------------
FX_CompressionAltMiss
-------------------------
*/

void FX_CompressionAltMiss( vec3_t origin, vec3_t normal )
{
	vec3_t	new_org;
	vec3_t	velocity;
	float	scale, dscale;
	int		i;

#ifdef _XBOX
	{
		static int s_stefxCompressionAltMissBudget = 32;
		if ( s_stefxCompressionAltMissBudget > 0 )
		{
			XBLF("STEFX: FX_CompressionAltMiss origin=(%g,%g,%g) normal=(%g,%g,%g) smokePuffs=8",
				origin[0], origin[1], origin[2],
				normal[0], normal[1], normal[2]);
			s_stefxCompressionAltMissBudget--;
		}
	}
#endif

	// Smoke puffs
	for ( i = 0; i < 8; i++ )
	{
		scale = random() * 12.0f + 4.0f;
		dscale = random() * 12.0f + 8.0f;

		VectorMA( origin, random() * 8.0f, normal, new_org );
		
		velocity[0] = normal[0] * crandom() * 24.0f;
		velocity[1] = normal[1] * crandom() * 24.0f;
		velocity[2] = normal[2] * ( random() * 24.0f + 8.0f ) + 8.0f; // move mostly up

		FX_AddSprite( new_org, 
					velocity, NULL, 
					scale, dscale,
					random() * 0.5f + 0.5f, 0.0f,
					random() * 45.0f,
					0.0f,
					800 + random() * 300.0f,
					cgs.media.steamShader );
	}

	// Scorch mark
	CG_ImpactMark( cgs.media.bulletmarksShader, origin, normal, random()*360, 1,1,1,1, qfalse, 6, qfalse );
	FX_CompressionHit( origin );

	CG_ExplosionEffects( origin, 1.0f, 150 );
}
