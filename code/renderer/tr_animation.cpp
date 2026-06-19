// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"


#include "tr_local.h"
#include "MatComp.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
#endif

#ifdef VV_LIGHTING
#include "tr_lightmanager.h"
#endif

extern int R_ComputeLOD( trRefEntity_t *ent );

/*

All bones should be an identity orientation to display the mesh exactly
as it is specified.

For all other frames, the bones represent the transformation from the 
orientation of the bone in the base frame to the orientation in this
frame.

*/


/*
=============
R_ACullModel
=============
*/
static int R_ACullModel( md4Header_t *header, trRefEntity_t *ent ) {
	vec3_t		bounds[2];
	md4Frame_t	*oldFrame, *newFrame;
	int			i;
	int			frameSize;
	// compute frame pointers

	if (header->ofsFrames<0) // Compressed
	{
		frameSize = (int)( &((md4CompFrame_t *)0)->bones[ tr.currentModel->md4->numBones ] );		
		newFrame = (md4Frame_t *)((byte *)header - header->ofsFrames + ent->e.frame * frameSize );
		oldFrame = (md4Frame_t *)((byte *)header - header->ofsFrames + ent->e.oldframe * frameSize );
		// HACK! These frames actually are md4CompFrames, but the first fields are the same, 
		// so this will work for this routine.
	}
	else
	{
		frameSize = (int)( &((md4Frame_t *)0)->bones[ tr.currentModel->md4->numBones ] );		
		newFrame = (md4Frame_t *)((byte *)header + header->ofsFrames + ent->e.frame * frameSize );
		oldFrame = (md4Frame_t *)((byte *)header + header->ofsFrames + ent->e.oldframe * frameSize );
	}

	// cull bounding sphere ONLY if this is not an upscaled entity
	if ( !ent->e.nonNormalizedAxes )
	{
		if ( ent->e.frame == ent->e.oldframe )
		{
			switch ( R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius ) )
			{
			case CULL_OUT:
				tr.pc.c_sphere_cull_md3_out++;
				return CULL_OUT;

			case CULL_IN:
				tr.pc.c_sphere_cull_md3_in++;
				return CULL_IN;

			case CULL_CLIP:
				tr.pc.c_sphere_cull_md3_clip++;
				break;
			}
		}
		else
		{
			int sphereCull, sphereCullB;

			sphereCull  = R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius );
			if ( newFrame == oldFrame ) {
				sphereCullB = sphereCull;
			} else {
				sphereCullB = R_CullLocalPointAndRadius( oldFrame->localOrigin, oldFrame->radius );
			}

			if ( sphereCull == sphereCullB )
			{
				if ( sphereCull == CULL_OUT )
				{
					tr.pc.c_sphere_cull_md3_out++;
					return CULL_OUT;
				}
				else if ( sphereCull == CULL_IN )
				{
					tr.pc.c_sphere_cull_md3_in++;
					return CULL_IN;
				}
				else
				{
					tr.pc.c_sphere_cull_md3_clip++;
				}
			}
		}
	}
	
	// calculate a bounding box in the current coordinate system
	for (i = 0 ; i < 3 ; i++) {
		bounds[0][i] = oldFrame->bounds[0][i] < newFrame->bounds[0][i] ? oldFrame->bounds[0][i] : newFrame->bounds[0][i];
		bounds[1][i] = oldFrame->bounds[1][i] > newFrame->bounds[1][i] ? oldFrame->bounds[1][i] : newFrame->bounds[1][i];
	}

	switch ( R_CullLocalBox( bounds ) )
	{
	case CULL_IN:
		tr.pc.c_box_cull_md3_in++;
		return CULL_IN;
	case CULL_CLIP:
		tr.pc.c_box_cull_md3_clip++;
		return CULL_CLIP;
	case CULL_OUT:
	default:
		tr.pc.c_box_cull_md3_out++;
		return CULL_OUT;
	}
}

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
static int R_STEFX_ACullModel( md4Header_t *header, trRefEntity_t *ent, qboolean logCull )
{
	int cull;
	vec3_t delta;
	float distSq;

	cull = R_ACullModel( header, ent );
	if ( cull != CULL_OUT )
	{
		return cull;
	}

	vec3_t center;
	VectorCopy( ent->e.origin, center );
	center[2] += 32.0f;
	cull = R_CullPointAndRadius( center, 128.0f );
	if ( logCull )
	{
		XBLF( "STEFX: R_AddAnimSurfaces EF MDR cull fallback ent=%d h=%d model='%s' coarse=%d center=(%g,%g,%g)",
			ent->e.number,
			ent->e.hModel,
			tr.currentModel ? tr.currentModel->name : "(null)",
			cull,
			center[0],
			center[1],
			center[2] );
	}
	if ( cull == CULL_OUT )
	{
		VectorSubtract( ent->e.origin, tr.refdef.vieworg, delta );
		distSq = DotProduct( delta, delta );
		if ( distSq < ( 1024.0f * 1024.0f ) )
		{
			if ( logCull )
			{
				XBLF( "STEFX: R_AddAnimSurfaces EF MDR cull near fail-open ent=%d h=%d model='%s' distSq=%g vieworg=(%g,%g,%g)",
					ent->e.number,
					ent->e.hModel,
					tr.currentModel ? tr.currentModel->name : "(null)",
					distSq,
					tr.refdef.vieworg[0],
					tr.refdef.vieworg[1],
					tr.refdef.vieworg[2] );
			}
			return CULL_CLIP;
		}
		if ( logCull )
		{
			XBLF( "STEFX: R_AddAnimSurfaces EF MDR cull out ent=%d h=%d model='%s'",
				ent->e.number,
				ent->e.hModel,
				tr.currentModel ? tr.currentModel->name : "(null)" );
		}
		return CULL_OUT;
	}
	return CULL_CLIP;
}
#endif


/*
=================
R_AComputeFogNum

=================
*/
static int R_AComputeFogNum( md4Header_t *header, trRefEntity_t *ent ) {
	int				i;
	fog_t			*fog;
	md4Frame_t		*frame;
	vec3_t			localOrigin;
	int				frameSize;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}


	if (header->ofsFrames<0) // Compressed
	{
		frameSize = (int)( &((md4CompFrame_t *)0)->bones[ header->numBones ] );		
		frame = (md4Frame_t *)((byte *)header - header->ofsFrames + ent->e.frame * frameSize );
		// HACK! These frames actually are md4CompFrames, but the first fields are the same, 
		// so this will work for this routine.
	}
	else
	{
		frameSize = (int)( &((md4Frame_t *)0)->bones[ header->numBones ] );		
		frame = (md4Frame_t *)((byte *)header + header->ofsFrames + ent->e.frame * frameSize );
	}

	VectorAdd( ent->e.origin, frame->localOrigin, localOrigin );
	int partialFog = 0;
	for ( i = 1 ; i < tr.world->numfogs ; i++ ) {
		fog = &tr.world->fogs[i];
		if ( localOrigin[0] - frame->radius >= fog->bounds[0][0] 
			&& localOrigin[0] + frame->radius <= fog->bounds[1][0] 
			&& localOrigin[1] - frame->radius >= fog->bounds[0][1]
			&& localOrigin[1] + frame->radius <= fog->bounds[1][1] 
			&& localOrigin[2] - frame->radius >= fog->bounds[0][2]
			&& localOrigin[2] + frame->radius <= fog->bounds[1][2] ) 
		{//totally inside it
			return i;
			break;
		}
		if ( ( localOrigin[0] - frame->radius >= fog->bounds[0][0] && localOrigin[1] - frame->radius >= fog->bounds[0][1] && localOrigin[2] - frame->radius >= fog->bounds[0][2] &&
			localOrigin[0] - frame->radius <= fog->bounds[1][0] && localOrigin[1] - frame->radius <= fog->bounds[1][1] && localOrigin[2] - frame->radius <= fog->bounds[1][2] ) || 
			( localOrigin[0] + frame->radius >= fog->bounds[0][0] && localOrigin[1] + frame->radius >= fog->bounds[0][1] && localOrigin[2] + frame->radius >= fog->bounds[0][2] &&
			localOrigin[0] + frame->radius <= fog->bounds[1][0] && localOrigin[1] + frame->radius <= fog->bounds[1][1] && localOrigin[2] + frame->radius <= fog->bounds[1][2] ) ) 
		{//partially inside it
			if ( tr.refdef.fogIndex == i || R_FogParmsMatch( tr.refdef.fogIndex, i ) )
			{//take new one only if it's the same one that the viewpoint is in
				return i;
				break;
			}
			else if ( !partialFog )
			{//first partialFog
				partialFog = i;
			}
		}
	}
	//if all else fails, return the first partialFog
	return partialFog;
}

/*
==============
R_AddAnimSurfaces
==============
*/
void R_AddAnimSurfaces( trRefEntity_t *ent ) {
	md4Header_t		*header;
	md4Surface_t	*surface;
	md4LOD_t		*lod;
	shader_t		*shader = 0;
	shader_t		*cust_shader = 0;
	int				fogNum = 0;
	qboolean		personalModel;
	int				cull;
	int				i, whichLod;
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	static int		s_stefxAnimSurfEnterBudget = 24;
	static int		s_stefxAnimSurfCullBudget = 24;
	static int		s_stefxAnimSurfVisibleBudget = 96;
	qboolean		stefxTrackAnimSurf = qfalse;
	qboolean		stefxLogAnimSurf = qfalse;
#endif

	// don't add third_person objects if not in a portal
	personalModel = (ent->e.renderfx & RF_THIRD_PERSON) && !tr.viewParms.isPortal;

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if ( tr.refdef.time >= 65000 &&
		tr.currentModel && strstr( tr.currentModel->name, "models/players/" ) )
	{
		stefxTrackAnimSurf = qtrue;
		if ( s_stefxAnimSurfEnterBudget > 0 )
		{
			--s_stefxAnimSurfEnterBudget;
			XBLF( "STEFX: R_AddAnimSurfaces enter ent=%d h=%d model='%s' frame=%d old=%d rf=0x%x personal=%d origin=(%g,%g,%g)",
				ent->e.number,
				ent->e.hModel,
				tr.currentModel ? tr.currentModel->name : "(null)",
				ent->e.frame,
				ent->e.oldframe,
				ent->e.renderfx,
				personalModel,
				ent->e.origin[0],
				ent->e.origin[1],
				ent->e.origin[2] );
		}
	}
#endif

	if ( ent->e.renderfx & RF_CAP_FRAMES) {
		if (ent->e.frame > tr.currentModel->md4->numFrames-1)
			ent->e.frame = tr.currentModel->md4->numFrames-1;
		if (ent->e.oldframe > tr.currentModel->md4->numFrames-1)
			ent->e.oldframe = tr.currentModel->md4->numFrames-1;
	}
	else if ( ent->e.renderfx & RF_WRAP_FRAMES ) {
		ent->e.frame %= tr.currentModel->md4->numFrames;
		ent->e.oldframe %= tr.currentModel->md4->numFrames;
	}

	//
	// Validate the frames so there is no chance of a crash.
	// This will write directly into the entity structure, so
	// when the surfaces are rendered, they don't need to be
	// range checked again.
	//
	if ( (ent->e.frame >= tr.currentModel->md4->numFrames) 
		|| (ent->e.frame < 0)
		|| (ent->e.oldframe >= tr.currentModel->md4->numFrames)
		|| (ent->e.oldframe < 0) ) 
	{
#ifdef _DEBUG
			VID_Printf (PRINT_ALL, "R_AddAnimSurfaces: no such frame %d to %d for '%s'\n",
#else
			VID_Printf (PRINT_DEVELOPER, "R_AddAnimSurfaces: no such frame %d to %d for '%s'\n",				
#endif
			ent->e.oldframe, ent->e.frame,
			tr.currentModel->name );
			ent->e.frame = 0;
			ent->e.oldframe = 0;
	}

	header = tr.currentModel->md4;

	//
	// cull the entire model if merged bounding box of both frames
	// is outside the view frustum.
	//
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	cull = R_STEFX_ACullModel( header, ent, stefxTrackAnimSurf && s_stefxAnimSurfCullBudget > 0 );
#else
	cull = R_ACullModel ( header, ent );
#endif
	if ( cull == CULL_OUT ) {
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		if ( stefxTrackAnimSurf && s_stefxAnimSurfCullBudget > 0 )
		{
			--s_stefxAnimSurfCullBudget;
			XBLF( "STEFX: R_AddAnimSurfaces cull out ent=%d h=%d model='%s' frame=%d old=%d",
				ent->e.number,
				ent->e.hModel,
				tr.currentModel ? tr.currentModel->name : "(null)",
				ent->e.frame,
				ent->e.oldframe );
		}
#endif
		return;
	}

	//
	// compute LOD
	//
	lod = (md4LOD_t *)( (byte *)header + header->ofsLODs );
	whichLod = R_ComputeLOD( ent );
	for ( i = 0; i < whichLod; i++)
	{
		lod = (md4LOD_t*)( (byte *)lod + lod->ofsEnd );
	}
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if ( stefxTrackAnimSurf && s_stefxAnimSurfVisibleBudget > 0 )
	{
		stefxLogAnimSurf = qtrue;
		--s_stefxAnimSurfVisibleBudget;
		XBLF( "STEFX: R_AddAnimSurfaces visible ent=%d h=%d lod=%d surfaces=%d cull=%d",
			ent->e.number,
			ent->e.hModel,
			whichLod,
			lod->numSurfaces,
			cull );
	}
#endif

	//
	// set up lighting now that we know we aren't culled
	//
	if ( !personalModel || r_shadows->integer > 1 ) {
#ifdef VV_LIGHTING
		VVLightMan.R_SetupEntityLighting( &tr.refdef, ent );
#else
		R_SetupEntityLighting( &tr.refdef, ent );
#endif
	}

	//
	// see if we are in a fog volume
	//
	fogNum = R_AComputeFogNum( header, ent );


	//
	// draw all surfaces
	//
	cust_shader = R_GetShaderByHandle( ent->e.customShader );


	surface = (md4Surface_t *)( (byte *)lod + lod->ofsSurfaces );
	for ( i = 0 ; i < lod->numSurfaces ; i++ ) {
		if ( ent->e.customShader ) {
			shader = cust_shader;
		} else if ( ent->e.customSkin > 0 && ent->e.customSkin < tr.numSkins ) {
			skin_t *skin;
			int		j;
			
			skin = R_GetSkinByHandle( ent->e.customSkin );
			
			// match the surface name to something in the skin file
			shader = tr.defaultShader;
			for ( j = 0 ; j < skin->numSurfaces ; j++ ) {
				// the names have both been lowercased
				if ( !strcmp( skin->surfaces[j]->name, surface->name ) ) {
					shader = skin->surfaces[j]->shader;
					break;
				}
			}
		} else {
			shader = R_GetShaderByHandle( surface->shaderIndex );
		}
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		if ( stefxLogAnimSurf && i < 4 )
		{
			XBLF( "STEFX: R_AddAnimSurfaces surface ent=%d h=%d i=%d name='%s' shader='%s' fog=%d customSkin=%d default=%d",
				ent->e.number,
				ent->e.hModel,
				i,
				surface->name,
				shader ? shader->name : "(null)",
				fogNum,
				ent->e.customSkin,
				shader == tr.defaultShader );
		}
#endif
		// we will add shadows even if the main object isn't visible in the view

		// stencil shadows can't do personal models unless I polyhedron clip
		if ( !personalModel
			&& r_shadows->integer == 2 
#ifndef VV_LIGHTING
			&& fogNum == 0
#endif
			&& (ent->e.renderfx & RF_SHADOW_PLANE )
			&& !(ent->e.renderfx & ( RF_NOSHADOW | RF_DEPTHHACK ) ) 
			&& shader->sort == SS_OPAQUE ) {
			R_AddDrawSurf( (surfaceType_t *)surface, tr.shadowShader, 0, qfalse );
		}

		// projection shadows work fine with personal models
		if ( r_shadows->integer == 3
			&& fogNum == 0
			&& (ent->e.renderfx & RF_SHADOW_PLANE )
			&& shader->sort == SS_OPAQUE ) {
			R_AddDrawSurf( (surfaceType_t *)surface, tr.projectionShadowShader, 0, qfalse );
		}

		// don't add third_person objects if not viewing through a portal
		if ( !personalModel ) {
			R_AddDrawSurf( (surfaceType_t *)surface, shader, fogNum, qfalse );
		}

		surface = (md4Surface_t *)( (byte *)surface + surface->ofsEnd );
	}
}


/*
==============
RB_SurfaceAnim
==============
*/
void RB_SurfaceAnim( md4Surface_t *surface ) {
	int				i, j, k;
	float			frontlerp, backlerp;
	int				*triangles;
	int				indexes;
	int				baseIndex, baseVertex;
	int				numVerts;
	md4Vertex_t		*v;
	md4Bone_t		bones[MD4_MAX_BONES];
	md4Bone_t		tbone[2];
	md4Bone_t		*bonePtr, *bone;
	md4Header_t		*header;
	md4Frame_t		*frame=0;
	md4Frame_t		*oldFrame=0;
	md4CompFrame_t	*cframe=0;
	md4CompFrame_t	*coldFrame=0;
	int				frameSize;
	qboolean		compressed;


	if (  backEnd.currentEntity->e.oldframe == backEnd.currentEntity->e.frame ) {
		backlerp = 0;
		frontlerp = 1;
	} else  {
		backlerp = backEnd.currentEntity->e.backlerp;
		frontlerp = 1.0 - backlerp;
	}
	header = (md4Header_t *)((byte *)surface + surface->ofsHeader);

	if (header->ofsFrames<0) // Compressed
	{
		compressed = qtrue;
		frameSize = (int)( &((md4CompFrame_t *)0)->bones[ header->numBones ] );		
		cframe = (md4CompFrame_t *)((byte *)header - header->ofsFrames + backEnd.currentEntity->e.frame * frameSize );
		coldFrame = (md4CompFrame_t *)((byte *)header - header->ofsFrames + backEnd.currentEntity->e.oldframe * frameSize );
	}
	else
	{
		compressed = qfalse;
		frameSize = (int)( &((md4Frame_t *)0)->bones[ header->numBones ] );
		frame = (md4Frame_t *)((byte *)header + header->ofsFrames + 
			backEnd.currentEntity->e.frame * frameSize );
		oldFrame = (md4Frame_t *)((byte *)header + header->ofsFrames + 
			backEnd.currentEntity->e.oldframe * frameSize );
	}



	RB_CheckOverflow( surface->numVerts, surface->numTriangles );

	triangles = (int *) ((byte *)surface + surface->ofsTriangles);
	indexes = surface->numTriangles * 3;
	baseIndex = tess.numIndexes;
	baseVertex = tess.numVertexes;
	for (j = 0 ; j < indexes ; j++) {
		tess.indexes[baseIndex + j] = baseVertex + triangles[j];
	}
	tess.numIndexes += indexes;

	//
	// lerp all the needed bones
	//
	if ( !backlerp && !compressed)
		// no lerping needed
		bonePtr = frame->bones;
	else 
	{
		bonePtr = bones;
		if (compressed)
		{
			for ( i = 0 ; i < header->numBones ; i++ ) 
			{
				if ( !backlerp )
					MC_UnCompress(bonePtr[i].matrix,cframe->bones[i].Comp);
				else
				{
					MC_UnCompress(tbone[0].matrix,cframe->bones[i].Comp);
					MC_UnCompress(tbone[1].matrix,coldFrame->bones[i].Comp);
					for ( j = 0 ; j < 12 ; j++ ) 
						((float *)&bonePtr[i])[j] = frontlerp * ((float *)&tbone[0])[j]
							+ backlerp * ((float *)&tbone[1])[j];
				}
			}
		}
		else
		{
			for ( i = 0 ; i < header->numBones*12 ; i++ ) 
				((float *)bonePtr)[i] = frontlerp * ((float *)frame->bones)[i]
					+ backlerp * ((float *)oldFrame->bones)[i];
		}
	}

	//
	// deform the vertexes by the lerped bones
	//
	numVerts = surface->numVerts;
	v = (md4Vertex_t *) ((byte *)surface + surface->ofsVerts);
	for ( j = 0; j < numVerts; j++ ) {
		vec3_t	tempVert, tempNormal;
		md4Weight_t	*w;

		VectorClear( tempVert );
		VectorClear( tempNormal );
		w = v->weights;
		for ( k = 0 ; k < v->numWeights ; k++, w++ ) {
			bone = bonePtr + w->boneIndex;

			tempVert[0] += w->boneWeight * ( DotProduct( bone->matrix[0], w->offset ) + bone->matrix[0][3] );
			tempVert[1] += w->boneWeight * ( DotProduct( bone->matrix[1], w->offset ) + bone->matrix[1][3] );
			tempVert[2] += w->boneWeight * ( DotProduct( bone->matrix[2], w->offset ) + bone->matrix[2][3] );

			tempNormal[0] += w->boneWeight * DotProduct( bone->matrix[0], v->normal );
			tempNormal[1] += w->boneWeight * DotProduct( bone->matrix[1], v->normal );
			tempNormal[2] += w->boneWeight * DotProduct( bone->matrix[2], v->normal );
		}

		tess.xyz[baseVertex + j][0] = tempVert[0];
		tess.xyz[baseVertex + j][1] = tempVert[1];
		tess.xyz[baseVertex + j][2] = tempVert[2];

		tess.normal[baseVertex + j][0] = tempNormal[0];
		tess.normal[baseVertex + j][1] = tempNormal[1];
		tess.normal[baseVertex + j][2] = tempNormal[2];

		tess.texCoords[baseVertex + j][0][0] = v->texCoords[0];
		tess.texCoords[baseVertex + j][0][1] = v->texCoords[1];

		v = (md4Vertex_t *)&v->weights[v->numWeights];
	}

	tess.numVertexes += surface->numVerts;
}
