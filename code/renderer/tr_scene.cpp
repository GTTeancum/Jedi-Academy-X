// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"



#include "tr_local.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
extern "C" volatile unsigned int g_SPXBSplitSlotActive;
extern "C" volatile unsigned int g_SPXBSplitSlot0DrawDelta;
extern "C" volatile unsigned int g_SPXBSplitSlot1DrawDelta;
extern "C" volatile unsigned int g_SPXBSplitSlot0Cluster;
extern "C" volatile unsigned int g_SPXBSplitSlot1Cluster;
extern "C" volatile unsigned int g_SPXBSplitP2Ent;
extern "C" volatile unsigned int g_SPXBSplitP2RendererRefs;
extern "C" volatile unsigned int g_SPXBSplitP2RendererLastModel;
extern "C" volatile unsigned int g_SPXBSplitP2RendererLastRenderfx;
extern "C" volatile unsigned int g_SPXBSplitP2RendererLastZ;
extern "C" volatile unsigned int g_SPXBHelmetP1Model;
extern "C" volatile unsigned int g_SPXBHelmetP2Model;
extern "C" volatile unsigned int g_SPXBHelmetRendererRefs;
extern "C" volatile unsigned int g_SPXBHelmetRendererLastModel;
extern "C" volatile unsigned int g_SPXBHelmetRendererLastRenderfx;
extern "C" volatile unsigned int g_SPXBHelmetRendererLastEnt;
#endif

#ifdef VV_LIGHTING
#include "tr_lightmanager.h"
#endif

#ifdef _XBOX
static bool R_XboxTracePolyShader( const shader_t *shader )
{
	const char *name = shader ? shader->name : "";

	if ( !name )
	{
		name = "";
	}

	return strstr( name, "gfx/effects/" ) ||
		strstr( name, "gfx/misc/" ) ||
		strstr( name, "gfx/interface/" ) ||
		strstr( name, "crosshair" );
}

#if defined(STEFX_ELITE_FORCE_SP)
static bool R_STEFX_IsSplitHelmetModel( qhandle_t model )
{
	return model &&
		( model == (qhandle_t)g_SPXBHelmetP1Model ||
			model == (qhandle_t)g_SPXBHelmetP2Model );
}
#endif
#endif

int			r_firstSceneDrawSurf;

int			r_numdlights;
int			r_firstSceneDlight;

int			r_numentities;
int			r_firstSceneEntity;

int			r_numpolys;
int			r_firstScenePoly;

int			r_numpolyverts;

int			skyboxportal;
int			drawskyboxportal;

/*
====================
R_ToggleSmpFrame

====================
*/
void R_ToggleSmpFrame( void ) {

	backEndData->commands.used = 0;

	r_firstSceneDrawSurf = 0;

	r_numdlights = 0;
	r_firstSceneDlight = 0;

#ifdef VV_LIGHTING
	VVLightMan.num_dlights = 0;
#endif

	r_numentities = 0;
	r_firstSceneEntity = 0;

	r_numpolys = 0;
	r_firstScenePoly = 0;

	r_numpolyverts = 0;
}


/*
====================
RE_ClearScene

====================
*/
void RE_ClearScene( void ) {
	r_firstSceneDlight = r_numdlights;
	r_firstSceneEntity = r_numentities;
	r_firstScenePoly = r_numpolys;
	tr.refdef.rdflags &= ~(RDF_doLAGoggles|RDF_doFullbright);	//probably not needed since it gets copied over in RE_RenderScene
}

/*
===========================================================================

DISCRETE POLYS

===========================================================================
*/

/*
=====================
R_AddPolygonSurfaces

Adds all the scene's polys into this view's drawsurf list
=====================
*/
void R_AddPolygonSurfaces( void ) {
	int			i;
	shader_t	*sh;
	srfPoly_t	*poly;

	tr.currentEntityNum = TR_WORLDENT;
	tr.shiftedEntityNum = tr.currentEntityNum << QSORT_ENTITYNUM_SHIFT;

	for ( i = 0, poly = tr.refdef.polys; i < tr.refdef.numPolys ; i++, poly++ ) {
		sh = R_GetShaderByHandle( poly->hShader );
#ifdef _XBOX
		if ( cls.state == CA_ACTIVE && R_XboxTracePolyShader( sh ) )
		{
			static int s_stefxAddPolySurfBudget = 160;
			if ( s_stefxAddPolySurfBudget > 0 )
			{
				XBLF( "STEFX: R_AddPolygonSurfaces shader='%s' handle=%d verts=%d fog=%d poly=%d/%d",
					sh ? sh->name : "<null>", poly->hShader, poly->numVerts, poly->fogIndex,
					i, tr.refdef.numPolys );
				--s_stefxAddPolySurfBudget;
			}
		}
#endif
		R_AddDrawSurf( ( surfaceType_t * )poly, sh, poly->fogIndex, qfalse );
	}
}

/*
=====================
RE_AddPolyToScene

=====================
*/
void RE_AddPolyToScene( qhandle_t hShader , int numVerts, const polyVert_t *verts ) {
	srfPoly_t	*poly;
	int			i;
	int			fogIndex = 0;
	fog_t		*fog;
	vec3_t		bounds[2];

	if ( !tr.registered ) {
		return;
	}

	if ( !hShader ) {
#ifndef FINAL_BUILD
		VID_Printf( PRINT_WARNING, "WARNING: RE_AddPolyToScene: NULL poly shader\n");
#endif
		return;
	}

	if ( r_numpolyverts + numVerts > MAX_POLYVERTS || r_numpolys >= MAX_POLYS ) {
#ifdef _XBOX
		static int s_stefxPolyOverflowBudget = 32;
		if ( s_stefxPolyOverflowBudget > 0 )
		{
			XBLF( "STEFX: RE_AddPolyToScene overflow handle=%d verts=%d numPolys=%d/%d polyVerts=%d/%d",
				hShader, numVerts, r_numpolys, MAX_POLYS, r_numpolyverts, MAX_POLYVERTS );
			--s_stefxPolyOverflowBudget;
		}
#endif
#if defined(_DEBUG)
		Com_Printf(S_COLOR_RED"Poly overflow!  Tell Brian.\n");
#endif
		return;
	}

	poly = &backEndData->polys[r_numpolys];
	poly->surfaceType = SF_POLY;
	poly->hShader = hShader;
	poly->numVerts = numVerts;
	poly->verts = &backEndData->polyVerts[r_numpolyverts];
	
	memcpy( poly->verts, verts, numVerts * sizeof( *verts ) );
	r_numpolys++;
	r_numpolyverts += numVerts;

	// see if it is in a fog volume
	if ( !tr.world || tr.world->numfogs == 1) {
		fogIndex = 0;
	} else {
		// find which fog volume the poly is in
		VectorCopy( poly->verts[0].xyz, bounds[0] );
		VectorCopy( poly->verts[0].xyz, bounds[1] );
		for ( i = 1 ; i < poly->numVerts ; i++ ) {
			AddPointToBounds( poly->verts[i].xyz, bounds[0], bounds[1] );
		}
		for ( int fI = 1 ; fI < tr.world->numfogs ; fI++ ) {
			fog = &tr.world->fogs[fI]; 
			if ( bounds[0][0] >= fog->bounds[0][0]
				&& bounds[0][1] >= fog->bounds[0][1]
				&& bounds[0][2] >= fog->bounds[0][2]
				&& bounds[1][0] <= fog->bounds[1][0]
				&& bounds[1][1] <= fog->bounds[1][1]
				&& bounds[1][2] <= fog->bounds[1][2] ) 
			{//completely in this one
				fogIndex = fI;
				break;
			}
			else if ( ( bounds[0][0] >= fog->bounds[0][0] && bounds[0][1] >= fog->bounds[0][1] && bounds[0][2] >= fog->bounds[0][2] &&
						bounds[0][0] <= fog->bounds[1][0] && bounds[0][1] <= fog->bounds[1][1] && bounds[0][2] <= fog->bounds[1][2]) ||
				( bounds[1][0] >= fog->bounds[0][0] && bounds[1][1] >= fog->bounds[0][1] && bounds[1][2] >= fog->bounds[0][2] &&
					bounds[1][0] <= fog->bounds[1][0] && bounds[1][1] <= fog->bounds[1][1] && bounds[1][2] <= fog->bounds[1][2] ) ) 
			{//partially in this one
				if ( tr.refdef.fogIndex == fI || R_FogParmsMatch( tr.refdef.fogIndex, fI ) )
				{//take new one only if it's the same one that the viewpoint is in
					fogIndex = fI;
					break;
				}
				else if ( !fogIndex )
				{//didn't find one yet, so use this one
					fogIndex = fI;
				}
			}
		}
	}
	poly->fogIndex = fogIndex;
#ifdef _XBOX
	{
		shader_t *shader = R_GetShaderByHandle( hShader );
		if ( cls.state == CA_ACTIVE && R_XboxTracePolyShader( shader ) )
		{
			static int s_stefxAddPolySceneBudget = 160;
			if ( s_stefxAddPolySceneBudget > 0 )
			{
				const polyVert_t *v = poly->verts;
				XBLF( "STEFX: RE_AddPolyToScene shader='%s' handle=%d verts=%d fog=%d storedPolys=%d storedVerts=%d v0=(%g,%g,%g) v1=(%g,%g,%g) color0=%u,%u,%u,%u",
					shader ? shader->name : "<null>", hShader, numVerts, fogIndex,
					r_numpolys, r_numpolyverts,
					v[0].xyz[0], v[0].xyz[1], v[0].xyz[2],
					numVerts > 1 ? v[1].xyz[0] : 0.0f,
					numVerts > 1 ? v[1].xyz[1] : 0.0f,
					numVerts > 1 ? v[1].xyz[2] : 0.0f,
					(unsigned int)v[0].modulate[0], (unsigned int)v[0].modulate[1],
					(unsigned int)v[0].modulate[2], (unsigned int)v[0].modulate[3] );
				--s_stefxAddPolySceneBudget;
			}
		}
	}
#endif
}


//=================================================================================


/*
=====================
RE_AddRefEntityToScene

=====================
*/
void RE_AddRefEntityToScene( const refEntity_t *ent ) {
	if ( !tr.registered ) {
		return;
	}
	if ( r_numentities >= TR_WORLDENT ) {
#ifndef FINAL_BUILD
		VID_Printf( PRINT_WARNING, "WARNING: RE_AddRefEntityToScene: too many entities\n");
#endif
		return;
	}
	if ( ent->reType < 0 || ent->reType >= RT_MAX_REF_ENTITY_TYPE ) {
		Com_Error( ERR_DROP, "RE_AddRefEntityToScene: bad reType %i", ent->reType );
	}

	backEndData->entities[r_numentities].e = *ent;
	backEndData->entities[r_numentities].lightingCalculated = qfalse;
#ifdef _XBOX
	backEndData->entities[r_numentities].visible = -1;
#if defined(STEFX_ELITE_FORCE_SP)
	if ( ent->reType == RT_LINE )
	{
		static int s_stefxLineSceneBudget = 96;
		if ( s_stefxLineSceneBudget > 0 )
		{
			shader_t *lineShader = R_GetShaderByHandle( ent->customShader );
			XBLog_WriteCriticalf("STEFX_RENDER_LINE scene slot=%d shader=%d name='%s' rf=0x%x width=%g rgba=(%u,%u,%u,%u) start=(%g,%g,%g) end=(%g,%g,%g)",
				r_numentities,
				ent->customShader,
				lineShader ? lineShader->name : "<null>",
				ent->renderfx,
				ent->radius,
				(unsigned int)ent->shaderRGBA[0],
				(unsigned int)ent->shaderRGBA[1],
				(unsigned int)ent->shaderRGBA[2],
				(unsigned int)ent->shaderRGBA[3],
				ent->origin[0], ent->origin[1], ent->origin[2],
				ent->oldorigin[0], ent->oldorigin[1], ent->oldorigin[2]);
			--s_stefxLineSceneBudget;
		}
	}
	if ( ent->reType == RT_MODEL && R_STEFX_IsSplitHelmetModel( ent->hModel ) )
	{
		++g_SPXBHelmetRendererRefs;
		g_SPXBHelmetRendererLastModel = (unsigned int)ent->hModel;
		g_SPXBHelmetRendererLastRenderfx = (unsigned int)ent->renderfx;
		g_SPXBHelmetRendererLastEnt = (unsigned int)ent->number;
	}
	if ( ent->reType == RT_MODEL && ent->number > 0 && ent->number == (int)g_SPXBSplitP2Ent )
	{
		++g_SPXBSplitP2RendererRefs;
		g_SPXBSplitP2RendererLastModel = (unsigned int)ent->hModel;
		g_SPXBSplitP2RendererLastRenderfx = (unsigned int)ent->renderfx;
		g_SPXBSplitP2RendererLastZ = (unsigned int)(int)ent->origin[2];
	}
	if (ent->reType == RT_MODEL && ent->hModel >= 2 && ent->hModel <= 8)
	{
		static int s_stefxSceneAddRefBudget = 96;
		if (s_stefxSceneAddRefBudget > 0)
		{
			XBLog_Writef("STEFX: renderer AddRef accepted slot=%d ent=%d hModel=%d renderfx=0x%x origin=(%g,%g,%g) axis0=(%g,%g,%g)",
				r_numentities,
				ent->number,
				ent->hModel,
				ent->renderfx,
				ent->origin[0], ent->origin[1], ent->origin[2],
				ent->axis[0][0], ent->axis[0][1], ent->axis[0][2]);
			--s_stefxSceneAddRefBudget;
		}
	}
#endif
#endif

	r_numentities++;
}


/*
=====================
RE_AddLightToScene

=====================
*/
void RE_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
#ifndef VV_LIGHTING
	dlight_t	*dl;

	if ( !tr.registered ) {
		return;
	}
	if ( r_numdlights >= MAX_DLIGHTS ) {
		return;
	}
	if ( intensity <= 0 ) {
		return;
	}
	dl = &backEndData->dlights[r_numdlights++];
	VectorCopy (org, dl->origin);
	dl->radius = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
#endif // VV_LIGHTING
}


/*
@@@@@@@@@@@@@@@@@@@@@
RE_RenderScene

Draw a 3D view into a part of the window, then return
to 2D drawing.

Rendering a scene may require multiple views to be rendered
to handle mirrors,
@@@@@@@@@@@@@@@@@@@@@
*/
extern int	recursivePortalCount;

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
static qboolean s_stefxSplitP2RefdefValid = qfalse;
static refdef_t s_stefxSplitP2Refdef;
static vec3_t s_stefxSplitP2PvsOrigin;

void RE_STEFX_SplitScreen_SetP2Refdef(const refdef_t *refdef, qboolean valid)
{
	if (!valid || !refdef)
	{
		s_stefxSplitP2RefdefValid = qfalse;
		return;
	}

	s_stefxSplitP2Refdef = *refdef;
	VectorCopy(refdef->vieworg, s_stefxSplitP2PvsOrigin);
	s_stefxSplitP2RefdefValid = qtrue;
}

void RE_STEFX_SplitScreen_SetP2PvsOrigin(const vec3_t origin)
{
	if (!origin)
	{
		return;
	}

	VectorCopy(origin, s_stefxSplitP2PvsOrigin);
}

static qboolean R_STEFX_GetP2Refdef(refdef_t *out, vec3_t pvsOrigin)
{
	if (!s_stefxSplitP2RefdefValid)
	{
		return qfalse;
	}
	if (out)
	{
		*out = s_stefxSplitP2Refdef;
	}
	if (pvsOrigin)
	{
		VectorCopy(s_stefxSplitP2PvsOrigin, pvsOrigin);
	}
	return qtrue;
}

static int R_STEFX_ClampSplitPlayers(int players)
{
	if (players < 1)
	{
		return 1;
	}
	if (players > 4)
	{
		return 4;
	}
	return players;
}

static qboolean R_STEFX_ShouldRenderSplitScreen(const refdef_t *fd, int *players)
{
	int requestedPlayers;

	if (!fd)
	{
		return qfalse;
	}
	if (!Cvar_VariableIntegerValue("stefx_splitScreen"))
	{
		return qfalse;
	}
	if (fd->rdflags & (RDF_SKYBOXPORTAL | RDF_NOWORLDMODEL))
	{
		return qfalse;
	}
	if (fd->width <= 1 || fd->height <= 1)
	{
		return qfalse;
	}

	requestedPlayers = R_STEFX_ClampSplitPlayers(Cvar_VariableIntegerValue("stefx_splitScreenPlayers"));
	if (requestedPlayers < 2)
	{
		return qfalse;
	}

	if (players)
	{
		*players = requestedPlayers;
	}
	return qtrue;
}

static float R_STEFX_CalcFovYForViewport(float fovX, int width, int height)
{
	float x;

	if (width <= 0 || height <= 0)
	{
		return fovX;
	}

	x = (float)width / tan(fovX / 360.0f * M_PI);
	return atan2((float)height, x) * 360.0f / M_PI;
}

static void R_STEFX_SetSplitViewport(trRefdef_t *refdef, viewParms_t *parms, const trRefdef_t *sourceRefdef, const viewParms_t *sourceParms, int slot, int players)
{
	int x;
	int y;
	int w;
	int h;
	int halfW;
	int halfH;

	*refdef = *sourceRefdef;
	*parms = *sourceParms;

	x = sourceRefdef->x;
	y = sourceRefdef->y;
	w = sourceRefdef->width;
	h = sourceRefdef->height;
	players = R_STEFX_ClampSplitPlayers(players);

	if (players == 2)
	{
		const int topHeight = sourceRefdef->height / 2;

		y = sourceRefdef->y + (slot ? topHeight : 0);
		h = slot ? (sourceRefdef->height - topHeight) : topHeight;
	}
	else if (players >= 3)
	{
		halfW = sourceRefdef->width / 2;
		halfH = sourceRefdef->height / 2;
		x = sourceRefdef->x + ((slot & 1) ? halfW : 0);
		y = sourceRefdef->y + ((slot & 2) ? halfH : 0);
		w = (slot & 1) ? (sourceRefdef->width - halfW) : halfW;
		h = (slot & 2) ? (sourceRefdef->height - halfH) : halfH;
	}

	refdef->x = x;
	refdef->y = y;
	refdef->width = w;
	refdef->height = h;
	refdef->fov_y = R_STEFX_CalcFovYForViewport(refdef->fov_x, w, h);

	parms->viewportX = x;
	parms->viewportY = glConfig.vidHeight - (y + h);
	parms->viewportWidth = w;
	parms->viewportHeight = h;
	parms->fovX = refdef->fov_x;
	parms->fovY = refdef->fov_y;
}

static void R_STEFX_ApplyExternalSplitView(trRefdef_t *refdef, viewParms_t *parms, const refdef_t *externalRefdef, const vec3_t pvsOrigin)
{
	int i;

	if (!refdef || !parms || !externalRefdef)
	{
		return;
	}

	refdef->fov_x = externalRefdef->fov_x;
	refdef->fov_y = R_STEFX_CalcFovYForViewport(refdef->fov_x, refdef->width, refdef->height);
	VectorCopy(externalRefdef->vieworg, refdef->vieworg);
	VectorCopy(externalRefdef->viewaxis[0], refdef->viewaxis[0]);
	VectorCopy(externalRefdef->viewaxis[1], refdef->viewaxis[1]);
	VectorCopy(externalRefdef->viewaxis[2], refdef->viewaxis[2]);
	refdef->time = externalRefdef->time;
	refdef->areamaskModified = qtrue;
	for (i = 0; i < MAX_MAP_AREA_BYTES; ++i)
	{
		refdef->areamask[i] = externalRefdef->areamask[i];
	}

	parms->fovX = refdef->fov_x;
	parms->fovY = refdef->fov_y;
	VectorCopy(refdef->vieworg, parms->or.origin);
	VectorCopy(refdef->viewaxis[0], parms->or.axis[0]);
	VectorCopy(refdef->viewaxis[1], parms->or.axis[1]);
	VectorCopy(refdef->viewaxis[2], parms->or.axis[2]);
	if (pvsOrigin)
	{
		VectorCopy(pvsOrigin, parms->pvsOrigin);
	}
	else
	{
		VectorCopy(refdef->vieworg, parms->pvsOrigin);
	}
}
#endif

void RE_RenderScene( const refdef_t *fd ) {
	viewParms_t		parms;
	int				startTime;
	static int		lastTime = 0;
#ifdef _XBOX
	const int xboxRenderSceneLog = (fd && fd->time >= 3400 && fd->time <= 5000);
	if (xboxRenderSceneLog)
	{
		XBLF("JA: CL_EARLY RE_RenderScene enter fd=%p time=%d rd=0x%x world=%p registered=%d",
			(void*)fd, fd ? fd->time : -1, fd ? fd->rdflags : 0, (void*)tr.world, tr.registered);
	}
#endif

	if ( !tr.registered ) {
#ifdef _XBOX
		if (xboxRenderSceneLog) XBLog_Write("JA: CL_EARLY RE_RenderScene not registered return");
#endif
		return;
	}
	GLimp_LogComment( "====== RE_RenderScene =====\n" );

	if ( r_norefresh->integer ) {
#ifdef _XBOX
		if (xboxRenderSceneLog) XBLog_Write("JA: CL_EARLY RE_RenderScene norefresh return");
#endif
		return;
	}

	startTime = Sys_Milliseconds();

	if (!tr.world && !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
#ifdef _XBOX
		if (xboxRenderSceneLog) XBLog_Write("JA: CL_EARLY RE_RenderScene NULL world Com_Error");
#endif
		Com_Error (ERR_DROP, "R_RenderScene: NULL worldmodel");
	}

//	memcpy( tr.refdef.text, fd->text, sizeof( tr.refdef.text ) );

	tr.refdef.x = fd->x;
	tr.refdef.y = fd->y;
	tr.refdef.width = fd->width;
	tr.refdef.height = fd->height;
	tr.refdef.fov_x = fd->fov_x;
	tr.refdef.fov_y = fd->fov_y;

	VectorCopy( fd->vieworg, tr.refdef.vieworg );
	VectorCopy( fd->viewaxis[0], tr.refdef.viewaxis[0] );
	VectorCopy( fd->viewaxis[1], tr.refdef.viewaxis[1] );
	VectorCopy( fd->viewaxis[2], tr.refdef.viewaxis[2] );

	tr.refdef.time = fd->time;
	tr.refdef.frametime = fd->time - lastTime;
	tr.refdef.rdflags = fd->rdflags;

	if (fd->rdflags & RDF_SKYBOXPORTAL)
	{
		skyboxportal = 1;
	}
	else
	{
		// cdr - only change last time for the real render, not the portal
		lastTime = fd->time;
	}

	if (fd->rdflags & RDF_DRAWSKYBOX)
	{
		drawskyboxportal = 1;
	}
	else
	{
		drawskyboxportal = 0;
	}

	// copy the areamask data over and note if it has changed, which
	// will force a reset of the visible leafs even if the view hasn't moved
	tr.refdef.areamaskModified = qfalse;
	if ( ! (tr.refdef.rdflags & RDF_NOWORLDMODEL) ) {
		int		areaDiff;
		int		i;

		// compare the area bits
		areaDiff = 0;
		for (i = 0 ; i < MAX_MAP_AREA_BYTES/4 ; i++) {
			areaDiff |= ((int *)tr.refdef.areamask)[i] ^ ((int *)fd->areamask)[i];
			((int *)tr.refdef.areamask)[i] = ((int *)fd->areamask)[i];
		}

		if ( areaDiff ) {
			// a door just opened or something
			tr.refdef.areamaskModified = qtrue;
		}
	}


	// derived info

	tr.refdef.floatTime = tr.refdef.time * 0.001;

	tr.refdef.numDrawSurfs = r_firstSceneDrawSurf;
	tr.refdef.drawSurfs = backEndData->drawSurfs;

	tr.refdef.num_entities = r_numentities - r_firstSceneEntity;
	tr.refdef.entities = &backEndData->entities[r_firstSceneEntity];

#ifndef VV_LIGHTING
	tr.refdef.num_dlights = r_numdlights - r_firstSceneDlight;
	tr.refdef.dlights = &backEndData->dlights[r_firstSceneDlight];
#endif

	tr.refdef.numPolys = r_numpolys - r_firstScenePoly;
	tr.refdef.polys = &backEndData->polys[r_firstScenePoly];
#ifdef _XBOX
	if (xboxRenderSceneLog)
	{
		XBLF("JA: CL_EARLY RE_RenderScene refdef rect=%d,%d %dx%d fov=%g/%g ents=%d polys=%d firstSurf=%d numDraw=%d vieworg=(%g,%g,%g)",
			tr.refdef.x,
			tr.refdef.y,
			tr.refdef.width,
			tr.refdef.height,
			tr.refdef.fov_x,
			tr.refdef.fov_y,
			tr.refdef.num_entities, tr.refdef.numPolys, r_firstSceneDrawSurf, tr.refdef.numDrawSurfs,
			tr.refdef.vieworg[0], tr.refdef.vieworg[1], tr.refdef.vieworg[2]);
	}
#endif

	// turn off dynamic lighting globally by clearing all the
	// dlights if it needs to be disabled or if vertex lighting is enabled
#ifndef VV_LIGHTING
	if ( r_dynamiclight->integer == 0 ||
		 r_vertexLight->integer == 1 ) {
		tr.refdef.num_dlights = 0;
	}
#endif

	// a single frame may have multiple scenes draw inside it --
	// a 3D game view, 3D status bar renderings, 3D menus, etc.
	// They need to be distinguished by the light flare code, because
	// the visibility state for a given surface may be different in
	// each scene / view.
	tr.frameSceneNum++;
	tr.sceneCount++;

	// setup view parms for the initial view
	//
	// set up viewport
	// The refdef takes 0-at-the-top y coordinates, so
	// convert to GL's 0-at-the-bottom space
	//
	memset( &parms, 0, sizeof( parms ) );
	parms.viewportX = tr.refdef.x;
	parms.viewportY = glConfig.vidHeight - ( tr.refdef.y + tr.refdef.height );
	parms.viewportWidth = tr.refdef.width;
	parms.viewportHeight = tr.refdef.height;
	parms.isPortal = qfalse;

	parms.fovX = tr.refdef.fov_x;
	parms.fovY = tr.refdef.fov_y;

	VectorCopy( fd->vieworg, parms.or.origin );
	VectorCopy( fd->viewaxis[0], parms.or.axis[0] );
	VectorCopy( fd->viewaxis[1], parms.or.axis[1] );
	VectorCopy( fd->viewaxis[2], parms.or.axis[2] );

	VectorCopy( fd->vieworg, parms.pvsOrigin );

	recursivePortalCount = 0;
#ifdef _XBOX
	if (xboxRenderSceneLog)
	{
		XBLF("JA: CL_EARLY RE_RenderScene before R_RenderView parms viewport=%d,%d %dx%d fov=%g/%g origin=(%g,%g,%g)",
			parms.viewportX,
			parms.viewportY,
			parms.viewportWidth,
			parms.viewportHeight,
			parms.fovX,
			parms.fovY,
			parms.or.origin[0],
			parms.or.origin[1],
			parms.or.origin[2]);
	}
#endif
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	{
		int splitPlayers = 1;
		if (R_STEFX_ShouldRenderSplitScreen(fd, &splitPlayers))
		{
			int slot;
			int splitDrawSurfBase;
			trRefdef_t sourceRefdef = tr.refdef;
			viewParms_t sourceParms = parms;
			refdef_t p2Refdef;
			vec3_t p2PvsOrigin;
			qboolean hasP2Refdef = R_STEFX_GetP2Refdef(&p2Refdef, p2PvsOrigin);
			static qboolean s_loggedSplitViewports = qfalse;
			const qboolean logSplitViewports = !s_loggedSplitViewports;
			splitDrawSurfBase = tr.refdef.numDrawSurfs;

			if (logSplitViewports)
			{
				XBLF("STEFX_SPLITSCREEN_RENDER armed players=%d p2Refdef=%d source=%d,%d %dx%d gl=%d,%d %dx%d fov=%g/%g",
					splitPlayers,
					hasP2Refdef ? 1 : 0,
					sourceRefdef.x,
					sourceRefdef.y,
					sourceRefdef.width,
					sourceRefdef.height,
					sourceParms.viewportX,
					sourceParms.viewportY,
					sourceParms.viewportWidth,
					sourceParms.viewportHeight,
					sourceRefdef.fov_x,
					sourceRefdef.fov_y);
			}

			for (slot = 0; slot < splitPlayers; ++slot)
			{
				int splitSlotDrawStart;
				R_STEFX_SetSplitViewport(&tr.refdef, &parms, &sourceRefdef, &sourceParms, slot, splitPlayers);
				tr.refdef.numDrawSurfs = splitDrawSurfBase;
				g_SPXBSplitSlotActive = (unsigned int)(slot + 1);
				if (slot == 0)
				{
					g_SPXBSplitSlot0DrawDelta = 0;
					g_SPXBSplitSlot0Cluster = 0xffffffffu;
				}
				else if (slot == 1)
				{
					g_SPXBSplitSlot1DrawDelta = 0;
					g_SPXBSplitSlot1Cluster = 0xffffffffu;
				}
				if (slot == 1 && hasP2Refdef)
				{
					/* Some EF maps report a valid-looking chase-camera cluster whose
					   PVS marks no world leaves. Keep the P2 camera independent, but
					   seed world visibility from P1 so the retry stays PVS-bounded. */
					int areaByte;
					R_STEFX_ApplyExternalSplitView(&tr.refdef, &parms, &p2Refdef, sourceParms.pvsOrigin);
					for (areaByte = 0; areaByte < MAX_MAP_AREA_BYTES; ++areaByte)
					{
						tr.refdef.areamask[areaByte] = sourceRefdef.areamask[areaByte];
					}
					tr.refdef.areamaskModified = qtrue;
				}
				if (logSplitViewports)
				{
					XBLF("STEFX_SPLITSCREEN_RENDER slot=%d p2View=%d drawBase=%d ref=%d,%d %dx%d gl=%d,%d %dx%d fov=%g/%g view=(%g,%g,%g) pvs=(%g,%g,%g)",
						slot,
						(slot == 1 && hasP2Refdef) ? 1 : 0,
						splitDrawSurfBase,
						tr.refdef.x,
						tr.refdef.y,
						tr.refdef.width,
						tr.refdef.height,
						parms.viewportX,
						parms.viewportY,
						parms.viewportWidth,
						parms.viewportHeight,
						tr.refdef.fov_x,
						tr.refdef.fov_y,
						tr.refdef.vieworg[0],
						tr.refdef.vieworg[1],
						tr.refdef.vieworg[2],
						parms.pvsOrigin[0],
						parms.pvsOrigin[1],
						parms.pvsOrigin[2]);
				}
				recursivePortalCount = 0;
				splitSlotDrawStart = tr.refdef.numDrawSurfs;
				R_RenderView( &parms );
				if (slot == 0)
				{
					g_SPXBSplitSlot0DrawDelta = (unsigned int)(tr.refdef.numDrawSurfs - splitSlotDrawStart);
					g_SPXBSplitSlot0Cluster = (unsigned int)tr.viewCluster;
				}
				else if (slot == 1)
				{
					g_SPXBSplitSlot1DrawDelta = (unsigned int)(tr.refdef.numDrawSurfs - splitSlotDrawStart);
					g_SPXBSplitSlot1Cluster = (unsigned int)tr.viewCluster;
				}
				if (tr.refdef.numDrawSurfs > splitDrawSurfBase)
				{
					splitDrawSurfBase = tr.refdef.numDrawSurfs;
				}
			}
			g_SPXBSplitSlotActive = 0;

			s_loggedSplitViewports = qtrue;
			tr.refdef = sourceRefdef;
			tr.refdef.numDrawSurfs = splitDrawSurfBase;
			parms = sourceParms;
		}
		else
		{
			R_RenderView( &parms );
		}
	}
#else
	R_RenderView( &parms );
#endif
#ifdef _XBOX
	if (xboxRenderSceneLog) XBLog_Write("JA: CL_EARLY RE_RenderScene after R_RenderView");
#endif

	// the next scene rendered in this frame will tack on after this one
	r_firstSceneDrawSurf = tr.refdef.numDrawSurfs;
	r_firstSceneEntity = r_numentities;
	r_firstSceneDlight = r_numdlights;
	r_firstScenePoly = r_numpolys;

	tr.frontEndMsec += Sys_Milliseconds() - startTime;
#ifdef _XBOX
	if (xboxRenderSceneLog) XBLog_Write("JA: CL_EARLY RE_RenderScene before RE_RenderWorldEffects");
#endif
	RE_RenderWorldEffects();
#ifdef _XBOX
	if (xboxRenderSceneLog) XBLog_Write("JA: CL_EARLY RE_RenderScene done");
#endif
}
