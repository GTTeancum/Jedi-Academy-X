// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"

#include "tr_local.h"

#ifdef VV_LIGHTING
#include "tr_lightmanager.h"
#endif

#ifdef _XBOX
#include "../qcommon/sparc.h"
#include "../win32/xb_log.h"
extern "C" volatile unsigned int g_SPXBSplitSlotActive;
extern "C" volatile unsigned int g_SPXBSplitSlot0WorldDelta;
extern "C" volatile unsigned int g_SPXBSplitSlot1WorldDelta;
extern "C" volatile unsigned int g_SPXBSplitSlot1WorldRetryDelta;
extern "C" volatile unsigned int g_SPXBSplitSlot1WorldFallback;
extern "C" volatile unsigned int g_SPXBSplitSlot0MarkedLeaves;
extern "C" volatile unsigned int g_SPXBSplitSlot1MarkedLeaves;
extern "C" volatile unsigned int g_SPXBSplitSlot0PvsRejected;
extern "C" volatile unsigned int g_SPXBSplitSlot1PvsRejected;
extern "C" volatile unsigned int g_SPXBSplitSlot0AreaRejected;
extern "C" volatile unsigned int g_SPXBSplitSlot1AreaRejected;
extern "C" volatile unsigned int g_SPXBSplitSlot0RootVis;
extern "C" volatile unsigned int g_SPXBSplitSlot1RootVis;
extern "C" volatile unsigned int g_SPXBSplitSlot0WorldAttempts;
extern "C" volatile unsigned int g_SPXBSplitSlot1WorldAttempts;
extern "C" volatile unsigned int g_SPXBSplitSlot0WorldCulled;
extern "C" volatile unsigned int g_SPXBSplitSlot1WorldCulled;
extern "C" volatile unsigned int g_SPXBSplitSlot0WorldAlready;
extern "C" volatile unsigned int g_SPXBSplitSlot1WorldAlready;
extern "C" volatile unsigned int g_SPXBSplitSlot0WorldAdded;
extern "C" volatile unsigned int g_SPXBSplitSlot1WorldAdded;
#endif

static bool lookingForWorstLeaf = false;

#ifdef _XBOX
static bool GetCoordsForLeaf(int leafNum, vec3_t coords)
{
	srfSurfaceFace_t *face;
	msurface_t *surf;
	int i;

	for(i=0; i<tr.world->leafs[leafNum].nummarksurfaces; i++) {
		surf = *(tr.world->marksurfaces + 
				tr.world->leafs[leafNum].firstMarkSurfNum + i);
		
		if(!surf->data || *surf->data != SF_FACE) {
			continue;
		}

		face = (srfSurfaceFace_t*)surf->data;
		Q_CastShort2Float(&coords[0], (short*)(face->srfPoints + 0));
		Q_CastShort2Float(&coords[1], (short*)(face->srfPoints + 1));
		Q_CastShort2Float(&coords[2], (short*)(face->srfPoints + 2));
		return true;
	}

	return false;
}

static const char *R_XboxMapShaderNameForSurface( const msurface_t *surf )
{
	if ( !surf || !tr.world || surf->xboxDebugShaderNum < 0 || surf->xboxDebugShaderNum >= tr.world->numShaders )
	{
		return "<bad>";
	}

	return tr.world->shaders[surf->xboxDebugShaderNum].shader;
}

static void R_XboxLogWorldSurfaceSubmit( const char *result, const msurface_t *surf, int dlightBits, qboolean noViewCount )
{
	const shader_t *shader;
	const char *shaderName;
	const char *mapShaderName;
	const char *mapName;
	int type;

	if ( !surf )
	{
		return;
	}

	shader = surf->shader;
	shaderName = shader ? shader->name : "<null>";
	mapShaderName = R_XboxMapShaderNameForSurface( surf );
	mapName = tr.world ? tr.world->name : "<noworld>";
	type = surf->data ? (int)*surf->data : -1;

	XBLF("STEFX_SURFACE_SUBMIT result='%s' map='%s' frame=%d view=%d ent=%d code=%d shaderNum=%d mapName='%s' resolved='%s' type=%d fog=%d dlight=0x%x noView=%d draw=%d sky=%d sort=%g default=%d explicit=%d passes=%d boundsMin=%g,%g,%g boundsMax=%g,%g,%g viewOrg=%g,%g,%g",
		result ? result : "<null>",
		mapName,
		tr.frameCount,
		tr.viewCount,
		tr.currentEntityNum,
		surf->xboxDebugCode,
		surf->xboxDebugShaderNum,
		mapShaderName,
		shaderName,
		type,
		surf->fogIndex,
		dlightBits,
		(int)noViewCount,
		tr.refdef.numDrawSurfs,
		(int)(shader && shader->sky != NULL),
		shader ? (double)shader->sort : -1.0,
		shader ? shader->defaultShader : -1,
		shader ? shader->explicitlyDefined : -1,
		shader ? shader->numUnfoggedPasses : -1,
		(double)surf->xboxDebugMins[0],
		(double)surf->xboxDebugMins[1],
		(double)surf->xboxDebugMins[2],
		(double)surf->xboxDebugMaxs[0],
		(double)surf->xboxDebugMaxs[1],
		(double)surf->xboxDebugMaxs[2],
		(double)tr.refdef.vieworg[0],
		(double)tr.refdef.vieworg[1],
		(double)tr.refdef.vieworg[2]);
}

static qboolean R_XboxWorldSurfaceUsesFallbackShader( const msurface_t *surf )
{
	if ( !surf || !surf->shader )
	{
		return qfalse;
	}

	return ( surf->shader == tr.defaultShader || surf->shader->defaultShader ) ? qtrue : qfalse;
}

static void R_XboxLogWorldFallbackSurface( const char *result, const msurface_t *surf, int dlightBits, qboolean noViewCount )
{
	static int s_fallbackLogBudget = 96;
	const char *mapShaderName;
	const char *shaderName;

	if ( s_fallbackLogBudget <= 0 || !R_XboxWorldSurfaceUsesFallbackShader( surf ) )
	{
		return;
	}

	mapShaderName = R_XboxMapShaderNameForSurface( surf );
	shaderName = ( surf && surf->shader && surf->shader->name ) ? surf->shader->name : "<null>";
	XBLF("STEFX_WORLD_FALLBACK result='%s' map='%s' frame=%d view=%d ent=%d code=%d shaderNum=%d mapName='%s' resolved='%s' type=%d fog=%d dlight=0x%x noView=%d draw=%d default=%d explicit=%d passes=%d",
		result ? result : "<null>",
		tr.world ? tr.world->name : "<noworld>",
		tr.frameCount,
		tr.viewCount,
		tr.currentEntityNum,
		surf ? surf->xboxDebugCode : -1,
		surf ? surf->xboxDebugShaderNum : -1,
		mapShaderName,
		shaderName,
		( surf && surf->data ) ? (int)*surf->data : -1,
		surf ? surf->fogIndex : -1,
		dlightBits,
		(int)noViewCount,
		tr.refdef.numDrawSurfs,
		( surf && surf->shader ) ? surf->shader->defaultShader : -1,
		( surf && surf->shader ) ? surf->shader->explicitlyDefined : -1,
		( surf && surf->shader ) ? surf->shader->numUnfoggedPasses : -1);
	--s_fallbackLogBudget;
}

static qboolean R_XboxTraceJunkSkySurface( const msurface_t *surf )
{
	const char *mapShaderName;
	const char *resolvedName;

	if ( !surf )
	{
		return qfalse;
	}

	mapShaderName = R_XboxMapShaderNameForSurface( surf );
	resolvedName = (surf->shader && surf->shader->name) ? surf->shader->name : "";
	return ( strstr( mapShaderName, "textures/common/junk_sky" ) ||
			 strstr( resolvedName, "textures/common/junk_sky" ) ) ? qtrue : qfalse;
}

static qboolean R_XboxTraceBorgBlackCandidateSurface( const msurface_t *surf )
{
	const char *mapShaderName;
	const char *resolvedName;

	if ( !surf )
	{
		return qfalse;
	}

	mapShaderName = R_XboxMapShaderNameForSurface( surf );
	resolvedName = (surf->shader && surf->shader->name) ? surf->shader->name : "";
	return ( !Q_stricmp( mapShaderName, "textures/common/black" ) ||
		!Q_stricmp( resolvedName, "textures/common/black" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/static2" ) ||
		!Q_stricmp( resolvedName, "textures/borg/static2" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/static2_nonsolid" ) ||
		!Q_stricmp( resolvedName, "textures/borg/static2_nonsolid" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/borgfield" ) ||
		!Q_stricmp( resolvedName, "textures/borg/borgfield" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/borgfield_nonsolid" ) ||
		!Q_stricmp( resolvedName, "textures/borg/borgfield_nonsolid" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/borgfield_opaque" ) ||
		!Q_stricmp( resolvedName, "textures/borg/borgfield_opaque" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/bars" ) ||
		!Q_stricmp( resolvedName, "textures/borg/bars" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/bars2" ) ||
		!Q_stricmp( resolvedName, "textures/borg/bars2" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/basic1" ) ||
		!Q_stricmp( resolvedName, "textures/borg/basic1" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/forceborder" ) ||
		!Q_stricmp( resolvedName, "textures/borg/forceborder" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/forceborder2" ) ||
		!Q_stricmp( resolvedName, "textures/borg/forceborder2" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/forceborder3" ) ||
		!Q_stricmp( resolvedName, "textures/borg/forceborder3" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/energy1" ) ||
		!Q_stricmp( resolvedName, "textures/borg/energy1" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/energy1_solid" ) ||
		!Q_stricmp( resolvedName, "textures/borg/energy1_solid" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/energy1_green" ) ||
		!Q_stricmp( resolvedName, "textures/borg/energy1_green" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/bigborg" ) ||
		!Q_stricmp( resolvedName, "textures/borg/bigborg" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/oddlight1" ) ||
		!Q_stricmp( resolvedName, "textures/borg/oddlight1" ) ) ? qtrue : qfalse;
}

static qboolean R_XboxTraceBorgVisibleFieldSurface( const msurface_t *surf )
{
	const char *mapShaderName;
	const char *resolvedName;

	if ( !surf )
	{
		return qfalse;
	}

	mapShaderName = R_XboxMapShaderNameForSurface( surf );
	resolvedName = (surf->shader && surf->shader->name) ? surf->shader->name : "";
	return ( !Q_stricmp( mapShaderName, "textures/borg/static2" ) ||
		!Q_stricmp( resolvedName, "textures/borg/static2" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/static2_nonsolid" ) ||
		!Q_stricmp( resolvedName, "textures/borg/static2_nonsolid" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/borgfield" ) ||
		!Q_stricmp( resolvedName, "textures/borg/borgfield" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/borgfield_nonsolid" ) ||
		!Q_stricmp( resolvedName, "textures/borg/borgfield_nonsolid" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/borgfield_opaque" ) ||
		!Q_stricmp( resolvedName, "textures/borg/borgfield_opaque" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/bars" ) ||
		!Q_stricmp( resolvedName, "textures/borg/bars" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/bars2" ) ||
		!Q_stricmp( resolvedName, "textures/borg/bars2" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/basic1" ) ||
		!Q_stricmp( resolvedName, "textures/borg/basic1" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/forceborder" ) ||
		!Q_stricmp( resolvedName, "textures/borg/forceborder" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/forceborder2" ) ||
		!Q_stricmp( resolvedName, "textures/borg/forceborder2" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/forceborder3" ) ||
		!Q_stricmp( resolvedName, "textures/borg/forceborder3" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/energy1" ) ||
		!Q_stricmp( resolvedName, "textures/borg/energy1" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/energy1_solid" ) ||
		!Q_stricmp( resolvedName, "textures/borg/energy1_solid" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/energy1_green" ) ||
		!Q_stricmp( resolvedName, "textures/borg/energy1_green" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/bigborg" ) ||
		!Q_stricmp( resolvedName, "textures/borg/bigborg" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/oddlight1" ) ||
		!Q_stricmp( resolvedName, "textures/borg/oddlight1" ) ) ? qtrue : qfalse;
}

static qboolean R_XboxTraceBorgBridgeWindowSurface( const msurface_t *surf )
{
	const char *mapShaderName;
	const char *resolvedName;

	if ( !surf )
	{
		return qfalse;
	}

	mapShaderName = R_XboxMapShaderNameForSurface( surf );
	resolvedName = (surf->shader && surf->shader->name) ? surf->shader->name : "";
	return ( !Q_stricmp( mapShaderName, "textures/borg/static" ) ||
		!Q_stricmp( resolvedName, "textures/borg/static" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/static2" ) ||
		!Q_stricmp( resolvedName, "textures/borg/static2" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/forceborder" ) ||
		!Q_stricmp( resolvedName, "textures/borg/forceborder" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/forceborder2" ) ||
		!Q_stricmp( resolvedName, "textures/borg/forceborder2" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/forceborder3" ) ||
		!Q_stricmp( resolvedName, "textures/borg/forceborder3" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/borgfield" ) ||
		!Q_stricmp( resolvedName, "textures/borg/borgfield" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/borgfield_nonsolid" ) ||
		!Q_stricmp( resolvedName, "textures/borg/borgfield_nonsolid" ) ||
		!Q_stricmp( mapShaderName, "textures/borg/borgfield_opaque" ) ||
		!Q_stricmp( resolvedName, "textures/borg/borgfield_opaque" ) ) ? qtrue : qfalse;
}

static qboolean R_XboxWorldIsBorg6( void )
{
	return ( tr.world && tr.world->name && !Q_stricmp( tr.world->name, "maps/borg6.bsp" ) ) ? qtrue : qfalse;
}

static qboolean R_XboxSurfaceIsCommonBlack( const msurface_t *surf )
{
	const char *mapShaderName;
	const char *resolvedName;

	if ( !surf )
	{
		return qfalse;
	}

	mapShaderName = R_XboxMapShaderNameForSurface( surf );
	resolvedName = (surf->shader && surf->shader->name) ? surf->shader->name : "";
	return ( !Q_stricmp( mapShaderName, "textures/common/black" ) ||
		!Q_stricmp( resolvedName, "textures/common/black" ) ) ? qtrue : qfalse;
}

static qboolean R_XboxShouldSkipBorgBridgeSurface( const msurface_t *surf )
{
	if ( !surf || !surf->data )
	{
		return qtrue;
	}

	if ( *surf->data == SF_SKIP )
	{
		return qtrue;
	}

	if ( R_XboxSurfaceIsCommonBlack( surf ) )
	{
		return qtrue;
	}

	return qfalse;
}

static const byte *R_ClusterPVS (int cluster);

static qboolean R_XboxLeafHasJunkSkySurface( const mleaf_s *leaf, int *firstCode, int *firstShaderNum )
{
	int i;
	msurface_t **mark;

	if ( firstCode )
	{
		*firstCode = -1;
	}
	if ( firstShaderNum )
	{
		*firstShaderNum = -1;
	}
	if ( !leaf || !tr.world || !tr.world->marksurfaces )
	{
		return qfalse;
	}

	if ( leaf->firstMarkSurfNum < 0 ||
		leaf->firstMarkSurfNum + leaf->nummarksurfaces > tr.world->nummarksurfaces )
	{
		return qfalse;
	}

	mark = tr.world->marksurfaces + leaf->firstMarkSurfNum;
	for ( i = 0; i < leaf->nummarksurfaces; ++i )
	{
		msurface_t *surf = mark[i];
		if ( R_XboxTraceJunkSkySurface( surf ) )
		{
			if ( firstCode )
			{
				*firstCode = surf->xboxDebugCode;
			}
			if ( firstShaderNum )
			{
				*firstShaderNum = surf->xboxDebugShaderNum;
			}
			return qtrue;
		}
	}

	return qfalse;
}

static qboolean R_XboxLeafHasBorgBlackCandidateSurface( const mleaf_s *leaf, int *firstCode, int *firstShaderNum )
{
	int i;
	msurface_t **mark;

	if ( firstCode )
	{
		*firstCode = -1;
	}
	if ( firstShaderNum )
	{
		*firstShaderNum = -1;
	}
	if ( !leaf || !tr.world || !tr.world->marksurfaces )
	{
		return qfalse;
	}

	if ( leaf->firstMarkSurfNum < 0 ||
		leaf->firstMarkSurfNum + leaf->nummarksurfaces > tr.world->nummarksurfaces )
	{
		return qfalse;
	}

	mark = tr.world->marksurfaces + leaf->firstMarkSurfNum;
	for ( i = 0; i < leaf->nummarksurfaces; ++i )
	{
		msurface_t *surf = mark[i];
		if ( R_XboxTraceBorgBlackCandidateSurface( surf ) )
		{
			if ( firstCode )
			{
				*firstCode = surf->xboxDebugCode;
			}
			if ( firstShaderNum )
			{
				*firstShaderNum = surf->xboxDebugShaderNum;
			}
			return qtrue;
		}
	}

	return qfalse;
}

static qboolean R_XboxLeafHasBorgVisibleFieldSurface( const mleaf_s *leaf, int *firstCode, int *firstShaderNum )
{
	int i;
	msurface_t **mark;

	if ( firstCode )
	{
		*firstCode = -1;
	}
	if ( firstShaderNum )
	{
		*firstShaderNum = -1;
	}
	if ( !leaf || !tr.world || !tr.world->marksurfaces )
	{
		return qfalse;
	}

	if ( leaf->firstMarkSurfNum < 0 ||
		leaf->firstMarkSurfNum + leaf->nummarksurfaces > tr.world->nummarksurfaces )
	{
		return qfalse;
	}

	mark = tr.world->marksurfaces + leaf->firstMarkSurfNum;
	for ( i = 0; i < leaf->nummarksurfaces; ++i )
	{
		msurface_t *surf = mark[i];
		if ( R_XboxTraceBorgBridgeWindowSurface( surf ) )
		{
			if ( firstCode )
			{
				*firstCode = surf ? surf->xboxDebugCode : -1;
			}
			if ( firstShaderNum )
			{
				*firstShaderNum = surf ? surf->xboxDebugShaderNum : -1;
			}
			return qtrue;
		}
	}

	return qfalse;
}

static void R_XboxLogJunkSkyLeafVisibility( const char *phase, const byte *vis )
{
	static int s_junkLeafSummaryBudget = 24;
	static int s_junkLeafDetailBudget = 96;
	int i;
	int junkLeafs = 0;
	int visibleJunkLeafs = 0;
	int pvsRejected = 0;
	int areaRejected = 0;
	int clusterRejected = 0;

	if ( !tr.world || !tr.world->leafs || !tr.world->marksurfaces )
	{
		return;
	}

	for ( i = 0; i < tr.world->numleafs; ++i )
	{
		mleaf_s *leaf = &tr.world->leafs[i];
		int firstCode = -1;
		int firstShaderNum = -1;
		int pvsVisible = 1;
		int areaMasked = 0;
		qboolean hasJunk = R_XboxLeafHasJunkSkySurface( leaf, &firstCode, &firstShaderNum );

		if ( !hasJunk )
		{
			continue;
		}

		++junkLeafs;

		if ( leaf->cluster < 0 || leaf->cluster >= tr.world->numClusters )
		{
			pvsVisible = 0;
			++clusterRejected;
		}
		else if ( vis )
		{
			pvsVisible = (vis[leaf->cluster >> 3] & (1 << (leaf->cluster & 7))) ? 1 : 0;
			if ( !pvsVisible )
			{
				++pvsRejected;
			}
		}

		if ( leaf->area >= 0 &&
			(tr.refdef.areamask[leaf->area >> 3] & (1 << (leaf->area & 7))) )
		{
			areaMasked = 1;
			++areaRejected;
		}

		if ( leaf->visframe == tr.visCount )
		{
			++visibleJunkLeafs;
		}

		if ( s_junkLeafDetailBudget > 0 )
		{
			XBLF("STEFX_JUNK_LEAF phase='%s' leaf=%d cluster=%d area=%d marks=%d firstMark=%d visframe=%d visCount=%d pvs=%d areaMasked=%d firstCode=%d shaderNum=%d viewOrg=%g,%g,%g",
				phase ? phase : "<null>",
				i,
				leaf->cluster,
				leaf->area,
				leaf->nummarksurfaces,
				leaf->firstMarkSurfNum,
				leaf->visframe,
				tr.visCount,
				pvsVisible,
				areaMasked,
				firstCode,
				firstShaderNum,
				tr.refdef.vieworg[0],
				tr.refdef.vieworg[1],
				tr.refdef.vieworg[2]);
			--s_junkLeafDetailBudget;
		}
	}

	if ( s_junkLeafSummaryBudget > 0 && junkLeafs > 0 )
	{
		XBLF("STEFX_JUNK_LEAF_SUMMARY phase='%s' map='%s' cluster=%d visCount=%d junkLeafs=%d visible=%d pvsRejected=%d areaRejected=%d clusterRejected=%d rootVis=%d viewOrg=%g,%g,%g",
			phase ? phase : "<null>",
			tr.world->name,
			tr.viewCluster,
			tr.visCount,
			junkLeafs,
			visibleJunkLeafs,
			pvsRejected,
			areaRejected,
			clusterRejected,
			(tr.world->nodes) ? tr.world->nodes[0].visframe : -1,
			tr.refdef.vieworg[0],
			tr.refdef.vieworg[1],
			tr.refdef.vieworg[2]);
		--s_junkLeafSummaryBudget;
	}
}

static void R_XboxLogBorgBlackLeafVisibility( const char *phase, const byte *vis )
{
	static int s_borgLeafSummaryBudget = 32;
	static int s_borgLeafDetailBudget = 160;
	int i;
	int borgLeafs = 0;
	int visibleBorgLeafs = 0;
	int pvsRejected = 0;
	int areaRejected = 0;
	int clusterRejected = 0;

	if ( !tr.world || !tr.world->leafs || !tr.world->marksurfaces )
	{
		return;
	}

	for ( i = 0; i < tr.world->numleafs; ++i )
	{
		mleaf_s *leaf = &tr.world->leafs[i];
		int firstCode = -1;
		int firstShaderNum = -1;
		int pvsVisible = 1;
		int areaMasked = 0;
		qboolean hasBorg = R_XboxLeafHasBorgBlackCandidateSurface( leaf, &firstCode, &firstShaderNum );

		if ( !hasBorg )
		{
			continue;
		}

		++borgLeafs;

		if ( leaf->cluster < 0 || leaf->cluster >= tr.world->numClusters )
		{
			pvsVisible = 0;
			++clusterRejected;
		}
		else if ( vis )
		{
			pvsVisible = (vis[leaf->cluster >> 3] & (1 << (leaf->cluster & 7))) ? 1 : 0;
			if ( !pvsVisible )
			{
				++pvsRejected;
			}
		}

		if ( leaf->area >= 0 &&
			(tr.refdef.areamask[leaf->area >> 3] & (1 << (leaf->area & 7))) )
		{
			areaMasked = 1;
			++areaRejected;
		}

		if ( leaf->visframe == tr.visCount )
		{
			++visibleBorgLeafs;
		}

		if ( s_borgLeafDetailBudget > 0 )
		{
			XBLF("STEFX_BORG_LEAF phase='%s' map='%s' frame=%d view=%d slot=%u leaf=%d cluster=%d area=%d marks=%d firstMark=%d visframe=%d visCount=%d pvs=%d areaMasked=%d firstCode=%d shaderNum=%d viewOrg=%g,%g,%g",
				phase ? phase : "<null>",
				tr.world->name,
				tr.frameCount,
				tr.viewCount,
				g_SPXBSplitSlotActive,
				i,
				leaf->cluster,
				leaf->area,
				leaf->nummarksurfaces,
				leaf->firstMarkSurfNum,
				leaf->visframe,
				tr.visCount,
				pvsVisible,
				areaMasked,
				firstCode,
				firstShaderNum,
				tr.refdef.vieworg[0],
				tr.refdef.vieworg[1],
				tr.refdef.vieworg[2]);
			--s_borgLeafDetailBudget;
		}
	}

	if ( s_borgLeafSummaryBudget > 0 && borgLeafs > 0 )
	{
		XBLF("STEFX_BORG_LEAF_SUMMARY phase='%s' map='%s' frame=%d view=%d slot=%u cluster=%d visCount=%d borgLeafs=%d visible=%d pvsRejected=%d areaRejected=%d clusterRejected=%d rootVis=%d viewOrg=%g,%g,%g",
			phase ? phase : "<null>",
			tr.world->name,
			tr.frameCount,
			tr.viewCount,
			g_SPXBSplitSlotActive,
			tr.viewCluster,
			tr.visCount,
			borgLeafs,
			visibleBorgLeafs,
			pvsRejected,
			areaRejected,
			clusterRejected,
			(tr.world->nodes) ? tr.world->nodes[0].visframe : -1,
			tr.refdef.vieworg[0],
			tr.refdef.vieworg[1],
			tr.refdef.vieworg[2]);
		--s_borgLeafSummaryBudget;
	}
}

static int R_XboxAddVisibleBorgFieldSurfaces( int dlightBits )
{
	static int s_borgDirectLogBudget = 96;
	int i;
	int leavesChecked = 0;
	int visibleCandidateLeaves = 0;
	int attempts = 0;
	int added = 0;

	if ( !tr.world || !tr.world->name || !tr.world->leafs || !tr.world->marksurfaces )
	{
		return 0;
	}

	if ( Q_stricmpn( tr.world->name, "maps/borg", 9 ) )
	{
		return 0;
	}

	for ( i = 0; i < tr.world->numleafs; ++i )
	{
		int j;
		msurface_t **mark;
		mleaf_s *leaf = &tr.world->leafs[i];
		qboolean leafHadCandidate = qfalse;

		if ( leaf->visframe != tr.visCount )
		{
			continue;
		}
		if ( leaf->firstMarkSurfNum < 0 ||
			leaf->firstMarkSurfNum + leaf->nummarksurfaces > tr.world->nummarksurfaces )
		{
			continue;
		}

		++leavesChecked;
		mark = tr.world->marksurfaces + leaf->firstMarkSurfNum;
		for ( j = 0; j < leaf->nummarksurfaces; ++j )
		{
			msurface_t *surf = mark[j];
			int drawBefore;

			if ( !R_XboxTraceBorgVisibleFieldSurface( surf ) )
			{
				continue;
			}

			if ( !leafHadCandidate )
			{
				++visibleCandidateLeaves;
				leafHadCandidate = qtrue;
			}
			drawBefore = tr.refdef.numDrawSurfs;
			++attempts;
			if ( s_borgDirectLogBudget > 0 )
			{
				XBLF("STEFX_BORG_DIRECT action='submit' map='%s' frame=%d view=%d slot=%u leaf=%d cluster=%d code=%d shaderNum=%d mapName='%s' resolved='%s' drawBefore=%d surfView=%d trView=%d dlight=0x%x",
					tr.world->name,
					tr.frameCount,
					tr.viewCount,
					g_SPXBSplitSlotActive,
					i,
					leaf->cluster,
					surf ? surf->xboxDebugCode : -1,
					surf ? surf->xboxDebugShaderNum : -1,
					R_XboxMapShaderNameForSurface( surf ),
					(surf && surf->shader) ? surf->shader->name : "<null>",
					drawBefore,
					surf ? surf->viewCount : -1,
					tr.viewCount,
					dlightBits);
				--s_borgDirectLogBudget;
			}
			R_AddWorldSurface( surf, dlightBits );
			if ( tr.refdef.numDrawSurfs > drawBefore )
			{
				++added;
			}
		}
	}

	if ( s_borgDirectLogBudget > 0 && attempts > 0 )
	{
		XBLF("STEFX_BORG_DIRECT_SUMMARY map='%s' frame=%d view=%d slot=%u visCount=%d leavesChecked=%d candidateLeaves=%d attempts=%d added=%d drawTotal=%d",
			tr.world->name,
			tr.frameCount,
			tr.viewCount,
			g_SPXBSplitSlotActive,
			tr.visCount,
			leavesChecked,
			visibleCandidateLeaves,
			attempts,
			added,
			tr.refdef.numDrawSurfs);
		--s_borgDirectLogBudget;
	}

	return added;
}

static int R_XboxAddBorg6BridgeLeafSurfaces( int dlightBits )
{
	const int maxBridgeClusters = 96;
	int bridgeClusters[96];
	static int s_borgBridgeLogBudget = 48;
	static int s_borgBridgeEnterBudget = 16;
	int i;
	int k;
	int fieldSourceLeaves = 0;
	int fieldClustersStored = 0;
	int duplicateFieldClusters = 0;
	int overflowFieldClusters = 0;
	int pvsSourcesUsed = 0;
	int candidateLeaves = 0;
	int visibleLeaves = 0;
	int areaMaskedLeaves = 0;
	int bridgeLeaves = 0;
	int attempts = 0;
	int added = 0;
	int skippedBlack = 0;
	int skippedSkip = 0;

	if ( s_borgBridgeEnterBudget > 0 )
	{
		XBLF("STEFX_BORG_BRIDGE_ENTER map='%s' frame=%d view=%d slot=%u world=%p leafs=%p marks=%p numleafs=%d nummarks=%d",
			(tr.world && tr.world->name) ? tr.world->name : "<noworld>",
			tr.frameCount,
			tr.viewCount,
			g_SPXBSplitSlotActive,
			tr.world,
			tr.world ? tr.world->leafs : NULL,
			tr.world ? tr.world->marksurfaces : NULL,
			tr.world ? tr.world->numleafs : -1,
			tr.world ? tr.world->nummarksurfaces : -1);
		--s_borgBridgeEnterBudget;
	}

	if ( !R_XboxWorldIsBorg6() || !tr.world->leafs || !tr.world->marksurfaces )
	{
		return 0;
	}

	for ( i = 0; i < tr.world->numleafs; ++i )
	{
		int k;
		int firstCode = -1;
		int firstShaderNum = -1;
		mleaf_s *leaf = &tr.world->leafs[i];

		if ( leaf->visframe != tr.visCount )
		{
			continue;
		}

		if ( leaf->cluster < 0 || leaf->cluster >= tr.world->numClusters )
		{
			continue;
		}

		if ( !R_XboxLeafHasBorgVisibleFieldSurface( leaf, &firstCode, &firstShaderNum ) )
		{
			continue;
		}

		++fieldSourceLeaves;

		for ( k = 0; k < fieldClustersStored; ++k )
		{
			if ( bridgeClusters[k] == leaf->cluster )
			{
				break;
			}
		}

		if ( k < fieldClustersStored )
		{
			++duplicateFieldClusters;
			continue;
		}

		if ( fieldClustersStored >= maxBridgeClusters )
		{
			++overflowFieldClusters;
			continue;
		}

		bridgeClusters[fieldClustersStored++] = leaf->cluster;
	}

	if ( fieldClustersStored <= 0 )
	{
		return 0;
	}

	for ( k = 0; k < fieldClustersStored; ++k )
	{
		const byte *vis = R_ClusterPVS( bridgeClusters[k] );

		if ( !vis )
		{
			continue;
		}
		++pvsSourcesUsed;

		for ( i = 0; i < tr.world->numleafs; ++i )
		{
			int j;
			msurface_t **mark;
			mleaf_s *leaf = &tr.world->leafs[i];

			if ( leaf->visframe == tr.visCount )
			{
				if ( k == 0 )
				{
					++visibleLeaves;
				}
				continue;
			}

			if ( leaf->cluster < 0 || leaf->cluster >= tr.world->numClusters )
			{
				continue;
			}

			if ( !( vis[leaf->cluster >> 3] & ( 1 << ( leaf->cluster & 7 ) ) ) )
			{
				continue;
			}

			++candidateLeaves;

			if ( leaf->area >= 0 &&
				(tr.refdef.areamask[leaf->area >> 3] & (1 << (leaf->area & 7))) )
			{
				++areaMaskedLeaves;
			}

			if ( leaf->firstMarkSurfNum < 0 ||
				leaf->firstMarkSurfNum + leaf->nummarksurfaces > tr.world->nummarksurfaces )
			{
				continue;
			}

			++bridgeLeaves;
			mark = tr.world->marksurfaces + leaf->firstMarkSurfNum;
			for ( j = 0; j < leaf->nummarksurfaces; ++j )
			{
				msurface_t *surf = mark[j];
				int drawBefore;

				if ( R_XboxSurfaceIsCommonBlack( surf ) )
				{
					++skippedBlack;
					continue;
				}

				if ( R_XboxShouldSkipBorgBridgeSurface( surf ) )
				{
					++skippedSkip;
					continue;
				}

				drawBefore = tr.refdef.numDrawSurfs;
				++attempts;
				R_AddWorldSurface( surf, dlightBits );
				if ( tr.refdef.numDrawSurfs > drawBefore )
				{
					++added;
				}
			}
		}
	}

	if ( s_borgBridgeLogBudget > 0 && candidateLeaves > 0 )
	{
		XBLF("STEFX_BORG_BRIDGE map='%s' frame=%d view=%d slot=%u visCount=%d mode='field-pvs' fieldSourceLeaves=%d fieldClusters=%d duplicateClusters=%d overflowClusters=%d pvsSources=%d candidateLeaves=%d visibleLeaves=%d areaMaskedLeaves=%d bridgeLeaves=%d attempts=%d added=%d skippedBlack=%d skippedSkip=%d drawTotal=%d",
			tr.world->name,
			tr.frameCount,
			tr.viewCount,
			g_SPXBSplitSlotActive,
			tr.visCount,
			fieldSourceLeaves,
			fieldClustersStored,
			duplicateFieldClusters,
			overflowFieldClusters,
			pvsSourcesUsed,
			candidateLeaves,
			visibleLeaves,
			areaMaskedLeaves,
			bridgeLeaves,
			attempts,
			added,
			skippedBlack,
			skippedSkip,
			tr.refdef.numDrawSurfs);
		--s_borgBridgeLogBudget;
	}

	return added;
}
#endif

/*
=================
R_CullTriSurf

Returns true if the grid is completely culled away.
Also sets the clipped hint bit in tess
=================
*/
static qboolean	R_CullTriSurf( srfTriangles_t *cv ) {
	int 	boxCull;

	boxCull = R_CullLocalBox( cv->bounds );

	if ( boxCull == CULL_OUT ) {
		return qtrue;
	}
	return qfalse;
}

/*
=================
R_CullGrid

Returns true if the grid is completely culled away.
Also sets the clipped hint bit in tess
=================
*/
static qboolean	R_CullGrid( srfGridMesh_t *cv ) {
	int 	boxCull;
	int 	sphereCull;

	if ( r_nocurves->integer ) {
		return qtrue;
	}

	if ( tr.currentEntityNum != TR_WORLDENT ) {
		sphereCull = R_CullLocalPointAndRadius( cv->localOrigin, cv->meshRadius );
	} else {
		sphereCull = R_CullPointAndRadius( cv->localOrigin, cv->meshRadius );
	}
	boxCull = CULL_OUT;
	
	// check for trivial reject
	if ( sphereCull == CULL_OUT )
	{
		tr.pc.c_sphere_cull_patch_out++;
		return qtrue;
	}
	// check bounding box if necessary
	else if ( sphereCull == CULL_CLIP )
	{
		tr.pc.c_sphere_cull_patch_clip++;

		boxCull = R_CullLocalBox( cv->meshBounds );

		if ( boxCull == CULL_OUT ) 
		{
			tr.pc.c_box_cull_patch_out++;
			return qtrue;
		}
		else if ( boxCull == CULL_IN )
		{
			tr.pc.c_box_cull_patch_in++;
		}
		else
		{
			tr.pc.c_box_cull_patch_clip++;
		}
	}
	else
	{
		tr.pc.c_sphere_cull_patch_in++;
	}

	return qfalse;
}


/*
================
R_CullSurface

Tries to back face cull surfaces before they are lighted or
added to the sorting list.

This will also allow mirrors on both sides of a model without recursion.
================
*/
static qboolean	R_CullSurface( surfaceType_t *surface, shader_t *shader ) {
	srfSurfaceFace_t *sface;
	float			d;

	if ( r_nocull->integer==1 ) {
		return qfalse;
	}

	if ( *surface == SF_GRID ) {
		return R_CullGrid( (srfGridMesh_t *)surface );
	}

	if ( *surface == SF_TRIANGLES ) {
		return R_CullTriSurf( (srfTriangles_t *)surface );
	}

	if ( *surface != SF_FACE ) {
		return qfalse;
	}

	if ( shader->cullType == CT_TWO_SIDED ) {
		return qfalse;
	}

#ifdef _XBOX
	/*
	 * The packed Xbox BSP path stores face data differently from the PC
	 * renderer.  This CPU-side face-plane cull is only an optimization, but
	 * bad packed plane data can drop otherwise visible world polygons while
	 * moving, showing up as HOM/grey gaps.  Let the BSP/PVS traversal and D3D
	 * cull state decide visibility for faces instead.
	 */
	return qfalse;
#endif

	// face culling
	if ( !r_facePlaneCull->integer ) {
		return qfalse;
	}

	sface = ( srfSurfaceFace_t * ) surface;
	d = DotProduct (tr.or.viewOrigin, sface->plane.normal);

	// don't cull exactly on the plane, because there are levels of rounding
	// through the BSP, ICD, and hardware that may cause pixel gaps if an
	// epsilon isn't allowed here 
	if ( shader->cullType == CT_FRONT_SIDED ) {
		if ( d < sface->plane.dist - 8 ) {
			return qtrue;
		}
	} else {
		if ( d > sface->plane.dist + 8 ) {
			return qtrue;
		}
	}

	return qfalse;
}


#ifndef VV_LIGHTING
static int R_DlightFace( srfSurfaceFace_t *face, int dlightBits ) {
	float		d;
	int			i;
	dlight_t	*dl;

	for ( i = 0 ; i < tr.refdef.num_dlights ; i++ ) {
		if ( ! ( dlightBits & ( 1 << i ) ) ) {
			continue;
		}
		dl = &tr.refdef.dlights[i];
		d = DotProduct( dl->origin, face->plane.normal ) - face->plane.dist;
		if ( !VectorCompare(face->plane.normal, vec3_origin) && (d < -dl->radius || d > dl->radius) ) {
			// dlight doesn't reach the plane
			dlightBits &= ~( 1 << i );
		}
	}

	if ( !dlightBits ) {
		tr.pc.c_dlightSurfacesCulled++;
	}

	face->dlightBits = dlightBits;
	return dlightBits;
}
#endif // VV_LIGHTING

#ifndef VV_LIGHTING
static int R_DlightGrid( srfGridMesh_t *grid, int dlightBits ) {
	int			i;
	dlight_t	*dl;

	for ( i = 0 ; i < tr.refdef.num_dlights ; i++ ) {
		if ( ! ( dlightBits & ( 1 << i ) ) ) {
			continue;
		}
		dl = &tr.refdef.dlights[i];
		if ( dl->origin[0] - dl->radius > grid->meshBounds[1][0]
			|| dl->origin[0] + dl->radius < grid->meshBounds[0][0]
			|| dl->origin[1] - dl->radius > grid->meshBounds[1][1]
			|| dl->origin[1] + dl->radius < grid->meshBounds[0][1]
			|| dl->origin[2] - dl->radius > grid->meshBounds[1][2]
			|| dl->origin[2] + dl->radius < grid->meshBounds[0][2] ) {
			// dlight doesn't reach the bounds
			dlightBits &= ~( 1 << i );
		}
	}

	if ( !dlightBits ) {
		tr.pc.c_dlightSurfacesCulled++;
	}

	grid->dlightBits = dlightBits;
	return dlightBits;
}
#endif // VV_LIGHTING


#ifndef VV_LIGHTING
static int R_DlightTrisurf( srfTriangles_t *surf, int dlightBits ) {
	// FIXME: more dlight culling to trisurfs...
	surf->dlightBits = dlightBits;
	return dlightBits;
#if 0
	int			i;
	dlight_t	*dl;

	for ( i = 0 ; i < tr.refdef.num_dlights ; i++ ) {
		if ( ! ( dlightBits & ( 1 << i ) ) ) {
			continue;
		}
		dl = &tr.refdef.dlights[i];
		if ( dl->origin[0] - dl->radius > grid->meshBounds[1][0]
			|| dl->origin[0] + dl->radius < grid->meshBounds[0][0]
			|| dl->origin[1] - dl->radius > grid->meshBounds[1][1]
			|| dl->origin[1] + dl->radius < grid->meshBounds[0][1]
			|| dl->origin[2] - dl->radius > grid->meshBounds[1][2]
			|| dl->origin[2] + dl->radius < grid->meshBounds[0][2] ) {
			// dlight doesn't reach the bounds
			dlightBits &= ~( 1 << i );
		}
	}

	if ( !dlightBits ) {
		tr.pc.c_dlightSurfacesCulled++;
	}

	grid->dlightBits = dlightBits;
	return dlightBits;
#endif
}
#endif // VV_LIGHTING

/*
====================
R_DlightSurface

The given surface is going to be drawn, and it touches a leaf
that is touched by one or more dlights, so try to throw out
more dlights if possible.
====================
*/
#ifndef VV_LIGHTING
static int R_DlightSurface( msurface_t *surf, int dlightBits ) {
	if ( *surf->data == SF_FACE ) {
		dlightBits = R_DlightFace( (srfSurfaceFace_t *)surf->data, dlightBits );
	} else if ( *surf->data == SF_GRID ) {
		dlightBits = R_DlightGrid( (srfGridMesh_t *)surf->data, dlightBits );
	} else if ( *surf->data == SF_TRIANGLES ) {
		dlightBits = R_DlightTrisurf( (srfTriangles_t *)surf->data, dlightBits );
	} else {
		dlightBits = 0;
	}

	if ( dlightBits ) {
		tr.pc.c_dlightSurfaces++;
	}

	return dlightBits;
}
#endif // VV_LIGHTING



/*
======================
R_AddWorldSurface
======================
*/
#ifdef VV_LIGHTING
void R_AddWorldSurface( msurface_t *surf, int dlightBits, qboolean noViewCount ) {
#else
static void R_AddWorldSurface( msurface_t *surf, int dlightBits, qboolean noViewCount = qfalse ) {
#endif
#ifdef _XBOX
	static int s_xboxWorldAddLogBudget = 16;
	static int s_xboxWorldCullLogBudget = 8;
	static int s_xboxWorldTraceAddBudget = 0;
	static int s_xboxWorldTraceCullBudget = 0;
	static int s_xboxJunkSkyWorldBudget = 32;
	static int s_xboxJunkSkyDrawBoundaryBudget = 32;
	static int s_xboxBorgSurfaceWorldBudget = 256;
	static int s_xboxBorgSurfaceDrawBoundaryBudget = 128;
	const qboolean traceJunkSky = R_XboxTraceJunkSkySurface( surf );
	const qboolean traceBorgBlackCandidate = R_XboxTraceBorgBlackCandidateSurface( surf );

	if ( traceJunkSky && s_xboxJunkSkyWorldBudget > 0 )
	{
		R_XboxLogWorldSurfaceSubmit( "enter", surf, dlightBits, noViewCount );
		--s_xboxJunkSkyWorldBudget;
	}
	if ( traceBorgBlackCandidate && s_xboxBorgSurfaceWorldBudget > 0 )
	{
		R_XboxLogWorldSurfaceSubmit( "borg-enter", surf, dlightBits, noViewCount );
		--s_xboxBorgSurfaceWorldBudget;
	}
	R_XboxLogWorldFallbackSurface( "enter", surf, dlightBits, noViewCount );
	if ( R_XboxWorldIsBorg6() && R_XboxSurfaceIsCommonBlack( surf ) )
	{
		static int s_xboxBorgCommonBlackSkipBudget = 64;
		if ( s_xboxBorgCommonBlackSkipBudget > 0 )
		{
			XBLF("STEFX_BORG_BLACK_SKIP map='%s' frame=%d view=%d slot=%u code=%d shaderNum=%d mapName='%s' shader='%s'",
				tr.world ? tr.world->name : "<noworld>",
				tr.frameCount,
				tr.viewCount,
				g_SPXBSplitSlotActive,
				surf ? surf->xboxDebugCode : -1,
				surf ? surf->xboxDebugShaderNum : -1,
				R_XboxMapShaderNameForSurface( surf ),
				(surf && surf->shader) ? surf->shader->name : "<null>");
			--s_xboxBorgCommonBlackSkipBudget;
		}
		return;
	}
#endif
	/*
	if ( surf->viewCount == tr.viewCount ) {
		return;		// already in this view
	}
	*/

	//rww - changed this to be like sof2mp's so RMG will look right.
	//Will this affect anything that is non-rmg?

	if (!noViewCount)
	{
#ifdef _XBOX
		if (g_SPXBSplitSlotActive == 1)
		{
			g_SPXBSplitSlot0WorldAttempts++;
		}
		else if (g_SPXBSplitSlotActive == 2)
		{
			g_SPXBSplitSlot1WorldAttempts++;
		}
#endif
		if ( surf->viewCount == tr.viewCount ) 
		{
#ifdef _XBOX
			if (g_SPXBSplitSlotActive == 1)
			{
				g_SPXBSplitSlot0WorldAlready++;
			}
			else if (g_SPXBSplitSlotActive == 2)
			{
				g_SPXBSplitSlot1WorldAlready++;
			}
#endif
#ifdef _XBOX
			if ( traceJunkSky && s_xboxJunkSkyWorldBudget > 0 )
			{
				R_XboxLogWorldSurfaceSubmit( "already", surf, dlightBits, noViewCount );
				--s_xboxJunkSkyWorldBudget;
			}
			if ( traceBorgBlackCandidate && s_xboxBorgSurfaceWorldBudget > 0 )
			{
				R_XboxLogWorldSurfaceSubmit( "borg-already", surf, dlightBits, noViewCount );
				--s_xboxBorgSurfaceWorldBudget;
			}
			R_XboxLogWorldFallbackSurface( "already", surf, dlightBits, noViewCount );
#endif
			// already in this view, but lets make sure all the dlight bits are set
			if ( *surf->data == SF_FACE ) 
			{
				((srfSurfaceFace_t *)surf->data)->dlightBits |= dlightBits;
			} 
			else if ( *surf->data == SF_GRID ) 
			{
				((srfGridMesh_t *)surf->data)->dlightBits |= dlightBits;
			} 
			else if ( *surf->data == SF_TRIANGLES ) 
			{
				((srfTriangles_t *)surf->data)->dlightBits |= dlightBits;
			}
			return;		
		}
		surf->viewCount = tr.viewCount;
		// FIXME: bmodel fog?
	}

//	surf->viewCount = tr.viewCount;
	// FIXME: bmodel fog?

	// try to cull before dlighting or adding
	if ( R_CullSurface( surf->data, surf->shader ) ) {
#ifdef _XBOX
		if (g_SPXBSplitSlotActive == 1)
		{
			g_SPXBSplitSlot0WorldCulled++;
		}
		else if (g_SPXBSplitSlotActive == 2)
		{
			g_SPXBSplitSlot1WorldCulled++;
		}
#endif
#ifdef _XBOX
		if ( traceJunkSky && s_xboxJunkSkyWorldBudget > 0 )
		{
			R_XboxLogWorldSurfaceSubmit( "culled-junk-sky", surf, dlightBits, noViewCount );
			--s_xboxJunkSkyWorldBudget;
		}
		if ( traceBorgBlackCandidate && s_xboxBorgSurfaceWorldBudget > 0 )
		{
			R_XboxLogWorldSurfaceSubmit( "borg-culled", surf, dlightBits, noViewCount );
			--s_xboxBorgSurfaceWorldBudget;
		}
		if (s_xboxWorldTraceCullBudget > 0)
		{
			R_XboxLogWorldSurfaceSubmit( "culled", surf, dlightBits, noViewCount );
			--s_xboxWorldTraceCullBudget;
		}
		if (s_xboxWorldCullLogBudget > 0)
		{
			XBLF("JA: R_AddWorldSurface culled shader='%s' sky=%d sort=%g type=%d fog=%d dlight=0x%x",
				surf->shader ? surf->shader->name : "<null>",
				(int)(surf->shader && surf->shader->sky != NULL),
				surf->shader ? (double)surf->shader->sort : -1.0,
				surf->data ? (int)*surf->data : -1,
				surf->fogIndex,
				dlightBits);
			--s_xboxWorldCullLogBudget;
		}
		R_XboxLogWorldFallbackSurface( "culled", surf, dlightBits, noViewCount );
#endif
		return;
	}

#ifdef _XBOX
	if ( traceJunkSky && s_xboxJunkSkyWorldBudget > 0 )
	{
		R_XboxLogWorldSurfaceSubmit( "add-junk-sky", surf, dlightBits, noViewCount );
		--s_xboxJunkSkyWorldBudget;
	}
	if ( traceBorgBlackCandidate && s_xboxBorgSurfaceWorldBudget > 0 )
	{
		R_XboxLogWorldSurfaceSubmit( "borg-add", surf, dlightBits, noViewCount );
		--s_xboxBorgSurfaceWorldBudget;
	}
	if (s_xboxWorldTraceAddBudget > 0)
	{
		R_XboxLogWorldSurfaceSubmit( "add", surf, dlightBits, noViewCount );
		--s_xboxWorldTraceAddBudget;
	}
	R_XboxLogWorldFallbackSurface( "add", surf, dlightBits, noViewCount );
	if (s_xboxWorldAddLogBudget > 0)
	{
		XBLF("JA: R_AddWorldSurface add shader='%s' sky=%d sort=%g type=%d fog=%d dlight=0x%x noView=%d viewCount=%d drawBefore=%d",
			surf->shader ? surf->shader->name : "<null>",
			(int)(surf->shader && surf->shader->sky != NULL),
			surf->shader ? (double)surf->shader->sort : -1.0,
			surf->data ? (int)*surf->data : -1,
			surf->fogIndex,
			dlightBits,
			(int)noViewCount,
			surf->viewCount,
			tr.refdef.numDrawSurfs);
		--s_xboxWorldAddLogBudget;
	}
#endif

#ifdef _XBOX
	if (g_SPXBSplitSlotActive == 1)
	{
		g_SPXBSplitSlot0WorldAdded++;
	}
	else if (g_SPXBSplitSlotActive == 2)
	{
		g_SPXBSplitSlot1WorldAdded++;
	}
#endif
	// check for dlighting
	if ( dlightBits ) {
#ifdef VV_LIGHTING
		dlightBits = VVLightMan.R_DlightSurface( surf, dlightBits );
#else
		dlightBits = R_DlightSurface( surf, dlightBits );
#endif
		dlightBits = ( dlightBits != 0 );
	}

#ifdef _XBOX
	if ( traceJunkSky && s_xboxJunkSkyDrawBoundaryBudget > 0 )
	{
		XBLF("STEFX_SURFACE_SUBMIT result='before-drawsurf' map='%s' code=%d shader='%s' drawBefore=%d fog=%d dlight=0x%x",
			tr.world ? tr.world->name : "<noworld>",
			surf->xboxDebugCode,
			surf->shader ? surf->shader->name : "<null>",
			tr.refdef.numDrawSurfs,
			surf->fogIndex,
			dlightBits);
		--s_xboxJunkSkyDrawBoundaryBudget;
	}
	if ( traceBorgBlackCandidate && s_xboxBorgSurfaceDrawBoundaryBudget > 0 )
	{
		XBLF("STEFX_SURFACE_SUBMIT result='borg-before-drawsurf' map='%s' frame=%d view=%d code=%d shaderNum=%d mapName='%s' shader='%s' drawBefore=%d fog=%d dlight=0x%x boundsMin=%g,%g,%g boundsMax=%g,%g,%g viewOrg=%g,%g,%g",
			tr.world ? tr.world->name : "<noworld>",
			tr.frameCount,
			tr.viewCount,
			surf->xboxDebugCode,
			surf->xboxDebugShaderNum,
			R_XboxMapShaderNameForSurface( surf ),
			surf->shader ? surf->shader->name : "<null>",
			tr.refdef.numDrawSurfs,
			surf->fogIndex,
			dlightBits,
			(double)surf->xboxDebugMins[0],
			(double)surf->xboxDebugMins[1],
			(double)surf->xboxDebugMins[2],
			(double)surf->xboxDebugMaxs[0],
			(double)surf->xboxDebugMaxs[1],
			(double)surf->xboxDebugMaxs[2],
			(double)tr.refdef.vieworg[0],
			(double)tr.refdef.vieworg[1],
			(double)tr.refdef.vieworg[2]);
		--s_xboxBorgSurfaceDrawBoundaryBudget;
	}
#endif
	R_AddDrawSurf( surf->data, surf->shader, surf->fogIndex, dlightBits );
#ifdef _XBOX
	if ( traceJunkSky && s_xboxJunkSkyDrawBoundaryBudget > 0 )
	{
		XBLF("STEFX_SURFACE_SUBMIT result='after-drawsurf' map='%s' code=%d shader='%s' drawAfter=%d",
			tr.world ? tr.world->name : "<noworld>",
			surf->xboxDebugCode,
			surf->shader ? surf->shader->name : "<null>",
			tr.refdef.numDrawSurfs);
		--s_xboxJunkSkyDrawBoundaryBudget;
	}
	if ( traceBorgBlackCandidate && s_xboxBorgSurfaceDrawBoundaryBudget > 0 )
	{
		XBLF("STEFX_SURFACE_SUBMIT result='borg-after-drawsurf' map='%s' frame=%d view=%d code=%d shaderNum=%d mapName='%s' shader='%s' drawAfter=%d",
			tr.world ? tr.world->name : "<noworld>",
			tr.frameCount,
			tr.viewCount,
			surf->xboxDebugCode,
			surf->xboxDebugShaderNum,
			R_XboxMapShaderNameForSurface( surf ),
			surf->shader ? surf->shader->name : "<null>",
			tr.refdef.numDrawSurfs);
		--s_xboxBorgSurfaceDrawBoundaryBudget;
	}
#endif
} 

/*
=============================================================

	BRUSH MODELS

=============================================================
*/

/*
=================
R_AddBrushModelSurfaces
=================
*/
void R_AddBrushModelSurfaces ( trRefEntity_t *ent ) {
	bmodel_t	*bmodel;
	int			clip;
	model_t		*pModel;
	int			i;
#ifdef _XBOX
	static int s_xboxBmodelLogBudget = 0;
	static int s_xboxBmodelFocusLogBudget = 96;
	qboolean xboxLogBmodel = (s_xboxBmodelLogBudget > 0 && cls.state == CA_ACTIVE);
	qboolean xboxFocusBmodel;
	int xboxBmodelDrawSurfsBefore;
#endif

	pModel = R_GetModelByHandle( ent->e.hModel );

	bmodel = pModel->bmodel;
#ifdef _XBOX
	xboxFocusBmodel = (s_xboxBmodelFocusLogBudget > 0 && cls.state == CA_ACTIVE &&
		(ent->e.hModel == 1 ||
		 (ent->e.hModel >= 2 && ent->e.hModel <= 5) ||
		 ent->e.hModel == 156 ||
		 (ent->e.hModel >= 129 && ent->e.hModel <= 156) ||
		 (ent->e.hModel >= 170 && ent->e.hModel <= 176) ||
		 ent->e.hModel == 175 ||
		 ent->e.hModel == 196 ||
		 ent->e.hModel == 200 ||
		 ent->e.hModel == 205 ||
		 ent->e.hModel == 206));
	xboxBmodelDrawSurfsBefore = tr.refdef.numDrawSurfs;
	if (xboxLogBmodel)
	{
		XBLF("JA: R_BMODEL ent=%d hModel=%d model='%s' bsp=%d surfaces=%d mins=%g,%g,%g maxs=%g,%g,%g origin=%g,%g,%g",
			ent->e.number,
			ent->e.hModel,
			pModel ? pModel->name : "<null>",
			pModel ? (int)pModel->bspInstance : -1,
			bmodel ? bmodel->numSurfaces : -1,
			bmodel ? bmodel->bounds[0][0] : 0.0f,
			bmodel ? bmodel->bounds[0][1] : 0.0f,
			bmodel ? bmodel->bounds[0][2] : 0.0f,
			bmodel ? bmodel->bounds[1][0] : 0.0f,
			bmodel ? bmodel->bounds[1][1] : 0.0f,
			bmodel ? bmodel->bounds[1][2] : 0.0f,
			ent->e.origin[0], ent->e.origin[1], ent->e.origin[2]);
		s_xboxBmodelLogBudget--;
	}
#endif

	clip = R_CullLocalBox( bmodel->bounds );
	if ( clip == CULL_OUT ) {
#ifdef _XBOX
		if ( ent->e.renderfx & RF_XBOX_NOCULL_BMODEL )
		{
			static int s_xboxBmodelNoCullLogBudget = 32;
			if ( s_xboxBmodelNoCullLogBudget > 0 )
			{
				XBLF("JA: R_BMODEL_FORCE_NOCULL ent=%d hModel=%d model='%s' renderfx=0x%x",
					ent->e.number,
					ent->e.hModel,
					pModel ? pModel->name : "<null>",
					ent->e.renderfx);
				--s_xboxBmodelNoCullLogBudget;
			}
		}
		else
		{
		if (xboxLogBmodel)
		{
			XBLF("JA: R_BMODEL_CULL_OUT ent=%d hModel=%d model='%s'",
				ent->e.number,
				ent->e.hModel,
				pModel ? pModel->name : "<null>");
		}
		if (xboxFocusBmodel)
		{
			XBLF("JA: R_BMODEL_FOCUS_CULL_OUT ent=%d hModel=%d model='%s'",
				ent->e.number,
				ent->e.hModel,
				pModel ? pModel->name : "<null>");
			s_xboxBmodelFocusLogBudget--;
		}
		return;
		}
#else
		return;
#endif
	}
	
#ifdef VV_LIGHTING
	VVLightMan.R_SetupEntityLighting(&tr.refdef, ent);
#else
	R_SetupEntityLighting(&tr.refdef, ent);
#endif

#ifdef VV_LIGHTING
	VVLightMan.R_DlightBmodel( bmodel, qfalse );
#else
	R_DlightBmodel( bmodel, qfalse );
#endif

	for ( i = 0 ; i < bmodel->numSurfaces ; i++ ) {
#ifdef _XBOX
		if (xboxFocusBmodel && i < 8)
		{
			msurface_t *surf = bmodel->firstSurface + i;
			shader_t *shader = surf ? surf->shader : NULL;
			XBLF("JA: R_BMODEL_FOCUS_SURF ent=%d hModel=%d model='%s' surf=%d shader='%s' lm0=%d passes=%d type=%d fog=%d",
				ent->e.number,
				ent->e.hModel,
				pModel ? pModel->name : "<null>",
				i,
				shader ? shader->name : "<null>",
				shader ? shader->lightmapIndex[0] : -999,
				shader ? shader->numUnfoggedPasses : -1,
				(surf && surf->data) ? (int)*surf->data : -1,
				surf ? surf->fogIndex : -1);
		}
#endif
		R_AddWorldSurface( bmodel->firstSurface + i, tr.currentEntity->dlightBits, qtrue );
	}
#ifdef _XBOX
	if (xboxFocusBmodel)
	{
		XBLF("JA: R_BMODEL_FOCUS_ADD ent=%d hModel=%d model='%s' surfaces=%d drawSurfsAdded=%d totalDrawSurfs=%d",
			ent->e.number,
			ent->e.hModel,
			pModel ? pModel->name : "<null>",
			bmodel ? bmodel->numSurfaces : -1,
			tr.refdef.numDrawSurfs - xboxBmodelDrawSurfsBefore,
			tr.refdef.numDrawSurfs);
		s_xboxBmodelFocusLogBudget--;
	}
#endif
}

float GetQuadArea( vec3_t v1, vec3_t v2, vec3_t v3, vec3_t v4 )
{
	vec3_t	vec1, vec2, dis1, dis2;

	// Get area of tri1
	VectorSubtract( v1, v2, vec1 );
	VectorSubtract( v1, v4, vec2 ); 
	CrossProduct( vec1, vec2, dis1 );
	VectorScale( dis1, 0.25f, dis1 );

	// Get area of tri2
	VectorSubtract( v3, v2, vec1 );
	VectorSubtract( v3, v4, vec2 );
	CrossProduct( vec1, vec2, dis2 );
	VectorScale( dis2, 0.25f, dis2 );

	// Return addition of disSqr of each tri area
	return ( dis1[0] * dis1[0] + dis1[1] * dis1[1] + dis1[2] * dis1[2] +
				dis2[0] * dis2[0] + dis2[1] * dis2[1] + dis2[2] * dis2[2] );
}

#ifdef _XBOX
float GetQuadArea( unsigned short v1[3], unsigned short v2[3], unsigned short v3[3], unsigned short v4[3])
{
	vec3_t fv1;
	vec3_t fv2;
	vec3_t fv3;
	vec3_t fv4;

	for(int i=0; i<3; i++) {
		Q_CastShort2Float(&fv1[i], (short*)&v1[i]);
		Q_CastShort2Float(&fv2[i], (short*)&v2[i]);
		Q_CastShort2Float(&fv3[i], (short*)&v3[i]);
		Q_CastShort2Float(&fv4[i], (short*)&v4[i]);
	}

	return GetQuadArea(fv1, fv2, fv3, fv4);
}
#endif

void RE_GetBModelVerts( int bmodelIndex, vec3_t *verts, vec3_t normal )
{
	msurface_t			*surfs;
	srfSurfaceFace_t	*face;
	bmodel_t			*bmodel;
	model_t				*pModel;
	int					i;
	//	Not sure if we really need to track the best two candidates
	int					maxDist[2]={0,0};
	int					maxIndx[2]={0,0};
	int					dist = 0;
	float				dot1, dot2;

	pModel = R_GetModelByHandle( bmodelIndex );
	bmodel = pModel->bmodel;

	// Loop through all surfaces on the brush and find the best two candidates
	for ( i = 0 ; i < bmodel->numSurfaces; i++ ) 
	{
		surfs = bmodel->firstSurface + i;
		face = ( srfSurfaceFace_t *)surfs->data;

		// It seems that the safest way to handle this is by finding the area of the faces
#ifdef _XBOX
		int nextSurfPoint = NEXT_SURFPOINT(face->flags);
		dist = GetQuadArea( face->srfPoints, face->srfPoints + nextSurfPoint, 
				face->srfPoints + nextSurfPoint * 2, face->srfPoints +
						 nextSurfPoint * 3 );
#else
		dist = GetQuadArea( face->points[0], face->points[1], face->points[2], face->points[3] );
#endif

		// Check against the highest max
		if ( dist > maxDist[0] )
		{
			// Shuffle our current maxes down
			maxDist[1] = maxDist[0];
			maxIndx[1] = maxIndx[0];

			maxDist[0] = dist;
			maxIndx[0] = i;
		}
		// Check against the second highest max
		else if ( dist >= maxDist[1] )
		{
			// just stomp the old
			maxDist[1] = dist;
			maxIndx[1] = i;
		}
	}

	// Hopefully we've found two best case candidates.  Now we should see which of these faces the viewer
	surfs = bmodel->firstSurface + maxIndx[0];
	face = ( srfSurfaceFace_t *)surfs->data;
	dot1 = DotProduct( face->plane.normal, tr.refdef.viewaxis[0] );
	
	surfs = bmodel->firstSurface + maxIndx[1];
	face = ( srfSurfaceFace_t *)surfs->data;
	dot2 = DotProduct( face->plane.normal, tr.refdef.viewaxis[0] );

	if ( dot2 < dot1 && dot2 < 0.0f )
	{
		i = maxIndx[1]; // use the second face
	}
	else if ( dot1 < dot2 && dot1 < 0.0f )
	{
		i = maxIndx[0]; // use the first face
	}
	else
	{ // Possibly only have one face, so may as well use the first face, which also should be the best one
		//i = rand() & 1; // ugh, we don't know which to use.  I'd hope this would never happen
		i = maxIndx[0]; // use the first face
	}

	surfs = bmodel->firstSurface + i;
	face = ( srfSurfaceFace_t *)surfs->data;

#ifdef _XBOX
	int nextSurfPoint = NEXT_SURFPOINT(face->flags);
	for ( int t = 0; t < 4; t++ )
	{
		Q_CastShort2Float(&verts[t][0], (short*)(face->srfPoints + nextSurfPoint * t + 0));
		Q_CastShort2Float(&verts[t][1], (short*)(face->srfPoints + nextSurfPoint * t + 1));
		Q_CastShort2Float(&verts[t][2], (short*)(face->srfPoints + nextSurfPoint * t + 2));
	}
#else
	for ( int t = 0; t < 4; t++ )
	{
		VectorCopy(	face->points[t], verts[t] );
	}
#endif
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/


/*
================
R_RecursiveWorldNode
================
*/
#ifndef VV_LIGHTING
static void R_RecursiveWorldNode( mnode_t *node, int planeBits, int dlightBits ) {

	do {
		int			newDlights[2];

		// if the node wasn't marked as potentially visible, exit
		if (node->visframe != tr.visCount) {
			return;
		}

		// if the bounding volume is outside the frustum, nothing
		// inside can be visible OPTIMIZE: don't do this all the way to leafs?

		if ( r_nocull->integer!=1 ) {
			int		r;

			if ( planeBits & 1 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[0]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~1;			// all descendants will also be in front
				}
			}

			if ( planeBits & 2 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[1]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~2;			// all descendants will also be in front
				}
			}

			if ( planeBits & 4 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[2]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~4;			// all descendants will also be in front
				}
			}

			if ( planeBits & 8 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[3]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~8;			// all descendants will also be in front
				}
			}

			if ( planeBits & 16 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[4]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~16;			// all descendants will also be in front
				}
			}

		}

		if ( node->contents != -1 ) {
			break;
		}

		// determine which dlights are needed
		if ( r_nocull->integer!=2 ) 
		{
			newDlights[0] = 0;
			newDlights[1] = 0;
			if ( dlightBits ) 
			{
				int	i;
				for ( i = 0 ; i < tr.refdef.num_dlights ; i++ )
				{
					dlight_t	*dl;
					float		dist;

					if ( dlightBits & ( 1 << i ) ) {
						dl = &tr.refdef.dlights[i];
						dist = DotProduct( dl->origin, node->plane->normal ) - node->plane->dist;
						
						if ( dist > -dl->radius ) {
							newDlights[0] |= ( 1 << i );
						}
						if ( dist < dl->radius ) {
							newDlights[1] |= ( 1 << i );
						}
					}
				}
			}
		}
		else
		{
			newDlights[0] = dlightBits;
			newDlights[1] = dlightBits;
		}
		// recurse down the children, front side first
		R_RecursiveWorldNode (node->children[0], planeBits, newDlights[0] );

		// tail recurse
		node = node->children[1];
		dlightBits = newDlights[1];
	} while ( 1 );

	{
		// leaf node, so add mark surfaces
		int			c;
		msurface_t	*surf, **mark;

		tr.pc.c_leafs++;

		// add to z buffer bounds
		if ( node->mins[0] < tr.viewParms.visBounds[0][0] ) {
			tr.viewParms.visBounds[0][0] = node->mins[0];
		}
		if ( node->mins[1] < tr.viewParms.visBounds[0][1] ) {
			tr.viewParms.visBounds[0][1] = node->mins[1];
		}
		if ( node->mins[2] < tr.viewParms.visBounds[0][2] ) {
			tr.viewParms.visBounds[0][2] = node->mins[2];
		}

		if ( node->maxs[0] > tr.viewParms.visBounds[1][0] ) {
			tr.viewParms.visBounds[1][0] = node->maxs[0];
		}
		if ( node->maxs[1] > tr.viewParms.visBounds[1][1] ) {
			tr.viewParms.visBounds[1][1] = node->maxs[1];
		}
		if ( node->maxs[2] > tr.viewParms.visBounds[1][2] ) {
			tr.viewParms.visBounds[1][2] = node->maxs[2];
		}

		// add the individual surfaces
		mark = node->firstmarksurface;
		c = node->nummarksurfaces;
		while (c--) {
			// the surface may have already been added if it
			// spans multiple leafs
			surf = *mark;
			R_AddWorldSurface( surf, dlightBits );
			mark++;
		}
	}

}
#endif // VV_LIGHTING


/*
===============
R_PointInLeaf
===============
*/
static mnode_t *R_PointInLeaf( vec3_t p ) {
	mnode_t		*node;
	float		d;
	cplane_t	*plane;
	
	if ( !tr.world ) {
		Com_Error (ERR_DROP, "R_PointInLeaf: bad model");
	}

	node = tr.world->nodes;
	while( 1 ) {
		if (node->contents != -1) {
			break;
		}
#ifdef _XBOX
		plane = tr.world->planes + node->planeNum;
#else
		plane = node->plane;
#endif
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0) {
			node = node->children[0];
		} else {
			node = node->children[1];
		}
	}
	
	return node;
}

/*
==============
R_ClusterPVS
==============
*/
static const byte *R_ClusterPVS (int cluster) {
	if (!tr.world || !tr.world->vis || cluster < 0 || cluster >= tr.world->numClusters ) {
		return tr.world->novis;
	}

#ifdef _XBOX
	return tr.world->vis->Decompress(cluster * tr.world->clusterBytes,
			tr.world->numClusters);
#else
	return tr.world->vis + cluster * tr.world->clusterBytes;
#endif
}

/*
=================
R_inPVS
=================
*/
#ifdef _XBOX
qboolean R_inPVS( vec3_t p1, vec3_t p2 ) {
	mleaf_s *leaf;
	byte	*vis;

	leaf = (mleaf_s*)R_PointInLeaf( p1 );
	vis = (byte*)CM_ClusterPVS( leaf->cluster );
	leaf = (mleaf_s*)R_PointInLeaf( p2 );

	if ( !vis || (!(vis[leaf->cluster>>3] & (1<<(leaf->cluster&7)))) ) {
		return qfalse;
	}
	return qtrue;
}
#else // _XBOX

qboolean R_inPVS( vec3_t p1, vec3_t p2 ) {
	mnode_t *leaf;
	byte	*vis;

	leaf = R_PointInLeaf( p1 );
	vis = CM_ClusterPVS( leaf->cluster );
	leaf = R_PointInLeaf( p2 );

	if ( !(vis[leaf->cluster>>3] & (1<<(leaf->cluster&7))) ) {
		return qfalse;
	}
	return qtrue;
}
#endif // _XBOX

/*
===============
R_MarkLeaves

Mark the leaves and nodes that are in the PVS for the current
cluster
===============
*/
#ifdef _XBOX
void R_MarkLeaves (mleaf_s *leafOverride) {
	const byte	*vis;
	mleaf_s	*leaf;
   	mnode_s	*parent;
	int		i;
	int		cluster;
#ifdef _XBOX
	int		pvsRejected = 0;
	int		areaRejected = 0;
	int		markedLeaves = 0;
	int		negativeCluster = 0;
	int		leafIndex = -1;
	const qboolean stefxForceAllVisible = ( stefx_xbox_world_novis && stefx_xbox_world_novis->integer ) ? qtrue : qfalse;
	static int s_xboxMarkLeavesLogBudget = 48;
	static int s_xboxMarkLeavesSameLogBudget = 16;
#endif

	// lockpvs lets designers walk around to determine the
	// extent of the current pvs
	if ( r_lockpvs->integer ) {
		return;
	}

	// current viewcluster
	if(!leafOverride) {
		leaf = (mleaf_s*)R_PointInLeaf( tr.viewParms.pvsOrigin );
	} else {
		leaf = leafOverride;
	}
	cluster = leaf->cluster;
#ifdef _XBOX
	if (tr.world && tr.world->leafs && leaf >= tr.world->leafs && leaf < tr.world->leafs + tr.world->numleafs) {
		leafIndex = (int)(leaf - tr.world->leafs);
	}
	if (s_xboxMarkLeavesLogBudget > 0) {
		XBLF("JA: R_MarkLeaves enter leaf=%d cluster=%d area=%d marks=%d firstMark=%d prevCluster=%d visCount=%d areaModified=%d pvsOrigin=(%g,%g,%g)",
			leafIndex,
			cluster,
			leaf ? leaf->area : -99,
			leaf ? leaf->nummarksurfaces : -1,
			leaf ? leaf->firstMarkSurfNum : -1,
			tr.viewCluster,
			tr.visCount,
			tr.refdef.areamaskModified,
			tr.viewParms.pvsOrigin[0],
			tr.viewParms.pvsOrigin[1],
			tr.viewParms.pvsOrigin[2]);
	}
#endif

	assert(leaf->contents != -1);

	// if the cluster is the same and the area visibility matrix
	// hasn't changed, we don't need to mark everything again

	if ( tr.viewCluster == cluster && !tr.refdef.areamaskModified && g_SPXBSplitSlotActive == 0 ) {
#ifdef _XBOX
		if (g_SPXBSplitSlotActive == 1)
		{
			g_SPXBSplitSlot0MarkedLeaves = 0xffffffffu;
			g_SPXBSplitSlot0PvsRejected = 0;
			g_SPXBSplitSlot0AreaRejected = 0;
			g_SPXBSplitSlot0RootVis = (unsigned int)((tr.world && tr.world->nodes) ? tr.world->nodes[0].visframe : -1);
		}
		else if (g_SPXBSplitSlotActive == 2)
		{
			g_SPXBSplitSlot1MarkedLeaves = 0xffffffffu;
			g_SPXBSplitSlot1PvsRejected = 0;
			g_SPXBSplitSlot1AreaRejected = 0;
			g_SPXBSplitSlot1RootVis = (unsigned int)((tr.world && tr.world->nodes) ? tr.world->nodes[0].visframe : -1);
		}
		if (s_xboxMarkLeavesSameLogBudget > 0)
		{
			XBLF("JA: R_MarkLeaves same cluster=%d visCount=%d areaModified=%d rootVis=%d leafVis=%d",
				cluster,
				tr.visCount,
				tr.refdef.areamaskModified,
				(tr.world && tr.world->nodes) ? tr.world->nodes[0].visframe : -1,
				leaf ? leaf->visframe : -1);
			--s_xboxMarkLeavesSameLogBudget;
		}
		R_XboxLogJunkSkyLeafVisibility( "same-cluster", NULL );
		R_XboxLogBorgBlackLeafVisibility( "same-cluster", NULL );
#endif
		return;
	}

	tr.visCount++;
	tr.viewCluster = cluster;

	if ( r_novis->integer || stefxForceAllVisible || tr.viewCluster == -1 ) {
		for (i=0 ; i<tr.world->numnodes ; i++) {
			if (tr.world->nodes[i].contents != CONTENTS_SOLID) {
				tr.world->nodes[i].visframe = tr.visCount;
			}
		}
#ifdef _XBOX
		for (i=0 ; i<tr.world->numleafs ; i++) {
			if (tr.world->leafs[i].contents != CONTENTS_SOLID) {
				tr.world->leafs[i].visframe = tr.visCount;
			}
		}
		if (g_SPXBSplitSlotActive == 1)
		{
			g_SPXBSplitSlot0MarkedLeaves = (unsigned int)tr.world->numleafs;
			g_SPXBSplitSlot0PvsRejected = 0;
			g_SPXBSplitSlot0AreaRejected = 0;
			g_SPXBSplitSlot0RootVis = (unsigned int)((tr.world && tr.world->nodes) ? tr.world->nodes[0].visframe : -1);
		}
		else if (g_SPXBSplitSlotActive == 2)
		{
			g_SPXBSplitSlot1MarkedLeaves = (unsigned int)tr.world->numleafs;
			g_SPXBSplitSlot1PvsRejected = 0;
			g_SPXBSplitSlot1AreaRejected = 0;
			g_SPXBSplitSlot1RootVis = (unsigned int)((tr.world && tr.world->nodes) ? tr.world->nodes[0].visframe : -1);
		}
		if (s_xboxMarkLeavesLogBudget > 0) {
			XBLF("JA: R_MarkLeaves all-visible cluster=%d visCount=%d nodes=%d leafs=%d rootVis=%d leafVis=%d r_novis=%d stefx_novis=%d",
				tr.viewCluster,
				tr.visCount,
				tr.world ? tr.world->numnodes : -1,
				tr.world ? tr.world->numleafs : -1,
				(tr.world && tr.world->nodes) ? tr.world->nodes[0].visframe : -1,
				leaf ? leaf->visframe : -1,
				r_novis ? r_novis->integer : -1,
				stefxForceAllVisible ? 1 : 0);
			--s_xboxMarkLeavesLogBudget;
		}
		R_XboxLogJunkSkyLeafVisibility( "all-visible", NULL );
		R_XboxLogBorgBlackLeafVisibility( "all-visible", NULL );
#endif
		return;
	}

	vis = R_ClusterPVS (tr.viewCluster);
	
	for (i=0,leaf=tr.world->leafs ; i<tr.world->numleafs ; i++, leaf++) {
		cluster = leaf->cluster;
		if ( cluster < 0 || cluster >= tr.world->numClusters ) {
#ifdef _XBOX
			negativeCluster++;
#endif
			continue;
		}

		// check general pvs
		if ( !(vis[cluster>>3] & (1<<(cluster&7))) ) {
#ifdef _XBOX
			pvsRejected++;
#endif
			continue;
		}

		// check for door connection
		if (!lookingForWorstLeaf &&
			   leaf->area >= 0 &&
			   (tr.refdef.areamask[leaf->area>>3] & (1<<(leaf->area&7)) ) ) {
#ifdef _XBOX
			areaRejected++;
#endif
			continue;		// not visible
		}

#ifdef _XBOX
		markedLeaves++;
#endif
		parent = (mnode_t*)leaf;
		assert(leaf->contents != -1);
		do {
			if (parent->visframe == tr.visCount)
				break;
			parent->visframe = tr.visCount;
			parent = parent->parent;
		} while (parent);
	}
#ifdef _XBOX
	{
		static int s_stefxPvsSummaryBudget = 16;
		if (s_stefxPvsSummaryBudget > 0)
		{
			XBLF("STEFX: PVS cluster=%d visCount=%d leaves=%d marked=%d pvsRejected=%d areaRejected=%d badCluster=%d areaModified=%d rootVis=%d pvsOrigin=(%g,%g,%g)",
				tr.viewCluster,
				tr.visCount,
				tr.world ? tr.world->numleafs : -1,
				markedLeaves,
				pvsRejected,
				areaRejected,
				negativeCluster,
				tr.refdef.areamaskModified,
				(tr.world && tr.world->nodes) ? tr.world->nodes[0].visframe : -1,
				tr.viewParms.pvsOrigin[0],
				tr.viewParms.pvsOrigin[1],
				tr.viewParms.pvsOrigin[2]);
			--s_stefxPvsSummaryBudget;
		}
		if (s_xboxMarkLeavesLogBudget > 0)
		{
			XBLF("JA: R_MarkLeaves cluster=%d visCount=%d leaves=%d marked=%d pvsRejected=%d areaRejected=%d badCluster=%d areaModified=%d rootVis=%d leafVis=%d",
				tr.viewCluster,
				tr.visCount,
				tr.world ? tr.world->numleafs : -1,
				markedLeaves,
				pvsRejected,
				areaRejected,
				negativeCluster,
				tr.refdef.areamaskModified,
				(tr.world && tr.world->nodes) ? tr.world->nodes[0].visframe : -1,
				leafOverride ? leafOverride->visframe : -1);
			--s_xboxMarkLeavesLogBudget;
		}
		if (g_SPXBSplitSlotActive == 1)
		{
			g_SPXBSplitSlot0MarkedLeaves = (unsigned int)markedLeaves;
			g_SPXBSplitSlot0PvsRejected = (unsigned int)pvsRejected;
			g_SPXBSplitSlot0AreaRejected = (unsigned int)areaRejected;
			g_SPXBSplitSlot0RootVis = (unsigned int)((tr.world && tr.world->nodes) ? tr.world->nodes[0].visframe : -1);
		}
		else if (g_SPXBSplitSlotActive == 2)
		{
			g_SPXBSplitSlot1MarkedLeaves = (unsigned int)markedLeaves;
			g_SPXBSplitSlot1PvsRejected = (unsigned int)pvsRejected;
			g_SPXBSplitSlot1AreaRejected = (unsigned int)areaRejected;
			g_SPXBSplitSlot1RootVis = (unsigned int)((tr.world && tr.world->nodes) ? tr.world->nodes[0].visframe : -1);
		}
		R_XboxLogJunkSkyLeafVisibility( "marked", vis );
		R_XboxLogBorgBlackLeafVisibility( "marked", vis );
	}
#endif
}
#else // _XBOX

static void R_MarkLeaves (void) {
	const byte	*vis;
	mnode_t	*leaf, *parent;
	int		i;
	int		cluster;

	// lockpvs lets designers walk around to determine the
	// extent of the current pvs
	if ( r_lockpvs->integer ) {
		return;
	}

	// current viewcluster
	leaf = R_PointInLeaf( tr.viewParms.pvsOrigin );
	cluster = leaf->cluster;

	// if the cluster is the same and the area visibility matrix
	// hasn't changed, we don't need to mark everything again

	// if r_showcluster was just turned on, remark everything 
	if ( tr.viewCluster == cluster && !tr.refdef.areamaskModified 
		&& !r_showcluster->modified ) {
		return;
	}

	if ( r_showcluster->modified || r_showcluster->integer ) {
		r_showcluster->modified = qfalse;
		if ( r_showcluster->integer ) {
			VID_Printf( PRINT_ALL, "cluster:%i  area:%i\n", cluster, leaf->area );
		}
	}

	tr.visCount++;
	tr.viewCluster = cluster;

	if ( r_novis->integer || tr.viewCluster == -1 ) {
		for (i=0 ; i<tr.world->numnodes ; i++) {
			if (tr.world->nodes[i].contents != CONTENTS_SOLID) {
				tr.world->nodes[i].visframe = tr.visCount;
			}
		}
		return;
	}

	vis = R_ClusterPVS (tr.viewCluster);
	
	for (i=0,leaf=tr.world->nodes ; i<tr.world->numnodes ; i++, leaf++) {
		cluster = leaf->cluster;
		if ( cluster < 0 || cluster >= tr.world->numClusters ) {
			continue;
		}

		// check general pvs
		if ( !(vis[cluster>>3] & (1<<(cluster&7))) ) {
			continue;
		}

		// check for door connection
		if ( (tr.refdef.areamask[leaf->area>>3] & (1<<(leaf->area&7)) ) ) {
			continue;		// not visible
		}

		parent = leaf;
		do {
			if (parent->visframe == tr.visCount)
				break;
			parent->visframe = tr.visCount;
			parent = parent->parent;
		} while (parent);
	}
}
#endif


/*
=============
R_AddWorldSurfaces
=============
*/
#ifdef _XBOX
void R_AddWorldSurfaces (void) {
#ifdef _XBOX
	static int s_xboxAddWorldLogBudget = 48;
#endif
	if ( !r_drawworld->integer ) {
#ifdef _XBOX
		if (s_xboxAddWorldLogBudget > 0) {
			XBLF("JA: R_AddWorldSurfaces skip r_drawworld=0 draw=%d", tr.refdef.numDrawSurfs);
			--s_xboxAddWorldLogBudget;
		}
#endif
		return;
	}

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
#ifdef _XBOX
		if (s_xboxAddWorldLogBudget > 0) {
			XBLF("JA: R_AddWorldSurfaces skip RDF_NOWORLDMODEL rd=0x%x draw=%d", tr.refdef.rdflags, tr.refdef.numDrawSurfs);
			--s_xboxAddWorldLogBudget;
		}
#endif
		return;
	}

	tr.currentEntityNum = TR_WORLDENT;//ENTITYNUM_WORLD;
	tr.shiftedEntityNum = tr.currentEntityNum << QSORT_ENTITYNUM_SHIFT;

	// clear out the visible min/max
	ClearBounds( tr.viewParms.visBounds[0], tr.viewParms.visBounds[1] );

	// perform frustum culling and add all the potentially visible surfaces
	if ( VVLightMan.num_dlights > MAX_DLIGHTS ) {
		VVLightMan.num_dlights = MAX_DLIGHTS ;
	}

	const int xboxLeafsBefore = tr.pc.c_leafs;
	const int xboxDrawBefore = tr.refdef.numDrawSurfs;
	int xboxWorldDelta;

#ifdef _XBOX
	if (s_xboxAddWorldLogBudget > 0)
	{
		XBLF("JA: R_AddWorldSurfaces before recursive draw=%d visCount=%d viewCluster=%d rootVis=%d dlights=%d rdflags=0x%x nodes=%d leafs=%d marks=%d",
			tr.refdef.numDrawSurfs,
			tr.visCount,
			tr.viewCluster,
			(tr.world && tr.world->nodes) ? tr.world->nodes[0].visframe : -1,
			VVLightMan.num_dlights,
			tr.refdef.rdflags,
			tr.world ? tr.world->numnodes : -1,
			tr.world ? tr.world->numleafs : -1,
			tr.world ? tr.world->nummarksurfaces : -1);
	}
#endif
	if (g_SPXBSplitSlotActive == 2)
	{
		g_SPXBSplitSlot1WorldRetryDelta = 0;
		g_SPXBSplitSlot1WorldFallback = 0;
		g_SPXBSplitSlot1WorldAttempts = 0;
		g_SPXBSplitSlot1WorldCulled = 0;
		g_SPXBSplitSlot1WorldAlready = 0;
		g_SPXBSplitSlot1WorldAdded = 0;
	}
	else if (g_SPXBSplitSlotActive == 1)
	{
		g_SPXBSplitSlot0WorldAttempts = 0;
		g_SPXBSplitSlot0WorldCulled = 0;
		g_SPXBSplitSlot0WorldAlready = 0;
		g_SPXBSplitSlot0WorldAdded = 0;
	}

	VVLightMan.R_RecursiveWorldNode( tr.world->nodes, 15, ( 1 << VVLightMan.num_dlights ) - 1 );
	xboxWorldDelta = tr.refdef.numDrawSurfs - xboxDrawBefore;

	if (g_SPXBSplitSlotActive == 2 && xboxWorldDelta == 0 && tr.world && tr.world->nodes)
	{
		const int retryBefore = tr.refdef.numDrawSurfs;
		g_SPXBSplitSlot1WorldFallback = 1;
		++tr.viewCount;
		VVLightMan.R_RecursiveWorldNode( tr.world->nodes, 15, ( 1 << VVLightMan.num_dlights ) - 1 );
		g_SPXBSplitSlot1WorldRetryDelta = (unsigned int)(tr.refdef.numDrawSurfs - retryBefore);
		xboxWorldDelta = tr.refdef.numDrawSurfs - xboxDrawBefore;
	}

	if (g_SPXBSplitSlotActive == 2 && xboxWorldDelta == 0 && tr.world && tr.world->nodes)
	{
		const int retryBefore = tr.refdef.numDrawSurfs;

		g_SPXBSplitSlot1WorldFallback = 2;
		++tr.viewCount;
		/* Keep this retry bounded by the PVS. The earlier all-visible probe proved
		   the render path, but it is too expensive for a release soak. */
		VVLightMan.R_RecursiveWorldNode( tr.world->nodes, 0, ( 1 << VVLightMan.num_dlights ) - 1 );
		g_SPXBSplitSlot1WorldRetryDelta = (unsigned int)(tr.refdef.numDrawSurfs - retryBefore);
		xboxWorldDelta = tr.refdef.numDrawSurfs - xboxDrawBefore;
	}
	R_XboxAddVisibleBorgFieldSurfaces( ( 1 << VVLightMan.num_dlights ) - 1 );
	R_XboxAddBorg6BridgeLeafSurfaces( ( 1 << VVLightMan.num_dlights ) - 1 );
	xboxWorldDelta = tr.refdef.numDrawSurfs - xboxDrawBefore;
#ifdef _XBOX
	if (g_SPXBSplitSlotActive == 1)
	{
		g_SPXBSplitSlot0WorldDelta = (unsigned int)xboxWorldDelta;
	}
	else if (g_SPXBSplitSlotActive == 2)
	{
		g_SPXBSplitSlot1WorldDelta = (unsigned int)xboxWorldDelta;
	}
	{
		static int s_stefxWorldVisBudget = 24;
		if (s_stefxWorldVisBudget > 0)
		{
			XBLF("STEFX: worldvis cluster=%d leafDelta=%d drawDelta=%d totalDraw=%d visCount=%d rootVis=%d r_novis=%d r_nocull=%d bounds=(%g,%g,%g)-(%g,%g,%g)",
				tr.viewCluster,
				tr.pc.c_leafs - xboxLeafsBefore,
				xboxWorldDelta,
				tr.refdef.numDrawSurfs,
				tr.visCount,
				(tr.world && tr.world->nodes) ? tr.world->nodes[0].visframe : -1,
				r_novis ? r_novis->integer : -1,
				r_nocull ? r_nocull->integer : -1,
				tr.viewParms.visBounds[0][0],
				tr.viewParms.visBounds[0][1],
				tr.viewParms.visBounds[0][2],
				tr.viewParms.visBounds[1][0],
				tr.viewParms.visBounds[1][1],
				tr.viewParms.visBounds[1][2]);
			--s_stefxWorldVisBudget;
		}
	}
	if (s_xboxAddWorldLogBudget > 0)
	{
		XBLF("JA: R_AddWorldSurfaces after recursive leafs=%d drawSurfs=%d visBounds=(%g,%g,%g)-(%g,%g,%g)",
			tr.pc.c_leafs,
			tr.refdef.numDrawSurfs,
			tr.viewParms.visBounds[0][0],
			tr.viewParms.visBounds[0][1],
			tr.viewParms.visBounds[0][2],
			tr.viewParms.visBounds[1][0],
			tr.viewParms.visBounds[1][1],
			tr.viewParms.visBounds[1][2]);
		--s_xboxAddWorldLogBudget;
	}
#endif
}

#else // _XBOX

void R_AddWorldSurfaces (void) {
	if ( !r_drawworld->integer ) {
		return;
	}

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return;
	}

	tr.currentEntityNum = TR_WORLDENT;
	tr.shiftedEntityNum = tr.currentEntityNum << QSORT_ENTITYNUM_SHIFT;

	// determine which leaves are in the PVS / areamask
	R_MarkLeaves ();

	// clear out the visible min/max
	ClearBounds( tr.viewParms.visBounds[0], tr.viewParms.visBounds[1] );

	// perform frustum culling and add all the potentially visible surfaces
	if ( tr.refdef.num_dlights > 32 ) {
		tr.refdef.num_dlights = 32 ;
	}

	R_RecursiveWorldNode( tr.world->nodes, 31, ( 1 << tr.refdef.num_dlights ) - 1 );
}

#endif // _XBOX
