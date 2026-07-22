// tr_shade.c

// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"

#include "tr_local.h"

#ifdef VV_LIGHTING
#include "tr_lightmanager.h"
#include "../win32/glw_win_dx8.h"
#include "../win32/win_lighteffects.h"
#endif

#ifdef _XBOX
#include "../win32/xb_log.h"
#ifndef VV_LIGHTING
#include "../win32/glw_win_dx8.h"
#endif
extern "C" volatile unsigned int g_SPXBRenderEndSurfaces;
#if defined(STEFX_ELITE_FORCE_SP)
extern "C" volatile unsigned int g_SPXBFallbackTraceMagic;
extern "C" volatile unsigned int g_SPXBFallbackStageCount;
extern "C" volatile unsigned int g_SPXBFallbackLastShaderHash;
extern "C" volatile unsigned int g_SPXBFallbackLastImageHash;
extern "C" volatile unsigned int g_SPXBFallbackLastStage;
extern "C" volatile unsigned int g_SPXBFallbackLastPasses;
extern "C" volatile unsigned int g_SPXBFallbackLastFlags;
extern "C" volatile unsigned int g_SPXBFallbackLastTexnum;
extern "C" volatile unsigned int g_SPXBFallbackLastLightmap;
extern "C" volatile unsigned int g_SPXBFallbackLastStateBits;
extern "C" volatile unsigned int g_SPXBFallbackLastIndexes;
extern "C" volatile unsigned int g_SPXBFallbackLastX1000;
extern "C" volatile unsigned int g_SPXBFallbackLastY1000;
extern "C" volatile unsigned int g_SPXBFallbackLastZ1000;
#endif
extern "C" void JkaFakeglSetEliteForceOverlayDrawContext(int active, int hud, int beam);

static const char *RB_XboxImageLogName( const image_t *image )
{
	if ( !image )
	{
		return "<null>";
	}
#ifndef FINAL_BUILD
	if ( !image->imgName[0] )
	{
		return "<unnamed>";
	}
	for ( int i = 0; i < MAX_QPATH && image->imgName[i]; ++i )
	{
		const unsigned char c = (unsigned char)image->imgName[i];
		if ( c < 32 || c > 126 )
		{
			return "<nonascii>";
		}
	}
	return image->imgName;
#else
	return "<image>";
#endif
}

static qboolean RB_XboxIsEliteForceHudShader( const shader_t *shader )
{
	const char *name = shader ? shader->name : "";

	if ( !name )
	{
		name = "";
	}

	return strstr( name, "gfx/interface/" ) ||
		strstr( name, "crosshair" );
}

static qboolean RB_XboxIsEliteForceBeamShader( const shader_t *shader )
{
	const char *name = shader ? shader->name : "";

	if ( !name )
	{
		name = "";
	}

	return !Q_stricmp( name, "gfx/effects/whitelaser" ) ||
		!Q_stricmp( name, "gfx/misc/spark" );
}

static qboolean RB_XboxIsEliteForceIntroShader( const shader_t *shader )
{
	const char *name = shader ? shader->name : "";

	return !Q_stricmp( name, "textures/common/70yearjourney" ) ||
		!Q_stricmp( name, "textures/common/enemyspace" ) ||
		!Q_stricmp( name, "textures/common/sevenspace" ) ||
		!Q_stricmp( name, "textures/common/tuvokhazard" );
}

static void RB_XboxLogEliteForceIntroDraw( const char *where )
{
	static int s_stefxIntroDraw70YearBudget = 12;
	static int s_stefxIntroDrawEnemyBudget = 12;
	static int s_stefxIntroDrawTuvokBudget = 12;
	static int s_stefxIntroDrawSevenBudget = 12;
	int *budget = NULL;
	const shaderStage_t *stage = NULL;
	const image_t *image = NULL;

	if ( !RB_XboxIsEliteForceIntroShader( tess.shader ) )
	{
		return;
	}

	if ( !Q_stricmp( tess.shader->name, "textures/common/70yearjourney" ) )
	{
		budget = &s_stefxIntroDraw70YearBudget;
	}
	else if ( !Q_stricmp( tess.shader->name, "textures/common/enemyspace" ) )
	{
		budget = &s_stefxIntroDrawEnemyBudget;
	}
	else if ( !Q_stricmp( tess.shader->name, "textures/common/tuvokhazard" ) )
	{
		budget = &s_stefxIntroDrawTuvokBudget;
	}
	else if ( !Q_stricmp( tess.shader->name, "textures/common/sevenspace" ) )
	{
		budget = &s_stefxIntroDrawSevenBudget;
	}

	if ( !budget || *budget <= 0 )
	{
		return;
	}

	if ( tess.shader && tess.shader->numUnfoggedPasses > 0 )
	{
		stage = &tess.shader->stages[0];
		image = stage->bundle[0].image;
	}

	XBLF( "STEFX: INTRO_DRAW where=%s shader='%s' img='%s' tex=%d verts=%d indexes=%d passes=%d fog=%d xyz0=(%g,%g,%g) st0=(%g,%g) color0=0x%08lx",
		where ? where : "<null>",
		tess.shader ? tess.shader->name : "<null>",
		RB_XboxImageLogName( image ),
		image ? image->texnum : -1,
		tess.numVertexes,
		tess.numIndexes,
		tess.shader ? tess.shader->numUnfoggedPasses : -1,
		tess.fogNum,
		tess.numVertexes > 0 ? tess.xyz[0][0] : 0.0f,
		tess.numVertexes > 0 ? tess.xyz[0][1] : 0.0f,
		tess.numVertexes > 0 ? tess.xyz[0][2] : 0.0f,
		tess.numVertexes > 0 ? tess.svars.texcoords[0][0][0] : 0.0f,
		tess.numVertexes > 0 ? tess.svars.texcoords[0][0][1] : 0.0f,
		tess.numVertexes > 0 ? (unsigned long)tess.svars.colors[0] : 0 );
	--*budget;
}

static qboolean RB_XboxIsEliteForceScriptPanelDraw( void )
{
	trRefEntity_t *ent = backEnd.currentEntity;

	if ( backEnd.projection2D || cls.state != CA_ACTIVE || !ent || ent == &tr.worldEntity )
	{
		return qfalse;
	}

	if ( ent->e.reType != RT_MODEL || !RB_XboxIsEliteForceIntroShader( tess.shader ) )
	{
		return qfalse;
	}

	return qtrue;
}

static int RB_XboxEliteForceScriptPanelCullType( int cullType )
{
	static int s_stefxScriptPanelCullBudget = 96;

	if ( !RB_XboxIsEliteForceScriptPanelDraw() )
	{
		return cullType;
	}

	if ( s_stefxScriptPanelCullBudget > 0 )
	{
		XBLF( "STEFX_SCRIPT_PANEL_CULL shader='%s' ent=%d hModel=%d oldCull=%d newCull=%d verts=%d indexes=%d",
			tess.shader ? tess.shader->name : "<null>",
			backEnd.currentEntity ? backEnd.currentEntity->e.number : -1,
			backEnd.currentEntity ? backEnd.currentEntity->e.hModel : -1,
			cullType,
			CT_TWO_SIDED,
			tess.numVertexes,
			tess.numIndexes );
		--s_stefxScriptPanelCullBudget;
	}

	return CT_TWO_SIDED;
}

static int RB_XboxAdjustEliteForceScriptPanelState( const shaderStage_t *stage, int stateBits, const char *where )
{
	static int s_stefxScriptPanelStateBudget = 192;
	int oldStateBits = stateBits;
	const image_t *img0 = stage ? stage->bundle[0].image : NULL;
	const image_t *img1 = stage ? stage->bundle[1].image : NULL;

	if ( !RB_XboxIsEliteForceScriptPanelDraw() )
	{
		return stateBits;
	}

	/*
	 * Elite Force drives several in-world briefing/cinematic images as
	 * func_usable inline brush-model panels that are exactly coplanar with
	 * textures/common/black backing faces in borg1.  The PC GL path tolerates
	 * that overlap; on Xbox the fakegl/D3D path still lets the opaque backing
	 * win, so draw these scripted panels as overlays while keeping depth writes
	 * off.  This is limited to the known EF scripted display shaders.
	 */
	stateBits &= ~( GLS_DEPTHFUNC_EQUAL | GLS_DEPTHMASK_TRUE );
	stateBits |= GLS_DEPTHTEST_DISABLE;

	if ( s_stefxScriptPanelStateBudget > 0 )
	{
		XBLF( "STEFX_SCRIPT_PANEL_STATE where=%s shader='%s' ent=%d hModel=%d stageActive=%d old=0x%x new=0x%x depthOff=%d depthEqual=%d depthMask=%d blend=0x%x verts=%d indexes=%d img0='%s' tex0=%d img1='%s' tex1=%d xyz0=(%g,%g,%g) st0=(%g,%g) st1=(%g,%g)",
			where ? where : "<null>",
			tess.shader ? tess.shader->name : "<null>",
			backEnd.currentEntity ? backEnd.currentEntity->e.number : -1,
			backEnd.currentEntity ? backEnd.currentEntity->e.hModel : -1,
			stage ? stage->active : -1,
			oldStateBits,
			stateBits,
			(int)((stateBits & GLS_DEPTHTEST_DISABLE) != 0),
			(int)((stateBits & GLS_DEPTHFUNC_EQUAL) != 0),
			(int)((stateBits & GLS_DEPTHMASK_TRUE) != 0),
			(int)(stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS )),
			tess.numVertexes,
			tess.numIndexes,
			RB_XboxImageLogName( img0 ),
			img0 ? img0->texnum : -1,
			RB_XboxImageLogName( img1 ),
			img1 ? img1->texnum : -1,
			tess.numVertexes > 0 ? tess.xyz[0][0] : 0.0f,
			tess.numVertexes > 0 ? tess.xyz[0][1] : 0.0f,
			tess.numVertexes > 0 ? tess.xyz[0][2] : 0.0f,
			tess.numVertexes > 0 ? tess.svars.texcoords[0][0][0] : 0.0f,
			tess.numVertexes > 0 ? tess.svars.texcoords[0][0][1] : 0.0f,
			tess.numVertexes > 0 ? tess.svars.texcoords[1][0][0] : 0.0f,
			tess.numVertexes > 0 ? tess.svars.texcoords[1][0][1] : 0.0f );
		--s_stefxScriptPanelStateBudget;
	}

	return stateBits;
}

static void RB_XboxBeginEliteForceScriptPanelFakeglState( const shaderStage_t *stage, const char *where )
{
	static int s_stefxScriptPanelFakeglBudget = 192;

	if ( !RB_XboxIsEliteForceScriptPanelDraw() )
	{
		return;
	}

	JkaFakeglSetEliteForceScriptPanelDrawContext( 1 );

	if ( s_stefxScriptPanelFakeglBudget > 0 )
	{
		const image_t *img0 = stage ? stage->bundle[0].image : NULL;
		XBLF( "STEFX_SCRIPT_PANEL_FAKEGL_BEGIN where=%s shader='%s' ent=%d hModel=%d img0='%s' tex0=%d verts=%d indexes=%d",
			where ? where : "<null>",
			tess.shader ? tess.shader->name : "<null>",
			backEnd.currentEntity ? backEnd.currentEntity->e.number : -1,
			backEnd.currentEntity ? backEnd.currentEntity->e.hModel : -1,
			RB_XboxImageLogName( img0 ),
			img0 ? img0->texnum : -1,
			tess.numVertexes,
			tess.numIndexes );
		--s_stefxScriptPanelFakeglBudget;
	}
}

static void RB_XboxEndEliteForceScriptPanelFakeglState( const char *where )
{
	static int s_stefxScriptPanelFakeglEndBudget = 96;

	if ( !RB_XboxIsEliteForceScriptPanelDraw() )
	{
		return;
	}

	JkaFakeglSetEliteForceScriptPanelDrawContext( 0 );

	if ( s_stefxScriptPanelFakeglEndBudget > 0 )
	{
		XBLF( "STEFX_SCRIPT_PANEL_FAKEGL_END where=%s shader='%s' ent=%d hModel=%d",
			where ? where : "<null>",
			tess.shader ? tess.shader->name : "<null>",
			backEnd.currentEntity ? backEnd.currentEntity->e.number : -1,
			backEnd.currentEntity ? backEnd.currentEntity->e.hModel : -1 );
		--s_stefxScriptPanelFakeglEndBudget;
	}
}

static void RB_XboxForceEliteForceOverlayD3DState( const shader_t *shader, qboolean additive, const char *where )
{
	static int s_stefxForceOverlayLogBudget = 160;
	static int s_stefxForceOverlaySkipBudget = 16;

	if ( !glw_state || !glw_state->device )
	{
		if ( cls.state == CA_ACTIVE && s_stefxForceOverlaySkipBudget > 0 )
		{
			XBLF( "STEFX: RB_ForceOverlayD3D skipped where=%s shader='%s' glw=%p device=%p projection2D=%d frame=%d",
				where ? where : "<null>", shader ? shader->name : "<null>",
				glw_state, glw_state ? glw_state->device : NULL,
				backEnd.projection2D, tr.frameCount );
			--s_stefxForceOverlaySkipBudget;
		}
		return;
	}

	glw_state->device->SetRenderState( D3DRS_ZENABLE, D3DZB_FALSE );
	glw_state->device->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
	glw_state->device->SetRenderState( D3DRS_ALPHATESTENABLE, FALSE );
	glw_state->device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
	glw_state->device->SetRenderState( D3DRS_SRCBLEND, additive ? D3DBLEND_ONE : D3DBLEND_SRCALPHA );
	glw_state->device->SetRenderState( D3DRS_DESTBLEND, additive ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA );
	glw_state->device->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );

	if ( cls.state == CA_ACTIVE && s_stefxForceOverlayLogBudget > 0 )
	{
		XBLF( "STEFX: RB_ForceOverlayD3D where=%s shader='%s' additive=%d projection2D=%d frame=%d",
			where ? where : "<null>", shader ? shader->name : "<null>",
			additive ? 1 : 0, backEnd.projection2D, tr.frameCount );
		--s_stefxForceOverlayLogBudget;
	}
}

static void RB_XboxPrepareEliteForceOverlayStage( const shaderStage_t *stage, qboolean additive, const char *where )
{
	static int s_stefxPrepareOverlayBudget = 160;
	const image_t *image = stage ? stage->bundle[0].image : NULL;

	GL_SelectTexture( 1 );
	glDisable( GL_TEXTURE_2D );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 0 );
	glEnable( GL_TEXTURE_2D );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );
	glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

	if ( additive )
	{
		glBlendFunc( GL_ONE, GL_ONE );
	}
	else
	{
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	}
	glEnable( GL_BLEND );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );

	if ( cls.state == CA_ACTIVE && s_stefxPrepareOverlayBudget > 0 )
	{
		XBLF( "STEFX: RB_PrepareOverlayStage where=%s shader='%s' img='%s' tex=%d additive=%d projection2D=%d verts=%d indexes=%d",
			where ? where : "<null>", tess.shader ? tess.shader->name : "<null>",
			image ? image->imgName : "<null>", image ? image->texnum : -1,
			additive ? 1 : 0, backEnd.projection2D,
			tess.numVertexes, tess.numIndexes );
		--s_stefxPrepareOverlayBudget;
	}
}

static void RB_XboxLogEliteForceOverlayDraw( const shaderStage_t *stage, qboolean hud, qboolean beam, const char *where )
{
	static int s_stefxOverlayDrawBudget = 192;

	if ( cls.state == CA_ACTIVE && s_stefxOverlayDrawBudget > 0 )
	{
		const image_t *image = stage ? stage->bundle[0].image : NULL;

		XBLF( "EF: OVERLAY_DRAW_SUBMIT where=%s shader='%s' img='%s' hud=%d beam=%d projection2D=%d verts=%d indexes=%d state=0x%x",
			where ? where : "<null>",
			tess.shader ? tess.shader->name : "<null>",
			image ? image->imgName : "<null>",
			hud ? 1 : 0,
			beam ? 1 : 0,
			backEnd.projection2D,
			tess.numVertexes,
			tess.numIndexes,
			stage ? stage->stateBits : 0 );
		--s_stefxOverlayDrawBudget;
	}
}

static qboolean RB_XboxIsEliteForceLegacyMaskedWorldOverlayShader( const shader_t *shader )
{
	const char *name = shader ? shader->name : NULL;

	if ( !name )
	{
		return qfalse;
	}

	return !Q_stricmp( name, "textures/borg/bigborg" ) ||
		!Q_stricmp( name, "textures/borg/oddlight1" );
}

static int RB_XboxAdjustEliteForceLegacyMaskedWorldOverlayState( const shaderStage_t *stage, int stateBits, int stageIndex, const char *where )
{
	static int s_stefxLegacyMaskedOverlayBudget = 96;
	const int blendBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
	const shader_t *shader = tess.shader;
	const image_t *image = stage ? stage->bundle[0].image : NULL;

	if ( backEnd.projection2D || cls.state != CA_ACTIVE || !stage ||
		backEnd.currentEntity != &tr.worldEntity ||
		!RB_XboxIsEliteForceLegacyMaskedWorldOverlayShader( shader ) )
	{
		return stateBits;
	}

	if ( blendBits != ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_SRC_ALPHA ) )
	{
		return stateBits;
	}

	stateBits &= ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
	stateBits |= GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;

	if ( s_stefxLegacyMaskedOverlayBudget > 0 )
	{
		XBLF( "STEFX_DRAW_STAGE_BLEND_FIX where=%s shader='%s' stage=%d img='%s' old=0x%x new=0x%x verts=%d indexes=%d",
			where ? where : "<null>",
			shader ? shader->name : "<null>",
			stageIndex,
			RB_XboxImageLogName( image ),
			blendBits,
			(int)( stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ),
			tess.numVertexes,
			tess.numIndexes );
		--s_stefxLegacyMaskedOverlayBudget;
	}

	return stateBits;
}
#endif

/*

  THIS ENTIRE FILE IS BACK END

  This file deals with applying shaders to surface data in the tess struct.
*/

shaderCommands_t	tess;
static qboolean	setArraysOnce;

color4ub_t	styleColors[MAX_LIGHT_STYLES];
bool		styleUpdated[MAX_LIGHT_STYLES];

extern bool g_bRenderGlowingObjects;

/*
================
R_ArrayElementDiscrete

This is just for OpenGL conformance testing, it should never be the fastest
================
*/
static void APIENTRY R_ArrayElementDiscrete( GLint index ) {
#ifndef _XBOX
	glColor4ubv( tess.svars.colors[ index ] );
	if ( glState.currenttmu ) {
		glMultiTexCoord2fARB( 0, tess.svars.texcoords[ 0 ][ index ][0], tess.svars.texcoords[ 0 ][ index ][1] );
		glMultiTexCoord2fARB( 1, tess.svars.texcoords[ 1 ][ index ][0], tess.svars.texcoords[ 1 ][ index ][1] );
	} else {
		glTexCoord2fv( tess.svars.texcoords[ 0 ][ index ] );
	}
	glVertex3fv( tess.xyz[ index ] );
#endif
}

/*
===================
R_DrawStripElements

===================
*/
static int		c_vertexes;		// for seeing how long our average strips are
static int		c_begins;
static void R_DrawStripElements( int numIndexes, const glIndex_t *indexes, void ( APIENTRY *element )(GLint) ) {
	int i;
	glIndex_t last[3];
	qboolean even;

	glBegin( GL_TRIANGLE_STRIP );
	c_begins++;

	if ( numIndexes <= 0 ) {
		return;
	}

	// prime the strip
	element( indexes[0] );
	element( indexes[1] );
	element( indexes[2] );
	c_vertexes += 3;

	last[0] = indexes[0];
	last[1] = indexes[1];
	last[2] = indexes[2];

	even = qfalse;

	for ( i = 3; i < numIndexes; i += 3 )
	{
		// odd numbered triangle in potential strip
		if ( !even )
		{
			// check previous triangle to see if we're continuing a strip
			if ( ( indexes[i+0] == last[2] ) && ( indexes[i+1] == last[1] ) )
			{
				element( indexes[i+2] );
				c_vertexes++;
				assert( indexes[i+2] < tess.numVertexes );
				even = qtrue;
			}
			// otherwise we're done with this strip so finish it and start
			// a new one
			else
			{
				glEnd();

				glBegin( GL_TRIANGLE_STRIP );
				c_begins++;

				element( indexes[i+0] );
				element( indexes[i+1] );
				element( indexes[i+2] );

				c_vertexes += 3;

				even = qfalse;
			}
		}
		else
		{
			// check previous triangle to see if we're continuing a strip
			if ( ( last[2] == indexes[i+1] ) && ( last[0] == indexes[i+0] ) )
			{
				element( indexes[i+2] );
				c_vertexes++;

				even = qfalse;
			}
			// otherwise we're done with this strip so finish it and start
			// a new one
			else
			{
				glEnd();

				glBegin( GL_TRIANGLE_STRIP );
				c_begins++;

				element( indexes[i+0] );
				element( indexes[i+1] );
				element( indexes[i+2] );
				c_vertexes += 3;

				even = qfalse;
			}
		}

		// cache the last three vertices
		last[0] = indexes[i+0];
		last[1] = indexes[i+1];
		last[2] = indexes[i+2];
	}

	glEnd();
}

#ifdef _XBOX
qboolean RB_IsCurrentShaderTransparent( void );
extern "C" void JkaFakeglSetEliteForceDrawContext(const char *shader, int stage, int expectedStages, unsigned int stateBits);

static qboolean RB_XboxShouldTraceSurface( void )
{
	const char *name;

	if ( cls.state != CA_ACTIVE || !tess.shader || !tess.shader->name )
	{
		return qfalse;
	}

	name = tess.shader->name;
	if ( !Q_stricmp( name, "textures/common/black" ) ||
		!Q_stricmp( name, "textures/borg/static2" ) ||
		!Q_stricmp( name, "textures/borg/static2_nonsolid" ) ||
		!Q_stricmp( name, "textures/borg/borgfield" ) ||
		!Q_stricmp( name, "textures/borg/borgfield_nonsolid" ) ||
		!Q_stricmp( name, "textures/borg/borgfield_opaque" ) ||
		!Q_stricmp( name, "textures/borg/energy1" ) ||
		!Q_stricmp( name, "textures/borg/energy1_solid" ) ||
		!Q_stricmp( name, "textures/borg/energy1_green" ) ||
		!Q_stricmp( name, "textures/borg/bars" ) ||
		!Q_stricmp( name, "textures/borg/bars2" ) ||
		!Q_stricmp( name, "textures/borg/basic1" ) ||
		!Q_stricmp( name, "textures/borg/borgladder" ) ||
		!Q_stricmp( name, "textures/borg/bigborg" ) ||
		!Q_stricmp( name, "textures/borg/oddlight1" ) ||
		!Q_stricmp( name, "textures/common/sky_light" ) ||
		!Q_stricmp( name, "textures/common/junk_sky" ) ||
		!Q_stricmp( name, "textures/scavenger/m_wallgrid" ) ||
		!Q_stricmp( name, "textures/scavenger/k_control_portal" ) ||
		!Q_stricmp( name, "textures/engineering/glass_nolightmap" ) ||
		!Q_stricmp( name, "textures/engineering/glass_nolightmap_nonsolid" ) ||
		!Q_stricmp( name, "textures/common/portal" ) ||
		strstr( name, "models/players/" ) )
	{
		return qtrue;
	}

	return qfalse;
#if 0
	if (cls.state == CA_ACTIVE)
	{
		return qfalse;
	}
	if (tess.shader && tess.shader->name && strstr(tess.shader->name, "textures/taspir/trim"))
	{
		return qtrue;
	}
	if (tess.shader && tess.shader->name &&
		(strstr(tess.shader->name, "models/players/jedi_tf") ||
		 strstr(tess.shader->name, "models/players/alora") ||
		 strstr(tess.shader->name, "models/players/alora2")))
	{
		return qtrue;
	}
	return qfalse;
#endif
}

static qboolean RB_XboxForceTraceSurface( void )
{
	return qfalse;
#if 0
	if (cls.state == CA_ACTIVE)
	{
		return qfalse;
	}
	if (tess.shader && tess.shader->name && strstr(tess.shader->name, "textures/taspir/trim"))
	{
		return qtrue;
	}
	if (tess.shader && tess.shader->name &&
		(strstr(tess.shader->name, "models/players/jedi_tf") ||
		 strstr(tess.shader->name, "models/players/alora") ||
		 strstr(tess.shader->name, "models/players/alora2")))
	{
		return qtrue;
	}
	return qfalse;
#endif
}

static void RB_XboxLogWorldDrawStage( const char *where, shaderCommands_t *input, const shaderStage_t *stage, int stageNum, int stateBits )
{
	static int s_stefxWorldDrawStageBudget = 512;
	static int s_stefxDrawContextCallBudget = 24;
	const image_t *img0;
	const image_t *img1;
	unsigned long color0;

	if ( backEnd.projection2D || cls.state != CA_ACTIVE || !input || !stage || !tess.shader )
	{
		return;
	}
	if ( !RB_XboxShouldTraceSurface() )
	{
		return;
	}
	if ( s_stefxWorldDrawStageBudget <= 0 )
	{
		return;
	}

	img0 = stage->bundle[0].image;
	img1 = stage->bundle[1].image;
	color0 = input->numVertexes > 0 ? (unsigned long)input->svars.colors[0] : 0;

	if ( s_stefxDrawContextCallBudget > 0 )
	{
		XBLF("STEFX_DRAW_CONTEXT_CALL where=%s shader='%s' stage=%d passes=%d state=0x%x verts=%d indexes=%d",
			where ? where : "<null>",
			tess.shader->name,
			stageNum,
			tess.shader ? tess.shader->numUnfoggedPasses : 0,
			stateBits,
			input->numVertexes,
			input->numIndexes);
		--s_stefxDrawContextCallBudget;
	}

	JkaFakeglSetEliteForceDrawContext( tess.shader->name,
		stageNum,
		tess.shader ? tess.shader->numUnfoggedPasses : 0,
		(unsigned int)stateBits );

	XBLF("STEFX_DRAW_STAGE where=%s shader='%s' stage=%d passes=%d verts=%d indexes=%d fog=%d ent=%p reType=%d state=0x%x sort=%g default=%d explicit=%d sky=%d cull=%d env=%d rgb=%d alpha=%d color0=0x%08lx img0='%s' tex0=%d lm0=%d vtxlm0=%d tc0=%d img1='%s' tex1=%d lm1=%d vtxlm1=%d tc1=%d st0=%g,%g st1=%g,%g xyz0=%g,%g,%g",
		where ? where : "<null>",
		tess.shader->name,
		stageNum,
		tess.shader ? tess.shader->numUnfoggedPasses : -1,
		input->numVertexes,
		input->numIndexes,
		tess.fogNum,
		backEnd.currentEntity,
		backEnd.currentEntity ? backEnd.currentEntity->e.reType : -1,
		stateBits,
		tess.shader ? (double)tess.shader->sort : -1.0,
		tess.shader ? tess.shader->defaultShader : -1,
		tess.shader ? tess.shader->explicitlyDefined : -1,
		(tess.shader && tess.shader->sky) ? 1 : 0,
		tess.shader ? tess.shader->cullType : -1,
		tess.shader ? tess.shader->multitextureEnv : -1,
		stage->rgbGen,
		stage->alphaGen,
		color0,
		RB_XboxImageLogName( img0 ),
		img0 ? img0->texnum : -1,
		stage->bundle[0].isLightmap ? 1 : 0,
		stage->bundle[0].vertexLightmap ? 1 : 0,
		stage->bundle[0].tcGen,
		RB_XboxImageLogName( img1 ),
		img1 ? img1->texnum : -1,
		stage->bundle[1].isLightmap ? 1 : 0,
		stage->bundle[1].vertexLightmap ? 1 : 0,
		stage->bundle[1].tcGen,
		input->numVertexes > 0 ? input->svars.texcoords[0][0][0] : 0.0f,
		input->numVertexes > 0 ? input->svars.texcoords[0][0][1] : 0.0f,
		input->numVertexes > 0 ? input->svars.texcoords[1][0][0] : 0.0f,
		input->numVertexes > 0 ? input->svars.texcoords[1][0][1] : 0.0f,
		input->numVertexes > 0 ? input->xyz[0][0] : 0.0f,
		input->numVertexes > 0 ? input->xyz[0][1] : 0.0f,
		input->numVertexes > 0 ? input->xyz[0][2] : 0.0f);

	--s_stefxWorldDrawStageBudget;
}

static const char *RB_XboxImageName( const image_t *image )
{
	if (!image)
	{
		return "<null>";
	}
#ifndef FINAL_BUILD
	return image->imgName;
#else
	return "<image>";
#endif
}

static qboolean RB_XboxImageLooksFallback( const image_t *image )
{
	if (!image)
	{
		return qtrue;
	}
	if (image == tr.defaultImage || image == tr.whiteImage)
	{
		return qtrue;
	}
	if (!image->imgName)
	{
		return qtrue;
	}
	if (!Q_stricmp(image->imgName, "*default") || !Q_stricmp(image->imgName, "*white"))
	{
		return qtrue;
	}
	return qfalse;
}

#if defined(STEFX_ELITE_FORCE_SP)
static unsigned int RB_XboxHashTraceName( const char *text )
{
	unsigned int hash = 2166136261u;

	if ( !text )
	{
		return 0;
	}

	while ( *text )
	{
		unsigned char c = (unsigned char)*text++;
		if ( c >= 'A' && c <= 'Z' )
		{
			c = (unsigned char)( c + ( 'a' - 'A' ) );
		}
		hash ^= c;
		hash *= 16777619u;
	}

	return hash ? hash : 1u;
}

static void RB_XboxUpdateFallbackStageTelemetry( const shaderCommands_t *input, const shaderStage_t *stage, int stageNum, int stateBits )
{
	static int s_stefxFallbackStageLogBudget = 24;
	const image_t *image0;
	const image_t *image1;
	qboolean fallback0;
	qboolean fallback1;
	const image_t *fallbackImage;
	unsigned int flags;

	if ( backEnd.projection2D || cls.state != CA_ACTIVE || !input || !stage || !tess.shader )
	{
		return;
	}

	image0 = stage->bundle[0].image;
	image1 = stage->bundle[1].image;
	fallback0 = RB_XboxImageLooksFallback( image0 );
	fallback1 = ( image1 && RB_XboxImageLooksFallback( image1 ) ) ? qtrue : qfalse;
	if ( !fallback0 && !fallback1 )
	{
		return;
	}

	fallbackImage = fallback0 ? image0 : image1;
	flags = ( fallback0 ? 1u : 0u ) |
		( fallback1 ? 2u : 0u ) |
		( stage->bundle[0].isLightmap ? 0x10u : 0u ) |
		( stage->bundle[1].isLightmap ? 0x20u : 0u ) |
		( stage->bundle[0].vertexLightmap ? 0x40u : 0u ) |
		( tess.shader->sky ? 0x100u : 0u ) |
		( tess.shader->defaultShader ? 0x200u : 0u ) |
		( tess.shader->explicitlyDefined ? 0x400u : 0u );

	g_SPXBFallbackTraceMagic = 0x46424B21; /* 'FBK!' */
	++g_SPXBFallbackStageCount;
	g_SPXBFallbackLastShaderHash = RB_XboxHashTraceName( tess.shader->name );
	g_SPXBFallbackLastImageHash = RB_XboxHashTraceName( RB_XboxImageLogName( fallbackImage ) );
	g_SPXBFallbackLastStage = (unsigned int)stageNum;
	g_SPXBFallbackLastPasses = (unsigned int)tess.shader->numUnfoggedPasses;
	g_SPXBFallbackLastFlags = flags;
	g_SPXBFallbackLastTexnum = fallbackImage ? (unsigned int)fallbackImage->texnum : 0xffffffffu;
	g_SPXBFallbackLastLightmap = (unsigned int)(
		( stage->bundle[0].isLightmap ? 1 : 0 ) |
		( stage->bundle[1].isLightmap ? 2 : 0 ) |
		( stage->bundle[0].vertexLightmap ? 4 : 0 ) );
	g_SPXBFallbackLastStateBits = (unsigned int)stateBits;
	g_SPXBFallbackLastIndexes = (unsigned int)input->numIndexes;
	g_SPXBFallbackLastX1000 = ( input->numVertexes > 0 ) ? (unsigned int)(int)( input->xyz[0][0] * 1000.0f ) : 0;
	g_SPXBFallbackLastY1000 = ( input->numVertexes > 0 ) ? (unsigned int)(int)( input->xyz[0][1] * 1000.0f ) : 0;
	g_SPXBFallbackLastZ1000 = ( input->numVertexes > 0 ) ? (unsigned int)(int)( input->xyz[0][2] * 1000.0f ) : 0;

	if ( s_stefxFallbackStageLogBudget > 0 )
	{
		XBLF("STEFX_FALLBACK_STAGE shader='%s' image='%s' stage=%d passes=%d flags=0x%x state=0x%x tex=%d indexes=%d xyz=%g,%g,%g",
			tess.shader->name ? tess.shader->name : "<null>",
			RB_XboxImageLogName( fallbackImage ),
			stageNum,
			tess.shader->numUnfoggedPasses,
			flags,
			stateBits,
			fallbackImage ? fallbackImage->texnum : -1,
			input->numIndexes,
			input->numVertexes > 0 ? input->xyz[0][0] : 0.0f,
			input->numVertexes > 0 ? input->xyz[0][1] : 0.0f,
			input->numVertexes > 0 ? input->xyz[0][2] : 0.0f);
		--s_stefxFallbackStageLogBudget;
	}
}
#endif

#if defined(STEFX_ELITE_FORCE_SP)
static void RB_STEFX_ForceNextTextureBind( int unit, const shaderStage_t *stage, const textureBundle_t *bundle )
{
	static int s_forceBindLogBudget = 48;
	image_t *image = bundle ? bundle->image : NULL;

	GL_InvalidateTextureUnit( unit );

	if ( cls.state == CA_ACTIVE && s_forceBindLogBudget > 0 )
	{
		XBLF("STEFX: FORCE_TEXTURE_REBIND unit=%d shader='%s' stage=%d image='%s' tex=%d light=%d",
			unit,
			tess.shader ? tess.shader->name : "<null>",
			stage ? stage->index : -1,
			image ? RB_XboxImageName( image ) : "<null>",
			image ? image->texnum : -1,
			image ? image->isLightmap : -1);
		--s_forceBindLogBudget;
	}
}
#endif

static qboolean RB_XboxStageLooksRenderSuspect( const shaderStage_t *stage )
{
	if (!stage || !stage->active)
	{
		return qfalse;
	}

	if (stage->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_ATEST_BITS))
	{
		return qtrue;
	}
	if (stage->bundle[0].tcGen == TCGEN_FOG || stage->bundle[1].tcGen == TCGEN_FOG)
	{
		return qtrue;
	}
	if (RB_XboxImageLooksFallback(stage->bundle[0].image) ||
		(stage->bundle[1].image && RB_XboxImageLooksFallback(stage->bundle[1].image)))
	{
		return qtrue;
	}
	if (strstr(RB_XboxImageName(stage->bundle[0].image), "fog") ||
		strstr(RB_XboxImageName(stage->bundle[1].image), "fog"))
	{
		return qtrue;
	}

	return qfalse;
}

static qboolean RB_XboxIsRenderSuspectSurface( const shader_t *shader )
{
	int i;

	if (!shader)
	{
		return qfalse;
	}

	if (shader->sky || shader->fogParms || (shader->fogPass && tess.fogNum))
	{
		return qtrue;
	}

	if (strstr(shader->name, "textures/fogs") ||
		strstr(shader->name, "sky") ||
		strstr(shader->name, "portal"))
	{
		return qtrue;
	}

	for (i = 0; i < shader->numUnfoggedPasses; ++i)
	{
		if (RB_XboxStageLooksRenderSuspect(&shader->stages[i]))
		{
			return qtrue;
		}
	}

	return qfalse;
}

static void RB_XboxLogRenderSuspectSurface( const char *where )
{
	static int suspectBudget = 0;
	const shader_t *shader = tess.shader;
	int i;

	if (cls.state != CA_ACTIVE || suspectBudget <= 0 || !RB_XboxIsRenderSuspectSurface(shader))
	{
		return;
	}

	XBLF("JA: RENDER_SUSPECT %s shader='%s' sky=%d fogPass=%d fogNum=%d sort=%g cull=%d surf=0x%x cont=0x%x passes=%d verts=%d indexes=%d dlight=0x%x",
		where,
		shader ? shader->name : "<null>",
		(int)(shader && shader->sky != NULL),
		shader ? (int)shader->fogPass : -1,
		tess.fogNum,
		shader ? shader->sort : 0.0f,
		shader ? (int)shader->cullType : -1,
		shader ? shader->surfaceFlags : 0,
		shader ? shader->contentFlags : 0,
		shader ? shader->numUnfoggedPasses : -1,
		tess.numVertexes,
		tess.numIndexes,
		tess.dlightBits);
	--suspectBudget;

	if (!shader)
	{
		return;
	}

	for (i = 0; i < shader->numUnfoggedPasses && i < 4 && suspectBudget > 0; ++i)
	{
		const shaderStage_t *stage = &shader->stages[i];
		if (!RB_XboxStageLooksRenderSuspect(stage) && !shader->sky && !shader->fogPass && !shader->fogParms)
		{
			continue;
		}
		XBLF("JA: RENDER_SUSPECT_STAGE shader='%s' stage=%d active=%d state=0x%x blend=0x%x atest=0x%x depthEq=%d depthOff=%d rgb=%d alpha=%d tc0=%d tc1=%d img0='%s' tex0=%d light0=%d fallback0=%d img1='%s' tex1=%d light1=%d fallback1=%d",
			shader->name,
			i,
			(int)stage->active,
			stage->stateBits,
			(int)(stage->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)),
			(int)(stage->stateBits & GLS_ATEST_BITS),
			(int)((stage->stateBits & GLS_DEPTHFUNC_EQUAL) != 0),
			(int)((stage->stateBits & GLS_DEPTHTEST_DISABLE) != 0),
			(int)stage->rgbGen,
			(int)stage->alphaGen,
			(int)stage->bundle[0].tcGen,
			(int)stage->bundle[1].tcGen,
			RB_XboxImageName(stage->bundle[0].image),
			stage->bundle[0].image ? stage->bundle[0].image->texnum : -1,
			stage->bundle[0].image ? stage->bundle[0].image->isLightmap : -1,
			(int)RB_XboxImageLooksFallback(stage->bundle[0].image),
			RB_XboxImageName(stage->bundle[1].image),
			stage->bundle[1].image ? stage->bundle[1].image->texnum : -1,
			stage->bundle[1].image ? stage->bundle[1].image->isLightmap : -1,
			(int)RB_XboxImageLooksFallback(stage->bundle[1].image));
		--suspectBudget;
	}
}

static qboolean RB_XboxIsModelShader( const shader_t *shader )
{
	if (!shader || !shader->name)
	{
		return qfalse;
	}

	return strstr(shader->name, "models/players/") ||
		strstr(shader->name, "models/weapons2/");
}

#if defined(STEFX_ELITE_FORCE_SP)
static qboolean RB_XboxIsEliteForcePlayerModelShader( const shader_t *shader )
{
	return shader && shader->name && strstr( shader->name, "models/players/" );
}

static void RB_XboxLogEliteForcePlayerModelReject( const shaderStage_t *stage, const char *reason )
{
	static int s_stefxPlayerModelRejectLogs = 96;
	const char *shaderName;
	const char *img0Name;
	trRefEntity_t *ent;

	if ( s_stefxPlayerModelRejectLogs <= 0 )
	{
		return;
	}

	shaderName = ( tess.shader && tess.shader->name ) ? tess.shader->name : "<null>";
	img0Name = stage ? RB_XboxImageName( stage->bundle[0].image ) : "<null-stage>";
	if ( !strstr( shaderName, "models/players/" ) &&
		!strstr( img0Name, "models/players/" ) )
	{
		return;
	}

	ent = backEnd.currentEntity;
	XBLF("STEFX: PLAYER_MODEL_REJECT reason=%s cls=%d projection2D=%d ent=%p reType=%d rtModel=%d h=%d shader='%s' stage=%p img0='%s' img1='%s' bundle1=%d verts=%d indexes=%d scene=%d rdflags=0x%x",
		reason ? reason : "<null>",
		cls.state,
		backEnd.projection2D ? 1 : 0,
		ent,
		ent ? ent->e.reType : -1,
		RT_MODEL,
		ent ? ent->e.hModel : -1,
		shaderName,
		stage,
		img0Name,
		stage ? RB_XboxImageName( stage->bundle[1].image ) : "<null-stage>",
		(stage && stage->bundle[1].image) ? 1 : 0,
		tess.numVertexes,
		tess.numIndexes,
		tr.sceneCount,
		backEnd.refdef.rdflags );
	--s_stefxPlayerModelRejectLogs;
}

static qboolean RB_XboxIsEliteForcePlayerModelSingleStageDraw( const shaderStage_t *stage )
{
	trRefEntity_t *ent = backEnd.currentEntity;

	if ( cls.state != CA_ACTIVE )
	{
		RB_XboxLogEliteForcePlayerModelReject( stage, "cls_state" );
		return qfalse;
	}
	if ( backEnd.projection2D )
	{
		RB_XboxLogEliteForcePlayerModelReject( stage, "projection2D" );
		return qfalse;
	}
	if ( !ent )
	{
		RB_XboxLogEliteForcePlayerModelReject( stage, "no_entity" );
		return qfalse;
	}
	if ( ent->e.reType != RT_MODEL )
	{
		RB_XboxLogEliteForcePlayerModelReject( stage, "reType" );
		return qfalse;
	}
	if ( !stage )
	{
		RB_XboxLogEliteForcePlayerModelReject( stage, "no_stage" );
		return qfalse;
	}
	if ( stage->bundle[1].image )
	{
		RB_XboxLogEliteForcePlayerModelReject( stage, "stage1_image" );
		return qfalse;
	}
	if ( !RB_XboxIsEliteForcePlayerModelShader( tess.shader ) )
	{
		RB_XboxLogEliteForcePlayerModelReject( stage, "shader" );
		return qfalse;
	}

	return qtrue;
}

static void RB_XboxPrepareEliteForcePlayerModelDraw( const shaderStage_t *stage )
{
	static int s_stefxPlayerModelStateLogs = 96;
	static int s_stefxPlayerModelPrepareEnterLogs = 96;

	if ( s_stefxPlayerModelPrepareEnterLogs > 0 &&
		RB_XboxIsEliteForcePlayerModelShader( tess.shader ) )
	{
		XBLF("STEFX: PLAYER_MODEL_PREPARE_ENTER ent=%p reType=%d shader='%s' stage=%p img0='%s' img1='%s' bundle1=%d verts=%d indexes=%d",
			backEnd.currentEntity,
			backEnd.currentEntity ? backEnd.currentEntity->e.reType : -1,
			tess.shader ? tess.shader->name : "<null>",
			stage,
			stage ? RB_XboxImageName( stage->bundle[0].image ) : "<null-stage>",
			stage ? RB_XboxImageName( stage->bundle[1].image ) : "<null-stage>",
			(stage && stage->bundle[1].image) ? 1 : 0,
			tess.numVertexes,
			tess.numIndexes );
		--s_stefxPlayerModelPrepareEnterLogs;
	}

	if ( !RB_XboxIsEliteForcePlayerModelSingleStageDraw( stage ) )
	{
		return;
	}

	/*
	 * EF MDR bodies and MD3 heads are ordinary one-texture model draws.
	 * World lightmap/multitexture submissions can leave stage 1 live in the
	 * Xbox fake-GL bridge, so make the one-stage contract explicit here.
	 */
	GL_SelectTexture( 1 );
	glDisable( GL_TEXTURE_2D );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	if ( glw_state && glw_state->device )
	{
		glw_state->device->SetTexture( 1, NULL );
		glw_state->device->SetTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
		glw_state->device->SetTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );
		glw_state->device->SetTextureStageState( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
	}
	GL_SelectTexture( 0 );
	glEnable( GL_TEXTURE_2D );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );
	glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	if ( glw_state && glw_state->device )
	{
		glw_state->device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
		glw_state->device->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		glw_state->device->SetTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
		glw_state->device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
		glw_state->device->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		glw_state->device->SetTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
	}

	if ( s_stefxPlayerModelStateLogs > 0 )
	{
		XBLF("STEFX: PLAYER_MODEL_STAGE_RESET ent=%d h=%d shader='%s' img0='%s' tex0=%d fallback0=%d verts=%d indexes=%d state=0x%x",
			backEnd.currentEntity ? backEnd.currentEntity->e.number : -1,
			backEnd.currentEntity ? backEnd.currentEntity->e.hModel : -1,
			tess.shader ? tess.shader->name : "<null>",
			RB_XboxImageName( stage->bundle[0].image ),
			stage->bundle[0].image ? stage->bundle[0].image->texnum : -1,
			(int)RB_XboxImageLooksFallback( stage->bundle[0].image ),
			tess.numVertexes,
			tess.numIndexes,
			stage->stateBits );
		--s_stefxPlayerModelStateLogs;
	}
}

static void RB_XboxLogEliteForcePlayerModelDrawInputs( const shaderStage_t *stage, const char *where )
{
	static int s_stefxPlayerModelDrawInputLogs = 96;
	int i;
	int minColor[4] = { 255, 255, 255, 255 };
	int maxColor[4] = { 0, 0, 0, 0 };

	if ( !RB_XboxIsEliteForcePlayerModelSingleStageDraw( stage ) ||
		s_stefxPlayerModelDrawInputLogs <= 0 )
	{
		return;
	}

	for ( i = 0; i < tess.numVertexes; ++i )
	{
		int c;
		unsigned long packedColor = tess.svars.colors[i];
		int colorComponents[4];
		colorComponents[0] = (int)((packedColor >> 16) & 0xff);
		colorComponents[1] = (int)((packedColor >> 8) & 0xff);
		colorComponents[2] = (int)(packedColor & 0xff);
		colorComponents[3] = (int)((packedColor >> 24) & 0xff);
		for ( c = 0; c < 4; ++c )
		{
			int color = colorComponents[c];
			if ( color < minColor[c] )
			{
				minColor[c] = color;
			}
			if ( color > maxColor[c] )
			{
				maxColor[c] = color;
			}
		}
	}

	XBLF("STEFX: PLAYER_MODEL_DRAW_INPUT %s ent=%d h=%d shader='%s' img0='%s' tex0=%d fallback0=%d verts=%d indexes=%d colorMin=(%d,%d,%d,%d) colorMax=(%d,%d,%d,%d) state=0x%x scene=%d rdflags=0x%x",
		where ? where : "<null>",
		backEnd.currentEntity ? backEnd.currentEntity->e.number : -1,
		backEnd.currentEntity ? backEnd.currentEntity->e.hModel : -1,
		tess.shader ? tess.shader->name : "<null>",
		RB_XboxImageName( stage->bundle[0].image ),
		stage->bundle[0].image ? stage->bundle[0].image->texnum : -1,
		(int)RB_XboxImageLooksFallback( stage->bundle[0].image ),
		tess.numVertexes,
		tess.numIndexes,
		minColor[0], minColor[1], minColor[2], minColor[3],
		maxColor[0], maxColor[1], maxColor[2], maxColor[3],
		stage->stateBits,
		tr.sceneCount,
		backEnd.refdef.rdflags );
	--s_stefxPlayerModelDrawInputLogs;
}
#endif

static void RB_XboxLogModelShaderSurface( const char *where )
{
	static int modelBudget = 160;
	const shader_t *shader = tess.shader;
	int i;
	trRefEntity_t *ent = backEnd.currentEntity;

	if (cls.state != CA_ACTIVE || modelBudget <= 0 || !RB_XboxIsModelShader(shader))
	{
		return;
	}

	XBLF("STEFX: MODEL_SHADER %s ent=%d reType=%d renderfx=0x%x shader='%s' sort=%g cull=%d passes=%d verts=%d indexes=%d fog=%d dlight=0x%x origin=(%g,%g,%g) scene=%d rdflags=0x%x rgba=%d,%d,%d,%d",
		where,
		ent ? ent->e.number : -1,
		ent ? ent->e.reType : -1,
		ent ? ent->e.renderfx : 0,
		shader->name,
		shader->sort,
		shader->cullType,
		shader->numUnfoggedPasses,
		tess.numVertexes,
		tess.numIndexes,
		tess.fogNum,
		tess.dlightBits,
		ent ? ent->e.origin[0] : 0.0f,
		ent ? ent->e.origin[1] : 0.0f,
		ent ? ent->e.origin[2] : 0.0f,
		tr.sceneCount,
		backEnd.refdef.rdflags,
		ent ? ent->e.shaderRGBA[0] : 0,
		ent ? ent->e.shaderRGBA[1] : 0,
		ent ? ent->e.shaderRGBA[2] : 0,
		ent ? ent->e.shaderRGBA[3] : 0);
	--modelBudget;

	for (i = 0; i < shader->numUnfoggedPasses && i < 3 && modelBudget > 0; ++i)
	{
		const shaderStage_t *stage = &shader->stages[i];
		if (!stage->active)
		{
			continue;
		}

		XBLF("STEFX: MODEL_SHADER_STAGE shader='%s' stage=%d state=0x%x blend=0x%x atest=0x%x depthEq=%d depthOff=%d rgb=%d alpha=%d tc0=%d tc1=%d img0='%s' tex0=%d fallback0=%d img1='%s' tex1=%d fallback1=%d",
			shader->name,
			i,
			stage->stateBits,
			(int)(stage->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)),
			(int)(stage->stateBits & GLS_ATEST_BITS),
			(int)((stage->stateBits & GLS_DEPTHFUNC_EQUAL) != 0),
			(int)((stage->stateBits & GLS_DEPTHTEST_DISABLE) != 0),
			(int)stage->rgbGen,
			(int)stage->alphaGen,
			(int)stage->bundle[0].tcGen,
			(int)stage->bundle[1].tcGen,
			RB_XboxImageName(stage->bundle[0].image),
			stage->bundle[0].image ? stage->bundle[0].image->texnum : -1,
			(int)RB_XboxImageLooksFallback(stage->bundle[0].image),
			RB_XboxImageName(stage->bundle[1].image),
			stage->bundle[1].image ? stage->bundle[1].image->texnum : -1,
			(int)RB_XboxImageLooksFallback(stage->bundle[1].image));
		--modelBudget;
	}
}

static void RB_XboxLogModelTransformProbe( const char *where )
{
	static int transformBudget = 0;
	const shader_t *shader = tess.shader;
	trRefEntity_t *ent = backEnd.currentEntity;
	int i;

	if (cls.state != CA_ACTIVE || transformBudget <= 0 || !RB_XboxIsModelShader(shader) ||
		tess.numVertexes <= 0 || !ent)
	{
		return;
	}

	if (!tr.world || Q_stricmp(tr.world->baseName, "yavin1") ||
		ent->e.number < 50 || ent->e.number > 60)
	{
		return;
	}

	XBLF("JA: MODEL_TRANSFORM %s ent=%d shader='%s' verts=%d indexes=%d viewOrg=(%g,%g,%g) entOrg=(%g,%g,%g) viewport=%d,%d %dx%d mm0=(%g,%g,%g,%g) mm1=(%g,%g,%g,%g) mm2=(%g,%g,%g,%g) mm3=(%g,%g,%g,%g)",
		where,
		ent->e.number,
		shader->name,
		tess.numVertexes,
		tess.numIndexes,
		backEnd.viewParms.or.origin[0],
		backEnd.viewParms.or.origin[1],
		backEnd.viewParms.or.origin[2],
		ent->e.origin[0],
		ent->e.origin[1],
		ent->e.origin[2],
		backEnd.viewParms.viewportX,
		backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth,
		backEnd.viewParms.viewportHeight,
		backEnd.ori.modelMatrix[0], backEnd.ori.modelMatrix[1], backEnd.ori.modelMatrix[2], backEnd.ori.modelMatrix[3],
		backEnd.ori.modelMatrix[4], backEnd.ori.modelMatrix[5], backEnd.ori.modelMatrix[6], backEnd.ori.modelMatrix[7],
		backEnd.ori.modelMatrix[8], backEnd.ori.modelMatrix[9], backEnd.ori.modelMatrix[10], backEnd.ori.modelMatrix[11],
		backEnd.ori.modelMatrix[12], backEnd.ori.modelMatrix[13], backEnd.ori.modelMatrix[14], backEnd.ori.modelMatrix[15]);
	--transformBudget;

	for (i = 0; i < tess.numVertexes && i < 4 && transformBudget > 0; ++i)
	{
		vec4_t eye;
		vec4_t clip;
		vec4_t normalized;
		vec4_t window;
		R_TransformModelToClip(tess.xyz[i], backEnd.ori.modelMatrix, backEnd.viewParms.projectionMatrix, eye, clip);
		R_TransformClipToWindow(clip, &backEnd.viewParms, normalized, window);
		XBLF("JA: MODEL_TRANSFORM_VERTEX ent=%d shader='%s' v=%d local=(%g,%g,%g) eye=(%g,%g,%g,%g) clip=(%g,%g,%g,%g) ndc=(%g,%g,%g) win=(%g,%g,%g)",
			ent->e.number,
			shader->name,
			i,
			tess.xyz[i][0],
			tess.xyz[i][1],
			tess.xyz[i][2],
			eye[0],
			eye[1],
			eye[2],
			eye[3],
			clip[0],
			clip[1],
			clip[2],
			clip[3],
			normalized[0],
			normalized[1],
			normalized[2],
			window[0],
			window[1],
			window[2]);
		--transformBudget;
	}
}

static qboolean RB_XboxIsYavinIntroModelDraw( const shaderStage_t *stage )
{
	trRefEntity_t *ent = backEnd.currentEntity;

	if ( cls.state != CA_ACTIVE || !tr.world || Q_stricmp( tr.world->baseName, "yavin1" ) ||
		!ent || ent->e.reType != RT_MODEL || !ent->e.ghoul2 ||
		ent->e.number < 49 || ent->e.number > 60 ||
		!RB_XboxIsModelShader( tess.shader ) ||
		!stage || stage->bundle[1].image )
	{
		return qfalse;
	}

	return qtrue;
}

static qboolean RB_XboxIsYavinIntroCurrentModelShader( void )
{
	trRefEntity_t *ent = backEnd.currentEntity;

	if ( cls.state != CA_ACTIVE || !tr.world || Q_stricmp( tr.world->baseName, "yavin1" ) ||
		!ent || ent->e.reType != RT_MODEL || !ent->e.ghoul2 ||
		ent->e.number < 49 || ent->e.number > 60 ||
		!RB_XboxIsModelShader( tess.shader ) )
	{
		return qfalse;
	}

	return qtrue;
}

static int RB_XboxYavinIntroCullType( int cullType )
{
	static int s_yavinIntroCullLogs = 0;

	if ( !RB_XboxIsYavinIntroCurrentModelShader() )
	{
		return cullType;
	}

	if ( s_yavinIntroCullLogs < 24 )
	{
		XBLF("JA: XBOX_YAVIN_INTRO_MODEL_CULL_DIAG ent=%d shader='%s' oldCull=%d newCull=%d rdflags=0x%x scene=%d",
			backEnd.currentEntity ? backEnd.currentEntity->e.number : -1,
			tess.shader ? tess.shader->name : "<null>",
			cullType,
			CT_TWO_SIDED,
			backEnd.refdef.rdflags,
			tr.sceneCount);
		++s_yavinIntroCullLogs;
	}

	return CT_TWO_SIDED;
}

static void RB_XboxPrepareYavinIntroModelDraw( const shaderStage_t *stage )
{
	static int s_yavinIntroModelStateLogs = 0;

	if ( !RB_XboxIsYavinIntroModelDraw( stage ) )
	{
		return;
	}

	// World lightmap draws leave texture unit 1 live in the fakegl bridge.
	// These intro actors are single-stage Ghoul2 draws, so make the one-stage
	// contract explicit before submitting their vertices.
	GL_SelectTexture( 1 );
	glDisable( GL_TEXTURE_2D );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 0 );
	glEnable( GL_TEXTURE_2D );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );

	if ( s_yavinIntroModelStateLogs < 24 )
	{
		XBLF("JA: XBOX_YAVIN_INTRO_MODEL_STATE_RESET ent=%d shader='%s' stage0Img='%s' tex0=%d stage1Img='%s'",
			backEnd.currentEntity ? backEnd.currentEntity->e.number : -1,
			tess.shader ? tess.shader->name : "<null>",
			RB_XboxImageName( stage->bundle[0].image ),
			stage->bundle[0].image ? stage->bundle[0].image->texnum : -1,
			RB_XboxImageName( stage->bundle[1].image ) );
		++s_yavinIntroModelStateLogs;
	}
}

static int RB_XboxAdjustYavinIntroModelState( const shaderStage_t *stage, int stateBits )
{
	static int s_yavinIntroDepthLogs = 0;
	int oldStateBits = stateBits;

	if ( !RB_XboxIsYavinIntroModelDraw( stage ) )
	{
		return stateBits;
	}

	/*
	 * Keep normal depth state here.  The earlier yavin1 diagnostic disabled
	 * depth testing for these actors, which proved they were being submitted
	 * but also made them draw through cockpit and foreground geometry.
	 */
	if ( s_yavinIntroDepthLogs < 24 )
	{
		XBLF("JA: XBOX_YAVIN_INTRO_MODEL_DEPTH_KEEP ent=%d shader='%s' oldState=0x%x newState=0x%x depthDisable=%d depthEqual=%d depthMask=%d rdflags=0x%x scene=%d",
			backEnd.currentEntity ? backEnd.currentEntity->e.number : -1,
			tess.shader ? tess.shader->name : "<null>",
			oldStateBits,
			stateBits,
			(int)((stateBits & GLS_DEPTHTEST_DISABLE) != 0),
			(int)((stateBits & GLS_DEPTHFUNC_EQUAL) != 0),
			(int)((stateBits & GLS_DEPTHMASK_TRUE) != 0),
			backEnd.refdef.rdflags,
			tr.sceneCount);
		++s_yavinIntroDepthLogs;
	}

	return stateBits;
}

static void RB_XboxLogYavinIntroModelDrawInputs( const shaderStage_t *stage, const char *where )
{
	static int s_yavinIntroDrawInputLogs = 0;
	trRefEntity_t *ent = backEnd.currentEntity;
	int i;
	int minIndex = 0x7fffffff;
	int maxIndex = -1;
	int minColor[4] = { 255, 255, 255, 255 };
	int maxColor[4] = { 0, 0, 0, 0 };
	float minWin[3] = { 999999.0f, 999999.0f, 999999.0f };
	float maxWin[3] = { -999999.0f, -999999.0f, -999999.0f };
	int clipped = 0;

	if ( !RB_XboxIsYavinIntroModelDraw( stage ) || s_yavinIntroDrawInputLogs >= 32 )
	{
		return;
	}

	for ( i = 0; i < tess.numIndexes; ++i )
	{
		int idx = tess.indexes[i];
		if ( idx < minIndex )
		{
			minIndex = idx;
		}
		if ( idx > maxIndex )
		{
			maxIndex = idx;
		}
	}

	for ( i = 0; i < tess.numVertexes; ++i )
	{
		int c;
		vec4_t eye;
		vec4_t clip;
		vec4_t normalized;
		vec4_t window;

#ifdef _XBOX
		{
			unsigned long packedColor = tess.svars.colors[i];
			int colorComponents[4];
			colorComponents[0] = (int)((packedColor >> 16) & 0xff);
			colorComponents[1] = (int)((packedColor >> 8) & 0xff);
			colorComponents[2] = (int)(packedColor & 0xff);
			colorComponents[3] = (int)((packedColor >> 24) & 0xff);
			for ( c = 0; c < 4; ++c )
			{
				int color = colorComponents[c];
				if ( color < minColor[c] )
				{
					minColor[c] = color;
				}
				if ( color > maxColor[c] )
				{
					maxColor[c] = color;
				}
			}
		}
#else
		for ( c = 0; c < 4; ++c )
		{
			int color = tess.svars.colors[i][c];
			if ( color < minColor[c] )
			{
				minColor[c] = color;
			}
			if ( color > maxColor[c] )
			{
				maxColor[c] = color;
			}
		}
#endif

		R_TransformModelToClip( tess.xyz[i], backEnd.ori.modelMatrix, backEnd.viewParms.projectionMatrix, eye, clip );
		if ( clip[3] <= 0.0f )
		{
			++clipped;
			continue;
		}
		R_TransformClipToWindow( clip, &backEnd.viewParms, normalized, window );
		for ( c = 0; c < 3; ++c )
		{
			if ( window[c] < minWin[c] )
			{
				minWin[c] = window[c];
			}
			if ( window[c] > maxWin[c] )
			{
				maxWin[c] = window[c];
			}
		}
	}

	XBLF("JA: XBOX_YAVIN_INTRO_MODEL_DRAW_INPUT %s ent=%d shader='%s' stageImg='%s' verts=%d indexes=%d idxRange=%d..%d clipped=%d winMin=(%g,%g,%g) winMax=(%g,%g,%g) colorMin=(%d,%d,%d,%d) colorMax=(%d,%d,%d,%d) state=0x%x rdflags=0x%x scene=%d",
		where ? where : "<null>",
		ent ? ent->e.number : -1,
		tess.shader ? tess.shader->name : "<null>",
		stage ? RB_XboxImageName( stage->bundle[0].image ) : "<null>",
		tess.numVertexes,
		tess.numIndexes,
		minIndex,
		maxIndex,
		clipped,
		minWin[0], minWin[1], minWin[2],
		maxWin[0], maxWin[1], maxWin[2],
		minColor[0], minColor[1], minColor[2], minColor[3],
		maxColor[0], maxColor[1], maxColor[2], maxColor[3],
		stage ? stage->stateBits : 0,
		backEnd.refdef.rdflags,
		tr.sceneCount);
	++s_yavinIntroDrawInputLogs;
}

static void RB_XboxForceYavinIntroModelColors( const shaderStage_t *stage )
{
	static int s_yavinIntroColorForceLogs = 0;
	int i;

	if ( !RB_XboxIsYavinIntroModelDraw( stage ) )
	{
		return;
	}

	for ( i = 0; i < tess.numVertexes; ++i )
	{
#ifdef _XBOX
		tess.svars.colors[i] = 0xffffffff;
#else
		tess.svars.colors[i][0] = 255;
		tess.svars.colors[i][1] = 255;
		tess.svars.colors[i][2] = 255;
		tess.svars.colors[i][3] = 255;
#endif
	}

	if ( s_yavinIntroColorForceLogs < 24 )
	{
		XBLF("JA: XBOX_YAVIN_INTRO_MODEL_COLOR_FORCE ent=%d shader='%s' verts=%d rdflags=0x%x scene=%d",
			backEnd.currentEntity ? backEnd.currentEntity->e.number : -1,
			tess.shader ? tess.shader->name : "<null>",
			tess.numVertexes,
			backEnd.refdef.rdflags,
			tr.sceneCount);
		++s_yavinIntroColorForceLogs;
	}
}

static qboolean RB_XboxShouldSkipYavinSkyOverlay( const shader_t *shader )
{
	if ( !shader || !tr.world || cls.state != CA_ACTIVE )
	{
		return qfalse;
	}

	if ( Q_stricmp( tr.world->baseName, "yavin1" ) &&
		Q_stricmp( tr.world->baseName, "yavin1b" ) &&
		Q_stricmp( tr.world->baseName, "yavin2" ) )
	{
		return qfalse;
	}

	if ( !Q_stricmp( shader->name, "textures/common/gradient2" ) )
	{
		return qtrue;
	}

	return qfalse;
}

static void RB_XboxRenderYield( void )
{
	/* Retail's render submit path does not voluntarily yield around each
	 * indexed draw chunk.  The frame boundary still kicks/presents the GPU;
	 * yielding here costs scheduler time on geometry-heavy scenes. */
}

static void RB_XboxDrawElementsChunked( int numIndexes, const glIndex_t *indexes )
{
	static int traceBudget = 16;
	static int chunkTraceBudget = 8;
	static int s_stefxDrawSubmitContextCallBudget = 24;
	qboolean trace;
	int indexBase;
	const int maxChunkIndexes = 384 * 3;

	if ( numIndexes <= 0 || !indexes )
	{
		return;
	}

	trace = RB_XboxShouldTraceSurface();

	if ( trace && traceBudget > 0 )
	{
		XBLF("JA: R_DrawElements submit shader='%s' indexes=%d verts=%d pass=%d\n",
			tess.shader ? tess.shader->name : "<null>",
			numIndexes,
			tess.numVertexes,
			tess.currentPass);
		traceBudget--;
	}

	if ( numIndexes <= maxChunkIndexes )
	{
		if ( trace && s_stefxDrawSubmitContextCallBudget > 0 )
		{
			XBLF("STEFX_DRAW_CONTEXT_CALL where=RB_XboxDrawElementsChunked shader='%s' stage=%d passes=%d state=0x%x indexes=%d verts=%d chunk=0",
				tess.shader ? tess.shader->name : "<null>",
				tess.currentPass,
				tess.shader ? tess.shader->numUnfoggedPasses : 0,
				tess.xstages[tess.currentPass].stateBits,
				numIndexes,
				tess.numVertexes);
			--s_stefxDrawSubmitContextCallBudget;
		}
		if ( trace )
		{
			JkaFakeglSetEliteForceDrawContext( tess.shader ? tess.shader->name : "<null>",
				tess.currentPass,
				tess.shader ? tess.shader->numUnfoggedPasses : 0,
				tess.xstages[tess.currentPass].stateBits );
		}
		glDrawElements( GL_TRIANGLES,
			numIndexes,
			GL_INDEX_TYPE,
			indexes );
		if ( trace )
		{
			JkaFakeglSetEliteForceDrawContext( "", -1, 0, 0 );
		}
		return;
	}

	for ( indexBase = 0; indexBase < numIndexes; )
	{
		int chunkIndexes = numIndexes - indexBase;
		if ( chunkIndexes > maxChunkIndexes )
		{
			chunkIndexes = maxChunkIndexes;
		}
		chunkIndexes -= chunkIndexes % 3;
		if ( chunkIndexes <= 0 )
		{
			break;
		}

		if ( chunkTraceBudget > 0 )
		{
			XBLF("JA: R_DrawElements chunk shader='%s' base=%d count=%d total=%d verts=%d pass=%d",
				tess.shader ? tess.shader->name : "<null>",
				indexBase,
				chunkIndexes,
				numIndexes,
				tess.numVertexes,
				tess.currentPass);
			--chunkTraceBudget;
		}

		if ( trace && s_stefxDrawSubmitContextCallBudget > 0 )
		{
			XBLF("STEFX_DRAW_CONTEXT_CALL where=RB_XboxDrawElementsChunked shader='%s' stage=%d passes=%d state=0x%x indexes=%d verts=%d chunk=%d",
				tess.shader ? tess.shader->name : "<null>",
				tess.currentPass,
				tess.shader ? tess.shader->numUnfoggedPasses : 0,
				tess.xstages[tess.currentPass].stateBits,
				chunkIndexes,
				tess.numVertexes,
				indexBase);
			--s_stefxDrawSubmitContextCallBudget;
		}

		if ( trace )
		{
			JkaFakeglSetEliteForceDrawContext( tess.shader ? tess.shader->name : "<null>",
				tess.currentPass,
				tess.shader ? tess.shader->numUnfoggedPasses : 0,
				tess.xstages[tess.currentPass].stateBits );
		}
		glDrawElements( GL_TRIANGLES,
			chunkIndexes,
			GL_INDEX_TYPE,
			indexes + indexBase );
		if ( trace )
		{
			JkaFakeglSetEliteForceDrawContext( "", -1, 0, 0 );
		}

		RB_XboxRenderYield();
		indexBase += chunkIndexes;
	}
}
#endif

/*
==================
R_DrawElements

Optionally performs our own glDrawElements that looks for strip conditions
instead of using the single glDrawElements call that may be inefficient
without compiled vertex arrays.
==================
*/
static void R_DrawElements( int numIndexes, const glIndex_t *indexes ) {
	int		primitives;

	primitives = r_primitives->integer;
#ifdef _XBOX
	{
		static int s_xboxDrawElementsEntryBudget = 48;
		if (cls.state == CA_ACTIVE && s_xboxDrawElementsEntryBudget > 0)
		{
			int idx0 = (indexes && numIndexes > 0) ? indexes[0] : -1;
			int idx1 = (indexes && numIndexes > 1) ? indexes[1] : -1;
			int idx2 = (indexes && numIndexes > 2) ? indexes[2] : -1;
			XBLF("JA: R_DrawElements entry shader='%s' indexes=%d verts=%d primitivesCvar=%d pass=%d idx0=%d,%d,%d stageIter=%p",
				tess.shader ? tess.shader->name : "<null>",
				numIndexes,
				tess.numVertexes,
				primitives,
				tess.currentPass,
				idx0,
				idx1,
				idx2,
				tess.currentStageIteratorFunc);
			--s_xboxDrawElementsEntryBudget;
		}
	}
#endif

	// default is to use triangles if compiled vertex arrays are present
	if ( primitives == 0 ) {
		if ( glLockArraysEXT ) {
			primitives = 2;
		} else {
			primitives = 1;
		}
	}


	if ( primitives == 2 ) {
#ifdef _XBOX
//		if (tess.useConstantColor)
//		{
//			glDisableClientState( GL_COLOR_ARRAY );
//			glColor4ubv( tess.constantColor );
//		}
#endif
#ifdef _XBOX
		RB_XboxDrawElementsChunked( numIndexes, indexes );
#else
		glDrawElements( GL_TRIANGLES,
						numIndexes,
						GL_INDEX_TYPE,
						indexes );
#endif
		return;
	}

#ifdef _XBOX
	if (primitives == 1 || primitives == 3)
	{
//		if (tess.useConstantColor)
//		{
//			glDisableClientState( GL_COLOR_ARRAY );
//			glColor4ubv( tess.constantColor );
//		}
		/*glDrawElements( GL_TRIANGLES, 
						numIndexes,
						GL_INDEX_TYPE,
						indexes );*/
#if 1	// VVFIXME : Temporary solution to try and increase framerate
		//glIndexedTriToStrip( numIndexes, indexes );

		if(strstr(tess.shader->name, "terrain")) {
			glIndexedTriToStrip( numIndexes, indexes );
		}
		else
            RB_XboxDrawElementsChunked( numIndexes, indexes );
#endif
	
		return;
	}
#else // _XBOX
	if ( primitives == 1 ) {
		R_DrawStripElements( numIndexes,  indexes, glArrayElement );
		return;
	}

	if ( primitives == 3 ) {
		R_DrawStripElements( numIndexes,  indexes, R_ArrayElementDiscrete );
		return;
	}
#endif // _XBOX

	// anything else will cause no drawing
}





/*
=============================================================

SURFACE SHADERS

=============================================================
*/


/*
=================
R_BindAnimatedImage

=================
*/
void R_BindAnimatedImage( const textureBundle_t *bundle) {
	int		index;

	if ( bundle->isVideoMap ) {
		CIN_RunCinematic(bundle->videoMapHandle);
		CIN_UploadCinematic(bundle->videoMapHandle);
		return;
	}

	if ((r_fullbright->integer || (tr.refdef.rdflags & RDF_doFullbright) ) && bundle->isLightmap)
	{
		GL_Bind( tr.whiteImage );
		return;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		GL_Bind( bundle->image );
		return;
	}
	
	if (backEnd.currentEntity->e.renderfx & RF_SETANIMINDEX )
	{
		index = backEnd.currentEntity->e.skinNum;
	}
	else
	{
		// it is necessary to do this messy calc to make sure animations line up
		// exactly with waveforms of the same frequency
		index = myftol( backEnd.refdef.floatTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE );
		index >>= FUNCTABLE_SIZE2;
		
		if ( index < 0 ) {
			index = 0;	// may happen with shader time offsets
		}
	}

	if ( bundle->oneShotAnimMap )
	{
		if ( index >= bundle->numImageAnimations )
		{
			// stick on last frame
			index = bundle->numImageAnimations - 1;
		}
	}
	else
	{
		// loop
		index %= bundle->numImageAnimations;
	}

	GL_Bind( *((image_t**)bundle->image + index) );
}


/*
================
DrawTris

Draws triangle outlines for debugging
================
*/
static void DrawTris (shaderCommands_t *input) 
{
	GL_Bind( tr.whiteImage );

	if ( r_showtriscolor->integer )
	{
		int i = r_showtriscolor->integer;
		if (i == 42) {
			i = Q_irand(0,8);
		}
		switch (i)
		{
		case 1:
			glColor3f( 1.0, 0.0, 0.0); //red
			break;
		case 2:
			glColor3f( 0.0, 1.0, 0.0); //green
			break;
		case 3:
			glColor3f( 1.0, 1.0, 0.0); //yellow
			break;
		case 4:
			glColor3f( 0.0, 0.0, 1.0); //blue
			break;
		case 5:
			glColor3f( 0.0, 1.0, 1.0); //cyan
			break;
		case 6:
			glColor3f( 1.0, 0.0, 1.0); //magenta
			break;
		case 7:
			glColor3f( 0.8f, 0.8f, 0.8f); //white/grey
			break;
		case 8:
			glColor3f( 0.0, 0.0, 0.0); //black
			break;
		}		
	}
	else
	{
		glColor3f( 1.0, 1.0, 1.0); //white
	}

	if ( r_showtris->integer == 2 )
	{
		// tries to do non-xray style showtris
		GL_State( GLS_POLYMODE_LINE );

		glEnable( GL_POLYGON_OFFSET_LINE );
		glPolygonOffset( -1, -2 );

		glDisableClientState( GL_COLOR_ARRAY );
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );

		glVertexPointer( 3, GL_FLOAT, 16, input->xyz );	// padded for SIMD

		if ( glLockArraysEXT ) 
		{
			glLockArraysEXT( 0, input->numVertexes );
			GLimp_LogComment( "glLockArraysEXT\n" );
		}

		R_DrawElements( input->numIndexes, input->indexes );

		if ( glUnlockArraysEXT ) 
		{
			glUnlockArraysEXT( );
			GLimp_LogComment( "glUnlockArraysEXT\n" );
		}

		glDisable( GL_POLYGON_OFFSET_LINE );
	}
	else
	{
		// same old showtris
		GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );
		glDepthRange( 0, 0 );

		glDisableClientState (GL_COLOR_ARRAY);
		glDisableClientState (GL_TEXTURE_COORD_ARRAY);

		glVertexPointer (3, GL_FLOAT, 16, input->xyz);	// padded for SIMD

		if (glLockArraysEXT) {
			glLockArraysEXT(0, input->numVertexes);
			GLimp_LogComment( "glLockArraysEXT\n" );
		}

		R_DrawElements( input->numIndexes, input->indexes );

		if (glUnlockArraysEXT) {
			glUnlockArraysEXT();
			GLimp_LogComment( "glUnlockArraysEXT\n" );
		}

		glDepthRange( 0, 1 );
	}
}

/*
================
DrawNormals

Draws vertex normals for debugging
================
*/
static void DrawNormals (shaderCommands_t *input) {
	int		i;
	vec3_t	temp;

	GL_Bind( tr.whiteImage );
	glColor3f (1,1,1);
	glDepthRange( 0, 0 );	// never occluded
	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );

	glBegin (GL_LINES);
	for ( int i = 0 ; i < input->numVertexes ; i++) {
		glVertex3fv (input->xyz[i]);
		VectorMA (input->xyz[i], 2, input->normal[i], temp);
		glVertex3fv (temp);
	}
	glEnd ();

	glDepthRange( 0, 1 );
}


/*
==============
RB_BeginSurface

We must set some things up before beginning any tesselation,
because a surface may be forced to perform a RB_End due
to overflow.
==============
*/
void RB_BeginSurface( shader_t *shader, int fogNum ) {
	shader_t *state = (shader->remappedShader) ? shader->remappedShader : shader;
#ifdef _XBOX
	if (state != shader)
	{
		static int remapDrawBudget = 96;
		if (remapDrawBudget > 0)
		{
			XBLF("STEFX_REMAP_DRAW old='%s' new='%s' oldPasses=%d newPasses=%d oldSort=%d newSort=%d fog=%d",
				shader ? shader->name : "<null>",
				state ? state->name : "<null>",
				shader ? shader->numUnfoggedPasses : -1,
				state ? state->numUnfoggedPasses : -1,
				shader ? shader->sort : -1,
				state ? state->sort : -1,
				fogNum);
			remapDrawBudget--;
		}
	}
#endif

	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.shader = state;//shader;
	tess.fogNum = fogNum;
	tess.dlightBits = 0;		// will be OR'd in by surface functions

	tess.SSInitializedWind = qfalse;	//is this right?

	tess.xstages = state->stages;
	tess.numPasses = state->numUnfoggedPasses;
	
	tess.currentStageIteratorFunc = state->sky ? RB_StageIteratorSky : RB_StageIteratorGeneric;

	tess.fading = false;

#ifdef _XBOX
	tess.setTangents = false;
	tess.pXyz = NULL;
	tess.pNormal = NULL;
	tess.pColor = NULL;
	tess.pTex1 = NULL;
	tess.pTex2 = NULL;
#endif

	tess.registration++;
}

/*
===================
DrawMultitextured

output = t0 * t1 or t0 + t1

t0 = most upstream according to spec
t1 = most downstream according to spec
===================
*/
static void DrawMultitextured( shaderCommands_t *input, int stage ) {
	shaderStage_t	*pStage;
#ifdef _XBOX
	static int traceBudget = 0;
	static int activeTraceBudget = 48;
	qboolean trace = RB_XboxShouldTraceSurface();
	qboolean forceTrace = RB_XboxForceTraceSurface();
	qboolean stefxBeamShader = RB_XboxIsEliteForceBeamShader( tess.shader );
	qboolean stefxHudShader = backEnd.projection2D && RB_XboxIsEliteForceHudShader( tess.shader );
	int stateBits = 0;
#endif

	pStage = &tess.xstages[stage];

#ifdef _XBOX
	stateBits = pStage->stateBits;
	if ( stefxBeamShader || stefxHudShader )
	{
		static int s_stefxMultitexOverlayBudget = 160;
		const int oldStateBits = stateBits;

		stateBits |= GLS_DEPTHTEST_DISABLE;
		stateBits &= ~( GLS_DEPTHFUNC_EQUAL | GLS_DEPTHMASK_TRUE | GLS_ATEST_BITS );

		if ( ( stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == 0 )
		{
			if ( stefxBeamShader )
			{
				stateBits |= GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
			}
			else
			{
				stateBits |= GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
			}
		}

		if ( cls.state == CA_ACTIVE && s_stefxMultitexOverlayBudget > 0 )
		{
			XBLF( "STEFX: DrawMultitextured overlay state shader='%s' stage=%d hud=%d beam=%d old=0x%x new=0x%x projection2D=%d verts=%d indexes=%d",
				tess.shader ? tess.shader->name : "<null>", stage,
				stefxHudShader ? 1 : 0, stefxBeamShader ? 1 : 0,
				oldStateBits, stateBits, backEnd.projection2D,
				input->numVertexes, input->numIndexes );
			--s_stefxMultitexOverlayBudget;
		}
	}

	if (!backEnd.projection2D && cls.state == CA_ACTIVE && activeTraceBudget > 0)
	{
		image_t *img0 = pStage->bundle[0].image;
		image_t *img1 = pStage->bundle[1].image;
		XBLF("EF: ACTIVE_MTEXTURE shader='%s' stage=%d verts=%d indexes=%d env=%d img0='%s' tex0=%d light0=%d img1='%s' tex1=%d light1=%d st0uv0=%g,%g st1uv0=%g,%g\n",
			tess.shader ? tess.shader->name : "<null>",
			stage,
			input->numVertexes,
			input->numIndexes,
			tess.shader ? tess.shader->multitextureEnv : -1,
			img0 ? img0->imgName : "<null>",
			img0 ? img0->texnum : -1,
			img0 ? img0->isLightmap : -1,
			img1 ? img1->imgName : "<null>",
			img1 ? img1->texnum : -1,
			img1 ? img1->isLightmap : -1,
			input->svars.texcoords[0][0][0],
			input->svars.texcoords[0][0][1],
			input->svars.texcoords[1][0][0],
			input->svars.texcoords[1][0][1]);
		activeTraceBudget--;
	}
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: DrawMultitextured enter shader='%s' stage=%d verts=%d indexes=%d state=0x%x env=%d img0=%p img1=%p\n",
			tess.shader ? tess.shader->name : "<null>",
			stage,
			input->numVertexes,
			input->numIndexes,
			pStage->stateBits,
			tess.shader ? tess.shader->multitextureEnv : -1,
			pStage->bundle[0].image,
			pStage->bundle[1].image);
		if ( traceBudget > 0 ) traceBudget--;
	}
	{
		static int s_xboxWorldStageStateLogCount = 0;
		if ( s_xboxWorldStageStateLogCount < 8 )
		{
			const DWORD color0 = input->svars.colors[0];
			const byte r = (byte)((color0 >> 16) & 0xff);
			const byte g = (byte)((color0 >> 8) & 0xff);
			const byte b = (byte)(color0 & 0xff);
			const byte a = (byte)((color0 >> 24) & 0xff);
			XBLF("JA: XBOX_WORLD_STAGE shader='%s' stage=%d ent=%p reType=%d state=0x%x rgbGen=%d alphaGen=%d lm0=%d lm1=%d vtxLm0=%d env=%d r_vertexLight=%d r_lightmap=%d r_fullbright=%d color0=%u,%u,%u,%u tc0=%g,%g tc1=%g,%g img0='%s' img1='%s'",
				tess.shader ? tess.shader->name : "<null>",
				stage,
				backEnd.currentEntity,
				backEnd.currentEntity ? backEnd.currentEntity->e.reType : -1,
				pStage->stateBits,
				pStage->rgbGen,
				pStage->alphaGen,
				pStage->bundle[0].isLightmap ? 1 : 0,
				pStage->bundle[1].isLightmap ? 1 : 0,
				pStage->bundle[0].vertexLightmap ? 1 : 0,
				tess.shader ? tess.shader->multitextureEnv : -1,
				r_vertexLight ? r_vertexLight->integer : -1,
				r_lightmap ? r_lightmap->integer : -1,
				r_fullbright ? r_fullbright->integer : -1,
				r, g, b, a,
				input->svars.texcoords[0][0][0],
				input->svars.texcoords[0][0][1],
				input->svars.texcoords[1][0][0],
				input->svars.texcoords[1][0][1],
				pStage->bundle[0].image ? pStage->bundle[0].image->imgName : "<null>",
				pStage->bundle[1].image ? pStage->bundle[1].image->imgName : "<null>");
			++s_xboxWorldStageStateLogCount;
		}
	}
#endif
#ifdef _XBOX
	stateBits = RB_XboxAdjustEliteForceScriptPanelState( pStage, stateBits, "DrawMultitextured" );
#if defined(STEFX_ELITE_FORCE_SP)
	stateBits = RB_XboxAdjustEliteForceLegacyMaskedWorldOverlayState( pStage, stateBits, stage, "DrawMultitextured" );
#endif
	GL_State( stateBits );
#else
	GL_State( pStage->stateBits );
#endif
#ifdef _XBOX
	if ( stefxBeamShader || stefxHudShader )
	{
		RB_XboxForceEliteForceOverlayD3DState( tess.shader, stefxBeamShader, "DrawMultitextured" );
		RB_XboxPrepareEliteForceOverlayStage( pStage, stefxBeamShader, "DrawMultitextured" );
	}
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: DrawMultitextured after GL_State shader='%s' stage=%d\n",
			tess.shader ? tess.shader->name : "<null>", stage);
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif

	//
	// base
	//
	GL_SelectTexture( 0 );
	glEnable( GL_TEXTURE_2D );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoords[0] );
#ifdef _XBOX
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: DrawMultitextured before bind0 shader='%s' stage=%d\n",
			tess.shader ? tess.shader->name : "<null>", stage);
		if ( traceBudget > 0 ) traceBudget--;
	}
	{
		static int s_xboxMultitexCoordLogCount = 0;
		if (trace && s_xboxMultitexCoordLogCount < 8)
		{
			XBLF("JA: DrawMultitextured coords shader='%s' stage=%d st0=%p st1=%p st0uv0=%g,%g st1uv0=%g,%g\n",
				tess.shader ? tess.shader->name : "<null>",
				stage,
				input->svars.texcoords[0],
				input->svars.texcoords[1],
				input->svars.texcoords[0][0][0],
				input->svars.texcoords[0][0][1],
				input->svars.texcoords[1][0][0],
				input->svars.texcoords[1][0][1]);
			s_xboxMultitexCoordLogCount++;
		}
	}
#endif
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	RB_STEFX_ForceNextTextureBind( 0, pStage, &pStage->bundle[0] );
#endif
	R_BindAnimatedImage( &pStage->bundle[0] );
#ifdef _XBOX
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: DrawMultitextured after bind0 shader='%s' stage=%d\n",
			tess.shader ? tess.shader->name : "<null>", stage);
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif

	//
	// lightmap/secondary pass
	//
	GL_SelectTexture( 1 );
	glEnable( GL_TEXTURE_2D );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );

	if ( r_lightmap->integer ) {
		GL_TexEnv( GL_REPLACE );
	} else {
		GL_TexEnv( tess.shader->multitextureEnv ); 
	}

	glTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoords[1] );

#ifdef _XBOX
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: DrawMultitextured before bind1 shader='%s' stage=%d\n",
			tess.shader ? tess.shader->name : "<null>", stage);
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	RB_STEFX_ForceNextTextureBind( 1, pStage, &pStage->bundle[1] );
#endif
	R_BindAnimatedImage( &pStage->bundle[1] );
#ifdef _XBOX
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: DrawMultitextured after bind1 shader='%s' stage=%d\n",
			tess.shader ? tess.shader->name : "<null>", stage);
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif

#ifdef _XBOX
	if ( stefxBeamShader || stefxHudShader )
	{
		RB_XboxForceEliteForceOverlayD3DState( tess.shader, stefxBeamShader, "DrawMultitextured before draw" );
		RB_XboxLogEliteForceOverlayDraw( pStage, stefxHudShader, stefxBeamShader, "DrawMultitextured" );
		JkaFakeglSetEliteForceOverlayDrawContext( 1, stefxHudShader, stefxBeamShader );
	}
	RB_XboxBeginEliteForceScriptPanelFakeglState( pStage, "DrawMultitextured" );
	if ( trace )
	{
		JkaFakeglSetEliteForceDrawContext( tess.shader ? tess.shader->name : "<null>", stage, 2, (unsigned int)stateBits );
	}
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: DrawMultitextured before draw shader='%s' stage=%d\n",
			tess.shader ? tess.shader->name : "<null>", stage);
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif
	R_DrawElements( input->numIndexes, input->indexes );
#ifdef _XBOX
	if ( trace )
	{
		JkaFakeglSetEliteForceDrawContext( "", -1, 0, 0 );
	}
	RB_XboxEndEliteForceScriptPanelFakeglState( "DrawMultitextured" );
	if ( stefxBeamShader || stefxHudShader )
	{
		JkaFakeglSetEliteForceOverlayDrawContext( 0, 0, 0 );
	}
#endif
#ifdef _XBOX
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: DrawMultitextured after draw shader='%s' stage=%d\n",
			tess.shader ? tess.shader->name : "<null>", stage);
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif

	//
	// disable texturing on TEXTURE1, then select TEXTURE0
	//
	glDisable( GL_TEXTURE_2D );
#ifdef _XBOX
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
#endif

	GL_SelectTexture( 0 );
}


#ifdef VV_LIGHTING
static void BuildTangentVectors( void ) {

	memset(tess.tangent, 0, sizeof(vec4_t) * SHADER_MAX_VERTEXES);

	for(int i = 0; i < tess.numIndexes; i += 3)
	{
		vec3_t vec1, vec2, du, dv, cp;

		vec1[0] = tess.xyz[tess.indexes[i+1]][0] - tess.xyz[tess.indexes[i]][0];
		vec1[1] = tess.svars.texcoords[0][tess.indexes[i+1]][0] - tess.svars.texcoords[0][tess.indexes[i]][0];
		vec1[2] = tess.svars.texcoords[0][tess.indexes[i+1]][1] - tess.svars.texcoords[0][tess.indexes[i]][1];

		vec2[0] = tess.xyz[tess.indexes[i+2]][0] - tess.xyz[tess.indexes[i]][0];
		vec2[1] = tess.svars.texcoords[0][tess.indexes[i+2]][0] - tess.svars.texcoords[0][tess.indexes[i]][0];
		vec2[2] = tess.svars.texcoords[0][tess.indexes[i+2]][1] - tess.svars.texcoords[0][tess.indexes[i]][1];

		CrossProduct(vec1, vec2, cp);

		if(cp[0] == 0.0f)
			cp[0] = 0.001f;

		du[0] = -cp[1] / cp[0];
		dv[0] = -cp[2] / cp[0];

		vec1[0] = tess.xyz[tess.indexes[i+1]][1] - tess.xyz[tess.indexes[i]][1];
		vec1[1] = tess.svars.texcoords[0][tess.indexes[i+1]][0] - tess.svars.texcoords[0][tess.indexes[i]][0];
		vec1[2] = tess.svars.texcoords[0][tess.indexes[i+1]][1] - tess.svars.texcoords[0][tess.indexes[i]][1];

		vec2[0] = tess.xyz[tess.indexes[i+2]][1] - tess.xyz[tess.indexes[i]][1];
		vec2[1] = tess.svars.texcoords[0][tess.indexes[i+2]][0] - tess.svars.texcoords[0][tess.indexes[i]][0];
		vec2[2] = tess.svars.texcoords[0][tess.indexes[i+2]][1] - tess.svars.texcoords[0][tess.indexes[i]][1];

		CrossProduct(vec1, vec2, cp);

		if(cp[0] == 0.0f)
			cp[0] = 0.001f;

		du[1] = -cp[1] / cp[0];
		dv[1] = -cp[2] / cp[0];

		vec1[0] = tess.xyz[tess.indexes[i+1]][2] - tess.xyz[tess.indexes[i]][2];
		vec1[1] = tess.svars.texcoords[0][tess.indexes[i+1]][0] - tess.svars.texcoords[0][tess.indexes[i]][0];
		vec1[2] = tess.svars.texcoords[0][tess.indexes[i+1]][1] - tess.svars.texcoords[0][tess.indexes[i]][1];

		vec2[0] = tess.xyz[tess.indexes[i+2]][2] - tess.xyz[tess.indexes[i]][2];
		vec2[1] = tess.svars.texcoords[0][tess.indexes[i+2]][0] - tess.svars.texcoords[0][tess.indexes[i]][0];
		vec2[2] = tess.svars.texcoords[0][tess.indexes[i+2]][1] - tess.svars.texcoords[0][tess.indexes[i]][1];

		CrossProduct(vec1, vec2, cp);

		if(cp[0] == 0.0f)
			cp[0] = 0.001f;

		du[2] = -cp[1] / cp[0];
		dv[2] = -cp[2] / cp[0];

		tess.tangent[tess.indexes[i]][0] += du[0];
		tess.tangent[tess.indexes[i]][1] += du[1];
		tess.tangent[tess.indexes[i]][2] += du[2];

		tess.tangent[tess.indexes[i+1]][0] += du[0];
		tess.tangent[tess.indexes[i+1]][1] += du[1];
		tess.tangent[tess.indexes[i+1]][2] += du[2];

		tess.tangent[tess.indexes[i+2]][0] += du[0];
		tess.tangent[tess.indexes[i+2]][1] += du[1];
		tess.tangent[tess.indexes[i+2]][2] += du[2];
	}
	
	for ( int i = 0; i < tess.numVertexes; i++)
	{
		VectorNormalizeFast(tess.tangent[i]);
	}
}
#endif // VV_LIGHTING

//--EF_old dlight code...reverting back to Quake III dlight to see if people like that better
// Lifted the whole function because someone hacked the heck out of this and it doesn't seem to
//	be a case where it's as easy as just changing the blend mode....
/*
===================
ProjectDlightTexture

Perform dynamic lighting with another rendering pass
===================
*/
/*
static void ProjectDlightTexture( void ) {
	int		l;
	vec3_t	origin;
	float	*texCoords;
	byte	*colors;
	byte	clipBits[SHADER_MAX_VERTEXES];
	MAC_STATIC float	texCoordsArray[SHADER_MAX_VERTEXES][2];
	byte	colorArray[SHADER_MAX_VERTEXES][4];
	unsigned	hitIndexes[SHADER_MAX_INDEXES];

	if ( !backEnd.refdef.num_dlights ) {
		return;
	}

	for ( l = 0 ; l < backEnd.refdef.num_dlights ; l++ ) {
		int		numIndexes;
		vec3_t	floatColor;
		float	scale;
		float	radius, chord;
		dlight_t	*dl;
		int i;

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;	// this surface definately doesn't have any of this light
		}
		texCoords = texCoordsArray[0];
		colors = colorArray[0];

		dl = &backEnd.refdef.dlights[l];
		VectorCopy( dl->transformed, origin );
		radius = dl->radius;
		chord = radius*radius*0.25f;
		scale = 1.0f / radius;
		floatColor[0] = dl->color[0] * 255f;
		floatColor[1] = dl->color[1] * 255f;
		floatColor[2] = dl->color[2] * 255f;

		for ( i = 0 ; i < tess.numVertexes ; i++, texCoords += 2, colors += 4 ) {
			vec3_t	distVec;
			int		clip;
			float	tempColor;
			float	modulate, dist;

//			if ( 0 ) {
//				clipBits[i] = 255;	// definately not dlighted
//				continue;
//			}
//
			backEnd.pc.c_dlightVertexes++;

			VectorSubtract( origin, tess.xyz[i], distVec );
			dist = VectorLengthSquared(distVec);

			texCoords[0] = 0.5 + distVec[0] * scale;	//xy projection
			texCoords[1] = 0.5 + distVec[1] * scale;

			clip = 0;
			if ( texCoords[0] < 0 ) {
				clip |= 1;
			} else if ( texCoords[0] > 1 ) {
				clip |= 2;
			}
			if ( texCoords[1] < 0 ) {
				clip |= 4;
			} else if ( texCoords[1] > 1 ) {
				clip |= 8;
			}
			clipBits[i] = clip;

			// modulate the strength based on the height and color
			if ( dist > chord) {
				clip |= 16;
				modulate = 255*1.0ff;
			} else {
				modulate = 255*2*dist*scale*scale;
			}
			tempColor = floatColor[0] + modulate;
			colors[0] = tempColor > 255 ? 255: myftol(tempColor);
			
			tempColor = floatColor[1] + modulate;
			colors[1] = tempColor > 255 ? 255: myftol(tempColor);

			tempColor = floatColor[2] + modulate;
			colors[2] = tempColor > 255 ? 255: myftol(tempColor);

//			colors[3] = 255;
			if ( distVec[2] > radius ) {
				colors[3] = 0;
			} else if ( distVec[2] < -radius ) {
				colors[3] = 0;
			} else {
				if ( distVec[2] < 0 ) {
					distVec[2] = -distVec[2];
				}
				if ( distVec[2] < radius * 0.5 ) {
					colors[3] = 255;
				} else {
					colors[3] = myftol(255* (radius - distVec[2]) * scale);
				}
			}

		}

		// build a list of triangles that need light
		numIndexes = 0;
		for ( i = 0 ; i < tess.numIndexes ; i += 3 ) {
			int		a, b, c;

			a = tess.indexes[i];
			b = tess.indexes[i+1];
			c = tess.indexes[i+2];
			if ( clipBits[a] & clipBits[b] & clipBits[c] ) {
				continue;	// not lighted
			}
			hitIndexes[numIndexes] = a;
			hitIndexes[numIndexes+1] = b;
			hitIndexes[numIndexes+2] = c;
			numIndexes += 3;
		}

		if ( !numIndexes ) {
			continue;
		}

		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		glTexCoordPointer( 2, GL_FLOAT, 0, texCoordsArray[0] );

		glEnableClientState( GL_COLOR_ARRAY );
		glColorPointer( 4, GL_UNSIGNED_BYTE, 0, colorArray );

		GL_Bind( tr.dlightImage );

		// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light
		// where they aren't rendered
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_SRC_COLOR | GLS_DEPTHFUNC_EQUAL);//our way
//		GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );	//Id way
		R_DrawElements( numIndexes, hitIndexes );
		backEnd.pc.c_totalIndexes += numIndexes;
		backEnd.pc.c_dlightIndexes += numIndexes;
	}
}
*/

// Lifted from Quake III to see if people like this kind of dlight better
/*
===================
ProjectDlightTexture

Perform dynamic lighting with another rendering pass
===================
*/
#ifndef VV_LIGHTING
static void ProjectDlightTexture2( void ) {
	int		i, l;
	vec3_t	origin;
	byte	clipBits[SHADER_MAX_VERTEXES];
	MAC_STATIC float	texCoordsArray[SHADER_MAX_VERTEXES][2];
	MAC_STATIC float	oldTexCoordsArray[SHADER_MAX_VERTEXES][2];
	MAC_STATIC float	vertCoordsArray[SHADER_MAX_VERTEXES][4];
	unsigned int		colorArray[SHADER_MAX_VERTEXES];
	glIndex_t	hitIndexes[SHADER_MAX_INDEXES];
	int		numIndexes;
	float	radius;
	int		fogging;
	shaderStage_t *dStage;
	vec3_t	posa;
	vec3_t	posb;
	vec3_t	posc;
	vec3_t	dist;
	vec3_t	e1;
	vec3_t	e2;
	vec3_t	normal;
	float	fac,modulate;
	vec3_t	floatColor;
	byte colorTemp[4];

	int		needResetVerts=0;

	if ( !backEnd.refdef.num_dlights ) 
	{
		return;
	}

	for ( l = 0 ; l < backEnd.refdef.num_dlights ; l++ )
	{
		dlight_t	*dl;

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;	// this surface definately doesn't have any of this light
		}

		dl = &backEnd.refdef.dlights[l];
		VectorCopy( dl->transformed, origin );
		radius = dl->radius;

		int		clipall = 63;
		for ( i = 0 ; i < tess.numVertexes ; i++) 
		{
			int		clip;
			VectorSubtract( origin, tess.xyz[i], dist );

			clip = 0;
			if (  dist[0] < -radius ) 
			{
				clip |= 1;
			}
			else if ( dist[0] > radius ) 
			{
				clip |= 2;
			}
			if (  dist[1] < -radius ) 
			{
				clip |= 4;
			}
			else if ( dist[1] > radius ) 
			{
				clip |= 8;
			}
			if (  dist[2] < -radius ) 
			{
				clip |= 16;
			}
			else if ( dist[2] > radius ) 
			{
				clip |= 32;
			}

			clipBits[i] = clip;
			clipall &= clip;
		}
		if ( clipall ) 
		{
			continue;	// this surface doesn't have any of this light
		}
		floatColor[0] = dl->color[0] * 255.0f;
		floatColor[1] = dl->color[1] * 255.0f;
		floatColor[2] = dl->color[2] * 255.0f;
		// build a list of triangles that need light
		numIndexes = 0;
		for ( i = 0 ; i < tess.numIndexes ; i += 3 ) 
		{
			int		a, b, c;

			a = tess.indexes[i];
			b = tess.indexes[i+1];
			c = tess.indexes[i+2];
			if ( clipBits[a] & clipBits[b] & clipBits[c] ) 
			{
				continue;	// not lighted
			}

			// copy the vertex positions
			VectorCopy(tess.xyz[a],posa);
			VectorCopy(tess.xyz[b],posb);
			VectorCopy(tess.xyz[c],posc);

			VectorSubtract( posa, posb,e1);
			VectorSubtract( posc, posb,e2);
			CrossProduct(e1,e2,normal);
//			fac=DotProduct(normal,origin)-DotProduct(normal,posa);
//			if (fac <= 0.0f || // backface
			if ( (!r_dlightBacks->integer && DotProduct(normal,origin)-DotProduct(normal,posa) <= 0.0f) || // backface
				DotProduct(normal,normal) < 1E-8f) // junk triangle
			{
				continue;
			}
			VectorNormalize(normal);
			fac=DotProduct(normal,origin)-DotProduct(normal,posa);
			if (fac >= radius)  // out of range
			{
				continue;
			}
			modulate = 1.0f-((fac*fac) / (radius*radius));
			fac = 0.5f/sqrtf(radius*radius - fac*fac);

			// save the verts
			VectorCopy(posa,vertCoordsArray[numIndexes]);
			VectorCopy(posb,vertCoordsArray[numIndexes+1]);
			VectorCopy(posc,vertCoordsArray[numIndexes+2]);

			// now we need e1 and e2 to be an orthonormal basis
			if (DotProduct(e1,e1) > DotProduct(e2,e2))
			{
				VectorNormalize(e1);
				CrossProduct(e1,normal,e2);
			}
			else
			{
				VectorNormalize(e2);
				CrossProduct(normal,e2,e1);
			}
			VectorScale(e1,fac,e1);
			VectorScale(e2,fac,e2);

			VectorSubtract( posa, origin,dist);
			texCoordsArray[numIndexes][0]=DotProduct(dist,e1)+0.5f;
			texCoordsArray[numIndexes][1]=DotProduct(dist,e2)+0.5f;

			VectorSubtract( posb, origin,dist);
			texCoordsArray[numIndexes+1][0]=DotProduct(dist,e1)+0.5f;
			texCoordsArray[numIndexes+1][1]=DotProduct(dist,e2)+0.5f;

			VectorSubtract( posc, origin,dist);
			texCoordsArray[numIndexes+2][0]=DotProduct(dist,e1)+0.5f;
			texCoordsArray[numIndexes+2][1]=DotProduct(dist,e2)+0.5f;

			if ((texCoordsArray[numIndexes][0] < 0.0f && texCoordsArray[numIndexes+1][0] < 0.0f && texCoordsArray[numIndexes+2][0] < 0.0f) ||
				(texCoordsArray[numIndexes][0] > 1.0f && texCoordsArray[numIndexes+1][0] > 1.0f && texCoordsArray[numIndexes+2][0] > 1.0f) ||
				(texCoordsArray[numIndexes][1] < 0.0f && texCoordsArray[numIndexes+1][1] < 0.0f && texCoordsArray[numIndexes+2][1] < 0.0f) ||
				(texCoordsArray[numIndexes][1] > 1.0f && texCoordsArray[numIndexes+1][1] > 1.0f && texCoordsArray[numIndexes+2][1] > 1.0f) )
			{
				continue; // didn't end up hitting this tri
			}

			// these are the old texture coordinates for the multitexture dlight

			/* old code, get from the svars = wrong
			oldTexCoordsArray[numIndexes][0]=tess.svars.texcoords[0][a][0];
			oldTexCoordsArray[numIndexes][1]=tess.svars.texcoords[0][a][1];
			oldTexCoordsArray[numIndexes+1][0]=tess.svars.texcoords[0][b][0];
			oldTexCoordsArray[numIndexes+1][1]=tess.svars.texcoords[0][b][1];
			oldTexCoordsArray[numIndexes+2][0]=tess.svars.texcoords[0][c][0];
			oldTexCoordsArray[numIndexes+2][1]=tess.svars.texcoords[0][c][1];
			*/
			oldTexCoordsArray[numIndexes][0]=tess.texCoords[a][0][0];
			oldTexCoordsArray[numIndexes][1]=tess.texCoords[a][0][1];
			oldTexCoordsArray[numIndexes+1][0]=tess.texCoords[b][0][0];
			oldTexCoordsArray[numIndexes+1][1]=tess.texCoords[b][0][1];
			oldTexCoordsArray[numIndexes+2][0]=tess.texCoords[c][0][0];
			oldTexCoordsArray[numIndexes+2][1]=tess.texCoords[c][0][1];

			colorTemp[0] = myftol(floatColor[0] * modulate);
			colorTemp[1] = myftol(floatColor[1] * modulate);
			colorTemp[2] = myftol(floatColor[2] * modulate);
			colorTemp[3] = 255;
			colorArray[numIndexes]=*(unsigned int *)colorTemp;
			colorArray[numIndexes+1]=*(unsigned int *)colorTemp;
			colorArray[numIndexes+2]=*(unsigned int *)colorTemp;

			hitIndexes[numIndexes] = numIndexes;
			hitIndexes[numIndexes+1] = numIndexes+1;
			hitIndexes[numIndexes+2] = numIndexes+2;
			numIndexes += 3;

			if (numIndexes>=SHADER_MAX_VERTEXES-3)
			{
				break; // we are out of space, so we are done :)
			}
		}

		if ( !numIndexes ) {
			continue;
		}
		//don't have fog enabled when we redraw with alpha test, or it will double over
		//and screw the tri up -rww
		if (r_drawfog->value == 2 && 
			tr.world &&
			(tess.fogNum == tr.world->globalFog || tess.fogNum == tr.world->numfogs))
		{
			fogging = glIsEnabled(GL_FOG);

			if (fogging)
			{
				glDisable(GL_FOG);
			}
		}
		else
		{
			fogging = 0;
		}

		if (!needResetVerts)
		{
			needResetVerts=1;
			if (glUnlockArraysEXT) 
			{
				glUnlockArraysEXT();
				GLimp_LogComment( "glUnlockArraysEXT\n" );
			}
		}
		glVertexPointer (3, GL_FLOAT, 16, vertCoordsArray);	// padded for SIMD

		dStage = NULL;
		if (tess.shader && glActiveTextureARB)
		{
			int i = 0;
			while (i < tess.shader->numUnfoggedPasses)
			{
				const int blendBits = (GLS_SRCBLEND_BITS+GLS_DSTBLEND_BITS);
				if (((tess.shader->stages[i].bundle[0].image && !tess.shader->stages[i].bundle[0].isLightmap && !tess.shader->stages[i].bundle[0].numTexMods && tess.shader->stages[i].bundle[0].tcGen != TCGEN_ENVIRONMENT_MAPPED && tess.shader->stages[i].bundle[0].tcGen != TCGEN_FOG) ||
					 (tess.shader->stages[i].bundle[1].image && !tess.shader->stages[i].bundle[1].isLightmap && !tess.shader->stages[i].bundle[1].numTexMods && tess.shader->stages[i].bundle[1].tcGen != TCGEN_ENVIRONMENT_MAPPED && tess.shader->stages[i].bundle[1].tcGen != TCGEN_FOG)) &&
					(tess.shader->stages[i].stateBits & blendBits) == 0 )
				{ //only use non-lightmap opaque stages
                    dStage = &tess.shader->stages[i];
					break;
				}
				i++;
			}
		}

		if (dStage)
		{
			GL_SelectTexture( 0 );
			GL_State(0);
			glTexCoordPointer( 2, GL_FLOAT, 0, oldTexCoordsArray[0] );
			if (dStage->bundle[0].image && !dStage->bundle[0].isLightmap && !dStage->bundle[0].numTexMods && dStage->bundle[0].tcGen != TCGEN_ENVIRONMENT_MAPPED && dStage->bundle[0].tcGen != TCGEN_FOG)
			{
				R_BindAnimatedImage( &dStage->bundle[0] );
			}
			else
			{
				R_BindAnimatedImage( &dStage->bundle[1] );
			}

			GL_SelectTexture( 1 );
			glEnable( GL_TEXTURE_2D );
			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glTexCoordPointer( 2, GL_FLOAT, 0, texCoordsArray[0] );
			glEnableClientState( GL_COLOR_ARRAY );
			glColorPointer( 4, GL_UNSIGNED_BYTE, 0, colorArray );
			GL_Bind( tr.dlightImage );
			GL_TexEnv( GL_MODULATE );


			GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL);// | GLS_ATEST_GT_0);

			R_DrawElements( numIndexes, hitIndexes );

			glDisable( GL_TEXTURE_2D );
			GL_SelectTexture(0);
		}
		else
		{
			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glTexCoordPointer( 2, GL_FLOAT, 0, texCoordsArray[0] );

			glEnableClientState( GL_COLOR_ARRAY );
			glColorPointer( 4, GL_UNSIGNED_BYTE, 0, colorArray );

			GL_Bind( tr.dlightImage );
			// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light
			// where they aren't rendered
			//if ( dl->additive ) {
			//	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
			//}
			//else 
			{
				GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
			}

			R_DrawElements( numIndexes, hitIndexes );
		}

		if (fogging)
		{
			glEnable(GL_FOG);
		}

		backEnd.pc.c_totalIndexes += numIndexes;
		backEnd.pc.c_dlightIndexes += numIndexes;
	}
	if (needResetVerts)
	{
		glVertexPointer (3, GL_FLOAT, 16, tess.xyz);	// padded for SIMD
		if (glLockArraysEXT)
		{
			glLockArraysEXT(0, tess.numVertexes);
			GLimp_LogComment( "glLockArraysEXT\n" );
		}
	}
}
static void ProjectDlightTexture( void ) {
	int		i, l;
	vec3_t	origin;
	float	*texCoords;
	byte	*colors;
	byte	clipBits[SHADER_MAX_VERTEXES];
	MAC_STATIC float	texCoordsArray[SHADER_MAX_VERTEXES][2];
	byte	colorArray[SHADER_MAX_VERTEXES][4];
	glIndex_t	hitIndexes[SHADER_MAX_INDEXES];
	int		numIndexes;
	float	scale;
	float	radius;
	int		fogging;
	vec3_t	floatColor;
	shaderStage_t *dStage;

	if ( !backEnd.refdef.num_dlights ) {
		return;
	}

	for ( l = 0 ; l < backEnd.refdef.num_dlights ; l++ ) {
		dlight_t	*dl;

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;	// this surface definately doesn't have any of this light
		}

		texCoords = texCoordsArray[0];
		colors = colorArray[0];

		dl = &backEnd.refdef.dlights[l];
		VectorCopy( dl->transformed, origin );
		radius = dl->radius;
		scale = 1.0f / radius;

		floatColor[0] = dl->color[0] * 255.0f;
		floatColor[1] = dl->color[1] * 255.0f;
		floatColor[2] = dl->color[2] * 255.0f;

		for ( i = 0 ; i < tess.numVertexes ; i++, texCoords += 2, colors += 4 ) {
			vec3_t	dist;
			int		clip;
			float	modulate;

			backEnd.pc.c_dlightVertexes++;

			VectorSubtract( origin, tess.xyz[i], dist );
			
			int l = 1;
			int bestIndex = 0;
			float greatest = tess.normal[i][0];
			if (greatest < 0.0f)
			{
				greatest = -greatest;
			}

			if (VectorCompare(tess.normal[i], vec3_origin))
			{ //damn you terrain!
				bestIndex = 2;
			}
			else
			{
				while (l < 3)
				{
					if ((tess.normal[i][l] > greatest && tess.normal[i][l] > 0.0f) ||
						(tess.normal[i][l] < -greatest && tess.normal[i][l] < 0.0f))
					{
						greatest = tess.normal[i][l];
						if (greatest < 0.0f)
						{
							greatest = -greatest;
						}
						bestIndex = l;
					}
					l++;
				}
			}

			float dUse = 0.0f;
			const float maxScale = 1.5f;
			const float maxGroundScale = 1.4f;
			const float lightScaleTolerance = 0.1f;

			if (bestIndex == 2)
			{
				dUse = origin[2]-tess.xyz[i][2];
				if (dUse < 0.0f)
				{
					dUse = -dUse;
				}
				dUse = (radius*0.5f)/dUse;
				if (dUse > maxGroundScale)
				{
					dUse = maxGroundScale;
				}
				else if (dUse < 0.1f)
				{
					dUse = 0.1f;
				}

				if (VectorCompare(tess.normal[i], vec3_origin) ||
					tess.normal[i][0] > lightScaleTolerance ||
					tess.normal[i][0] < -lightScaleTolerance ||
					tess.normal[i][1] > lightScaleTolerance ||
					tess.normal[i][1] < -lightScaleTolerance)
				{ //if not perfectly flat, we must use a constant dist
					scale = 1.0f / radius;
				}
				else
				{
					scale = 1.0f / (radius*dUse);
				}

				texCoords[0] = 0.5f + dist[0] * scale;
				texCoords[1] = 0.5f + dist[1] * scale;
			}
			else if (bestIndex == 1)
			{
				dUse = origin[1]-tess.xyz[i][1];
				if (dUse < 0.0f)
				{
					dUse = -dUse;
				}
				dUse = (radius*0.5f)/dUse;
				if (dUse > maxScale)
				{
					dUse = maxScale;
				}
				else if (dUse < 0.1f)
				{
					dUse = 0.1f;
				}
				if (tess.normal[i][0] > lightScaleTolerance ||
					tess.normal[i][0] < -lightScaleTolerance ||
					tess.normal[i][2] > lightScaleTolerance ||
					tess.normal[i][2] < -lightScaleTolerance)
				{ //if not perfectly flat, we must use a constant dist
					scale = 1.0f / radius;
				}
				else
				{
					scale = 1.0f / (radius*dUse);
				}

				texCoords[0] = 0.5f + dist[0] * scale;
				texCoords[1] = 0.5f + dist[2] * scale;
			}
			else
			{
				dUse = origin[0]-tess.xyz[i][0];
				if (dUse < 0.0f)
				{
					dUse = -dUse;
				}
				dUse = (radius*0.5f)/dUse;
				if (dUse > maxScale)
				{
					dUse = maxScale;
				}
				else if (dUse < 0.1f)
				{
					dUse = 0.1f;
				}
				if (tess.normal[i][2] > lightScaleTolerance ||
					tess.normal[i][2] < -lightScaleTolerance ||
					tess.normal[i][1] > lightScaleTolerance ||
					tess.normal[i][1] < -lightScaleTolerance)
				{ //if not perfectly flat, we must use a constant dist
					scale = 1.0f / radius;
				}
				else
				{
					scale = 1.0f / (radius*dUse);
				}

				texCoords[0] = 0.5f + dist[1] * scale;
				texCoords[1] = 0.5f + dist[2] * scale;
			}
			
			clip = 0;
			if ( texCoords[0] < 0.0f ) {
				clip |= 1;
			} else if ( texCoords[0] > 1.0f ) {
				clip |= 2;
			}
			if ( texCoords[1] < 0.0f ) {
				clip |= 4;
			} else if ( texCoords[1] > 1.0f ) {
				clip |= 8;
			}
			// modulate the strength based on the height and color
			if ( dist[bestIndex] > radius ) {
				clip |= 16;
				modulate = 0.0f;
			} else if ( dist[bestIndex] < -radius ) {
				clip |= 32;
				modulate = 0.0f;
			} else {
				dist[bestIndex] = Q_fabs(dist[bestIndex]);
				if ( dist[bestIndex] < radius * 0.5f ) {
					modulate = 1.0f;
				} else {
					modulate = 2.0f * (radius - dist[bestIndex]) * scale;
				}
			}
			clipBits[i] = clip;

			colors[0] = myftol(floatColor[0] * modulate);
			colors[1] = myftol(floatColor[1] * modulate);
			colors[2] = myftol(floatColor[2] * modulate);
			colors[3] = 255;
		}
		// build a list of triangles that need light
		numIndexes = 0;
		for ( i = 0 ; i < tess.numIndexes ; i += 3 ) {
			int		a, b, c;

			a = tess.indexes[i];
			b = tess.indexes[i+1];
			c = tess.indexes[i+2];
			if ( clipBits[a] & clipBits[b] & clipBits[c] ) {
				continue;	// not lighted
			}
			hitIndexes[numIndexes] = a;
			hitIndexes[numIndexes+1] = b;
			hitIndexes[numIndexes+2] = c;
			numIndexes += 3;
		}

		if ( !numIndexes ) {
			continue;
		}

		//don't have fog enabled when we redraw with alpha test, or it will double over
		//and screw the tri up -rww
		if (r_drawfog->value == 2 && 
			tr.world &&
			(tess.fogNum == tr.world->globalFog || tess.fogNum == tr.world->numfogs))
		{
			fogging = glIsEnabled(GL_FOG);

			if (fogging)
			{
				glDisable(GL_FOG);
			}
		}
		else
		{
			fogging = 0;
		}


		dStage = NULL;
		if (tess.shader && glActiveTextureARB)
		{
			int i = 0;
			while (i < tess.shader->numUnfoggedPasses)
			{
				const int blendBits = (GLS_SRCBLEND_BITS+GLS_DSTBLEND_BITS);
				if (((tess.shader->stages[i].bundle[0].image && !tess.shader->stages[i].bundle[0].isLightmap && !tess.shader->stages[i].bundle[0].numTexMods && tess.shader->stages[i].bundle[0].tcGen != TCGEN_ENVIRONMENT_MAPPED && tess.shader->stages[i].bundle[0].tcGen != TCGEN_FOG) ||
					 (tess.shader->stages[i].bundle[1].image && !tess.shader->stages[i].bundle[1].isLightmap && !tess.shader->stages[i].bundle[1].numTexMods && tess.shader->stages[i].bundle[1].tcGen != TCGEN_ENVIRONMENT_MAPPED && tess.shader->stages[i].bundle[1].tcGen != TCGEN_FOG)) &&
					(tess.shader->stages[i].stateBits & blendBits) == 0 )
				{ //only use non-lightmap opaque stages
                    dStage = &tess.shader->stages[i];
					break;
				}
				i++;
			}
		}

		if (dStage)
		{
			GL_SelectTexture( 0 );
			GL_State(0);
			glTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );
			if (dStage->bundle[0].image && !dStage->bundle[0].isLightmap && !dStage->bundle[0].numTexMods && dStage->bundle[0].tcGen != TCGEN_ENVIRONMENT_MAPPED && dStage->bundle[0].tcGen != TCGEN_FOG)
			{
				R_BindAnimatedImage( &dStage->bundle[0] );
			}
			else
			{
				R_BindAnimatedImage( &dStage->bundle[1] );
			}

			GL_SelectTexture( 1 );
			glEnable( GL_TEXTURE_2D );
			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glTexCoordPointer( 2, GL_FLOAT, 0, texCoordsArray[0] );
			glEnableClientState( GL_COLOR_ARRAY );
			glColorPointer( 4, GL_UNSIGNED_BYTE, 0, colorArray );
			GL_Bind( tr.dlightImage );
			GL_TexEnv( GL_MODULATE );

			GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL);// | GLS_ATEST_GT_0);

			R_DrawElements( numIndexes, hitIndexes );

			glDisable( GL_TEXTURE_2D );
			GL_SelectTexture(0);
		}
		else
		{
			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glTexCoordPointer( 2, GL_FLOAT, 0, texCoordsArray[0] );

			glEnableClientState( GL_COLOR_ARRAY );
			glColorPointer( 4, GL_UNSIGNED_BYTE, 0, colorArray );

			GL_Bind( tr.dlightImage );
			// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light
			// where they aren't rendered
			//if ( dl->additive ) {
			//	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
			//}
			//else
			{
				GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
			}

			R_DrawElements( numIndexes, hitIndexes );
		}

		if (fogging)
		{
			glEnable(GL_FOG);
		}

		backEnd.pc.c_totalIndexes += numIndexes;
		backEnd.pc.c_dlightIndexes += numIndexes;
	}
}
#endif // VV_LIGHTING

#ifdef VV_LIGHTING
/*
===================
ProjectDlightTextureVV

SP Xbox normally asks the Vicarious Visions light-effects backend to render
dynamic lights, but that backend is stubbed in this source drop. Project the
same VVLightMan lights with the stock software texture-coordinate pass.
===================
*/
static void ProjectDlightTextureVV( void ) {
	int		i, l;
	vec3_t	origin;
	float	*texCoords;
	byte	*colors;
	byte	clipBits[SHADER_MAX_VERTEXES];
	MAC_STATIC float	texCoordsArray[SHADER_MAX_VERTEXES][2];
	byte	colorArray[SHADER_MAX_VERTEXES][4];
	glIndex_t	hitIndexes[SHADER_MAX_INDEXES];
	int		numIndexes;
	float	scale;
	float	radius;
	int		fogging;
	vec3_t	floatColor;

	if ( !VVLightMan.num_dlights || !tr.dlightImage ) {
		return;
	}

	for ( l = 0 ; l < VVLightMan.num_dlights ; l++ ) {
		VVdlight_t	*dl;

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;
		}

		texCoords = texCoordsArray[0];
		colors = colorArray[0];

		dl = &VVLightMan.dlights[l];
		VectorCopy( dl->transformed, origin );
		radius = dl->radius;
		scale = 1.0f / radius;

		floatColor[0] = dl->color[0] * 255.0f;
		floatColor[1] = dl->color[1] * 255.0f;
		floatColor[2] = dl->color[2] * 255.0f;

		for ( i = 0 ; i < tess.numVertexes ; i++, texCoords += 2, colors += 4 ) {
			vec3_t	dist;
			int		clip;
			float	modulate;
			int		axis = 1;
			int		bestIndex = 0;
			float	greatest = tess.normal[i][0];
			float	dUse = 0.0f;
			const float maxScale = 1.5f;
			const float maxGroundScale = 1.4f;
			const float lightScaleTolerance = 0.1f;

			backEnd.pc.c_dlightVertexes++;
			VectorSubtract( origin, tess.xyz[i], dist );

			if (greatest < 0.0f) {
				greatest = -greatest;
			}

			if (VectorCompare(tess.normal[i], vec3_origin)) {
				bestIndex = 2;
			} else {
				while (axis < 3) {
					if ((tess.normal[i][axis] > greatest && tess.normal[i][axis] > 0.0f) ||
						(tess.normal[i][axis] < -greatest && tess.normal[i][axis] < 0.0f)) {
						greatest = tess.normal[i][axis];
						if (greatest < 0.0f) {
							greatest = -greatest;
						}
						bestIndex = axis;
					}
					axis++;
				}
			}

			if (bestIndex == 2) {
				dUse = Q_fabs(origin[2]-tess.xyz[i][2]);
				dUse = (radius*0.5f)/dUse;
				if (dUse > maxGroundScale) {
					dUse = maxGroundScale;
				} else if (dUse < 0.1f) {
					dUse = 0.1f;
				}
				if (VectorCompare(tess.normal[i], vec3_origin) ||
					tess.normal[i][0] > lightScaleTolerance || tess.normal[i][0] < -lightScaleTolerance ||
					tess.normal[i][1] > lightScaleTolerance || tess.normal[i][1] < -lightScaleTolerance) {
					scale = 1.0f / radius;
				} else {
					scale = 1.0f / (radius*dUse);
				}
				texCoords[0] = 0.5f + dist[0] * scale;
				texCoords[1] = 0.5f + dist[1] * scale;
			} else if (bestIndex == 1) {
				dUse = Q_fabs(origin[1]-tess.xyz[i][1]);
				dUse = (radius*0.5f)/dUse;
				if (dUse > maxScale) {
					dUse = maxScale;
				} else if (dUse < 0.1f) {
					dUse = 0.1f;
				}
				if (tess.normal[i][0] > lightScaleTolerance || tess.normal[i][0] < -lightScaleTolerance ||
					tess.normal[i][2] > lightScaleTolerance || tess.normal[i][2] < -lightScaleTolerance) {
					scale = 1.0f / radius;
				} else {
					scale = 1.0f / (radius*dUse);
				}
				texCoords[0] = 0.5f + dist[0] * scale;
				texCoords[1] = 0.5f + dist[2] * scale;
			} else {
				dUse = Q_fabs(origin[0]-tess.xyz[i][0]);
				dUse = (radius*0.5f)/dUse;
				if (dUse > maxScale) {
					dUse = maxScale;
				} else if (dUse < 0.1f) {
					dUse = 0.1f;
				}
				if (tess.normal[i][2] > lightScaleTolerance || tess.normal[i][2] < -lightScaleTolerance ||
					tess.normal[i][1] > lightScaleTolerance || tess.normal[i][1] < -lightScaleTolerance) {
					scale = 1.0f / radius;
				} else {
					scale = 1.0f / (radius*dUse);
				}
				texCoords[0] = 0.5f + dist[1] * scale;
				texCoords[1] = 0.5f + dist[2] * scale;
			}

			clip = 0;
			if ( texCoords[0] < 0.0f ) {
				clip |= 1;
			} else if ( texCoords[0] > 1.0f ) {
				clip |= 2;
			}
			if ( texCoords[1] < 0.0f ) {
				clip |= 4;
			} else if ( texCoords[1] > 1.0f ) {
				clip |= 8;
			}
			if ( dist[bestIndex] > radius ) {
				clip |= 16;
				modulate = 0.0f;
			} else if ( dist[bestIndex] < -radius ) {
				clip |= 32;
				modulate = 0.0f;
			} else {
				dist[bestIndex] = Q_fabs(dist[bestIndex]);
				if ( dist[bestIndex] < radius * 0.5f ) {
					modulate = 1.0f;
				} else {
					modulate = 2.0f * (radius - dist[bestIndex]) * scale;
				}
			}
			clipBits[i] = clip;

			// The Xbox color-array shim expects packed D3D byte order in memory.
			colors[0] = myftol(floatColor[2] * modulate);
			colors[1] = myftol(floatColor[1] * modulate);
			colors[2] = myftol(floatColor[0] * modulate);
			colors[3] = 255;
		}

		numIndexes = 0;
		for ( i = 0 ; i < tess.numIndexes ; i += 3 ) {
			int		a = tess.indexes[i];
			int		b = tess.indexes[i+1];
			int		c = tess.indexes[i+2];

			if ( clipBits[a] & clipBits[b] & clipBits[c] ) {
				continue;
			}
			hitIndexes[numIndexes] = a;
			hitIndexes[numIndexes+1] = b;
			hitIndexes[numIndexes+2] = c;
			numIndexes += 3;
		}

		if ( !numIndexes ) {
			continue;
		}

		if (r_drawfog->value == 2 && tr.world &&
			(tess.fogNum == tr.world->globalFog || tess.fogNum == tr.world->numfogs)) {
			fogging = glIsEnabled(GL_FOG);
			if (fogging) {
				glDisable(GL_FOG);
			}
		} else {
			fogging = 0;
		}

		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		glTexCoordPointer( 2, GL_FLOAT, 0, texCoordsArray[0] );

		glEnableClientState( GL_COLOR_ARRAY );
		glColorPointer( 4, GL_UNSIGNED_BYTE, 0, colorArray );

		GL_Bind( tr.dlightImage );
		GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );

		R_DrawElements( numIndexes, hitIndexes );

		if (fogging) {
			glEnable(GL_FOG);
		}

		backEnd.pc.c_totalIndexes += numIndexes;
		backEnd.pc.c_dlightIndexes += numIndexes;
	}
}
#endif // VV_LIGHTING


/*
===================
RB_FogPass

Blends a fog texture on top of everything else
===================
*/
static void RB_FogPass( void ) {
	fog_t		*fog;
	int			i;

#ifdef _XBOX
	RB_XboxLogRenderSuspectSurface("RB_FogPass");
#endif

	glEnableClientState( GL_COLOR_ARRAY );
	glColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors );

	glEnableClientState( GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );

	fog = tr.world->fogs + tess.fogNum;

	for ( i = 0; i < tess.numVertexes; i++ ) {
		* ( int * )&tess.svars.colors[i] = fog->colorInt;
	}

	RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[0] );

	GL_Bind( tr.fogImage );

	if ( tess.shader->fogPass == FP_EQUAL ) {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
	} else {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	}

	R_DrawElements( tess.numIndexes, tess.indexes );
}


/*
===============
ComputeColors
===============
*/
#ifdef _XBOX
static void ComputeColors( shaderStage_t *pStage, alphaGen_t forceAlphaGen, colorGen_t forceRGBGen )
{
	int i;

	if ( tess.shader != tr.projectionShadowShader && tess.shader != tr.shadowShader && 
		( backEnd.currentEntity->e.renderfx & (RF_DISINTEGRATE1|RF_DISINTEGRATE2)))
	{
		RB_CalcDisintegrateColors( (unsigned char *)tess.svars.colors, (colorGen_t)pStage->rgbGen );
		RB_CalcDisintegrateVertDeform();

		// We've done some custom alpha and color stuff, so we can skip the rest.  Let it do fog though
		forceRGBGen = CGEN_SKIP;
		forceAlphaGen = AGEN_SKIP;
	}

	//
	// rgbGen
	//
	if ( !forceRGBGen )
	{
		forceRGBGen = (colorGen_t)pStage->rgbGen;
	}

	if ( backEnd.currentEntity->e.renderfx & RF_VOLUMETRIC ) // does not work for rotated models, technically, this should also be a CGEN type, but that would entail adding new shader commands....which is too much work for one thing
	{
		int			i;
		float		*normal, dot;
		DWORD *color;
		int			numVertexes;

		normal = tess.normal[0];
		color = tess.svars.colors;

		numVertexes = tess.numVertexes;

		for ( i = 0 ; i < numVertexes ; i++, normal += 4, color ++) 
		{
			dot = DotProduct( normal, backEnd.refdef.viewaxis[0] );

			dot *= dot * dot * dot;

			if ( dot < 0.2f ) // so low, so just clamp it
			{
				dot = 0.0f;
			}

			*color = D3DCOLOR_RGBA( (int)(backEnd.currentEntity->e.shaderRGBA[0] * (1-dot)),
				(int)(backEnd.currentEntity->e.shaderRGBA[0] * (1-dot)),
				(int)(backEnd.currentEntity->e.shaderRGBA[0] * (1-dot)),
				(int)(backEnd.currentEntity->e.shaderRGBA[0] * (1-dot)) );
		}

		forceRGBGen = CGEN_SKIP;
		forceAlphaGen = AGEN_SKIP;
	}

	if ( !forceAlphaGen )	//set this up so we can override below
	{
		forceAlphaGen = (alphaGen_t)pStage->alphaGen;
	}

	DWORD color;

	switch ( forceRGBGen )
	{
	case CGEN_SKIP:
		break;
	case CGEN_IDENTITY:
		memset( tess.svars.colors, 0xffffffff, sizeof(DWORD) * tess.numVertexes );
		break;
	default:
	case CGEN_IDENTITY_LIGHTING:
		color = ((tr.identityLightByte & 0xff) << 24 |
			(tr.identityLightByte & 0xff) << 16 |
			(tr.identityLightByte & 0xff) << 8  |
			(tr.identityLightByte & 0xff) << 0);
		memset( tess.svars.colors, color, sizeof(DWORD) * tess.numVertexes );
		break;
	case CGEN_LIGHTING_DIFFUSE:
#ifdef VV_LIGHTING
		VVLightMan.RB_CalcDiffuseColor( tess.svars.colors );
#else
		RB_CalcDiffuseColor( ( unsigned char * ) tess.svars.colors );
#endif
		break;
	case CGEN_LIGHTING_DIFFUSE_ENTITY:
#ifdef VV_LIGHTING
		VVLightMan.RB_CalcDiffuseEntityColor( tess.svars.colors );
#else
		RB_CalcDiffuseEntityColor( ( unsigned char * ) tess.svars.colors );
#endif
		if ( forceAlphaGen == AGEN_IDENTITY && 
			backEnd.currentEntity->e.shaderRGBA[3] == 0xff 
			)
		{
			forceAlphaGen = AGEN_SKIP;	//already got it in this set since it does all 4 components
		}
		break;
	case CGEN_EXACT_VERTEX:
		for ( i = 0; i < tess.numVertexes; i++ )
		{
			tess.svars.colors[i] = D3DCOLOR_RGBA( (int)(tess.vertexColors[i][0]),
												  (int)(tess.vertexColors[i][1]),
												  (int)(tess.vertexColors[i][2]),
												  (int)(tess.vertexColors[i][3]) );
		}
		break;
	case CGEN_CONST:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			tess.svars.colors[i] = D3DCOLOR_RGBA( (int)(pStage->constantColor[0]),
												  (int)(pStage->constantColor[1]),
												  (int)(pStage->constantColor[2]),
												  (int)(pStage->constantColor[3]) );
		}
		break;
	case CGEN_VERTEX:
		if ( tr.identityLight == 1 )
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				tess.svars.colors[i] = D3DCOLOR_RGBA( (int)(tess.vertexColors[i][0]),
					(int)(tess.vertexColors[i][1]),
					(int)(tess.vertexColors[i][2]),
					(int)(tess.vertexColors[i][3]));
			}
		}
		else
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				tess.svars.colors[i] = D3DCOLOR_RGBA( (int)(tess.vertexColors[i][0] * tr.identityLight),
					(int)(tess.vertexColors[i][1] * tr.identityLight),
					(int)(tess.vertexColors[i][2] * tr.identityLight),
					(int)(tess.vertexColors[i][3]));
			}
		}
		break;
	case CGEN_ONE_MINUS_VERTEX:
		if ( tr.identityLight == 1 )
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				tess.svars.colors[i] = D3DCOLOR_XRGB( (int)(255 - tess.vertexColors[i][0]),
					(int)(255 - tess.vertexColors[i][1]),
					(int)(255 - tess.vertexColors[i][2]));
			}
		}
		else
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				tess.svars.colors[i] = D3DCOLOR_XRGB( (int)((255 - tess.vertexColors[i][0]) * tr.identityLight),
					(int)((255 - tess.vertexColors[i][1]) * tr.identityLight),
					(int)((255 - tess.vertexColors[i][2]) * tr.identityLight));
			}
		}
		break;
	case CGEN_FOG:
		{
			fog_t		*fog;

			fog = tr.world->fogs + tess.fogNum;

			for ( i = 0; i < tess.numVertexes; i++ ) {
				* ( int * )&tess.svars.colors[i] = fog->colorInt;
			}
		}
		break;
	case CGEN_WAVEFORM:
		RB_CalcWaveColor( &pStage->rgbWave, tess.svars.colors );
		break;
	case CGEN_ENTITY:
		RB_CalcColorFromEntity( tess.svars.colors );
		if ( forceAlphaGen == AGEN_IDENTITY && 
			backEnd.currentEntity->e.shaderRGBA[3] == 0xff 
			)
		{
			forceAlphaGen = AGEN_SKIP;	//already got it in this set since it does all 4 components
		}

		break;
	case CGEN_ONE_MINUS_ENTITY:
		RB_CalcColorFromOneMinusEntity( tess.svars.colors );
		break;
	case CGEN_LIGHTMAPSTYLE:
		for ( i = 0; i < tess.numVertexes; i++ ) 
		{
			tess.svars.colors[i] = *(DWORD *)styleColors[pStage->lightmapStyle];
		}
		break;
	}

	//
	// alphaGen
	//
	DWORD rgb;
	switch ( forceAlphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( forceRGBGen != CGEN_IDENTITY &&  forceRGBGen != CGEN_LIGHTING_DIFFUSE ) {
			if ( ( forceRGBGen == CGEN_VERTEX && tr.identityLight != 1 ) ||
				forceRGBGen != CGEN_VERTEX ) {
					for ( i = 0; i < tess.numVertexes; i++ ) {
						rgb = (DWORD)((tess.svars.colors[i]) & 0x00ffffff);
						tess.svars.colors[i] = rgb | ((255 & 0xff) << 24);
					}
				}
		}
		break;
	case AGEN_CONST:
		if ( forceRGBGen != CGEN_CONST ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				rgb = (DWORD)((tess.svars.colors[i]) & 0x00ffffff);
				tess.svars.colors[i] = rgb | ((pStage->constantColor[3] & 0xff) << 24);
			}
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha( &pStage->alphaWave, tess.svars.colors );
		break;
	case AGEN_LIGHTING_SPECULAR:
		RB_CalcSpecularAlpha( tess.svars.colors );
		break;
	case AGEN_ENTITY:
		if ( forceRGBGen != CGEN_ENTITY ) { //already got it in the CGEN_entity since it does all 4 components
			RB_CalcAlphaFromEntity( tess.svars.colors );
		}
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( tess.svars.colors );
		break;
	case AGEN_VERTEX:
		if ( forceRGBGen != CGEN_VERTEX ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				rgb = (DWORD)((tess.svars.colors[i]) & 0x00ffffff);
				tess.svars.colors[i] = rgb | ((tess.vertexColors[i][3] & 0xff) << 24);
			}
		}
		break;
	case AGEN_ONE_MINUS_VERTEX:
		for ( i = 0; i < tess.numVertexes; i++ )
		{
			rgb = (DWORD)((tess.svars.colors[i]) & 0x00ffffff);
			tess.svars.colors[i] = rgb | (((255 - tess.vertexColors[i][3]) & 0xff) << 24);
		}
		break;
	case AGEN_PORTAL:
		{
			unsigned char alpha;

			for ( i = 0; i < tess.numVertexes; i++ )
			{
				float len;
				vec3_t v;

				VectorSubtract( tess.xyz[i], backEnd.viewParms.or.origin, v );
				len = VectorLength( v );

				len /= tess.shader->portalRange;

				if ( len < 0 )
				{
					alpha = 0;
				}
				else if ( len > 1 )
				{
					alpha = 0xff;
				}
				else
				{
					alpha = len * 0xff;
				}

				rgb = (DWORD)((tess.svars.colors[i]) & 0x00ffffff);
				tess.svars.colors[i] = rgb | ((alpha & 0xff) << 24);
			}
		}
		break;
	case AGEN_BLEND:
		if ( forceRGBGen != CGEN_VERTEX ) 
		{
			for ( i = 0; i < tess.numVertexes; i++ ) 
			{
				rgb = (DWORD)((tess.svars.colors[i]) & 0x00ffffff);
				tess.svars.colors[i] = rgb | ((tess.vertexAlphas[i][pStage->index] & 0xff) << 24);
			}
		}
		break;
	}

	//
	// fog adjustment for colors to fade out as fog increases
	//
	if ( tess.fogNum )
	{
		switch ( pStage->adjustColorsForFog )
		{
		case ACFF_MODULATE_RGB:
			RB_CalcModulateColorsByFog( tess.svars.colors );
			break;
		case ACFF_MODULATE_ALPHA:
			RB_CalcModulateAlphasByFog( tess.svars.colors );
			break;
		case ACFF_MODULATE_RGBA:
			RB_CalcModulateRGBAsByFog( tess.svars.colors );
			break;
		case ACFF_NONE:
			break;
		}
	}
}

#else // _XBOX

static void ComputeColors( shaderStage_t *pStage, alphaGen_t forceAlphaGen, colorGen_t forceRGBGen )
{
	int i;

	if ( tess.shader != tr.projectionShadowShader && tess.shader != tr.shadowShader && 
			( backEnd.currentEntity->e.renderfx & (RF_DISINTEGRATE1|RF_DISINTEGRATE2)))
	{
		RB_CalcDisintegrateColors( (unsigned char *)tess.svars.colors, pStage->rgbGen );
		RB_CalcDisintegrateVertDeform();

		// We've done some custom alpha and color stuff, so we can skip the rest.  Let it do fog though
		forceRGBGen = CGEN_SKIP;
		forceAlphaGen = AGEN_SKIP;
	}

	//
	// rgbGen
	//
	if ( !forceRGBGen )
	{
		forceRGBGen = pStage->rgbGen;
	}

	if ( backEnd.currentEntity->e.renderfx & RF_VOLUMETRIC ) // does not work for rotated models, technically, this should also be a CGEN type, but that would entail adding new shader commands....which is too much work for one thing
	{
		int			i;
		float		*normal, dot;
		unsigned char *color;
		int			numVertexes;

		normal = tess.normal[0];
		color = tess.svars.colors[0];

		numVertexes = tess.numVertexes;

		for ( i = 0 ; i < numVertexes ; i++, normal += 4, color += 4) 
		{
			dot = DotProduct( normal, backEnd.refdef.viewaxis[0] );

			dot *= dot * dot * dot;

			if ( dot < 0.2f ) // so low, so just clamp it
			{
				dot = 0.0f;
			}

			color[0] = color[1] = color[2] = color[3] = myftol( backEnd.currentEntity->e.shaderRGBA[0] * (1-dot) );
		}

		forceRGBGen = CGEN_SKIP;
		forceAlphaGen = AGEN_SKIP;
	}

	if ( !forceAlphaGen )	//set this up so we can override below
	{
		forceAlphaGen = pStage->alphaGen;
	}

	switch ( forceRGBGen )
	{
		case CGEN_SKIP:
			break;
		case CGEN_IDENTITY:
			memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );
			break;
		default:
		case CGEN_IDENTITY_LIGHTING:
			memset( tess.svars.colors, tr.identityLightByte, tess.numVertexes * 4 );
			break;
		case CGEN_LIGHTING_DIFFUSE:
			RB_CalcDiffuseColor( ( unsigned char * ) tess.svars.colors );
			break;
		case CGEN_LIGHTING_DIFFUSE_ENTITY:
			RB_CalcDiffuseEntityColor( ( unsigned char * ) tess.svars.colors );

			if ( forceAlphaGen == AGEN_IDENTITY && 
				 backEnd.currentEntity->e.shaderRGBA[3] == 0xff 
				)
			{
				forceAlphaGen = AGEN_SKIP;	//already got it in this set since it does all 4 components
			}
			break;
		case CGEN_EXACT_VERTEX:
			memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			break;
		case CGEN_CONST:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				*(int *)tess.svars.colors[i] = *(int *)pStage->constantColor;
			}
			break;
		case CGEN_VERTEX:
			if ( tr.identityLight == 1 )
			{
				memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = tess.vertexColors[i][0] * tr.identityLight;
					tess.svars.colors[i][1] = tess.vertexColors[i][1] * tr.identityLight;
					tess.svars.colors[i][2] = tess.vertexColors[i][2] * tr.identityLight;
					tess.svars.colors[i][3] = tess.vertexColors[i][3];
				}
			}
			break;
		case CGEN_ONE_MINUS_VERTEX:
			if ( tr.identityLight == 1 )
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = 255 - tess.vertexColors[i][0];
					tess.svars.colors[i][1] = 255 - tess.vertexColors[i][1];
					tess.svars.colors[i][2] = 255 - tess.vertexColors[i][2];
				}
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = ( 255 - tess.vertexColors[i][0] ) * tr.identityLight;
					tess.svars.colors[i][1] = ( 255 - tess.vertexColors[i][1] ) * tr.identityLight;
					tess.svars.colors[i][2] = ( 255 - tess.vertexColors[i][2] ) * tr.identityLight;
				}
			}
			break;
		case CGEN_FOG:
			{
				fog_t		*fog;

				fog = tr.world->fogs + tess.fogNum;

				for ( i = 0; i < tess.numVertexes; i++ ) {
					* ( int * )&tess.svars.colors[i] = fog->colorInt;
				}
			}
			break;
		case CGEN_WAVEFORM:
			RB_CalcWaveColor( &pStage->rgbWave, ( unsigned char * ) tess.svars.colors );
			break;
		case CGEN_ENTITY:
			RB_CalcColorFromEntity( ( unsigned char * ) tess.svars.colors );
			if ( forceAlphaGen == AGEN_IDENTITY && 
				 backEnd.currentEntity->e.shaderRGBA[3] == 0xff 
				)
			{
				forceAlphaGen = AGEN_SKIP;	//already got it in this set since it does all 4 components
			}

			break;
		case CGEN_ONE_MINUS_ENTITY:
			RB_CalcColorFromOneMinusEntity( ( unsigned char * ) tess.svars.colors );
			break;
		case CGEN_LIGHTMAPSTYLE:
			for ( i = 0; i < tess.numVertexes; i++ ) 
			{
				* ( int * )&tess.svars.colors[i] = *(int *)styleColors[pStage->lightmapStyle];
			}
			break;
		}

	//
	// alphaGen
	//

	switch ( forceAlphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( forceRGBGen != CGEN_IDENTITY &&  forceRGBGen != CGEN_LIGHTING_DIFFUSE ) {
			if ( ( forceRGBGen == CGEN_VERTEX && tr.identityLight != 1 ) ||
				 forceRGBGen != CGEN_VERTEX ) {
				for ( i = 0; i < tess.numVertexes; i++ ) {
					tess.svars.colors[i][3] = 0xff;
				}
			}
		}
		break;
	case AGEN_CONST:
		if ( forceRGBGen != CGEN_CONST ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = pStage->constantColor[3];
			}
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha( &pStage->alphaWave, ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_LIGHTING_SPECULAR:
		RB_CalcSpecularAlpha( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_ENTITY:
		if ( forceRGBGen != CGEN_ENTITY ) { //already got it in the CGEN_entity since it does all 4 components
			RB_CalcAlphaFromEntity( ( unsigned char * ) tess.svars.colors );
		}
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_VERTEX:
		if ( forceRGBGen != CGEN_VERTEX ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = tess.vertexColors[i][3];
			}
		}
        break;
    case AGEN_ONE_MINUS_VERTEX:
		for ( i = 0; i < tess.numVertexes; i++ )
		{
			tess.svars.colors[i][3] = 255 - tess.vertexColors[i][3];
		}
		break;
	case AGEN_PORTAL:
		{
			unsigned char alpha;

			for ( i = 0; i < tess.numVertexes; i++ )
			{
				float len;
				vec3_t v;

				VectorSubtract( tess.xyz[i], backEnd.viewParms.or.origin, v );
				len = VectorLength( v );

				len /= tess.shader->portalRange;

				if ( len < 0 )
				{
					alpha = 0;
				}
				else if ( len > 1 )
				{
					alpha = 0xff;
				}
				else
				{
					alpha = len * 0xff;
				}

				tess.svars.colors[i][3] = alpha;
			}
		}
		break;
	case AGEN_BLEND:
		if ( forceRGBGen != CGEN_VERTEX ) 
		{
			for ( i = 0; i < tess.numVertexes; i++ ) 
			{
				tess.svars.colors[i][3] = tess.vertexAlphas[i][pStage->index];
			}
		}
		break;
	}

	//
	// fog adjustment for colors to fade out as fog increases
	//
	if ( tess.fogNum )
	{
		switch ( pStage->adjustColorsForFog )
		{
		case ACFF_MODULATE_RGB:
			RB_CalcModulateColorsByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_MODULATE_ALPHA:
			RB_CalcModulateAlphasByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_MODULATE_RGBA:
			RB_CalcModulateRGBAsByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_NONE:
			break;
		}
	}
}

#endif // _XBOX

/*
===============
ComputeTexCoords
===============
*/
static void ComputeTexCoords( shaderStage_t *pStage ) {
	int		i;
	int b;
#ifdef _XBOX
	const int bundleCount = pStage->bundle[1].image ? NUM_TEXTURE_BUNDLES : 1;
#else
	const int bundleCount = NUM_TEXTURE_BUNDLES;
#endif

	for ( b = 0; b < bundleCount; b++ ) {
		int tm;

		//
		// generate the texture coordinates
		//
		switch ( pStage->bundle[b].tcGen )
		{
		case TCGEN_IDENTITY:
			memset( tess.svars.texcoords[b], 0, sizeof( float ) * 2 * tess.numVertexes );
			break;
		case TCGEN_TEXTURE:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][0][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][0][1];
			}
			break;
		case TCGEN_LIGHTMAP:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][1][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][1][1];
			}
			break;
		case TCGEN_LIGHTMAP1:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][2][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][2][1];
			}
			break;
		case TCGEN_LIGHTMAP2:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][3][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][3][1];
			}
			break;
		case TCGEN_LIGHTMAP3:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][4][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][4][1];
			}
			break;
		case TCGEN_VECTOR:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[0] );
				tess.svars.texcoords[b][i][1] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[1] );
			}
			break;
		case TCGEN_FOG:
			RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_ENVIRONMENT_MAPPED:
			RB_CalcEnvironmentTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_BAD:
			return;
		}

		//
		// alter texture coordinates
		//
		for ( tm = 0; tm < pStage->bundle[b].numTexMods ; tm++ ) {
			switch ( pStage->bundle[b].texMods[tm].type )
			{
			case TMOD_NONE:
				tm = TR_MAX_TEXMODS;		// break out of for loop
				break;

			case TMOD_TURBULENT:
				RB_CalcTurbulentTexCoords( &pStage->bundle[b].texMods[tm].wave, 
						                 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ENTITY_TRANSLATE:
				RB_CalcScrollTexCoords( backEnd.currentEntity->e.shaderTexCoord,
									 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCROLL:
				RB_CalcScrollTexCoords( pStage->bundle[b].texMods[tm].translate,	//union scroll into translate
										 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCALE:
				RB_CalcScaleTexCoords( pStage->bundle[b].texMods[tm].translate,		//union scroll into translate
									 ( float * ) tess.svars.texcoords[b] );
				break;
			
			case TMOD_STRETCH:
				RB_CalcStretchTexCoords( &pStage->bundle[b].texMods[tm].wave, 
						               ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_TRANSFORM:
				RB_CalcTransformTexCoords( &pStage->bundle[b].texMods[tm],
						                 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ROTATE:
				RB_CalcRotateTexCoords( pStage->bundle[b].texMods[tm].translate[0], //union rotateSpeed into translate[0]
										( float * ) tess.svars.texcoords[b] );
				break;

			default:
				Com_Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'\n", pStage->bundle[b].texMods[tm].type, tess.shader->name );
				break;
			}
		}
	}
}

/*
** RB_IterateStagesGeneric
*/
static vec4_t	GLFogOverrideColors[GLFOGOVERRIDE_MAX] =
{
	{ 0.0, 0.0, 0.0, 1.0 },	// GLFOGOVERRIDE_NONE
	{ 0.0, 0.0, 0.0, 1.0 },	// GLFOGOVERRIDE_BLACK
	{ 1.0, 1.0, 1.0, 1.0 }	// GLFOGOVERRIDE_WHITE
};

static const float logtestExp2 = (sqrt( -log( 1.0 / 255.0 ) ));
extern bool tr_stencilled; //tr_backend.cpp
static void RB_IterateStagesGeneric( shaderCommands_t *input )
{
	int stage;
	bool	UseGLFog = false;
	bool	FogColorChange = false;
	fog_t	*fog = NULL;
#ifdef _XBOX
	qboolean forceTrace = RB_XboxForceTraceSurface();
#endif

	if (tess.fogNum && tess.shader->fogPass && (tess.fogNum == tr.world->globalFog || tess.fogNum == tr.world->numfogs) 
		&& r_drawfog->value == 2) 
	{	// only gl fog global fog and the "special fog"
		fog = tr.world->fogs + tess.fogNum;

		if (tr.rangedFog)
		{ //ranged fog, used for sniper scope
			float fStart = fog->parms.depthForOpaque;
			float fEnd = tr.distanceCull;

			if (tr.rangedFog < 0.0f)
			{ //special designer override
				fStart = -tr.rangedFog;
				fEnd = fog->parms.depthForOpaque;

				if (fStart >= fEnd)
				{
					fStart = fEnd-1.0f;
				}
			}
			else
			{
				//the greater tr.rangedFog is, the more fog we will get between the view point and cull distance
				if ((tr.distanceCull-fStart) < tr.rangedFog)
				{ //assure a minimum range between fog beginning and cutoff distance
					fStart = tr.distanceCull-tr.rangedFog;

					if (fStart < 16.0f)
					{
						fStart = 16.0f;
					}
				}
			}

			glFogi(GL_FOG_MODE, GL_LINEAR);
			glFogf(GL_FOG_START, fStart);
			glFogf(GL_FOG_END, fEnd);
		}
		else
		{
			glFogi(GL_FOG_MODE, GL_EXP2);
			glFogf(GL_FOG_DENSITY, logtestExp2 / fog->parms.depthForOpaque);
		}

		if ( g_bRenderGlowingObjects )
		{
			const float fogColor[3] = { 0.0f, 0.0f, 0.0f };
			glFogfv(GL_FOG_COLOR, fogColor );
		}
		else
		{
			glFogfv(GL_FOG_COLOR, fog->parms.color);
		}
		
		glEnable(GL_FOG);
		UseGLFog = true;
	}

	for ( stage = 0; stage < input->shader->numUnfoggedPasses; stage++ )
	{
		shaderStage_t *pStage = &tess.xstages[stage];
		if ( !pStage->active )
		{
			assert(pStage->active);//wtf?
			break;
		}

		// Reject this stage if it's not a glow stage but we are doing a glow pass.
		if ( g_bRenderGlowingObjects && !pStage->glow )
		{
			continue;
		}

		int	stateBits = pStage->stateBits;
		alphaGen_t	forceAlphaGen = (alphaGen_t)0;
		colorGen_t	forceRGBGen = (colorGen_t)0;

#ifdef _XBOX
		qboolean stefxBeamShader = RB_XboxIsEliteForceBeamShader( tess.shader );
		qboolean stefxHudShader = backEnd.projection2D && RB_XboxIsEliteForceHudShader( tess.shader );
		tess.currentPass = stage;
		if ( stage > 0 &&
			( stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) ==
				( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) &&
			tess.xstages[stage - 1].bundle[0].tcGen == TCGEN_ENVIRONMENT_MAPPED &&
			tess.xstages[stage - 1].bundle[0].image &&
			pStage->bundle[0].image &&
			!pStage->bundle[0].isLightmap )
		{
			static int s_xboxEnvBaseReplaceLogs = 0;
			if ( s_xboxEnvBaseReplaceLogs < 64 )
			{
				XBLF("JA: XBOX_ENV_BASE_REPLACE shader='%s' stage=%d prevImg='%s' baseImg='%s' oldState=0x%x",
					tess.shader ? tess.shader->name : "<null>",
					stage,
					tess.xstages[stage - 1].bundle[0].image ?
						tess.xstages[stage - 1].bundle[0].image->imgName : "<null>",
					pStage->bundle[0].image ? pStage->bundle[0].image->imgName : "<null>",
					stateBits);
				++s_xboxEnvBaseReplaceLogs;
			}
			stateBits &= ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
			stateBits |= GLS_DEPTHFUNC_EQUAL;
		}
		if ( forceTrace )
		{
			XBLF("JA: RB_IterateStagesGeneric stage enter shader='%s' stage=%d active=%d state=0x%x bundle1=%p fading=%d\n",
				tess.shader ? tess.shader->name : "<null>",
				stage,
				pStage->active,
				stateBits,
				pStage->bundle[1].image,
				input->fading);
		}
#endif

		// allow skipping out to show just lightmaps during development
#ifndef _XBOX
		if ( stage && r_lightmap->integer)
		{
			if ( !( pStage->bundle[0].isLightmap || pStage->bundle[1].isLightmap || pStage->bundle[0].vertexLightmap ) )
			{
				continue;	// need to keep going in case the LM is in a later stage
			} 
			else
			{
				stateBits = (GLS_DSTBLEND_ZERO | GLS_SRCBLEND_ONE);	//we want to replace the prior stages with this LM, not blend
			}
		}
#endif

		if ( backEnd.currentEntity )
		{
			if ( backEnd.currentEntity->e.renderfx & RF_DISINTEGRATE1 )
			{
				// we want to be able to rip a hole in the thing being disintegrated, and by doing the depth-testing it avoids some kinds of artefacts, but will probably introduce others?
				//	NOTE: adjusting the alphaFunc seems to help a bit
				stateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHMASK_TRUE | GLS_ATEST_GE_C0;
			}

			if ( backEnd.currentEntity->e.renderfx & RF_ALPHA_FADE )
			{
				if ( backEnd.currentEntity->e.shaderRGBA[3] < 255 )
				{
					stateBits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
					forceAlphaGen = AGEN_ENTITY;
				}
			}

			if ( backEnd.currentEntity->e.renderfx & RF_STEFX_FORCE_ENT_ALPHA )
			{
				forceAlphaGen = AGEN_ENTITY;
				if ( backEnd.currentEntity->e.shaderRGBA[3] < 255 && !( stateBits & GLS_ATEST_BITS ) )
				{
					stateBits &= ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_ATEST_BITS );
					stateBits |= GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_ATEST_GT_0;
				}
			}

			if ( backEnd.currentEntity->e.renderfx & RF_RGB_TINT )
			{//want to use RGBGen from ent
				forceRGBGen = CGEN_ENTITY;
			}
		}

#ifdef _XBOX
		if ( stefxBeamShader || stefxHudShader )
		{
			static int s_stefxOverlayStateAdjustBudget = 160;
			const int oldStateBits = stateBits;

			stateBits |= GLS_DEPTHTEST_DISABLE;
			stateBits &= ~( GLS_DEPTHFUNC_EQUAL | GLS_DEPTHMASK_TRUE | GLS_ATEST_BITS );

			if ( ( stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == 0 )
			{
				if ( stefxBeamShader )
				{
					stateBits |= GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
				}
				else
				{
					stateBits |= GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
				}
			}

			if ( cls.state == CA_ACTIVE && s_stefxOverlayStateAdjustBudget > 0 )
			{
				XBLF( "STEFX: RB_IterateStagesGeneric overlay state shader='%s' stage=%d hud=%d beam=%d old=0x%x new=0x%x projection2D=%d verts=%d indexes=%d",
					tess.shader ? tess.shader->name : "<null>", stage,
					stefxHudShader ? 1 : 0, stefxBeamShader ? 1 : 0,
					oldStateBits, stateBits, backEnd.projection2D,
					input->numVertexes, input->numIndexes );
				--s_stefxOverlayStateAdjustBudget;
			}
		}
#endif

		if (pStage->ss && pStage->ss->surfaceSpriteType)
		{
			// We check for surfacesprites AFTER drawing everything else
			continue;
		}

		if (UseGLFog)
		{
			if (pStage->mGLFogColorOverride)
			{
				glFogfv(GL_FOG_COLOR, GLFogOverrideColors[pStage->mGLFogColorOverride]);
				FogColorChange = true;
			}
			else if (FogColorChange && fog)
			{
				FogColorChange = false;
				glFogfv(GL_FOG_COLOR, fog->parms.color);
			}
		}

#ifdef _XBOX
		glDisable(GL_LIGHTING);
#endif

		if (!input->fading)
		{ //this means ignore this, while we do a fade-out
#ifdef _XBOX
			if ( forceTrace )
			{
				XBLF("JA: RB_IterateStagesGeneric before ComputeColors shader='%s' stage=%d\n",
					tess.shader ? tess.shader->name : "<null>", stage);
			}
#endif
			ComputeColors( pStage, forceAlphaGen, forceRGBGen );
#ifdef _XBOX
			RB_XboxLogYavinIntroModelDrawInputs( pStage, "after ComputeColors" );
#if defined(STEFX_ELITE_FORCE_SP)
			RB_XboxLogEliteForcePlayerModelDrawInputs( pStage, "after ComputeColors" );
#endif
			RB_XboxForceYavinIntroModelColors( pStage );
			RB_XboxLogYavinIntroModelDrawInputs( pStage, "after ColorForce" );
			if ( forceTrace )
			{
				XBLF("JA: RB_IterateStagesGeneric after ComputeColors shader='%s' stage=%d\n",
					tess.shader ? tess.shader->name : "<null>", stage);
			}
#endif
		}
#ifdef _XBOX
		if ( forceTrace )
		{
			XBLF("JA: RB_IterateStagesGeneric before ComputeTexCoords shader='%s' stage=%d\n",
				tess.shader ? tess.shader->name : "<null>", stage);
		}
#endif
		ComputeTexCoords( pStage );
#ifdef _XBOX
		if ( forceTrace )
		{
			XBLF("JA: RB_IterateStagesGeneric after ComputeTexCoords shader='%s' stage=%d\n",
				tess.shader ? tess.shader->name : "<null>", stage);
		}
		RB_XboxLogWorldDrawStage( "RB_IterateStagesGeneric", input, pStage, stage, stateBits );
#if defined(STEFX_ELITE_FORCE_SP)
		RB_XboxUpdateFallbackStageTelemetry( input, pStage, stage, stateBits );
#endif
		if ( RB_XboxShouldTraceSurface() )
		{
			static int s_stefxPostStagePathBudget = 96;
			if ( s_stefxPostStagePathBudget > 0 )
			{
				XBLF("STEFX: POST_STAGE_PATH shader='%s' stage=%d bundle1=%d isLightmap0=%d rgb=%d alpha=%d state=0x%x setArraysOnce=%d",
					tess.shader ? tess.shader->name : "<null>",
					stage,
					pStage->bundle[1].image ? 1 : 0,
					pStage->bundle[0].isLightmap ? 1 : 0,
					pStage->rgbGen,
					pStage->alphaGen,
					stateBits,
					setArraysOnce ? 1 : 0 );
				--s_stefxPostStagePathBudget;
			}
		}
		if ( !backEnd.projection2D && cls.state == CA_ACTIVE )
		{
			static int s_efActiveStageBudget = 12;
			if ( s_efActiveStageBudget > 0 )
			{
				const unsigned long color0 = input->svars.colors[0];
				XBLF("EF: ACTIVE_STAGE shader='%s' stage=%d passes=%d verts=%d indexes=%d state=0x%x bundle1=%d env=%d img0='%s' tex0=%d lm0=%d tc0=%d img1='%s' tex1=%d lm1=%d tc1=%d color0=0x%08lx st0=%g,%g st1=%g,%g xyz0=%g,%g,%g",
					tess.shader ? tess.shader->name : "<null>",
					stage,
					input->shader ? input->shader->numUnfoggedPasses : -1,
					input->numVertexes,
					input->numIndexes,
					stateBits,
					pStage->bundle[1].image ? 1 : 0,
					tess.shader ? tess.shader->multitextureEnv : -1,
					RB_XboxImageLogName( pStage->bundle[0].image ),
					pStage->bundle[0].image ? pStage->bundle[0].image->texnum : -1,
					pStage->bundle[0].isLightmap ? 1 : 0,
					pStage->bundle[0].tcGen,
					RB_XboxImageLogName( pStage->bundle[1].image ),
					pStage->bundle[1].image ? pStage->bundle[1].image->texnum : -1,
					pStage->bundle[1].isLightmap ? 1 : 0,
					pStage->bundle[1].tcGen,
					color0,
					input->svars.texcoords[0][0][0],
					input->svars.texcoords[0][0][1],
					input->svars.texcoords[1][0][0],
					input->svars.texcoords[1][0][1],
					input->xyz[0][0],
					input->xyz[0][1],
					input->xyz[0][2]);
				--s_efActiveStageBudget;
			}
		}
		if ( tess.shader && tess.shader->name && strstr( tess.shader->name, "textures/borg/" ) )
		{
			static int s_efBorgStageBudget = 12;
			if ( s_efBorgStageBudget > 0 )
			{
				const unsigned long color0 = input->svars.colors[0];
				const unsigned int r = (unsigned int)((color0 >> 16) & 0xff);
				const unsigned int g = (unsigned int)((color0 >> 8) & 0xff);
				const unsigned int b = (unsigned int)(color0 & 0xff);
				const unsigned int a = (unsigned int)((color0 >> 24) & 0xff);
				XBLF("EF: RB_STAGE_SAMPLE shader='%s' stage=%d passes=%d verts=%d indexes=%d state=0x%x rgb=%d alpha=%d bundle1=%d env=%d img0='%s' lm0=%d tcGen0=%d img1='%s' lm1=%d tcGen1=%d color0=%u,%u,%u,%u st0=%g,%g st1=%g,%g xyz0=%g,%g,%g",
					tess.shader->name,
					stage,
					input->shader ? input->shader->numUnfoggedPasses : -1,
					input->numVertexes,
					input->numIndexes,
					stateBits,
					pStage->rgbGen,
					pStage->alphaGen,
					pStage->bundle[1].image ? 1 : 0,
					tess.shader ? tess.shader->multitextureEnv : -1,
					RB_XboxImageLogName( pStage->bundle[0].image ),
					pStage->bundle[0].isLightmap ? 1 : 0,
					pStage->bundle[0].tcGen,
					RB_XboxImageLogName( pStage->bundle[1].image ),
					pStage->bundle[1].isLightmap ? 1 : 0,
					pStage->bundle[1].tcGen,
					r, g, b, a,
					input->svars.texcoords[0][0][0],
					input->svars.texcoords[0][0][1],
					input->svars.texcoords[1][0][0],
					input->svars.texcoords[1][0][1],
					input->xyz[0][0],
					input->xyz[0][1],
					input->xyz[0][2]);
				--s_efBorgStageBudget;
			}
		}
#endif

		if ( !setArraysOnce )
		{
			glEnableClientState( GL_COLOR_ARRAY );
			glColorPointer( 4, GL_UNSIGNED_BYTE, 0, input->svars.colors );
		}

		if (pStage->bundle[0].isLightmap && r_debugStyle->integer >= 0)
		{
			if (pStage->lightmapStyle != r_debugStyle->integer)
			{
				if (pStage->lightmapStyle == 0)
				{
					GL_State( GLS_DSTBLEND_ZERO | GLS_SRCBLEND_ZERO );
					R_DrawElements( input->numIndexes, input->indexes );
				}
				continue;
			}
		}

#ifdef VV_LIGHTING
		if(pStage->rgbGen == CGEN_LIGHTING_DIFFUSE ||
			pStage->rgbGen == CGEN_LIGHTING_DIFFUSE_ENTITY)
		{
            glEnableClientState( GL_NORMAL_ARRAY );
			glNormalPointer(GL_FLOAT, 16, tess.normal );
		}

#ifdef _XBOX
		if ((pStage->isSpecular || pStage->isEnvironment || pStage->isBumpMap) && tess.shader)
		{
			static int s_xboxLightEffectsFallbackLogCount = 0;
			if (s_xboxLightEffectsFallbackLogCount < 32)
			{
				XBLF("JA: XBOX_LIGHTEFFECTS_FALLBACK shader='%s' stage=%d spec=%d env=%d bump=%d",
					tess.shader->name,
					stage,
					pStage->isSpecular ? 1 : 0,
					pStage->isEnvironment ? 1 : 0,
					pStage->isBumpMap ? 1 : 0);
				++s_xboxLightEffectsFallbackLogCount;
			}
		}
#else
		if(pStage->isSpecular)
		{
			glEnableClientState( GL_NORMAL_ARRAY );
			glNormalPointer(GL_FLOAT, 16, tess.normal );
			if(!tess.setTangents)
                BuildTangentVectors();
			glTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoords[0] );
			R_BindAnimatedImage( &pStage->bundle[0] );
			GL_State( stateBits );
			glw_state->lightEffects->RenderSpecular();
			glDisableClientState( GL_NORMAL_ARRAY );
			continue;
		}
		if(pStage->isEnvironment)
		{
			glEnableClientState( GL_NORMAL_ARRAY );
			glNormalPointer( GL_FLOAT, 16, tess.normal );
			R_BindAnimatedImage( &pStage->bundle[0] );
			GL_State( stateBits );
			glw_state->lightEffects->RenderEnvironment();
			glDisableClientState( GL_NORMAL_ARRAY );
			continue;
		}
		if(pStage->isBumpMap)
		{
			glEnableClientState( GL_NORMAL_ARRAY );
			glNormalPointer( GL_FLOAT, 16, tess.normal );
			if(!tess.setTangents)
                BuildTangentVectors();
			GL_SelectTexture( 0 );
			R_BindAnimatedImage( &pStage->bundle[0] );
			GL_SelectTexture( 1 );
			glEnable( GL_TEXTURE_2D );
			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			R_BindAnimatedImage( &pStage->bundle[1] );
			GL_State( stateBits );
			glw_state->lightEffects->RenderBump();
			glDisable( GL_TEXTURE_2D );
			glDisableClientState( GL_TEXTURE_COORD_ARRAY );
			GL_SelectTexture( 0 );
			glDisableClientState( GL_NORMAL_ARRAY );
			continue;
		}
#endif
#endif // VV_LIGHTING
		//
		// do multitexture
		//
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		if ( RB_XboxShouldTraceSurface() )
		{
			static int s_stefxPlayerModelBranchLogs = 96;
			if ( s_stefxPlayerModelBranchLogs > 0 )
			{
				XBLF("STEFX: TRACE_BRANCH shader='%s' isPlayer=%d stage=%d bundle1=%d img0='%s' img1='%s' vertexLightmap=%d rgb=%d alpha=%d state=0x%x setArraysOnce=%d",
					tess.shader ? tess.shader->name : "<null>",
					RB_XboxIsEliteForcePlayerModelShader( tess.shader ) ? 1 : 0,
					stage,
					pStage->bundle[1].image ? 1 : 0,
					RB_XboxImageName( pStage->bundle[0].image ),
					RB_XboxImageName( pStage->bundle[1].image ),
					pStage->bundle[0].vertexLightmap ? 1 : 0,
					pStage->rgbGen,
					pStage->alphaGen,
					stateBits,
					setArraysOnce ? 1 : 0 );
				--s_stefxPlayerModelBranchLogs;
			}
		}
#endif
		if ( pStage->bundle[1].image != 0 )
		{
#ifdef _XBOX
			if ( forceTrace )
			{
				XBLF("JA: RB_IterateStagesGeneric before DrawMultitextured shader='%s' stage=%d\n",
					tess.shader ? tess.shader->name : "<null>", stage);
			}
#endif
			DrawMultitextured( input, stage );
#ifdef _XBOX
			if ( forceTrace )
			{
				XBLF("JA: RB_IterateStagesGeneric after DrawMultitextured shader='%s' stage=%d\n",
					tess.shader ? tess.shader->name : "<null>", stage);
			}
#endif
		}
		else
		{
			static bool lStencilled = false;

			if ( !setArraysOnce )
			{
				glTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoords[0] );
			}

			//
			// set state
			//
			if ( (tess.shader == tr.distortionShader) || 
				 (backEnd.currentEntity && (backEnd.currentEntity->e.renderfx & RF_DISTORTION)) )
			{ //special distortion effect -rww
				//tr.screenImage should have been set for this specific entity before we got in here.
				GL_Bind( tr.screenImage );
				GL_Cull(CT_TWO_SIDED);
			}
			else if ( pStage->bundle[0].vertexLightmap && ( r_vertexLight->integer ) && r_lightmap->integer )
			{
				GL_Bind( tr.whiteImage );
			}
			else 
#ifdef _XBOX
			{
				if ( forceTrace )
				{
					XBLF("JA: RB_IterateStagesGeneric before bind single shader='%s' stage=%d\n",
						tess.shader ? tess.shader->name : "<null>", stage);
				}
				R_BindAnimatedImage( &pStage->bundle[0] );
				if ( forceTrace )
				{
					XBLF("JA: RB_IterateStagesGeneric after bind single shader='%s' stage=%d\n",
						tess.shader ? tess.shader->name : "<null>", stage);
				}
#if defined(STEFX_ELITE_FORCE_SP)
				RB_XboxPrepareEliteForcePlayerModelDraw( pStage );
#endif
				RB_XboxPrepareYavinIntroModelDraw( pStage );
				stateBits = RB_XboxAdjustYavinIntroModelState( pStage, stateBits );
				stateBits = RB_XboxAdjustEliteForceScriptPanelState( pStage, stateBits, "RB_IterateStagesGeneric" );
#if defined(STEFX_ELITE_FORCE_SP)
				stateBits = RB_XboxAdjustEliteForceLegacyMaskedWorldOverlayState( pStage, stateBits, stage, "RB_IterateStagesGeneric" );
#endif
			}
#else
				R_BindAnimatedImage( &pStage->bundle[0] );
#endif

			if (tess.shader == tr.distortionShader &&
				glConfig.stencilBits >= 4)
			{ //draw it to the stencil buffer!
				tr_stencilled = true;
				lStencilled = true;
				glEnable(GL_STENCIL_TEST);
				// BTO - Xbox fix: High stencil bit is reserved for glow
				glStencilFunc(GL_ALWAYS, 1, 0x7F); //0xFFFFFFFF);
				glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
				glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

				//don't depthmask, don't blend.. don't do anything
				GL_State(0);
			}
			else
			{
				GL_State( stateBits );
#ifdef _XBOX
				if ( stefxBeamShader || stefxHudShader )
				{
					RB_XboxForceEliteForceOverlayD3DState( tess.shader, stefxBeamShader, "RB_IterateStagesGeneric" );
					RB_XboxPrepareEliteForceOverlayStage( pStage, stefxBeamShader, "RB_IterateStagesGeneric" );
				}
#endif
			}

			//
			// draw
			//
#ifdef _XBOX
			if ( forceTrace )
			{
				XBLF("JA: RB_IterateStagesGeneric before single draw shader='%s' stage=%d\n",
					tess.shader ? tess.shader->name : "<null>", stage);
			}
			if ( stefxBeamShader || stefxHudShader )
			{
				RB_XboxLogEliteForceOverlayDraw( pStage, stefxHudShader, stefxBeamShader, "RB_IterateStagesGeneric" );
				JkaFakeglSetEliteForceOverlayDrawContext( 1, stefxHudShader, stefxBeamShader );
			}
			RB_XboxBeginEliteForceScriptPanelFakeglState( pStage, "RB_IterateStagesGeneric" );
			RB_XboxLogYavinIntroModelDrawInputs( pStage, "before single draw" );
#if defined(STEFX_ELITE_FORCE_SP)
			RB_XboxLogEliteForcePlayerModelDrawInputs( pStage, "before single draw" );
#endif
			if ( forceTrace )
			{
				JkaFakeglSetEliteForceDrawContext( tess.shader ? tess.shader->name : "<null>", stage, 1, (unsigned int)stateBits );
			}
#endif
			R_DrawElements( input->numIndexes, input->indexes );
#ifdef _XBOX
			if ( forceTrace )
			{
				JkaFakeglSetEliteForceDrawContext( "", -1, 0, 0 );
			}
			RB_XboxEndEliteForceScriptPanelFakeglState( "RB_IterateStagesGeneric" );
			if ( stefxBeamShader || stefxHudShader )
			{
				JkaFakeglSetEliteForceOverlayDrawContext( 0, 0, 0 );
			}
#endif
#ifdef _XBOX
			if ( forceTrace )
			{
				XBLF("JA: RB_IterateStagesGeneric after single draw shader='%s' stage=%d\n",
					tess.shader ? tess.shader->name : "<null>", stage);
			}
#endif

			if (lStencilled)
			{ //re-enable the color buffer, disable stencil test
				lStencilled = false;
				glDisable(GL_STENCIL_TEST);
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			}
		}

#ifdef VV_LIGHTING
		// Lighting may have been turned on above
		glDisable(GL_LIGHTING);
		glDisableClientState( GL_NORMAL_ARRAY );

		if(tess.shader == tr.projectionShadowShader) {
			glDisable(GL_STENCIL_TEST);
		}
#endif
	}
	if (FogColorChange)
	{
		glFogfv(GL_FOG_COLOR, fog->parms.color);
	}
}

#ifdef _XBOX
qboolean RB_IsCurrentShaderTransparent( void )
{
	if ( backEnd.currentEntity )
	{
		if ( backEnd.currentEntity->e.renderfx & RF_DISINTEGRATE1 )
		{
			return qtrue;
		}
		
		if ( backEnd.currentEntity->e.renderfx & RF_ALPHA_FADE &&
			backEnd.currentEntity->e.shaderRGBA[3] < 255 )
		{
			return qtrue;
		}
	}

	for ( int stage = 0; stage < tess.shader->numUnfoggedPasses; stage++ )
	{
		if ( !(tess.xstages[stage].stateBits & (GLS_SRCBLEND_BITS|GLS_DSTBLEND_BITS)) ||
			tess.xstages[stage].stateBits & GLS_ATEST_BITS )
		{
			return qfalse;
		}
	}

	return qtrue;
}
#endif

/*
** RB_StageIteratorGeneric
*/
void RB_StageIteratorGeneric( void )
{
	shaderCommands_t *input;
	int stage;
#ifdef _XBOX
	static int traceBudget = 0;
	qboolean trace;
	qboolean forceTrace;
#endif

	input = &tess;
#ifdef _XBOX
	RB_XboxLogRenderSuspectSurface("RB_StageIteratorGeneric");
	RB_XboxLogModelShaderSurface("RB_StageIteratorGeneric");
	RB_XboxLogEliteForceIntroDraw("RB_StageIteratorGeneric");
	if ( RB_XboxShouldSkipYavinSkyOverlay( tess.shader ) )
	{
		static int s_xboxYavinSkyOverlaySkipLogBudget = 0;
		if ( s_xboxYavinSkyOverlaySkipLogBudget > 0 )
		{
			XBLF("JA: XBOX_YAVIN_SKY_OVERLAY_SKIP map='%s' shader='%s' verts=%d indexes=%d rdflags=0x%x scene=%d",
				tr.world ? tr.world->baseName : "<null>",
				tess.shader ? tess.shader->name : "<null>",
				tess.numVertexes,
				tess.numIndexes,
				backEnd.refdef.rdflags,
				tr.sceneCount);
			--s_xboxYavinSkyOverlaySkipLogBudget;
		}
		return;
	}
#endif
#ifdef _XBOX
	trace = RB_XboxShouldTraceSurface();
	forceTrace = RB_XboxForceTraceSurface();
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: RB_StageIteratorGeneric enter shader='%s' verts=%d indexes=%d passes=%d fog=%d dlight=0x%x\n",
			tess.shader ? tess.shader->name : "<null>",
			tess.numVertexes,
			tess.numIndexes,
			tess.numPasses,
			tess.fogNum,
			tess.dlightBits);
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif

	RB_DeformTessGeometry();
#ifdef _XBOX
	RB_XboxLogModelTransformProbe("after_deform");
#endif
#ifdef _XBOX
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: RB_StageIteratorGeneric after deform shader='%s'\n",
			tess.shader ? tess.shader->name : "<null>");
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif

	//
	// log this call
	//
#ifndef _XBOX
	if ( r_logFile->integer ) 
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va("--- RB_StageIteratorGeneric( %s ) ---\n", tess.shader->name) );
	}
#endif

	//
	// set face culling appropriately
	//
#ifdef _XBOX
	GL_Cull( RB_XboxEliteForceScriptPanelCullType( RB_XboxYavinIntroCullType( input->shader->cullType ) ) );
#else
	GL_Cull( input->shader->cullType );
#endif
#ifdef _XBOX
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: RB_StageIteratorGeneric after cull shader='%s' cull=%d\n",
			tess.shader ? tess.shader->name : "<null>",
			input->shader ? input->shader->cullType : -1);
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif

	// set polygon offset if necessary
	if ( input->shader->polygonOffset )
	{
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	//
	// if there is only a single pass then we can enable color
	// and texture arrays before we compile, otherwise we need
	// to avoid compiling those arrays since they will change
	// during multipass rendering
	//
	if ( tess.numPasses > 1 || input->shader->multitextureEnv )
	{
		setArraysOnce = qfalse;
		glDisableClientState (GL_COLOR_ARRAY);
		glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	}
	else
	{
		setArraysOnce = qtrue;

		glEnableClientState( GL_COLOR_ARRAY);
		glColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors );

		glEnableClientState( GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );
	}

	// If this is a glowing surface, write the glow flag into the stencil buffer
#ifdef _XBOX
	if ( r_hdreffect->integer )
	{
		// Turn on stenciling, make sure all pixels pass the test
		glw_state->device->SetRenderState( D3DRS_STENCILENABLE, TRUE );
		glw_state->device->SetRenderState( D3DRS_STENCILFUNC, D3DCMP_ALWAYS );
		// Make sure that stencil writes will hit the high bit (the one we care about)
		glw_state->device->SetRenderState( D3DRS_STENCILWRITEMASK, 0xFFFFFFFF );

		if ( input->shader->hasGlow )
		{
			// Write only the high (eighth) bit
			glw_state->device->SetRenderState( D3DRS_STENCILREF, 0x80 );
			glw_state->device->SetRenderState( D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE );
		}
		else
		{
			// Clear out the high (eighth) bit
			glw_state->device->SetRenderState( D3DRS_STENCILPASS, D3DSTENCILOP_ZERO );
		}
	}
	else
	{
#ifdef _XBOX
		if(tess.shader != tr.projectionShadowShader)
#endif
		glw_state->device->SetRenderState( D3DRS_STENCILENABLE, FALSE );
	}
#endif

	//
	// lock XYZ
	//
	glVertexPointer (3, GL_FLOAT, 16, input->xyz);	// padded for SIMD

	if (glLockArraysEXT)
	{
		glLockArraysEXT(0, input->numVertexes);
		GLimp_LogComment( "glLockArraysEXT\n" );
	}

	//
	// enable color and texcoord arrays after the lock if necessary
	//
	if ( !setArraysOnce )
	{
		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		glEnableClientState( GL_COLOR_ARRAY );
	}

	//
	// call shader function
	//
#ifdef _XBOX
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: RB_StageIteratorGeneric before iterate shader='%s'\n",
			tess.shader ? tess.shader->name : "<null>");
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif
	RB_IterateStagesGeneric( input );
#ifdef _XBOX
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: RB_StageIteratorGeneric after iterate shader='%s'\n",
			tess.shader ? tess.shader->name : "<null>");
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif

	// 
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE
		&& !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) ) ) {
#ifdef _XBOX
		if ( trace && ( traceBudget > 0 || forceTrace ) )
		{
			XBLF("JA: RB_StageIteratorGeneric before dlight shader='%s' bits=0x%x\n",
				tess.shader ? tess.shader->name : "<null>",
				tess.dlightBits);
			if ( traceBudget > 0 ) traceBudget--;
		}
#endif
#ifdef VV_LIGHTING
		glEnableClientState( GL_NORMAL_ARRAY );
		glNormalPointer(GL_FLOAT, 16, tess.normal );
		if(!tess.setTangents)
            BuildTangentVectors();
		{
			bool renderedDlights = glw_state->lightEffects->RenderDynamicLights();
#ifdef _XBOX
			static int s_xboxDlightRenderLogCount = 0;
			if (s_xboxDlightRenderLogCount < 64)
			{
				XBLF("JA: XBOX_RENDER_DLIGHT #%d shader='%s' bits=0x%x rendered=%d verts=%d indexes=%d",
					s_xboxDlightRenderLogCount,
					tess.shader ? tess.shader->name : "<null>",
					tess.dlightBits,
					renderedDlights ? 1 : 0,
					tess.numVertexes,
					tess.numIndexes);
				s_xboxDlightRenderLogCount++;
			}
#endif
			if (!renderedDlights)
			{
#ifdef _XBOX
				static int s_xboxDlightFallbackLogCount = 0;
				if (s_xboxDlightFallbackLogCount < 64)
				{
					XBLF("JA: XBOX_PROJECT_DLIGHT_FALLBACK #%d shader='%s' bits=0x%x vvLights=%d verts=%d indexes=%d",
						s_xboxDlightFallbackLogCount,
						tess.shader ? tess.shader->name : "<null>",
						tess.dlightBits,
						VVLightMan.num_dlights,
						tess.numVertexes,
						tess.numIndexes);
					s_xboxDlightFallbackLogCount++;
				}
#endif
				ProjectDlightTextureVV();
			}
		}
		glDisableClientState( GL_NORMAL_ARRAY );
#else
		if (r_dlightStyle->integer>0)
		{
			ProjectDlightTexture2();
		}
		else
		{
			ProjectDlightTexture();
		}
#endif
#ifdef _XBOX
		if ( trace && ( traceBudget > 0 || forceTrace ) )
		{
			XBLF("JA: RB_StageIteratorGeneric after dlight shader='%s'\n",
				tess.shader ? tess.shader->name : "<null>");
			if ( traceBudget > 0 ) traceBudget--;
		}
#endif
	}

	//
	// now do fog
	//
	if (tr.world && (tess.fogNum != tr.world->globalFog || r_drawfog->value != 2) && r_drawfog->value && tess.fogNum && tess.shader->fogPass)
	{
#ifdef _XBOX
		if ( trace && ( traceBudget > 0 || forceTrace ) )
		{
			XBLF("JA: RB_StageIteratorGeneric before fog shader='%s' fog=%d\n",
				tess.shader ? tess.shader->name : "<null>",
				tess.fogNum);
			if ( traceBudget > 0 ) traceBudget--;
		}
#endif
		RB_FogPass();
#ifdef _XBOX
		if ( trace && ( traceBudget > 0 || forceTrace ) )
		{
			XBLF("JA: RB_StageIteratorGeneric after fog shader='%s'\n",
				tess.shader ? tess.shader->name : "<null>");
			if ( traceBudget > 0 ) traceBudget--;
		}
#endif
	}

	// 
	// unlock arrays
	//
	if (glUnlockArraysEXT) 
	{
		glUnlockArraysEXT();
		GLimp_LogComment( "glUnlockArraysEXT\n" );
	}
#ifdef _XBOX
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: RB_StageIteratorGeneric after unlock shader='%s'\n",
			tess.shader ? tess.shader->name : "<null>");
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif

	//
	// reset polygon offset
	//
	if ( input->shader->polygonOffset )
	{
		glDisable( GL_POLYGON_OFFSET_FILL );
	}

	// Now check for surfacesprites.
	if (r_surfaceSprites->integer)
	{
		for ( stage = 1; stage < tess.shader->numUnfoggedPasses; stage++ )
		{
			if (tess.xstages[stage].ss && tess.xstages[stage].ss->surfaceSpriteType)
			{	// Draw the surfacesprite
				RB_DrawSurfaceSprites( &tess.xstages[stage], input);
			}
		}
	}

	//don't disable the hardware fog til after we do surface sprites
	if (r_drawfog->value == 2 && 
		tess.fogNum && tess.shader->fogPass &&
		(tess.fogNum == tr.world->globalFog || tess.fogNum == tr.world->numfogs))
	{
		glDisable(GL_FOG);
	}
#ifdef _XBOX
	if ( trace && ( traceBudget > 0 || forceTrace ) )
	{
		XBLF("JA: RB_StageIteratorGeneric exit shader='%s'\n",
			tess.shader ? tess.shader->name : "<null>");
		if ( traceBudget > 0 ) traceBudget--;
	}
#endif
}


/*
** RB_EndSurface
*/
void RB_EndSurface( void ) {
	shaderCommands_t *input;

#ifdef _XBOX
	g_SPXBRenderEndSurfaces++;
#endif

	input = &tess;

	if (input->numIndexes == 0) {
		return;
	}

	if (input->indexes[SHADER_MAX_INDEXES-1] != 0) {
#ifdef _XBOX
		XBLF("JA: RB_EndSurface index sentinel hit shader='%s' verts=%d indexes=%d fog=%d ent=%d reType=%d idxLast=%d\n",
			input->shader ? input->shader->name : "<null>",
			input->numVertexes,
			input->numIndexes,
			input->fogNum,
			tr.currentEntityNum,
			backEnd.currentEntity ? backEnd.currentEntity->e.reType : -1,
			input->indexes[SHADER_MAX_INDEXES-1]);
#endif
		Com_Error (ERR_DROP, "RB_EndSurface() - SHADER_MAX_INDEXES hit");
	}	
	if (input->xyz[SHADER_MAX_VERTEXES-1][0] > 0.001f || input->xyz[SHADER_MAX_VERTEXES-1][0] < -0.001f) {
#ifdef _XBOX
		XBLF("JA: RB_EndSurface vertex sentinel hit shader='%s' verts=%d indexes=%d fog=%d ent=%d reType=%d xyzLast=(%.3f %.3f %.3f %.3f)\n",
			input->shader ? input->shader->name : "<null>",
			input->numVertexes,
			input->numIndexes,
			input->fogNum,
			tr.currentEntityNum,
			backEnd.currentEntity ? backEnd.currentEntity->e.reType : -1,
			input->xyz[SHADER_MAX_VERTEXES-1][0],
			input->xyz[SHADER_MAX_VERTEXES-1][1],
			input->xyz[SHADER_MAX_VERTEXES-1][2],
			input->xyz[SHADER_MAX_VERTEXES-1][3]);
		VectorClear(input->xyz[SHADER_MAX_VERTEXES-1]);
		input->xyz[SHADER_MAX_VERTEXES-1][3] = 0.0f;
#else
		Com_Error (ERR_DROP, "RB_EndSurface() - SHADER_MAX_VERTEXES hit");
#endif
	} else {
		input->xyz[SHADER_MAX_VERTEXES-1][0] = 0.0f;
	}

	if ( tess.shader == tr.shadowShader ) {
		RB_ShadowTessEnd();
		return;
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	if ( r_debugSort->integer && r_debugSort->integer < tess.shader->sort ) {
		return;
	}

	if ( skyboxportal )
	{
		// world
		if(!(backEnd.refdef.rdflags & RDF_SKYBOXPORTAL)) 
		{
			if(tess.currentStageIteratorFunc == RB_StageIteratorSky)
			{	// don't process these tris at all
#ifdef _XBOX
				static int s_xboxSkyPortalFallbackLogBudget = 16;
				if (s_xboxSkyPortalFallbackLogBudget > 0)
				{
					XBLF("JA: XBOX_SKYPORTAL_MAIN_SKY_GATE shader='%s' verts=%d indexes=%d rdflags=0x%x drawsky=%d action=%s",
						tess.shader ? tess.shader->name : "<null>",
						tess.numVertexes,
						tess.numIndexes,
						backEnd.refdef.rdflags,
						drawskyboxportal,
						drawskyboxportal ? "skip" : "draw-main");
					--s_xboxSkyPortalFallbackLogBudget;
				}
				if (drawskyboxportal)
				{
					return;
				}
#else
				return;
#endif
			}
		}
		// portal sky
		else
		{
			if(!drawskyboxportal)
			{
				if( !(tess.currentStageIteratorFunc == RB_StageIteratorSky))
				{	// /only/ process sky tris
					return;
				}
			}
		}
	}

	//
	// update performance counters
	//
	if (!backEnd.projection2D)
	{
		backEnd.pc.c_shaders++;
		backEnd.pc.c_vertexes += tess.numVertexes;
		backEnd.pc.c_indexes += tess.numIndexes;
		backEnd.pc.c_totalIndexes += tess.numIndexes * tess.numPasses;
		if (tess.fogNum && tess.shader->fogPass && r_drawfog->value == 1)
		{	// Fogging adds an additional pass
			backEnd.pc.c_totalIndexes += tess.numIndexes;
		}
	}

	//
	// call off to shader specific tess end function
	//
#ifdef _XBOX
	{
		static int junkSkySurfaceBudget = 32;
		const qboolean traceJunkSky = (tess.shader && tess.shader->name &&
			strstr( tess.shader->name, "textures/common/junk_sky" )) ? qtrue : qfalse;
		static int activeSurfaceBudget = 12;
		if (!backEnd.projection2D && cls.state == CA_ACTIVE && activeSurfaceBudget > 0)
		{
			XBLF("EF: WORLD_SURFACE shader='%s' verts=%d indexes=%d passes=%d fog=%d dlight=0x%x ent=%d reType=%d func=%p",
				tess.shader ? tess.shader->name : "<null>",
				tess.numVertexes,
				tess.numIndexes,
				tess.numPasses,
				tess.fogNum,
				tess.dlightBits,
				tr.currentEntityNum,
				backEnd.currentEntity ? backEnd.currentEntity->e.reType : -1,
				tess.currentStageIteratorFunc);
			--activeSurfaceBudget;
		}
		if ( traceJunkSky && junkSkySurfaceBudget > 0 )
		{
			XBLF("STEFX_JUNK_SKY_ENDSURFACE before shader='%s' verts=%d indexes=%d passes=%d fog=%d dlight=0x%x ent=%d reType=%d func=%p rdflags=0x%x skyportal=%d drawsky=%d",
				tess.shader ? tess.shader->name : "<null>",
				tess.numVertexes,
				tess.numIndexes,
				tess.numPasses,
				tess.fogNum,
				tess.dlightBits,
				tr.currentEntityNum,
				backEnd.currentEntity ? backEnd.currentEntity->e.reType : -1,
				tess.currentStageIteratorFunc,
				backEnd.refdef.rdflags,
				skyboxportal,
				drawskyboxportal);
			--junkSkySurfaceBudget;
		}
		static int traceBudget = 0;
		qboolean trace = RB_XboxShouldTraceSurface();

		if ( trace && traceBudget > 0 )
		{
			XBLF("JA: RB_EndSurface before iterator shader='%s' verts=%d indexes=%d passes=%d currentPass=%d fog=%d dlight=0x%x ent=%d reType=%d func=%p\n",
				tess.shader ? tess.shader->name : "<null>",
				tess.numVertexes,
				tess.numIndexes,
				tess.numPasses,
				tess.currentPass,
				tess.fogNum,
				tess.dlightBits,
				tr.currentEntityNum,
				backEnd.currentEntity ? backEnd.currentEntity->e.reType : -1,
				tess.currentStageIteratorFunc);
			traceBudget--;
		}
	}
#endif
	tess.currentStageIteratorFunc();
#ifdef _XBOX
	{
		static int junkSkySurfaceAfterBudget = 32;
		const qboolean traceJunkSky = (tess.shader && tess.shader->name &&
			strstr( tess.shader->name, "textures/common/junk_sky" )) ? qtrue : qfalse;
		static int traceBudget = 0;
		qboolean trace = RB_XboxShouldTraceSurface();

		if ( traceJunkSky && junkSkySurfaceAfterBudget > 0 )
		{
			XBLF("STEFX_JUNK_SKY_ENDSURFACE after shader='%s' verts=%d indexes=%d passes=%d currentPass=%d skyRendered=%d",
				tess.shader ? tess.shader->name : "<null>",
				tess.numVertexes,
				tess.numIndexes,
				tess.numPasses,
				tess.currentPass,
				backEnd.skyRenderedThisView ? 1 : 0);
			--junkSkySurfaceAfterBudget;
		}

		if ( trace && traceBudget > 0 )
		{
			XBLF("JA: RB_EndSurface after iterator shader='%s' verts=%d indexes=%d passes=%d currentPass=%d\n",
				tess.shader ? tess.shader->name : "<null>",
				tess.numVertexes,
				tess.numIndexes,
				tess.numPasses,
				tess.currentPass);
			traceBudget--;
		}
	}
#endif

#ifdef _XBOX
	tess.currentPass = 0;
#endif

	//
	// draw debugging stuff
	//
	if ( r_showtris->integer ) 
	{
		DrawTris (input);
	}

	if ( r_shownormals->integer ) {
		DrawNormals (input);
	}

	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;

	GLimp_LogComment( "----------\n" );
}
