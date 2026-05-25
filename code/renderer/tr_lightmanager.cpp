/*
** tr_lightmanager.cpp
*/

#ifdef VV_LIGHTING

#include "../server/exe_headers.h"
#include "tr_local.h"

#include "tr_lightmanager.h"

#include "../win32/glw_win_dx8.h"
#include "../win32/win_lighteffects.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
#endif


VVLightManager VVLightMan;

#ifdef _XBOX
static int s_xboxVVEntityLightLogCount = 0;
static int s_xboxVVDiffuseLogCount = 0;
static int s_xboxVVDiffuseEntityLogCount = 0;
static int s_xboxVVAddDlightLogCount = 0;
static int s_xboxVVWorldDlightLogCount = 0;
#endif


VVLightManager::VVLightManager()
{

}


void VVLightManager::RE_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	VVdlight_t	*dl;

	if ( !tr.registered ) {
		return;
	}
	if ( num_dlights >= MAX_DLIGHTS ) {
		return;
	}
	if ( intensity <= 0 ) {
		return;
	}
	
	dl = &dlights[num_dlights++];

	VectorCopy (org, dl->origin);
	dl->type = LT_POINT;
	dl->radius = intensity;// * 5.0f;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;

#ifdef _XBOX
	if ( s_xboxVVAddDlightLogCount < 64 ) {
		XBLF("JA: VV_ADD_DLIGHT #%d count=%d origin=%g,%g,%g radius=%g color=%g,%g,%g",
			s_xboxVVAddDlightLogCount,
			num_dlights,
			org[0], org[1], org[2],
			intensity,
			r, g, b);
		s_xboxVVAddDlightLogCount++;
	}
#endif
}

void VVLightManager::RE_AddLightToScene( VVdlight_t *light )
{
	VVdlight_t *dl;

	if ( !tr.registered ) {
		return;
	}

	if( num_dlights >= MAX_DLIGHTS ) {
		return;
	}

	dl = &dlights[num_dlights++];

	VectorCopy(light->origin, dl->origin);
	VectorCopy(light->direction, dl->direction);
	VectorCopy(light->color, dl->color);
	dl->attenuation = light->attenuation;
	dl->type = light->type;
	dl->radius = light->radius;

#ifdef _XBOX
	if ( s_xboxVVAddDlightLogCount < 64 ) {
		XBLF("JA: VV_ADD_DLIGHT_STRUCT #%d count=%d type=%d origin=%g,%g,%g radius=%g color=%g,%g,%g attenuation=%g",
			s_xboxVVAddDlightLogCount,
			num_dlights,
			(int)light->type,
			light->origin[0], light->origin[1], light->origin[2],
			light->radius,
			light->color[0], light->color[1], light->color[2],
			light->attenuation);
		s_xboxVVAddDlightLogCount++;
	}
#endif
}


//void VVLightManager::RE_AddStaticLightToScene( VVslight_t *light )
//{
//	VVslight_t *sl;
//
//	if( !tr.registered ) {
//		return;
//	}
//
//	if( num_slights >= MAX_NUM_STATIC_LIGHTS ) {
//		return;
//	}
//
//	sl = &slights[num_slights++];
//
//	VectorCopy(light->origin, sl->origin);
//	VectorCopy(light->color, sl->color);
//	sl->radius = light->radius;// * 2.0f;
//}



void VVLightManager::R_TransformDlights( orientationr_t *orient) {
	int		i;
	vec3_t	temp;
	VVdlight_t *dl;

	for ( i = 0 ; i < num_dlights ; i++ ) {
		dl = &dlights[i];
		VectorSubtract( dl->origin, orient->origin, temp );
		dl->transformed[0] = DotProduct( temp, orient->axis[0] );
		dl->transformed[1] = DotProduct( temp, orient->axis[1] );
		dl->transformed[2] = DotProduct( temp, orient->axis[2] );
	}
}

	

/*
=============
R_DlightBmodel

Determine which dynamic lights may effect this bmodel
=============
*/
void VVLightManager::R_DlightBmodel( bmodel_t *bmodel, qboolean NoLight ) {
	int			i, j;
	VVdlight_t	*dl;
	int			mask;
	msurface_t	*surf;

	mask = 0;

	// transform all the lights
	R_TransformDlights( &tr.or );

	if (!NoLight)
	{
		for ( i=0 ; i<num_dlights ; i++ ) { 
			dl = &dlights[i];

			// see if the point is close enough to the bounds to matter
			for ( j = 0 ; j < 3 ; j++ ) {
				if ( dl->transformed[j] - bmodel->bounds[1][j] > dl->radius ) {
					break;
				}
				if ( bmodel->bounds[0][j] - dl->transformed[j] > dl->radius ) {
					break;
				}
			}
			// Directional lights are always considered (MATT - change that?)
			if ( j < 3 && dl->type != LT_DIRECTIONAL ) {
				continue;
			} 

			// we need to check this light
			mask |= 1 << i;  
		}
	}

	tr.currentEntity->needDlights = (mask != 0);

	// set the dlight bits in all the surfaces
	for ( i = 0 ; i < bmodel->numSurfaces ; i++ ) {
		surf = bmodel->firstSurface + i;

		if ( *surf->data == SF_FACE ) {
			((srfSurfaceFace_t *)surf->data)->dlightBits = mask;
		} else if ( *surf->data == SF_GRID ) {
			((srfGridMesh_t *)surf->data)->dlightBits = mask;
		} else if ( *surf->data == SF_TRIANGLES ) {
			((srfTriangles_t *)surf->data)->dlightBits = mask;
		}
	}
}


int VVLightManager::R_DlightFace( srfSurfaceFace_t *face, int dlightBits ) {
	float		d;
	int			i;
	VVdlight_t	*dl;

	for ( i = 0 ; i < num_dlights ; i++ ) {

		/*if ( ! ( dlightBits & ( 1 << i ) ) ) {
			continue;
		}*/
		dlightBits |= (1 << i);

		dl = &dlights[i];
		d = DotProduct( dl->origin, face->plane.normal ) - face->plane.dist;
		// Directional lights are always considered (MATT - change that?)
		if ( d < -dl->radius || d > dl->radius ) {
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


//void VVLightManager::R_SlightFace( srfSurfaceFace_t *face ) {
//	float		d;
//	int			i, count = 0;
//	VVslight_t	*sl;
//
//	for ( i = 0; i < num_slights; i++ ) {
//
//		if(count > MAX_STATIC_LIGHTS_SURFACE - 1)
//			break;
//
//		sl = &slights[i];
//		d = DotProduct( sl->origin, face->plane.normal ) - face->plane.dist;
//
//		if ( d > -sl->radius && d < sl->radius ) {
//			face->slightBits[count++] = i;
//		}
//	}
//}


int VVLightManager::R_DlightGrid( srfGridMesh_t *grid, int dlightBits ) {
	int			i;
	VVdlight_t	*dl;

	for ( i = 0 ; i < num_dlights ; i++ ) {
		if ( ! ( dlightBits & ( 1 << i ) ) ) {
			continue;
		}
		dl = &dlights[i];
		// Directional lights are always considered (MATT - change that?)
		if (( dl->origin[0] - dl->radius > grid->meshBounds[1][0]
			|| dl->origin[0] + dl->radius < grid->meshBounds[0][0]
			|| dl->origin[1] - dl->radius > grid->meshBounds[1][1]
			|| dl->origin[1] + dl->radius < grid->meshBounds[0][1]
			|| dl->origin[2] - dl->radius > grid->meshBounds[1][2]
			|| dl->origin[2] + dl->radius < grid->meshBounds[0][2] ) 
			&& dl->type != LT_DIRECTIONAL ) {
			// dlight doesn't reach the bounds
			dlightBits &= ~( 1 << i );
		}
		dlightBits |= (1 << i );
	}

	if ( !dlightBits ) {
		tr.pc.c_dlightSurfacesCulled++;
	}

	grid->dlightBits = dlightBits;
	return dlightBits;
}


//void VVLightManager::R_SlightGrid( srfGridMesh_t *grid ) {
//	int			i, count = 0;
//	VVslight_t	*sl;
//
//	for ( i = 0 ; i < num_slights ; i++ ) {
//
//		if(count > MAX_STATIC_LIGHTS_SURFACE - 1)
//			break;
//		
//		sl = &slights[i];
//		
//		if ( sl->origin[0] - sl->radius > grid->meshBounds[1][0]
//			|| sl->origin[0] + sl->radius < grid->meshBounds[0][0]
//			|| sl->origin[1] - sl->radius > grid->meshBounds[1][1]
//			|| sl->origin[1] + sl->radius < grid->meshBounds[0][1]
//			|| sl->origin[2] - sl->radius > grid->meshBounds[1][2]
//			|| sl->origin[2] + sl->radius < grid->meshBounds[0][2] ) {
//			// slight doesn't reach the bounds
//		}
//		else
//		{
//			grid->slightBits[count++]= i;
//		}
//	}
//}


int VVLightManager::R_DlightTrisurf( srfTriangles_t *surf, int dlightBits ) {
	// FIXME: more dlight culling to trisurfs...
	surf->dlightBits = dlightBits;
	return dlightBits;
}


//void VVLightManager::R_SlightTrisurf( srfTriangles_t *surf ) {
//	/*int i;
//
//	for( i = 0; i < num_slights; i++ )
//	{
//        slightBits[i] = 1;
//	}*/
//}

/*
====================
R_DlightSurface

The given surface is going to be drawn, and it touches a leaf
that is touched by one or more dlights, so try to throw out
more dlights if possible.
====================
*/
int VVLightManager::R_DlightSurface( msurface_t *surf, int dlightBits ) {
	if ( *surf->data == SF_FACE ) {
		dlightBits = VVLightManager::R_DlightFace( (srfSurfaceFace_t *)surf->data, dlightBits );
	} else if ( *surf->data == SF_GRID ) {
		dlightBits = VVLightManager::R_DlightGrid( (srfGridMesh_t *)surf->data, dlightBits );
	} else if ( *surf->data == SF_TRIANGLES ) {
		dlightBits = VVLightManager::R_DlightTrisurf( (srfTriangles_t *)surf->data, dlightBits );
	} else {
		dlightBits = 0;
	}

	if ( dlightBits ) {
		tr.pc.c_dlightSurfaces++;
	}

	return dlightBits;
}


/*
=================
R_SetupEntityLighting

Calculates all the lighting values that will be used
by the Calc_* functions
=================
*/

#define	DLIGHT_AT_RADIUS		16
#define	DLIGHT_MINIMUM_RADIUS	16

void VVLightManager::R_SetupEntityLighting( const trRefdef_t *refdef, trRefEntity_t *ent ) {
	int				i;
	VVdlight_t		*dl;
	vec3_t			dir;
	float			d;
	vec3_t			lightDir;
	vec3_t			shadowLightDir;
	vec3_t			lightOrigin;
	float			power;

	// lighting calculations 
	if ( ent->lightingCalculated ) {
		return;
	}
	ent->lightingCalculated = qtrue;

	ent->dlightBits = 0;

	//
	// trace a sample point down to find ambient light
	//
	if ( ent->e.renderfx & RF_LIGHTING_ORIGIN ) {
		// seperate lightOrigins are needed so an object that is
		// sinking into the ground can still be lit, and so
		// multi-part models can be lit identically
		VectorCopy( ent->e.lightingOrigin, lightOrigin );
	} else {
		VectorCopy( ent->e.origin, lightOrigin );
	}

	// if NOWORLDMODEL, only use dynamic lights (menu system, etc)
	if ( !(refdef->rdflags & RDF_NOWORLDMODEL ) 
		&& tr.world->lightGridData ) {
		R_SetupEntityLightingGrid( ent );
	} else {
		ent->ambientLight[0] = ent->ambientLight[1] = 
			ent->ambientLight[2] = tr.identityLight * 150;
		ent->directedLight[0] = ent->directedLight[1] = 
			ent->directedLight[2] = tr.identityLight * 150;
		// BTO - Fix for UI model rendering. tr.sunDirection is invalid
		// pick an arbitrary light direction
//		VectorCopy( tr.sunDirection, ent->lightDir );
		ent->lightDir[0] = ent->lightDir[1] = 0.0f;
		ent->lightDir[2] = 1.0f;
	}

	// bonus items and view weapons have a fixed minimum add
	if (  ent->e.renderfx & RF_MORELIGHT  ) {
		ent->ambientLight[0] += tr.identityLight * 96;
		ent->ambientLight[1] += tr.identityLight * 96;
		ent->ambientLight[2] += tr.identityLight * 96;
	}
	else {
		// give everything a minimum light add
		ent->ambientLight[0] += tr.identityLight * 32;
		ent->ambientLight[1] += tr.identityLight * 32;
		ent->ambientLight[2] += tr.identityLight * 32;
	}

	//
	// modify the light by dynamic lights
	//
	d = VectorLength( ent->directedLight );
	VectorScale( ent->lightDir, d, lightDir );
	VectorScale( ent->lightDir, d, shadowLightDir );

	for ( i = 0 ; i < num_dlights ; i++ ) {
		dl = &dlights[i];
		VectorSubtract( dl->origin, lightOrigin, dir );
		d = VectorNormalize( dir );

		if( d <= dl->radius )
		{
			ent->dlightBits |= (1 << i);
			ent->needDlights = qtrue;
		}

		power = DLIGHT_AT_RADIUS * ( dl->radius * dl->radius );
		if ( d < DLIGHT_MINIMUM_RADIUS ) {
			d = DLIGHT_MINIMUM_RADIUS;
		}
		d = power / ( d * d );

		VectorMA( shadowLightDir, d, dir, shadowLightDir );
	}

	// clamp
	for ( i = 0 ; i < 3 ; i++ ) {
		if ( ent->ambientLight[i] > tr.identityLightByte ) {
			ent->ambientLight[i] = tr.identityLightByte;
		}
		if ( ent->directedLight[i] > tr.identityLightByte ) {
			ent->directedLight[i] = tr.identityLightByte;
		}
	}

	// save out the byte packet version
	((byte *)&ent->ambientLightInt)[0] = myftol( ent->ambientLight[0] );
	((byte *)&ent->ambientLightInt)[1] = myftol( ent->ambientLight[1] );
	((byte *)&ent->ambientLightInt)[2] = myftol( ent->ambientLight[2] );
	((byte *)&ent->ambientLightInt)[3] = 0xff;
	
	// transform the direction to local space
	VectorNormalize( lightDir );
	VectorNormalize( shadowLightDir );

	ent->shadowDir[0] = DotProduct( shadowLightDir, ent->e.axis[0] );
	ent->shadowDir[1] = DotProduct( shadowLightDir, ent->e.axis[1] );
	ent->shadowDir[2] = DotProduct( shadowLightDir, ent->e.axis[2] );

	ent->lightDir[0] = DotProduct( lightDir, ent->e.axis[0] );
	ent->lightDir[1] = DotProduct( lightDir, ent->e.axis[1] );
	ent->lightDir[2] = DotProduct( lightDir, ent->e.axis[2] );

#ifdef _XBOX
	if ( s_xboxVVEntityLightLogCount < 8 ) {
		XBLF("JA: VV_ENTITY_LIGHT #%d ent=%p hModel=%d reType=%d renderfx=0x%x noworld=%d hasGrid=%d dlights=%d origin=%g,%g,%g amb=%g,%g,%g dir=%g,%g,%g ambInt=0x%08x localDir=%g,%g,%g shadowDir=%g,%g,%g",
			s_xboxVVEntityLightLogCount,
			ent,
			ent->e.hModel,
			ent->e.reType,
			ent->e.renderfx,
			(refdef->rdflags & RDF_NOWORLDMODEL) ? 1 : 0,
			(tr.world && tr.world->lightGridData) ? 1 : 0,
			num_dlights,
			ent->e.origin[0], ent->e.origin[1], ent->e.origin[2],
			ent->ambientLight[0], ent->ambientLight[1], ent->ambientLight[2],
			ent->directedLight[0], ent->directedLight[1], ent->directedLight[2],
			ent->ambientLightInt,
			ent->lightDir[0], ent->lightDir[1], ent->lightDir[2],
			ent->shadowDir[0], ent->shadowDir[1], ent->shadowDir[2]);
		s_xboxVVEntityLightLogCount++;
	}
#endif
}

inline void Short2Float(float *f, const short *s)
{
	*f = ((float)*s);
}

void VVLightManager::ShortToVec3(const short in[3], vec3_t &out)
{
	Short2Float(&out[0], &in[0]);
	Short2Float(&out[1], &in[1]);
	Short2Float(&out[2], &in[2]);
}

int VVLightManager::BoxOnPlaneSide (const short emins[3], const short emaxs[3], struct cplane_s *p)
{
	vec3_t mins;
	vec3_t maxs;
	ShortToVec3(emins, mins);
	ShortToVec3(emaxs, maxs);
	return ::BoxOnPlaneSide(mins, maxs, p);
}


void VVLightManager::R_RecursiveWorldNode( mnode_t *node, int planeBits, int dlightBits ) {

	do {
		int			newDlights[2];

		// if the node wasn't marked as potentially visible, exit
		if (node->visframe != tr.visCount) {
			return;
		}

		// if the bounding volume is outside the frustum, nothing
		// inside can be visible OPTIMIZE: don't do this all the way to leafs?

		if ( !r_nocull->integer ) {
#ifdef _XBOX
			static int s_xboxNodeCullBypassLogBudget = 12;
			if (s_xboxNodeCullBypassLogBudget > 0)
			{
				XBLF("JA: XBOX_NODE_CULL_BYPASS visCount=%d planeBits=0x%x nodeMins=(%d,%d,%d) nodeMaxs=(%d,%d,%d)",
					tr.visCount,
					planeBits,
					node->mins[0], node->mins[1], node->mins[2],
					node->maxs[0], node->maxs[1], node->maxs[2]);
				--s_xboxNodeCullBypassLogBudget;
			}
#else
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

#endif
		}

		if ( node->contents != -1 ) {
			break;
		}

		// node is just a decision point, so go down both sides
		// since we don't care about sort orders, just go positive to negative

		// determine which dlights are needed
		newDlights[0] = 0;
		newDlights[1] = 0;
		if ( dlightBits ) {
			int	i;

			for ( i = 0 ; i < num_dlights ; i++ ) {
				VVdlight_t	*dl;
				float		dist;

				if ( dlightBits & ( 1 << i ) ) {
					dl = &dlights[i];
					dist = DotProduct( dl->origin, 
						tr.world->planes[node->planeNum].normal ) - 
						tr.world->planes[node->planeNum].dist;
					
					if ( dist > -dl->radius ) {
						newDlights[0] |= ( 1 << i );
					}
					if ( dist < dl->radius ) {
						newDlights[1] |= ( 1 << i );
					}
				}
			}
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
		mleaf_s *leaf;

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
		leaf = (mleaf_s*)node;
		mark = tr.world->marksurfaces + leaf->firstMarkSurfNum;
		c = leaf->nummarksurfaces;
		while (c--) {
			// the surface may have already been added if it
			// spans multiple leafs
			surf = *mark;
#ifdef _XBOX
			// MATT! - this is a temp hack until bspthing starts parsing flares
			if(surf->data)
#endif
			R_AddWorldSurface( surf, dlightBits );
			mark++;
		}
	}

}

void VVLightManager::RB_CalcDiffuseColorWorld()
{
	trRefEntity_t	*ent;
	VVdlight_t		*dl;

	if(!num_dlights)
		return;

	ent = backEnd.currentEntity;

#ifdef _XBOX
	if ( s_xboxVVWorldDlightLogCount < 32 ) {
		const char *shaderName = (tess.shader && tess.shader->name) ? tess.shader->name : "(null)";
		XBLF("JA: VV_WORLD_DLIGHT #%d shader='%s' bits=0x%x dlights=%d verts=%d indexes=%d",
			s_xboxVVWorldDlightLogCount,
			shaderName,
			tess.dlightBits,
			num_dlights,
			tess.numVertexes,
			tess.numIndexes);
		s_xboxVVWorldDlightLogCount++;
	}
#endif

	for(int i = 0, l = 0; i < num_dlights; i++) 
	{
		if ( ( tess.dlightBits & ( 1 << i ) ) ) 
		{
			glEnable(GL_LIGHTING);
			dl = &dlights[i];

			vec3_t newColor;
			newColor[0] = dl->color[0] * 255.0f;
			newColor[1] = dl->color[1] * 255.0f;
			newColor[2] = dl->color[2] * 255.0f;

			glLightfv(l, GL_DIFFUSE, newColor);

/*			vec3_t ambient;
			ambient[0] = ambient[1] = ambient[2] = 128;

			glLightfv(l, GL_AMBIENT, ambient);//ent->ambientLight);*/
			
			if(dl->type == LT_POINT) {
				glLightfv(l, GL_SPOT_CUTOFF, &dl->radius);
				glLightfv(l, GL_POSITION, dl->origin);
			} else if(dl->type == LT_DIRECTIONAL) {
				glLightfv(l, GL_SPOT_DIRECTION, dl->direction);
			}

			l++;
		}
	}

	float color[4];
	color[0] = 255;
	color[1] = 255;
	color[2] = 255;
	color[3] = 255;
	glMaterialfv( GL_FRONT, GL_AMBIENT, color );
}


void VVLightManager::RB_CalcDiffuseColor( DWORD *colors )
{
	int				i, j;
	float			*normal;
	float			incoming;
	trRefEntity_t	*ent;
	vec3_t			ambientLight;
	vec3_t			directedLight;
	vec3_t			lightDir;
	int				numVertexes;

	ent = backEnd.currentEntity;
	VectorCopy( ent->ambientLight, ambientLight );
	VectorCopy( ent->directedLight, directedLight );
	VectorCopy( ent->lightDir, lightDir );

	normal = tess.normal[0];
	numVertexes = tess.numVertexes;

	for ( i = 0 ; i < numVertexes ; i++, normal += 4 ) {
		incoming = DotProduct( normal, lightDir );
		if ( incoming <= 0 ) {
			colors[i] = D3DCOLOR_RGBA(
				((byte *)&ent->ambientLightInt)[0],
				((byte *)&ent->ambientLightInt)[1],
				((byte *)&ent->ambientLightInt)[2],
				255);
			continue;
		}

		j = myftol( ambientLight[0] + incoming * directedLight[0] );
		if ( j > 255 ) {
			j = 255;
		}
		int r = j;

		j = myftol( ambientLight[1] + incoming * directedLight[1] );
		if ( j > 255 ) {
			j = 255;
		}
		int g = j;

		j = myftol( ambientLight[2] + incoming * directedLight[2] );
		if ( j > 255 ) {
			j = 255;
		}
		int b = j;

		colors[i] = D3DCOLOR_RGBA( r, g, b, 255 );
	}

#ifdef _XBOX
	if ( s_xboxVVDiffuseLogCount < 64 && numVertexes > 0 ) {
		float firstIncoming = DotProduct( tess.normal[0], lightDir );
		const char *shaderName = (tess.shader && tess.shader->name) ? tess.shader->name : "(null)";
		XBLF("JA: VV_DIFFUSE_COLOR #%d shader='%s' verts=%d ent=%p hModel=%d amb=%g,%g,%g dir=%g,%g,%g lightDir=%g,%g,%g n0=%g,%g,%g incoming0=%g out0=0x%08x",
			s_xboxVVDiffuseLogCount,
			shaderName,
			numVertexes,
			ent,
			ent ? ent->e.hModel : 0,
			ambientLight[0], ambientLight[1], ambientLight[2],
			directedLight[0], directedLight[1], directedLight[2],
			lightDir[0], lightDir[1], lightDir[2],
			tess.normal[0][0], tess.normal[0][1], tess.normal[0][2],
			firstIncoming,
			colors[0]);
		s_xboxVVDiffuseLogCount++;
	}
#endif
}


void VVLightManager::RB_CalcDiffuseEntityColor( DWORD *colors )
{
	if ( !backEnd.currentEntity )
	{//error, use the normal lighting
		RB_CalcDiffuseColor(colors);
		return;
	}

	int				i;
	float			*normal;
	float			incoming;
	int				numVertexes;
	float			r, g, b;
	trRefEntity_t	*ent;
	vec3_t			ambientLight;
	vec3_t			directedLight;
	vec3_t			lightDir;

	ent = backEnd.currentEntity;
	VectorCopy( ent->ambientLight, ambientLight );
	VectorCopy( ent->directedLight, directedLight );
	VectorCopy( ent->lightDir, lightDir );

	r = ent->e.shaderRGBA[0] / 255.0f;
	g = ent->e.shaderRGBA[1] / 255.0f;
	b = ent->e.shaderRGBA[2] / 255.0f;

	normal = tess.normal[0];
	numVertexes = tess.numVertexes;

	for ( i = 0 ; i < numVertexes ; i++, normal += 4 ) {
		incoming = DotProduct( normal, lightDir );
		float outR = ambientLight[0];
		float outG = ambientLight[1];
		float outB = ambientLight[2];

		if ( incoming > 0 ) {
			outR += incoming * directedLight[0];
			outG += incoming * directedLight[1];
			outB += incoming * directedLight[2];
		}

		if ( outR > 255 ) {
			outR = 255;
		}
		if ( outG > 255 ) {
			outG = 255;
		}
		if ( outB > 255 ) {
			outB = 255;
		}

		colors[i] = D3DCOLOR_RGBA(
			myftol( outR * r ),
			myftol( outG * g ),
			myftol( outB * b ),
			ent->e.shaderRGBA[3] );
	}

#ifdef _XBOX
	if ( s_xboxVVDiffuseEntityLogCount < 16 && numVertexes > 0 ) {
		float firstIncoming = DotProduct( tess.normal[0], lightDir );
		const char *shaderName = (tess.shader && tess.shader->name) ? tess.shader->name : "(null)";
		XBLF("JA: VV_DIFFUSE_ENTITY_COLOR #%d shader='%s' verts=%d ent=%p hModel=%d rgba=%d,%d,%d,%d amb=%g,%g,%g dir=%g,%g,%g lightDir=%g,%g,%g n0=%g,%g,%g incoming0=%g out0=0x%08x",
			s_xboxVVDiffuseEntityLogCount,
			shaderName,
			numVertexes,
			ent,
			ent ? ent->e.hModel : 0,
			ent->e.shaderRGBA[0], ent->e.shaderRGBA[1], ent->e.shaderRGBA[2], ent->e.shaderRGBA[3],
			ambientLight[0], ambientLight[1], ambientLight[2],
			directedLight[0], directedLight[1], directedLight[2],
			lightDir[0], lightDir[1], lightDir[2],
			tess.normal[0][0], tess.normal[0][1], tess.normal[0][2],
			firstIncoming,
			colors[0]);
		s_xboxVVDiffuseEntityLogCount++;
	}
#endif
}


//void R_LoadLevelLightdef(const char *filename)
//{
//	const char *text;
//	const char *curText;
//	char *token;
//	VVslight_t light;
//
//	VVLightMan.num_slights = 0;
//
//	if ( ri.FS_ReadFile( filename, (void**)&curText ) <= 0 )
//	{
//		ri.Printf( PRINT_WARNING, "WARNING: no lightdef file found\n" );
//		return;
//	}
//
//	text = curText;
//
//	while(1)
//	{
//		token = COM_ParseExt( &text, qtrue );
//		if(!token[0])
//			break;
//
//		// Skip to the light's origin
//		while(strcmp(token, "origin"))
//		{
//			token = COM_ParseExt( &text, qtrue );
//			if(!token[0])
//				break;
//		}
//
//		// Write the origin
//		// X
//		token = COM_ParseExt( &text, qtrue );
//		if(!token[0])
//			break;
//		light.origin[0] = atof(token);
//
//		// Y
//		token = COM_ParseExt( &text, qtrue );
//		if(!token[0])
//			break;
//		light.origin[1] = atof(token);
//
//		// Z
//		token = COM_ParseExt( &text, qtrue );
//		if(!token[0])
//			break;
//		light.origin[2] = atof(token);
//
//		// Skip to the light's range
//		while(strcmp(token, "light"))
//		{
//			token = COM_ParseExt( &text, qtrue );
//			if(!token[0])
//				break;
//		}
//
//		// Write the light range
//		token = COM_ParseExt( &text, qtrue );
//		if(!token[0])
//			break;
//		light.radius = atof(token);
//
//		// Default color for now
//		light.color[0] = 1.0f;
//		light.color[1] = 1.0f;
//		light.color[2] = 1.0f;
//
//		VVLightMan.RE_AddStaticLightToScene(&light);
//	}
//	
//	ri.FS_FreeFile( (void*)curText );
//}

#define MAX_LIGHT_TABLE		55

static levelLightParm_t _levelLightParms[MAX_LIGHT_TABLE];
static bool isLightInit = false;

static void ClearLightParmTable(void)
{
	memset(_levelLightParms, 0, sizeof(levelLightParm_t) * MAX_LIGHT_TABLE);
	isLightInit = false;
}

/*
**
** R_GetLightParmsForLevel
**
*/
void R_GetLightParmsForLevel()
{
	if(!isLightInit)
		return;

	char levelname[64];

	COM_StripExtension(tr.world->baseName, levelname);

	for(int i = 0; i < MAX_LIGHT_TABLE; i++)
	{
		if(Q_stricmp(COM_SkipPath(levelname), _levelLightParms[i].levelName) == 0)
		{
			if(VectorLength(_levelLightParms[i].sundir))
			{
				Cvar_SetValue("r_sundir_x", _levelLightParms[i].sundir[0]);
				Cvar_SetValue("r_sundir_y", _levelLightParms[i].sundir[1]);
				Cvar_SetValue("r_sundir_z", _levelLightParms[i].sundir[2]);
			}
			
			if(_levelLightParms[i].hdrEnable)
                Cvar_Set("r_hdreffect", "1");
			else
				Cvar_Set("r_hdreffect", "0");
			Cvar_SetValue("r_hdrbloom", _levelLightParms[i].hdrBloom);
			Cvar_SetValue("r_hdrcutoff", _levelLightParms[i].hdrCutoff);
		}
	}
}

/*
**
** R_LoadLevelFogTable
**
*/
void R_LoadLevelLightParms()
{
	const char *lightText;
	const char *curText;
	char *token;
	int level = 0;

	if ( FS_ReadFile( "shaders/lightparms.txt", (void**)&curText ) <= 0 )
	{
		Com_Printf( "WARNING: no light parms file found\n" );
		return;
	}

	ClearLightParmTable();

	lightText = curText;

	while(1)
	{
		// Level name
		token = COM_ParseExt( &lightText, qtrue );
		if(!token[0])
			break;
		strcpy( _levelLightParms[level].levelName, token );

		// Sun dir X
		token = COM_ParseExt( &lightText, qtrue );
		if(!token[0])
			break;
		_levelLightParms[level].sundir[0] = atof(token);

		// Sun dir Y
		token = COM_ParseExt( &lightText, qtrue );
		if(!token[0])
			break;
		_levelLightParms[level].sundir[1] = atof(token);

		// Sun dir Z
		token = COM_ParseExt( &lightText, qtrue );
		if(!token[0])
			break;
		_levelLightParms[level].sundir[2] = atof(token);

		// HDR enable
		token = COM_ParseExt( &lightText, qtrue );
		if(!token[0])
			break;
		_levelLightParms[level].hdrEnable = atof(token);

		// HDR bloom
		token = COM_ParseExt( &lightText, qtrue );
		if(!token[0])
			break;
		_levelLightParms[level].hdrBloom = atof(token);

		// HDR cutoff
		token = COM_ParseExt( &lightText, qtrue );
		if(!token[0])
			break;
		_levelLightParms[level].hdrCutoff = atof(token);

		level++;
		if(level >= MAX_LIGHT_TABLE)
			break;
	}

	isLightInit = true;

	FS_FreeFile( (void*)curText );
}

#endif //VV_LIGHTING
