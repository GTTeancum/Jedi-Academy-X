#ifndef STEFX_RETAIL_RENDERER_CONTRACT_H
#define STEFX_RETAIL_RENDERER_CONTRACT_H

#if defined(STEFX_RETAIL_RENDERER_ACTIVE)
#define STEFX_RETAIL_NAMESPACE_BEGIN
#define STEFX_RETAIL_NAMESPACE_END
#define STEFX_RETAIL_SCOPE ::
#else
#define STEFX_RETAIL_NAMESPACE_BEGIN namespace stefx_retail_renderer {
#define STEFX_RETAIL_NAMESPACE_END }
#define STEFX_RETAIL_SCOPE stefx_retail_renderer::
#endif

STEFX_RETAIL_NAMESPACE_BEGIN

extern trGlobals_t tr;
extern backEndData_t *backEndData;
extern backEndState_t backEnd;
extern shaderCommands_t tess;
extern color4ub_t styleColors[MAX_LIGHT_STYLES];
extern bool g_bRenderGlowingObjects;
extern bool g_bDynamicGlowSupported;
extern int skyboxportal;
extern int drawskyboxportal;

void GL_Bind( image_t *image );
void GL_SelectTexture( int unit );
void GL_Cull( int cullType );
void GL_TexEnv( int env );
void GL_State( unsigned long stateBits );

void RB_CheckOverflow( int verts, int indexes );
void RB_AddQuadStampExt( vec3_t origin, vec3_t left, vec3_t up, byte *color,
		float s1, float t1, float s2, float t2 );
void RB_AddQuadStamp( vec3_t origin, vec3_t left, vec3_t up, byte *color );
void RB_SurfacePolychain( srfPoly_t *p );
void RB_SurfaceTriangles( srfTriangles_t *srf );
void RB_SurfaceMesh( md3Surface_t *surface );
void RB_SurfaceFace( srfSurfaceFace_t *surf );
void RB_SurfaceGrid( srfGridMesh_t *cv );
void RB_SurfaceEntity( surfaceType_t *surfType );
void RB_SurfaceBad( surfaceType_t *surfType );
void RB_SurfaceFlare( srfFlare_t *surf );
void RB_SurfaceDisplayList( void *surf );
void RB_SurfaceSkip( void *surf );
extern void (*rb_surfaceTable[SF_NUM_SURFACE_TYPES])( void * );
#ifdef STEFX_ELITE_FORCE_SP
qboolean STEFX_EF_SurfaceEntity( void );
#endif

void RB_CalcStretchTexCoords( const waveForm_t *wf, float *st );
void RB_CalcDeformVertexes( deformStage_t *ds );
void RB_CalcDeformNormals( deformStage_t *ds );
void RB_CalcBulgeVertexes( deformStage_t *ds );
void RB_CalcMoveVertexes( deformStage_t *ds );
void DeformText( const char *text );
void RB_DeformTessGeometry( void );
void RB_CalcColorFromEntity( DWORD *dstColors );
void RB_CalcColorFromEntity( unsigned char *dstColors );
void RB_CalcColorFromOneMinusEntity( DWORD *dstColors );
void RB_CalcColorFromOneMinusEntity( unsigned char *dstColors );
void RB_CalcAlphaFromEntity( DWORD *dstColors );
void RB_CalcAlphaFromEntity( unsigned char *dstColors );
void RB_CalcAlphaFromOneMinusEntity( DWORD *dstColors );
void RB_CalcAlphaFromOneMinusEntity( unsigned char *dstColors );
void RB_CalcWaveColor( const waveForm_t *wf, DWORD *dstColors );
void RB_CalcWaveColor( const waveForm_t *wf, unsigned char *dstColors );
void RB_CalcWaveAlpha( const waveForm_t *wf, DWORD *dstColors );
void RB_CalcWaveAlpha( const waveForm_t *wf, unsigned char *dstColors );
void RB_CalcModulateColorsByFog( DWORD *colors );
void RB_CalcModulateColorsByFog( unsigned char *colors );
void RB_CalcModulateAlphasByFog( DWORD *colors );
void RB_CalcModulateAlphasByFog( unsigned char *colors );
void RB_CalcModulateRGBAsByFog( DWORD *colors );
void RB_CalcModulateRGBAsByFog( unsigned char *colors );
void RB_CalcFogTexCoords( float *st );
void RB_CalcEnvironmentTexCoords( float *st );
void RB_CalcTurbulentTexCoords( const waveForm_t *wf, float *st );
void RB_CalcScaleTexCoords( const float scale[2], float *st );
void RB_CalcScrollTexCoords( const float scrollSpeed[2], float *st );
void RB_CalcTransformTexCoords( const texModInfo_t *tmi, float *st );
void RB_CalcRotateTexCoords( float degsPerSecond, float *st );
void RB_CalcSpecularAlpha( DWORD *alphas );
void RB_CalcSpecularAlpha( unsigned char *alphas );
void RB_CalcDiffuseColor( DWORD *colors );
void RB_CalcDiffuseColor( unsigned char *colors );
void RB_CalcDiffuseEntityColor( DWORD *colors );
void RB_CalcDiffuseEntityColor( unsigned char *colors );
void RB_CalcDisintegrateColors( DWORD *colors );
void RB_CalcDisintegrateColors( unsigned char *colors );
void RB_CalcDisintegrateVertDeform( void );

void R_BindAnimatedImage( const textureBundle_t *bundle );
void RB_BeginSurface( shader_t *shader, int fogNum );
void ForceAlpha( unsigned char *dstColors, int forceEntityAlpha );
void RB_StageIteratorGeneric( void );
void RB_StageIteratorSky( void );
void RB_EndSurface( void );
void RB_ExecuteRenderCommands( const void *data );

void R_RotateForEntity( const trRefEntity_t *ent, const viewParms_t *viewParms,
		orientationr_t *orientation );
void R_RotateForViewer( void );
void R_SetupFrustum( void );
void R_TransformDlights( int count, dlight_t *dl, orientationr_t *ori );
void R_DecomposeSort( unsigned sort, int *entityNum, shader_t **shader,
		int *fogNum, int *dlighted );
void R_AddDrawSurf( const surfaceType_t *surface, const shader_t *shader,
	int fogIndex, int dlightMap );
void R_AddDrawSurfCmd( drawSurf_t *drawSurfs, int numDrawSurfs );
void R_AddBrushModelSurfaces( trRefEntity_t *ent );
void R_AddPolygonSurfaces( void );
void R_AddWorldSurfaces( void );
void R_DlightBmodel( bmodel_t *bmodel, qboolean noLight );
void R_SetupEntityLighting( const trRefdef_t *refdef, trRefEntity_t *ent );
int R_CullLocalBox( const vec3_t bounds[2] );
int R_CullLocalPointAndRadius( const vec3_t pt, float radius );
int R_CullPointAndRadius( const vec3_t pt, float radius );
void R_LocalPointToWorld( const vec3_t local, vec3_t world );
void R_TransformModelToClip( const vec3_t src, const float *modelMatrix,
		const float *projectionMatrix, vec4_t eye, vec4_t dst );
void R_TransformClipToWindow( const vec4_t clip, const viewParms_t *view,
		vec4_t normalized, vec4_t window );
void R_SyncRenderThread( void );
void R_ToggleSmpFrame( void );
void R_RenderView( viewParms_t *parms );

STEFX_RETAIL_NAMESPACE_END

#endif
