#ifdef VV_LIGHTING

#include "../server/exe_headers.h"

#include "../renderer/tr_local.h"
#include "glw_win_dx8.h"
#include "win_local.h"
#include "../renderer/tr_lightmanager.h"

#include "win_lighteffects.h"

#include <xgraphics.h>
#include <xgmath.h>

#include "shader_constants.h"
#include "xb_log.h"

extern const char *Sys_RemapPath( const char *filename );
#ifdef _XBOX
extern "C" void JAMP_InvalidateD3DStateAfterLightEffects( void );
extern "C" void JAMP_SetRenderStateCachedForLightEffects( DWORD type, DWORD value );
extern "C" void JAMP_SetTextureCachedForLightEffects( int stage, IDirect3DBaseTexture8 *texture );
extern "C" void JAMP_SetTextureStageStateCachedForLightEffects( int stage, DWORD type, DWORD value );
extern "C" void JAMP_SetTransformCachedForLightEffects( DWORD state, const D3DMATRIX *matrix );
extern "C" void JAMP_SetVertexShaderCachedForExternalWrite( DWORD mask );
extern "C" void JAMP_SetPixelShaderCachedForLightEffects( DWORD shader );
extern "C" void JAMP_SetVertexShaderConstantCachedForLightEffects( DWORD reg, const void *data, DWORD count );
extern "C" void JAMP_SetPixelShaderConstantCachedForLightEffects( DWORD reg, const void *data, DWORD count );

#define LE_SetRenderState(state, value) JAMP_SetRenderStateCachedForLightEffects( (state), (DWORD)(value) )
#define LE_SetTexture(stage, texture) JAMP_SetTextureCachedForLightEffects( (stage), (IDirect3DBaseTexture8 *)(texture) )
#define LE_SetTextureStageState(stage, type, value) JAMP_SetTextureStageStateCachedForLightEffects( (stage), (type), (DWORD)(value) )
#define LE_SetTransform(state, matrix) JAMP_SetTransformCachedForLightEffects( (state), (const D3DMATRIX *)(matrix) )
#define LE_SetVertexShader(shader) JAMP_SetVertexShaderCachedForExternalWrite( (DWORD)(shader) )
#define LE_SetPixelShader(shader) JAMP_SetPixelShaderCachedForLightEffects( (DWORD)(shader) )
#define LE_SetVertexShaderConstant(reg, data, count) JAMP_SetVertexShaderConstantCachedForLightEffects( (DWORD)(reg), (data), (DWORD)(count) )
#define LE_SetPixelShaderConstant(reg, data, count) JAMP_SetPixelShaderConstantCachedForLightEffects( (DWORD)(reg), (data), (DWORD)(count) )
#else
#define LE_SetRenderState(state, value) glw_state->device->SetRenderState( (D3DRENDERSTATETYPE)(state), (DWORD)(value) )
#define LE_SetTexture(stage, texture) glw_state->device->SetTexture( (stage), (IDirect3DBaseTexture8 *)(texture) )
#define LE_SetTextureStageState(stage, type, value) glw_state->device->SetTextureStageState( (stage), (D3DTEXTURESTAGESTATETYPE)(type), (DWORD)(value) )
#define LE_SetTransform(state, matrix) glw_state->device->SetTransform( (D3DTRANSFORMSTATETYPE)(state), (const D3DMATRIX *)(matrix) )
#define LE_SetVertexShader(shader) glw_state->device->SetVertexShader( (DWORD)(shader) )
#define LE_SetPixelShader(shader) glw_state->device->SetPixelShader( (DWORD)(shader) )
#define LE_SetVertexShaderConstant(reg, data, count) glw_state->device->SetVertexShaderConstant( (DWORD)(reg), (data), (DWORD)(count) )
#define LE_SetPixelShaderConstant(reg, data, count) glw_state->device->SetPixelShaderConstant( (DWORD)(reg), (data), (DWORD)(count) )
#endif

static bool CreateFlatBumpMap( LPDIRECT3DTEXTURE8 *ppTexture )
{
	if ( !ppTexture )
	{
		return false;
	}

	*ppTexture = NULL;
	if ( !glw_state || !glw_state->device )
	{
		return false;
	}

	if ( FAILED( glw_state->device->CreateTexture( 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, ppTexture ) ) )
	{
		return false;
	}

	DWORD srcBits[16];
	for ( int i = 0; i < 16; ++i )
	{
		srcBits[i] = D3DCOLOR_ARGB( 255, 128, 128, 255 );
	}

	D3DLOCKED_RECT lock;
	if ( FAILED( (*ppTexture)->LockRect( 0, &lock, 0, 0L ) ) )
	{
		(*ppTexture)->Release();
		*ppTexture = NULL;
		return false;
	}

	XGSwizzleRect( srcBits, 0, NULL, lock.pBits, 4, 4, NULL, sizeof( DWORD ) );
	(*ppTexture)->UnlockRect( 0 );
	return true;
}

static LPDIRECT3DTEXTURE8 LightEffects_GetDefaultBumpTexture()
{
	static qboolean s_defaultBumpTried = qfalse;
	static image_t *s_defaultBumpImage = NULL;

	if ( !s_defaultBumpTried && tr.registered )
	{
		s_defaultBumpTried = qtrue;
		s_defaultBumpImage = R_FindImageFile( "media/defaultbump", qtrue, qfalse, qtrue, GL_REPEAT );
		if ( !s_defaultBumpImage )
		{
			XBLog_Write( "JA: LightEffects defaultbump.dds missing; using generated flat bump" );
		}
	}

	if ( s_defaultBumpImage && glw_state )
	{
		glwstate_t::texturexlat_t::iterator it = glw_state->textureXlat.find( s_defaultBumpImage->texnum );
		if ( it != glw_state->textureXlat.end() )
		{
			return it->second.mipmap;
		}
	}

	return NULL;
}

static LPDIRECT3DTEXTURE8 LightEffects_GetSpecularTexture()
{
	static qboolean s_specularTried = qfalse;
	static image_t *s_specularImage = NULL;

	if ( !s_specularTried && tr.registered )
	{
		s_specularTried = qtrue;
		s_specularImage = R_FindImageFile( "media/diffspec", qtrue, qfalse, qtrue, GL_REPEAT );
		if ( !s_specularImage )
		{
			XBLog_Write( "JA: LightEffects diffspec.dds missing; generic specular fallback remains active" );
		}
	}

	if ( s_specularImage && glw_state )
	{
		glwstate_t::texturexlat_t::iterator it = glw_state->textureXlat.find( s_specularImage->texnum );
		if ( it != glw_state->textureXlat.end() )
		{
			return it->second.mipmap;
		}
	}

	return NULL;
}

static void LightEffects_SetSurfaceConstants()
{
	LE_SetTransform( D3DTS_PROJECTION, glw_state->matrixStack[glw_state->MatrixMode_Projection]->GetTop() );

	XGMATRIX matComposite;
	XGMATRIX matProjectionViewport;
	glw_state->device->GetProjectionViewportMatrix( &matProjectionViewport );

	D3DVIEWPORT8 view;
	glw_state->device->GetViewport( &view );
	matProjectionViewport._31 += view.X;
	matProjectionViewport._32 += view.Y;

	XGMatrixMultiply( &matComposite, (XGMATRIX *)glw_state->matrixStack[glwstate_t::MatrixMode_Model]->GetTop(), &matProjectionViewport );
	XGMatrixTranspose( &matComposite, &matComposite );
	LE_SetVertexShaderConstant( CV_WORLDVIEWPROJ_0, &matComposite, 4 );

	float fViewportOffsets[4] = { 0.53125f, 0.53125f, 0.0f, 0.0f };
	LE_SetVertexShaderConstant( CV_VIEWPORT_OFFSETS, &fViewportOffsets, 1 );
	LE_SetVertexShaderConstant( CV_ONE, D3DXVECTOR4( 1.0f, 1.0f, 1.0f, 1.0f ), 1 );
	LE_SetVertexShaderConstant( CV_HALF, D3DXVECTOR4( 0.5f, 0.5f, 0.5f, 0.5f ), 1 );
}

static void LightEffects_SetLightPositionConstant( D3DXVECTOR3 *pPtLightPos )
{
	if ( pPtLightPos )
	{
		LE_SetVertexShaderConstant( CV_LIGHT_POSITION, pPtLightPos, 1 );
	}
}

static int LightEffects_FindBaseTextureStageForDlights( const shader_t *shader )
{
	static const shader_t *s_cachedShader = NULL;
	static int s_cachedStage = -2;

	if ( shader == s_cachedShader )
	{
		return s_cachedStage;
	}

	s_cachedShader = shader;
	s_cachedStage = -1;

	if ( !shader || !qglActiveTextureARB )
	{
		return s_cachedStage;
	}

	for ( int i = 0; i < shader->numUnfoggedPasses; ++i )
	{
		const int blendBits = ( GLS_SRCBLEND_BITS + GLS_DSTBLEND_BITS );
		const shaderStage_t *stage = &shader->stages[i];
		if ( ( ( stage->bundle[0].image && !stage->bundle[0].isLightmap && !stage->bundle[0].numTexMods && stage->bundle[0].tcGen != TCGEN_ENVIRONMENT_MAPPED && stage->bundle[0].tcGen != TCGEN_FOG ) ||
			( stage->bundle[1].image && !stage->bundle[1].isLightmap && !stage->bundle[1].numTexMods && stage->bundle[1].tcGen != TCGEN_ENVIRONMENT_MAPPED && stage->bundle[1].tcGen != TCGEN_FOG ) ) &&
			( stage->stateBits & blendBits ) == 0 )
		{
			s_cachedStage = i;
			break;
		}
	}

	return s_cachedStage;
}

static void LightEffects_BindBaseTextureForDlights()
{
	shaderStage_t *dStage = NULL;
	int dStageIndex = LightEffects_FindBaseTextureStageForDlights( tess.shader );

	if ( dStageIndex >= 0 && tess.xstages )
	{
		dStage = &tess.xstages[dStageIndex];
	}

	GL_SelectTexture( 0 );
	if ( dStage )
	{
		GL_State( 0 );
		if ( dStage->bundle[0].numImageAnimations > 1 )
		{
			int index;
			if ( backEnd.currentEntity->e.renderfx & RF_SETANIMINDEX )
			{
				index = backEnd.currentEntity->e.skinNum;
			}
			else
			{
				index = myftol( backEnd.refdef.floatTime * dStage->bundle[0].imageAnimationSpeed * FUNCTABLE_SIZE );
				index >>= FUNCTABLE_SIZE2;
				if ( index < 0 )
				{
					index = 0;
				}
			}

			if ( dStage->bundle[0].oneShotAnimMap )
			{
				if ( index >= dStage->bundle[0].numImageAnimations )
				{
					index = dStage->bundle[0].numImageAnimations - 1;
				}
			}
			else
			{
				index %= dStage->bundle[0].numImageAnimations;
			}
			GL_Bind( *( (image_t **)dStage->bundle[0].image + index ) );
		}
		else if ( dStage->bundle[0].image && !dStage->bundle[0].isLightmap && !dStage->bundle[0].numTexMods && dStage->bundle[0].tcGen != TCGEN_ENVIRONMENT_MAPPED && dStage->bundle[0].tcGen != TCGEN_FOG )
		{
			GL_Bind( dStage->bundle[0].image );
		}
		else if ( dStage->bundle[1].image )
		{
			GL_Bind( dStage->bundle[1].image );
		}
		else
		{
			GL_Bind( tr.whiteImage );
		}
		GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	}
	else
	{
		GL_Bind( tr.whiteImage );
		GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE );
	}
}

LightEffects::LightEffects()
{
	m_pCubeMap = NULL;
	m_pBumpMap = NULL;
	m_pFalloffMap = NULL;
	m_dwVertexShaderLight = 0L;
	m_dwPixelShaderLight = 0L;
	m_dwVertexShaderSpecular_Dynamic = 0L;
	m_dwPixelShaderSpecular_Dynamic = 0L;
	m_dwVertexShaderSpecular_Static = 0L;
	m_dwPixelShaderSpecular_Static = 0L;
	m_dwVertexShaderEnvironment = 0L;
	m_dwVertexShaderBump = 0L;
	m_dwPixelShaderBump = 0L;
	m_bInLightPhase = false;
	m_bInitialized = false;

	Initialize();
}

LightEffects::~LightEffects()
{
	if ( m_pCubeMap )
	{
		m_pCubeMap->Release();
	}
	m_pCubeMap = NULL;

	if ( m_pBumpMap )
	{
		m_pBumpMap->Release();
	}
	m_pBumpMap = NULL;

	if ( m_pFalloffMap )
	{
		m_pFalloffMap->Release();
	}
	m_pFalloffMap = NULL;

	if ( glw_state && glw_state->device )
	{
		if ( m_dwVertexShaderLight )
		{
			glw_state->device->DeleteVertexShader( m_dwVertexShaderLight );
		}
		if ( m_dwPixelShaderLight )
		{
			glw_state->device->DeletePixelShader( m_dwPixelShaderLight );
		}
		if ( m_dwVertexShaderSpecular_Dynamic )
		{
			glw_state->device->DeleteVertexShader( m_dwVertexShaderSpecular_Dynamic );
		}
		if ( m_dwPixelShaderSpecular_Dynamic )
		{
			glw_state->device->DeletePixelShader( m_dwPixelShaderSpecular_Dynamic );
		}
		if ( m_dwVertexShaderSpecular_Static )
		{
			glw_state->device->DeleteVertexShader( m_dwVertexShaderSpecular_Static );
		}
		if ( m_dwPixelShaderSpecular_Static )
		{
			glw_state->device->DeletePixelShader( m_dwPixelShaderSpecular_Static );
		}
		if ( m_dwVertexShaderEnvironment )
		{
			glw_state->device->DeleteVertexShader( m_dwVertexShaderEnvironment );
		}
		if ( m_dwVertexShaderBump )
		{
			glw_state->device->DeleteVertexShader( m_dwVertexShaderBump );
		}
		if ( m_dwPixelShaderBump )
		{
			glw_state->device->DeletePixelShader( m_dwPixelShaderBump );
		}
	}
}

void LightEffects::StartLightPhase()
{
	m_bInLightPhase = true;
}

void LightEffects::EndLightPhase()
{
	m_bInLightPhase = false;
}

bool LightEffects::Initialize()
{
	if ( m_bInitialized )
	{
		return true;
	}

	if ( !glw_state || !glw_state->device )
	{
		XBLog_Write( "JA: LightEffects init skipped: no D3D device" );
		return false;
	}

	DWORD dwVertexDecl[] =
	{
		D3DVSD_STREAM( 0 ),
		D3DVSD_REG( 0, D3DVSDT_FLOAT3 ),
		D3DVSD_REG( 1, D3DVSDT_FLOAT3 ),
		D3DVSD_REG( 2, D3DVSDT_FLOAT2 ),
		D3DVSD_REG( 3, D3DVSDT_FLOAT3 ),
		D3DVSD_END()
	};

	DWORD dwVertexDeclBump[] =
	{
		D3DVSD_STREAM( 0 ),
		D3DVSD_REG( 0, D3DVSDT_FLOAT3 ),
		D3DVSD_REG( 1, D3DVSDT_FLOAT3 ),
		D3DVSD_REG( 2, D3DVSDT_FLOAT2 ),
		D3DVSD_REG( 3, D3DVSDT_FLOAT2 ),
		D3DVSD_REG( 4, D3DVSDT_FLOAT3 ),
		D3DVSD_END()
	};

	DWORD dwVertexDeclEnv[] =
	{
		D3DVSD_STREAM( 0 ),
		D3DVSD_REG( 0, D3DVSDT_FLOAT3 ),
		D3DVSD_REG( 1, D3DVSDT_FLOAT3 ),
		D3DVSD_REG( 2, D3DVSDT_D3DCOLOR ),
		D3DVSD_END()
	};

	if ( !CreateVertexShader( Sys_RemapPath( "base\\media\\dlight.xvu" ), dwVertexDecl, &m_dwVertexShaderLight ) )
	{
		XBLog_Write( "JA: LightEffects init failed: dlight.xvu" );
		return false;
	}

	if ( !CreateVertexShader( Sys_RemapPath( "base\\media\\specular_dynamic.xvu" ), dwVertexDecl, &m_dwVertexShaderSpecular_Dynamic ) )
	{
		XBLog_Write( "JA: LightEffects optional specular_dynamic.xvu unavailable" );
	}
	if ( !CreateVertexShader( Sys_RemapPath( "base\\media\\specular_static.xvu" ), dwVertexDecl, &m_dwVertexShaderSpecular_Static ) )
	{
		XBLog_Write( "JA: LightEffects optional specular_static.xvu unavailable" );
	}
	if ( !CreateVertexShader( Sys_RemapPath( "base\\media\\bump.xvu" ), dwVertexDeclBump, &m_dwVertexShaderBump ) )
	{
		XBLog_Write( "JA: LightEffects optional bump.xvu unavailable" );
	}
	if ( !CreateVertexShader( Sys_RemapPath( "base\\media\\environment.xvu" ), dwVertexDeclEnv, &m_dwVertexShaderEnvironment ) )
	{
		XBLog_Write( "JA: LightEffects optional environment.xvu unavailable" );
	}

	if ( !CreatePixelShader( Sys_RemapPath( "base\\media\\dlight.xpu" ), &m_dwPixelShaderLight ) )
	{
		XBLog_Write( "JA: LightEffects init failed: dlight.xpu" );
		return false;
	}

	if ( !CreatePixelShader( Sys_RemapPath( "base\\media\\specular_dynamic.xpu" ), &m_dwPixelShaderSpecular_Dynamic ) )
	{
		XBLog_Write( "JA: LightEffects optional specular_dynamic.xpu unavailable" );
	}
	if ( !CreatePixelShader( Sys_RemapPath( "base\\media\\specular_static.xpu" ), &m_dwPixelShaderSpecular_Static ) )
	{
		XBLog_Write( "JA: LightEffects optional specular_static.xpu unavailable" );
	}
	if ( !CreatePixelShader( Sys_RemapPath( "base\\media\\bump.xpu" ), &m_dwPixelShaderBump ) )
	{
		XBLog_Write( "JA: LightEffects optional bump.xpu unavailable" );
	}

	if ( !CreateFlatBumpMap( &m_pBumpMap ) )
	{
		XBLog_Write( "JA: LightEffects init failed: flat bump map" );
		return false;
	}

	UINT width = 32;
	UINT height = 32;
	UINT depth = 32;
	D3DFORMAT format = D3DFMT_A8;
	if ( FAILED( glw_state->device->CreateVolumeTexture( width, height, depth, 1, 0, format, D3DPOOL_DEFAULT, &m_pFalloffMap ) ) )
	{
		XBLog_Write( "JA: LightEffects init failed: falloff volume" );
		return false;
	}

	D3DVOLUME_DESC desc;
	D3DLOCKED_BOX lock;
	m_pFalloffMap->GetLevelDesc( 0, &desc );
	m_pFalloffMap->LockBox( 0, &lock, 0, 0L );
	BYTE *pBits = (BYTE *)lock.pBits;

	for ( UINT w = 0; w < width; ++w )
	{
		for ( UINT v = 0; v < height; ++v )
		{
			for ( UINT u = 0; u < depth; ++u )
			{
				FLOAT x = ( 2.0f * u ) / ( width - 1 ) - 1.0f;
				FLOAT y = ( 2.0f * v ) / ( height - 1 ) - 1.0f;
				FLOAT z = ( 2.0f * w ) / ( depth - 1 ) - 1.0f;

				FLOAT distance = (float)( x * x + y * y + z * z );
				if ( distance == 0.0f )
				{
					*pBits++ = (BYTE)255;
				}
				else
				{
					FLOAT falloff = min( 1.0f, max( 0.0f, ( ( 1.0f / 2.0f ) / distance - 1.0f / 2.0f ) / 1.0f ) );
					*pBits++ = (BYTE)( 255 * falloff );
				}
			}
		}
	}

	DWORD dwPixelSize = XGBytesPerPixelFromFormat( desc.Format );
	DWORD dwTextureSize = desc.Width * desc.Height * desc.Depth * dwPixelSize;
	BYTE *pSrcBits = new BYTE[dwTextureSize];
	memcpy( pSrcBits, lock.pBits, dwTextureSize );
	XGSwizzleBox( pSrcBits, 0, 0, NULL, lock.pBits, desc.Width, desc.Height, desc.Depth, NULL, dwPixelSize );
	delete [] pSrcBits;
	m_pFalloffMap->UnlockBox( 0 );

	if ( !CreateNormalizationCubeMap( 64, &m_pCubeMap ) )
	{
		XBLog_Write( "JA: LightEffects init failed: normalization cube" );
		return false;
	}

	m_bInitialized = true;
	XBLog_Write( "JA: LightEffects dynamic-light path initialized" );
	return true;
}

bool LightEffects::RenderDynamicLights()
{
	VVdlight_t *dl;
	vec3_t origin;
	byte clipBits[SHADER_MAX_VERTEXES];
	glIndex_t hitIndexes[SHADER_MAX_INDEXES];
	int numIndexes;
	float radius;
	int fogging = 0;
	vec3_t dist;
	vec3_t e1;
	vec3_t e2;
	vec3_t normal;
	float fac;
	bool baseTextureReady = true;
	bool disableFogForDlights = false;
	bool useBatchedLightStream = false;
	bool batchedLightStreamReady = false;
	int activeDlightCount = 0;
	LPDIRECT3DTEXTURE8 defaultBumpTexture = NULL;

	if ( !VVLightMan.num_dlights )
	{
		return true;
	}

	if ( !m_bInitialized || !glw_state || !glw_state->device )
	{
		return false;
	}

	defaultBumpTexture = LightEffects_GetDefaultBumpTexture();

	LE_SetRenderState( D3DRS_LIGHTING, FALSE );

	LE_SetTextureStageState( 1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP );
	LE_SetTextureStageState( 1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP );
	LE_SetTextureStageState( 1, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR );

	LE_SetTextureStageState( 2, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP );
	LE_SetTextureStageState( 2, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP );
	LE_SetTextureStageState( 2, D3DTSS_ADDRESSW, D3DTADDRESS_CLAMP );
	LE_SetTextureStageState( 2, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );

	LE_SetTextureStageState( 3, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP );
	LE_SetTextureStageState( 3, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP );
	LE_SetTextureStageState( 3, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 3, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 3, D3DTSS_MIPFILTER, D3DTEXF_POINT );

	LE_SetTextureStageState( 2, D3DTSS_ALPHAKILL, D3DTALPHAKILL_ENABLE );

	if ( tess.shader->isBumpMap )
	{
		bool boundBump = false;
		for ( int stage = 0; stage < tess.shader->numUnfoggedPasses; ++stage )
		{
			shaderStage_t *pStage = &tess.xstages[stage];
			if ( pStage->isBumpMap && pStage->bundle[1].image )
			{
				glwstate_t::texturexlat_t::iterator it = glw_state->textureXlat.find( pStage->bundle[1].image->texnum );
				if ( it != glw_state->textureXlat.end() )
				{
					LE_SetTexture( 1, it->second.mipmap );
					boundBump = true;
				}
			}
		}
		if ( !boundBump )
		{
			LE_SetTexture( 1, defaultBumpTexture ? defaultBumpTexture : m_pBumpMap );
		}
	}
	else
	{
		LE_SetTexture( 1, defaultBumpTexture ? defaultBumpTexture : m_pBumpMap );
	}

	LE_SetTexture( 2, m_pFalloffMap );
	LE_SetTexture( 3, m_pCubeMap );

	LE_SetPixelShader( m_dwPixelShaderLight );
	LE_SetVertexShader( m_dwVertexShaderLight );
	LightEffects_SetSurfaceConstants();
	LightEffects_BindBaseTextureForDlights();
	{
		glwstate_t::texturexlat_t::iterator inf = glw_state->textureXlat.find( glw_state->currentTexture[0] );
		if ( inf != glw_state->textureXlat.end() )
		{
			LE_SetTexture( 0, inf->second.mipmap );
		}
		else
		{
			baseTextureReady = false;
		}
	}

	if ( baseTextureReady )
	{
		for ( int bits = tess.dlightBits; bits && activeDlightCount < 2; bits >>= 1 )
		{
			activeDlightCount += ( bits & 1 );
		}
		useBatchedLightStream = ( activeDlightCount > 1 );

		disableFogForDlights =
			( r_drawfog->value == 2 &&
			tr.world &&
			( tess.fogNum == tr.world->globalFog || tess.fogNum == tr.world->numfogs ) );
		if ( disableFogForDlights )
		{
			fogging = qglIsEnabled( GL_FOG );
			if ( fogging )
			{
				qglDisable( GL_FOG );
			}
		}

		for ( int l = 0, activeDlightBits = tess.dlightBits;
			activeDlightBits && l < VVLightMan.num_dlights;
			++l, activeDlightBits >>= 1 )
		{
			if ( !( activeDlightBits & 1 ) )
			{
				continue;
			}

			dl = &VVLightMan.dlights[l];
			VectorCopy( dl->transformed, origin );
			radius = dl->radius;

			if ( !backEnd.currentEntity->e.ghoul2 )
			{
				int clipall = 63;
				for ( int i = 0; i < tess.numVertexes; ++i )
				{
					int clip = 0;
					VectorSubtract( origin, tess.xyz[i], dist );

					if ( dist[0] < -radius )
					{
						clip |= 1;
					}
					else if ( dist[0] > radius )
					{
						clip |= 2;
					}
					if ( dist[1] < -radius )
					{
						clip |= 4;
					}
					else if ( dist[1] > radius )
					{
						clip |= 8;
					}
					if ( dist[2] < -radius )
					{
						clip |= 16;
					}
					else if ( dist[2] > radius )
					{
						clip |= 32;
					}

					clipBits[i] = (byte)clip;
					clipall &= clip;
				}
				if ( clipall )
				{
					continue;
				}

				numIndexes = 0;
				for ( int i = 0; i < tess.numIndexes; i += 3 )
				{
					int a = tess.indexes[i];
					int b = tess.indexes[i + 1];
					int c = tess.indexes[i + 2];
					if ( clipBits[a] & clipBits[b] & clipBits[c] )
					{
						continue;
					}

					VectorSubtract( tess.xyz[a], tess.xyz[b], e1 );
					VectorSubtract( tess.xyz[c], tess.xyz[b], e2 );
					CrossProduct( e1, e2, normal );

					VectorNormalize( normal );
					fac = DotProduct( normal, origin ) - DotProduct( normal, tess.xyz[a] );
					if ( fac >= radius )
					{
						continue;
					}

					hitIndexes[numIndexes] = tess.indexes[i];
					hitIndexes[numIndexes + 1] = tess.indexes[i + 1];
					hitIndexes[numIndexes + 2] = tess.indexes[i + 2];
					numIndexes += 3;

					if ( numIndexes >= SHADER_MAX_VERTEXES - 3 )
					{
						break;
					}
				}

				if ( !numIndexes )
				{
					continue;
				}
			}

			D3DXVECTOR4 vecLightRange( 1.0f / dl->radius, 0.0f, 0.0f, 0.0f );
			LE_SetVertexShaderConstant( CV_ONE_OVER_LIGHT_RANGE, (void *)&vecLightRange.x, 1 );
			LE_SetPixelShaderConstant( CP_DIFFUSE_COLOR, &dl->color[0], 1 );
			LightEffects_SetLightPositionConstant( (D3DXVECTOR3 *)&dl->transformed );

			if ( backEnd.currentEntity->e.ghoul2 )
			{
				if ( useBatchedLightStream )
				{
					if ( !batchedLightStreamReady )
					{
						renderObject_LightBeginSurface();
						batchedLightStreamReady = true;
					}
					renderObject_LightDrawIndexes( tess.numIndexes, tess.indexes );
				}
				else
				{
					renderObject_Light( tess.numIndexes, tess.indexes );
				}
			}
			else
			{
				if ( useBatchedLightStream )
				{
					if ( !batchedLightStreamReady )
					{
						renderObject_LightBeginSurface();
						batchedLightStreamReady = true;
					}
					renderObject_LightDrawIndexes( numIndexes, hitIndexes );
				}
				else
				{
					renderObject_Light( numIndexes, hitIndexes );
				}
			}

		}

		if ( fogging )
		{
			qglEnable( GL_FOG );
		}
	}

	LE_SetPixelShader( 0 );
	glState.currenttextures[0] = -2;
	glState.currenttextures[1] = -2;
	glw_state->currentTexture[0] = -2;
	glw_state->currentTexture[1] = -2;

	LE_SetTextureStageState( 2, D3DTSS_ALPHAKILL, D3DTALPHAKILL_DISABLE );
	LE_SetTexture( 2, NULL );
	LE_SetTexture( 3, NULL );
#ifdef _XBOX
	JAMP_InvalidateD3DStateAfterLightEffects();
#endif

	return true;
}

bool LightEffects::RenderStaticLights()
{
	return false;
}

bool LightEffects::RenderSpecular()
{
	LPDIRECT3DTEXTURE8 baseTexture = NULL;
	LPDIRECT3DTEXTURE8 specularTexture = NULL;

	if ( !glw_state || !glw_state->device ||
		!m_dwVertexShaderSpecular_Static || !m_dwPixelShaderSpecular_Static )
	{
		return false;
	}

	specularTexture = LightEffects_GetSpecularTexture();
	if ( !specularTexture )
	{
		return false;
	}

	glwstate_t::texturexlat_t::iterator it = glw_state->textureXlat.find( glw_state->currentTexture[0] );
	if ( it == glw_state->textureXlat.end() )
	{
		return false;
	}
	baseTexture = it->second.mipmap;

	LE_SetRenderState( D3DRS_LIGHTING, FALSE );
	LE_SetRenderState( D3DRS_FOGENABLE, FALSE );
	LE_SetTexture( 0, baseTexture );
	LE_SetTexture( 2, specularTexture );
	LE_SetVertexShaderConstant( CV_CAMERA_DIRECTION, D3DXVECTOR4( tr.viewParms.or.axis[0][0],
		tr.viewParms.or.axis[0][1],
		tr.viewParms.or.axis[0][2],
		1.0f ), 1 );

	RenderSpecular_Static();
	if ( tess.dlightBits )
	{
		RenderSpecular_Dynamic();
	}

	LE_SetPixelShader( 0 );
	LE_SetTexture( 2, NULL );
	glState.currenttextures[0] = -2;
	glState.currenttextures[1] = -2;
	glw_state->currentTexture[0] = -2;
	glw_state->currentTexture[1] = -2;
#ifdef _XBOX
	JAMP_InvalidateD3DStateAfterLightEffects();
#endif
	return true;
}

bool LightEffects::RenderSpecular_Dynamic()
{
	VVdlight_t *dl;

	if ( !m_dwVertexShaderSpecular_Dynamic || !m_dwPixelShaderSpecular_Dynamic )
	{
		return false;
	}

	LE_SetTextureStageState( 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP );
	LE_SetTextureStageState( 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP );
	LE_SetTextureStageState( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 2, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP );
	LE_SetTextureStageState( 2, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP );
	LE_SetTextureStageState( 2, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 2, D3DTSS_MIPFILTER, D3DTEXF_POINT );
	LE_SetTextureStageState( 3, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP );
	LE_SetTextureStageState( 3, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP );
	LE_SetTextureStageState( 3, D3DTSS_ADDRESSW, D3DTADDRESS_CLAMP );
	LE_SetTextureStageState( 3, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 3, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
	LE_SetTexture( 3, m_pFalloffMap );
	LE_SetPixelShader( m_dwPixelShaderSpecular_Dynamic );
	LE_SetVertexShader( m_dwVertexShaderSpecular_Dynamic );

	for ( int i = 0; i < VVLightMan.num_dlights; ++i )
	{
		if ( !( tess.dlightBits & ( 1 << i ) ) )
		{
			continue;
		}

		dl = &VVLightMan.dlights[i];
		D3DXVECTOR4 vecLightRange( 1.0f / dl->radius, 0.0f, 0.0f, 0.0f );
		LE_SetVertexShaderConstant( CV_ONE_OVER_LIGHT_RANGE, (void *)&vecLightRange.x, 1 );
		LightEffects_SetLightPositionConstant( (D3DXVECTOR3 *)&dl->transformed );
		renderObject_Light( tess.numIndexes, tess.indexes );
	}

	LE_SetTexture( 3, NULL );
	return true;
}

bool LightEffects::RenderSpecular_Static()
{
	if ( !m_dwVertexShaderSpecular_Static || !m_dwPixelShaderSpecular_Static )
	{
		return false;
	}

	LE_SetTextureStageState( 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP );
	LE_SetTextureStageState( 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP );
	LE_SetTextureStageState( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 2, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP );
	LE_SetTextureStageState( 2, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP );
	LE_SetTextureStageState( 2, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
	LE_SetTextureStageState( 2, D3DTSS_MIPFILTER, D3DTEXF_NONE );
	LE_SetPixelShader( m_dwPixelShaderSpecular_Static );
	LE_SetVertexShader( m_dwVertexShaderSpecular_Static );
	LightEffects_SetSurfaceConstants();

	D3DXVECTOR4 vLight;
	if ( backEnd.currentEntity && ( backEnd.currentEntity->e.hModel || backEnd.currentEntity->e.ghoul2 ) )
	{
		vLight.x = backEnd.currentEntity->lightDir[0];
		vLight.y = backEnd.currentEntity->lightDir[1];
		vLight.z = backEnd.currentEntity->lightDir[2];
	}
	else
	{
		vLight.x = -960.0f;
		vLight.y = 1920.0f;
		vLight.z = 96.0f;
	}
	vLight.w = 1.0f;

	LE_SetVertexShaderConstant( CV_LIGHT_DIRECTION, vLight, 1 );
	renderObject_Light( tess.numIndexes, tess.indexes );
	return true;
}

bool LightEffects::RenderEnvironment()
{
	if ( !glw_state || !glw_state->device || !m_dwVertexShaderEnvironment )
	{
		return false;
	}

	LE_SetRenderState( D3DRS_LIGHTING, FALSE );
	LE_SetVertexShaderConstant( CV_CAMERA_DIRECTION, D3DXVECTOR4( backEnd.ori.viewOrigin[0],
		backEnd.ori.viewOrigin[1],
		backEnd.ori.viewOrigin[2],
		1.0f ), 1 );
	LightEffects_SetSurfaceConstants();

	XGMATRIX *view = (XGMATRIX *)glw_state->matrixStack[glw_state->MatrixMode_Model]->GetTop();
	XGMATRIX viewtran;
	XGMatrixTranspose( &viewtran, view );
	LE_SetVertexShaderConstant( CV_VIEW_0, viewtran, 4 );
	LE_SetVertexShader( m_dwVertexShaderEnvironment );
	renderObject_Env();
#ifdef _XBOX
	JAMP_InvalidateD3DStateAfterLightEffects();
#endif
	return true;
}

bool LightEffects::RenderBump()
{
	glwstate_t::texturexlat_t::iterator baseIt;
	glwstate_t::texturexlat_t::iterator bumpIt;

	if ( !glw_state || !glw_state->device || !m_dwVertexShaderBump || !m_dwPixelShaderBump )
	{
		return false;
	}

	baseIt = glw_state->textureXlat.find( glw_state->currentTexture[0] );
	bumpIt = glw_state->textureXlat.find( glw_state->currentTexture[1] );
	if ( baseIt == glw_state->textureXlat.end() || bumpIt == glw_state->textureXlat.end() )
	{
		return false;
	}

	LE_SetRenderState( D3DRS_LIGHTING, FALSE );
	LE_SetRenderState( D3DRS_FOGENABLE, FALSE );
	LE_SetTexture( 0, baseIt->second.mipmap );
	LE_SetTexture( 1, bumpIt->second.mipmap );
	LE_SetTextureStageState( 0, D3DTSS_MAXANISOTROPY, bumpIt->second.anisotropy );
	LE_SetTextureStageState( 0, D3DTSS_MINFILTER, bumpIt->second.minFilter );
	LE_SetTextureStageState( 0, D3DTSS_MIPFILTER, bumpIt->second.mipFilter );
	LE_SetTextureStageState( 0, D3DTSS_MAGFILTER, bumpIt->second.magFilter );
	LE_SetTextureStageState( 0, D3DTSS_ADDRESSU, bumpIt->second.wrapU );
	LE_SetTextureStageState( 0, D3DTSS_ADDRESSV, bumpIt->second.wrapV );
	LE_SetTextureStageState( 1, D3DTSS_MAXANISOTROPY, bumpIt->second.anisotropy );
	LE_SetTextureStageState( 1, D3DTSS_MINFILTER, bumpIt->second.minFilter );
	LE_SetTextureStageState( 1, D3DTSS_MIPFILTER, bumpIt->second.mipFilter );
	LE_SetTextureStageState( 1, D3DTSS_MAGFILTER, bumpIt->second.magFilter );
	LE_SetTextureStageState( 1, D3DTSS_ADDRESSU, bumpIt->second.wrapU );
	LE_SetTextureStageState( 1, D3DTSS_ADDRESSV, bumpIt->second.wrapV );
	LE_SetRenderState( D3DRS_SPECULARENABLE, TRUE );
	LE_SetPixelShader( m_dwPixelShaderBump );
	LE_SetVertexShader( m_dwVertexShaderBump );
	LightEffects_SetSurfaceConstants();

	D3DXVECTOR4 vAmbient;
	D3DXVECTOR4 vDiffuse;
	D3DXVECTOR4 vLightDir;

	if ( backEnd.currentEntity && ( backEnd.currentEntity->e.hModel || backEnd.currentEntity->e.ghoul2 ) )
	{
		if ( tess.shader->stages[tess.currentPass].rgbGen == CGEN_LIGHTING_DIFFUSE_ENTITY )
		{
			vAmbient.x = ( backEnd.currentEntity->ambientLight[0] / 255.0f ) * ( backEnd.currentEntity->e.shaderRGBA[0] / 255.0f );
			vAmbient.y = ( backEnd.currentEntity->ambientLight[1] / 255.0f ) * ( backEnd.currentEntity->e.shaderRGBA[1] / 255.0f );
			vAmbient.z = ( backEnd.currentEntity->ambientLight[2] / 255.0f ) * ( backEnd.currentEntity->e.shaderRGBA[2] / 255.0f );
		}
		else
		{
			vAmbient.x = backEnd.currentEntity->ambientLight[0] / 255.0f;
			vAmbient.y = backEnd.currentEntity->ambientLight[1] / 255.0f;
			vAmbient.z = backEnd.currentEntity->ambientLight[2] / 255.0f;
		}

		vDiffuse.x = backEnd.currentEntity->directedLight[0] / 255.0f;
		vDiffuse.y = backEnd.currentEntity->directedLight[1] / 255.0f;
		vDiffuse.z = backEnd.currentEntity->directedLight[2] / 255.0f;
		vLightDir.x = DotProduct( backEnd.currentEntity->lightDir, backEnd.currentEntity->e.axis[0] );
		vLightDir.y = DotProduct( backEnd.currentEntity->lightDir, backEnd.currentEntity->e.axis[1] );
		vLightDir.z = DotProduct( backEnd.currentEntity->lightDir, backEnd.currentEntity->e.axis[2] );
	}
	else
	{
		vec3_t sundir;
		sundir[0] = r_sundir_x->value;
		sundir[1] = r_sundir_y->value;
		sundir[2] = r_sundir_z->value;
		VectorNormalize( sundir );

		vLightDir.x = sundir[0];
		vLightDir.y = sundir[1];
		vLightDir.z = sundir[2];
		vAmbient.x = tr.sunAmbient[0] / 1.5f;
		vAmbient.y = tr.sunAmbient[1] / 1.5f;
		vAmbient.z = tr.sunAmbient[2] / 1.5f;
		vDiffuse.x = 1.0f;
		vDiffuse.y = 1.0f;
		vDiffuse.z = 1.0f;
	}
	vAmbient.w = 1.0f;
	vDiffuse.w = 1.0f;
	vLightDir.w = 1.0f;

	LE_SetPixelShaderConstant( CP_AMBIENT_COLOR, vAmbient, 1 );
	LE_SetPixelShaderConstant( CP_DIFFUSE_COLOR, vDiffuse, 1 );
	LE_SetVertexShaderConstant( CV_LIGHT_DIRECTION, vLightDir, 1 );
	LE_SetVertexShaderConstant( CV_CAMERA_DIRECTION, D3DXVECTOR4( tr.viewParms.or.axis[0][0],
		tr.viewParms.or.axis[0][1],
		tr.viewParms.or.axis[0][2],
		1.0f ), 1 );
	renderObject_Bump();
	LE_SetPixelShader( 0 );
	LE_SetRenderState( D3DRS_SPECULARENABLE, FALSE );
#ifdef _XBOX
	JAMP_InvalidateD3DStateAfterLightEffects();
#endif
	return true;
}

void LightEffects::ProcessVertices( D3DXVECTOR3* pDirLightDir, D3DXVECTOR3* pPtLightPos )
{
	(void)pDirLightDir;

	LightEffects_SetSurfaceConstants();
	LightEffects_SetLightPositionConstant( pPtLightPos );
}

static inline D3DCOLOR VectorToRGBA( const D3DXVECTOR3* v, FLOAT fHeight = 1.0f )
{
	D3DCOLOR r = (D3DCOLOR)( ( v->x + 1.0f ) * 127.5f );
	D3DCOLOR g = (D3DCOLOR)( ( v->y + 1.0f ) * 127.5f );
	D3DCOLOR b = (D3DCOLOR)( ( v->z + 1.0f ) * 127.5f );
	D3DCOLOR a = (D3DCOLOR)( 255.0f * fHeight );
	return ( ( a << 24L ) + ( r << 16L ) + ( g << 8L ) + ( b << 0L ) );
}

bool LightEffects::CreateNormalizationCubeMap( DWORD dwSize, LPDIRECT3DCUBETEXTURE8* ppCubeMap )
{
	if ( FAILED( glw_state->device->CreateCubeTexture( dwSize, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, ppCubeMap ) ) )
	{
		return false;
	}

	DWORD *pSourceBits = new DWORD[dwSize * dwSize];
	for ( DWORD i = 0; i < 6; ++i )
	{
		LPDIRECT3DSURFACE8 pCubeMapFace;
		(*ppCubeMap)->GetCubeMapSurface( (D3DCUBEMAP_FACES)i, 0, &pCubeMapFace );

		DWORD *pPixel = pSourceBits;
		D3DXVECTOR3 n;
		FLOAT w;
		FLOAT h;

		for ( DWORD y = 0; y < dwSize; ++y )
		{
			h = (FLOAT)y / (FLOAT)( dwSize - 1 );
			h = ( h * 2.0f ) - 1.0f;

			for ( DWORD x = 0; x < dwSize; ++x )
			{
				w = (FLOAT)x / (FLOAT)( dwSize - 1 );
				w = ( w * 2.0f ) - 1.0f;

				switch ( i )
				{
				case D3DCUBEMAP_FACE_POSITIVE_X:
					n.x = +1.0f;
					n.y = -h;
					n.z = -w;
					break;
				case D3DCUBEMAP_FACE_NEGATIVE_X:
					n.x = -1.0f;
					n.y = -h;
					n.z = +w;
					break;
				case D3DCUBEMAP_FACE_POSITIVE_Y:
					n.x = +w;
					n.y = +1.0f;
					n.z = +h;
					break;
				case D3DCUBEMAP_FACE_NEGATIVE_Y:
					n.x = +w;
					n.y = -1.0f;
					n.z = -h;
					break;
				case D3DCUBEMAP_FACE_POSITIVE_Z:
					n.x = +w;
					n.y = -h;
					n.z = +1.0f;
					break;
				case D3DCUBEMAP_FACE_NEGATIVE_Z:
				default:
					n.x = -w;
					n.y = -h;
					n.z = -1.0f;
					break;
				}

				D3DXVec3Normalize( &n, &n );
				*pPixel++ = VectorToRGBA( &n );
			}
		}

		D3DLOCKED_RECT lock;
		pCubeMapFace->LockRect( &lock, 0, 0L );
		XGSwizzleRect( pSourceBits, 0, NULL, lock.pBits, dwSize, dwSize, NULL, sizeof( DWORD ) );
		pCubeMapFace->UnlockRect();
		pCubeMapFace->Release();
	}

	delete [] pSourceBits;
	return true;
}

#endif // VV_LIGHTING
